/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/RoadError.h#4 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef ROADERROR_H
#define ROADERROR_H

#include "libqhull_r/user_r.h"
#include "libqhullcpp/RoadLogEvent.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using std::endl;

namespace orgQhull {

#



    class RoadError;

class RoadError : public std::exception {

private:
#
    int                 error_code;
    RoadLogEvent        log_event;
    mutable std::string error_message;

#
    static const char  *  ROADtag;
    static std::ostringstream  global_log;


public:
#

#
    RoadError();
    RoadError(const RoadError &other);
    RoadError(int code, const std::string &message);
    RoadError(int code, const char *fmt);
    RoadError(int code, const char *fmt, int d);
    RoadError(int code, const char *fmt, int d, int d2);
    RoadError(int code, const char *fmt, int d, int d2, float f);
    RoadError(int code, const char *fmt, int d, int d2, float f, const char *s);
    RoadError(int code, const char *fmt, int d, int d2, float f, const void *x);
    RoadError(int code, const char *fmt, int d, int d2, float f, int i);
    RoadError(int code, const char *fmt, int d, int d2, float f, long long i);
    RoadError(int code, const char *fmt, int d, int d2, float f, double e);

    RoadError &         operator=(const RoadError &other);
                        ~RoadError() throw() {};

#

    static void         clearGlobalLog() { global_log.seekp(0); }
    static bool         emptyGlobalLog() { return global_log.tellp()<=0; }
    static const char  *stringGlobalLog() { return global_log.str().c_str(); }

#
    virtual const char *what() const throw();

#
    bool                isValid() const { return log_event.isValid(); }
    int                 errorCode() const { return error_code; };

    RoadLogEvent        roadLogEvent() const { return log_event; };

#
    void                logErrorLastResort() const;
};

}

#

inline std::ostream &   operator<<(std::ostream &os, const orgQhull::RoadError &e) { return os << e.what(); }

#endif
