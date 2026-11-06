#include "RegionDetect.hpp"

#include "HeadEnvelope.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace Slic3r::Nonplanar {
namespace {
constexpr double pi = 3.14159265358979323846;
struct Limits { double slope, span; };

Limits head_limits(const HeadEnvelope &head)
{
    double floor = std::numeric_limits<double>::infinity();
    for (const HeadBox &box : head.boxes)
        if (!box.nozzle && box.min.z() > 0.)
            floor = std::min(floor, box.min.z());
    double angle = 0.;

    const auto ext = nlohmann::json::parse(head.extensions, nullptr, false);
    if (ext.is_object() && ext.contains("coverage")) {
        const auto &coverage = ext["coverage"];
        if (coverage.contains("nozzle_cone")) {
            const auto &cone = coverage["nozzle_cone"];
            const double half = cone.value("half_angle_deg", 90.);
            const double top = cone.value("top_z", 0.);
            if (half > 0. && half < 90. && top > head.margin)
                angle = std::atan2(top - head.margin, top * std::tan(half * pi / 180.));
        }
        if (coverage.contains("duct_limits"))
            floor = std::min(floor, coverage["duct_limits"].value("z_min", floor));
    }
    if (angle == 0. && std::isfinite(floor)) {


        std::map<double, double> radii;
        for (const HeadBox &box : head.boxes) {
            if (!box.nozzle || box.max.z() <= 0. || box.max.z() > floor + 1e-6) continue;
            const double x = std::max(std::abs(box.min.x()), std::abs(box.max.x()));
            const double y = std::max(std::abs(box.min.y()), std::abs(box.max.y()));
            radii[box.max.z()] = std::max(radii[box.max.z()], std::hypot(x, y));
        }
        std::vector<double> growth;
        for (auto i = radii.begin(); i != radii.end(); ++i) {
            auto next = std::next(i);
            if (next != radii.end() && next->second > i->second + 1e-6)
                growth.push_back((next->second-i->second) / (next->first-i->first));
        }
        if (!growth.empty() && floor > head.margin) {
            std::sort(growth.begin(), growth.end());
            angle = std::atan((1.-head.margin/floor) / growth[growth.size()/2]);
        }
    }
    if (angle == 0.) {


        angle = pi / 2.;
        for (int i = 0; i < 32; ++i)
            angle = std::min(angle, head.max_slope(2. * pi * i / 32., head.tip_allowance.height, head.reach()));
    }
    return {angle * 180. / pi, std::isfinite(floor) ? std::max(0., floor - head.margin) : 0.};
}
}

const char *region_kind_name(RegionKind kind)
{
    switch (kind) {
    case RegionKind::Dome: return "dome";
    case RegionKind::SlopedLid: return "sloped_lid";
    case RegionKind::ReliefTop: return "relief_top";
    default: return "curved_top";
    }
}

std::vector<Region> detect_nonplanar_regions(const ModelVolume &volume, const Transform3d &transform,
                                            const HeadEnvelope &head)
{
    const Limits limits = head_limits(head);
    if (limits.slope <= 1. || limits.span <= 0.) return {};
    const auto &mesh = volume.mesh().its;
    std::vector<Vec3d> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const Vec3f &v : mesh.vertices) vertices.push_back(transform * v.cast<double>());
    struct Face {
        Vec3d normal = Vec3d::Zero(), center = Vec3d::Zero();
        double area = 0., slope = 90., low = 0., high = 0.;
        bool eligible = false;
    };
    std::vector<Face> faces(mesh.indices.size());

    std::vector<std::vector<int>> neighbors(faces.size());
    std::map<std::pair<int, int>, int> edges;
    for (size_t i = 0; i < faces.size(); ++i) {
        const auto &idx = mesh.indices[i];
        const Vec3d &a = vertices[idx[0]], &b = vertices[idx[1]], &c = vertices[idx[2]];
        Face &f = faces[i];
        const Vec3d cross = (b-a).cross(c-a);
        f.area = cross.norm() * 0.5;
        if (f.area < 1e-10) continue;

        f.normal = cross.normalized() * (transform.linear().determinant() < 0. ? -1. : 1.);
        f.center = (a+b+c) / 3.;
        f.low = std::min({a.z(), b.z(), c.z()});
        f.high = std::max({a.z(), b.z(), c.z()});
        f.slope = std::acos(std::clamp(f.normal.z(), -1., 1.)) * 180. / pi;
        f.eligible = f.normal.z() > 1e-8 && f.slope <= limits.slope;
        if (!f.eligible) continue;
        for (int e = 0; e < 3; ++e) {
            const auto edge = std::minmax(idx[e], idx[(e+1)%3]);
            const auto [it, inserted] = edges.emplace(edge, int(i));
            if (!inserted) {
                neighbors[i].push_back(it->second);
                neighbors[it->second].push_back(int(i));
            }
        }
    }
    struct Piece { double area = 0., low = 0., high = 0.; Vec3d center = Vec3d::Zero(); Polygon projection; };
    std::vector<Piece> pieces(faces.size());
    std::vector<bool> in_band(faces.size(), false), grouped(faces.size(), false);
    std::vector<bool> visited(faces.size(), false);
    std::vector<Region> regions;
    for (size_t seed = 0; seed < faces.size(); ++seed) {
        if (visited[seed] || !faces[seed].eligible) continue;
        std::vector<int> component{int(seed)};
        visited[seed] = true;
        double component_low = faces[seed].low, component_high = faces[seed].high;
        bool curved = false;
        for (size_t q = 0; q < component.size(); ++q) {
            const int i = component[q];
            component_low = std::min(component_low, faces[i].low);
            component_high = std::max(component_high, faces[i].high);
            curved |= faces[i].slope > 1.;
            for (int next : neighbors[i])
                if (!visited[next]) { visited[next] = true; component.push_back(next); }
        }
        if (!curved) continue;


        const int bands = std::max(1, int(std::ceil((component_high-component_low) / limits.span)));
        for (int band = 0; band < bands; ++band) {
            const double high = component_high - band * limits.span;
            const double low = std::max(component_low, high-limits.span);
            for (int i : component) {
                in_band[i] = grouped[i] = false;
                if (faces[i].high < low || faces[i].low > high || (band > 0 && faces[i].low >= high-1e-8)) continue;
                const auto &idx = mesh.indices[i];
                std::vector<Vec3d> polygon{vertices[idx[0]], vertices[idx[1]], vertices[idx[2]]};
                auto clip = [&](double z, bool above) {
                    std::vector<Vec3d> clipped;
                    for (size_t j = 0; j < polygon.size(); ++j) {
                        const Vec3d &a = polygon[j], &b = polygon[(j+1)%polygon.size()];
                        const bool inside_a = above ? a.z() >= z : a.z() <= z;
                        const bool inside_b = above ? b.z() >= z : b.z() <= z;
                        if (inside_a) clipped.push_back(a);
                        if (inside_a != inside_b) clipped.push_back(a + ((z-a.z())/(b.z()-a.z())) * (b-a));
                    }
                    polygon = std::move(clipped);
                };
                clip(low, true); clip(high, false);
                Piece &piece = pieces[i];
                piece = Piece{};
                if (polygon.size() < 3) continue;
                piece.low = piece.high = polygon.front().z();
                for (const Vec3d &v : polygon) {
                    piece.low = std::min(piece.low, v.z()); piece.high = std::max(piece.high, v.z());
                    piece.projection.points.push_back(Point::new_scale(v.x(), v.y()));
                }
                piece.projection.make_counter_clockwise();
                for (size_t j = 1; j+1 < polygon.size(); ++j) {
                    const double area = (polygon[j]-polygon[0]).cross(polygon[j+1]-polygon[0]).norm() * 0.5;
                    piece.area += area;
                    piece.center += area * (polygon[0]+polygon[j]+polygon[j+1]) / 3.;
                }
                if (piece.area <= 1e-10) continue;
                piece.center /= piece.area;
                in_band[i] = true;
            }
            for (int first : component) {
                if (!in_band[first] || grouped[first]) continue;
                Region region;
                region.min_slope = 90.; region.min_z = high; region.max_z = low;
                Vec3d mean_normal = Vec3d::Zero();
                double curved_area = 0.;
                std::vector<int> queue{first};
                grouped[first] = true;
                for (size_t q = 0; q < queue.size(); ++q) {
                    const int i = queue[q];
                    const Face &f = faces[i]; const Piece &piece = pieces[i];
                    region.facets.push_back(i);
                    region.projection.push_back(piece.projection);
                    region.area += piece.area;
                    region.centroid += piece.area * piece.center;
                    mean_normal += piece.area * f.normal;
                    region.min_z = std::min(region.min_z, piece.low); region.max_z = std::max(region.max_z, piece.high);
                    if (f.slope > 1.) {
                        curved_area += piece.area;
                        region.min_slope = std::min(region.min_slope, f.slope);
                        region.max_slope = std::max(region.max_slope, f.slope);
                    }
                    for (int next : neighbors[i])
                        if (in_band[next] && !grouped[next]) { grouped[next] = true; queue.push_back(next); }
                }

                if (region.area < 25. || curved_area < 1.) continue;
                region.centroid /= region.area;
                region.height_span = region.max_z-region.min_z;
                const double coherence = mean_normal.norm() / region.area;
                if (coherence > 0.998 && region.max_slope-region.min_slope < 3.) region.kind = RegionKind::SlopedLid;
                else if (region.height_span < 1.2 && curved_area < 0.5 * region.area) region.kind = RegionKind::ReliefTop;
                else if (coherence < 0.97 && region.max_slope-region.min_slope > 10.) region.kind = RegionKind::Dome;
                regions.push_back(std::move(region));
            }
        }
    }
    std::stable_sort(regions.begin(), regions.end(), [](const Region &a, const Region &b) { return a.area > b.area; });
    return regions;
}
}
