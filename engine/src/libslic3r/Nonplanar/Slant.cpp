#include "Slant.hpp"
#include "HeadEnvelope.hpp"
#include "Multiplanar.hpp"
#include "libslic3r/GCode/ClearanceRepair.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace Slic3r::Nonplanar {

SlantField::SlantField(const SlantShape &shape) : m_s(shape)
{
    m_k0  = (m_s.start - m_s.first) / m_s.h;
    m_dt  = m_s.tilt / std::max(1, m_s.morph);
    m_low = m_s.h - 0.5 * m_dt * m_s.width;
}

double SlantField::s_of(const Vec2d &xy) const { return std::clamp(m_s.u_max - m_s.u.dot(xy), 0., m_s.width); }

double SlantField::z_of(double k, double s) const
{
    if (k <= m_k0)
        return m_s.first + k * m_s.h;
    const double j = std::min(k - m_k0, double(m_s.morph));
    const double z = m_s.start + j * (m_low + m_dt * s);
    return k - m_k0 <= m_s.morph ? z : z + (k - m_k0 - m_s.morph) * m_s.h;
}

double SlantField::phi(const Vec3d &p) const
{
    const double s = s_of(p.head<2>()), z = p.z();
    double       k;
    if (z <= m_s.start)
        k = (z - m_s.first) / m_s.h;
    else if (const double top = z_of(m_k0 + m_s.morph, s); z <= top)
        k = m_k0 + (z - m_s.start) / (m_low + m_dt * s);
    else
        k = m_k0 + m_s.morph + (z - top) / m_s.h;
    return m_s.first + k * m_s.h;
}

double SlantField::z_at(const Vec2d &xy, double level) const { return z_of((level - m_s.first) / m_s.h, s_of(xy)); }

namespace {

constexpr double MinTilt    = 2. * M_PI / 180.;
constexpr double SlopeMargin = 3. * M_PI / 180.;
constexpr int    Directions = 8;
constexpr double StepWeight = 0.5;

double slant_budget(const HeadEnvelope &head, double psi, double width, double h)
{
    const double rise = psi + M_PI;
    double       tilt = head.max_slope(rise, h, width);
    HeadEnvelope body = head;
    body.boxes.erase(std::remove_if(body.boxes.begin(), body.boxes.end(), [](const HeadBox &b) { return b.nozzle; }), body.boxes.end());
    if (!body.boxes.empty())
        tilt = std::min(tilt, body.max_slope(rise, 0., width));
    if (const double gantry = head.gantry_clearance(); std::isfinite(gantry))
        tilt = std::min(tilt, std::atan(std::max(0., gantry - head.margin) / width));
    return tilt - SlopeMargin;
}

struct Score
{
    double steps = 0., overhang = 0.;
    double total() const { return overhang + StepWeight * steps; }
};

Score score(const indexed_triangle_set &mesh, const LayerField &field, double h, double first, double sin_threshold)
{
    Score out;
    for (const stl_triangle_vertex_indices &t : mesh.indices) {
        const std::array<Vec3d, 3> v { mesh.vertices[t[0]].cast<double>(), mesh.vertices[t[1]].cast<double>(), mesh.vertices[t[2]].cast<double>() };
        const Vec3d  cr   = (v[1] - v[0]).cross(v[2] - v[0]);
        const double area = 0.5 * cr.norm();
        if (area <= 0.)
            continue;
        const Vec3d n = cr.normalized(), centroid = (v[0] + v[1] + v[2]) / 3.;
        const Vec3d g = field.gradient(centroid).normalized();
        const double c = n.dot(g);
        if (c < -sin_threshold) {
            if (std::max({ v[0].z(), v[1].z(), v[2].z() }) > first + h)
                out.overhang += area;
            continue;
        }
        if (c <= 0.)
            continue;
        std::array<double, 7> s;
        for (int i = 0; i < 3; ++i)
            s[i] = (field.phi(v[i]) - first) / h;
        s[3] = (field.phi(0.5 * (v[0] + v[1])) - first) / h;
        s[4] = (field.phi(0.5 * (v[1] + v[2])) - first) / h;
        s[5] = (field.phi(0.5 * (v[2] + v[0])) - first) / h;
        s[6] = (field.phi(centroid) - first) / h;
        const auto [lo, hi] = std::minmax_element(s.begin(), s.begin() + 3);
        double e2 = 1. / 12.;
        if (*hi - *lo < 1.) {
            e2 = 0.;
            for (double x : s)
                e2 += (x - std::round(x)) * (x - std::round(x)) / 7.;
        }
        out.steps += area * c * c * e2 * 12.;
    }
    return out;
}

class FlatField final : public LayerField
{
public:
    double phi(const Vec3d &p) const override { return p.z(); }
    double z_at(const Vec2d &, double level) const override { return level; }
    Vec3d  gradient(const Vec3d &) const override { return Vec3d::UnitZ(); }
};

}

std::shared_ptr<const LayerField> slant_field(const PrintObject &object, SlantChoice &choice)
{
    choice = SlantChoice{};
    const PrintObjectConfig &config = object.config();
    const PrintConfig       &print  = object.print()->config();
    const indexed_triangle_set mesh = slicing_frame_mesh(object);
    if (mesh.vertices.empty())
        return {};
    const double h      = config.layer_height.value;
    const double first  = print.initial_layer_print_height.value;
    const double sin_th = std::sin(0.5 * M_PI - Geometry::deg2rad(config.multiplanar_overhang_threshold.value));
    const double nozzle = print.nozzle_diameter.get_at(0);
    const double lo_h   = print.min_layer_height.get_at(0) > 0. ? print.min_layer_height.get_at(0) : 0.2 * nozzle;
    const double hi_h   = print.max_layer_height.get_at(0) > 0. ? print.max_layer_height.get_at(0) : 0.75 * nozzle;
    const double spread = std::max(1e-3, std::min(h - lo_h, hi_h - h));
    const HeadEnvelope head = resolve_head_envelope(print);
    {
        const Score planar     = score(mesh, FlatField(), h, first, sin_th);
        choice.planar_steps    = planar.steps;
        choice.planar_overhang = planar.overhang;
    }
    const double start = std::max(first, config.nonplanar_slant_start_height.value);
    const int    step  = slant_step(*object.model_object());
    const double scale = std::pow(0.75, step);
    if (step >= SlantSteps) {
        std::ostringstream why;
        why << "its tilted layers met the print head in " << slant_step_hits(*object.model_object()) << " moves at every angle tried";
        choice.planar_limit = "access";
        choice.reason       = why.str();
        choice.steps        = choice.planar_steps;
        choice.overhang     = choice.planar_overhang;
        return {};
    }

    struct Option
    {
        SlantShape shape;
        double     budget = 0.;
        Score      s;
    };
    std::vector<Option> options;
    double              best_budget = 0.;
    const double        direction   = config.nonplanar_slant_direction.value;
    const int           n_dir       = direction >= 0. ? 1 : Directions;
    for (int k = 0; k < n_dir; ++k) {
        const double psi = direction >= 0. ? Geometry::deg2rad(direction) : 2. * M_PI * k / Directions;
        Option       o;
        SlantShape  &sh = o.shape;
        sh.u            = Vec2d(std::cos(psi), std::sin(psi));
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const stl_vertex &v : mesh.vertices) {
            lo = std::min(lo, sh.u.dot(v.head<2>().cast<double>()));
            hi = std::max(hi, sh.u.dot(v.head<2>().cast<double>()));
        }
        sh.u_max    = hi;
        sh.width    = std::max(hi - lo, EPSILON);
        o.budget    = slant_budget(head, psi, sh.width, h);
        best_budget = std::max(best_budget, o.budget);
        const double alpha = scale * (config.nonplanar_slant_angle.value > 0. ? std::min(Geometry::deg2rad(config.nonplanar_slant_angle.value), o.budget) : o.budget);
        if (alpha < MinTilt)
            continue;
        sh.tilt  = std::tan(alpha);
        sh.first = first;
        sh.start = start;
        sh.h     = h;
        sh.morph = std::max(std::max(1, config.nonplanar_slant_morph_layers.value), int(std::ceil(sh.tilt * sh.width / (2. * spread) - 1e-9)));
        o.s      = score(mesh, SlantField(sh), h, first, sin_th);
        options.push_back(o);
    }
    if (options.empty()) {
        std::ostringstream why;
        why << "the print head clears no layer tilted more than " << std::lround(Geometry::rad2deg(MinTilt)) << " deg over this part (at most "
            << std::fixed << std::setprecision(1) << Geometry::rad2deg(std::max(0., best_budget)) << " deg)";
        choice.budget_deg   = Geometry::rad2deg(std::max(0., best_budget));
        choice.planar_limit = "access";
        choice.reason       = why.str();
        choice.steps        = choice.planar_steps;
        choice.overhang     = choice.planar_overhang;
        return {};
    }
    const Option &o = *std::min_element(options.begin(), options.end(), [](const Option &a, const Option &b) {
        return a.s.total() < b.s.total() - 1e-9 || (std::abs(a.s.total() - b.s.total()) <= 1e-9 && a.shape.tilt > b.shape.tilt);
    });
    const SlantShape &sh = o.shape;
    const double      dt = sh.tilt / sh.morph;
    choice.angle_deg     = Geometry::rad2deg(std::atan(sh.tilt));
    choice.direction_deg = Geometry::rad2deg(std::atan2(sh.u.y(), sh.u.x()));
    if (choice.direction_deg < 0.)
        choice.direction_deg += 360.;
    choice.budget_deg   = Geometry::rad2deg(o.budget);
    choice.start_mm     = sh.start;
    choice.morph_layers = sh.morph;
    choice.lowered      = step;
    choice.min_gap_mm   = sh.h - 0.5 * dt * sh.width;
    choice.max_gap_mm   = sh.h + 0.5 * dt * sh.width;
    choice.full_tilt_mm = sh.start + sh.morph * choice.min_gap_mm;
    choice.steps        = o.s.steps;
    choice.overhang     = o.s.overhang;
    BOOST_LOG_TRIVIAL(info) << "Slant " << object.model_object()->name << ": " << choice.angle_deg << " deg toward " << choice.direction_deg
                            << " deg (head limit " << choice.budget_deg << "), flat to " << choice.start_mm << " mm, " << choice.morph_layers
                            << " morph layers (gaps " << choice.min_gap_mm << ".." << choice.max_gap_mm << " mm), full tilt from "
                            << choice.full_tilt_mm << " mm at the low edge; stair-step " << choice.steps << " mm2 against "
                            << choice.planar_steps << " flat, overhang " << choice.overhang << " against " << choice.planar_overhang;
    return std::make_shared<const SlantField>(sh);
}

}
