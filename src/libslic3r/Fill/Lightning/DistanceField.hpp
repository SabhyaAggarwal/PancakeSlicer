//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_DISTANCE_FIELD_H
#define LIGHTNING_DISTANCE_FIELD_H

#include "../../BoundingBox.hpp"
#include "../../Point.hpp"
#include "../../Polygon.hpp"
#include "libslic3r/libslic3r.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include <cassert>

namespace Slic3r::FillLightning
{

class DistanceField
{
public:
    DistanceField(const coord_t& radius, const Polygons& current_outline, const BoundingBox& current_outlines_bbox, const Polygons& current_overhang);
    
    bool tryGetNextPoint(Point *out_unsupported_location, size_t *out_unsupported_cell_idx, const size_t start_idx = 0) const
    {
        for (size_t point_idx = start_idx; point_idx < m_unsupported_points.size(); ++point_idx) {
            if (!m_unsupported_points_erased[point_idx]) {
                *out_unsupported_cell_idx = point_idx;
                *out_unsupported_location = m_unsupported_points[point_idx].loc;
                return true;
            }
        }

        return false;
    }

    void update(const Point& to_node, const Point& added_leaf);

protected:
    coord_t m_cell_size;

    coord_t m_supporting_radius;
    int64_t m_supporting_radius2;

    struct UnsupportedCell
    {
        Point loc;
        coord_t dist_to_boundary;
    };

    std::vector<UnsupportedCell> m_unsupported_points;
    std::vector<bool>            m_unsupported_points_erased;

    const BoundingBox          m_unsupported_points_bbox;

    class UnsupportedPointsGrid
    {
    public:
        UnsupportedPointsGrid() = default;
        void initialize(const std::vector<UnsupportedCell> &unsupported_points, const std::function<Point(const Point &)> &map_cell_to_grid)
        {
            if (unsupported_points.empty())
                return;

            BoundingBox unsupported_points_bbox;
            for (const UnsupportedCell &cell : unsupported_points)
                unsupported_points_bbox.merge(cell.loc);

            m_size        = unsupported_points.size();
            m_grid_range  = BoundingBox(map_cell_to_grid(unsupported_points_bbox.min), map_cell_to_grid(unsupported_points_bbox.max));
            m_grid_size   = m_grid_range.size() + Point::Ones();

            m_data.assign(m_grid_size.y() * m_grid_size.x(), std::numeric_limits<size_t>::max());
            m_data_erased.assign(m_grid_size.y() * m_grid_size.x(), true);

            for (size_t cell_idx = 0; cell_idx < unsupported_points.size(); ++cell_idx) {
                const size_t flat_idx   = map_to_flat_array(map_cell_to_grid(unsupported_points[cell_idx].loc));
                assert(m_data[flat_idx] == std::numeric_limits<size_t>::max());
                m_data[flat_idx]        = cell_idx;
                m_data_erased[flat_idx] = false;
            }
        }

        size_t size() const { return m_size; }

        size_t find_cell_idx(const Point &grid_addr)
        {
            if (!m_grid_range.contains(grid_addr))
                return std::numeric_limits<size_t>::max();

            if (const size_t flat_idx = map_to_flat_array(grid_addr); !m_data_erased[flat_idx]) {
                assert(m_data[flat_idx] != std::numeric_limits<size_t>::max());
                return m_data[flat_idx];
            }

            return std::numeric_limits<size_t>::max();
        }

        void mark_erased(const Point &grid_addr)
        {
            assert(m_grid_range.contains(grid_addr));
            if (!m_grid_range.contains(grid_addr))
                return;

            const size_t flat_idx = map_to_flat_array(grid_addr);
            assert(!m_data_erased[flat_idx] && m_data[flat_idx] != std::numeric_limits<size_t>::max());
            assert(m_size != 0);

            m_data_erased[flat_idx] = true;
            --m_size;
        }

    private:
        size_t m_size = 0;

        BoundingBox m_grid_range;
        Point       m_grid_size;

        std::vector<size_t> m_data;
        std::vector<bool>   m_data_erased;

        inline size_t map_to_flat_array(const Point &loc) const
        {
            const Point  offset_loc = loc - m_grid_range.min;
            const size_t flat_idx   = m_grid_size.x() * offset_loc.y() + offset_loc.x();
            assert(offset_loc.x() >= 0 && offset_loc.y() >= 0);
            assert(flat_idx < size_t(m_grid_size.y() * m_grid_size.x()));
            return flat_idx;
        }
    };

    UnsupportedPointsGrid m_unsupported_points_grid;

    Point to_grid_point(const Point &point) const {
        return (point - m_unsupported_points_bbox.min) / m_cell_size;
    }

    Point from_grid_point(const Point &point) const {
        return point * m_cell_size + m_unsupported_points_bbox.min;
    }

#ifdef LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT
    friend void export_distance_field_to_svg(const std::string &path, const Polygons &outline, const Polygons &overhang, const std::list<DistanceField::UnsupportedCell> &unsupported_points, const Points &points);
#endif
};

}

#endif
