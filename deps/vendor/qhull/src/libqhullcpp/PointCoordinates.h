/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/PointCoordinates.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHPOINTCOORDINATES_H
#define QHPOINTCOORDINATES_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullPoints.h"
#include "libqhullcpp/Coordinates.h"

#include <ostream>
#include <string>

#ifndef QHULL_NO_STL
#include <vector>
#endif

namespace orgQhull {

#


    class PointCoordinates;

class PointCoordinates : public QhullPoints {

private:
#
    Coordinates         point_coordinates;

    std::string         describe_points;

public:
#


                        PointCoordinates();
    explicit            PointCoordinates(const std::string &aComment);
                        PointCoordinates(int pointDimension, const std::string &aComment);

    explicit            PointCoordinates(const Qhull &q);
                        PointCoordinates(const Qhull &q, const std::string &aComment);
                        PointCoordinates(const Qhull &q, int pointDimension, const std::string &aComment);
                        PointCoordinates(const Qhull &q, int pointDimension, const std::string &aComment, countT coordinatesCount, const coordT *c);

    explicit            PointCoordinates(QhullQh *qqh);
                        PointCoordinates(QhullQh *qqh, const std::string &aComment);
                        PointCoordinates(QhullQh *qqh, int pointDimension, const std::string &aComment);
                        PointCoordinates(QhullQh *qqh, int pointDimension, const std::string &aComment, countT coordinatesCount, const coordT *c);

                        PointCoordinates(const PointCoordinates &other);
    PointCoordinates &  operator=(const PointCoordinates &other);
                        ~PointCoordinates();

#

#ifndef QHULL_NO_STL
    void                append(const std::vector<coordT> &otherCoordinates) { if(!otherCoordinates.empty()){ append((int)otherCoordinates.size(), &otherCoordinates[0]); } }
    std::vector<coordT> toStdVector() const { return point_coordinates.toStdVector(); }
#endif
#ifdef QHULL_USES_QT
    void                append(const QList<coordT> &pointCoordinates) { if(!pointCoordinates.isEmpty()){ append(pointCoordinates.count(), &pointCoordinates[0]); } }
    QList<coordT>       toQList() const { return point_coordinates.toQList(); }
#endif

#

    void                checkValid() const;
    std::string         comment() const { return describe_points; }
    void                makeValid() { defineAs(point_coordinates.count(), point_coordinates.data()); }
    const Coordinates & getCoordinates() const { return point_coordinates; }
    void                setComment(const std::string &s) { describe_points= s; }
    void                setDimension(int i);

private:

    void                defineAs(countT coordinatesCount, coordT *c) { QhullPoints::defineAs(coordinatesCount, c); }
public:

#


#

    Coordinates::ConstIterator  beginCoordinates() const { return point_coordinates.begin(); }
    Coordinates::Iterator       beginCoordinates() { return point_coordinates.begin(); }
    Coordinates::ConstIterator  beginCoordinates(countT pointIndex) const;
    Coordinates::Iterator       beginCoordinates(countT pointIndex);
    Coordinates::ConstIterator  endCoordinates() const { return point_coordinates.end(); }
    Coordinates::Iterator       endCoordinates() { return point_coordinates.end(); }

#


#
    PointCoordinates    operator+(const PointCoordinates &other) const;

#

    void                append(countT coordinatesCount, const coordT *c);
    void                append(const coordT &c) { append(1, &c); }
    void                append(const QhullPoint &p);

    void                append(const Coordinates &c) { append(c.count(), c.data()); }
    void                append(const PointCoordinates &other);
    void                appendComment(const std::string &s);
    void                appendPoints(std::istream &in);
    PointCoordinates &  operator+=(const PointCoordinates &other) { append(other); return *this; }
    PointCoordinates &  operator+=(const coordT &c) { append(c); return *this; }
    PointCoordinates &  operator+=(const QhullPoint &p) { append(p); return *this; }
    PointCoordinates &  operator<<(const PointCoordinates &other) { return *this += other; }
    PointCoordinates &  operator<<(const coordT &c) { return *this += c; }
    PointCoordinates &  operator<<(const QhullPoint &p) { return *this += p; }

    void                reserveCoordinates(countT newCoordinates);

#
private:
    int                 indexOffset(int i) const;

};


class PointCoordinatesIterator
{
    typedef PointCoordinates::const_iterator const_iterator;

private:
    const PointCoordinates *c;
    const_iterator      i;

public:
                        PointCoordinatesIterator(const PointCoordinates &container) : c(&container), i(c->constBegin()) {}
                        PointCoordinatesIterator &operator=(const PointCoordinates &container) { c = &container; i = c->constBegin(); return *this; }

    void                toFront() { i = c->constBegin(); }
    void                toBack() { i = c->constEnd(); }
    bool                hasNext() const { return i != c->constEnd(); }
    const QhullPoint    next() { return *i++; }
    const QhullPoint    peekNext() const { return *i; }
    bool                hasPrevious() const { return i != c->constBegin(); }
    const QhullPoint    previous() { return *--i; }
    const QhullPoint    peekPrevious() const { const_iterator p = i; return *--p; }
    bool                findNext(const QhullPoint &t) { while(i != c->constEnd()){ if (*i++ == t) return true;} return false; }
    bool                findPrevious(const QhullPoint &t) { while(i != c->constBegin()){ if (*(--i) == t) return true;} return false;  }
};


\
}

#

std::ostream &          operator<<(std::ostream &os, const orgQhull::PointCoordinates &p);

#endif
