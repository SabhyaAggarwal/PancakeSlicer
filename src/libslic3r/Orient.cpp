#include "Orient.hpp"
#include "Geometry.hpp"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <ios>
#include <iomanip>
#include "TriangleMesh.hpp"
#include "Point.hpp"
#include <functional>
#include <cmath>
#include "libslic3r.h"
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include "Model.hpp"
#include "AABBMesh.hpp"
#include "BoundingBox.hpp"
#include "PrintConfig.hpp"
#include <numeric>
#include <boost/geometry/index/rtree.hpp>
#include <boost/log/trivial.hpp>
#include <string>
#include <sstream>
#include <ostream>
#include <tbb/parallel_for.h>
#include <vector>
#include <unordered_map>
#include <utility>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/multiprecision/integer.hpp>
#include <boost/rational.hpp>
#include "BoundingBox.hpp"

#undef MAX3
#define MAX3(a,b,c) std::max(std::max(a,b),c)

#undef MEDIAN
#define MEDIAN3(a,b,c) std::max(std::min(a,b), std::min(std::max(a,b),c))
#ifndef SQ
#define SQ(x) ((x)*(x))
#endif

namespace Slic3r {

namespace orientation {

    struct CostItems {
        float overhang = 0;
        float bottom = 0;
        float bottom_hull = 0;
        float contour = 0;
        float area_laf = 0;
        float area_projected = 0;
        float volume = 0;
        float area_total = 0;
        float radius = 0;
        float height_to_bottom_hull_ratio = 0;
        float unprintability = 0;
        Eigen::VectorXf areas_cooling;
        CostItems() = default;
        static std::string field_names() {
            return "                                      overhang, bottom, bothull, contour, A_laf, A_prj, unprintability";
        }
        std::string field_values() {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << overhang << ",\t" << bottom << ",\t" << bottom_hull << ",\t" << contour << ",\t" << area_laf << ",\t" << area_projected << ",\t" << unprintability;
            return ss.str();
        }
    };





class AutoOrienter {
public:
    int face_count_hull;
    OrientMesh *orient_mesh = NULL;
    TriangleMesh* mesh;
    TriangleMesh mesh_convex_hull;
    Eigen::MatrixXf normals, normals_quantize, normals_hull, normals_hull_quantize;
    Eigen::VectorXf areas, areas_hull;
    Eigen::VectorXf is_apperance;
    Eigen::MatrixXf z_projected;
    Eigen::VectorXf z_max, z_max_hull;
    Eigen::VectorXf z_median;
    Eigen::VectorXf z_mean;
    Eigen::VectorXf areas_cooling;
    std::vector<Vec3f> face_normals;
    std::vector<Vec3f> face_normals_hull;
    OrientParams params;
    bool has_cooling_fan = false;

    std::vector< Vec3f> orientations;
    std::function<void(unsigned)> progressind = { };

public:
    AutoOrienter(OrientMesh* orient_mesh_,
                 const OrientParams           &params_,
                 std::function<void(unsigned)> progressind_,
                 std::function<bool(void)>     stopcond_)
    {
        orient_mesh = orient_mesh_;
        mesh = &orient_mesh->mesh;
        params = params_;
        has_cooling_fan = orient_mesh->has_cooling_fan;
        progressind = progressind_;
        params.ASCENT = cos(PI - orient_mesh->overhang_angle * PI / 180);




        preprocess();
    }

    AutoOrienter(TriangleMesh* mesh_)
    {
        mesh = mesh_;
        preprocess();
    }

    struct VecHash {
        size_t operator()(const Vec3f& n1) const {
            return std::hash<coord_t>()(int(n1(0)*100+100)) + std::hash<coord_t>()(int(n1(1)*100+100)) * 101 + std::hash<coord_t>()(int(n1(2)*100+100)) * 10221;
        }
    };

    Vec3f quantize_vec3f(const Vec3f n1) {
        return Vec3f(floor(n1(0) * 1000) / 1000, floor(n1(1) * 1000) / 1000, floor(n1(2) * 1000) / 1000);
    }


    void collect_orientations()
    {
        orientations = { { 0,0,-1 } };

        area_cumulation_accurate(face_normals, normals_quantize, areas, 10);

        area_cumulation_accurate(face_normals_hull, normals_hull_quantize, areas_hull, 14);

        add_supplements();

        if(progressind)
            progressind(20);

        remove_duplicates();

        if (progressind)
            progressind(30);
    }

    using Ranked = std::vector<std::pair<Vec3f, CostItems>>;


    Ranked rank(bool log)
    {
        std::unordered_map<Vec3f, CostItems, VecHash> results;
        if (log) {
            BOOST_LOG_TRIVIAL(info) << CostItems::field_names();
            std::cout << CostItems::field_names() << std::endl;
        }
        for (int i = 0; i < orientations.size();i++) {
            Vec3f orientation = -orientations[i];

            project_vertices(orientation);

            auto cost_items = get_features(orientation, params.min_volume);

            float unprintability = target_function(cost_items, params.min_volume);

            results[orientation] = cost_items;

            if (log) {
                BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(4) << "orientation:" << orientation.transpose() << ", cost:" << std::fixed << std::setprecision(4) << cost_items.field_values();
                std::cout << std::fixed << std::setprecision(4) << "orientation:" << orientation.transpose() << ", cost:" << std::fixed << std::setprecision(4) << cost_items.field_values() << std::endl;
            }
        }
        if (progressind)
            progressind(60);

        Ranked results_vector(results.begin(), results.end());
        sort(results_vector.begin(), results_vector.end(), [](const auto& p1, const auto& p2) {return p1.second.unprintability < p2.second.unprintability; });

        if (progressind)
            progressind(80);
        return results_vector;
    }


    static size_t best_index(const Ranked &results_vector, Vec3f &best_orientation)
    {

        Vec3f n1 = {0, 0, 1};
        best_orientation = results_vector[0].first;
        size_t best_index = 0;

        for (int i = 1; i< results_vector.size()-1; i++) {
            if (abs(results_vector[i].second.unprintability - results_vector[0].second.unprintability) < EPSILON && abs(results_vector[0].first.dot(n1)-1) > EPSILON) {
                if (abs(results_vector[i].first.dot(n1)-1) < EPSILON*EPSILON) {
                    best_orientation = n1;
                    best_index = i;
                    break;
                }
            }
            else {
                break;
            }

        }
        return best_index;
    }

    Vec3d process()
    {
        collect_orientations();
        Ranked results_vector = rank(true);
        Vec3f best_orientation;
        const size_t best = best_index(results_vector, best_orientation);


        areas_cooling = results_vector[best].second.areas_cooling;

        BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(6) << "best:" << best_orientation.transpose() << ", costs:" << results_vector[0].second.field_values();
        std::cout << std::fixed << std::setprecision(6) << "best:" << best_orientation.transpose() << ", costs:" << results_vector[0].second.field_values() << std::endl;

        return best_orientation.cast<double>();
    }


    Ranked candidates(size_t &best)
    {
        collect_orientations();
        Ranked ranked = rank(false);
        Vec3f  best_orientation;
        best = best_index(ranked, best_orientation);
        return ranked;
    }

    void preprocess()
    {
        int count_apperance = 0;
        {
            int face_count = mesh->facets_count();
            auto its = mesh->its;
            face_normals = its_face_normals(its);
            areas = Eigen::VectorXf::Zero(face_count);
            is_apperance = Eigen::VectorXf::Zero(face_count);
            normals = Eigen::MatrixXf::Zero(face_count, 3);
            normals_quantize = Eigen::MatrixXf::Zero(face_count, 3);
            for (size_t i = 0; i < face_count; i++)
            {
                float area = its.facet_area(i);
                normals.row(i) = face_normals[i];
                normals_quantize.row(i) = quantize_vec3f(face_normals[i]);
                areas(i) = area;
                is_apperance(i) = (its.get_property(i).type == EnumFaceTypes::eExteriorAppearance);
                count_apperance += (is_apperance(i)==1);
            }
        }

        if (orient_mesh)
            BOOST_LOG_TRIVIAL(debug) <<orient_mesh->name<< ", count_apperance=" << count_apperance;


        {
            mesh_convex_hull = mesh->convex_hull_3d();


            int face_count = mesh_convex_hull.facets_count();
            auto its = mesh_convex_hull.its;
            face_count_hull = mesh_convex_hull.facets_count();
            face_normals_hull = its_face_normals(its);
            areas_hull = Eigen::VectorXf::Zero(face_count);
            normals_hull = Eigen::MatrixXf::Zero(face_count_hull, 3);
            normals_hull_quantize = Eigen::MatrixXf::Zero(face_count_hull, 3);
            for (size_t i = 0; i < face_count; i++)
            {
                float area = its.facet_area(i);

                normals_hull.row(i) = face_normals_hull[i];
                normals_hull_quantize.row(i) = quantize_vec3f(face_normals_hull[i]);
                areas_hull(i) = area;
            }
        }
    }

    void area_cumulation(const Eigen::MatrixXf& normals_, const Eigen::VectorXf& areas_, int num_directions = 10)
    {
        std::unordered_map<stl_normal, float, VecHash> alignments;

        for (size_t i = 0; i < areas_.size(); i++)
            alignments.insert(std::pair(normals_.row(i), 0));

        for (size_t i = 0; i < areas_.size(); i++)
        {
            alignments[normals_.row(i)] += areas_(i);
        }

        typedef std::pair<stl_normal, float> PAIR;
        std::vector<PAIR> align_counts(alignments.begin(), alignments.end());
        sort(align_counts.begin(), align_counts.end(), [](const PAIR& p1, const PAIR& p2) {return p1.second > p2.second; });

        num_directions = std::min((size_t)num_directions, align_counts.size());
        for (size_t i = 0; i < num_directions; i++)
        {
            orientations.push_back(align_counts[i].first);

            BOOST_LOG_TRIVIAL(debug) << align_counts[i].first.transpose() << ", area: " << align_counts[i].second;
        }
    }

    void area_cumulation_accurate( std::vector<Vec3f>& normals_, const Eigen::MatrixXf& quantize_normals_, const Eigen::VectorXf& areas_, int num_directions = 10)
    {
        std::unordered_map<stl_normal, std::pair<std::vector<float>, Vec3f>, VecHash> alignments_;
        Vec3f n1 = { 0, 0, 0 };
        std::vector<float> current_areas = {0, 0};

        for (size_t i = 0; i < areas_.size(); i++) {
            alignments_.insert(std::pair(quantize_normals_.row(i), std::pair(current_areas, n1)));
        }

        for (size_t i = 0; i < areas_.size(); i++)
        {
            alignments_[quantize_normals_.row(i)].first[1] += areas_(i);
            if (areas_(i) > alignments_[quantize_normals_.row(i)].first[0]){
                alignments_[quantize_normals_.row(i)].second = normals_[i];
                alignments_[quantize_normals_.row(i)].first[0] = areas_(i);
            }
        }

        typedef std::pair<stl_normal, std::pair<std::vector<float>, Vec3f>> PAIR;
        std::vector<PAIR> align_counts(alignments_.begin(), alignments_.end());
        sort(align_counts.begin(), align_counts.end(), [](const PAIR& p1, const PAIR& p2) {return p1.second.first[1] > p2.second.first[1]; });

        num_directions = std::min((size_t)num_directions, align_counts.size());
        for (size_t i = 0; i < num_directions; i++)
        {
            orientations.push_back(align_counts[i].second.second);
            BOOST_LOG_TRIVIAL(debug) << align_counts[i].second.second.transpose() << ", area: " << align_counts[i].second.first[1];
        }
    }
    void add_supplements()
    {
        std::vector<Vec3f> vecs = { {0, 0, -1} ,{0.70710678, 0, -0.70710678},{0, 0.70710678, -0.70710678},
            {-0.70710678, 0, -0.70710678},{0, -0.70710678, -0.70710678},
            {1, 0, 0},{0.70710678, 0.70710678, 0},{0, 1, 0},{-0.70710678, 0.70710678, 0},
            {-1, 0, 0},{-0.70710678, -0.70710678, 0},{0, -1, 0},{0.70710678, -0.70710678, 0},
            {0.70710678, 0, 0.70710678},{0, 0.70710678, 0.70710678},
            {-0.70710678, 0, 0.70710678},{0, -0.70710678, 0.70710678},{0, 0, 1} };
        orientations.insert(orientations.end(), vecs.begin(), vecs.end());
    }





    void remove_duplicates(double tol=0.0000001)
    {
        for (auto it = orientations.begin()+1; it < orientations.end(); )
        {
            bool duplicate = false;
            for (auto it_ok = orientations.begin(); it_ok < it; it_ok++)
            {
                if (it_ok->isApprox(*it, tol)) {
                    duplicate = true;
                    break;
                }
            }
            const Vec3f all_zero = { 0,0,0 };
            if (duplicate || it->isApprox(all_zero,tol))
                it = orientations.erase(it);
            else
                it++;
        }
    }

    void project_vertices(Vec3f orientation)
    {
        int face_count = mesh->facets_count();
        auto its = mesh->its;
        z_projected.resize(face_count, 3);
        z_max.resize(face_count, 1);
        z_median.resize(face_count, 1);
        z_mean.resize(face_count, 1);
        for (size_t i = 0; i < face_count; i++)
        {
            float z0 = its.get_vertex(i,0).dot(orientation);
            float z1 = its.get_vertex(i,1).dot(orientation);
            float z2 = its.get_vertex(i,2).dot(orientation);
            z_projected(i, 0) = z0;
            z_projected(i, 1) = z1;
            z_projected(i, 2) = z2;
            z_max(i) = MAX3(z0,z1,z2);
            z_median(i) = MEDIAN3(z0,z1,z2);
            z_mean(i) = (z0 + z1 + z2) / 3;
        }

        z_max_hull.resize(mesh_convex_hull.facets_count(), 1);
        its = mesh_convex_hull.its;
        for (size_t i = 0; i < z_max_hull.rows(); i++)
        {
            float z0 = its.get_vertex(i,0).dot(orientation);
            float z1 = its.get_vertex(i,1).dot(orientation);
            float z2 = its.get_vertex(i,2).dot(orientation);
            z_max_hull(i) = MAX3(z0, z1, z2);
        }
    }

    static Eigen::VectorXi argsort(const Eigen::VectorXf& vec, std::string order="ascend")
    {
        Eigen::VectorXi ind = Eigen::VectorXi::LinSpaced(vec.size(), 0, vec.size() - 1);
        std::function<bool(int, int)> rule;
        if (order == "ascend") {
            rule = [vec](int i, int j)->bool {
                return vec(i) < vec(j);
                };
            }
        else {
            rule = [vec](int i, int j)->bool {
                return vec(i) > vec(j);
                };
            }
        std::sort(ind.data(), ind.data() + ind.size(), rule);
        return ind;





    }


    CostItems get_features(Vec3f orientation, bool min_volume = true)
    {
        CostItems costs;
        costs.area_total = mesh->bounding_box().area();
        costs.radius = mesh->bounding_box().radius();

        costs.volume = mesh->stats().volume > 0 ? mesh->stats().volume : its_volume(mesh->its);

        float total_min_z = z_projected.minCoeff();

        auto bottom_condition = (z_max.array() < total_min_z + this->params.FIRST_LAY_H - EPSILON).eval();
        auto bottom_condition_hull = (z_max_hull.array() < total_min_z + this->params.FIRST_LAY_H - EPSILON).eval();
        auto bottom_condition_2nd  = (z_max.array() < total_min_z + this->params.FIRST_LAY_H / 2.f - EPSILON).eval();




        costs.bottom = bottom_condition.select(areas, 0).sum()*0.5 + bottom_condition_2nd.select(areas, 0).sum();


        Eigen::VectorXf normal_projection(normals.rows(), 1);
        for (size_t i = 0; i < normals.rows(); i++)
        {
            normal_projection(i) = normals.row(i).dot(orientation);
        }
        auto areas_appearance = areas.cwiseProduct((is_apperance * params.APPERANCE_FACE_SUPP + Eigen::VectorXf::Ones(is_apperance.rows(), is_apperance.cols()))).eval();
        auto overhang_areas = ((normal_projection.array() < params.ASCENT) * (!bottom_condition_2nd)).select(areas_appearance, 0).eval();
        Eigen::MatrixXf inner = normal_projection.array() - params.ASCENT;
        inner = inner.cwiseMin(0).cwiseAbs();
        if (min_volume)
        {
            Eigen::MatrixXf heights = z_mean.array() - total_min_z;
            costs.overhang = (heights.array()* overhang_areas.array()*inner.array()).sum();
        }
        else {
            costs.overhang = overhang_areas.array().cwiseAbs().sum();
        }

        {

#if 1

            costs.contour = 4 * sqrt(costs.bottom);
#else
            float contour = 0;
            int face_count = mesh->facets_count();
            auto its = mesh->its;
            int contour_amout = 0;
            for (size_t i = 0; i < face_count; i++)
            {
                if (bottom_condition(i)) {
                    Eigen::VectorXi index = argsort(z_projected.row(i));
                    stl_vertex line = its.get_vertex(i, index(0)) - its.get_vertex(i, index(1));
                    contour += line.norm();
                    contour_amout++;
                }
            }
            costs.contour += contour + params.CONTOUR_AMOUNT * contour_amout;
#endif
        }


        costs.bottom_hull = (bottom_condition_hull).select(areas_hull, 0).sum();


        auto normal_projection_abs = normal_projection.cwiseAbs().eval();
        Eigen::MatrixXf laf_areas = ((normal_projection_abs.array() < params.LAF_MAX) * (normal_projection_abs.array() > params.LAF_MIN) * (z_max.array() > total_min_z + params.FIRST_LAY_H)).select(areas, 0);
        costs.area_laf = laf_areas.sum();

        if (has_cooling_fan)
        {

            float angle_thres_high = -0.6427f;
            float angle_thres_low = -0.97f;

            Eigen::VectorXf ones_f = Eigen::VectorXf::Ones(mesh->facets_count());
            auto overhang_area_condition = (normal_projection.array() < angle_thres_high && normal_projection.array() > angle_thres_low).eval();
            Eigen::VectorXf areas_ = (overhang_area_condition * !bottom_condition_2nd).select(areas, 0);
            Eigen::VectorXf weighted_areas = areas_.cwiseProduct(ones_f - normal_projection);
            costs.areas_cooling = weighted_areas;
        }





        return costs;
    }

    float target_function(CostItems& costs, bool min_volume)
    {
        float cost=0;
        float bottom = costs.bottom;
        float bottom_hull = costs.bottom_hull;
        if (min_volume)
        {
            float overhang = costs.overhang / 25;
            cost = params.TAR_A * (overhang + params.TAR_B) + params.RELATIVE_F * ( overhang*params.TAR_C + params.TAR_D + params.TAR_LAF * costs.area_laf * params.use_low_angle_face) / (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * bottom + params.BOTTOM_HULL_F * bottom_hull + params.TAR_E * overhang + params.TAR_PROJ_AREA * costs.area_projected);
        }
        else {
            float overhang = costs.overhang;
            cost = params.RELATIVE_F * (costs.overhang * params.TAR_C + params.TAR_D + params.TAR_LAF * costs.area_laf * params.use_low_angle_face) / (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * bottom + params.BOTTOM_HULL_F * bottom_hull + params.TAR_PROJ_AREA * costs.area_projected);
        }
        cost += (costs.bottom < params.BOTTOM_MIN) * 100;

        costs.unprintability = cost;

        return cost;
    }

    Vec3d find_cooling_direction2(Vec3d euler_angles, const Eigen::VectorXf& areas_in, TriangleMesh& mesh)
    {
        Vec3f machine_cool_dir = this->orient_mesh->cooling_direction.cast<float>();
        const size_t num_faces = areas.rows();
        Vec3f best_direction = { 0, 0, 0 };


        TriangleMesh mesh_copy = TriangleMesh(mesh.its);
        mesh_copy.rotate_x(euler_angles(0, 0));
        mesh_copy.rotate_y(euler_angles(1, 0));
        mesh_copy.rotate_z(euler_angles(2, 0));
        auto bounding_box = mesh_copy.bounding_box();
        Eigen::VectorXf translate_distance = bounding_box.min.array().cast<float>();
        Vec3d mesh_center = mesh_copy.center();
        mesh_copy.translate(-mesh_center(0), -mesh_center(1), -translate_distance(2));


        const size_t sample_nums = 180;
        std::vector<Vec3f> cool_dirs;
        for (size_t i = 0; i < sample_nums; i++)
        {
            float angle_deg = i * (360.0 / sample_nums);
            float angle_rad = angle_deg * (PI / 180.0);
            cool_dirs.push_back(Vec3f{ std::cos(angle_rad), std::sin(angle_rad), 0});
        }


        std::vector<Vec3f> face_normals_copy = its_face_normals(mesh_copy.its);
        float overhang_projected_max = 0.f;
        float overhang_projected_origin = 0.f;
        for (auto cool_dir : cool_dirs)
        {
            float overhang_projected_tmp = 0.f;
            for (size_t i = 0; i < num_faces; i++)
            {
                float cool_dir_projection = face_normals_copy[i].dot(cool_dir);
                if (areas_in[i] > 0 && cool_dir_projection > 0)
                {
                    overhang_projected_tmp += areas_in[i] * cool_dir_projection;
                }
            }
            if (overhang_projected_tmp > overhang_projected_max)
            {
                overhang_projected_max = overhang_projected_tmp;
                best_direction = cool_dir;
            }
            if (cool_dir.dot(machine_cool_dir) > 0.999)
            {
                overhang_projected_origin = overhang_projected_tmp;
            }
        }


        if (std::abs(overhang_projected_origin - overhang_projected_max) < 1.0f)
        {
            best_direction = machine_cool_dir;
        }
        BOOST_LOG_TRIVIAL(info) << "best cooling dir = " << best_direction.transpose() << "\n";
        return best_direction.cast<double>();
    }
};




static void orient_nonplanar(OrientMesh &mesh, const std::function<bool()> &stopfn)
{
    if (stopfn && stopfn())
        return;
    const Nonplanar::OrientResult res = Nonplanar::auto_orient(mesh.mesh.its, mesh.nonplanar_params);
    if (stopfn && stopfn())
        return;
    mesh.orientation     = res.chosen.up;
    mesh.rotation_matrix = Nonplanar::rotation_to_z(res.chosen.up);
    const Eigen::AngleAxisd turn(mesh.rotation_matrix);
    mesh.axis         = turn.axis();
    mesh.angle        = turn.angle();
    mesh.euler_angles = Geometry::extract_euler_angles(mesh.rotation_matrix);
    BOOST_LOG_TRIVIAL(info) << mesh.name << ": non-planar orientation " << mesh.orientation.transpose() << (res.rotated ? " (turned)" : " (kept)")
                            << ", cost " << res.chosen.cost << " against " << res.original.cost;


    const Nonplanar::OrientScore *other = &res.original;
    if (!res.rotated)
        for (const Nonplanar::OrientScore &score : res.ranked)
            if ((score.up - res.original.up).norm() > 1e-6) {
                other = &score;
                break;
            }
    char buf[256];
    if (res.rotated)
        std::snprintf(buf, sizeof(buf), "turned to up (%.2f, %.2f, %.2f): cost %.0f s against %.0f s as loaded, curved top %.0f mm2 against %.0f mm2",
                      res.chosen.up.x(), res.chosen.up.y(), res.chosen.up.z(), res.chosen.cost, other->cost, res.chosen.curved_top, other->curved_top);
    else if (other != &res.original)
        std::snprintf(buf, sizeof(buf), "kept as loaded: cost %.0f s against %.0f s for the best turn, curved top %.0f mm2", res.chosen.cost,
                      other->cost, res.chosen.curved_top);
    else
        std::snprintf(buf, sizeof(buf), "kept as loaded: no other orientation scored, cost %.0f s", res.chosen.cost);
    mesh.nonplanar_decision = buf;
}

void _orient(OrientMeshs& meshs_,
        const OrientParams           &params,
        std::function<void(unsigned, std::string)> progressfn,
        std::function<bool()>         stopfn)
{
    if (!params.parallel)
    {
        for (size_t i = 0; i != meshs_.size(); ++i) {
            auto& mesh_ = meshs_[i];
            progressfn(i, mesh_.name);

            if (mesh_.nonplanar) {
                orient_nonplanar(mesh_, stopfn);
                continue;
            }
            AutoOrienter orienter(&mesh_, params,  {}, stopfn);
            mesh_.orientation = orienter.process();
            Geometry::rotation_from_two_vectors(mesh_.orientation, { 0,0,1 }, mesh_.axis, mesh_.angle, &mesh_.rotation_matrix);
            BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(3) << "v,phi: " << mesh_.axis.transpose() << ", " << mesh_.angle;

        }
    }
    else {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, meshs_.size()), [&meshs_, &params, progressfn, stopfn](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                auto& mesh_ = meshs_[i];
                progressfn(i, mesh_.name);
                if (mesh_.nonplanar) {
                    orient_nonplanar(mesh_, stopfn);
                    continue;
                }
                AutoOrienter orienter(&mesh_, params, {}, stopfn);
                mesh_.orientation = orienter.process();
                Geometry::rotation_from_two_vectors(mesh_.orientation, { 0,0,1 }, mesh_.axis, mesh_.angle, &mesh_.rotation_matrix);
                mesh_.euler_angles = Geometry::extract_euler_angles(mesh_.rotation_matrix);

                if (mesh_.has_cooling_fan)
                {
                    mesh_.orientation_vertical = orienter.find_cooling_direction2(mesh_.euler_angles, orienter.areas_cooling, mesh_.mesh);
                    BOOST_LOG_TRIVIAL(info) << "cooling direction: " << mesh_.orientation_vertical.transpose() << "\n";
                    Geometry::rotation_from_two_vectors(mesh_.orientation_vertical, mesh_.cooling_direction, mesh_.axis_vertical, mesh_.angle_vertical, &mesh_.rotation_matrix_vertical);
                }
                BOOST_LOG_TRIVIAL(debug) << "rotation_from_two_vectors: " << mesh_.orientation << "; " << mesh_.axis << "; " << mesh_.angle << "; euler: " << mesh_.euler_angles.transpose();
            }});
    }
}

void orient(OrientMeshs &      arrangables,
             const OrientMeshs &excludes,
             const OrientParams &  params)
{

    auto &cfn = params.stopcondition;
    auto &pri = params.progressind;

    _orient(arrangables, params, pri, cfn);

}

void orient(ModelObject* obj)
{
    auto m = obj->mesh();
    AutoOrienter orienter(&m);
    Vec3d orientation = orienter.process();
    Vec3d axis;
    double angle;
    Geometry::rotation_from_two_vectors(orientation, { 0,0,1 }, axis, angle);

    obj->rotate(angle, axis);
    obj->ensure_on_bed();
}

void orient(ModelInstance* instance)
{
    auto m = instance->get_object()->mesh();
    AutoOrienter orienter(&m);
    Vec3d orientation = orienter.process();
    Vec3d axis;
    double angle;
    Matrix3d rotation_matrix;
    Geometry::rotation_from_two_vectors(orientation, { 0,0,1 }, axis, angle, &rotation_matrix);
    instance->rotate(rotation_matrix);
}

std::vector<CandidateScore> score_candidates(const TriangleMesh &mesh, double overhang_angle)
{
    OrientMesh om;
    om.mesh           = mesh;
    om.overhang_angle = overhang_angle;
    AutoOrienter      orienter(&om, OrientParams{}, {}, {});
    size_t            best;
    const auto        ranked = orienter.candidates(best);

    const OrientParams          params;
    const float                 ascent  = float(cos(PI - overhang_angle * PI / 180));
    const indexed_triangle_set &its     = mesh.its;
    const auto                  normals = its_face_normals(its);
    const AABBMesh              aabb(its);

    std::vector<CandidateScore> out(ranked.size());
    tbb::parallel_for(size_t(0), ranked.size(), [&](size_t c) {
        const Vec3f    up = ranked[c].first;
        CandidateScore &s = out[c];
        s.up             = up.cast<double>();
        s.original       = (up - Vec3f(0, 0, 1)).norm() < EPSILON;
        s.best           = c == best;
        s.unprintability = ranked[c].second.unprintability;
        s.support_area   = ranked[c].second.overhang;
        s.bottom_area    = ranked[c].second.bottom;

        Vec3d    axis;
        double   angle;
        Matrix3d rotation;
        Geometry::rotation_from_two_vectors(s.up, {0, 0, 1}, axis, angle, &rotation);
        BoundingBoxf3 box;
        for (const stl_vertex &v : its.vertices)
            box.merge(rotation * v.cast<double>());
        s.size = box.size();



        const double  bed  = box.min.z();
        const Vec3d   down = -s.up;
        for (size_t f = 0; f < its.indices.size(); ++f) {
            const double dot = normals[f].cast<double>().dot(s.up);
            if (dot >= ascent)
                continue;
            const Vec3d p[3] = {its.vertices[its.indices[f][0]].cast<double>(), its.vertices[its.indices[f][1]].cast<double>(),
                                its.vertices[its.indices[f][2]].cast<double>()};
            const double h[3] = {(rotation * p[0]).z() - bed, (rotation * p[1]).z() - bed, (rotation * p[2]).z() - bed};
            if (std::max({h[0], h[1], h[2]}) < params.FIRST_LAY_H / 2.f - EPSILON)
                continue;
            const double area = 0.5 * (p[1] - p[0]).cross(p[2] - p[0]).norm();
            const int    n    = std::clamp(int(std::ceil(std::sqrt(area))), 1, 8);
            int          floating = 0;
            double       height   = 0;
            for (int i = 0; i < n; ++i)
                for (int j = 0; i + j < n; ++j)
                    for (int k = 0; k < (i + j + 1 < n ? 2 : 1); ++k) {

                        const double a = (i + (k ? 2. : 1.) / 3.) / n, b = (j + (k ? 2. : 1.) / 3.) / n;
                        const Vec3d  q = p[0] + a * (p[1] - p[0]) + b * (p[2] - p[0]);
                        if (!aabb.query_ray_hit(q + 1e-3 * down, down).is_hit()) {
                            ++floating;
                            height += (rotation * q).z() - bed;
                        }
                    }
            const double share = double(floating) / double(n * n);
            s.float_area += area * share;
            if (floating > 0)
                s.float_volume += area * std::abs(dot) * share * height / floating;
        }
    });
    return out;
}

void orient_for_cooling(TriangleMesh& mesh, const FanDirection& fan_dir)
{
    Vec3f best_direction{ 0, 0, 0 };
    Vec3f machine_cool_dir{ 0, 0, 0 };

    if (fan_dir == FanDirection::fdUndefine)
    {

        return;
    }
    else if (fan_dir == FanDirection::fdRight)
    {
        machine_cool_dir = { 1, 0, 0 };
    }
    else
    {

        machine_cool_dir = { -1, 0, 0 };
    }


    int nfaces = mesh.facets_count();
    auto face_normals = its_face_normals(mesh.its);

    Eigen::VectorXf normal_projection(nfaces, 1);
    for (auto i = 0; i < nfaces; i++)
    {
        normal_projection(i) = face_normals[i].dot(Vec3f(0, 0, 1));
    }
    float angle_thres_high = -0.6427f;
    float angle_thres_low = -0.97f;

    Eigen::VectorXf weighted_areas = Eigen::VectorXf::Zero(nfaces);
    for (int i = 0; i < nfaces; i++)
    {
        if (normal_projection(i) < angle_thres_high && normal_projection(i) > angle_thres_low)
        {
            weighted_areas(i) = mesh.its.facet_area(i) * (1.0f - normal_projection(i));
        }
    }

    const size_t sample_nums = 180;
    std::vector<Vec3f> cool_dirs;
    for (size_t i = 0; i < sample_nums; i++)
    {
        float angle_deg = i * (360.0 / sample_nums);
        float angle_rad = angle_deg * (PI / 180.0);
        cool_dirs.push_back(Vec3f{ std::cos(angle_rad), std::sin(angle_rad), 0 });
    }


    float overhang_projected_max = 0.f;
    float overhang_projected_origin = 0.f;
    for (auto cool_dir : cool_dirs)
    {
        float overhang_projected_tmp = 0.f;
        for (size_t i = 0; i < nfaces; i++)
        {
            float cool_dir_projection = face_normals[i].dot(cool_dir);
            if (weighted_areas[i] > 0 && cool_dir_projection > 0)
            {
                overhang_projected_tmp += weighted_areas[i] * cool_dir_projection;
            }
        }
        if (overhang_projected_tmp > overhang_projected_max)
        {
            overhang_projected_max = overhang_projected_tmp;
            best_direction = cool_dir;
        }
        if (cool_dir.dot(machine_cool_dir) > 0.999)
        {
            overhang_projected_origin = overhang_projected_tmp;
        }
    }


    if (std::abs(overhang_projected_origin - overhang_projected_max) < 1.0f)
    {
        return;
    }


    Vec3d axis;
    double angle;
    Matrix3d rotation_matrix;
    Geometry::rotation_from_two_vectors(best_direction.cast<double>(), machine_cool_dir.cast<double>(), axis, angle, &rotation_matrix);
    mesh.rotate(angle, axis);
}

}
}
