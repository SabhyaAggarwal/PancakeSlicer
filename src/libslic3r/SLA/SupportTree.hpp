#ifndef SLA_SUPPORTTREE_HPP
#define SLA_SUPPORTTREE_HPP

#include <math.h>
#include <vector>
#include <memory>
#include <Eigen/Geometry>

#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>

#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/IndexedMesh.hpp>
#include <libslic3r/SLA/SupportPoint.hpp>
#include <libslic3r/SLA/JobController.hpp>

namespace Slic3r {

class TriangleMesh;
class Model;
class ModelInstance;
class ModelObject;

namespace sla {

enum class PillarConnectionMode
{
    zigzag,
    cross,
    dynamic
};

struct SupportTreeConfig
{
    bool   enabled = true;


    double head_front_radius_mm = 0.2;


    double head_penetration_mm = 0.5;


    double head_back_radius_mm = 0.5;

    double head_fallback_radius_mm = 0.25;


    double head_width_mm = 1.0;


    PillarConnectionMode pillar_connection_mode = PillarConnectionMode::dynamic;


    bool ground_facing_only = false;





    double pillar_widening_factor = 0.5;


    double base_radius_mm = 2.0;


    double base_height_mm = 1.0;


    double bridge_slope = M_PI/4;


    double max_bridge_length_mm = 10.0;


    double max_pillar_link_distance_mm = 10.0;



    double object_elevation_mm = 10;



    double pillar_base_safety_distance_mm = 0.5;

    unsigned max_bridges_on_pillar = 3;

    double head_fullwidth() const {
        return 2 * head_front_radius_mm + head_width_mm +
               2 * head_back_radius_mm - head_penetration_mm;
    }






    static const double constexpr normal_cutoff_angle = 150.0 * M_PI / 180.0;


    static const double constexpr safety_distance_mm = 0.5;

    static const double constexpr max_solo_pillar_height_mm = 15.0;
    static const double constexpr max_dual_pillar_height_mm = 35.0;
    static const double constexpr optimizer_rel_score_diff = 1e-6;
    static const unsigned constexpr optimizer_max_iterations = 1000;
    static const unsigned constexpr pillar_cascade_neighbors = 3;

};







enum class MeshType { Support, Pad };

struct SupportableMesh
{
    IndexedMesh  emesh;
    SupportPoints pts;
    SupportTreeConfig cfg;


    explicit SupportableMesh(const indexed_triangle_set & trmsh,
                             const SupportPoints &sp,
                             const SupportTreeConfig &c)
        : emesh{trmsh}, pts{sp}, cfg{c}
    {}

    explicit SupportableMesh(const IndexedMesh   &em,
                             const SupportPoints &sp,
                             const SupportTreeConfig &c)
        : emesh{em}, pts{sp}, cfg{c}
    {}
};


class SupportTree
{
    JobController m_ctl;
public:
    using UPtr = std::unique_ptr<SupportTree>;

    static UPtr create(const SupportableMesh &input,
                       const JobController &ctl = {});

    virtual ~SupportTree() = default;

    virtual const indexed_triangle_set &retrieve_mesh(MeshType meshtype) const = 0;






    virtual const indexed_triangle_set &add_pad(const ExPolygons &modelbase,
                                                const PadConfig & pcfg) = 0;

    virtual void remove_pad() = 0;

    std::vector<ExPolygons> slice(const std::vector<float> &,
                                  float closing_radius) const;

    void retrieve_full_mesh(indexed_triangle_set &outmesh) const;

    const JobController &ctl() const { return m_ctl; }
};

}

}

#endif
