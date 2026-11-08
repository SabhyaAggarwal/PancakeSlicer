#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

#include "libslic3r/Model.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>

using namespace Slic3r;

namespace {

uint64_t edge_key(int a, int b)
{
    if (a > b)
        std::swap(a, b);
    return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}

void chart_topology(const indexed_triangle_set &mesh, const std::vector<int> &faces, int &loops, int &euler)
{
    std::unordered_map<uint64_t, int> edge_use;
    std::unordered_map<int, int>      local;
    for (const int f : faces) {
        const stl_triangle_vertex_indices &t = mesh.indices[size_t(f)];
        for (int i = 0; i < 3; ++i) {
            ++edge_use[edge_key(t[i], t[(i + 1) % 3])];
            local.emplace(t[i], int(local.size()));
        }
    }
    euler = int(local.size()) - int(edge_use.size()) + int(faces.size());

    std::unordered_map<int, int> parent;
    const std::function<int(int)> find = [&](int x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    for (const auto &[key, uses] : edge_use)
        if (uses == 1)
            for (const int v : { int(key >> 32), int(uint32_t(key)) })
                parent.emplace(v, v);
    for (const auto &[key, uses] : edge_use)
        if (uses == 1) {
            const int a = find(int(key >> 32)), b = find(int(uint32_t(key)));
            if (a != b)
                parent[b] = a;
        }
    std::unordered_map<int, int> roots;
    for (const auto &[v, p] : parent)
        roots[find(v)] = 1;
    loops = int(roots.size());
}

float signed_area_2d(const Vec2f &a, const Vec2f &b, const Vec2f &c)
{
    return 0.5f * ((b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y()));
}

}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: texture_unwrap_dump <project.3mf>\n");
        return 2;
    }

    Model                     model;
    DynamicPrintConfig        config;
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    PlateDataPtrs             plate_data;
    std::vector<Preset *>     project_presets;
    bool                      is_bbl_3mf = false, is_orca_3mf = false;
    Semver                    file_version;
    const boost::filesystem::path tmp = boost::filesystem::temp_directory_path() / "texture_unwrap_dump";
    boost::filesystem::create_directories(tmp);
    set_data_dir(tmp.string());

    if (!load_bbs_3mf(argv[1], &config, &ctx, &model, &plate_data, &project_presets, &is_bbl_3mf, &is_orca_3mf,
                      &file_version, nullptr,
                      LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances |
                          LoadStrategy::Silence)) {
        std::printf("failed to load %s\n", argv[1]);
        return 1;
    }

    std::printf("loaded: %zu object(s)\n", model.objects.size());

    for (const ModelObject *object : model.objects)
        for (const ModelVolume *volume : object->volumes) {
            if (volume->texture_displacement_layers.empty()) {
                std::printf("volume \"%s\": no texture displacement layers; paint masks per slot:",
                            volume->name.c_str());
                for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
                    std::printf(" %zu", volume->texture_displacement_facet(i).get_data().triangles_to_split.size());
                std::printf("\n");
                continue;
            }
            std::printf("volume \"%s\": %zu base triangles, %zu layer(s)\n", volume->name.c_str(),
                        volume->mesh().its.indices.size(), volume->texture_displacement_layers.size());

            for (const TextureDisplacementLayer &layer : volume->texture_displacement_layers) {
                std::printf("\n  layer %d \"%s\"  mapping=%d  seam_angle=%.1f  connect=%d  islands_stored=%zu\n",
                            layer.slot, layer.name.c_str(), int(layer.projection_method),
                            layer.lscm_seam_angle_deg, int(layer.auto_connect_islands), layer.islands.size());
                if (layer.projection_method != TextureProjectionMethod::LSCM)
                    continue;

                const indexed_triangle_set patch =
                    extract_painted_patch(volume->mesh().its, volume->texture_displacement_facet(layer.slot).get_data());
                std::printf("  patch: %zu vertices, %zu triangles\n", patch.vertices.size(), patch.indices.size());
                if (patch.indices.empty())
                    continue;

                const auto t0 = std::chrono::steady_clock::now();
                const PatchUnwrap unwrap = compute_patch_unwrap(patch, layer.lscm_seam_angle_deg, 0.f,
                                                                layer.lscm_seam_edges);
                const auto t1 = std::chrono::steady_clock::now();
                std::printf("  TIMING compute_patch_unwrap: %.0f ms\n",
                            std::chrono::duration<double, std::milli>(t1 - t0).count());
                std::printf("  unwrap: %d charts, %zu unwrapped triangles\n", unwrap.chart_count,
                            unwrap.indices.size());

                std::vector<std::vector<int>> chart_faces(size_t(std::max(unwrap.chart_count, 0)));
                for (size_t i = 0; i < unwrap.indices.size(); ++i) {
                    const int chart = unwrap.vertex_chart[size_t(unwrap.indices[i][0])];
                    if (chart >= 0 && size_t(chart) < chart_faces.size())
                        chart_faces[size_t(chart)].push_back(unwrap.source_face[i]);
                }

                int bad_charts = 0;
                for (size_t c = 0; c < chart_faces.size(); ++c) {
                    int loops = 0, euler = 0;
                    chart_topology(patch, chart_faces[c], loops, euler);

                    int pos = 0, neg = 0;
                    for (size_t i = 0; i < unwrap.indices.size(); ++i) {
                        const stl_triangle_vertex_indices &t = unwrap.indices[i];
                        if (unwrap.vertex_chart[size_t(t[0])] != int(c))
                            continue;
                        const float a = signed_area_2d(unwrap.uvs[size_t(t[0])], unwrap.uvs[size_t(t[1])],
                                                       unwrap.uvs[size_t(t[2])]);
                        if (a > 0.f) ++pos; else if (a < 0.f) ++neg;
                    }
                    const int flipped = std::min(pos, neg);
                    const bool disk   = loops == 1 && euler == 1;
                    if (!disk || flipped > 0) {
                        ++bad_charts;
                        std::printf("    chart %2zu: %4zu faces  loops=%d euler=%d%s  flipped=%d/%d%s\n", c,
                                    chart_faces[c].size(), loops, euler, disk ? "" : "  NOT A DISK", flipped,
                                    pos + neg, flipped ? "  FOLDED" : "");
                    }
                }
                std::printf("  charts with a defect: %d / %d\n", bad_charts, unwrap.chart_count);

                {
                    const auto n0 = std::chrono::steady_clock::now();
                    const std::vector<TextureIsland> net = compute_connected_net(unwrap);
                    const auto n1 = std::chrono::steady_clock::now();
                    std::printf("  TIMING compute_connected_net: %.0f ms (%zu islands)\n",
                                std::chrono::duration<double, std::milli>(n1 - n0).count(), net.size());
                }
                const auto t2 = std::chrono::steady_clock::now();
                const std::vector<Vec2f> uv = compute_lscm_uvs(patch, layer);
                const auto t3 = std::chrono::steady_clock::now();
                std::printf("  TIMING compute_lscm_uvs:     %.0f ms  (called on every preview, overlay and bake)\n",
                            std::chrono::duration<double, std::milli>(t3 - t2).count());
                if (uv.size() != patch.vertices.size()) {
                    std::printf("  compute_lscm_uvs returned %zu uvs for %zu vertices\n", uv.size(),
                                patch.vertices.size());
                    continue;
                }
                const auto t4 = std::chrono::steady_clock::now();
                const std::vector<Vec2f> corner = compute_lscm_corner_uvs(patch, layer);
                const auto t5 = std::chrono::steady_clock::now();
                std::printf("  TIMING compute_lscm_corner_uvs: %.0f ms\n",
                            std::chrono::duration<double, std::milli>(t5 - t4).count());
                std::unordered_map<uint64_t, std::vector<Vec2f>> edge_seen;
                if (corner.size() == patch.indices.size() * 3)
                    for (size_t f = 0; f < patch.indices.size(); ++f) {
                        const stl_triangle_vertex_indices &t = patch.indices[f];
                        for (int k = 0; k < 3; ++k) {
                            const int a = t[k], b = t[(k + 1) % 3];
                            const int probe = std::min(a, b);
                            const int local = (a == probe) ? k : (k + 1) % 3;
                            edge_seen[edge_key(a, b)].push_back(corner[f * 3 + size_t(local)]);
                        }
                    }
                std::vector<int> chart_of_face(patch.indices.size(), -1);
                for (size_t i = 0; i < unwrap.indices.size(); ++i)
                    chart_of_face[size_t(unwrap.source_face[i])] = unwrap.vertex_chart[size_t(unwrap.indices[i][0])];

                std::unordered_map<uint64_t, std::vector<int>> edge_faces;
                for (size_t f = 0; f < patch.indices.size(); ++f) {
                    const stl_triangle_vertex_indices &t = patch.indices[f];
                    for (int k = 0; k < 3; ++k)
                        edge_faces[edge_key(t[k], t[(k + 1) % 3])].push_back(int(f));
                }

                int adjacent = 0, broken = 0, broken_same_chart = 0;
                float worst = 0.f;
                std::map<std::pair<int, int>, std::pair<int, float>> by_pair;
                for (const auto &[key, seen] : edge_seen) {
                    if (seen.size() != 2)
                        continue;
                    ++adjacent;
                    const float d = (seen[0] - seen[1]).norm();
                    if (d <= 1e-4f)
                        continue;
                    ++broken;
                    worst = std::max(worst, d);
                    const auto &faces_here = edge_faces[key];
                    int c1 = -1, c2 = -1;
                    if (faces_here.size() == 2) {
                        c1 = chart_of_face[size_t(faces_here[0])];
                        c2 = chart_of_face[size_t(faces_here[1])];
                    }
                    if (c1 == c2)
                        ++broken_same_chart;
                    auto &slot = by_pair[{ std::min(c1, c2), std::max(c1, c2) }];
                    ++slot.first;
                    slot.second = std::max(slot.second, d);
                }
                std::printf("  broken edges inside a single chart: %d\n", broken_same_chart);
                std::printf("  broken by chart pair:");
                for (const auto &[pk, v] : by_pair)
                    std::printf(" (%d,%d)x%d/%.1f", pk.first, pk.second, v.first, v.second);
                std::printf("\n");
                float seam_mm = 0.f;
                for (const auto &[key, seen] : edge_seen) {
                    if (seen.size() != 2 || (seen[0] - seen[1]).norm() <= 1e-4f)
                        continue;
                    seam_mm += (patch.vertices[size_t(key >> 32)] - patch.vertices[size_t(uint32_t(key))]).norm();
                }
                std::printf("  interior edges: %d, discontinuous: %d, total torn seam: %.2f mm (worst jump %.3f)\n",
                            adjacent, broken, seam_mm, worst);
                std::printf("  stored islands %zu vs charts %d -> %s\n", layer.islands.size(),
                            unwrap.chart_count,
                            layer.islands.size() == size_t(unwrap.chart_count) ? "stored placements used"
                                                                              : "net rebuilt");
            }
        }
    return 0;
}
