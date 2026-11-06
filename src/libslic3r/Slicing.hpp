

#ifndef slic3r_Slicing_hpp_
#define slic3r_Slicing_hpp_

#include <cassert>
#include <cstring>
#include <map>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "libslic3r.h"
#include "Utils.hpp"
#include "Point.hpp"

namespace Slic3r
{

class PrintConfig;
class PrintObjectConfig;
class ModelConfig;
class ModelObject;
class DynamicPrintConfig;




struct SlicingParameters
{
	SlicingParameters() = default;


    static SlicingParameters create_from_config(
         const PrintConfig               &print_config,
         const PrintObjectConfig         &object_config,
         coordf_t                         object_height,
         const std::vector<unsigned int> &object_extruders,
         const Vec3d                     &object_shrinkage_compensation);


    bool        has_raft() const { return raft_layers() > 0; }
    size_t      raft_layers() const { return base_raft_layers + interface_raft_layers; }


    bool        first_object_layer_height_fixed()  const { return ! has_raft() || first_object_layer_bridging; }


    coordf_t    object_print_z_height() const { return object_print_z_max - object_print_z_min; }



     coordf_t    object_print_z_uncompensated_height() const { return object_print_z_uncompensated_max - object_print_z_min; }

    bool        valid { false };


    size_t      base_raft_layers { 0 };

    size_t      interface_raft_layers { 0 };


    coordf_t    base_raft_layer_height { 0 };
    coordf_t    interface_raft_layer_height { 0 };
    coordf_t    contact_raft_layer_height { 0 };



    coordf_t    layer_height { 0 };


    coordf_t    min_layer_height { 0 };
    coordf_t    max_layer_height { 0 };
    coordf_t    max_suport_layer_height { 0 };


    coordf_t    surface_error { 0 };


    coordf_t    overhang_reach { 0 };


    coordf_t    shell_top_nz { 2 };


    coordf_t    curved_underside_nz { 2 };

    coordf_t    road_width { 0 };



    coordf_t    first_print_layer_height { 0 };




    coordf_t    first_object_layer_height { 0 };


    bool 		first_object_layer_bridging { false };


    bool        zero_gap_interface_top { false };
    bool        zero_gap_interface_bottom { false };
    bool        zero_gap_interface_raft { false };

    coordf_t    gap_raft_object { 0 };

    coordf_t    gap_object_support { 0 };

    coordf_t    gap_support_object { 0 };




    coordf_t    raft_base_top_z { 0 };
    coordf_t    raft_interface_top_z { 0 };
    coordf_t    raft_contact_top_z { 0 };

    coordf_t 	object_print_z_min { 0 };

    coordf_t 	object_print_z_max { 0 };



     coordf_t     object_print_z_uncompensated_max { 0 };

     coordf_t    object_shrinkage_compensation_z { 0 };




    double      belt_floor_shear_factor { 0.0 };
    int         belt_floor_from_axis { 1 };
    double      belt_floor_z_shift { 0.0 };
};
static_assert(IsTriviallyCopyable<SlicingParameters>::value, "SlicingParameters class is not POD (and it should be - see constructor).");


inline bool equal_layering(const SlicingParameters &sp1, const SlicingParameters &sp2)
{
    assert(sp1.valid);
    assert(sp2.valid);
    return  sp1.base_raft_layers                    == sp2.base_raft_layers                     &&
            sp1.interface_raft_layers               == sp2.interface_raft_layers                &&
            sp1.base_raft_layer_height              == sp2.base_raft_layer_height               &&
            sp1.interface_raft_layer_height         == sp2.interface_raft_layer_height          &&
            sp1.contact_raft_layer_height           == sp2.contact_raft_layer_height            &&
            sp1.layer_height                        == sp2.layer_height                         &&
            sp1.min_layer_height                    == sp2.min_layer_height                     &&
            sp1.max_layer_height                    == sp2.max_layer_height                     &&

            sp1.first_print_layer_height            == sp2.first_print_layer_height             &&
            sp1.first_object_layer_height           == sp2.first_object_layer_height            &&
            sp1.first_object_layer_bridging         == sp2.first_object_layer_bridging          &&


#if 0
            sp1.zero_gap_interface_top              == sp2.zero_gap_interface_top               &&
            sp1.zero_gap_interface_bottom           == sp2.zero_gap_interface_bottom            &&
            sp1.zero_gap_interface_raft             == sp2.zero_gap_interface_raft              &&
            sp1.gap_raft_object                     == sp2.gap_raft_object                      &&
            sp1.gap_object_support                  == sp2.gap_object_support                   &&
            sp1.gap_support_object                  == sp2.gap_support_object                   &&
#endif
            sp1.raft_base_top_z                     == sp2.raft_base_top_z                      &&
            sp1.raft_interface_top_z                == sp2.raft_interface_top_z                 &&
            sp1.raft_contact_top_z                  == sp2.raft_contact_top_z                   &&
            sp1.object_print_z_min                  == sp2.object_print_z_min;
}

typedef std::pair<coordf_t,coordf_t> t_layer_height_range;
typedef std::map<t_layer_height_range, ModelConfig> t_layer_config_ranges;

std::vector<coordf_t> layer_height_profile_from_ranges(
    const SlicingParameters     &slicing_params,
    const t_layer_config_ranges &layer_config_ranges);

std::vector<double> layer_height_profile_adaptive(
    const SlicingParameters& slicing_params,
    const ModelObject& object, float quality_factor);


std::vector<coordf_t> layer_height_profile_surface_error(
    const SlicingParameters &slicing_params,
    const ModelObject       &object);

struct HeightProfileSmoothingParams
{
    unsigned int radius;
    bool keep_min;

    HeightProfileSmoothingParams() : radius(5), keep_min(false) {}
    HeightProfileSmoothingParams(unsigned int radius, bool keep_min) : radius(radius), keep_min(keep_min) {}
};

std::vector<double> smooth_height_profile(
    const std::vector<double>& profile, const SlicingParameters& slicing_params,
    const HeightProfileSmoothingParams& smoothing_params);

enum LayerHeightEditActionType : unsigned int {
    LAYER_HEIGHT_EDIT_ACTION_INCREASE = 0,
    LAYER_HEIGHT_EDIT_ACTION_DECREASE = 1,
    LAYER_HEIGHT_EDIT_ACTION_REDUCE   = 2,
    LAYER_HEIGHT_EDIT_ACTION_SMOOTH   = 3
};

void adjust_layer_height_profile(
    const ModelObject           &model_object,
    const SlicingParameters     &slicing_params,
    std::vector<coordf_t>       &layer_height_profile,
    coordf_t                     z,
    coordf_t                     layer_thickness_delta,
    coordf_t                     band_width,
    LayerHeightEditActionType    action);



std::vector<coordf_t> generate_object_layers(
    const SlicingParameters     &slicing_params,
    const std::vector<coordf_t> &layer_height_profile,
    bool is_precise_z_height);


bool check_object_layers_fixed(
    const SlicingParameters     &slicing_params,
    const std::vector<coordf_t> &layer_height_profile);




int generate_layer_height_texture(
    const SlicingParameters     &slicing_params,
    const std::vector<coordf_t> &layers,
    void *data, int rows, int cols, bool level_of_detail_2nd_level);

namespace Slicing {

	coordf_t min_layer_height_from_nozzle(const DynamicPrintConfig &print_config, int idx_nozzle);




	coordf_t max_layer_height_from_nozzle(const DynamicPrintConfig &print_config, int idx_nozzle);
}

}

namespace cereal
{
	template<class Archive> void serialize(Archive& archive, Slic3r::t_layer_height_range &lhr) { archive(lhr.first, lhr.second); }
}

#endif
