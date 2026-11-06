/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullFacetSet.h#2 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLFACETSET_H
#define QHULLFACETSET_H

#include "libqhullcpp/QhullSet.h"
#include "libqhullcpp/QhullFacet.h"

#include <ostream>

namespace orgQhull {

#
    class Qhull;

#

    class QhullFacetSet;
    typedef QhullSetIterator<QhullFacet> QhullFacetSetIterator;

class QhullFacetSet : public QhullSet<QhullFacet> {

#
public:
    typedef facetT *   base_type;

private:
#
    bool                select_all;

public:
#

                        QhullFacetSet(const Qhull &q, setT *s) : QhullSet<QhullFacet>(q, s), select_all(false) {}
                        QhullFacetSet(QhullQh *qqh, setT *s) : QhullSet<QhullFacet>(qqh, s), select_all(false) {}

                        QhullFacetSet(const QhullFacetSet &other) : QhullSet<QhullFacet>(other), select_all(other.select_all) {}

    QhullFacetSet &     operator=(const QhullFacetSet &other) { QhullSet<QhullFacet>::operator=(other); select_all= other.select_all; return *this; }

private:

                        QhullFacetSet();
public:

#
#ifndef QHULL_NO_STL
    std::vector<QhullFacet> toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<QhullFacet>   toQList() const;
#endif

#

    countT              count() const;
    bool                contains(const QhullFacet &f) const;
    countT              count(const QhullFacet &f) const;
    bool                isSelectAll() const { return select_all; }

    void                selectAll() { select_all= true; }
    void                selectGood() { select_all= false; }

#


    struct PrintFacetSet{
        const QhullFacetSet *facet_set;
        const char *    print_message;
                        PrintFacetSet(const char *message, const QhullFacetSet *s) : facet_set(s), print_message(message) {}
    };
    const PrintFacetSet print(const char *message) const { return PrintFacetSet(message, this); }

    struct PrintIdentifiers{
        const QhullFacetSet *facet_set;
        const char *    print_message;
                        PrintIdentifiers(const char *message, const QhullFacetSet *s) : facet_set(s), print_message(message) {}
    };
    PrintIdentifiers    printIdentifiers(const char *message) const { return PrintIdentifiers(message, this); }

};

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetSet &fs);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetSet::PrintFacetSet &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullFacetSet::PrintIdentifiers &p);

#endif
