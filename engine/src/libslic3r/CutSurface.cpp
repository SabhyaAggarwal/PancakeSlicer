#include "CutSurface.hpp"
#include "libslic3r/Emboss.hpp"
#include <vector>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh/Surface_mesh.h>
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include "libslic3r/BoundingBox.hpp"
#include <cstddef>
#include "libslic3r/Polygon.hpp"
#include <cassert>
#include <array>
#include <cmath>
#include <math.h>
#include <algorithm>
#include <queue>
#include <CGAL/enum.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/Named_function_parameters.h>
#include "libslic3r/AABBTreeIndirect.hpp"
#include <cstdlib>
#include "libslic3r/libslic3r.h"
#include <boost/property_map/property_map.hpp>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/boost/graph/graph_traits_Surface_mesh.h>
#include <iterator>
#include <optional>
#include <CGAL/Surface_mesh/IO/OFF.h>

using namespace Slic3r;
#include "ExPolygonsIndex.hpp"
#include <boost/next_prior.hpp>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Cartesian_converter.h>
#include <tbb/parallel_for.h>

#include "TriangleMesh.hpp"
#include "Utils.hpp"
#include "ClipperUtils.hpp"

namespace priv {

using Project = Emboss::IProjection;
using Project3d = Emboss::IProject3d;

void set_skip_for_out_of_aoi(std::vector<bool>          &skip_indicies,
                             const indexed_triangle_set &its,
                             const Project              &projection,
                             const BoundingBox          &shapes_bb);

void set_skip_by_angle(std::vector<bool>          &skip_indicies,
                       const indexed_triangle_set &its,
                       const Project3d            &projection,
                       double                      max_angle = 89.);

    
using EpicKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using CutMesh = CGAL::Surface_mesh<EpicKernel::Point_3>;
using CutMeshes = std::vector<CutMesh>;

using VI = CGAL::SM_Vertex_index;
using HI = CGAL::SM_Halfedge_index;
using EI = CGAL::SM_Edge_index;
using FI = CGAL::SM_Face_index;
using P3 = CGAL::Epick::Point_3;

inline Vec3d to_vec3d(const P3 &p) { return Vec3d(p.x(),p.y(),p.z()); }

CutMesh to_cgal(const indexed_triangle_set &its,
                const std::vector<bool>    &skip_indicies,
                bool                        flip = false);

CutMesh to_cgal(const ExPolygons &shapes, const Project &projection);
bool exist_duplicit_vertex(const CutMesh& mesh);


struct IntersectingElement
{
    uint32_t shape_point_index{std::numeric_limits<uint32_t>::max()};

    unsigned char attr{std::numeric_limits<unsigned char>::max()};

    enum class Type: unsigned char {
        edge_1 = 0,
        face_1 = 1,
        edge_2 = 2,
        face_2 = 3,
        undefined = 4
    };
        
    IntersectingElement &set_type(Type t)
    {
        attr = static_cast<unsigned char>(
            attr + (int) t - (int) get_type());
        return *this;
    }
    void set_is_first(){ attr += 8; }
    void set_is_last(){ attr += 16; }
    Type get_type() const { return static_cast<Type>(attr % 8);}
    bool is_first() const { return 8 <= attr && attr < 16; }
    bool is_last() const { return attr >= 16; }
};

using EdgeShapeMap = CutMesh::Property_map<EI, IntersectingElement>;
using FaceShapeMap = CutMesh::Property_map<FI, IntersectingElement>;

using VertexShapeMap = CutMesh::Property_map<VI, const IntersectingElement *>;

const std::string edge_shape_map_name = "e:IntersectingElement";
const std::string face_shape_map_name = "f:IntersectingElement";

const std::string vert_shape_map_name = "v:IntersectingElement";

enum class FaceType {
    inside,
    outside,
    not_constrained,

    inside_processed
};
using FaceTypeMap = CutMesh::Property_map<FI, FaceType>;
const std::string face_type_map_name = "f:side";

using CvtVI2VI = CutMesh::Property_map<VI, VI>;
const std::string patch_source_name = "v:patch_source";

using ReductionMap = CvtVI2VI;
const std::string vertex_reduction_map_name = "v:reduction";

using EdgeBoolMap = CutMesh::Property_map<EI, bool>;
const std::string is_constrained_edge_name = "e:is_constrained";

void create_reduce_map(ReductionMap &reduction_map, const CutMesh &meshes);

using CutAOI = std::pair<std::vector<FI>, std::vector<HI>>;
using CutAOIs = std::vector<CutAOI>;
using VCutAOIs = std::vector<CutAOIs>;

CutAOIs cut_from_model(CutMesh                &cgal_model,
                       const ExPolygons       &shapes,
                        CutMesh      &cgal_shape,
                       float                   projection_ratio,
                       const ExPolygonsIndices &s2i);

using Loop  = std::vector<VI>;
using Loops = std::vector<Loop>;

Loops create_loops(const std::vector<HI> &outlines, const CutMesh &mesh);

struct SurfacePatch
{
    CutMesh mesh;
    Loops loops;

    BoundingBoxf3 bb;

    size_t model_id;
    size_t aoi_id;
    size_t shape_id = 0;

    bool is_whole_aoi = true;
};
using SurfacePatches = std::vector<SurfacePatch>;

struct ModelCutId
{
    uint32_t model_index;
    uint32_t cut_index;
};

class ModelCut2index
{
    std::vector<uint32_t> m_offsets;
    uint32_t m_count;

public:
    ModelCut2index(const VCutAOIs &cuts);
    uint32_t   calc_index(const ModelCutId &id) const;
    ModelCutId calc_id(uint32_t index) const;
    uint32_t                     get_count() const { return m_count; };
    const std::vector<uint32_t> &get_offsets() const { return m_offsets; }
};

SurfacePatches diff_models(VCutAOIs             &cuts,
                            CutMeshes  &cut_models,
                            CutMeshes  &models,
                           const Project3d      &projection);

bool is_over_whole_expoly(const CutAOI    &cutAOI,
                          const ExPolygon &shape,
                          const CutMesh   &mesh);

bool is_over_whole_expoly(const SurfacePatch &patch,
                          const ExPolygons   &shapes,
                          const VCutAOIs     &cutAOIs,
                          const CutMeshes    &meshes);
Polygons unproject_loops(const SurfacePatch &patch, const Project &projection, Vec2d &depth_range);

ExPolygon to_expoly(const SurfacePatch &patch, const Project &projection, Vec2d &depth_range);

struct ProjectionDistance
{
    uint32_t model_index = std::numeric_limits<uint32_t>::max();

    uint32_t aoi_index = std::numeric_limits<uint32_t>::max();

    uint32_t patch_index = std::numeric_limits<uint32_t>::max();

    float distance = std::numeric_limits<float>::max();    
};
using ProjectionDistances =  std::vector<ProjectionDistance>;

using VDistances = std::vector<ProjectionDistances>;

VDistances calc_distances(const SurfacePatches &patches,
                          const CutMeshes      &models,
                          const CutMesh        &shapes_mesh,
                          size_t                count_shapes_points,
                          float                 projection_ratio);

ProjectionDistances choose_best_distance(
    const VDistances       &distances,
    const ExPolygons       &shapes,
    const Point            &start,
    const ExPolygonsIndices &s2i,
    const SurfacePatches   &patches);

std::vector<bool> select_patches(const ProjectionDistances &best_distances,
                                 const SurfacePatches      &patches,
                                 const ExPolygons          &shapes,
                                 const BoundingBox         &shapes_bb,
                                 const ExPolygonsIndices   &s2i,
                                 const VCutAOIs            &cutAOIs,
                                 const CutMeshes           &meshes,
                                 const Project             &projection);

void append(SurfaceCut &sc, SurfaceCut &&sc_add);

SurfaceCut patch2cut(SurfacePatch &patch);

SurfaceCut merge_patches( SurfacePatches &patches,
                         const std::vector<bool>  &mask);

#ifdef DEBUG_OUTPUT_DIR
void prepare_dir(const std::string &dir);
void initialize_store(const std::string &dir_to_clear);
void store(const CutMesh &mesh, const FaceTypeMap &face_type_map, const std::string &dir, bool is_filled = false);
void store(const ExPolygons &shapes, const std::string &svg_file);
void store(const CutMesh &mesh, const ReductionMap &reduction_map, const std::string &dir);
void store(const CutAOIs &aois, const CutMesh &mesh, const std::string &dir);
void store(const SurfacePatches &patches, const std::string &dir);
void store(const Vec3f &vertex, const Vec3f &normal, const std::string &file, float size = 2.f);
using Connection = std::pair<size_t, size_t>; using Connections = std::vector<Connection>;
void store(const ExPolygons &shapes, const std::vector<bool> &mask_distances, const Connections &connections, const std::string &file_svg);
void store(const SurfaceCut &cut, const std::string &file, const std::string &contour_dir);
void store(const std::vector<indexed_triangle_set> &models, const std::string &obj_filename);
void store(const std::vector<CutMesh>&models, const std::string &dir);
void store(const Emboss::IProjection &projection, const Point &point_to_project, float projection_ratio, const std::string &obj_filename);
#endif
}

#ifdef DEBUG_OUTPUT_DIR
#include "libslic3r/SVG.hpp"
#include <boost/log/trivial.hpp>
#include <filesystem>
#endif

SurfaceCut Slic3r::cut_surface(const ExPolygons &shapes,
                               const std::vector<indexed_triangle_set> &models,
                               const Emboss::IProjection &projection,
                               float projection_ratio)
{
    assert(!models.empty());
    assert(!shapes.empty());
    if (models.empty() || shapes.empty() ) return {};

#ifdef DEBUG_OUTPUT_DIR
    priv::initialize_store(DEBUG_OUTPUT_DIR);
    priv::store(models, DEBUG_OUTPUT_DIR + "models_input.obj");
    priv::store(shapes, DEBUG_OUTPUT_DIR + "shapes.svg");
#endif
    BoundingBox shapes_bb = get_extents(shapes);
#ifdef DEBUG_OUTPUT_DIR
    priv::store(projection, shapes_bb.center(), projection_ratio, DEBUG_OUTPUT_DIR + "projection_center.obj");
#endif
    const float max_angle = 89.9f;
    priv::CutMeshes cgal_models;
    priv::CutMeshes cgal_neg_models;
    cgal_models.reserve(models.size());
    for (const indexed_triangle_set &its : models) {
        std::vector<bool> skip_indicies(its.indices.size(), {false});
        priv::set_skip_for_out_of_aoi(skip_indicies, its, projection, shapes_bb);

        bool flip = true;
        cgal_neg_models.push_back(priv::to_cgal(its, skip_indicies, flip));
        
        priv::set_skip_by_angle(skip_indicies, its, projection, max_angle);
        cgal_models.push_back(priv::to_cgal(its, skip_indicies));
    }
#ifdef DEBUG_OUTPUT_DIR
    priv::store(cgal_models, DEBUG_OUTPUT_DIR + "model/");
    priv::store(cgal_neg_models, DEBUG_OUTPUT_DIR + "model_neg/");
#endif

    priv::CutMesh cgal_shape = priv::to_cgal(shapes, projection);
#ifdef DEBUG_OUTPUT_DIR
    CGAL::IO::write_OFF(DEBUG_OUTPUT_DIR + "shape.off", cgal_shape);
#endif
    ExPolygonsIndices s2i(shapes);
    priv::VCutAOIs model_cuts;
    for (priv::CutMesh &cgal_model : cgal_models) { 
        priv::CutAOIs cutAOIs = priv::cut_from_model(
            cgal_model, shapes, cgal_shape, projection_ratio, s2i);
#ifdef DEBUG_OUTPUT_DIR
        size_t index = &cgal_model - &cgal_models.front();
        priv::store(cutAOIs, cgal_model, DEBUG_OUTPUT_DIR + "model_AOIs/" + std::to_string(index) + "/");
#endif
        model_cuts.push_back(std::move(cutAOIs));
    }

    priv::SurfacePatches patches = priv::diff_models(model_cuts, cgal_models, cgal_neg_models, projection);
#ifdef DEBUG_OUTPUT_DIR
    priv::store(patches, DEBUG_OUTPUT_DIR + "patches/");
#endif
    if (patches.empty()) return {};    

    for (priv::SurfacePatch &patch : patches)
        patch.shape_id = s2i.cvt(patch.shape_id).expolygons_index;

    uint32_t shapes_points = s2i.get_count();
    priv::VDistances distances = priv::calc_distances(patches, cgal_models, cgal_shape, shapes_points, projection_ratio);
        
    Point start = shapes_bb.center();
    priv::ProjectionDistances best_projection = priv::choose_best_distance(distances, shapes, start, s2i, patches);
    std::vector<bool> use_patch = priv::select_patches(best_projection, patches, shapes, shapes_bb, s2i, model_cuts, cgal_models, projection);
    SurfaceCut result = merge_patches(patches, use_patch);

#ifdef DEBUG_OUTPUT_DIR
    priv::store(result, DEBUG_OUTPUT_DIR + "result.obj", DEBUG_OUTPUT_DIR + "result_contours/");
#endif
    return result;
}

indexed_triangle_set Slic3r::cut2model(const SurfaceCut         &cut,
                                       const Emboss::IProject3d &projection)
{
    assert(!cut.empty());
    size_t count_vertices = cut.vertices.size() * 2;
    size_t count_indices  = cut.indices.size() * 2;

    for (const auto &c : cut.contours) {
        assert(!c.empty());
        count_indices += c.size() * 2;
    }
    
    indexed_triangle_set result;
    result.vertices.reserve(count_vertices);
    result.indices.reserve(count_indices);

    result.vertices.insert(result.vertices.end(), 
        cut.vertices.begin(), cut.vertices.end());
    result.indices.insert(result.indices.end(), 
        cut.indices.begin(), cut.indices.end());

    for (const Vec3f &v : cut.vertices) {
        Vec3d vd = v.cast<double>();
        Vec3d vd2 = projection.project(vd);
        result.vertices.push_back(vd2.cast<float>());
    }

    size_t back_offset = cut.vertices.size();
    for (const auto &i : cut.indices) {
        assert(i.x() + back_offset < result.vertices.size());
        assert(i.y() + back_offset < result.vertices.size());
        assert(i.z() + back_offset < result.vertices.size());
        assert(i.x() >= 0 && i.x() < cut.vertices.size());
        assert(i.y() >= 0 && i.y() < cut.vertices.size());
        assert(i.z() >= 0 && i.z() < cut.vertices.size());
        result.indices.emplace_back(i.x() + back_offset,
                                    i.z() + back_offset,
                                    i.y() + back_offset);
    }

    for (const auto &contour : cut.contours) {
        size_t prev_front_index = contour.back();
        size_t prev_back_index  = back_offset + prev_front_index;
        for (size_t front_index : contour) {
            assert(front_index < cut.vertices.size());
            size_t back_index  = back_offset + front_index;
            result.indices.emplace_back(front_index, prev_front_index, back_index);
            result.indices.emplace_back(prev_front_index, prev_back_index, back_index);
            prev_front_index = front_index;
            prev_back_index  = back_index;
        }
    }

    assert(count_vertices == result.vertices.size());
    assert(count_indices == result.indices.size());
    return result;
}

namespace priv {
using PointNormal = std::pair<Vec3d, Vec3d>;
using PointNormals = std::array<PointNormal, 4>;

bool is_out_of(const Vec3d &v, const PointNormal &point_normal);

using IsOnSides = std::vector<std::array<bool, 4>>;
bool is_all_on_one_side(const Vec3i32 &t, const IsOnSides& is_on_sides);

}

bool priv::is_out_of(const Vec3d &v, const PointNormal &point_normal)
{
    const Vec3d& p = point_normal.first;
    const Vec3d& n = point_normal.second;
    double signed_distance = (v - p).dot(n);
    return signed_distance > 1e-5;
};

bool priv::is_all_on_one_side(const Vec3i32 &t, const IsOnSides& is_on_sides) {
    for (size_t side = 0; side < 4; side++) {
        bool result = true;
        for (auto vi : t) {
            if (!is_on_sides[vi][side]) {
                result = false;
                break;
            }
        }
        if (result) return true;
    }
    return false;
}

void priv::set_skip_for_out_of_aoi(std::vector<bool>          &skip_indicies,
                                   const indexed_triangle_set &its,
                                   const Project              &projection,
                                   const BoundingBox          &shapes_bb)
{
    assert(skip_indicies.size() == its.indices.size());
    std::array<std::pair<Vec3d, Vec3d>, 4> bb;
    int index = 0;
    for (Point v :
         {shapes_bb.min, Point{shapes_bb.min.x(), shapes_bb.max.y()},
          shapes_bb.max, Point{shapes_bb.max.x(), shapes_bb.min.y()}})
        bb[index++] = projection.create_front_back(v);

    size_t prev_i = 3;
    PointNormals point_normals;
    for (size_t i = 0; i < 4; i++) {
        const Vec3d &p1 = bb[i].first;
        const Vec3d &p2 = bb[i].second;
        const Vec3d &p3 = bb[prev_i].first;
        prev_i = i;

        Vec3d v1 = p2 - p1;
        v1.normalize();
        Vec3d v2 = p3 - p1;
        v2.normalize();

        Vec3d normal = v2.cross(v1);
        normal.normalize(); 

        point_normals[i] = {p1, normal};
    }

    if (is_out_of(point_normals[2].first, point_normals[0])) {
        for (auto &pn : point_normals)
            pn.second *= -1;
    }    

    IsOnSides is_on_sides(its.vertices.size(), {false,false,false,false});    
    
    tbb::parallel_for(tbb::blocked_range<size_t>(0, its.vertices.size()),
    [&its, &point_normals, &is_on_sides](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            Vec3d v = its.vertices[i].cast<double>();
            for (int side : {0, 2}) {
                if (is_out_of(v, point_normals[side])) {
                    is_on_sides[i][side] = true;
                    break;
                }
            }
            for (int side : {1, 3}) {
                if (is_out_of(v, point_normals[side])) {
                    is_on_sides[i][side] = true;
                    break;
                }        
            }
        }
    });
    tbb::parallel_for(tbb::blocked_range<size_t>(0, its.indices.size()),
    [&its, &is_on_sides, &skip_indicies](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            if (is_all_on_one_side(its.indices[i], is_on_sides)) 
                skip_indicies[i] = true;
        }
    });
}

indexed_triangle_set Slic3r::its_mask(const indexed_triangle_set &its,
                                      const std::vector<bool>    &mask)
{
    if (its.indices.size() != mask.size()) {
        assert(false);
        return {};
    }

    std::vector<uint32_t> cvt_vetices(its.vertices.size(), {std::numeric_limits<uint32_t>::max()});
    size_t vertices_count = 0;
    size_t faces_count    = 0;
    for (const auto &t : its.indices) {
        size_t index = &t - &its.indices.front();
        if (!mask[index]) continue;
        ++faces_count;
        for (const auto vi : t) {
            uint32_t &cvt = cvt_vetices[vi];
            if (cvt == std::numeric_limits<uint32_t>::max())
                cvt = vertices_count++;
        }
    }
    if (faces_count == 0) return {};
    
    indexed_triangle_set result;
    result.indices.reserve(faces_count);
    result.vertices = std::vector<Vec3f>(vertices_count);
    for (size_t i = 0; i < its.vertices.size(); ++i) {
        uint32_t index = cvt_vetices[i];
        if (index == std::numeric_limits<uint32_t>::max()) continue;
        result.vertices[index] = its.vertices[i];
    }

    for (const stl_triangle_vertex_indices &f : its.indices)
        if (mask[&f - &its.indices.front()])        
            result.indices.push_back(stl_triangle_vertex_indices(
                cvt_vetices[f[0]], cvt_vetices[f[1]], cvt_vetices[f[2]]));
    
    return result;    
}

indexed_triangle_set Slic3r::its_cut_AoI(const indexed_triangle_set &its,
                                         const BoundingBox          &bb,
                                         const Emboss::IProjection &projection)
{
    std::vector<bool> skip_indicies(its.indices.size(), false);
    priv::set_skip_for_out_of_aoi(skip_indicies, its, projection, bb);
    skip_indicies.flip();
    return its_mask(its, skip_indicies);
}

void priv::set_skip_by_angle(std::vector<bool>          &skip_indicies,
                             const indexed_triangle_set &its,
                             const Project3d            &projection,
                             double                      max_angle)
{
    assert(max_angle < 90. && max_angle > 89.);
    assert(skip_indicies.size() == its.indices.size());
    float threshold = static_cast<float>(cos(max_angle / 180. * M_PI));
    for (const stl_triangle_vertex_indices& face : its.indices) {
        size_t index = &face - &its.indices.front();
        if (skip_indicies[index]) continue;
        Vec3f n = its_face_normal(its, face);
        const Vec3f& v = its.vertices[face[0]];
        const Vec3d vd = v.cast<double>();
        Vec3d projectedd  = projection.project(vd);
        Vec3f projected   = projectedd.cast<float>();
        Vec3f project_dir = projected - v;
        project_dir.normalize();
        float cos_alpha = project_dir.dot(n);
        if (cos_alpha > threshold) continue;
        skip_indicies[index] = true;
    }
}

priv::CutMesh priv::to_cgal(const indexed_triangle_set &its,
                            const std::vector<bool>    &skip_indicies,
                            bool                        flip)
{
    const std::vector<stl_vertex>                  &vertices = its.vertices;
    const std::vector<stl_triangle_vertex_indices> &indices = its.indices;

    std::vector<bool> use_vetices(vertices.size(), {false});

    size_t vertices_count = 0;
    size_t faces_count    = 0;
    size_t edges_count    = 0;

    for (const auto &t : indices) {     
        size_t index = &t - &indices.front();
        if (skip_indicies[index]) continue;        
        ++faces_count;
        size_t count_used_vertices = 0;
        for (const auto vi : t) {
            if (!use_vetices[vi]) {
                ++vertices_count;
                use_vetices[vi] = true;
            } else {
                ++count_used_vertices;
            }
        }
        switch (count_used_vertices) {
        case 3: break;
        case 2: edges_count += 2; break;
        case 1:
        case 0: edges_count += 3; break;
        default: assert(false);
        }        
    }
    assert(vertices_count <= vertices.size());
    assert(edges_count <= (indices.size() * 3));
    assert(faces_count <= indices.size());

    CutMesh result;
    result.reserve(vertices_count, edges_count, faces_count);

    std::vector<VI> to_filtrated_vertices_index(vertices.size());
    size_t filtrated_vertices_index = 0;
    for (size_t i = 0; i < vertices.size(); ++i) 
        if (use_vetices[i]) { 
            to_filtrated_vertices_index[i] = VI(filtrated_vertices_index);
            ++filtrated_vertices_index;
        }

    for (const stl_vertex& v : vertices) {
        if (!use_vetices[&v - &vertices.front()]) continue;
        result.add_vertex(CutMesh::Point{v.x(), v.y(), v.z()});
    }

    if (!flip) {
        for (const stl_triangle_vertex_indices &f : indices) {
            if (skip_indicies[&f - &indices.front()]) continue;
            result.add_face(to_filtrated_vertices_index[f[0]],
                            to_filtrated_vertices_index[f[1]],
                            to_filtrated_vertices_index[f[2]]);
        }
    } else {
        for (const stl_triangle_vertex_indices &f : indices) {
            if (skip_indicies[&f - &indices.front()]) continue;
            result.add_face(to_filtrated_vertices_index[f[2]],
                            to_filtrated_vertices_index[f[1]],
                            to_filtrated_vertices_index[f[0]]);
        }
    }
    
    return result;
}

bool priv::exist_duplicit_vertex(const CutMesh &mesh) {
    std::vector<Vec3d> points;
    points.reserve(mesh.vertices().size());
    for (VI vi : mesh.vertices()) { 
        const P3 &p = mesh.point(vi);
        points.emplace_back(p.x(), p.y(), p.z());
    }
    std::sort(points.begin(), points.end(), [](const Vec3d &v1, const Vec3d &v2) {
        return v1.x() < v2.x() || 
              (v1.x() == v2.x() && 
                  (v1.y() < v2.y() || 
                  (v1.y() == v2.y()  && 
                      v1.z() < v2.z())));
    });
    auto it = std::adjacent_find(points.begin(), points.end());
    return it != points.end();
}

priv::CutMesh priv::to_cgal(const ExPolygons  &shapes,
                            const Project     &projection)
{
    if (shapes.empty()) return {};
        
    CutMesh result;
    EdgeShapeMap edge_shape_map = result.add_property_map<EI, IntersectingElement>(edge_shape_map_name).first;
    FaceShapeMap face_shape_map = result.add_property_map<FI, IntersectingElement>(face_shape_map_name).first;

    std::vector<VI> indices;
    auto insert_contour = [&projection, &indices, &result, 
        &edge_shape_map, &face_shape_map]
        (const Polygon &polygon) {
        indices.clear();
        indices.reserve(polygon.points.size() * 2);
        size_t  num_vertices_old = result.number_of_vertices();
        for (const Point &polygon_point : polygon.points) {
            auto [front, back] = projection.create_front_back(polygon_point);
            P3 v_front{front.x(), front.y(), front.z()};
            VI vi1 = result.add_vertex(v_front);
            assert(vi1.idx() == (indices.size() + num_vertices_old));
            indices.push_back(vi1);

            P3 v_back{back.x(), back.y(), back.z()};
            VI vi2 = result.add_vertex(v_back);
            assert(vi2.idx() == (indices.size() + num_vertices_old));
            indices.push_back(vi2);
        }

        auto find_edge = [&result](FI fi, VI from, VI to) {
            HI hi = result.halfedge(fi);
            for (; result.target(hi) != to; hi = result.next(hi));
            assert(result.source(hi) == from);
            assert(result.target(hi) == to);
            return result.edge(hi);
        };

        uint32_t contour_index = static_cast<uint32_t>(num_vertices_old / 2);
        for (int32_t i = 0; i < int32_t(indices.size()); i += 2) {
            bool    is_first  = i == 0;
            bool    is_last   = size_t(i + 2) >= indices.size();
            int32_t j = is_last ? 0 : (i + 2);
            
            FI fi1 = result.add_face(indices[i], indices[j], indices[i + 1]);
            EI ei1 = find_edge(fi1, indices[i + 1], indices[i]);
            EI ei2 = find_edge(fi1, indices[j], indices[i + 1]);
            FI fi2 = result.add_face(indices[j], indices[j + 1], indices[i + 1]);
            IntersectingElement element {contour_index, (unsigned char)IntersectingElement::Type::undefined};
            if (is_first) element.set_is_first();
            if (is_last) element.set_is_last();
            edge_shape_map[ei1] = element.set_type(IntersectingElement::Type::edge_1);
            face_shape_map[fi1] = element.set_type(IntersectingElement::Type::face_1);
            edge_shape_map[ei2] = element.set_type(IntersectingElement::Type::edge_2);
            face_shape_map[fi2] = element.set_type(IntersectingElement::Type::face_2);
            ++contour_index;
        }
    };

    size_t count_point = count_points(shapes);
    result.reserve(result.number_of_vertices() + 2 * count_point,
                   result.number_of_edges() + 4 * count_point,
                   result.number_of_faces() + 2 * count_point);

    for (const ExPolygon &shape : shapes) {
        insert_contour(shape.contour);
        for (const Polygon &hole : shape.holes)
            insert_contour(hole);
    }
    assert(!exist_duplicit_vertex(result));
    return result;
}

priv::ModelCut2index::ModelCut2index(const VCutAOIs &cuts)
{
    m_offsets.reserve(cuts.size());
    uint32_t offset = 0;
    for (const CutAOIs &model_cuts: cuts) {
        m_offsets.push_back(offset);
        offset += model_cuts.size();
    }
    m_count = offset;
}

uint32_t priv::ModelCut2index::calc_index(const ModelCutId &id) const
{
    assert(id.model_index < m_offsets.size());
    uint32_t offset = m_offsets[id.model_index];
    uint32_t res = offset + id.cut_index;
    assert(((id.model_index+1) < m_offsets.size() && res < m_offsets[id.model_index+1]) ||
           ((id.model_index+1) == m_offsets.size() && res < m_count));
    return res;
}

priv::ModelCutId priv::ModelCut2index::calc_id(uint32_t index) const
{
    assert(index < m_count);
    ModelCutId result{0,0};
    for (size_t model_index = 1; model_index < m_offsets.size(); ++model_index) {
        if (m_offsets[model_index] > index) break;
        result.model_index = model_index;
    }
    result.cut_index = index - m_offsets[result.model_index];
    return result;
}

namespace priv {

struct Visitor : public CGAL::Polygon_mesh_processing::Corefinement::Default_visitor<CutMesh> {
    Visitor(const CutMesh &object, const CutMesh &shape, EdgeShapeMap edge_shape_map,
            FaceShapeMap face_shape_map, VertexShapeMap vert_shape_map, bool* is_valid) :
        object(object), shape(shape), edge_shape_map(edge_shape_map), face_shape_map(face_shape_map),
        vert_shape_map(vert_shape_map), is_valid(is_valid)
    {}

    const CutMesh &object;
    const CutMesh &shape;

    EdgeShapeMap edge_shape_map;
    FaceShapeMap face_shape_map;

    VertexShapeMap vert_shape_map;

    bool* is_valid;

    std::vector<const IntersectingElement*> intersections;

    void intersection_point_detected(std::size_t    i_id,
                                     int            sdim,
                                     HI             h_f,
                                     HI             h_e,
                                     const CutMesh &tm_f,
                                     const CutMesh &tm_e,
                                     bool           is_target_coplanar,
                                     bool           is_source_coplanar);

    void new_vertex_added(std::size_t i_id, VI v, const CutMesh &tm);
};

bool is_face_inside(HI                      hi,
                    const CutMesh          &mesh,
                    const CutMesh          &shape_mesh,
                    const VertexShapeMap   &vertex_shape_map,
                    const ExPolygonsIndices &shape2index);

void set_face_type(FaceTypeMap            &face_type_map,
                   const CutMesh          &mesh,
                   const VertexShapeMap   &vertex_shape_map,
                   const EdgeBoolMap          &ecm,
                   const CutMesh          &shape_mesh,
                   const ExPolygonsIndices &shape2index);

void flood_fill_inner(const CutMesh &mesh, FaceTypeMap &face_type_map);

void collect_surface_data(std::queue<FI>  &process,
                          std::vector<FI> &faces,
                          std::vector<HI> &outlines,
                          FaceTypeMap     &face_type_map,
                          const CutMesh   &mesh);

CutAOIs create_cut_area_of_interests(const CutMesh    &mesh,
                                     const ExPolygons &shapes,
                                     FaceTypeMap      &face_type_map);

}

void priv::Visitor::intersection_point_detected(std::size_t    i_id,
                                                int            sdim,
                                                HI             h_f,
                                                HI             h_e,
                                                const CutMesh &tm_f,
                                                const CutMesh &tm_e,
                                                bool is_target_coplanar,
                                                bool is_source_coplanar)
{
    if (i_id >= intersections.size()) {
        size_t capacity = Slic3r::next_highest_power_of_2(i_id + 1);
        intersections.reserve(capacity);
        intersections.resize(capacity);
    }

    const IntersectingElement *intersection_ptr = nullptr;
    if (&tm_e == &shape) {
        assert(&tm_f == &object);
        switch (sdim) {
        case 1:
            intersection_ptr = &edge_shape_map[shape.edge(h_e)];
            break;
        case 2:
            intersection_ptr = &face_shape_map[shape.face(h_e)];
            break;
        default: assert(false);
        }
        if (is_target_coplanar)
            vert_shape_map[object.source(h_f)] = intersection_ptr;
        if (is_source_coplanar)
            vert_shape_map[object.target(h_f)] = intersection_ptr;
    } else {
        assert(&tm_f == &shape && &tm_e == &object);
        assert(!is_target_coplanar);
        assert(!is_source_coplanar);
        if (is_target_coplanar || is_source_coplanar) 
            *is_valid = false;
        intersection_ptr = &edge_shape_map[shape.edge(h_f)];
        if (sdim == 0) vert_shape_map[object.target(h_e)] = intersection_ptr;
    }

    if (intersection_ptr->shape_point_index == std::numeric_limits<uint32_t>::max()) {
        *is_valid = false;
    }
    intersections[i_id] = intersection_ptr;
}

void priv::Visitor::new_vertex_added(std::size_t i_id, VI v, const CutMesh &tm)
{
    assert(&tm == &object);
    assert(i_id < intersections.size());
    const IntersectingElement *intersection_ptr = intersections[i_id];
    assert(intersection_ptr != nullptr);
    vert_shape_map[v] = intersection_ptr;
}

bool priv::is_face_inside(HI                      hi,
                          const CutMesh          &mesh,
                          const CutMesh          &shape_mesh,
                          const VertexShapeMap   &vertex_shape_map,
                          const ExPolygonsIndices &shape2index)
{
    VI vi_from = mesh.source(hi);
    VI vi_to   = mesh.target(hi);
    const IntersectingElement &shape_from = *vertex_shape_map[vi_from];
    const IntersectingElement &shape_to   = *vertex_shape_map[vi_to];
    assert(shape_from.shape_point_index != std::numeric_limits<uint32_t>::max());
    assert(shape_from.attr != (unsigned char) IntersectingElement::Type::undefined);
    assert(shape_to.shape_point_index != std::numeric_limits<uint32_t>::max());
    assert(shape_to.attr != (unsigned char) IntersectingElement::Type::undefined);

    uint32_t                  i_from    = shape_from.shape_point_index;
    uint32_t                  i_to      = shape_to.shape_point_index;
    IntersectingElement::Type type_from = shape_from.get_type();
    IntersectingElement::Type type_to   = shape_to.get_type();
    if (i_from == i_to && type_from == type_to) {
        assert(type_from == IntersectingElement::Type::face_1 ||
                type_from == IntersectingElement::Type::face_2);

        uint32_t i = i_from * 2;
        uint32_t j = i + 2;
        if (shape_from.is_last()) {
            ExPolygonsIndex point_id = shape2index.cvt(i_from);
            point_id.point_index  = 0;
            j = shape2index.cvt(point_id)*2;
        }

        const P3 &p = mesh.point(mesh.target(mesh.next(hi)));

        CGAL::Sign abcp = type_from == IntersectingElement::Type::face_1 ?
                        CGAL::orientation(shape_mesh.point(VI(i)),
                                            shape_mesh.point(VI(i + 1)),
                                            shape_mesh.point(VI(j)), p) :
                        CGAL::orientation(shape_mesh.point(VI(j)),
                                            shape_mesh.point(VI(i + 1)),
                                            shape_mesh.point(VI(j + 1)), p);
        return abcp == CGAL::POSITIVE;
    } else if (i_from < i_to || (i_from == i_to && type_from < type_to)) {
        bool is_last = shape_to.is_last() && shape_from.is_first();
        assert(i_from == i_to || is_last || (i_from + 1) == i_to);
        return !is_last;
    } else {
        assert(i_from > i_to || (i_from == i_to && type_from > type_to));
        bool is_last = shape_to.is_first() && shape_from.is_last();
        assert(i_from == i_to || is_last || (i_to + 1) == i_from);
        return is_last;
    }

    assert(false);
    return false;
}

void priv::set_face_type(FaceTypeMap            &face_type_map,
                         const CutMesh          &mesh,
                         const VertexShapeMap   &vertex_shape_map,
                         const EdgeBoolMap      &ecm,
                         const CutMesh          &shape_mesh,
                         const ExPolygonsIndices &shape2index)
{
    for (EI ei : mesh.edges()) {
        if (!ecm[ei]) continue;
        HI hi = mesh.halfedge(ei);
        FI fi = mesh.face(hi);
        bool is_inside = is_face_inside(hi, mesh, shape_mesh, vertex_shape_map, shape2index);        
        face_type_map[fi] = is_inside ? FaceType::inside : FaceType::outside;
        HI hi_op = mesh.opposite(hi);
        assert(hi_op.is_valid());
        if (!hi_op.is_valid()) continue;
        FI fi_op = mesh.face(hi_op);
        assert(fi_op.is_valid());
        if (!fi_op.is_valid()) continue;
        face_type_map[fi_op] = (!is_inside) ? FaceType::inside : FaceType::outside;
    }
}

priv::CutAOIs priv::cut_from_model(CutMesh                &cgal_model,
                                   const ExPolygons       &shapes,
                                   CutMesh                &cgal_shape,
                                   float                   projection_ratio,
                                   const ExPolygonsIndices &s2i)
{
    VertexShapeMap vert_shape_map = cgal_model.add_property_map<VI, const IntersectingElement*>(vert_shape_map_name, nullptr).first;
    
    bool is_valid = true;
    const EdgeShapeMap edge_shape_map = cgal_shape.property_map<EI, IntersectingElement>(edge_shape_map_name).value();
    const FaceShapeMap face_shape_map = cgal_shape.property_map<FI, IntersectingElement>(face_shape_map_name).value();
    Visitor visitor{cgal_model, cgal_shape, edge_shape_map, face_shape_map, vert_shape_map, &is_valid};

    EdgeBoolMap ecm = cgal_model.add_property_map<EI, bool>(is_constrained_edge_name, false).first;
    const auto &p = CGAL::parameters::visitor(visitor)
                        .edge_is_constrained_map(ecm)
                        .throw_on_self_intersection(false);
    const auto& q = CGAL::parameters::do_not_modify(true);
    CGAL::Polygon_mesh_processing::corefine(cgal_model, cgal_shape, p, q);

    if (!is_valid) return {};

    FaceTypeMap face_type_map = cgal_model.add_property_map<FI, FaceType>(face_type_map_name, FaceType::not_constrained).first;

    set_face_type(face_type_map, cgal_model, vert_shape_map, ecm, cgal_shape, s2i);
#ifdef DEBUG_OUTPUT_DIR
    store(cgal_model, face_type_map, DEBUG_OUTPUT_DIR + "constrained/");
#endif
    flood_fill_inner(cgal_model, face_type_map);

#ifdef DEBUG_OUTPUT_DIR
    store(cgal_model, face_type_map, DEBUG_OUTPUT_DIR + "filled/", true);
#endif
    return create_cut_area_of_interests(cgal_model, shapes, face_type_map);
}

void priv::flood_fill_inner(const CutMesh &mesh,
                            FaceTypeMap   &face_type_map)
{
    std::vector<FI> process;
    size_t guess_size = 128;
    process.reserve(guess_size);

    auto has_inside_neighbor = [&mesh, &face_type_map](FI fi) {
        HI hi     = mesh.halfedge(fi);
        HI hi_end = hi;
        auto exist_next = [&hi, &hi_end, &mesh]() -> bool {
            hi = mesh.next(hi);
            return hi != hi_end;
        };
        do {
            HI hi_opposite = mesh.opposite(hi);
            if (!hi_opposite.is_valid()) continue;
            FI fi_opposite = mesh.face(hi_opposite);
            if (!fi_opposite.is_valid()) continue;
            if (face_type_map[fi_opposite] == FaceType::inside) return true;
        } while (exist_next());
        return false;
    };

    for (FI fi : mesh.faces()) {
        FaceType type = face_type_map[fi];
        if (type != FaceType::not_constrained) continue;
        if (!has_inside_neighbor(fi)) continue;
        assert(process.empty());
        process.push_back(fi); 

        while (!process.empty()) {
            FI process_fi = process.back();
            process.pop_back();
            FaceType& process_type = face_type_map[process_fi];
            if (process_type == FaceType::inside) continue;
            process_type = FaceType::inside;

            HI hi = mesh.halfedge(process_fi);
            HI hi_end = hi;
            auto exist_next = [&hi, &hi_end, &mesh]() -> bool {
                hi = mesh.next(hi);
                return hi != hi_end;
            };
            do {
                HI hi_opposite = mesh.opposite(hi);
                if (!hi_opposite.is_valid()) continue;                
                FI fi_opposite = mesh.face(hi_opposite);
                if (!fi_opposite.is_valid()) continue;                
                FaceType type_opposite = face_type_map[fi_opposite];
                if (type_opposite == FaceType::not_constrained)
                    process.push_back(fi_opposite);
            } while (exist_next());
        }
    }
}

void priv::collect_surface_data(std::queue<FI>  &process,
                                std::vector<FI> &faces,
                                std::vector<HI> &outlines,
                                FaceTypeMap     &face_type_map,
                                const CutMesh   &mesh)
{
    assert(!process.empty());
    assert(faces.empty());
    assert(outlines.empty());
    while (!process.empty()) {
        FI fi = process.front();
        process.pop();

        FaceType &fi_type = face_type_map[fi];
        if (fi_type == FaceType::inside_processed) continue;
        assert(fi_type == FaceType::inside);
        fi_type = FaceType::inside_processed;
        faces.push_back(fi);

        HI hi     = mesh.halfedge(fi);
        HI hi_end = hi;
        do {
            HI hi_opposite = mesh.opposite(hi);
            if (!hi_opposite.is_valid()) { 
                outlines.push_back(hi);
                hi = mesh.next(hi);
                continue; 
            }            
            FI fi_opposite = mesh.face(hi_opposite);
            if (!fi_opposite.is_valid()) {
                outlines.push_back(hi);
                hi = mesh.next(hi);
                continue;
            }
            FaceType side = face_type_map[fi_opposite];
            if (side == FaceType::inside) {
                process.emplace(fi_opposite);
            } else if (side == FaceType::outside) {
                outlines.push_back(hi);
            }
            hi = mesh.next(hi);
        } while (hi != hi_end);
    }
}

void priv::create_reduce_map(ReductionMap &reduction_map, const CutMesh &mesh)
{
    const VertexShapeMap vert_shape_map = mesh.property_map<VI, const IntersectingElement*>(vert_shape_map_name).value();
    const EdgeBoolMap ecm = mesh.property_map<EI, bool>(is_constrained_edge_name).value();

    auto is_reducible_vertex = [&vert_shape_map](VI reduction_from) -> bool {
        const IntersectingElement *ie = vert_shape_map[reduction_from];
        if (ie == nullptr) return false;
        IntersectingElement::Type type = ie->get_type();
        return type == IntersectingElement::Type::edge_2;
    };

    auto add_reduction = [&]
    (HI hi) {
        VI erase = mesh.target(hi);
        VI left = mesh.source(hi);
        assert(is_reducible_vertex(erase));
        assert(!is_reducible_vertex(left));
        VI &vi = reduction_map[erase];
        if (vi.is_valid())
            return;

        Vec3d v_erase = to_vec3d(mesh.point(erase));
        Vec3d v_left = to_vec3d(mesh.point(left));
        for (FI fi : mesh.faces_around_target(hi)) {
            if (!fi.is_valid())
                continue;
            VI vi_a, vi_b;
            for (VI vi : mesh.vertices_around_face(mesh.halfedge(fi))) {
                if (!vi.is_valid())
                    continue;
                if (vi == erase)
                    continue;
                if (!vi_a.is_valid())
                    vi_a = vi;
                else {
                    assert(!vi_b.is_valid());
                    vi_b = vi;
                }
            } 
            assert(vi_b.is_valid());
            if (vi_a == left || vi_b == left)
                continue;
            
            Vec3d v_a = to_vec3d(mesh.point(vi_a));
            Vec3d v_b = to_vec3d(mesh.point(vi_b));
            Vec3d v_ab = v_b - v_a;
            Vec3d v_ae = v_erase - v_a;
            Vec3d v_al = v_left - v_a;

            Vec3d n1 = v_ab.cross(v_ae);
            Vec3d n2 = v_ab.cross(v_al);
            if (((n1.x() > 0) != (n2.x() > 0)) ||
                ((n1.y() > 0) != (n2.y() > 0)) ||
                ((n1.z() > 0) != (n2.z() > 0)))
                return;
        }

        reduction_map[erase] = left;
    };

    for (EI ei : mesh.edges()) { 
        if (!ecm[ei]) continue;
        HI hi = mesh.halfedge(ei);
        VI vi = mesh.target(hi);
        if (is_reducible_vertex(vi)) add_reduction(hi);
        
        HI hi_op = mesh.opposite(hi);
        VI vi_op = mesh.target(hi_op);
        if (is_reducible_vertex(vi_op)) add_reduction(hi_op);
    }
#ifdef DEBUG_OUTPUT_DIR
    store(mesh, reduction_map, DEBUG_OUTPUT_DIR + "reduces/");
#endif
}

priv::CutAOIs priv::create_cut_area_of_interests(const CutMesh    &mesh,
                                                 const ExPolygons &shapes,
                                                 FaceTypeMap &face_type_map)
{
    size_t faces_per_cut    = mesh.faces().size() / shapes.size();
    size_t outlines_per_cut = faces_per_cut / 2;
    size_t cuts_per_model   = shapes.size() * 2;

    CutAOIs result;
    result.reserve(cuts_per_model);

    std::queue<FI> process;
    for (FI fi : mesh.faces()) {
        if (face_type_map[fi] != FaceType::inside) continue;

        CutAOI cut;
        std::vector<FI> &faces    = cut.first;
        std::vector<HI> &outlines = cut.second;

        faces.reserve(faces_per_cut);
        outlines.reserve(outlines_per_cut);

        assert(process.empty());
        process.push(fi);
        collect_surface_data(process, faces, outlines, face_type_map, mesh);
        assert(!faces.empty());
        assert(!outlines.empty());
        result.emplace_back(std::move(cut));
    }
    return result;
}

namespace priv {

float calc_distance(const P3 &p,
                    uint32_t  pi,                    
                    const CutMesh &shapes_mesh,
                    float          projection_ratio);

}

float priv::calc_distance(const P3 &p,
                          uint32_t  pi,                          
                          const CutMesh &shapes_mesh,
                          float          projection_ratio)
{
    VI vi_start(2 * pi);
    VI vi_end(2 * pi + 1);

    const P3 &start = shapes_mesh.point(vi_start);
    const P3 &end   = shapes_mesh.point(vi_end);

    size_t max_i   = 0;
    float  max_val = 0.f;
    for (size_t i = 0; i < 3; i++) {
        float val = start[i] - end[i];
        if (val < 0.f) val *= -1.f;
        if (max_val < val) {
            max_val = val;
            max_i   = i;
        }
    }

    float from_start    = p[max_i] - start[max_i];
    float best_distance = projection_ratio * (end[max_i] - start[max_i]);
    return from_start - best_distance;
}

priv::VDistances priv::calc_distances(const SurfacePatches &patches,
                                      const CutMeshes &models,
                                      const CutMesh &shapes_mesh,
                                      size_t count_shapes_points,
                                      float projection_ratio)
{
    priv::VDistances result(count_shapes_points);
    for (const SurfacePatch &patch : patches) {
        const VertexShapeMap vert_shape_map = 
            models[patch.model_id].property_map<VI, const IntersectingElement *>(vert_shape_map_name).value();
        uint32_t patch_index = &patch - &patches.front();
        const CvtVI2VI cvt = patch.mesh.property_map<VI, VI>(patch_source_name).value();
        for (const Loop &loop : patch.loops) 
        for (const VI &vi_patch : loop) {
            VI vi_model = cvt[vi_patch];
            if (!vi_model.is_valid()) continue;            
            const IntersectingElement *ie = vert_shape_map[vi_model];
            if (ie == nullptr) continue;
            assert(ie->shape_point_index != std::numeric_limits<uint32_t>::max());
            assert(ie->attr != (unsigned char) IntersectingElement::Type::undefined);
            uint32_t pi = ie->shape_point_index;
            assert(pi <= count_shapes_points);
            std::vector<ProjectionDistance> &pds = result[pi];
            uint32_t model_index = patch.model_id;
            uint32_t  aoi_index = patch.aoi_id;
            const P3 &p = patch.mesh.point(vi_patch);
            float distance = calc_distance(p, pi, shapes_mesh, projection_ratio);
            pds.push_back({model_index, aoi_index, patch_index, distance});
        }
    }
    return result;
}


#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/Line.hpp"
namespace priv {

float calc_size_sq(const Point &p);

struct ClosePoint
{
    uint32_t index = std::numeric_limits<uint32_t>::max();
    float dist_sq = std::numeric_limits<float>::max();
};

struct SearchData{
std::vector<Linef> lines;
std::vector<size_t> cvt;
AABBTreeIndirect::Tree<2, double> tree;
};

SearchData create_search_data(const ExPolygons &shapes, const std::vector<bool>& mask);
uint32_t get_closest_point_index(const SearchData &sd, size_t line_idx, const Vec2d &hit_point, const ExPolygons &shapes, const ExPolygonsIndices &s2i);

uint32_t find_closest_point_index(const Point &p, const ExPolygons &shapes, const ExPolygonsIndices &s2i, const std::vector<bool> &mask);

std::pair<uint32_t, uint32_t> find_closest_point_pair(const ExPolygons &shapes, const std::vector<bool> &done_shapes, const ExPolygonsIndices  &s2i, const std::vector<bool> &mask);

const ProjectionDistance *get_closest_projection(const ProjectionDistances &distance, float wanted_distance);

void fill_polygon_distances(const ProjectionDistance &pd, uint32_t index, const ExPolygonsIndex &id, ProjectionDistances & result, const ExPolygon &shape, const VDistances &distances);

void fill_shape_distances(uint32_t start_index, const ProjectionDistance *start_pd, ProjectionDistances &result, const ExPolygonsIndices &s2i, const ExPolygon &shape, const VDistances &distances);

ClosePoint find_close_point(const Point &p, ProjectionDistances &result, std::vector<bool>& finished_shapes,const ExPolygonsIndices &s2i, const ExPolygons &shapes);

}

float priv::calc_size_sq(const Point &p){    
    return (float) p.x() * p.x() + (float) p.y() * p.y();
}

priv::SearchData priv::create_search_data(const ExPolygons        &shapes,
                                          const std::vector<bool> &mask)
{
    SearchData sd;
    sd.lines.reserve(mask.size());
    sd.cvt.reserve(mask.size());
    size_t index     = 0;
    auto   add_lines = [&sd, &index, &mask]
    (const Polygon &poly) {
        Vec2d prev = poly.back().cast<double>();
        bool use_point = mask[index + poly.points.size() - 1];
        for (const Point &p : poly.points) {
            if (!use_point) {
                use_point = mask[index];
                if (use_point) prev = p.cast<double>();
            } else if (!mask[index]) {
                use_point = false;
            } else {
                Vec2d p_d = p.cast<double>();
                sd.lines.emplace_back(prev, p_d);
                sd.cvt.push_back(index);
                prev = p_d;
            }
            ++index;
        }
    };

    for (const ExPolygon &shape : shapes) {
        add_lines(shape.contour);
        for (const Polygon &hole : shape.holes) add_lines(hole);
    }
    sd.tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(sd.lines);
    return sd;
}

uint32_t priv::get_closest_point_index(const SearchData &sd,
                                       size_t            line_idx,
                                       const Vec2d      &hit_point,
                                       const ExPolygons       &shapes,
                                       const ExPolygonsIndices &s2i)
{
    const Linef &line = sd.lines[line_idx];
    Vec2d dir = line.a - line.b;
    Vec2d dir_abs = dir.cwiseAbs();
    int i = (dir_abs.x() > dir_abs.y())? 0 :1;

    bool use_index = abs(line.a[i] - hit_point[i]) >
                     abs(line.b[i] - hit_point[i]);
    size_t point_index = sd.cvt[line_idx];

    [[maybe_unused]] auto is_same = [&s2i, &shapes]
    (const Vec2d &p, size_t i) -> bool {
        auto id = s2i.cvt(i);
        const ExPolygon &shape = shapes[id.expolygons_index];
        const Polygon   &poly  = (id.polygon_index == 0) ?
                                           shape.contour :
                                           shape.holes[id.polygon_index - 1];
        Vec2crd p_ = p.cast<coord_t>();
        return p_ == poly[id.point_index];
    };

    if (use_index) { 
        assert(is_same(line.b, point_index));
        return point_index; 
    }
    auto id = s2i.cvt(point_index);
    if (id.point_index != 0) {
        assert(is_same(line.a, point_index - 1));
        return point_index - 1;
    }
    const ExPolygon &shape = shapes[id.expolygons_index];
    size_t count_polygon_points = (id.polygon_index == 0) ?
        shape.contour.size() :
        shape.holes[id.polygon_index - 1].size();
    size_t prev_point_index = point_index  + (count_polygon_points - 1);
    assert(is_same(line.a, prev_point_index));
    return prev_point_index;
}

uint32_t priv::find_closest_point_index(const Point            &p,
                                        const ExPolygons       &shapes,
                                        const ExPolygonsIndices &s2i,
                                        const std::vector<bool> &mask)
{    
    SearchData sd = create_search_data(shapes, mask);
    if (sd.tree.nodes().size() == 0){
        double closest_square_distance = INFINITY;
        uint32_t closest_id = -1; 
        for (uint32_t i = 0; i < mask.size(); i++) 
            if (mask[i]){
                ExPolygonsIndex ei = s2i.cvt(i);
                const Point& s_p = ei.is_contour()? 
                    shapes[ei.expolygons_index].contour[ei.point_index]:
                shapes[ei.expolygons_index].holes[ei.hole_index()][ei.point_index];
                double square_distance = (p - s_p).cast<double>().squaredNorm();
                if (closest_id >= mask.size() || 
                    closest_square_distance > square_distance) {
                    closest_id = i;
                    closest_square_distance = square_distance;
                }
            }
        assert(closest_id < mask.size());
        return closest_id;
    }
    size_t line_idx = std::numeric_limits<size_t>::max();
    Vec2d  hit_point;
    Vec2d  p_d = p.cast<double>();
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(
        sd.lines, sd.tree, p_d, line_idx, hit_point);
    assert(distance_sq > 0);
    
    return get_closest_point_index(sd, line_idx, hit_point, shapes, s2i);
}

std::pair<uint32_t, uint32_t> priv::find_closest_point_pair(
    const ExPolygons        &shapes,
    const std::vector<bool> &done_shapes,
    const ExPolygonsIndices  &s2i,
    const std::vector<bool> &mask)
{
    assert(mask.size() == s2i.get_count());
    assert(done_shapes.size() == shapes.size());
    std::vector<bool> unfinished_mask = mask;

    size_t index = 0;
    for (size_t shape_index = 0; shape_index < shapes.size(); shape_index++) {
        size_t count = count_points(shapes[shape_index]);
        if (done_shapes[shape_index]) { 
            for (size_t i = 0; i < count; ++i, ++index)
                unfinished_mask[index] = false;
        } else {
            index += count;
        }
    }
    assert(index == s2i.get_count());
    SearchData sd = create_search_data(shapes, unfinished_mask);

    struct ClosestPair
    {
        size_t finish_idx          = std::numeric_limits<size_t>::max();
        size_t unfinished_line_idx = std::numeric_limits<size_t>::max();
        Vec2d  hit_point           = Vec2d();
        double distance_sq         = std::numeric_limits<double>::max();
    } cp;

    index = 0;
    for (size_t shape_index = 0; shape_index < shapes.size(); shape_index++) {
        const ExPolygon shape = shapes[shape_index];
        if (!done_shapes[shape_index]) {            
            index += count_points(shape);
            continue;
        }        

        auto search_in_polygon = [&index, &cp, &sd, &mask](const Polygon& polygon) {
            for (size_t i = 0; i < polygon.size(); ++i, ++index) { 
                if (mask[index] == false) continue;
                Vec2d p_d = polygon[i].cast<double>();
                size_t line_idx = std::numeric_limits<size_t>::max();
                Vec2d  hit_point;
                double distance_sq = AABBTreeLines::squared_distance_to_indexed_lines(
                    sd.lines, sd.tree, p_d, line_idx, hit_point, cp.distance_sq);
                if (distance_sq < 0 || 
                    distance_sq >= cp.distance_sq) continue;
                assert(line_idx < sd.lines.size());
                cp.distance_sq         = distance_sq;
                cp.unfinished_line_idx = line_idx;
                cp.hit_point           = hit_point;
                cp.finish_idx          = index;
            }
        };
        search_in_polygon(shape.contour);
        for (const Polygon& hole: shape.holes)
            search_in_polygon(hole);
    }
    assert(index == s2i.get_count());
    if (cp.finish_idx == std::numeric_limits<size_t>::max()) {
        return std::make_pair(std::numeric_limits<size_t>::max(),
                              std::numeric_limits<size_t>::max());
    }

    size_t unfinished_idx = get_closest_point_index(sd, cp.unfinished_line_idx, cp.hit_point, shapes, s2i);
    return std::make_pair(cp.finish_idx, unfinished_idx);
}

const priv::ProjectionDistance *priv::get_closest_projection(
    const ProjectionDistances &distance, float wanted_distance)
{
    float min_d = std::numeric_limits<float>::max();
    const ProjectionDistance *min_pd = nullptr;
    for (const ProjectionDistance &pd : distance) { 
        float d = std::fabs(pd.distance - wanted_distance);
        if (min_d > d) { 
            min_d = d;
            min_pd = &pd;
        }
    }
    return min_pd;
}

void priv::fill_polygon_distances(const ProjectionDistance &pd,
                                  uint32_t                  index,
                                  const ExPolygonsIndex       &id,
                                  ProjectionDistances      &result,
                                  const ExPolygon          &shape,
                                  const VDistances         &distances)
{
    const Points& points = (id.polygon_index == 0) ?
                                            shape.contour.points :
                                            shape.holes[id.polygon_index - 1].points;
    uint32_t first_index = index - id.point_index;
    uint32_t last_index  = first_index + points.size();        

    uint32_t act_index = index;
    const ProjectionDistance* act_pd = &pd;

    result[act_index] = pd;

    auto exist_next = [&distances, &act_index, &act_pd, &result]
    (uint32_t nxt_index) {
        const ProjectionDistance *nxt_pd = get_closest_projection(distances[nxt_index] ,act_pd->distance);
        if (nxt_pd == nullptr) return false;

        assert(result[nxt_index].aoi_index == std::numeric_limits<uint32_t>::max());
        result[nxt_index] = *nxt_pd;
        act_index = nxt_index;
        act_pd    = &result[nxt_index];
        return true;
    };

    uint32_t finish_index = (index == first_index) ? (last_index - 1) :
                                                        (index - 1);
    do {
        uint32_t nxt_index = act_index + 1;
        if (nxt_index == last_index) nxt_index = first_index;
        if (!exist_next(nxt_index)) break;            
    } while (act_index != finish_index);

    if (act_index == finish_index) return;

    act_index = index;
    act_pd    = &pd;
    do {
        uint32_t nxt_index = (act_index == first_index) ? 
            (last_index-1) : (act_index - 1);
        assert(nxt_index != index);
        if (!exist_next(nxt_index)) break;
    } while (true);
}

void priv::fill_shape_distances(uint32_t                  start_index,
                                const ProjectionDistance *start_pd,
                                ProjectionDistances      &result,
                                const ExPolygonsIndices   &s2i,
                                const ExPolygon         &shape,
                                const VDistances         &distances)
{
    uint32_t expolygons_index = s2i.cvt(start_index).expolygons_index;
    uint32_t first_shape_index = s2i.cvt({expolygons_index, 0, 0});
    do {
        fill_polygon_distances(*start_pd, start_index, s2i.cvt(start_index),result, shape, distances);
        auto find_close_finished_point = [&first_shape_index, &shape, &result]
        (const Point &p) -> ClosePoint {
            uint32_t index = first_shape_index;                
            ClosePoint cp;
            auto check_finished_points = [&cp, &result, &index, &p]
            (const Points& pts) { 
                for (const Point &p_ : pts) {
                    if (result[index].aoi_index == std::numeric_limits<uint32_t>::max()) {
                        ++index;
                        continue;
                    }
                    float distance = calc_size_sq(p_ - p);
                    if (cp.dist_sq > distance) { 
                        cp.dist_sq = distance;
                        cp.index   = index;
                    }
                    ++index;
                }
            };
            check_finished_points(shape.contour.points);
            for (const Polygon &h : shape.holes)
                check_finished_points(h.points);
            return cp;
        };
                        
        start_index = std::numeric_limits<uint32_t>::max();
        uint32_t finished_index = std::numeric_limits<uint32_t>::max();
        float dist_sq = std::numeric_limits<float>::max();

        uint32_t index = first_shape_index;
        auto check_unfinished_points = [&index, &result, &distances, &find_close_finished_point, &dist_sq, &start_index, &finished_index]
        (const Points& pts) { 
            for (const Point &p : pts) {
                if (result[index].aoi_index !=
                    std::numeric_limits<uint32_t>::max() ||
                    distances[index].empty()) {
                    ++index;
                    continue;
                }
                ClosePoint cp = find_close_finished_point(p);
                if (dist_sq > cp.dist_sq) { 
                    dist_sq = cp.dist_sq;
                    start_index = index;
                    finished_index = cp.index;
                }
                ++index;
            }
        };
        check_unfinished_points(shape.contour.points);
        for (const Polygon &h : shape.holes)
            check_unfinished_points(h.points);
    } while (start_index != std::numeric_limits<uint32_t>::max());
}

priv::ClosePoint priv::find_close_point(const Point         &p,
                                        ProjectionDistances &result,
                                        std::vector<bool>   &finished_shapes,
                                        const ExPolygonsIndices &s2i,
                                        const ExPolygons       &shapes)
{
    ClosePoint cp;
    for (uint32_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
        if (!finished_shapes[shape_index]) continue;
        const ExPolygon &shape = shapes[shape_index];
        uint32_t index = s2i.cvt({shape_index, 0, 0});
        auto find_close_point_in_points = [&p, &cp, &index, &result]
        (const Points &pts){
            for (const Point &p_ : pts) {
                if (result[index].aoi_index ==
                    std::numeric_limits<uint32_t>::max()) {
                    ++index;
                    continue;
                }
                float distance_sq = calc_size_sq(p - p_);
                if (cp.dist_sq > distance_sq) { 
                    cp.dist_sq = distance_sq;
                    cp.index = index;
                }
                ++index;
            }
        };
        find_close_point_in_points(shape.contour.points);
        for (const Polygon& h:shape.holes)
            find_close_point_in_points(h.points);
    }
    return cp;
}

priv::ProjectionDistances priv::choose_best_distance(
    const VDistances &distances, const ExPolygons &shapes, const Point &start, const ExPolygonsIndices &s2i, const SurfacePatches &patches)
{
    assert(distances.size() == count_points(shapes));

    std::vector<std::vector<uint32_t>> shapes_patches(shapes.size());
    for (const SurfacePatch &patch : patches)
        shapes_patches[patch.shape_id].push_back(&patch-&patches.front());

    ProjectionDistances result(distances.size());

    std::vector<bool> finished_shapes(shapes.size(), {false});
    
    float wanted_distance = 0.f;
    
    std::vector<bool> mask_distances(s2i.get_count(), {true});
    for (const auto &d : distances)
        if (d.empty()) mask_distances[&d - &distances.front()] = false;

    uint32_t unfinished_index = find_closest_point_index(start, shapes, s2i, mask_distances);
    assert(unfinished_index < s2i.get_count());
    if (unfinished_index >= s2i.get_count())
        return result;

#ifdef DEBUG_OUTPUT_DIR
    Connections connections;
    connections.reserve(shapes.size());
    connections.emplace_back(unfinished_index, unfinished_index);
#endif

    do {
        const ProjectionDistance* pd = get_closest_projection(distances[unfinished_index], wanted_distance);
        assert(pd != nullptr);
        uint32_t expolygons_index = s2i.cvt(unfinished_index).expolygons_index;        
        const ExPolygon &shape = shapes[expolygons_index];
        std::vector<uint32_t> &shape_patches = shapes_patches[expolygons_index];
        if (shape_patches.size() == 1){
            uint32_t first_shape_index = s2i.cvt({expolygons_index, 0, 0});
            uint32_t laset_shape_index = first_shape_index + count_points(shape);
            for (uint32_t i = first_shape_index; i < laset_shape_index; ++i) { 
                const ProjectionDistances &pds = distances[i];
                if (pds.empty()) continue;
                assert(pds.front().patch_index == shape_patches.front());
                result[i] = pds.front();
                if (pds.size() == 1) continue;

                float relative_distance = fabs(result[i].distance - pd->distance);
                for (uint32_t pds_index = 1; pds_index < pds.size(); ++pds_index) {
                    assert(pds[pds_index].patch_index == shape_patches.front());
                    float relative_distance2 = fabs(pds[pds_index].distance - pd->distance);
                    if (relative_distance > relative_distance2) { 
                        relative_distance = relative_distance2;
                        result[i]         = pds[pds_index];
                    }
                }
            }
        } else {
            assert(!shape_patches.empty());
            fill_shape_distances(unfinished_index, pd, result, s2i, shape, distances);
        }

        finished_shapes[expolygons_index] = true;
        auto [finished, unfinished] = find_closest_point_pair(
            shapes, finished_shapes, s2i, mask_distances);

        if (finished == std::numeric_limits<uint32_t>::max()) break;

        assert(unfinished != std::numeric_limits<uint32_t>::max());
        const ProjectionDistance &closest_pd = result[finished];
        assert(closest_pd.aoi_index != std::numeric_limits<uint32_t>::max());
        wanted_distance = closest_pd.distance;
        unfinished_index = unfinished;

#ifdef DEBUG_OUTPUT_DIR
        connections.emplace_back(finished, unfinished);
#endif
    } while (true);
#ifdef DEBUG_OUTPUT_DIR
    store(shapes, mask_distances, connections, DEBUG_OUTPUT_DIR + "closest_points.svg");
#endif
    return result;
}

namespace priv {
const VI default_vi(std::numeric_limits<uint32_t>::max());

struct Source{ HI  hi; int sdim=0;};
using Sources                            = std::vector<Source>;
const std::string vertex_source_map_name = "v:SourceIntersecting";
using VertexSourceMap                    = CutMesh::Property_map<VI, Source>;

struct IntersectionSources
{
    const CutMesh *patch;
    const CutMesh *model;

    VertexSourceMap vmap;

    Sources* sources;

    void intersection_point_detected(std::size_t    i_id,
                                     int            sdim,
                                     HI             h_f,
                                     HI             h_e,
                                     const CutMesh &tm_f,
                                     const CutMesh &tm_e,
                                     bool           is_target_coplanar,
                                     bool           is_source_coplanar)
    {
        Source source;
        if (&tm_e == model) {
            source = {h_e, sdim};
            assert(&tm_f == patch);
            if (is_target_coplanar) {
                assert(sdim == 0);
                vmap[tm_f.source(h_f)] = source;
            }
            if (is_source_coplanar) {
                assert(sdim == 0);
                vmap[tm_f.target(h_f)] = source;
            }

            if (is_source_coplanar || is_target_coplanar) source = {};
        } else {
            source = {h_f, sdim};
            assert(&tm_f == model && &tm_e == patch);
            assert(!is_target_coplanar);
            assert(!is_source_coplanar);
        }

        assert(sources->size() == i_id);
        sources->push_back(source);
    }

    void new_vertex_added(std::size_t i_id, VI v, const CutMesh &tm)
    {
        assert(!vmap[v].hi.is_valid());
        assert(i_id < sources->size());
        assert(sources->at(i_id).hi.is_valid());
        vmap[v] = sources->at(i_id);
    }

    void before_subface_creations(FI , CutMesh & ) {}
    void after_subface_created(FI , CutMesh & ) {}
    void after_subface_creations(CutMesh &) {}
    void before_subface_created(CutMesh &) {}
    void before_edge_split(HI , CutMesh & ) {}
    void edge_split(HI , CutMesh & ) {}
    void after_edge_split() {}
    void add_retriangulation_edge(HI , CutMesh & ) {}
};

void create_face_types(FaceTypeMap           &map,
                       const CutMesh         &tm1,
                       const CutMesh         &tm2,
                       const EdgeBoolMap         &ecm,
                       const VertexSourceMap &sources);

bool clip_cut(SurfacePatch &cut, CutMesh clipper);

BoundingBoxf3 bounding_box(const CutAOI &cut, const CutMesh &mesh);
BoundingBoxf3 bounding_box(const CutMesh &mesh);
BoundingBoxf3 bounding_box(const SurfacePatch &ecut);

SurfacePatch create_surface_patch(const std::vector<FI> &fis,
                                   CutMesh     &mesh,
                                  const ReductionMap    *rmap = nullptr);

}

void priv::create_face_types(FaceTypeMap           &map,
                             const CutMesh         &tm1,
                             const CutMesh         &tm2,
                             const EdgeBoolMap         &ecm,
                             const VertexSourceMap &sources)
{
    auto get_intersection_source = [&tm2](const Source& s1, const Source& s2)->FI{        
        FI fi1 = tm2.face(s1.hi);
        if (s1.sdim == 2) return fi1;
        FI fi2 = tm2.face(s2.hi);
        if (s2.sdim == 2) return fi2;
        if (fi1 == fi2) return fi1;

        HI hi1_opposit = tm2.opposite(s1.hi);
        FI fi1_opposit;
        if (hi1_opposit.is_valid())
            fi1_opposit = tm2.face(hi1_opposit);
        if (fi2 == fi1_opposit) return fi2;

        HI hi2_opposit = tm2.opposite(s2.hi);
        FI fi2_opposit;
        if (hi2_opposit.is_valid())
            fi2_opposit = tm2.face(hi2_opposit);
        if (fi1 == fi2_opposit) return fi1;
        if (fi1_opposit.is_valid() && fi1_opposit == fi2_opposit)
            return fi1_opposit;

        for (FI fi_around_hi1 : tm2.faces_around_target(s1.hi)) {
            for (FI fi_around_hi2 : tm2.faces_around_target(s2.hi)) { 
                if (fi_around_hi1 == fi_around_hi2) 
                    return fi_around_hi1;
            }
        }

        assert(false);
        return FI();
    };

    for (FI fi : tm1.faces()) map[fi] = FaceType::not_constrained;
    for (EI ei1 : tm1.edges()) {
        if (!get(ecm, ei1)) continue;

        HI hi1 = tm1.halfedge(ei1);
        assert(hi1.is_valid());
        FI f1a = tm1.face(hi1);
        assert(f1a.is_valid());
        HI hi_op = tm1.opposite(hi1);
        assert(hi_op.is_valid());
        FI f1b = tm1.face(hi_op);
        assert(f1b.is_valid());

        VI vi1_source = tm1.source(hi1);
        assert(vi1_source.is_valid());
        VI vi1_target = tm1.target(hi1);
        assert(vi1_target.is_valid());

        const Source &s_s = sources[vi1_source];
        const Source &s_t = sources[vi1_target];
        FI fi2 = get_intersection_source(s_s, s_t);

        if (!fi2.is_valid()) continue;

        HI hi2 = tm2.halfedge(fi2);
        std::array<const P3 *, 3> t;
        size_t ti =0;
        for (VI vi2 : tm2.vertices_around_face(hi2))
            t[ti++] = &tm2.point(vi2);

        VI vi1a_tip = tm1.target(tm1.next(hi1));
        assert(vi1a_tip.is_valid());
        const P3 &p = tm1.point(vi1a_tip);

        if (CGAL::orientation(*t[0], *t[1], *t[2], p) == CGAL::POSITIVE) {
            map[f1a] = FaceType::inside;
            map[f1b] = FaceType::outside;
        } else {
            map[f1a] = FaceType::outside;
            map[f1b] = FaceType::inside;
        }
    }
}

#include <CGAL/Polygon_mesh_processing/clip.h>
bool priv::clip_cut(SurfacePatch &cut, CutMesh clipper)
{
    CutMesh& tm = cut.mesh; 
    auto is_mesh_usable_for_clip = [](const CutMesh &mesh) {
        return !mesh.is_empty() &&
               mesh.number_of_vertices() >= 3 &&
               mesh.number_of_faces() > 0 &&
               mesh.is_valid(false);
    };

    if (!is_mesh_usable_for_clip(tm) || !is_mesh_usable_for_clip(clipper))
        return false;

    if (CGAL::Polygon_mesh_processing::does_self_intersect(tm) ||
        CGAL::Polygon_mesh_processing::does_self_intersect(clipper))
        return false;

    CutMesh backup_copy = tm; 

    class ExistIntersectionClipVisitor: public CGAL::Polygon_mesh_processing::Corefinement::Default_visitor<CutMesh>
    {
        bool* exist_intersection;
    public:
        ExistIntersectionClipVisitor(bool *exist_intersection): exist_intersection(exist_intersection){}
        void intersection_point_detected(std::size_t, int , HI, HI, const CutMesh&, const CutMesh&, bool, bool)
        { *exist_intersection = true;}
    };
    bool exist_intersection = false;
    ExistIntersectionClipVisitor visitor{&exist_intersection};    
    
    const auto &np_tm = CGAL::parameters::visitor(visitor)
                            .throw_on_self_intersection(false);
    
    const auto &np_c = CGAL::parameters::throw_on_self_intersection(false);
        
    bool suc = CGAL::Polygon_mesh_processing::clip(tm, clipper, np_tm, np_c);

    assert(suc); 
    if (!exist_intersection  || !suc) {
        cut.mesh = backup_copy;
        return false;
    }
    return true;
}

BoundingBoxf3 priv::bounding_box(const CutAOI &cut, const CutMesh &mesh) {
    const P3& p_from_cut = mesh.point(mesh.target(mesh.halfedge(cut.first.front())));
    Vec3d min = to_vec3d(p_from_cut);
    Vec3d max = min;
    for (FI fi : cut.first) { 
        for(VI vi: mesh.vertices_around_face(mesh.halfedge(fi))){
            const P3& p = mesh.point(vi);
            for (size_t i = 0; i < 3; ++i) { 
                if (min[i] > p[i]) min[i] = p[i];
                if (max[i] < p[i]) max[i] = p[i];
            }
        } 
    }
    return BoundingBoxf3(min, max);
}

BoundingBoxf3 priv::bounding_box(const CutMesh &mesh)
{
    Vec3d min = to_vec3d(*mesh.points().begin());
    Vec3d max = min;
    for (VI vi : mesh.vertices()) {
        const P3 &p = mesh.point(vi);
        for (size_t i = 0; i < 3; ++i) {
            if (min[i] > p[i]) min[i] = p[i];
            if (max[i] < p[i]) max[i] = p[i];
        }
    }
    return BoundingBoxf3(min, max);
}

BoundingBoxf3 priv::bounding_box(const SurfacePatch &ecut) {
    return bounding_box(ecut.mesh);
}

priv::SurfacePatch priv::create_surface_patch(const std::vector<FI> &fis,
                                               CutMesh   &mesh,
                                              const ReductionMap    *rmap)
{
    auto is_counted = mesh.add_property_map<VI, bool>("v:is_counted").first;
    uint32_t count_vertices = 0;
    if (rmap == nullptr) {
        for (FI fi : fis) 
            for (VI vi : mesh.vertices_around_face(mesh.halfedge(fi))) 
                if (!is_counted[vi]) { 
                    is_counted[vi] = true;
                    ++count_vertices;
                }
    } else {
        for (FI fi : fis)
            for (VI vi : mesh.vertices_around_face(mesh.halfedge(fi))) {
                if ((*rmap)[vi].is_valid()) continue;
                if (!is_counted[vi]) {
                    is_counted[vi] = true;
                    ++count_vertices;
                }
            }        
    }
    mesh.remove_property_map(is_counted);

    uint32_t count_faces = fis.size();    
    uint32_t count_edges = count_faces*3; 

    CutMesh cm;
    cm.reserve(count_vertices, count_edges, count_faces);

    CvtVI2VI mesh2result = mesh.add_property_map<VI,VI>("v:mesh2result").first;

    if (rmap == nullptr) {
        for (FI fi : fis) {
            std::array<VI, 3> t;
            int index = 0;
            for (VI vi : mesh.vertices_around_face(mesh.halfedge(fi))) {
                VI &vi_cvt = mesh2result[vi];
                if (!vi_cvt.is_valid()) {
                    vi_cvt = VI(cm.vertices().size());
                    cm.add_vertex(mesh.point(vi));
                }
                t[index++] = vi_cvt;
            }
            cm.add_face(t[0], t[1], t[2]);
        }
    } else {
        for (FI fi :fis) {
            std::array<VI, 3> t;
            int  index = 0;
            bool exist_reduction = false;
            for (VI vi : mesh.vertices_around_face(mesh.halfedge(fi))) {
                VI vi_r = (*rmap)[vi];
                if (vi_r.is_valid()) { 
                    exist_reduction = true;
                    vi = vi_r;
                }
                VI &vi_cvt = mesh2result[vi];
                if (!vi_cvt.is_valid()) {
                    vi_cvt = VI(cm.vertices().size());
                    cm.add_vertex(mesh.point(vi));
                }
                t[index++] = vi_cvt;
            }

            if (exist_reduction &&
                (t[0] == t[1] || 
                 t[1] == t[2] ||
                 t[2] == t[0]))
                continue;

            cm.add_face(t[0], t[1], t[2]);
        }
    }
    
    assert(count_vertices == cm.vertices().size());
    assert((rmap == nullptr && count_faces == cm.faces().size()) ||
           (rmap != nullptr && count_faces >= cm.faces().size()));
    assert(count_edges >= cm.edges().size());
    
    CvtVI2VI cvt = cm.add_property_map<VI, VI>(patch_source_name).first;
    for (VI vi_s : mesh.vertices()) { 
        VI vi_d = mesh2result[vi_s];
        if (!vi_d.is_valid()) continue;
        cvt[vi_d] = vi_s;
    }
    mesh.remove_property_map(mesh2result);
    return {std::move(cm)};
}

namespace priv {

struct SurfacePatchEx
{
    SurfacePatch patch;

    bool full_inside = false;
    bool just_cliped = false;
};
using SurfacePatchesEx = std::vector<SurfacePatchEx>;


using BBS = std::vector<BoundingBoxf3>;
BBS create_bbs(const VCutAOIs &cuts, const CutMeshes &cut_models);

using Primitive = CGAL::AABB_face_graph_triangle_primitive<CutMesh>;
using Traits    = CGAL::AABB_traits_3<EpicKernel, Primitive>;
using Ray       = EpicKernel::Ray_3;
using Tree      = CGAL::AABB_tree<Traits>;
using Trees     = std::vector<Tree>;
Trees create_trees(const CutMeshes &models);

bool has_bb_intersection(const BoundingBoxf3  &bb,
                         size_t                model_index,
                         const BBS            &bbs,
                         const ModelCut2index &m2i);

bool is_patch_inside_of_model(const SurfacePatch &patch,
                              const Tree         &tree,
                              const Project3d    &projection);

uint32_t get_shape_point_index(const CutAOI &cut, const CutMesh &model);

SurfacePatch separate_patch(const std::vector<FI>   &fis,
                             SurfacePatch &patch,
                            const CvtVI2VI     &cvt_from);

void divide_patch(size_t i, SurfacePatchesEx &patches);

void collect_open_edges(SurfacePatches &patches);

}

std::vector<BoundingBoxf3> priv::create_bbs(const VCutAOIs        &cuts,
                                            const CutMeshes &cut_models)
{
    size_t count = 0;
    for (const CutAOIs &cut : cuts) count += cut.size();

    std::vector<BoundingBoxf3> bbs;
    bbs.reserve(count);
    for (size_t model_index = 0; model_index < cut_models.size(); ++model_index) {
        const CutMesh &cut_model = cut_models[model_index];
        const CutAOIs &cutAOIs   = cuts[model_index];
        for (size_t cut_index = 0; cut_index < cutAOIs.size(); ++cut_index) {
            const CutAOI &cut = cutAOIs[cut_index];
            bbs.push_back(bounding_box(cut, cut_model));
        }
    }
    return bbs;
}


priv::Trees priv::create_trees(const CutMeshes &models) {
    Trees result;
    result.reserve(models.size());
    for (const CutMesh &model : models) {
        Tree tree;
        tree.insert(faces(model).first, faces(model).second, model);
        tree.build();
        result.emplace_back(std::move(tree));
    }
    return result;
}

bool priv::has_bb_intersection(const BoundingBoxf3  &bb,
                               size_t                model_index,
                               const BBS            &bbs,
                               const ModelCut2index &m2i)
{
    const auto&offsets = m2i.get_offsets();
    size_t start = offsets[model_index];
    size_t next  = model_index + 1;
    size_t end   = (next < offsets.size()) ? offsets[next] : m2i.get_count();
    for (size_t bb_index = start; bb_index < end; bb_index++)
        if (bb.intersects(bbs[bb_index])) return true;
    return false;
}

bool priv::is_patch_inside_of_model(const SurfacePatch &patch,
                                    const Tree         &tree,
                                    const Project3d    &projection)
{
    const P3 &a = patch.mesh.point(VI(0));
    Vec3d a_ = to_vec3d(a);
    Vec3d b_ = projection.project(a_);
    P3 b(b_.x(), b_.y(), b_.z());

    Ray ray_query(a, b);
    size_t count = tree.number_of_intersected_primitives(ray_query);
    bool is_in = (count % 2) == 1;

    assert(((tree.number_of_intersected_primitives(
                    Ray(a, P3(2 * a.x() - b.x(), 
                            2 * a.y() - b.y(),
                            2 * a.z() - b.z()))) %
                2) == 1) == is_in);
    return is_in;        
}

uint32_t priv::get_shape_point_index(const CutAOI &cut, const CutMesh &model)
{
    const VertexShapeMap vert_shape_map = model.property_map<VI, const IntersectingElement *>(vert_shape_map_name).value();
    for (HI hi : cut.second) {
        VI vi = model.source(hi);
        const IntersectingElement *ie = vert_shape_map[vi];
        if (ie == nullptr) continue;
        assert(ie->shape_point_index != std::numeric_limits<uint32_t>::max());
        return ie->shape_point_index;
    }
    assert(false);
    return 0;
}

priv::SurfacePatch priv::separate_patch(const std::vector<FI>& fis,
                                        SurfacePatch &patch,
                                        const CvtVI2VI     &cvt_from)
{
    assert(patch.mesh.is_valid());    
    SurfacePatch patch_new = create_surface_patch(fis, patch.mesh);
    patch_new.bb           = bounding_box(patch_new.mesh);
    patch_new.aoi_id       = patch.aoi_id;
    patch_new.model_id     = patch.model_id;
    patch_new.shape_id     = patch.shape_id;
    CvtVI2VI cvt = patch_new.mesh.property_map<VI, VI>(patch_source_name).value();
    for (VI &vi : cvt) {
        if (!vi.is_valid()) continue;
        vi = cvt_from[vi];
    }
    return patch_new;
}

void priv::divide_patch(size_t i, SurfacePatchesEx &patches)
{
    SurfacePatchEx &patch_ex = patches[i];
    assert(patch_ex.just_cliped); 
    patch_ex.just_cliped = false;

    SurfacePatch& patch = patch_ex.patch;
    CutMesh& cm = patch.mesh;
    assert(!cm.faces().empty());
    std::string patch_number_name = "f:patch_number";
    CutMesh::Property_map<FI,bool> is_processed = cm.add_property_map<FI, bool>(patch_number_name, false).first;
    
    const CvtVI2VI cvt_from = patch.mesh.property_map<VI, VI>(patch_source_name).value();

    std::vector<FI> fis;
    fis.reserve(cm.faces().size());

    SurfacePatchesEx new_patches;
    std::vector<FI> queue;
    for (FI fi_cm : cm.faces()) {
        if (is_processed[fi_cm]) continue;
        assert(queue.empty());
        queue.push_back(fi_cm);
        if (!fis.empty()) {
            SurfacePatchEx patch_ex_n;
            patch_ex_n.patch = separate_patch(fis, patch, cvt_from);
            patch_ex_n.patch.is_whole_aoi = false;
            new_patches.push_back(std::move(patch_ex_n));
            fis.clear();
        }
        do {
            FI fi_q = queue.back();
            queue.pop_back();
            if (is_processed[fi_q]) continue;             
            is_processed[fi_q] = true;
            fis.push_back(fi_q);
            HI hi = cm.halfedge(fi_q);
            for (FI fi : cm.faces_around_face(hi)) {
                if (!fi.is_valid()) continue;
                if (!is_processed[fi]) queue.push_back(fi);
            }
        } while (!queue.empty());
    }
    cm.remove_property_map(is_processed);
    assert(!fis.empty());

    if (new_patches.empty()) {
        patch.bb = bounding_box(cm);
        patch.is_whole_aoi = false;
    } else {
        patch = separate_patch(fis, patch, cvt_from);
        patches.insert(patches.end(), new_patches.begin(), new_patches.end());
    }
}

void priv::collect_open_edges(SurfacePatches &patches) {
    std::vector<HI> open_half_edges;
    for (SurfacePatch &patch : patches) {
        open_half_edges.clear();
        const CutMesh &mesh = patch.mesh;
        for (FI fi : mesh.faces()) {
            HI hi1 = mesh.halfedge(fi);
            assert(hi1.is_valid());
            HI hi2 = mesh.next(hi1);
            assert(hi2.is_valid());
            HI hi3 = mesh.next(hi2);
            assert(hi3.is_valid());
            assert(mesh.next(hi3) == hi1);
            for (HI hi : {hi1, hi2, hi3}) {
                HI hi_op = mesh.opposite(hi);
                FI fi_op = mesh.face(hi_op);
                if (!fi_op.is_valid())
                    open_half_edges.push_back(hi);
            }
        }
        patch.loops = create_loops(open_half_edges, mesh);
    }
}

priv::SurfacePatches priv::diff_models(VCutAOIs            &cuts,
                                        CutMeshes &cut_models,
                                        CutMeshes &models,
                                       const Project3d     &projection)
{
    priv::ModelCut2index m2i(cuts);

    std::vector<BoundingBoxf3> bbs = create_bbs(cuts, cut_models);
    Trees trees(models.size());

    SurfacePatches patches;

    SurfacePatchesEx aoi_patches;

    patches.reserve(m2i.get_count());
    size_t index = 0;
    for (size_t model_index = 0; model_index < models.size(); ++model_index) {
        CutAOIs &model_cuts = cuts[model_index];
        CutMesh &cut_model_ = cut_models[model_index];
        const CutMesh &cut_model = cut_model_;
        ReductionMap vertex_reduction_map = cut_model_.add_property_map<VI, VI>(vertex_reduction_map_name).first;
        create_reduce_map(vertex_reduction_map, cut_model);

        for (size_t cut_index = 0; cut_index < model_cuts.size(); ++cut_index, ++index) {
            const CutAOI &cut = model_cuts[cut_index];
            SurfacePatchEx patch_ex;
            SurfacePatch  &patch = patch_ex.patch;
            patch = create_surface_patch(cut.first, cut_model_, &vertex_reduction_map);
            patch.bb = bbs[index];
            patch.aoi_id   = cut_index;
            patch.model_id = model_index;
            patch.shape_id = get_shape_point_index(cut, cut_model);
            patch.is_whole_aoi = true;

            aoi_patches.clear();
            aoi_patches.push_back(patch_ex);
            for (size_t model_index2 = 0; model_index2 < models.size(); ++model_index2) {
                if (model_index == model_index2) continue;
                for (SurfacePatchEx &patch_ex : aoi_patches) {
                    SurfacePatch &patch = patch_ex.patch;
                    if (has_bb_intersection(patch.bb, model_index2, bbs, m2i) &&
                        clip_cut(patch, models[model_index2])){
                        patch_ex.just_cliped = true;
                    } else { 
                        Tree &tree = trees[model_index2];
                        if (tree.empty()) {
                            const CutMesh &model   = models[model_index2];
                            auto           f_range = faces(model);
                            tree.insert(f_range.first, f_range.second, model);
                            tree.build();
                        }
                        if (is_patch_inside_of_model(patch, tree, projection))
                            patch_ex.full_inside = true;
                    }
                }
                for (size_t i = aoi_patches.size(); i != 0; --i) {
                    auto it = aoi_patches.begin() + (i - 1);
                    if (it->full_inside) aoi_patches.erase(it);
                }

                if (aoi_patches.empty()) break;

                size_t end = aoi_patches.size();
                for (size_t i = 0; i < end; ++i)
                    if (aoi_patches[i].just_cliped)
                        divide_patch(i, aoi_patches);
            }

            if (!aoi_patches.empty()) {
                patches.reserve(patches.size() + aoi_patches.size());
                for (SurfacePatchEx &patch : aoi_patches)
                    patches.push_back(std::move(patch.patch));
                
            }
        }
        cut_model_.remove_property_map(vertex_reduction_map);
    }

    collect_open_edges(patches);
    return patches;
}

bool priv::is_over_whole_expoly(const SurfacePatch &patch,
                                const ExPolygons   &shapes,
                                const VCutAOIs     &cutAOIs,
                                const CutMeshes    &meshes)
{
    if (!patch.is_whole_aoi) return false;
    return is_over_whole_expoly(cutAOIs[patch.model_id][patch.aoi_id],
                                shapes[patch.shape_id],
                                meshes[patch.model_id]);
}

bool priv::is_over_whole_expoly(const CutAOI    &cutAOI,
                                const ExPolygon &shape,
                                const CutMesh   &mesh)
{
    const VertexShapeMap vert_shape_map = mesh.property_map<VI, const IntersectingElement*>(vert_shape_map_name).value();
    for (HI hi : cutAOI.second) { 
        const IntersectingElement *ie_s = vert_shape_map[mesh.source(hi)];
        const IntersectingElement *ie_t = vert_shape_map[mesh.target(hi)];
        if (ie_s == nullptr || ie_t == nullptr) 
            return false;

        assert(ie_s->attr != (unsigned char) IntersectingElement::Type::undefined);
        assert(ie_t->attr != (unsigned char) IntersectingElement::Type::undefined);

        uint32_t i_s = ie_s->shape_point_index;
        uint32_t i_t = ie_t->shape_point_index;
        assert(i_s != std::numeric_limits<uint32_t>::max());
        assert(i_t != std::numeric_limits<uint32_t>::max());
        if (i_s == std::numeric_limits<uint32_t>::max() ||
            i_t == std::numeric_limits<uint32_t>::max())
            return false;

        if (i_s == i_t) continue;

        if (i_s > i_t) { 
            std::swap(i_s, i_t);
            std::swap(ie_s, ie_t);
        }
        bool is_last_polygon_segment = ie_s->is_first() && ie_t->is_last();
        if (is_last_polygon_segment) {
            std::swap(i_s, i_t);
            std::swap(ie_s, ie_t);        
        }

        if (!is_last_polygon_segment &&
            (ie_s->is_last() || (i_s + 1) != i_t))
            return false;
        
        IntersectingElement::Type t_s = ie_s->get_type();
        IntersectingElement::Type t_t = ie_t->get_type();
        if (t_s == IntersectingElement::Type::undefined ||
            t_t == IntersectingElement::Type::undefined)
            return false;

        if (t_t != IntersectingElement::Type::edge_1)
            return false;

        if (t_s == IntersectingElement::Type::face_1)
            return false;        
    }

    auto is_open = [&mesh](HI hi)->bool {
        HI opposite = mesh.opposite(hi);
        return !mesh.face(opposite).is_valid();
    };

    std::vector<HI> opens;
    opens.reserve(cutAOI.second.size());
    for (HI hi : cutAOI.second)
        if (is_open(hi)) opens.push_back(hi);
    std::sort(opens.begin(), opens.end());

    for (FI fi: cutAOI.first) {
        HI face_hi = mesh.halfedge(fi);
        for (HI hi : mesh.halfedges_around_face(face_hi)) {
            if (!is_open(hi)) continue;
            auto lb = std::lower_bound(opens.begin(), opens.end(), hi);
            if (lb == opens.end() || *lb != hi)
                return false;
        }
    }
    return true;
}

std::vector<bool> priv::select_patches(const ProjectionDistances &best_distances,
                                       const SurfacePatches      &patches,
                                       const ExPolygons          &shapes,
                                       const BoundingBox         &shapes_bb,
                                       const ExPolygonsIndices   &s2i,
                                       const VCutAOIs            &cutAOIs,
                                       const CutMeshes           &meshes,
                                       const Project             &projection)
{
    Point s = shapes_bb.size();
    const float extend_delta = (s.x() + s.y())/ float(2 * 100);
        
    std::vector<std::vector<uint32_t>> used_shapes_patches(shapes.size());    
    std::vector<bool> in_distances(patches.size(), {false});
    for (const ProjectionDistance &d : best_distances) {
        if (d.patch_index == std::numeric_limits<uint32_t>::max()) continue;
        if (in_distances[d.patch_index]) continue;
        in_distances[d.patch_index] = true;

        ExPolygonsIndex id = s2i.cvt(&d - &best_distances.front());
        used_shapes_patches[id.expolygons_index].push_back(d.patch_index);
    }

    std::vector<std::vector<uint32_t>> shapes_patches(shapes.size());
    for (const SurfacePatch &patch : patches) 
        shapes_patches[patch.shape_id].push_back(&patch - &patches.front());

#ifdef DEBUG_OUTPUT_DIR
    std::string store_dir = DEBUG_OUTPUT_DIR + "select_patches/";
    prepare_dir(store_dir);
#endif

    for (size_t shape_index = 0; shape_index < shapes.size(); shape_index++) {
        const ExPolygon &shape = shapes[shape_index];
        std::vector<uint32_t> &used_shape_patches = used_shapes_patches[shape_index];
        if (used_shape_patches.empty()) continue;
        if (used_shapes_patches.size() == shapes_patches[shape_index].size()) continue;
        if (used_shape_patches.size() == 1) { 
            uint32_t patch_index = used_shape_patches.front();
            const SurfacePatch &patch = patches[patch_index];
            if (is_over_whole_expoly(patch, shapes, cutAOIs, meshes)) continue;
        }

        ExPolygons fill;
        fill.reserve(used_shape_patches.size());

        Vec2d used_patches_depth(std::numeric_limits<double>::max(), std::numeric_limits<double>::min());
        for (uint32_t patch_index : used_shape_patches) {
            ExPolygon patch_area = to_expoly(patches[patch_index], projection, used_patches_depth);
            ExPolygons patch_areas = offset_ex(patch_area, extend_delta);
            fill.insert(fill.end(), patch_areas.begin(), patch_areas.end());
        }
        fill = union_ex(fill);

        ExPolygons rest = diff_ex(ExPolygons{shape}, fill, ApplySafetyOffset::Yes);
#ifdef DEBUG_OUTPUT_DIR
        BoundingBox shape_bb = get_extents(shape);
        SVG svg(store_dir + "shape_" + std::to_string(shape_index) + ".svg", shape_bb);
        svg.draw(fill, "darkgreen");
        svg.draw(rest, "green");
#endif
        if (rest.empty()) continue;

        struct PatchShape{
            uint32_t patch_index;
            ExPolygon shape;
            ExPolygons intersection;
            double depth_range_center_distance;
        };
        using PatchShapes = std::vector<PatchShape>;
        PatchShapes patch_shapes;

        double used_patches_depth_center = (used_patches_depth[0] + used_patches_depth[1]) / 2;

        std::sort(used_shape_patches.begin(), used_shape_patches.end());
        for (uint32_t patch_index : shapes_patches[shape_index]) { 
            auto it = std::lower_bound(used_shape_patches.begin(), used_shape_patches.end(), patch_index);
            if (it != used_shape_patches.end() && *it == patch_index) continue;

            Vec2d patche_depth_range(std::numeric_limits<double>::max(), std::numeric_limits<double>::min());
            ExPolygon patch_shape = to_expoly(patches[patch_index], projection, patche_depth_range);
            double depth_center = (patche_depth_range[0] + patche_depth_range[1]) / 2;
            double depth_range_center_distance = std::fabs(used_patches_depth_center - depth_center);

            ExPolygons patch_intersection = intersection_ex(ExPolygons{patch_shape}, rest);
            if (patch_intersection.empty()) continue;

            patch_shapes.push_back({patch_index, patch_shape, patch_intersection, depth_range_center_distance});
        }

        if (patch_shapes.empty()) continue;
        if (patch_shapes.size() == 1) {
            used_shape_patches.push_back(patch_shapes.front().patch_index);        
            continue;
        }

        std::sort(patch_shapes.begin(), patch_shapes.end(), [](const PatchShape &a, const PatchShape &b) 
            { return a.depth_range_center_distance < b.depth_range_center_distance; });

#ifdef DEBUG_OUTPUT_DIR
        for (size_t i = patch_shapes.size(); i > 0; --i) { 
            const PatchShape &p = patch_shapes[i - 1];
            int gray_level =  (i * 200) / patch_shapes.size();
            std::stringstream color;
            color << "#" << std::hex << std::setfill('0') << std::setw(2) << gray_level << gray_level << gray_level;
            svg.draw(p.shape, color.str());
            Point text_pos = get_extents(p.shape).center().cast<int>();
            svg.draw_text(text_pos, std::to_string(i-1).c_str(), "orange", std::ceil(shape_bb.size().x() / 20 * 0.000001));
        }
#endif

        for (const PatchShape &patch : patch_shapes) { 
            ExPolygons patch_intersection = intersection_ex(patch.intersection, rest);
            if (patch_intersection.empty()) continue;

            ExPolygons intersection = offset_ex(patch.intersection, extend_delta);
            rest = diff_ex(rest, intersection, ApplySafetyOffset::Yes);

            used_shape_patches.push_back(patch.patch_index);
            if (rest.empty()) break;
        }

    }

    std::vector<bool> result(patches.size(), {false});
    for (const std::vector<uint32_t> &patches: used_shapes_patches)
        for (uint32_t patch_index : patches) { 
            assert(patch_index < result.size());
            assert(!result[patch_index]);
            result[patch_index] = true;
        }
    return result;
}

priv::Loops priv::create_loops(const std::vector<HI> &outlines, const CutMesh& mesh)
{
    Loops loops;
    Loops unclosed;
    for (HI hi : outlines) {
        VI vi_s = mesh.source(hi);
        VI vi_t = mesh.target(hi);
        Loop *loop_move    = nullptr;
        Loop *loop_connect = nullptr;
        for (std::vector<VI> &cut : unclosed) {
            if (cut.back() != vi_s) continue;
            if (cut.front() == vi_t) {
                loop_move = &cut;
            } else {
                loop_connect = &cut;
            }
            break;
        }
        if (loop_move != nullptr) {
            size_t index = loop_move - &unclosed.front();
            loops.emplace_back(std::move(*loop_move));
            unclosed.erase(unclosed.begin() + index);
        } else if (loop_connect != nullptr) {
            Loop *loop_tail = nullptr;
            for (Loop &cut : unclosed) {
                if (cut.front() != vi_t) continue;
                loop_tail = &cut;
                break;
            }
            if (loop_tail != nullptr) {
                size_t index = loop_tail - &unclosed.front();
                loop_connect->insert(loop_connect->end(),
                                    make_move_iterator(loop_tail->begin()),
                                    make_move_iterator(loop_tail->end()));
                unclosed.erase(unclosed.begin() + index);
            } else {
                loop_connect->push_back(vi_t);
            }
        } else {
            bool create_cut = true;
            for (Loop &cut : unclosed) {
                if (cut.front() != vi_t) continue;
                cut.insert(cut.begin(), vi_s);
                create_cut = false;
                break;
            }
            if (create_cut)
                unclosed.emplace_back(std::vector{vi_s, vi_t});
        }
    }
    assert(unclosed.empty());
    return loops;
}

Polygons priv::unproject_loops(const SurfacePatch &patch, const Project &projection, Vec2d &depth_range)
{
    assert(!patch.loops.empty());
    if (patch.loops.empty()) return {};

    Polygons polys;
    polys.reserve(patch.loops.size());

    size_t count = 0;
    for (const Loop &l : patch.loops) count += l.size();
    std::vector<float> depths;
    depths.reserve(count);

    Points pts;
    for (const Loop &l : patch.loops) {
        pts.clear();
        pts.reserve(l.size());
        for (VI vi : l) {
            const P3 &p3 = patch.mesh.point(vi);
            Vec3d p = to_vec3d(p3);
            double depth;
            std::optional<Vec2d> p2_opt = projection.unproject(p, &depth);
            if (depth_range[0] > depth) depth_range[0] = depth;
            if (depth_range[1] < depth) depth_range[1] = depth;
            assert(p2_opt.has_value());
            if (!p2_opt.has_value()) continue;

            pts.push_back(p2_opt->cast<Point::coord_type>());
            depths.push_back(static_cast<float>(depth));
        }
        assert(pts.size() >= 3);
        if (pts.size() < 3) continue;

        polys.emplace_back(pts);
    }

    assert(!polys.empty());
    return polys;
}

ExPolygon priv::to_expoly(const SurfacePatch &patch, const Project &projection, Vec2d &depth_range)
{
    Polygons polys = unproject_loops(patch, projection, depth_range);
    PolyFillType fill_type = pftEvenOdd;
    ExPolygons expolys = Slic3r::union_ex(polys, fill_type);
    if (expolys.size() == 1)
        return expolys.front();

    assert(false);

    if (expolys.empty()) return {};
    const ExPolygon *biggest = &expolys.front();
    for (size_t index = 1; index < expolys.size(); ++index) {
        const ExPolygon *current = &expolys[index];
        if (biggest->contour.size() < current->contour.size())
            biggest = current;
    }
    return *biggest;
}

SurfaceCut priv::patch2cut(SurfacePatch &patch)
{
    CutMesh &mesh = patch.mesh;

    std::string convert_map_name = "v:convert";
    CutMesh::Property_map<VI, SurfaceCut::Index> convert_map = 
        mesh.add_property_map<VI, SurfaceCut::Index>(convert_map_name).first;

    size_t indices_size  = mesh.faces().size();
    size_t vertices_size = mesh.vertices().size();

    SurfaceCut sc;
    sc.indices.reserve(indices_size);
    sc.vertices.reserve(vertices_size);
    for (VI vi : mesh.vertices()) {
        convert_map[vi] = sc.vertices.size();
        const P3 &p = mesh.point(vi);
        sc.vertices.emplace_back(p.x(), p.y(), p.z());
    }

    for (FI fi : mesh.faces()) {
        HI hi = mesh.halfedge(fi);
        assert(mesh.next(hi).is_valid());
        assert(mesh.next(mesh.next(hi)).is_valid());
        assert(mesh.next(mesh.next(mesh.next(hi))) == hi);

        Vec3i32 ti;
        size_t i = 0;
        for (VI vi : { mesh.source(hi), 
                       mesh.target(hi), 
                       mesh.target(mesh.next(hi))})
            ti[i++] = convert_map[vi];
        sc.indices.push_back(ti);
    }

    sc.contours.reserve(patch.loops.size());
    for (const Loop &loop : patch.loops) { 
        sc.contours.push_back({});
        std::vector<SurfaceCut::Index> &contour = sc.contours.back();
        contour.reserve(loop.size());
        for (VI vi : loop) contour.push_back(convert_map[vi]);
    }

    mesh.remove_property_map(convert_map);
    return sc;
}

void priv::append(SurfaceCut &sc, SurfaceCut &&sc_add)
{
    if (sc.empty()) {
        sc = std::move(sc_add);
        return;
    }

    if (!sc_add.contours.empty()) {
        SurfaceCut::Index offset = static_cast<SurfaceCut::Index>(
            sc.vertices.size());
        size_t require = sc.contours.size() + sc_add.contours.size();
        if (sc.contours.capacity() < require) sc.contours.reserve(require);
        for (std::vector<SurfaceCut::Index> &cut : sc_add.contours)
            for (SurfaceCut::Index &i : cut) i += offset;
        Slic3r::append(sc.contours, std::move(sc_add.contours));
    }
    its_merge(sc, std::move(sc_add));
}

SurfaceCut priv::merge_patches(SurfacePatches &patches, const std::vector<bool>& mask)
{
    SurfaceCut result;
    for (SurfacePatch &patch : patches) {
        size_t index = &patch - &patches.front();
        if (!mask[index]) continue;
        append(result, patch2cut(patch));
    }    
    return result;
}

#ifdef DEBUG_OUTPUT_DIR
void priv::prepare_dir(const std::string &dir){
    namespace fs = std::filesystem;
    if (fs::exists(dir)) {
        for (auto &path : fs::directory_iterator(dir)) fs::remove_all(path);
    } else {
        fs::create_directories(dir);
    }
}

namespace priv{
int reduction_order = 0;
int filled_order    = 0;
int constrained_order = 0;
int diff_patch_order  = 0;

}

void priv::initialize_store(const std::string& dir)
{
    prepare_dir(dir);
    reduction_order   = 0;
    filled_order      = 0;
    constrained_order = 0;
    diff_patch_order  = 0;
}

void priv::store(const Vec3f       &vertex,
                 const Vec3f       &normal,
                 const std::string &file,
                 float              size)
{
    int    flatten = 20;
    size_t min_i   = 0;
    for (size_t i = 1; i < 3; i++)
        if (normal[min_i] > normal[i]) min_i = i;
    Vec3f up_  = Vec3f::Zero();
    up_[min_i] = 1.f;
    Vec3f side = normal.cross(up_).normalized() * size;
    Vec3f up   = side.cross(normal).normalized() * size;

    indexed_triangle_set its;
    its.vertices.reserve(flatten + 1);
    its.indices.reserve(flatten);

    its.vertices.push_back(vertex);
    its.vertices.push_back(vertex + up);
    size_t max_i = static_cast<size_t>(flatten);
    for (size_t i = 1; i < max_i; i++) {
        float angle = i * 2 * M_PI / flatten;
        Vec3f v     = vertex + sin(angle) * side + cos(angle) * up;
        its.vertices.push_back(v);
        its.indices.emplace_back(0, i, i + 1);
    }
    its.indices.emplace_back(0, flatten, 1);
    its_write_obj(its, file.c_str());
}

void priv::store(const CutMesh &mesh, const FaceTypeMap &face_type_map, const std::string& dir, bool is_filled)
{
    std::string off_file;
    if (is_filled) {
        if (filled_order == 0) prepare_dir(dir);
        off_file = dir + "model" + std::to_string(filled_order++) + ".off";
    }else{
        if (constrained_order == 0) prepare_dir(dir);
        off_file = dir + "model" + std::to_string(constrained_order++) + ".off";
    }

    CutMesh &mesh_ = const_cast<CutMesh &>(mesh);
    auto face_colors = mesh_.add_property_map<priv::FI, CGAL::Color>("f:color").first;    
    for (FI fi : mesh.faces()) { 
        auto &color = face_colors[fi];
        switch (face_type_map[fi]) {
        case FaceType::inside: color = CGAL::Color{100, 250, 100}; break;
        case FaceType::inside_processed: color = CGAL::Color{170, 0, 0}; break;
        case FaceType::outside: color = CGAL::Color{100, 0, 100}; break;
        case FaceType::not_constrained: color = CGAL::Color{127, 127, 127}; break;
        default: color = CGAL::Color{0, 0, 255};
        }
    }
    CGAL::IO::write_OFF(off_file, mesh, CGAL::parameters::face_color_map(face_colors));
    mesh_.remove_property_map(face_colors);
}

void priv::store(const ExPolygons &shapes, const std::string &svg_file) { 
    SVG svg(svg_file);
    svg.draw(shapes);
}

void priv::store(const CutMesh &mesh, const ReductionMap &reduction_map, const std::string& dir)
{
    if (reduction_order == 0) prepare_dir(dir);
    std::string off_file = dir + "model" + std::to_string(reduction_order++) + ".off";

    CutMesh &mesh_ = const_cast<CutMesh &>(mesh);
    auto vertex_colors = mesh_.add_property_map<priv::VI, CGAL::Color>("v:color").first;    
    for (VI vi: mesh.vertices())
        vertex_colors[vi] = CGAL::Color{127, 127, 127};

    for (VI reduction_from : mesh.vertices()) {
        VI reduction_to = reduction_map[reduction_from];
        if (!reduction_to.is_valid()) continue;
        vertex_colors[reduction_from] = CGAL::Color{255, 0, 0};
        vertex_colors[reduction_to] = CGAL::Color{0, 0, 255};
    }
    
    CGAL::IO::write_OFF(off_file, mesh, CGAL::parameters::vertex_color_map(vertex_colors));
    mesh_.remove_property_map(vertex_colors);
}

namespace priv {
indexed_triangle_set create_indexed_triangle_set(const std::vector<FI> &faces,
                                                 const CutMesh         &mesh);
}

indexed_triangle_set priv::create_indexed_triangle_set(
    const std::vector<FI> &faces, const CutMesh &mesh)
{
    std::vector<VI> vertices;
    vertices.reserve(faces.size() * 2);

    indexed_triangle_set its;
    its.indices.reserve(faces.size());
    for (FI fi : faces) {
        HI hi     = mesh.halfedge(fi);
        HI hi_end = hi;

        int   ti = 0;
        Vec3i32 t;

        do {
            VI   vi  = mesh.source(hi);
            auto res = std::find(vertices.begin(), vertices.end(), vi);
            t[ti++]  = res - vertices.begin();
            if (res == vertices.end()) vertices.push_back(vi);
            hi = mesh.next(hi);
        } while (hi != hi_end);

        its.indices.push_back(t);
    }

    its.vertices.reserve(vertices.size());
    for (VI vi : vertices) {
        const auto &p = mesh.point(vi);
        its.vertices.emplace_back(p.x(), p.y(), p.z());
    }
    return its;
}

void priv::store(const CutAOIs &aois, const CutMesh &mesh, const std::string &dir) {
    auto create_outline_its =
        [&mesh](const std::vector<HI> &outlines) -> indexed_triangle_set {
        static const float line_width = 0.1f;
        indexed_triangle_set its;
        its.indices.reserve(2*outlines.size());
        its.vertices.reserve(outlines.size()*4);
        for (HI hi : outlines) { 
            VI vi_a = mesh.source(hi);
            VI vi_b = mesh.target(hi);
            VI vi_c = mesh.target(mesh.next(hi));
            P3 p3_a = mesh.point(vi_a);
            P3 p3_b = mesh.point(vi_b);
            P3 p3_c = mesh.point(vi_c);

            Vec3f a(p3_a.x(), p3_a.y(), p3_a.z());
            Vec3f b(p3_b.x(), p3_b.y(), p3_b.z());
            Vec3f c(p3_c.x(), p3_c.y(), p3_c.z());

            Vec3f v1 = b - a;
            v1.normalize();
            Vec3f v2 = c - a;
            v2.normalize();
            Vec3f norm = v1.cross(v2);
            norm.normalize();
            Vec3f perp_to_edge = norm.cross(v1);
            perp_to_edge.normalize();
            Vec3f dir = -perp_to_edge * line_width;

            size_t ai = its.vertices.size();
            its.vertices.push_back(a);
            size_t bi = its.vertices.size();
            its.vertices.push_back(b);
            size_t ai2 = its.vertices.size();
            its.vertices.push_back(a + dir);
            size_t bi2 = its.vertices.size();
            its.vertices.push_back(b + dir);

            its.indices.push_back(Vec3i32(ai, ai2, bi));
            its.indices.push_back(Vec3i32(ai2, bi2, bi));
        }
        return its;    
    };

    prepare_dir(dir);
    for (const auto &aoi : aois) {
        size_t      index = &aoi - &aois.front();
        std::string file  = dir + "aoi" + std::to_string(index) + ".obj";
        indexed_triangle_set its = create_indexed_triangle_set(aoi.first, mesh);
        its_write_obj(its, file.c_str());

        if (aoi.second.empty()) continue;
        std::string file_outline = dir + "outline" + std::to_string(index) + ".obj";
        indexed_triangle_set outline = create_outline_its(aoi.second);
        its_write_obj(outline, file_outline.c_str());
    }
}

void priv::store(const SurfacePatches &patches, const std::string &dir) {
    prepare_dir(dir);
    for (const priv::SurfacePatch &patch : patches) {
        size_t index = &patch - &patches.front();
        if (patch.mesh.faces().empty()) continue;
        CGAL::IO::write_OFF(dir + "patch" + std::to_string(index) + ".off", patch.mesh);
    }
}

void priv::store(const ExPolygons &shapes, const std::vector<bool> &mask, const Connections &connections, const std::string &file_svg)
{
    auto bb = get_extents(shapes);
    int  width = get_extents(shapes.front()).size().x() / 70;

    SVG svg(file_svg, bb);
    svg.draw(shapes);
    
    ExPolygonsIndices s2i(shapes);
    auto get_point = [&shapes, &s2i](size_t i)->Point {
        auto id = s2i.cvt(i);
        const ExPolygon &s  = shapes[id.expolygons_index];
        const Polygon   &p  = (id.polygon_index == 0) ?
                                              s.contour :
                                              s.holes[id.polygon_index - 1];
        return p[id.point_index];
    };

    bool is_first = true;
    for (const Connection &c : connections) { 
        if (is_first) {
            is_first = false;
            Point p = get_point(c.first);
            svg.draw(p, "purple", 4 * width);
            continue;
        }
        Point p1 = get_point(c.first);
        Point p2 = get_point(c.second);
        svg.draw(Line(p1, p2), "red", width);
    }

    for (size_t i = 0; i < s2i.get_count(); i++) { 
        Point p = get_point(i);
        svg.draw(p, "black", 2*width);
        if (!mask[i])
            svg.draw(p, "white", width);
    }
    svg.Close();
}

namespace priv {
indexed_triangle_set create_contour_its(const indexed_triangle_set& its, const std::vector<unsigned int> &contour);

unsigned int get_triangle_tip(unsigned int                vi1,
                              unsigned int                vi2,
                              const indexed_triangle_set &its);
}


unsigned int priv::get_triangle_tip(unsigned int                vi1,
                                    unsigned int                vi2,
                                    const indexed_triangle_set &its)
{
    assert(vi1 < its.vertices.size());
    assert(vi2 < its.vertices.size());
    for (const auto &t : its.indices) {
        unsigned int tvi = std::numeric_limits<unsigned int>::max();
        for (const auto &vi : t) {
            unsigned int vi_ = static_cast<unsigned int>(vi);
            if (vi_ == vi1) continue;
            if (vi_ == vi2) continue;
            if (tvi == std::numeric_limits<unsigned int>::max()) {
                tvi = vi_;
            } else {
                tvi = std::numeric_limits<unsigned int>::max();
                break;
            }
        }
        if (tvi != std::numeric_limits<unsigned int>::max())
            return tvi;
    }
    assert(false);
    return std::numeric_limits<unsigned int>::max();
}

indexed_triangle_set priv::create_contour_its(
    const indexed_triangle_set &its, const std::vector<unsigned int> &contour)
{
    static const float line_width = 0.1f;
    indexed_triangle_set result;
    result.vertices.reserve((contour.size() + 1) * 4);
    result.indices.reserve((contour.size() + 1) * 2);
    unsigned int prev_vi = contour.back();
    for (unsigned int vi : contour) {
        const Vec3f &a = its.vertices[vi];
        const Vec3f &b = its.vertices[prev_vi];
        const Vec3f &c = its.vertices[get_triangle_tip(vi, prev_vi, its)];

        Vec3f v1 = b - a;
        v1.normalize();
        Vec3f v2 = c - a;
        v2.normalize();
        Vec3f norm = v1.cross(v2);
        norm.normalize();
        Vec3f perp_to_edge = norm.cross(v1);
        perp_to_edge.normalize();

        Vec3f dir = -perp_to_edge * line_width;

        size_t ai = result.vertices.size();
        result.vertices.push_back(a);
        size_t bi = result.vertices.size();
        result.vertices.push_back(b);
        size_t ai2 = result.vertices.size();
        result.vertices.push_back(a + dir);
        size_t bi2 = result.vertices.size();
        result.vertices.push_back(b + dir);

        result.indices.push_back(Vec3i32(ai, bi, ai2));
        result.indices.push_back(Vec3i32(ai2, bi, bi2));
        prev_vi = vi;
    }
    return result;
}

void priv::store(const SurfaceCut &cut, const std::string &file, const std::string &contour_dir) {
    prepare_dir(contour_dir);
    its_write_obj(cut, file.c_str());
    for (const auto& contour : cut.contours) {
        size_t c_index = &contour - &cut.contours.front();
        std::string c_file  = contour_dir + std::to_string(c_index) + ".obj";
        indexed_triangle_set c_its = create_contour_its(cut, contour);
        its_write_obj(c_its, c_file.c_str());
    }
}

void priv::store(const std::vector<indexed_triangle_set> &models,
                 const std::string                       &obj_filename)
{
    indexed_triangle_set merged_model;
    for (const indexed_triangle_set &model : models)
        its_merge(merged_model, model);
    its_write_obj(merged_model, obj_filename.c_str());
}

void priv::store(const std::vector<priv::CutMesh> &models,
                 const std::string                &dir)
{
    prepare_dir(dir);
    if (models.empty()) return;
    if (models.size() == 1) {
        CGAL::IO::write_OFF(dir + "model.off", models.front());
        return;
    }
    size_t model_index = 0;
    for (const priv::CutMesh& model : models) {
        std::string filename = dir + "model" + std::to_string(model_index++) + ".off";
        CGAL::IO::write_OFF(filename, model);
    }
}

void priv::store(const Emboss::IProjection &projection,
                 const Point               &point_to_project,
                 float                      projection_ratio,
                 const std::string         &obj_filename)
{
    auto [front, back] = projection.create_front_back(point_to_project);
    Vec3d diff         = back - front;
    Vec3d pos          = front + diff * projection_ratio;
    priv::store(pos.cast<float>(), diff.normalized().cast<float>(),
                DEBUG_OUTPUT_DIR + "projection_center.obj");
}

#endif

bool Slic3r::corefine_test(const std::string &model_path, const std::string &shape_path) {
    priv::CutMesh model, shape;
    if (!CGAL::IO::read_OFF(model_path, model)) return false;
    if (!CGAL::IO::read_OFF(shape_path, shape)) return false;
        
    CGAL::Polygon_mesh_processing::corefine(model, shape);
    return true;
}
