/*<html><pre>  -<a                             href="qh-set_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   qset_r.h
     header file for qset_r.c that implements set

   see qh-set_r.htm and qset_r.c

   only uses mem_r.c, malloc/free

   for error handling, writes message and calls
      qh_errexit(qhT *qh, qhmem_ERRqhull, NULL, NULL);

   set operations satisfy the following properties:
    - sets have a max size, the actual size (if different) is stored at the end
    - every set is NULL terminated
    - sets may be sorted or unsorted, the caller must distinguish this

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/qset_r.h#4 $$Change: 2079 $
   $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
*/

#ifndef qhDEFset
#define qhDEFset 1

#include <stdio.h>



#ifndef DEFsetT
#define DEFsetT 1
typedef struct setT setT;
#endif

#ifndef DEFqhT
#define DEFqhT 1
typedef struct qhT qhT;
#endif










































typedef union setelemT setelemT;
union setelemT {
  void    *p;
  int   i;
};

struct setT {
  int maxsize;
  setelemT e[1];




};









#define SETelemsize ((int)sizeof(setelemT))




































#define FOREACHsetelement_(type, set, variable) \
        if (((variable= NULL), set)) for (\
          variable##p= (type **)&((set)->e[0].p); \
          (variable= *variable##p++);)





























#define FOREACHsetelement_i_(qh, type, set, variable) \
        if (((variable= NULL), set)) for (\
          variable##_i= 0, variable= (type *)((set)->e[0].p), \
                   variable##_n= qh_setsize(qh, set);\
          variable##_i < variable##_n;\
          variable= (type *)((set)->e[++variable##_i].p) )



























#define FOREACHsetelementreverse_(qh, type, set, variable) \
        if (((variable= NULL), set)) for (\
           variable##temp= qh_setsize(qh, set)-1, variable= qh_setlast(qh, set);\
           variable; variable= \
           ((--variable##temp >= 0) ? SETelemt_(set, variable##temp, type) : NULL))


























#define FOREACHsetelementreverse12_(type, set, variable) \
        if (((variable= NULL), set)) for (\
          variable##p= (type **)&((set)->e[1].p); \
          (variable= *variable##p); \
          variable##p == ((type **)&((set)->e[0].p))?variable##p += 2: \
              (variable##p == ((type **)&((set)->e[1].p))?variable##p--:variable##p++))


























#define FOREACHelem_(set) FOREACHsetelement_(void, set, elem)


























#define FOREACHset_(sets) FOREACHsetelement_(setT, sets, set)














#define SETindex_(set, elem) ((int)((void **)elem##p - (void **)&(set)->e[1].p))










#define SETref_(elem) (elem##p[-1])











#define SETelem_(set, n)           ((set)->e[n].p)










#define SETelemt_(set, n, type)    ((type*)((set)->e[n].p))










#define SETelemaddr_(set, n, type) ((type **)(&((set)->e[n].p)))








#define SETfirst_(set)             ((set)->e[0].p)








#define SETfirstt_(set, type)      ((type*)((set)->e[0].p))








#define SETsecond_(set)            ((set)->e[1].p)







#define SETsecondt_(set, type)     ((type*)((set)->e[1].p))







#define SETaddr_(set,type)         ((type **)(&((set)->e[0].p)))











#define SETreturnsize_(set, size) (((size)= ((set)->e[(set)->maxsize].i))?(--(size)):((size)= (set)->maxsize))










#define SETempty_(set)            (!set || (SETfirst_(set) ? 0 : 1))













#define SETsizeaddr_(set) (&((set)->e[(set)->maxsize]))











#define SETtruncate_(set, size) {set->e[set->maxsize].i= size+1;   \
      set->e[size].p= NULL;}



#ifdef __cplusplus
extern "C" {
#endif

void  qh_setaddsorted(qhT *qh, setT **setp, void *elem);
void  qh_setaddnth(qhT *qh, setT **setp, int nth, void *newelem);
void  qh_setappend(qhT *qh, setT **setp, void *elem);
void  qh_setappend_set(qhT *qh, setT **setp, setT *setA);
void  qh_setappend2ndlast(qhT *qh, setT **setp, void *elem);
void  qh_setcheck(qhT *qh, setT *set, const char *tname, unsigned id);
void  qh_setcompact(qhT *qh, setT *set);
setT *qh_setcopy(qhT *qh, setT *set, int extra);
void *qh_setdel(setT *set, void *elem);
void *qh_setdellast(setT *set);
void *qh_setdelnth(qhT *qh, setT *set, int nth);
void *qh_setdelnthsorted(qhT *qh, setT *set, int nth);
void *qh_setdelsorted(setT *set, void *newelem);
setT *qh_setduplicate(qhT *qh, setT *set, int elemsize);
void **qh_setendpointer(setT *set);
int   qh_setequal(setT *setA, setT *setB);
int   qh_setequal_except(setT *setA, void *skipelemA, setT *setB, void *skipelemB);
int   qh_setequal_skip(setT *setA, int skipA, setT *setB, int skipB);
void  qh_setfree(qhT *qh, setT **set);
void  qh_setfree2(qhT *qh, setT **setp, int elemsize);
void  qh_setfreelong(qhT *qh, setT **set);
int   qh_setin(setT *set, void *setelem);
int qh_setindex(setT *set, void *setelem);
void  qh_setlarger(qhT *qh, setT **setp);
void *qh_setlast(setT *set);
setT *qh_setnew(qhT *qh, int size);
setT *qh_setnew_delnthsorted(qhT *qh, setT *set, int size, int nth, int prepend);
void  qh_setprint(qhT *qh, FILE *fp, const char* string, setT *set);
void  qh_setreplace(qhT *qh, setT *set, void *oldelem, void *newelem);
int qh_setsize(qhT *qh, setT *set);
setT *qh_settemp(qhT *qh, int setsize);
void  qh_settempfree(qhT *qh, setT **set);
void  qh_settempfree_all(qhT *qh);
setT *qh_settemppop(qhT *qh);
void  qh_settemppush(qhT *qh, setT *set);
void  qh_settruncate(qhT *qh, setT *set, int size);
int   qh_setunique(qhT *qh, setT **set, void *elem);
void  qh_setzero(qhT *qh, setT *set, int idx, int size);

#ifdef __cplusplus
}
#endif

#endif
