#ifndef slic3r_ShortestPath_hpp_
#define slic3r_ShortestPath_hpp_

#include "Polyline.hpp"
#include "Point.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "ExPolygon.hpp"

namespace Slic3r { class ExtrusionEntity; }
namespace Slic3r { class ExtrusionPath; }
namespace Slic3r { class Line; }

namespace Slic3r {

std::vector<size_t> 				 chain_points(const Points &points, const Point *start_near = nullptr);

std::vector<size_t> 				 chain_points_with_postprocessing(const Points &points, const Point *start_near = nullptr);
std::vector<size_t> 				 chain_expolygons(const ExPolygons &input_exploy);

std::vector<std::pair<size_t, bool>> chain_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near = nullptr);
void                                 reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const std::vector<std::pair<size_t, bool>> &chain);
void                                 chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point &start_near);
void                                 chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near = nullptr);

void                                 chain_and_reorder_extrusion_entities(std::vector<const ExtrusionEntity*> &entities, const Point &start_near,
                                                                          std::vector<std::unique_ptr<ExtrusionEntity>> &reversed_clones);

std::vector<std::pair<size_t, bool>> chain_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near = nullptr);
void                                 reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, std::vector<std::pair<size_t, bool>> &chain);
void                                 chain_and_reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near = nullptr);

Polylines 							 chain_polylines(Polylines &&src, const Point *start_near = nullptr);
inline Polylines 					 chain_polylines(const Polylines& src, const Point* start_near = nullptr) { Polylines tmp(src); return chain_polylines(std::move(tmp), start_near); }
template<typename T> inline void reorder_by_shortest_traverse(std::vector<T> &polylines_out)
{
    Points start_point;
    start_point.reserve(polylines_out.size());
    for (const T& contour : polylines_out) start_point.push_back(contour.points.front());

    std::vector<Points::size_type> order = chain_points(start_point);

    std::vector<T> Temp = polylines_out;
    polylines_out.erase(polylines_out.begin(), polylines_out.end());

    for (size_t i:order) polylines_out.emplace_back(std::move(Temp[i]));
}




class Print;
struct PrintInstance;

class PrintObject;
std::vector<const PrintInstance*> chain_print_object_instances(const std::vector<const PrintObject*>& print_objects, const Point* start_near);
std::vector<const PrintInstance*> 	 chain_print_object_instances(const Print &print);


Polylines 							 chain_lines(const std::vector<Line> &lines, const double point_distance_epsilon);

}

#endif
