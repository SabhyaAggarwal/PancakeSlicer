/*<html><pre>  -<a                             href="qh-user_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   userprintf_r.c
   qh_fprintf()

   see README.txt  see COPYING.txt for copyright information.

   If you recompile and load this file, then userprintf_r.o will not be loaded
   from qhull.a or qhull.lib

   See libqhull_r.h for data structures, macros, and user-callable functions.
   See user_r.c for qhull-related, redefinable functions
   see user_r.h for user-definable constants
   See usermem_r.c for qh_exit(), qh_free(), and qh_malloc()
   see Qhull.cpp and RboxPoints.cpp for examples.

   Please report any errors that you fix to qhull@qhull.org
*/

#include "libqhull_r.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>















void qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... ) {
    va_list args;

    if (!fp) {
        if(!qh){
            qh_fprintf_stderr(6241, "userprintf_r.c: fp and qh not defined for qh_fprintf '%s'", fmt);
            qh_exit(qhmem_ERRqhull);
        }

        qh_fprintf_stderr(6232, "Qhull internal error (userprintf_r.c): fp is 0.  Wrong qh_fprintf called.\n");
        qh_errexit(qh, 6232, NULL, NULL);
    }
    va_start(args, fmt);
    if (qh && qh->ANNOTATEoutput) {
      fprintf(fp, "[QH%.4d]", msgcode);
    }else if (msgcode >= MSG_ERROR && msgcode < MSG_STDERR ) {
      fprintf(fp, "QH%.4d ", msgcode);
    }
    vfprintf(fp, fmt, args);
    va_end(args);



}
