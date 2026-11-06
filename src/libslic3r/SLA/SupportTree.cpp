




#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/libslic3r.h"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>

#include <libslic3r/TriangleMeshSlicer.hpp>

#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <boost/log/trivial.hpp>
#include <vector>
#include <utility>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/SLA/JobController.hpp"



#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {
namespace sla {

void SupportTree::retrieve_full_mesh(indexed_triangle_set &outmesh) const {
    its_merge(outmesh, retrieve_mesh(MeshType::Support));
    its_merge(outmesh, retrieve_mesh(MeshType::Pad));
}

std::vector<ExPolygons> SupportTree::slice(const std::vector<float> &grid,
                                           float                     cr) const
{
    const indexed_triangle_set &sup_mesh = retrieve_mesh(MeshType::Support);
    const indexed_triangle_set &pad_mesh = retrieve_mesh(MeshType::Pad);

    using Slices = std::vector<ExPolygons>;
    auto slices = reserve_vector<Slices>(2);

    if (!sup_mesh.empty()) {
        slices.emplace_back();
        slices.back() = slice_mesh_ex(sup_mesh, grid, cr, ctl().cancelfn);
    }

    if (!pad_mesh.empty()) {
        slices.emplace_back();

        auto bb = bounding_box(pad_mesh);
        auto maxzit = std::upper_bound(grid.begin(), grid.end(), bb.max.z());

        auto cap = grid.end() - maxzit;
        auto padgrid = reserve_vector<float>(size_t(cap > 0 ? cap : 0));
        std::copy(grid.begin(), maxzit, std::back_inserter(padgrid));

        slices.back() = slice_mesh_ex(pad_mesh, padgrid, cr, ctl().cancelfn);
    }

    size_t len = grid.size();
    for (const Slices &slv : slices) { len = std::min(len, slv.size()); }


    if (slices.empty()) return {};

    Slices &mrg = slices.front();

    for (auto it = std::next(slices.begin()); it != slices.end(); ++it) {
        for (size_t i = 0; i < len; ++i) {
            Slices &slv = *it;
            std::copy(slv[i].begin(), slv[i].end(), std::back_inserter(mrg[i]));
            slv[i] = {};
        }
    }

    return mrg;
}

SupportTree::UPtr SupportTree::create(const SupportableMesh &sm,
                                      const JobController &  ctl)
{
    auto builder = make_unique<SupportTreeBuilder>();
    builder->m_ctl = ctl;

    if (sm.cfg.enabled) {

        SupportTreeBuildsteps::execute(*builder, sm);
        builder->merge_and_cleanup();
    } else {

        builder->ground_level = sm.emesh.ground_level();
    }

    return std::move(builder);
}

}}
