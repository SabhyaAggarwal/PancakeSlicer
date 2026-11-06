/*<html><pre>  -<a                             href="qh-user_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   user.h
   user redefinable constants

   for each source file, user_r.h is included first

   see qh-user_r.htm.  see COPYING for copyright information.

   See user_r.c for sample code.

   before reading any code, review libqhull_r.h for data structure definitions

Sections:
   ============= qhull library constants ======================
   ============= data types and configuration macros ==========
   ============= performance related constants ================
   ============= memory constants =============================
   ============= joggle constants =============================
   ============= conditional compilation ======================
   ============= -merge constants- ============================

Code flags --
  NOerrors -- the code does not call qh_errexit()
  WARN64 -- the code may be incompatible with 64-bit pointers

*/

#include <time.h>

#ifndef qhDEFuser
#define qhDEFuser 1


#if !defined(SAG_COM) && !defined(__CYGWIN__) && (defined(WIN64) || defined(_WIN64) || defined(__WIN64__) || defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__))
#   define QHULL_OS_WIN
#elif defined(__MWERKS__) && defined(__INTEL__)
#   define QHULL_OS_WIN
#endif












#define qh_FILENAMElen 500















#define MSG_TRACE0 0
#define MSG_TRACE1 1000
#define MSG_TRACE2 2000
#define MSG_TRACE3 3000
#define MSG_TRACE4 4000
#define MSG_TRACE5 5000
#define MSG_ERROR  6000
#define MSG_WARNING 7000
#define MSG_STDERR  8000
#define MSG_OUTPUT  9000
#define MSG_QHULL_ERROR 10000
#define MSG_FIXUP  11000
#define MSG_MAXLEN  3000







#define qh_OPTIONline 80




















































#define REALfloat 0

#if (REALfloat == 1)
#define realT float
#define REALmax FLT_MAX
#define REALmin FLT_MIN
#define REALepsilon FLT_EPSILON
#define qh_REALdigits 8
#define qh_REAL_1 "%6.8g "
#define qh_REAL_2n "%6.8g %6.8g\n"
#define qh_REAL_3n "%6.8g %6.8g %6.8g\n"

#elif (REALfloat == 0)
#define realT double
#define REALmax DBL_MAX
#define REALmin DBL_MIN
#define REALepsilon DBL_EPSILON
#define qh_REALdigits 16
#define qh_REAL_1 "%6.16g "
#define qh_REAL_2n "%6.16g %6.16g\n"
#define qh_REAL_3n "%6.16g %6.16g %6.16g\n"

#else
#error unknown float option
#endif













#ifndef DEFcountT
#define DEFcountT 1
typedef int countT;
#endif
#define COUNTmax 0x7fffffff





























#define qh_CLOCKtype 1

#if (qh_CLOCKtype == 1)

#if defined(CLOCKS_PER_SECOND)
#define qh_CPUclock    ((unsigned long)clock())
#define qh_SECticks CLOCKS_PER_SECOND

#elif defined(CLOCKS_PER_SEC)
#define qh_CPUclock    ((unsigned long)clock())
#define qh_SECticks CLOCKS_PER_SEC

#elif defined(CLK_TCK)
#define qh_CPUclock    ((unsigned long)clock())
#define qh_SECticks CLK_TCK

#else
#define qh_CPUclock    ((unsigned long)clock())
#define qh_SECticks 1E6
#endif

#elif (qh_CLOCKtype == 2)
#define qh_CPUclock    qh_clock()
#define qh_SECticks 100

#else
#error unknown clock option
#endif

































#define qh_RANDOMtype 5

#if (qh_RANDOMtype == 1)
#define qh_RANDOMmax ((realT)0x7fffffffUL)
#define qh_RANDOMint random()
#define qh_RANDOMseed_(qh, seed) srandom(seed);

#elif (qh_RANDOMtype == 2)
#ifdef RAND_MAX
#define qh_RANDOMmax ((realT)RAND_MAX)
#else
#define qh_RANDOMmax ((realT)32767)
#endif
#define qh_RANDOMint  rand()
#define qh_RANDOMseed_(qh, seed) srand((unsigned)seed);

#elif (qh_RANDOMtype == 3)
#define qh_RANDOMmax ((realT)0x7fffffffUL)
#define qh_RANDOMint  rand()
#define qh_RANDOMseed_(qh, seed) srand((unsigned)seed);

#elif (qh_RANDOMtype == 4)
#define qh_RANDOMmax ((realT)0x7fffffffUL)
#define qh_RANDOMint lrand48()
#define qh_RANDOMseed_(qh, seed) srand48(seed);

#elif (qh_RANDOMtype == 5)
#define qh_RANDOMmax ((realT)2147483646UL)
#define qh_RANDOMint qh_rand(qh)
#define qh_RANDOMseed_(qh, seed) qh_srand(qh, seed);


#else
#error: unknown random option
#endif







#define qh_ORIENTclock 0


























#define qh_JOGGLEdefault 30000.0







#define qh_JOGGLEincrease 10.0










#define qh_JOGGLEretry 2










#define qh_JOGGLEagain 1











#define qh_JOGGLEmaxincrease 1e-2







#define qh_JOGGLEmaxretry 100














#define qh_HASHfactor 2










#define qh_VERIFYdirect 1000000







#define qh_INITIALsearch 6











#define qh_INITIALmax 8
























#define qh_MEMalign ((int)(fmax_(sizeof(realT), sizeof(void *))))










#define qh_MEMbufsize 0x10000










#define qh_MEMinitbuf 0x20000







#define qh_INFINITE  -10.101










#define qh_DEFAULTbox 0.5
#define qh_DEFAULTzbox 1e6




































#define qh_COMPUTEfurthest 0










#define qh_KEEPstatistics 1












#define qh_MAXoutside 1

































#if 0
    exitcode= qh_new_qhull(qhT *qh, dim, numpoints, points, ismalloc,
                      flags, outfile, errfile);
    qh_freeqhull(qhT *qh, !qh_ALL);
    qh_memfreeshort(qhT *qh, &curlong, &totlong);
#endif







#define qh_QUICKhelp    0















#define qh_DIMmergeVertex 6







#define qh_DIMreduceBuild 5











#define qh_BESTcentrum 20
#define qh_BESTcentrum2 2










#define qh_BESTnonconvex 15










#define qh_MAXnewmerges 2












#define qh_MAXnewcentrum 5










#define qh_COPLANARratio 3





























#define qh_DISToutside ((qh_USEfindbestnew ? 2 : 1) * \
     fmax_((qh->MERGING ? 2 : 1)*qh->MINoutside, qh->max_outside))













#define qh_RATIOnearinside 5













#define qh_SEARCHdist ((qh_USEfindbestnew ? 2 : 1) * \
      (qh->max_outside + 2 * qh->DISTround + fmax_( qh->MINvisible, qh->MAXcoplanar)));













#define qh_USEfindbestnew (zzval_(Ztotmerge) > 50)















#define qh_WIDEcoplanar 6












#define qh_WIDEduplicate 100















#define qh_MAXnarrow -0.99999999











#define qh_WARNnarrow -0.999999999999999


















#define qh_ZEROdelaunay 2




















#if 0
#define QHULL_CRTDBG
#endif

#if defined(_MSC_VER) && defined(_DEBUG) && defined(QHULL_CRTDBG)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#endif
