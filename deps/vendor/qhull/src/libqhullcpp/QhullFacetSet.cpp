/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullFacetSet.cpp#2 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#

#include "libqhullcpp/QhullFacetSet.h"

#include "libqhullcpp/QhullFacet.h"
#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/QhullRidge.h"
#include "libqhullcpp/QhullVertex.h"

#ifndef QHULL_NO_STL
using std::vector;
#endif

#ifdef _MSC_VER
#pragma warning( disable : 4611)
#pragma warning( disable : 4996)
#endif

namespace orgQhull {

#



#ifndef QHULL_NO_STL
std::vector<QhullFacet> QhullFacetSet::
toStdVector() const
{
    QhullSetIterator<QhullFacet> i(*this);
    std::vector<QhullFacet> vs;
    while(i.hasNext()){
        QhullFacet f= i.next();
        if(isSelectAll() || f.isGood()){
            vs.push_back(f);
        }
    }
    return vs;
}
#endif

#

bool QhullFacetSet::
contains(const QhullFacet &facet) const
{
    if(isSelectAll()){
        return QhullSet<QhullFacet>::contains(facet);
    }
    for(QhullFacetSet::const_iterator i=begin(); i != end(); ++i){
        QhullFacet f= *i;
        if(f==facet && f.isGood()){
            return true;
        }
    }
    return false;
}

int QhullFacetSet::
count() const
{
    if(isSelectAll()){
        return QhullSet<QhullFacet>::count();
    }
    int counter= 0;
    for(QhullFacetSet::const_iterator i=begin(); i != end(); ++i){
        QhullFacet f= *i;
        if(f.isGood()){
            counter++;
        }
    }
    return counter;
}

int QhullFacetSet::
count(const QhullFacet &facet) const
{
    if(isSelectAll()){
        return QhullSet<QhullFacet>::count(facet);
    }
    int counter= 0;
    for(QhullFacetSet::const_iterator i=begin(); i != end(); ++i){
        QhullFacet f= *i;
        if(f==facet && f.isGood()){
            counter++;
        }
    }
    return counter;
}

}

#

using std::endl;
using std::ostream;
using orgQhull::QhullFacet;
using orgQhull::QhullFacetSet;

ostream &
operator<<(ostream &os, const QhullFacetSet &fs)
{
    os << fs.print("");
    return os;
}

ostream &

operator<<(ostream &os, const QhullFacetSet::PrintFacetSet &pr)
{
    os << pr.print_message;
    QhullFacetSet fs= *pr.facet_set;
    for(QhullFacetSet::iterator i=fs.begin(); i != fs.end(); ++i){
        QhullFacet f= *i;
        if(fs.isSelectAll() || f.isGood()){
            os << f;
        }
    }
    return os;
}


ostream &
operator<<(ostream &os, const QhullFacetSet::PrintIdentifiers &p)
{
    os << p.print_message;
    for(QhullFacetSet::const_iterator i=p.facet_set->begin(); i!=p.facet_set->end(); ++i){
        const QhullFacet f= *i;
        if(f.getFacetT()==qh_MERGEridge){
            os << " MERGE";
        }else if(f.getFacetT()==qh_DUPLICATEridge){
            os << " DUP";
        }else if(p.facet_set->isSelectAll() || f.isGood()){
            os << " f" << f.id();
        }
    }
    os << endl;
    return os;
}
