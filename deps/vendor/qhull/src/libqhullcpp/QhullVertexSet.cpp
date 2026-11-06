/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullVertexSet.cpp#3 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#

#include "libqhullcpp/QhullVertexSet.h"

#include "libqhullcpp/QhullVertex.h"
#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/QhullRidge.h"
#include "libqhullcpp/QhullVertex.h"
#include "libqhullcpp/Qhull.h"

using std::string;
using std::vector;

#ifdef _MSC_VER
#pragma warning( disable : 4611)

#endif

namespace orgQhull {

QhullVertexSet::
QhullVertexSet(const Qhull &q, facetT *facetlist, setT *facetset, bool allfacets)
: QhullSet<QhullVertex>(q.qh(), 0)
, qhsettemp_defined(false)
{
    QH_TRY_(q.qh()){
        setT *vertices= qh_facetvertices(q.qh(), facetlist, facetset, allfacets);
        defineAs(vertices);
        qhsettemp_defined= true;
    }
    q.qh()->NOerrexit= true;
    q.qh()->maybeThrowQhullMessage(QH_TRY_status);
}



QhullVertexSet::
QhullVertexSet(QhullQh *qqh, facetT *facetlist, setT *facetset, bool allfacets)
: QhullSet<QhullVertex>(qqh, 0)
, qhsettemp_defined(false)
{
    QH_TRY_(qh()){
        setT *vertices= qh_facetvertices(qh(), facetlist, facetset, allfacets);
        defineAs(vertices);
        qhsettemp_defined= true;
    }
    qh()->NOerrexit= true;
    qh()->maybeThrowQhullMessage(QH_TRY_status);
}





QhullVertexSet::
QhullVertexSet(const QhullVertexSet &other)
: QhullSet<QhullVertex>(other)
, qhsettemp_defined(false)
{
    if(other.qhsettemp_defined){
        throw QhullError(10077, "QhullVertexSet: Cannot use copy constructor since qhsettemp_defined (e.g., QhullVertexSet for a set and/or list of QhFacet).  Contains %d vertices", other.count());
    }
}



QhullVertexSet & QhullVertexSet::
operator=(const QhullVertexSet &other)
{
    QhullSet<QhullVertex>::operator=(other);
    qhsettemp_defined= false;
    if(other.qhsettemp_defined){
        throw QhullError(10078, "QhullVertexSet: Cannot use copy constructor since qhsettemp_defined (e.g., QhullVertexSet for a set and/or list of QhFacet).  Contains %d vertices", other.count());
    }
    return *this;
}

void QhullVertexSet::
freeQhSetTemp()
{
    if(qhsettemp_defined){
        qhsettemp_defined= false;
        QH_TRY_(qh()){
            qh_settempfree(qh(), referenceSetT());
        }
        qh()->NOerrexit= true;
        qh()->maybeThrowQhullMessage(QH_TRY_status, QhullError::NOthrow);
    }
}

QhullVertexSet::
~QhullVertexSet()
{
    freeQhSetTemp();
}


#ifndef QHULL_NO_STL
std::vector<QhullVertex> QhullVertexSet::
toStdVector() const
{
    QhullSetIterator<QhullVertex> i(*this);
    std::vector<QhullVertex> vs;
    while(i.hasNext()){
        QhullVertex v= i.next();
        vs.push_back(v);
    }
    return vs;
}
#endif

}

#

using std::endl;
using std::ostream;
using orgQhull::QhullPoint;
using orgQhull::QhullVertex;
using orgQhull::QhullVertexSet;
using orgQhull::QhullVertexSetIterator;


ostream &
operator<<(ostream &os, const QhullVertexSet::PrintIdentifiers &pr)
{
    os << pr.print_message;
    for(QhullVertexSet::const_iterator i= pr.vertex_set->begin(); i!=pr.vertex_set->end(); ++i){
        const QhullVertex v= *i;
        os << " v" << v.id();
    }
    os << endl;
    return os;
}


ostream &
operator<<(ostream &os, const QhullVertexSet::PrintVertexSet &pr){

    os << pr.print_message;
    const QhullVertexSet *vs= pr.vertex_set;
    QhullVertexSetIterator i= *vs;
    while(i.hasNext()){
        const QhullVertex v= i.next();
        const QhullPoint p= v.point();
        os << " p" << p.id() << "(v" << v.id() << ")";
    }
    os << endl;

    return os;
}
