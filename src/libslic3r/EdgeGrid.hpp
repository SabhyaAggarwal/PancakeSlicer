#ifndef slic3r_EdgeGrid_hpp_
#define slic3r_EdgeGrid_hpp_

#include <cstddef>
#include <cassert>
#include <limits>
#include <cstdlib>
#include <algorithm>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <utility>
#include <string>

#include "Line.hpp"
#include "Point.hpp"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {
namespace EdgeGrid {


class Contour {
public:
	Contour() = default;
	Contour(const Slic3r::Point *begin, const Slic3r::Point *end, bool open) : m_begin(begin), m_end(end), m_open(open) {}
	Contour(const Slic3r::Point *data, size_t size, bool open) : Contour(data, data + size, open) {}
	Contour(const Points &pts, bool open) : Contour(pts.data(), pts.size(), open) {}

    const Slic3r::Point *begin()  const { return m_begin; }
    const Slic3r::Point *end()    const { return m_end; }
    bool                 open()   const { return m_open; }
    bool                 closed() const { return !m_open; }

    const Slic3r::Point &front()  const { return *m_begin; }
    const Slic3r::Point &back()   const { return *(m_end - 1); }


	const Slic3r::Point& segment_start(size_t idx) const {
		assert(idx < this->num_segments());
		return m_begin[idx];
	}


	const Slic3r::Point& segment_end(size_t idx) const {
		assert(idx < this->num_segments());
		const Slic3r::Point *ptr = m_begin + idx + 1;
		return ptr == m_end ? *m_begin : *ptr;
	}


	const Slic3r::Point& segment_prev(size_t idx) const {
		assert(idx < this->num_segments());
		assert(idx > 0 || ! m_open);
		return idx == 0 ? m_end[-1] : m_begin[idx - 1];
	}


	const size_t 		 segment_idx_prev(size_t idx) const {
		assert(idx < this->num_segments());
		assert(idx > 0 || ! m_open);
		return (idx == 0 ? this->size() : idx) - 1;
	}


	const size_t 		 segment_idx_next(size_t idx) const {
		assert(idx < this->num_segments());
		++ idx;
		return m_begin + idx == m_end ? 0 : idx;
	}

	size_t               num_segments() const { return this->size() - (m_open ? 1 : 0); }

    Line                 get_segment(size_t idx) const
    {
        assert(idx < this->num_segments());
        return Line(this->segment_start(idx), this->segment_end(idx));
    }

    Lines                get_segments() const
    {
        Lines lines;
        lines.reserve(this->num_segments());
        if (this->num_segments() > 2) {
            for (auto it = this->begin(); it != this->end() - 1; ++it) lines.push_back(Line(*it, *(it + 1)));
            if (!m_open) lines.push_back(Line(this->back(), this->front()));
        }
        return lines;
    }

private:
	size_t  			 size() const { return m_end - m_begin; }

	const Slic3r::Point *m_begin { nullptr };
	const Slic3r::Point *m_end   { nullptr };
	bool                 m_open  { false };
};

class Grid
{
public:
	Grid() = default;
	Grid(const BoundingBox &bbox) : m_bbox(bbox) {}

	void set_bbox(const BoundingBox &bbox) { m_bbox = bbox; }







	void create(const std::vector<Points> &polylines_or_polygons, coord_t resolution, bool open);
	void create(const Polygons &polygons, const Polylines &polylines, coord_t resolution);


	void create(const Polygons &polygons, coord_t resolution);
	void create(const std::vector<const Polygon*> &polygons, coord_t resolution);
	void create(const std::vector<Points> &polygons, coord_t resolution) { this->create(polygons, resolution, false); }
	void create(const ExPolygon &expoly, coord_t resolution);
	void create(const ExPolygons &expolygons, coord_t resolution);

	const std::vector<Contour>& contours() const { return m_contours; }

#if 0

	bool intersect(const MultiPoint &polyline, bool closed);
	bool intersect(const Polygon &polygon) { return intersect(static_cast<const MultiPoint&>(polygon), true); }
	bool intersect(const Polygons &polygons) { for (size_t i = 0; i < polygons.size(); ++ i) if (intersect(polygons[i])) return true; return false; }
	bool intersect(const ExPolygon &expoly) { if (intersect(expoly.contour)) return true; for (size_t i = 0; i < expoly.holes.size(); ++ i) if (intersect(expoly.holes[i])) return true; return false; }
	bool intersect(const ExPolygons &expolygons) { for (size_t i = 0; i < expolygons.size(); ++ i) if (intersect(expolygons[i])) return true; return false; }


	bool inside(const Point &pt);
#endif




	void calculate_sdf();


	float signed_distance_bilinear(const Point &pt) const;



	struct ClosestPointResult {
		size_t contour_idx  	= size_t(-1);
		size_t start_point_idx  = size_t(-1);

		double distance 		= std::numeric_limits<double>::max();

		double t 				= 0.;

		bool valid() const { return contour_idx != size_t(-1); }
	};
	ClosestPointResult closest_point_signed_distance(const Point &pt, coord_t search_radius) const;


	bool signed_distance_edges(const Point &pt, coord_t search_radius, coordf_t &result_min_dist, bool *pon_segment = nullptr) const;




	bool signed_distance(const Point &pt, coord_t search_radius, coordf_t &result_min_dist) const;

	const BoundingBox& 	bbox() const { return m_bbox; }
	const coord_t 		resolution() const { return m_resolution; }
	const size_t		rows() const { return m_rows; }
	const size_t		cols() const { return m_cols; }


	Polygons 			contours_simplified(coord_t offset, bool fill_holes) const;

	typedef std::pair<const Contour*, size_t> ContourPoint;
	typedef std::pair<const Contour*, size_t> ContourEdge;
	std::vector<std::pair<ContourEdge, ContourEdge>> intersecting_edges() const;
	bool 											 has_intersecting_edges() const;

	template<typename VISITOR> void visit_cells_intersecting_line(Slic3r::Point p1, Slic3r::Point p2, VISITOR &visitor) const
	{

		assert(m_bbox.contains(p1));
		assert(m_bbox.contains(p2));
		p1 -= m_bbox.min;
		p2 -= m_bbox.min;
        assert(p1.x() >= 0 && size_t(p1.x()) < m_cols * m_resolution);
        assert(p1.y() >= 0 && size_t(p1.y()) < m_rows * m_resolution);
        assert(p2.x() >= 0 && size_t(p2.x()) < m_cols * m_resolution);
        assert(p2.y() >= 0 && size_t(p2.y()) < m_rows * m_resolution);

		coord_t ix = p1(0) / m_resolution;
		coord_t iy = p1(1) / m_resolution;
		coord_t ixb = p2(0) / m_resolution;
		coord_t iyb = p2(1) / m_resolution;
		assert(ix >= 0 && size_t(ix) < m_cols);
		assert(iy >= 0 && size_t(iy) < m_rows);
		assert(ixb >= 0 && size_t(ixb) < m_cols);
		assert(iyb >= 0 && size_t(iyb) < m_rows);

		if (! visitor(iy, ix) || (ix == ixb && iy == iyb))

			return;

		coord_t dx = std::abs(p2(0) - p1(0));
		coord_t dy = std::abs(p2(1) - p1(1));
		if (p1(0) < p2(0)) {
			int64_t ex = int64_t((ix + 1)*m_resolution - p1(0)) * int64_t(dy);
			if (p1(1) < p2(1)) {

				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix <= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
						assert(ix <= ixb);
					}
					else if (ex == ey) {
						ex = int64_t(dy) * m_resolution;
						ey = int64_t(dx) * m_resolution;
						ix += 1;
						iy += 1;
						assert(ix <= ixb);
						assert(iy <= iyb);
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
						assert(iy <= iyb);
					}
					if (ix < 0 || iy < 0 || ix >= (int64_t)m_cols || iy >= (int64_t)m_rows)
						return;
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
			else {

				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix <= ixb && iy >= iyb);
					if (ex <= ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
						assert(ix <= ixb);
					}
					else {
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
						assert(iy >= iyb);
					}
					if (ix < 0 || iy < 0 || ix >= (int64_t)m_cols || iy >= (int64_t)m_rows)
						return;
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
		}
		else {
			int64_t ex = int64_t(p1(0) - ix*m_resolution) * int64_t(dy);
			if (p1(1) < p2(1)) {

				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix >= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
						assert(ix >= ixb);
					}
					else {
						assert(ex >= ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
						assert(iy <= iyb);
					}
					if (ix < 0 || iy < 0 || ix >= (int64_t)m_cols || iy >= (int64_t)m_rows)
						return;
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
			else {

				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix >= ixb && iy >= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
						assert(ix >= ixb);
					}
					else if (ex == ey) {



						if (dx > 0) {
							ex = int64_t(dy) * m_resolution;
							ix -= 1;
							assert(ix >= ixb);
						}
						if (dy > 0) {
							ey = int64_t(dx) * m_resolution;
							iy -= 1;
							assert(iy >= iyb);
						}
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
						assert(iy >= iyb);
					}
					if (ix < 0 || iy < 0 || ix >= (int64_t)m_cols || iy >= (int64_t)m_rows)
						return;
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
		}
	}

	template<typename VISITOR> void visit_cells_intersecting_box(BoundingBox bbox, VISITOR &visitor) const
	{

		bbox.min -= m_bbox.min;
		bbox.max -= m_bbox.min + Point(1, 1);

		bbox.min /= m_resolution;
		bbox.max /= m_resolution;

		bbox.min.x() = std::max<coord_t>(bbox.min.x(), 0);
		bbox.min.y() = std::max<coord_t>(bbox.min.y(), 0);
		bbox.max.x() = std::min<coord_t>(bbox.max.x(), (coord_t)m_cols - 1);
		bbox.max.y() = std::min<coord_t>(bbox.max.y(), (coord_t)m_rows - 1);
		for (coord_t iy = bbox.min.y(); iy <= bbox.max.y(); ++ iy)
			for (coord_t ix = bbox.min.x(); ix <= bbox.max.x(); ++ ix)
				if (! visitor(iy, ix))
					return;
	}

    std::pair<std::vector<std::pair<size_t, size_t>>::const_iterator, std::vector<std::pair<size_t, size_t>>::const_iterator> cell_data_range(coord_t row, coord_t col) const
	{
        assert(row >= 0 && size_t(row) < m_rows);
        assert(col >= 0 && size_t(col) < m_cols);
		const EdgeGrid::Grid::Cell &cell = m_cells[row * m_cols + col];
		return std::make_pair(m_cell_data.begin() + cell.begin, m_cell_data.begin() + cell.end);
	}

	std::pair<const Slic3r::Point&, const Slic3r::Point&> segment(const std::pair<size_t, size_t> &contour_and_segment_idx) const
	{
		const Contour &contour = m_contours[contour_and_segment_idx.first];
		size_t iseg = contour_and_segment_idx.second;
		return std::pair<const Slic3r::Point&, const Slic3r::Point&>(contour.segment_start(iseg), contour.segment_end(iseg));
	}

	Line line(const std::pair<size_t, size_t> &contour_and_segment_idx) const
	{
		const Contour &contour = m_contours[contour_and_segment_idx.first];
		size_t iseg = contour_and_segment_idx.second;
		return Line(contour.segment_start(iseg), contour.segment_end(iseg));
	}

protected:
	struct Cell {
		Cell() : begin(0), end(0) {}
		size_t begin;
		size_t end;
	};

	void create_from_m_contours(coord_t resolution);
#if 0
	bool line_cell_intersect(const Point &p1, const Point &p2, const Cell &cell);
#endif
	bool cell_inside_or_crossing(int r, int c) const
	{
		if (r < 0 || (size_t)r >= m_rows ||
			c < 0 || (size_t)c >= m_cols)


			return false;
		const Cell &cell = m_cells[r * m_cols + c];
		return
			(cell.begin < cell.end) ||
			(! m_signed_distance_field.empty() && m_signed_distance_field[r * (m_cols + 1) + c] <= 0.f);
	}


	BoundingBox 								m_bbox;

	coord_t										m_resolution;
	size_t										m_rows = 0;
	size_t										m_cols = 0;




	std::vector<Contour>						m_contours;


	std::vector<std::pair<size_t, size_t> >		m_cell_data;


	std::vector<Cell> 							m_cells;



	std::vector<float>							m_signed_distance_field;
};


extern void save_png(const Grid &grid, const BoundingBox &bbox, coord_t resolution, const char *path, size_t scale = 1);

}


extern std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> intersecting_edges(const Polygons &polygons);


extern void export_intersections_to_svg(const std::string &filename, const Polygons &polygons);

}

#endif
