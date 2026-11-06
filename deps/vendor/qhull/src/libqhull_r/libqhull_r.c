/*<html><pre>  -<a                             href="qh-qhull_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   libqhull_r.c
   Quickhull algorithm for convex hulls

   qhull() and top-level routines

   see qh-qhull_r.htm, libqhull.h, unix_r.c

   see qhull_ra.h for internal functions

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/libqhull_r.c#2 $$Change: 2047 $
   $DateTime: 2016/01/04 22:03:18 $$Author: bbarber $
*/

#include "qhull_ra.h"









































void qh_qhull(qhT *qh) {
  int numoutside;

  qh->hulltime= qh_CPUclock;
  if (qh->RERUN || qh->JOGGLEmax < REALmax/2)
    qh_build_withrestart(qh);
  else {
    qh_initbuild(qh);
    qh_buildhull(qh);
  }
  if (!qh->STOPpoint && !qh->STOPcone) {
    if (qh->ZEROall_ok && !qh->TESTvneighbors && qh->MERGEexact)
      qh_checkzero(qh, qh_ALL);
    if (qh->ZEROall_ok && !qh->TESTvneighbors && !qh->WAScoplanar) {
      trace2((qh, qh->ferr, 2055, "qh_qhull: all facets are clearly convex and no coplanar points.  Post-merging and check of maxout not needed.\n"));
      qh->DOcheckmax= False;
    }else {
      if (qh->MERGEexact || (qh->hull_dim > qh_DIMreduceBuild && qh->PREmerge))
        qh_postmerge(qh, "First post-merge", qh->premerge_centrum, qh->premerge_cos,
             (qh->POSTmerge ? False : qh->TESTvneighbors));
      else if (!qh->POSTmerge && qh->TESTvneighbors)
        qh_postmerge(qh, "For testing vertex neighbors", qh->premerge_centrum,
             qh->premerge_cos, True);
      if (qh->POSTmerge)
        qh_postmerge(qh, "For post-merging", qh->postmerge_centrum,
             qh->postmerge_cos, qh->TESTvneighbors);
      if (qh->visible_list == qh->facet_list) {
        qh->findbestnew= True;
        qh_partitionvisible(qh  , !qh_ALL, &numoutside);
        qh->findbestnew= False;
        qh_deletevisible(qh  );
        qh_resetlists(qh, False, qh_RESETvisible  );
      }
    }
    if (qh->DOcheckmax){
      if (qh->REPORTfreq) {
        qh_buildtracing(qh, NULL, NULL);
        qh_fprintf(qh, qh->ferr, 8115, "\nTesting all coplanar points.\n");
      }
      qh_check_maxout(qh);
    }
    if (qh->KEEPnearinside && !qh->maxoutdone)
      qh_nearcoplanar(qh);
  }
  if (qh_setsize(qh, qh->qhmem.tempstack) != 0) {
    qh_fprintf(qh, qh->ferr, 6164, "qhull internal error (qh_qhull): temporary sets not empty(%d)\n",
             qh_setsize(qh, qh->qhmem.tempstack));
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  qh->hulltime= qh_CPUclock - qh->hulltime;
  qh->QHULLfinished= True;
  trace1((qh, qh->ferr, 1036, "Qhull: algorithm completed\n"));
}























































boolT qh_addpoint(qhT *qh, pointT *furthest, facetT *facet, boolT checkdist) {
  int goodvisible, goodhorizon;
  vertexT *vertex;
  facetT *newfacet;
  realT dist, newbalance, pbalance;
  boolT isoutside= False;
  int numpart, numpoints, numnew, firstnew;

  qh->maxoutdone= False;
  if (qh_pointid(qh, furthest) == qh_IDunknown)
    qh_setappend(qh, &qh->other_points, furthest);
  if (!facet) {
    qh_fprintf(qh, qh->ferr, 6213, "qhull internal error (qh_addpoint): NULL facet.  Need to call qh_findbestfacet first\n");
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  if (checkdist) {
    facet= qh_findbest(qh, furthest, facet, !qh_ALL, !qh_ISnewfacets, !qh_NOupper,
                        &dist, &isoutside, &numpart);
    zzadd_(Zpartition, numpart);
    if (!isoutside) {
      zinc_(Znotmax);
      facet->notfurthest= True;
      qh_partitioncoplanar(qh, furthest, facet, &dist);
      return True;
    }
  }
  qh_buildtracing(qh, furthest, facet);
  if (qh->STOPpoint < 0 && qh->furthest_id == -qh->STOPpoint-1) {
    facet->notfurthest= True;
    return False;
  }
  qh_findhorizon(qh, furthest, facet, &goodvisible, &goodhorizon);
  if (qh->ONLYgood && !(goodvisible+goodhorizon) && !qh->GOODclosest) {
    zinc_(Znotgood);
    facet->notfurthest= True;


    qh_resetlists(qh, False, qh_RESETvisible  );
    return True;
  }
  zzinc_(Zprocessed);
  firstnew= qh->facet_id;
  vertex= qh_makenewfacets(qh, furthest  );
  qh_makenewplanes(qh  );
  numnew= qh->facet_id - firstnew;
  newbalance= numnew - (realT) (qh->num_facets-qh->num_visible)
                         * qh->hull_dim/qh->num_vertices;
  wadd_(Wnewbalance, newbalance);
  wadd_(Wnewbalance2, newbalance * newbalance);
  if (qh->ONLYgood
  && !qh_findgood(qh, qh->newfacet_list, goodhorizon) && !qh->GOODclosest) {
    FORALLnew_facets
      qh_delfacet(qh, newfacet);
    qh_delvertex(qh, vertex);
    qh_resetlists(qh, True, qh_RESETvisible  );
    zinc_(Znotgoodnew);
    facet->notfurthest= True;
    return True;
  }
  if (qh->ONLYgood)
    qh_attachnewfacets(qh  );
  qh_matchnewfacets(qh);
  qh_updatevertices(qh);
  if (qh->STOPcone && qh->furthest_id == qh->STOPcone-1) {
    facet->notfurthest= True;
    return False;
  }
  qh->findbestnew= False;
  if (qh->PREmerge || qh->MERGEexact) {
    qh_premerge(qh, vertex, qh->premerge_centrum, qh->premerge_cos);
    if (qh_USEfindbestnew)
      qh->findbestnew= True;
    else {
      FORALLnew_facets {
        if (!newfacet->simplicial) {
          qh->findbestnew= True;
          break;
        }
      }
    }
  }else if (qh->BESToutside)
    qh->findbestnew= True;
  qh_partitionvisible(qh  , !qh_ALL, &numpoints);
  qh->findbestnew= False;
  qh->findbest_notsharp= False;
  zinc_(Zpbalance);
  pbalance= numpoints - (realT) qh->hull_dim
                * (qh->num_points - qh->num_vertices)/qh->num_vertices;
  wadd_(Wpbalance, pbalance);
  wadd_(Wpbalance2, pbalance * pbalance);
  qh_deletevisible(qh  );
  zmax_(Zmaxvertex, qh->num_vertices);
  qh->NEWfacets= False;
  if (qh->IStracing >= 4) {
    if (qh->num_facets < 2000)
      qh_printlists(qh);
    qh_printfacetlist(qh, qh->newfacet_list, NULL, True);
    qh_checkpolygon(qh, qh->facet_list);
  }else if (qh->CHECKfrequently) {
    if (qh->num_facets < 50)
      qh_checkpolygon(qh, qh->facet_list);
    else
      qh_checkpolygon(qh, qh->newfacet_list);
  }
  if (qh->STOPpoint > 0 && qh->furthest_id == qh->STOPpoint-1)
    return False;
  qh_resetlists(qh, True, qh_RESETvisible  );

  trace2((qh, qh->ferr, 2056, "qh_addpoint: added p%d new facets %d new balance %2.2g point balance %2.2g\n",
    qh_pointid(qh, furthest), numnew, newbalance, pbalance));
  return True;
}










void qh_build_withrestart(qhT *qh) {
  int restart;

  qh->ALLOWrestart= True;
  while (True) {
    restart= setjmp(qh->restartexit);
    if (restart) {
      zzinc_(Zretry);
      wmax_(Wretrymax, qh->JOGGLEmax);

      qh->STOPcone= qh_IDunknown;
    }
    if (!qh->RERUN && qh->JOGGLEmax < REALmax/2) {
      if (qh->build_cnt > qh_JOGGLEmaxretry) {
        qh_fprintf(qh, qh->ferr, 6229, "qhull precision error: %d attempts to construct a convex hull\n\
        with joggled input.  Increase joggle above 'QJ%2.2g'\n\
        or modify qh_JOGGLE... parameters in user.h\n",
           qh->build_cnt, qh->JOGGLEmax);
        qh_errexit(qh, qh_ERRqhull, NULL, NULL);
      }
      if (qh->build_cnt && !restart)
        break;
    }else if (qh->build_cnt && qh->build_cnt >= qh->RERUN)
      break;
    qh->STOPcone= 0;
    qh_freebuild(qh, True);
    qh->build_cnt++;
    if (!qh->qhull_optionsiz)
      qh->qhull_optionsiz= (int)strlen(qh->qhull_options);
    else {
      qh->qhull_options [qh->qhull_optionsiz]= '\0';
      qh->qhull_optionlen= qh_OPTIONline;
    }
    qh_option(qh, "_run", &qh->build_cnt, NULL);
    if (qh->build_cnt == qh->RERUN) {
      qh->IStracing= qh->TRACElastrun;
      if (qh->TRACEpoint != qh_IDunknown || qh->TRACEdist < REALmax/2 || qh->TRACEmerge) {
        qh->TRACElevel= (qh->IStracing? qh->IStracing : 3);
        qh->IStracing= 0;
      }
      qh->qhmem.IStracing= qh->IStracing;
    }
    if (qh->JOGGLEmax < REALmax/2)
      qh_joggleinput(qh);
    qh_initbuild(qh);
    qh_buildhull(qh);
    if (qh->JOGGLEmax < REALmax/2 && !qh->MERGING)
      qh_checkconvex(qh, qh->facet_list, qh_ALGORITHMfault);
  }
  qh->ALLOWrestart= False;
}























void qh_buildhull(qhT *qh) {
  facetT *facet;
  pointT *furthest;
  vertexT *vertex;
  int id;

  trace1((qh, qh->ferr, 1037, "qh_buildhull: start build hull\n"));
  FORALLfacets {
    if (facet->visible || facet->newfacet) {
      qh_fprintf(qh, qh->ferr, 6165, "qhull internal error (qh_buildhull): visible or new facet f%d in facet list\n",
                   facet->id);
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
  }
  FORALLvertices {
    if (vertex->newlist) {
      qh_fprintf(qh, qh->ferr, 6166, "qhull internal error (qh_buildhull): new vertex f%d in vertex list\n",
                   vertex->id);
      qh_errprint(qh, "ERRONEOUS", NULL, NULL, NULL, vertex);
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }
    id= qh_pointid(qh, vertex->point);
    if ((qh->STOPpoint>0 && id == qh->STOPpoint-1) ||
        (qh->STOPpoint<0 && id == -qh->STOPpoint-1) ||
        (qh->STOPcone>0 && id == qh->STOPcone-1)) {
      trace1((qh, qh->ferr, 1038,"qh_buildhull: stop point or cone P%d in initial hull\n", id));
      return;
    }
  }
  qh->facet_next= qh->facet_list;
  while ((furthest= qh_nextfurthest(qh, &facet))) {
    qh->num_outside--;
    if (!qh_addpoint(qh, furthest, facet, qh->ONLYmax))
      break;
  }
  if (qh->NARROWhull)
    qh_outcoplanar(qh   );
  if (qh->num_outside && !furthest) {
    qh_fprintf(qh, qh->ferr, 6167, "qhull internal error (qh_buildhull): %d outside points were never processed.\n", qh->num_outside);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  trace1((qh, qh->ferr, 1039, "qh_buildhull: completed the hull construction\n"));
}




























void qh_buildtracing(qhT *qh, pointT *furthest, facetT *facet) {
  realT dist= 0;
  float cpu;
  int total, furthestid;
  time_t timedata;
  struct tm *tp;
  vertexT *vertex;

  qh->old_randomdist= qh->RANDOMdist;
  qh->RANDOMdist= False;
  if (!furthest) {
    time(&timedata);
    tp= localtime(&timedata);
    cpu= (float)qh_CPUclock - (float)qh->hulltime;
    cpu /= (float)qh_SECticks;
    total= zzval_(Ztotmerge) - zzval_(Zcyclehorizon) + zzval_(Zcyclefacettot);
    qh_fprintf(qh, qh->ferr, 8118, "\n\
At %02d:%02d:%02d & %2.5g CPU secs, qhull has created %d facets and merged %d.\n\
 The current hull contains %d facets and %d vertices.  Last point was p%d\n",
      tp->tm_hour, tp->tm_min, tp->tm_sec, cpu, qh->facet_id -1,
      total, qh->num_facets, qh->num_vertices, qh->furthest_id);
    return;
  }
  furthestid= qh_pointid(qh, furthest);
  if (qh->TRACEpoint == furthestid) {
    qh->IStracing= qh->TRACElevel;
    qh->qhmem.IStracing= qh->TRACElevel;
  }else if (qh->TRACEpoint != qh_IDunknown && qh->TRACEdist < REALmax/2) {
    qh->IStracing= 0;
    qh->qhmem.IStracing= 0;
  }
  if (qh->REPORTfreq && (qh->facet_id-1 > qh->lastreport+qh->REPORTfreq)) {
    qh->lastreport= qh->facet_id-1;
    time(&timedata);
    tp= localtime(&timedata);
    cpu= (float)qh_CPUclock - (float)qh->hulltime;
    cpu /= (float)qh_SECticks;
    total= zzval_(Ztotmerge) - zzval_(Zcyclehorizon) + zzval_(Zcyclefacettot);
    zinc_(Zdistio);
    qh_distplane(qh, furthest, facet, &dist);
    qh_fprintf(qh, qh->ferr, 8119, "\n\
At %02d:%02d:%02d & %2.5g CPU secs, qhull has created %d facets and merged %d.\n\
 The current hull contains %d facets and %d vertices.  There are %d\n\
 outside points.  Next is point p%d(v%d), %2.2g above f%d.\n",
      tp->tm_hour, tp->tm_min, tp->tm_sec, cpu, qh->facet_id -1,
      total, qh->num_facets, qh->num_vertices, qh->num_outside+1,
      furthestid, qh->vertex_id, dist, getid_(facet));
  }else if (qh->IStracing >=1) {
    cpu= (float)qh_CPUclock - (float)qh->hulltime;
    cpu /= (float)qh_SECticks;
    qh_distplane(qh, furthest, facet, &dist);
    qh_fprintf(qh, qh->ferr, 8120, "qh_addpoint: add p%d(v%d) to hull of %d facets(%2.2g above f%d) and %d outside at %4.4g CPU secs.  Previous was p%d.\n",
      furthestid, qh->vertex_id, qh->num_facets, dist,
      getid_(facet), qh->num_outside+1, cpu, qh->furthest_id);
  }
  zmax_(Zvisit2max, (int)qh->visit_id/2);
  if (qh->visit_id > (unsigned) INT_MAX) {
    zinc_(Zvisit);
    qh->visit_id= 0;
    FORALLfacets
      facet->visitid= 0;
  }
  zmax_(Zvvisit2max, (int)qh->vertex_visit/2);
  if (qh->vertex_visit > (unsigned) INT_MAX) {
    zinc_(Zvvisit);
    qh->vertex_visit= 0;
    FORALLvertices
      vertex->visitid= 0;
  }
  qh->furthest_id= furthestid;
  qh->RANDOMdist= qh->old_randomdist;
}














void qh_errexit2(qhT *qh, int exitcode, facetT *facet, facetT *otherfacet) {

  qh_errprint(qh, "ERRONEOUS", facet, otherfacet, NULL, NULL);
  qh_errexit(qh, exitcode, NULL, NULL);
}






























void qh_findhorizon(qhT *qh, pointT *point, facetT *facet, int *goodvisible, int *goodhorizon) {
  facetT *neighbor, **neighborp, *visible;
  int numhorizon= 0, coplanar= 0;
  realT dist;

  trace1((qh, qh->ferr, 1040,"qh_findhorizon: find horizon for point p%d facet f%d\n",qh_pointid(qh, point),facet->id));
  *goodvisible= *goodhorizon= 0;
  zinc_(Ztotvisible);
  qh_removefacet(qh, facet);
  qh_appendfacet(qh, facet);
  qh->num_visible= 1;
  if (facet->good)
    (*goodvisible)++;
  qh->visible_list= facet;
  facet->visible= True;
  facet->f.replace= NULL;
  if (qh->IStracing >=4)
    qh_errprint(qh, "visible", facet, NULL, NULL, NULL);
  qh->visit_id++;
  FORALLvisible_facets {
    if (visible->tricoplanar && !qh->TRInormals) {
      qh_fprintf(qh, qh->ferr, 6230, "Qhull internal error (qh_findhorizon): does not work for tricoplanar facets.  Use option 'Q11'\n");
      qh_errexit(qh, qh_ERRqhull, visible, NULL);
    }
    visible->visitid= qh->visit_id;
    FOREACHneighbor_(visible) {
      if (neighbor->visitid == qh->visit_id)
        continue;
      neighbor->visitid= qh->visit_id;
      zzinc_(Znumvisibility);
      qh_distplane(qh, point, neighbor, &dist);
      if (dist > qh->MINvisible) {
        zinc_(Ztotvisible);
        qh_removefacet(qh, neighbor);
        qh_appendfacet(qh, neighbor);
        neighbor->visible= True;
        neighbor->f.replace= NULL;
        qh->num_visible++;
        if (neighbor->good)
          (*goodvisible)++;
        if (qh->IStracing >=4)
          qh_errprint(qh, "visible", neighbor, NULL, NULL, NULL);
      }else {
        if (dist > - qh->MAXcoplanar) {
          neighbor->coplanar= True;
          zzinc_(Zcoplanarhorizon);
          qh_precision(qh, "coplanar horizon");
          coplanar++;
          if (qh->MERGING) {
            if (dist > 0) {
              maximize_(qh->max_outside, dist);
              maximize_(qh->max_vertex, dist);
#if qh_MAXoutside
              maximize_(neighbor->maxoutside, dist);
#endif
            }else
              minimize_(qh->min_vertex, dist);
          }
          trace2((qh, qh->ferr, 2057, "qh_findhorizon: point p%d is coplanar to horizon f%d, dist=%2.7g < qh->MINvisible(%2.7g)\n",
              qh_pointid(qh, point), neighbor->id, dist, qh->MINvisible));
        }else
          neighbor->coplanar= False;
        zinc_(Ztothorizon);
        numhorizon++;
        if (neighbor->good)
          (*goodhorizon)++;
        if (qh->IStracing >=4)
          qh_errprint(qh, "horizon", neighbor, NULL, NULL, NULL);
      }
    }
  }
  if (!numhorizon) {
    qh_precision(qh, "empty horizon");
    qh_fprintf(qh, qh->ferr, 6168, "qhull precision error (qh_findhorizon): empty horizon\n\
QhullPoint p%d was above all facets.\n", qh_pointid(qh, point));
    qh_printfacetlist(qh, qh->facet_list, NULL, True);
    qh_errexit(qh, qh_ERRprec, NULL, NULL);
  }
  trace1((qh, qh->ferr, 1041, "qh_findhorizon: %d horizon facets(good %d), %d visible(good %d), %d coplanar\n",
       numhorizon, *goodhorizon, qh->num_visible, *goodvisible, coplanar));
  if (qh->IStracing >= 4 && qh->num_facets < 50)
    qh_printlists(qh);
}























pointT *qh_nextfurthest(qhT *qh, facetT **visible) {
  facetT *facet;
  int size, idx;
  realT randr, dist;
  pointT *furthest;

  while ((facet= qh->facet_next) != qh->facet_tail) {
    if (!facet->outsideset) {
      qh->facet_next= facet->next;
      continue;
    }
    SETreturnsize_(facet->outsideset, size);
    if (!size) {
      qh_setfree(qh, &facet->outsideset);
      qh->facet_next= facet->next;
      continue;
    }
    if (qh->NARROWhull) {
      if (facet->notfurthest)
        qh_furthestout(qh, facet);
      furthest= (pointT*)qh_setlast(facet->outsideset);
#if qh_COMPUTEfurthest
      qh_distplane(qh, furthest, facet, &dist);
      zinc_(Zcomputefurthest);
#else
      dist= facet->furthestdist;
#endif
      if (dist < qh->MINoutside) {
        qh->facet_next= facet->next;
        continue;
      }
    }
    if (!qh->RANDOMoutside && !qh->VIRTUALmemory) {
      if (qh->PICKfurthest) {
        qh_furthestnext(qh  );
        facet= qh->facet_next;
      }
      *visible= facet;
      return((pointT*)qh_setdellast(facet->outsideset));
    }
    if (qh->RANDOMoutside) {
      int outcoplanar = 0;
      if (qh->NARROWhull) {
        FORALLfacets {
          if (facet == qh->facet_next)
            break;
          if (facet->outsideset)
            outcoplanar += qh_setsize(qh, facet->outsideset);
        }
      }
      randr= qh_RANDOMint;
      randr= randr/(qh_RANDOMmax+1);
      idx= (int)floor((qh->num_outside - outcoplanar) * randr);
      FORALLfacet_(qh->facet_next) {
        if (facet->outsideset) {
          SETreturnsize_(facet->outsideset, size);
          if (!size)
            qh_setfree(qh, &facet->outsideset);
          else if (size > idx) {
            *visible= facet;
            return((pointT*)qh_setdelnth(qh, facet->outsideset, idx));
          }else
            idx -= size;
        }
      }
      qh_fprintf(qh, qh->ferr, 6169, "qhull internal error (qh_nextfurthest): num_outside %d is too low\nby at least %d, or a random real %g >= 1.0\n",
              qh->num_outside, idx+1, randr);
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }else {
      facet= qh->facet_tail->previous;
      if (!(furthest= (pointT*)qh_setdellast(facet->outsideset))) {
        if (facet->outsideset)
          qh_setfree(qh, &facet->outsideset);
        qh_removefacet(qh, facet);
        qh_prependfacet(qh, facet, &qh->facet_list);
        continue;
      }
      *visible= facet;
      return furthest;
    }
  }
  return NULL;
}
































void qh_partitionall(qhT *qh, setT *vertices, pointT *points, int numpoints){
  setT *pointset;
  vertexT *vertex, **vertexp;
  pointT *point, **pointp, *bestpoint;
  int size, point_i, point_n, point_end, remaining, i, id;
  facetT *facet;
  realT bestdist= -REALmax, dist, distoutside;

  trace1((qh, qh->ferr, 1042, "qh_partitionall: partition all points into outside sets\n"));
  pointset= qh_settemp(qh, numpoints);
  qh->num_outside= 0;
  pointp= SETaddr_(pointset, pointT);
  for (i=numpoints, point= points; i--; point += qh->hull_dim)
    *(pointp++)= point;
  qh_settruncate(qh, pointset, numpoints);
  FOREACHvertex_(vertices) {
    if ((id= qh_pointid(qh, vertex->point)) >= 0)
      SETelem_(pointset, id)= NULL;
  }
  id= qh_pointid(qh, qh->GOODpointp);
  if (id >=0 && qh->STOPcone-1 != id && -qh->STOPpoint-1 != id)
    SETelem_(pointset, id)= NULL;
  if (qh->GOODvertexp && qh->ONLYgood && !qh->MERGING) {
    if ((id= qh_pointid(qh, qh->GOODvertexp)) >= 0)
      SETelem_(pointset, id)= NULL;
  }
  if (!qh->BESToutside) {
    distoutside= qh_DISToutside;
    zval_(Ztotpartition)= qh->num_points - qh->hull_dim - 1;
    remaining= qh->num_facets;
    point_end= numpoints;
    FORALLfacets {
      size= point_end/(remaining--) + 100;
      facet->outsideset= qh_setnew(qh, size);
      bestpoint= NULL;
      point_end= 0;
      FOREACHpoint_i_(qh, pointset) {
        if (point) {
          zzinc_(Zpartitionall);
          qh_distplane(qh, point, facet, &dist);
          if (dist < distoutside)
            SETelem_(pointset, point_end++)= point;
          else {
            qh->num_outside++;
            if (!bestpoint) {
              bestpoint= point;
              bestdist= dist;
            }else if (dist > bestdist) {
              qh_setappend(qh, &facet->outsideset, bestpoint);
              bestpoint= point;
              bestdist= dist;
            }else
              qh_setappend(qh, &facet->outsideset, point);
          }
        }
      }
      if (bestpoint) {
        qh_setappend(qh, &facet->outsideset, bestpoint);
#if !qh_COMPUTEfurthest
        facet->furthestdist= bestdist;
#endif
      }else
        qh_setfree(qh, &facet->outsideset);
      qh_settruncate(qh, pointset, point_end);
    }
  }

  if (qh->BESToutside || qh->MERGING || qh->KEEPcoplanar || qh->KEEPinside) {
    qh->findbestnew= True;
    FOREACHpoint_i_(qh, pointset) {
      if (point)
        qh_partitionpoint(qh, point, qh->facet_list);
    }
    qh->findbestnew= False;
  }
  zzadd_(Zpartitionall, zzval_(Zpartition));
  zzval_(Zpartition)= 0;
  qh_settempfree(qh, &pointset);
  if (qh->IStracing >= 4)
    qh_printfacetlist(qh, qh->facet_list, NULL, True);
}






































void qh_partitioncoplanar(qhT *qh, pointT *point, facetT *facet, realT *dist) {
  facetT *bestfacet;
  pointT *oldfurthest;
  realT bestdist, dist2= 0, angle;
  int numpart= 0, oldfindbest;
  boolT isoutside;

  qh->WAScoplanar= True;
  if (!dist) {
    if (qh->findbestnew)
      bestfacet= qh_findbestnew(qh, point, facet, &bestdist, qh_ALL, &isoutside, &numpart);
    else
      bestfacet= qh_findbest(qh, point, facet, qh_ALL, !qh_ISnewfacets, qh->DELAUNAY,
                          &bestdist, &isoutside, &numpart);
    zinc_(Ztotpartcoplanar);
    zzadd_(Zpartcoplanar, numpart);
    if (!qh->DELAUNAY && !qh->KEEPinside) {
      if (qh->KEEPnearinside) {
        if (bestdist < -qh->NEARinside) {
          zinc_(Zcoplanarinside);
          trace4((qh, qh->ferr, 4062, "qh_partitioncoplanar: point p%d is more than near-inside facet f%d dist %2.2g findbestnew %d\n",
                  qh_pointid(qh, point), bestfacet->id, bestdist, qh->findbestnew));
          return;
        }
      }else if (bestdist < -qh->MAXcoplanar) {
          trace4((qh, qh->ferr, 4063, "qh_partitioncoplanar: point p%d is inside facet f%d dist %2.2g findbestnew %d\n",
                  qh_pointid(qh, point), bestfacet->id, bestdist, qh->findbestnew));
        zinc_(Zcoplanarinside);
        return;
      }
    }
  }else {
    bestfacet= facet;
    bestdist= *dist;
  }
  if (bestdist > qh->max_outside) {
    if (!dist && facet != bestfacet) {
      zinc_(Zpartangle);
      angle= qh_getangle(qh, facet->normal, bestfacet->normal);
      if (angle < 0) {


        zinc_(Zpartflip);
        trace2((qh, qh->ferr, 2058, "qh_partitioncoplanar: repartition point p%d from f%d.  It is above flipped facet f%d dist %2.2g\n",
                qh_pointid(qh, point), facet->id, bestfacet->id, bestdist));
        oldfindbest= qh->findbestnew;
        qh->findbestnew= False;
        qh_partitionpoint(qh, point, bestfacet);
        qh->findbestnew= oldfindbest;
        return;
      }
    }
    qh->max_outside= bestdist;
    if (bestdist > qh->TRACEdist) {
      qh_fprintf(qh, qh->ferr, 8122, "qh_partitioncoplanar: ====== p%d from f%d increases max_outside to %2.2g of f%d last p%d\n",
                     qh_pointid(qh, point), facet->id, bestdist, bestfacet->id, qh->furthest_id);
      qh_errprint(qh, "DISTANT", facet, bestfacet, NULL, NULL);
    }
  }
  if (qh->KEEPcoplanar + qh->KEEPinside + qh->KEEPnearinside) {
    oldfurthest= (pointT*)qh_setlast(bestfacet->coplanarset);
    if (oldfurthest) {
      zinc_(Zcomputefurthest);
      qh_distplane(qh, oldfurthest, bestfacet, &dist2);
    }
    if (!oldfurthest || dist2 < bestdist)
      qh_setappend(qh, &bestfacet->coplanarset, point);
    else
      qh_setappend2ndlast(qh, &bestfacet->coplanarset, point);
  }
  trace4((qh, qh->ferr, 4064, "qh_partitioncoplanar: point p%d is coplanar with facet f%d(or inside) dist %2.2g\n",
          qh_pointid(qh, point), bestfacet->id, bestdist));
}




































void qh_partitionpoint(qhT *qh, pointT *point, facetT *facet) {
  realT bestdist;
  boolT isoutside;
  facetT *bestfacet;
  int numpart;
#if qh_COMPUTEfurthest
  realT dist;
#endif

  if (qh->findbestnew)
    bestfacet= qh_findbestnew(qh, point, facet, &bestdist, qh->BESToutside, &isoutside, &numpart);
  else
    bestfacet= qh_findbest(qh, point, facet, qh->BESToutside, qh_ISnewfacets, !qh_NOupper,
                          &bestdist, &isoutside, &numpart);
  zinc_(Ztotpartition);
  zzadd_(Zpartition, numpart);
  if (qh->NARROWhull) {
    if (qh->DELAUNAY && !isoutside && bestdist >= -qh->MAXcoplanar)
      qh_precision(qh, "nearly incident point(narrow hull)");
    if (qh->KEEPnearinside) {
      if (bestdist >= -qh->NEARinside)
        isoutside= True;
    }else if (bestdist >= -qh->MAXcoplanar)
      isoutside= True;
  }

  if (isoutside) {
    if (!bestfacet->outsideset
    || !qh_setlast(bestfacet->outsideset)) {
      qh_setappend(qh, &(bestfacet->outsideset), point);
      if (!bestfacet->newfacet) {
        qh_removefacet(qh, bestfacet);
        qh_appendfacet(qh, bestfacet);
      }
#if !qh_COMPUTEfurthest
      bestfacet->furthestdist= bestdist;
#endif
    }else {
#if qh_COMPUTEfurthest
      zinc_(Zcomputefurthest);
      qh_distplane(qh, oldfurthest, bestfacet, &dist);
      if (dist < bestdist)
        qh_setappend(qh, &(bestfacet->outsideset), point);
      else
        qh_setappend2ndlast(qh, &(bestfacet->outsideset), point);
#else
      if (bestfacet->furthestdist < bestdist) {
        qh_setappend(qh, &(bestfacet->outsideset), point);
        bestfacet->furthestdist= bestdist;
      }else
        qh_setappend2ndlast(qh, &(bestfacet->outsideset), point);
#endif
    }
    qh->num_outside++;
    trace4((qh, qh->ferr, 4065, "qh_partitionpoint: point p%d is outside facet f%d new? %d (or narrowhull)\n",
          qh_pointid(qh, point), bestfacet->id, bestfacet->newfacet));
  }else if (qh->DELAUNAY || bestdist >= -qh->MAXcoplanar) {
    zzinc_(Zcoplanarpart);
    if (qh->DELAUNAY)
      qh_precision(qh, "nearly incident point");
    if ((qh->KEEPcoplanar + qh->KEEPnearinside) || bestdist > qh->max_outside)
      qh_partitioncoplanar(qh, point, bestfacet, &bestdist);
    else {
      trace4((qh, qh->ferr, 4066, "qh_partitionpoint: point p%d is coplanar to facet f%d (dropped)\n",
          qh_pointid(qh, point), bestfacet->id));
    }
  }else if (qh->KEEPnearinside && bestdist > -qh->NEARinside) {
    zinc_(Zpartnear);
    qh_partitioncoplanar(qh, point, bestfacet, &bestdist);
  }else {
    zinc_(Zpartinside);
    trace4((qh, qh->ferr, 4067, "qh_partitionpoint: point p%d is inside all facets, closest to f%d dist %2.2g\n",
          qh_pointid(qh, point), bestfacet->id, bestdist));
    if (qh->KEEPinside)
      qh_partitioncoplanar(qh, point, bestfacet, &bestdist);
  }
}



































void qh_partitionvisible(qhT *qh  , boolT allpoints, int *numoutside) {
  facetT *visible, *newfacet;
  pointT *point, **pointp;
  int coplanar=0, size;
  unsigned count;
  vertexT *vertex, **vertexp;

  if (qh->ONLYmax)
    maximize_(qh->MINoutside, qh->max_vertex);
  *numoutside= 0;
  FORALLvisible_facets {
    if (!visible->outsideset && !visible->coplanarset)
      continue;
    newfacet= visible->f.replace;
    count= 0;
    while (newfacet && newfacet->visible) {
      newfacet= newfacet->f.replace;
      if (count++ > qh->facet_id)
        qh_infiniteloop(qh, visible);
    }
    if (!newfacet)
      newfacet= qh->newfacet_list;
    if (newfacet == qh->facet_tail) {
      qh_fprintf(qh, qh->ferr, 6170, "qhull precision error (qh_partitionvisible): all new facets deleted as\n        degenerate facets. Can not continue.\n");
      qh_errexit(qh, qh_ERRprec, NULL, NULL);
    }
    if (visible->outsideset) {
      size= qh_setsize(qh, visible->outsideset);
      *numoutside += size;
      qh->num_outside -= size;
      FOREACHpoint_(visible->outsideset)
        qh_partitionpoint(qh, point, newfacet);
    }
    if (visible->coplanarset && (qh->KEEPcoplanar + qh->KEEPinside + qh->KEEPnearinside)) {
      size= qh_setsize(qh, visible->coplanarset);
      coplanar += size;
      FOREACHpoint_(visible->coplanarset) {
        if (allpoints)
          qh_partitionpoint(qh, point, newfacet);
        else
          qh_partitioncoplanar(qh, point, newfacet, NULL);
      }
    }
  }
  FOREACHvertex_(qh->del_vertices) {
    if (vertex->point) {
      if (allpoints)
        qh_partitionpoint(qh, vertex->point, qh->newfacet_list);
      else
        qh_partitioncoplanar(qh, vertex->point, qh->newfacet_list, NULL);
    }
  }
  trace1((qh, qh->ferr, 1043,"qh_partitionvisible: partitioned %d points from outsidesets and %d points from coplanarsets\n", *numoutside, coplanar));
}









void qh_precision(qhT *qh, const char *reason) {

  if (qh->ALLOWrestart && !qh->PREmerge && !qh->MERGEexact) {
    if (qh->JOGGLEmax < REALmax/2) {
      trace0((qh, qh->ferr, 26, "qh_precision: qhull restart because of %s\n", reason));

      longjmp(qh->restartexit, qh_ERRprec);
    }
  }
}















void qh_printsummary(qhT *qh, FILE *fp) {
  realT ratio, outerplane, innerplane;
  float cpu;
  int size, id, nummerged, numvertices, numcoplanars= 0, nonsimplicial=0;
  int goodused;
  facetT *facet;
  const char *s;
  int numdel= zzval_(Zdelvertextot);
  int numtricoplanars= 0;

  size= qh->num_points + qh_setsize(qh, qh->other_points);
  numvertices= qh->num_vertices - qh_setsize(qh, qh->del_vertices);
  id= qh_pointid(qh, qh->GOODpointp);
  FORALLfacets {
    if (facet->coplanarset)
      numcoplanars += qh_setsize(qh, facet->coplanarset);
    if (facet->good) {
      if (facet->simplicial) {
        if (facet->keepcentrum && facet->tricoplanar)
          numtricoplanars++;
      }else if (qh_setsize(qh, facet->vertices) != qh->hull_dim)
        nonsimplicial++;
    }
  }
  if (id >=0 && qh->STOPcone-1 != id && -qh->STOPpoint-1 != id)
    size--;
  if (qh->STOPcone || qh->STOPpoint)
      qh_fprintf(qh, fp, 9288, "\nAt a premature exit due to 'TVn', 'TCn', 'TRn', or precision error with 'QJn'.");
  if (qh->UPPERdelaunay)
    goodused= qh->GOODvertex + qh->GOODpoint + qh->SPLITthresholds;
  else if (qh->DELAUNAY)
    goodused= qh->GOODvertex + qh->GOODpoint + qh->GOODthreshold;
  else
    goodused= qh->num_good;
  nummerged= zzval_(Ztotmerge) - zzval_(Zcyclehorizon) + zzval_(Zcyclefacettot);
  if (qh->VORONOI) {
    if (qh->UPPERdelaunay)
      qh_fprintf(qh, fp, 9289, "\n\
Furthest-site Voronoi vertices by the convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    else
      qh_fprintf(qh, fp, 9290, "\n\
Voronoi diagram by the convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    qh_fprintf(qh, fp, 9291, "  Number of Voronoi regions%s: %d\n",
              qh->ATinfinity ? " and at-infinity" : "", numvertices);
    if (numdel)
      qh_fprintf(qh, fp, 9292, "  Total number of deleted points due to merging: %d\n", numdel);
    if (numcoplanars - numdel > 0)
      qh_fprintf(qh, fp, 9293, "  Number of nearly incident points: %d\n", numcoplanars - numdel);
    else if (size - numvertices - numdel > 0)
      qh_fprintf(qh, fp, 9294, "  Total number of nearly incident points: %d\n", size - numvertices - numdel);
    qh_fprintf(qh, fp, 9295, "  Number of%s Voronoi vertices: %d\n",
              goodused ? " 'good'" : "", qh->num_good);
    if (nonsimplicial)
      qh_fprintf(qh, fp, 9296, "  Number of%s non-simplicial Voronoi vertices: %d\n",
              goodused ? " 'good'" : "", nonsimplicial);
  }else if (qh->DELAUNAY) {
    if (qh->UPPERdelaunay)
      qh_fprintf(qh, fp, 9297, "\n\
Furthest-site Delaunay triangulation by the convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    else
      qh_fprintf(qh, fp, 9298, "\n\
Delaunay triangulation by the convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    qh_fprintf(qh, fp, 9299, "  Number of input sites%s: %d\n",
              qh->ATinfinity ? " and at-infinity" : "", numvertices);
    if (numdel)
      qh_fprintf(qh, fp, 9300, "  Total number of deleted points due to merging: %d\n", numdel);
    if (numcoplanars - numdel > 0)
      qh_fprintf(qh, fp, 9301, "  Number of nearly incident points: %d\n", numcoplanars - numdel);
    else if (size - numvertices - numdel > 0)
      qh_fprintf(qh, fp, 9302, "  Total number of nearly incident points: %d\n", size - numvertices - numdel);
    qh_fprintf(qh, fp, 9303, "  Number of%s Delaunay regions: %d\n",
              goodused ? " 'good'" : "", qh->num_good);
    if (nonsimplicial)
      qh_fprintf(qh, fp, 9304, "  Number of%s non-simplicial Delaunay regions: %d\n",
              goodused ? " 'good'" : "", nonsimplicial);
  }else if (qh->HALFspace) {
    qh_fprintf(qh, fp, 9305, "\n\
Halfspace intersection by the convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    qh_fprintf(qh, fp, 9306, "  Number of halfspaces: %d\n", size);
    qh_fprintf(qh, fp, 9307, "  Number of non-redundant halfspaces: %d\n", numvertices);
    if (numcoplanars) {
      if (qh->KEEPinside && qh->KEEPcoplanar)
        s= "similar and redundant";
      else if (qh->KEEPinside)
        s= "redundant";
      else
        s= "similar";
      qh_fprintf(qh, fp, 9308, "  Number of %s halfspaces: %d\n", s, numcoplanars);
    }
    qh_fprintf(qh, fp, 9309, "  Number of intersection points: %d\n", qh->num_facets - qh->num_visible);
    if (goodused)
      qh_fprintf(qh, fp, 9310, "  Number of 'good' intersection points: %d\n", qh->num_good);
    if (nonsimplicial)
      qh_fprintf(qh, fp, 9311, "  Number of%s non-simplicial intersection points: %d\n",
              goodused ? " 'good'" : "", nonsimplicial);
  }else {
    qh_fprintf(qh, fp, 9312, "\n\
Convex hull of %d points in %d-d:\n\n", size, qh->hull_dim);
    qh_fprintf(qh, fp, 9313, "  Number of vertices: %d\n", numvertices);
    if (numcoplanars) {
      if (qh->KEEPinside && qh->KEEPcoplanar)
        s= "coplanar and interior";
      else if (qh->KEEPinside)
        s= "interior";
      else
        s= "coplanar";
      qh_fprintf(qh, fp, 9314, "  Number of %s points: %d\n", s, numcoplanars);
    }
    qh_fprintf(qh, fp, 9315, "  Number of facets: %d\n", qh->num_facets - qh->num_visible);
    if (goodused)
      qh_fprintf(qh, fp, 9316, "  Number of 'good' facets: %d\n", qh->num_good);
    if (nonsimplicial)
      qh_fprintf(qh, fp, 9317, "  Number of%s non-simplicial facets: %d\n",
              goodused ? " 'good'" : "", nonsimplicial);
  }
  if (numtricoplanars)
      qh_fprintf(qh, fp, 9318, "  Number of triangulated facets: %d\n", numtricoplanars);
  qh_fprintf(qh, fp, 9319, "\nStatistics for: %s | %s",
                      qh->rbox_command, qh->qhull_command);
  if (qh->ROTATErandom != INT_MIN)
    qh_fprintf(qh, fp, 9320, " QR%d\n\n", qh->ROTATErandom);
  else
    qh_fprintf(qh, fp, 9321, "\n\n");
  qh_fprintf(qh, fp, 9322, "  Number of points processed: %d\n", zzval_(Zprocessed));
  qh_fprintf(qh, fp, 9323, "  Number of hyperplanes created: %d\n", zzval_(Zsetplane));
  if (qh->DELAUNAY)
    qh_fprintf(qh, fp, 9324, "  Number of facets in hull: %d\n", qh->num_facets - qh->num_visible);
  qh_fprintf(qh, fp, 9325, "  Number of distance tests for qhull: %d\n", zzval_(Zpartition)+
      zzval_(Zpartitionall)+zzval_(Znumvisibility)+zzval_(Zpartcoplanar));
#if 0
  {realT stddev, ave;
  qh_fprintf(qh, fp, 9326, "  average new facet balance: %2.2g\n",
          wval_(Wnewbalance)/zval_(Zprocessed));
  stddev= qh_stddev(zval_(Zprocessed), wval_(Wnewbalance),
                                 wval_(Wnewbalance2), &ave);
  qh_fprintf(qh, fp, 9327, "  new facet standard deviation: %2.2g\n", stddev);
  qh_fprintf(qh, fp, 9328, "  average partition balance: %2.2g\n",
          wval_(Wpbalance)/zval_(Zpbalance));
  stddev= qh_stddev(zval_(Zpbalance), wval_(Wpbalance),
                                 wval_(Wpbalance2), &ave);
  qh_fprintf(qh, fp, 9329, "  partition standard deviation: %2.2g\n", stddev);
  }
#endif
  if (nummerged) {
    qh_fprintf(qh, fp, 9330,"  Number of distance tests for merging: %d\n",zzval_(Zbestdist)+
          zzval_(Zcentrumtests)+zzval_(Zdistconvex)+zzval_(Zdistcheck)+
          zzval_(Zdistzero));
    qh_fprintf(qh, fp, 9331,"  Number of distance tests for checking: %d\n",zzval_(Zcheckpart));
    qh_fprintf(qh, fp, 9332,"  Number of merged facets: %d\n", nummerged);
  }
  if (!qh->RANDOMoutside && qh->QHULLfinished) {
    cpu= (float)qh->hulltime;
    cpu /= (float)qh_SECticks;
    wval_(Wcpu)= cpu;
    qh_fprintf(qh, fp, 9333, "  CPU seconds to compute hull (after input): %2.4g\n", cpu);
  }
  if (qh->RERUN) {
    if (!qh->PREmerge && !qh->MERGEexact)
      qh_fprintf(qh, fp, 9334, "  Percentage of runs with precision errors: %4.1f\n",
           zzval_(Zretry)*100.0/qh->build_cnt);
  }else if (qh->JOGGLEmax < REALmax/2) {
    if (zzval_(Zretry))
      qh_fprintf(qh, fp, 9335, "  After %d retries, input joggled by: %2.2g\n",
         zzval_(Zretry), qh->JOGGLEmax);
    else
      qh_fprintf(qh, fp, 9336, "  Input joggled by: %2.2g\n", qh->JOGGLEmax);
  }
  if (qh->totarea != 0.0)
    qh_fprintf(qh, fp, 9337, "  %s facet area:   %2.8g\n",
            zzval_(Ztotmerge) ? "Approximate" : "Total", qh->totarea);
  if (qh->totvol != 0.0)
    qh_fprintf(qh, fp, 9338, "  %s volume:       %2.8g\n",
            zzval_(Ztotmerge) ? "Approximate" : "Total", qh->totvol);
  if (qh->MERGING) {
    qh_outerinner(qh, NULL, &outerplane, &innerplane);
    if (outerplane > 2 * qh->DISTround) {
      qh_fprintf(qh, fp, 9339, "  Maximum distance of %spoint above facet: %2.2g",
            (qh->QHULLfinished ? "" : "merged "), outerplane);
      ratio= outerplane/(qh->ONEmerge + qh->DISTround);

      if (ratio > 0.05 && 2* qh->ONEmerge > qh->MINoutside && qh->JOGGLEmax > REALmax/2)
        qh_fprintf(qh, fp, 9340, " (%.1fx)\n", ratio);
      else
        qh_fprintf(qh, fp, 9341, "\n");
    }
    if (innerplane < -2 * qh->DISTround) {
      qh_fprintf(qh, fp, 9342, "  Maximum distance of %svertex below facet: %2.2g",
            (qh->QHULLfinished ? "" : "merged "), innerplane);
      ratio= -innerplane/(qh->ONEmerge+qh->DISTround);
      if (ratio > 0.05 && qh->JOGGLEmax > REALmax/2)
        qh_fprintf(qh, fp, 9343, " (%.1fx)\n", ratio);
      else
        qh_fprintf(qh, fp, 9344, "\n");
    }
  }
  qh_fprintf(qh, fp, 9345, "\n");
}
