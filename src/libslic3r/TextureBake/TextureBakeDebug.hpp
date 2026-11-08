#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r::TextureBake { struct TriSoup; }

namespace Slic3r {

struct BakeStageMesh
{
    std::vector<Vec3f>   vertices;
    std::vector<Vec3i32> indices;

    bool   empty() const { return indices.empty(); }
    size_t triangle_count() const { return indices.size(); }
};

struct BakeStageSnapshot
{
    std::string   name;
    std::string   detail;
    BakeStageMesh mesh;

    double ms        = 0.0;
    size_t triangles = 0;
    size_t vertices  = 0;

    size_t open_edges         = 0;
    size_t non_manifold_edges = 0;
    size_t degenerate         = 0;
    bool   topology_checked   = false;

    bool mesh_dropped = false;
};

void bake_stage_topology(const BakeStageMesh &mesh, size_t &open_edges, size_t &non_manifold_edges,
                         size_t &degenerate);

size_t dump_bake_stages(const std::vector<BakeStageSnapshot> &stages, const std::string &dir);

class BakeStageRecorder
{
public:
    void enable(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled; }

    void set_check_topology(bool on) { m_check_topology = on; }
    bool check_topology() const { return m_check_topology; }

    void   set_mesh_cap(size_t triangles) { m_mesh_cap = triangles; }
    size_t mesh_cap() const { return m_mesh_cap; }

    void capture(const char *name, const TextureBake::TriSoup &soup, double ms,
                 const std::string &detail = {});
    void capture(const char *name, const std::vector<Vec3f> &vertices,
                 const std::vector<Vec3i32> &indices, double ms, const std::string &detail = {});
    void capture_note(const char *name, double ms, const std::string &detail);

    size_t mark() const { return m_stages.size(); }

    void rebase(size_t from, const Transform3d *to_local, bool flip_winding);

    const std::vector<BakeStageSnapshot> &stages() const { return m_stages; }
    std::vector<BakeStageSnapshot>        take() { return std::move(m_stages); }
    void                                  clear() { m_stages.clear(); }
    bool                                  empty() const { return m_stages.empty(); }

    double total_ms() const;

    size_t dump_obj(const std::string &dir) const { return dump_bake_stages(m_stages, dir); }

private:
    void finish(BakeStageSnapshot &s);

    std::vector<BakeStageSnapshot> m_stages;
    bool                           m_enabled        = false;
    bool                           m_check_topology = true;
    size_t                         m_mesh_cap       = 4'000'000;
};

}
