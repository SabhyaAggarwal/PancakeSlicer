#ifndef slic3r_ExPolygonSerialize_hpp_
#define slic3r_ExPolygonSerialize_hpp_

#include "ExPolygon.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

#include <cereal/access.hpp>
namespace cereal {

template<class Archive> 
void serialize(Archive &archive, Slic3r::Polygon &polygon) {	
	archive(polygon.points);
}

template<class Archive> 
void serialize(Archive &archive, Slic3r::ExPolygon &expoly) {
	archive(expoly.contour, expoly.holes); 
}

}
#endif
