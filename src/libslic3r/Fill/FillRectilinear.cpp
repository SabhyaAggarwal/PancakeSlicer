#include <numeric>
#include <math.h>
#include <initializer_list>
#include <array>
#include <map>
#include <stdlib.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include <boost/container/small_vector.hpp>
#include <boost/log/trivial.hpp>
#include <boost/static_assert.hpp>
#include <boost/math/constants/constants.hpp>
#include <vector>
#include <utility>
#include <string>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Geometry.hpp"
#include "../Surface.hpp"
#include "../ShortestPath.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "FillCornerSmoothing.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Flow.hpp"
#include "FillRectilinear.hpp"

#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #include "SVG.hpp"
#endif

#if defined(SLIC3R_DEBUG) || defined(INFILL_DEBUG_OUTPUT)
    #include "SVG.hpp"
#endif

#include <cassert>

#include "../libslic3r.h"

namespace Slic3r {

static inline coordf_t segment_length(const Polygon &poly, size_t seg1, const Point &p1, size_t seg2, const Point &p2)
{
#ifdef SLIC3R_DEBUG
    for (size_t i = 0; i < 2; ++ i) {
        size_t seg = (i == 0) ? seg1 : seg2;
        Point  px  = (i == 0) ? p1   : p2;
        Point  pa  = poly.points[((seg == 0) ? poly.points.size() : seg) - 1];
        Point  pb  = poly.points[seg];
        if (pa(0) > pb(0))
            std::swap(pa(0), pb(0));
        if (pa(1) > pb(1))
            std::swap(pa(1), pb(1));
        assert(px(0) >= pa(0) && px(0) <= pb(0));
        assert(px(1) >= pa(1) && px(1) <= pb(1));
    }
#endif
    const Point *pPrev = &p1;
    const Point *pThis = NULL;
    coordf_t len = 0;
    if (seg1 <= seg2) {
        for (size_t i = seg1; i < seg2; ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
    } else {
        for (size_t i = seg1; i < poly.points.size(); ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
        for (size_t i = 0; i < seg2; ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
    }
    len += (*pPrev - p2).cast<double>().norm();
    return len;
}

static inline void polygon_segment_append(Points &out, const Polygon &polygon, size_t seg1, size_t seg2)
{
    if (seg1 == seg2) {
    } else if (seg1 < seg2) {
        out.insert(out.end(), polygon.points.begin() + seg1, polygon.points.begin() + seg2);
    } else {
        out.reserve(out.size() + seg2 + polygon.points.size() - seg1);
        out.insert(out.end(), polygon.points.begin() + seg1, polygon.points.end());
        out.insert(out.end(), polygon.points.begin(), polygon.points.begin() + seg2);
    }
}

static inline void polygon_segment_append_reversed(Points &out, const Polygon &polygon, size_t seg1, size_t seg2)
{
    if (seg1 >= seg2) {
        out.reserve(seg1 - seg2);
        for (size_t i = seg1; i > seg2; -- i)
            out.push_back(polygon.points[i - 1]);
    } else {
        out.reserve(out.size() + seg2 + polygon.points.size() - seg1);
        for (size_t i = seg1; i > 0; -- i)
            out.push_back(polygon.points[i - 1]);
        for (size_t i = polygon.points.size(); i > seg2; -- i)
            out.push_back(polygon.points[i - 1]);
    }
}

struct SegmentIntersection
{
    size_t      iContour { 0 };
    size_t      iSegment { 0 };
    int64_t     pos_p { 0 };
    uint32_t    pos_q { 1 };

    coord_t     pos() const {
        int64_t p = pos_p;
        if (p < 0)
            p -= int64_t(pos_q>>1);
        else
            p += int64_t(pos_q>>1);
        return coord_t(p / int64_t(pos_q)); 
    }

    int32_t	prev_on_contour { 0 };
    int32_t	next_on_contour { 0 };

    enum SegmentIntersectionType : char {
        UNKNOWN,
        OUTER_LOW,
        OUTER_HIGH,
        INNER_LOW,
        INNER_HIGH,
    };
    SegmentIntersectionType type { UNKNOWN };

    enum class LinkType : uint8_t {
    	Horizontal,
    	Up,
    	Down,
        Phony,
    };

    enum class LinkQuality : uint8_t {
    	Invalid,
        Valid,
    	TooLong,
    };

    LinkType 		prev_on_contour_type { LinkType::Horizontal };
    LinkType 		next_on_contour_type { LinkType::Horizontal };
    LinkQuality 	prev_on_contour_quality { LinkQuality::Valid };
    LinkQuality 	next_on_contour_quality { LinkQuality::Valid };
    bool 	 		consumed_vertical_up { false };
    bool 	 		consumed_perimeter_right { false };

    bool 	is_inner() const { return type == INNER_LOW  || type == INNER_HIGH; }
    bool 	is_outer() const { return type == OUTER_LOW  || type == OUTER_HIGH; }
    bool 	is_low  () const { return type == INNER_LOW  || type == OUTER_LOW; }
    bool 	is_high () const { return type == INNER_HIGH || type == OUTER_HIGH; }

    enum class Side {
    	Left,
    	Right
    };
    enum class Direction {
    	Up,
    	Down
    };

    bool 	has_left_horizontal()    		 	const { return this->prev_on_contour_type == LinkType::Horizontal; }
    bool 	has_right_horizontal()   		 	const { return this->next_on_contour_type == LinkType::Horizontal; }
    bool 	has_horizontal(Side side)		 	const { return side == Side::Left ? this->has_left_horizontal() : this->has_right_horizontal(); }

    bool 	has_left_vertical_up()   		 	const { return this->prev_on_contour_type == LinkType::Up; }
    bool 	has_left_vertical_down() 		 	const { return this->prev_on_contour_type == LinkType::Down; }
    bool 	has_left_vertical(Direction dir) 	const { return dir == Direction::Up ? this->has_left_vertical_up() : this->has_left_vertical_down(); }
    bool 	has_left_vertical()    	 		 	const { return this->has_left_vertical_up() || this->has_left_vertical_down(); }
    bool 	has_left_vertical_outside()			const { return this->is_low() ? this->has_left_vertical_down() : this->has_left_vertical_up(); }

    bool 	has_right_vertical_up()   			const { return this->next_on_contour_type == LinkType::Up; }
    bool 	has_right_vertical_down() 			const { return this->next_on_contour_type == LinkType::Down; }
    bool 	has_right_vertical(Direction dir) 	const { return dir == Direction::Up ? this->has_right_vertical_up() : this->has_right_vertical_down(); }
    bool 	has_right_vertical()    			const { return this->has_right_vertical_up() || this->has_right_vertical_down(); }
    bool 	has_right_vertical_outside()		const { return this->is_low() ? this->has_right_vertical_down() : this->has_right_vertical_up(); }

    bool 	has_vertical()						const { return this->has_left_vertical() || this->has_right_vertical(); }
    bool 	has_vertical(Side side)				const { return side == Side::Left ? this->has_left_vertical() : this->has_right_vertical(); }
    bool 	has_vertical_up()					const { return this->has_left_vertical_up() || this->has_right_vertical_up(); }
    bool 	has_vertical_down()					const { return this->has_left_vertical_down() || this->has_right_vertical_down(); }
    bool 	has_vertical(Direction dir)			const { return dir == Direction::Up ? this->has_vertical_up() : this->has_vertical_down(); }

    int 	left_horizontal()  					const { return this->has_left_horizontal() 	? this->prev_on_contour : -1; }
    int 	right_horizontal()  				const { return this->has_right_horizontal() ? this->next_on_contour : -1; }
    int 	horizontal(Side side)  				const { return side == Side::Left ? this->left_horizontal() : this->right_horizontal(); }
    LinkQuality horizontal_quality(Side side)	const {
    	assert(this->has_horizontal(side));
    	return side == Side::Left ? this->prev_on_contour_quality : this->next_on_contour_quality;
    }

    int 	left_vertical_up()   		 		const { return this->has_left_vertical_up()    ? this->prev_on_contour : -1; }
    int 	left_vertical_down()   		 		const { return this->has_left_vertical_down()  ? this->prev_on_contour : -1; }
    int 	left_vertical(Direction dir) 		const { return (dir == Direction::Up ? this->has_left_vertical_up() : this->has_left_vertical_down()) ? this->prev_on_contour : -1; }
    int 	left_vertical()   			 		const { return this->has_left_vertical() 	   ? this->prev_on_contour : -1; }
    int 	left_vertical_outside()				const { return this->is_low() ? this->left_vertical_down() : this->left_vertical_up(); }
    int 	right_vertical_up()   		 		const { return this->has_right_vertical_up()   ? this->next_on_contour : -1; }
    int 	right_vertical_down()   	 		const { return this->has_right_vertical_down() ? this->next_on_contour : -1; }
    int 	right_vertical(Direction dir) 		const { return (dir == Direction::Up ? this->has_right_vertical_up() : this->has_right_vertical_down()) ? this->next_on_contour : -1; }
    int 	right_vertical()   			 		const { return this->has_right_vertical() 	   ? this->next_on_contour : -1; }
    int 	right_vertical_outside()			const { return this->is_low() ? this->right_vertical_down() : this->right_vertical_up(); }

    int 	vertical_up(Side side)				const { return side == Side::Left ? this->left_vertical_up() : this->right_vertical_up(); }
    int 	vertical_down(Side side)			const { return side == Side::Left ? this->left_vertical_down() : this->right_vertical_down(); }
    int 	vertical_outside(Side side)			const { return side == Side::Left ? this->left_vertical_outside() : this->right_vertical_outside(); }
    int 	vertical_up()						const { 
    	return this->has_left_vertical_up() ? this->left_vertical_up() : this->right_vertical_up();
    }
    LinkQuality vertical_up_quality()			const {
    	return this->has_left_vertical_up() ? this->prev_on_contour_quality : this->next_on_contour_quality;
    }
    int 	vertical_down()						const {
    	return this->has_left_vertical_down() ? this->left_vertical_down() : this->right_vertical_down();
    }
    LinkQuality vertical_down_quality()			const {
    	return this->has_left_vertical_down() ? this->prev_on_contour_quality : this->next_on_contour_quality;
    }
    int 	vertical_outside()					const { return this->is_low() ? this->vertical_down() : this->vertical_up(); }
    LinkQuality vertical_outside_quality()		const { return this->is_low() ? this->vertical_down_quality() : this->vertical_up_quality(); }

    bool operator<(const SegmentIntersection &other) const
    {
        assert(pos_q > 0);
        assert(other.pos_q > 0);
        if (pos_p == 0 || other.pos_p == 0) {
            return pos_p < other.pos_p;
        } else {
            int sign1 = (pos_p > 0) ? 1 : -1;
            int sign2 = (other.pos_p > 0) ? 1 : -1;
            int signs = sign1 * sign2;
            assert(signs == 1 || signs == -1);
            if (signs < 0) {
                return sign1 < 0;
            } else {
                uint64_t p1, p2;
                if (sign1 > 0) {
                    p1 = uint64_t(pos_p);
                    p2 = uint64_t(other.pos_p);
                } else {
                    p1 = uint64_t(- pos_p);
                    p2 = uint64_t(- other.pos_p);
                };
                uint64_t l_hi = (p1 >> 32) * uint64_t(other.pos_q);
                uint64_t l_lo = (p1 & 0xffffffffll) * uint64_t(other.pos_q);
                l_hi += (l_lo >> 32);
                uint64_t r_hi = (p2 >> 32) * uint64_t(pos_q);
                uint64_t r_lo = (p2 & 0xffffffffll) * uint64_t(pos_q);
                r_hi += (r_lo >> 32);
                if (l_hi == r_hi) {
                    l_lo &= 0xffffffffll;
                    r_lo &= 0xffffffffll;
                    return (sign1 < 0) ? (l_lo > r_lo) : (l_lo < r_lo);
                }
                return (sign1 < 0) ? (l_hi > r_hi) : (l_hi < r_hi);
            }
        }
    }

    bool operator==(const SegmentIntersection &other) const 
    {
        assert(pos_q > 0);
        assert(other.pos_q > 0);
        if (pos_p == 0 || other.pos_p == 0) {
            return pos_p == other.pos_p;
        }

        bool positive = pos_p > 0;
        if (positive != (other.pos_p > 0))
            return false;
        uint64_t p1 = positive ? uint64_t(pos_p) : uint64_t(- pos_p);
        uint64_t p2 = positive ? uint64_t(other.pos_p) : uint64_t(- other.pos_p);
        uint64_t l_lo = (p1 & 0xffffffffll) * uint64_t(other.pos_q);
        uint64_t r_lo = (p2 & 0xffffffffll) * uint64_t(pos_q);
        if (l_lo != r_lo)
            return false;
        uint64_t l_hi = (p1 >> 32) * uint64_t(other.pos_q);
        uint64_t r_hi = (p2 >> 32) * uint64_t(pos_q);
        return l_hi + (l_lo >> 32) == r_hi + (r_lo >> 32);
    }
};
static_assert(sizeof(SegmentIntersection::pos_q) == 4, "SegmentIntersection::pos_q has to be 32bit long!");

struct SegmentedIntersectionLine
{
    size_t                              idx;
    coord_t                             pos;
    std::vector<SegmentIntersection>    intersections;
};

static SegmentIntersection phony_outer_intersection(SegmentIntersection::SegmentIntersectionType type, coord_t pos)
{
    assert(type == SegmentIntersection::OUTER_LOW || type == SegmentIntersection::OUTER_HIGH);
    SegmentIntersection out;
    out.iContour                = std::numeric_limits<size_t>::max();
    out.iSegment                = std::numeric_limits<size_t>::max();
    out.pos_p                   = pos;
    out.type                    = type;
    out.prev_on_contour         = -1;
    out.next_on_contour         = -1;
    out.prev_on_contour_type    = SegmentIntersection::LinkType::Phony;
    out.next_on_contour_type    = SegmentIntersection::LinkType::Phony;
    out.prev_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
    out.next_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
    return out;
}

struct ExPolygonWithOffset
{
public:
    ExPolygonWithOffset(
        const ExPolygon &expolygon,
        float   angle,
        coord_t aoffset1,
        coord_t aoffset2 = 0)
    {
        polygons_src = expolygon;
        if (angle != 0.f) {
            polygons_src.contour.rotate(angle);
            for (Polygon &hole : polygons_src.holes)
                hole.rotate(angle);
        }

        double miterLimit = DefaultMiterLimit;
        assert(aoffset2 <= 0);
        remove_sticks(polygons_src);
        polygons_outer = aoffset1 == 0 ? to_polygons(polygons_src) : offset(polygons_src, float(aoffset1), jtMiter, miterLimit);
        if (aoffset2 < 0)
            polygons_inner = shrink(polygons_outer, float(aoffset1 - aoffset2), jtMiter, miterLimit);
        const double min_area_threshold = 0.01 * aoffset2 * aoffset2;
        remove_small(polygons_outer, min_area_threshold);
        remove_small(polygons_inner, min_area_threshold);
        remove_sticks(polygons_outer);
        remove_sticks(polygons_inner);
		n_contours_outer = polygons_outer.size();
        n_contours_inner = polygons_inner.size();
        n_contours = n_contours_outer + n_contours_inner;
        polygons_ccw.assign(n_contours, false);
        for (size_t i = 0; i < n_contours; ++ i) {
            contour(i).remove_duplicate_points();
            assert(! contour(i).has_duplicate_points());
            polygons_ccw[i] = Slic3r::Geometry::is_ccw(contour(i));
        }
    }

    ExPolygonWithOffset(const ExPolygonWithOffset &rhs, float angle) : ExPolygonWithOffset(rhs) {
        if (angle != 0.f) {
            this->polygons_src.contour.rotate(angle);
            for (Polygon &hole : this->polygons_src.holes)
                hole.rotate(angle);
            for (Polygon &poly : this->polygons_outer)
                poly.rotate(angle);
            for (Polygon &poly : this->polygons_inner)
                poly.rotate(angle);
        }
    }

    bool             is_contour_outer(size_t idx) const { return idx < n_contours_outer; }
    bool             is_contour_inner(size_t idx) const { return idx >= n_contours_outer; }

    const Polygon&   contour(size_t idx) const 
        { return is_contour_outer(idx) ? polygons_outer[idx] : polygons_inner[idx - n_contours_outer]; }

    Polygon&         contour(size_t idx)
        { return is_contour_outer(idx) ? polygons_outer[idx] : polygons_inner[idx - n_contours_outer]; }

    bool             is_contour_ccw(size_t idx) const { return polygons_ccw[idx]; }

    BoundingBox      bounding_box_src() const 
        { return get_extents(polygons_src); }
    BoundingBox      bounding_box_outer() const 
        { return get_extents(polygons_outer); }
    BoundingBox      bounding_box_inner() const 
        { return get_extents(polygons_inner); }

#ifdef SLIC3R_DEBUG
    void export_to_svg(Slic3r::SVG &svg) {
        svg.draw_outline(polygons_src,   "black");
        svg.draw_outline(polygons_outer, "green");
        svg.draw_outline(polygons_inner, "brown");
    }
#endif

    ExPolygon        polygons_src;
    Polygons         polygons_outer;
    Polygons         polygons_inner;

    size_t           n_contours_outer;
    size_t           n_contours_inner;
    size_t           n_contours;

protected:
    std::vector<unsigned char> polygons_ccw;
};

static inline int distance_of_segmens(const Polygon &poly, size_t seg1, size_t seg2, bool forward)
{
    int d = int(seg2) - int(seg1);
    if (! forward)
        d = - d;
    if (d < 0)
        d += int(poly.points.size());
    return d;
}

static inline bool intersection_on_prev_next_vertical_line_valid(
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    SegmentIntersection::Side                      side)
{
    const SegmentedIntersectionLine &vline_this = segs[iVerticalLine];
    const SegmentIntersection       &it_this    = vline_this.intersections[iIntersection];
	if (it_this.has_vertical(side))
		return false;
    int iIntersectionOther = it_this.horizontal(side);
    if (iIntersectionOther == -1)
        return false;
    assert(side == SegmentIntersection::Side::Right ? (iVerticalLine + 1 < segs.size()) : (iVerticalLine > 0));
    const SegmentedIntersectionLine &vline_other = segs[side == SegmentIntersection::Side::Right ? (iVerticalLine + 1) : (iVerticalLine - 1)];
    const SegmentIntersection       &it_other    = vline_other.intersections[iIntersectionOther];
    assert(it_other.is_inner());
    assert(iIntersectionOther > 0 && size_t(iIntersectionOther + 1) < vline_other.intersections.size());
    const SegmentIntersection       &it_other2   = vline_other.intersections[it_other.is_low() ? iIntersectionOther - 1 : iIntersectionOther + 1];
    if (it_other2.is_inner())
        return false;
    assert(it_other.is_low() == it_other2.is_low());
    if (it_this.horizontal_quality(side) != SegmentIntersection::LinkQuality::Valid)
    	return false;
    if (side == SegmentIntersection::Side::Right ? it_this.consumed_perimeter_right : it_other.consumed_perimeter_right)
        return false;
    if (it_other.is_low() ? it_other.consumed_vertical_up : vline_other.intersections[iIntersectionOther - 1].consumed_vertical_up)
        return false;
#if 0
    if (it_other.vertical_outside() != -1 && it_other.vertical_outside_quality() == SegmentIntersection::LinkQuality::Valid)
        return false;
#endif
    return true;
}

static inline bool intersection_on_prev_vertical_line_valid(
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iIntersection)
{
    return intersection_on_prev_next_vertical_line_valid(segs, iVerticalLine, iIntersection, SegmentIntersection::Side::Left);
}

static inline bool intersection_on_next_vertical_line_valid(
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iIntersection)
{
    return intersection_on_prev_next_vertical_line_valid(segs, iVerticalLine, iIntersection, SegmentIntersection::Side::Right);
}

static inline coordf_t measure_perimeter_horizontal_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset, 
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersection2)
{
    size_t                           iVerticalLineOther = iVerticalLine + 1;
    assert(iVerticalLineOther < segs.size());
    const SegmentedIntersectionLine &vline  = segs[iVerticalLine];
    const SegmentIntersection       &it     = vline.intersections[iIntersection];
    const SegmentedIntersectionLine &vline2 = segs[iVerticalLineOther];
    const SegmentIntersection       &it2    = vline2.intersections[iIntersection2];
    assert(it.iContour == it2.iContour);
    const Polygon                   &poly   = poly_with_offset.contour(it.iContour);
    assert(it.type == it2.type);
    assert(it.iContour == it2.iContour);

    Point p1(vline.pos,  it.pos());
    Point p2(vline2.pos, it2.pos());
    return it.is_low() ?
        segment_length(poly, it .iSegment, p1, it2.iSegment, p2) :
        segment_length(poly, it2.iSegment, p2, it .iSegment, p1);
}

static inline void emit_perimeter_prev_next_segment(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    Polyline                                      &out,
    bool                                           dir_is_next)
{
    size_t iVerticalLineOther = iVerticalLine;
    if (dir_is_next) {
        ++ iVerticalLineOther;
        assert(iVerticalLineOther < segs.size());
    } else {
        assert(iVerticalLineOther > 0);
        -- iVerticalLineOther;
    }

    const SegmentedIntersectionLine &il     = segs[iVerticalLine];
    const SegmentIntersection       &itsct  = il.intersections[iIntersection];
    const SegmentedIntersectionLine &il2    = segs[iVerticalLineOther];
    const SegmentIntersection       &itsct2 = il2.intersections[iIntersection2];
    const Polygon                   &poly   = poly_with_offset.contour(iInnerContour);
    assert(itsct.type == itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    assert(itsct.is_inner());
    const bool                       forward = itsct.is_low() == dir_is_next;
    if (forward)
        polygon_segment_append(out.points, poly, itsct.iSegment, itsct2.iSegment);
    else
        polygon_segment_append_reversed(out.points, poly, itsct.iSegment, itsct2.iSegment);
    out.points.push_back(Point(il2.pos, itsct2.pos()));
}

static inline coordf_t measure_perimeter_segment_on_vertical_line_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    bool                                           forward)
{
    const SegmentedIntersectionLine &il = segs[iVerticalLine];
    const SegmentIntersection       &itsct = il.intersections[iIntersection];
    const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
    const Polygon                   &poly = poly_with_offset.contour(itsct.iContour);
    assert(itsct.is_inner() == itsct2.is_inner());
    assert(itsct.type != itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    Point p1(il.pos, itsct.pos());
    Point p2(il.pos, itsct2.pos());
    return forward ?
        segment_length(poly, itsct .iSegment, p1, itsct2.iSegment, p2) :
        segment_length(poly, itsct2.iSegment, p2, itsct .iSegment, p1);
}

static inline void emit_perimeter_segment_on_vertical_line(
	const ExPolygonWithOffset                     &poly_with_offset,
	const std::vector<SegmentedIntersectionLine>  &segs,
	size_t                                         iVerticalLine,
	size_t                                         iInnerContour,
	size_t                                         iIntersection,
	size_t                                         iIntersection2,
	Polyline                                      &out,
	bool                                           forward)
{
	const SegmentedIntersectionLine &il = segs[iVerticalLine];
	const SegmentIntersection       &itsct = il.intersections[iIntersection];
	const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
	const Polygon                   &poly = poly_with_offset.contour(iInnerContour);
	assert(itsct.is_inner());
	assert(itsct2.is_inner());
	assert(itsct.type != itsct2.type);
    assert(itsct.iContour == iInnerContour);
	assert(itsct.iContour == itsct2.iContour);
	if (forward)
		polygon_segment_append(out.points, poly, itsct.iSegment, itsct2.iSegment);
	else
		polygon_segment_append_reversed(out.points, poly, itsct.iSegment, itsct2.iSegment);
	out.points.push_back(Point(il.pos, itsct2.pos()));
}

enum DirectionMask
{
    DIR_FORWARD  = 1,
    DIR_BACKWARD = 2
};

static std::vector<SegmentedIntersectionLine> slice_region_by_vertical_lines(const ExPolygonWithOffset &poly_with_offset, size_t n_vlines, coord_t x0, coord_t line_spacing)
{
    std::vector<SegmentedIntersectionLine> segs(n_vlines, SegmentedIntersectionLine());
    for (coord_t i = 0; i < coord_t(n_vlines); ++ i) {
        segs[i].idx = i;
        segs[i].pos = x0 + i * line_spacing;
    }
    for (size_t iContour = 0; iContour < poly_with_offset.n_contours; ++ iContour) {
        const Points &contour = poly_with_offset.contour(iContour).points;
        if (contour.size() < 2)
            continue;
        for (size_t iSegment = 0; iSegment < contour.size(); ++ iSegment) {
            size_t iPrev = ((iSegment == 0) ? contour.size() : iSegment) - 1;
            const Point &p1 = contour[iPrev];
            const Point &p2 = contour[iSegment];
            coord_t l = p1(0);
            coord_t r = p2(0);
            if (l > r)
                std::swap(l, r);
            int il = (l - x0) / line_spacing;
            while (il * line_spacing + x0 < l)
                ++ il;
            il = std::max(int(0), il);
            int ir = (r - x0 + line_spacing) / line_spacing;
            while (ir * line_spacing + x0 > r)
                -- ir;
            ir = std::min(int(segs.size()) - 1, ir);
            if (il > ir)
                continue;
            assert(il >= 0 && size_t(il) < segs.size());
            assert(ir >= 0 && size_t(ir) < segs.size());
            for (int i = il; i <= ir; ++ i) {
                coord_t this_x = segs[i].pos;
				assert(this_x == i * line_spacing + x0);
                SegmentIntersection is;
                is.iContour = iContour;
                is.iSegment = iSegment;
                assert(l <= this_x);
                assert(r >= this_x);
                if (p1.x() == this_x) {
                    if (p2.x() == this_x) {
                        continue;
                    }
                    const Point &p0 = prev_value_modulo(iPrev, contour);
                    if (int64_t(p0.x() - p1.x()) * int64_t(p2.x() - p1.x()) > 0) {
                        continue;
                    }
                    is.pos_p = p1.y();
                    is.pos_q = 1;
                } else if (p2.x() == this_x) {
                    const Point &p3 = next_value_modulo(iSegment, contour);
                    if (int64_t(p3.x() - p2.x()) * int64_t(p1.x() - p2.x()) > 0) {
                        continue;
                    }
                    is.pos_p = p2.y();
                    is.pos_q = 1;
                } else {
                    if (p2.x() > p1.x()) {
                        is.pos_p = this_x - p1.x();
                        is.pos_q = p2.x() - p1.x();
                    } else {
                        is.pos_p = p1.x() - this_x;
                        is.pos_q = p1.x() - p2.x();
                    }
                    assert(is.pos_q > 1);
                    assert(is.pos_p > 0 && is.pos_p < is.pos_q);
                    is.pos_p *= int64_t(p2.y() - p1.y());
                    is.pos_p += p1.y() * int64_t(is.pos_q);
                }
                assert(is.pos() + 1 >= std::min(p1.y(), p2.y()));
                assert(is.pos() <= std::max(p1.y(), p2.y()) + 1);
                segs[i].intersections.push_back(is);
            }
        }
    }

    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        std::sort(sil.intersections.begin(), sil.intersections.end());
        size_t j = 0;
        for (size_t i = 0; i < sil.intersections.size(); ++ i) {
            SegmentIntersection       &is       = sil.intersections[i];
            const size_t               iContour = is.iContour;
            const Points              &contour  = poly_with_offset.contour(iContour).points;
            const size_t               iSegment = is.iSegment;
            const size_t               iPrev    = prev_idx_modulo(iSegment, contour);
            const coord_t              dir      = contour[iSegment].x() - contour[iPrev].x();
            const bool                 low      = dir > 0;
            is.type = poly_with_offset.is_contour_outer(iContour) ?
                (low ? SegmentIntersection::OUTER_LOW : SegmentIntersection::OUTER_HIGH) :
                (low ? SegmentIntersection::INNER_LOW : SegmentIntersection::INNER_HIGH);
            bool take_next = true;
            if (j > 0) {
                SegmentIntersection &is2 = sil.intersections[j - 1];
                if (iContour == is2.iContour && is.pos_q == 1 && is2.pos_q == 1) {
                    if (is.pos_p == is2.pos_p) {
                        assert(iSegment == prev_idx_modulo(is2.iSegment, contour) || is2.iSegment == iPrev);
                        assert(is.type == is2.type);
                        take_next = false;
                    } else if (is.type == is2.type) {
                        if (low) {
                        } else {
                            sil.intersections[j-1] = sil.intersections[i];
                        }
                        take_next = false;
                    }
                }
            }
            if (take_next) {
                if (j < i)
                    sil.intersections[j] = sil.intersections[i];
                ++ j;
            }
        }
        if (j < sil.intersections.size())
            sil.intersections.erase(sil.intersections.begin() + j, sil.intersections.end());
    }

#ifdef INFILL_DEBUG_OUTPUT
    #define INFILL_DEBUG_ASSERT(CONDITION)
    try {
#else
    #define INFILL_DEBUG_ASSERT(CONDITION) assert(CONDITION)
#endif
#define ASSERT_THROW(CONDITION) do { INFILL_DEBUG_ASSERT(CONDITION); if (! (CONDITION)) throw InfillFailedException(); } while (0)
    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        ASSERT_THROW((sil.intersections.size() & 1) == 0);
        for (size_t i = 0; i < sil.intersections.size();) {
            ASSERT_THROW(sil.intersections[i].type == SegmentIntersection::OUTER_LOW);
            size_t j = i + 1;
            ASSERT_THROW(j < sil.intersections.size());
            ASSERT_THROW(sil.intersections[j].type == SegmentIntersection::INNER_LOW || sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            ASSERT_THROW(j < sil.intersections.size());
            ASSERT_THROW((j & 1) == 1);
            ASSERT_THROW(sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            ASSERT_THROW(i + 1 == j || sil.intersections[j - 1].type == SegmentIntersection::INNER_HIGH);
            i = j + 1;
        }
    }
#undef ASSERT_THROW
#undef INFILL_DEBUG_ASSERT
#ifdef INFILL_DEBUG_OUTPUT
    } catch (const InfillFailedException & ) {
        static int iRun = 0;
        BoundingBox bbox = get_extents(poly_with_offset.polygons_src);
        bbox.offset(scale_(3.));
        ::Slic3r::SVG svg(debug_out_path("slice_region_by_vertical_lines-failed-%d.svg", iRun ++), bbox);
        svg.draw(poly_with_offset.polygons_src);
        svg.draw_outline(poly_with_offset.polygons_src, "green");
        svg.draw_outline(poly_with_offset.polygons_outer, "green");
        svg.draw_outline(poly_with_offset.polygons_inner, "green");
        for (size_t i_seg = 0; i_seg < segs.size(); ++i_seg) {
            SegmentedIntersectionLine &sil = segs[i_seg];
            for (size_t i = 0; i < sil.intersections.size();) {
                if (sil.intersections[i].type != SegmentIntersection::OUTER_LOW) {
                    svg.draw(Point(sil.pos, sil.intersections[i].pos()), "red");
                    break;
                }
                size_t j = i + 1;
                if (j == sil.intersections.size()) {
                    svg.draw(Point(sil.pos, sil.intersections[i].pos()), "magenta");
                    break;
                }
                if (! (sil.intersections[j].type == SegmentIntersection::INNER_LOW || sil.intersections[j].type == SegmentIntersection::OUTER_HIGH)) {
                    svg.draw(Point(sil.pos, sil.intersections[j].pos()), "blue");
                    break;
                }
                for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++j);
                if (j == sil.intersections.size()) {
                    svg.draw(Point(sil.pos, sil.intersections[j - 1].pos()), "magenta");
                    break;
                }
                if ((j & 1) != 1 || sil.intersections[j].type != SegmentIntersection::OUTER_HIGH) {
                    svg.draw(Point(sil.pos, sil.intersections[j].pos()), "red");
                    break;
                }
                if (! (i + 1 == j || sil.intersections[j - 1].type == SegmentIntersection::INNER_HIGH)) {
                    svg.draw(Point(sil.pos, sil.intersections[j].pos()), "red");
                    break;
                }
                svg.draw(Line(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[j].pos())), "black");
                i = j + 1;
            }
        }
        assert(false);
        throw;
    }
#endif
    return segs;
}

#ifndef NDEBUG
bool validate_segment_intersection_connectivity(const std::vector<SegmentedIntersectionLine> &segs)
{
    for (size_t i_vline = 0; i_vline + 1 < segs.size(); ++ i_vline) {
        const SegmentedIntersectionLine &il_left  = segs[i_vline];
        const SegmentedIntersectionLine &il_right = segs[i_vline + 1];
        for (const SegmentIntersection &it : il_left.intersections) {
            if (it.has_right_horizontal()) {
                const SegmentIntersection &it_right = il_right.intersections[it.right_horizontal()];
                assert(it.iContour == it_right.iContour);
                assert(it.type == it_right.type);
                assert(it_right.has_left_horizontal());
                assert(it_right.left_horizontal() == int(&it - il_left.intersections.data()));
            }
        }
        for (const SegmentIntersection &it : il_right.intersections) {
            if (it.has_left_horizontal()) {
                const SegmentIntersection &it_left = il_left.intersections[it.left_horizontal()];
                assert(it.iContour == it_left.iContour);
                assert(it.type == it_left.type);
                assert(it_left.has_right_horizontal());
                assert(it_left.right_horizontal() == int(&it - il_right.intersections.data()));
            }
        }
    }
    for (size_t i_vline = 0; i_vline < segs.size(); ++ i_vline) {
        const SegmentedIntersectionLine &il = segs[i_vline];
        for (const SegmentIntersection &it : il.intersections) {
            auto i_it = int(&it - il.intersections.data());
            if (it.has_left_vertical_up()) {
                assert(il.intersections[it.left_vertical_up()].left_vertical_down() == i_it);
                assert(il.intersections[it.left_vertical_up()].prev_on_contour_quality == it.prev_on_contour_quality);
            }
            if (it.has_left_vertical_down()) {
                assert(il.intersections[it.left_vertical_down()].left_vertical_up() == i_it);
                assert(il.intersections[it.left_vertical_down()].prev_on_contour_quality == it.prev_on_contour_quality);
            }
            if (it.has_right_vertical_up()) {
                assert(il.intersections[it.right_vertical_up()].right_vertical_down() == i_it);
                assert(il.intersections[it.right_vertical_up()].next_on_contour_quality == it.next_on_contour_quality);
            }
            if (it.has_right_vertical_down()) {
                assert(il.intersections[it.right_vertical_down()].right_vertical_up() == i_it);
                assert(il.intersections[it.right_vertical_down()].next_on_contour_quality == it.next_on_contour_quality);
            }
        }
    }
    return true;
}
#endif
static void connect_segment_intersections_by_contours(
	const ExPolygonWithOffset &poly_with_offset, std::vector<SegmentedIntersectionLine> &segs,
	const FillParams &params, const coord_t link_max_length)
{
    for (size_t i_vline = 0; i_vline < segs.size(); ++ i_vline) {
	    SegmentedIntersectionLine       &il      = segs[i_vline];
	    const SegmentedIntersectionLine *il_prev = i_vline > 0 ? &segs[i_vline - 1] : nullptr;
	    const SegmentedIntersectionLine *il_next = i_vline + 1 < segs.size() ? &segs[i_vline + 1] : nullptr;

        for (int i_intersection = 0; i_intersection < int(il.intersections.size()); ++ i_intersection) {
		    SegmentIntersection &itsct   = il.intersections[i_intersection];
	        const Polygon 		&poly    = poly_with_offset.contour(itsct.iContour);
            const bool           forward = itsct.is_low();
		    int iprev  = -1;
            int d_prev = std::numeric_limits<int>::max();
		    if (il_prev) {
                for (int i = 0; i < int(il_prev->intersections.size()); ++ i) {
			        const SegmentIntersection &itsct2 = il_prev->intersections[i];
			        if (itsct.iContour == itsct2.iContour && itsct.type == itsct2.type) {
			            int d = distance_of_segmens(poly, itsct2.iSegment, itsct.iSegment, forward);
			            if (d < d_prev) {
			                iprev = i;
                            d_prev = d;
			            }
			        }
			    }
			}

		    int inext  = -1;
            int d_next = std::numeric_limits<int>::max();
            if (il_next) {
                for (int i = 0; i < int(il_next->intersections.size()); ++ i) {
			        const SegmentIntersection &itsct2 = il_next->intersections[i];
			        if (itsct.iContour == itsct2.iContour && itsct.type == itsct2.type) {
			            int d = distance_of_segmens(poly, itsct.iSegment, itsct2.iSegment, forward);
			            if (d < d_next) {
			                inext = i;
                            d_next = d;
			            }
			        }
			    }
			}

            bool same_prev = false;
            bool same_next = false;
            for (int i = 0; i < int(il.intersections.size()); ++ i)
                if (const SegmentIntersection &it2 = il.intersections[i];
                    i != i_intersection && it2.iContour == itsct.iContour && it2.type != itsct.type) {
                    int d = distance_of_segmens(poly, it2.iSegment, itsct.iSegment, forward);
                    if (d < d_prev) {
                        iprev     = i;
                        d_prev    = d;
                        same_prev = true;
                    }
                    d = distance_of_segmens(poly, itsct.iSegment, it2.iSegment, forward);
                    if (d < d_next) {
                        inext     = i;
                        d_next    = d;
                        same_next = true;
                    }
                }
            assert(iprev >= 0);
            assert(inext >= 0);

            itsct.prev_on_contour 	    = iprev;
            itsct.prev_on_contour_type  = same_prev ? 
                (iprev < i_intersection ? SegmentIntersection::LinkType::Down : SegmentIntersection::LinkType::Up) :
                SegmentIntersection::LinkType::Horizontal;
            itsct.next_on_contour 	    = inext;
            itsct.next_on_contour_type  = same_next ?
                (inext < i_intersection ? SegmentIntersection::LinkType::Down : SegmentIntersection::LinkType::Up) :
                SegmentIntersection::LinkType::Horizontal;

        	if (same_prev) {
        		SegmentIntersection *it  = &itsct;
        		SegmentIntersection *end = il.intersections.data() + iprev;
        		assert(it != end);
        		if (it > end)
        			std::swap(it, end);
                for (++ it; it != end; ++ it)
                    if (it->is_inner()) {
        				itsct.prev_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
                        break;
                    }
            }

        	if (same_next) {
        		SegmentIntersection *it  = &itsct;
        		SegmentIntersection *end = il.intersections.data() + inext;
        		assert(it != end);
        		if (it > end)
        			std::swap(it, end);
                for (++ it; it != end; ++ it)
                    if (it->is_inner()) {
        				itsct.next_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
                        break;
                    }
            }

            if (same_prev && same_next) {
            	assert(iprev != i_intersection);
            	assert(inext != i_intersection);
            	if ((iprev > i_intersection) == (inext > i_intersection)) {
	                itsct.prev_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
	                itsct.next_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
	            }
            }

			if (params.dont_connect()) {
				if (itsct.prev_on_contour_quality == SegmentIntersection::LinkQuality::Valid)
					itsct.prev_on_contour_quality = SegmentIntersection::LinkQuality::TooLong;
				if (itsct.next_on_contour_quality == SegmentIntersection::LinkQuality::Valid)
					itsct.next_on_contour_quality = SegmentIntersection::LinkQuality::TooLong;
			} else if (link_max_length > 0) {
				if (itsct.prev_on_contour_quality == SegmentIntersection::LinkQuality::Valid &&
            	    (same_prev ? 
            		 	measure_perimeter_segment_on_vertical_line_length(poly_with_offset, segs, i_vline, iprev, i_intersection, forward) :
            			measure_perimeter_horizontal_segment_length(poly_with_offset, segs, i_vline - 1, iprev, i_intersection)) > link_max_length)
	    			itsct.prev_on_contour_quality = SegmentIntersection::LinkQuality::TooLong;
				if (itsct.next_on_contour_quality == SegmentIntersection::LinkQuality::Valid &&
            		(same_next ?
            			measure_perimeter_segment_on_vertical_line_length(poly_with_offset, segs, i_vline, i_intersection, inext, forward) :
            			measure_perimeter_horizontal_segment_length(poly_with_offset, segs, i_vline, i_intersection, inext)) > link_max_length)
	    			itsct.next_on_contour_quality = SegmentIntersection::LinkQuality::TooLong;
            }
	    }

        for (int i_intersection = 0; i_intersection < int(il.intersections.size()); ++ i_intersection) {
		    SegmentIntersection &it = il.intersections[i_intersection];
            if (it.has_left_vertical() && it.prev_on_contour_quality == SegmentIntersection::LinkQuality::Invalid) {
			    SegmentIntersection &it2 = il.intersections[it.left_vertical()];
			    assert(it2.left_vertical() == i_intersection);
			    it2.prev_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
            }
            if (it.has_right_vertical() && it.next_on_contour_quality == SegmentIntersection::LinkQuality::Invalid) {
			    SegmentIntersection &it2 = il.intersections[it.right_vertical()];
			    assert(it2.right_vertical() == i_intersection);
			    it2.next_on_contour_quality = SegmentIntersection::LinkQuality::Invalid;
            }
		}
    }

    assert(validate_segment_intersection_connectivity(segs));
}

static void pinch_contours_insert_phony_outer_intersections(std::vector<SegmentedIntersectionLine> &segs)
{
    std::vector<size_t>                 insert_after;
    std::vector<int32_t>                map;
    std::vector<SegmentIntersection>    temp_intersections;

    for (size_t i_vline = 1; i_vline < segs.size(); ++ i_vline) {
        SegmentedIntersectionLine &il = segs[i_vline];
        assert(il.intersections.empty() || il.intersections.size() >= 2);
        if (! il.intersections.empty()) {
            assert(il.intersections.front().type == SegmentIntersection::OUTER_LOW);
            assert(il.intersections.back().type == SegmentIntersection::OUTER_HIGH);
            auto end = il.intersections.end() - 1;
            insert_after.clear();
            for (auto it = il.intersections.begin() + 1; it != end;) {
                if (it->type == SegmentIntersection::OUTER_HIGH) {
                    ++ it;
                    assert(it->type == SegmentIntersection::OUTER_LOW);
                    ++ it;
                } else {
                    assert(it->type == SegmentIntersection::INNER_LOW);
                    auto hi  = ++ it;
                    assert(hi->type == SegmentIntersection::INNER_HIGH);
                    auto lo2 = ++ it;
                    if (lo2->type == SegmentIntersection::INNER_LOW) {
                        int up = hi->vertical_up();
                        int dn = lo2->vertical_down();

                        assert(up == -1 || up > 0);
                        assert(dn == -1 || dn >= 0);
                        assert((up == -1 && dn == -1) || (dn + 1 == up));

                        bool pinched = dn + 1 != up;
                        if (pinched) {
#if 0
                            static int pinch_idx = 0;
                            printf("Pinched %d\n", pinch_idx++);
#endif
                            insert_after.emplace_back(hi - il.intersections.begin());
                        }
                    }
                }
            }

            if (! insert_after.empty()) {
                map.clear();
                {
                    size_t i = 0;
                    temp_intersections.clear();
                    for (size_t idx_inset_after : insert_after) {
                        for (; i <= idx_inset_after; ++ i) {
                            map.emplace_back(temp_intersections.size());
                            temp_intersections.emplace_back(il.intersections[i]);
                        }
                        coord_t pos = (temp_intersections.back().pos() + il.intersections[i].pos()) / 2;
                        temp_intersections.emplace_back(phony_outer_intersection(SegmentIntersection::OUTER_HIGH, pos));
                        temp_intersections.emplace_back(phony_outer_intersection(SegmentIntersection::OUTER_LOW, pos));
                    }
                    for (; i < il.intersections.size(); ++ i) {
                        map.emplace_back(temp_intersections.size());
                        temp_intersections.emplace_back(il.intersections[i]);
                    }
                    temp_intersections.swap(il.intersections);
                }
                for (SegmentIntersection &ip : il.intersections) {
                    if (ip.has_left_vertical())
                        ip.prev_on_contour = map[ip.prev_on_contour];
                    if (ip.has_right_vertical())
                        ip.next_on_contour = map[ip.next_on_contour];
                }
                for (SegmentIntersection &ip : segs[i_vline - 1].intersections)
                    if (ip.has_right_horizontal())
                        ip.next_on_contour = map[ip.next_on_contour];
                if (i_vline + 1 < segs.size()) {
                    for (SegmentIntersection &ip : segs[i_vline + 1].intersections)
                        if (ip.has_left_horizontal())
                            ip.prev_on_contour = map[ip.prev_on_contour];
                }
            }
        }
    }

    assert(validate_segment_intersection_connectivity(segs));
}

static const SegmentIntersection& end_of_vertical_run_raw(const SegmentIntersection &start)
{
	assert(start.type == SegmentIntersection::INNER_LOW);
    auto *it = &start;
    do {
        ++ it;
    } while (it->type != SegmentIntersection::OUTER_HIGH);
    if ((it - 1)->is_inner()) {
        -- it;
        assert(it->type == SegmentIntersection::INNER_HIGH);
    }
    return *it;
}

static const SegmentIntersection& end_of_vertical_run(const SegmentedIntersectionLine &il, const SegmentIntersection &start)
{
	assert(start.type == SegmentIntersection::INNER_LOW);
	const SegmentIntersection *end = &end_of_vertical_run_raw(start);
	assert(end->type == SegmentIntersection::INNER_HIGH);
	for (;;) {
		int up = end->vertical_up();
		if (up == -1 || (end->has_left_vertical_up() ? end->prev_on_contour_quality : end->next_on_contour_quality) != SegmentIntersection::LinkQuality::Valid)
			break;
		const SegmentIntersection &new_start = il.intersections[up];
		assert(end->iContour == new_start.iContour);
		assert(new_start.type == SegmentIntersection::INNER_LOW);
		end = &end_of_vertical_run_raw(new_start);
	}
	assert(end->type == SegmentIntersection::INNER_HIGH);
	return *end;
}
static SegmentIntersection& end_of_vertical_run(SegmentedIntersectionLine &il, SegmentIntersection &start)
{
	return const_cast<SegmentIntersection&>(end_of_vertical_run(std::as_const(il), std::as_const(start)));
}

static void traverse_graph_generate_polylines(const ExPolygonWithOffset              &poly_with_offset,
                                              const FillParams                       &params,
                                              std::vector<SegmentedIntersectionLine> &segs,
                                              const bool                              consistent_pattern,
                                              Polylines                              &polylines_out)
{
    for (int i_vline = 0; i_vline < int(segs.size()); ++ i_vline) {
        SegmentedIntersectionLine &vline = segs[i_vline];
        for (int i_intersection = 0; i_intersection + 1 < int(vline.intersections.size()); ++ i_intersection) {
            if (vline.intersections[i_intersection].type == SegmentIntersection::OUTER_LOW &&
                vline.intersections[i_intersection + 1].type == SegmentIntersection::OUTER_HIGH) {
                bool consumed = false;
                consumed = true;
                vline.intersections[i_intersection].consumed_vertical_up = consumed;
            }
        }
    }

    int    	  i_vline = 0;
    int    	  i_intersection = -1;
    Point     pointLast;
    Polyline* polyline_current = nullptr;
    if (! polylines_out.empty())
        pointLast = polylines_out.back().points.back();
    for (;;) {
        if (i_intersection == -1) {
            for (int i_vline2 = 0; i_vline2 < int(segs.size()); ++i_vline2) {
                const SegmentedIntersectionLine &vline = segs[i_vline2];
                if (!vline.intersections.empty()) {
                    assert(vline.intersections.size() > 1);
                    assert((vline.intersections.size() & 1) == 0);
                    assert(vline.intersections.front().type == SegmentIntersection::OUTER_LOW);

                    const bool forward_pass = !consistent_pattern || (i_vline2 % 2 == 0);
                    for (int i = 0; i < int(vline.intersections.size()); ++i) {
                        const int                  intrsctn_idx = forward_pass ? i : int(vline.intersections.size()) - i - 1;
                        const SegmentIntersection &intrsctn     = vline.intersections[intrsctn_idx];
                        if (intrsctn.is_outer()) {
                            assert(intrsctn.is_low() || intrsctn_idx > 0);
                            const bool consumed = intrsctn.is_low() ? intrsctn.consumed_vertical_up : vline.intersections[intrsctn_idx - 1].consumed_vertical_up;
                            if (!consumed) {
                                i_vline        = i_vline2;
                                i_intersection = intrsctn_idx;
                                goto found;
                            }
                        }
                    }
                }
            }
            if (i_intersection == -1)
                break;
        found:
            polylines_out.push_back(Polyline());
            polyline_current = &polylines_out.back();
            pointLast = Point(segs[i_vline].pos, segs[i_vline].intersections[i_intersection].pos());
            polyline_current->points.push_back(pointLast);
        }

        SegmentedIntersectionLine &vline 		= segs[i_vline];
        SegmentIntersection 	  *it 			= &vline.intersections[i_intersection];
        bool 					   going_up 	= it->is_low();
        bool 					   try_connect 	= false;
        if (going_up) {
            assert(! it->consumed_vertical_up);
            assert(size_t(i_intersection + 1) < vline.intersections.size());
            if (it->is_inner()) {
                assert(i_intersection > 0);
                -- it;
                -- i_intersection;
            }
            do {
                it->consumed_vertical_up = true;
                ++ it;
                ++ i_intersection;
                assert(size_t(i_intersection) < vline.intersections.size());
            } while (it->type != SegmentIntersection::OUTER_HIGH);
            if ((it - 1)->is_inner()) {
                -- it;
                -- i_intersection;
                assert(it->type == SegmentIntersection::INNER_HIGH);
                try_connect = true;
            }
        } else {
            assert(it->is_high());
            assert(i_intersection > 0);
            assert(!(it - 1)->consumed_vertical_up);
            if (it->is_inner())
                it->consumed_vertical_up = true;
            do {
                assert(i_intersection > 0);
                -- it;
                -- i_intersection;
                it->consumed_vertical_up = true;
            } while (it->type != SegmentIntersection::OUTER_LOW);
            if ((it + 1)->is_inner()) {
                ++ it;
                ++ i_intersection;
                assert(it->type == SegmentIntersection::INNER_LOW);
                try_connect = true;
            }
        }
        if (try_connect) {
        	int  i_prev = it->left_horizontal();
        	int  i_next = it->right_horizontal();

            bool intersection_prev_valid = intersection_on_prev_vertical_line_valid(segs, i_vline, i_intersection) && !consistent_pattern;
            bool intersection_next_valid = intersection_on_next_vertical_line_valid(segs, i_vline, i_intersection);
            bool intersection_horizontal_valid = intersection_prev_valid || intersection_next_valid;

            if (i_prev != -1)
                segs[i_vline - 1].intersections[i_prev].consumed_perimeter_right = true;
            if (i_next != -1)
                it->consumed_perimeter_right = true;

            if (intersection_horizontal_valid) {
	            assert(it->is_inner());
            	bool take_next = intersection_next_valid;
            	if (intersection_prev_valid && intersection_next_valid) {
            		coordf_t dist_prev = measure_perimeter_horizontal_segment_length(poly_with_offset, segs, i_vline - 1, i_prev, i_intersection);
	                coordf_t dist_next = measure_perimeter_horizontal_segment_length(poly_with_offset, segs, i_vline, i_intersection, i_next);
	                take_next = dist_next < dist_prev;
	            }
                polyline_current->points.emplace_back(vline.pos, it->pos());
                emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline, it->iContour, i_intersection, take_next ? i_next : i_prev, *polyline_current, take_next);
                if (take_next) {
                    ++ i_vline;
                    i_intersection = i_next;
                }
                else {
                    -- i_vline;
                    i_intersection = i_prev;
                }
                continue;
            }

            int i_vertical = it->vertical_outside();
            auto vertical_link_quality = (i_vertical == -1 || vline.intersections[i_vertical + (going_up ? 0 : -1)].consumed_vertical_up) ? 
            	SegmentIntersection::LinkQuality::Invalid : it->vertical_outside_quality();
#if 0            	
            if (vertical_link_quality == SegmentIntersection::LinkQuality::Valid ||
            	(! intersection_horizontal_valid && vertical_link_quality != SegmentIntersection::LinkQuality::Invalid)) {
#else
           	if (vertical_link_quality != SegmentIntersection::LinkQuality::Invalid) {
#endif
                assert(it->iContour == vline.intersections[i_vertical].iContour);
                polyline_current->points.emplace_back(vline.pos, it->pos());
                if (vertical_link_quality == SegmentIntersection::LinkQuality::Valid)
                    emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, it->iContour, i_intersection, i_vertical,
                        *polyline_current, going_up ? it->has_left_vertical_up() : it->has_right_vertical_down());
                else {
                    polylines_out.emplace_back();
                    polyline_current = &polylines_out.back();
                    polyline_current->points.emplace_back(vline.pos, vline.intersections[i_vertical].pos());
                }
                if (going_up)
                    for (int i = i_intersection; i < i_vertical; ++i)
                        vline.intersections[i].consumed_vertical_up = true;
                else
                    for (int i = i_vertical; i < i_intersection; ++i)
                        vline.intersections[i].consumed_vertical_up = true;
                it->consumed_perimeter_right = true;
                (going_up ? ++it : --it)->consumed_perimeter_right = true;
                i_intersection = i_vertical;
                continue;
            }

            going_up ? ++ it : -- it;
        }

        assert(it->is_outer());
        assert(it->is_high() == going_up);
        pointLast = Point(vline.pos, it->pos());
        polyline_current->points.emplace_back(pointLast);
        polyline_current->remove_duplicate_points();
        assert(! polyline_current->has_duplicate_points());
        if (polyline_current->points.size() <= 1 ||
            (polyline_current->points.size() == 2 &&
                std::abs(polyline_current->points.front()(0) - polyline_current->points.back()(0)) < SCALED_EPSILON &&
                std::abs(polyline_current->points.front()(1) - polyline_current->points.back()(1)) < SCALED_EPSILON))
            polylines_out.pop_back();
        it 				 = nullptr;
        i_intersection   = -1;
        polyline_current = nullptr;
    }
}

struct MonotonicRegion
{
    struct Boundary {
        int vline;
        int low;
        int high;
    };

    Boundary 	left;
    Boundary 	right;

    float 		len1 { 0.f };
    float 		len2 { 0.f };
    bool 		flips { false };

    float       length(bool region_flipped) const { return region_flipped ? len2 : len1; }
    int 		left_intersection_point(bool region_flipped) const { return region_flipped ? left.high : left.low; }
    int 		right_intersection_point(bool region_flipped) const { return (region_flipped == flips) ? right.low : right.high; }

#if NDEBUG
    boost::container::small_vector<MonotonicRegion*, 4>	left_neighbors;
    boost::container::small_vector<MonotonicRegion*, 4>	right_neighbors;
#else
    std::vector<MonotonicRegion*> left_neighbors;
    std::vector<MonotonicRegion*> right_neighbors;
#endif
};

struct AntPath
{
	float length 	 { -1. };
	float visibility { -1. };
	float pheromone  { 0 };
};

struct MonotonicRegionLink
{
    MonotonicRegion     *region;
    bool 				 flipped;
    AntPath 			*next;
    AntPath 			*next_flipped;
};

class AntPathMatrix
{
public:
	AntPathMatrix(
		const std::vector<MonotonicRegion> 			    &regions, 
		const ExPolygonWithOffset 						&poly_with_offset, 
		const std::vector<SegmentedIntersectionLine> 	&segs,
		const float 									 initial_pheromone) : 
		m_regions(regions),
		m_poly_with_offset(poly_with_offset),
		m_segs(segs),
		m_matrix(regions.size() * regions.size() * 4, AntPath{ -1., -1., initial_pheromone}) {}

	void update_inital_pheromone(float initial_pheromone)
	{
		for (AntPath &ap : m_matrix)
			ap.pheromone = initial_pheromone;
	}

	AntPath& operator()(const MonotonicRegion &region_from, bool flipped_from, const MonotonicRegion &region_to, bool flipped_to)
	{
		int row = 2 * int(&region_from - m_regions.data()) + flipped_from;
		int col = 2 * int(&region_to   - m_regions.data()) + flipped_to;
		AntPath &path = m_matrix[row * m_regions.size() * 2 + col];
		if (path.length == -1.) {
			int i_from = region_from.right_intersection_point(flipped_from);
			int i_to   = region_to.left_intersection_point(flipped_to);
			const SegmentedIntersectionLine &vline_from = m_segs[region_from.right.vline];
			const SegmentedIntersectionLine &vline_to   = m_segs[region_to.left.vline];
			if (region_from.right.vline + 1 == region_from.left.vline) {
				int i_right = vline_from.intersections[i_from].right_horizontal();
				if (i_right == i_to && vline_from.intersections[i_from].next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
	                path.length = unscale<float>(measure_perimeter_horizontal_segment_length(m_poly_with_offset, m_segs, region_from.right.vline, i_from, i_to));
				}
			}
			if (path.length == -1.) {
			    path.length = unscale<float>(Vec2f(vline_to.pos - vline_from.pos, vline_to.intersections[i_to].pos() - vline_from.intersections[i_from].pos()).norm());
			}
			path.visibility = 1.f / (path.length + float(EPSILON));
		}
		return path;
	}

	AntPath& operator()(const MonotonicRegionLink &region_from, const MonotonicRegion &region_to, bool flipped_to)
		{ return (*this)(*region_from.region, region_from.flipped, region_to, flipped_to); }
	AntPath& operator()(const MonotonicRegion &region_from, bool flipped_from, const MonotonicRegionLink &region_to)
		{ return (*this)(region_from, flipped_from, *region_to.region, region_to.flipped); }
    AntPath& operator()(const MonotonicRegionLink &region_from, const MonotonicRegionLink &region_to)
        { return (*this)(*region_from.region, region_from.flipped, *region_to.region, region_to.flipped); }

private:
	const std::vector<MonotonicRegion>    			&m_regions;
	const ExPolygonWithOffset 						&m_poly_with_offset;
	const std::vector<SegmentedIntersectionLine> 	&m_segs;
	std::vector<AntPath>					         m_matrix;
};

static const SegmentIntersection& vertical_run_bottom(const SegmentedIntersectionLine &vline, const SegmentIntersection &start)
{
	assert(start.is_inner());
	const SegmentIntersection *it = &start;
	for (;;) {
		while (it->type != SegmentIntersection::INNER_LOW)
			-- it;
        if ((it - 1)->type == SegmentIntersection::INNER_HIGH)
            -- it;
        else {
            int down = it->vertical_down();
            if (down == -1 || it->vertical_down_quality() != SegmentIntersection::LinkQuality::Valid)
                break;
            it = &vline.intersections[down];
            assert(it->type == SegmentIntersection::INNER_HIGH);
        }
	}
	return *it;
}
static SegmentIntersection& vertical_run_bottom(SegmentedIntersectionLine& vline, SegmentIntersection& start)
{
    return const_cast<SegmentIntersection&>(vertical_run_bottom(std::as_const(vline), std::as_const(start)));
}

static const SegmentIntersection& vertical_run_top(const SegmentedIntersectionLine &vline, const SegmentIntersection &start)
{
	assert(start.is_inner());
	const SegmentIntersection *it = &start;
	for (;;) {
		while (it->type != SegmentIntersection::INNER_HIGH)
			++ it;
        if ((it + 1)->type == SegmentIntersection::INNER_LOW)
            ++ it;
        else {
            int up = it->vertical_up();
            if (up == -1 || it->vertical_up_quality() != SegmentIntersection::LinkQuality::Valid)
                break;
            it = &vline.intersections[up];
            assert(it->type == SegmentIntersection::INNER_LOW);
        }
	}
	return *it;
}
static SegmentIntersection& vertical_run_top(SegmentedIntersectionLine& vline, SegmentIntersection& start)
{
    return const_cast<SegmentIntersection&>(vertical_run_top(std::as_const(vline), std::as_const(start)));
}

static SegmentIntersection* overlap_bottom(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_other, SegmentIntersection::Side side)
{
	SegmentIntersection *other = nullptr;
    assert(start.is_inner());
    assert(end.is_inner());
    const SegmentIntersection *it = &start;
    for (;;) {
        if (it->is_inner()) {
            int i = it->horizontal(side);
            if (i != -1) {
                other = &vline_other.intersections[i];
                break;
            }
            if (it == &end)
                break;
        }
        if (it->type != SegmentIntersection::INNER_HIGH)
            ++ it;
        else if ((it + 1)->type == SegmentIntersection::INNER_LOW)
            ++ it;
        else {
            int up = it->vertical_up();
            if (up == -1 || it->vertical_up_quality() != SegmentIntersection::LinkQuality::Valid)
                break;
            it = &vline_this.intersections[up];
            assert(it->type == SegmentIntersection::INNER_LOW);
        }
    }
	return other == nullptr ? nullptr : &vertical_run_bottom(vline_other, *other);
}

static SegmentIntersection* overlap_top(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_other, SegmentIntersection::Side side)
{
    SegmentIntersection *other = nullptr;
    assert(start.is_inner());
    assert(end.is_inner());
    const SegmentIntersection *it = &end;
    for (;;) {
        if (it->is_inner()) {
            int i = it->horizontal(side);
            if (i != -1) {
                other = &vline_other.intersections[i];
                break;
            }
            if (it == &start)
                break;
        }
        if (it->type != SegmentIntersection::INNER_LOW)
            -- it;
        else if ((it - 1)->type == SegmentIntersection::INNER_HIGH)
            -- it;
        else {
            int down = it->vertical_down();
            if (down == -1 || it->vertical_down_quality() != SegmentIntersection::LinkQuality::Valid)
                break;
            it = &vline_this.intersections[down];
            assert(it->type == SegmentIntersection::INNER_HIGH);
        }
    }
	return other == nullptr ? nullptr : &vertical_run_top(vline_other, *other);
}

static std::pair<SegmentIntersection*, SegmentIntersection*> left_overlap(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_left)
{
	std::pair<SegmentIntersection*, SegmentIntersection*> out(nullptr, nullptr);
	out.first = overlap_bottom(start, end, vline_this, vline_left, SegmentIntersection::Side::Left);
	if (out.first != nullptr)
		out.second = overlap_top(start, end, vline_this, vline_left, SegmentIntersection::Side::Left);
    assert((out.first == nullptr && out.second == nullptr) || out.first < out.second);
	return out;
}

static std::pair<SegmentIntersection*, SegmentIntersection*> left_overlap(std::pair<SegmentIntersection*, SegmentIntersection*> &start_end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_left)
{
	assert((start_end.first == nullptr) == (start_end.second == nullptr));
	return start_end.first == nullptr ? start_end : left_overlap(*start_end.first, *start_end.second, vline_this, vline_left);
}

static std::pair<SegmentIntersection*, SegmentIntersection*> right_overlap(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_right)
{
	std::pair<SegmentIntersection*, SegmentIntersection*> out(nullptr, nullptr);
	out.first = overlap_bottom(start, end, vline_this, vline_right, SegmentIntersection::Side::Right);
	if (out.first != nullptr)
		out.second = overlap_top(start, end, vline_this, vline_right, SegmentIntersection::Side::Right);
    assert((out.first == nullptr && out.second == nullptr) || out.first < out.second);
    return out;
}

static std::pair<SegmentIntersection*, SegmentIntersection*> right_overlap(std::pair<SegmentIntersection*, SegmentIntersection*> &start_end, SegmentedIntersectionLine &vline_this, SegmentedIntersectionLine &vline_right)
{
	assert((start_end.first == nullptr) == (start_end.second == nullptr));
	return start_end.first == nullptr ? start_end : right_overlap(*start_end.first, *start_end.second, vline_this, vline_right);
}

static std::vector<MonotonicRegion> generate_montonous_regions(std::vector<SegmentedIntersectionLine> &segs)
{
	std::vector<MonotonicRegion> monotonic_regions;

#ifndef NDEBUG
	#define SLIC3R_DEBUG_MONOTONOUS_REGIONS
#endif

#ifdef SLIC3R_DEBUG_MONOTONOUS_REGIONS
    std::vector<std::vector<std::pair<int, int>>> consumed(segs.size());
    auto test_overlap = [&consumed](int segment, int low, int high) {
        for (const std::pair<int, int>& interval : consumed[segment])
            if ((low >= interval.first && low <= interval.second) ||
                (interval.first >= low && interval.first <= high))
                return true;
        consumed[segment].emplace_back(low, high);
        return false;
    };
#else
    [[maybe_unused]] auto test_overlap = [](int, int, int) { return false; };
#endif

    for (int i_vline_seed = 0; i_vline_seed < int(segs.size()); ++ i_vline_seed) {
        SegmentedIntersectionLine  &vline_seed = segs[i_vline_seed];
        for (int i_intersection_seed = 1; i_intersection_seed + 1 < int(vline_seed.intersections.size()); ) {
            while (i_intersection_seed < int(vline_seed.intersections.size()) &&
	        	   vline_seed.intersections[i_intersection_seed].type != SegmentIntersection::INNER_LOW)
	        	++ i_intersection_seed;
            if (i_intersection_seed == int(vline_seed.intersections.size()))
                break;
			SegmentIntersection *start = &vline_seed.intersections[i_intersection_seed];
            SegmentIntersection *end   = &end_of_vertical_run(vline_seed, *start);
			if (! start->consumed_vertical_up) {
		        int i_vline = i_vline_seed;
                std::pair<SegmentIntersection*, SegmentIntersection*> left(start, end);
				MonotonicRegion region;
				region.left.vline = i_vline;
				region.left.low   = int(left.first  - vline_seed.intersections.data());
				region.left.high  = int(left.second - vline_seed.intersections.data());
				region.right      = region.left;
                assert(! test_overlap(region.left.vline, region.left.low, region.left.high));
				start->consumed_vertical_up = true;
				int num_lines = 1;
                while (++ i_vline < int(segs.size())) {
			        SegmentedIntersectionLine  &vline_left	= segs[i_vline - 1];
			        SegmentedIntersectionLine  &vline_right = segs[i_vline];
					std::pair<SegmentIntersection*, SegmentIntersection*> right 	      = right_overlap(left, vline_left, vline_right);
                    if (right.first == nullptr)
                        break;
                    SegmentIntersection*                                  right_top_first = &vertical_run_top(vline_right, *right.first);
                    if (right_top_first != right.second)
                        break;
                    std::pair<SegmentIntersection*, SegmentIntersection*> right_left      = left_overlap(right, vline_right, vline_left);
					if (left != right_left)
						break;
					region.right.vline = i_vline;
					region.right.low   = int(right.first  - vline_right.intersections.data());
					region.right.high  = int(right.second - vline_right.intersections.data());
					right.first->consumed_vertical_up = true;
                    assert(! test_overlap(region.right.vline, region.right.low, region.right.high));
                    ++ num_lines;
					left = right;
				}
				region.flips = (num_lines & 1) != 0;
                monotonic_regions.emplace_back(region);
			}
			i_intersection_seed = int(end - vline_seed.intersections.data()) + 1;
		}
    }

    return monotonic_regions;
}

#ifdef INFILL_DEBUG_OUTPUT
static void export_monotonous_regions_to_svg(
    const ExPolygonWithOffset                       &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>    &segs,
    const std::vector<MonotonicRegion>              &monotonic_regions,
    const std::string                               &path)
{
    BoundingBox bbox = get_extents(poly_with_offset.polygons_src);
    bbox.offset(scale_(3.));

    ::Slic3r::SVG svg(path, bbox);
    svg.draw(poly_with_offset.polygons_src);
    svg.draw_outline(poly_with_offset.polygons_src, "green");
    svg.draw_outline(poly_with_offset.polygons_outer, "green");
    svg.draw_outline(poly_with_offset.polygons_inner, "green");

    for (const SegmentedIntersectionLine &sil : segs) {
        for (size_t i = 0; i + 1 < sil.intersections.size(); ++ i)
            if (sil.intersections[i].type == SegmentIntersection::INNER_LOW && sil.intersections[i + 1].type == SegmentIntersection::INNER_HIGH) {
                Line l(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[i + 1].pos()));
                svg.draw(l, "blue");
            } else if (sil.intersections[i].type == SegmentIntersection::INNER_HIGH && sil.intersections[i].has_vertical_up()) {
                std::string color;
                const SegmentIntersection *it = &sil.intersections[i];
                switch (it->vertical_up_quality()) {
                case SegmentIntersection::LinkQuality::Invalid:     color = "red"; break;
                case SegmentIntersection::LinkQuality::Valid:       color = "blue"; break;
                case SegmentIntersection::LinkQuality::TooLong:
                default:                                            color = "yellow"; break;
                }
                Polyline polyline;
                polyline.points.push_back({ sil.pos, it->pos() });
                emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, &sil - segs.data() , it->iContour, it - sil.intersections.data(), it->vertical_up(), polyline, it->has_left_vertical_up());
                svg.draw(polyline, color, scale_(0.05));
            }
    }

    for (const MonotonicRegion &region : monotonic_regions) {
        auto draw_boundary_line = [&poly_with_offset, &segs, &svg](const MonotonicRegion::Boundary &boundary) {
            const SegmentedIntersectionLine &sil = segs[boundary.vline];
            for (size_t i = boundary.low; i < boundary.high; ++ i)
                if (sil.intersections[i].type == SegmentIntersection::INNER_LOW && sil.intersections[i + 1].type == SegmentIntersection::INNER_HIGH) {
                    Line l(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[i + 1].pos()));
                    svg.draw(l, "red", scale_(0.05));
                }
        };
        draw_boundary_line(region.left);
        draw_boundary_line(region.right);
    }
}
#endif
static float montonous_region_path_length(const MonotonicRegion &region, bool dir, const ExPolygonWithOffset &poly_with_offset, const std::vector<SegmentedIntersectionLine> &segs)
{
	int   i_intersection = region.left_intersection_point(dir);
	int   i_vline 		 = region.left.vline;
	float total_length   = 0.;
	bool  no_perimeter   = false;
	Vec2f last_point;

	for (;;) {
        const SegmentedIntersectionLine &vline 		= segs[i_vline];
        const SegmentIntersection       *it    		= &vline.intersections[i_intersection];
        const bool                       going_up   = it->is_low();

        if (no_perimeter)
        	total_length += (last_point - Vec2f(vline.pos, (it + (going_up ? - 1 : 1))->pos())).norm();

        int iright = it->right_horizontal();
        if (going_up) {
            for (;;) {
	            do {
	                ++ it;
					iright = std::max(iright, it->right_horizontal());
                    assert(it->is_inner());
                } while (it->type != SegmentIntersection::INNER_HIGH || (it + 1)->type != SegmentIntersection::OUTER_HIGH);
	            int inext = it->vertical_up();
                if (inext == -1 || it->vertical_up_quality() != SegmentIntersection::LinkQuality::Valid)
	            	break;
                assert(it->iContour == vline.intersections[inext].iContour);
                it = vline.intersections.data() + inext;
            } 
        } else {
            assert(it->is_high());
            assert(i_intersection > 0);
            for (;;) {
	            do {
	                -- it;
	                if (int iright_new = it->right_horizontal(); iright_new != -1)
	                	iright = iright_new;
                    assert(it->is_inner());
	            } while (it->type != SegmentIntersection::INNER_LOW || (it - 1)->type != SegmentIntersection::OUTER_LOW);
	            int inext = it->vertical_down();
	            if (inext == -1 || it->vertical_down_quality() != SegmentIntersection::LinkQuality::Valid)
	            	break;
                assert(it->iContour == vline.intersections[inext].iContour);
                it = vline.intersections.data() + inext;
            } 
        }

        if (i_vline == region.right.vline)
        	break;

        int inext = it->right_horizontal();
        if (iright == -1)
            break;
        assert(inext == -1 || inext == iright);

        const SegmentedIntersectionLine &vline_right = segs[i_vline + 1];
        const SegmentIntersection       *right       = going_up ? 
            &vertical_run_top(vline_right, vline_right.intersections[iright]) : &vertical_run_bottom(vline_right, vline_right.intersections[iright]);
        i_intersection = int(right - vline_right.intersections.data());

        if (inext == i_intersection && it->next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
    		total_length += 0.5f * float(measure_perimeter_horizontal_segment_length(poly_with_offset, segs, i_vline, it - vline.intersections.data(), inext));
			no_perimeter = false;
        } else {
        	going_up ? ++ it : -- it;
	        assert(it->is_outer());
	        assert(it->is_high() == going_up);
			last_point = Vec2f(vline.pos, it->pos());
			no_perimeter = true;
        }

        ++ i_vline;
    }

    return unscale<float>(total_length);
}

static void connect_monotonic_regions(std::vector<MonotonicRegion> &regions, const ExPolygonWithOffset &poly_with_offset, std::vector<SegmentedIntersectionLine> &segs)
{
	using MapType = std::pair<SegmentIntersection*, MonotonicRegion*>;
	std::vector<MapType> map_intersection_to_region_start;
	std::vector<MapType> map_intersection_to_region_end;
	map_intersection_to_region_start.reserve(regions.size());
	map_intersection_to_region_end.reserve(regions.size());
	for (MonotonicRegion &region : regions) {
		map_intersection_to_region_start.emplace_back(&segs[region.left.vline].intersections[region.left.low], &region);
		map_intersection_to_region_end.emplace_back(&segs[region.right.vline].intersections[region.right.low], &region);
	}
	auto intersections_lower = [](const MapType &l, const MapType &r){ return l.first < r.first ; };
    std::sort(map_intersection_to_region_start.begin(), map_intersection_to_region_start.end(), intersections_lower);
	std::sort(map_intersection_to_region_end.begin(), map_intersection_to_region_end.end(), intersections_lower);

	for (MonotonicRegion &region : regions) {
		if (region.left.vline > 0) {
			auto &vline = segs[region.left.vline];
            auto &vline_left = segs[region.left.vline - 1];
            auto[lbegin, lend] = left_overlap(vline.intersections[region.left.low], vline.intersections[region.left.high], vline, vline_left);
            if (lbegin != nullptr) {
                for (;;) {
                    MapType key(lbegin, nullptr);
                    auto it = std::lower_bound(map_intersection_to_region_end.begin(), map_intersection_to_region_end.end(), key);
                    assert(it != map_intersection_to_region_end.end() && it->first == key.first);
				    it->second->right_neighbors.emplace_back(&region);
				    SegmentIntersection *lnext = &vertical_run_top(vline_left, *lbegin);
				    if (lnext == lend)
					    break;
				    while (lnext->type != SegmentIntersection::INNER_LOW)
					    ++ lnext;
				    lbegin = lnext;
			    }
            }
		}
        if (region.right.vline + 1 < int(segs.size())) {
			auto &vline = segs[region.right.vline];
            auto &vline_right = segs[region.right.vline + 1];
            auto [rbegin, rend] = right_overlap(vline.intersections[region.right.low], vline.intersections[region.right.high], vline, vline_right);
            if (rbegin != nullptr) {
			    for (;;) {
				    MapType key(rbegin, nullptr);
				    auto it = std::lower_bound(map_intersection_to_region_start.begin(), map_intersection_to_region_start.end(), key);
				    assert(it != map_intersection_to_region_start.end() && it->first == key.first);
				    it->second->left_neighbors.emplace_back(&region);
				    SegmentIntersection *rnext = &vertical_run_top(vline_right, *rbegin);
				    if (rnext == rend)
					    break;
				    while (rnext->type != SegmentIntersection::INNER_LOW)
					    ++ rnext;
				    rbegin = rnext;
			    }
            }
		}
	}

    for (MonotonicRegion &region : regions) {
        std::sort(region.left_neighbors.begin(),  region.left_neighbors.end());
        std::sort(region.right_neighbors.begin(), region.right_neighbors.end());
    }
    for (MonotonicRegion &region : regions) {
        for (MonotonicRegion *neighbor : region.left_neighbors) {
            auto it = std::lower_bound(neighbor->right_neighbors.begin(), neighbor->right_neighbors.end(), &region);
            if (it == neighbor->right_neighbors.end() || *it != &region)
                neighbor->right_neighbors.insert(it, &region);
        }
        for (MonotonicRegion *neighbor : region.right_neighbors) {
            auto it = std::lower_bound(neighbor->left_neighbors.begin(), neighbor->left_neighbors.end(), &region);
            if (it == neighbor->left_neighbors.end() || *it != &region)
                neighbor->left_neighbors.insert(it, &region);
        }
    }

#ifndef NDEBUG
    for (MonotonicRegion &region : regions) {
        for (MonotonicRegion *neighbor : region.left_neighbors) {
            assert(std::count(region.left_neighbors.begin(), region.left_neighbors.end(), neighbor) == 1);
            assert(std::find(neighbor->right_neighbors.begin(), neighbor->right_neighbors.end(), &region) != neighbor->right_neighbors.end());
        }
        for (MonotonicRegion *neighbor : region.right_neighbors) {
            assert(std::count(region.right_neighbors.begin(), region.right_neighbors.end(), neighbor) == 1);
            assert(std::find(neighbor->left_neighbors.begin(), neighbor->left_neighbors.end(), &region) != neighbor->left_neighbors.end());
        }
    }
#endif
    for (MonotonicRegion &region : regions) {
    	region.len1 = montonous_region_path_length(region, false, poly_with_offset, segs);
    	region.len2 = montonous_region_path_length(region, true,  poly_with_offset, segs);
    	if (region.len1 > region.len2) {
    		region.len1 -= region.len2;
    		region.len2 = 0;
    	} else {
    		region.len2 -= region.len1;
    		region.len1 = 0;
    	}
    }
}

void monotonic_3_opt(std::vector<MonotonicRegionLink> &path, const std::vector<SegmentedIntersectionLine> &segs)
{
}

template<typename... TArgs>
inline void print_ant(const std::string& fmt, TArgs&&... args) {
#ifdef SLIC3R_DEBUG_ANTS
    std::cout << Slic3r::format(fmt, std::forward<TArgs>(args)...) << std::endl;
#endif
}

static std::vector<MonotonicRegionLink> chain_monotonic_regions(
	std::vector<MonotonicRegion> &regions, const ExPolygonWithOffset &poly_with_offset, const std::vector<SegmentedIntersectionLine> &segs, std::mt19937_64 &rng)
{
	std::vector<int32_t>			left_neighbors_unprocessed(regions.size(), 1);
	std::vector<MonotonicRegion*> 	queue;
	queue.reserve(regions.size());
	for (MonotonicRegion &region : regions)
		if (region.left_neighbors.empty())
			queue.emplace_back(&region);
		else
			left_neighbors_unprocessed[&region - regions.data()] += int(region.left_neighbors.size());
	auto left_neighbors_unprocessed_initial = left_neighbors_unprocessed;
	auto queue_initial 						= queue;

	std::vector<MonotonicRegionLink> path, best_path;
	path.reserve(regions.size());
	best_path.reserve(regions.size());
	float best_path_length = std::numeric_limits<float>::max();

	struct NextCandidate {
        MonotonicRegion    *region = nullptr;
        AntPath  	        *link;
        AntPath  	        *link_flipped;
        float                probability;
        bool 		         dir = false;
	};
	std::vector<NextCandidate> next_candidates;

    [[maybe_unused]]auto validate_unprocessed =
#ifdef NDEBUG
        []() { return true; };
#else
        [&regions, &left_neighbors_unprocessed, &path, &queue]() {
            std::vector<unsigned char> regions_processed(regions.size(), false);
            std::vector<unsigned char> regions_in_queue(regions.size(), false);
            for (const MonotonicRegion *region : queue) {
                assert(left_neighbors_unprocessed[region - regions.data()] == 1);
                regions_in_queue[region - regions.data()] = true;
            }
            for (const MonotonicRegionLink &link : path) {
                assert(left_neighbors_unprocessed[link.region - regions.data()] == 0);
                regions_processed[link.region - regions.data()] = true;
            }
            for (size_t i = 0; i < regions_processed.size(); ++ i) {
                assert(! regions_processed[i] || ! regions_in_queue[i]);
                const MonotonicRegion &region = regions[i];
                if (regions_processed[i] || regions_in_queue[i]) {
                    assert(left_neighbors_unprocessed[i] == (regions_in_queue[i] ? 1 : 0));
                    for (const MonotonicRegion *left : region.left_neighbors) {
                        assert(regions_processed[left - regions.data()]);
                        assert(left_neighbors_unprocessed[left - regions.data()] == 0);
                    }
                } else {
                    assert(left_neighbors_unprocessed[i] > 1);
                    int32_t num_predecessors_unprocessed = 0;
                    bool   has_left_last_on_path       = false;
                    for (const MonotonicRegion* left : region.left_neighbors) {
                        size_t iprev = left - regions.data();
                        if (regions_processed[iprev]) {
                        	assert(left_neighbors_unprocessed[iprev] == 0);
                            if (left == path.back().region) {
                                assert(! has_left_last_on_path);
                                has_left_last_on_path = true;
                                ++ num_predecessors_unprocessed;
                            }
                        } else {
                        	if (regions_in_queue[iprev])
	                    		assert(left_neighbors_unprocessed[iprev] == 1);
	                    	else 
	                    		assert(left_neighbors_unprocessed[iprev] > 1);
	                    	++ num_predecessors_unprocessed;
                        }
                    }
                    assert(num_predecessors_unprocessed > 0);
                    assert(left_neighbors_unprocessed[i] == num_predecessors_unprocessed + 1);
                }
            }
            return true;
        };
#endif
	constexpr int const   num_rounds = 25;
	constexpr int const   num_rounds_no_change_exit = 8;
	const int             num_ants = std::min(int(regions.size()), 10);
	float                 pheromone_initial_deposit = 0.5f;
	constexpr float const pheromone_evaporation = 0.1f;
    constexpr float const pheromone_diversification = 0.1f;
	constexpr float const probability_take_best = 0.9f;
	constexpr float const pheromone_alpha = 1.f;
	constexpr float const pheromone_beta  = 2.f;

    AntPathMatrix path_matrix(regions, poly_with_offset, segs, pheromone_initial_deposit);

    {
		queue = queue_initial;
		left_neighbors_unprocessed = left_neighbors_unprocessed_initial;
        assert(validate_unprocessed());
        MonotonicRegionLink path_end { queue.back(), false };
        queue.pop_back();
        -- left_neighbors_unprocessed[path_end.region - regions.data()];

        float total_length = path_end.region->length(false);
		while (! queue.empty() || ! path_end.region->right_neighbors.empty()) {
			MonotonicRegion 		    &region = *path_end.region;
			bool 			  			 dir    = path_end.flipped;
			NextCandidate 				 next_candidate;
			next_candidate.probability = 0;
			for (MonotonicRegion *next : region.right_neighbors) {
                assert(left_neighbors_unprocessed[next - regions.data()] > 1);
				if (left_neighbors_unprocessed[next - regions.data()] == 2) {
                    AntPath &path1 = path_matrix(region, dir, *next, false);
                    AntPath &path2 = path_matrix(region, dir, *next, true);
                    if (path1.visibility > next_candidate.probability)
                        next_candidate = { next, &path1, &path1, path1.visibility, false };
                    if (path2.visibility > next_candidate.probability)
                        next_candidate = { next, &path2, &path2, path2.visibility, true  };
				}
			}
			bool from_queue = next_candidate.probability == 0;
            if (from_queue) {
                for (MonotonicRegion *next : queue) {
                    AntPath &path1 = path_matrix(region, dir, *next, false);
                    AntPath &path2 = path_matrix(region, dir, *next, true);
                    if (path1.visibility > next_candidate.probability)
                        next_candidate = { next, &path1, &path1, path1.visibility, false };
                    if (path2.visibility > next_candidate.probability)
                        next_candidate = { next, &path2, &path2, path2.visibility, true  };
                }
            }
			for (MonotonicRegion *next : region.right_neighbors)
				if (-- left_neighbors_unprocessed[next - regions.data()] == 1 && next_candidate.region != next)
	                queue.emplace_back(next);
            if (from_queue) {
                auto it = std::find(queue.begin(), queue.end(), next_candidate.region);
                assert(it != queue.end());
                *it = queue.back();
                queue.pop_back();
            }
            assert(next_candidate.region);
			MonotonicRegion *next_region = next_candidate.region;
			bool              next_dir    = next_candidate.dir;
            total_length += next_region->length(next_dir) + path_matrix(*path_end.region, path_end.flipped, *next_region, next_dir).length;
            path_end = { next_region, next_dir };
            assert(left_neighbors_unprocessed[next_region - regions.data()] == 1);
            left_neighbors_unprocessed[next_region - regions.data()] = 0;          
        }

        pheromone_initial_deposit = 0.1f / total_length;
        path_matrix.update_inital_pheromone(pheromone_initial_deposit);
    }

	auto path_probability = [](AntPath &path) {
		return pow(path.pheromone, pheromone_alpha) * pow(path.visibility, pheromone_beta);
	};

#ifdef SLIC3R_DEBUG_ANTS
    static int irun = 0;
    ++ irun;
#endif

    int num_rounds_no_change = 0;
    for (int round = 0; round < num_rounds && num_rounds_no_change < num_rounds_no_change_exit; ++ round)
	{
		bool improved = false;
		for (int ant = 0; ant < num_ants; ++ ant) 
		{
			print_ant("Round %1% ant %2%", round, ant);
			path.clear();
			queue = queue_initial;
			left_neighbors_unprocessed = left_neighbors_unprocessed_initial;
            assert(validate_unprocessed());
            int first_idx = std::uniform_int_distribution<>(0, int(queue.size()) - 1)(rng);
            path.emplace_back(MonotonicRegionLink{ queue[first_idx], rng() > rng.max() / 2 });
            *(queue.begin() + first_idx) = std::move(queue.back());
            queue.pop_back();
            -- left_neighbors_unprocessed[path.back().region - regions.data()];
            assert(left_neighbors_unprocessed[path.back().region - regions.data()] == 0);
            assert(validate_unprocessed());
            print_ant("\tRegion (%1%:%2%,%3%) (%4%:%5%,%6%)",
				path.back().region->left.vline, 
                path.back().flipped ? path.back().region->left.high : path.back().region->left.low,
                path.back().flipped ? path.back().region->left.low  : path.back().region->left.high,
                path.back().region->right.vline, 
                path.back().flipped == path.back().region->flips ? path.back().region->right.high : path.back().region->right.low,
                path.back().flipped == path.back().region->flips ? path.back().region->right.low : path.back().region->right.high);

			while (! queue.empty() || ! path.back().region->right_neighbors.empty()) {
				MonotonicRegion 		    &region = *path.back().region;
				bool 			  			 dir    = path.back().flipped;
                next_candidates.clear();
				next_candidates.reserve(region.right_neighbors.size() * 2);
				for (MonotonicRegion *next : region.right_neighbors) {
					int &unprocessed = left_neighbors_unprocessed[next - regions.data()];
					assert(unprocessed > 1);
					if (-- unprocessed == 1) {
                        AntPath &path1  	   = path_matrix(region,   dir, *next, false);
                        AntPath &path1_flipped = path_matrix(region, ! dir, *next, true);
                        AntPath &path2 	       = path_matrix(region,   dir, *next, true);
                        AntPath &path2_flipped = path_matrix(region, ! dir, *next, false);
                        next_candidates.emplace_back(NextCandidate{ next, &path1, &path1_flipped, path_probability(path1), false });
                        next_candidates.emplace_back(NextCandidate{ next, &path2, &path2_flipped, path_probability(path2), true  });
					}
				}
                size_t num_direct_neighbors = next_candidates.size();
                if (num_direct_neighbors == 0) {
                    for (MonotonicRegion *next : queue) {
                    	assert(left_neighbors_unprocessed[next - regions.data()] == 1);
                        AntPath &path1  	   = path_matrix(region,   dir, *next, false);
                        AntPath &path1_flipped = path_matrix(region, ! dir, *next, true);
                        AntPath &path2 	       = path_matrix(region,   dir, *next, true);
                        AntPath &path2_flipped = path_matrix(region, ! dir, *next, false);
                        next_candidates.emplace_back(NextCandidate{ next, &path1, &path1_flipped, path_probability(path1), false });
                        next_candidates.emplace_back(NextCandidate{ next, &path2, &path2_flipped, path_probability(path2), true  });
                    }
                }
				float dice = float(rng()) / float(rng.max());
                std::vector<NextCandidate>::iterator take_path;
				if (dice < probability_take_best) {
					take_path = std::max_element(next_candidates.begin(), next_candidates.end(), [](auto &l, auto &r){ return l.probability < r.probability; });
					print_ant("\tTaking best path at probability %1% below %2%", dice,  probability_take_best);
				} else {
                    float total_probability = std::accumulate(next_candidates.begin(), next_candidates.end(), 0.f, [](const float l, const NextCandidate& r) { return l + r.probability; });
                    float probability_threshold = float(rng()) * total_probability / float(rng.max());
                    take_path = next_candidates.end();
                    -- take_path;
                    for (auto it = next_candidates.begin(); it < next_candidates.end(); ++ it)
                        if ((probability_threshold -= it->probability) <= 0.) {
                            take_path = it;
                            break;
                        }
					print_ant("\tTaking path at probability threshold %1% of %2%", probability_threshold, total_probability);
				}
                for (std::vector<NextCandidate>::iterator it_next_candidate = next_candidates.begin(); it_next_candidate != next_candidates.begin() + num_direct_neighbors; ++ it_next_candidate)
                    if ((queue.empty() || it_next_candidate->region != queue.back()) && it_next_candidate->region != take_path->region)
                        queue.emplace_back(it_next_candidate->region);
                if (size_t(take_path - next_candidates.begin()) >= num_direct_neighbors) {
                    auto it = std::find(queue.begin(), queue.end(), take_path->region);
                    assert(it != queue.end());
                    *it = queue.back();
                    queue.pop_back();
                }
				MonotonicRegion  *next_region = take_path->region;
				bool              next_dir    = take_path->dir;
                path.back().next         = take_path->link;
                path.back().next_flipped = take_path->link_flipped;
                path.emplace_back(MonotonicRegionLink{ next_region, next_dir });
                assert(left_neighbors_unprocessed[next_region - regions.data()] == 1);
                left_neighbors_unprocessed[next_region - regions.data()] = 0;
				print_ant("\tRegion (%1%:%2%,%3%) (%4%:%5%,%6%) length to prev %7%", 
                    next_region->left.vline, 
                    next_dir ? next_region->left.high : next_region->left.low,
                    next_dir ? next_region->left.low  : next_region->left.high,
					next_region->right.vline, 
                    next_dir == next_region->flips ? next_region->right.high : next_region->right.low,
                    next_dir == next_region->flips ? next_region->right.low  : next_region->right.high,
					take_path->link->length);

                print_ant("\tRegion (%1%:%2%,%3%) (%4%:%5%,%6%)",
                    path.back().region->left.vline,
                    path.back().flipped ? path.back().region->left.high : path.back().region->left.low,
                    path.back().flipped ? path.back().region->left.low : path.back().region->left.high,
                    path.back().region->right.vline,
                    path.back().flipped == path.back().region->flips ? path.back().region->right.high : path.back().region->right.low,
                    path.back().flipped == path.back().region->flips ? path.back().region->right.low : path.back().region->right.high);

				take_path->link->pheromone = (1.f - pheromone_diversification) * take_path->link->pheromone + pheromone_diversification * pheromone_initial_deposit;
                assert(validate_unprocessed());
            }

			monotonic_3_opt(path, segs);

            assert(! path.empty());
            float path_length = std::accumulate(path.begin(), path.end() - 1,
                path.back().region->length(path.back().flipped),
                [&path_matrix](const float l, const MonotonicRegionLink &r) { 
                    const MonotonicRegionLink &next = *(&r + 1);
                    return l + r.region->length(r.flipped) + path_matrix(*r.region, r.flipped, *next.region, next.flipped).length;
                });
			print_ant("\tThis length: %1%, shortest length: %2%", path_length, best_path_length);
			if (path_length < best_path_length) {
				best_path_length = path_length;
				std::swap(best_path, path);
#if 0
				if (round == 0 && ant == 0)
					std::cout << std::endl;
			    std::cout << Slic3r::format("round %1% ant %2% path length %3%", round, ant, path_length) << std::endl;
#endif
                if (path_length == 0)
                    goto end;
                improved = true;
			}
		}

        float total_cost = best_path_length + float(EPSILON);
        for (size_t i = 0; i + 1 < path.size(); ++ i) {
            MonotonicRegionLink &link = path[i];
            link.next->pheromone = (1.f - pheromone_evaporation) * link.next->pheromone + pheromone_evaporation / total_cost;
        }

        if (improved)
            num_rounds_no_change = 0;
        else
			++ num_rounds_no_change;
	}

end:
    return best_path;
}

static void polylines_from_paths(const std::vector<MonotonicRegionLink> &path, const ExPolygonWithOffset &poly_with_offset, const std::vector<SegmentedIntersectionLine> &segs, Polylines &polylines_out)
{
	Polyline *polyline = nullptr;
	auto finish_polyline = [&polyline, &polylines_out]() {
        polyline->remove_duplicate_points();
        assert(!polyline->has_duplicate_points());
        if (polyline->points.size() <= 1 ||
            (polyline->points.size() == 2 &&
                std::abs(polyline->points.front().x() - polyline->points.back().x()) < SCALED_EPSILON &&
                std::abs(polyline->points.front().y() - polyline->points.back().y()) < SCALED_EPSILON))
            polylines_out.pop_back();
        else if (polylines_out.size() >= 2) {
            assert(polyline->points.size() >= 2);
            Polyline &pl_prev = polylines_out[polylines_out.size() - 2];
            if (std::abs(polyline->points.front().x() - pl_prev.points.back().x()) < SCALED_EPSILON &&
                std::abs(polyline->points.front().y() - pl_prev.points.back().y()) < SCALED_EPSILON) {
                pl_prev.points.back() = (pl_prev.points.back() + polyline->points.front()) / 2;
                pl_prev.points.insert(pl_prev.points.end(), polyline->points.begin() + 1, polyline->points.end());
                polylines_out.pop_back();
            }
        }
    	polyline = nullptr;
    };

	for (const MonotonicRegionLink &path_segment : path) {
		MonotonicRegion &region = *path_segment.region;
		bool 		     dir    = path_segment.flipped;

		int  i_intersection = region.left_intersection_point(dir);
		int  i_vline 		= region.left.vline;

        if (polyline != nullptr && &path_segment != path.data()) {
        	const MonotonicRegionLink 	      &path_segment_prev  = *(&path_segment - 1);
			const MonotonicRegion 		      &region_prev		  = *path_segment_prev.region;
			bool 			  			       dir_prev 		  = path_segment_prev.flipped;
			int                                i_vline_prev       = region_prev.right.vline;
			const SegmentedIntersectionLine   &vline_prev         = segs[i_vline_prev];
			int 		       				   i_intersection_prev = region_prev.right_intersection_point(dir_prev);
			const SegmentIntersection         *ip_prev 			  = &vline_prev.intersections[i_intersection_prev];
			bool 						       extended           = false;
			if (i_vline_prev + 1 == i_vline) {
				if (ip_prev->right_horizontal() == i_intersection && ip_prev->next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
		            emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline_prev, ip_prev->iContour, i_intersection_prev, i_intersection, *polyline, true);
		            extended = true;
				}
	        }
	        if (! extended) {
                assert(ip_prev->is_inner());
                ip_prev->is_low() ? -- ip_prev : ++ ip_prev;
		        assert(ip_prev->is_outer());
	        	polyline->points.back() = Point(vline_prev.pos, ip_prev->pos());
				finish_polyline();
			}
        }

		for (;;) {
	        const SegmentedIntersectionLine &vline = segs[i_vline];
            const SegmentIntersection       *it    = &vline.intersections[i_intersection];
            const bool                       going_up = it->is_low();
            if (polyline == nullptr) {
				polylines_out.emplace_back();
	            polyline = &polylines_out.back();
	        	polyline->points.emplace_back(vline.pos, (it + (going_up ? - 1 : 1))->pos());
			} else
				polyline->points.emplace_back(vline.pos, it->pos());

			int iright = it->right_horizontal();
	        if (going_up) {
	            for (;;) {
		            do {
		                ++ it;
						iright = std::max(iright, it->right_horizontal());
                        assert(it->is_inner());
                    } while (it->type != SegmentIntersection::INNER_HIGH || (it + 1)->type != SegmentIntersection::OUTER_HIGH);
	                polyline->points.emplace_back(vline.pos, it->pos());
		            int inext = it->vertical_up();
                    if (inext == -1 || it->vertical_up_quality() != SegmentIntersection::LinkQuality::Valid)
		            	break;
	                assert(it->iContour == vline.intersections[inext].iContour);
	                emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, it->iContour, it - vline.intersections.data(), inext, *polyline, it->has_left_vertical_up());
	                it = vline.intersections.data() + inext;
	            } 
	        } else {
                assert(it->is_high());
                assert(i_intersection > 0);
	            for (;;) {
		            do {
		                -- it;
		                if (int iright_new = it->right_horizontal(); iright_new != -1)
		                	iright = iright_new;
                        assert(it->is_inner());
		            } while (it->type != SegmentIntersection::INNER_LOW || (it - 1)->type != SegmentIntersection::OUTER_LOW);
	                polyline->points.emplace_back(vline.pos, it->pos());
		            int inext = it->vertical_down();
		            if (inext == -1 || it->vertical_down_quality() != SegmentIntersection::LinkQuality::Valid)
		            	break;
	                assert(it->iContour == vline.intersections[inext].iContour);
	                emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, it->iContour, it - vline.intersections.data(), inext, *polyline, it->has_right_vertical_down());
	                it = vline.intersections.data() + inext;
	            } 
	        }

	        if (i_vline == region.right.vline)
	        	break;

            int inext = it->right_horizontal();
            if (iright == -1) {
                going_up ? ++ it : -- it;
                polyline->points.back() = Point(vline.pos, it->pos());
                finish_polyline();
                break;
            }
            assert(inext == -1 || inext == iright);

            const SegmentedIntersectionLine &vline_right = segs[i_vline + 1];
            const SegmentIntersection       *right       = going_up ? 
                &vertical_run_top(vline_right, vline_right.intersections[iright]) : &vertical_run_bottom(vline_right, vline_right.intersections[iright]);
            i_intersection = int(right - vline_right.intersections.data());

	        if (inext == i_intersection && it->next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
	            emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline, it->iContour, it - vline.intersections.data(), inext, *polyline, true);
	        } else {
	        	going_up ? ++ it : -- it;
		        assert(it->is_outer());
		        assert(it->is_high() == going_up);
	        	polyline->points.back() = Point(vline.pos, it->pos());
				finish_polyline();
	        }

	        ++ i_vline;
	    }
    }

    if (polyline != nullptr) {
        const MonotonicRegion            &region = *path.back().region;
        const SegmentedIntersectionLine  &vline  = segs[region.right.vline];
        const SegmentIntersection        *ip     = &vline.intersections[region.right_intersection_point(path.back().flipped)];
        assert(ip->is_inner());
        ip->is_low() ? -- ip : ++ ip;
        assert(ip->is_outer());
        polyline->points.back() = Point(vline.pos, ip->pos());
        finish_polyline();
    }
}

bool FillRectilinear::fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out)
{
    size_t n_polylines_out_initial = polylines_out.size();

    const float INFILL_OVERLAP_OVER_SPACING = 0.45f;
    assert(INFILL_OVERLAP_OVER_SPACING > 0 && INFILL_OVERLAP_OVER_SPACING < 0.5f);

    std::pair<float, Point> rotate_vector = this->_infill_direction(surface);
    if (params.locked_zag)
        rotate_vector.first += float(M_PI/2.);
    rotate_vector.first += angleBase;

    assert(params.density > 0.0001f && params.density <= 1.f);
    coord_t line_spacing = coord_t(scale_(this->spacing) / params.density);

    ExPolygonWithOffset poly_with_offset(
        surface->expolygon, 
        - rotate_vector.first, 
        float(scale_(this->overlap - (0.5 - INFILL_OVERLAP_OVER_SPACING) * this->spacing)),
        float(scale_(this->overlap - 0.5f * this->spacing)));
    if (poly_with_offset.n_contours_inner == 0) {
        return true;
    }

    BoundingBox bounding_box_src = poly_with_offset.bounding_box_src();
    BoundingBox bounding_box     = bounding_box_src;
    if (this->has_consistent_pattern()) {
        const Point c = this->bounding_box.center();
        bounding_box  = this->extended_object_bounding_box();
        bounding_box.translate(c.rotated(- rotate_vector.first) - c);
    }

    if (params.full_infill() && !params.dont_adjust) {
        line_spacing = this->_adjust_solid_spacing(bounding_box_src.size().x(), line_spacing);
        this->spacing = unscale<double>(line_spacing);
    } else {
        Point refpt = rotate_vector.second.rotated(- rotate_vector.first);
        coord_t pattern_shift_scaled = coord_t(scale_(pattern_shift)) % line_spacing;
        refpt.x() -= (pattern_shift_scaled >= 0) ? pattern_shift_scaled : (line_spacing + pattern_shift_scaled);
        bounding_box.merge(align_to_grid(
            bounding_box.min, 
            Point(line_spacing, line_spacing), 
            refpt));
    }

    size_t  n_vlines = (bounding_box.max(0) - bounding_box.min(0) + line_spacing - 1) / line_spacing;
	coord_t x0 = bounding_box.min(0);
	if (params.full_infill())
		x0 += (line_spacing + coord_t(SCALED_EPSILON)) / 2;

    int gap_line = params.horiz_move / line_spacing;
    if (gap_line % 2 == 0) {
        x0 += params.horiz_move - gap_line * line_spacing;
    } else {
        x0 += params.horiz_move - (gap_line - 1) * line_spacing;
        n_vlines += 1;
    }
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    BoundingBox bbox_svg = poly_with_offset.bounding_box_outer();
    ::Slic3r::SVG svg(debug_out_path("FillRectilinear-%d.svg", iRun), bbox_svg);
    poly_with_offset.export_to_svg(svg);
    {
        ::Slic3r::SVG svg(debug_out_path("FillRectilinear-initial-%d.svg", iRun), bbox_svg);
        poly_with_offset.export_to_svg(svg);
    }
    iRun ++;
#endif
    std::vector<SegmentedIntersectionLine> segs = slice_region_by_vertical_lines(poly_with_offset, n_vlines, x0, line_spacing);
	connect_segment_intersections_by_contours(poly_with_offset, segs, params, link_max_length);

#ifdef SLIC3R_DEBUG
    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        for (size_t i = 0; i < sil.intersections.size();) {
            size_t j = i + 1;
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            if (i + 1 == j) {
                svg.draw(Line(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[j].pos())), "blue");
            } else {
                svg.draw(Line(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[i+1].pos())), "green");
                svg.draw(Line(Point(sil.pos, sil.intersections[i+1].pos()), Point(sil.pos, sil.intersections[j-1].pos())), (j - i + 1 > 4) ? "yellow" : "magenta");
                svg.draw(Line(Point(sil.pos, sil.intersections[j-1].pos()), Point(sil.pos, sil.intersections[j].pos())), "green");
            }
            i = j + 1;
        }
    }
    svg.Close();
#endif
    bool monotonic_infill = params.monotonic;
    if (monotonic_infill) {
        pinch_contours_insert_phony_outer_intersections(segs);
		std::vector<MonotonicRegion> regions = generate_montonous_regions(segs);
#ifdef INFILL_DEBUG_OUTPUT
        {
            static int iRun;
            export_monotonous_regions_to_svg(poly_with_offset, segs, regions, debug_out_path("%s-%03d.svg", "MontonousRegions-initial", iRun ++));
        }
#endif
		connect_monotonic_regions(regions, poly_with_offset, segs);
        if (! regions.empty()) {
		    std::mt19937_64 rng;
		    std::vector<MonotonicRegionLink> path = chain_monotonic_regions(regions, poly_with_offset, segs, rng);
		    polylines_from_paths(path, poly_with_offset, segs, polylines_out);
        }
	} else {
		traverse_graph_generate_polylines(poly_with_offset, params, segs, this->has_consistent_pattern(), polylines_out);
    }

#ifdef SLIC3R_DEBUG
    {
        {
            ::Slic3r::SVG svg(debug_out_path("FillRectilinear-final-%03d.svg", iRun), bbox_svg);
            poly_with_offset.export_to_svg(svg);
            for (size_t i = n_polylines_out_initial; i < polylines_out.size(); ++ i)
                svg.draw(polylines_out[i].lines(), "black");
        }
        for (size_t i_polyline = n_polylines_out_initial; i_polyline < polylines_out.size(); ++ i_polyline) {
            ::Slic3r::SVG svg(debug_out_path("FillRectilinear-final-%03d-%03d.svg", iRun, i_polyline), bbox_svg);
            svg.draw(polylines_out[i_polyline].lines(), "black");
        }
    }
#endif
    for (Polylines::iterator it = polylines_out.begin() + n_polylines_out_initial; it != polylines_out.end(); ++ it) {
        assert(! it->has_duplicate_points());
        it->rotate(rotate_vector.first);
        it->remove_duplicate_points();

        if (params.symmetric_infill_y_axis) {
            it->symmetric_y(params.symmetric_y_axis);
        }
    }

#ifdef SLIC3R_DEBUG
    for (Polyline &polyline : polylines_out)
        assert(! polyline.has_duplicate_points());
#endif



    return true;
}

void make_fill_lines(const ExPolygonWithOffset &poly_with_offset, Point refpt, double angle, coord_t x_margin, coord_t line_spacing, coord_t pattern_shift, Polylines &fill_lines)
{
    BoundingBox bounding_box = poly_with_offset.bounding_box_src();
    coord_t     x_min = bounding_box.min.x() + x_margin;
    coord_t     x_max = bounding_box.max.x() - x_margin;
    coord_t pattern_shift_scaled = pattern_shift % line_spacing;
    refpt.x() -= (pattern_shift_scaled >= 0) ? pattern_shift_scaled : (line_spacing + pattern_shift_scaled);
    bounding_box.merge(Slic3r::align_to_grid(bounding_box.min, Point(line_spacing, line_spacing), refpt));

    const size_t n_vlines = (bounding_box.max.x() - bounding_box.min.x() + line_spacing - 1) / line_spacing;
    const double cos_a    = cos(angle);
    const double sin_a    = sin(angle);
    for (const SegmentedIntersectionLine &vline : slice_region_by_vertical_lines(poly_with_offset, n_vlines, bounding_box.min.x(), line_spacing))
        if (vline.pos >= x_min) {
            if (vline.pos > x_max)
                break;
            for (auto it = vline.intersections.begin(); it != vline.intersections.end();) {
                auto it_low  = it ++;
                assert(it_low->type == SegmentIntersection::OUTER_LOW);
                if (it_low->type != SegmentIntersection::OUTER_LOW)
                    continue;
                auto it_high = it;
                assert(it_high->type == SegmentIntersection::OUTER_HIGH);
                if (it_high->type == SegmentIntersection::OUTER_HIGH) {
                    if (angle == 0.)
                        fill_lines.emplace_back(Point(vline.pos, it_low->pos()), Point(vline.pos, it_high->pos()));
                    else
                        fill_lines.emplace_back(Point(vline.pos, it_low->pos()).rotated(cos_a, sin_a), Point(vline.pos, it_high->pos()).rotated(cos_a, sin_a));
                    ++ it;
                }
            }
        }
}

static inline void remove_overlapped(Polylines& polylines, coord_t line_width){
    const coord_t tolerance = coord_t(0.75 * line_width);
    Polylines cleaned;
    cleaned.reserve(polylines.size());

    auto midpoint = [](const Polyline& line) -> Point {
        const Point& p1 = line.first_point();
        const Point& p2 = line.last_point();
        return Point((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2);
    };

    for (const Polyline& line : polylines) {
        Point mp1 = midpoint(line);
        bool overlapped = false;

        for (const Polyline& existing : cleaned) {
            Point mp2 = midpoint(existing);

            if (std::abs(mp1.y() - mp2.y()) > tolerance &&
                std::abs(mp1.x() - mp2.x()) > tolerance)
                continue;

            if (mp1.distance_to(mp2) < tolerance) {
                overlapped = true;
                break;
            }
        }

        if (!overlapped)
            cleaned.push_back(line);
    }

    polylines = std::move(cleaned);
}

bool FillRectilinear::fill_surface_by_multilines(const Surface *surface, FillParams params, const std::initializer_list<SweepParams> &sweep_params, Polylines &polylines_out)
{
    assert(sweep_params.size() >= 1);
    assert(!params.full_infill());
    params.density /= double(sweep_params.size());
    assert(params.density > 0.0001f && params.density <= 1.f);

    ExPolygonWithOffset poly_with_offset_base(surface->expolygon, 0, float(scale_(this->overlap + 0.5 * params.multiline * this->spacing)));
    if (poly_with_offset_base.n_contours == 0)
        return true;

    Polylines fill_lines;
    coord_t line_width   = coord_t(scale_(this->spacing));
    coord_t line_spacing = coord_t(scale_(this->spacing) * params.multiline / params.density);
    std::pair<float, Point> rotate_vector = this->_infill_direction(surface);
    for (const SweepParams &sweep : sweep_params) {
        float angle = rotate_vector.first + sweep.angle_base;

        make_fill_lines(ExPolygonWithOffset(poly_with_offset_base, -angle), rotate_vector.second.rotated(-angle), angle,
                        line_width + coord_t(SCALED_EPSILON), line_spacing, coord_t(scale_(sweep.pattern_shift)), fill_lines);
    }
        
    multiline_fill(fill_lines, params, spacing);
 
    const ExPolygons contracted = offset_ex(surface->expolygon, -float(scale_(0.5 * this->spacing)));

    const ExPolygons& intersection_surface = contracted.empty() ? ExPolygons{surface->expolygon} : contracted;

    fill_lines = intersection_pl(std::move(fill_lines), intersection_surface);

    if ((params.pattern == ipLateralLattice || params.pattern == ipLateralHoneycomb) && params.multiline > 1)
        remove_overlapped(fill_lines, line_width);

    if (!fill_lines.empty()) {
        if (params.dont_connect()) {
            if (fill_lines.size() > 1)
                fill_lines = chain_polylines(std::move(fill_lines));
            append(polylines_out, std::move(fill_lines));
        } else
            connect_infill(std::move(fill_lines), to_polygons(intersection_surface), get_extents(surface->expolygon.contour), polylines_out,
                           this->spacing, params);
    }

    return true;
}

static std::vector<Vec2d> cubic_upper_level(double tau, double h, double period, double d1)
{
    const double s3     = std::sqrt(3.);
    const double y_cut  = std::clamp(tau - 0.5 * d1, 0., h - d1) + d1;
    const double y_flat = std::min(h, h + y_cut - 2. * d1);
    const double x2     = (h - tau) / s3 + d1;
    const double x3     = (h + tau) / s3 - d1;
    const std::array<Vec2d, 5> lines{ Vec2d(s3, tau), Vec2d(1. / s3, h - x2 / s3), Vec2d(0., y_flat),
                                      Vec2d(-1. / s3, h + x3 / s3), Vec2d(-s3, tau + s3 * period) };
    auto y_at = [&lines, y_cut](double x) {
        double y = std::numeric_limits<double>::max();
        for (const Vec2d &l : lines)
            y = std::min(y, l.x() * x + l.y());
        return std::max(y, y_cut);
    };

    std::vector<double> xs;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].x() != 0.)
            xs.emplace_back((y_cut - lines[i].y()) / lines[i].x());
        for (size_t j = i + 1; j < lines.size(); ++j)
            xs.emplace_back((lines[j].y() - lines[i].y()) / (lines[i].x() - lines[j].x()));
    }
    xs.erase(std::remove_if(xs.begin(), xs.end(), [period](double x) { return x <= 1. || x >= period - 1.; }), xs.end());
    xs.insert(xs.end(), { 0., period });
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [](double a, double b) { return b - a < 1.; }), xs.end());

    std::vector<Vec2d> pts;
    for (double x : xs) {
        const Vec2d p(x, y_at(x));
        if (pts.size() >= 2) {
            const Vec2d &a = pts[pts.size() - 2], &b = pts.back();
            if (std::abs((b.y() - a.y()) / (b.x() - a.x()) - (p.y() - b.y()) / (p.x() - b.x())) < EPSILON)
                pts.pop_back();
        }
        pts.emplace_back(p);
    }
    return pts;
}

bool FillRectilinear::fill_surface_trapezoidal(
    const Surface*                            surface,
    FillParams                                params,
    Polylines&                                polylines_out,
    int                                       Pattern_type)
{
    assert(params.multiline > 1);

    Polylines polylines;

    const coord_t d1 = coord_t(scale_(this->spacing)) * params.multiline;
    coord_t                  period;
    double                   base_angle;
    std::pair<double, Point> rotate_vector = this->_infill_direction(surface);

    if (Pattern_type == 0) {
        period     = coord_t((2.0 * d1 / params.density) * std::sqrt(2.0));
        base_angle = rotate_vector.first + M_PI_4;
    } else {
        period     = coord_t((2.0 * d1 / params.density) * std::sqrt(3.0));
        base_angle = rotate_vector.first + M_PI_2;
    }

    ExPolygon expolygon = surface->expolygon;
    if (std::abs(base_angle) >= EPSILON) {
        expolygon.rotate(-base_angle, rotate_vector.second);
    }

    const size_t infill_layer_id = (surface->thickness_layers > 0) ? this->layer_id / surface->thickness_layers : this->layer_id;
    const size_t layer_mod = infill_layer_id % 3;
    const double angle     = layer_mod * 2.0 * M_PI / 3.0;

    Polygon local = expolygon.contour;
    if (Pattern_type != 0) {
        local.translate(-rotate_vector.second.x(), -rotate_vector.second.y());
        if (layer_mod)
            local.rotate(-angle);
    }
    BoundingBox cover = get_extents(local);
    cover.offset(period);

    switch (Pattern_type) {
    case 0:
    {
        
        const coord_t d2 = coord_t(0.5 * period - d1);

        BoundingBox bb = this->extended_object_bounding_box();
        const Point grid_center = bb.center();
        bb.merge(align_to_grid(bb.min, Point(period, period), grid_center));
        const coord_t xmin = bb.min.x();
        const coord_t ymin = bb.min.y();

        auto transpose = [&grid_center](const Point &p) {
            return Point(grid_center.x() + p.y() - grid_center.y(), grid_center.y() + p.x() - grid_center.x());
        };
        if (infill_layer_id % 2 == 1)
            cover = BoundingBox(transpose(cover.min), transpose(cover.max));
        const coord_t row_spacing = period / 2;
        const coord_t first_x = xmin + (coord_t(std::floor(double(cover.min.x() - xmin) / period)) - 1) * period;
        const coord_t last_x  = xmin + (coord_t(std::ceil(double(cover.max.x() - xmin) / period)) + 1) * period;
        const coord_t first_row = coord_t(std::floor(double(cover.min.y() - ymin) / row_spacing)) - 1;
        const coord_t last_row  = coord_t(std::ceil(double(cover.max.y() - ymin) / row_spacing)) + 1;

        Polyline base_row_normal;
        base_row_normal.points.reserve(((last_x - first_x) / period + 1) * 5);
        Polyline base_row_flipped;
        base_row_flipped.points.reserve(((last_x - first_x) / period + 1) * 5);
        for (coord_t x = first_x; x < last_x; x += period) {
            base_row_normal.points.emplace_back(Point(x, d1 / 2));
            base_row_normal.points.emplace_back(Point(x + d1 / 2, d1 / 2));
            base_row_normal.points.emplace_back(Point(x + d1 / 2 + d2, d1 / 2 + d2));
            base_row_normal.points.emplace_back(Point(x + d1 / 2 + d2 + d1, d1 / 2 + d2));
            base_row_normal.points.emplace_back(Point(x + period - d1 / 2, d1 / 2));
        }

        base_row_flipped.points = base_row_normal.points;
        for (auto& p : base_row_flipped.points) {
            p.y() = period / 2 - p.y();
        }
        
        const size_t estimated_rows = size_t(last_row - first_row + 1);
        polylines.reserve(estimated_rows);

        bool flip_vertical = (first_row % 2) != 0;

        for (coord_t row = first_row; row <= last_row; ++row) {
            const coord_t y = ymin + row * row_spacing;
            Polyline pl_row = flip_vertical ? base_row_flipped : base_row_normal;

            for (Point& p : pl_row.points) {
                p.y() += y;
            }

            polylines.emplace_back(std::move(pl_row));
            flip_vertical = !flip_vertical;
        }

        if (infill_layer_id % 2 == 1)
            for (Polyline& pl : polylines)
                for (Point& p : pl.points)
                    p = transpose(p);
        break;
    }

    case 1:
    {
        const coord_t d2_tri = coord_t(2.0 / std::sqrt(3.0) * d1);
        const coord_t h      = coord_t(0.5 * std::sqrt(3.0) * period);
        const coord_t x_min_aligned = (coord_t(std::floor(double(cover.min.x()) / period)) - 1) * period;
        const coord_t x_max_aligned = (coord_t(std::ceil(double(cover.max.x()) / period)) + 1) * period;
        const coord_t first_row = coord_t(std::floor(double(cover.min.y()) / h)) - 1;
        const coord_t last_row  = coord_t(std::ceil(double(cover.max.y()) / h)) + 1;

        const size_t estimated_rows = size_t(last_row - first_row + 1);
        const size_t estimated_polylines = estimated_rows * 2;
        polylines.reserve(estimated_polylines);

        Polyline base_line_template;
        base_line_template.points.reserve(2);
        Polyline trapezoid_row_normal;
        trapezoid_row_normal.points.reserve(((x_max_aligned - x_min_aligned) / period) * 5);
        Polyline trapezoid_row_shifted;
        trapezoid_row_shifted.points.reserve(((x_max_aligned - x_min_aligned) / period) * 5);
        base_line_template.points.emplace_back(Point(x_min_aligned, 0));
        base_line_template.points.emplace_back(Point(x_max_aligned, 0));

        for (coord_t x = x_min_aligned; x < x_max_aligned; x += period) {
            trapezoid_row_normal.points.emplace_back(Point(x + d2_tri / 2, d1));
            trapezoid_row_normal.points.emplace_back(Point(x + period / 2 - d2_tri / 2, h - d1));
            trapezoid_row_normal.points.emplace_back(Point(x + period / 2 + d2_tri / 2, h - d1));
            trapezoid_row_normal.points.emplace_back(Point(x + period - d2_tri / 2, d1));
            trapezoid_row_normal.points.emplace_back(Point(x + period, d1));
        }

        trapezoid_row_shifted.points = trapezoid_row_normal.points;
        for (auto& p : trapezoid_row_shifted.points)
            p.y() = h - p.y();

        bool shift_row = (first_row % 2) != 0;
        for (coord_t row = first_row; row <= last_row; ++row) {
            const coord_t y = row * h;
            Polyline base_line = base_line_template;
            for (Point& p : base_line.points) {
                p.y() += y;
            }
            polylines.emplace_back(std::move(base_line));

            Polyline trapezoid_line = shift_row ? trapezoid_row_shifted : trapezoid_row_normal;
            for (Point& p : trapezoid_line.points) {
                p.y() += y;
            }

            if (!trapezoid_line.points.empty()) {
                polylines.emplace_back(std::move(trapezoid_line));
            }

            shift_row = !shift_row;
        }
        break;
    }

    case 2:
    {
        const coord_t hex_height     = coord_t(0.5 * std::sqrt(3.0) * period);
        const coord_t tri_height     = hex_height / 2;
        const coord_t d1_half        = d1 / 2;
        const coord_t chamfer_height = std::sqrt(3.0) * d1_half;
        const coord_t d1_half_base   = d1_half / std::sqrt(3.0);
        const coord_t half_period    = period / 2;
        const coord_t quarter_period = period / 4;
        const coord_t row_y_offset   = tri_height - (2 * tri_height) / 3;

        const int64_t first_tile = int64_t(std::floor(double(cover.min.x()) / period)) - 1;
        const int64_t last_tile  = int64_t(std::ceil(double(cover.max.x()) / period)) + 1;
        const int64_t first_row  = int64_t(std::floor(double(cover.min.y() - row_y_offset) / hex_height)) - 1;
        const int64_t last_row   = int64_t(std::ceil(double(cover.max.y() - row_y_offset) / hex_height)) + 1;

        const size_t estimated_rows      = size_t(last_row - first_row + 1);
        const size_t estimated_polylines = estimated_rows * 2;
        polylines.reserve(estimated_polylines);

        Polyline star_row_normal;
        star_row_normal.points.reserve(size_t(last_tile - first_tile) * 7);
        Polyline star_row_mirrored;
        star_row_mirrored.points.reserve(size_t(last_tile - first_tile) * 7);

        for (int64_t tile = first_tile; tile < last_tile; ++tile) {
            const coord_t x = coord_t(tile * period) - half_period;
            star_row_normal.points.emplace_back(Point(x, hex_height));
            star_row_normal.points.emplace_back(Point(x + quarter_period - d1, hex_height));
            star_row_normal.points.emplace_back(Point(x + quarter_period + d1_half, hex_height - chamfer_height));
            star_row_normal.points.emplace_back(Point(x + half_period - d1_half_base, tri_height + d1_half));
            star_row_normal.points.emplace_back(Point(x + half_period + d1_half_base, tri_height + d1_half));
            star_row_normal.points.emplace_back(Point(x + (period * 3) / 4 - d1_half, hex_height - chamfer_height));
            star_row_normal.points.emplace_back(Point(x + (period * 3) / 4 + d1, hex_height));
        }

        star_row_mirrored.points = star_row_normal.points;
        for (auto& p : star_row_mirrored.points)
            p.y() = hex_height - p.y();

        const coord_t global_x_shift = half_period;
        auto append_row_with_shift   = [&polylines](const Polyline& row_template, coord_t x_shift, coord_t y_shift) {
            Polyline row = row_template;
            for (Point& p : row.points) {
                p.x() += x_shift;
                p.y() += y_shift;
            }
            if (!row.points.empty())
                polylines.emplace_back(std::move(row));
        };

        for (int64_t row = first_row; row <= last_row; ++row) {
            const coord_t y = coord_t(row * hex_height) + row_y_offset;
            const coord_t x_shift = (row % 2 == 0) ? 0 : half_period;
            append_row_with_shift(star_row_normal, x_shift + global_x_shift, y);
            append_row_with_shift(star_row_mirrored, x_shift + global_x_shift, y);
        }
        break;
    }

    case 3:
    {
        auto pos_mod = [](double a, double m) { const double r = std::fmod(a, m); return r < 0. ? r + m : r; };
        const double h     = 0.5 * std::sqrt(3.0) * period;
        const double shift = scale_(std::sqrt(0.5) * this->z);
        const double tau   = pos_mod(-3. * shift, h);
        const double y0    = pos_mod(-2. * shift, 2. * h);

        std::array<std::vector<Vec2d>, 2> levels{ cubic_upper_level(h - tau, h, period, d1), cubic_upper_level(tau, h, period, d1) };
        for (Vec2d &p : levels.front())
            p.y() = h - p.y();

        const int64_t n_min = int64_t(std::floor((cover.min.y() - y0) / h)) - 1;
        const int64_t n_max = int64_t(std::ceil((cover.max.y() - y0) / h)) + 1;
        polylines.reserve(size_t(n_max - n_min + 1) * levels.size());
        for (int64_t n = n_min; n <= n_max; ++n) {
            const double  x_off = (n & 1) ? 0.5 * period : 0.;
            const double  base  = y0 + double(n) * h - tau;
            const int64_t j_min = int64_t(std::floor((cover.min.x() - x_off) / period)) - 1;
            const int64_t j_max = int64_t(std::ceil((cover.max.x() - x_off) / period));
            for (const std::vector<Vec2d> &level : levels) {
                Polyline row;
                row.points.reserve(size_t(j_max - j_min + 1) * level.size());
                for (int64_t j = j_min; j <= j_max; ++j)
                    for (size_t i = (j == j_min) ? 0 : 1; i < level.size(); ++i)
                        row.points.emplace_back(coord_t(std::round(x_off + double(j * period) + level[i].x())),
                                                coord_t(std::round(base + level[i].y())));
                polylines.emplace_back(std::move(row));
            }
        }
        break;
    }

    default:
        break;
    }

    if (Pattern_type != 0)
        for (Polyline &pl : polylines) {
            if (layer_mod)
                pl.rotate(angle, Point(0, 0));
            pl.translate(rotate_vector.second);
        }

    smooth_polylines_corners(polylines, params.smooth_factor, scaled<double>(params.resolution));

    polylines = intersection_pl(std::move(polylines), offset(expolygon, float(d1 / 2)));

    multiline_fill(polylines, params, spacing);

    const Vec2d row_dir = Pattern_type != 0 ? Vec2d(std::cos(angle), std::sin(angle)) : infill_layer_id % 2 ? Vec2d::UnitY() : Vec2d::UnitX();
    for (Polyline &pl : polylines)
        if (pl.size() > 3 && pl.first_point() == pl.last_point()) {
            pl.points.pop_back();
            std::rotate(pl.points.begin(), std::min_element(pl.points.begin(), pl.points.end(), [&row_dir](const Point &a, const Point &b) {
                return row_dir.dot(a.cast<double>()) < row_dir.dot(b.cast<double>());
            }), pl.points.end());
            pl.points.emplace_back(pl.points.front());
        }

    ExPolygons contracted = offset_ex(expolygon, -float(scale_(0.5 * this->spacing)));

    const ExPolygons& intersection_surface = contracted.empty() ? ExPolygons{expolygon} : contracted;

    polylines = intersection_pl(std::move(polylines), intersection_surface);

    const double minlength = scale_(0.8 * this->spacing);
    if (minlength > 0 && !polylines.empty()) {
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    int infill_start_idx = polylines_out.size();
    if (!polylines.empty()) {
        if (params.dont_connect()) {
            if (polylines.size() > 1)
                polylines = chain_polylines(std::move(polylines));
            append(polylines_out, std::move(polylines));
        } else {
            connect_infill(std::move(polylines), to_polygons(intersection_surface), get_extents(intersection_surface), polylines_out,
                           this->spacing, params);
        }

        if (std::abs(base_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + infill_start_idx; it != polylines_out.end(); ++it) {
                it->rotate(base_angle, rotate_vector.second);
            }
        }
    }

    return true;
}

Polylines FillRectilinear::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    if (params.full_infill() || params.multiline == 1 || params.pattern == ipCrossZag || params.pattern == ipZigZag || params.pattern == ipLockedZag) {
        if (!fill_surface_by_lines(surface, params, 0.f, 0.f, polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillRectilinear::fill_surface() fill_surface_by_lines() failed to fill a region.";
    } else {
        if (!fill_surface_by_multilines(surface, params, {{0.f, 0.f}}, polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillRectilinear::fill_surface() fill_surface_by_multilines() failed to fill a region.";
    }
    return polylines_out;
}

Polylines FillMonotonic::fill_surface(const Surface *surface, const FillParams &params)
{
    FillParams params2 = params;
    params2.monotonic = true;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0.f, polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillMonotonous::fill_surface() failed to fill a region.";
    return polylines_out;
}

Polylines FillMonotonicLine::fill_surface(const Surface* surface, const FillParams& params)
{
    FillParams params2 = params;
    params2.monotonic = true;
    params2.anchor_length_max = 0.0f;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0.f, polylines_out))
        BOOST_LOG_TRIVIAL(error) << "Failed to fill a region by MonotonicLine pattern";
    return polylines_out;
}

Polylines FillGrid::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;

    if (params.multiline > 1) {
        if (!this->fill_surface_trapezoidal(
                 surface, params, polylines_out, 0))
            BOOST_LOG_TRIVIAL(error) << "FillGrid::fill_surface_trapezoidal() failed.";

    } else {
        if (!this->fill_surface_by_multilines(
                surface, params,
                { { 0.f, 0.f }, { float(M_PI / 2.), 0.f } },
                polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillGrid::fill_surface() failed to fill a region.";
    

       if (this->layer_id % 2 == 1)
           for (int i = 0; i < polylines_out.size(); i++)
               std::reverse(polylines_out[i].begin(), polylines_out[i].end());
    }
    return polylines_out;
}

Polylines FillLateralLattice::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    coordf_t dx1 = tan(Geometry::deg2rad(params.lateral_lattice_angle_1)) * z;
    coordf_t dx2 = tan(Geometry::deg2rad(params.lateral_lattice_angle_2)) * z;
    if (! this->fill_surface_by_multilines(
            surface, params,
            { { float(M_PI / 2.), float(dx1) }, { float(M_PI / 2.), float(dx2) } },
            polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillLateralLattice::fill_surface() failed to fill a region.";

    if (this->layer_id % 2 == 1)
        for (int i = 0; i < polylines_out.size(); i++)
            std::reverse(polylines_out[i].begin(), polylines_out[i].end());
    return polylines_out;
}

Polylines FillTriangles::fill_surface(const Surface *surface, const FillParams &params){
    Polylines polylines_out;
        if (params.multiline > 1) {
        if (!this->fill_surface_trapezoidal(
                 surface, params, polylines_out, 1))
            BOOST_LOG_TRIVIAL(error) << "FillGrid::fill_surface_trapezoidal() failed.";

    } else {
    if (! this->fill_surface_by_multilines(
            surface, params,
            { { 0.f, 0.f }, { float(M_PI / 3.), 0.f }, { float(2. * M_PI / 3.), 0. } },
            polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillTriangles::fill_surface() failed to fill a region.";
    }
    return polylines_out;
    
}

Polylines FillStars::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    if (params.multiline > 1) {
        if (!this->fill_surface_trapezoidal(
                 surface, params, polylines_out, 2))
            BOOST_LOG_TRIVIAL(error) << "FillStars::fill_surface_trapezoidal() failed.";
    } else {
        if (! this->fill_surface_by_multilines(
                surface, params,
                { { 0.f, 0.f }, { float(M_PI / 3.), 0.f }, { float(2. * M_PI / 3.), float((3./2.) * this->spacing * params.multiline / params.density) } },
                polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillStars::fill_surface() failed to fill a region.";
    }
    return polylines_out;
}

Polylines FillCubic::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    if (params.multiline > 1) {
        if (!this->fill_surface_trapezoidal(surface, params, polylines_out, 3))
            BOOST_LOG_TRIVIAL(error) << "FillCubic::fill_surface_trapezoidal() failed.";
        return polylines_out;
    }
    coordf_t dx = sqrt(0.5) * z;
    if (! this->fill_surface_by_multilines(
            surface, params, 
            { { 0.f, float(dx) }, { float(M_PI / 3.), - float(dx) }, { float(M_PI * 2. / 3.), float(dx) } },
            polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillCubic::fill_surface() failed to fill a region.";
    return polylines_out; 
}

Polylines FillQuarterCubic::fill_surface(const Surface* surface, const FillParams& params)
{
    using namespace boost::math::float_constants;
    Polylines polylines_out;

    coord_t line_width = coord_t(scale_(this->spacing)) * params.multiline;
    coord_t period = coord_t(scale_(this->spacing) *params.multiline / params.density) * 4;

    double  pattern_z_shift = 0.0;
    coord_t shift = coord_t(one_div_root_two * (scale_(z) + pattern_z_shift * period * 2)) % period;
    shift = std::min(shift, period - shift);
    shift = std::min(shift, period / 2 - line_width / 2);
    shift = std::max(shift, line_width / 2);
    float dx1 = unscale_(shift);

    pattern_z_shift = 0.5;
    shift = coord_t(one_div_root_two * (scale_(z) + pattern_z_shift * period * 2)) % period;
    shift = std::min(shift, period - shift);
    shift = std::min(shift, period / 2 - line_width / 2);
    shift = std::max(shift, line_width / 2);
    float dx2 = unscale_(shift);
    if (!this->fill_surface_by_multilines(
            surface, params, 
            {{0.f, dx1}, {0.f, -dx1}, {float(M_PI / 2.), dx2}, {float(M_PI / 2.), -dx2}},
            polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillQuarterCubic::fill_surface() failed to fill a region.";

    return polylines_out;
}

Polylines FillLateralHoneycomb::fill_surface(const Surface *surface, const FillParams &params)
{

    using namespace boost::math::float_constants;

    const float half_horizontal_period = .5f * (1*(2/3.f) + 2*(1/3.f)) * float(spacing) * params.multiline / params.density;
    const float vertical_period = 3 * half_horizontal_period / tanf(degree * float(params.infill_overhang_angle));

    const float vertical_thirds_float = 3 * float(z) / vertical_period;
    const int vertical_thirds_int = vertical_thirds_float;
    const bool single_line = (vertical_thirds_int + 1) % 3;

    const bool odd_layer = ((vertical_thirds_int + 1) / 3) % 2;
    const float horizontal_offset = odd_layer ? half_horizontal_period : 0;

    Polylines polylines_out;

    if (single_line)
    {
        FillParams multiline_params = params;
        multiline_params.density *= 1 / (1*(2/3.) + 2*(1/3.));

        if (!fill_surface_by_multilines(
                surface, multiline_params,
                { { half_pi, horizontal_offset } },
                polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillLateralHoneycomb::fill_surface() failed to fill a region.";
    } else {
        FillParams multiline_params = params;
        multiline_params.density *= 2 / (1*(2/3.) + 2*(1/3.));

        const float horizontal_position = (1 - (vertical_thirds_float - vertical_thirds_int)) * half_horizontal_period;

        if (!fill_surface_by_multilines(
                surface, multiline_params,
                { { half_pi, -horizontal_position + horizontal_offset }, { half_pi, horizontal_position + horizontal_offset } },
                polylines_out))
            BOOST_LOG_TRIVIAL(error) << "FillLateralHoneycomb::fill_surface() failed to fill a region.";
    }

    if (this->layer_id % 2 == 1)
        for (int i = 0; i < polylines_out.size(); i++)
            std::reverse(polylines_out[i].begin(), polylines_out[i].end());

    return polylines_out;
}

Polylines FillSupportBase::fill_surface(const Surface *surface, const FillParams &params)
{
    assert(! params.full_infill());

    Polylines polylines_out;
    std::pair<float, Point> rotate_vector = this->_infill_direction(surface);
    ExPolygonWithOffset poly_with_offset(surface->expolygon, - rotate_vector.first, float(scale_(this->overlap - 0.5 * this->spacing)));
    if (poly_with_offset.n_contours > 0) {
        Polylines fill_lines;
        coord_t line_spacing = coord_t(scale_(this->spacing) / params.density);
        make_fill_lines(poly_with_offset, rotate_vector.second.rotated(- rotate_vector.first), 0, 0, line_spacing, 0, fill_lines);
        connect_base_support(std::move(fill_lines), poly_with_offset.polygons_outer, poly_with_offset.bounding_box_outer(), polylines_out, this->spacing, params);
        const double cos_a = cos(rotate_vector.first);
        const double sin_a = sin(rotate_vector.first);
        for (Polyline &pl : polylines_out)
            for (Point &pt : pl.points)
                pt.rotate(cos_a, sin_a);
    }
    return polylines_out;
}

Points sample_grid_pattern(const ExPolygon& expolygon, coord_t spacing, const BoundingBox& global_bounding_box)
{
    ExPolygonWithOffset poly_with_offset(expolygon, 0, 0, 0);
    std::vector<SegmentedIntersectionLine> segs = slice_region_by_vertical_lines(
        poly_with_offset, 
        (global_bounding_box.max.x() - global_bounding_box.min.x() + spacing - 1) / spacing,
        global_bounding_box.min.x(),
        spacing);

    Points out;
    for (const SegmentedIntersectionLine &sil : segs) {
        for (size_t i = 0; i < sil.intersections.size(); i += 2) {
            coord_t a = sil.intersections[i].pos();
            coord_t b = sil.intersections[i + 1].pos();
            for (coord_t y = a - (a % spacing) - spacing; y < b; y += spacing)
                if (y > a)
                    out.emplace_back(sil.pos, y);
        }
    }
    return out;
}

Points sample_grid_pattern(const ExPolygons& expolygons, coord_t spacing, const BoundingBox& global_bounding_box)
{
    Points out;
    for (const ExPolygon& expoly : expolygons)
        append(out, sample_grid_pattern(expoly, spacing, global_bounding_box));
    return out;
}

Points sample_grid_pattern(const Polygons& polygons, coord_t spacing, const BoundingBox& global_bounding_box)
{
    return sample_grid_pattern(union_ex(polygons), spacing, global_bounding_box);
}

Polylines FillMonotonicLines::fill_surface(const Surface *surface, const FillParams &params)
{
    FillParams params2 = params;
    params2.monotonic = true;
    params2.anchor_length_max = 0.0f;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0.f, polylines_out))
        BOOST_LOG_TRIVIAL(error) << "FillMonotonicLines::fill_surface() failed to fill a region.";
    return polylines_out;
}
    

void FillLockedZag::fill_surface_locked_zag (const Surface *                          surface,
                                             const FillParams &                       params,
                                             std::vector<std::pair<Polylines, Flow>> &multi_width_polyline)
{
    Polylines skin_lines;
    Polylines skeloton_lines;
    double    offset_threshold  = params.skin_infill_depth;
    double    overlap_threshold = params.infill_lock_depth;
    Surface   cross_surface     = *surface;
    Surface   zig_surface       = *surface;
    ExPolygons zig_expas   = offset_ex({surface->expolygon}, -offset_threshold);
    ExPolygons cross_expas = diff_ex(surface->expolygon, zig_expas);

    bool       zig_get    = false;
    FillParams zig_params = params;
    zig_params.horiz_move = 0;
    auto generate_for_different_flow = [&multi_width_polyline](const std::map<Flow, ExPolygons> &flow_params, const Polylines &polylines) {
        auto it = flow_params.begin();
        while (it != flow_params.end()) {
            ExPolygons region_exp = union_safety_offset_ex(it->second);

            Polylines polys = intersection_pl(polylines, region_exp);
            multi_width_polyline.emplace_back(polys, it->first);
            it++;
        }
    };

    auto it = this->lock_param.skeleton_density_params.begin();
    while (it != this->lock_param.skeleton_density_params.end()) {
        ExPolygons region_exp = union_safety_offset_ex(it->second);
        ExPolygons exps       = intersection_ex(region_exp, zig_expas);
        zig_params.density    = it->first;
        exps                  = intersection_ex(offset_ex(exps, overlap_threshold), surface->expolygon);
        for (ExPolygon &exp : exps) {
            zig_surface.expolygon = exp;

            Polylines zig_polylines_out = this->fill_surface(&zig_surface, zig_params);
            skeloton_lines.insert(skeloton_lines.end(), zig_polylines_out.begin(), zig_polylines_out.end());
        }
        it++;
    }

    generate_for_different_flow(this->lock_param.skeleton_flow_params, skeloton_lines);

    bool       cross_get      = false;
    FillParams cross_params   = params;
    cross_params.locked_zag = false;
    auto skin_density         = this->lock_param.skin_density_params.begin();
    while (skin_density != this->lock_param.skin_density_params.end()) {
        ExPolygons region_exp = union_safety_offset_ex(skin_density->second);
        ExPolygons exps       = intersection_ex(region_exp, cross_expas);
        cross_params.density  = skin_density->first;
        for (ExPolygon &exp : exps) {
            cross_surface.expolygon       = exp;
            Polylines cross_polylines_out = this->fill_surface(&cross_surface, cross_params);
            skin_lines.insert(skin_lines.end(), cross_polylines_out.begin(), cross_polylines_out.end());
        }
        skin_density++;
    }

    generate_for_different_flow(this->lock_param.skin_flow_params, skin_lines);
}

void FillLockedZag::fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out)
{
    Polylines polylines;
    ThickPolylines thick_polylines;
    std::vector<std::pair<Polylines, Flow>> multi_width_polyline;
    try {
        this->fill_surface_locked_zag(surface, params, multi_width_polyline);
    }
    catch (InfillFailedException&) {}

    if (!thick_polylines.empty() || !multi_width_polyline.empty()) {
        ExtrusionEntityCollection* eec = nullptr;
        out.push_back(eec = new ExtrusionEntityCollection());
        eec->no_sort = this->no_sort();
        size_t idx   = eec->entities.size();
        {
            for (std::pair<Polylines, Flow> &poly_with_flow: multi_width_polyline) {
                double flow_mm3_per_mm = poly_with_flow.second.mm3_per_mm();
                double flow_width      = poly_with_flow.second.width();
                if (params.using_internal_flow) {
                } else {
                    Flow new_flow   = poly_with_flow.second.with_spacing(this->spacing);
                    flow_mm3_per_mm = new_flow.mm3_per_mm();
                    flow_width      = new_flow.width();
                }
            extrusion_entities_append_paths(
                eec->entities, std::move(poly_with_flow.first),
                params.extrusion_role,
                flow_mm3_per_mm, float(flow_width), poly_with_flow.second.height());
            }
        }
        if (!params.can_reverse) {
            for (size_t i = idx; i < eec->entities.size(); i++)
                eec->entities[i]->set_reverse();
        }
    }
}

}
