#ifndef TREESUPPORT_H
#define TREESUPPORT_H

#include <cstddef>
#include <cmath>
#include <forward_list>
#include <list>
#include <memory>
#include <unordered_map>
#include <functional>
#include <map>
#include <math.h>
#include <unordered_set>
#include <vector>
#include <utility>
#include "ExPolygon.hpp"
#include "Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "Slicing.hpp"
#include "MinimumSpanningTree.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Support/TreeSupportCommon.hpp"
#include "libslic3r/Support/SupportParameters.hpp"
#include "tbb/concurrent_unordered_map.h"
#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Fill/Lightning/Generator.hpp"
#include "TreeModelVolumes.hpp"
#include "TreeSupport3D.hpp"

#ifndef SQ
#define SQ(x) ((x)*(x))
#endif

namespace Slic3r
{
class PrintObject;
class TreeSupport;
class SupportLayer;

struct LayerHeightData
{
    coordf_t print_z       = 0;
    coordf_t height        = 0;
    size_t   obj_layer_nr  = 0;
    LayerHeightData()      = default;
    LayerHeightData(coordf_t z, coordf_t h, size_t obj_layer) : print_z(z), height(h), obj_layer_nr(obj_layer) {}
    coordf_t bottom_z() {
        return print_z - height;
    }
};

enum TreeNodeType {
    eCircle,
    eSquare,
    ePolygon
};





struct SupportNode
{
    static constexpr SupportNode* NO_PARENT = nullptr;

    SupportNode()
        : distance_to_top(0)
        , position(Point(0, 0))
        , obj_layer_nr(0)
        , support_roof_layers_below(0)
        , to_buildplate(true)
        , parent(nullptr)
        , print_z(0.0)
        , height(0.0)
    {}


    SupportNode(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, SupportNode* parent,
        coordf_t     print_z_, coordf_t height_, coordf_t dist_mm_to_top_ = 0, coordf_t radius_ = 0)
        : distance_to_top(distance_to_top)
        , position(position)
        , obj_layer_nr(obj_layer_nr)
        , support_roof_layers_below(support_roof_layers_below)
        , to_buildplate(to_buildplate)
        , parent(parent)
        , print_z(print_z_)
        , height(height_)
        , dist_mm_to_top(dist_mm_to_top_)
        , radius(radius_)
    {
        if (parent) {
            parents.push_back(parent);
            type = parent->type;
            overhang = parent->overhang;
            if (dist_mm_to_top == 0)
                dist_mm_to_top = parent->dist_mm_to_top + parent->height;
            if (radius == 0 && parent->radius>0)
                radius = parent->radius + (dist_mm_to_top - parent->dist_mm_to_top) * diameter_angle_scale_factor;
            parent->child = this;
            for (auto& neighbor : parent->merged_neighbours) {
                neighbor->child = this;
                parents.push_back(neighbor);
            }
            is_sharp_tail = parent->is_sharp_tail;
            skin_direction = parent->skin_direction;
        }
    }

#ifdef DEBUG
    ~SupportNode()
    {
        parent = nullptr;
        merged_neighbours.clear();
    }
#endif





    int distance_to_top;
    coordf_t dist_mm_to_top = 0;


    static double diameter_angle_scale_factor;




    Point          position;
    Point          movement;
    mutable double radius          = 0.0;
    mutable double max_move_dist   = 0.0;
    TreeNodeType   type            = eCircle;
    bool           is_corner       = false;
    bool           is_processed    = false;
    bool           need_extra_wall = false;
    bool           is_sharp_tail   = false;
    bool           valid = true;
    ExPolygon      overhang;







    Point skin_direction;









    int support_roof_layers_below;
    int obj_layer_nr;








    bool to_buildplate;








    SupportNode* parent;
    std::vector<SupportNode*> parents;
    SupportNode* child = nullptr;








    std::list<SupportNode*> merged_neighbours;

    coordf_t print_z;
    coordf_t height;

    bool operator==(const SupportNode& other) const
    {
        return position == other.position;
    }
};






class TreeSupportData
{
public:
    TreeSupportData() = default;







    TreeSupportData(const PrintObject& object, coordf_t xy_distance, coordf_t radius_sample_resolution);
    ~TreeSupportData() {
        clear_nodes();
    }


    TreeSupportData(TreeSupportData&&) = delete;
    TreeSupportData& operator=(TreeSupportData&&) = delete;

    TreeSupportData(const TreeSupportData&) = delete;
    TreeSupportData& operator=(const TreeSupportData&) = delete;











    const ExPolygons& get_collision(coordf_t radius, size_t layer_idx) const;















    const ExPolygons& get_avoidance(coordf_t radius, size_t layer_idx, int recursions=0) const;

    Polygons get_contours(size_t layer_nr) const;
    Polygons get_contours_with_holes(size_t layer_nr) const;

    SupportNode* create_node(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, SupportNode* parent,
        coordf_t     print_z_, coordf_t height_, coordf_t dist_mm_to_top_ = 0, coordf_t radius_ = 0);
    void clear_nodes();
    std::vector<LayerHeightData> layer_heights;

    std::vector<std::unique_ptr<SupportNode>> contact_nodes;


private:



    struct RadiusLayerPair {
        coordf_t radius;
        size_t layer_nr;
        int recursions;

    };
    struct RadiusLayerPairEquality {
        constexpr bool operator()(const RadiusLayerPair& _Left, const RadiusLayerPair& _Right) const {
            return _Left.radius == _Right.radius && _Left.layer_nr == _Right.layer_nr;
        }
    };
    struct RadiusLayerPairHash {
        size_t operator()(const RadiusLayerPair& elem) const {
            return std::hash<coord_t>()(elem.radius) ^ std::hash<coord_t>()(elem.layer_nr * 7919);
        }
    };






    coordf_t ceil_radius(coordf_t radius) const;







    const ExPolygons& calculate_collision(const RadiusLayerPair& key) const;







    const ExPolygons& calculate_avoidance(const RadiusLayerPair& key) const;

    tbb::spin_mutex  m_mutex;

public:
    bool is_slim = false;



    coordf_t m_xy_distance;

    double branch_scale_factor = 1.0;








    coordf_t m_radius_sample_resolution;




    std::vector<ExPolygons> m_layer_outlines;


    std::vector<ExPolygons> m_layer_outlines_below;

    std::vector<double> m_max_move_distances;













    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_collision_cache;
    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_avoidance_cache;

    friend TreeSupport;
};

struct LineHash {
    size_t operator()(const Line& line) const {
        return (std::hash<coord_t>()(line.a(0)) ^ std::hash<coord_t>()(line.b(1))) * 102 +
            (std::hash<coord_t>()(line.a(1)) ^ std::hash<coord_t>()(line.b(0))) * 10222;
    }
};




class TreeSupport
{
public:





    TreeSupport(PrintObject& object, const SlicingParameters &slicing_params);

    void move_bounds_to_contact_nodes(std::vector<TreeSupport3D::SupportElements> &move_bounds,
                                      PrintObject                                 &print_object,
                                      const TreeSupport3D::TreeSupportSettings    &config);








    void generate();

    void detect_overhangs(bool check_support_necessity = false);

    SupportNode* create_node(const Point  position,
        const int    distance_to_top,
        const int    obj_layer_nr,
        const int    support_roof_layers_below,
        const bool   to_buildplate,
        SupportNode* parent,
        coordf_t     print_z_,
        coordf_t     height_,
        coordf_t     dist_mm_to_top_ = 0,
        coordf_t     radius_ = 0)
    {
        return m_ts_data->create_node(position, distance_to_top, obj_layer_nr, support_roof_layers_below, to_buildplate, parent, print_z_, height_, dist_mm_to_top_, radius_);
    }

    int  avg_node_per_layer = 0;
    float nodes_angle = 0;
    bool  has_sharp_tails = false;
    bool  has_cantilever = false;
    double max_cantilever_dist = 0;
    SupportType support_type;

    std::unique_ptr<FillLightning::Generator> generator;
    std::unordered_map<double, size_t> printZ_to_lightninglayer;

    std::function<void()> throw_on_cancel;
    const PrintConfig* m_print_config;




    ExPolygon m_machine_border;

    enum OverhangType { Detected = 0, Enforced, SharpTail };
    std::map<const ExPolygon*, OverhangType> overhang_types;
    std::vector<std::pair<Vec3f, Vec3f>>      m_vertical_enforcer_points;

private:






    std::vector<std::vector<SupportNode*>> contact_nodes;
    std::shared_ptr<TreeSupportData> m_ts_data;
    std::unique_ptr<TreeSupport3D::TreeModelVolumes> m_model_volumes;
    PrintObject    *m_object;
    const PrintObjectConfig* m_object_config;
    SlicingParameters        m_slicing_params;

    SupportParameters   m_support_params;
    size_t          m_raft_layers = 0;

    bool            m_contact_gap_recounted = false;
    size_t          m_highest_overhang_layer = 0;
    std::vector<std::vector<MinimumSpanningTree>> m_spanning_trees;
    std::vector< std::unordered_map<Line, bool, LineHash>> m_mst_line_x_layer_contour_caches;
    coordf_t base_radius                        = 0.0;
    const coordf_t MAX_BRANCH_RADIUS = 10.0;
    const coordf_t MIN_BRANCH_RADIUS = 0.4;
    const coordf_t MAX_BRANCH_RADIUS_FIRST_LAYER = 12.0;
    const coordf_t MIN_BRANCH_RADIUS_FIRST_LAYER = 2.0;
    double diameter_angle_scale_factor = tan(5.0*M_PI/180.0);

    const double minimum_roof_area{SQ(scaled<double>(1.))};
    float        top_z_distance = 0.0;

    bool  is_strong = false;
    bool  is_slim                            = false;
    bool  with_infill                        = false;



    double belt_floor_print_z(const Point &pos_slicing) const;




    bool belt_node_landed(const Point &pos_slicing, double radius, double print_z) const;

    bool belt_polygon_landed(const ExPolygon &poly, double print_z) const;













    void draw_circles();













    void drop_nodes();

    void smooth_nodes();






    std::vector<LayerHeightData> plan_layer_heights();













    void generate_contact_points();






    void insert_dropped_node(std::vector<SupportNode*>& nodes_layer, SupportNode* node);
    void create_tree_support_layers();
    void generate_toolpaths();

    coordf_t calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor);

    coordf_t calc_branch_radius(coordf_t base_radius, coordf_t mm_to_top, double diameter_angle_scale_factor, bool use_min_distance=true);
    coordf_t   calc_radius(coordf_t mm_to_top);
    coordf_t get_radius(const SupportNode* node);
    ExPolygons get_avoidance(coordf_t radius, size_t obj_layer_nr);

    ExPolygons get_collision(coordf_t radius, size_t layer_nr);

    Polygons get_collision_polys(coordf_t radius, size_t layer_nr);


    Polygons get_trim_support_regions(
        const PrintObject& object,
        SupportLayer* support_layer_ptr,
        const coordf_t       gap_extra_above,
        const coordf_t       gap_extra_below,
        const coordf_t       gap_xy);
};

}

#endif
