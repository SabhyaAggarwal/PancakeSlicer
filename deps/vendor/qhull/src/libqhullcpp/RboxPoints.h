/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/RboxPoints.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef RBOXPOINTS_H
#define RBOXPOINTS_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullPoint.h"
#include "libqhullcpp/PointCoordinates.h"

#include <stdarg.h>
#include <string>
#include <vector>
#include <istream>
#include <ostream>
#include <sstream>

namespace orgQhull {

#

    class RboxPoints;

class RboxPoints : public PointCoordinates {

private:
#

    countT              rbox_new_count;
    int                 rbox_status;
    std::string         rbox_message;


    friend void ::qh_fprintf_rbox(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... );

public:
#
                        RboxPoints();
    explicit            RboxPoints(const char *rboxCommand);
                        ~RboxPoints();
private:
                        RboxPoints(const RboxPoints &);
                        RboxPoints &operator=(const RboxPoints &);
private:
    void                allocateQhullQh();

public:
#
    void                clearRboxMessage();
    countT              newCount() const { return rbox_new_count; }
    std::string         rboxMessage() const;
    int                 rboxStatus() const;
    bool                hasRboxMessage() const;
    void                setNewCount(countT pointCount) { QHULL_ASSERT(pointCount>=0); rbox_new_count= pointCount; }

#
    void                appendPoints(const char* rboxCommand);
    using               PointCoordinates::appendPoints;
    void                reservePoints() { reserveCoordinates((count()+newCount())*dimension()); }
};

}

#endif
