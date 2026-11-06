#pragma once










#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../Point.hpp"

namespace Slic3r {
namespace TextureBake {

static constexpr double WELD_GRID_EXPORT     = 1e4;
static constexpr double WELD_GRID_GEOMETRY   = 1e5;
static constexpr double WELD_GRID_DECIMATION = 1e6;



inline int64_t grid_round(double v) { return int64_t(std::floor(v + 0.5)); }







class QuantizedPointMap
{
public:
    explicit QuantizedPointMap(double quant, size_t expected = 256) : m_quant(quant)
    {
        size_t       cap    = 16;
        const size_t target = std::max<size_t>(16, size_t(std::ceil(double(expected) / 0.6)));
        while (cap < target)
            cap *= 2;
        alloc(cap);
    }

    size_t size() const { return m_size; }

    bool   inserted() const { return m_inserted; }

    int get(float x, float y, float z)
    {
        return m_cells[slot(grid_round(double(x) * m_quant), grid_round(double(y) * m_quant),
                            grid_round(double(z) * m_quant))].val;
    }
    int get(const Vec3f &p) { return get(p.x(), p.y(), p.z()); }



    int get_or_set(float x, float y, float z, int value)
    {
        return get_or_set_key(grid_round(double(x) * m_quant), grid_round(double(y) * m_quant),
                              grid_round(double(z) * m_quant), value);
    }
    int get_or_set(const Vec3f &p, int value) { return get_or_set(p.x(), p.y(), p.z(), value); }



    int get_key(int64_t a, int64_t b, int64_t c) { return m_cells[slot(a, b, c)].val; }
    int get_or_set_key(int64_t a, int64_t b, int64_t c, int value)
    {
        const size_t i = slot(a, b, c);
        Cell        &cell = m_cells[i];
        if (cell.val != -1) {
            m_inserted = false;
            return cell.val;
        }
        cell.qx = a; cell.qy = b; cell.qz = c;
        cell.val   = value;
        m_inserted = true;
        if (++m_size > size_t(double(m_cap) * 0.7))
            grow();
        return value;
    }

private:
    struct Cell
    {
        int64_t qx = 0, qy = 0, qz = 0;
        int32_t val = -1;
    };

    void alloc(size_t cap)
    {
        m_cap  = cap;
        m_mask = cap - 1;
        m_cells.assign(cap, Cell{});
    }

    size_t slot(int64_t qx, int64_t qy, int64_t qz) const
    {





        uint32_t h = (uint32_t(qx) * 0x9E3779B1u) ^ (uint32_t(qy) * 0x85EBCA77u) ^ (uint32_t(qz) * 0xC2B2AE3Du);
        h ^= h >> 15;
        size_t i = size_t(h) & m_mask;


        while (m_cells[i].val != -1) {
            const Cell &c = m_cells[i];
            if (c.qx == qx && c.qy == qy && c.qz == qz)
                return i;
            i = (i + 1) & m_mask;
        }
        return i;
    }

    void grow()
    {
        std::vector<Cell> old = std::move(m_cells);
        alloc(m_cap * 2);
        for (const Cell &c : old)
            if (c.val != -1)
                m_cells[slot(c.qx, c.qy, c.qz)] = c;
    }

    double            m_quant;
    size_t            m_cap = 0, m_mask = 0, m_size = 0;
    bool              m_inserted = false;
    std::vector<Cell> m_cells;
};



struct TriSoup
{
    std::vector<Vec3f> pos;
    std::vector<Vec3f> nrm;
    std::vector<float> exclude_weight;

    size_t triangle_count() const { return pos.size() / 3; }
    bool   empty() const { return pos.empty(); }
};


struct WeldResult
{
    std::vector<int> vertex_id;
    int              unique_count = 0;
};
WeldResult weld_vertices(const std::vector<Vec3f> &positions, double quant);

}
}
