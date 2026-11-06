/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullFacetList.h#2 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLFACETLIST_H
#define QHULLFACETLIST_H

#include "libqhullcpp/QhullLinkedList.h"
#include "libqhullcpp/QhullFacet.h"

#include <ostream>

#ifndef QHULL_NO_STL
#include <vector>
#endif

namespace orgQhull {

#
    class Qhull;
    class QhullFacet;
    class QhullQh;

#


    class QhullFacetList;

    typedef QhullLinkedListIterator<QhullFacet> QhullFacetListIterator;

class QhullFacetList : public QhullLinkedList<QhullFacet> {

#
private:
    bool                select_all;

#
public:
                        QhullFacetList(const Qhull &q, facetT *b, facetT *e);
                        QhullFacetList(QhullQh *qqh, facetT *b, facetT *e);
                        QhullFacetList(QhullFacet b, QhullFacet e) : QhullLinkedList<QhullFacet>(b, e), select_all(false) {}

                        QhullFacetList(const QhullFacetList &other) : QhullLinkedList<QhullFacet>(*other.begin(), *other.end()), select_all(other.select_all) {}
    QhullFacetList &    operator=(const QhullFacetList &other) { QhullLinkedList<QhullFacet>::operator =(other); select_all= other.select_all; return *this; }
                        ~QhullFacetList() {}

private:
                    QhullFacetList();
public:

#
#ifndef QHULL_NO_STL
    std::vector<QhullFacet> toStdVector() const;
    std::vector<QhullVertex> vertices_toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<QhullFacet>   toQList() const;
    QList<QhullVertex>  vertices_toQList() const;
#endif

#

    countT              count() const;
    bool                contains(const QhullFacet &f) const;
    countT              count(const QhullFacet &f) const;
    bool                isSelectAll() const { return select_all; }
    QhullQh *           qh() const { return first().qh(); }
    void                selectAll() { select_all= true; }
    void                selectGood() { select_all= false; }


#
    struct PrintFacetList{
        const QhullFacetList *facet_list;
        const char *    print_message;
                        PrintFacetList(const QhullFacetList &fl, const char *message) : facet_list(&fl), print_message(message) {}
    };
    PrintFacetList      print(const char *message) const  { return PrintFacetList(*this, message); }

    struct PrintFacets{
        const QhullFacetList *facet_list;
                        PrintFacets(const QhullFacetList &fl) : facet_list(&fl) {}
    };
    PrintFacets         printFacets() const { return PrintFacets(*this); }

    struct PrintVertices{
        const QhullFacetList *facet_list;
                        PrintVertices(const QhullFacetList &fl) : facet_list(&fl) {}
    };
    PrintVertices       printVertices() const { return PrintVertices(*this); }
};

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetList::PrintFacetList &p);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetList::PrintFacets &p);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetList::PrintVertices &p);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetList &fs);

#endif
