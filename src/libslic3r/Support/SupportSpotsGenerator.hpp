#ifndef SRC_LIBSLIC3R_SUPPORTABLEISSUESSEARCH_HPP_
#define SRC_LIBSLIC3R_SUPPORTABLEISSUESSEARCH_HPP_

#include "PrintConfig.hpp"
#include "MaterialType.hpp"
#include <boost/log/trivial.hpp>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { class Layer; }


namespace Slic3r {

namespace SupportSpotsGenerator {

struct Params
{
    Params(
        const std::vector<std::string> &filament_types , int raft_layers_count, BrimType brim_type, float brim_width)
        :  raft_layers_count(raft_layers_count), brim_type(brim_type), brim_width(brim_width)
    {
        if (filament_types.size() > 1) {
            BOOST_LOG_TRIVIAL(debug)
                << "SupportSpotsGenerator does not currently handle different materials properly, only first will be used";
        }
        if (filament_types.empty() || filament_types[0].empty()) {
            BOOST_LOG_TRIVIAL(error) << "SupportSpotsGenerator error: empty filament_type";
            filament_type = std::string("PLA");
        } else {
            filament_type = filament_types[0];
            BOOST_LOG_TRIVIAL(debug) << "SupportSpotsGenerator: applying filament type: " << filament_type;
        }
    }


    const float bridge_distance = 16.0f;


    const int raft_layers_count;
    std::string filament_type;

    BrimType brim_type;
    const float brim_width;

    const std::pair<float,float> malformation_distance_factors = std::pair<float, float> { 0.2, 1.1 };
    const float max_curled_height_factor = 10.0f;
    const float curled_distance_expansion = 1.0f;
    const float curling_tolerance_limit = 0.1f;

    const float min_distance_between_support_points = 3.0f;
    const float support_points_interface_radius = 1.5f;
    const float min_distance_to_allow_local_supports = 1.0f;

    const float gravity_constant = 9806.65f;
    const double filament_density = 1.25e-3f;
    const double material_yield_strength = 33.0f * 1e6f;
    const float standard_extruder_conflict_force = 10.0f * gravity_constant;
    const float malformations_additive_conflict_extruder_force = 65.0f * gravity_constant;


    double get_bed_adhesion_yield_strength() const {
        if (raft_layers_count > 0) {
            return get_support_spots_adhesion_strength() * 2.0;
        }

    double yield_strength = 0.02;
    MaterialType::get_yield_strength(filament_type, yield_strength);
        return yield_strength * 1e6;
    }

    double get_support_spots_adhesion_strength() const {
         return 0.016f * 1e6;
    }
};

void estimate_malformations(std::vector<Layer *> &layers, const Params &params);


enum class SupportPointCause {
    LongBridge,
    FloatingBridgeAnchor,
    FloatingExtrusion,
    SeparationFromBed,
    UnstableFloatingPart,
    WeakObjectPart
    };




































































}}

#endif
