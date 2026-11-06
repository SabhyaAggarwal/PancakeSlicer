#ifndef slic3r_SupportParameters_hpp_
#define slic3r_SupportParameters_hpp_

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cstddef>
#include <cassert>
#include <math.h>
#include <limits>
#include "../libslic3r.h"
#include "../Flow.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"

namespace Slic3r {

inline int number_of_support_interface_bottom_layers(const PrintObjectConfig& object_config)
{
    return object_config.support_interface_bottom_layers.value < 0 ?
        object_config.support_interface_top_layers.value :
        object_config.support_interface_bottom_layers.value;
}

struct SupportParameters {
    SupportParameters() = delete;
    SupportParameters(const PrintObject& object)
    {
        const PrintConfig& print_config = object.print()->config();
        const PrintObjectConfig& object_config = object.config();
        const SlicingParameters& slicing_params = object.slicing_parameters();

        this->zero_gap_interface_top = slicing_params.zero_gap_interface_top;
        this->zero_gap_interface_bottom = slicing_params.zero_gap_interface_bottom;
        const bool soluble_interface_non_soluble_base =

            object_config.support_interface_filament.value > 0 && print_config.filament_soluble.get_at(object_config.support_interface_filament.value - 1) &&

            (object_config.support_filament.value == 0 || ! print_config.filament_soluble.get_at(object_config.support_filament.value - 1));
        const bool non_soluble_base_top = this->zero_gap_interface_top && soluble_interface_non_soluble_base;
        const bool non_soluble_base_bottom = this->zero_gap_interface_bottom && soluble_interface_non_soluble_base;

	    {
	        this->num_top_interface_layers    = std::max(0, object_config.support_interface_top_layers.value);
	        this->num_bottom_interface_layers = std::max(0, number_of_support_interface_bottom_layers(object_config));
	        this->has_top_contacts              = num_top_interface_layers    > 0;
	        this->has_bottom_contacts           = num_bottom_interface_layers > 0;



            bool different_support_interface_filament = object_config.support_interface_filament != 0 &&
                                                       object_config.support_interface_filament != object_config.support_filament;

            if (non_soluble_base_top) {
                this->num_top_base_interface_layers = size_t(std::min(int(num_top_interface_layers) / 2, 2));
            } else {

                this->num_top_base_interface_layers = different_support_interface_filament && num_top_interface_layers > 1 ? 1 : 0;
            }

            if (non_soluble_base_bottom) {
                this->num_bottom_base_interface_layers = size_t(std::min(int(num_bottom_interface_layers) / 2, 2));
            } else {

                this->num_bottom_base_interface_layers = different_support_interface_filament && num_bottom_interface_layers > 1 ? 1 : 0;
            }
	    }
        this->first_layer_flow = Slic3r::support_material_1st_layer_flow(&object, float(slicing_params.first_print_layer_height));
        this->support_material_flow = Slic3r::support_material_flow(&object, float(slicing_params.layer_height));
        this->support_material_interface_flow = Slic3r::support_material_interface_flow(&object, float(slicing_params.layer_height));
        this->raft_interface_flow                = support_material_interface_flow;

        this->ironing = object_config.support_ironing;
        this->ironing_flow = support_material_interface_flow.with_height(support_material_interface_flow.height() * 0.01 * object_config.support_ironing_flow.value);
        this->ironing_spacing = std::max(IRONING_SPACING_MIN, object_config.support_ironing_spacing.value);
        this->ironing_pattern = object_config.support_ironing_pattern;


        this->support_layer_height_min = scaled<coord_t>(0.01);
        for (auto lh : print_config.min_layer_height.values)
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, lh));
        for (auto layer : object.layers())
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, layer->height));

        if (this->num_top_interface_layers == 0 && this->num_bottom_interface_layers == 0) {

            this->support_material_interface_flow = this->support_material_flow;
        }



        coordf_t external_perimeter_width = 0.;
        coordf_t bridge_flow_ratio = 0;
        for (size_t region_id = 0; region_id < object.num_printing_regions(); ++ region_id) {
            const PrintRegion &region = object.printing_region(region_id);
            external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(object, frExternalPerimeter, slicing_params.layer_height).width()));
            bridge_flow_ratio += region.config().bridge_flow;
        }
        this->gap_xy = object_config.support_object_xy_distance.value;
        this->gap_xy_first_layer = object_config.support_object_first_layer_gap.value;
        bridge_flow_ratio /= object.num_printing_regions();

        this->support_material_bottom_interface_flow = this->zero_gap_interface_bottom || !object_config.thick_bridges ?
            this->support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
            Flow::bridging_flow(bridge_flow_ratio * this->support_material_interface_flow.nozzle_diameter(), this->support_material_interface_flow.nozzle_diameter());

        this->can_merge_support_regions = object_config.support_filament.value == object_config.support_interface_filament.value;
        if (!this->can_merge_support_regions && (object_config.support_filament.value == 0 || object_config.support_interface_filament.value == 0)) {

            auto object_extruders = object.object_extruders();
            if (object_extruders.size() == 1 &&
                *object_extruders.begin() == std::max<unsigned int>(object_config.support_filament.value, object_config.support_interface_filament.value))

                this->can_merge_support_regions = true;
        }


        this->base_angle = Geometry::deg2rad(float(object_config.support_angle.value));
        this->interface_angle = Geometry::deg2rad(float(object_config.support_angle.value + 90.));

        this->top_interface_spacing = (this->ironing ? 0 : object_config.support_interface_spacing.value) + this->support_material_interface_flow.spacing();
        this->top_interface_density = std::min(1., this->support_material_interface_flow.spacing() / this->top_interface_spacing);

        this->bottom_interface_spacing = object_config.support_bottom_interface_spacing.value + this->support_material_interface_flow.spacing();
        this->bottom_interface_density = std::min(1., this->support_material_interface_flow.spacing() / this->bottom_interface_spacing);

        double raft_interface_spacing = (this->ironing ? 0 : object_config.support_interface_spacing.value) + this->raft_interface_flow.spacing();
        this->raft_interface_density = std::min(1., this->raft_interface_flow.spacing() / raft_interface_spacing);
        this->support_spacing = object_config.support_base_pattern_spacing.value + this->support_material_flow.spacing();
        this->support_density = std::min(1., this->support_material_flow.spacing() / this->support_spacing);
        if (this->num_top_interface_layers == 0) {

            this->top_interface_spacing = this->support_spacing;
            this->top_interface_density = this->support_density;
        }

        SupportMaterialPattern  support_pattern = object_config.support_base_pattern;
        this->with_sheath = object_config.tree_support_wall_count > 0;
        this->base_fill_pattern =
            support_pattern == smpHoneycomb ? ipHoneycomb :
            this->support_density > 0.95 || this->with_sheath ? ipRectilinear : ipSupportBase;
        this->interface_fill_pattern = (this->top_interface_density > 0.95 ? ipRectilinear : ipSupportBase);
        this->raft_interface_fill_pattern = this->raft_interface_density > 0.95 ? ipRectilinear : ipSupportBase;
        const coordf_t contact_interface_density = this->num_top_interface_layers > 0 ?
            this->top_interface_density : this->bottom_interface_density;
        const bool zero_gap_contact_interface = this->num_top_interface_layers > 0 ?
            this->zero_gap_interface_top : this->zero_gap_interface_bottom;
        if (object_config.support_interface_pattern == smipGrid)
            this->contact_fill_pattern = ipGrid;
        else if (object_config.support_interface_pattern == smipRectilinearInterlaced)
            this->contact_fill_pattern = ipRectilinear;
        else if (object_config.support_interface_pattern == smipSpiralInset)
            this->contact_fill_pattern = ipSpiralInset;
        else
            this->contact_fill_pattern =
            (object_config.support_interface_pattern == smipAuto && zero_gap_contact_interface) ||
            object_config.support_interface_pattern == smipConcentric ?
            ipConcentric :
            (contact_interface_density > 0.95 ? ipRectilinear : ipSupportBase);

        this->raft_angle_1st_layer  = 0.f;
        this->raft_angle_base       = 0.f;
        this->raft_angle_interface  = 0.f;
        if (slicing_params.base_raft_layers > 1) {
            assert(slicing_params.raft_layers() >= 4);

            this->raft_angle_1st_layer  = this->interface_angle;
            this->raft_angle_base       = this->base_angle;
            this->raft_angle_interface  = this->interface_angle;
            if ((slicing_params.interface_raft_layers & 1) == 0)

                this->raft_angle_interface += float(0.5 * M_PI);
        } else if (slicing_params.base_raft_layers == 1 || slicing_params.interface_raft_layers > 1) {
            assert(slicing_params.raft_layers() == 2 || slicing_params.raft_layers() == 3);

            this->raft_angle_1st_layer  = this->base_angle;
            this->raft_angle_interface  = this->interface_angle + 0.5 * M_PI;
        } else if (slicing_params.interface_raft_layers == 1) {

            assert(slicing_params.base_raft_layers == 0);
            assert(slicing_params.interface_raft_layers == 1);
            assert(slicing_params.raft_layers() == 1);
            this->raft_angle_1st_layer = float(0.5 * M_PI);
            this->raft_angle_interface = this->raft_angle_1st_layer;
        } else {

            assert(slicing_params.base_raft_layers == 0);
            assert(slicing_params.interface_raft_layers == 0);
            assert(slicing_params.raft_layers() == 0);
        }

	    const auto     nozzle_diameter = print_config.nozzle_diameter.get_at(object_config.support_interface_filament - 1);
        const coordf_t extrusion_width = object_config.line_width.get_abs_value(nozzle_diameter);
        support_extrusion_width        = object_config.support_line_width.get_abs_value(nozzle_diameter);
        support_extrusion_width        = support_extrusion_width > 0 ? support_extrusion_width : extrusion_width;

        independent_layer_height = print_config.independent_support_layer_height;


        tree_branch_diameter_double_wall_area_scaled = object_config.tree_support_wall_count.value > 1  ? 0.1 :
                                                       object_config.tree_support_wall_count.value == 0 ? 0.25 * sqr(scaled<double>(5.0)) * M_PI :
                                                                                                          std::numeric_limits<double>::max();

        support_style = object_config.support_style;
        support_interface_pattern = object_config.support_interface_pattern;
        if (support_style != smsDefault) {
            if ((support_style == smsSnug || support_style == smsGrid) && is_tree(object_config.support_type)) support_style = smsDefault;
            if ((support_style == smsTreeSlim || support_style == smsTreeStrong || support_style == smsTreeHybrid || support_style == smsTreeOrganic) &&
                !is_tree(object_config.support_type))
                support_style = smsDefault;
        }
        if (support_style == smsDefault) {
            if (is_tree(object_config.support_type)) {

                support_style = object.tree_default_style();
            } else {
                support_style = smsGrid;
            }
        }
    }

    bool                    zero_gap_interface_top;
    bool                    zero_gap_interface_bottom;


    bool                    has_top_contacts;

    bool                    has_bottom_contacts;

    size_t                  num_top_interface_layers;

    size_t                  num_bottom_interface_layers;

    size_t                  num_top_base_interface_layers;

    size_t                  num_bottom_base_interface_layers;

    bool                    has_contacts() const { return this->has_top_contacts || this->has_bottom_contacts; }
    bool                    has_interfaces() const { return this->num_top_interface_layers + this->num_bottom_interface_layers > 0; }
    bool                    has_base_interfaces() const { return this->num_top_base_interface_layers + this->num_bottom_base_interface_layers > 0; }
    size_t                  num_top_interface_layers_only() const { return this->num_top_interface_layers - this->num_top_base_interface_layers; }
    size_t                  num_bottom_interface_layers_only() const { return this->num_bottom_interface_layers - this->num_bottom_base_interface_layers; }


	Flow 					first_layer_flow;


	Flow 					support_material_flow;

	Flow 					support_material_interface_flow;

	Flow 					support_material_bottom_interface_flow;

	Flow    				raft_interface_flow;
    coordf_t support_extrusion_width;

	bool 					can_merge_support_regions;

    coordf_t 				support_layer_height_min;


    coordf_t	gap_xy;
    coordf_t	gap_xy_first_layer;

    float    				base_angle;
    float    				interface_angle;
    coordf_t 				top_interface_spacing;
    coordf_t 				bottom_interface_spacing;
    coordf_t				support_expansion=0;

    coordf_t 				top_interface_density;

    coordf_t 				bottom_interface_density;

    coordf_t 				raft_interface_density;
    coordf_t 				support_spacing;

    coordf_t 				support_density;
    SupportMaterialStyle    support_style = smsDefault;
    SupportMaterialInterfacePattern support_interface_pattern = smipAuto;


    InfillPattern           base_fill_pattern;

    InfillPattern           interface_fill_pattern;

    InfillPattern           raft_interface_fill_pattern;

    InfillPattern 			contact_fill_pattern;

    bool                    with_sheath;

    double                  tree_branch_diameter_double_wall_area_scaled = 0.25 * sqr(scaled<double>(5.0)) * M_PI;;

    float 					raft_angle_1st_layer;
    float 					raft_angle_base;
    float 					raft_angle_interface;


    float 					raft_interface_angle(size_t interface_id) const
        { return this->raft_angle_interface + ((interface_id & 1) ? float(- M_PI_4) : float(+ M_PI_4)); }



    float 					support_interface_angle(size_t interface_id) const
        {
            float angle;

            switch (this->support_interface_pattern) {
                case SupportMaterialInterfacePattern::smipRectilinear:
                    angle = support_style == SupportMaterialStyle::smsSnug ? this->interface_angle - float(M_PI_4) : this->interface_angle;
                    break;
                case SupportMaterialInterfacePattern::smipRectilinearInterlaced:
                    angle = this->interface_angle + ((interface_id & 1) ? float(M_PI_4) : float(-M_PI_4));
                    break;
                case SupportMaterialInterfacePattern::smipGrid:
                    angle = this->base_angle;
                    break;
                default:
                    angle = this->interface_angle;
                    break;
            }

            return angle;
        }

    bool independent_layer_height = false;
    const double thresh_big_overhang = Slic3r::sqr(scale_(10));

	bool          ironing;
    Flow          ironing_flow;
    InfillPattern ironing_pattern;
    float         ironing_spacing;
};

}

#endif
