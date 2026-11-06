/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/Qhull.h#3 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLCPP_H
#define QHULLCPP_H

#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/QhullVertex.h"
#include "libqhullcpp/QhullFacet.h"

namespace orgQhull {
















#
    class QhullFacetList;
    class QhullPoints;
    class QhullQh;
    class RboxPoints;

#
    class Qhull;


class Qhull {

private:
#
    QhullQh *           qh_qh;
    Coordinates         origin_point;
    bool                run_called;
    Coordinates         feasible_point;

public:
#
                        Qhull();
                        Qhull(const RboxPoints &rboxPoints, const char *qhullCommand2);
                        Qhull(const char *inputComment2, int pointDimension, int pointCount, const realT *pointCoordinates, const char *qhullCommand2);
                        ~Qhull() throw();
private:
                        Qhull(const Qhull &);
    Qhull &             operator=(const Qhull &);

private:
    void                allocateQhullQh();

public:

#
    void                checkIfQhullInitialized();
    int                 dimension() const { return qh_qh->input_dim; }
    void                disableOutputStream() { qh_qh->disableOutputStream(); }
    void                enableOutputStream() { qh_qh->enableOutputStream(); }
    countT              facetCount() const { return qh_qh->num_facets; }
    Coordinates         feasiblePoint() const;
    int                 hullDimension() const { return qh_qh->hull_dim; }
    bool                hasOutputStream() const { return qh_qh->hasOutputStream(); }
    bool                initialized() const { return (qh_qh->hull_dim>0); }
    const char *        inputComment() const { return qh_qh->rbox_command; }
    QhullPoint          inputOrigin();

    QhullPoint          origin() { QHULL_ASSERT(initialized()); return QhullPoint(qh_qh, origin_point.data()); }
    QhullQh *           qh() const { return qh_qh; };
    const char *        qhullCommand() const { return qh_qh->qhull_command; }
    const char *        rboxCommand() const { return qh_qh->rbox_command; }
    int                 rotateRandom() const { return qh_qh->ROTATErandom; }
    void                setFeasiblePoint(const Coordinates &c) { feasible_point= c; }
    countT              vertexCount() const { return qh_qh->num_vertices; }

#
    double              angleEpsilon() const { return qh_qh->angleEpsilon(); }
    void                appendQhullMessage(const std::string &s) { qh_qh->appendQhullMessage(s); }
    void                clearQhullMessage() { qh_qh->clearQhullMessage(); }
    double              distanceEpsilon() const { return qh_qh->distanceEpsilon(); }
    double              factorEpsilon() const { return qh_qh->factorEpsilon(); }
    std::string         qhullMessage() const { return qh_qh->qhullMessage(); }
    bool                hasQhullMessage() const { return qh_qh->hasQhullMessage(); }
    int                 qhullStatus() const { return qh_qh->qhullStatus(); }
    void                setErrorStream(std::ostream *os) { qh_qh->setErrorStream(os); }
    void                setFactorEpsilon(double a) { qh_qh->setFactorEpsilon(a); }
    void                setOutputStream(std::ostream *os) { qh_qh->setOutputStream(os); }

#
    QhullFacet          beginFacet() const { return QhullFacet(qh_qh, qh_qh->facet_list); }
    QhullVertex         beginVertex() const { return QhullVertex(qh_qh, qh_qh->vertex_list); }
    void                defineVertexNeighborFacets();
    QhullFacet          endFacet() const { return QhullFacet(qh_qh, qh_qh->facet_tail); }
    QhullVertex         endVertex() const { return QhullVertex(qh_qh, qh_qh->vertex_tail); }
    QhullFacetList      facetList() const;
    QhullFacet          firstFacet() const { return beginFacet(); }
    QhullVertex         firstVertex() const { return beginVertex(); }
    QhullPoints         points() const;
    QhullPointSet       otherPoints() const;

    coordT *            pointCoordinateBegin() const { return qh_qh->first_point; }
    coordT *            pointCoordinateEnd() const { return qh_qh->first_point + qh_qh->num_points*qh_qh->hull_dim; }
    QhullVertexList     vertexList() const;

#
    double              area();
    void                outputQhull();
    void                outputQhull(const char * outputflags);
    void                runQhull(const RboxPoints &rboxPoints, const char *qhullCommand2);
    void                runQhull(const char *inputComment2, int pointDimension, int pointCount, const realT *pointCoordinates, const char *qhullCommand2);
    double              volume();

#
private:
    void                initializeFeasiblePoint(int hulldim);
};

}

#endif
