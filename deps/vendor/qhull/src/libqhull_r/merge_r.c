/*<html><pre>  -<a                             href="qh-merge_r.htm#TOC"
  >-------------------------------</a><a name="TOP">-</a>

   merge_r.c
   merges non-convex facets

   see qh-merge_r.htm and merge_r.h

   other modules call qh_premerge() and qh_postmerge()

   the user may call qh_postmerge() to perform additional merges.

   To remove deleted facets and vertices (qhull() in libqhull_r.c):
     qh_partitionvisible(qh, !qh_ALL, &numoutside);  // visible_list, newfacet_list
     qh_deletevisible();         // qh.visible_list
     qh_resetlists(qh, False, qh_RESETvisible);       // qh.visible_list newvertex_list newfacet_list

   assumes qh.CENTERtype= centrum

   merges occur in qh_mergefacet and in qh_mergecycle
   vertex->neighbors not set until the first merge occurs

   Copyright (c) 1993-2015 C.B. Barber.
   $Id: //main/2015/qhull/src/libqhull_r/merge_r.c#5 $$Change: 2064 $
   $DateTime: 2016/01/18 12:36:08 $$Author: bbarber $
*/

#include "qhull_ra.h"

#ifndef qh_NOmerge

























void qh_premerge(qhT *qh, vertexT *apex, realT maxcentrum, realT maxangle) {
  boolT othermerge= False;
  facetT *newfacet;

  if (qh->ZEROcentrum && qh_checkzero(qh, !qh_ALL))
    return;
  trace2((qh, qh->ferr, 2008, "qh_premerge: premerge centrum %2.2g angle %2.2g for apex v%d facetlist f%d\n",
            maxcentrum, maxangle, apex->id, getid_(qh->newfacet_list)));
  if (qh->IStracing >= 4 && qh->num_facets < 50)
    qh_printlists(qh);
  qh->centrum_radius= maxcentrum;
  qh->cos_max= maxangle;
  qh->degen_mergeset= qh_settemp(qh, qh->TEMPsize);
  qh->facet_mergeset= qh_settemp(qh, qh->TEMPsize);
  if (qh->hull_dim >=3) {
    qh_mark_dupridges(qh, qh->newfacet_list);
    qh_mergecycle_all(qh, qh->newfacet_list, &othermerge);
    qh_forcedmerges(qh, &othermerge  );
    FORALLnew_facets {
      if (!newfacet->simplicial && !newfacet->mergeridge)
        qh_degen_redundant_neighbors(qh, newfacet, NULL);
    }
    if (qh_merge_degenredundant(qh))
      othermerge= True;
  }else
    qh_mergecycle_all(qh, qh->newfacet_list, &othermerge);
  qh_flippedmerges(qh, qh->newfacet_list, &othermerge);
  if (!qh->MERGEexact || zzval_(Ztotmerge)) {
    zinc_(Zpremergetot);
    qh->POSTmerging= False;
    qh_getmergeset_initial(qh, qh->newfacet_list);
    qh_all_merges(qh, othermerge, False);
  }
  qh_settempfree(qh, &qh->facet_mergeset);
  qh_settempfree(qh, &qh->degen_mergeset);
}





































void qh_postmerge(qhT *qh, const char *reason, realT maxcentrum, realT maxangle,
                      boolT vneighbors) {
  facetT *newfacet;
  boolT othermerges= False;
  vertexT *vertex;

  if (qh->REPORTfreq || qh->IStracing) {
    qh_buildtracing(qh, NULL, NULL);
    qh_printsummary(qh, qh->ferr);
    if (qh->PRINTstatistics)
      qh_printallstatistics(qh, qh->ferr, "reason");
    qh_fprintf(qh, qh->ferr, 8062, "\n%s with 'C%.2g' and 'A%.2g'\n",
        reason, maxcentrum, maxangle);
  }
  trace2((qh, qh->ferr, 2009, "qh_postmerge: postmerge.  test vneighbors? %d\n",
            vneighbors));
  qh->centrum_radius= maxcentrum;
  qh->cos_max= maxangle;
  qh->POSTmerging= True;
  qh->degen_mergeset= qh_settemp(qh, qh->TEMPsize);
  qh->facet_mergeset= qh_settemp(qh, qh->TEMPsize);
  if (qh->visible_list != qh->facet_list) {
    qh->NEWfacets= True;
    qh->visible_list= qh->newfacet_list= qh->facet_list;
    FORALLnew_facets {
      newfacet->newfacet= True;
       if (!newfacet->simplicial)
        newfacet->newmerge= True;
     zinc_(Zpostfacets);
    }
    qh->newvertex_list= qh->vertex_list;
    FORALLvertices
      vertex->newlist= True;
    if (qh->VERTEXneighbors) {
      FORALLvertices
        vertex->delridge= True;
      if (qh->MERGEexact) {
        if (qh->hull_dim <= qh_DIMreduceBuild)
          qh_reducevertices(qh);
      }
    }
    if (!qh->PREmerge && !qh->MERGEexact)
      qh_flippedmerges(qh, qh->newfacet_list, &othermerges);
  }
  qh_getmergeset_initial(qh, qh->newfacet_list);
  qh_all_merges(qh, False, vneighbors);
  qh_settempfree(qh, &qh->facet_mergeset);
  qh_settempfree(qh, &qh->degen_mergeset);
}





































void qh_all_merges(qhT *qh, boolT othermerge, boolT vneighbors) {
  facetT *facet1, *facet2;
  mergeT *merge;
  boolT wasmerge= True, isreduce;
  void **freelistp;
  vertexT *vertex;
  mergeType mergetype;
  int numcoplanar=0, numconcave=0, numdegenredun= 0, numnewmerges= 0;

  trace2((qh, qh->ferr, 2010, "qh_all_merges: starting to merge facets beginning from f%d\n",
            getid_(qh->newfacet_list)));
  while (True) {
    wasmerge= False;
    while (qh_setsize(qh, qh->facet_mergeset)) {
      while ((merge= (mergeT*)qh_setdellast(qh->facet_mergeset))) {
        facet1= merge->facet1;
        facet2= merge->facet2;
        mergetype= merge->type;
        qh_memfree_(qh, merge, (int)sizeof(mergeT), freelistp);
        if (facet1->visible || facet2->visible)
          continue;
        if ((facet1->newfacet && !facet1->tested)
                || (facet2->newfacet && !facet2->tested)) {
          if (qh->MERGEindependent && mergetype <= MRGanglecoplanar)
            continue;
        }
        qh_merge_nonconvex(qh, facet1, facet2, mergetype);
        numdegenredun += qh_merge_degenredundant(qh);
        numnewmerges++;
        wasmerge= True;
        if (mergetype == MRGconcave)
          numconcave++;
        else
          numcoplanar++;
      }
      if (qh->POSTmerging && qh->hull_dim <= qh_DIMreduceBuild
      && numnewmerges > qh_MAXnewmerges) {
        numnewmerges= 0;
        qh_reducevertices(qh);
      }
      qh_getmergeset(qh, qh->newfacet_list);
    }
    if (qh->VERTEXneighbors) {
      isreduce= False;
      if (qh->hull_dim >=4 && qh->POSTmerging) {
        FORALLvertices
          vertex->delridge= True;
        isreduce= True;
      }
      if ((wasmerge || othermerge) && (!qh->MERGEexact || qh->POSTmerging)
          && qh->hull_dim <= qh_DIMreduceBuild) {
        othermerge= False;
        isreduce= True;
      }
      if (isreduce) {
        if (qh_reducevertices(qh)) {
          qh_getmergeset(qh, qh->newfacet_list);
          continue;
        }
      }
    }
    if (vneighbors && qh_test_vneighbors(qh  ))
      continue;
    break;
  }
  if (qh->CHECKfrequently && !qh->MERGEexact) {
    qh->old_randomdist= qh->RANDOMdist;
    qh->RANDOMdist= False;
    qh_checkconvex(qh, qh->newfacet_list, qh_ALGORITHMfault);

    qh->RANDOMdist= qh->old_randomdist;
  }
  trace1((qh, qh->ferr, 1009, "qh_all_merges: merged %d coplanar facets %d concave facets and %d degen or redundant facets.\n",
    numcoplanar, numconcave, numdegenredun));
  if (qh->IStracing >= 4 && qh->num_facets < 50)
    qh_printlists(qh);
}




























void qh_appendmergeset(qhT *qh, facetT *facet, facetT *neighbor, mergeType mergetype, realT *angle) {
  mergeT *merge, *lastmerge;
  void **freelistp;

  if (facet->redundant)
    return;
  if (facet->degenerate && mergetype == MRGdegen)
    return;
  qh_memalloc_(qh, (int)sizeof(mergeT), freelistp, merge, mergeT);
  merge->facet1= facet;
  merge->facet2= neighbor;
  merge->type= mergetype;
  if (angle && qh->ANGLEmerge)
    merge->angle= *angle;
  if (mergetype < MRGdegen)
    qh_setappend(qh, &(qh->facet_mergeset), merge);
  else if (mergetype == MRGdegen) {
    facet->degenerate= True;
    if (!(lastmerge= (mergeT*)qh_setlast(qh->degen_mergeset))
    || lastmerge->type == MRGdegen)
      qh_setappend(qh, &(qh->degen_mergeset), merge);
    else
      qh_setaddnth(qh, &(qh->degen_mergeset), 0, merge);
  }else if (mergetype == MRGredundant) {
    facet->redundant= True;
    qh_setappend(qh, &(qh->degen_mergeset), merge);
  }else   {
    if (facet->redundant || neighbor->redundant) {
      qh_fprintf(qh, qh->ferr, 6092, "qhull error (qh_appendmergeset): facet f%d or f%d is already a mirrored facet\n",
           facet->id, neighbor->id);
      qh_errexit2(qh, qh_ERRqhull, facet, neighbor);
    }
    if (!qh_setequal(facet->vertices, neighbor->vertices)) {
      qh_fprintf(qh, qh->ferr, 6093, "qhull error (qh_appendmergeset): mirrored facets f%d and f%d do not have the same vertices\n",
           facet->id, neighbor->id);
      qh_errexit2(qh, qh_ERRqhull, facet, neighbor);
    }
    facet->redundant= True;
    neighbor->redundant= True;
    qh_setappend(qh, &(qh->degen_mergeset), merge);
  }
}






















setT *qh_basevertices(qhT *qh, facetT *samecycle) {
  facetT *same;
  vertexT *apex, *vertex, **vertexp;
  setT *vertices= qh_settemp(qh, qh->TEMPsize);

  apex= SETfirstt_(samecycle->vertices, vertexT);
  apex->visitid= ++qh->vertex_visit;
  FORALLsame_cycle_(samecycle) {
    if (same->mergeridge)
      continue;
    FOREACHvertex_(same->vertices) {
      if (vertex->visitid != qh->vertex_visit) {
        qh_setappend(qh, &vertices, vertex);
        vertex->visitid= qh->vertex_visit;
        vertex->seen= False;
      }
    }
  }
  trace4((qh, qh->ferr, 4019, "qh_basevertices: found %d vertices\n",
         qh_setsize(qh, vertices)));
  return vertices;
}



















void qh_checkconnect(qhT *qh  ) {
  facetT *facet, *newfacet, *errfacet= NULL, *neighbor, **neighborp;

  facet= qh->newfacet_list;
  qh_removefacet(qh, facet);
  qh_appendfacet(qh, facet);
  facet->visitid= ++qh->visit_id;
  FORALLfacet_(facet) {
    FOREACHneighbor_(facet) {
      if (neighbor->visitid != qh->visit_id) {
        qh_removefacet(qh, neighbor);
        qh_appendfacet(qh, neighbor);
        neighbor->visitid= qh->visit_id;
      }
    }
  }
  FORALLnew_facets {
    if (newfacet->visitid == qh->visit_id)
      break;
    qh_fprintf(qh, qh->ferr, 6094, "qhull error: f%d is not attached to the new facets\n",
         newfacet->id);
    errfacet= newfacet;
  }
  if (errfacet)
    qh_errexit(qh, qh_ERRqhull, errfacet, NULL);
}





































boolT qh_checkzero(qhT *qh, boolT testall) {
  facetT *facet, *neighbor, **neighborp;
  facetT *horizon, *facetlist;
  int neighbor_i;
  vertexT *vertex, **vertexp;
  realT dist;

  if (testall)
    facetlist= qh->facet_list;
  else {
    facetlist= qh->newfacet_list;
    FORALLfacet_(facetlist) {
      horizon= SETfirstt_(facet->neighbors, facetT);
      if (!horizon->simplicial)
        goto LABELproblem;
      if (facet->flipped || facet->dupridge || !facet->normal)
        goto LABELproblem;
    }
    if (qh->MERGEexact && qh->ZEROall_ok) {
      trace2((qh, qh->ferr, 2011, "qh_checkzero: skip convexity check until first pre-merge\n"));
      return True;
    }
  }
  FORALLfacet_(facetlist) {
    qh->vertex_visit++;
    neighbor_i= 0;
    horizon= NULL;
    FOREACHneighbor_(facet) {
      if (!neighbor_i && !testall) {
        horizon= neighbor;
        neighbor_i++;
        continue;
      }
      vertex= SETelemt_(facet->vertices, neighbor_i++, vertexT);
      vertex->visitid= qh->vertex_visit;
      zzinc_(Zdistzero);
      qh_distplane(qh, vertex->point, neighbor, &dist);
      if (dist >= -qh->DISTround) {
        qh->ZEROall_ok= False;
        if (!qh->MERGEexact || testall || dist > qh->DISTround)
          goto LABELnonconvex;
      }
    }
    if (!testall && horizon) {
      FOREACHvertex_(horizon->vertices) {
        if (vertex->visitid != qh->vertex_visit) {
          zzinc_(Zdistzero);
          qh_distplane(qh, vertex->point, facet, &dist);
          if (dist >= -qh->DISTround) {
            qh->ZEROall_ok= False;
            if (!qh->MERGEexact || dist > qh->DISTround)
              goto LABELnonconvex;
          }
          break;
        }
      }
    }
  }
  trace2((qh, qh->ferr, 2012, "qh_checkzero: testall %d, facets are %s\n", testall,
        (qh->MERGEexact && !testall) ?
           "not concave, flipped, or duplicate ridged" : "clearly convex"));
  return True;

 LABELproblem:
  qh->ZEROall_ok= False;
  trace2((qh, qh->ferr, 2013, "qh_checkzero: facet f%d needs pre-merging\n",
       facet->id));
  return False;

 LABELnonconvex:
  trace2((qh, qh->ferr, 2014, "qh_checkzero: facet f%d and f%d are not clearly convex.  v%d dist %.2g\n",
         facet->id, neighbor->id, vertex->id, dist));
  return False;
}







int qh_compareangle(const void *p1, const void *p2) {
  const mergeT *a= *((mergeT *const*)p1), *b= *((mergeT *const*)p2);

  return((a->angle > b->angle) ? 1 : -1);
}







int qh_comparemerge(const void *p1, const void *p2) {
  const mergeT *a= *((mergeT *const*)p1), *b= *((mergeT *const*)p2);

  return(a->type - b->type);
}







int qh_comparevisit(const void *p1, const void *p2) {
  const vertexT *a= *((vertexT *const*)p1), *b= *((vertexT *const*)p2);

  return(a->visitid - b->visitid);
}















void qh_copynonconvex(qhT *qh, ridgeT *atridge) {
  facetT *facet, *otherfacet;
  ridgeT *ridge, **ridgep;

  facet= atridge->top;
  otherfacet= atridge->bottom;
  FOREACHridge_(facet->ridges) {
    if (otherfacet == otherfacet_(ridge, facet) && ridge != atridge) {
      ridge->nonconvex= True;
      trace4((qh, qh->ferr, 4020, "qh_copynonconvex: moved nonconvex flag from r%d to r%d\n",
              atridge->id, ridge->id));
      break;
    }
  }
}



















void qh_degen_redundant_facet(qhT *qh, facetT *facet) {
  vertexT *vertex, **vertexp;
  facetT *neighbor, **neighborp;

  trace4((qh, qh->ferr, 4021, "qh_degen_redundant_facet: test facet f%d for degen/redundant\n",
          facet->id));
  FOREACHneighbor_(facet) {
    qh->vertex_visit++;
    FOREACHvertex_(neighbor->vertices)
      vertex->visitid= qh->vertex_visit;
    FOREACHvertex_(facet->vertices) {
      if (vertex->visitid != qh->vertex_visit)
        break;
    }
    if (!vertex) {
      qh_appendmergeset(qh, facet, neighbor, MRGredundant, NULL);
      trace2((qh, qh->ferr, 2015, "qh_degen_redundant_facet: f%d is contained in f%d.  merge\n", facet->id, neighbor->id));
      return;
    }
  }
  if (qh_setsize(qh, facet->neighbors) < qh->hull_dim) {
    qh_appendmergeset(qh, facet, facet, MRGdegen, NULL);
    trace2((qh, qh->ferr, 2016, "qh_degen_redundant_neighbors: f%d is degenerate.\n", facet->id));
  }
}































void qh_degen_redundant_neighbors(qhT *qh, facetT *facet, facetT *delfacet) {
  vertexT *vertex, **vertexp;
  facetT *neighbor, **neighborp;
  int size;

  trace4((qh, qh->ferr, 4022, "qh_degen_redundant_neighbors: test neighbors of f%d with delfacet f%d\n",
          facet->id, getid_(delfacet)));
  if ((size= qh_setsize(qh, facet->neighbors)) < qh->hull_dim) {
    qh_appendmergeset(qh, facet, facet, MRGdegen, NULL);
    trace2((qh, qh->ferr, 2017, "qh_degen_redundant_neighbors: f%d is degenerate with %d neighbors.\n", facet->id, size));
  }
  if (!delfacet)
    delfacet= facet;
  qh->vertex_visit++;
  FOREACHvertex_(facet->vertices)
    vertex->visitid= qh->vertex_visit;
  FOREACHneighbor_(delfacet) {

    if (neighbor == facet)
      continue;
    FOREACHvertex_(neighbor->vertices) {
      if (vertex->visitid != qh->vertex_visit)
        break;
    }
    if (!vertex) {
      qh_appendmergeset(qh, neighbor, facet, MRGredundant, NULL);
      trace2((qh, qh->ferr, 2018, "qh_degen_redundant_neighbors: f%d is contained in f%d.  merge\n", neighbor->id, facet->id));
    }
  }
  FOREACHneighbor_(delfacet) {
    if (neighbor == facet)
      continue;
    if ((size= qh_setsize(qh, neighbor->neighbors)) < qh->hull_dim) {
      qh_appendmergeset(qh, neighbor, neighbor, MRGdegen, NULL);
      trace2((qh, qh->ferr, 2019, "qh_degen_redundant_neighbors: f%d is degenerate with %d neighbors.  Neighbor of f%d.\n", neighbor->id, size, facet->id));
    }
  }
}
































vertexT *qh_find_newvertex(qhT *qh, vertexT *oldvertex, setT *vertices, setT *ridges) {
  vertexT *vertex, **vertexp;
  setT *newridges;
  ridgeT *ridge, **ridgep;
  int size, hashsize;
  int hash;

#ifndef qh_NOtrace
  if (qh->IStracing >= 4) {
    qh_fprintf(qh, qh->ferr, 8063, "qh_find_newvertex: find new vertex for v%d from ",
             oldvertex->id);
    FOREACHvertex_(vertices)
      qh_fprintf(qh, qh->ferr, 8064, "v%d ", vertex->id);
    FOREACHridge_(ridges)
      qh_fprintf(qh, qh->ferr, 8065, "r%d ", ridge->id);
    qh_fprintf(qh, qh->ferr, 8066, "\n");
  }
#endif
  FOREACHvertex_(vertices)
    vertex->visitid= 0;
  FOREACHridge_(ridges) {
    FOREACHvertex_(ridge->vertices)
      vertex->visitid++;
  }
  FOREACHvertex_(vertices) {
    if (!vertex->visitid) {
      qh_setdelnth(qh, vertices, SETindex_(vertices,vertex));
      vertexp--;
    }
  }
  qh->vertex_visit += (unsigned int)qh_setsize(qh, ridges);
  if (!qh_setsize(qh, vertices)) {
    trace4((qh, qh->ferr, 4023, "qh_find_newvertex: vertices not in ridges for v%d\n",
            oldvertex->id));
    return NULL;
  }
  qsort(SETaddr_(vertices, vertexT), (size_t)qh_setsize(qh, vertices),
                sizeof(vertexT *), qh_comparevisit);

  if (qh->PRINTstatistics) {
    size= qh_setsize(qh, vertices);
    zinc_(Zintersect);
    zadd_(Zintersecttot, size);
    zmax_(Zintersectmax, size);
  }
  hashsize= qh_newhashtable(qh, qh_setsize(qh, ridges));
  FOREACHridge_(ridges)
    qh_hashridge(qh, qh->hash_table, hashsize, ridge, oldvertex);
  FOREACHvertex_(vertices) {
    newridges= qh_vertexridges(qh, vertex);
    FOREACHridge_(newridges) {
      if (qh_hashridge_find(qh, qh->hash_table, hashsize, ridge, vertex, oldvertex, &hash)) {
        zinc_(Zdupridge);
        break;
      }
    }
    qh_settempfree(qh, &newridges);
    if (!ridge)
      break;
  }
  if (vertex) {

    trace2((qh, qh->ferr, 2020, "qh_find_newvertex: found v%d for old v%d from %d vertices and %d ridges.\n",
      vertex->id, oldvertex->id, qh_setsize(qh, vertices), qh_setsize(qh, ridges)));
  }else {
    zinc_(Zfindfail);
    trace0((qh, qh->ferr, 14, "qh_find_newvertex: no vertex for renaming v%d(all duplicated ridges) during p%d\n",
      oldvertex->id, qh->furthest_id));
  }
  qh_setfree(qh, &qh->hash_table);
  return vertex;
}















void qh_findbest_test(qhT *qh, boolT testcentrum, facetT *facet, facetT *neighbor,
      facetT **bestfacet, realT *distp, realT *mindistp, realT *maxdistp) {
  realT dist, mindist, maxdist;

  if (testcentrum) {
    zzinc_(Zbestdist);
    qh_distplane(qh, facet->center, neighbor, &dist);
    dist *= qh->hull_dim;
    if (dist < 0) {
      maxdist= 0;
      mindist= dist;
      dist= -dist;
    }else {
      mindist= 0;
      maxdist= dist;
    }
  }else
    dist= qh_getdistance(qh, facet, neighbor, &mindist, &maxdist);
  if (dist < *distp) {
    *bestfacet= neighbor;
    *mindistp= mindist;
    *maxdistp= maxdist;
    *distp= dist;
  }
}




























facetT *qh_findbestneighbor(qhT *qh, facetT *facet, realT *distp, realT *mindistp, realT *maxdistp) {
  facetT *neighbor, **neighborp, *bestfacet= NULL;
  ridgeT *ridge, **ridgep;
  boolT nonconvex= True, testcentrum= False;
  int size= qh_setsize(qh, facet->vertices);

  if(qh->CENTERtype==qh_ASvoronoi){
    qh_fprintf(qh, qh->ferr, 6272, "qhull error: cannot call qh_findbestneighor for f%d while qh.CENTERtype is qh_ASvoronoi\n", facet->id);
    qh_errexit(qh, qh_ERRqhull, facet, NULL);
  }
  *distp= REALmax;
  if (size > qh_BESTcentrum2 * qh->hull_dim + qh_BESTcentrum) {
    testcentrum= True;
    zinc_(Zbestcentrum);
    if (!facet->center)
       facet->center= qh_getcentrum(qh, facet);
  }
  if (size > qh->hull_dim + qh_BESTnonconvex) {
    FOREACHridge_(facet->ridges) {
      if (ridge->nonconvex) {
        neighbor= otherfacet_(ridge, facet);
        qh_findbest_test(qh, testcentrum, facet, neighbor,
                          &bestfacet, distp, mindistp, maxdistp);
      }
    }
  }
  if (!bestfacet) {
    nonconvex= False;
    FOREACHneighbor_(facet)
      qh_findbest_test(qh, testcentrum, facet, neighbor,
                        &bestfacet, distp, mindistp, maxdistp);
  }
  if (!bestfacet) {
    qh_fprintf(qh, qh->ferr, 6095, "qhull internal error (qh_findbestneighbor): no neighbors for f%d\n", facet->id);
    qh_errexit(qh, qh_ERRqhull, facet, NULL);
  }
  if (testcentrum)
    qh_getdistance(qh, facet, bestfacet, mindistp, maxdistp);
  trace3((qh, qh->ferr, 3002, "qh_findbestneighbor: f%d is best neighbor for f%d testcentrum? %d nonconvex? %d dist %2.2g min %2.2g max %2.2g\n",
     bestfacet->id, facet->id, testcentrum, nonconvex, *distp, *mindistp, *maxdistp));
  return(bestfacet);
}


























void qh_flippedmerges(qhT *qh, facetT *facetlist, boolT *wasmerge) {
  facetT *facet, *neighbor, *facet1;
  realT dist, mindist, maxdist;
  mergeT *merge, **mergep;
  setT *othermerges;
  int nummerge=0;

  trace4((qh, qh->ferr, 4024, "qh_flippedmerges: begin\n"));
  FORALLfacet_(facetlist) {
    if (facet->flipped && !facet->visible)
      qh_appendmergeset(qh, facet, facet, MRGflip, NULL);
  }
  othermerges= qh_settemppop(qh);
  qh->facet_mergeset= qh_settemp(qh, qh->TEMPsize);
  qh_settemppush(qh, othermerges);
  FOREACHmerge_(othermerges) {
    facet1= merge->facet1;
    if (merge->type != MRGflip || facet1->visible)
      continue;
    if (qh->TRACEmerge-1 == zzval_(Ztotmerge))
      qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
    neighbor= qh_findbestneighbor(qh, facet1, &dist, &mindist, &maxdist);
    trace0((qh, qh->ferr, 15, "qh_flippedmerges: merge flipped f%d into f%d dist %2.2g during p%d\n",
      facet1->id, neighbor->id, dist, qh->furthest_id));
    qh_mergefacet(qh, facet1, neighbor, &mindist, &maxdist, !qh_MERGEapex);
    nummerge++;
    if (qh->PRINTstatistics) {
      zinc_(Zflipped);
      wadd_(Wflippedtot, dist);
      wmax_(Wflippedmax, dist);
    }
    qh_merge_degenredundant(qh);
  }
  FOREACHmerge_(othermerges) {
    if (merge->facet1->visible || merge->facet2->visible)
      qh_memfree(qh, merge, (int)sizeof(mergeT));
    else
      qh_setappend(qh, &qh->facet_mergeset, merge);
  }
  qh_settempfree(qh, &othermerges);
  if (nummerge)
    *wasmerge= True;
  trace1((qh, qh->ferr, 1010, "qh_flippedmerges: merged %d flipped facets into a good neighbor\n", nummerge));
}






























void qh_forcedmerges(qhT *qh, boolT *wasmerge) {
  facetT *facet1, *facet2;
  mergeT *merge, **mergep;
  realT dist1, dist2, mindist1, mindist2, maxdist1, maxdist2;
  setT *othermerges;
  int nummerge=0, numflip=0;

  if (qh->TRACEmerge-1 == zzval_(Ztotmerge))
    qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
  trace4((qh, qh->ferr, 4025, "qh_forcedmerges: begin\n"));
  othermerges= qh_settemppop(qh);
  qh->facet_mergeset= qh_settemp(qh, qh->TEMPsize);
  qh_settemppush(qh, othermerges);
  FOREACHmerge_(othermerges) {
    if (merge->type != MRGridge)
        continue;
    if (qh->TRACEmerge-1 == zzval_(Ztotmerge))
        qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
    facet1= merge->facet1;
    facet2= merge->facet2;
    while (facet1->visible)
      facet1= facet1->f.replace;
    while (facet2->visible)
      facet2= facet2->f.replace;
    if (facet1 == facet2)
      continue;
    if (!qh_setin(facet2->neighbors, facet1)) {
      qh_fprintf(qh, qh->ferr, 6096, "qhull internal error (qh_forcedmerges): f%d and f%d had a duplicate ridge but as f%d and f%d they are no longer neighbors\n",
               merge->facet1->id, merge->facet2->id, facet1->id, facet2->id);
      qh_errexit2(qh, qh_ERRqhull, facet1, facet2);
    }
    dist1= qh_getdistance(qh, facet1, facet2, &mindist1, &maxdist1);
    dist2= qh_getdistance(qh, facet2, facet1, &mindist2, &maxdist2);
    qh_check_dupridge(qh, facet1, dist1, facet2, dist2);
    if (dist1 < dist2)
      qh_mergefacet(qh, facet1, facet2, &mindist1, &maxdist1, !qh_MERGEapex);
    else {
      qh_mergefacet(qh, facet2, facet1, &mindist2, &maxdist2, !qh_MERGEapex);
      dist1= dist2;
      facet1= facet2;
    }
    if (facet1->flipped) {
      zinc_(Zmergeflipdup);
      numflip++;
    }else
      nummerge++;
    if (qh->PRINTstatistics) {
      zinc_(Zduplicate);
      wadd_(Wduplicatetot, dist1);
      wmax_(Wduplicatemax, dist1);
    }
  }
  FOREACHmerge_(othermerges) {
    if (merge->type == MRGridge)
      qh_memfree(qh, merge, (int)sizeof(mergeT));
    else
      qh_setappend(qh, &qh->facet_mergeset, merge);
  }
  qh_settempfree(qh, &othermerges);
  if (nummerge)
    *wasmerge= True;
  trace1((qh, qh->ferr, 1011, "qh_forcedmerges: merged %d facets and %d flipped facets across duplicated ridges\n",
                nummerge, numflip));
}































void qh_getmergeset(qhT *qh, facetT *facetlist) {
  facetT *facet, *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;
  int nummerges;

  nummerges= qh_setsize(qh, qh->facet_mergeset);
  trace4((qh, qh->ferr, 4026, "qh_getmergeset: started.\n"));
  qh->visit_id++;
  FORALLfacet_(facetlist) {
    if (facet->tested)
      continue;
    facet->visitid= qh->visit_id;
    facet->tested= True;
    FOREACHneighbor_(facet)
      neighbor->seen= False;
    FOREACHridge_(facet->ridges) {
      if (ridge->tested && !ridge->nonconvex)
        continue;

      neighbor= otherfacet_(ridge, facet);
      if (neighbor->seen) {
        ridge->tested= True;
        ridge->nonconvex= False;
      }else if (neighbor->visitid != qh->visit_id) {
        ridge->tested= True;
        ridge->nonconvex= False;
        neighbor->seen= True;
        if (qh_test_appendmerge(qh, facet, neighbor))
          ridge->nonconvex= True;
      }
    }
  }
  nummerges= qh_setsize(qh, qh->facet_mergeset);
  if (qh->ANGLEmerge)
    qsort(SETaddr_(qh->facet_mergeset, mergeT), (size_t)nummerges, sizeof(mergeT *), qh_compareangle);
  else
    qsort(SETaddr_(qh->facet_mergeset, mergeT), (size_t)nummerges, sizeof(mergeT *), qh_comparemerge);
  if (qh->POSTmerging) {
    zadd_(Zmergesettot2, nummerges);
  }else {
    zadd_(Zmergesettot, nummerges);
    zmax_(Zmergesetmax, nummerges);
  }
  trace2((qh, qh->ferr, 2021, "qh_getmergeset: %d merges found\n", nummerges));
}




























void qh_getmergeset_initial(qhT *qh, facetT *facetlist) {
  facetT *facet, *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;
  int nummerges;

  qh->visit_id++;
  FORALLfacet_(facetlist) {
    facet->visitid= qh->visit_id;
    facet->tested= True;
    FOREACHneighbor_(facet) {
      if (neighbor->visitid != qh->visit_id) {
        if (qh_test_appendmerge(qh, facet, neighbor)) {
          FOREACHridge_(neighbor->ridges) {
            if (facet == otherfacet_(ridge, neighbor)) {
              ridge->nonconvex= True;
              break;
            }
          }
        }
      }
    }
    FOREACHridge_(facet->ridges)
      ridge->tested= True;
  }
  nummerges= qh_setsize(qh, qh->facet_mergeset);
  if (qh->ANGLEmerge)
    qsort(SETaddr_(qh->facet_mergeset, mergeT), (size_t)nummerges, sizeof(mergeT *), qh_compareangle);
  else
    qsort(SETaddr_(qh->facet_mergeset, mergeT), (size_t)nummerges, sizeof(mergeT *), qh_comparemerge);
  if (qh->POSTmerging) {
    zadd_(Zmergeinittot2, nummerges);
  }else {
    zadd_(Zmergeinittot, nummerges);
    zmax_(Zmergeinitmax, nummerges);
  }
  trace2((qh, qh->ferr, 2022, "qh_getmergeset_initial: %d merges found\n", nummerges));
}















void qh_hashridge(qhT *qh, setT *hashtable, int hashsize, ridgeT *ridge, vertexT *oldvertex) {
  int hash;
  ridgeT *ridgeA;

  hash= qh_gethash(qh, hashsize, ridge->vertices, qh->hull_dim-1, 0, oldvertex);
  while (True) {
    if (!(ridgeA= SETelemt_(hashtable, hash, ridgeT))) {
      SETelem_(hashtable, hash)= ridge;
      break;
    }else if (ridgeA == ridge)
      break;
    if (++hash == hashsize)
      hash= 0;
  }
}




























ridgeT *qh_hashridge_find(qhT *qh, setT *hashtable, int hashsize, ridgeT *ridge,
              vertexT *vertex, vertexT *oldvertex, int *hashslot) {
  int hash;
  ridgeT *ridgeA;

  *hashslot= 0;
  zinc_(Zhashridge);
  hash= qh_gethash(qh, hashsize, ridge->vertices, qh->hull_dim-1, 0, vertex);
  while ((ridgeA= SETelemt_(hashtable, hash, ridgeT))) {
    if (ridgeA == ridge)
      *hashslot= -1;
    else {
      zinc_(Zhashridgetest);
      if (qh_setequal_except(ridge->vertices, vertex, ridgeA->vertices, oldvertex))
        return ridgeA;
    }
    if (++hash == hashsize)
      hash= 0;
  }
  if (!*hashslot)
    *hashslot= hash;
  return NULL;
}




























void qh_makeridges(qhT *qh, facetT *facet) {
  facetT *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;
  int neighbor_i, neighbor_n;
  boolT toporient, mergeridge= False;

  if (!facet->simplicial)
    return;
  trace4((qh, qh->ferr, 4027, "qh_makeridges: make ridges for f%d\n", facet->id));
  facet->simplicial= False;
  FOREACHneighbor_(facet) {
    if (neighbor == qh_MERGEridge)
      mergeridge= True;
    else
      neighbor->seen= False;
  }
  FOREACHridge_(facet->ridges)
    otherfacet_(ridge, facet)->seen= True;
  FOREACHneighbor_i_(qh, facet) {
    if (neighbor == qh_MERGEridge)
      continue;
    else if (!neighbor->seen) {
      ridge= qh_newridge(qh);
      ridge->vertices= qh_setnew_delnthsorted(qh, facet->vertices, qh->hull_dim,
                                                          neighbor_i, 0);
      toporient= facet->toporient ^ (neighbor_i & 0x1);
      if (toporient) {
        ridge->top= facet;
        ridge->bottom= neighbor;
      }else {
        ridge->top= neighbor;
        ridge->bottom= facet;
      }
#if 0
      flip= (facet->toporient ^ neighbor->toporient)^(skip1 & 0x1) ^ (skip2 & 0x1);
      if (facet->toporient ^ (skip1 & 0x1) ^ flip) {
        ridge->top= neighbor;
        ridge->bottom= facet;
      }else {
        ridge->top= facet;
        ridge->bottom= neighbor;
      }
#endif
      qh_setappend(qh, &(facet->ridges), ridge);
      qh_setappend(qh, &(neighbor->ridges), ridge);
    }
  }
  if (mergeridge) {
    while (qh_setdel(facet->neighbors, qh_MERGEridge))
      ;
  }
}






































void qh_mark_dupridges(qhT *qh, facetT *facetlist) {
  facetT *facet, *neighbor, **neighborp;
  int nummerge=0;
  mergeT *merge, **mergep;


  trace4((qh, qh->ferr, 4028, "qh_mark_dupridges: identify duplicate ridges\n"));
  FORALLfacet_(facetlist) {
    if (facet->dupridge) {
      FOREACHneighbor_(facet) {
        if (neighbor == qh_MERGEridge) {
          facet->mergeridge= True;
          continue;
        }
        if (neighbor->dupridge
        && !qh_setin(neighbor->neighbors, facet)) {
          qh_appendmergeset(qh, facet, neighbor, MRGridge, NULL);
          facet->mergeridge2= True;
          facet->mergeridge= True;
          nummerge++;
        }
      }
    }
  }
  if (!nummerge)
    return;
  FORALLfacet_(facetlist) {
    if (facet->mergeridge && !facet->mergeridge2)
      qh_makeridges(qh, facet);
  }
  FOREACHmerge_(qh->facet_mergeset) {
    if (merge->type == MRGridge) {
      qh_setappend(qh, &merge->facet2->neighbors, merge->facet1);
      qh_makeridges(qh, merge->facet1);
    }
  }
  trace1((qh, qh->ferr, 1012, "qh_mark_dupridges: found %d duplicated ridges\n",
                nummerge));
}

























void qh_maydropneighbor(qhT *qh, facetT *facet) {
  ridgeT *ridge, **ridgep;
  realT angledegen= qh_ANGLEdegen;
  facetT *neighbor, **neighborp;

  qh->visit_id++;
  trace4((qh, qh->ferr, 4029, "qh_maydropneighbor: test f%d for no ridges to a neighbor\n",
          facet->id));
  FOREACHridge_(facet->ridges) {
    ridge->top->visitid= qh->visit_id;
    ridge->bottom->visitid= qh->visit_id;
  }
  FOREACHneighbor_(facet) {
    if (neighbor->visitid != qh->visit_id) {
      trace0((qh, qh->ferr, 17, "qh_maydropneighbor: facets f%d and f%d are no longer neighbors during p%d\n",
            facet->id, neighbor->id, qh->furthest_id));
      zinc_(Zdropneighbor);
      qh_setdel(facet->neighbors, neighbor);
      neighborp--;
      qh_setdel(neighbor->neighbors, facet);
      if (qh_setsize(qh, neighbor->neighbors) < qh->hull_dim) {
        zinc_(Zdropdegen);
        qh_appendmergeset(qh, neighbor, neighbor, MRGdegen, &angledegen);
        trace2((qh, qh->ferr, 2023, "qh_maydropneighbors: f%d is degenerate.\n", neighbor->id));
      }
    }
  }
  if (qh_setsize(qh, facet->neighbors) < qh->hull_dim) {
    zinc_(Zdropdegen);
    qh_appendmergeset(qh, facet, facet, MRGdegen, &angledegen);
    trace2((qh, qh->ferr, 2024, "qh_maydropneighbors: f%d is degenerate.\n", facet->id));
  }
}



























int qh_merge_degenredundant(qhT *qh) {
  int size;
  mergeT *merge;
  facetT *bestneighbor, *facet1, *facet2;
  realT dist, mindist, maxdist;
  vertexT *vertex, **vertexp;
  int nummerges= 0;
  mergeType mergetype;

  while ((merge= (mergeT*)qh_setdellast(qh->degen_mergeset))) {
    facet1= merge->facet1;
    facet2= merge->facet2;
    mergetype= merge->type;
    qh_memfree(qh, merge, (int)sizeof(mergeT));
    if (facet1->visible)
      continue;
    facet1->degenerate= False;
    facet1->redundant= False;
    if (qh->TRACEmerge-1 == zzval_(Ztotmerge))
      qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
    if (mergetype == MRGredundant) {
      zinc_(Zneighbor);
      while (facet2->visible) {
        if (!facet2->f.replace) {
          qh_fprintf(qh, qh->ferr, 6097, "qhull internal error (qh_merge_degenredunant): f%d redundant but f%d has no replacement\n",
               facet1->id, facet2->id);
          qh_errexit2(qh, qh_ERRqhull, facet1, facet2);
        }
        facet2= facet2->f.replace;
      }
      if (facet1 == facet2) {
        qh_degen_redundant_facet(qh, facet1);
        continue;
      }
      trace2((qh, qh->ferr, 2025, "qh_merge_degenredundant: facet f%d is contained in f%d, will merge\n",
            facet1->id, facet2->id));
      qh_mergefacet(qh, facet1, facet2, NULL, NULL, !qh_MERGEapex);

      nummerges++;
    }else {
      if (!(size= qh_setsize(qh, facet1->neighbors))) {
        zinc_(Zdelfacetdup);
        trace2((qh, qh->ferr, 2026, "qh_merge_degenredundant: facet f%d has no neighbors.  Deleted\n", facet1->id));
        qh_willdelete(qh, facet1, NULL);
        FOREACHvertex_(facet1->vertices) {
          qh_setdel(vertex->neighbors, facet1);
          if (!SETfirst_(vertex->neighbors)) {
            zinc_(Zdegenvertex);
            trace2((qh, qh->ferr, 2027, "qh_merge_degenredundant: deleted v%d because f%d has no neighbors\n",
                 vertex->id, facet1->id));
            vertex->deleted= True;
            qh_setappend(qh, &qh->del_vertices, vertex);
          }
        }
        nummerges++;
      }else if (size < qh->hull_dim) {
        bestneighbor= qh_findbestneighbor(qh, facet1, &dist, &mindist, &maxdist);
        trace2((qh, qh->ferr, 2028, "qh_merge_degenredundant: facet f%d has %d neighbors, merge into f%d dist %2.2g\n",
              facet1->id, size, bestneighbor->id, dist));
        qh_mergefacet(qh, facet1, bestneighbor, &mindist, &maxdist, !qh_MERGEapex);
        nummerges++;
        if (qh->PRINTstatistics) {
          zinc_(Zdegen);
          wadd_(Wdegentot, dist);
          wmax_(Wdegenmax, dist);
        }
      }
    }
  }
  return nummerges;
}


















void qh_merge_nonconvex(qhT *qh, facetT *facet1, facetT *facet2, mergeType mergetype) {
  facetT *bestfacet, *bestneighbor, *neighbor;
  realT dist, dist2, mindist, mindist2, maxdist, maxdist2;

  if (qh->TRACEmerge-1 == zzval_(Ztotmerge))
    qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
  trace3((qh, qh->ferr, 3003, "qh_merge_nonconvex: merge #%d for f%d and f%d type %d\n",
      zzval_(Ztotmerge) + 1, facet1->id, facet2->id, mergetype));

  if (!facet1->newfacet) {
    bestfacet= facet2;
    facet2= facet1;
    facet1= bestfacet;
  }else
    bestfacet= facet1;
  bestneighbor= qh_findbestneighbor(qh, bestfacet, &dist, &mindist, &maxdist);
  neighbor= qh_findbestneighbor(qh, facet2, &dist2, &mindist2, &maxdist2);
  if (dist < dist2) {
    qh_mergefacet(qh, bestfacet, bestneighbor, &mindist, &maxdist, !qh_MERGEapex);
  }else if (qh->AVOIDold && !facet2->newfacet
  && ((mindist >= -qh->MAXcoplanar && maxdist <= qh->max_outside)
       || dist * 1.5 < dist2)) {
    zinc_(Zavoidold);
    wadd_(Wavoidoldtot, dist);
    wmax_(Wavoidoldmax, dist);
    trace2((qh, qh->ferr, 2029, "qh_merge_nonconvex: avoid merging old facet f%d dist %2.2g.  Use f%d dist %2.2g instead\n",
           facet2->id, dist2, facet1->id, dist2));
    qh_mergefacet(qh, bestfacet, bestneighbor, &mindist, &maxdist, !qh_MERGEapex);
  }else {
    qh_mergefacet(qh, facet2, neighbor, &mindist2, &maxdist2, !qh_MERGEapex);
    dist= dist2;
  }
  if (qh->PRINTstatistics) {
    if (mergetype == MRGanglecoplanar) {
      zinc_(Zacoplanar);
      wadd_(Wacoplanartot, dist);
      wmax_(Wacoplanarmax, dist);
    }else if (mergetype == MRGconcave) {
      zinc_(Zconcave);
      wadd_(Wconcavetot, dist);
      wmax_(Wconcavemax, dist);
    }else {
      zinc_(Zcoplanar);
      wadd_(Wcoplanartot, dist);
      wmax_(Wcoplanarmax, dist);
    }
  }
}






























void qh_mergecycle(qhT *qh, facetT *samecycle, facetT *newfacet) {
  int traceonce= False, tracerestore= 0;
  vertexT *apex;
#ifndef qh_NOtrace
  facetT *same;
#endif

  if (newfacet->tricoplanar) {
    if (!qh->TRInormals) {
      qh_fprintf(qh, qh->ferr, 6224, "Qhull internal error (qh_mergecycle): does not work for tricoplanar facets.  Use option 'Q11'\n");
      qh_errexit(qh, qh_ERRqhull, newfacet, NULL);
    }
    newfacet->tricoplanar= False;
    newfacet->keepcentrum= False;
  }
  if (!qh->VERTEXneighbors)
    qh_vertexneighbors(qh);
  zzinc_(Ztotmerge);
  if (qh->REPORTfreq2 && qh->POSTmerging) {
    if (zzval_(Ztotmerge) > qh->mergereport + qh->REPORTfreq2)
      qh_tracemerging(qh);
  }
#ifndef qh_NOtrace
  if (qh->TRACEmerge == zzval_(Ztotmerge))
    qh->qhmem.IStracing= qh->IStracing= qh->TRACElevel;
  trace2((qh, qh->ferr, 2030, "qh_mergecycle: merge #%d for facets from cycle f%d into coplanar horizon f%d\n",
        zzval_(Ztotmerge), samecycle->id, newfacet->id));
  if (newfacet == qh->tracefacet) {
    tracerestore= qh->IStracing;
    qh->IStracing= 4;
    qh_fprintf(qh, qh->ferr, 8068, "qh_mergecycle: ========= trace merge %d of samecycle %d into trace f%d, furthest is p%d\n",
               zzval_(Ztotmerge), samecycle->id, newfacet->id,  qh->furthest_id);
    traceonce= True;
  }
  if (qh->IStracing >=4) {
    qh_fprintf(qh, qh->ferr, 8069, "  same cycle:");
    FORALLsame_cycle_(samecycle)
      qh_fprintf(qh, qh->ferr, 8070, " f%d", same->id);
    qh_fprintf(qh, qh->ferr, 8071, "\n");
  }
  if (qh->IStracing >=4)
    qh_errprint(qh, "MERGING CYCLE", samecycle, newfacet, NULL, NULL);
#endif
  apex= SETfirstt_(samecycle->vertices, vertexT);
  qh_makeridges(qh, newfacet);
  qh_mergecycle_neighbors(qh, samecycle, newfacet);
  qh_mergecycle_ridges(qh, samecycle, newfacet);
  qh_mergecycle_vneighbors(qh, samecycle, newfacet);
  if (SETfirstt_(newfacet->vertices, vertexT) != apex)
    qh_setaddnth(qh, &newfacet->vertices, 0, apex);
  if (!newfacet->newfacet)
    qh_newvertices(qh, newfacet->vertices);
  qh_mergecycle_facets(qh, samecycle, newfacet);
  qh_tracemerge(qh, samecycle, newfacet);

  if (traceonce) {
    qh_fprintf(qh, qh->ferr, 8072, "qh_mergecycle: end of trace facet\n");
    qh->IStracing= tracerestore;
  }
}






























void qh_mergecycle_all(qhT *qh, facetT *facetlist, boolT *wasmerge) {
  facetT *facet, *same, *prev, *horizon;
  facetT *samecycle= NULL, *nextfacet, *nextsame;
  vertexT *apex, *vertex, **vertexp;
  int cycles=0, total=0, facets, nummerge;

  trace2((qh, qh->ferr, 2031, "qh_mergecycle_all: begin\n"));
  for (facet= facetlist; facet && (nextfacet= facet->next); facet= nextfacet) {
    if (facet->normal)
      continue;
    if (!facet->mergehorizon) {
      qh_fprintf(qh, qh->ferr, 6225, "Qhull internal error (qh_mergecycle_all): f%d without normal\n", facet->id);
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
    horizon= SETfirstt_(facet->neighbors, facetT);
    if (facet->f.samecycle == facet) {
      zinc_(Zonehorizon);

      apex= SETfirstt_(facet->vertices, vertexT);
      FOREACHvertex_(facet->vertices) {
        if (vertex != apex)
          vertex->delridge= True;
      }
      horizon->f.newcycle= NULL;
      qh_mergefacet(qh, facet, horizon, NULL, NULL, qh_MERGEapex);
    }else {
      samecycle= facet;
      facets= 0;
      prev= facet;
      for (same= facet->f.samecycle; same;
           same= (same == facet ? NULL :nextsame)) {
        nextsame= same->f.samecycle;
        if (same->cycledone || same->visible)
          qh_infiniteloop(qh, same);
        same->cycledone= True;
        if (same->normal) {
          prev->f.samecycle= same->f.samecycle;
          same->f.samecycle= NULL;
        }else {
          prev= same;
          facets++;
        }
      }
      while (nextfacet && nextfacet->cycledone)
        nextfacet= nextfacet->next;
      horizon->f.newcycle= NULL;
      qh_mergecycle(qh, samecycle, horizon);
      nummerge= horizon->nummerge + facets;
      if (nummerge > qh_MAXnummerge)
        horizon->nummerge= qh_MAXnummerge;
      else
        horizon->nummerge= (short unsigned int)nummerge;
      zzinc_(Zcyclehorizon);
      total += facets;
      zzadd_(Zcyclefacettot, facets);
      zmax_(Zcyclefacetmax, facets);
    }
    cycles++;
  }
  if (cycles)
    *wasmerge= True;
  trace1((qh, qh->ferr, 1013, "qh_mergecycle_all: merged %d same cycles or facets into coplanar horizons\n", cycles));
}

























void qh_mergecycle_facets(qhT *qh, facetT *samecycle, facetT *newfacet) {
  facetT *same, *next;

  trace4((qh, qh->ferr, 4030, "qh_mergecycle_facets: make newfacet new and samecycle deleted\n"));
  qh_removefacet(qh, newfacet);
  qh_appendfacet(qh, newfacet);
  newfacet->newfacet= True;
  newfacet->simplicial= False;
  newfacet->newmerge= True;

  for (same= samecycle->f.samecycle; same; same= (same == samecycle ?  NULL : next)) {
    next= same->f.samecycle;
    qh_willdelete(qh, same, newfacet);
  }
  if (newfacet->center
      && qh_setsize(qh, newfacet->vertices) <= qh->hull_dim + qh_MAXnewcentrum) {
    qh_memfree(qh, newfacet->center, qh->normal_size);
    newfacet->center= NULL;
  }
  trace3((qh, qh->ferr, 3004, "qh_mergecycle_facets: merged facets from cycle f%d into f%d\n",
             samecycle->id, newfacet->id));
}




































void qh_mergecycle_neighbors(qhT *qh, facetT *samecycle, facetT *newfacet) {
  facetT *same, *neighbor, **neighborp;
  int delneighbors= 0, newneighbors= 0;
  unsigned int samevisitid;
  ridgeT *ridge, **ridgep;

  samevisitid= ++qh->visit_id;
  FORALLsame_cycle_(samecycle) {
    if (same->visitid == samevisitid || same->visible)
      qh_infiniteloop(qh, samecycle);
    same->visitid= samevisitid;
  }
  newfacet->visitid= ++qh->visit_id;
  trace4((qh, qh->ferr, 4031, "qh_mergecycle_neighbors: delete shared neighbors from newfacet\n"));
  FOREACHneighbor_(newfacet) {
    if (neighbor->visitid == samevisitid) {
      SETref_(neighbor)= NULL;
      delneighbors++;
    }else
      neighbor->visitid= qh->visit_id;
  }
  qh_setcompact(qh, newfacet->neighbors);

  trace4((qh, qh->ferr, 4032, "qh_mergecycle_neighbors: update neighbors\n"));
  FORALLsame_cycle_(samecycle) {
    FOREACHneighbor_(same) {
      if (neighbor->visitid == samevisitid)
        continue;
      if (neighbor->simplicial) {
        if (neighbor->visitid != qh->visit_id) {
          qh_setappend(qh, &newfacet->neighbors, neighbor);
          qh_setreplace(qh, neighbor->neighbors, same, newfacet);
          newneighbors++;
          neighbor->visitid= qh->visit_id;
          FOREACHridge_(neighbor->ridges) {
            if (ridge->top == same) {
              ridge->top= newfacet;
              break;
            }else if (ridge->bottom == same) {
              ridge->bottom= newfacet;
              break;
            }
          }
        }else {
          qh_makeridges(qh, neighbor);
          qh_setdel(neighbor->neighbors, same);

        }
      }else {
        qh_setdel(neighbor->neighbors, same);
        if (neighbor->visitid != qh->visit_id) {
          qh_setappend(qh, &neighbor->neighbors, newfacet);
          qh_setappend(qh, &newfacet->neighbors, neighbor);
          neighbor->visitid= qh->visit_id;
          newneighbors++;
        }
      }
    }
  }
  trace2((qh, qh->ferr, 2032, "qh_mergecycle_neighbors: deleted %d neighbors and added %d\n",
             delneighbors, newneighbors));
}



































void qh_mergecycle_ridges(qhT *qh, facetT *samecycle, facetT *newfacet) {
  facetT *same, *neighbor= NULL;
  int numold=0, numnew=0;
  int neighbor_i, neighbor_n;
  unsigned int samevisitid;
  ridgeT *ridge, **ridgep;
  boolT toporient;
  void **freelistp;

  trace4((qh, qh->ferr, 4033, "qh_mergecycle_ridges: delete shared ridges from newfacet\n"));
  samevisitid= qh->visit_id -1;
  FOREACHridge_(newfacet->ridges) {
    neighbor= otherfacet_(ridge, newfacet);
    if (neighbor->visitid == samevisitid)
      SETref_(ridge)= NULL;
  }
  qh_setcompact(qh, newfacet->ridges);

  trace4((qh, qh->ferr, 4034, "qh_mergecycle_ridges: add ridges to newfacet\n"));
  FORALLsame_cycle_(samecycle) {
    FOREACHridge_(same->ridges) {
      if (ridge->top == same) {
        ridge->top= newfacet;
        neighbor= ridge->bottom;
      }else if (ridge->bottom == same) {
        ridge->bottom= newfacet;
        neighbor= ridge->top;
      }else if (ridge->top == newfacet || ridge->bottom == newfacet) {
        qh_setappend(qh, &newfacet->ridges, ridge);
        numold++;
        continue;
      }else {
        qh_fprintf(qh, qh->ferr, 6098, "qhull internal error (qh_mergecycle_ridges): bad ridge r%d\n", ridge->id);
        qh_errexit(qh, qh_ERRqhull, NULL, ridge);
      }
      if (neighbor == newfacet) {
        qh_setfree(qh, &(ridge->vertices));
        qh_memfree_(qh, ridge, (int)sizeof(ridgeT), freelistp);
        numold++;
      }else if (neighbor->visitid == samevisitid) {
        qh_setdel(neighbor->ridges, ridge);
        qh_setfree(qh, &(ridge->vertices));
        qh_memfree_(qh, ridge, (int)sizeof(ridgeT), freelistp);
        numold++;
      }else {
        qh_setappend(qh, &newfacet->ridges, ridge);
        numold++;
      }
    }
    if (same->ridges)
      qh_settruncate(qh, same->ridges, 0);
    if (!same->simplicial)
      continue;
    FOREACHneighbor_i_(qh, same) {
      if (neighbor->visitid != samevisitid && neighbor->simplicial) {
        ridge= qh_newridge(qh);
        ridge->vertices= qh_setnew_delnthsorted(qh, same->vertices, qh->hull_dim,
                                                          neighbor_i, 0);
        toporient= same->toporient ^ (neighbor_i & 0x1);
        if (toporient) {
          ridge->top= newfacet;
          ridge->bottom= neighbor;
        }else {
          ridge->top= neighbor;
          ridge->bottom= newfacet;
        }
        qh_setappend(qh, &(newfacet->ridges), ridge);
        qh_setappend(qh, &(neighbor->ridges), ridge);
        numnew++;
      }
    }
  }

  trace2((qh, qh->ferr, 2033, "qh_mergecycle_ridges: found %d old ridges and %d new ones\n",
             numold, numnew));
}


























void qh_mergecycle_vneighbors(qhT *qh, facetT *samecycle, facetT *newfacet) {
  facetT *neighbor, **neighborp;
  unsigned int mergeid;
  vertexT *vertex, **vertexp, *apex;
  setT *vertices;

  trace4((qh, qh->ferr, 4035, "qh_mergecycle_vneighbors: update vertex neighbors for newfacet\n"));
  mergeid= qh->visit_id - 1;
  newfacet->visitid= mergeid;
  vertices= qh_basevertices(qh, samecycle);
  apex= SETfirstt_(samecycle->vertices, vertexT);
  qh_setappend(qh, &vertices, apex);
  FOREACHvertex_(vertices) {
    vertex->delridge= True;
    FOREACHneighbor_(vertex) {
      if (neighbor->visitid == mergeid)
        SETref_(neighbor)= NULL;
    }
    qh_setcompact(qh, vertex->neighbors);
    qh_setappend(qh, &vertex->neighbors, newfacet);
    if (!SETsecond_(vertex->neighbors)) {
      zinc_(Zcyclevertex);
      trace2((qh, qh->ferr, 2034, "qh_mergecycle_vneighbors: deleted v%d when merging cycle f%d into f%d\n",
        vertex->id, samecycle->id, newfacet->id));
      qh_setdelsorted(newfacet->vertices, vertex);
      vertex->deleted= True;
      qh_setappend(qh, &qh->del_vertices, vertex);
    }
  }
  qh_settempfree(qh, &vertices);
  trace3((qh, qh->ferr, 3005, "qh_mergecycle_vneighbors: merged vertices from cycle f%d into f%d\n",
             samecycle->id, newfacet->id));
}




















































void qh_mergefacet(qhT *qh, facetT *facet1, facetT *facet2, realT *mindist, realT *maxdist, boolT mergeapex) {
  boolT traceonce= False;
  vertexT *vertex, **vertexp;
  int tracerestore=0, nummerge;

  if (facet1->tricoplanar || facet2->tricoplanar) {
    if (!qh->TRInormals) {
      qh_fprintf(qh, qh->ferr, 6226, "Qhull internal error (qh_mergefacet): does not work for tricoplanar facets.  Use option 'Q11'\n");
      qh_errexit2(qh, qh_ERRqhull, facet1, facet2);
    }
    if (facet2->tricoplanar) {
      facet2->tricoplanar= False;
      facet2->keepcentrum= False;
    }
  }
  zzinc_(Ztotmerge);
  if (qh->REPORTfreq2 && qh->POSTmerging) {
    if (zzval_(Ztotmerge) > qh->mergereport + qh->REPORTfreq2)
      qh_tracemerging(qh);
  }
#ifndef qh_NOtrace
  if (qh->build_cnt >= qh->RERUN) {
    if (mindist && (-*mindist > qh->TRACEdist || *maxdist > qh->TRACEdist)) {
      tracerestore= 0;
      qh->IStracing= qh->TRACElevel;
      traceonce= True;
      qh_fprintf(qh, qh->ferr, 8075, "qh_mergefacet: ========= trace wide merge #%d(%2.2g) for f%d into f%d, last point was p%d\n", zzval_(Ztotmerge),
             fmax_(-*mindist, *maxdist), facet1->id, facet2->id, qh->furthest_id);
    }else if (facet1 == qh->tracefacet || facet2 == qh->tracefacet) {
      tracerestore= qh->IStracing;
      qh->IStracing= 4;
      traceonce= True;
      qh_fprintf(qh, qh->ferr, 8076, "qh_mergefacet: ========= trace merge #%d involving f%d, furthest is p%d\n",
                 zzval_(Ztotmerge), qh->tracefacet_id,  qh->furthest_id);
    }
  }
  if (qh->IStracing >= 2) {
    realT mergemin= -2;
    realT mergemax= -2;

    if (mindist) {
      mergemin= *mindist;
      mergemax= *maxdist;
    }
    qh_fprintf(qh, qh->ferr, 8077, "qh_mergefacet: #%d merge f%d into f%d, mindist= %2.2g, maxdist= %2.2g\n",
    zzval_(Ztotmerge), facet1->id, facet2->id, mergemin, mergemax);
  }
#endif
  if (facet1 == facet2 || facet1->visible || facet2->visible) {
    qh_fprintf(qh, qh->ferr, 6099, "qhull internal error (qh_mergefacet): either f%d and f%d are the same or one is a visible facet\n",
             facet1->id, facet2->id);
    qh_errexit2(qh, qh_ERRqhull, facet1, facet2);
  }
  if (qh->num_facets - qh->num_visible <= qh->hull_dim + 1) {
    qh_fprintf(qh, qh->ferr, 6227, "\n\
qhull precision error: Only %d facets remain.  Can not merge another\n\
pair.  The input is too degenerate or the convexity constraints are\n\
too strong.\n", qh->hull_dim+1);
    if (qh->hull_dim >= 5 && !qh->MERGEexact)
      qh_fprintf(qh, qh->ferr, 8079, "Option 'Qx' may avoid this problem.\n");
    qh_errexit(qh, qh_ERRprec, NULL, NULL);
  }
  if (!qh->VERTEXneighbors)
    qh_vertexneighbors(qh);
  qh_makeridges(qh, facet1);
  qh_makeridges(qh, facet2);
  if (qh->IStracing >=4)
    qh_errprint(qh, "MERGING", facet1, facet2, NULL, NULL);
  if (mindist) {
    maximize_(qh->max_outside, *maxdist);
    maximize_(qh->max_vertex, *maxdist);
#if qh_MAXoutside
    maximize_(facet2->maxoutside, *maxdist);
#endif
    minimize_(qh->min_vertex, *mindist);
    if (!facet2->keepcentrum
    && (*maxdist > qh->WIDEfacet || *mindist < -qh->WIDEfacet)) {
      facet2->keepcentrum= True;
      zinc_(Zwidefacet);
    }
  }
  nummerge= facet1->nummerge + facet2->nummerge + 1;
  if (nummerge >= qh_MAXnummerge)
    facet2->nummerge= qh_MAXnummerge;
  else
    facet2->nummerge= (short unsigned int)nummerge;
  facet2->newmerge= True;
  facet2->dupridge= False;
  qh_updatetested(qh, facet1, facet2);
  if (qh->hull_dim > 2 && qh_setsize(qh, facet1->vertices) == qh->hull_dim)
    qh_mergesimplex(qh, facet1, facet2, mergeapex);
  else {
    qh->vertex_visit++;
    FOREACHvertex_(facet2->vertices)
      vertex->visitid= qh->vertex_visit;
    if (qh->hull_dim == 2)
      qh_mergefacet2d(qh, facet1, facet2);
    else {
      qh_mergeneighbors(qh, facet1, facet2);
      qh_mergevertices(qh, facet1->vertices, &facet2->vertices);
    }
    qh_mergeridges(qh, facet1, facet2);
    qh_mergevertex_neighbors(qh, facet1, facet2);
    if (!facet2->newfacet)
      qh_newvertices(qh, facet2->vertices);
  }
  if (!mergeapex)
    qh_degen_redundant_neighbors(qh, facet2, facet1);
  if (facet2->coplanar || !facet2->newfacet) {
    zinc_(Zmergeintohorizon);
  }else if (!facet1->newfacet && facet2->newfacet) {
    zinc_(Zmergehorizon);
  }else {
    zinc_(Zmergenew);
  }
  qh_willdelete(qh, facet1, facet2);
  qh_removefacet(qh, facet2);
  qh_appendfacet(qh, facet2);
  facet2->newfacet= True;
  facet2->tested= False;
  qh_tracemerge(qh, facet1, facet2);
  if (traceonce) {
    qh_fprintf(qh, qh->ferr, 8080, "qh_mergefacet: end of wide tracing\n");
    qh->IStracing= tracerestore;
  }
}

























void qh_mergefacet2d(qhT *qh, facetT *facet1, facetT *facet2) {
  vertexT *vertex1A, *vertex1B, *vertex2A, *vertex2B, *vertexA, *vertexB;
  facetT *neighbor1A, *neighbor1B, *neighbor2A, *neighbor2B, *neighborA, *neighborB;

  vertex1A= SETfirstt_(facet1->vertices, vertexT);
  vertex1B= SETsecondt_(facet1->vertices, vertexT);
  vertex2A= SETfirstt_(facet2->vertices, vertexT);
  vertex2B= SETsecondt_(facet2->vertices, vertexT);
  neighbor1A= SETfirstt_(facet1->neighbors, facetT);
  neighbor1B= SETsecondt_(facet1->neighbors, facetT);
  neighbor2A= SETfirstt_(facet2->neighbors, facetT);
  neighbor2B= SETsecondt_(facet2->neighbors, facetT);
  if (vertex1A == vertex2A) {
    vertexA= vertex1B;
    vertexB= vertex2B;
    neighborA= neighbor2A;
    neighborB= neighbor1A;
  }else if (vertex1A == vertex2B) {
    vertexA= vertex1B;
    vertexB= vertex2A;
    neighborA= neighbor2B;
    neighborB= neighbor1A;
  }else if (vertex1B == vertex2A) {
    vertexA= vertex1A;
    vertexB= vertex2B;
    neighborA= neighbor2A;
    neighborB= neighbor1B;
  }else {
    vertexA= vertex1A;
    vertexB= vertex2A;
    neighborA= neighbor2B;
    neighborB= neighbor1B;
  }

  if (vertexA->id > vertexB->id) {
    SETfirst_(facet2->vertices)= vertexA;
    SETsecond_(facet2->vertices)= vertexB;
    if (vertexB == vertex2A)
      facet2->toporient= !facet2->toporient;
    SETfirst_(facet2->neighbors)= neighborA;
    SETsecond_(facet2->neighbors)= neighborB;
  }else {
    SETfirst_(facet2->vertices)= vertexB;
    SETsecond_(facet2->vertices)= vertexA;
    if (vertexB == vertex2B)
      facet2->toporient= !facet2->toporient;
    SETfirst_(facet2->neighbors)= neighborB;
    SETsecond_(facet2->neighbors)= neighborA;
  }
  qh_makeridges(qh, neighborB);
  qh_setreplace(qh, neighborB->neighbors, facet1, facet2);
  trace4((qh, qh->ferr, 4036, "qh_mergefacet2d: merged v%d and neighbor f%d of f%d into f%d\n",
       vertexA->id, neighborB->id, facet1->id, facet2->id));
}





















void qh_mergeneighbors(qhT *qh, facetT *facet1, facetT *facet2) {
  facetT *neighbor, **neighborp;

  trace4((qh, qh->ferr, 4037, "qh_mergeneighbors: merge neighbors of f%d and f%d\n",
          facet1->id, facet2->id));
  qh->visit_id++;
  FOREACHneighbor_(facet2) {
    neighbor->visitid= qh->visit_id;
  }
  FOREACHneighbor_(facet1) {
    if (neighbor->visitid == qh->visit_id) {
      if (neighbor->simplicial)
        qh_makeridges(qh, neighbor);
      if (SETfirstt_(neighbor->neighbors, facetT) != facet1)
        qh_setdel(neighbor->neighbors, facet1);
      else {
        qh_setdel(neighbor->neighbors, facet2);
        qh_setreplace(qh, neighbor->neighbors, facet1, facet2);
      }
    }else if (neighbor != facet2) {
      qh_setappend(qh, &(facet2->neighbors), neighbor);
      qh_setreplace(qh, neighbor->neighbors, facet1, facet2);
    }
  }
  qh_setdel(facet1->neighbors, facet2);
  qh_setdel(facet2->neighbors, facet1);
}





















void qh_mergeridges(qhT *qh, facetT *facet1, facetT *facet2) {
  ridgeT *ridge, **ridgep;
  vertexT *vertex, **vertexp;

  trace4((qh, qh->ferr, 4038, "qh_mergeridges: merge ridges of f%d and f%d\n",
          facet1->id, facet2->id));
  FOREACHridge_(facet2->ridges) {
    if ((ridge->top == facet1) || (ridge->bottom == facet1)) {
      FOREACHvertex_(ridge->vertices)
        vertex->delridge= True;
      qh_delridge(qh, ridge);
      ridgep--;
    }
  }
  FOREACHridge_(facet1->ridges) {
    if (ridge->top == facet1)
      ridge->top= facet2;
    else
      ridge->bottom= facet2;
    qh_setappend(qh, &(facet2->ridges), ridge);
  }
}













































void qh_mergesimplex(qhT *qh, facetT *facet1, facetT *facet2, boolT mergeapex) {
  vertexT *vertex, **vertexp, *apex;
  ridgeT *ridge, **ridgep;
  boolT issubset= False;
  int vertex_i= -1, vertex_n;
  facetT *neighbor, **neighborp, *otherfacet;

  if (mergeapex) {
    if (!facet2->newfacet)
      qh_newvertices(qh, facet2->vertices);
    apex= SETfirstt_(facet1->vertices, vertexT);
    if (SETfirstt_(facet2->vertices, vertexT) != apex)
      qh_setaddnth(qh, &facet2->vertices, 0, apex);
    else
      issubset= True;
  }else {
    zinc_(Zmergesimplex);
    FOREACHvertex_(facet1->vertices)
      vertex->seen= False;
    FOREACHridge_(facet1->ridges) {
      if (otherfacet_(ridge, facet1) == facet2) {
        FOREACHvertex_(ridge->vertices) {
          vertex->seen= True;
          vertex->delridge= True;
        }
        break;
      }
    }
    FOREACHvertex_(facet1->vertices) {
      if (!vertex->seen)
        break;
    }
    apex= vertex;
    trace4((qh, qh->ferr, 4039, "qh_mergesimplex: merge apex v%d of f%d into facet f%d\n",
          apex->id, facet1->id, facet2->id));
    FOREACHvertex_i_(qh, facet2->vertices) {
      if (vertex->id < apex->id) {
        break;
      }else if (vertex->id == apex->id) {
        issubset= True;
        break;
      }
    }
    if (!issubset)
      qh_setaddnth(qh, &facet2->vertices, vertex_i, apex);
    if (!facet2->newfacet)
      qh_newvertices(qh, facet2->vertices);
    else if (!apex->newlist) {
      qh_removevertex(qh, apex);
      qh_appendvertex(qh, apex);
    }
  }
  trace4((qh, qh->ferr, 4040, "qh_mergesimplex: update vertex neighbors of f%d\n",
          facet1->id));
  FOREACHvertex_(facet1->vertices) {
    if (vertex == apex && !issubset)
      qh_setreplace(qh, vertex->neighbors, facet1, facet2);
    else {
      qh_setdel(vertex->neighbors, facet1);
      if (!SETsecond_(vertex->neighbors))
        qh_mergevertex_del(qh, vertex, facet1, facet2);
    }
  }
  trace4((qh, qh->ferr, 4041, "qh_mergesimplex: merge ridges and neighbors of f%d into f%d\n",
          facet1->id, facet2->id));
  qh->visit_id++;
  FOREACHneighbor_(facet2)
    neighbor->visitid= qh->visit_id;
  FOREACHridge_(facet1->ridges) {
    otherfacet= otherfacet_(ridge, facet1);
    if (otherfacet == facet2) {
      qh_setdel(facet2->ridges, ridge);
      qh_setfree(qh, &(ridge->vertices));
      qh_memfree(qh, ridge, (int)sizeof(ridgeT));
      qh_setdel(facet2->neighbors, facet1);
    }else {
      qh_setappend(qh, &facet2->ridges, ridge);
      if (otherfacet->visitid != qh->visit_id) {
        qh_setappend(qh, &facet2->neighbors, otherfacet);
        qh_setreplace(qh, otherfacet->neighbors, facet1, facet2);
        otherfacet->visitid= qh->visit_id;
      }else {
        if (otherfacet->simplicial)
          qh_makeridges(qh, otherfacet);
        if (SETfirstt_(otherfacet->neighbors, facetT) != facet1)
          qh_setdel(otherfacet->neighbors, facet1);
        else {
          qh_setdel(otherfacet->neighbors, facet2);
          qh_setreplace(qh, otherfacet->neighbors, facet1, facet2);
        }
      }
      if (ridge->top == facet1)
        ridge->top= facet2;
      else
        ridge->bottom= facet2;
    }
  }
  SETfirst_(facet1->ridges)= NULL;
  trace3((qh, qh->ferr, 3006, "qh_mergesimplex: merged simplex f%d apex v%d into facet f%d\n",
          facet1->id, getid_(apex), facet2->id));
}











void qh_mergevertex_del(qhT *qh, vertexT *vertex, facetT *facet1, facetT *facet2) {

  zinc_(Zmergevertex);
  trace2((qh, qh->ferr, 2035, "qh_mergevertex_del: deleted v%d when merging f%d into f%d\n",
          vertex->id, facet1->id, facet2->id));
  qh_setdelsorted(facet2->vertices, vertex);
  vertex->deleted= True;
  qh_setappend(qh, &qh->del_vertices, vertex);
}

















void qh_mergevertex_neighbors(qhT *qh, facetT *facet1, facetT *facet2) {
  vertexT *vertex, **vertexp;

  trace4((qh, qh->ferr, 4042, "qh_mergevertex_neighbors: merge vertex neighbors of f%d and f%d\n",
          facet1->id, facet2->id));
  if (qh->tracevertex) {
    qh_fprintf(qh, qh->ferr, 8081, "qh_mergevertex_neighbors: of f%d and f%d at furthest p%d f0= %p\n",
             facet1->id, facet2->id, qh->furthest_id, qh->tracevertex->neighbors->e[0].p);
    qh_errprint(qh, "TRACE", NULL, NULL, NULL, qh->tracevertex);
  }
  FOREACHvertex_(facet1->vertices) {
    if (vertex->visitid != qh->vertex_visit)
      qh_setreplace(qh, vertex->neighbors, facet1, facet2);
    else {
      qh_setdel(vertex->neighbors, facet1);
      if (!SETsecond_(vertex->neighbors))
        qh_mergevertex_del(qh, vertex, facet1, facet2);
    }
  }
  if (qh->tracevertex)
    qh_errprint(qh, "TRACE", NULL, NULL, NULL, qh->tracevertex);
}
















void qh_mergevertices(qhT *qh, setT *vertices1, setT **vertices2) {
  int newsize= qh_setsize(qh, vertices1)+qh_setsize(qh, *vertices2) - qh->hull_dim + 1;
  setT *mergedvertices;
  vertexT *vertex, **vertexp, **vertex2= SETaddr_(*vertices2, vertexT);

  mergedvertices= qh_settemp(qh, newsize);
  FOREACHvertex_(vertices1) {
    if (!*vertex2 || vertex->id > (*vertex2)->id)
      qh_setappend(qh, &mergedvertices, vertex);
    else {
      while (*vertex2 && (*vertex2)->id > vertex->id)
        qh_setappend(qh, &mergedvertices, *vertex2++);
      if (!*vertex2 || (*vertex2)->id < vertex->id)
        qh_setappend(qh, &mergedvertices, vertex);
      else
        qh_setappend(qh, &mergedvertices, *vertex2++);
    }
  }
  while (*vertex2)
    qh_setappend(qh, &mergedvertices, *vertex2++);
  if (newsize < qh_setsize(qh, mergedvertices)) {
    qh_fprintf(qh, qh->ferr, 6100, "qhull internal error (qh_mergevertices): facets did not share a ridge\n");
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  qh_setfree(qh, vertices2);
  *vertices2= mergedvertices;
  qh_settemppop(qh);
}

























setT *qh_neighbor_intersections(qhT *qh, vertexT *vertex) {
  facetT *neighbor, **neighborp, *neighborA, *neighborB;
  setT *intersect;
  int neighbor_i, neighbor_n;

  FOREACHneighbor_(vertex) {
    if (neighbor->simplicial)
      return NULL;
  }
  neighborA= SETfirstt_(vertex->neighbors, facetT);
  neighborB= SETsecondt_(vertex->neighbors, facetT);
  zinc_(Zintersectnum);
  if (!neighborA)
    return NULL;
  if (!neighborB)
    intersect= qh_setcopy(qh, neighborA->vertices, 0);
  else
    intersect= qh_vertexintersect_new(qh, neighborA->vertices, neighborB->vertices);
  qh_settemppush(qh, intersect);
  qh_setdelsorted(intersect, vertex);
  FOREACHneighbor_i_(qh, vertex) {
    if (neighbor_i >= 2) {
      zinc_(Zintersectnum);
      qh_vertexintersect(qh, &intersect, neighbor->vertices);
      if (!SETfirst_(intersect)) {
        zinc_(Zintersectfail);
        qh_settempfree(qh, &intersect);
        return NULL;
      }
    }
  }
  trace3((qh, qh->ferr, 3007, "qh_neighbor_intersections: %d vertices in neighbor intersection of v%d\n",
          qh_setsize(qh, intersect), vertex->id));
  return intersect;
}











void qh_newvertices(qhT *qh, setT *vertices) {
  vertexT *vertex, **vertexp;

  FOREACHvertex_(vertices) {
    if (!vertex->newlist) {
      qh_removevertex(qh, vertex);
      qh_appendvertex(qh, vertex);
    }
  }
}




























boolT qh_reducevertices(qhT *qh) {
  int numshare=0, numrename= 0;
  boolT degenredun= False;
  facetT *newfacet;
  vertexT *vertex, **vertexp;

  if (qh->hull_dim == 2)
    return False;
  if (qh_merge_degenredundant(qh))
    degenredun= True;
 LABELrestart:
  FORALLnew_facets {
    if (newfacet->newmerge) {
      if (!qh->MERGEvertices)
        newfacet->newmerge= False;
      qh_remove_extravertices(qh, newfacet);
    }
  }
  if (!qh->MERGEvertices)
    return False;
  FORALLnew_facets {
    if (newfacet->newmerge) {
      newfacet->newmerge= False;
      FOREACHvertex_(newfacet->vertices) {
        if (vertex->delridge) {
          if (qh_rename_sharedvertex(qh, vertex, newfacet)) {
            numshare++;
            vertexp--;
          }
        }
      }
    }
  }
  FORALLvertex_(qh->newvertex_list) {
    if (vertex->delridge && !vertex->deleted) {
      vertex->delridge= False;
      if (qh->hull_dim >= 4 && qh_redundant_vertex(qh, vertex)) {
        numrename++;
        if (qh_merge_degenredundant(qh)) {
          degenredun= True;
          goto LABELrestart;
        }
      }
    }
  }
  trace1((qh, qh->ferr, 1014, "qh_reducevertices: renamed %d shared vertices and %d redundant vertices. Degen? %d\n",
          numshare, numrename, degenredun));
  return degenredun;
}























vertexT *qh_redundant_vertex(qhT *qh, vertexT *vertex) {
  vertexT *newvertex= NULL;
  setT *vertices, *ridges;

  trace3((qh, qh->ferr, 3008, "qh_redundant_vertex: check if v%d can be renamed\n", vertex->id));
  if ((vertices= qh_neighbor_intersections(qh, vertex))) {
    ridges= qh_vertexridges(qh, vertex);
    if ((newvertex= qh_find_newvertex(qh, vertex, vertices, ridges)))
      qh_renamevertex(qh, vertex, newvertex, ridges, NULL, NULL);
    qh_settempfree(qh, &ridges);
    qh_settempfree(qh, &vertices);
  }
  return newvertex;
}


















boolT qh_remove_extravertices(qhT *qh, facetT *facet) {
  ridgeT *ridge, **ridgep;
  vertexT *vertex, **vertexp;
  boolT foundrem= False;

  trace4((qh, qh->ferr, 4043, "qh_remove_extravertices: test f%d for extra vertices\n",
          facet->id));
  FOREACHvertex_(facet->vertices)
    vertex->seen= False;
  FOREACHridge_(facet->ridges) {
    FOREACHvertex_(ridge->vertices)
      vertex->seen= True;
  }
  FOREACHvertex_(facet->vertices) {
    if (!vertex->seen) {
      foundrem= True;
      zinc_(Zremvertex);
      qh_setdelsorted(facet->vertices, vertex);
      qh_setdel(vertex->neighbors, facet);
      if (!qh_setsize(qh, vertex->neighbors)) {
        vertex->deleted= True;
        qh_setappend(qh, &qh->del_vertices, vertex);
        zinc_(Zremvertexdel);
        trace2((qh, qh->ferr, 2036, "qh_remove_extravertices: v%d deleted because it's lost all ridges\n", vertex->id));
      }else
        trace3((qh, qh->ferr, 3009, "qh_remove_extravertices: v%d removed from f%d because it's lost all ridges\n", vertex->id, facet->id));
      vertexp--;
    }
  }
  return foundrem;
}




























vertexT *qh_rename_sharedvertex(qhT *qh, vertexT *vertex, facetT *facet) {
  facetT *neighbor, **neighborp, *neighborA= NULL;
  setT *vertices, *ridges;
  vertexT *newvertex;

  if (qh_setsize(qh, vertex->neighbors) == 2) {
    neighborA= SETfirstt_(vertex->neighbors, facetT);
    if (neighborA == facet)
      neighborA= SETsecondt_(vertex->neighbors, facetT);
  }else if (qh->hull_dim == 3)
    return NULL;
  else {
    qh->visit_id++;
    FOREACHneighbor_(facet)
      neighbor->visitid= qh->visit_id;
    FOREACHneighbor_(vertex) {
      if (neighbor->visitid == qh->visit_id) {
        if (neighborA)
          return NULL;
        neighborA= neighbor;
      }
    }
    if (!neighborA) {
      qh_fprintf(qh, qh->ferr, 6101, "qhull internal error (qh_rename_sharedvertex): v%d's neighbors not in f%d\n",
        vertex->id, facet->id);
      qh_errprint(qh, "ERRONEOUS", facet, NULL, NULL, vertex);
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }
  }

  ridges= qh_settemp(qh, qh->TEMPsize);
  neighborA->visitid= ++qh->visit_id;
  qh_vertexridges_facet(qh, vertex, facet, &ridges);
  trace2((qh, qh->ferr, 2037, "qh_rename_sharedvertex: p%d(v%d) is shared by f%d(%d ridges) and f%d\n",
    qh_pointid(qh, vertex->point), vertex->id, facet->id, qh_setsize(qh, ridges), neighborA->id));
  zinc_(Zintersectnum);
  vertices= qh_vertexintersect_new(qh, facet->vertices, neighborA->vertices);
  qh_setdel(vertices, vertex);
  qh_settemppush(qh, vertices);
  if ((newvertex= qh_find_newvertex(qh, vertex, vertices, ridges)))
    qh_renamevertex(qh, vertex, newvertex, ridges, facet, neighborA);
  qh_settempfree(qh, &vertices);
  qh_settempfree(qh, &ridges);
  return newvertex;
}


















void qh_renameridgevertex(qhT *qh, ridgeT *ridge, vertexT *oldvertex, vertexT *newvertex) {
  int nth= 0, oldnth;
  facetT *temp;
  vertexT *vertex, **vertexp;

  oldnth= qh_setindex(ridge->vertices, oldvertex);
  qh_setdelnthsorted(qh, ridge->vertices, oldnth);
  FOREACHvertex_(ridge->vertices) {
    if (vertex == newvertex) {
      zinc_(Zdelridge);
      if (ridge->nonconvex)
        qh_copynonconvex(qh, ridge);
      trace2((qh, qh->ferr, 2038, "qh_renameridgevertex: ridge r%d deleted.  It contained both v%d and v%d\n",
        ridge->id, oldvertex->id, newvertex->id));
      qh_delridge(qh, ridge);
      return;
    }
    if (vertex->id < newvertex->id)
      break;
    nth++;
  }
  qh_setaddnth(qh, &ridge->vertices, nth, newvertex);
  if (abs(oldnth - nth)%2) {
    trace3((qh, qh->ferr, 3010, "qh_renameridgevertex: swapped the top and bottom of ridge r%d\n",
            ridge->id));
    temp= ridge->top;
    ridge->top= ridge->bottom;
    ridge->bottom= temp;
  }
}
































void qh_renamevertex(qhT *qh, vertexT *oldvertex, vertexT *newvertex, setT *ridges, facetT *oldfacet, facetT *neighborA) {
  facetT *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;
  boolT istrace= False;

  if (qh->IStracing >= 2 || oldvertex->id == qh->tracevertex_id ||
        newvertex->id == qh->tracevertex_id)
    istrace= True;
  FOREACHridge_(ridges)
    qh_renameridgevertex(qh, ridge, oldvertex, newvertex);
  if (!oldfacet) {
    zinc_(Zrenameall);
    if (istrace)
      qh_fprintf(qh, qh->ferr, 8082, "qh_renamevertex: renamed v%d to v%d in several facets\n",
               oldvertex->id, newvertex->id);
    FOREACHneighbor_(oldvertex) {
      qh_maydropneighbor(qh, neighbor);
      qh_setdelsorted(neighbor->vertices, oldvertex);
      if (qh_remove_extravertices(qh, neighbor))
        neighborp--;
    }
    if (!oldvertex->deleted) {
      oldvertex->deleted= True;
      qh_setappend(qh, &qh->del_vertices, oldvertex);
    }
  }else if (qh_setsize(qh, oldvertex->neighbors) == 2) {
    zinc_(Zrenameshare);
    if (istrace)
      qh_fprintf(qh, qh->ferr, 8083, "qh_renamevertex: renamed v%d to v%d in oldfacet f%d\n",
               oldvertex->id, newvertex->id, oldfacet->id);
    FOREACHneighbor_(oldvertex)
      qh_setdelsorted(neighbor->vertices, oldvertex);
    oldvertex->deleted= True;
    qh_setappend(qh, &qh->del_vertices, oldvertex);
  }else {
    zinc_(Zrenamepinch);
    if (istrace || qh->IStracing)
      qh_fprintf(qh, qh->ferr, 8084, "qh_renamevertex: renamed pinched v%d to v%d between f%d and f%d\n",
               oldvertex->id, newvertex->id, oldfacet->id, neighborA->id);
    qh_setdelsorted(oldfacet->vertices, oldvertex);
    qh_setdel(oldvertex->neighbors, oldfacet);
    qh_remove_extravertices(qh, neighborA);
  }
}




































boolT qh_test_appendmerge(qhT *qh, facetT *facet, facetT *neighbor) {
  realT dist, dist2= -REALmax, angle= -REALmax;
  boolT isconcave= False, iscoplanar= False, okangle= False;

  if (qh->SKIPconvex && !qh->POSTmerging)
    return False;
  if ((!qh->MERGEexact || qh->POSTmerging) && qh->cos_max < REALmax/2) {
    angle= qh_getangle(qh, facet->normal, neighbor->normal);
    zinc_(Zangletests);
    if (angle > qh->cos_max) {
      zinc_(Zcoplanarangle);
      qh_appendmergeset(qh, facet, neighbor, MRGanglecoplanar, &angle);
      trace2((qh, qh->ferr, 2039, "qh_test_appendmerge: coplanar angle %4.4g between f%d and f%d\n",
         angle, facet->id, neighbor->id));
      return True;
    }else
      okangle= True;
  }
  if (!facet->center)
    facet->center= qh_getcentrum(qh, facet);
  zzinc_(Zcentrumtests);
  qh_distplane(qh, facet->center, neighbor, &dist);
  if (dist > qh->centrum_radius)
    isconcave= True;
  else {
    if (dist > -qh->centrum_radius)
      iscoplanar= True;
    if (!neighbor->center)
      neighbor->center= qh_getcentrum(qh, neighbor);
    zzinc_(Zcentrumtests);
    qh_distplane(qh, neighbor->center, facet, &dist2);
    if (dist2 > qh->centrum_radius)
      isconcave= True;
    else if (!iscoplanar && dist2 > -qh->centrum_radius)
      iscoplanar= True;
  }
  if (!isconcave && (!iscoplanar || (qh->MERGEexact && !qh->POSTmerging)))
    return False;
  if (!okangle && qh->ANGLEmerge) {
    angle= qh_getangle(qh, facet->normal, neighbor->normal);
    zinc_(Zangletests);
  }
  if (isconcave) {
    zinc_(Zconcaveridge);
    if (qh->ANGLEmerge)
      angle += qh_ANGLEconcave + 0.5;
    qh_appendmergeset(qh, facet, neighbor, MRGconcave, &angle);
    trace0((qh, qh->ferr, 18, "qh_test_appendmerge: concave f%d to f%d dist %4.4g and reverse dist %4.4g angle %4.4g during p%d\n",
           facet->id, neighbor->id, dist, dist2, angle, qh->furthest_id));
  }else   {
    zinc_(Zcoplanarcentrum);
    qh_appendmergeset(qh, facet, neighbor, MRGcoplanar, &angle);
    trace2((qh, qh->ferr, 2040, "qh_test_appendmerge: coplanar f%d to f%d dist %4.4g, reverse dist %4.4g angle %4.4g\n",
              facet->id, neighbor->id, dist, dist2, angle));
  }
  return True;
}


























boolT qh_test_vneighbors(qhT *qh  ) {
  facetT *newfacet, *neighbor, **neighborp;
  vertexT *vertex, **vertexp;
  int nummerges= 0;

  trace1((qh, qh->ferr, 1015, "qh_test_vneighbors: testing vertex neighbors for convexity\n"));
  if (!qh->VERTEXneighbors)
    qh_vertexneighbors(qh);
  FORALLnew_facets
    newfacet->seen= False;
  FORALLnew_facets {
    newfacet->seen= True;
    newfacet->visitid= qh->visit_id++;
    FOREACHneighbor_(newfacet)
      newfacet->visitid= qh->visit_id;
    FOREACHvertex_(newfacet->vertices) {
      FOREACHneighbor_(vertex) {
        if (neighbor->seen || neighbor->visitid == qh->visit_id)
          continue;
        if (qh_test_appendmerge(qh, newfacet, neighbor))
          nummerges++;
      }
    }
  }
  zadd_(Ztestvneighbor, nummerges);
  trace1((qh, qh->ferr, 1016, "qh_test_vneighbors: found %d non-convex, vertex neighbors\n",
           nummerges));
  return (nummerges > 0);
}







void qh_tracemerge(qhT *qh, facetT *facet1, facetT *facet2) {
  boolT waserror= False;

#ifndef qh_NOtrace
  if (qh->IStracing >= 4)
    qh_errprint(qh, "MERGED", facet2, NULL, NULL, NULL);
  if (facet2 == qh->tracefacet || (qh->tracevertex && qh->tracevertex->newlist)) {
    qh_fprintf(qh, qh->ferr, 8085, "qh_tracemerge: trace facet and vertex after merge of f%d and f%d, furthest p%d\n", facet1->id, facet2->id, qh->furthest_id);
    if (facet2 != qh->tracefacet)
      qh_errprint(qh, "TRACE", qh->tracefacet,
        (qh->tracevertex && qh->tracevertex->neighbors) ?
           SETfirstt_(qh->tracevertex->neighbors, facetT) : NULL,
        NULL, qh->tracevertex);
  }
  if (qh->tracevertex) {
    if (qh->tracevertex->deleted)
      qh_fprintf(qh, qh->ferr, 8086, "qh_tracemerge: trace vertex deleted at furthest p%d\n",
            qh->furthest_id);
    else
      qh_checkvertex(qh, qh->tracevertex);
  }
  if (qh->tracefacet) {
    qh_checkfacet(qh, qh->tracefacet, True, &waserror);
    if (waserror)
      qh_errexit(qh, qh_ERRqhull, qh->tracefacet, NULL);
  }
#endif
  if (qh->CHECKfrequently || qh->IStracing >= 4) {
    qh_checkfacet(qh, facet2, True, &waserror);
    if (waserror)
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
}
















void qh_tracemerging(qhT *qh) {
  realT cpu;
  int total;
  time_t timedata;
  struct tm *tp;

  qh->mergereport= zzval_(Ztotmerge);
  time(&timedata);
  tp= localtime(&timedata);
  cpu= qh_CPUclock;
  cpu /= qh_SECticks;
  total= zzval_(Ztotmerge) - zzval_(Zcyclehorizon) + zzval_(Zcyclefacettot);
  qh_fprintf(qh, qh->ferr, 8087, "\n\
At %d:%d:%d & %2.5g CPU secs, qhull has merged %d facets.  The hull\n\
  contains %d facets and %d vertices.\n",
      tp->tm_hour, tp->tm_min, tp->tm_sec, cpu,
      total, qh->num_facets - qh->num_visible,
      qh->num_vertices-qh_setsize(qh, qh->del_vertices));
}























void qh_updatetested(qhT *qh, facetT *facet1, facetT *facet2) {
  ridgeT *ridge, **ridgep;
  int size;

  facet2->tested= False;
  FOREACHridge_(facet1->ridges)
    ridge->tested= False;
  if (!facet2->center)
    return;
  size= qh_setsize(qh, facet2->vertices);
  if (!facet2->keepcentrum) {
    if (size > qh->hull_dim + qh_MAXnewcentrum) {
      facet2->keepcentrum= True;
      zinc_(Zwidevertices);
    }
  }else if (size <= qh->hull_dim + qh_MAXnewcentrum) {

    if (size == qh->hull_dim || qh->POSTmerging)
      facet2->keepcentrum= False;
  }
  if (!facet2->keepcentrum) {
    qh_memfree(qh, facet2->center, qh->normal_size);
    facet2->center= NULL;
    FOREACHridge_(facet2->ridges)
      ridge->tested= False;
  }
}
















setT *qh_vertexridges(qhT *qh, vertexT *vertex) {
  facetT *neighbor, **neighborp;
  setT *ridges= qh_settemp(qh, qh->TEMPsize);
  int size;

  qh->visit_id++;
  FOREACHneighbor_(vertex)
    neighbor->visitid= qh->visit_id;
  FOREACHneighbor_(vertex) {
    if (*neighborp)
      qh_vertexridges_facet(qh, vertex, neighbor, &ridges);
  }
  if (qh->PRINTstatistics || qh->IStracing) {
    size= qh_setsize(qh, ridges);
    zinc_(Zvertexridge);
    zadd_(Zvertexridgetot, size);
    zmax_(Zvertexridgemax, size);
    trace3((qh, qh->ferr, 3011, "qh_vertexridges: found %d ridges for v%d\n",
             size, vertex->id));
  }
  return ridges;
}



















void qh_vertexridges_facet(qhT *qh, vertexT *vertex, facetT *facet, setT **ridges) {
  ridgeT *ridge, **ridgep;
  facetT *neighbor;

  FOREACHridge_(facet->ridges) {
    neighbor= otherfacet_(ridge, facet);
    if (neighbor->visitid == qh->visit_id
    && qh_setin(ridge->vertices, vertex))
      qh_setappend(qh, ridges, ridge);
  }
  facet->visitid= qh->visit_id-1;
}











void qh_willdelete(qhT *qh, facetT *facet, facetT *replace) {

  qh_removefacet(qh, facet);
  qh_prependfacet(qh, facet, &qh->visible_list);
  qh->num_visible++;
  facet->visible= True;
  facet->f.replace= replace;
}

#else
void qh_premerge(qhT *qh, vertexT *apex, realT maxcentrum, realT maxangle) {
}
void qh_postmerge(qhT *qh, const char *reason, realT maxcentrum, realT maxangle,
                      boolT vneighbors) {
}
boolT qh_checkzero(qhT *qh, boolT testall) {
   }
#endif
