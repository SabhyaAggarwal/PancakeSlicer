#include "RotaryRouter.hpp"
#include "AtomToolpath.hpp"
#include "ConformalShells.hpp"
#include "LeanVase.hpp"
#include "MultiplanarConfirm.hpp"
#include "RotarySkin.hpp"

#include "libslic3r/GCode/ToolKinematics.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <admesh/stl.h>
#include <math.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <boost/log/trivial.hpp>

namespace Slic3r::Nonplanar {

static bool flat_overhangs_claim(const PrintObject &object)
{
    constexpr double FlatUpDeg = 3., WallDeg = 1., DownDeg = 30., MinHeight = 0.5, MinArea = 20., MaxOtherShare = 0.01;
    if (object.config().nonplanar_mode.value != NonplanarMode::Auto || object.model_object()->instances.empty())
        return false;
    TriangleMesh mesh = object.model_object()->raw_mesh();
    mesh.transform(object.model_object()->instances.front()->get_matrix());
    const float  bottom = mesh.bounding_box().min.z();
    const double flat_up = std::cos(FlatUpDeg * M_PI / 180.), wall = std::sin(WallDeg * M_PI / 180.),
                 down = -std::cos(DownDeg * M_PI / 180.);
    double undersides = 0., other = 0., total = 0.;
    for (const stl_triangle_vertex_indices &face : mesh.its.indices) {
        const Vec3f a = mesh.its.vertices[face[0]], b = mesh.its.vertices[face[1]], c = mesh.its.vertices[face[2]];
        const Vec3f n = (b - a).cross(c - a);
        const float twice = n.norm();
        if (twice <= 0.f)
            continue;
        const double nz = n.z() / twice, area = 0.5 * twice;
        total += area;
        if (nz >= flat_up || std::abs(nz) <= wall || (nz < 0. && std::max({ a.z(), b.z(), c.z() }) <= bottom + 0.05f))
            continue;
        if (nz <= down) {
            if (std::min({ a.z(), b.z(), c.z() }) > bottom + MinHeight)
                undersides += area;
            continue;
        }
        other += area;
    }
    return undersides >= MinArea && other <= MaxOtherShare * total;
}

const std::vector<RotaryRoute> &rotary_routes()
{
    static const std::vector<RotaryRoute> routes {
        { "lean_vase", RotaryRoute::Stage::Replace, lean_vase_considered,
          [](PrintObject &object, ConformalResult &result) {
              std::string        why;
              const AtomToolpath plan = lean_vase_toolpath(object, why);
              if (plan.points.empty() || !lean_vase_in_time(object, plan, why)) {
                  BOOST_LOG_TRIVIAL(warning) << "Lean vase: " << object.model_object()->name << " keeps the spiral vase: " << why;
                  return false;
              }
              object.print()->set_status(40, L("Leaning the vase wall"));
              host_ordered_plan(object, plan, AtomPlacement(), result);
              return true;
          },
          lean_vase_flares },
        { "flat_overhangs", RotaryRoute::Stage::Replace, flat_overhangs_claim,
          [](PrintObject &, ConformalResult &) { return false; } },
        { "wall_skin", RotaryRoute::Stage::Append, rotary_skin_claims,
          [](PrintObject &object, ConformalResult &result) {
              build_rotary_skin(object, result);
              return true;
          } },
    };
    return routes;
}

static bool routed(const PrintObject &object)
{
    return object.config().nonplanar_mode.value != NonplanarMode::Off && object.config().nonplanar_atom_toolpath.value.empty() &&
           make_tool_kinematics(object.print()->config())->rotary_count() > 0;
}

bool rotary_route_claimed(const PrintObject &object)
{
    const std::vector<RotaryRoute> &routes = rotary_routes();
    return !routes.empty() && routed(object) && std::any_of(routes.begin(), routes.end(), [&object](const RotaryRoute &route) {
               return route.stage == RotaryRoute::Stage::Replace && (route.decides ? route.decides(object) : route.claims(object));
           });
}

static bool appended(const PrintObject &object)
{
    return object.config().nonplanar_atom_toolpath.value.empty() && !rotary_auto_measurement() &&
           (routed(object) || auto_rotary_requested(object));
}

bool rotary_route_appends(const PrintObject &object)
{
    const std::vector<RotaryRoute> &routes = rotary_routes();
    return appended(object) && std::any_of(routes.begin(), routes.end(), [&object](const RotaryRoute &route) {
               return route.stage == RotaryRoute::Stage::Append && route.claims(object);
           });
}

bool build_rotary_routes(PrintObject &object, ConformalResult &result, RotaryRoute::Stage stage)
{
    const std::vector<RotaryRoute> &routes = rotary_routes();
    if (routes.empty() || !(stage == RotaryRoute::Stage::Append ? appended(object) : routed(object)))
        return false;
    for (const RotaryRoute &route : routes)
        if (route.stage == stage && route.claims(object) && route.build(object, result) && stage == RotaryRoute::Stage::Replace)
            return true;
    return false;
}

RoutePrediction predict_rotary_route(const indexed_triangle_set &mesh, const PrintObject & )
{
    RoutePrediction prediction;
    if (mesh.vertices.empty())
        return prediction;
    float bottom = mesh.vertices.front().z();
    for (const stl_vertex &v : mesh.vertices)
        bottom = std::min(bottom, v.z());
    const double flat = std::cos(3. * M_PI / 180.), wall = std::sin(1. * M_PI / 180.);
    for (const stl_triangle_vertex_indices &face : mesh.indices) {
        const Vec3f a = mesh.vertices[face[0]], b = mesh.vertices[face[1]], c = mesh.vertices[face[2]];
        const Vec3f n = (b - a).cross(c - a);
        const float length = n.norm();
        if (length <= 1e-12f)
            continue;
        const double nz = n.z() / length;
        const bool on_bed = nz < 0. && std::max({ a.z(), b.z(), c.z() }) <= bottom + 0.05f;
        if (!(nz >= flat || std::abs(nz) <= wall || on_bed))
            return prediction;
    }
    prediction.planar = prediction.confidence = 1.;
    return prediction;
}

}
