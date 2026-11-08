#ifndef MARCHINGSQUARES_HPP
#define MARCHINGSQUARES_HPP

#include "Execution/ExecutionTBB.hpp"
#include <ostream>
#include "libslic3r/Execution/Execution.hpp"
#include <array>
#include <iterator>
#include <type_traits>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iomanip>

namespace marchsq {

struct Coord
{
    long r = 0, c = 0;

    Coord() = default;
    explicit Coord(long s) : r(s), c(s) {}
    Coord(long _r, long _c) : r(_r), c(_c) {}

    size_t seq(const Coord& res) const { return r * res.c + c; }
    Coord& operator+=(const Coord& b)
    {
        r += b.r;
        c += b.c;
        return *this;
    }
    Coord operator+(const Coord& b) const
    {
        Coord a = *this;
        a += b;
        return a;
    }
    bool operator==(const Coord& o) const { return (r == o.r) && (c == o.c); }
    bool operator!=(const Coord& o) const { return !(*this == o); }
};

inline std::ostream& operator<<(std::ostream& os, const Coord& o) { return os << "(r=" << o.r << ",c=" << o.c << ")"; }

using Ring = std::vector<Coord>;

inline std::ostream& operator<<(std::ostream& os, const Ring& r)
{
    os << "[" << r.size() << "]:";
    for (Coord c : r)
        os << " " << c;
    return os << "\n";
}

template<class T, class Enable = void> struct _RasterTraits
{
    using ValueType = typename T::ValueType;

    static ValueType get(const T& raster, size_t row, size_t col);

    static size_t rows(const T& raster);
    static size_t cols(const T& raster);
};

template<class ExecutionPolicy, class Enable = void> struct _Loop
{
    template<class It, class Fn> static void for_each_idx(It from, It to, Fn&& fn)
    {
        for (auto it = from; it < to; ++it)
            fn(*it, size_t(it - from));
    }
};

template<> struct _Loop<Slic3r::ExecutionTBB>
{
    template<class It, class Fn> static void for_each_idx(It from, It to, Fn&& fn)
    {
        Slic3r::execution::for_each(
            Slic3r::ex_tbb, size_t(0), size_t(to - from), [&from, &fn](size_t i) { fn(from[i], i); }, Slic3r::execution::max_concurrency(Slic3r::ex_tbb));
    }
};

namespace __impl {

template<class T> using RasterTraits = _RasterTraits<std::decay_t<T>>;
template<class T> using TRasterValue = typename RasterTraits<T>::ValueType;

template<class T> size_t rows(const T& raster) { return RasterTraits<T>::rows(raster); }

template<class T> size_t cols(const T& raster) { return RasterTraits<T>::cols(raster); }

template<class T> TRasterValue<T> isoval(const T& rst, const Coord& crd) { return RasterTraits<T>::get(rst, crd.r, crd.c); }

template<class ExecutionPolicy, class It, class Fn> void for_each_idx(ExecutionPolicy&& policy, It from, It to, Fn&& fn)
{
    _Loop<ExecutionPolicy>::for_each_idx(from, to, fn);
}

template<class E> constexpr std::underlying_type_t<E> _t(E e) noexcept { return static_cast<std::underlying_type_t<E>>(e); }

enum class Dir : uint8_t {
    none      = 0b0000,
    left      = 0b0001,
    down      = 0b0010,
    right     = 0b0100,
    leftright = left | right,
    up        = 0b1000,
    updown    = up | down,
    all       = 0b1111
};

inline std::ostream& operator<<(std::ostream& os, const Dir& d) { return os << ".<v#>x##^#X#####"[_t(d)]; }

static const constexpr std::array<uint32_t, 16> NEXT_CCW = [] {
    auto map = decltype(NEXT_CCW){};
    for (uint32_t cbda = 0; cbda < 16; cbda++) {
        const uint32_t dcba = (cbda & 0b0001) | ((cbda >> 1) & 0b0110) | ((cbda << 2) & 0b1000);
        const uint32_t adcb = (dcba >> 1) | ((dcba << 3) & 0b1000);
        map[cbda]           = ~dcba & adcb;
    }
    return map;
}();

inline void step(Coord& crd, const Dir d, const long n = 1)
{
    switch (d) {
    case Dir::left: crd.c -= n; break;
    case Dir::down: crd.r += n; break;
    case Dir::right: crd.c += n; break;
    case Dir::up: crd.r -= n; break;
    }
}

template<class Rst> class Grid
{
    const Rst*            m_rst = nullptr;
    Coord                 m_window, m_rastsize, m_gridsize;
    size_t                m_gridlen;
    std::vector<uint32_t> m_tags;
    std::vector<uint32_t> m_dirs;
    inline Coord rastercoord(const Coord& crd) const
    {
        return {(crd.r - 1) * m_window.r, (crd.c - 1) * m_window.c};
    }

    Coord bl(const Coord& crd) const { return tl(crd) + Coord{m_window.r, 0}; }
    Coord br(const Coord& crd) const { return tl(crd) + Coord{m_window.r, m_window.c}; }
    Coord tr(const Coord& crd) const { return tl(crd) + Coord{0, m_window.c}; }
    Coord tl(const Coord& crd) const { return rastercoord(crd); }

    inline bool is_within(const Coord& crd) const { return crd.r >= 0 && crd.r < m_rastsize.r && crd.c >= 0 && crd.c < m_rastsize.c; }

    uint32_t get_tags_block32(const size_t bidx, const TRasterValue<Rst> v) const
    {
        Coord    gcrd = coord(bidx * 32);
        Coord    rcrd = rastercoord(gcrd);
        uint32_t tags = 0;
        for (uint32_t b = 1; b; b <<= 1) {
            if (is_within(rcrd) && isoval(*m_rst, rcrd) > v)
                tags |= b;
            gcrd.c += 1;
            rcrd.c += m_window.c;
            if (gcrd.c >= m_gridsize.c) {
                gcrd = Coord(gcrd.r + 1, 0);
                rcrd = rastercoord(gcrd);
            }
        }
        return tags;
    }

    uint32_t get_dirs_block8(const size_t bidx) const
    {
        size_t   gidx = bidx * 8;
        uint32_t dirs = 0;

        uint32_t tags9 = get_tags9(gidx) | (get_tags9(gidx + m_gridsize.c) << 16);
        if ((tags9 != 0) && (tags9 != 0x01ff01ff)) {
            for (auto s = 0; s < 32; s += 4) {
                uint8_t tags = (tags9 & 0b11) | ((tags9 >> 14) & 0b1100);
                dirs |= NEXT_CCW[tags] << s;
                tags9 >>= 1;
            }
        }
        return dirs;
    }

    uint32_t get_tags9(size_t gidx) const
    {
        uint32_t tags = 0;
        size_t   i    = gidx / 32;
        int      o    = gidx % 32;

        if (gidx < m_gridlen) {
            tags = (m_tags[i] >> o) & 0x1ff;
            if ((o > (32 - 9)) && ((i + 1) < m_tags.size())) {
                tags |= (m_tags[i + 1] << (32 - o)) & 0x1ff;
            }
        }
        return tags;
    }

    void clr_dirs(const size_t gidx, const Dir d)
    {
        assert(gidx < m_gridlen);
        size_t i = gidx / 8;
        int    o = (gidx % 8);

        m_dirs[i] &= ~(static_cast<uint32_t>(d) << (o * 4));
    }

    Dir get_dirs(const size_t gidx) const
    {
        assert(gidx < m_gridlen);
        size_t i = gidx / 8;
        int    o = (gidx % 8);

        return Dir((m_dirs[i] >> (o * 4)) & 0b1111);
    }

    Coord coord(size_t i) const { return {long(i) / m_gridsize.c, long(i) % m_gridsize.c}; }

    size_t seq(const Coord& crd) const { return crd.seq(m_gridsize); }

    size_t stepidx(const size_t idx, const Dir d) const
    {
        assert(idx < m_gridlen);
        switch (d) {
        case Dir::left: return idx - 1;
        case Dir::down: return idx + m_gridsize.c;
        case Dir::right: return idx + 1;
        case Dir::up: return idx - m_gridsize.c;
        default: return idx;
        }
    }

    size_t search_start_cell(size_t gidx = 0) const
    {
        size_t i = gidx / 8;
        int    o = gidx % 8;
        while (i < m_dirs.size()) {
            if (!m_dirs[i]) {
                i++;
                o = 0;
            } else {
                for (uint32_t m = 0b1111 << (o * 4); !(m_dirs[i] & m); m <<= 4)
                    o++;
                break;
            }
        }
        return i * 8 + o;
    }

    Dir next_dir(size_t idx, Dir prev = Dir::all) const
    {
        Dir next = get_dirs(idx);

        switch (next) {
        case Dir::leftright:
            assert(prev == Dir::up || prev == Dir::down || prev == Dir::all);
            return (prev == Dir::up) ? Dir::left : Dir::right;
        case Dir::updown:
            assert(prev == Dir::left || prev == Dir::right || prev == Dir::all);
            return (prev == Dir::right) ? Dir::up : Dir::down;
        default:
            assert(next == Dir::none || next == Dir::left || next == Dir::down || next == Dir::right || next == Dir::up);
            return next;
        }
    }

    struct CellIt
    {
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = TRasterValue<Rst>;
        using pointer           = TRasterValue<Rst>*;
        using reference         = TRasterValue<Rst>&;
        using difference_type   = long;

        Coord      crd;
        Dir        dir  = Dir::none;
        const Rst* grid = nullptr;

        inline TRasterValue<Rst>  operator*() const { return isoval(*grid, crd); }
        inline TRasterValue<Rst>& operator[](long n) const { return *((*this) + n); }
        inline CellIt&            operator++()
        {
            step(crd, dir);
            return *this;
        }
        inline CellIt operator++(int)
        {
            CellIt o = *this;
            ++(*this);
            return o;
        }
        inline CellIt& operator--()
        {
            step(crd, dir, -1);
            return *this;
        }
        inline CellIt operator--(int)
        {
            CellIt o = *this;
            --(*this);
            return o;
        }
        inline CellIt& operator+=(long n)
        {
            step(crd, dir, n);
            return *this;
        }
        inline CellIt& operator-=(long n)
        {
            step(crd, dir, -n);
            return *this;
        }
        inline CellIt operator+(long n) const
        {
            CellIt o = *this;
            o += n;
            return o;
        }
        inline CellIt operator-(long n) const
        {
            CellIt o = *this;
            o -= n;
            return o;
        }
        inline long operator-(const CellIt& o) const
        {
            switch (dir) {
            case Dir::left: return o.crd.c - crd.c;
            case Dir::down: return crd.r - o.crd.r;
            case Dir::right: return crd.c - o.crd.c;
            case Dir::up: return o.crd.r - crd.r;
            default: return 0;
            }
        }
        inline bool operator==(const CellIt& o) const { return crd == o.crd; }
        inline bool operator!=(const CellIt& o) const { return crd != o.crd; }
        inline bool operator<(const CellIt& o) const { return (*this - o) < 0; }
        inline bool operator>(const CellIt& o) const { return (*this - o) > 0; }
        inline bool operator<=(const CellIt& o) const { return (*this - o) <= 0; }
        inline bool operator>=(const CellIt& o) const { return (*this - o) >= 0; }
    };

    struct Edge
    {
        CellIt from, to;
    };

    Edge _edge(const Coord& ringvertex) const
    {
        size_t idx  = ringvertex.r;
        Coord  cell = coord(idx);
        Dir    d    = Dir(ringvertex.c);

        switch (d) {
        case Dir::left: return {{tl(cell), Dir::down, m_rst}, {bl(cell)}};
        case Dir::down: return {{bl(cell), Dir::right, m_rst}, {br(cell)}};
        case Dir::right: return {{br(cell), Dir::up, m_rst}, {tr(cell)}};
        case Dir::up: return {{tr(cell), Dir::left, m_rst}, {tl(cell)}};
        default: assert(false);
        }
        return {};
    }

    Edge edge(const Coord& ringvertex) const
    {
        Edge e   = _edge(ringvertex);
        e.to.dir = e.from.dir;
        ++e.to;

        e.from.crd.r = std::clamp(e.from.crd.r, 0l, m_rastsize.r - 1);
        e.from.crd.c = std::clamp(e.from.crd.c, 0l, m_rastsize.c - 1);
        e.to.crd.r   = std::clamp(e.to.crd.r, 0l, m_rastsize.r);
        e.to.crd.c   = std::clamp(e.to.crd.c, 0l, m_rastsize.c);
        return e;
    }

    void interpolate_edge(Coord& ecrd, TRasterValue<Rst> isoval) const
    {
        assert((0 <= ecrd.r) && (ecrd.r < m_gridlen));
        assert((ecrd.c == long(Dir::left)) || (ecrd.c == long(Dir::down)) || (ecrd.c == long(Dir::right)) || (ecrd.c == long(Dir::up)));
        Edge e = edge(ecrd);
        ecrd   = std::lower_bound(e.from, e.to, isoval).crd;
        if (e.from.dir == Dir::up)
            ecrd.r += 1;
        else if (e.from.dir == Dir::left)
            ecrd.c += 1;
    }

public:
    explicit Grid(const Rst& rst, const Coord& window)
        : m_rst{&rst}
        , m_window{window}
        , m_rastsize{static_cast<long>(rows(rst)), static_cast<long>(cols(rst))}
        , m_gridsize{2 + m_rastsize.r / m_window.r, 2 + m_rastsize.c / m_window.c}
        , m_gridlen{static_cast<size_t>(m_gridsize.r * m_gridsize.c)}
        , m_tags(m_gridlen / 32 + 1, 0)
        , m_dirs(m_gridlen / 8 + 1, 0)
    {}

    template<class ExecutionPolicy> void tag_grid(ExecutionPolicy&& policy, TRasterValue<Rst> isoval)
    {
        for_each_idx(std::forward<ExecutionPolicy>(policy), m_tags.begin(), m_tags.end(),
                     [this, isoval](uint32_t& tag_block, size_t bidx) { tag_block = get_tags_block32(bidx, isoval); });
        for_each_idx(std::forward<ExecutionPolicy>(policy), m_dirs.begin(), m_dirs.end(),
                     [this](uint32_t& dirs_block, size_t bidx) { dirs_block = get_dirs_block8(bidx); });
    }

    std::vector<Ring> scan_rings()
    {
        std::vector<Ring> rings;
        size_t            startidx = 0;
        while ((startidx = search_start_cell(startidx)) < m_gridlen) {
            Ring ring;

            size_t idx  = startidx;
            Dir    next = next_dir(idx);
            do {
                assert(next != Dir::none);
                Coord ringvertex{long(idx), long(next)};
                ring.emplace_back(ringvertex);
                clr_dirs(idx, next);
                idx  = stepidx(idx, next);
                next = next_dir(idx, next);
                assert(idx < m_gridlen);
            } while (idx != startidx);
            assert(next == Dir::none || next == Dir::left || next == Dir::up);
            if (ring.size() > 1)
                rings.emplace_back(ring);
        }
        return rings;
    }

    template<class ExecutionPolicy> void interpolate_rings(ExecutionPolicy&& policy, std::vector<Ring>& rings, TRasterValue<Rst> isov)
    {
        for_each_idx(std::forward<ExecutionPolicy>(policy), rings.begin(), rings.end(), [this, isov](Ring& ring, size_t) {
            for (Coord& e : ring)
                interpolate_edge(e, isov);
        });
    }

    std::ostream& streamtags(std::ostream& os)
    {
        os << "   :";
        for (auto c = 0; c < m_gridsize.c; c++)
            os << (c % 10);
        os << "\n";
        for (auto r = 0; r < m_gridsize.r; r++) {
            os << std::setw(3) << r << ":";
            for (auto c = 0; c < m_gridsize.c; c++)
                os << ((get_tags9(seq(Coord(r, c))) & 1) ? "H" : ".");
            os << "\n";
        }
        return os;
    }

    std::ostream& streamdirs(std::ostream& os)
    {
        os << "   :";
        for (auto c = 0; c < m_gridsize.c; c++)
            os << (c % 10);
        os << "\n";
        for (auto r = 0; r < m_gridsize.r; r++) {
            os << std::setw(3) << r << ":";
            for (auto c = 0; c < m_gridsize.c; c++)
                os << get_dirs(seq(Coord(r, c)));
            os << std::endl;
        }
        return os;
    }
};

template<class Raster, class ExecutionPolicy>
std::vector<marchsq::Ring> execute_with_policy(ExecutionPolicy&&    policy,
                                               const Raster&        raster,
                                               TRasterValue<Raster> isoval,
                                               Coord                windowsize = {})
{
    if (!rows(raster) || !cols(raster))
        return {};

    size_t ratio = cols(raster) / rows(raster);

    if (!windowsize.r)
        windowsize.r = 2;
    if (!windowsize.c)
        windowsize.c = std::max(2l, long(windowsize.r * ratio));

    Grid<Raster> grid{raster, windowsize};

    grid.tag_grid(std::forward<ExecutionPolicy>(policy), isoval);
    std::vector<marchsq::Ring> rings = grid.scan_rings();
    grid.interpolate_rings(std::forward<ExecutionPolicy>(policy), rings, isoval);

    return rings;
}

template<class Raster> std::vector<marchsq::Ring> execute(const Raster& raster, TRasterValue<Raster> isoval, Coord windowsize = {})
{
    return execute_with_policy(nullptr, raster, isoval, windowsize);
}

}

using __impl::execute_with_policy;
using __impl::execute;

}

#endif
