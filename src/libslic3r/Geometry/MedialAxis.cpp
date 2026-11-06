#include <boost/log/trivial.hpp>
#include "MedialAxis.hpp"

#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cmath>
#include <utility>
#include <cstddef>

#include "libslic3r/Line.hpp"
#include "libslic3r/Polyline.hpp"
#include "VoronoiOffset.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#ifdef SLIC3R_DEBUG
namespace boost { namespace polygon {

// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_graphic_utils.hpp header file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
template <typename CT>
class voronoi_visual_utils {
 public:

















  template <class InCT1, class InCT2,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<InCT1> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<InCT2> >::type
        >::type
      >::type
    >::type,
    void
  >::type discretize(
      const Point<InCT1>& point,
      const Segment<InCT2>& segment,
      const CT max_dist,
      std::vector< Point<CT> >* discretization) {



    CT segm_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segm_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT sqr_segment_length = segm_vec_x * segm_vec_x + segm_vec_y * segm_vec_y;



    CT projection_start = sqr_segment_length *
        get_point_projection((*discretization)[0], segment);
    CT projection_end = sqr_segment_length *
        get_point_projection((*discretization)[1], segment);




    CT point_vec_x = cast(x(point)) - cast(x(low(segment)));
    CT point_vec_y = cast(y(point)) - cast(y(low(segment)));
    CT rot_x = segm_vec_x * point_vec_x + segm_vec_y * point_vec_y;
    CT rot_y = segm_vec_x * point_vec_y - segm_vec_y * point_vec_x;


    Point<CT> last_point = (*discretization)[1];
    discretization->pop_back();


    std::stack<CT> point_stack;
    point_stack.push(projection_end);
    CT cur_x = projection_start;
    CT cur_y = parabola_y(cur_x, rot_x, rot_y);


    const CT max_dist_transformed = max_dist * max_dist * sqr_segment_length;
    while (!point_stack.empty()) {
      CT new_x = point_stack.top();
      CT new_y = parabola_y(new_x, rot_x, rot_y);



      CT mid_x = (new_y - cur_y) / (new_x - cur_x) * rot_y + rot_x;
      CT mid_y = parabola_y(mid_x, rot_x, rot_y);



      CT dist = (new_y - cur_y) * (mid_x - cur_x) -
          (new_x - cur_x) * (mid_y - cur_y);
      dist = dist * dist / ((new_y - cur_y) * (new_y - cur_y) +
          (new_x - cur_x) * (new_x - cur_x));
      if (dist <= max_dist_transformed) {

        point_stack.pop();
        CT inter_x = (segm_vec_x * new_x - segm_vec_y * new_y) /
            sqr_segment_length + cast(x(low(segment)));
        CT inter_y = (segm_vec_x * new_y + segm_vec_y * new_x) /
            sqr_segment_length + cast(y(low(segment)));
        discretization->push_back(Point<CT>(inter_x, inter_y));
        cur_x = new_x;
        cur_y = new_y;
      } else {
        point_stack.push(mid_x);
      }
    }


    discretization->back() = last_point;
  }

 private:

  static CT parabola_y(CT x, CT a, CT b) {
    return ((x - a) * (x - a) + b * b) / (b + b);
  }








  template <class InCT,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<int> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<long> >::type
        >::type
      >::type
    >::type,
    CT
  >::type get_point_projection(
      const Point<CT>& point, const Segment<InCT>& segment) {
    CT segment_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segment_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT point_vec_x = x(point) - cast(x(low(segment)));
    CT point_vec_y = y(point) - cast(y(low(segment)));
    CT sqr_segment_length =
        segment_vec_x * segment_vec_x + segment_vec_y * segment_vec_y;
    CT vec_dot = segment_vec_x * point_vec_x + segment_vec_y * point_vec_y;
    return vec_dot / sqr_segment_length;
  }

  template <typename InCT>
  static CT cast(const InCT& value) {
    return static_cast<CT>(value);
  }
};

} }
#endif

namespace Slic3r { namespace Geometry {


#ifdef SLIC3R_DEBUG
// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_visualizer.cpp file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
namespace Voronoi { namespace Internal {

    typedef double coordinate_type;
    typedef boost::polygon::point_data<coordinate_type> point_type;
    typedef boost::polygon::segment_data<coordinate_type> segment_type;
    typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
    typedef boost::polygon::voronoi_diagram<coordinate_type> VD;
    typedef VD::cell_type cell_type;
    typedef VD::cell_type::source_index_type source_index_type;
    typedef VD::cell_type::source_category_type source_category_type;
    typedef VD::edge_type edge_type;
    typedef VD::cell_container_type cell_container_type;
    typedef VD::cell_container_type vertex_container_type;
    typedef VD::edge_container_type edge_container_type;
    typedef VD::const_cell_iterator const_cell_iterator;
    typedef VD::const_vertex_iterator const_vertex_iterator;
    typedef VD::const_edge_iterator const_edge_iterator;

    static const std::size_t EXTERNAL_COLOR = 1;

    inline void color_exterior(const VD::edge_type* edge)
    {
        if (edge->color() == EXTERNAL_COLOR)
            return;
        edge->color(EXTERNAL_COLOR);
        edge->twin()->color(EXTERNAL_COLOR);
        const VD::vertex_type* v = edge->vertex1();
        if (v == NULL || !edge->is_primary())
            return;
        v->color(EXTERNAL_COLOR);
        const VD::edge_type* e = v->incident_edge();
        do {
            color_exterior(e);
            e = e->rot_next();
        } while (e != v->incident_edge());
    }

    inline point_type retrieve_point(const std::vector<segment_type> &segments, const cell_type& cell)
    {
        assert(cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT || cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT);
        return (cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? low(segments[cell.source_index()]) : high(segments[cell.source_index()]);
    }

    inline void clip_infinite_edge(const std::vector<segment_type> &segments, const edge_type& edge, coordinate_type bbox_max_size, std::vector<point_type>* clipped_edge)
    {
        const cell_type& cell1 = *edge.cell();
        const cell_type& cell2 = *edge.twin()->cell();
        point_type origin, direction;

        if (cell1.contains_point() && cell2.contains_point()) {
            point_type p1 = retrieve_point(segments, cell1);
            point_type p2 = retrieve_point(segments, cell2);
            origin.x((p1.x() + p2.x()) * 0.5);
            origin.y((p1.y() + p2.y()) * 0.5);
            direction.x(p1.y() - p2.y());
            direction.y(p2.x() - p1.x());
        } else {
            origin = cell1.contains_segment() ? retrieve_point(segments, cell2) : retrieve_point(segments, cell1);
            segment_type segment = cell1.contains_segment() ? segments[cell1.source_index()] : segments[cell2.source_index()];
            coordinate_type dx = high(segment).x() - low(segment).x();
            coordinate_type dy = high(segment).y() - low(segment).y();
            if ((low(segment) == origin) ^ cell1.contains_point()) {
                direction.x(dy);
                direction.y(-dx);
            } else {
                direction.x(-dy);
                direction.y(dx);
            }
        }
        coordinate_type koef = bbox_max_size / (std::max)(fabs(direction.x()), fabs(direction.y()));
        if (edge.vertex0() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() - direction.x() * koef,
                origin.y() - direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex0()->x(), edge.vertex0()->y()));
        }
        if (edge.vertex1() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() + direction.x() * koef,
                origin.y() + direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex1()->x(), edge.vertex1()->y()));
        }
    }

    inline void sample_curved_edge(const std::vector<segment_type> &segments, const edge_type& edge, std::vector<point_type> &sampled_edge, coordinate_type max_dist)
    {
        point_type point = edge.cell()->contains_point() ?
            retrieve_point(segments, *edge.cell()) :
            retrieve_point(segments, *edge.twin()->cell());
        segment_type segment = edge.cell()->contains_point() ?
            segments[edge.twin()->cell()->source_index()] :
            segments[edge.cell()->source_index()];
        ::boost::polygon::voronoi_visual_utils<coordinate_type>::discretize(point, segment, max_dist, &sampled_edge);
    }

}   }

void dump_voronoi_to_svg(const Lines &lines,   boost::polygon::voronoi_diagram<double> &vd, const ThickPolylines *polylines, const char *path)
{
    const double        scale                       = 0.2;
    const std::string   inputSegmentPointColor      = "lightseagreen";
    const coord_t       inputSegmentPointRadius     = coord_t(0.09 * scale / SCALING_FACTOR);
    const std::string   inputSegmentColor           = "lightseagreen";
    const coord_t       inputSegmentLineWidth       = coord_t(0.03 * scale / SCALING_FACTOR);

    const std::string   voronoiPointColor           = "black";
    const coord_t       voronoiPointRadius          = coord_t(0.06 * scale / SCALING_FACTOR);
    const std::string   voronoiLineColorPrimary     = "black";
    const std::string   voronoiLineColorSecondary   = "green";
    const std::string   voronoiArcColor             = "red";
    const coord_t       voronoiLineWidth            = coord_t(0.02 * scale / SCALING_FACTOR);

    const bool          internalEdgesOnly           = false;
    const bool          primaryEdgesOnly            = false;

    BoundingBox bbox = BoundingBox(lines);
    bbox.min(0) -= coord_t(1. / SCALING_FACTOR);
    bbox.min(1) -= coord_t(1. / SCALING_FACTOR);
    bbox.max(0) += coord_t(1. / SCALING_FACTOR);
    bbox.max(1) += coord_t(1. / SCALING_FACTOR);

    ::Slic3r::SVG svg(path, bbox);

    if (polylines != NULL)
        svg.draw(*polylines, "lime", "lime", voronoiLineWidth);




    const double bbox_dim_max = double(bbox.max(0) - bbox.min(0)) + double(bbox.max(1) - bbox.min(1));

    const double discretization_step = 0.0005 * bbox_dim_max;


    std::vector<Voronoi::Internal::segment_type> segments;
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++ it)
        segments.push_back(Voronoi::Internal::segment_type(
            Voronoi::Internal::point_type(double(it->a(0)), double(it->a(1))),
            Voronoi::Internal::point_type(double(it->b(0)), double(it->b(1)))));


    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it)
        if (!it->is_finite())
            Voronoi::Internal::color_exterior(&(*it));


    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        svg.draw(it->a, inputSegmentPointColor, inputSegmentPointRadius);
        svg.draw(it->b, inputSegmentPointColor, inputSegmentPointRadius);
    }

    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        svg.draw(Line(Point(coord_t(it->a(0)), coord_t(it->a(1))), Point(coord_t(it->b(0)), coord_t(it->b(1)))), inputSegmentColor, inputSegmentLineWidth);

#if 1

    for (boost::polygon::voronoi_diagram<double>::const_vertex_iterator it = vd.vertices().begin(); it != vd.vertices().end(); ++it)
        if (! internalEdgesOnly || it->color() != Voronoi::Internal::EXTERNAL_COLOR)
            svg.draw(Point(coord_t(it->x()), coord_t(it->y())), voronoiPointColor, voronoiPointRadius);

    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it) {
        if (primaryEdgesOnly && !it->is_primary())
            continue;
        if (internalEdgesOnly && (it->color() == Voronoi::Internal::EXTERNAL_COLOR))
            continue;
        std::vector<Voronoi::Internal::point_type> samples;
        std::string color = voronoiLineColorPrimary;
        if (!it->is_finite()) {
            Voronoi::Internal::clip_infinite_edge(segments, *it, bbox_dim_max, &samples);
            if (! it->is_primary())
                color = voronoiLineColorSecondary;
        } else {


            samples.push_back(Voronoi::Internal::point_type(it->vertex0()->x(), it->vertex0()->y()));
            samples.push_back(Voronoi::Internal::point_type(it->vertex1()->x(), it->vertex1()->y()));
            if (it->is_curved()) {
                Voronoi::Internal::sample_curved_edge(segments, *it, samples, discretization_step);
                color = voronoiArcColor;
            } else if (! it->is_primary())
                color = voronoiLineColorSecondary;
        }
        for (std::size_t i = 0; i + 1 < samples.size(); ++i)
            svg.draw(Line(Point(coord_t(samples[i].x()), coord_t(samples[i].y())), Point(coord_t(samples[i+1].x()), coord_t(samples[i+1].y()))), color, voronoiLineWidth);
    }
#endif

    if (polylines != NULL)
        svg.draw(*polylines, "blue", voronoiLineWidth);

    svg.Close();
}
#endif

template<typename VD, typename SEGMENTS>
inline const typename VD::point_type retrieve_cell_point(const typename VD::cell_type& cell, const SEGMENTS &segments)
{
    assert(cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT || cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT);
    return (cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? low(segments[cell.source_index()]) : high(segments[cell.source_index()]);
}

template<typename VD, typename SEGMENTS>
inline std::pair<typename VD::coord_type, typename VD::coord_type> measure_edge_thickness(const VD &vd, const typename VD::edge_type& edge, const SEGMENTS &segments)
{
    typedef typename VD::coord_type T;
    const typename VD::point_type  pa(edge.vertex0()->x(), edge.vertex0()->y());
    const typename VD::point_type  pb(edge.vertex1()->x(), edge.vertex1()->y());
    const typename VD::cell_type  &cell1 = *edge.cell();
    const typename VD::cell_type  &cell2 = *edge.twin()->cell();
    if (cell1.contains_segment()) {
        if (cell2.contains_segment()) {


            const typename VD::segment_type segment1 = segments[cell1.source_index()];
            const typename VD::point_type p1a = project_point_to_segment(segment1, pa);
            const typename VD::point_type p1b = project_point_to_segment(segment1, pb);
            return std::pair<T, T>(T(2.)*dist(pa, p1a), T(2.)*dist(pb, p1b));
        } else {



            const typename  VD::point_type p2 = retrieve_cell_point<VD>(cell2, segments);
            return std::pair<T, T>(T(2.)*dist(pa, p2), T(2.)*dist(pb, p2));
        }
    } else if (cell2.contains_segment()) {


        const typename VD::point_type p1 = retrieve_cell_point<VD>(cell1, segments);
        return std::pair<T, T>(T(2.)*dist(pa, p1), T(2.)*dist(pb, p1));
    } else {

        const typename VD::point_type p1 = retrieve_cell_point<VD>(cell1, segments);
        return std::pair<T, T>(T(2.)*dist(pa, p1), T(2.)*dist(pb, p1));
    }
}


template<typename VD>
class Lines2VDSegments
{
public:
    Lines2VDSegments(const Lines &alines) : lines(alines) {}
    typename VD::segment_type operator[](size_t idx) const {
        return typename VD::segment_type(
            typename VD::point_type(typename VD::coord_type(lines[idx].a(0)), typename VD::coord_type(lines[idx].a(1))),
            typename VD::point_type(typename VD::coord_type(lines[idx].b(0)), typename VD::coord_type(lines[idx].b(1))));
    }
private:
    const Lines &lines;
};

MedialAxis::MedialAxis(double min_width, double max_width, const ExPolygon &expolygon) :
    m_expolygon(expolygon), m_lines(expolygon.lines()), m_min_width(min_width), m_max_width(max_width)
{}

void MedialAxis::build(ThickPolylines* polylines)
{
    m_vd.construct_voronoi(m_lines.begin(), m_lines.end());





    if (!m_vd.is_valid()) {
        m_lines = to_lines(closing_ex({m_expolygon}, float(2. * SCALED_EPSILON)));
        m_vd.construct_voronoi(m_lines.begin(), m_lines.end());

        if (!m_vd.is_valid())
            BOOST_LOG_TRIVIAL(error) << "MedialAxis - Invalid Voronoi diagram even after morphological closing.";
    }

    Slic3r::Voronoi::annotate_inside_outside(m_vd, m_lines);




















    m_edge_data.assign(m_vd.edges().size() / 2, EdgeData{});
    for (VD::const_edge_iterator edge = m_vd.edges().begin(); edge != m_vd.edges().end(); edge += 2)
        if (edge->is_primary() && edge->is_finite() &&
            (Voronoi::vertex_category(edge->vertex0()) == Voronoi::VertexCategory::Inside ||
             Voronoi::vertex_category(edge->vertex1()) == Voronoi::VertexCategory::Inside) &&
            this->validate_edge(&*edge)) {

            this->edge_data(*edge).first.active = true;
        }


    ThickPolyline reverse_polyline;
    for (VD::const_edge_iterator seed_edge = m_vd.edges().begin(); seed_edge != m_vd.edges().end(); seed_edge += 2)
        if (EdgeData &seed_edge_data = this->edge_data(*seed_edge).first; seed_edge_data.active) {

            seed_edge_data.active = false;


            ThickPolyline polyline;
            polyline.points.emplace_back(seed_edge->vertex0()->x(), seed_edge->vertex0()->y());
            polyline.points.emplace_back(seed_edge->vertex1()->x(), seed_edge->vertex1()->y());
            polyline.width.emplace_back(seed_edge_data.width_start);
            polyline.width.emplace_back(seed_edge_data.width_end);

            this->process_edge_neighbors(&*seed_edge, &polyline);
            assert(polyline.width.size() == polyline.points.size() * 2 - 2);


            reverse_polyline.clear();
            this->process_edge_neighbors(seed_edge->twin(), &reverse_polyline);
            polyline.points.insert(polyline.points.begin(), reverse_polyline.points.rbegin(), reverse_polyline.points.rend());
            polyline.width.insert(polyline.width.begin(), reverse_polyline.width.rbegin(), reverse_polyline.width.rend());
            polyline.endpoints.first = reverse_polyline.endpoints.second;
            assert(polyline.width.size() == polyline.points.size() * 2 - 2);


            if (polyline.first_point() == polyline.last_point()) {
                polyline.endpoints.first = false;
                polyline.endpoints.second = false;
            }


            polylines->emplace_back(std::move(polyline));
        }

    #ifdef SLIC3R_DEBUG
    {
        static int iRun = 0;
        dump_voronoi_to_svg(m_lines, m_vd, polylines, debug_out_path("MedialAxis-%d.svg", iRun ++).c_str());
        printf("Thick lines: ");
        for (ThickPolylines::const_iterator it = polylines->begin(); it != polylines->end(); ++ it) {
            ThickLines lines = it->thicklines();
            for (ThickLines::const_iterator it2 = lines.begin(); it2 != lines.end(); ++ it2) {
                printf("%f,%f ", it2->a_width, it2->b_width);
            }
        }
        printf("\n");
    }
    #endif
}

void MedialAxis::build(Polylines* polylines)
{
    ThickPolylines tp;
    this->build(&tp);
    polylines->reserve(polylines->size() + tp.size());
    for (auto &pl : tp)
        polylines->emplace_back(pl.points);
}

void MedialAxis::process_edge_neighbors(const VD::edge_type *edge, ThickPolyline* polyline)
{
    for (;;) {



        const VD::edge_type *twin = edge->twin();


        size_t               num_neighbors  = 0;
        const VD::edge_type *first_neighbor = nullptr;
        for (const VD::edge_type *neighbor = twin->rot_next(); neighbor != twin; neighbor = neighbor->rot_next())
            if (this->edge_data(*neighbor).first.active) {
                if (num_neighbors == 0)
                    first_neighbor = neighbor;
                ++ num_neighbors;
            }


        if (num_neighbors == 1) {
            if (std::pair<EdgeData&, bool> neighbor_data = this->edge_data(*first_neighbor);
                neighbor_data.first.active) {
                neighbor_data.first.active = false;
                polyline->points.emplace_back(first_neighbor->vertex1()->x(), first_neighbor->vertex1()->y());
                if (neighbor_data.second) {
                    polyline->width.push_back(neighbor_data.first.width_end);
                    polyline->width.push_back(neighbor_data.first.width_start);
                } else {
                    polyline->width.push_back(neighbor_data.first.width_start);
                    polyline->width.push_back(neighbor_data.first.width_end);
                }
                edge = first_neighbor;

                continue;
            }
        } else if (num_neighbors == 0) {
            polyline->endpoints.second = true;
        } else {

        }

        break;
    }
}

bool MedialAxis::validate_edge(const VD::edge_type* edge)
{
    auto retrieve_segment = [this](const VD::cell_type* cell) -> const Line& { return m_lines[cell->source_index()]; };
    auto retrieve_endpoint = [retrieve_segment](const VD::cell_type* cell) -> const Point& {
        const Line &line = retrieve_segment(cell);
        return cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT ? line.a : line.b;
    };











    const Line line({ edge->vertex0()->x(), edge->vertex0()->y() },
                    { edge->vertex1()->x(), edge->vertex1()->y() });


    const VD::cell_type* cell_l = edge->cell();
    const VD::cell_type* cell_r = edge->twin()->cell();
    const Line &segment_l = retrieve_segment(cell_l);
    const Line &segment_r = retrieve_segment(cell_r);

























    coordf_t w0 = cell_r->contains_segment()
        ? segment_r.distance_to(line.a)*2
        : (retrieve_endpoint(cell_r) - line.a).cast<double>().norm()*2;

    coordf_t w1 = cell_l->contains_segment()
        ? segment_l.distance_to(line.b)*2
        : (retrieve_endpoint(cell_l) - line.b).cast<double>().norm()*2;

    if (cell_l->contains_segment() && cell_r->contains_segment()) {

        double angle = fabs(segment_r.orientation() - segment_l.orientation());
        if (angle > PI)
            angle = 2. * PI - angle;
        assert(angle >= 0 && angle <= PI);







        if (PI - angle > PI / 8.) {



            if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON || line.length() >= m_min_width)
                return false;
        }
    } else {
        if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON)
            return false;
    }

    if ((w0 >= m_min_width || w1 >= m_min_width) &&
        (w0 <= m_max_width || w1 <= m_max_width)) {
        std::pair<EdgeData&, bool> ed = this->edge_data(*edge);
        if (ed.second)
            std::swap(w0, w1);
        ed.first.width_start = w0;
        ed.first.width_end   = w1;
        return true;
    }

    return false;
}

} }
