#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Nonplanar/LayerField.hpp"
#include "libslic3r/Nonplanar/HeadEnvelope.hpp"
#include "libslic3r/Nonplanar/LayerFieldOpt.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Slic3r;

static indexed_triangle_set deform_mesh(const indexed_triangle_set &mesh, const Nonplanar::LayerField &field, double max_edge)
{
    indexed_triangle_set out = mesh;
    const float max2 = float(max_edge * max_edge);
    for (int pass = 0; pass < 12; ++pass) {
        std::unordered_map<uint64_t, int> mid;
        auto split = [&](int a, int b) -> int {
            if ((out.vertices[a] - out.vertices[b]).squaredNorm() <= max2)
                return -1;
            const uint64_t key = (uint64_t(std::min(a, b)) << 32) | uint32_t(std::max(a, b));
            auto [it, fresh] = mid.emplace(key, int(out.vertices.size()));
            if (fresh)
                out.vertices.push_back(0.5f * (out.vertices[a] + out.vertices[b]));
            return it->second;
        };
        std::vector<stl_triangle_vertex_indices> next;
        next.reserve(out.indices.size());
        bool changed = false;
        for (const stl_triangle_vertex_indices f : out.indices) {
            const int v[3] = { f[0], f[1], f[2] };
            const int m[3] = { split(v[0], v[1]), split(v[1], v[2]), split(v[2], v[0]) };
            const int cnt  = (m[0] >= 0) + (m[1] >= 0) + (m[2] >= 0);
            if (cnt == 0) {
                next.push_back(f);
                continue;
            }
            changed = true;
            if (cnt == 3) {
                next.emplace_back(v[0], m[0], m[2]);
                next.emplace_back(m[0], v[1], m[1]);
                next.emplace_back(m[2], m[1], v[2]);
                next.emplace_back(m[0], m[1], m[2]);
                continue;
            }
            const int r = cnt == 1 ? (m[0] >= 0 ? 0 : m[1] >= 0 ? 1 : 2) : ((m[0] < 0 ? 0 : m[1] < 0 ? 1 : 2) + 1) % 3;
            const int a = v[r], b = v[(r + 1) % 3], c = v[(r + 2) % 3], mab = m[r], mbc = m[(r + 1) % 3];
            if (cnt == 1) {
                next.emplace_back(a, mab, c);
                next.emplace_back(mab, b, c);
            } else {
                next.emplace_back(a, mab, c);
                next.emplace_back(mab, mbc, c);
                next.emplace_back(mab, b, mbc);
            }
        }
        out.indices = std::move(next);
        if (!changed)
            break;
    }
    for (Vec3f &v : out.vertices)
        v.z() = float(field.phi(v.cast<double>()));
    return out;
}

static bool inside(const indexed_triangle_set &its, const Vec3d &p)
{
    int crossings = 0;
    for (const stl_triangle_vertex_indices &f : its.indices) {
        const Vec3d a = its.vertices[f[0]].cast<double>(), b = its.vertices[f[1]].cast<double>(), c = its.vertices[f[2]].cast<double>();
        const double d = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
        if (std::abs(d) < 1e-12)
            continue;
        const double w1 = ((p.x() - a.x()) * (c.y() - a.y()) - (p.y() - a.y()) * (c.x() - a.x())) / d;
        const double w2 = ((b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x())) / d;
        if (w1 < 0. || w2 < 0. || w1 + w2 > 1.)
            continue;
        if (a.z() + w1 * (b.z() - a.z()) + w2 * (c.z() - a.z()) > p.z())
            ++crossings;
    }
    return crossings % 2 == 1;
}

struct AxisChunk
{
    Vec3d lo { Vec3d::Zero() }, hi { Vec3d::Zero() }, axis { Vec3d::UnitZ() };
    bool  contains(const Vec3d &p) const { return (p.array() >= lo.array()).all() && (p.array() <= hi.array()).all(); }
};

static void print_metrics(const char *name, const Nonplanar::LayerFieldMetrics &m, bool last)
{
    std::printf("  \"%s\": {\"curved_top_area\": %.1f, \"curved_top_in_cap\": %.4f, \"conformal_top\": %.4f, "
                "\"top_rms_angle\": %.2f, \"overhang_area\": %.1f, \"max_overhang\": %.1f, \"max_field_slope\": %.2f, "
                "\"field_slope_p99\": %.2f, \"slope_cap\": %.2f, \"min_thickness\": %.3f, \"max_thickness\": %.3f, "
                "\"head_violation_area\": %.1f, \"head_layers_checked\": %d, \"support_estimate\": %.1f, \"support_area_h\": %.1f, "
                "\"support_perim_h\": %.1f, \"support_region_h\": %.1f, \"support_regions\": %d, \"axis_dev_mean\": %.2f, "
                "\"axis_dev_p95\": %.2f, \"axis_within_3\": %.3f}%s\n",
                name, m.curved_top_area, m.curved_top_in_cap, m.conformal_top, m.top_rms_angle, m.overhang_area, m.max_overhang,
                m.max_field_slope * 180. / PI, m.field_slope_p99 * 180. / PI, m.slope_cap * 180. / PI, m.min_thickness,
                m.max_thickness, m.head_violation_area, m.head_layers_checked, m.support_estimate, m.support.area_height,
                m.support.perimeter_height, m.support.region_height, m.support.regions, m.axis_dev_mean, m.axis_dev_p95, m.axis_within_3,
                last ? "" : ",");
}

int main(int argc, char **argv)
{
    std::string resources = "resources", head_id, model, deformed, deformed_planar, support_gcode;
    Vec2d bed_center = Vec2d::Zero();
    std::vector<AxisChunk> chunks;
    bool  planar_only = false;
    Nonplanar::LayerFieldParams params;
    int head_check = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error(a + " needs a value");
            return argv[++i];
        };
        if (a == "--resources") resources = next();
        else if (a == "--head") head_id = next();
        else if (a == "--layer-height") params.layer_height = std::stod(next());
        else if (a == "--overhang") params.overhang_angle = std::stod(next());
        else if (a == "--max-slope") params.max_slope = std::stod(next()) * PI / 180.;
        else if (a == "--head-check") head_check = std::stoi(next());
        else if (a == "--w-conform") params.w_conform = std::stod(next());
        else if (a == "--support-angle") params.support_angle = std::stod(next());
        else if (a == "--w-xy") params.w_smooth_xy = std::stod(next());
        else if (a == "--flat-base") params.flat_base_height = std::stod(next());
        else if (a == "--axis-chunks") {
            std::stringstream ss(next());
            for (std::string item; std::getline(ss, item, ';');) {
                std::stringstream is(item);
                AxisChunk c;
                double *v[9] = { &c.lo.x(), &c.hi.x(), &c.lo.y(), &c.hi.y(), &c.lo.z(), &c.hi.z(), &c.axis.x(), &c.axis.y(), &c.axis.z() };
                for (int k = 0; k < 9; ++k) {
                    std::string t;
                    std::getline(is, t, ':');
                    *v[k] = std::stod(t);
                }
                c.axis.normalize();
                chunks.push_back(c);
            }
        }
        else if (a == "--w-overhang") params.w_overhang = std::stod(next());
        else if (a == "--deformed") deformed = next();
        else if (a == "--deformed-planar") deformed_planar = next();
        else if (a == "--support-gcode") support_gcode = next();
        else if (a == "--planar") planar_only = true;
        else if (a == "--bed-center") {
            const std::string v = next();
            bed_center = Vec2d(std::stod(v.substr(0, v.find(','))), std::stod(v.substr(v.find(',') + 1)));
        }
        else model = a;
    }
    if (model.empty()) {
        std::cerr << "usage: nonplanar_layer_field_tool [--resources DIR] [--head ID] [--head-check N] [--deformed OUT.stl] MODEL.stl\n";
        return 2;
    }
    set_resources_dir(resources);
    Nonplanar::HeadEnvelope head;
    if (!head_id.empty()) {
        const Nonplanar::HeadEnvelope *h = Nonplanar::HeadLibrary::instance().find(head_id);
        if (!h) {
            std::cerr << "unknown head " << head_id << "\n";
            return 2;
        }
        head = *h;
        params.head = &head;
    }
    if (!chunks.empty())
        params.tool_axis = [&chunks](const Vec3d &p) {
            for (const AxisChunk &c : chunks)
                if (c.contains(p))
                    return c.axis;
            return Vec3d(Vec3d::UnitZ());
        };
    TriangleMesh mesh;
    if (!mesh.ReadSTLFile(model.c_str()))
        throw std::runtime_error("cannot read " + model);
    mesh.translate(0.f, 0.f, -mesh.bounding_box().min.z());
    if (!support_gcode.empty()) {
        const Nonplanar::StackLayerField f = planar_only ? Nonplanar::StackLayerField::planar(mesh.bounding_box()) :
                                                      Nonplanar::solve_layer_field(mesh.its, params);
        const BoundingBoxf3 bb = mesh.bounding_box();
        const Vec2d shift = Vec2d(bb.center().x(), bb.center().y()) - bed_center;
        std::ifstream in(support_gcode);
        std::string line, feature;
        double x = 0., y = 0., zl = 0., vol = 0., vol_deformed = 0.;
        bool relative = true;
        const double area = PI * 0.875 * 0.875;
        auto num = [&](char c, double &v) {
            const size_t i = line.find(std::string(" ") + c);
            if (i == std::string::npos) return false;
            v = std::atof(line.c_str() + i + 2);
            return true;
        };
        while (std::getline(in, line)) {
            if (line.rfind("; FEATURE:", 0) == 0) feature = line.substr(10);
            else if (line.rfind(";TYPE:", 0) == 0) feature = line.substr(6);
            else if (line.rfind("; Z_HEIGHT:", 0) == 0) zl = std::atof(line.c_str() + 11);
            else if (line.rfind(";Z:", 0) == 0) zl = std::atof(line.c_str() + 3);
            else if (line.rfind("M83", 0) == 0) relative = true;
            else if (line.rfind("M82", 0) == 0) relative = false;
            else if (line.rfind("G1 ", 0) == 0 || line.rfind("G2 ", 0) == 0 || line.rfind("G3 ", 0) == 0) {
                double e = 0.;
                num('X', x); num('Y', y);
                if (relative && num('E', e) && e > 0. && feature.find("upport") != std::string::npos) {
                    const Vec2d p = Vec2d(x, y) + shift;
                    const double s = std::max(0.1, zl - 0.5 * params.layer_height);
                    const double r = (f.z_at(p, s + 0.5 * params.layer_height) - f.z_at(p, s - 0.5 * params.layer_height)) / params.layer_height;
                    vol += e * area * r;
                    vol_deformed += e * area;
                }
            }
        }
        std::printf("{\"support_mm3\": %.1f, \"support_deformed_mm3\": %.1f}\n", vol, vol_deformed);
        return 0;
    }
    Nonplanar::LayerFieldReport report;
    const Nonplanar::StackLayerField field = Nonplanar::solve_layer_field(mesh.its, params, &report);
    const auto t0 = std::chrono::steady_clock::now();
    report.planar = Nonplanar::evaluate_layer_field(mesh.its, Nonplanar::StackLayerField::planar(mesh.bounding_box()), params, 0.5, head_check);
    report.field  = Nonplanar::evaluate_layer_field(mesh.its, field, params, 0.5, head_check);
    const double eval_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("{\n  \"model\": \"%s\", \"solve_seconds\": %.3f, \"eval_seconds\": %.2f, \"columns\": %d, \"levels\": %d,\n",
                model.c_str(), report.solve_seconds, eval_s, report.columns, report.levels);
    print_metrics("planar", report.planar, false);
    print_metrics("field", report.field, false);
    for (size_t ci = 0; ci < chunks.size(); ++ci) {
        AxisChunk c = chunks[ci];
        c.lo += Vec3d::Constant(1.5);
        c.hi -= Vec3d::Constant(1.5);
        std::vector<double> dev;
        for (double z = c.lo.z() + 0.5; z < c.hi.z(); z += 1.)
            for (double y = c.lo.y() + 0.5; y < c.hi.y(); y += 1.)
                for (double x = c.lo.x() + 0.5; x < c.hi.x(); x += 1.)
                    if (inside(mesh.its, Vec3d(x, y, z)))
                        dev.push_back(std::acos(std::clamp(std::abs(field.gradient(Vec3d(x, y, z)).normalized().dot(c.axis)), 0., 1.)) * 180. / PI);
        std::sort(dev.begin(), dev.end());
        double sum = 0.;
        for (double d : dev)
            sum += d;
        const size_t within = size_t(std::lower_bound(dev.begin(), dev.end(), 3.) - dev.begin());
        std::printf("  \"chunk_%zu\": {\"samples\": %zu, \"dev_mean\": %.2f, \"dev_p95\": %.2f, \"dev_max\": %.2f, \"within_3\": %.3f},\n", ci,
                    dev.size(), dev.empty() ? 0. : sum / dev.size(), dev.empty() ? 0. : dev[size_t(0.95 * (dev.size() - 1))],
                    dev.empty() ? 0. : dev.back(), dev.empty() ? 0. : double(within) / dev.size());
    }
    std::string log = report.log;
    for (char &c : log)
        if (c == '\n') c = '|';
    std::printf("  \"log\": \"%s\"\n}\n", log.c_str());
    const double max_edge = 2. * field.pitch() / 2.;
    if (!deformed.empty())
        TriangleMesh(deform_mesh(mesh.its, field, max_edge)).write_binary(deformed.c_str());
    if (!deformed_planar.empty())
        TriangleMesh(deform_mesh(mesh.its, Nonplanar::StackLayerField::planar(mesh.bounding_box()), max_edge))
            .write_binary(deformed_planar.c_str());
    return 0;
}
