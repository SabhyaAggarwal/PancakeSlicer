#include "ShellSeams.hpp"
#include "libslic3r/GCode/ConformalEmit.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>
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
        const bool   soft  = shape.scored || run < CoastMinRun;
        const double taper = end > 0. ? (soft ? end : shape.taper) * scale : 0.;
        const double coast = end > 0. && !soft ? shape.coast * scale : 0.;
        first_run          = false;
        auto factor        = [&](double t) {
            double f = ramp > 0. ? std::clamp(t / ramp, 0., 1.) : 1.;
            const double left = run - t;
            if (left < coast)
                f = 0.;
            else if (taper > 0. && left < coast + taper)
                f = std::min(f, soft ? std::max(std::pow(left / taper, 3.), SoftTaperFloor) : (left - coast) / taper);
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
                       double min_rise, bool chain_inside)
{
    storage.clear();
    storage.reserve(order.size());
    const coord_t rise = coord_t(scale_(min_rise));
    for (size_t i = 0; i < order.size(); ++i) {
        const ExtrusionPathConformal &path = *order[i];
        if (path.preserve_points || path.polyline.points.size() < 2 || !path.tool_axis.empty())
            continue;
        const Point3 &first = path.polyline.points.front(), &last = path.polyline.points.back();
        const bool joined = (i > 0 && order[i - 1]->polyline.points.back().to_point() == first.to_point()) ||
                            (i + 1 < order.size() && order[i + 1]->polyline.points.front().to_point() == last.to_point());
        const bool tucked = i > 0 && (order[i - 1]->polyline.points.back().to_point() - first.to_point()).cast<double>().norm() <
                                         scale_(StartTuck);
        const bool loop = (last.to_point() - first.to_point()).cast<double>().norm() <= 1.5 * scale_(path.width);
        if (joined || tucked || loop || last.z() >= first.z() - rise)
            continue;
        if (chain_inside && i > 0 && (path.role() == erInternalInfill || path.role() == erSolidInfill || path.role() == erPerimeter)) {
            const Point from = order[i - 1]->polyline.points.back().to_point();
            if ((last.to_point() - from).cast<double>().squaredNorm() > (first.to_point() - from).cast<double>().squaredNorm())
                continue;
        }
        if (!path.bead_height.empty() && (path.bead_height.front() <= 0.f || path.bead_height.back() <= 0.f))
            continue;
        storage.emplace_back(path);
        storage.back().reverse();
        order[i] = &storage.back();
    }
}

void order_pass_roads(std::vector<const ExtrusionPathConformal *> &order, size_t begin, size_t end,
                      std::deque<ExtrusionPathConformal> &storage)
{
    if (end < begin + 3)
        return;
    struct Unit
    {
        size_t begin, end;
        Vec2d  a, b;
        bool   flippable;
        bool   flipped = false;
        Vec2d  start() const { return flipped ? b : a; }
        Vec2d  last() const { return flipped ? a : b; }
    };
    std::vector<Unit> units;
    for (size_t i = begin; i < end; ++i) {
        const ExtrusionPathConformal &road = *order[i];
        if (!units.empty() && order[i - 1]->polyline.points.back() == road.polyline.points.front()) {
            units.back().end       = i + 1;
            units.back().b         = unscale(road.polyline.points.back().to_point());
            units.back().flippable = false;
            continue;
        }
        const bool open = road.polyline.points.size() >= 2 && road.polyline.points.front() != road.polyline.points.back();
        const bool ends = road.bead_height.empty() || (road.bead_height.front() > 0.f && road.bead_height.back() > 0.f);
        units.push_back({ i, i + 1, unscale(road.polyline.points.front().to_point()), unscale(road.polyline.points.back().to_point()),
                          open && ends && road.can_reverse() && !road.preserve_points && road.tool_axis.empty() });
    }
    const size_t pinned = end < order.size() && order[end - 1]->polyline.points.back() == order[end]->polyline.points.front() ? 1 : 0;
    const size_t n      = units.size();
    if (n < 3 || n > 600)
        return;
    std::vector<size_t> seq(n);
    for (size_t i = 0; i < n; ++i)
        seq[i] = i;
    auto gap = [&units](size_t from, size_t to) { return (units[to].start() - units[from].last()).norm(); };
    constexpr double Gain = 0.1;
    for (int sweep = 0; sweep < 10; ++sweep) {
        bool improved = false;
        for (size_t len = 1; len <= 3; ++len)
            for (size_t i = 1; i + len + pinned <= seq.size(); ++i) {
                const size_t first = seq[i], last = seq[i + len - 1];
                const size_t size = seq.size() - len;
                auto rest = [&](size_t k) { return k < i ? seq[k] : seq[k + len]; };
                auto added = [&](size_t k) {
                    const size_t p = rest(k - 1);
                    return gap(p, first) + (k < size ? gap(last, rest(k)) - gap(p, rest(k)) : 0.);
                };
                double best   = added(i) - Gain;
                size_t best_k = size_t(-1);
                bool   best_flip = false;
                for (const bool flip : { false, true }) {
                    if (flip && (len > 1 || !units[first].flippable))
                        continue;
                    if (flip)
                        units[first].flipped = !units[first].flipped;
                    for (size_t k = 1; k + pinned <= size; ++k)
                        if ((flip || k != i) && added(k) < best)
                            best = added(k), best_k = k, best_flip = flip;
                    if (flip)
                        units[first].flipped = !units[first].flipped;
                }
                if (best_k == size_t(-1))
                    continue;
                if (best_flip)
                    units[first].flipped = !units[first].flipped;
                if (best_k != i) {
                    if (best_k < i)
                        std::rotate(seq.begin() + best_k, seq.begin() + i, seq.begin() + i + len);
                    else
                        std::rotate(seq.begin() + i, seq.begin() + i + len, seq.begin() + best_k + len);
                }
                improved = true;
            }
        if (!improved)
            break;
    }
    std::vector<const ExtrusionPathConformal *> out;
    out.reserve(end - begin);
    for (const size_t u : seq) {
        const Unit &unit = units[u];
        if (unit.flipped) {
            storage.emplace_back(*order[unit.begin]);
            storage.back().reverse();
            out.emplace_back(&storage.back());
        } else
            out.insert(out.end(), order.begin() + unit.begin, order.begin() + unit.end);
    }
    std::copy(out.begin(), out.end(), order.begin() + begin);
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

bool scarf_curved_loop(ExtrusionPathConformal &path, const CurvedScarf &scarf, const std::function<Vec2d(const Vec2d &)> &gradient)
{
    const Points3 &pts = path.polyline.points;
    const size_t   n   = pts.size();
    if (n < 4 || scarf.length <= 0. || !path.tool_axis.empty() || path.bead_height.size() != n ||
        (!path.bead_width.empty() && path.bead_width.size() != n))
        return false;
    for (float h : path.bead_height)
        if (h <= 0.f)
            return false;
    auto xy = [&pts](size_t i) { return unscale(pts[i].to_point()); };
    if ((xy(n - 1) - xy(0)).norm() > 1.5 * path.width)
        return false;
    std::vector<double> s(n, 0.);
    double turn = 0., max_turn = 0.;
    for (size_t i = 1; i < n; ++i)
        s[i] = s[i - 1] + (xy(i) - xy(i - 1)).norm();
    for (size_t i = 1; i + 1 < n; ++i) {
        const Vec2d a = xy(i) - xy(i - 1), b = xy(i + 1) - xy(i);
        if (a.squaredNorm() > 0. && b.squaredNorm() > 0.)
            turn += std::atan2(a.x() * b.y() - a.y() * b.x(), a.dot(b));
    }
    if (std::abs(turn) < 1.6 * PI || std::abs(turn) > 2.4 * PI || s.back() < 4. * scarf.nozzle)
        return false;
    if (scarf.max_turn >= 0.) {
        auto at = [&](double t) {
            t        = std::clamp(t, 0., s.back());
            size_t k = size_t(std::upper_bound(s.begin(), s.end(), t) - s.begin());
            k        = std::clamp<size_t>(k, 1, n - 1);
            const double l = s[k] - s[k - 1];
            return Vec2d(xy(k - 1) + (l > 0. ? (t - s[k - 1]) / l : 0.) * (xy(k) - xy(k - 1)));
        };
        for (size_t i = 0; i < n; ++i) {
            const Vec2d a = at(s[i]) - at(s[i] - scarf.nozzle), b = at(s[i] + scarf.nozzle) - at(s[i]);
            if (a.squaredNorm() > 0. && b.squaredNorm() > 0.)
                max_turn = std::max(max_turn, std::abs(std::atan2(a.x() * b.y() - a.y() * b.x(), a.dot(b))));
        }
        if (max_turn > scarf.max_turn)
            return false;
    }
    const double length = std::min(scarf.length, s.back());
    const double share0 = std::clamp(scarf.start_share, CurvedScarfMinShare, 1.);
    Points3            ramp;
    std::vector<float> ramp_h, ramp_w;
    std::vector<double> ramp_s;
    for (size_t i = 0; i < n && s[i] <= length; ++i) {
        ramp.emplace_back(pts[i]);
        ramp_h.emplace_back(path.bead_height[i]);
        ramp_w.emplace_back(path.bead_width.empty() ? 0.f : path.bead_width[i]);
        ramp_s.emplace_back(s[i]);
    }
    const size_t m = ramp.size();
    if (m < n && ramp_s.back() < length - EPSILON) {
        const double t = (length - s[m - 1]) / (s[m] - s[m - 1]);
        ramp.emplace_back((pts[m - 1].cast<double>() + t * (pts[m] - pts[m - 1]).cast<double>()).cast<coord_t>());
        ramp_h.emplace_back(float(path.bead_height[m - 1] + t * (path.bead_height[m] - path.bead_height[m - 1])));
        ramp_w.emplace_back(path.bead_width.empty() ? 0.f : float(path.bead_width[m - 1] + t * (path.bead_width[m] - path.bead_width[m - 1])));
        ramp_s.emplace_back(length);
    }
    auto share = [&](double d) { return share0 + (1. - share0) * std::min(1., d / length); };
    Points3            out_pts;
    std::vector<float> out_h, out_w;
    auto push = [&](const Point3 &p, float h, float w) {
        if (!out_pts.empty() && out_pts.back() == p)
            return;
        out_pts.emplace_back(p);
        out_h.emplace_back(h);
        out_w.emplace_back(w);
    };
    for (size_t k = 0; k < ramp.size(); ++k) {
        const double f    = share(ramp_s[k]);
        const double drop = (1. - f) * ramp_h[k];
        Vec2d        g    = gradient ? gradient(unscale(ramp[k].to_point())) : Vec2d::Zero();
        if (std::isnan(g.x()) || std::isnan(g.y()))
            g = Vec2d::Zero();
        const double c = 1. / std::sqrt(1. + g.squaredNorm());
        const Vec3d d = drop * c * c * Vec3d(g.x(), g.y(), -1.);
        push(Point3(ramp[k] + Vec3d(scale_(d.x()), scale_(d.y()), scale_(d.z())).cast<coord_t>()), float(f * ramp_h[k]), ramp_w[k]);
    }
    for (size_t i = m; i < n; ++i)
        push(pts[i], path.bead_height[i], path.bead_width.empty() ? 0.f : path.bead_width[i]);
    for (size_t k = 0; k < ramp.size(); ++k)
        push(ramp[k], float(std::max(SoftTaperFloor, 1. - share(ramp_s[k])) * ramp_h[k]), ramp_w[k]);
    path.polyline.points = std::move(out_pts);
    path.bead_height     = std::move(out_h);
    if (!path.bead_width.empty())
        path.bead_width = std::move(out_w);
    return true;
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
