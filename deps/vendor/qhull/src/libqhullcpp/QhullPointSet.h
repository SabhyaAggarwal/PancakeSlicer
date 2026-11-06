/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullPointSet.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLPOINTSET_H
#define QHULLPOINTSET_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullSet.h"
#include "libqhullcpp/QhullPoint.h"

#include <ostream>

namespace orgQhull {

#
    class Qhull;
    class QhullPoint;

#


    class QhullPointSet;

class QhullPointSet : public QhullSet<QhullPoint> {

private:
#

public:

#
                        QhullPointSet(const Qhull &q, setT *s) : QhullSet<QhullPoint>(q, s) {}

                        QhullPointSet(QhullQh *qqh, setT *s) : QhullSet<QhullPoint>(qqh, s) {}

                        QhullPointSet(const QhullPointSet &other) : QhullSet<QhullPoint>(other) {}

    QhullPointSet &     operator=(const QhullPointSet &other) { QhullSet<QhullPoint>::operator=(other); return *this; }
                        ~QhullPointSet() {}


private:
                        QhullPointSet();
public:

#
    struct PrintIdentifiers{
        const QhullPointSet *point_set;
        const char *    print_message;
        PrintIdentifiers(const char *message, const QhullPointSet *s) : point_set(s), print_message(message) {}
    };
    PrintIdentifiers printIdentifiers(const char *message) const { return PrintIdentifiers(message, this); }

    struct PrintPointSet{
        const QhullPointSet *point_set;
        const char *    print_message;
        PrintPointSet(const char *message, const QhullPointSet &s) : point_set(&s), print_message(message) {}
    };
    PrintPointSet       print(const char *message) const { return PrintPointSet(message, *this); }

};

typedef QhullSetIterator<QhullPoint>  QhullPointSetIterator;

}

#

std::ostream &operator<<(std::ostream &os, const orgQhull::QhullPointSet::PrintIdentifiers &pr);
std::ostream &operator<<(std::ostream &os, const orgQhull::QhullPointSet::PrintPointSet &pr);

#endif
