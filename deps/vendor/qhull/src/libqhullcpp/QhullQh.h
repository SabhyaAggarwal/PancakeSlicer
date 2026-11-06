/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullQh.h#2 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QHULLQH_H
#define QHULLQH_H

#include "libqhull_r/qhull_ra.h"

#include <string>

#ifdef _MSC_VER
#pragma warning( disable : 4611)

#endif







#define QH_TRY_ERROR 10071

#define QH_TRY_(qh) \
    int QH_TRY_status; \
    if(qh->NOerrexit){ \
        qh->NOerrexit= False; \
        QH_TRY_status= setjmp(qh->errexit); \
    }else{ \
        throw QhullError(QH_TRY_ERROR, "Cannot invoke QH_TRY_() from inside a QH_TRY_.  Or missing 'qh->NOerrexit=true' after previously called QH_TRY_(qh){...}"); \
    } \
    if(!QH_TRY_status)

#define QH_TRY_NO_THROW_(qh) \
    int QH_TRY_status; \
    if(qh->NOerrexit){ \
        qh->NOerrexit= False; \
        QH_TRY_status= setjmp(qh->errexit); \
    }else{ \
        QH_TRY_status= QH_TRY_ERROR; \
    } \
    if(!QH_TRY_status)

namespace orgQhull {

#

    class QhullQh;


class QhullQh : public qhT {

#

#
private:
    int                 qhull_status;
    std::string         qhull_message;
    std::ostream *      error_stream;
    std::ostream *      output_stream;
    double              factor_epsilon;
    bool                use_output_stream;

    friend void         ::qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... );

    static const double default_factor_epsilon;

#
public:
                        QhullQh();
                        ~QhullQh();
private:

                        QhullQh(const QhullQh &);
    QhullQh &           operator=(const QhullQh &);
public:

#
    double              factorEpsilon() const { return factor_epsilon; }
    void                setFactorEpsilon(double a) { factor_epsilon= a; }
    void                disableOutputStream() { use_output_stream= false; }
    void                enableOutputStream() { use_output_stream= true; }

#
    void                appendQhullMessage(const std::string &s);
    void                clearQhullMessage();
    std::string         qhullMessage() const;
    bool                hasOutputStream() const { return use_output_stream; }
    bool                hasQhullMessage() const;
    void                maybeThrowQhullMessage(int exitCode);
    void                maybeThrowQhullMessage(int exitCode, int noThrow) throw();
    int                 qhullStatus() const;
    void                setErrorStream(std::ostream *os);
    void                setOutputStream(std::ostream *os);

#
    double              angleEpsilon() const { return this->ANGLEround*factor_epsilon; }
    void                checkAndFreeQhullMemory();
    double              distanceEpsilon() const { return this->DISTround*factor_epsilon; }

};

}

#endif
