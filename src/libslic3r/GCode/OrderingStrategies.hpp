

#ifndef slic3r_OrderingStrategies_hpp_
#define slic3r_OrderingStrategies_hpp_

#include "../Point.hpp"
#include <cstddef>

#ifndef SLIC3R_TEST_HARNESS
#include "../Print.hpp"
#endif

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace Slic3r { enum class PrintOrder; }

namespace Slic3r {





bool tsp_2opt_improve(std::vector<size_t>& path, const Points& centers, int max_passes = 10);



bool tsp_remove_crossings(std::vector<size_t>& path, const Points& centers);


void tsp_rotate_minimize_closing(std::vector<size_t>& path, const Points& centers);


inline double tsp_cycle_path_length(const std::vector<size_t>& path, const Points& centers)
{
    if (path.size() < 2) return 0.0;
    double total = 0.0;
    for (size_t i = 0; i < path.size(); ++i) {
        size_t next = (i + 1) % path.size();
        total += (centers[path[i]].cast<double>() - centers[path[next]].cast<double>()).norm();
    }
    return total;
}


inline double tsp_max_edge_length(const std::vector<size_t>& path, const Points& centers)
{
    if (path.size() < 2) return 0.0;
    double mx = 0.0;
    for (size_t i = 0; i < path.size(); ++i) {
        size_t next = (i + 1) % path.size();
        double d = (centers[path[i]].cast<double>() - centers[path[next]].cast<double>()).norm();
        if (d > mx) mx = d;
    }
    return mx;
}



#ifndef SLIC3R_TEST_HARNESS





template<typename CoreFn>
std::vector<const PrintInstance*> chain_instances_with_core(
    const std::vector<const PrintObject*>& print_objects,
    const Point* start_near,
    CoreFn&& core_fn)
{
    Points instance_centers;
    std::vector<std::pair<size_t, size_t>> instances;
    for (size_t i = 0; i < print_objects.size(); ++i) {
        const PrintObject& object = *print_objects[i];
        for (size_t j = 0; j < object.instances().size(); ++j) {
            instance_centers.emplace_back(object.instances()[j].shift);
            instances.emplace_back(i, j);
        }
    }

    if (instance_centers.empty()) return {};


    if (start_near != nullptr) {
        size_t best_start = 0;
        double best_d2 = std::numeric_limits<double>::max();
        for (size_t k = 0; k < instance_centers.size(); ++k) {
            double d2 = (instance_centers[k].cast<double>() - start_near->cast<double>()).squaredNorm();
            if (d2 < best_d2) { best_d2 = d2; best_start = k; }
        }
        std::rotate(instance_centers.begin(), instance_centers.begin() + best_start, instance_centers.end());
        std::rotate(instances.begin(), instances.begin() + best_start, instances.end());
    }

    auto path = core_fn(instance_centers);




    if (start_near != nullptr && !path.empty()) {


        auto it = std::find(path.begin(), path.end(), size_t(0));
        if (it != path.begin())
            std::rotate(path.begin(), it, path.end());
    } else {
        tsp_rotate_minimize_closing(path, instance_centers);
    }

    std::vector<const PrintInstance*> out;
    out.reserve(path.size());
    for (size_t step : path) {
        out.emplace_back(&print_objects[instances[step].first]->instances()[instances[step].second]);
    }
    return out;
}

#endif




std::vector<size_t> snake_core(const Points& centers);

#ifndef SLIC3R_TEST_HARNESS




std::vector<const PrintInstance*> chain_print_object_instances_snake(const std::vector<const PrintObject*>& print_objects, const Point* start_near);
std::vector<const PrintInstance*> chain_print_object_instances_snake(const Print& print);



std::vector<const PrintInstance*> chain_print_object_instances_best_of(const std::vector<const PrintObject*>& print_objects, const Point* start_near);
std::vector<const PrintInstance*> chain_print_object_instances_best_of(const Print& print);




std::vector<size_t> order_points_with_strategy(const Points& points, PrintOrder print_order, const Point* start_near);

#endif

}

#endif
