#ifndef slic3r_FillBase_hpp_
#define slic3r_FillBase_hpp_

#include <assert.h>
#include <map>
#include <cstddef>
#include <math.h>
#include <memory.h>
#include <float.h>
#include <stdint.h>
#include <stdexcept>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../Exception.hpp"
#include "../Utils.hpp"
#include "../ExPolygon.hpp"
#include "../PrintConfig.hpp"
#include "../Flow.hpp"
#include "../ExtrusionEntity.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { class ExtrusionEntityCollection; }

namespace Slic3r {

class Surface;
class TpmsRadialField;
enum InfillPattern : int;

namespace FillAdaptive {
    struct Octree;
};

class InfillFailedException : public Slic3r::RuntimeError {
public:
    InfillFailedException() : Slic3r::RuntimeError("Infill failed") {}
};

struct LockRegionParam
{
    LockRegionParam() {}
    std::map<float, ExPolygons> skin_density_params;
    std::map<float, ExPolygons> skeleton_density_params;
    std::map<Flow, ExPolygons>  skin_flow_params;
    std::map<Flow, ExPolygons>  skeleton_flow_params;
};

struct FillParams
{
    bool        full_infill() const { return density > 0.9999f; }
    bool        dont_connect() const { return anchor_length_max < 0.05f; }

    float       density 		{ 0.f };
    int   multiline{1};

    float       anchor_length       { 1000.f };
    float       anchor_length_max   { 1000.f };

    double      resolution          { 0.0125 };

    bool        dont_adjust 	{ true };

    bool 		monotonic		{ false };

    bool        complete 		{ false };

    bool        use_arachne{ false };
    coordf_t    layer_height    { 0.f };

    bool        gyroid_optimized { false };

    TpmsAdaptiveMode     tpms_adaptive { TpmsAdaptiveMode::Disabled };
    float                tpms_interior_density { 0.f };
    TpmsAdaptiveGradient tpms_adaptive_gradient { TpmsAdaptiveGradient::Linear };

    double      smooth_factor { 0. };

    coordf_t    lateral_lattice_angle_1    { 0.f };
    coordf_t    lateral_lattice_angle_2    { 0.f };
    InfillPattern pattern{ ipRectilinear };

    float       infill_overhang_angle    { 60 };

    Flow            flow;
    ExtrusionRole   extrusion_role{ ExtrusionRole(0) };
    bool            using_internal_flow{ false };
    float           no_extrusion_overlap{ 0.0 };
    const           PrintRegionConfig* config{ nullptr };
    bool            dont_sort{ false };
    bool            can_reverse{true};

    SurfaceFillOrder fill_order { SurfaceFillOrder::Default };

    float           horiz_move{0.0};
    bool            symmetric_infill_y_axis{false};
    bool            infill_complete_top{false};
    coord_t         symmetric_y_axis{0};
    bool            locked_zag{false};
    float           infill_lock_depth{0.0};
    float           skin_infill_depth{0.0};
    CenterOfSurfacePattern center_of_surface_pattern{CenterOfSurfacePattern::Each_Surface};
};
static_assert(IsTriviallyCopyable<FillParams>::value, "FillParams class is not POD (and it should be - see constructor).");

class Fill
{
public:
    size_t      layer_id;
    coordf_t    z;
    coordf_t    spacing;
    coordf_t    overlap;
    float       angle;

    bool        fixed_angle{false};
    coord_t     link_max_length;
    coord_t     loop_clipping;
    BoundingBox bounding_box;

    FillAdaptive::Octree* adapt_fill_octree = nullptr;

    const TpmsRadialField* tpms_radial_field = nullptr;

    const PrintConfig       *print_config        = nullptr;
    const PrintObjectConfig *print_object_config = nullptr;

    ExPolygons  no_overlap_expolygons;
    bool dont_alternate_fill_direction = false;

    static float infill_anchor;
    static float infill_anchor_max;

public:
    virtual ~Fill() {}
    virtual Fill* clone() const = 0;

    static Fill* new_from_type(const InfillPattern type);
    static Fill* new_from_type(const std::string &type);
    static bool  use_bridge_flow(const InfillPattern type);

    void         set_bounding_box(const Slic3r::BoundingBox &bbox) { bounding_box = bbox; }
    BoundingBox  extended_object_bounding_box() const;
    virtual bool use_bridge_flow() const { return false; }

    virtual bool no_sort() const { return false; }

    virtual bool is_self_crossing() = 0;

    virtual bool has_consistent_pattern() const { return false; }

    virtual bool aligned_to_origin() const { return false; }

    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);
    virtual ThickPolylines fill_surface_arachne(const Surface* surface, const FillParams& params);
    virtual void set_lock_region_param(const LockRegionParam &lock_param){};
    virtual void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out);

protected:
    Fill() :
        layer_id(size_t(-1)),
        z(0.),
        spacing(0.),
        overlap(0.),
        angle(FLT_MAX),
        link_max_length(0),
        loop_clipping(0),
        bounding_box(Point(0, 0), Point(-1, -1))
        {}

    virtual void    _fill_surface_single(
        const FillParams                & ,
        unsigned int                      ,
        const std::pair<float, Point>   & ,
        ExPolygon                         ,
        Polylines                       & ) {}

    virtual void _fill_surface_single(const FillParams& params,
        unsigned int                   thickness_layers,
        const std::pair<float, Point>& direction,
        ExPolygon                      expolygon,
        ThickPolylines& thick_polylines_out) {}

    virtual float _layer_angle(size_t idx) const { return fixed_angle ? 0.f : (idx & 1) ? float(M_PI/2.) : 0.f; }

    virtual std::pair<float, Point> _infill_direction(const Surface *surface) const;
    
    void _create_gap_fill(const Surface* surface, const FillParams& params, ExtrusionEntityCollection* out);

public:
    static void connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_infill(Polylines &&infill_ordered, const Polygons &boundary, const BoundingBox& bbox, Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_infill(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary, const BoundingBox &bbox, Polylines &polylines_out, double spacing, const FillParams &params);

    static void chain_or_connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params);

    static void connect_base_support(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params);
    static void connect_base_support(Polylines &&infill_ordered, const Polygons &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params);

    static coord_t  _adjust_solid_spacing(const coord_t width, const coord_t distance);
};
   void multiline_fill(Polylines& polylines, const FillParams& params, float spacing);
}

#endif
