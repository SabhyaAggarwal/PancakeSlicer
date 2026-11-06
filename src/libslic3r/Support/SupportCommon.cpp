#include "../ClipperUtils.hpp"
#include "../ClipperZUtils.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../Fill/FillBase.hpp"
#include "../MutablePolygon.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"

#include <cmath>
#include <boost/container/static_vector.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <math.h>
#include <initializer_list>
#include <tbb/parallel_for.h>
#include <utility>
#include <vector>
#include <unordered_map>

#include "SupportCommon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Polyline.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Surface.hpp"
#include "libslic3r/Utils.hpp"




#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
    #include "../utils.hpp"
    #include "../SVG.hpp"
#endif

#include <cassert>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"

namespace Slic3r {



#define SUPPORT_MATERIAL_MARGIN 1.5



#define SUPPORT_SURFACES_OFFSET_PARAMETERS jtSquare, 0.


std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> generate_interface_layers(
    const PrintObjectConfig           &config,
    const SupportParameters           &support_params,
    const SupportGeneratorLayersPtr   &bottom_contacts,
    const SupportGeneratorLayersPtr   &top_contacts,

    SupportGeneratorLayersPtr         &top_interface_layers,
    SupportGeneratorLayersPtr         &top_base_interface_layers,

    SupportGeneratorLayersPtr         &intermediate_layers,
    SupportGeneratorLayerStorage      &layer_storage)
{
    std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> base_and_interface_layers;

    if (! intermediate_layers.empty() && support_params.has_interfaces()) {

        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - start";
        const bool                 snug_supports          = support_params.support_style == smsSnug;
        const bool                 smooth_supports        = support_params.support_style != smsGrid;
        SupportGeneratorLayersPtr &interface_layers       = base_and_interface_layers.first;
        SupportGeneratorLayersPtr &base_interface_layers  = base_and_interface_layers.second;


        const bool organic_tree = support_params.support_style == smsTreeOrganic;
        const size_t num_top_interface_layers         = support_params.has_top_contacts ?
                                                        support_params.num_top_interface_layers - 1 : 0;
        const size_t num_bottom_interface_layers      = support_params.has_bottom_contacts ?
                                                        support_params.num_bottom_interface_layers - (organic_tree ? 0 : 1) : 0;
        const size_t num_top_base_interface_layers    = std::min(support_params.num_top_base_interface_layers,    num_top_interface_layers);
        const size_t num_bottom_base_interface_layers = std::min(support_params.num_bottom_base_interface_layers, num_bottom_interface_layers);
        const size_t num_top_interface_layers_only    = num_top_interface_layers    - num_top_base_interface_layers;
        const size_t num_bottom_interface_layers_only = num_bottom_interface_layers - num_bottom_base_interface_layers;

        interface_layers.assign(intermediate_layers.size(), nullptr);
        if (support_params.has_base_interfaces())
            base_interface_layers.assign(intermediate_layers.size(), nullptr);
        const auto smoothing_distance    = support_params.support_material_interface_flow.scaled_spacing() * 1.5;

        const auto minimum_island_radius_top = support_params.support_material_interface_flow.scaled_spacing() / support_params.top_interface_density;
        const auto minimum_island_radius_bottom = support_params.support_material_interface_flow.scaled_spacing() / support_params.bottom_interface_density;
        const auto closing_distance      = smoothing_distance;


        auto insert_layer = [&layer_storage, smooth_supports, closing_distance, smoothing_distance, minimum_island_radius_top, minimum_island_radius_bottom](
                SupportGeneratorLayer &intermediate_layer, Polygons &bottom, Polygons &&top, SupportGeneratorLayer *top_interface_layer,
                const Polygons *subtract, SupporLayerType type) -> SupportGeneratorLayer* {
            bool has_top_interface = top_interface_layer && ! top_interface_layer->polygons.empty();
            assert(! bottom.empty() || ! top.empty() || has_top_interface);

            auto regularize = [&](Polygons polys, coordf_t minimum_island_radius) -> Polygons {
                if (polys.empty())
                    return polys;
                return smooth_supports ?
                    smooth_outward(closing(std::move(polys), closing_distance + minimum_island_radius, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance) :
                    union_safety_offset(std::move(polys));
            };

            Polygons bottom_polys = regularize(std::move(bottom), minimum_island_radius_bottom);
            Polygons top_polys = regularize(std::move(top), minimum_island_radius_top);
            append(bottom_polys, std::move(top_polys));
            bottom = intersection(std::move(bottom_polys), intermediate_layer.polygons);
            if (has_top_interface) {


                bottom = union_(std::move(top_interface_layer->polygons), bottom);
                top_interface_layer->polygons.clear();
            }
            if (! bottom.empty()) {


                if (! bottom.empty()) {
                    SupportGeneratorLayer &layer_new = top_interface_layer ? *top_interface_layer : layer_storage.allocate(type);
                    layer_new.polygons   = std::move(bottom);
                    layer_new.print_z    = intermediate_layer.print_z;
                    layer_new.bottom_z   = intermediate_layer.bottom_z;
                    layer_new.height     = intermediate_layer.height;
                    layer_new.bridging   = intermediate_layer.bridging;

                    intermediate_layer.polygons = diff(intermediate_layer.polygons, layer_new.polygons);
                    if (subtract)

                        layer_new.polygons = diff(std::move(layer_new.polygons), *subtract);


                    return &layer_new;
                }
            }
            return nullptr;
        };
        tbb::parallel_for(tbb::blocked_range<int>(0, int(intermediate_layers.size())),
            [&bottom_contacts, &top_contacts, &top_interface_layers, &top_base_interface_layers, &intermediate_layers, &insert_layer,
             num_top_interface_layers, num_bottom_interface_layers, num_top_base_interface_layers, num_bottom_base_interface_layers,
             num_top_interface_layers_only, num_bottom_interface_layers_only,
             snug_supports, &interface_layers, &base_interface_layers](const tbb::blocked_range<int>& range) {



                auto idx_top_contact_first        = -1;

                auto idx_bottom_contact_first     = -1;

                auto idx_top_interface_first      = -1;

                auto idx_top_base_interface_first = -1;
                auto num_intermediate = int(intermediate_layers.size());
                for (int idx_intermediate_layer = range.begin(); idx_intermediate_layer < range.end(); ++ idx_intermediate_layer) {
                    SupportGeneratorLayer &intermediate_layer = *intermediate_layers[idx_intermediate_layer];
                    Polygons polygons_top_contact_projected_interface;
                    Polygons polygons_top_contact_projected_base;
                    Polygons polygons_bottom_contact_projected_interface;
                    Polygons polygons_bottom_contact_projected_base;
                    if (num_top_interface_layers > 0) {

                        coordf_t top_z              = intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + int(num_top_interface_layers) - 1)]->print_z;
                        coordf_t top_interface_z     = std::numeric_limits<coordf_t>::max();
                        if (num_top_base_interface_layers > 0)

                            top_interface_z = num_top_interface_layers_only == 0 ?

                                - std::numeric_limits<coordf_t>::max() :
                                intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + int(num_top_interface_layers_only) - 1)]->print_z;

                        idx_top_contact_first = idx_higher_or_equal(top_contacts, idx_top_contact_first, [&intermediate_layer](const SupportGeneratorLayer *layer){ return layer->print_z >= intermediate_layer.print_z; });

                        for (int idx_top_contact = idx_top_contact_first; idx_top_contact < int(top_contacts.size()); ++ idx_top_contact) {
                            const SupportGeneratorLayer &top_contact_layer = *top_contacts[idx_top_contact];

                            if (top_contact_layer.bottom_z - EPSILON > top_z)
                                break;
                            polygons_append(top_contact_layer.bottom_z - EPSILON > top_interface_z ? polygons_top_contact_projected_base : polygons_top_contact_projected_interface,


                                snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                        }
                    }
                    if (num_bottom_interface_layers > 0) {

                        coordf_t bottom_z           = intermediate_layers[std::max(0, idx_intermediate_layer - int(num_bottom_interface_layers) + 1)]->bottom_z;
                        coordf_t bottom_interface_z = - std::numeric_limits<coordf_t>::max();
                        if (num_bottom_base_interface_layers > 0)

                            bottom_interface_z = num_bottom_interface_layers_only == 0 ?

                                std::numeric_limits<coordf_t>::max() :
                                intermediate_layers[std::max(0, idx_intermediate_layer - int(num_bottom_interface_layers_only))]->bottom_z;

                        idx_bottom_contact_first = idx_higher_or_equal(bottom_contacts, idx_bottom_contact_first, [bottom_z](const SupportGeneratorLayer *layer){ return layer->print_z >= bottom_z - EPSILON; });

                        for (int idx_bottom_contact = idx_bottom_contact_first; idx_bottom_contact < int(bottom_contacts.size()); ++ idx_bottom_contact) {
                            const SupportGeneratorLayer &bottom_contact_layer = *bottom_contacts[idx_bottom_contact];
                            if (bottom_contact_layer.print_z - EPSILON > intermediate_layer.bottom_z)
                                break;
                            polygons_append(bottom_contact_layer.print_z - EPSILON > bottom_interface_z ? polygons_bottom_contact_projected_interface : polygons_bottom_contact_projected_base, bottom_contact_layer.polygons);
                        }
                    }
                    auto resolve_same_layer = [](SupportGeneratorLayersPtr &layers, int &idx, coordf_t print_z) -> SupportGeneratorLayer* {
                        if (! layers.empty()) {
                            idx = idx_higher_or_equal(layers, idx, [print_z](const SupportGeneratorLayer *layer) { return layer->print_z > print_z - EPSILON; });
                            if (idx < int(layers.size()) && layers[idx]->print_z < print_z + EPSILON)
                                return layers[idx];
                        }
                        return nullptr;
                    };
                    SupportGeneratorLayer *top_interface_layer      = resolve_same_layer(top_interface_layers, idx_top_interface_first, intermediate_layer.print_z);
                    SupportGeneratorLayer *top_base_interface_layer = resolve_same_layer(top_base_interface_layers, idx_top_base_interface_first, intermediate_layer.print_z);
                    SupportGeneratorLayer *interface_layer          = nullptr;
                    if (! polygons_bottom_contact_projected_interface.empty() || ! polygons_top_contact_projected_interface.empty() ||
                        (top_interface_layer && ! top_interface_layer->polygons.empty())) {
                        interface_layer = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_interface, std::move(polygons_top_contact_projected_interface), top_interface_layer,
                            nullptr, polygons_top_contact_projected_interface.empty() ? SupporLayerType::BottomInterface : SupporLayerType::TopInterface);
                        interface_layers[idx_intermediate_layer] = interface_layer;
                    }
                    if (! polygons_bottom_contact_projected_base.empty() || ! polygons_top_contact_projected_base.empty() ||
                        (top_base_interface_layer && ! top_base_interface_layer->polygons.empty()))
                        base_interface_layers[idx_intermediate_layer] = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_base, std::move(polygons_top_contact_projected_base), top_base_interface_layer,
                            interface_layer ? &interface_layer->polygons : nullptr, SupporLayerType::Base);
                }
            });




        auto merge_remove_empty = [](SupportGeneratorLayersPtr &in1, SupportGeneratorLayersPtr &in2) {
            auto remove_empty = [](SupportGeneratorLayersPtr &vec) {
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(), [](const SupportGeneratorLayer *ptr) { return ptr == nullptr || ptr->polygons.empty(); }),
                    vec.end());
            };
            remove_empty(in1);
            remove_empty(in2);
            if (in2.empty())
                return std::move(in1);
            else if (in1.empty())
                return std::move(in2);
            else {
                SupportGeneratorLayersPtr out(in1.size() + in2.size(), nullptr);
                std::merge(in1.begin(), in1.end(), in2.begin(), in2.end(), out.begin(), [](auto* l, auto* r) { return l->print_z < r->print_z; });
                return out;
            }
        };
        interface_layers      = merge_remove_empty(interface_layers,      top_interface_layers);
        base_interface_layers = merge_remove_empty(base_interface_layers, top_base_interface_layers);
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - end";
    }

    return base_and_interface_layers;
}

SupportGeneratorLayersPtr generate_raft_base(
    const PrintObject                 &object,
    const SupportParameters           &support_params,
	const SlicingParameters			  &slicing_params,
    const SupportGeneratorLayersPtr   &top_contacts,
    const SupportGeneratorLayersPtr   &interface_layers,
    const SupportGeneratorLayersPtr   &base_interface_layers,
    const SupportGeneratorLayersPtr   &base_layers,
    SupportGeneratorLayerStorage      &layer_storage)
{

    Polygons brim;
    if (object.has_brim()) {


        const BrimType brim_type       = object.config().brim_type;


        const bool     brim_outer      = brim_type == btOuterOnly || brim_type == btOuterAndInner || brim_type == btLeadingEdgeOnly;
        const bool     brim_inner      = brim_type == btInnerOnly || brim_type == btOuterAndInner;

        const auto     brim_object_gap = scaled<float>(object.config().brim_object_gap.value);

        for (const ExPolygon &ex : object.layers().front()->lslices) {
            if (brim_outer && brim_inner)
                polygons_append(brim, offset(ex, brim_object_gap));
            else {
                if (brim_outer)
                    polygons_append(brim, offset(ex.contour, brim_object_gap, jtRound, float(scale_(0.1))));
                else
                    brim.emplace_back(ex.contour);
                if (brim_inner) {
                    Polygons holes = ex.holes;
                    polygons_reverse(holes);
                    holes = shrink(holes, brim_object_gap, jtRound, float(scale_(0.1)));
                    polygons_reverse(holes);
                    polygons_append(brim, std::move(holes));
                } else
                    polygons_append(brim, ex.holes);
            }
        }
        brim = union_(brim);
    }


    const float inflate_factor_fine      = float(scale_((slicing_params.raft_layers() > 1) ? 0.5 : EPSILON));




    const bool  belt_floor_active        = std::abs(slicing_params.belt_floor_shear_factor) > EPSILON;
    const float inflate_factor_1st_layer = belt_floor_active ? 0.f :
        std::max(0.f, float(scale_(object.config().raft_first_layer_expansion)) - inflate_factor_fine);
    SupportGeneratorLayer       *contacts         = top_contacts         .empty() ? nullptr : top_contacts         .front();
    SupportGeneratorLayer       *interfaces       = interface_layers     .empty() ? nullptr : interface_layers     .front();
    SupportGeneratorLayer       *base_interfaces  = base_interface_layers.empty() ? nullptr : base_interface_layers.front();
    SupportGeneratorLayer       *columns_base     = base_layers          .empty() ? nullptr : base_layers          .front();
    if (contacts != nullptr && contacts->print_z > std::max(slicing_params.first_print_layer_height, slicing_params.raft_contact_top_z) + EPSILON)

        contacts = nullptr;
    if (interfaces != nullptr && interfaces->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)

        interfaces = nullptr;
    if (base_interfaces != nullptr && base_interfaces->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)

        base_interfaces = nullptr;
    if (columns_base != nullptr && columns_base->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)

        columns_base = nullptr;

    Polygons interface_polygons;
    if (contacts != nullptr && ! contacts->polygons.empty())
        polygons_append(interface_polygons, expand(contacts->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (interfaces != nullptr && ! interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (base_interfaces != nullptr && ! base_interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(base_interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));


    SupportGeneratorLayersPtr raft_layers;

    if (slicing_params.raft_layers() > 1) {
        Polygons base;
        Polygons columns;
        Polygons first_layer;
        if (columns_base != nullptr) {
            if (columns_base->bottom_print_z() > slicing_params.raft_interface_top_z - EPSILON) {

                base = columns_base->polygons;
                columns = base;
                if (! interface_polygons.empty())

                    columns = diff(columns, interface_polygons);
            } else {

                assert(is_approx(columns_base->print_z, slicing_params.first_print_layer_height));
                first_layer = columns_base->polygons;
            }
        }
        if (! interface_polygons.empty()) {

            base = union_(base, interface_polygons);
        }


        {
            SupportGeneratorLayer &new_layer = layer_storage.allocate(slicing_params.base_raft_layers > 0 ? SupporLayerType::RaftBase : SupporLayerType::RaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = slicing_params.first_print_layer_height;
            new_layer.height  = slicing_params.first_print_layer_height;
            new_layer.bottom_z = 0.;
            first_layer = union_(std::move(first_layer), base);
            new_layer.polygons = inflate_factor_1st_layer > 0 ? expand(first_layer, inflate_factor_1st_layer) : first_layer;
        }

        for (size_t i = 1; i < slicing_params.base_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            SupportGeneratorLayer &new_layer  = layer_storage.allocate_unguarded(SupporLayerType::RaftBase);
            raft_layers.push_back(&new_layer);
            new_layer.print_z  = print_z + slicing_params.base_raft_layer_height;
            new_layer.height   = slicing_params.base_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = base;
        }

        for (size_t i = 1; i < slicing_params.interface_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            SupportGeneratorLayer &new_layer = layer_storage.allocate_unguarded(SupporLayerType::RaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = print_z + slicing_params.interface_raft_layer_height;
            new_layer.height  = slicing_params.interface_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = interface_polygons;

            new_layer.contact_polygons = std::make_unique<Polygons>(columns);
        }
    } else {
        if (columns_base != nullptr) {

            Polygons &raft     = columns_base->polygons;
            Polygons  trimming;


            trimming = offset(object.layers().front()->lslices, (float) scale_(support_params.gap_xy_first_layer), SUPPORT_SURFACES_OFFSET_PARAMETERS);
            if (inflate_factor_1st_layer > SCALED_EPSILON) {

                auto  nsteps = std::max(5, int(ceil(inflate_factor_1st_layer / support_params.first_layer_flow.scaled_width())));
                float step   = inflate_factor_1st_layer / nsteps;
                for (int i = 0; i < nsteps; ++ i)
                    raft = diff(expand(raft, step), trimming);
            } else
                raft = diff(raft, trimming);
            if (! interface_polygons.empty())
                columns_base->polygons = diff(columns_base->polygons, interface_polygons);
        }
        if (! brim.empty()) {
            if (columns_base)
                columns_base->polygons = diff(columns_base->polygons, brim);
            if (contacts)
                contacts->polygons = diff(contacts->polygons, brim);
            if (interfaces)
                interfaces->polygons = diff(interfaces->polygons, brim);
            if (base_interfaces)
                base_interfaces->polygons = diff(base_interfaces->polygons, brim);
        }
    }

    return raft_layers;
}

static inline void fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon              &&expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        assert(!fill_params.use_arachne);
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }
    extrusion_entities_append_paths(
        dst,
        std::move(polylines),
        role,
        flow.mm3_per_mm(), flow.width(), flow.height());
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    for (ExPolygon &expoly : expolygons)
        fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, density, role, flow);
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density     = density;
    fill_params.dont_adjust = true;
    fill_expolygons_generate_paths(dst, std::move(expolygons), filler, fill_params, density, role, flow);
}

static Polylines draw_perimeters(const ExPolygon &expoly, double clip_length)
{

    Polylines polylines;
    polylines.reserve(expoly.holes.size() + 1);
    for (size_t i = 0; i <= expoly.holes.size();  ++ i) {
        Polyline pl(i == 0 ? expoly.contour.points : expoly.holes[i - 1].points);
        pl.points.emplace_back(pl.points.front());
        if (i > 0)

            pl.reverse();

        pl.clip_end(clip_length);
        polylines.emplace_back(std::move(pl));
    }
    return polylines;
}

void tree_supports_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    const Polygons          &polygons,
    const Flow              &flow,
    const SupportParameters &support_params)
{



    auto shrink_expolygon_with_contour_idx = [](const Slic3r::ExPolygon &expoly, const float delta, JoinType joinType, double miterLimit, ClipperZUtils::ZPaths &out) -> int
    {
        assert(delta > 0);
        auto append_paths_with_z = [](const Polygons &src, coord_t contour_idx, ClipperZUtils::ZPaths &dst) {
            dst.reserve(next_highest_power_of_2(dst.size() + src.size()));
            for (const Polygon &contour : src)
                dst.emplace_back(ClipperZUtils::to_zpath(contour.points, contour_idx));
        };

        auto offset_ccw = [joinType, miterLimit](Polygon polygon, float d) {
            if (! polygon.is_counter_clockwise())
                polygon.reverse();
            return offset(polygon, d, joinType, miterLimit);
        };


        ClipperZUtils::ZPaths contours;
        {
            Polygons contours_raw = offset_ccw(expoly.contour, - delta);
            if (contours_raw.empty())

                return 0;
            append_paths_with_z(contours_raw, 0, contours);
        }

        if (expoly.holes.empty()) {

            append(out, std::move(contours));
        } else {

            ClipperZUtils::ZPaths holes;
            for (const Polygon &hole : expoly.holes)
                append_paths_with_z(offset_ccw(hole, delta), 1 + (&hole - expoly.holes.data()), holes);


            if (holes.empty()) {

                append(out, std::move(contours));
            } else {


                ClipperZUtils::ZPaths output = ClipperZUtils::clip_zpaths(ctDifference, contours, false, holes,
                    [](const ClipperZUtils::ZPoint &, const ClipperZUtils::ZPoint &, const ClipperZUtils::ZPoint &, const ClipperZUtils::ZPoint &, ClipperZUtils::ZPoint &pt) {

                        pt.z() = -1;
                    });
                if (! output.empty()) {
                    append(out, std::move(output));
                } else {

                    return 0;
                }
            }
        }

        return 1;
    };

    const double spacing = flow.scaled_spacing();

    const double clip_length = spacing * 0.15;
    const double anchor_length = spacing * 6.;
    ClipperZUtils::ZPaths anchor_candidates;
    for (ExPolygon& expoly : closing_ex(polygons, float(SCALED_EPSILON), float(SCALED_EPSILON + 0.5 * flow.scaled_width()))) {
        std::unique_ptr<ExtrusionEntityCollection> eec;
        ExPolygons                                 regions_to_draw_inner_wall{expoly};
        if (support_params.tree_branch_diameter_double_wall_area_scaled > 0)
            if (double area = expoly.area(); area > support_params.tree_branch_diameter_double_wall_area_scaled) {
                BOOST_LOG_TRIVIAL(debug)<< "TreeSupports: double wall area: " << area<< " > " << support_params.tree_branch_diameter_double_wall_area_scaled;
                eec = std::make_unique<ExtrusionEntityCollection>();

                eec->no_sort = true;

                ExPolygons level2 = offset2_ex({expoly}, -1.5 * flow.scaled_width(), 0.5 * flow.scaled_width());
                if (level2.size() > 0) {
                    regions_to_draw_inner_wall = level2;
                    extrusion_entities_append_paths(eec->entities, draw_perimeters(expoly, clip_length), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height(),

                            false);
                    expoly = level2.front();
                }
            }
        for (ExPolygon &expoly : regions_to_draw_inner_wall)
        {



            anchor_candidates.clear();
            shrink_expolygon_with_contour_idx(expoly, flow.scaled_width(), jtMiter, 1.2, anchor_candidates);

            for (auto &path : anchor_candidates)
                if (ClipperZUtils::area(path) > 0) std::reverse(path.begin(), path.end());


            Polylines polylines;
            polylines.reserve(expoly.holes.size() + 1);
            for (int idx_loop = 0; idx_loop < int(expoly.num_contours()); ++idx_loop) {

                const Polygon &loop = expoly.contour_or_hole(idx_loop);
                Polyline       pl(loop.points);

                if (idx_loop == 0)

                    pl.reverse();
                pl.points.emplace_back(pl.points.front());
                pl.clip_end(clip_length);
                if (pl.size() < 2) continue;

                ClipperZUtils::ZPath *closest_contour = nullptr;
                Vec2d                 closest_point;
                int                   closest_point_idx = -1;
                double                closest_point_t   = 0.;
                double                d2min             = std::numeric_limits<double>::max();
                Vec2d                 seam_pt           = pl.back().cast<double>();
                for (ClipperZUtils::ZPath &path : anchor_candidates)
                    for (int i = 0; i < int(path.size()); ++i) {
                        int j = next_idx_modulo(i, path);
                        if (path[i].z() == idx_loop || path[j].z() == idx_loop) {
                            Vec2d pi(path[i].x(), path[i].y());
                            Vec2d pj(path[j].x(), path[j].y());
                            Vec2d v  = pj - pi;
                            Vec2d w  = seam_pt - pi;
                            auto  l2 = v.squaredNorm();
                            auto  t  = std::clamp((l2 == 0) ? 0 : v.dot(w) / l2, 0., 1.);
                            if ((path[i].z() == idx_loop || t > EPSILON) && (path[j].z() == idx_loop || t < 1. - EPSILON)) {

                                Vec2d  fp = pi + v * t;
                                double d2 = (fp - seam_pt).squaredNorm();
                                if (d2 < d2min) {
                                    d2min             = d2;
                                    closest_contour   = &path;
                                    closest_point     = fp;
                                    closest_point_idx = i;
                                    closest_point_t   = t;
                                }
                            }
                        }
                    }
                if (d2min < sqr(flow.scaled_width() * 3.)) {


                    pl.points.emplace_back(closest_point.cast<coord_t>());
                    const ClipperZUtils::ZPath &path             = *closest_contour;
                    double                      remaining_length = anchor_length - (seam_pt - closest_point).norm();
                    int                         i                = closest_point_idx;
                    int                         j                = next_idx_modulo(i, *closest_contour);
                    Vec2d                       pi(path[i].x(), path[i].y());
                    Vec2d                       pj(path[j].x(), path[j].y());
                    Vec2d                       v = pj - pi;
                    double                      l = v.norm();
                    if (remaining_length < (1. - closest_point_t) * l) {

                        pl.points.emplace_back((closest_point + v * (remaining_length / l)).cast<coord_t>());
                    } else {

                        pl.points.emplace_back(path[j].x(), path[j].y());
                        pi = pj;
                        for (i = j; path[i].z() == idx_loop && remaining_length > 0; i = j, pi = pj) {
                            j  = next_idx_modulo(i, path);
                            pj = Vec2d(path[j].x(), path[j].y());
                            v  = pj - pi;
                            l  = v.norm();
                            if (i == closest_point_idx) {

                                break;
                            }
                            if (remaining_length <= l) {
                                pl.points.emplace_back((pi + v * (remaining_length / l)).cast<coord_t>());
                                break;
                            }
                            pl.points.emplace_back(path[j].x(), path[j].y());
                            remaining_length -= l;
                        }
                    }
                }

                pl.reverse();
                polylines.emplace_back(std::move(pl));
            }

            ExtrusionEntitiesPtr &out = eec ? eec->entities : dst;
            extrusion_entities_append_paths(out, std::move(polylines), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height(),

                                            false);
        }
        if (eec) {
            std::reverse(eec->entities.begin(), eec->entities.end());
            dst.emplace_back(eec.release());
        }
    }
}

void fill_expolygons_with_sheath_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    const Polygons          &polygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow,
    const SupportParameters& support_params,
    bool                     with_sheath,
    bool                     no_sort)
{
    if (polygons.empty())
        return;

    if (with_sheath) {
        if (density == 0) {
            tree_supports_generate_paths(dst, polygons, flow, support_params);
            return;
        }
    }
    else {
        fill_expolygons_generate_paths(dst, closing_ex(polygons, float(SCALED_EPSILON)), filler, density, role, flow);
        return;
    }

    FillParams fill_params;
    fill_params.density     = density;
    fill_params.dont_adjust = true;

    const double spacing = flow.scaled_spacing();

    const double clip_length = spacing * 0.15;

    for (ExPolygon &expoly : closing_ex(polygons, float(SCALED_EPSILON), float(SCALED_EPSILON + 0.5*flow.scaled_width()))) {

        std::unique_ptr<ExtrusionEntityCollection> eec;
        if (no_sort) {
            eec = std::make_unique<ExtrusionEntityCollection>();
            eec->no_sort = true;
        }
        ExtrusionEntitiesPtr &out = no_sort ? eec->entities : dst;
        extrusion_entities_append_paths(out, draw_perimeters(expoly, clip_length), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height());

        fill_expolygons_generate_paths(out, offset_ex(expoly, float(-0.4 * spacing)), filler, fill_params, density, role, flow);
        if (no_sort && ! eec->empty())
            dst.emplace_back(eec.release());
    }
}


struct SupportGeneratorLayerExtruded
{
    SupportGeneratorLayerExtruded& operator=(SupportGeneratorLayerExtruded &&rhs) {
        this->layer = rhs.layer;
        this->extrusions = std::move(rhs.extrusions);
        m_polygons_to_extrude = std::move(rhs.m_polygons_to_extrude);
        rhs.layer = nullptr;
        return *this;
    }

    bool empty() const {
        return layer == nullptr || layer->polygons.empty();
    }

    void set_polygons_to_extrude(Polygons &&polygons) {
        if (m_polygons_to_extrude == nullptr)
            m_polygons_to_extrude = std::make_unique<Polygons>(std::move(polygons));
        else
            *m_polygons_to_extrude = std::move(polygons);
    }
    Polygons& polygons_to_extrude() { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }
    const Polygons& polygons_to_extrude() const { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }

    bool could_merge(const SupportGeneratorLayerExtruded &other) const {
        return ! this->empty() && ! other.empty() &&
            std::abs(this->layer->height - other.layer->height) < EPSILON &&
            this->layer->bridging == other.layer->bridging;
    }


    void merge(SupportGeneratorLayerExtruded &&other) {
        assert(this->could_merge(other));

        if (other.m_polygons_to_extrude != nullptr) {
            if (m_polygons_to_extrude == nullptr) {

                assert(this->extrusions.empty());
                m_polygons_to_extrude = std::make_unique<Polygons>(this->layer->polygons);
            }
            Slic3r::polygons_append(*m_polygons_to_extrude, std::move(*other.m_polygons_to_extrude));
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
            other.m_polygons_to_extrude.reset();
        } else if (m_polygons_to_extrude != nullptr) {
            assert(other.m_polygons_to_extrude == nullptr);

            assert(other.extrusions.empty());
            Slic3r::polygons_append(*m_polygons_to_extrude, other.layer->polygons);
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
        }

        this->extrusions.insert(this->extrusions.end(), other.extrusions.begin(), other.extrusions.end());
        other.extrusions.clear();

        Slic3r::polygons_append(this->layer->polygons, std::move(other.layer->polygons));
        this->layer->polygons = union_safety_offset(this->layer->polygons);
        other.layer->polygons.clear();
    }

    void polygons_append(Polygons &dst) const {
        if (layer != NULL && ! layer->polygons.empty())
            Slic3r::polygons_append(dst, layer->polygons);
    }


    SupportGeneratorLayer  *layer { nullptr };

    ExtrusionEntitiesPtr                  extrusions;

private:


    std::unique_ptr<Polygons>             m_polygons_to_extrude;
};

typedef std::vector<SupportGeneratorLayerExtruded*> SupportGeneratorLayerExtrudedPtrs;

struct LoopInterfaceProcessor
{
    LoopInterfaceProcessor(coordf_t circle_r) :
        n_contact_loops(0),
        circle_radius(circle_r),
        circle_distance(circle_r * 3.)
    {

        circle.points.reserve(6);
        for (size_t i = 0; i < 6; ++ i) {
            double angle = double(i) * M_PI / 3.;
            circle.points.push_back(Point(circle_radius * cos(angle), circle_radius * sin(angle)));
        }
    }



    void generate(SupportGeneratorLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const;

    int         n_contact_loops;
    coordf_t    circle_radius;
    coordf_t    circle_distance;
    Polygon     circle;
};

void LoopInterfaceProcessor::generate(SupportGeneratorLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const
{
    if (n_contact_loops == 0 || top_contact_layer.empty())
        return;

    Flow flow = interface_flow_src.with_height(top_contact_layer.layer->height);

    Polygons overhang_polygons;
    if (top_contact_layer.layer->overhang_polygons != nullptr)
        overhang_polygons = std::move(*top_contact_layer.layer->overhang_polygons);



    ExPolygons top_contact_expolygons = offset_ex(union_ex(top_contact_layer.layer->polygons), - 0.5f * flow.scaled_width());


    coord_t circle_grid_resolution = 1;
    coord_t circle_grid_powerof2 = 0;
    {

        coord_t circle_grid_resolution_non_powerof2 = coord_t(2. * circle_distance + 3.);
        while (circle_grid_resolution < circle_grid_resolution_non_powerof2) {
            circle_grid_resolution <<= 1;
            ++ circle_grid_powerof2;
        }
    }

    struct PointAccessor {
        const Point* operator()(const Point &pt) const { return &pt; }
    };
    typedef ClosestPointInRadiusLookup<Point, PointAccessor> ClosestPointLookupType;

    Polygons loops0;
    {


        Polygons external_loops;

        Polygons circles;
        Polygons overhang_with_margin = offset(union_ex(overhang_polygons), 0.5f * flow.scaled_width());
        for (ExPolygons::iterator it_contact_expoly = top_contact_expolygons.begin(); it_contact_expoly != top_contact_expolygons.end(); ++ it_contact_expoly) {

            ClosestPointLookupType circle_centers_lookup(coord_t(circle_distance - SCALED_EPSILON));
            Points circle_centers;
            Point  center_last;

            for (size_t i_contour = 0; i_contour <= it_contact_expoly->holes.size(); ++ i_contour) {
                Polygon     &contour = (i_contour == 0) ? it_contact_expoly->contour : it_contact_expoly->holes[i_contour - 1];
                const Point *seg_current_pt = nullptr;
                coordf_t     seg_current_t  = 0.;
                if (! intersection_pl(contour.split_at_first_point(), overhang_with_margin).empty()) {



                    if (circle_centers.empty()) {

                        seg_current_pt = &contour.points.front();
                        seg_current_t  = 0.;
                        center_last    = *seg_current_pt;
                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    for (Points::const_iterator it = contour.points.begin() + 1; it != contour.points.end(); ++it) {

                        const Point &p1 = *(it-1);
                        const Point &p2 = *it;

                        const Vec2d v_seg(coordf_t(p2(0)) - coordf_t(p1(0)), coordf_t(p2(1)) - coordf_t(p1(1)));
                        const Vec2d v_cntr(coordf_t(p1(0) - center_last(0)), coordf_t(p1(1) - center_last(1)));
                        coordf_t a = v_seg.squaredNorm();
                        coordf_t b = 2. * v_seg.dot(v_cntr);
                        coordf_t c = v_cntr.squaredNorm() - circle_distance * circle_distance;
                        coordf_t disc = b * b - 4. * a * c;
                        if (disc > 0.) {

                            coordf_t t1 = (-b - sqrt(disc)) / (2. * a);
                            coordf_t t2 = (-b + sqrt(disc)) / (2. * a);
                            coordf_t t0 = (seg_current_pt == &p1) ? seg_current_t : 0.;

                            coordf_t t;
                            if (t0 <= t1)
                                t = t0;
                            else if (t2 <= 1.)
                                t = t2;
                            else {

                                seg_current_pt = nullptr;
                                continue;
                            }
                            seg_current_pt = &p1;
                            seg_current_t  = t;
                            center_last    = Point(p1(0) + coord_t(v_seg(0) * t), p1(1) + coord_t(v_seg(1) * t));


                            std::pair<const Point*, coordf_t> circle_closest = circle_centers_lookup.find(center_last);
                            if (circle_closest.first != nullptr) {
                                -- it;
                                continue;
                            }
                        } else {

                            seg_current_pt = &p1;
                            seg_current_t  = 0.;
                            center_last    = p1;
                        }

                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    external_loops.push_back(std::move(contour));
                    for (const Point &center : circle_centers) {
                        circles.push_back(circle);
                        circles.back().translate(center);
                    }
                }
            }
        }

        loops0 = diff(external_loops, circles);
    }

    Polylines loop_lines;
    {

        Polygons loop_polygons = loops0;
        for (int i = 1; i < n_contact_loops; ++ i)
            polygons_append(loop_polygons,
                opening(
                    loops0,
                    i * flow.scaled_spacing() + 0.5f * flow.scaled_spacing(),
                    0.5f * flow.scaled_spacing()));



        loop_lines.reserve(loop_polygons.size());
        std::unordered_map<Point, int, PointHash> map_split_points;
        for (Polygons::const_iterator it = loop_polygons.begin(); it != loop_polygons.end(); ++ it) {
            assert(map_split_points.find(it->first_point()) == map_split_points.end());
            map_split_points[it->first_point()] = -1;
            loop_lines.push_back(it->split_at_first_point());
        }
        loop_lines = intersection_pl(loop_lines, expand(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));


        for (int i_line = 0; i_line < int(loop_lines.size()); ++ i_line) {
            Polyline &polyline = loop_lines[i_line];
            auto it = map_split_points.find(polyline.first_point());
            if (it != map_split_points.end()) {


                assert(it->second != -2);
                if (it->second < 0) {

                    it->second = i_line;
                } else {

                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
                continue;
            }
            it = map_split_points.find(polyline.last_point());
            if (it != map_split_points.end()) {


                assert(it->second != -2);
                if (it->second < 0) {

                    it->second = i_line;
                } else {

                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
            }
        }

        remove_degenerate(loop_lines);
    }








    top_contact_layer.set_polygons_to_extrude(diff(top_contact_layer.layer->polygons, offset(loop_lines, float(circle_radius * 1.1))));


    extrusion_entities_append_paths(
        top_contact_layer.extrusions,
        std::move(loop_lines),
        ExtrusionRole::erSupportMaterialInterface, flow.mm3_per_mm(), flow.width(), flow.height());
}

#ifdef SLIC3R_DEBUG
static std::string dbg_index_to_color(int idx)
{
    if (idx < 0)
        return "yellow";
    idx = idx % 3;
    switch (idx) {
        case 0: return "red";
        case 1: return "green";
        default: return "blue";
    }
}
#endif










static void modulate_extrusion_by_overlapping_layers(

    ExtrusionEntitiesPtr                               &extrusions_in_out,
    const SupportGeneratorLayer          &this_layer,

    const SupportGeneratorLayersPtr      &overlapping_layers)
{
    size_t n_overlapping_layers = overlapping_layers.size();
    if (n_overlapping_layers == 0 || extrusions_in_out.empty())

        return;


    ExtrusionPath *extrusion_path_template = dynamic_cast<ExtrusionPath*>(extrusions_in_out.front());
    assert(extrusion_path_template != nullptr);
    ExtrusionRole extrusion_role  = extrusion_path_template->role();
    float         extrusion_width = extrusion_path_template->width;

    struct ExtrusionPathFragment
    {
        ExtrusionPathFragment() : mm3_per_mm(-1), width(-1), height(-1) {};
        ExtrusionPathFragment(double mm3_per_mm, float width, float height) : mm3_per_mm(mm3_per_mm), width(width), height(height) {};

        Polylines       polylines;
        double          mm3_per_mm;
        float           width;
        float           height;
    };



    std::vector<ExtrusionPathFragment> path_fragments(
        n_overlapping_layers + 1,
        ExtrusionPathFragment(extrusion_path_template->mm3_per_mm, extrusion_path_template->width, extrusion_path_template->height));

    extrusion_path_template = nullptr;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun;
    BoundingBox bbox;
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        bbox.merge(get_extents(overlapping_layer.polygons));
    }
    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        assert(path != nullptr);
        bbox.merge(get_extents(path->polyline));
    }
    SVG svg(debug_out_path("support-fragments-%d-%lf.svg", iRun, this_layer.print_z).c_str(), bbox);
    const float transparency = 0.5f;

    svg.draw(union_ex(this_layer.polygons), dbg_index_to_color(-1), transparency);
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(union_ex(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), transparency);
    }

    svg.draw(to_polylines(this_layer.polygons), dbg_index_to_color(-1), scale_(0.2));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(to_polylines(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), scale_(0.1));
    }

    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        std::string color_name;
        switch ((it - extrusions_in_out.begin()) % 9) {
            case 0: color_name = "magenta"; break;
            case 1: color_name = "deepskyblue"; break;
            case 2: color_name = "coral"; break;
            case 3: color_name = "goldenrod"; break;
            case 4: color_name = "orange"; break;
            case 5: color_name = "olivedrab"; break;
            case 6: color_name = "blueviolet"; break;
            case 7: color_name = "brown"; break;
            default: color_name = "orchid"; break;
        }
        svg.draw(path->polyline, color_name, scale_(0.2));
    }
#endif


    std::vector<std::pair<Point, Point>> path_ends;

    {
        Polylines &polylines = path_fragments.back().polylines;
        for (ExtrusionEntity *ee : extrusions_in_out) {
            ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(ee);
            assert(path != nullptr);
            polylines.emplace_back(path->polyline.to_polyline());
            path_ends.emplace_back(std::pair<Point, Point>(polylines.back().points.front(), polylines.back().points.back()));
            delete path;
        }
    }


    extrusions_in_out.clear();



    for (int i_overlapping_layer = int(n_overlapping_layers) - 1; i_overlapping_layer >= 0; -- i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        ExtrusionPathFragment &frag = path_fragments[i_overlapping_layer];
        Polygons polygons_trimming = offset(union_ex(overlapping_layer.polygons), float(scale_(0.5*extrusion_width)));
        frag.polylines = intersection_pl(path_fragments.back().polylines, polygons_trimming);
        path_fragments.back().polylines = diff_pl(path_fragments.back().polylines, polygons_trimming);

        assert(this_layer.print_z > overlapping_layer.print_z);
        frag.height = float(this_layer.print_z - overlapping_layer.print_z);
        frag.mm3_per_mm = Flow(frag.width, frag.height, -1.f).mm3_per_mm();
#ifdef SLIC3R_DEBUG
        svg.draw(frag.polylines, dbg_index_to_color(i_overlapping_layer), scale_(0.1));
#endif
    }

#ifdef SLIC3R_DEBUG
    svg.draw(path_fragments.back().polylines, dbg_index_to_color(-1), scale_(0.1));
    svg.Close();
#endif






    struct ExtrusionPathFragmentEnd
    {
        ExtrusionPathFragmentEnd(size_t alayer_idx, size_t apolyline_idx, bool ais_start) :
            layer_idx(alayer_idx), polyline_idx(apolyline_idx), is_start(ais_start) {}
        size_t layer_idx;
        size_t polyline_idx;
        bool   is_start;
    };
    class ExtrusionPathFragmentEndPointAccessor {
    public:
        ExtrusionPathFragmentEndPointAccessor(const std::vector<ExtrusionPathFragment> &path_fragments) : m_path_fragments(path_fragments) {}

        const Point* operator()(const ExtrusionPathFragmentEnd &fragment_end) const {
            const Polyline &polyline = m_path_fragments[fragment_end.layer_idx].polylines[fragment_end.polyline_idx];
            return polyline.points.empty() ? nullptr :
                (fragment_end.is_start ? &polyline.points.front() : &polyline.points.back());
        }
    private:
        const std::vector<ExtrusionPathFragment> &m_path_fragments;
    };
    const coord_t search_radius = 7;
    ClosestPointInRadiusLookup<ExtrusionPathFragmentEnd, ExtrusionPathFragmentEndPointAccessor> map_fragment_starts(
        search_radius, ExtrusionPathFragmentEndPointAccessor(path_fragments));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer <= n_overlapping_layers; ++ i_overlapping_layer) {
        const Polylines &polylines = path_fragments[i_overlapping_layer].polylines;
        for (size_t i_polyline = 0; i_polyline < polylines.size(); ++ i_polyline) {

            if (polylines[i_polyline].points.size() >= 2) {
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, true));
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, false));
            }
        }
    }


    for (size_t i_path = 0; i_path < path_ends.size(); ++ i_path) {
        const Point &pt_start = path_ends[i_path].first;
        const Point &pt_end   = path_ends[i_path].second;
        Point pt_current = pt_start;

        ExtrusionMultiPath multipath;
        for (;;) {

            std::pair<const ExtrusionPathFragmentEnd*, coordf_t> end_and_dist2 = map_fragment_starts.find(pt_current);


            assert(end_and_dist2.first == nullptr || end_and_dist2.second < search_radius * search_radius);
            if (end_and_dist2.first == nullptr) {





                break;
            }
            const ExtrusionPathFragmentEnd &fragment_end_min = *end_and_dist2.first;

            ExtrusionPathFragment &frag = path_fragments[fragment_end_min.layer_idx];
            Polyline              &frag_polyline = frag.polylines[fragment_end_min.polyline_idx];

            ExtrusionPath         *path = multipath.paths.empty() ? nullptr : &multipath.paths.back();
            if (path != nullptr) {

                assert(this_layer.layer_type == SupporLayerType::BottomContact || path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm);
                if (path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm) {
                    path = nullptr;
                }

            }
            if (path == nullptr) {

                multipath.paths.push_back(ExtrusionPath(extrusion_role, frag.mm3_per_mm, frag.width, frag.height));
                path = &multipath.paths.back();
            }


            if (! fragment_end_min.is_start)
                frag_polyline.reverse();

            assert(frag_polyline.points.front() == pt_current);
            frag_polyline.points.front() = pt_current;

            if (! path->polyline.points.empty())
                path->polyline.points.pop_back();

            path->polyline.append(Polyline3(std::move(frag_polyline)));
            frag_polyline.points.clear();
            const Point3 &pt_back3 = path->polyline.points.back();
            pt_current = Point(pt_back3.x(), pt_back3.y());
            if (pt_current == pt_end) {

                break;
            }
        }
        if (!multipath.paths.empty()) {
            if (multipath.paths.size() == 1) {

                extrusions_in_out.push_back(new ExtrusionPath(std::move(multipath.paths.front())));
            } else {


                extrusions_in_out.push_back(new ExtrusionMultiPath(std::move(multipath)));
            }
        }
    }


    for (auto it_fragment = path_fragments.begin(); it_fragment != path_fragments.end(); ++ it_fragment)
        extrusion_entities_append_paths(extrusions_in_out, std::move(it_fragment->polylines), extrusion_role, it_fragment->mm3_per_mm, it_fragment->width, it_fragment->height);
}


static constexpr const std::initializer_list<SupporLayerType> support_types_interface{
    SupporLayerType::RaftInterface, SupporLayerType::BottomContact, SupporLayerType::BottomInterface, SupporLayerType::TopContact, SupporLayerType::TopInterface
};

SupportGeneratorLayersPtr generate_support_layers(
    PrintObject                         &object,
    const SupportGeneratorLayersPtr     &raft_layers,
    const SupportGeneratorLayersPtr     &bottom_contacts,
    const SupportGeneratorLayersPtr     &top_contacts,
    const SupportGeneratorLayersPtr     &intermediate_layers,
    const SupportGeneratorLayersPtr     &interface_layers,
    const SupportGeneratorLayersPtr     &base_interface_layers)
{


    SupportGeneratorLayersPtr layers_sorted;
    layers_sorted.reserve(raft_layers.size() + bottom_contacts.size() + top_contacts.size() + intermediate_layers.size() + interface_layers.size() + base_interface_layers.size());
    append(layers_sorted, raft_layers);
    append(layers_sorted, bottom_contacts);
    append(layers_sorted, top_contacts);
    append(layers_sorted, intermediate_layers);
    append(layers_sorted, interface_layers);
    append(layers_sorted, base_interface_layers);

    std::sort(layers_sorted.begin(), layers_sorted.end());
    layers_sorted.erase(std::unique(layers_sorted.begin(), layers_sorted.end()), layers_sorted.end());


    std::sort(layers_sorted.begin(), layers_sorted.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
    int layer_id = 0;
    int layer_id_interface = 0;
    assert(object.support_layers().empty());
    for (size_t i = 0; i < layers_sorted.size();) {


        size_t j = i + 1;
        coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
        for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j) ;

        coordf_t zavg = 0.5 * (layers_sorted[i]->print_z + layers_sorted[j - 1]->print_z);
        coordf_t height_min = layers_sorted[i]->height;
        bool     empty = true;

        size_t   num_interfaces = 0;
        size_t   num_top_contacts = 0;
        double   top_contact_bottom_z = 0;
        for (size_t u = i; u < j; ++u) {
            SupportGeneratorLayer &layer = *layers_sorted[u];
            if (! layer.polygons.empty()) {
                empty             = false;
                const bool is_base_interface = std::find(base_interface_layers.begin(), base_interface_layers.end(), &layer) != base_interface_layers.end();
                num_interfaces += one_of(layer.layer_type, support_types_interface) || is_base_interface;
                if (layer.layer_type == SupporLayerType::TopContact) {
                    ++ num_top_contacts;
                    assert(num_top_contacts <= 1);


                    top_contact_bottom_z = layer.bottom_z;
                }
            }
            layer.print_z = zavg;
            height_min = std::min(height_min, layer.height);
        }
        if (! empty) {


            bool   this_layer_contacts_only = num_top_contacts > 0 && num_top_contacts == num_interfaces;
            size_t this_layer_id_interface  = layer_id_interface;
            if (this_layer_contacts_only) {

                for (auto it = object.support_layers().rbegin(); it != object.support_layers().rend(); ++ it)
                    if (const SupportLayer &other_layer = **it; std::abs(other_layer.print_z - top_contact_bottom_z) < EPSILON) {


                        this_layer_id_interface = other_layer.interface_id() + 1;
                    }
            }
            object.add_support_layer(layer_id ++, this_layer_id_interface, height_min, zavg);
            if (num_interfaces && ! this_layer_contacts_only)
                ++ layer_id_interface;
        }
        i = j;
    }
    return layers_sorted;
}

void generate_support_toolpaths(
    SupportLayerPtrs                    &support_layers,
    const PrintObjectConfig             &config,
    const SupportParameters             &support_params,
    const SlicingParameters             &slicing_params,
    const SupportGeneratorLayersPtr     &raft_layers,
    const SupportGeneratorLayersPtr     &bottom_contacts,
    const SupportGeneratorLayersPtr     &top_contacts,
    const SupportGeneratorLayersPtr     &intermediate_layers,
    const SupportGeneratorLayersPtr     &interface_layers,
    const SupportGeneratorLayersPtr     &base_interface_layers)
{

    LoopInterfaceProcessor loop_interface_processor(1.5 * support_params.support_material_interface_flow.scaled_width());
    loop_interface_processor.n_contact_loops = config.support_interface_loop_pattern.value ? 1 : 0;

    std::vector<float>      angles { support_params.base_angle };
    if (config.support_base_pattern == smpRectilinearGrid)
        angles.push_back(support_params.interface_angle);

    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));


    const coordf_t link_max_length_factor = 0.;


    auto n_raft_layers = std::min<size_t>(support_layers.size(), std::max(0, int(slicing_params.raft_layers()) - 1));

    tbb::parallel_for(tbb::blocked_range<size_t>(0, n_raft_layers),
        [&support_layers, &raft_layers, &intermediate_layers, &config, &support_params, &slicing_params,
            &bbox_object, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            assert(support_layer_id < raft_layers.size());
            SupportLayer               &support_layer = *support_layers[support_layer_id];
            assert(support_layer.support_fills.entities.empty());
            SupportGeneratorLayer      &raft_layer    = *raft_layers[support_layer_id];

            std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(support_params.raft_interface_fill_pattern));
            std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(support_params.base_fill_pattern));
            filler_interface->set_bounding_box(bbox_object);
            filler_support->set_bounding_box(bbox_object);




            const Polygons &tree_polygons =
                support_layer_id > 0 && support_layer_id < intermediate_layers.size() && is_approx(intermediate_layers[support_layer_id]->print_z, support_layer.print_z) ?
                intermediate_layers[support_layer_id]->polygons : Polygons();


            if (support_layer_id > 0) {
                const Polygons &to_infill_polygons = (support_layer_id < slicing_params.base_raft_layers) ?
                    raft_layer.polygons :

                    ((raft_layer.contact_polygons == nullptr) ? Polygons() : *raft_layer.contact_polygons);

                Flow flow(float(support_params.support_material_flow.width()), float(raft_layer.height), support_params.support_material_flow.nozzle_diameter());
                assert(!raft_layer.bridging);
                if (! to_infill_polygons.empty()) {
                    Fill *filler = filler_support.get();
                    filler->angle = support_params.raft_angle_base;
                    filler->spacing = support_params.support_material_flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_params.support_density));
                    fill_expolygons_with_sheath_generate_paths(

                        support_layer.support_fills.entities,

                        tree_polygons.empty() ? to_infill_polygons : diff(to_infill_polygons, tree_polygons),

                        filler, float(support_params.support_density),

                        ExtrusionRole::erSupportMaterial, flow,
                        support_params, support_params.with_sheath, false);
                }
                if (! tree_polygons.empty())
                    tree_supports_generate_paths(support_layer.support_fills.entities, tree_polygons, flow, support_params);
            }

            Fill *filler = filler_interface.get();
            Flow  flow = support_params.first_layer_flow;
            float density = 0.f;
            if (support_layer_id == 0) {

                filler->angle = support_params.raft_angle_1st_layer;
                filler->spacing = support_params.first_layer_flow.spacing();
                density       = float(config.raft_first_layer_density.value * 0.01);
            } else if (support_layer_id >= slicing_params.base_raft_layers) {
                filler->angle = support_params.raft_interface_angle(support_layer.interface_id());


                filler->spacing = support_params.support_material_flow.spacing();
                assert(! raft_layer.bridging);
                flow          = Flow(float(support_params.raft_interface_flow.width()), float(raft_layer.height), support_params.raft_interface_flow.nozzle_diameter());
                density       = float(support_params.raft_interface_density);
            } else
                continue;
            filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
            fill_expolygons_with_sheath_generate_paths(

                support_layer.support_fills.entities,

                tree_polygons.empty() ? raft_layer.polygons : diff(raft_layer.polygons, tree_polygons),

                filler, density,

                (support_layer_id < slicing_params.base_raft_layers) ? ExtrusionRole::erSupportMaterial : ExtrusionRole::erSupportMaterialInterface, flow,

                support_params, support_layer_id == 0, support_layer_id == 0);
        }
    });

    struct LayerCacheItem {
        LayerCacheItem(SupportGeneratorLayerExtruded *layer_extruded = nullptr) : layer_extruded(layer_extruded) {}
        SupportGeneratorLayerExtruded         *layer_extruded;
        std::vector<SupportGeneratorLayer*>    overlapping;
    };
    struct LayerCache {
        SupportGeneratorLayerExtruded                                     bottom_contact_layer;
        SupportGeneratorLayerExtruded                                     top_contact_layer;
        SupportGeneratorLayerExtruded                                     base_layer;
        SupportGeneratorLayerExtruded                                     interface_layer;
        SupportGeneratorLayerExtruded                                     base_interface_layer;
        boost::container::static_vector<LayerCacheItem, 5>  nonempty;

        float    ironing_angle;
        Polygons polys_to_iron;

        void add_nonempty_and_sort() {
            for (SupportGeneratorLayerExtruded *item : { &bottom_contact_layer, &top_contact_layer, &interface_layer, &base_interface_layer, &base_layer })
                if (! item->empty())
                    this->nonempty.emplace_back(item);

            std::stable_sort(this->nonempty.begin(), this->nonempty.end(), [](const LayerCacheItem &lc1, const LayerCacheItem &lc2) { return lc1.layer_extruded->layer->height > lc2.layer_extruded->layer->height; });
        }
    };
    std::vector<LayerCache>             layer_caches(support_layers.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [&config, &slicing_params, &support_params, &support_layers, &bottom_contacts, &top_contacts, &intermediate_layers, &interface_layers, &base_interface_layers, &layer_caches, &loop_interface_processor,
            &bbox_object, &angles, n_raft_layers, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {

        size_t idx_layer_bottom_contact   = size_t(-1);
        size_t idx_layer_top_contact      = size_t(-1);
        size_t idx_layer_intermediate     = size_t(-1);
        size_t idx_layer_interface        = size_t(-1);
        size_t idx_layer_base_interface   = size_t(-1);
        const auto fill_type_first_layer  = ipRectilinear;
        auto filler_interface       = std::unique_ptr<Fill>(Fill::new_from_type(support_params.contact_fill_pattern));

        auto filler_first_layer_ptr = std::unique_ptr<Fill>(range.begin() == 0 && support_params.contact_fill_pattern != fill_type_first_layer ? Fill::new_from_type(fill_type_first_layer) : nullptr);

        auto filler_first_layer     = filler_first_layer_ptr ? filler_first_layer_ptr.get() : filler_interface.get();

        const bool top_interfaces_enabled    = support_params.num_top_interface_layers > 0;
        const bool bottom_interfaces_enabled = support_params.num_bottom_interface_layers > 0;
        const coordf_t base_interface_density = top_interfaces_enabled || !bottom_interfaces_enabled ?
            support_params.top_interface_density : support_params.bottom_interface_density;
        auto filler_raft_contact_ptr = std::unique_ptr<Fill>(range.begin() == n_raft_layers && !top_interfaces_enabled ?
            Fill::new_from_type(support_params.raft_interface_fill_pattern) : nullptr);

        auto filler_raft_contact     = filler_raft_contact_ptr ? filler_raft_contact_ptr.get() : filler_interface.get();

        auto filler_base_interface  = std::unique_ptr<Fill>(base_interface_layers.empty() ? nullptr :
            Fill::new_from_type(base_interface_density > 0.95 || support_params.with_sheath ? ipRectilinear : ipSupportBase));
        auto filler_support         = std::unique_ptr<Fill>(Fill::new_from_type(support_params.base_fill_pattern));
        filler_interface->set_bounding_box(bbox_object);
        if (filler_first_layer_ptr)
            filler_first_layer_ptr->set_bounding_box(bbox_object);
        if (filler_raft_contact_ptr)
            filler_raft_contact_ptr->set_bounding_box(bbox_object);
        if (filler_base_interface)
            filler_base_interface->set_bounding_box(bbox_object);
        filler_support->set_bounding_box(bbox_object);
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            const float   support_interface_angle = support_params.support_interface_angle(support_layer.interface_id());


            SupportGeneratorLayerExtruded &bottom_contact_layer = layer_cache.bottom_contact_layer;
            SupportGeneratorLayerExtruded &top_contact_layer    = layer_cache.top_contact_layer;
            SupportGeneratorLayerExtruded &base_layer           = layer_cache.base_layer;
            SupportGeneratorLayerExtruded &interface_layer      = layer_cache.interface_layer;
            SupportGeneratorLayerExtruded &base_interface_layer = layer_cache.base_interface_layer;

            {
                auto fun = [&support_layer](const SupportGeneratorLayer *l){ return l->print_z >= support_layer.print_z - EPSILON; };
                idx_layer_bottom_contact  = idx_higher_or_equal(bottom_contacts,     idx_layer_bottom_contact,  fun);
                idx_layer_top_contact     = idx_higher_or_equal(top_contacts,        idx_layer_top_contact,     fun);
                idx_layer_intermediate    = idx_higher_or_equal(intermediate_layers, idx_layer_intermediate,    fun);
                idx_layer_interface       = idx_higher_or_equal(interface_layers,    idx_layer_interface,       fun);
                idx_layer_base_interface  = idx_higher_or_equal(base_interface_layers, idx_layer_base_interface,fun);
            }

            if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact]->print_z < support_layer.print_z + EPSILON)
                bottom_contact_layer.layer = bottom_contacts[idx_layer_bottom_contact];
            if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact]->print_z < support_layer.print_z + EPSILON)
                top_contact_layer.layer = top_contacts[idx_layer_top_contact];
            if (idx_layer_interface < interface_layers.size() && interface_layers[idx_layer_interface]->print_z < support_layer.print_z + EPSILON)
                interface_layer.layer = interface_layers[idx_layer_interface];
            if (idx_layer_base_interface < base_interface_layers.size() && base_interface_layers[idx_layer_base_interface]->print_z < support_layer.print_z + EPSILON)
                base_interface_layer.layer = base_interface_layers[idx_layer_base_interface];
            if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate]->print_z < support_layer.print_z + EPSILON)
                base_layer.layer = intermediate_layers[idx_layer_intermediate];


            bool raft_layer = slicing_params.interface_raft_layers && top_contact_layer.layer && is_approx(top_contact_layer.layer->print_z, slicing_params.raft_contact_top_z);

            const bool organic_tree = support_params.support_style == SupportMaterialStyle::smsTreeOrganic;
            const bool top_interfaces = support_params.num_top_interface_layers > 0;
            const bool bottom_interfaces = support_params.num_bottom_interface_layers > 0;
            if (!top_interfaces) {


                if (support_params.can_merge_support_regions && ! raft_layer) {
                    if (base_layer.could_merge(top_contact_layer))
                        base_layer.merge(std::move(top_contact_layer));
                    else if (base_layer.empty())
                        base_layer = std::move(top_contact_layer);
                }
            } else {
                if (support_params.ironing && !top_contact_layer.empty()) {

                    layer_cache.ironing_angle = support_interface_angle;
                    layer_cache.polys_to_iron = top_contact_layer.polygons_to_extrude();
                }

                loop_interface_processor.generate(top_contact_layer, support_params.support_material_interface_flow);



                if (top_contact_layer.could_merge(interface_layer) && ! raft_layer)
                    top_contact_layer.merge(std::move(interface_layer));
            }


            if (!organic_tree) {
                if (!bottom_interfaces && support_params.can_merge_support_regions) {
                    if (base_layer.could_merge(bottom_contact_layer))
                        base_layer.merge(std::move(bottom_contact_layer));
                    else if (base_layer.empty() && ! bottom_contact_layer.empty() && ! bottom_contact_layer.layer->bridging)
                        base_layer = std::move(bottom_contact_layer);
                } else if (bottom_contact_layer.could_merge(top_contact_layer) && ! raft_layer) {
                    if (top_interfaces && bottom_interfaces) {
                        top_contact_layer.merge(std::move(bottom_contact_layer));
                    } else if (bottom_interfaces) {
                        top_contact_layer.set_polygons_to_extrude(
                            diff(top_contact_layer.polygons_to_extrude(), bottom_contact_layer.polygons_to_extrude()));
                    } else {
                        bottom_contact_layer.set_polygons_to_extrude(
                            diff(bottom_contact_layer.polygons_to_extrude(), top_contact_layer.polygons_to_extrude()));
                    }
                } else if (bottom_contact_layer.could_merge(interface_layer)) {
                    const bool interface_layer_is_bottom = interface_layer.layer->layer_type == SupporLayerType::BottomInterface;
                    if (bottom_interfaces && interface_layer_is_bottom) {
                        bottom_contact_layer.merge(std::move(interface_layer));
                    } else {
                        bottom_contact_layer.set_polygons_to_extrude(
                            diff(bottom_contact_layer.polygons_to_extrude(), interface_layer.polygons_to_extrude()));
                    }
                }
            }





            if (organic_tree && support_params.zero_gap_interface_top && !support_params.can_merge_support_regions &&
                (!base_layer.empty() || !base_interface_layer.empty())) {
                Polygons interface_polygons;
                if (!top_contact_layer.empty())
                    polygons_append(interface_polygons, top_contact_layer.polygons_to_extrude());
                if (!interface_layer.empty())
                    polygons_append(interface_polygons, interface_layer.polygons_to_extrude());
                if (!interface_polygons.empty()) {
                    const coord_t trim_margin = std::max(
                        support_params.support_material_flow.scaled_width(),
                        support_params.support_material_interface_flow.scaled_width());
                    Polygons interface_keepout = offset(interface_polygons, trim_margin);
                    if (!base_layer.empty())
                        base_layer.set_polygons_to_extrude(diff(base_layer.polygons_to_extrude(), interface_keepout));
                    if (!base_interface_layer.empty())
                        base_interface_layer.set_polygons_to_extrude(diff(base_interface_layer.polygons_to_extrude(), interface_keepout));
                }
            }

#if 0
            if ( ! interface_layer.empty() && ! base_layer.empty()) {




                Polygons islands = top_level_islands(interface_layer.layer->polygons);
                polygons_append(interface_layer.layer->polygons, intersection(base_layer.layer->polygons, islands));
                base_layer.layer->polygons = diff(base_layer.layer->polygons, islands);
            }
#endif


            enum class InterfaceLayerType { TopContact, BottomContact, RaftContact, Interface, InterfaceAsBase };
            auto extrude_interface = [&](SupportGeneratorLayerExtruded &layer_ex, InterfaceLayerType interface_layer_type) {
                if (! layer_ex.empty() && ! layer_ex.polygons_to_extrude().empty()) {
                    bool interface_as_base = interface_layer_type == InterfaceLayerType::InterfaceAsBase;
                    bool raft_contact      = interface_layer_type == InterfaceLayerType::RaftContact;

                    bool bottom_interface  = interface_layer_type == InterfaceLayerType::BottomContact ||
                        (interface_layer_type == InterfaceLayerType::Interface && layer_ex.layer->layer_type == SupporLayerType::BottomInterface);


                    auto *filler = raft_contact ? filler_raft_contact : filler_interface.get();
                    auto interface_flow = layer_ex.layer->bridging ?
                        Flow::bridging_flow(layer_ex.layer->height, support_params.support_material_bottom_interface_flow.nozzle_diameter()) :
                        (raft_contact ? &support_params.raft_interface_flow :
                         interface_as_base ? &support_params.support_material_flow : &support_params.support_material_interface_flow)
                            ->with_height(float(layer_ex.layer->height));
                    filler->angle = interface_as_base ?

                            angles[support_layer_id % angles.size()] :

                            raft_contact ?
                                support_params.raft_interface_angle(support_layer.interface_id()) :
                                support_interface_angle;

                    double density = raft_contact ? support_params.raft_interface_density :
                        interface_as_base ? support_params.support_density :
                        bottom_interface ? support_params.bottom_interface_density : support_params.top_interface_density;
                    filler->spacing = raft_contact ? support_params.raft_interface_flow.spacing() :
                        interface_as_base ? support_params.support_material_flow.spacing() : support_params.support_material_interface_flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                    fill_expolygons_generate_paths(

                        layer_ex.extrusions,

                        union_safety_offset_ex(layer_ex.polygons_to_extrude()),

                        filler, float(density),

                        interface_as_base ? ExtrusionRole::erSupportMaterial : ExtrusionRole::erSupportMaterialInterface, interface_flow);
                }
            };
            extrude_interface(top_contact_layer,    raft_layer ? InterfaceLayerType::RaftContact : top_interfaces ? InterfaceLayerType::TopContact : InterfaceLayerType::InterfaceAsBase);
            if (!organic_tree)
                extrude_interface(bottom_contact_layer, bottom_interfaces ? InterfaceLayerType::BottomContact : InterfaceLayerType::InterfaceAsBase);
            const bool interface_layer_enabled = !interface_layer.empty() &&
                (interface_layer.layer->layer_type == SupporLayerType::BottomInterface ? bottom_interfaces : top_interfaces);
            extrude_interface(interface_layer,      interface_layer_enabled ? InterfaceLayerType::Interface : InterfaceLayerType::InterfaceAsBase);

            if ( ! base_interface_layer.empty() && ! base_interface_layer.polygons_to_extrude().empty()) {
                Fill *filler = filler_base_interface.get();


                assert(! base_interface_layer.layer->bridging);
                Flow interface_flow = support_params.support_material_flow.with_height(float(base_interface_layer.layer->height));
                filler->angle   = support_interface_angle;
                filler->spacing = support_params.support_material_interface_flow.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / base_interface_density));
                fill_expolygons_generate_paths(

                    base_interface_layer.extrusions,


                    union_safety_offset_ex(base_interface_layer.polygons_to_extrude()),

                    filler, float(base_interface_density),

                    ExtrusionRole::erSupportMaterial, interface_flow);
            }


            if (! base_layer.empty() && ! base_layer.polygons_to_extrude().empty()) {
                Fill             *filler          = filler_support.get();
                filler->angle = angles[support_layer_id % angles.size()];


                assert(! base_layer.layer->bridging);
                auto flow = support_params.support_material_flow.with_height(float(base_layer.layer->height));
                filler->spacing = support_params.support_material_flow.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_params.support_density));
                float density = float(support_params.support_density);
                bool  sheath  = support_params.with_sheath;
                bool  no_sort = false;
                bool  done    = false;






                const bool is_belt_printer = std::abs(slicing_params.belt_floor_shear_factor) > EPSILON;
                if (! is_belt_printer && base_layer.layer->bottom_z < EPSILON) {

                    filler = filler_first_layer;
                    filler->angle = Geometry::deg2rad(float(config.support_angle.value + 90.));
                    density = float(config.raft_first_layer_density.value * 0.01);
                    flow = support_params.first_layer_flow;



                    filler->spacing = flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                    sheath  = true;
                    no_sort = true;
                } else if (support_params.support_style == SupportMaterialStyle::smsTreeOrganic &&
                           (config.support_base_pattern == smpNone || config.support_base_pattern == smpDefault)) {


                    SupportParameters support_params2 = support_params;
                    if (support_layer.print_z > 100.0)
                        support_params2.tree_branch_diameter_double_wall_area_scaled = 0.1;
                    tree_supports_generate_paths(base_layer.extrusions, base_layer.polygons_to_extrude(), flow, support_params2);
                    done = true;
                }
                if (! done)
                    fill_expolygons_with_sheath_generate_paths(

                        base_layer.extrusions,

                        base_layer.polygons_to_extrude(),

                        filler, density,

                        ExtrusionRole::erSupportMaterial, flow,
                        support_params, sheath, no_sort);
            }


            if (! base_layer.empty() && ! base_interface_layer.empty() && ! base_layer.polygons_to_extrude().empty() && ! base_interface_layer.polygons_to_extrude().empty() &&
                base_layer.could_merge(base_interface_layer))
                base_layer.merge(std::move(base_interface_layer));

            layer_cache.add_nonempty_and_sort();



            Polygons polys;

            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {

                layer_cache_item.layer_extruded->polygons_append(polys);









                layer_cache_item.overlapping.reserve(20);
                coordf_t bottom_z = layer_cache_item.layer_extruded->layer->bottom_print_z() + EPSILON;
                auto add_overlapping = [&layer_cache_item, bottom_z](const SupportGeneratorLayersPtr &layers, size_t idx_top) {
                    for (int i = int(idx_top) - 1; i >= 0 && layers[i]->print_z > bottom_z; -- i)
                        layer_cache_item.overlapping.push_back(layers[i]);
                };
                add_overlapping(top_contacts, idx_layer_top_contact);
                if (layer_cache_item.layer_extruded->layer->layer_type == SupporLayerType::BottomContact) {

                    add_overlapping(intermediate_layers,   idx_layer_intermediate);
                    add_overlapping(interface_layers,      idx_layer_interface);
                    add_overlapping(base_interface_layers, idx_layer_base_interface);
                }

                std::stable_sort(layer_cache_item.overlapping.begin(), layer_cache_item.overlapping.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
            }
            assert(support_layer.support_islands.empty());
            if (! polys.empty()) {
                support_layer.support_islands = union_ex(polys);



            }
        }
    });


    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [&support_layers, &layer_caches, &support_params, &bbox_object]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id) {
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];

            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {

                modulate_extrusion_by_overlapping_layers(layer_cache_item.layer_extruded->extrusions, *layer_cache_item.layer_extruded->layer, layer_cache_item.overlapping);
                support_layer.support_fills.append(std::move(layer_cache_item.layer_extruded->extrusions));
            }


            if (!layer_cache.polys_to_iron.empty()) {
                auto f = std::unique_ptr<Fill>(Fill::new_from_type(support_params.ironing_pattern));
                f->set_bounding_box(bbox_object);
                f->layer_id        = support_layer.id();
                f->z               = support_layer.print_z;
                f->overlap         = 0;
                f->angle           = layer_cache.ironing_angle;
                f->spacing         = support_params.ironing_spacing;
                f->link_max_length = (coord_t) scale_(3. * f->spacing);

                ExPolygons polys_to_iron = union_safety_offset_ex(layer_cache.polys_to_iron);
                layer_cache.polys_to_iron.clear();


                if (support_layer_id < support_layers.size() - 1) {
                    const auto& upper_layer = support_layers[support_layer_id + 1];
                    if (!upper_layer->support_islands.empty() && upper_layer->bottom_z() <= support_layer.print_z + EPSILON) {
                        polys_to_iron = diff_ex(polys_to_iron, upper_layer->support_islands);
                    }
                }

                fill_expolygons_generate_paths(

                    support_layer.support_fills.entities,

                    std::move(polys_to_iron),

                    f.get(), 1.f,

                    ExtrusionRole::erIroning, support_params.ironing_flow);
            }
        }
    });

#ifndef NDEBUG
    struct Test {
        static bool verify_nonempty(const ExtrusionEntityCollection *collection) {
            for (const ExtrusionEntity *ee : collection->entities) {
                if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee))
                    assert(! path->empty());
                else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(ee))
                    assert(! multipath->empty());
                else if (const ExtrusionEntityCollection *eecol = dynamic_cast<const ExtrusionEntityCollection*>(ee)) {
                    assert(! eecol->empty());
                    return verify_nonempty(eecol);
                } else
                    assert(false);
            }
            return true;
        }
    };
    for (const SupportLayer *support_layer : support_layers)
        assert(Test::verify_nonempty(&support_layer->support_fills));
#endif
}







































































































Vec2d build_plate_tilt_slope(const PrintConfig &print_config)
{
    auto slope = [](double tilt_deg) { return std::tan(Geometry::deg2rad(std::clamp(tilt_deg, -89., 89.))); };
    return { slope(print_config.build_plate_tilt_y.value), slope(print_config.build_plate_tilt_x.value) };
}

}
