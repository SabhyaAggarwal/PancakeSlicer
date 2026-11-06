#ifndef MINAREABOUNDINGBOX_HPP
#define MINAREABOUNDINGBOX_HPP

#include "libslic3r/Point.hpp"

namespace Slic3r {

class Polygon;
class ExPolygon;

void remove_collinear_points(Polygon& p);
void remove_collinear_points(ExPolygon& p);





class MinAreaBoundigBox {
    Point m_axis;
    long double m_bottom = 0.0l, m_right = 0.0l;
public:


    enum PolygonLevel {
        pcConvex, pcSimple
    };




    explicit MinAreaBoundigBox(const Polygon&, PolygonLevel = pcSimple);
    explicit MinAreaBoundigBox(const ExPolygon&, PolygonLevel = pcSimple);
    explicit MinAreaBoundigBox(const Points&, PolygonLevel = pcSimple);



    double angle_to_X()  const;


    long double width()  const;


    long double height() const;


    long double area()   const;



    const Point& axis()  const { return m_axis; }
};

}

#endif
