/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullVertexSet.h#2 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLVERTEXSET_H
#define QHULLVERTEXSET_H

#include "libqhullcpp/QhullSet.h"
#include "libqhullcpp/QhullVertex.h"

#include <ostream>

namespace orgQhull {

#

#


    class QhullVertexSet;
    typedef QhullSetIterator<QhullVertex> QhullVertexSetIterator;

class QhullVertexSet : public QhullSet<QhullVertex> {

private:
#
    bool                qhsettemp_defined;

public:
#
                        QhullVertexSet(const Qhull &q, setT *s) : QhullSet<QhullVertex>(q, s), qhsettemp_defined(false) {}
                        QhullVertexSet(const Qhull &q, facetT *facetlist, setT *facetset, bool allfacets);

                        QhullVertexSet(QhullQh *qqh, setT *s) : QhullSet<QhullVertex>(qqh, s), qhsettemp_defined(false) {}
                        QhullVertexSet(QhullQh *qqh, facetT *facetlist, setT *facetset, bool allfacets);

                        QhullVertexSet(const QhullVertexSet &other);
    QhullVertexSet &    operator=(const QhullVertexSet &other);
                        ~QhullVertexSet();

private:
                        QhullVertexSet();
public:

#
    void                freeQhSetTemp();

#
#ifndef QHULL_NO_STL
    std::vector<QhullVertex> toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<QhullVertex>   toQList() const;
#endif

#
    struct PrintVertexSet{
        const QhullVertexSet *vertex_set;
        const char *    print_message;

                        PrintVertexSet(const char *message, const QhullVertexSet *s) : vertex_set(s), print_message(message) {}
    };
    const PrintVertexSet print(const char *message) const { return PrintVertexSet(message, this); }

    struct PrintIdentifiers{
        const QhullVertexSet *vertex_set;
        const char *    print_message;
                        PrintIdentifiers(const char *message, const QhullVertexSet *s) : vertex_set(s), print_message(message) {}
    };
    PrintIdentifiers    printIdentifiers(const char *message) const { return PrintIdentifiers(message, this); }

};

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullVertexSet::PrintVertexSet &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullVertexSet::PrintIdentifiers &p);
inline std::ostream &operator<<(std::ostream &os, const orgQhull::QhullVertexSet &vs) { os << vs.print(""); return os; }

#endif
