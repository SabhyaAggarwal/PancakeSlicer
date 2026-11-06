/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullVertex.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLVERTEX_H
#define QHULLVERTEX_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/QhullLinkedList.h"
#include "libqhullcpp/QhullSet.h"

#include <ostream>

namespace orgQhull {

#
    class QhullFacetSet;

#

    class QhullVertex;
    typedef QhullLinkedList<QhullVertex> QhullVertexList;
    typedef QhullLinkedListIterator<QhullVertex> QhullVertexListIterator;











class QhullVertex {

#
public:
    typedef vertexT *   base_type;

private:
#
    vertexT *           qh_vertex;
    QhullQh *           qh_qh;

#
    static vertexT      s_empty_vertex;

public:
#

#
                        QhullVertex() : qh_vertex(&s_empty_vertex), qh_qh(0) {}
    explicit            QhullVertex(const Qhull &q);
                        QhullVertex(const Qhull &q, vertexT *v);
    explicit            QhullVertex(QhullQh *qqh) : qh_vertex(&s_empty_vertex), qh_qh(qqh) {}
                        QhullVertex(QhullQh *qqh, vertexT *v) : qh_vertex(v ? v : &s_empty_vertex), qh_qh(qqh) {}

                        QhullVertex(const QhullVertex &other) : qh_vertex(other.qh_vertex), qh_qh(other.qh_qh) {}

    QhullVertex &       operator=(const QhullVertex &other) { qh_vertex= other.qh_vertex; qh_qh= other.qh_qh; return *this; }
                        ~QhullVertex() {}

#
    int                 dimension() const { return (qh_qh ? qh_qh->hull_dim : 0); }
    vertexT *           getBaseT() const { return getVertexT(); }
    vertexT *           getVertexT() const { return qh_vertex; }
    countT              id() const { return qh_vertex->id; }
    bool                isValid() const { return (qh_qh && qh_vertex != &s_empty_vertex); }

    bool                neighborFacetsDefined() const { return qh_vertex->neighbors != 0; }
    QhullVertex         next() const { return QhullVertex(qh_qh, qh_vertex->next); }
    bool                operator==(const QhullVertex &other) const { return qh_vertex==other.qh_vertex; }
    bool                operator!=(const QhullVertex &other) const { return !operator==(other); }
    QhullPoint          point() const { return QhullPoint(qh_qh, qh_vertex->point); }
    QhullVertex         previous() const { return QhullVertex(qh_qh, qh_vertex->previous); }
    QhullQh *           qh() const { return qh_qh; }

#

    QhullFacetSet       neighborFacets() const;

#
    struct PrintVertex{
        const QhullVertex *vertex;
        const char *    print_message;
                        PrintVertex(const char *message, const QhullVertex &v) : vertex(&v), print_message(message) {}
    };
    PrintVertex         print(const char *message) const { return PrintVertex(message, *this); }
};

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullVertex::PrintVertex &pr);
inline std::ostream &operator<<(std::ostream &os, const orgQhull::QhullVertex &v) { os << v.print(""); return os; }

#endif
