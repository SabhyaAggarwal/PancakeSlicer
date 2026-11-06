#pragma once
#include "TriMesh.hpp"
#include "CgalUtils.hpp"
#include "Callbacks.hpp"
#include <CGAL/boost/graph/border.h>
#include <CGAL/Named_function_parameters.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/boost/graph/graph_traits_Surface_mesh.h>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r { namespace tex2color {







inline constexpr std::size_t MAX_REPAIRABLE_MESH_HOLE_EDGES = 500;






inline constexpr std::size_t MAX_REPAIRABLE_MESH_BOUNDARY_EDGES = 5000;

struct RepairSetting
{

    std::size_t max_hole_edges = MAX_REPAIRABLE_MESH_HOLE_EDGES;


    std::size_t max_boundary_edges = MAX_REPAIRABLE_MESH_BOUNDARY_EDGES;
};

struct BoundaryEdgeStats
{
    std::size_t total_boundary_edges = 0;
    std::size_t max_cycle_edges      = 0;
    std::size_t cycle_count          = 0;
};



inline BoundaryEdgeStats ComputeBoundaryEdgeStats(const cgalutils::CGALMesh& cgal_mesh)
{
    namespace PMP = CGAL::Polygon_mesh_processing;
    using CGALMesh = cgalutils::CGALMesh;
    using HalfedgeDescriptor = boost::graph_traits<CGALMesh>::halfedge_descriptor;

    std::vector<HalfedgeDescriptor> border_cycles;
    CGAL::extract_boundary_cycles(cgal_mesh, std::back_inserter(border_cycles));

    BoundaryEdgeStats stats;
    stats.cycle_count = border_cycles.size();
    for (const HalfedgeDescriptor h0 : border_cycles) {
        std::size_t len = 0;
        HalfedgeDescriptor h = h0;
        do {
            ++len;
            h = next(h, cgal_mesh);
        } while (h != h0);
        stats.max_cycle_edges = std::max(stats.max_cycle_edges, len);
        stats.total_boundary_edges += len;
    }
    return stats;
}




inline void CloseBoundariesAndRepairManifoldness(cgalutils::CGALMesh& cgal_mesh)
{
    namespace PMP = CGAL::Polygon_mesh_processing;
    using CGALMesh = cgalutils::CGALMesh;
    using HalfedgeDescriptor = boost::graph_traits<CGALMesh>::halfedge_descriptor;
    using FaceDescriptor = boost::graph_traits<CGALMesh>::face_descriptor;

    PMP::stitch_borders(cgal_mesh);
    PMP::duplicate_non_manifold_vertices(cgal_mesh);

    std::vector<HalfedgeDescriptor> border_cycles;
    CGAL::extract_boundary_cycles(cgal_mesh, std::back_inserter(border_cycles));

    for (const HalfedgeDescriptor h : border_cycles) {
        std::vector<FaceDescriptor> patch_faces;
        PMP::triangulate_hole(cgal_mesh, h, CGAL::parameters::face_output_iterator(std::back_inserter(patch_faces)));
    }

    PMP::remove_degenerate_faces(cgal_mesh);
    PMP::duplicate_non_manifold_vertices(cgal_mesh);
}

inline bool RepairMesh(const TriMesh& mesh,
                       std::shared_ptr<TriMesh>& out_mesh,
                       AlgoProgressCallback progress_callback = nullptr,
                       AlgoCancelCallback cancel_callback = nullptr,
                       const RepairSetting& setting = RepairSetting{})
{
    namespace PMP = CGAL::Polygon_mesh_processing;
    using Clock = std::chrono::steady_clock;
    auto elapsed_ms = [](Clock::time_point t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    };

    const Clock::time_point t_total = Clock::now();


    std::vector<cgalutils::Kernel::Point_3> soup_points;
    std::vector<std::vector<std::size_t>> soup_triangles;

    soup_points.reserve(mesh.vertices.size());
    for (const TriVertex& v : mesh.vertices) {
        soup_points.emplace_back(v.x(), v.y(), v.z());
    }

    soup_triangles.reserve(mesh.indices.size());
    for (const TriFace& f : mesh.indices) {
        soup_triangles.push_back({static_cast<std::size_t>(f[0]),
                                  static_cast<std::size_t>(f[1]),
                                  static_cast<std::size_t>(f[2])});
    }

    if (progress_callback) {
        progress_callback({30, "Repairing polygon soup"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    {
        const auto t0 = Clock::now();
        PMP::repair_polygon_soup(soup_points, soup_triangles);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=repair_polygon_soup took="
                                << elapsed_ms(t0) << " ms";
    }

    if (progress_callback) {
        progress_callback({50, "Orienting polygon soup"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    {
        const auto t0 = Clock::now();
        PMP::orient_polygon_soup(soup_points, soup_triangles);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=orient_polygon_soup took="
                                << elapsed_ms(t0) << " ms";
    }

    if (progress_callback) {
        progress_callback({70, "Converting to CGAL mesh"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    cgalutils::CGALMesh cgal_mesh;
    {
        const auto t0 = Clock::now();
        PMP::polygon_soup_to_polygon_mesh(soup_points, soup_triangles, cgal_mesh);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=polygon_soup_to_polygon_mesh took="
                                << elapsed_ms(t0) << " ms";
    }

    {
        const auto t0 = Clock::now();
        PMP::remove_degenerate_faces(cgal_mesh);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=remove_degenerate_faces took="
                                << elapsed_ms(t0) << " ms";
    }

    if (progress_callback) {
        progress_callback({80, "Closing mesh boundaries"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }





    BoundaryEdgeStats stats;
    {
        const auto t0 = Clock::now();
        PMP::stitch_borders(cgal_mesh);
        PMP::duplicate_non_manifold_vertices(cgal_mesh);
        stats = ComputeBoundaryEdgeStats(cgal_mesh);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=boundary_stats took="
                                << elapsed_ms(t0) << " ms"
                                << " total_boundary_edges=" << stats.total_boundary_edges
                                << " max_cycle_edges=" << stats.max_cycle_edges
                                << " cycle_count=" << stats.cycle_count;
    }

    const bool can_repair_holes =
        stats.total_boundary_edges <= setting.max_boundary_edges &&
        stats.max_cycle_edges      <= setting.max_hole_edges;

    if (can_repair_holes) {
        const auto t0 = Clock::now();
        CloseBoundariesAndRepairManifoldness(cgal_mesh);
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=close_boundaries took="
                                << elapsed_ms(t0) << " ms";
    } else {
        BOOST_LOG_TRIVIAL(info)
            << "TextureToColor: RepairMesh skip hole closing"
            << ", total_boundary_edges=" << stats.total_boundary_edges
            << " (limit=" << setting.max_boundary_edges << ")"
            << ", max_cycle_edges=" << stats.max_cycle_edges
            << " (limit=" << setting.max_hole_edges << ")"
            << ", cycle_count=" << stats.cycle_count;
    }

    if (progress_callback) {
        progress_callback({85, "Converting from CGAL mesh"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    std::shared_ptr<TriMesh> out;
    {
        const auto t0 = Clock::now();
        out = std::make_shared<TriMesh>(cgalutils::cgal_to_trimesh(cgal_mesh));
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh stage=cgal_to_trimesh took="
                                << elapsed_ms(t0) << " ms";
    }

    out_mesh = std::move(out);
    if (progress_callback) {
        progress_callback({100, "Done"});
    }

    BOOST_LOG_TRIVIAL(info) << "TextureToColor: RepairMesh total=" << elapsed_ms(t_total) << " ms";

    return true;
}

}
}
