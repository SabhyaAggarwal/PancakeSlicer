#ifndef slic3r_PolygonTrimmer_hpp_
#define slic3r_PolygonTrimmer_hpp_

#include "Point.hpp"
#include <vector>
#include <string>
#include "Polygon.hpp"

namespace Slic3r {

namespace EdgeGrid {
	class Grid;
}

struct TrimmedLoop
{
	Points          			points;
	std::vector<unsigned int> 	segments;

	bool 	is_trimmed() const { return ! segments.empty(); }
};

TrimmedLoop trim_loop(const Polygon &loop, const EdgeGrid::Grid &grid);
std::vector<TrimmedLoop> trim_loops(const Polygons &loops, const EdgeGrid::Grid &grid);

}

#endif
