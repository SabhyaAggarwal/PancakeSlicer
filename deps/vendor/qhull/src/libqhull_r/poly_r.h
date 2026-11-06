/*<html><pre>  -<a                             href="qh-poly_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   poly_r.h
   header file for poly_r.c and poly2_r.c

   see qh-poly_r.htm, libqhull_r.h and poly_r.c

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/poly_r.h#5 $$Change: 2079 $
   $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
*/

#ifndef qhDEFpoly
#define qhDEFpoly 1

#include "libqhull_r.h"









#define qh_ALGORITHMfault 0







#define qh_DATAfault 1










#define qh_DUPLICATEridge (facetT *)1L










#define qh_MERGEridge (facetT *)2L



















#define FORALLfacet_( facetlist ) if (facetlist ) for ( facet=( facetlist ); facet && facet->next; facet= facet->next )











#define FORALLnew_facets for ( newfacet=qh->newfacet_list;newfacet && newfacet->next;newfacet=newfacet->next )











#define FORALLvertex_( vertexlist ) for (vertex=( vertexlist );vertex && vertex->next;vertex= vertex->next )











#define FORALLvisible_facets for (visible=qh->visible_list; visible && visible->visible; visible= visible->next)











#define FORALLsame_(newfacet) for (same= newfacet->f.samecycle; same != newfacet; same= same->f.samecycle)











#define FORALLsame_cycle_(newfacet) \
     for (same= newfacet->f.samecycle; \
         same; same= (same == newfacet ?  NULL : same->f.samecycle))
















#define FOREACHneighborA_(facet)  FOREACHsetelement_(facetT, facet->neighbors, neighborA)











#define FOREACHvisible_(facets) FOREACHsetelement_(facetT, facets, visible)











#define FOREACHnewfacet_(facets) FOREACHsetelement_(facetT, facets, newfacet)











#define FOREACHvertexA_(vertices) FOREACHsetelement_(vertexT, vertices, vertexA)












#define FOREACHvertexreverse12_(vertices) FOREACHsetelementreverse12_(vertexT, vertices, vertex)




#ifdef __cplusplus
extern "C" {
#endif

void    qh_appendfacet(qhT *qh, facetT *facet);
void    qh_appendvertex(qhT *qh, vertexT *vertex);
void    qh_attachnewfacets(qhT *qh  );
boolT   qh_checkflipped(qhT *qh, facetT *facet, realT *dist, boolT allerror);
void    qh_delfacet(qhT *qh, facetT *facet);
void    qh_deletevisible(qhT *qh  );
setT   *qh_facetintersect(qhT *qh, facetT *facetA, facetT *facetB, int *skipAp,int *skipBp, int extra);
int     qh_gethash(qhT *qh, int hashsize, setT *set, int size, int firstindex, void *skipelem);
facetT *qh_makenewfacet(qhT *qh, setT *vertices, boolT toporient, facetT *facet);
void    qh_makenewplanes(qhT *qh  );
facetT *qh_makenew_nonsimplicial(qhT *qh, facetT *visible, vertexT *apex, int *numnew);
facetT *qh_makenew_simplicial(qhT *qh, facetT *visible, vertexT *apex, int *numnew);
void    qh_matchneighbor(qhT *qh, facetT *newfacet, int newskip, int hashsize,
                          int *hashcount);
void    qh_matchnewfacets(qhT *qh);
boolT   qh_matchvertices(qhT *qh, int firstindex, setT *verticesA, int skipA,
                          setT *verticesB, int *skipB, boolT *same);
facetT *qh_newfacet(qhT *qh);
ridgeT *qh_newridge(qhT *qh);
int     qh_pointid(qhT *qh, pointT *point);
void    qh_removefacet(qhT *qh, facetT *facet);
void    qh_removevertex(qhT *qh, vertexT *vertex);
void    qh_updatevertices(qhT *qh);




void    qh_addhash(void *newelem, setT *hashtable, int hashsize, int hash);
void    qh_check_bestdist(qhT *qh);
void    qh_check_dupridge(qhT *qh, facetT *facet1, realT dist1, facetT *facet2, realT dist2);
void    qh_check_maxout(qhT *qh);
void    qh_check_output(qhT *qh);
void    qh_check_point(qhT *qh, pointT *point, facetT *facet, realT *maxoutside, realT *maxdist, facetT **errfacet1, facetT **errfacet2);
void    qh_check_points(qhT *qh);
void    qh_checkconvex(qhT *qh, facetT *facetlist, int fault);
void    qh_checkfacet(qhT *qh, facetT *facet, boolT newmerge, boolT *waserrorp);
void    qh_checkflipped_all(qhT *qh, facetT *facetlist);
void    qh_checkpolygon(qhT *qh, facetT *facetlist);
void    qh_checkvertex(qhT *qh, vertexT *vertex);
void    qh_clearcenters(qhT *qh, qh_CENTER type);
void    qh_createsimplex(qhT *qh, setT *vertices);
void    qh_delridge(qhT *qh, ridgeT *ridge);
void    qh_delvertex(qhT *qh, vertexT *vertex);
setT   *qh_facet3vertex(qhT *qh, facetT *facet);
facetT *qh_findbestfacet(qhT *qh, pointT *point, boolT bestoutside,
           realT *bestdist, boolT *isoutside);
facetT *qh_findbestlower(qhT *qh, facetT *upperfacet, pointT *point, realT *bestdistp, int *numpart);
facetT *qh_findfacet_all(qhT *qh, pointT *point, realT *bestdist, boolT *isoutside,
                          int *numpart);
int     qh_findgood(qhT *qh, facetT *facetlist, int goodhorizon);
void    qh_findgood_all(qhT *qh, facetT *facetlist);
void    qh_furthestnext(qhT *qh  );
void    qh_furthestout(qhT *qh, facetT *facet);
void    qh_infiniteloop(qhT *qh, facetT *facet);
void    qh_initbuild(qhT *qh);
void    qh_initialhull(qhT *qh, setT *vertices);
setT   *qh_initialvertices(qhT *qh, int dim, setT *maxpoints, pointT *points, int numpoints);
vertexT *qh_isvertex(pointT *point, setT *vertices);
vertexT *qh_makenewfacets(qhT *qh, pointT *point  );
void    qh_matchduplicates(qhT *qh, facetT *atfacet, int atskip, int hashsize, int *hashcount);
void    qh_nearcoplanar(qhT *qh  );
vertexT *qh_nearvertex(qhT *qh, facetT *facet, pointT *point, realT *bestdistp);
int     qh_newhashtable(qhT *qh, int newsize);
vertexT *qh_newvertex(qhT *qh, pointT *point);
ridgeT *qh_nextridge3d(ridgeT *atridge, facetT *facet, vertexT **vertexp);
void    qh_outcoplanar(qhT *qh  );
pointT *qh_point(qhT *qh, int id);
void    qh_point_add(qhT *qh, setT *set, pointT *point, void *elem);
setT   *qh_pointfacet(qhT *qh  );
setT   *qh_pointvertex(qhT *qh  );
void    qh_prependfacet(qhT *qh, facetT *facet, facetT **facetlist);
void    qh_printhashtable(qhT *qh, FILE *fp);
void    qh_printlists(qhT *qh);
void    qh_resetlists(qhT *qh, boolT stats, boolT resetVisible  );
void    qh_setvoronoi_all(qhT *qh);
void    qh_triangulate(qhT *qh  );
void    qh_triangulate_facet(qhT *qh, facetT *facetA, vertexT **first_vertex);
void    qh_triangulate_link(qhT *qh, facetT *oldfacetA, facetT *facetA, facetT *oldfacetB, facetT *facetB);
void    qh_triangulate_mirror(qhT *qh, facetT *facetA, facetT *facetB);
void    qh_triangulate_null(qhT *qh, facetT *facetA);
void    qh_vertexintersect(qhT *qh, setT **vertexsetA,setT *vertexsetB);
setT   *qh_vertexintersect_new(qhT *qh, setT *vertexsetA,setT *vertexsetB);
void    qh_vertexneighbors(qhT *qh  );
boolT   qh_vertexsubset(setT *vertexsetA, setT *vertexsetB);

#ifdef __cplusplus
}
#endif

#endif
