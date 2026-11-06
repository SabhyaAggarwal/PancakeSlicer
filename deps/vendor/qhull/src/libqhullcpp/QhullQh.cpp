/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullQh.cpp#5 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#


#include "libqhullcpp/QhullQh.h"

#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullStat.h"

#include <sstream>
#include <iostream>

#include <stdarg.h>

using std::cerr;
using std::string;
using std::vector;
using std::ostream;

#ifdef _MSC_VER
#pragma warning( disable : 4611)
#pragma warning( disable : 4996)
#endif

namespace orgQhull {

#
const double QhullQh::
default_factor_epsilon= 1.0;

#


QhullQh::
QhullQh()
: qhull_status(qh_ERRnone)
, qhull_message()
, error_stream(0)
, output_stream(0)
, factor_epsilon(QhullQh::default_factor_epsilon)
, use_output_stream(false)
{

    qh_meminit(this, NULL);
    qh_initstatistics(this);
    qh_initqhull_start2(this, NULL, NULL, qh_FILEstderr);
    this->ISqhullQh= True;
}

QhullQh::
~QhullQh()
{
    checkAndFreeQhullMemory();
}

#






void QhullQh::
checkAndFreeQhullMemory()
{
#ifdef qh_NOmem
    qh_freeqhull(this, qh_ALL);
#else
    qh_memcheck(this);
    qh_freeqhull(this, !qh_ALL);
    countT curlong;
    countT totlong;
    qh_memfreeshort(this, &curlong, &totlong);
    if (curlong || totlong)
        throw QhullError(10026, "Qhull error: qhull did not free %d bytes of long memory (%d pieces).", totlong, curlong);
#endif
}

#

void QhullQh::
appendQhullMessage(const string &s)
{
    if(output_stream && use_output_stream && this->USEstdout){
        *output_stream << s;
    }else if(error_stream){
        *error_stream << s;
    }else{
        qhull_message += s;
    }
}


void QhullQh::
clearQhullMessage()
{
    qhull_status= qh_ERRnone;
    qhull_message.clear();
    RoadError::clearGlobalLog();
}


bool QhullQh::
hasQhullMessage() const
{
    return (!qhull_message.empty() || qhull_status!=qh_ERRnone);

}

void QhullQh::
maybeThrowQhullMessage(int exitCode)
{
    if(!NOerrexit){
        if(qhull_message.size()>0){
            qhull_message.append("\n");
        }
        if(exitCode || qhull_status==qh_ERRnone){
            qhull_status= 10073;
        }else{
            qhull_message.append("QH10073: ");
        }
        qhull_message.append("Cannot call maybeThrowQhullMessage() from QH_TRY_().  Or missing 'qh->NOerrexit=true;' after QH_TRY_(){...}.");
    }
    if(qhull_status==qh_ERRnone){
        qhull_status= exitCode;
    }
    if(qhull_status!=qh_ERRnone){
        QhullError e(qhull_status, qhull_message);
        clearQhullMessage();
        throw e;
    }
}

void QhullQh::
maybeThrowQhullMessage(int exitCode, int noThrow)  throw()
{
    QHULL_UNUSED(noThrow);

    if(qhull_status==qh_ERRnone){
        qhull_status= exitCode;
    }
    if(qhull_status!=qh_ERRnone){
        QhullError e(qhull_status, qhull_message);
        e.logErrorLastResort();
    }
}


std::string QhullQh::
qhullMessage() const
{
    if(qhull_message.empty() && qhull_status!=qh_ERRnone){
        return "qhull: no message for error.  Check cerr or error stream\n";
    }else{
        return qhull_message;
    }
}

int QhullQh::
qhullStatus() const
{
    return qhull_status;
}

void QhullQh::
setErrorStream(ostream *os)
{
    error_stream= os;
}


void QhullQh::
setOutputStream(ostream *os)
{
    output_stream= os;
    use_output_stream= (os!=0);
}

}













extern "C"
void qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... ) {
    va_list args;

    using namespace orgQhull;

    if(!qh->ISqhullQh){
        qh_fprintf_stderr(10025, "Qhull error: qh_fprintf called from a Qhull instance without QhullQh defined\n");
        qh_exit(10025);
    }
    QhullQh *qhullQh= static_cast<QhullQh *>(qh);
    va_start(args, fmt);
    if(msgcode<MSG_OUTPUT || fp == qh_FILEstderr){
        if(msgcode>=MSG_ERROR && msgcode<MSG_WARNING){
            if(qhullQh->qhull_status<MSG_ERROR || qhullQh->qhull_status>=MSG_WARNING){
                qhullQh->qhull_status= msgcode;
            }
        }
        char newMessage[MSG_MAXLEN];

        vsnprintf(newMessage, sizeof(newMessage), fmt, args);
        qhullQh->appendQhullMessage(newMessage);
        va_end(args);
        return;
    }
    if(qhullQh->output_stream && qhullQh->use_output_stream){
        char newMessage[MSG_MAXLEN];
        vsnprintf(newMessage, sizeof(newMessage), fmt, args);
        *qhullQh->output_stream << newMessage;
        va_end(args);
        return;
    }

    char newMessage[MSG_MAXLEN];
    vsnprintf(newMessage, sizeof(newMessage), fmt, args);
    qhullQh->appendQhullMessage(newMessage);
    va_end(args);
}
