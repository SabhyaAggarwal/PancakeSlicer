#include "ShellSeams.hpp"
#include "HeightField.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <regex>

namespace Slic3r::Nonplanar {

namespace {

ConformalPoint interpolate(const ConformalPoint &a, const ConformalPoint &b, double t)
{
    ConformalPoint p;
    p.pose.tip    = a.pose.tip + t * (b.pose.tip - a.pose.tip);
    p.pose.axis   = (a.pose.axis + t * (b.pose.axis - a.pose.axis)).normalized();
    p.bead_height = a.bead_height + float(t) * (b.bead_height - a.bead_height);
    p.width       = a.width + float(t) * (b.width - a.width);
    return p;
}

void add_breaks(std::vector<double> &breaks, double from, double to, double piece)
{
    if (to <= from)
        return;
    const int count = std::max(1, int(std::ceil((to - from) / piece - 1e-9)));
    for (int m = 0; m <= count; ++m)
        breaks.emplace_back(from + (to - from) * double(m) / count);
}

}

std::vector<ConformalPoint> shape_runs(const std::vector<ConformalPoint> &points, const FlowShape &shape, double min_piece,
                                       std::vector<double> &flow)
{
    const size_t                n = points.size();
    std::vector<ConformalPoint> out;
    flow.clear();
    if (n == 0)
        return out;
    out.reserve(n + 24);
    out.emplace_back(points.front());
    auto extruded = [&points](size_t i) { return points[i].bead_height > 0.f && points[i + 1].bead_height > 0.f; };
    bool first_run = true;
    for (size_t i = 0; i + 1 < n;) {
        if (!extruded(i)) {
            out.emplace_back(points[i + 1]);
            flow.emplace_back(1.);
            ++i;
            continue;
        }
        size_t              j = i;
        std::vector<double> s{ 0. };
        while (j + 1 < n && extruded(j)) {
            s.emplace_back(s.back() + (points[j + 1].pose.tip - points[j].pose.tip).norm());
            ++j;
        }
        const double run  = s.back();
        const double ramp = first_run ? std::min(shape.ramp, 0.25 * run) : 0.;
        const double end   = shape.open_end && j + 1 == n ? 0. : shape.taper + shape.coast;
        const double scale = end > 0.25 * run && end > 0. ? 0.25 * run / end : 1.;
        const double taper = end > 0. ? shape.taper * scale : 0., coast = end > 0. ? shape.coast * scale : 0.;
        first_run          = false;
        auto factor        = [&](double t) {
            double f = ramp > 0. ? std::clamp(t / ramp, 0., 1.) : 1.;
            const double left = run - t;
            if (left < coast)
                f = 0.;
            else if (taper > 0. && left < coast + taper)
                f = std::min(f, (left - coast) / taper);
            return f;
        };
        std::vector<double> breaks;
        add_breaks(breaks, 0., ramp, std::min(ramp, std::max(ramp / 5., min_piece)));
        add_breaks(breaks, run - coast - taper, run - coast, std::min(taper, std::max(taper / 5., min_piece)));
        if (coast > 0.)
            breaks.emplace_back(run - coast);
        std::sort(breaks.begin(), breaks.end());
        size_t b = 0;
        for (size_t k = i; k < j; ++k) {
            const ConformalPoint &pa = points[k], &pb = points[k + 1];
            const double          s0 = s[k - i], s1 = s[k + 1 - i], l = s1 - s0;
            double                at = s0;
            while (b < breaks.size() && breaks[b] <= s0 + EPSILON)
                ++b;
            for (; b < breaks.size() && breaks[b] < s1 - EPSILON; ++b) {
                out.emplace_back(interpolate(pa, pb, (breaks[b] - s0) / l));
                flow.emplace_back(factor(0.5 * (at + breaks[b])));
                at = breaks[b];
            }
            out.emplace_back(pb);
            flow.emplace_back(l > 0. ? factor(0.5 * (at + s1)) : 1.);
        }
        i = j;
    }
    return out;
}

void orient_free_roads(std::vector<const ExtrusionPathConformal *> &order, std::vector<ExtrusionPathConformal> &storage,
                       double min_rise)
{
    storage.clear();
    storage.reserve(order.size());
    const coord_t rise = coord_t(scale_(min_rise));
    for (size_t i = 0; i < order.size(); ++i) {
        const ExtrusionPathConformal &path = *order[i];
        if (path.polyline.points.size() < 2 || !path.tool_axis.empty())
            continue;
        const Point3 &first = path.polyline.points.front(), &last = path.polyline.points.back();
        const bool joined = (i > 0 && order[i - 1]->polyline.points.back().to_point() == first.to_point()) ||
                            (i + 1 < order.size() && order[i + 1]->polyline.points.front().to_point() == last.to_point());
        const bool tucked = i > 0 && (order[i - 1]->polyline.points.back().to_point() - first.to_point()).cast<double>().norm() <
                                         scale_(StartTuck);
        if (joined || tucked || last.z() >= first.z() - rise)
            continue;
        if (!path.bead_height.empty() && (path.bead_height.front() <= 0.f || path.bead_height.back() <= 0.f))
            continue;
        storage.emplace_back(path);
        storage.back().reverse();
        order[i] = &storage.back();
    }
}

double pressure_advance_scale(double height, double nominal_height)
{
    if (height <= 0. || nominal_height <= 0.)
        return 1.;
    return std::clamp(0.75 + 0.25 * std::pow(nominal_height / height, 3.), 0.5, 2.);
}

PressureAdvance parse_pressure_advance(const std::string &gcode)
{
    static const std::regex re(R"(^\s*(M900\b[^;]*?\bK|M572\b[^;]*?\bS|SET_PRESSURE_ADVANCE\b[^;]*?\bADVANCE=)\s*([0-9]*\.?[0-9]+))",
                               std::regex::icase);
    PressureAdvance out;
    size_t          pos = 0;
    while (pos < gcode.size()) {
        const size_t      eol  = gcode.find('\n', pos);
        const std::string line = gcode.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos                    = eol == std::string::npos ? gcode.size() : eol + 1;
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            out.value = std::stod(m[2].str());
            out.command = m[1].str();
        }
    }
    return out;
}

std::string PressureAdvanceTracker::set(double value) const
{
    if (m_base.command.empty())
        return m_writer.set_pressure_advance(value);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", value);
    return m_base.command + buf + " ; pressure advance for the bead height\n";
}

void PressureAdvanceTracker::update(double height, std::string &out)
{
    if (m_base.value < 0. || height <= 0. || m_height <= 0.)
        return;
    if (std::abs(height - m_height) <= PressureAdvanceStep * m_height)
        return;
    m_height = height;
    out += this->set(m_base.value * pressure_advance_scale(height, m_nominal));
    m_changed = true;
    ++m_changes;
}

void PressureAdvanceTracker::restore(std::string &out)
{
    if (!m_changed)
        return;
    out += this->set(m_base.value);
    m_changed = false;
    m_height  = m_nominal;
}

double mean_bead_height(const std::vector<ConformalPoint> &points)
{
    double sum = 0., length = 0.;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (points[i].bead_height > 0.f && points[i + 1].bead_height > 0.f) {
            const double l = (points[i + 1].pose.tip - points[i].pose.tip).norm();
            sum += 0.5 * (points[i].bead_height + points[i + 1].bead_height) * l;
            length += l;
        }
    return length > 0. ? sum / length : 0.;
}

size_t convex_seam(const std::vector<double> &rear, const std::vector<double> &curvature, const std::vector<double> &z)
{
    const size_t n = rear.size();
    std::vector<size_t> pick;
    for (size_t i = 0; i < n; ++i)
        if (rear[i] >= 0.75)
            pick.emplace_back(i);
    if (pick.empty())
        for (size_t i = 0; i < n; ++i)
            pick.emplace_back(i);
    if (pick.empty())
        return 0;
    auto keep = [&pick](auto &&value, double tol) {
        double best = std::numeric_limits<double>::max();
        for (size_t i : pick)
            best = std::min(best, value(i));
        pick.erase(std::remove_if(pick.begin(), pick.end(), [&](size_t i) { return value(i) > best + tol; }), pick.end());
    };
    keep([&](size_t i) { return curvature[i]; }, 0.005);
    keep([&](size_t i) { return z[i]; }, 0.05);
    keep([&](size_t i) { return -rear[i]; }, 0.);
    return pick.front();
}

double meridian_curvature(const HeightField &surface, const Polyline &meridian, double length)
{
    std::vector<double> slope, at;
    double              s = 0.;
    for (size_t i = 0; i + 1 < meridian.points.size() && s < length; ++i) {
        const Vec2d  a = unscale(meridian.points[i]), b = unscale(meridian.points[i + 1]);
        const double l = (b - a).norm();
        const float  za = surface.sample(a), zb = surface.sample(b);
        if (l <= 0. || std::isnan(za) || std::isnan(zb))
            break;
        slope.emplace_back((zb - za) / l);
        at.emplace_back(s + 0.5 * l);
        s += l;
    }
    if (slope.size() < 2)
        return 0.;
    double sum = 0.;
    for (size_t i = 1; i < slope.size(); ++i)
        sum += std::abs(slope[i] - slope[i - 1]) / std::max(at[i] - at[i - 1], EPSILON);
    return sum / double(slope.size() - 1);
}

}
