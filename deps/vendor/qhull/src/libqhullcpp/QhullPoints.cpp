/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullPoints.cpp#3 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#include "libqhullcpp/QhullPoints.h"

#include "libqhullcpp/Qhull.h"

#include <iostream>

#ifndef QHULL_NO_STL
#include <vector>
#endif

#ifdef _MSC_VER
#endif

namespace orgQhull {

#

QhullPoints::
QhullPoints(const Qhull &q)
: point_first(0)
, point_end(0)
, qh_qh(q.qh())
, point_dimension(q.hullDimension())
{
}

QhullPoints::
QhullPoints(const Qhull &q, countT coordinateCount2, coordT *c)
: point_first(c)
, point_end(c+coordinateCount2)
, qh_qh(q.qh())
, point_dimension(q.hullDimension())
{
    QHULL_ASSERT(q.hullDimension());
    QHULL_ASSERT(coordinateCount2>=0);
}

QhullPoints::
QhullPoints(const Qhull &q, int pointDimension, countT coordinateCount2, coordT *c)
: point_first(c)
, point_end(c+coordinateCount2)
, qh_qh(q.qh())
, point_dimension(pointDimension)
{
    QHULL_ASSERT(pointDimension>=0);
    QHULL_ASSERT(coordinateCount2>=0);
}

QhullPoints::
QhullPoints(QhullQh *qqh, int pointDimension, countT coordinateCount2, coordT *c)
: point_first(c)
, point_end(c+coordinateCount2)
, qh_qh(qqh)
, point_dimension(pointDimension)
{
    QHULL_ASSERT(pointDimension>=0);
    QHULL_ASSERT(coordinateCount2>=0);
}

#


#ifndef QHULL_NO_STL
std::vector<QhullPoint> QhullPoints::
toStdVector() const
{
    QhullPointsIterator i(*this);
    std::vector<QhullPoint> vs;
    while(i.hasNext()){
        vs.push_back(i.next());
    }
    return vs;
}
#endif

#

countT QhullPoints::
extraCoordinatesCount() const
{
    if(point_dimension>0){
        return (countT)((point_end-point_first)%(size_t)point_dimension);
    }
    return 0;
}



bool QhullPoints::
operator==(const QhullPoints &other) const
{
    if((point_end-point_first) != (other.point_end-other.point_first)){
        return false;
    }
    if(point_dimension!=other.point_dimension){
        return false;
    }
    if(point_first==other.point_first){
        return true;
    }
    if(!qh_qh || qh_qh->hull_dim==0){
        const coordT *c= point_first;
        const coordT *c2= other.point_first;
        while(c<point_end){
            if(*c++!=*c2++){
                return false;
            }
        }
    }else{
        const_iterator i= begin();
        const_iterator i2= other.begin();
        while(i<end()){
            if(*i++!=*i2++){
                return false;
            }
        }
    }
    return true;
}



void QhullPoints::
resetQhullQh(QhullQh *qqh)
{
    qh_qh= qqh;
    point_dimension= (qqh ? qqh->hull_dim : 0);
    point_first= 0;
    point_end= 0;
}

QhullPoint QhullPoints::
value(countT idx) const
{
    QhullPoint p(qh_qh);
    if(idx>=0 && idx<count()){
        p.defineAs(point_dimension, point_first+idx*point_dimension);
    }
    return p;
}

QhullPoint QhullPoints::
value(countT idx, QhullPoint &defaultValue) const
{
    QhullPoint p(qh_qh);
    if(idx>=0 && idx<count()){
        p.defineAs(point_dimension, point_first+idx*point_dimension);
    }else{
        p.defineAs(defaultValue);
    }
    return p;
}

#

bool QhullPoints::
contains(const QhullPoint &t) const
{
    const_iterator i= begin();
    while(i != end()){
        if(*i==t){
            return true;
        }
        i++;
    }
    return false;
}

countT QhullPoints::
count(const QhullPoint &t) const
{
    countT n= 0;
    const_iterator i= begin();
    while(i != end()){
        if(*i==t){
            ++n;
        }
        i++;
    }
    return n;
}

countT QhullPoints::
indexOf(const coordT *pointCoordinates) const
{
    if(!includesCoordinates(pointCoordinates) || point_dimension==0){
        return -1;
    }
    size_t offset= pointCoordinates-point_first;
    countT idx= (countT)(offset/(size_t)point_dimension);
    countT extra= (countT)(offset%(size_t)point_dimension);
    if(extra!=0){
        throw QhullError(10066, "Qhull error: coordinates %x are not at point boundary (extra %d at index %d)", extra, idx, 0.0, pointCoordinates);
    }
    return idx;
}

countT QhullPoints::
indexOf(const coordT *pointCoordinates, int noThrow) const
{
    size_t extra= 0;
    if(noThrow){
        if(!includesCoordinates(pointCoordinates) || point_dimension==0){
            return -1;
        }
        extra= (pointCoordinates-point_first)%(size_t)point_dimension;
    }
    return indexOf(pointCoordinates-extra);
}

countT QhullPoints::
indexOf(const QhullPoint &t) const
{
    countT j=0;
    const_iterator i= begin();
    while(i!=end()){
        if(*i==t){
            return j;
        }
        ++i;
        ++j;
    }
    return -1;
}

countT QhullPoints::
lastIndexOf(const QhullPoint &t) const
{
    countT j= count();
    const_iterator i= end();
    while(i != begin()){
        --i;
        --j;
        if(*i==t){
            return j;
        }
    }
    return -1;
}

QhullPoints QhullPoints::
mid(countT idx, countT length) const
{
    countT n= count();
    if(idx<0 || idx>=n){
        n= 0;
    }else if(length<0 || idx+length>=n){
        n -= idx;
    }else{
        n -= idx+length;
    }
    return QhullPoints(qh_qh, point_dimension, n*point_dimension, point_first+idx*point_dimension);
}

#

bool QhullPointsIterator::
findNext(const QhullPoint &p)
{
    while(i!=ps->constEnd()){
        if(*i++ == p){
            return true;
        }
    }
    return false;
}

bool QhullPointsIterator::
findPrevious(const QhullPoint &p)
{
    while(i!=ps->constBegin()){
        if(*--i == p){
            return true;
        }
    }
    return false;
}

}

#

using std::ostream;
using orgQhull::QhullPoint;
using orgQhull::QhullPoints;
using orgQhull::QhullPointsIterator;

ostream &
operator<<(ostream &os, const QhullPoints &p)
{
    QhullPointsIterator i(p);
    while(i.hasNext()){
        os << i.next();
    }
    return os;
}

ostream &
operator<<(ostream &os, const QhullPoints::PrintPoints &pr)
{
    os << pr.point_message;
    QhullPoints ps= *pr.points;
    for(QhullPoints::iterator i=ps.begin(); i!=ps.end(); ++i){
        QhullPoint p= *i;
        if(pr.with_identifier){
            os << p.printWithIdentifier("");
        }else{
            os << p.print("");
        }
    }
    return os;
}
