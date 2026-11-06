/*<html><pre>  -<a                             href="qh-set_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   qset_r.c
   implements set manipulations needed for quickhull

   see qh-set_r.htm and qset_r.h

   Be careful of strict aliasing (two pointers of different types
   that reference the same location).  The last slot of a set is
   either the actual size of the set plus 1, or the NULL terminator
   of the set (i.e., setelemT).

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/qset_r.c#3 $$Change: 2062 $
   $DateTime: 2016/01/17 13:13:18 $$Author: bbarber $
*/

#include "libqhull_r.h"
#include "qset_r.h"
#include "mem_r.h"
#include <stdio.h>
#include <string.h>





#ifndef qhDEFlibqhull
typedef struct ridgeT ridgeT;
typedef struct facetT facetT;
void    qh_errexit(qhT *qh, int exitcode, facetT *, ridgeT *);
void    qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... );
#  ifdef _MSC_VER
#  pragma warning( disable : 4127)
#  pragma warning( disable : 4706)
#  endif
#endif






















void qh_setaddnth(qhT *qh, setT **setp, int nth, void *newelem) {
  int oldsize, i;
  setelemT *sizep;
  setelemT *oldp, *newp;

  if (!*setp || (sizep= SETsizeaddr_(*setp))->i==0) {
    qh_setlarger(qh, setp);
    sizep= SETsizeaddr_(*setp);
  }
  oldsize= sizep->i - 1;
  if (nth < 0 || nth > oldsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6171, "qhull internal error (qh_setaddnth): nth %d is out-of-bounds for set:\n", nth);
    qh_setprint(qh, qh->qhmem.ferr, "", *setp);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  sizep->i++;
  oldp= (setelemT *)SETelemaddr_(*setp, oldsize, void);
  newp= oldp+1;
  for (i=oldsize-nth+1; i--; )
    (newp--)->p= (oldp--)->p;
  newp->p= newelem;
}

















void qh_setaddsorted(qhT *qh, setT **setp, void *newelem) {
  int newindex=0;
  void *elem, **elemp;

  FOREACHelem_(*setp) {
    if (elem < newelem)
      newindex++;
    else if (elem == newelem)
      return;
    else
      break;
  }
  qh_setaddnth(qh, setp, newindex, newelem);
}

















void qh_setappend(qhT *qh, setT **setp, void *newelem) {
  setelemT *sizep;
  setelemT *endp;
  int count;

  if (!newelem)
    return;
  if (!*setp || (sizep= SETsizeaddr_(*setp))->i==0) {
    qh_setlarger(qh, setp);
    sizep= SETsizeaddr_(*setp);
  }
  count= (sizep->i)++ - 1;
  endp= (setelemT *)SETelemaddr_(*setp, count, void);
  (endp++)->p= newelem;
  endp->p= NULL;
}
















void qh_setappend_set(qhT *qh, setT **setp, setT *setA) {
  int sizeA, size;
  setT *oldset;
  setelemT *sizep;

  if (!setA)
    return;
  SETreturnsize_(setA, sizeA);
  if (!*setp)
    *setp= qh_setnew(qh, sizeA);
  sizep= SETsizeaddr_(*setp);
  if (!(size= sizep->i))
    size= (*setp)->maxsize;
  else
    size--;
  if (size + sizeA > (*setp)->maxsize) {
    oldset= *setp;
    *setp= qh_setcopy(qh, oldset, sizeA);
    qh_setfree(qh, &oldset);
    sizep= SETsizeaddr_(*setp);
  }
  if (sizeA > 0) {
    sizep->i= size+sizeA+1;
    memcpy((char *)&((*setp)->e[size].p), (char *)&(setA->e[0].p), (size_t)(sizeA+1) * SETelemsize);
  }
}


















void qh_setappend2ndlast(qhT *qh, setT **setp, void *newelem) {
    setelemT *sizep;
    setelemT *endp, *lastp;
    int count;

    if (!*setp || (sizep= SETsizeaddr_(*setp))->i==0) {
        qh_setlarger(qh, setp);
        sizep= SETsizeaddr_(*setp);
    }
    count= (sizep->i)++ - 1;
    endp= (setelemT *)SETelemaddr_(*setp, count, void);
    lastp= endp-1;
    *(endp++)= *lastp;
    endp->p= NULL;
    lastp->p= newelem;
}











void qh_setcheck(qhT *qh, setT *set, const char *tname, unsigned id) {
  int maxsize, size;
  int waserr= 0;

  if (!set)
    return;
  SETreturnsize_(set, size);
  maxsize= set->maxsize;
  if (size > maxsize || !maxsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6172, "qhull internal error (qh_setcheck): actual size %d of %s%d is greater than max size %d\n",
             size, tname, id, maxsize);
    waserr= 1;
  }else if (set->e[size].p) {
    qh_fprintf(qh, qh->qhmem.ferr, 6173, "qhull internal error (qh_setcheck): %s%d(size %d max %d) is not null terminated.\n",
             tname, id, size-1, maxsize);
    waserr= 1;
  }
  if (waserr) {
    qh_setprint(qh, qh->qhmem.ferr, "ERRONEOUS", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
}




















void qh_setcompact(qhT *qh, setT *set) {
  int size;
  void **destp, **elemp, **endp, **firstp;

  if (!set)
    return;
  SETreturnsize_(set, size);
  destp= elemp= firstp= SETaddr_(set, void);
  endp= destp + size;
  while (1) {
    if (!(*destp++ = *elemp++)) {
      destp--;
      if (elemp > endp)
        break;
    }
  }
  qh_settruncate(qh, set, (int)(destp-firstp));
}
















setT *qh_setcopy(qhT *qh, setT *set, int extra) {
  setT *newset;
  int size;

  if (extra < 0)
    extra= 0;
  SETreturnsize_(set, size);
  newset= qh_setnew(qh, size+extra);
  SETsizeaddr_(newset)->i= size+1;
  memcpy((char *)&(newset->e[0].p), (char *)&(set->e[0].p), (size_t)(size+1) * SETelemsize);
  return(newset);
}






















void *qh_setdel(setT *set, void *oldelem) {
  setelemT *sizep;
  setelemT *elemp;
  setelemT *lastp;

  if (!set)
    return NULL;
  elemp= (setelemT *)SETaddr_(set, void);
  while (elemp->p != oldelem && elemp->p)
    elemp++;
  if (elemp->p) {
    sizep= SETsizeaddr_(set);
    if (!(sizep->i)--)
      sizep->i= set->maxsize;
    lastp= (setelemT *)SETelemaddr_(set, sizep->i-1, void);
    elemp->p= lastp->p;
    lastp->p= NULL;
    return oldelem;
  }
  return NULL;
}



















void *qh_setdellast(setT *set) {
  int setsize;
  int maxsize;
  setelemT *sizep;
  void *returnvalue;

  if (!set || !(set->e[0].p))
    return NULL;
  sizep= SETsizeaddr_(set);
  if ((setsize= sizep->i)) {
    returnvalue= set->e[setsize - 2].p;
    set->e[setsize - 2].p= NULL;
    sizep->i--;
  }else {
    maxsize= set->maxsize;
    returnvalue= set->e[maxsize - 1].p;
    set->e[maxsize - 1].p= NULL;
    sizep->i= maxsize;
  }
  return returnvalue;
}



















void *qh_setdelnth(qhT *qh, setT *set, int nth) {
  void *elem;
  setelemT *sizep;
  setelemT *elemp, *lastp;

  sizep= SETsizeaddr_(set);
  if ((sizep->i--)==0)
    sizep->i= set->maxsize;
  if (nth < 0 || nth >= sizep->i) {
    qh_fprintf(qh, qh->qhmem.ferr, 6174, "qhull internal error (qh_setdelnth): nth %d is out-of-bounds for set:\n", nth);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  elemp= (setelemT *)SETelemaddr_(set, nth, void);
  lastp= (setelemT *)SETelemaddr_(set, sizep->i-1, void);
  elem= elemp->p;
  elemp->p= lastp->p;
  lastp->p= NULL;
  return elem;
}





















void *qh_setdelnthsorted(qhT *qh, setT *set, int nth) {
  void *elem;
  setelemT *sizep;
  setelemT *newp, *oldp;

  sizep= SETsizeaddr_(set);
  if (nth < 0 || (sizep->i && nth >= sizep->i-1) || nth >= set->maxsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6175, "qhull internal error (qh_setdelnthsorted): nth %d is out-of-bounds for set:\n", nth);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  newp= (setelemT *)SETelemaddr_(set, nth, void);
  elem= newp->p;
  oldp= newp+1;
  while (((newp++)->p= (oldp++)->p))
    ;
  if ((sizep->i--)==0)
    sizep->i= set->maxsize;
  return elem;
}



















void *qh_setdelsorted(setT *set, void *oldelem) {
  setelemT *sizep;
  setelemT *newp, *oldp;

  if (!set)
    return NULL;
  newp= (setelemT *)SETaddr_(set, void);
  while(newp->p != oldelem && newp->p)
    newp++;
  if (newp->p) {
    oldp= newp+1;
    while (((newp++)->p= (oldp++)->p))
      ;
    sizep= SETsizeaddr_(set);
    if ((sizep->i--)==0)
      sizep->i= set->maxsize;
    return oldelem;
  }
  return NULL;
}

















setT *qh_setduplicate(qhT *qh, setT *set, int elemsize) {
  void          *elem, **elemp, *newElem;
  setT          *newSet;
  int           size;

  if (!(size= qh_setsize(qh, set)))
    return NULL;
  newSet= qh_setnew(qh, size);
  FOREACHelem_(set) {
    newElem= qh_memalloc(qh, elemsize);
    memcpy(newElem, elem, (size_t)elemsize);
    qh_setappend(qh, &newSet, newElem);
  }
  return newSet;
}










void **qh_setendpointer(setT *set) {

  setelemT *sizep= SETsizeaddr_(set);
  int n= sizep->i;
  return (n ? &set->e[n-1].p : &sizep->p);
}















int qh_setequal(setT *setA, setT *setB) {
  void **elemAp, **elemBp;
  int sizeA= 0, sizeB= 0;

  if (setA) {
    SETreturnsize_(setA, sizeA);
  }
  if (setB) {
    SETreturnsize_(setB, sizeB);
  }
  if (sizeA != sizeB)
    return 0;
  if (!sizeA)
    return 1;
  elemAp= SETaddr_(setA, void);
  elemBp= SETaddr_(setB, void);
  if (!memcmp((char *)elemAp, (char *)elemBp, sizeA*SETelemsize))
    return 1;
  return 0;
}






















int qh_setequal_except(setT *setA, void *skipelemA, setT *setB, void *skipelemB) {
  void **elemA, **elemB;
  int skip=0;

  elemA= SETaddr_(setA, void);
  elemB= SETaddr_(setB, void);
  while (1) {
    if (*elemA == skipelemA) {
      skip++;
      elemA++;
    }
    if (skipelemB) {
      if (*elemB == skipelemB) {
        skip++;
        elemB++;
      }
    }else if (*elemA != *elemB) {
      skip++;
      if (!(skipelemB= *elemB++))
        return 0;
    }
    if (!*elemA)
      break;
    if (*elemA++ != *elemB++)
      return 0;
  }
  if (skip != 2 || *elemB)
    return 0;
  return 1;
}


















int qh_setequal_skip(setT *setA, int skipA, setT *setB, int skipB) {
  void **elemA, **elemB, **skipAp, **skipBp;

  elemA= SETaddr_(setA, void);
  elemB= SETaddr_(setB, void);
  skipAp= SETelemaddr_(setA, skipA, void);
  skipBp= SETelemaddr_(setB, skipB, void);
  while (1) {
    if (elemA == skipAp)
      elemA++;
    if (elemB == skipBp)
      elemB++;
    if (!*elemA)
      break;
    if (*elemA++ != *elemB++)
      return 0;
  }
  if (*elemB)
    return 0;
  return 1;
}


















void qh_setfree(qhT *qh, setT **setp) {
  int size;
  void **freelistp;

  if (*setp) {
    size= sizeof(setT) + ((*setp)->maxsize)*SETelemsize;
    if (size <= qh->qhmem.LASTsize) {
      qh_memfree_(qh, *setp, size, freelistp);
    }else
      qh_memfree(qh, *setp, size);
    *setp= NULL;
  }
}















void qh_setfree2(qhT *qh, setT **setp, int elemsize) {
  void          *elem, **elemp;

  FOREACHelem_(*setp)
    qh_memfree(qh, elem, elemsize);
  qh_setfree(qh, setp);
}



















void qh_setfreelong(qhT *qh, setT **setp) {
  int size;

  if (*setp) {
    size= sizeof(setT) + ((*setp)->maxsize)*SETelemsize;
    if (size > qh->qhmem.LASTsize) {
      qh_memfree(qh, *setp, size);
      *setp= NULL;
    }
  }
}














int qh_setin(setT *set, void *setelem) {
  void *elem, **elemp;

  FOREACHelem_(set) {
    if (elem == setelem)
      return 1;
  }
  return 0;
}

















int qh_setindex(setT *set, void *atelem) {
  void **elem;
  int size, i;

  if (!set)
    return -1;
  SETreturnsize_(set, size);
  if (size > set->maxsize)
    return -1;
  elem= SETaddr_(set, void);
  for (i=0; i < size; i++) {
    if (*elem++ == atelem)
      return i;
  }
  return -1;
}


















void qh_setlarger(qhT *qh, setT **oldsetp) {
  int size= 1;
  setT *newset, *set, **setp, *oldset;
  setelemT *sizep;
  setelemT *newp, *oldp;

  if (*oldsetp) {
    oldset= *oldsetp;
    SETreturnsize_(oldset, size);
    qh->qhmem.cntlarger++;
    qh->qhmem.totlarger += size+1;
    newset= qh_setnew(qh, 2 * size);
    oldp= (setelemT *)SETaddr_(oldset, void);
    newp= (setelemT *)SETaddr_(newset, void);
    memcpy((char *)newp, (char *)oldp, (size_t)(size+1) * SETelemsize);
    sizep= SETsizeaddr_(newset);
    sizep->i= size+1;
    FOREACHset_((setT *)qh->qhmem.tempstack) {
      if (set == oldset)
        *(setp-1)= newset;
    }
    qh_setfree(qh, oldsetp);
  }else
    newset= qh_setnew(qh, 3);
  *oldsetp= newset;
}














void *qh_setlast(setT *set) {
  int size;

  if (set) {
    size= SETsizeaddr_(set)->i;
    if (!size)
      return SETelem_(set, set->maxsize - 1);
    else if (size > 1)
      return SETelem_(set, size - 2);
  }
  return NULL;
}

















setT *qh_setnew(qhT *qh, int setsize) {
  setT *set;
  int sizereceived;
  int size;
  void **freelistp;

  if (!setsize)
    setsize++;
  size= sizeof(setT) + setsize * SETelemsize;
  if (size>0 && size <= qh->qhmem.LASTsize) {
    qh_memalloc_(qh, size, freelistp, set, setT);
#ifndef qh_NOmem
    sizereceived= qh->qhmem.sizetable[ qh->qhmem.indextable[size]];
    if (sizereceived > size)
      setsize += (sizereceived - size)/SETelemsize;
#endif
  }else
    set= (setT*)qh_memalloc(qh, size);
  set->maxsize= setsize;
  set->e[setsize].i= 1;
  set->e[0].p= NULL;
  return(set);
}




















setT *qh_setnew_delnthsorted(qhT *qh, setT *set, int size, int nth, int prepend) {
  setT *newset;
  void **oldp, **newp;
  int tailsize= size - nth -1, newsize;

  if (tailsize < 0) {
    qh_fprintf(qh, qh->qhmem.ferr, 6176, "qhull internal error (qh_setnew_delnthsorted): nth %d is out-of-bounds for set:\n", nth);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  newsize= size-1 + prepend;
  newset= qh_setnew(qh, newsize);
  newset->e[newset->maxsize].i= newsize+1;
  oldp= SETaddr_(set, void);
  newp= SETaddr_(newset, void) + prepend;
  switch (nth) {
  case 0:
    break;
  case 1:
    *(newp++)= *oldp++;
    break;
  case 2:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  case 3:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  case 4:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  default:
    memcpy((char *)newp, (char *)oldp, (size_t)nth * SETelemsize);
    newp += nth;
    oldp += nth;
    break;
  }
  oldp++;
  switch (tailsize) {
  case 0:
    break;
  case 1:
    *(newp++)= *oldp++;
    break;
  case 2:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  case 3:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  case 4:
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    *(newp++)= *oldp++;
    break;
  default:
    memcpy((char *)newp, (char *)oldp, (size_t)tailsize * SETelemsize);
    newp += tailsize;
  }
  *newp= NULL;
  return(newset);
}











void qh_setprint(qhT *qh, FILE *fp, const char* string, setT *set) {
  int size, k;

  if (!set)
    qh_fprintf(qh, fp, 9346, "%s set is null\n", string);
  else {
    SETreturnsize_(set, size);
    qh_fprintf(qh, fp, 9347, "%s set=%p maxsize=%d size=%d elems=",
             string, set, set->maxsize, size);
    if (size > set->maxsize)
      size= set->maxsize+1;
    for (k=0; k < size; k++)
      qh_fprintf(qh, fp, 9348, " %p", set->e[k].p);
    qh_fprintf(qh, fp, 9349, "\n");
  }
}















void qh_setreplace(qhT *qh, setT *set, void *oldelem, void *newelem) {
  void **elemp;

  elemp= SETaddr_(set, void);
  while (*elemp != oldelem && *elemp)
    elemp++;
  if (*elemp)
    *elemp= newelem;
  else {
    qh_fprintf(qh, qh->qhmem.ferr, 6177, "qhull internal error (qh_setreplace): elem %p not found in set\n",
       oldelem);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
}
















int qh_setsize(qhT *qh, setT *set) {
  int size;
  setelemT *sizep;

  if (!set)
    return(0);
  sizep= SETsizeaddr_(set);
  if ((size= sizep->i)) {
    size--;
    if (size > set->maxsize) {
      qh_fprintf(qh, qh->qhmem.ferr, 6178, "qhull internal error (qh_setsize): current set size %d is greater than maximum size %d\n",
               size, set->maxsize);
      qh_setprint(qh, qh->qhmem.ferr, "set: ", set);
      qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
    }
  }else
    size= set->maxsize;
  return size;
}
















setT *qh_settemp(qhT *qh, int setsize) {
  setT *newset;

  newset= qh_setnew(qh, setsize);
  qh_setappend(qh, &qh->qhmem.tempstack, newset);
  if (qh->qhmem.IStracing >= 5)
    qh_fprintf(qh, qh->qhmem.ferr, 8123, "qh_settemp: temp set %p of %d elements, depth %d\n",
       newset, newset->maxsize, qh_setsize(qh, qh->qhmem.tempstack));
  return newset;
}


















void qh_settempfree(qhT *qh, setT **set) {
  setT *stackedset;

  if (!*set)
    return;
  stackedset= qh_settemppop(qh);
  if (stackedset != *set) {
    qh_settemppush(qh, stackedset);
    qh_fprintf(qh, qh->qhmem.ferr, 6179, "qhull internal error (qh_settempfree): set %p(size %d) was not last temporary allocated(depth %d, set %p, size %d)\n",
             *set, qh_setsize(qh, *set), qh_setsize(qh, qh->qhmem.tempstack)+1,
             stackedset, qh_setsize(qh, stackedset));
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  qh_setfree(qh, set);
}












void qh_settempfree_all(qhT *qh) {
  setT *set, **setp;

  FOREACHset_(qh->qhmem.tempstack)
    qh_setfree(qh, &set);
  qh_setfree(qh, &qh->qhmem.tempstack);
}













setT *qh_settemppop(qhT *qh) {
  setT *stackedset;

  stackedset= (setT*)qh_setdellast(qh->qhmem.tempstack);
  if (!stackedset) {
    qh_fprintf(qh, qh->qhmem.ferr, 6180, "qhull internal error (qh_settemppop): pop from empty temporary stack\n");
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  if (qh->qhmem.IStracing >= 5)
    qh_fprintf(qh, qh->qhmem.ferr, 8124, "qh_settemppop: depth %d temp set %p of %d elements\n",
       qh_setsize(qh, qh->qhmem.tempstack)+1, stackedset, qh_setsize(qh, stackedset));
  return stackedset;
}













void qh_settemppush(qhT *qh, setT *set) {
  if (!set) {
    qh_fprintf(qh, qh->qhmem.ferr, 6267, "qhull error (qh_settemppush): can not push a NULL temp\n");
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  qh_setappend(qh, &qh->qhmem.tempstack, set);
  if (qh->qhmem.IStracing >= 5)
    qh_fprintf(qh, qh->qhmem.ferr, 8125, "qh_settemppush: depth %d temp set %p of %d elements\n",
      qh_setsize(qh, qh->qhmem.tempstack), set, qh_setsize(qh, set));
}


















void qh_settruncate(qhT *qh, setT *set, int size) {

  if (size < 0 || size > set->maxsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6181, "qhull internal error (qh_settruncate): size %d out of bounds for set:\n", size);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  set->e[set->maxsize].i= size+1;
  set->e[size].p= NULL;
}














int qh_setunique(qhT *qh, setT **set, void *elem) {

  if (!qh_setin(*set, elem)) {
    qh_setappend(qh, set, elem);
    return 1;
  }
  return 0;
}




















void qh_setzero(qhT *qh, setT *set, int idx, int size) {
  int count;

  if (idx < 0 || idx >= size || size > set->maxsize) {
    qh_fprintf(qh, qh->qhmem.ferr, 6182, "qhull internal error (qh_setzero): index %d or size %d out of bounds for set:\n", idx, size);
    qh_setprint(qh, qh->qhmem.ferr, "", set);
    qh_errexit(qh, qhmem_ERRqhull, NULL, NULL);
  }
  set->e[set->maxsize].i=  size+1;
  count= size - idx + 1;
  memset((char *)SETelemaddr_(set, idx, void), 0, (size_t)count * SETelemsize);
}
