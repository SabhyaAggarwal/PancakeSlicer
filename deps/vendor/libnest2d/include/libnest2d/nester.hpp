#ifndef NESTER_HPP
#define NESTER_HPP

#include <memory>
#include <vector>
#include <map>
#include <array>
#include <algorithm>
#include <functional>

#include <libnest2d/geometry_traits.hpp>
#define LARGE_COST_TO_REJECT 1e7

namespace libnest2d {

    static const constexpr int BIN_ID_UNSET = -1;
    static const constexpr int BIN_ID_UNFIT = -1;











template<class RawShape>
class _Item {
    using Coord = TCoord<TPoint<RawShape>>;
    using Vertex = TPoint<RawShape>;
    using Box = _Box<Vertex>;

    using VertexConstIterator = typename TContour<RawShape>::const_iterator;


    RawShape sh_;


    Vertex translation_{0, 0};
    Radians rotation_{0.0};
    Coord inflation_{0};




    bool has_rotation_ = false, has_translation_ = false, has_inflation_ = false;


    mutable RawShape tr_cache_;
    mutable bool tr_cache_valid_ = false;
    mutable double area_cache_ = 0;
    mutable bool area_cache_valid_ = false;
    mutable RawShape inflate_cache_;
    mutable bool inflate_cache_valid_ = false;

    enum class Convexity: char {
        UNCHECKED,
        C_TRUE,
        C_FALSE
    };

    mutable Convexity convexity_ = Convexity::UNCHECKED;
    mutable VertexConstIterator rmt_;
    mutable VertexConstIterator lmb_;
    mutable bool rmt_valid_ = false, lmb_valid_ = false;
    mutable struct BBCache {
        Box bb; bool valid;
        BBCache(): valid(false) {}
    } bb_cache_;

    int binid_{BIN_ID_UNSET}, priority_{0};
    bool fixed_{false};

public:
    int itemid_{ 0 };
    std::vector<int> extrude_ids;
    int filament_temp_type = -1;
    double height{ 0 };
    double print_temp{ 0 };
    double bed_temp{ 0 };
    double vitrify_temp{ 0 };
    std::string name;

    bool is_virt_object{ false };
    bool is_wipe_tower{ false };
    bool has_tried_with_excluded{ false };


    using ShapeType = RawShape;










    using Iterator = VertexConstIterator;









    static BP2D_CONSTEXPR Orientation orientation() {
        return OrientationType<TContour<RawShape>>::Value;
    }






    explicit inline _Item(const RawShape& sh): sh_(sh) {}






    explicit inline _Item(RawShape&& sh): sh_(std::move(sh)) {}





    inline _Item(const std::initializer_list< Vertex >& il):
        sh_(sl::create<RawShape>(il)) {}

    inline _Item(const TContour<RawShape>& contour,
                 const THolesContainer<RawShape>& holes = {}):
        sh_(sl::create<RawShape>(contour, holes)) {}

    inline _Item(TContour<RawShape>&& contour,
                 THolesContainer<RawShape>&& holes):
        sh_(sl::create<RawShape>(std::move(contour), std::move(holes))) {}

    inline bool isFixed() const noexcept { return fixed_; }
    inline void markAsFixedInBin(int binid)
    {
        fixed_ = binid >= 0;
        binid_ = binid;
    }

    inline void binId(int idx) { binid_ = idx; }
    inline int binId() const noexcept { return binid_; }

    inline void priority(int p) { priority_ = p; }
    inline int priority() const noexcept { return priority_; }

    inline void itemId(int idx) { itemid_ = idx; }
    inline int itemId() const noexcept { return itemid_; }





    inline std::string toString() const
    {
        return sl::toString(sh_);
    }


    inline Iterator begin() const
    {
        return sl::cbegin(sh_);
    }


    inline Iterator cbegin() const
    {
        return sl::cbegin(sh_);
    }


    inline Iterator end() const
    {
        return sl::cend(sh_);
    }


    inline Iterator cend() const
    {
        return sl::cend(sh_);
    }











    inline Vertex vertex(unsigned long idx) const
    {
        return sl::vertex(sh_, idx);
    }










    inline void setVertex(unsigned long idx, const Vertex& v )
    {
        invalidateCache();
        sl::vertex(sh_, idx) = v;
    }









    inline double area() const {
        double ret ;
        if(area_cache_valid_) ret = area_cache_;
        else {
            ret = sl::area(infaltedShape());
            area_cache_ = ret;
            area_cache_valid_ = true;
        }
        return ret;
    }

    inline bool isContourConvex() const {
        bool ret = false;

        switch(convexity_) {
        case Convexity::UNCHECKED:
            ret = sl::isConvex(sl::contour(transformedShape()));
            convexity_ = ret? Convexity::C_TRUE : Convexity::C_FALSE;
            break;
        case Convexity::C_TRUE: ret = true; break;
        case Convexity::C_FALSE:;
        }

        return ret;
    }

    inline bool isHoleConvex(unsigned  ) const {
        return false;
    }

    inline bool areHolesConvex() const {
        return false;
    }


    inline size_t vertexCount() const {
        return sl::contourVertexCount(sh_);
    }

    inline size_t holeCount() const {
        return sl::holeCount(sh_);
    }






    inline bool isInside(const Vertex& p) const
    {
        return sl::isInside(p, transformedShape());
    }

    inline bool isInside(const _Item& sh) const
    {
        return sl::isInside(transformedShape(), sh.transformedShape());
    }

    inline bool isInside(const RawShape& sh) const
    {
        return sl::isInside(transformedShape(), sh);
    }

    inline bool isInside(const _Box<TPoint<RawShape>>& box) const;
    inline bool isInside(const _Circle<TPoint<RawShape>>& box) const;

    inline void translate(const Vertex& d) BP2D_NOEXCEPT
    {
        translation(translation() + d);
    }

    inline void rotate(const Radians& rads) BP2D_NOEXCEPT
    {
        rotation(rotation() + rads);
    }

    inline void inflation(Coord distance) BP2D_NOEXCEPT
    {
        inflation_ = distance;
        has_inflation_ = true;
        invalidateCache();
    }

    inline Coord inflation() const BP2D_NOEXCEPT {
        return inflation_;
    }

    inline void inflate(Coord distance) BP2D_NOEXCEPT
    {
        inflation(inflation() + distance);
    }

    inline Radians rotation() const BP2D_NOEXCEPT
    {
        return rotation_;
    }

    inline TPoint<RawShape> translation() const BP2D_NOEXCEPT
    {
        return translation_;
    }

    inline void rotation(Radians rot) BP2D_NOEXCEPT
    {
        if(rotation_ != rot) {
            rotation_ = rot; has_rotation_ = true; tr_cache_valid_ = false;
            rmt_valid_ = false; lmb_valid_ = false;
            bb_cache_.valid = false;
        }
    }

    inline void translation(const TPoint<RawShape>& tr) BP2D_NOEXCEPT
    {
        if(translation_ != tr) {
            translation_ = tr; has_translation_ = true; tr_cache_valid_ = false;

        }
    }

    inline const RawShape& transformedShape() const
    {
        if(tr_cache_valid_) return tr_cache_;

        RawShape cpy = infaltedShape();
        if(has_rotation_) sl::rotate(cpy, rotation_);
        if(has_translation_) sl::translate(cpy, translation_);
        tr_cache_ = cpy; tr_cache_valid_ = true;
        rmt_valid_ = false; lmb_valid_ = false;

        return tr_cache_;
    }

    inline operator RawShape() const
    {
        return transformedShape();
    }

    inline const RawShape& rawShape() const BP2D_NOEXCEPT
    {
        return sh_;
    }

    inline void resetTransformation() BP2D_NOEXCEPT
    {
        has_translation_ = false; has_rotation_ = false; has_inflation_ = false;
        invalidateCache();
    }

    inline Box boundingBox() const {
        if(!bb_cache_.valid) {
            if(!has_rotation_)
                bb_cache_.bb = sl::boundingBox(infaltedShape());
            else {

                auto rotsh = infaltedShape();
                sl::rotate(rotsh, rotation_);
                bb_cache_.bb = sl::boundingBox(rotsh);
            }
            bb_cache_.valid = true;
        }

        auto &bb = bb_cache_.bb; auto &tr = translation_;
        return {bb.minCorner() + tr, bb.maxCorner() + tr };
    }

    inline Vertex referenceVertex() const {
        return rightmostTopVertex();
    }

    inline Vertex rightmostTopVertex() const {
        if(!rmt_valid_ || !tr_cache_valid_) {
            auto& tsh = transformedShape();
            rmt_ = std::max_element(sl::cbegin(tsh), sl::cend(tsh), vsort);
            rmt_valid_ = true;
        }
        return *rmt_;
    }

    inline Vertex leftmostBottomVertex() const {
        if(!lmb_valid_ || !tr_cache_valid_) {
            auto& tsh = transformedShape();
            lmb_ = std::min_element(sl::cbegin(tsh), sl::cend(tsh), vsort);
            lmb_valid_ = true;
        }
        return *lmb_;
    }



    inline static bool intersects(const _Item& sh1, const _Item& sh2)
    {
        return sl::intersects(sh1.transformedShape(),
                                     sh2.transformedShape());
    }

    inline static bool touches(const _Item& sh1, const _Item& sh2)
    {
        return sl::touches(sh1.transformedShape(),
                                  sh2.transformedShape());
    }

private:

    inline const RawShape& infaltedShape() const {
        if(has_inflation_ ) {
            if(inflate_cache_valid_) return inflate_cache_;

            inflate_cache_ = sh_;
            sl::offset(inflate_cache_, inflation_);
            inflate_cache_valid_ = true;
            return inflate_cache_;
        }
        return sh_;
    }

    inline void invalidateCache() const BP2D_NOEXCEPT
    {
        tr_cache_valid_ = false;
        lmb_valid_ = false; rmt_valid_ = false;
        area_cache_valid_ = false;
        inflate_cache_valid_ = false;
        bb_cache_.valid = false;
        convexity_ = Convexity::UNCHECKED;
    }

    static inline bool vsort(const Vertex& v1, const Vertex& v2)
    {
        TCompute<Vertex> x1 = getX(v1), x2 = getX(v2);
        TCompute<Vertex> y1 = getY(v1), y2 = getY(v2);
        return y1 == y2 ? x1 < x2 : y1 < y2;
    }
};

template<class Sh> Sh create_rect(TCoord<Sh> width, TCoord<Sh> height)
{
    auto sh = sl::create<Sh>(
        {{0, 0}, {0, height}, {width, height}, {width, 0}});

    if constexpr (ClosureTypeV<Sh> == Closure::CLOSED)
        sl::addVertex(sh, {0, 0});

    if constexpr (OrientationTypeV<Sh> == Orientation::COUNTER_CLOCKWISE)
        std::reverse(sl::begin(sh), sl::end(sh));

    return sh;
}




template<class Sh>
class _Rectangle: public _Item<Sh> {
    using _Item<Sh>::vertex;
    using TO = Orientation;
public:

    using Unit = TCoord<Sh>;

    inline _Rectangle(Unit w, Unit h): _Item<Sh>{create_rect<Sh>(w, h)} {}

    inline Unit width() const BP2D_NOEXCEPT {
        return getX(vertex(2));
    }

    inline Unit height() const BP2D_NOEXCEPT {
        return getY(vertex(2));
    }
};

template<class RawShape>
inline bool _Item<RawShape>::isInside(const _Box<TPoint<RawShape>>& box) const {
    return sl::isInside(boundingBox(), box);
}

template<class RawShape> inline bool
_Item<RawShape>::isInside(const _Circle<TPoint<RawShape>>& circ) const {
    return sl::isInside(transformedShape(), circ);
}

template<class RawShape> using _ItemRef = std::reference_wrapper<_Item<RawShape>>;
template<class RawShape> using _ItemGroup = std::vector<_ItemRef<RawShape>>;




template<class RawShape>
using _PackGroup = std::vector<std::vector<_ItemRef<RawShape>>>;

template<class Iterator>
struct ConstItemRange {
    Iterator from;
    Iterator to;
    bool valid = false;

    ConstItemRange() = default;
    ConstItemRange(Iterator f, Iterator t): from(f), to(t), valid(true) {}
};

template<class Container>
inline ConstItemRange<typename Container::const_iterator>
rem(typename Container::const_iterator it, const Container& cont) {
    return {std::next(it), cont.end()};
}










template<class PlacementStrategy>
class PlacementStrategyLike {
    PlacementStrategy impl_;
public:

    using RawShape = typename PlacementStrategy::ShapeType;


    using Item = _Item<RawShape>;


    using Config = typename PlacementStrategy::Config;







    using BinType = typename PlacementStrategy::BinType;





    using PackResult = typename PlacementStrategy::PackResult;

    using ItemGroup = _ItemGroup<RawShape>;
    using DefaultIterator = typename ItemGroup::const_iterator;






    explicit PlacementStrategyLike(const BinType& bin,
                                   const Config& config = Config()):
        impl_(bin)
    {
        configure(config);
    }









    inline void configure(const Config& config) { impl_.configure(config); }













    template<class Iter = DefaultIterator>
    inline PackResult trypack(
            Item& item,
            const ConstItemRange<Iter>& remaining = ConstItemRange<Iter>())
    {
        return impl_.trypack(item, remaining);
    }







    inline void accept(PackResult& r) { impl_.accept(r); }














    template<class Range = ConstItemRange<DefaultIterator>>
    inline PackResult pack(
            Item& item,
            const Range& remaining = Range())
    {
        return impl_.pack(item, remaining);
    }





    inline void preload(const ItemGroup& packeditems)
    {
        impl_.preload(packeditems);
    }


    inline void unpackLast() { impl_.unpackLast(); }


    inline const BinType& bin() const { return impl_.bin(); }


    inline void bin(const BinType& bin) { impl_.bin(bin); }


    inline ItemGroup getItems() { return impl_.getItems(); }

    inline int getPackedSize()
    {
        int  size  = 0;
        auto items = getItems();
        for (const auto &itm : items) {
            if (itm.get().isFixed() == false) { size++; }
        }
        return size;
    }


    inline void clearItems() { impl_.clearItems(); }

    inline void clearItems(const std::function<bool(const Item &itm)> &func) { impl_.clearItems(func); }

    inline double filledArea() const { return impl_.filledArea(); }

    inline double score() const { return impl_.score(); }

    inline void plateID(int id) { impl_.plateID(id); }
    inline int plateID() { return impl_.plateID(); }

};


using ProgressFunction = std::function<void(unsigned)>;
using StopCondition = std::function<bool(void)>;
using UnfitIndicator = std::function<void(std::string)>;




template<class SelectionStrategy>
class SelectionStrategyLike {
    SelectionStrategy impl_;
public:
    using RawShape = typename SelectionStrategy::ShapeType;
    using Item = _Item<RawShape>;
    using PackGroup = _PackGroup<RawShape>;
    using Config = typename SelectionStrategy::Config;










    inline void configure(const Config& config) {
        impl_.configure(config);
    }







    void progressIndicator(ProgressFunction fn) { impl_.progressIndicator(fn); }


    void unfitIndicator(UnfitIndicator fn) { impl_.unfitIndicator(fn); }

    void stopCondition(StopCondition cond) { impl_.stopCondition(cond); }













    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    inline void packItems(
            TIterator first,
            TIterator last,
            TBin&& bin,
            PConfig&& config = PConfig() )
    {
        impl_.template packItems<TPlacer>(first, last,
                                 std::forward<TBin>(bin),
                                 std::forward<PConfig>(config));
    }






    inline const PackGroup& getResult() const {
        return impl_.getResult();
    }

    inline int lastPackedBinId() const {
        return impl_.lastPackedBinId();
    }

    void clear() { impl_.clear(); }
};





template<class PlacementStrategy, class SelectionStrategy >
class _Nester {
    using TSel = SelectionStrategyLike<SelectionStrategy>;
    TSel selector_;

public:
    using Item = typename PlacementStrategy::Item;
    using ShapeType = typename Item::ShapeType;
    using ItemRef = std::reference_wrapper<Item>;
    using TPlacer = PlacementStrategyLike<PlacementStrategy>;
    using BinType = typename TPlacer::BinType;
    using PlacementConfig = typename TPlacer::Config;
    using SelectionConfig = typename TSel::Config;
    using Coord = TCoord<TPoint<typename Item::ShapeType>>;
    using PackGroup = _PackGroup<typename Item::ShapeType>;
    using ResultType = PackGroup;

private:
    BinType bin_;
    PlacementConfig pconfig_;
    Coord min_obj_distance_;

    using SItem =  typename SelectionStrategy::Item;
    using TPItem = remove_cvref_t<Item>;
    using TSItem = remove_cvref_t<SItem>;

    StopCondition stopfn_;

    template<class It> using TVal = remove_ref_t<typename It::value_type>;

    template<class It, class Out>
    using ItemIteratorOnly =
        enable_if_t<std::is_convertible<TVal<It>&, TPItem&>::value, Out>;

public:







    template<class TBinType = BinType,
             class PConf = PlacementConfig,
             class SConf = SelectionConfig>
    _Nester(TBinType&& bin, Coord min_obj_distance = 0,
            const PConf& pconfig = PConf(), const SConf& sconfig = SConf()):
        bin_(std::forward<TBinType>(bin)),
        pconfig_(pconfig),
        min_obj_distance_(min_obj_distance)
    {
        static_assert( std::is_same<TPItem, TSItem>::value,
                       "Incompatible placement and selection strategy!");

        selector_.configure(sconfig);
    }

    void configure(const PlacementConfig& pconf) { pconfig_ = pconf; }
    void configure(const SelectionConfig& sconf) { selector_.configure(sconf); }
    void configure(const PlacementConfig& pconf, const SelectionConfig& sconf)
    {
        pconfig_ = pconf;
        selector_.configure(sconf);
    }
    void configure(const SelectionConfig& sconf, const PlacementConfig& pconf)
    {
        pconfig_ = pconf;
        selector_.configure(sconf);
    }











    template<class It>
    inline ItemIteratorOnly<It, size_t> execute(It from, It to)
    {
        auto infl = static_cast<Coord>(std::ceil(min_obj_distance_/2.0));
        if(infl > 0) std::for_each(from, to, [infl](Item& item) {
            item.inflate(infl);
        });

        selector_.template packItems<PlacementStrategy>(
            from, to, bin_, pconfig_);

        if(min_obj_distance_ > 0) std::for_each(from, to, [infl](Item& item) {
            item.inflate(-infl);
        });

        return selector_.getResult().size();
    }


    inline _Nester& progressIndicator(ProgressFunction func)
    {
        selector_.progressIndicator(func); return *this;
    }


    inline _Nester& unfitIndicator(UnfitIndicator func)
    {
        selector_.unfitIndicator(func); return *this;
    }


    inline _Nester& stopCondition(StopCondition fn)
    {
        stopfn_ = fn; selector_.stopCondition(fn); return *this;
    }

    inline const PackGroup& lastResult() const
    {
        return selector_.getResult();
    }

    inline int lastPackedBinId() const {
        return selector_.lastPackedBinId();
    }
};

}

#endif
