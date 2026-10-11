#pragma once
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r {
struct GCodeProcessorResult;
class ToolKinematics;
class Print;

class MaterialColumns
{
public:
    MaterialColumns(const BoundingBoxf &bbox, double pitch);
    void deposit(const Vec3d &a, const Vec3d &b, double width);
    double top_in_rect(const BoundingBoxf &rect) const;
    double ceiling() const { return m_ceiling; }
    double clearance(const Vec3d &tip, const Nonplanar::HeadEnvelope &head, std::string *component = nullptr) const;
    double swept_clearance(const Vec3d &a, const Vec3d &b, const Nonplanar::HeadEnvelope &head,
                           std::string *component = nullptr) const;
private:
    void cells(const BoundingBoxf &rect, int &x0, int &y0, int &x1, int &y1) const;
    BoundingBoxf m_bbox;
    double m_pitch;
    int m_nx, m_ny, m_tile, m_tx;
    std::vector<double> m_top, m_tiles;
    double m_ceiling = -std::numeric_limits<double>::infinity();
};

double safe_travel_z(const MaterialColumns &columns, const Nonplanar::HeadEnvelope &head,
                     const Vec2d &a, const Vec2d &b, double hop);
struct ClearanceHit
{
    size_t move_id = 0;
    unsigned int gcode_line = 0;
    std::string component;
    double penetration = 0.;
    Vec3d tip { Vec3d::Zero() };
    unsigned int layer = 0;
};
struct ClearanceParams
{
    Nonplanar::HeadEnvelope head;
    double pitch = 0.25;
    double sweep = 0.25;
    double bead_eps = 0.05;
};
struct ClearanceReport
{
    bool ran = false;
    size_t hit_moves = 0;
    double min_clearance = std::numeric_limits<double>::infinity();
    std::vector<float> per_move;
    std::vector<ClearanceHit> worst;
    std::vector<std::string> omissions;
    bool passed() const { return ran && hit_moves == 0; }
};
ClearanceReport verify_clearance(const GCodeProcessorResult &result, const ClearanceParams &params,
                                 const ToolKinematics &kinematics,
                                 const std::function<void()> &throw_if_canceled = {});
void check_export_clearance(Print &print, GCodeProcessorResult &result);
}
