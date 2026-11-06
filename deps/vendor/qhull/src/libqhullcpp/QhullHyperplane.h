/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullHyperplane.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHHYPERPLANE_H
#define QHHYPERPLANE_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullIterator.h"
#include "libqhullcpp/QhullQh.h"

#include <ostream>

namespace orgQhull {

#
    class Qhull;
    class QhullPoint;

#

    class QhullHyperplane;

    class QhullHyperplaneIterator;

class QhullHyperplane {
public:
#
    typedef const coordT *                  iterator;
    typedef const coordT *                  const_iterator;
    typedef QhullHyperplane::iterator       Iterator;
    typedef QhullHyperplane::const_iterator ConstIterator;

private:
#
    coordT *            hyperplane_coordinates;
    QhullQh *           qh_qh;
    coordT              hyperplane_offset;
    int                 hyperplane_dimension;

#
public:
                        QhullHyperplane() : hyperplane_coordinates(0), qh_qh(0), hyperplane_offset(0.0), hyperplane_dimension(0) {}
    explicit            QhullHyperplane(const Qhull &q);
                        QhullHyperplane(const Qhull &q, int hyperplaneDimension, coordT *c, coordT hyperplaneOffset);
    explicit            QhullHyperplane(QhullQh *qqh) : hyperplane_coordinates(0), qh_qh(qqh), hyperplane_offset(0.0), hyperplane_dimension(0) {}
                        QhullHyperplane(QhullQh *qqh, int hyperplaneDimension, coordT *c, coordT hyperplaneOffset) : hyperplane_coordinates(c), qh_qh(qqh), hyperplane_offset(hyperplaneOffset), hyperplane_dimension(hyperplaneDimension) {}

                        QhullHyperplane(const QhullHyperplane &other)  : hyperplane_coordinates(other.hyperplane_coordinates), qh_qh(other.qh_qh), hyperplane_offset(other.hyperplane_offset), hyperplane_dimension(other.hyperplane_dimension) {}

    QhullHyperplane &   operator=(const QhullHyperplane &other) { hyperplane_coordinates= other.hyperplane_coordinates; qh_qh= other.qh_qh; hyperplane_offset= other.hyperplane_offset; hyperplane_dimension= other.hyperplane_dimension; return *this; }
                        ~QhullHyperplane() {}

#

#ifndef QHULL_NO_STL
    std::vector<coordT> toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<coordT>       toQList() const;
#endif

#
public:
    const coordT *      coordinates() const { return hyperplane_coordinates; }
    coordT *            coordinates() { return hyperplane_coordinates; }
    void                defineAs(int hyperplaneDimension, coordT *c, coordT hyperplaneOffset) { QHULL_ASSERT(hyperplaneDimension>=0); hyperplane_coordinates= c; hyperplane_dimension= hyperplaneDimension; hyperplane_offset= hyperplaneOffset; }

    void                defineAs(QhullHyperplane &other) { hyperplane_coordinates= other.coordinates(); hyperplane_dimension= other.dimension();  hyperplane_offset= other.offset(); }
    int                 dimension() const { return hyperplane_dimension; }
    bool                isValid() const { return hyperplane_coordinates!=0 && hyperplane_dimension>0; }
    coordT              offset() const { return hyperplane_offset; }
    bool                operator==(const QhullHyperplane &other) const;
    bool                operator!=(const QhullHyperplane &other) const { return !operator==(other); }
    const coordT &      operator[](int idx) const { QHULL_ASSERT(idx>=0 && idx<hyperplane_dimension); return *(hyperplane_coordinates+idx); }
    coordT &            operator[](int idx) { QHULL_ASSERT(idx>=0 && idx<hyperplane_dimension); return *(hyperplane_coordinates+idx); }
    void                setCoordinates(coordT *c) { hyperplane_coordinates= c; }
    void                setDimension(int hyperplaneDimension) { hyperplane_dimension= hyperplaneDimension; }
    void                setOffset(coordT hyperplaneOffset) { hyperplane_offset= hyperplaneOffset; }

#
    iterator            begin() { return hyperplane_coordinates; }
    const_iterator      begin() const { return hyperplane_coordinates; }
    const_iterator      constBegin() const { return hyperplane_coordinates; }
    const_iterator      constEnd() const { return hyperplane_coordinates+hyperplane_dimension; }
    int                 count() { return hyperplane_dimension; }
    iterator            end() { return hyperplane_coordinates+hyperplane_dimension; }
    const_iterator      end() const { return hyperplane_coordinates+hyperplane_dimension; }
    size_t              size() { return (size_t)hyperplane_dimension; }

#
    double              distance(const QhullPoint &p) const;
    double              hyperplaneAngle(const QhullHyperplane &other) const;
    double              norm() const;

#
    struct PrintHyperplane{
        const QhullHyperplane  *hyperplane;
        const char *    print_message;
        const char *    hyperplane_offset_message;
                        PrintHyperplane(const char *message, const char *offsetMessage, const QhullHyperplane &p) : hyperplane(&p), print_message(message), hyperplane_offset_message(offsetMessage) {}
    };
    PrintHyperplane          print(const char *message) const { return PrintHyperplane(message, "", *this); }
    PrintHyperplane          print(const char *message, const char *offsetMessage) const { return PrintHyperplane(message, offsetMessage, *this); }

};

QHULL_DECLARE_SEQUENTIAL_ITERATOR(QhullHyperplane, coordT)

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullHyperplane::PrintHyperplane &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullHyperplane &p);

#endif
