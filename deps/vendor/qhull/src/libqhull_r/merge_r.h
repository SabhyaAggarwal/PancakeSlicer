/*<html><pre>  -<a                             href="qh-merge_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   merge_r.h
   header file for merge_r.c

   see qh-merge_r.htm and merge_r.c

   Copyright (c) 1993-2015 C.B. Barber.
   $Id: //main/2015/qhull/src/libqhull_r/merge_r.h#3 $$Change: 2079 $
   $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
*/

#ifndef qhDEFmerge
#define qhDEFmerge 1

#include "libqhull_r.h"










#define qh_ANGLEredundant 6.0







#define qh_ANGLEdegen     5.0











#define qh_ANGLEconcave  1.5







typedef enum {
  MRGnone= 0,
  MRGcoplanar,
  MRGanglecoplanar,

  MRGconcave,
  MRGflip,
  MRGridge,

  MRGdegen,
  MRGredundant,

  MRGmirror,
  ENDmrg
} mergeType;







#define qh_MERGEapex     True










typedef struct mergeT mergeT;
struct mergeT {
  realT   angle;
  facetT *facet1;
  facetT *facet2;
  mergeType type;
};
















#define FOREACHmerge_( merges ) FOREACHsetelement_(mergeT, merges, merge)



#ifdef __cplusplus
extern "C" {
#endif

void    qh_premerge(qhT *qh, vertexT *apex, realT maxcentrum, realT maxangle);
void    qh_postmerge(qhT *qh, const char *reason, realT maxcentrum, realT maxangle,
             boolT vneighbors);
void    qh_all_merges(qhT *qh, boolT othermerge, boolT vneighbors);
void    qh_appendmergeset(qhT *qh, facetT *facet, facetT *neighbor, mergeType mergetype, realT *angle);
setT   *qh_basevertices(qhT *qh, facetT *samecycle);
void    qh_checkconnect(qhT *qh  );
boolT   qh_checkzero(qhT *qh, boolT testall);
int     qh_compareangle(const void *p1, const void *p2);
int     qh_comparemerge(const void *p1, const void *p2);
int     qh_comparevisit(const void *p1, const void *p2);
void    qh_copynonconvex(qhT *qh, ridgeT *atridge);
void    qh_degen_redundant_facet(qhT *qh, facetT *facet);
void    qh_degen_redundant_neighbors(qhT *qh, facetT *facet, facetT *delfacet);
vertexT *qh_find_newvertex(qhT *qh, vertexT *oldvertex, setT *vertices, setT *ridges);
void    qh_findbest_test(qhT *qh, boolT testcentrum, facetT *facet, facetT *neighbor,
           facetT **bestfacet, realT *distp, realT *mindistp, realT *maxdistp);
facetT *qh_findbestneighbor(qhT *qh, facetT *facet, realT *distp, realT *mindistp, realT *maxdistp);
void    qh_flippedmerges(qhT *qh, facetT *facetlist, boolT *wasmerge);
void    qh_forcedmerges(qhT *qh, boolT *wasmerge);
void    qh_getmergeset(qhT *qh, facetT *facetlist);
void    qh_getmergeset_initial(qhT *qh, facetT *facetlist);
void    qh_hashridge(qhT *qh, setT *hashtable, int hashsize, ridgeT *ridge, vertexT *oldvertex);
ridgeT *qh_hashridge_find(qhT *qh, setT *hashtable, int hashsize, ridgeT *ridge,
              vertexT *vertex, vertexT *oldvertex, int *hashslot);
void    qh_makeridges(qhT *qh, facetT *facet);
void    qh_mark_dupridges(qhT *qh, facetT *facetlist);
void    qh_maydropneighbor(qhT *qh, facetT *facet);
int     qh_merge_degenredundant(qhT *qh);
void    qh_merge_nonconvex(qhT *qh, facetT *facet1, facetT *facet2, mergeType mergetype);
void    qh_mergecycle(qhT *qh, facetT *samecycle, facetT *newfacet);
void    qh_mergecycle_all(qhT *qh, facetT *facetlist, boolT *wasmerge);
void    qh_mergecycle_facets(qhT *qh, facetT *samecycle, facetT *newfacet);
void    qh_mergecycle_neighbors(qhT *qh, facetT *samecycle, facetT *newfacet);
void    qh_mergecycle_ridges(qhT *qh, facetT *samecycle, facetT *newfacet);
void    qh_mergecycle_vneighbors(qhT *qh, facetT *samecycle, facetT *newfacet);
void    qh_mergefacet(qhT *qh, facetT *facet1, facetT *facet2, realT *mindist, realT *maxdist, boolT mergeapex);
void    qh_mergefacet2d(qhT *qh, facetT *facet1, facetT *facet2);
void    qh_mergeneighbors(qhT *qh, facetT *facet1, facetT *facet2);
void    qh_mergeridges(qhT *qh, facetT *facet1, facetT *facet2);
void    qh_mergesimplex(qhT *qh, facetT *facet1, facetT *facet2, boolT mergeapex);
void    qh_mergevertex_del(qhT *qh, vertexT *vertex, facetT *facet1, facetT *facet2);
void    qh_mergevertex_neighbors(qhT *qh, facetT *facet1, facetT *facet2);
void    qh_mergevertices(qhT *qh, setT *vertices1, setT **vertices);
setT   *qh_neighbor_intersections(qhT *qh, vertexT *vertex);
void    qh_newvertices(qhT *qh, setT *vertices);
boolT   qh_reducevertices(qhT *qh);
vertexT *qh_redundant_vertex(qhT *qh, vertexT *vertex);
boolT   qh_remove_extravertices(qhT *qh, facetT *facet);
vertexT *qh_rename_sharedvertex(qhT *qh, vertexT *vertex, facetT *facet);
void    qh_renameridgevertex(qhT *qh, ridgeT *ridge, vertexT *oldvertex, vertexT *newvertex);
void    qh_renamevertex(qhT *qh, vertexT *oldvertex, vertexT *newvertex, setT *ridges,
                        facetT *oldfacet, facetT *neighborA);
boolT   qh_test_appendmerge(qhT *qh, facetT *facet, facetT *neighbor);
boolT   qh_test_vneighbors(qhT *qh  );
void    qh_tracemerge(qhT *qh, facetT *facet1, facetT *facet2);
void    qh_tracemerging(qhT *qh);
void    qh_updatetested(qhT *qh, facetT *facet1, facetT *facet2);
setT   *qh_vertexridges(qhT *qh, vertexT *vertex);
void    qh_vertexridges_facet(qhT *qh, vertexT *vertex, facetT *facet, setT **ridges);
void    qh_willdelete(qhT *qh, facetT *facet, facetT *replace);

#ifdef __cplusplus
}
#endif

#endif
