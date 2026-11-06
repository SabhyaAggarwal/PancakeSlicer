/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/RoadError.cpp#2 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#
#

#include "libqhullcpp/RoadError.h"

#include <string>
#include <sstream>
#include <iostream>

using std::cerr;
using std::cout;
using std::string;

#ifdef _MSC_VER
#endif

namespace orgQhull {

#



const char * RoadError::
ROADtag= "QH";

std::ostringstream RoadError::
global_log;

#

RoadError::
RoadError()
: error_code(0)
, log_event()
, error_message()
{ }

RoadError::
RoadError(const RoadError &other)
: error_code(other.error_code)
, log_event(other.log_event)
, error_message(other.error_message)
{
}

RoadError::
RoadError(int code, const std::string &message)
: error_code(code)
, log_event(message.c_str())
, error_message(log_event.toString(ROADtag, error_code))
{
    log_event.cstr_1= error_message.c_str();
}

RoadError::
RoadError(int code, const char *fmt)
: error_code(code)
, log_event(fmt)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d)
: error_code(code)
, log_event(fmt, d)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2)
: error_code(code)
, log_event(fmt, d, d2)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f)
: error_code(code)
, log_event(fmt, d, d2, f)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f, const char *s)
: error_code(code)
, log_event(fmt, d, d2, f, s)
, error_message(log_event.toString(ROADtag, code))
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f, const void *x)
: error_code(code)
, log_event(fmt, d, d2, f, x)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f, int i)
: error_code(code)
, log_event(fmt, d, d2, f, i)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f, long long i)
: error_code(code)
, log_event(fmt, d, d2, f, i)
, error_message()
{ }

RoadError::
RoadError(int code, const char *fmt, int d, int d2, float f, double e)
: error_code(code)
, log_event(fmt, d, d2, f, e)
, error_message()
{ }

RoadError & RoadError::
operator=(const RoadError &other)
{
    error_code= other.error_code;
    error_message= other.error_message;
    log_event= other.log_event;
    return *this;
}

#
const char * RoadError::
what() const throw()
{
    if(error_message.empty()){
        error_message= log_event.toString(ROADtag, error_code);
    }
    return error_message.c_str();
}

#




void RoadError::
logErrorLastResort() const
{
    global_log << what() << endl;
}


}
