#ifndef slic3r_BridgeDetector_hpp_
#define slic3r_BridgeDetector_hpp_

#include "ClipperUtils.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "PrincipalComponents2D.hpp"
#include "libslic3r.h"
#include "ExPolygon.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Slic3r {







class BridgeDetector {
public:

    const ExPolygons            &expolygons;

    ExPolygons                   expolygons_owned;

    const ExPolygons   			&lower_slices;

    coord_t                      spacing;

    double                       resolution;

    double                       angle;

    BridgeDetector(ExPolygon _expolygon, const ExPolygons &_lower_slices, coord_t _extrusion_width);
    BridgeDetector(const ExPolygons &_expolygons, const ExPolygons &_lower_slices, coord_t _extrusion_width);

    bool detect_angle(double bridge_direction_override = 0.);
    Polygons coverage(double angle = -1, bool precise = true) const;
    void unsupported_edges(double angle, Polylines* unsupported) const;
    Polylines unsupported_edges(double angle = -1) const;

private:

    BridgeDetector& operator=(const BridgeDetector &);

    void initialize();

    struct BridgeDirection {
        BridgeDirection(double a = -1.) : angle(a), coverage(0.), max_length(0.), archored_percent(0.){}

        bool operator<(const BridgeDirection &other) const {

            return this->coverage > other.coverage;
        };
        double angle;
        double coverage;
        double max_length;
        double archored_percent;
    };


    std::vector<double> bridge_direction_candidates() const;


    Polylines _edges;

    ExPolygons _anchor_regions;
};



inline std::tuple<Vec2d, double> detect_bridging_direction(const Lines &floating_edges, const Polygons &overhang_area)
{
    if (floating_edges.empty()) {

        auto [pc1, pc2] = compute_principal_components(overhang_area);
        if (pc2 == Vec2f::Zero()) {
            return {Vec2d{1.0,0.0}, 0.0};
        } else {
            return {pc2.normalized().cast<double>(), 0.0};
        }
    }


    std::unordered_map<double, Vec2d> directions{};
    for (const Line &l : floating_edges) {
        Vec2d normal = l.normal().cast<double>().normalized();
        double quantized_angle = std::ceil(std::atan2(normal.y(),normal.x()) * 1000.0);
        directions.emplace(quantized_angle, normal);
    }
    std::vector<std::pair<Vec2d, double>> direction_costs{};

    for (const auto& d : directions) {
        direction_costs.emplace_back(d.second, 0.0);
    }

    for (const Line &l : floating_edges) {
        Vec2d line = (l.b - l.a).cast<double>();
        for (auto &dir_cost : direction_costs) {

            dir_cost.second += std::abs(line.dot(dir_cost.first));
        }
    }

    Vec2d  result_dir = Vec2d::Ones();
    double min_cost   = std::numeric_limits<double>::max();
    for (const auto &cost : direction_costs) {
        if (cost.second < min_cost) {

            result_dir = Vec2d{cost.first.y(), -cost.first.x()};
            min_cost   = cost.second;
        }
    }

    return {result_dir, min_cost};
};


inline std::tuple<Vec2d, double> detect_bridging_direction(const Polygons &to_cover, const Polygons &anchors_area)
{
    Polygons  overhang_area      = diff(to_cover, anchors_area);
    Lines     floating_edges     = to_lines(diff_pl(to_polylines(overhang_area), expand(anchors_area, float(SCALED_EPSILON))));
    return detect_bridging_direction(floating_edges, overhang_area);
}

}

#endif
