#include <limits>
#include "BeltBrim.hpp"

#include "ClipperUtils.hpp"
#include "Flow.hpp"
#include "Layer.hpp"
#include "Polygon.hpp"
#include "Print.hpp"
#include "ShortestPath.hpp"
#include "Support/BeltFloorContext.hpp"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Point.hpp"
#include "Polyline.hpp"
#include "PrintConfig.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace Slic3r {



static inline Point scale_u_point(const Point &p, int from_axis, double factor)
{


    return from_axis == 0 ?
        Point(coord_t(std::llround(double(p.x()) * factor)), p.y()) :
        Point(p.x(), coord_t(std::llround(double(p.y()) * factor)));
}

static inline void scale_u_polygon(Polygon &poly, int from_axis, double factor)
{
    for (Point &p : poly.points)
        p = scale_u_point(p, from_axis, factor);
}

ExPolygons belt_scale_u(const ExPolygons &src, const BeltBrimFrame &frame, double factor)
{
    ExPolygons out = src;
    for (ExPolygon &ex : out) {
        scale_u_polygon(ex.contour, frame.from_axis, factor);
        for (Polygon &hole : ex.holes)
            scale_u_polygon(hole, frame.from_axis, factor);
    }
    return out;
}

Polylines belt_scale_u(const Polylines &src, const BeltBrimFrame &frame, double factor)
{
    Polylines out = src;
    for (Polyline &pl : out)
        for (Point &p : pl.points)
            p = scale_u_point(p, frame.from_axis, factor);
    return out;
}



ExPolygons sweep_ex(const ExPolygons &src, const Point &t)
{
    if (src.empty())
        return {};
    if (t == Point(0, 0))
        return src;







    Polygons quads;
    for (const ExPolygon &ex : src)
        for (size_t c = 0; c < ex.num_contours(); ++ c)
            for (const Line &e : ex.contour_or_hole(c).lines()) {
                if (e.a == e.b)
                    continue;
                Polygon q;
                q.points = { e.a, e.b, e.b + t, e.a + t };



                if (q.is_clockwise())
                    q.reverse();
                quads.emplace_back(std::move(q));
            }

    ExPolygons shifted = src;
    for (ExPolygon &ex : shifted)
        ex.translate(t);



    return union_ex(union_ex(src, shifted), quads);
}



ExPolygons belt_brim_region(const ExPolygons    &footprint_flat,
                            bool                 has_outer,
                            bool                 has_inner,
                            coord_t              brim_width,
                            coord_t              object_gap,
                            coord_t              leading,
                            coord_t              lateral,
                            const BeltBrimFrame &frame)
{
    if (footprint_flat.empty() || (! has_outer && ! has_inner))
        return {};

    ExPolygons out;

    if (has_outer) {


        Polygons contours;
        contours.reserve(footprint_flat.size());
        for (const ExPolygon &ex : footprint_flat)
            contours.emplace_back(ex.contour);



        ExPolygons inner = offset_ex(contours, float(object_gap), jtRound, SCALED_RESOLUTION);







        ExPolygons envelope = brim_width > 0 ? closing_ex(inner, float(brim_width)) : inner;

        ExPolygons base  = envelope;
        if (leading > 0) {


            const Point t = frame.from_axis == 0 ?
                Point(frame.downhill_sign() * leading, 0) :
                Point(0, frame.downhill_sign() * leading);
            base = union_ex(base, sweep_ex(envelope, t));
        }
        if (lateral > 0) {



            const Point t = frame.from_axis == 0 ? Point(0, lateral) : Point(lateral, 0);
            ExPolygons widened = union_ex(sweep_ex(base, t), sweep_ex(base, Point(-t.x(), -t.y())));
            base = union_ex(base, to_polygons(widened));
        }
        ExPolygons outer = offset_ex(base, float(brim_width), jtRound, SCALED_RESOLUTION);
        expolygons_append(out, diff_ex(outer, envelope));
    }

    if (has_inner) {


        Polygons holes;
        for (const ExPolygon &ex : footprint_flat)
            polygons_append(holes, ex.holes);
        polygons_reverse(holes);
        if (! holes.empty()) {
            ExPolygons hole_inner = offset_ex(holes, - float(brim_width + object_gap));
            ExPolygons hole_outer = offset_ex(holes, - float(object_gap));
            expolygons_append(out, intersection_ex(diff_ex(hole_outer, hole_inner), holes));
        }
    }

    return union_ex(out);
}



std::vector<coord_t> belt_brim_line_positions(coord_t u_lo,
                                              coord_t u_hi,
                                              coord_t pitch_u,
                                              coord_t u_anchor)
{
    std::vector<coord_t> out;
    if (pitch_u <= 0 || u_hi <= u_lo)
        return out;




    int64_t k = int64_t(std::floor(double(u_lo - u_anchor) / double(pitch_u))) - 1;
    while (u_anchor + coord_t(k) * pitch_u < u_lo)
        ++ k;
    for (;; ++ k) {
        const coord_t u = u_anchor + coord_t(k) * pitch_u;
        if (u >= u_hi)
            break;
        out.emplace_back(u);
    }
    return out;
}







static Polygon band_box(const BoundingBox &bounds, int from_axis, coordf_t u_lo, coordf_t u_hi)
{
    coord_t lo = scale_(u_lo);
    coord_t hi = scale_(u_hi);
    const coord_t bmin = from_axis == 0 ? bounds.min.x() : bounds.min.y();
    const coord_t bmax = from_axis == 0 ? bounds.max.x() : bounds.max.y();
    lo = std::max(lo, bmin);
    hi = std::min(hi, bmax);
    Polygon poly;
    if (hi <= lo)
        return poly;
    if (from_axis == 0)
        poly.points = { Point(lo, bounds.min.y()), Point(hi, bounds.min.y()),
                        Point(hi, bounds.max.y()), Point(lo, bounds.max.y()) };
    else
        poly.points = { Point(bounds.min.x(), lo), Point(bounds.max.x(), lo),
                        Point(bounds.max.x(), hi), Point(bounds.min.x(), hi) };
    return poly;
}

ExPolygons belt_brim_clip_leading_edge(const ExPolygons &region, const BeltBrimFrame &frame, coordf_t u_cut)
{
    if (region.empty())
        return region;
    BoundingBox keep_bb = get_extents(region);
    keep_bb.offset(scale_(1.));
    const bool    low_side = frame.downhill_sign() < 0;
    const Polygon keep = band_box(keep_bb, frame.from_axis,
        low_side ? unscale<double>(frame.from_axis == 0 ? keep_bb.min.x() : keep_bb.min.y()) : u_cut,
        low_side ? u_cut : unscale<double>(frame.from_axis == 0 ? keep_bb.max.x() : keep_bb.max.y()));
    return keep.empty() ? ExPolygons{} : intersection_ex(region, Polygons{ keep });
}


struct BeltBrimContext
{
    BeltFloorContext ctx;
    BeltBrimFrame    frame;
    ExPolygons       region;
    BoundingBox      region_bbox;
    Flow             brim_flow;
    coord_t          pitch_u   = 0;
    coord_t          u_anchor  = 0;
    double           in_plane_pitch = 0.;
};


static void belt_brim_band_paths(const BeltBrimContext      &bc,
                                 coordf_t                    print_z,
                                 coordf_t                    height,
                                 const Polygons             &obstacles,
                                 ExtrusionEntityCollection  &out,
                                 ExPolygons                 &areas_out)
{
    coordf_t u_lo = bc.ctx.cutoff_u(print_z - height);
    coordf_t u_hi = bc.ctx.cutoff_u(print_z);
    if (u_lo > u_hi)
        std::swap(u_lo, u_hi);


    const double band_in_plane = (u_hi - u_lo) * bc.frame.u_stretch();




    static constexpr double BAND_CLEARANCE_FRACTION = 0.75;

    std::vector<coord_t> us;
    double uniform_clearance = 0.;
    double line_pitch        = bc.in_plane_pitch;



    if (band_in_plane <= 1.5 * bc.in_plane_pitch + EPSILON) {












        us.push_back(scale_(bc.ctx.cutoff_u(print_z - BAND_CLEARANCE_FRACTION * height)));
        uniform_clearance = BAND_CLEARANCE_FRACTION * height;
        line_pitch        = band_in_plane;
    } else {



        us = belt_brim_line_positions(scale_(u_lo), scale_(u_hi), bc.pitch_u, bc.u_anchor);
    }
    if (us.empty())
        return;

    const Polygons region_polys = to_polygons(bc.region);





    const coord_t margin = coord_t(SCALED_EPSILON) + 1;
    coord_t       u_prev = std::numeric_limits<coord_t>::min();
    for (coord_t u : us) {










        double clearance = uniform_clearance;
        if (clearance <= 0.) {
            const Point probe = bc.frame.from_axis == 0 ? Point(u, 0) : Point(0, u);
            clearance = print_z - bc.ctx.floor_print_z(probe);
            if (clearance < BAND_CLEARANCE_FRACTION * height) {
                clearance = BAND_CLEARANCE_FRACTION * height;
                u         = scale_(bc.ctx.cutoff_u(print_z - clearance));
            }
            clearance = std::min(clearance, height);
        }


        if (u_prev != std::numeric_limits<coord_t>::min() && std::abs(u - u_prev) < bc.pitch_u / 2)
            continue;
        u_prev = u;

        Polyline line;
        if (bc.frame.from_axis == 0)
            line.points = { Point(u, coord_t(bc.region_bbox.min.y() - margin)),
                            Point(u, coord_t(bc.region_bbox.max.y() + margin)) };
        else
            line.points = { Point(coord_t(bc.region_bbox.min.x() - margin), u),
                            Point(coord_t(bc.region_bbox.max.x() + margin), u) };

        Polylines pieces = intersection_pl(Polylines{ line }, region_polys);
        if (! obstacles.empty())
            pieces = diff_pl(pieces, obstacles);
        if (pieces.empty())
            continue;




        const Flow f = bc.brim_flow.with_cross_section(float(line_pitch * clearance));


        for (const Polygon &p : offset(pieces, 0.5f * float(f.scaled_width())))
            areas_out.emplace_back(ExPolygon(p));

        extrusion_entities_append_paths(out.entities, chain_polylines(std::move(pieces)),
                                        erBrim, f.mm3_per_mm(), f.width(), float(clearance));
    }
}


















static Polygons belt_brim_obstacles(const Print &print, const PrintObject &self,
                                    const BoundingBox &region_bbox, coordf_t print_z, coordf_t tol)
{
    const Point shift_self = self.instances().empty() ? Point(0, 0)
                                                      : self.instances().front().shift_without_plate_offset();
    Polygons out;
    for (const PrintObject *o : print.objects()) {
        const bool is_self = (o == &self);
        for (const PrintInstance &inst : o->instances()) {
            const Point delta = inst.shift_without_plate_offset() - shift_self;
            if (const Layer *l = o->get_layer_at_printz(print_z, tol)) {
                BoundingBox lb = get_extents(l->lslices);
                lb.translate(delta.x(), delta.y());
                if (lb.overlap(region_bbox)) {
                    Polygons ps = to_polygons(l->lslices);
                    for (Polygon &p : ps)
                        p.translate(delta);
                    polygons_append(out, std::move(ps));
                }
            }
            if (! is_self)
                continue;
            if (const SupportLayer *sl = o->get_support_layer_at_printz(print_z, tol)) {
                Polygons ps = sl->support_fills.polygons_covered_by_spacing();
                for (Polygon &p : ps)
                    p.translate(delta);
                polygons_append(out, std::move(ps));
            }
        }
    }
    if (out.size() < 2)
        return out;
    return union_(out);
}

void make_belt_brim(PrintObject &object)
{
    object.clear_belt_brim();
    if (! object.has_belt_brim())
        return;

    const Print &print = *object.print();
    BeltBrimContext bc;
    if (! bc.ctx.init(object.slicing_parameters(), print.config()))
        return;
    bc.frame = BeltBrimFrame{ bc.ctx.shear_factor(), bc.ctx.from_axis() };

    const size_t nlayers = object.layers().size();
    if (nlayers == 0)
        return;




    ExPolygons footprint_acc;

    const Layer *first_contact = nullptr;
    for (size_t i = 0; i < nlayers; ++ i) {
        const Layer &layer = *object.layers()[i];
        if (layer.lslices.empty())
            continue;


        coordf_t u_lo = bc.ctx.cutoff_u(layer.print_z - layer.height);
        coordf_t u_hi = bc.ctx.cutoff_u(layer.print_z);
        if (u_lo > u_hi)
            std::swap(u_lo, u_hi);
        BoundingBox bb = get_extents(layer.lslices);
        bb.offset(scale_(1.));
        const Polygon band = band_box(bb, bc.frame.from_axis, u_lo, u_hi);
        if (band.empty())
            continue;
        ExPolygons contact = intersection_ex(layer.lslices, Polygons{ band });
        if (contact.empty())
            continue;
        if (first_contact == nullptr)
            first_contact = &layer;
        expolygons_append(footprint_acc, std::move(contact));
    }
    const ExPolygons footprint = union_ex(footprint_acc);
    if (footprint.empty())
        return;


    const PrintObjectConfig &cfg = object.config();
    bc.brim_flow = print.brim_flow();
    const double  flow_w = bc.brim_flow.scaled_spacing() * SCALING_FACTOR;

    const coord_t width   = scale_(std::floor(cfg.brim_width.value / flow_w / 2) * flow_w * 2);
    const coord_t leading = scale_(cfg.leading_brim_length.value);
    const coord_t lateral = scale_(cfg.extra_brim_width.value);
    const coord_t gap     = scale_(cfg.brim_object_gap.value);




    const BrimType bt = cfg.brim_type.value;
    const bool has_outer = bt == btOuterOnly || bt == btOuterAndInner
                        || bt == btAutoBrim  || bt == btEar || bt == btPainted
                        || bt == btLeadingEdgeOnly;
    const bool has_inner = bt == btInnerOnly || bt == btOuterAndInner;

    bc.region = belt_unflatten(
        belt_brim_region(belt_flatten(footprint, bc.frame), has_outer, has_inner,
                         width, gap, leading, lateral, bc.frame),
        bc.frame);

    if (bt == btLeadingEdgeOnly && first_contact != nullptr && ! bc.region.empty())





        bc.region = belt_brim_clip_leading_edge(bc.region, bc.frame, bc.ctx.cutoff_u(first_contact->print_z));

    if (bc.region.empty())
        return;
    bc.region_bbox = get_extents(bc.region);




    bc.pitch_u = std::max<coord_t>(1, coord_t(bc.brim_flow.scaled_spacing() * bc.frame.cos_tilt()));
    bc.in_plane_pitch = unscale<double>(bc.pitch_u) * bc.frame.u_stretch();
    {
        const BoundingBox fbb = get_extents(footprint);
        const bool low_side = bc.frame.shear > 0.;
        bc.u_anchor = bc.frame.from_axis == 0 ? (low_side ? fbb.min.x() : fbb.max.x())
                                              : (low_side ? fbb.min.y() : fbb.max.y());
    }


    std::vector<ExtrusionEntityCollection> by_layer(nlayers);
    std::vector<ExPolygons>                areas_by_layer(nlayers);
    for (size_t i = 0; i < nlayers; ++ i) {
        const Layer &layer = *object.layers()[i];
        const Polygons obstacles = belt_brim_obstacles(print, object, bc.region_bbox, layer.print_z, 0.5 * layer.height);
        belt_brim_band_paths(bc, layer.print_z, layer.height, obstacles, by_layer[i], areas_by_layer[i]);
    }



    std::vector<BeltBrimBand> prologue;
    {
        const Layer   &first  = *object.layers().front();
        const coordf_t h      = first.height;
        const bool     low_side = bc.frame.shear > 0.;
        const coord_t  u_lead_s = bc.frame.from_axis == 0
            ? (low_side ? bc.region_bbox.min.x() : bc.region_bbox.max.x())
            : (low_side ? bc.region_bbox.min.y() : bc.region_bbox.max.y());
        const coordf_t u_lead  = unscale<double>(u_lead_s);

        const coordf_t z_lead  = bc.ctx.shear_factor() * u_lead
                               + bc.ctx.floor_offset() + bc.ctx.z_shift();
        if (h > EPSILON)
            for (coordf_t z = first.print_z - h; z > z_lead - h; z -= h) {
                const Polygons obstacles = belt_brim_obstacles(print, object, bc.region_bbox, z, 0.5 * h);
                BeltBrimBand band;
                band.print_z = z;
                band.height  = h;
                belt_brim_band_paths(bc, z, h, obstacles, band.fills, band.areas);
                if (! band.fills.empty())
                    prologue.emplace_back(std::move(band));
            }

        std::reverse(prologue.begin(), prologue.end());
    }

    object.set_belt_brim(std::move(by_layer), std::move(areas_by_layer), std::move(prologue));
}

}
