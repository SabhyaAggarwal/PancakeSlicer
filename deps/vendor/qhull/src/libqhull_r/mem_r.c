/*<html><pre>  -<a                             href="qh-mem_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

  mem_r.c
    memory management routines for qhull

  See libqhull/mem_r.c for a standalone program.

  To initialize memory:

    qh_meminit(qh, stderr);
    qh_meminitbuffers(qh, qh->IStracing, qh_MEMalign, 7, qh_MEMbufsize,qh_MEMinitbuf);
    qh_memsize(qh, (int)sizeof(facetT));
    qh_memsize(qh, (int)sizeof(facetT));
    ...
    qh_memsetup(qh);

  To free up all memory buffers:
    qh_memfreeshort(qh, &curlong, &totlong);

  if qh_NOmem,
    malloc/free is used instead of mem.c

  notes:
    uses Quickfit algorithm (freelists for commonly allocated sizes)
    assumes small sizes for freelists (it discards the tail of memory buffers)

  see:
    qh-mem_r.htm and mem_r.h
    global_r.c (qh_initbuffers) for an example of using mem_r.c

  Copyright (c) 1993-2015 The Geometry Center.
  $Id: //main/2015/qhull/src/libqhull_r/mem_r.c#5 $$Change: 2065 $
  $DateTime: 2016/01/18 13:51:04 $$Author: bbarber $
*/

#include "libqhull_r.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef qh_NOmem



static int qh_intcompare(const void *i, const void *j);









static int qh_intcompare(const void *i, const void *j) {
  return(*((const int *)i) - *((const int *)j));
}
































void *qh_memalloc(qhT *qh, int insize) {
  void **freelistp, *newbuffer;
  int idx, size, n;
  int outsize, bufsize;
  void *object;

  if (insize<0) {
      qh_fprintf(qh, qh->qhmem.ferr, 6235, "qhull error (qh_memalloc): negative request size (%d).  Did int overflow due to high-D?\n", insize);
      qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
  }
  if (insize>=0 && insize <= qh->qhmem.LASTsize) {
    idx= qh->qhmem.indextable[insize];
    outsize= qh->qhmem.sizetable[idx];
    qh->qhmem.totshort += outsize;
    freelistp= qh->qhmem.freelists+idx;
    if ((object= *freelistp)) {
      qh->qhmem.cntquick++;
      qh->qhmem.totfree -= outsize;
      *freelistp= *((void **)*freelistp);
#ifdef qh_TRACEshort
      n= qh->qhmem.cntshort+qh->qhmem.cntquick+qh->qhmem.freeshort;
      if (qh->qhmem.IStracing >= 5)
          qh_fprintf(qh, qh->qhmem.ferr, 8141, "qh_mem %p n %8d alloc quick: %d bytes (tot %d cnt %d)\n", object, n, outsize, qh->qhmem.totshort, qh->qhmem.cntshort+qh->qhmem.cntquick-qh->qhmem.freeshort);
#endif
      return(object);
    }else {
      qh->qhmem.cntshort++;
      if (outsize > qh->qhmem.freesize) {
        qh->qhmem.totdropped += qh->qhmem.freesize;
        if (!qh->qhmem.curbuffer)
          bufsize= qh->qhmem.BUFinit;
        else
          bufsize= qh->qhmem.BUFsize;
        if (!(newbuffer= qh_malloc((size_t)bufsize))) {
          qh_fprintf(qh, qh->qhmem.ferr, 6080, "qhull error (qh_memalloc): insufficient memory to allocate short memory buffer (%d bytes)\n", bufsize);
          qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
        }
        *((void **)newbuffer)= qh->qhmem.curbuffer;

        qh->qhmem.curbuffer= newbuffer;
        size= (sizeof(void **) + qh->qhmem.ALIGNmask) & ~qh->qhmem.ALIGNmask;
        qh->qhmem.freemem= (void *)((char *)newbuffer+size);
        qh->qhmem.freesize= bufsize - size;
        qh->qhmem.totbuffer += bufsize - size;

        n = qh->qhmem.totshort + qh->qhmem.totfree + qh->qhmem.totdropped + qh->qhmem.freesize - outsize;
        if (qh->qhmem.totbuffer != n) {
            qh_fprintf(qh, qh->qhmem.ferr, 6212, "qh_memalloc internal error: short totbuffer %d != totshort+totfree... %d\n", qh->qhmem.totbuffer, n);
            qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
        }
      }
      object= qh->qhmem.freemem;
      qh->qhmem.freemem= (void *)((char *)qh->qhmem.freemem + outsize);
      qh->qhmem.freesize -= outsize;
      qh->qhmem.totunused += outsize - insize;
#ifdef qh_TRACEshort
      n= qh->qhmem.cntshort+qh->qhmem.cntquick+qh->qhmem.freeshort;
      if (qh->qhmem.IStracing >= 5)
          qh_fprintf(qh, qh->qhmem.ferr, 8140, "qh_mem %p n %8d alloc short: %d bytes (tot %d cnt %d)\n", object, n, outsize, qh->qhmem.totshort, qh->qhmem.cntshort+qh->qhmem.cntquick-qh->qhmem.freeshort);
#endif
      return object;
    }
  }else {
    if (!qh->qhmem.indextable) {
      qh_fprintf(qh, qh->qhmem.ferr, 6081, "qhull internal error (qh_memalloc): qhmem has not been initialized.\n");
      qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
    }
    outsize= insize;
    qh->qhmem.cntlong++;
    qh->qhmem.totlong += outsize;
    if (qh->qhmem.maxlong < qh->qhmem.totlong)
      qh->qhmem.maxlong= qh->qhmem.totlong;
    if (!(object= qh_malloc((size_t)outsize))) {
      qh_fprintf(qh, qh->qhmem.ferr, 6082, "qhull error (qh_memalloc): insufficient memory to allocate %d bytes\n", outsize);
      qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
    }
    if (qh->qhmem.IStracing >= 5)
      qh_fprintf(qh, qh->qhmem.ferr, 8057, "qh_mem %p n %8d alloc long: %d bytes (tot %d cnt %d)\n", object, qh->qhmem.cntlong+qh->qhmem.freelong, outsize, qh->qhmem.totlong, qh->qhmem.cntlong-qh->qhmem.freelong);
  }
  return(object);
}







void qh_memcheck(qhT *qh) {
  int i, count, totfree= 0;
  void *object;

  if (!qh) {
    qh_fprintf_stderr(6243, "qh_memcheck(qh) error: qh is 0.  It does not point to a qhT");
    qh_exit(qhmem_ERRqhull);
  }
  if (qh->qhmem.ferr == 0 || qh->qhmem.IStracing < 0 || qh->qhmem.IStracing > 10 || (((qh->qhmem.ALIGNmask+1) & qh->qhmem.ALIGNmask) != 0)) {
    qh_fprintf_stderr(6244, "qh_memcheck error: either qh->qhmem is overwritten or qh->qhmem is not initialized.  Call qh_mem_new() or qh_new_qhull() before calling qh_mem routines.  ferr 0x%x IsTracing %d ALIGNmask 0x%x", qh->qhmem.ferr, qh->qhmem.IStracing, qh->qhmem.ALIGNmask);
    qh_exit(qhmem_ERRqhull);
  }
  if (qh->qhmem.IStracing != 0)
    qh_fprintf(qh, qh->qhmem.ferr, 8143, "qh_memcheck: check size of freelists on qh->qhmem\nqh_memcheck: A segmentation fault indicates an overwrite of qh->qhmem\n");
  for (i=0; i < qh->qhmem.TABLEsize; i++) {
    count=0;
    for (object= qh->qhmem.freelists[i]; object; object= *((void **)object))
      count++;
    totfree += qh->qhmem.sizetable[i] * count;
  }
  if (totfree != qh->qhmem.totfree) {
    qh_fprintf(qh, qh->qhmem.ferr, 6211, "Qhull internal error (qh_memcheck): totfree %d not equal to freelist total %d\n", qh->qhmem.totfree, totfree);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  if (qh->qhmem.IStracing != 0)
    qh_fprintf(qh, qh->qhmem.ferr, 8144, "qh_memcheck: total size of freelists totfree is the same as qh->qhmem.totfree\n", totfree);
}



















void qh_memfree(qhT *qh, void *object, int insize) {
  void **freelistp;
  int idx, outsize;

  if (!object)
    return;
  if (insize <= qh->qhmem.LASTsize) {
    qh->qhmem.freeshort++;
    idx= qh->qhmem.indextable[insize];
    outsize= qh->qhmem.sizetable[idx];
    qh->qhmem.totfree += outsize;
    qh->qhmem.totshort -= outsize;
    freelistp= qh->qhmem.freelists + idx;
    *((void **)object)= *freelistp;
    *freelistp= object;
#ifdef qh_TRACEshort
    idx= qh->qhmem.cntshort+qh->qhmem.cntquick+qh->qhmem.freeshort;
    if (qh->qhmem.IStracing >= 5)
        qh_fprintf(qh, qh->qhmem.ferr, 8142, "qh_mem %p n %8d free short: %d bytes (tot %d cnt %d)\n", object, idx, outsize, qh->qhmem.totshort, qh->qhmem.cntshort+qh->qhmem.cntquick-qh->qhmem.freeshort);
#endif
  }else {
    qh->qhmem.freelong++;
    qh->qhmem.totlong -= insize;
    if (qh->qhmem.IStracing >= 5)
      qh_fprintf(qh, qh->qhmem.ferr, 8058, "qh_mem %p n %8d free long: %d bytes (tot %d cnt %d)\n", object, qh->qhmem.cntlong+qh->qhmem.freelong, insize, qh->qhmem.totlong, qh->qhmem.cntlong-qh->qhmem.freelong);
    qh_free(object);
  }
}




















void qh_memfreeshort(qhT *qh, int *curlong, int *totlong) {
  void *buffer, *nextbuffer;
  FILE *ferr;

  *curlong= qh->qhmem.cntlong - qh->qhmem.freelong;
  *totlong= qh->qhmem.totlong;
  for (buffer= qh->qhmem.curbuffer; buffer; buffer= nextbuffer) {
    nextbuffer= *((void **) buffer);
    qh_free(buffer);
  }
  qh->qhmem.curbuffer= NULL;
  if (qh->qhmem.LASTsize) {
    qh_free(qh->qhmem.indextable);
    qh_free(qh->qhmem.freelists);
    qh_free(qh->qhmem.sizetable);
  }
  ferr= qh->qhmem.ferr;
  memset((char *)&qh->qhmem, 0, sizeof(qh->qhmem));
  qh->qhmem.ferr= ferr;
}









void qh_meminit(qhT *qh, FILE *ferr) {

  memset((char *)&qh->qhmem, 0, sizeof(qh->qhmem));
  if (ferr)
      qh->qhmem.ferr= ferr;
  else
      qh->qhmem.ferr= stderr;
  if (sizeof(void*) < sizeof(int)) {
    qh_fprintf(qh, qh->qhmem.ferr, 6083, "qhull internal error (qh_meminit): sizeof(void*) %d < sizeof(int) %d.  qset.c will not work\n", (int)sizeof(void*), (int)sizeof(int));
    qh_exit(qhmem_ERRqhull);
  }
  if (sizeof(void*) > sizeof(ptr_intT)) {
      qh_fprintf(qh, qh->qhmem.ferr, 6084, "qhull internal error (qh_meminit): sizeof(void*) %d > sizeof(ptr_intT) %d. Change ptr_intT in mem.h to 'long long'\n", (int)sizeof(void*), (int)sizeof(ptr_intT));
      qh_exit(qhmem_ERRqhull);
  }
  qh_memcheck(qh);
}












void qh_meminitbuffers(qhT *qh, int tracelevel, int alignment, int numsizes, int bufsize, int bufinit) {

  qh->qhmem.IStracing= tracelevel;
  qh->qhmem.NUMsizes= numsizes;
  qh->qhmem.BUFsize= bufsize;
  qh->qhmem.BUFinit= bufinit;
  qh->qhmem.ALIGNmask= alignment-1;
  if (qh->qhmem.ALIGNmask & ~qh->qhmem.ALIGNmask) {
    qh_fprintf(qh, qh->qhmem.ferr, 6085, "qhull internal error (qh_meminit): memory alignment %d is not a power of 2\n", alignment);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  qh->qhmem.sizetable= (int *) calloc((size_t)numsizes, sizeof(int));
  qh->qhmem.freelists= (void **) calloc((size_t)numsizes, sizeof(void *));
  if (!qh->qhmem.sizetable || !qh->qhmem.freelists) {
    qh_fprintf(qh, qh->qhmem.ferr, 6086, "qhull error (qh_meminit): insufficient memory\n");
    qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
  }
  if (qh->qhmem.IStracing >= 1)
    qh_fprintf(qh, qh->qhmem.ferr, 8059, "qh_meminitbuffers: memory initialized with alignment %d\n", alignment);
}







void qh_memsetup(qhT *qh) {
  int k,i;

  qsort(qh->qhmem.sizetable, (size_t)qh->qhmem.TABLEsize, sizeof(int), qh_intcompare);
  qh->qhmem.LASTsize= qh->qhmem.sizetable[qh->qhmem.TABLEsize-1];
  if(qh->qhmem.LASTsize >= qh->qhmem.BUFsize || qh->qhmem.LASTsize >= qh->qhmem.BUFinit) {
    qh_fprintf(qh, qh->qhmem.ferr, 6087, "qhull error (qh_memsetup): largest mem size %d is >= buffer size %d or initial buffer size %d\n",
            qh->qhmem.LASTsize, qh->qhmem.BUFsize, qh->qhmem.BUFinit);
    qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
  }
  if (!(qh->qhmem.indextable= (int *)qh_malloc((qh->qhmem.LASTsize+1) * sizeof(int)))) {
    qh_fprintf(qh, qh->qhmem.ferr, 6088, "qhull error (qh_memsetup): insufficient memory\n");
    qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
  }
  for (k=qh->qhmem.LASTsize+1; k--; )
    qh->qhmem.indextable[k]= k;
  i= 0;
  for (k=0; k <= qh->qhmem.LASTsize; k++) {
    if (qh->qhmem.indextable[k] <= qh->qhmem.sizetable[i])
      qh->qhmem.indextable[k]= i;
    else
      qh->qhmem.indextable[k]= ++i;
  }
}







void qh_memsize(qhT *qh, int size) {
  int k;

  if(qh->qhmem.LASTsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6089, "qhull error (qh_memsize): called after qhmem_setup\n");
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  size= (size + qh->qhmem.ALIGNmask) & ~qh->qhmem.ALIGNmask;
  for (k=qh->qhmem.TABLEsize; k--; ) {
    if (qh->qhmem.sizetable[k] == size)
      return;
  }
  if (qh->qhmem.TABLEsize < qh->qhmem.NUMsizes)
    qh->qhmem.sizetable[qh->qhmem.TABLEsize++]= size;
  else
    qh_fprintf(qh, qh->qhmem.ferr, 7060, "qhull warning (memsize): free list table has room for only %d sizes\n", qh->qhmem.NUMsizes);
}










void qh_memstatistics(qhT *qh, FILE *fp) {
  int i;
  int count;
  void *object;

  qh_memcheck(qh);
  qh_fprintf(qh, fp, 9278, "\nmemory statistics:\n\
%7d quick allocations\n\
%7d short allocations\n\
%7d long allocations\n\
%7d short frees\n\
%7d long frees\n\
%7d bytes of short memory in use\n\
%7d bytes of short memory in freelists\n\
%7d bytes of dropped short memory\n\
%7d bytes of unused short memory (estimated)\n\
%7d bytes of long memory allocated (max, except for input)\n\
%7d bytes of long memory in use (in %d pieces)\n\
%7d bytes of short memory buffers (minus links)\n\
%7d bytes per short memory buffer (initially %d bytes)\n",
           qh->qhmem.cntquick, qh->qhmem.cntshort, qh->qhmem.cntlong,
           qh->qhmem.freeshort, qh->qhmem.freelong,
           qh->qhmem.totshort, qh->qhmem.totfree,
           qh->qhmem.totdropped + qh->qhmem.freesize, qh->qhmem.totunused,
           qh->qhmem.maxlong, qh->qhmem.totlong, qh->qhmem.cntlong - qh->qhmem.freelong,
           qh->qhmem.totbuffer, qh->qhmem.BUFsize, qh->qhmem.BUFinit);
  if (qh->qhmem.cntlarger) {
    qh_fprintf(qh, fp, 9279, "%7d calls to qh_setlarger\n%7.2g     average copy size\n",
           qh->qhmem.cntlarger, ((float)qh->qhmem.totlarger)/(float)qh->qhmem.cntlarger);
    qh_fprintf(qh, fp, 9280, "  freelists(bytes->count):");
  }
  for (i=0; i < qh->qhmem.TABLEsize; i++) {
    count=0;
    for (object= qh->qhmem.freelists[i]; object; object= *((void **)object))
      count++;
    qh_fprintf(qh, fp, 9281, " %d->%d", qh->qhmem.sizetable[i], count);
  }
  qh_fprintf(qh, fp, 9282, "\n\n");
}











#else

void *qh_memalloc(qhT *qh, int insize) {
  void *object;

  if (!(object= qh_malloc((size_t)insize))) {
    qh_fprintf(qh, qh->qhmem.ferr, 6090, "qhull error (qh_memalloc): insufficient memory\n");
    qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
  }
  qh->qhmem.cntlong++;
  qh->qhmem.totlong += insize;
  if (qh->qhmem.maxlong < qh->qhmem.totlong)
      qh->qhmem.maxlong= qh->qhmem.totlong;
  if (qh->qhmem.IStracing >= 5)
    qh_fprintf(qh, qh->qhmem.ferr, 8060, "qh_mem %p n %8d alloc long: %d bytes (tot %d cnt %d)\n", object, qh->qhmem.cntlong+qh->qhmem.freelong, insize, qh->qhmem.totlong, qh->qhmem.cntlong-qh->qhmem.freelong);
  return object;
}

void qh_memfree(qhT *qh, void *object, int insize) {

  if (!object)
    return;
  qh_free(object);
  qh->qhmem.freelong++;
  qh->qhmem.totlong -= insize;
  if (qh->qhmem.IStracing >= 5)
    qh_fprintf(qh, qh->qhmem.ferr, 8061, "qh_mem %p n %8d free long: %d bytes (tot %d cnt %d)\n", object, qh->qhmem.cntlong+qh->qhmem.freelong, insize, qh->qhmem.totlong, qh->qhmem.cntlong-qh->qhmem.freelong);
}

void qh_memfreeshort(qhT *qh, int *curlong, int *totlong) {
  *totlong= qh->qhmem.totlong;
  *curlong= qh->qhmem.cntlong - qh->qhmem.freelong;
  memset((char *)&qh->qhmem, 0, sizeof(qh->qhmem));
}

void qh_meminit(qhT *qh, FILE *ferr) {

  memset((char *)&qh->qhmem, 0, sizeof(qh->qhmem));
  if (ferr)
      qh->qhmem.ferr= ferr;
  else
      qh->qhmem.ferr= stderr;
  if (sizeof(void*) < sizeof(int)) {
    qh_fprintf(qh, qh->qhmem.ferr, 6091, "qhull internal error (qh_meminit): sizeof(void*) %d < sizeof(int) %d.  qset.c will not work\n", (int)sizeof(void*), (int)sizeof(int));
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
}

void qh_meminitbuffers(qhT *qh, int tracelevel, int alignment, int numsizes, int bufsize, int bufinit) {

  qh->qhmem.IStracing= tracelevel;
}

void qh_memsetup(qhT *qh) {

}

void qh_memsize(qhT *qh, int size) {

}

void qh_memstatistics(qhT *qh, FILE *fp) {

  qh_fprintf(qh, fp, 9409, "\nmemory statistics:\n\
%7d long allocations\n\
%7d long frees\n\
%7d bytes of long memory allocated (max, except for input)\n\
%7d bytes of long memory in use (in %d pieces)\n",
           qh->qhmem.cntlong,
           qh->qhmem.freelong,
           qh->qhmem.maxlong, qh->qhmem.totlong, qh->qhmem.cntlong - qh->qhmem.freelong);
}

#endif













void qh_memtotal(qhT *qh, int *totlong, int *curlong, int *totshort, int *curshort, int *maxlong, int *totbuffer) {
    *totlong= qh->qhmem.totlong;
    *curlong= qh->qhmem.cntlong - qh->qhmem.freelong;
    *totshort= qh->qhmem.totshort;
    *curshort= qh->qhmem.cntshort + qh->qhmem.cntquick - qh->qhmem.freeshort;
    *maxlong= qh->qhmem.maxlong;
    *totbuffer= qh->qhmem.totbuffer;
}
