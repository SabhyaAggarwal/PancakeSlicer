#pragma once



#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/Point.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r {

class ExtrusionEntityCollection;
class MaterialColumns;

namespace Nonplanar {







constexpr const char *NpFanTag      = ";_NP_FAN:";
constexpr const char *NpFanEndTag   = ";_NP_FAN_END";
constexpr const char *NpPassHoldTag = ";_NP_PASS_HOLD";


constexpr double SmallRoadLength = 2.;



class SupportSnapshot
{
public:
    SupportSnapshot() = default;

    SupportSnapshot(const MaterialColumns &columns, const BoundingBoxf &area);
    bool   empty() const { return m_top.empty(); }

    double top(const Vec2d &xy) const;

private:
    Vec2d               m_origin{ Vec2d::Zero() };
    double              m_pitch = 0.;
    int                 m_nx = 0, m_ny = 0;
    std::vector<double> m_top;
};

struct RoadFanParams
{
    int    lateral_samples = 5;
    double downhill_start  = 0.087;
    double downhill_full   = 0.577;
    double downhill_weight = 0.5;
    double hang_weight     = 1.;


    double overhang_from   = 2.;
    double lead            = 1.0;
    double hold            = 1.0;
    double step            = 0.1;
};





std::vector<float> road_hang(const std::vector<ConformalPoint> &points, double width, const SupportSnapshot &support,
                             const std::function<Vec2d(const Vec2d &)> &gradient, int samples);






std::vector<float> road_fan_demand(const std::vector<ConformalPoint> &points, const std::vector<double> &speeds, double width,
                                   const SupportSnapshot &support, const std::function<Vec2d(const Vec2d &)> &gradient,
                                   const RoadFanParams &params);


std::vector<std::string> road_fan_tags(const std::vector<float> &demand, float &level);


bool pass_is_solid(const ExtrusionEntityCollection &pass);

bool pass_is_top(const ExtrusionEntityCollection &pass);

}
}
