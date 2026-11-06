/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullFacet.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLFACET_H
#define QHULLFACET_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullHyperplane.h"
#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/QhullSet.h"
#include "libqhullcpp/QhullPointSet.h"

#include <ostream>

namespace orgQhull {

#
    class Coordinates;
    class Qhull;
    class QhullFacetSet;
    class QhullRidge;
    class QhullVertex;
    class QhullVertexSet;

#
    class QhullFacet;
    typedef QhullSet<QhullRidge>  QhullRidgeSet;


class QhullFacet {

#
public:
    typedef facetT *   base_type;

private:
#
    facetT *            qh_facet;
    QhullQh *           qh_qh;

#
    static facetT       s_empty_facet;

public:
#
                        QhullFacet() : qh_facet(&s_empty_facet), qh_qh(0) {}
    explicit            QhullFacet(const Qhull &q);
                        QhullFacet(const Qhull &q, facetT *f);
    explicit            QhullFacet(QhullQh *qqh) : qh_facet(&s_empty_facet), qh_qh(qqh) {}
                        QhullFacet(QhullQh *qqh, facetT *f) : qh_facet(f ? f : &s_empty_facet), qh_qh(qqh) {}

                        QhullFacet(const QhullFacet &other) : qh_facet(other.qh_facet ? other.qh_facet : &s_empty_facet), qh_qh(other.qh_qh) {}

    QhullFacet &        operator=(const QhullFacet &other) { qh_facet= other.qh_facet ? other.qh_facet : &s_empty_facet; qh_qh= other.qh_qh; return *this; }
                        ~QhullFacet() {}


#
    int                 dimension() const { return (qh_qh ? qh_qh->hull_dim : 0); }
    QhullPoint          getCenter() { return getCenter(qh_PRINTpoints); }
    QhullPoint          getCenter(qh_PRINT printFormat);
    facetT *            getBaseT() const { return getFacetT(); }

    facetT *            getFacetT() const { return qh_facet; }
    QhullHyperplane     hyperplane() const { return QhullHyperplane(qh_qh, dimension(), qh_facet->normal, qh_facet->offset); }
    countT              id() const { return (qh_facet ? qh_facet->id : (int)qh_IDunknown); }
    QhullHyperplane     innerplane() const;
    bool                isValid() const { return qh_qh && qh_facet && qh_facet != &s_empty_facet; }
    bool                isGood() const { return qh_facet && qh_facet->good; }
    bool                isSimplicial() const { return qh_facet && qh_facet->simplicial; }
    bool                isTopOrient() const { return qh_facet && qh_facet->toporient; }
    bool                isTriCoplanar() const { return qh_facet && qh_facet->tricoplanar; }
    bool                isUpperDelaunay() const { return qh_facet && qh_facet->upperdelaunay; }
    QhullFacet          next() const { return QhullFacet(qh_qh, qh_facet->next); }
    bool                operator==(const QhullFacet &other) const { return qh_facet==other.qh_facet; }
    bool                operator!=(const QhullFacet &other) const { return !operator==(other); }
    QhullHyperplane     outerplane() const;
    QhullFacet          previous() const { return QhullFacet(qh_qh, qh_facet->previous); }
    QhullQh *           qh() const { return qh_qh; }
    QhullFacet          tricoplanarOwner() const;
    QhullPoint          voronoiVertex();

#

    double              distance(const Coordinates &c) const { return distance(c.data()); }
    double              distance(const pointT *p) const { return distance(QhullPoint(qh_qh, const_cast<coordT *>(p))); }
    double              distance(const QhullPoint &p) const { return hyperplane().distance(p); }
    double              facetArea();

#

    QhullPointSet       coplanarPoints() const;
    QhullFacetSet       neighborFacets() const;
    QhullPointSet       outsidePoints() const;
    QhullRidgeSet       ridges() const;
    QhullVertexSet      vertices() const;

#
    struct PrintCenter{
        QhullFacet *    facet;
        const char *    message;
        qh_PRINT        print_format;
                        PrintCenter(QhullFacet &f, qh_PRINT printFormat, const char * s) : facet(&f), message(s), print_format(printFormat){}
    };
    PrintCenter         printCenter(qh_PRINT printFormat, const char *message) { return PrintCenter(*this, printFormat, message); }

    struct PrintFacet{
        QhullFacet *    facet;
        const char *    message;
        explicit        PrintFacet(QhullFacet &f, const char * s) : facet(&f), message(s) {}
    };
    PrintFacet          print(const char *message) { return PrintFacet(*this, message); }

    struct PrintFlags{
        const QhullFacet *facet;
        const char *    message;
                        PrintFlags(const QhullFacet &f, const char *s) : facet(&f), message(s) {}
    };
    PrintFlags          printFlags(const char *message) const { return PrintFlags(*this, message); }

    struct PrintHeader{
        QhullFacet *    facet;
                        PrintHeader(QhullFacet &f) : facet(&f) {}
    };
    PrintHeader         printHeader() { return PrintHeader(*this); }

    struct PrintRidges{
        const QhullFacet *facet;
                        PrintRidges(QhullFacet &f) : facet(&f) {}
    };
    PrintRidges         printRidges() { return PrintRidges(*this); }

};

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacet::PrintFacet &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacet::PrintCenter &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacet::PrintFlags &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacet::PrintHeader &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacet::PrintRidges &pr);
std::ostream &operator<<(std::ostream &os, orgQhull::QhullFacet &f);

#endif
