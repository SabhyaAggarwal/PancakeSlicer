#pragma once

#include <functional>
#include <string>
#include <vector>

struct indexed_triangle_set;

namespace Slic3r {
class PrintObject;
}

namespace Slic3r::Nonplanar {

struct ConformalResult;









struct RotaryRoute
{
    enum class Stage { Replace, Append };
    const char *name;
    Stage       stage;
    std::function<bool(const PrintObject &)>                      claims;
    std::function<bool(PrintObject &, ConformalResult &)>         build;

    std::function<bool(const PrintObject &)>                      decides = {};
};


const std::vector<RotaryRoute> &rotary_routes();




bool rotary_route_claimed(const PrintObject &object);


bool rotary_route_appends(const PrintObject &object);


bool build_rotary_routes(PrintObject &object, ConformalResult &result, RotaryRoute::Stage stage);




struct RoutePrediction
{
    double      planar = 0.;
    std::string route;
    double      confidence = 0.;
};
RoutePrediction predict_rotary_route(const indexed_triangle_set &mesh, const PrintObject &object);


constexpr double PlanarEarlyThreshold = 0.95;

}
