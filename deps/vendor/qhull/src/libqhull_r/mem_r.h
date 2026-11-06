/*<html><pre>  -<a                             href="qh-mem_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   mem_r.h
     prototypes for memory management functions

   see qh-mem_r.htm, mem_r.c and qset_r.h

   for error handling, writes message and calls
     qh_errexit(qhT *qh, qhmem_ERRmem, NULL, NULL) if insufficient memory
       and
     qh_errexit(qhT *qh, qhmem_ERRqhull, NULL, NULL) otherwise

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/mem_r.h#4 $$Change: 2079 $
   $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
*/

#ifndef qhDEFmem
#define qhDEFmem 1

#include <stdio.h>

#ifndef DEFsetT
#define DEFsetT 1
typedef struct setT setT;
#endif

#ifndef DEFqhT
#define DEFqhT 1
typedef struct qhT qhT;
#endif























#define qh_TRACEshort















#define qhmem_ERRmem 4
#define qhmem_ERRqhull 5

















#if (defined(__MINGW64__)) && defined(_WIN64)
typedef long long ptr_intT;
#elif (_MSC_VER) && defined(_WIN64)
typedef long long ptr_intT;
#else
typedef long ptr_intT;
#endif



















typedef struct qhmemT qhmemT;


struct qhmemT {
  int      BUFsize;
  int      BUFinit;
  int      TABLEsize;
  int      NUMsizes;
  int      LASTsize;
  int      ALIGNmask;
  void   **freelists;
  int     *sizetable;
  int     *indextable;
  void    *curbuffer;
  void    *freemem;
  int      freesize;
  setT    *tempstack;
  FILE    *ferr;
  int      IStracing;
  int      cntquick;

  int      cntshort;
  int      cntlong;
  int      freeshort;
  int      freelong;
  int      totbuffer;
  int      totdropped;
  int      totfree;
  int      totlong;
  int      maxlong;
  int      totshort;
  int      totunused;
  int      cntlarger;
  int      totlarger;
};












#if defined qh_NOmem
#define qh_memalloc_(qh, insize, freelistp, object, type) {\
  object= (type*)qh_memalloc(qh, insize); }
#elif defined qh_TRACEshort
#define qh_memalloc_(qh, insize, freelistp, object, type) {\
    freelistp= NULL;   \
    object= (type*)qh_memalloc(qh, insize); }
#else

#define qh_memalloc_(qh, insize, freelistp, object, type) {\
  freelistp= qh->qhmem.freelists + qh->qhmem.indextable[insize];\
  if ((object= (type*)*freelistp)) {\
    qh->qhmem.totshort += qh->qhmem.sizetable[qh->qhmem.indextable[insize]]; \
    qh->qhmem.totfree -= qh->qhmem.sizetable[qh->qhmem.indextable[insize]]; \
    qh->qhmem.cntquick++;  \
    *freelistp= *((void **)*freelistp);\
  }else object= (type*)qh_memalloc(qh, insize);}
#endif











#if defined qh_NOmem
#define qh_memfree_(qh, object, insize, freelistp) {\
  qh_memfree(qh, object, insize); }
#elif defined qh_TRACEshort
#define qh_memfree_(qh, object, insize, freelistp) {\
    freelistp= NULL;   \
    qh_memfree(qh, object, insize); }
#else

#define qh_memfree_(qh, object, insize, freelistp) {\
  if (object) { \
    qh->qhmem.freeshort++;\
    freelistp= qh->qhmem.freelists + qh->qhmem.indextable[insize];\
    qh->qhmem.totshort -= qh->qhmem.sizetable[qh->qhmem.indextable[insize]]; \
    qh->qhmem.totfree += qh->qhmem.sizetable[qh->qhmem.indextable[insize]]; \
    *((void **)object)= *freelistp;\
    *freelistp= object;}}
#endif



#ifdef __cplusplus
extern "C" {
#endif

void *qh_memalloc(qhT *qh, int insize);
void qh_memcheck(qhT *qh);
void qh_memfree(qhT *qh, void *object, int insize);
void qh_memfreeshort(qhT *qh, int *curlong, int *totlong);
void qh_meminit(qhT *qh, FILE *ferr);
void qh_meminitbuffers(qhT *qh, int tracelevel, int alignment, int numsizes,
                        int bufsize, int bufinit);
void qh_memsetup(qhT *qh);
void qh_memsize(qhT *qh, int size);
void qh_memstatistics(qhT *qh, FILE *fp);
void qh_memtotal(qhT *qh, int *totlong, int *curlong, int *totshort, int *curshort, int *maxlong, int *totbuffer);

#ifdef __cplusplus
}
#endif

#endif
