//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_POLYLINE_STITCHER_H
#define UTILS_POLYLINE_STITCHER_H

#include <stddef.h>
#include <stdint.h>
#include <cassert>
#include <functional>
#include <limits>
#include <vector>
#include <cinttypes>
#include <cstddef>

#include "SparsePointGrid.hpp"
#include "PolygonsPointIndex.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne
{

template<typename Paths, typename Path, typename Junction>
class PolylineStitcher
{
public:
    static void stitch(const Paths& lines, Paths& result_lines, Paths& result_polygons, coord_t max_stitch_distance = scaled<coord_t>(0.1), coord_t snap_distance = scaled<coord_t>(0.01))
    {
        if (lines.empty())
            return;

        SparsePointGrid<PathsPointIndex<Paths>, PathsPointIndexLocator<Paths>> grid(max_stitch_distance, lines.size() * 2);

        for (size_t line_idx = 0; line_idx < lines.size(); line_idx++)
        {
            const auto line = lines[line_idx];
            grid.insert(PathsPointIndex<Paths>(&lines, line_idx, 0));
            grid.insert(PathsPointIndex<Paths>(&lines, line_idx, line.size() - 1));
        }

        std::vector<bool> processed(lines.size(), false);

        for (size_t line_idx = 0; line_idx < lines.size(); line_idx++)
        {
            if (processed[line_idx])
            {
                continue;
            }
            processed[line_idx] = true;
            const auto line = lines[line_idx];
            bool should_close = isOdd(line);

            Path chain = line;
            bool closest_is_closing_polygon = false;
            for (bool go_in_reverse_direction : { false, true })
            {
                if (go_in_reverse_direction)
                {
                    chain.reverse();
                }
                int64_t chain_length = chain.polylineLength();

                while (true)
                {
                    Point from = make_point(chain.back());

                    PathsPointIndex<Paths> closest;
                    coord_t closest_distance = std::numeric_limits<coord_t>::max();
                    grid.processNearby(from, max_stitch_distance,
                                       std::function<bool (const PathsPointIndex<Paths>&)> (
                                           [from, &chain, &closest, &closest_is_closing_polygon, &closest_distance, &processed, &chain_length, go_in_reverse_direction, max_stitch_distance, snap_distance, should_close]
                                           (const PathsPointIndex<Paths>& nearby)->bool
                                           {
                                               bool is_closing_segment = false;
                                               coord_t dist = (nearby.p().template cast<int64_t>() - from.template cast<int64_t>()).norm();
                                               if (dist > max_stitch_distance)
                                               {
                                                   return true;
                                               }
                                               if ((nearby.p().template cast<int64_t>() - make_point(chain.front()).template cast<int64_t>()).squaredNorm() < snap_distance * snap_distance)
                                               {
                                                   if (chain_length + dist < 3 * max_stitch_distance
                                                       || chain.size() <= 2)
                                                   {
                                                       return true;
                                                   }
                                                   is_closing_segment = true;
                                                   if (!should_close)
                                                   {
                                                       dist += scaled<coord_t>(0.01);
                                                   }
                                                   else
                                                   {
                                                       dist -= scaled<coord_t>(0.01);
                                                   }
                                               }
                                               else if (processed[nearby.poly_idx])
                                               {
                                                   return true;
                                               }
                                               bool nearby_would_be_reversed = nearby.point_idx != 0;
                                               nearby_would_be_reversed = nearby_would_be_reversed != go_in_reverse_direction;
                                               if (!canReverse(nearby) && nearby_would_be_reversed)
                                               {
                                                   return true;
                                               }
                                               if (!canConnect(chain, (*nearby.polygons)[nearby.poly_idx]))
                                               {
                                                   return true;
                                               }
                                               if (dist < closest_distance)
                                               {
                                                   closest_distance = dist;
                                                   closest = nearby;
                                                   closest_is_closing_polygon = is_closing_segment;
                                               }
                                               if (dist < snap_distance)
                                               {
                                                   return false;
                                               }
                                               return true;
                                           })
                    );

                    if (!closest.initialized()
                        || closest_is_closing_polygon
                    )
                    {
                        break;
                    }

                    coord_t segment_dist = (make_point(chain.back()).template cast<int64_t>() - closest.p().template cast<int64_t>()).norm();
                    assert(segment_dist <= max_stitch_distance + scaled<coord_t>(0.01));
                    const size_t old_size = chain.size();
                    if (closest.point_idx == 0)
                    {
                        auto start_pos = (*closest.polygons)[closest.poly_idx].begin();
                        if (segment_dist < snap_distance)
                        {
                            ++start_pos;
                        }
                        chain.insert(chain.end(), start_pos, (*closest.polygons)[closest.poly_idx].end());
                    }
                    else
                    {
                        auto start_pos = (*closest.polygons)[closest.poly_idx].rbegin();
                        if (segment_dist < snap_distance)
                        {
                            ++start_pos;
                        }
                        chain.insert(chain.end(), start_pos, (*closest.polygons)[closest.poly_idx].rend());
                    }
                    for(size_t i = old_size; i < chain.size(); ++i)
                    {
                        chain_length += (make_point(chain[i]).template cast<int64_t>() - make_point(chain[i - 1]).template cast<int64_t>()).norm();
                    }
                    should_close = should_close & !isOdd((*closest.polygons)[closest.poly_idx]);
                    assert( ! processed[closest.poly_idx]);
                    processed[closest.poly_idx] = true;
                }
                if (closest_is_closing_polygon)
                {
                    if (go_in_reverse_direction)
                    {
                        chain.reverse();
                    }

                    break;
                }
            }
            if (closest_is_closing_polygon)
            {
                result_polygons.emplace_back(chain);
            }
            else
            {
                PathsPointIndex<Paths> ppi_here(&lines, line_idx, 0);
                if ( ! canReverse(ppi_here))
                {
                    chain.reverse();
                }
                result_lines.emplace_back(chain);
            }
        }
    }

    static bool canReverse(const PathsPointIndex<Paths> &polyline);

    static bool canConnect(const Path &a, const Path &b);

    static bool isOdd(const Path &line);
};

}
#endif