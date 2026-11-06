/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/PointCoordinates.cpp#3 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#include "libqhullcpp/PointCoordinates.h"

#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullPoint.h"

#include <iterator>
#include <iostream>

using std::istream;
using std::string;
using std::ws;

#ifdef _MSC_VER
#pragma warning( disable : 4996)
#endif

namespace orgQhull {

#

#

PointCoordinates::
PointCoordinates()
: QhullPoints()
, point_coordinates()
, describe_points()
{
}

PointCoordinates::
PointCoordinates(const std::string &aComment)
: QhullPoints()
, point_coordinates()
, describe_points(aComment)
{
}

PointCoordinates::
PointCoordinates(int pointDimension, const std::string &aComment)
: QhullPoints()
, point_coordinates()
, describe_points(aComment)
{
    setDimension(pointDimension);
}


PointCoordinates::
PointCoordinates(const Qhull &q)
: QhullPoints(q)
, point_coordinates()
, describe_points()
{
}

PointCoordinates::
PointCoordinates(const Qhull &q, const std::string &aComment)
: QhullPoints(q)
, point_coordinates()
, describe_points(aComment)
{
}

PointCoordinates::
PointCoordinates(const Qhull &q, int pointDimension, const std::string &aComment)
: QhullPoints(q)
, point_coordinates()
, describe_points(aComment)
{
    setDimension(pointDimension);
}

PointCoordinates::
PointCoordinates(const Qhull &q, int pointDimension, const std::string &aComment, countT coordinatesCount, const coordT *c)
: QhullPoints(q)
, point_coordinates()
, describe_points(aComment)
{
    setDimension(pointDimension);
    append(coordinatesCount, c);
}

PointCoordinates::
PointCoordinates(QhullQh *qqh)
: QhullPoints(qqh)
, point_coordinates()
, describe_points()
{
}

PointCoordinates::
PointCoordinates(QhullQh *qqh, const std::string &aComment)
: QhullPoints(qqh)
, point_coordinates()
, describe_points(aComment)
{
}

PointCoordinates::
PointCoordinates(QhullQh *qqh, int pointDimension, const std::string &aComment)
: QhullPoints(qqh)
, point_coordinates()
, describe_points(aComment)
{
    setDimension(pointDimension);
}

PointCoordinates::
PointCoordinates(QhullQh *qqh, int pointDimension, const std::string &aComment, countT coordinatesCount, const coordT *c)
: QhullPoints(qqh)
, point_coordinates()
, describe_points(aComment)
{
    setDimension(pointDimension);
    append(coordinatesCount, c);
}

PointCoordinates::
PointCoordinates(const PointCoordinates &other)
: QhullPoints(other)
, point_coordinates(other.point_coordinates)
, describe_points(other.describe_points)
{
    makeValid();
}

PointCoordinates & PointCoordinates::
operator=(const PointCoordinates &other)
{
    QhullPoints::operator=(other);
    point_coordinates= other.point_coordinates;
    describe_points= other.describe_points;
    makeValid();
    return *this;
}

PointCoordinates::
~PointCoordinates()
{ }

#

void PointCoordinates::
checkValid() const
{
    if(getCoordinates().data()!=data()
    || getCoordinates().count()!=coordinateCount()){
        throw QhullError(10060, "Qhull error: first point (%x) is not PointCoordinates.data() or count (%d) is not PointCoordinates.count (%d)", coordinateCount(), getCoordinates().count(), 0.0, data());
    }
}

void PointCoordinates::
setDimension(int i)
{
    if(i<0){
        throw QhullError(10062, "Qhull error: can not set PointCoordinates dimension to %d", i);
    }
    int currentDimension=QhullPoints::dimension();
    if(currentDimension!=0 && i!=currentDimension){
        throw QhullError(10063, "Qhull error: can not change PointCoordinates dimension (from %d to %d)", currentDimension, i);
    }
    QhullPoints::setDimension(i);
}

#

Coordinates::ConstIterator PointCoordinates::
beginCoordinates(countT pointIndex) const
{
    return point_coordinates.begin()+indexOffset(pointIndex);
}

Coordinates::Iterator PointCoordinates::
beginCoordinates(countT pointIndex)
{
    return point_coordinates.begin()+indexOffset(pointIndex);
}

#

void PointCoordinates::
append(countT coordinatesCount, const coordT *c)
{
    if(coordinatesCount<=0){
        return;
    }
    if(includesCoordinates(c)){
        throw QhullError(10065, "Qhull error: can not append a subset of PointCoordinates to itself.  The coordinates for point %d may move.", indexOf(c, QhullError::NOthrow));
    }
    reserveCoordinates(coordinatesCount);
    std::copy(c, c+coordinatesCount, std::back_inserter(point_coordinates));
    makeValid();
}

void PointCoordinates::
append(const PointCoordinates &other)
{
    setDimension(other.dimension());
    append(other.coordinateCount(), other.data());
}

void PointCoordinates::
append(const QhullPoint &p)
{
    setDimension(p.dimension());
    append(p.dimension(), p.coordinates());
}

void PointCoordinates::
appendComment(const std::string &s){
    if(char c= s[0] && describe_points.empty()){
        if(c=='-' || isdigit(c)){
            throw QhullError(10028, "Qhull argument error: comments can not start with a number or minus, %s", 0, 0, 0.0, s.c_str());
        }
    }
    describe_points += s;
}



void PointCoordinates::
appendPoints(istream &in)
{
    int inDimension;
    countT inCount;
    in >> ws >> inDimension >> ws;
    if(!in.good()){
        in.clear();
        string remainder;
        getline(in, remainder);
        throw QhullError(10005, "Qhull error: input did not start with dimension or count -- %s", 0, 0, 0, remainder.c_str());
    }
    char c= (char)in.peek();
    if(c!='-' && !isdigit(c)){
        getline(in, describe_points);
        in >> ws;
    }
    in >> inCount >> ws;
    if(!in.good()){
        in.clear();
        string remainder;
        getline(in, remainder);
        throw QhullError(10009, "Qhull error: input did not start with dimension and count -- %d %s", inDimension, 0, 0, remainder.c_str());
    }
    c= (char)in.peek();
    if(c!='-' && !isdigit(c)){
        getline(in, describe_points);
        in >> ws;
    }
    if(inCount<inDimension){
        std::swap(inCount, inDimension);
    }
    setDimension(inDimension);
    reserveCoordinates(inCount*inDimension);
    countT coordinatesCount= 0;
    while(!in.eof()){
        realT p;
        in >> p >> ws;
        if(in.fail()){
            in.clear();
            string remainder;
            getline(in, remainder);
            throw QhullError(10008, "Qhull error: failed to read coordinate %d  of point %d\n   %s", coordinatesCount % inDimension, coordinatesCount/inDimension, 0, remainder.c_str());
        }else{
            point_coordinates.push_back(p);
            coordinatesCount++;
        }
    }
    if(coordinatesCount != inCount*inDimension){
        if(coordinatesCount%inDimension==0){
            throw QhullError(10006, "Qhull error: expected %d %d-d PointCoordinates but read %i PointCoordinates", int(inCount), inDimension, 0.0, int(coordinatesCount/inDimension));
        }else{
            throw QhullError(10012, "Qhull error: expected %d %d-d PointCoordinates but read %i PointCoordinates plus %f extra coordinates", inCount, inDimension, float(coordinatesCount%inDimension), coordinatesCount/inDimension);
        }
    }
    makeValid();
}

PointCoordinates PointCoordinates::
operator+(const PointCoordinates &other) const
{
    PointCoordinates pc= *this;
    pc << other;
    return pc;
}

void PointCoordinates::
reserveCoordinates(countT newCoordinates)
{

    point_coordinates.reserve((countT)point_coordinates.size()+newCoordinates);
    makeValid();
}

#

countT PointCoordinates::
indexOffset(countT i) const {
    countT n= i*dimension();
    countT coordinatesCount= point_coordinates.count();
    if(i<0 || n>coordinatesCount){
        throw QhullError(10061, "Qhull error: point_coordinates is too short (%d) for point %d", coordinatesCount, i);
    }
    return n;
}

}

#

using std::endl;
using std::ostream;

using orgQhull::Coordinates;
using orgQhull::PointCoordinates;

ostream&
operator<<(ostream &os, const PointCoordinates &p)
{
    p.checkValid();
    countT count= p.count();
    int dimension= p.dimension();
    string comment= p.comment();
    if(comment.empty()){
        os << dimension << endl;
    }else{
        os << dimension << " " << comment << endl;
    }
    os << count << endl;
    Coordinates::ConstIterator c= p.beginCoordinates();
    for(countT i=0; i<count; i++){
        for(int j=0; j<dimension; j++){
            os << *c++ << " ";
        }
        os << endl;
    }
    return os;
}
