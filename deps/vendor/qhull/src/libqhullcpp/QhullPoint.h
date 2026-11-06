/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullPoint.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHPOINT_H
#define QHPOINT_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullIterator.h"
#include "libqhullcpp/QhullQh.h"
#include "libqhullcpp/Coordinates.h"

#include <ostream>

namespace orgQhull {

#
    class QhullPoint;
    class QhullPointIterator;

#
    class Qhull;



class QhullPoint {

#
public:
    typedef coordT *                    base_type;
    typedef const coordT *              iterator;
    typedef const coordT *              const_iterator;
    typedef QhullPoint::iterator        Iterator;
    typedef QhullPoint::const_iterator  ConstIterator;

#
protected:
    coordT *            point_coordinates;
    QhullQh *           qh_qh;


    int                 point_dimension;
public:

#



                        QhullPoint() : point_coordinates(0), qh_qh(0), point_dimension(0) {}
                        QhullPoint(int pointDimension, coordT *c) : point_coordinates(c), qh_qh(0), point_dimension(pointDimension) { QHULL_ASSERT(pointDimension>0); }
    explicit            QhullPoint(const Qhull &q);
                        QhullPoint(const Qhull &q, coordT *c);
                        QhullPoint(const Qhull &q, Coordinates &c);
                        QhullPoint(const Qhull &q, int pointDimension, coordT *c);
    explicit            QhullPoint(QhullQh *qqh) : point_coordinates(0), qh_qh(qqh), point_dimension(qqh->hull_dim) {}
                        QhullPoint(QhullQh *qqh, coordT *c) : point_coordinates(c), qh_qh(qqh), point_dimension(qqh->hull_dim) { QHULL_ASSERT(qqh->hull_dim>0); }
                        QhullPoint(QhullQh *qqh, Coordinates &c) : point_coordinates(c.data()), qh_qh(qqh), point_dimension(c.count()) {}
                        QhullPoint(QhullQh *qqh, int pointDimension, coordT *c) : point_coordinates(c), qh_qh(qqh), point_dimension(pointDimension) {}

                        QhullPoint(const QhullPoint &other) : point_coordinates(other.point_coordinates), qh_qh(other.qh_qh), point_dimension(other.point_dimension) {}

    QhullPoint &        operator=(const QhullPoint &other) { point_coordinates= other.point_coordinates; qh_qh= other.qh_qh; point_dimension= other.point_dimension; return *this; }
                        ~QhullPoint() {}


#

#ifndef QHULL_NO_STL
    std::vector<coordT> toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<coordT>       toQList() const;
#endif

#
public:
    const coordT *      coordinates() const { return point_coordinates; }
    coordT *            coordinates() { return point_coordinates; }
    void                defineAs(coordT *c) { QHULL_ASSERT(point_dimension>0); point_coordinates= c; }
    void                defineAs(int pointDimension, coordT *c) { QHULL_ASSERT(pointDimension>=0); point_coordinates= c; point_dimension= pointDimension; }
    void                defineAs(QhullPoint &other) { point_coordinates= other.point_coordinates; qh_qh= other.qh_qh; point_dimension= other.point_dimension; }
    int                 dimension() const { return point_dimension; }
    coordT *            getBaseT() const { return point_coordinates; }
    countT              id() const { return qh_pointid(qh_qh, point_coordinates); }
    bool                isValid() const { return (point_coordinates!=0 && point_dimension>0); };
    bool                operator==(const QhullPoint &other) const;
    bool                operator!=(const QhullPoint &other) const { return ! operator==(other); }
    const coordT &      operator[](int idx) const { QHULL_ASSERT(point_coordinates!=0 && idx>=0 && idx<point_dimension); return *(point_coordinates+idx); }
    coordT &            operator[](int idx) { QHULL_ASSERT(point_coordinates!=0 && idx>=0 && idx<point_dimension); return *(point_coordinates+idx); }
    QhullQh *           qh() { return qh_qh; }
    void                setCoordinates(coordT *c) { point_coordinates= c; }
    void                setDimension(int pointDimension) { point_dimension= pointDimension; }

#
    iterator            begin() { return point_coordinates; }
    const_iterator      begin() const { return point_coordinates; }
    const_iterator      constBegin() const { return point_coordinates; }
    const_iterator      constEnd() const { return (point_coordinates ? point_coordinates+point_dimension : 0); }
    int                 count() { return (point_coordinates ? point_dimension : 0); }
    iterator            end() { return (point_coordinates ? point_coordinates+point_dimension : 0); }
    const_iterator      end() const { return (point_coordinates ? point_coordinates+point_dimension : 0); }
    size_t              size() { return (size_t)(point_coordinates ? point_dimension : 0); }

#
    void                advancePoint(countT idx) { if(point_coordinates) { point_coordinates += idx*point_dimension; } }
    double              distance(const QhullPoint &p) const;

#

    struct PrintPoint{
        const QhullPoint *point;
        const char *    point_message;
        bool            with_identifier;
                        PrintPoint(const char *message, bool withIdentifier, const QhullPoint &p) : point(&p), point_message(message), with_identifier(withIdentifier) {}
    };
    PrintPoint          print(const char *message) const { return PrintPoint(message, false, *this); }
    PrintPoint          printWithIdentifier(const char *message) const { return PrintPoint(message, true, *this); }

};

QHULL_DECLARE_SEQUENTIAL_ITERATOR(QhullPoint, coordT)

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullPoint::PrintPoint &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullPoint &p);

#endif
