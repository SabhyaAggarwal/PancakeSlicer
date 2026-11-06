/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullRidge.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLRIDGE_H
#define QHULLRIDGE_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullSet.h"
#include "libqhullcpp/QhullVertex.h"
#include "libqhullcpp/QhullVertexSet.h"
#include "libqhullcpp/QhullFacet.h"

#include <ostream>

namespace orgQhull {

#
    class Qhull;
    class QhullVertex;
    class QhullVertexSet;
    class QhullFacet;

#

    class QhullRidge;
    typedef QhullSet<QhullRidge>  QhullRidgeSet;
    typedef QhullSetIterator<QhullRidge>  QhullRidgeSetIterator;

















class QhullRidge {

#
public:
    typedef ridgeT *   base_type;

#
private:
    ridgeT *            qh_ridge;
    QhullQh *           qh_qh;

#
    static ridgeT       s_empty_ridge;

public:
#

#
                        QhullRidge() : qh_ridge(&s_empty_ridge), qh_qh(0) {}
    explicit            QhullRidge(const Qhull &q);
                        QhullRidge(const Qhull &q, ridgeT *r);
    explicit            QhullRidge(QhullQh *qqh) : qh_ridge(&s_empty_ridge), qh_qh(qqh) {}
                        QhullRidge(QhullQh *qqh, ridgeT *r) : qh_ridge(r ? r : &s_empty_ridge), qh_qh(qqh) {}

                        QhullRidge(const QhullRidge &other) : qh_ridge(other.qh_ridge), qh_qh(other.qh_qh) {}

    QhullRidge &        operator=(const QhullRidge &other) { qh_ridge= other.qh_ridge; qh_qh= other.qh_qh; return *this; }
                        ~QhullRidge() {}

#
    QhullFacet          bottomFacet() const { return QhullFacet(qh_qh, qh_ridge->bottom); }
    int                 dimension() const { return ((qh_qh && qh_qh->hull_dim) ? qh_qh->hull_dim-1 : 0); }
    ridgeT *            getBaseT() const { return getRidgeT(); }
    ridgeT *            getRidgeT() const { return qh_ridge; }
    countT              id() const { return qh_ridge->id; }
    bool                isValid() const { return (qh_qh && qh_ridge != &s_empty_ridge); }
    bool                operator==(const QhullRidge &other) const { return qh_ridge==other.qh_ridge; }
    bool                operator!=(const QhullRidge &other) const { return !operator==(other); }
    QhullFacet          otherFacet(const QhullFacet &f) const { return QhullFacet(qh_qh, (qh_ridge->top==f.getFacetT() ? qh_ridge->bottom : qh_ridge->top)); }
    QhullFacet          topFacet() const { return QhullFacet(qh_qh, qh_ridge->top); }

#
    bool                hasNextRidge3d(const QhullFacet &f) const;
    QhullRidge          nextRidge3d(const QhullFacet &f) const { return nextRidge3d(f, 0); }
    QhullRidge          nextRidge3d(const QhullFacet &f, QhullVertex *nextVertex) const;
    QhullVertexSet      vertices() const { return QhullVertexSet(qh_qh, qh_ridge->vertices); }

#

    struct PrintRidge{
        const QhullRidge *ridge;
        const char *    print_message;
                        PrintRidge(const char *message, const QhullRidge &r) : ridge(&r), print_message(message) {}
    };
    PrintRidge          print(const char* message) const { return PrintRidge(message, *this); }
};

}

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullRidge &r);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullRidge::PrintRidge &pr);

#endif
