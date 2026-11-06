/*<html><pre>  -<a                             href="qh-poly_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   poly2_r.c
   implements polygons and simplicies

   see qh-poly_r.htm, poly_r.h and libqhull_r.h

   frequently used code is in poly_r.c

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/poly2_r.c#10 $$Change: 2069 $
   $DateTime: 2016/01/18 22:05:03 $$Author: bbarber $
*/

#include "qhull_ra.h"









void qh_addhash(void *newelem, setT *hashtable, int hashsize, int hash) {
  int scan;
  void *elem;

  for (scan= (int)hash; (elem= SETelem_(hashtable, scan));
       scan= (++scan >= hashsize ? 0 : scan)) {
    if (elem == newelem)
      break;
  }

  if (!elem)
    SETelem_(hashtable, scan)= newelem;
}



























void qh_check_bestdist(qhT *qh) {
  boolT waserror= False, unassigned;
  facetT *facet, *bestfacet, *errfacet1= NULL, *errfacet2= NULL;
  facetT *facetlist;
  realT dist, maxoutside, maxdist= -REALmax;
  pointT *point;
  int numpart= 0, facet_i, facet_n, notgood= 0, notverified= 0;
  setT *facets;

  trace1((qh, qh->ferr, 1020, "qh_check_bestdist: check points below nearest facet.  Facet_list f%d\n",
      qh->facet_list->id));
  maxoutside= qh_maxouter(qh);
  maxoutside += qh->DISTround;

  trace1((qh, qh->ferr, 1021, "qh_check_bestdist: check that all points are within %2.2g of best facet\n", maxoutside));
  facets= qh_pointfacet(qh  );
  if (!qh_QUICKhelp && qh->PRINTprecision)
    qh_fprintf(qh, qh->ferr, 8091, "\n\
qhull output completed.  Verifying that %d points are\n\
below %2.2g of the nearest %sfacet.\n",
             qh_setsize(qh, facets), maxoutside, (qh->ONLYgood ?  "good " : ""));
  FOREACHfacet_i_(qh, facets) {
    if (facet)
      unassigned= False;
    else {
      unassigned= True;
      facet= qh->facet_list;
    }
    point= qh_point(qh, facet_i);
    if (point == qh->GOODpointp)
      continue;
    qh_distplane(qh, point, facet, &dist);
    numpart++;
    bestfacet= qh_findbesthorizon(qh, !qh_IScheckmax, point, facet, qh_NOupper, &dist, &numpart);

    maximize_(maxdist, dist);
    if (dist > maxoutside) {
      if (qh->ONLYgood && !bestfacet->good
          && !((bestfacet= qh_findgooddist(qh, point, bestfacet, &dist, &facetlist))
               && dist > maxoutside))
        notgood++;
      else {
        waserror= True;
        qh_fprintf(qh, qh->ferr, 6109, "qhull precision error: point p%d is outside facet f%d, distance= %6.8g maxoutside= %6.8g\n",
                facet_i, bestfacet->id, dist, maxoutside);
        if (errfacet1 != bestfacet) {
          errfacet2= errfacet1;
          errfacet1= bestfacet;
        }
      }
    }else if (unassigned && dist < -qh->MAXcoplanar)
      notverified++;
  }
  qh_settempfree(qh, &facets);
  if (notverified && !qh->DELAUNAY && !qh_QUICKhelp && qh->PRINTprecision)
    qh_fprintf(qh, qh->ferr, 8092, "\n%d points were well inside the hull.  If the hull contains\n\
a lens-shaped component, these points were not verified.  Use\n\
options 'Qci Tv' to verify all points.\n", notverified);
  if (maxdist > qh->outside_err) {
    qh_fprintf(qh, qh->ferr, 6110, "qhull precision error (qh_check_bestdist): a coplanar point is %6.2g from convex hull.  The maximum value(qh.outside_err) is %6.2g\n",
              maxdist, qh->outside_err);
    qh_errexit2(qh, qh_ERRprec, errfacet1, errfacet2);
  }else if (waserror && qh->outside_err > REALmax/2)
    qh_errexit2(qh, qh_ERRprec, errfacet1, errfacet2);

  trace0((qh, qh->ferr, 20, "qh_check_bestdist: max distance outside %2.2g\n", maxdist));
}
















#ifndef qh_NOmerge
void qh_check_dupridge(qhT *qh, facetT *facet1, realT dist1, facetT *facet2, realT dist2) {
  vertexT *vertex, **vertexp, *vertexA, **vertexAp;
  realT dist, innerplane, mergedist, outerplane, prevdist, ratio;
  realT minvertex= REALmax;

  mergedist= fmin_(dist1, dist2);
  qh_outerinner(qh, NULL, &outerplane, &innerplane);
  prevdist= fmax_(outerplane, innerplane);
  maximize_(prevdist, qh->ONEmerge + qh->DISTround);
  maximize_(prevdist, qh->MINoutside + qh->DISTround);
  ratio= mergedist/prevdist;
  FOREACHvertex_(facet1->vertices) {
    FOREACHvertexA_(facet1->vertices) {
      if (vertex > vertexA){
        dist= qh_pointdist(vertex->point, vertexA->point, qh->hull_dim);
        minimize_(minvertex, dist);
      }
    }
  }
  trace0((qh, qh->ferr, 16, "qh_check_dupridge: duplicate ridge between f%d and f%d due to nearly-coincident vertices (%2.2g), dist %2.2g, reverse dist %2.2g, ratio %2.2g while processing p%d\n",
        facet1->id, facet2->id, minvertex, dist1, dist2, ratio, qh->furthest_id));
  if (ratio > qh_WIDEduplicate) {
    qh_fprintf(qh, qh->ferr, 6271, "qhull precision error (qh_check_dupridge): wide merge (%.0f times wider) due to duplicate ridge with nearly coincident points (%2.2g) between f%d and f%d, merge dist %2.2g, while processing p%d\n- Ignore error with option 'Q12'\n- To be fixed in a later version of Qhull\n",
          ratio, minvertex, facet1->id, facet2->id, mergedist, qh->furthest_id);
    if (qh->DELAUNAY)
      qh_fprintf(qh, qh->ferr, 8145, "- A bounding box for the input sites may alleviate this error.\n");
    if(minvertex > qh_WIDEduplicate*prevdist)
      qh_fprintf(qh, qh->ferr, 8146, "- Vertex distance %2.2g is greater than %d times maximum distance %2.2g\n  Please report to bradb@shore.net with steps to reproduce and all output\n",
          minvertex, qh_WIDEduplicate, prevdist);
    if (!qh->NOwide)
      qh_errexit2(qh, qh_ERRqhull, facet1, facet2);
  }
}
#endif

































#ifndef qh_NOmerge
void qh_check_maxout(qhT *qh) {
  facetT *facet, *bestfacet, *neighbor, **neighborp, *facetlist;
  realT dist, maxoutside, minvertex, old_maxoutside;
  pointT *point;
  int numpart= 0, facet_i, facet_n, notgood= 0;
  setT *facets, *vertices;
  vertexT *vertex;

  trace1((qh, qh->ferr, 1022, "qh_check_maxout: check and update maxoutside for each facet.\n"));
  maxoutside= minvertex= 0;
  if (qh->VERTEXneighbors
  && (qh->PRINTsummary || qh->KEEPinside || qh->KEEPcoplanar
        || qh->TRACElevel || qh->PRINTstatistics
        || qh->PRINTout[0] == qh_PRINTsummary || qh->PRINTout[0] == qh_PRINTnone)) {
    trace1((qh, qh->ferr, 1023, "qh_check_maxout: determine actual maxoutside and minvertex\n"));
    vertices= qh_pointvertex(qh  );
    FORALLvertices {
      FOREACHneighbor_(vertex) {
        zinc_(Zdistvertex);
        qh_distplane(qh, vertex->point, neighbor, &dist);
        minimize_(minvertex, dist);
        if (-dist > qh->TRACEdist || dist > qh->TRACEdist
        || neighbor == qh->tracefacet || vertex == qh->tracevertex)
          qh_fprintf(qh, qh->ferr, 8093, "qh_check_maxout: p%d(v%d) is %.2g from f%d\n",
                    qh_pointid(qh, vertex->point), vertex->id, dist, neighbor->id);
      }
    }
    if (qh->MERGING) {
      wmin_(Wminvertex, qh->min_vertex);
    }
    qh->min_vertex= minvertex;
    qh_settempfree(qh, &vertices);
  }
  facets= qh_pointfacet(qh  );
  do {
    old_maxoutside= fmax_(qh->max_outside, maxoutside);
    FOREACHfacet_i_(qh, facets) {
      if (facet) {
        point= qh_point(qh, facet_i);
        if (point == qh->GOODpointp)
          continue;
        zzinc_(Ztotcheck);
        qh_distplane(qh, point, facet, &dist);
        numpart++;
        bestfacet= qh_findbesthorizon(qh, qh_IScheckmax, point, facet, !qh_NOupper, &dist, &numpart);
        if (bestfacet && dist > maxoutside) {
          if (qh->ONLYgood && !bestfacet->good
          && !((bestfacet= qh_findgooddist(qh, point, bestfacet, &dist, &facetlist))
               && dist > maxoutside))
            notgood++;
          else
            maxoutside= dist;
        }
        if (dist > qh->TRACEdist || (bestfacet && bestfacet == qh->tracefacet))
          qh_fprintf(qh, qh->ferr, 8094, "qh_check_maxout: p%d is %.2g above f%d\n",
          qh_pointid(qh, point), dist, (bestfacet ? bestfacet->id : UINT_MAX));
      }
    }
  }while
    (maxoutside > 2*old_maxoutside);


  zzadd_(Zcheckpart, numpart);
  qh_settempfree(qh, &facets);
  wval_(Wmaxout)= maxoutside - qh->max_outside;
  wmax_(Wmaxoutside, qh->max_outside);
  qh->max_outside= maxoutside;
  qh_nearcoplanar(qh  );
  qh->maxoutdone= True;
  trace1((qh, qh->ferr, 1024, "qh_check_maxout: maxoutside %2.2g, min_vertex %2.2g, outside of not good %d\n",
       maxoutside, qh->min_vertex, notgood));
}
#else
void qh_check_maxout(qhT *qh) {
}
#endif








void qh_check_output(qhT *qh) {
  int i;

  if (qh->STOPcone)
    return;
  if (qh->VERIFYoutput | qh->IStracing | qh->CHECKfrequently) {
    qh_checkpolygon(qh, qh->facet_list);
    qh_checkflipped_all(qh, qh->facet_list);
    qh_checkconvex(qh, qh->facet_list, qh_ALGORITHMfault);
  }else if (!qh->MERGING && qh_newstats(qh, qh->qhstat.precision, &i)) {
    qh_checkflipped_all(qh, qh->facet_list);
    qh_checkconvex(qh, qh->facet_list, qh_ALGORITHMfault);
  }
}









void qh_check_point(qhT *qh, pointT *point, facetT *facet, realT *maxoutside, realT *maxdist, facetT **errfacet1, facetT **errfacet2) {
  realT dist;


  qh_distplane(qh, point, facet, &dist);
  if (dist > *maxoutside) {
    if (*errfacet1 != facet) {
      *errfacet2= *errfacet1;
      *errfacet1= facet;
    }
    qh_fprintf(qh, qh->ferr, 6111, "qhull precision error: point p%d is outside facet f%d, distance= %6.8g maxoutside= %6.8g\n",
              qh_pointid(qh, point), facet->id, dist, *maxoutside);
  }
  maximize_(*maxdist, dist);
}

























void qh_check_points(qhT *qh) {
  facetT *facet, *errfacet1= NULL, *errfacet2= NULL;
  realT total, maxoutside, maxdist= -REALmax;
  pointT *point, **pointp, *pointtemp;
  boolT testouter;

  maxoutside= qh_maxouter(qh);
  maxoutside += qh->DISTround;

  trace1((qh, qh->ferr, 1025, "qh_check_points: check all points below %2.2g of all facet planes\n",
          maxoutside));
  if (qh->num_good)
     total= (float)qh->num_good * (float)qh->num_points;
  else
     total= (float)qh->num_facets * (float)qh->num_points;
  if (total >= qh_VERIFYdirect && !qh->maxoutdone) {
    if (!qh_QUICKhelp && qh->SKIPcheckmax && qh->MERGING)
      qh_fprintf(qh, qh->ferr, 7075, "qhull input warning: merging without checking outer planes('Q5' or 'Po').\n\
Verify may report that a point is outside of a facet.\n");
    qh_check_bestdist(qh);
  }else {
    if (qh_MAXoutside && qh->maxoutdone)
      testouter= True;
    else
      testouter= False;
    if (!qh_QUICKhelp) {
      if (qh->MERGEexact)
        qh_fprintf(qh, qh->ferr, 7076, "qhull input warning: exact merge ('Qx').  Verify may report that a point\n\
is outside of a facet.  See qh-optq.htm#Qx\n");
      else if (qh->SKIPcheckmax || qh->NOnearinside)
        qh_fprintf(qh, qh->ferr, 7077, "qhull input warning: no outer plane check ('Q5') or no processing of\n\
near-inside points ('Q8').  Verify may report that a point is outside\n\
of a facet.\n");
    }
    if (qh->PRINTprecision) {
      if (testouter)
        qh_fprintf(qh, qh->ferr, 8098, "\n\
Output completed.  Verifying that all points are below outer planes of\n\
all %sfacets.  Will make %2.0f distance computations.\n",
              (qh->ONLYgood ?  "good " : ""), total);
      else
        qh_fprintf(qh, qh->ferr, 8099, "\n\
Output completed.  Verifying that all points are below %2.2g of\n\
all %sfacets.  Will make %2.0f distance computations.\n",
              maxoutside, (qh->ONLYgood ?  "good " : ""), total);
    }
    FORALLfacets {
      if (!facet->good && qh->ONLYgood)
        continue;
      if (facet->flipped)
        continue;
      if (!facet->normal) {
        qh_fprintf(qh, qh->ferr, 7061, "qhull warning (qh_check_points): missing normal for facet f%d\n", facet->id);
        continue;
      }
      if (testouter) {
#if qh_MAXoutside
        maxoutside= facet->maxoutside + 2* qh->DISTround;

#endif
      }
      FORALLpoints {
        if (point != qh->GOODpointp)
          qh_check_point(qh, point, facet, &maxoutside, &maxdist, &errfacet1, &errfacet2);
      }
      FOREACHpoint_(qh->other_points) {
        if (point != qh->GOODpointp)
          qh_check_point(qh, point, facet, &maxoutside, &maxdist, &errfacet1, &errfacet2);
      }
    }
    if (maxdist > qh->outside_err) {
      qh_fprintf(qh, qh->ferr, 6112, "qhull precision error (qh_check_points): a coplanar point is %6.2g from convex hull.  The maximum value(qh.outside_err) is %6.2g\n",
                maxdist, qh->outside_err );
      qh_errexit2(qh, qh_ERRprec, errfacet1, errfacet2 );
    }else if (errfacet1 && qh->outside_err > REALmax/2)
        qh_errexit2(qh, qh_ERRprec, errfacet1, errfacet2 );

    trace0((qh, qh->ferr, 21, "qh_check_points: max distance outside %2.2g\n", maxdist));
  }
}






























void qh_checkconvex(qhT *qh, facetT *facetlist, int fault) {
  facetT *facet, *neighbor, **neighborp, *errfacet1=NULL, *errfacet2=NULL;
  vertexT *vertex;
  realT dist;
  pointT *centrum;
  boolT waserror= False, centrum_warning= False, tempcentrum= False, allsimplicial;
  int neighbor_i;

  trace1((qh, qh->ferr, 1026, "qh_checkconvex: check all ridges are convex\n"));
  if (!qh->RERUN) {
    zzval_(Zconcaveridges)= 0;
    zzval_(Zcoplanarridges)= 0;
  }
  FORALLfacet_(facetlist) {
    if (facet->flipped) {
      qh_precision(qh, "flipped facet");
      qh_fprintf(qh, qh->ferr, 6113, "qhull precision error: f%d is flipped(interior point is outside)\n",
               facet->id);
      errfacet1= facet;
      waserror= True;
      continue;
    }
    if (qh->MERGING && (!qh->ZEROcentrum || !facet->simplicial || facet->tricoplanar))
      allsimplicial= False;
    else {
      allsimplicial= True;
      neighbor_i= 0;
      FOREACHneighbor_(facet) {
        vertex= SETelemt_(facet->vertices, neighbor_i++, vertexT);
        if (!neighbor->simplicial || neighbor->tricoplanar) {
          allsimplicial= False;
          continue;
        }
        qh_distplane(qh, vertex->point, neighbor, &dist);
        if (dist > -qh->DISTround) {
          if (fault == qh_DATAfault) {
            qh_precision(qh, "coplanar or concave ridge");
            qh_fprintf(qh, qh->ferr, 6114, "qhull precision error: initial simplex is not convex. Distance=%.2g\n", dist);
            qh_errexit(qh, qh_ERRsingular, NULL, NULL);
          }
          if (dist > qh->DISTround) {
            zzinc_(Zconcaveridges);
            qh_precision(qh, "concave ridge");
            qh_fprintf(qh, qh->ferr, 6115, "qhull precision error: f%d is concave to f%d, since p%d(v%d) is %6.4g above\n",
              facet->id, neighbor->id, qh_pointid(qh, vertex->point), vertex->id, dist);
            errfacet1= facet;
            errfacet2= neighbor;
            waserror= True;
          }else if (qh->ZEROcentrum) {
            if (dist > 0) {
              zzinc_(Zcoplanarridges);
              qh_precision(qh, "coplanar ridge");
              qh_fprintf(qh, qh->ferr, 6116, "qhull precision error: f%d is clearly not convex to f%d, since p%d(v%d) is %6.4g above\n",
                facet->id, neighbor->id, qh_pointid(qh, vertex->point), vertex->id, dist);
              errfacet1= facet;
              errfacet2= neighbor;
              waserror= True;
            }
          }else {
            zzinc_(Zcoplanarridges);
            qh_precision(qh, "coplanar ridge");
            trace0((qh, qh->ferr, 22, "qhull precision error: f%d may be coplanar to f%d, since p%d(v%d) is within %6.4g during p%d\n",
              facet->id, neighbor->id, qh_pointid(qh, vertex->point), vertex->id, dist, qh->furthest_id));
          }
        }
      }
    }
    if (!allsimplicial) {
      if (qh->CENTERtype == qh_AScentrum) {
        if (!facet->center)
          facet->center= qh_getcentrum(qh, facet);
        centrum= facet->center;
      }else {
        if (!centrum_warning && (!facet->simplicial || facet->tricoplanar)) {
           centrum_warning= True;
           qh_fprintf(qh, qh->ferr, 7062, "qhull warning: recomputing centrums for convexity test.  This may lead to false, precision errors.\n");
        }
        centrum= qh_getcentrum(qh, facet);
        tempcentrum= True;
      }
      FOREACHneighbor_(facet) {
        if (qh->ZEROcentrum && facet->simplicial && neighbor->simplicial)
          continue;
        if (facet->tricoplanar || neighbor->tricoplanar)
          continue;
        zzinc_(Zdistconvex);
        qh_distplane(qh, centrum, neighbor, &dist);
        if (dist > qh->DISTround) {
          zzinc_(Zconcaveridges);
          qh_precision(qh, "concave ridge");
          qh_fprintf(qh, qh->ferr, 6117, "qhull precision error: f%d is concave to f%d.  Centrum of f%d is %6.4g above f%d\n",
            facet->id, neighbor->id, facet->id, dist, neighbor->id);
          errfacet1= facet;
          errfacet2= neighbor;
          waserror= True;
        }else if (dist >= 0.0) {

          zzinc_(Zcoplanarridges);
          qh_precision(qh, "coplanar ridge");
          qh_fprintf(qh, qh->ferr, 6118, "qhull precision error: f%d is coplanar or concave to f%d.  Centrum of f%d is %6.4g above f%d\n",
            facet->id, neighbor->id, facet->id, dist, neighbor->id);
          errfacet1= facet;
          errfacet2= neighbor;
          waserror= True;
        }
      }
      if (tempcentrum)
        qh_memfree(qh, centrum, qh->normal_size);
    }
  }
  if (waserror && !qh->FORCEoutput)
    qh_errexit2(qh, qh_ERRprec, errfacet1, errfacet2);
}







































void qh_checkfacet(qhT *qh, facetT *facet, boolT newmerge, boolT *waserrorp) {
  facetT *neighbor, **neighborp, *errother=NULL;
  ridgeT *ridge, **ridgep, *errridge= NULL, *ridge2;
  vertexT *vertex, **vertexp;
  unsigned previousid= INT_MAX;
  int numneighbors, numvertices, numridges=0, numRvertices=0;
  boolT waserror= False;
  int skipA, skipB, ridge_i, ridge_n, i;
  setT *intersection;

  if (facet->visible) {
    qh_fprintf(qh, qh->ferr, 6119, "qhull internal error (qh_checkfacet): facet f%d is on the visible_list\n",
      facet->id);
    qh_errexit(qh, qh_ERRqhull, facet, NULL);
  }
  if (!facet->normal) {
    qh_fprintf(qh, qh->ferr, 6120, "qhull internal error (qh_checkfacet): facet f%d does not have  a normal\n",
      facet->id);
    waserror= True;
  }
  qh_setcheck(qh, facet->vertices, "vertices for f", facet->id);
  qh_setcheck(qh, facet->ridges, "ridges for f", facet->id);
  qh_setcheck(qh, facet->outsideset, "outsideset for f", facet->id);
  qh_setcheck(qh, facet->coplanarset, "coplanarset for f", facet->id);
  qh_setcheck(qh, facet->neighbors, "neighbors for f", facet->id);
  FOREACHvertex_(facet->vertices) {
    if (vertex->deleted) {
      qh_fprintf(qh, qh->ferr, 6121, "qhull internal error (qh_checkfacet): deleted vertex v%d in f%d\n", vertex->id, facet->id);
      qh_errprint(qh, "ERRONEOUS", NULL, NULL, NULL, vertex);
      waserror= True;
    }
    if (vertex->id >= previousid) {
      qh_fprintf(qh, qh->ferr, 6122, "qhull internal error (qh_checkfacet): vertices of f%d are not in descending id order at v%d\n", facet->id, vertex->id);
      waserror= True;
      break;
    }
    previousid= vertex->id;
  }
  numneighbors= qh_setsize(qh, facet->neighbors);
  numvertices= qh_setsize(qh, facet->vertices);
  numridges= qh_setsize(qh, facet->ridges);
  if (facet->simplicial) {
    if (numvertices+numneighbors != 2*qh->hull_dim
    && !facet->degenerate && !facet->redundant) {
      qh_fprintf(qh, qh->ferr, 6123, "qhull internal error (qh_checkfacet): for simplicial facet f%d, #vertices %d + #neighbors %d != 2*qh->hull_dim\n",
                facet->id, numvertices, numneighbors);
      qh_setprint(qh, qh->ferr, "", facet->neighbors);
      waserror= True;
    }
  }else {
    if (!newmerge
    &&(numvertices < qh->hull_dim || numneighbors < qh->hull_dim)
    && !facet->degenerate && !facet->redundant) {
      qh_fprintf(qh, qh->ferr, 6124, "qhull internal error (qh_checkfacet): for facet f%d, #vertices %d or #neighbors %d < qh->hull_dim\n",
         facet->id, numvertices, numneighbors);
       waserror= True;
    }

    if (numridges < numneighbors
    ||(qh->hull_dim == 3 && numvertices > numridges && !qh->NEWfacets)
    ||(qh->hull_dim == 2 && numridges + numvertices + numneighbors != 6)) {
      if (!facet->degenerate && !facet->redundant) {
        qh_fprintf(qh, qh->ferr, 6125, "qhull internal error (qh_checkfacet): for facet f%d, #ridges %d < #neighbors %d or(3-d) > #vertices %d or(2-d) not all 2\n",
            facet->id, numridges, numneighbors, numvertices);
        waserror= True;
      }
    }
  }
  FOREACHneighbor_(facet) {
    if (neighbor == qh_MERGEridge || neighbor == qh_DUPLICATEridge) {
      qh_fprintf(qh, qh->ferr, 6126, "qhull internal error (qh_checkfacet): facet f%d still has a MERGE or DUP neighbor\n", facet->id);
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
    neighbor->seen= True;
  }
  FOREACHneighbor_(facet) {
    if (!qh_setin(neighbor->neighbors, facet)) {
      qh_fprintf(qh, qh->ferr, 6127, "qhull internal error (qh_checkfacet): facet f%d has neighbor f%d, but f%d does not have neighbor f%d\n",
              facet->id, neighbor->id, neighbor->id, facet->id);
      errother= neighbor;
      waserror= True;
    }
    if (!neighbor->seen) {
      qh_fprintf(qh, qh->ferr, 6128, "qhull internal error (qh_checkfacet): facet f%d has a duplicate neighbor f%d\n",
              facet->id, neighbor->id);
      errother= neighbor;
      waserror= True;
    }
    neighbor->seen= False;
  }
  FOREACHridge_(facet->ridges) {
    qh_setcheck(qh, ridge->vertices, "vertices for r", ridge->id);
    ridge->seen= False;
  }
  FOREACHridge_(facet->ridges) {
    if (ridge->seen) {
      qh_fprintf(qh, qh->ferr, 6129, "qhull internal error (qh_checkfacet): facet f%d has a duplicate ridge r%d\n",
              facet->id, ridge->id);
      errridge= ridge;
      waserror= True;
    }
    ridge->seen= True;
    numRvertices= qh_setsize(qh, ridge->vertices);
    if (numRvertices != qh->hull_dim - 1) {
      qh_fprintf(qh, qh->ferr, 6130, "qhull internal error (qh_checkfacet): ridge between f%d and f%d has %d vertices\n",
                ridge->top->id, ridge->bottom->id, numRvertices);
      errridge= ridge;
      waserror= True;
    }
    neighbor= otherfacet_(ridge, facet);
    neighbor->seen= True;
    if (!qh_setin(facet->neighbors, neighbor)) {
      qh_fprintf(qh, qh->ferr, 6131, "qhull internal error (qh_checkfacet): for facet f%d, neighbor f%d of ridge r%d not in facet\n",
           facet->id, neighbor->id, ridge->id);
      errridge= ridge;
      waserror= True;
    }
  }
  if (!facet->simplicial) {
    FOREACHneighbor_(facet) {
      if (!neighbor->seen) {
        qh_fprintf(qh, qh->ferr, 6132, "qhull internal error (qh_checkfacet): facet f%d does not have a ridge for neighbor f%d\n",
              facet->id, neighbor->id);
        errother= neighbor;
        waserror= True;
      }
      intersection= qh_vertexintersect_new(qh, facet->vertices, neighbor->vertices);
      qh_settemppush(qh, intersection);
      FOREACHvertex_(facet->vertices) {
        vertex->seen= False;
        vertex->seen2= False;
      }
      FOREACHvertex_(intersection)
        vertex->seen= True;
      FOREACHridge_(facet->ridges) {
        if (neighbor != otherfacet_(ridge, facet))
            continue;
        FOREACHvertex_(ridge->vertices) {
          if (!vertex->seen) {
            qh_fprintf(qh, qh->ferr, 6133, "qhull internal error (qh_checkfacet): vertex v%d in r%d not in f%d intersect f%d\n",
                  vertex->id, ridge->id, facet->id, neighbor->id);
            qh_errexit(qh, qh_ERRqhull, facet, ridge);
          }
          vertex->seen2= True;
        }
      }
      if (!newmerge) {
        FOREACHvertex_(intersection) {
          if (!vertex->seen2) {
            if (qh->IStracing >=3 || !qh->MERGING) {
              qh_fprintf(qh, qh->ferr, 6134, "qhull precision error (qh_checkfacet): vertex v%d in f%d intersect f%d but\n\
 not in a ridge.  This is ok under merging.  Last point was p%d\n",
                     vertex->id, facet->id, neighbor->id, qh->furthest_id);
              if (!qh->FORCEoutput && !qh->MERGING) {
                qh_errprint(qh, "ERRONEOUS", facet, neighbor, NULL, vertex);
                if (!qh->MERGING)
                  qh_errexit(qh, qh_ERRqhull, NULL, NULL);
              }
            }
          }
        }
      }
      qh_settempfree(qh, &intersection);
    }
  }else {
    FOREACHneighbor_(facet) {
      if (neighbor->simplicial) {
        skipA= SETindex_(facet->neighbors, neighbor);
        skipB= qh_setindex(neighbor->neighbors, facet);
        if (skipA<0 || skipB<0 || !qh_setequal_skip(facet->vertices, skipA, neighbor->vertices, skipB)) {
          qh_fprintf(qh, qh->ferr, 6135, "qhull internal error (qh_checkfacet): facet f%d skip %d and neighbor f%d skip %d do not match \n",
                   facet->id, skipA, neighbor->id, skipB);
          errother= neighbor;
          waserror= True;
        }
      }
    }
  }
  if (qh->hull_dim < 5 && (qh->IStracing > 2 || qh->CHECKfrequently)) {
    FOREACHridge_i_(qh, facet->ridges) {
      for (i=ridge_i+1; i < ridge_n; i++) {
        ridge2= SETelemt_(facet->ridges, i, ridgeT);
        if (qh_setequal(ridge->vertices, ridge2->vertices)) {
          qh_fprintf(qh, qh->ferr, 6227, "Qhull internal error (qh_checkfacet): ridges r%d and r%d have the same vertices\n",
                  ridge->id, ridge2->id);
          errridge= ridge;
          waserror= True;
        }
      }
    }
  }
  if (waserror) {
    qh_errprint(qh, "ERRONEOUS", facet, errother, errridge, NULL);
    *waserrorp= True;
  }
}








void qh_checkflipped_all(qhT *qh, facetT *facetlist) {
  facetT *facet;
  boolT waserror= False;
  realT dist;

  if (facetlist == qh->facet_list)
    zzval_(Zflippedfacets)= 0;
  FORALLfacet_(facetlist) {
    if (facet->normal && !qh_checkflipped(qh, facet, &dist, !qh_ALL)) {
      qh_fprintf(qh, qh->ferr, 6136, "qhull precision error: facet f%d is flipped, distance= %6.12g\n",
              facet->id, dist);
      if (!qh->FORCEoutput) {
        qh_errprint(qh, "ERRONEOUS", facet, NULL, NULL, NULL);
        waserror= True;
      }
    }
  }
  if (waserror) {
    qh_fprintf(qh, qh->ferr, 8101, "\n\
A flipped facet occurs when its distance to the interior point is\n\
greater than %2.2g, the maximum roundoff error.\n", -qh->DISTround);
    qh_errexit(qh, qh_ERRprec, NULL, NULL);
  }
}























void qh_checkpolygon(qhT *qh, facetT *facetlist) {
  facetT *facet;
  vertexT *vertex, **vertexp, *vertexlist;
  int numfacets= 0, numvertices= 0, numridges= 0;
  int totvneighbors= 0, totvertices= 0;
  boolT waserror= False, nextseen= False, visibleseen= False;

  trace1((qh, qh->ferr, 1027, "qh_checkpolygon: check all facets from f%d\n", facetlist->id));
  if (facetlist != qh->facet_list || qh->ONLYgood)
    nextseen= True;
  FORALLfacet_(facetlist) {
    if (facet == qh->visible_list)
      visibleseen= True;
    if (!facet->visible) {
      if (!nextseen) {
        if (facet == qh->facet_next)
          nextseen= True;
        else if (qh_setsize(qh, facet->outsideset)) {
          if (!qh->NARROWhull
#if !qh_COMPUTEfurthest
               || facet->furthestdist >= qh->MINoutside
#endif
                        ) {
            qh_fprintf(qh, qh->ferr, 6137, "qhull internal error (qh_checkpolygon): f%d has outside points before qh->facet_next\n",
                     facet->id);
            qh_errexit(qh, qh_ERRqhull, facet, NULL);
          }
        }
      }
      numfacets++;
      qh_checkfacet(qh, facet, False, &waserror);
    }
  }
  if (qh->visible_list && !visibleseen && facetlist == qh->facet_list) {
    qh_fprintf(qh, qh->ferr, 6138, "qhull internal error (qh_checkpolygon): visible list f%d no longer on facet list\n", qh->visible_list->id);
    qh_printlists(qh);
    qh_errexit(qh, qh_ERRqhull, qh->visible_list, NULL);
  }
  if (facetlist == qh->facet_list)
    vertexlist= qh->vertex_list;
  else if (facetlist == qh->newfacet_list)
    vertexlist= qh->newvertex_list;
  else
    vertexlist= NULL;
  FORALLvertex_(vertexlist) {
    vertex->seen= False;
    vertex->visitid= 0;
  }
  FORALLfacet_(facetlist) {
    if (facet->visible)
      continue;
    if (facet->simplicial)
      numridges += qh->hull_dim;
    else
      numridges += qh_setsize(qh, facet->ridges);
    FOREACHvertex_(facet->vertices) {
      vertex->visitid++;
      if (!vertex->seen) {
        vertex->seen= True;
        numvertices++;
        if (qh_pointid(qh, vertex->point) == qh_IDunknown) {
          qh_fprintf(qh, qh->ferr, 6139, "qhull internal error (qh_checkpolygon): unknown point %p for vertex v%d first_point %p\n",
                   vertex->point, vertex->id, qh->first_point);
          waserror= True;
        }
      }
    }
  }
  qh->vertex_visit += (unsigned int)numfacets;
  if (facetlist == qh->facet_list) {
    if (numfacets != qh->num_facets - qh->num_visible) {
      qh_fprintf(qh, qh->ferr, 6140, "qhull internal error (qh_checkpolygon): actual number of facets is %d, cumulative facet count is %d - %d visible facets\n",
              numfacets, qh->num_facets, qh->num_visible);
      waserror= True;
    }
    qh->vertex_visit++;
    if (qh->VERTEXneighbors) {
      FORALLvertices {
        qh_setcheck(qh, vertex->neighbors, "neighbors for v", vertex->id);
        if (vertex->deleted)
          continue;
        totvneighbors += qh_setsize(qh, vertex->neighbors);
      }
      FORALLfacet_(facetlist)
        totvertices += qh_setsize(qh, facet->vertices);
      if (totvneighbors != totvertices) {
        qh_fprintf(qh, qh->ferr, 6141, "qhull internal error (qh_checkpolygon): vertex neighbors inconsistent.  Totvneighbors %d, totvertices %d\n",
                totvneighbors, totvertices);
        waserror= True;
      }
    }
    if (numvertices != qh->num_vertices - qh_setsize(qh, qh->del_vertices)) {
      qh_fprintf(qh, qh->ferr, 6142, "qhull internal error (qh_checkpolygon): actual number of vertices is %d, cumulative vertex count is %d\n",
              numvertices, qh->num_vertices - qh_setsize(qh, qh->del_vertices));
      waserror= True;
    }
    if (qh->hull_dim == 2 && numvertices != numfacets) {
      qh_fprintf(qh, qh->ferr, 6143, "qhull internal error (qh_checkpolygon): #vertices %d != #facets %d\n",
        numvertices, numfacets);
      waserror= True;
    }
    if (qh->hull_dim == 3 && numvertices + numfacets - numridges/2 != 2) {
      qh_fprintf(qh, qh->ferr, 7063, "qhull warning: #vertices %d + #facets %d - #edges %d != 2\n\
        A vertex appears twice in a edge list.  May occur during merging.",
        numvertices, numfacets, numridges/2);

    }
  }
  if (waserror)
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
}












void qh_checkvertex(qhT *qh, vertexT *vertex) {
  boolT waserror= False;
  facetT *neighbor, **neighborp, *errfacet=NULL;

  if (qh_pointid(qh, vertex->point) == qh_IDunknown) {
    qh_fprintf(qh, qh->ferr, 6144, "qhull internal error (qh_checkvertex): unknown point id %p\n", vertex->point);
    waserror= True;
  }
  if (vertex->id >= qh->vertex_id) {
    qh_fprintf(qh, qh->ferr, 6145, "qhull internal error (qh_checkvertex): unknown vertex id %d\n", vertex->id);
    waserror= True;
  }
  if (!waserror && !vertex->deleted) {
    if (qh_setsize(qh, vertex->neighbors)) {
      FOREACHneighbor_(vertex) {
        if (!qh_setin(neighbor->vertices, vertex)) {
          qh_fprintf(qh, qh->ferr, 6146, "qhull internal error (qh_checkvertex): neighbor f%d does not contain v%d\n", neighbor->id, vertex->id);
          errfacet= neighbor;
          waserror= True;
        }
      }
    }
  }
  if (waserror) {
    qh_errprint(qh, "ERRONEOUS", NULL, NULL, NULL, vertex);
    qh_errexit(qh, qh_ERRqhull, errfacet, NULL);
  }
}











void qh_clearcenters(qhT *qh, qh_CENTER type) {
  facetT *facet;

  if (qh->CENTERtype != type) {
    FORALLfacets {
      if (facet->tricoplanar && !facet->keepcentrum)
          facet->center= NULL;
      else if (qh->CENTERtype == qh_ASvoronoi){
        if (facet->center) {
          qh_memfree(qh, facet->center, qh->center_size);
          facet->center= NULL;
        }
      }else   {
        if (facet->center) {
          qh_memfree(qh, facet->center, qh->normal_size);
          facet->center= NULL;
        }
      }
    }
    qh->CENTERtype= type;
  }
  trace2((qh, qh->ferr, 2043, "qh_clearcenters: switched to center type %d\n", type));
}



















void qh_createsimplex(qhT *qh, setT *vertices) {
  facetT *facet= NULL, *newfacet;
  boolT toporient= True;
  int vertex_i, vertex_n, nth;
  setT *newfacets= qh_settemp(qh, qh->hull_dim+1);
  vertexT *vertex;

  qh->facet_list= qh->newfacet_list= qh->facet_tail= qh_newfacet(qh);
  qh->num_facets= qh->num_vertices= qh->num_visible= 0;
  qh->vertex_list= qh->newvertex_list= qh->vertex_tail= qh_newvertex(qh, NULL);
  FOREACHvertex_i_(qh, vertices) {
    newfacet= qh_newfacet(qh);
    newfacet->vertices= qh_setnew_delnthsorted(qh, vertices, vertex_n,
                                                vertex_i, 0);
    newfacet->toporient= (unsigned char)toporient;
    qh_appendfacet(qh, newfacet);
    newfacet->newfacet= True;
    qh_appendvertex(qh, vertex);
    qh_setappend(qh, &newfacets, newfacet);
    toporient ^= True;
  }
  FORALLnew_facets {
    nth= 0;
    FORALLfacet_(qh->newfacet_list) {
      if (facet != newfacet)
        SETelem_(newfacet->neighbors, nth++)= facet;
    }
    qh_settruncate(qh, newfacet->neighbors, qh->hull_dim);
  }
  qh_settempfree(qh, &newfacets);
  trace1((qh, qh->ferr, 1028, "qh_createsimplex: created simplex\n"));
}












void qh_delridge(qhT *qh, ridgeT *ridge) {
  void **freelistp;

  qh_setdel(ridge->top->ridges, ridge);
  qh_setdel(ridge->bottom->ridges, ridge);
  qh_setfree(qh, &(ridge->vertices));
  qh_memfree_(qh, ridge, (int)sizeof(ridgeT), freelistp);
}












void qh_delvertex(qhT *qh, vertexT *vertex) {

  if (vertex == qh->tracevertex)
    qh->tracevertex= NULL;
  qh_removevertex(qh, vertex);
  qh_setfree(qh, &vertex->neighbors);
  qh_memfree(qh, vertex, (int)sizeof(vertexT));
}















setT *qh_facet3vertex(qhT *qh, facetT *facet) {
  ridgeT *ridge, *firstridge;
  vertexT *vertex;
  int cntvertices, cntprojected=0;
  setT *vertices;

  cntvertices= qh_setsize(qh, facet->vertices);
  vertices= qh_settemp(qh, cntvertices);
  if (facet->simplicial) {
    if (cntvertices != 3) {
      qh_fprintf(qh, qh->ferr, 6147, "qhull internal error (qh_facet3vertex): only %d vertices for simplicial facet f%d\n",
                  cntvertices, facet->id);
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
    qh_setappend(qh, &vertices, SETfirst_(facet->vertices));
    if (facet->toporient ^ qh_ORIENTclock)
      qh_setappend(qh, &vertices, SETsecond_(facet->vertices));
    else
      qh_setaddnth(qh, &vertices, 0, SETsecond_(facet->vertices));
    qh_setappend(qh, &vertices, SETelem_(facet->vertices, 2));
  }else {
    ridge= firstridge= SETfirstt_(facet->ridges, ridgeT);
    while ((ridge= qh_nextridge3d(ridge, facet, &vertex))) {
      qh_setappend(qh, &vertices, vertex);
      if (++cntprojected > cntvertices || ridge == firstridge)
        break;
    }
    if (!ridge || cntprojected != cntvertices) {
      qh_fprintf(qh, qh->ferr, 6148, "qhull internal error (qh_facet3vertex): ridges for facet %d don't match up.  got at least %d\n",
                  facet->id, cntprojected);
      qh_errexit(qh, qh_ERRqhull, facet, ridge);
    }
  }
  return vertices;
}



































facetT *qh_findbestfacet(qhT *qh, pointT *point, boolT bestoutside,
           realT *bestdist, boolT *isoutside) {
  facetT *bestfacet= NULL;
  int numpart, totpart= 0;

  bestfacet= qh_findbest(qh, point, qh->facet_list,
                            bestoutside, !qh_ISnewfacets, bestoutside  ,
                            bestdist, isoutside, &totpart);
  if (*bestdist < -qh->DISTround) {
    bestfacet= qh_findfacet_all(qh, point, bestdist, isoutside, &numpart);
    totpart += numpart;
    if ((isoutside && *isoutside && bestoutside)
    || (isoutside && !*isoutside && bestfacet->upperdelaunay)) {
      bestfacet= qh_findbest(qh, point, bestfacet,
                            bestoutside, False, bestoutside,
                            bestdist, isoutside, &totpart);
      totpart += numpart;
    }
  }
  trace3((qh, qh->ferr, 3014, "qh_findbestfacet: f%d dist %2.2g isoutside %d totpart %d\n",
          bestfacet->id, *bestdist, (isoutside ? *isoutside : UINT_MAX), totpart));
  return bestfacet;
}
















facetT *qh_findbestlower(qhT *qh, facetT *upperfacet, pointT *point, realT *bestdistp, int *numpart) {
  facetT *neighbor, **neighborp, *bestfacet= NULL;
  realT bestdist= -REALmax/2  ;
  realT dist;
  vertexT *vertex;
  boolT isoutside= False;

  zinc_(Zbestlower);
  FOREACHneighbor_(upperfacet) {
    if (neighbor->upperdelaunay || neighbor->flipped)
      continue;
    (*numpart)++;
    qh_distplane(qh, point, neighbor, &dist);
    if (dist > bestdist) {
      bestfacet= neighbor;
      bestdist= dist;
    }
  }
  if (!bestfacet) {
    zinc_(Zbestlowerv);

    vertex= qh_nearvertex(qh, upperfacet, point, &dist);
    qh_vertexneighbors(qh);
    FOREACHneighbor_(vertex) {
      if (neighbor->upperdelaunay || neighbor->flipped)
        continue;
      (*numpart)++;
      qh_distplane(qh, point, neighbor, &dist);
      if (dist > bestdist) {
        bestfacet= neighbor;
        bestdist= dist;
      }
    }
  }
  if (!bestfacet) {
    zinc_(Zbestlowerall);
    zmax_(Zbestloweralln, qh->num_facets);

    trace3((qh, qh->ferr, 3025, "qh_findbestlower: all neighbors of facet %d are flipped or upper Delaunay.  Search all facets\n",
       upperfacet->id));

    bestfacet= qh_findfacet_all(qh, point, &bestdist, &isoutside, numpart);
  }
  *bestdistp= bestdist;
  trace3((qh, qh->ferr, 3015, "qh_findbestlower: f%d dist %2.2g for f%d p%d\n",
          bestfacet->id, bestdist, upperfacet->id, qh_pointid(qh, point)));
  return bestfacet;
}






















facetT *qh_findfacet_all(qhT *qh, pointT *point, realT *bestdist, boolT *isoutside,
                          int *numpart) {
  facetT *bestfacet= NULL, *facet;
  realT dist;
  int totpart= 0;

  *bestdist= -REALmax;
  *isoutside= False;
  FORALLfacets {
    if (facet->flipped || !facet->normal)
      continue;
    totpart++;
    qh_distplane(qh, point, facet, &dist);
    if (dist > *bestdist) {
      *bestdist= dist;
      bestfacet= facet;
      if (dist > qh->MINoutside) {
        *isoutside= True;
        break;
      }
    }
  }
  *numpart= totpart;
  trace3((qh, qh->ferr, 3016, "qh_findfacet_all: f%d dist %2.2g isoutside %d totpart %d\n",
          getid_(bestfacet), *bestdist, *isoutside, totpart));
  return bestfacet;
}

































int qh_findgood(qhT *qh, facetT *facetlist, int goodhorizon) {
  facetT *facet, *bestfacet= NULL;
  realT angle, bestangle= REALmax, dist;
  int  numgood=0;

  FORALLfacet_(facetlist) {
    if (facet->good)
      numgood++;
  }
  if (qh->GOODvertex>0 && !qh->MERGING) {
    FORALLfacet_(facetlist) {
      if (!qh_isvertex(qh->GOODvertexp, facet->vertices)) {
        facet->good= False;
        numgood--;
      }
    }
  }
  if (qh->GOODpoint && numgood) {
    FORALLfacet_(facetlist) {
      if (facet->good && facet->normal) {
        zinc_(Zdistgood);
        qh_distplane(qh, qh->GOODpointp, facet, &dist);
        if ((qh->GOODpoint > 0) ^ (dist > 0.0)) {
          facet->good= False;
          numgood--;
        }
      }
    }
  }
  if (qh->GOODthreshold && (numgood || goodhorizon || qh->GOODclosest)) {
    FORALLfacet_(facetlist) {
      if (facet->good && facet->normal) {
        if (!qh_inthresholds(qh, facet->normal, &angle)) {
          facet->good= False;
          numgood--;
          if (angle < bestangle) {
            bestangle= angle;
            bestfacet= facet;
          }
        }
      }
    }
    if (!numgood && (!goodhorizon || qh->GOODclosest)) {
      if (qh->GOODclosest) {
        if (qh->GOODclosest->visible)
          qh->GOODclosest= NULL;
        else {
          qh_inthresholds(qh, qh->GOODclosest->normal, &angle);
          if (angle < bestangle)
            bestfacet= qh->GOODclosest;
        }
      }
      if (bestfacet && bestfacet != qh->GOODclosest) {
        if (qh->GOODclosest)
          qh->GOODclosest->good= False;
        qh->GOODclosest= bestfacet;
        bestfacet->good= True;
        numgood++;
        trace2((qh, qh->ferr, 2044, "qh_findgood: f%d is closest(%2.2g) to thresholds\n",
           bestfacet->id, bestangle));
        return numgood;
      }
    }else if (qh->GOODclosest) {
      qh->GOODclosest->good= False;
      qh->GOODclosest= NULL;
    }
  }
  zadd_(Zgoodfacet, numgood);
  trace2((qh, qh->ferr, 2045, "qh_findgood: found %d good facets with %d good horizon\n",
               numgood, goodhorizon));
  if (!numgood && qh->GOODvertex>0 && !qh->MERGING)
    return goodhorizon;
  return numgood;
}


























void qh_findgood_all(qhT *qh, facetT *facetlist) {
  facetT *facet, *bestfacet=NULL;
  realT angle, bestangle= REALmax;
  int  numgood=0, startgood;

  if (!qh->GOODvertex && !qh->GOODthreshold && !qh->GOODpoint
  && !qh->SPLITthresholds)
    return;
  if (!qh->ONLYgood)
    qh_findgood(qh, qh->facet_list, 0);
  FORALLfacet_(facetlist) {
    if (facet->good)
      numgood++;
  }
  if (qh->GOODvertex <0 || (qh->GOODvertex > 0 && qh->MERGING)) {
    FORALLfacet_(facetlist) {
      if (facet->good && ((qh->GOODvertex > 0) ^ !!qh_isvertex(qh->GOODvertexp, facet->vertices))) {
        if (!--numgood) {
          if (qh->ONLYgood) {
            qh_fprintf(qh, qh->ferr, 7064, "qhull warning: good vertex p%d does not match last good facet f%d.  Ignored.\n",
               qh_pointid(qh, qh->GOODvertexp), facet->id);
            return;
          }else if (qh->GOODvertex > 0)
            qh_fprintf(qh, qh->ferr, 7065, "qhull warning: point p%d is not a vertex('QV%d').\n",
                qh->GOODvertex-1, qh->GOODvertex-1);
          else
            qh_fprintf(qh, qh->ferr, 7066, "qhull warning: point p%d is a vertex for every facet('QV-%d').\n",
                -qh->GOODvertex - 1, -qh->GOODvertex - 1);
        }
        facet->good= False;
      }
    }
  }
  startgood= numgood;
  if (qh->SPLITthresholds) {
    FORALLfacet_(facetlist) {
      if (facet->good) {
        if (!qh_inthresholds(qh, facet->normal, &angle)) {
          facet->good= False;
          numgood--;
          if (angle < bestangle) {
            bestangle= angle;
            bestfacet= facet;
          }
        }
      }
    }
    if (!numgood && bestfacet) {
      bestfacet->good= True;
      numgood++;
      trace0((qh, qh->ferr, 23, "qh_findgood_all: f%d is closest(%2.2g) to thresholds\n",
           bestfacet->id, bestangle));
      return;
    }
  }
  qh->num_good= numgood;
  trace0((qh, qh->ferr, 24, "qh_findgood_all: %d good facets remain out of %d facets\n",
        numgood, startgood));
}











void qh_furthestnext(qhT *qh  ) {
  facetT *facet, *bestfacet= NULL;
  realT dist, bestdist= -REALmax;

  FORALLfacets {
    if (facet->outsideset) {
#if qh_COMPUTEfurthest
      pointT *furthest;
      furthest= (pointT*)qh_setlast(facet->outsideset);
      zinc_(Zcomputefurthest);
      qh_distplane(qh, furthest, facet, &dist);
#else
      dist= facet->furthestdist;
#endif
      if (dist > bestdist) {
        bestfacet= facet;
        bestdist= dist;
      }
    }
  }
  if (bestfacet) {
    qh_removefacet(qh, bestfacet);
    qh_prependfacet(qh, bestfacet, &qh->facet_next);
    trace1((qh, qh->ferr, 1029, "qh_furthestnext: made f%d next facet(dist %.2g)\n",
            bestfacet->id, bestdist));
  }
}
















void qh_furthestout(qhT *qh, facetT *facet) {
  pointT *point, **pointp, *bestpoint= NULL;
  realT dist, bestdist= -REALmax;

  FOREACHpoint_(facet->outsideset) {
    qh_distplane(qh, point, facet, &dist);
    zinc_(Zcomputefurthest);
    if (dist > bestdist) {
      bestpoint= point;
      bestdist= dist;
    }
  }
  if (bestpoint) {
    qh_setdel(facet->outsideset, point);
    qh_setappend(qh, &facet->outsideset, point);
#if !qh_COMPUTEfurthest
    facet->furthestdist= bestdist;
#endif
  }
  facet->notfurthest= False;
  trace3((qh, qh->ferr, 3017, "qh_furthestout: p%d is furthest outside point of f%d\n",
          qh_pointid(qh, point), facet->id));
}








void qh_infiniteloop(qhT *qh, facetT *facet) {

  qh_fprintf(qh, qh->ferr, 6149, "qhull internal error (qh_infiniteloop): potential infinite loop detected\n");
  qh_errexit(qh, qh_ERRqhull, facet, NULL);
}


























void qh_initbuild(qhT *qh) {
  setT *maxpoints, *vertices;
  facetT *facet;
  int i, numpart;
  realT dist;
  boolT isoutside;

  qh->furthest_id= qh_IDunknown;
  qh->lastreport= 0;
  qh->facet_id= qh->vertex_id= qh->ridge_id= 0;
  qh->visit_id= qh->vertex_visit= 0;
  qh->maxoutdone= False;

  if (qh->GOODpoint > 0)
    qh->GOODpointp= qh_point(qh, qh->GOODpoint-1);
  else if (qh->GOODpoint < 0)
    qh->GOODpointp= qh_point(qh, -qh->GOODpoint-1);
  if (qh->GOODvertex > 0)
    qh->GOODvertexp= qh_point(qh, qh->GOODvertex-1);
  else if (qh->GOODvertex < 0)
    qh->GOODvertexp= qh_point(qh, -qh->GOODvertex-1);
  if ((qh->GOODpoint
       && (qh->GOODpointp < qh->first_point
           || qh->GOODpointp > qh_point(qh, qh->num_points-1)))
    || (qh->GOODvertex
        && (qh->GOODvertexp < qh->first_point
            || qh->GOODvertexp > qh_point(qh, qh->num_points-1)))) {
    qh_fprintf(qh, qh->ferr, 6150, "qhull input error: either QGn or QVn point is > p%d\n",
             qh->num_points-1);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  maxpoints= qh_maxmin(qh, qh->first_point, qh->num_points, qh->hull_dim);
  if (qh->SCALElast)
    qh_scalelast(qh, qh->first_point, qh->num_points, qh->hull_dim,
               qh->MINlastcoord, qh->MAXlastcoord, qh->MAXwidth);
  qh_detroundoff(qh);
  if (qh->DELAUNAY && qh->upper_threshold[qh->hull_dim-1] > REALmax/2
                  && qh->lower_threshold[qh->hull_dim-1] < -REALmax/2) {
    for (i=qh_PRINTEND; i--; ) {
      if (qh->PRINTout[i] == qh_PRINTgeom && qh->DROPdim < 0
          && !qh->GOODthreshold && !qh->SPLITthresholds)
        break;
    }
    if (i < 0) {
      if (qh->UPPERdelaunay) {
        qh->lower_threshold[qh->hull_dim-1]= qh->ANGLEround * qh_ZEROdelaunay;
        qh->GOODthreshold= True;
      }else {
        qh->upper_threshold[qh->hull_dim-1]= -qh->ANGLEround * qh_ZEROdelaunay;
        if (!qh->GOODthreshold)
          qh->SPLITthresholds= True;

      }
    }
  }
  vertices= qh_initialvertices(qh, qh->hull_dim, maxpoints, qh->first_point, qh->num_points);
  qh_initialhull(qh, vertices);
  qh_partitionall(qh, vertices, qh->first_point, qh->num_points);
  if (qh->PRINToptions1st || qh->TRACElevel || qh->IStracing) {
    if (qh->TRACElevel || qh->IStracing)
      qh_fprintf(qh, qh->ferr, 8103, "\nTrace level %d for %s | %s\n",
         qh->IStracing ? qh->IStracing : qh->TRACElevel, qh->rbox_command, qh->qhull_command);
    qh_fprintf(qh, qh->ferr, 8104, "Options selected for Qhull %s:\n%s\n", qh_version, qh->qhull_options);
  }
  qh_resetlists(qh, False, qh_RESETvisible  );
  qh->facet_next= qh->facet_list;
  qh_furthestnext(qh  );
  if (qh->PREmerge) {
    qh->cos_max= qh->premerge_cos;
    qh->centrum_radius= qh->premerge_centrum;
  }
  if (qh->ONLYgood) {
    if (qh->GOODvertex > 0 && qh->MERGING) {
      qh_fprintf(qh, qh->ferr, 6151, "qhull input error: 'Qg QVn' (only good vertex) does not work with merging.\nUse 'QJ' to joggle the input or 'Q0' to turn off merging.\n");
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    if (!(qh->GOODthreshold || qh->GOODpoint
         || (!qh->MERGEexact && !qh->PREmerge && qh->GOODvertexp))) {
      qh_fprintf(qh, qh->ferr, 6152, "qhull input error: 'Qg' (ONLYgood) needs a good threshold('Pd0D0'), a\n\
good point(QGn or QG-n), or a good vertex with 'QJ' or 'Q0' (QVn).\n");
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    if (qh->GOODvertex > 0  && !qh->MERGING
        && !qh_isvertex(qh->GOODvertexp, vertices)) {
      facet= qh_findbestnew(qh, qh->GOODvertexp, qh->facet_list,
                          &dist, !qh_ALL, &isoutside, &numpart);
      zadd_(Zdistgood, numpart);
      if (!isoutside) {
        qh_fprintf(qh, qh->ferr, 6153, "qhull input error: point for QV%d is inside initial simplex.  It can not be made a vertex.\n",
               qh_pointid(qh, qh->GOODvertexp));
        qh_errexit(qh, qh_ERRinput, NULL, NULL);
      }
      if (!qh_addpoint(qh, qh->GOODvertexp, facet, False)) {
        qh_settempfree(qh, &vertices);
        qh_settempfree(qh, &maxpoints);
        return;
      }
    }
    qh_findgood(qh, qh->facet_list, 0);
  }
  qh_settempfree(qh, &vertices);
  qh_settempfree(qh, &maxpoints);
  trace1((qh, qh->ferr, 1030, "qh_initbuild: initial hull created and points partitioned\n"));
}















void qh_initialhull(qhT *qh, setT *vertices) {
  facetT *facet, *firstfacet, *neighbor, **neighborp;
  realT dist, angle, minangle= REALmax;
#ifndef qh_NOtrace
  int k;
#endif

  qh_createsimplex(qh, vertices);
  qh_resetlists(qh, False, qh_RESETvisible);
  qh->facet_next= qh->facet_list;
  qh->interior_point= qh_getcenter(qh, vertices);
  firstfacet= qh->facet_list;
  qh_setfacetplane(qh, firstfacet);
  zinc_(Znumvisibility);
  qh_distplane(qh, qh->interior_point, firstfacet, &dist);
  if (dist > 0) {
    FORALLfacets
      facet->toporient ^= (unsigned char)True;
  }
  FORALLfacets
    qh_setfacetplane(qh, facet);
  FORALLfacets {
    if (!qh_checkflipped(qh, facet, NULL, qh_ALL)) {
      trace1((qh, qh->ferr, 1031, "qh_initialhull: initial orientation incorrect.  Correct all facets\n"));
      facet->flipped= False;
      FORALLfacets {
        facet->toporient ^= (unsigned char)True;
        qh_orientoutside(qh, facet);
      }
      break;
    }
  }
  FORALLfacets {
    if (!qh_checkflipped(qh, facet, NULL, !qh_ALL)) {
      if (qh->DELAUNAY && ! qh->ATinfinity) {
        if (qh->UPPERdelaunay)
          qh_fprintf(qh, qh->ferr, 6240, "Qhull precision error: Initial simplex is cocircular or cospherical.  Option 'Qs' searches all points.  Can not compute the upper Delaunay triangulation or upper Voronoi diagram of cocircular/cospherical points.\n");
        else
          qh_fprintf(qh, qh->ferr, 6239, "Qhull precision error: Initial simplex is cocircular or cospherical.  Use option 'Qz' for the Delaunay triangulation or Voronoi diagram of cocircular/cospherical points.  Option 'Qz' adds a point \"at infinity\".  Use option 'Qs' to search all points for the initial simplex.\n");
        qh_errexit(qh, qh_ERRinput, NULL, NULL);
      }
      qh_precision(qh, "initial simplex is flat");
      qh_fprintf(qh, qh->ferr, 6154, "Qhull precision error: Initial simplex is flat (facet %d is coplanar with the interior point)\n",
                   facet->id);
      qh_errexit(qh, qh_ERRsingular, NULL, NULL);
    }
    FOREACHneighbor_(facet) {
      angle= qh_getangle(qh, facet->normal, neighbor->normal);
      minimize_( minangle, angle);
    }
  }
  if (minangle < qh_MAXnarrow && !qh->NOnarrow) {
    realT diff= 1.0 + minangle;

    qh->NARROWhull= True;
    qh_option(qh, "_narrow-hull", NULL, &diff);
    if (minangle < qh_WARNnarrow && !qh->RERUN && qh->PRINTprecision)
      qh_printhelp_narrowhull(qh, qh->ferr, minangle);
  }
  zzval_(Zprocessed)= qh->hull_dim+1;
  qh_checkpolygon(qh, qh->facet_list);
  qh_checkconvex(qh, qh->facet_list,   qh_DATAfault);
#ifndef qh_NOtrace
  if (qh->IStracing >= 1) {
    qh_fprintf(qh, qh->ferr, 8105, "qh_initialhull: simplex constructed, interior point:");
    for (k=0; k < qh->hull_dim; k++)
      qh_fprintf(qh, qh->ferr, 8106, " %6.4g", qh->interior_point[k]);
    qh_fprintf(qh, qh->ferr, 8107, "\n");
  }
#endif
}



















setT *qh_initialvertices(qhT *qh, int dim, setT *maxpoints, pointT *points, int numpoints) {
  pointT *point, **pointp;
  setT *vertices, *simplex, *tested;
  realT randr;
  int idx, point_i, point_n, k;
  boolT nearzero= False;

  vertices= qh_settemp(qh, dim + 1);
  simplex= qh_settemp(qh, dim+1);
  if (qh->ALLpoints)
    qh_maxsimplex(qh, dim, NULL, points, numpoints, &simplex);
  else if (qh->RANDOMoutside) {
    while (qh_setsize(qh, simplex) != dim+1) {
      randr= qh_RANDOMint;
      randr= randr/(qh_RANDOMmax+1);
      idx= (int)floor(qh->num_points * randr);
      while (qh_setin(simplex, qh_point(qh, idx))) {
            idx++;
        idx= idx < qh->num_points ? idx : 0;
      }
      qh_setappend(qh, &simplex, qh_point(qh, idx));
    }
  }else if (qh->hull_dim >= qh_INITIALmax) {
    tested= qh_settemp(qh, dim+1);
    qh_setappend(qh, &simplex, SETfirst_(maxpoints));
    qh_setappend(qh, &simplex, SETsecond_(maxpoints));
    qh_maxsimplex(qh, fmin_(qh_INITIALsearch, dim), maxpoints, points, numpoints, &simplex);
    k= qh_setsize(qh, simplex);
    FOREACHpoint_i_(qh, maxpoints) {
      if (point_i & 0x1) {
        if (!qh_setin(simplex, point) && !qh_setin(tested, point)){
          qh_detsimplex(qh, point, simplex, k, &nearzero);
          if (nearzero)
            qh_setappend(qh, &tested, point);
          else {
            qh_setappend(qh, &simplex, point);
            if (++k == dim)
              break;
          }
        }
      }
    }
    while (k != dim && (point= (pointT*)qh_setdellast(maxpoints))) {
      if (!qh_setin(simplex, point) && !qh_setin(tested, point)){
        qh_detsimplex(qh, point, simplex, k, &nearzero);
        if (nearzero)
          qh_setappend(qh, &tested, point);
        else {
          qh_setappend(qh, &simplex, point);
          k++;
        }
      }
    }
    idx= 0;
    while (k != dim && (point= qh_point(qh, idx++))) {
      if (!qh_setin(simplex, point) && !qh_setin(tested, point)){
        qh_detsimplex(qh, point, simplex, k, &nearzero);
        if (!nearzero){
          qh_setappend(qh, &simplex, point);
          k++;
        }
      }
    }
    qh_settempfree(qh, &tested);
    qh_maxsimplex(qh, dim, maxpoints, points, numpoints, &simplex);
  }else
    qh_maxsimplex(qh, dim, maxpoints, points, numpoints, &simplex);
  FOREACHpoint_(simplex)
    qh_setaddnth(qh, &vertices, 0, qh_newvertex(qh, point));
  qh_settempfree(qh, &simplex);
  return vertices;
}











vertexT *qh_isvertex(pointT *point, setT *vertices) {
  vertexT *vertex, **vertexp;

  FOREACHvertex_(vertices) {
    if (vertex->point == point)
      return vertex;
  }
  return NULL;
}




































vertexT *qh_makenewfacets(qhT *qh, pointT *point  ) {
  facetT *visible, *newfacet= NULL, *newfacet2= NULL, *neighbor, **neighborp;
  vertexT *apex;
  int numnew=0;

  qh->newfacet_list= qh->facet_tail;
  qh->newvertex_list= qh->vertex_tail;
  apex= qh_newvertex(qh, point);
  qh_appendvertex(qh, apex);
  qh->visit_id++;
  if (!qh->ONLYgood)
    qh->NEWfacets= True;
  FORALLvisible_facets {
    FOREACHneighbor_(visible)
      neighbor->seen= False;
    if (visible->ridges) {
      visible->visitid= qh->visit_id;
      newfacet2= qh_makenew_nonsimplicial(qh, visible, apex, &numnew);
    }
    if (visible->simplicial)
      newfacet= qh_makenew_simplicial(qh, visible, apex, &numnew);
    if (!qh->ONLYgood) {
      if (newfacet2)
        newfacet= newfacet2;
      if (newfacet)
        visible->f.replace= newfacet;
      else
        zinc_(Zinsidevisible);
      SETfirst_(visible->neighbors)= NULL;
    }
  }
  trace1((qh, qh->ferr, 1032, "qh_makenewfacets: created %d new facets from point p%d to horizon\n",
          numnew, qh_pointid(qh, point)));
  if (qh->IStracing >= 4)
    qh_printfacetlist(qh, qh->newfacet_list, NULL, qh_ALL);
  return apex;
}






























#ifndef qh_NOmerge
void qh_matchduplicates(qhT *qh, facetT *atfacet, int atskip, int hashsize, int *hashcount) {
  boolT same, ismatch;
  int hash, scan;
  facetT *facet, *newfacet, *maxmatch= NULL, *maxmatch2= NULL, *nextfacet;
  int skip, newskip, nextskip= 0, maxskip= 0, maxskip2= 0, makematch;
  realT maxdist= -REALmax, mindist, dist2, low, high;

  hash= qh_gethash(qh, hashsize, atfacet->vertices, qh->hull_dim, 1,
                     SETelem_(atfacet->vertices, atskip));
  trace2((qh, qh->ferr, 2046, "qh_matchduplicates: find duplicate matches for f%d skip %d hash %d hashcount %d\n",
          atfacet->id, atskip, hash, *hashcount));
  for (makematch= 0; makematch < 2; makematch++) {
    qh->visit_id++;
    for (newfacet= atfacet, newskip= atskip; newfacet; newfacet= nextfacet, newskip= nextskip) {
      zinc_(Zhashlookup);
      nextfacet= NULL;
      newfacet->visitid= qh->visit_id;
      for (scan= hash; (facet= SETelemt_(qh->hash_table, scan, facetT));
           scan= (++scan >= hashsize ? 0 : scan)) {
        if (!facet->dupridge || facet->visitid == qh->visit_id)
          continue;
        zinc_(Zhashtests);
        if (qh_matchvertices(qh, 1, newfacet->vertices, newskip, facet->vertices, &skip, &same)) {
          ismatch= (same == (boolT)(newfacet->toporient ^ facet->toporient));
          if (SETelemt_(facet->neighbors, skip, facetT) != qh_DUPLICATEridge) {
            if (!makematch) {
              qh_fprintf(qh, qh->ferr, 6155, "qhull internal error (qh_matchduplicates): missing dupridge at f%d skip %d for new f%d skip %d hash %d\n",
                     facet->id, skip, newfacet->id, newskip, hash);
              qh_errexit2(qh, qh_ERRqhull, facet, newfacet);
            }
          }else if (ismatch && makematch) {
            if (SETelemt_(newfacet->neighbors, newskip, facetT) == qh_DUPLICATEridge) {
              SETelem_(facet->neighbors, skip)= newfacet;
              if (newfacet->tricoplanar)
                SETelem_(newfacet->neighbors, newskip)= facet;
              else
                SETelem_(newfacet->neighbors, newskip)= qh_MERGEridge;
              *hashcount -= 2;
              trace4((qh, qh->ferr, 4059, "qh_matchduplicates: duplicate f%d skip %d matched with new f%d skip %d merge\n",
                    facet->id, skip, newfacet->id, newskip));
            }
          }else if (ismatch) {
            mindist= qh_getdistance(qh, facet, newfacet, &low, &high);
            dist2= qh_getdistance(qh, newfacet, facet, &low, &high);
            minimize_(mindist, dist2);
            if (mindist > maxdist) {
              maxdist= mindist;
              maxmatch= facet;
              maxskip= skip;
              maxmatch2= newfacet;
              maxskip2= newskip;
            }
            trace3((qh, qh->ferr, 3018, "qh_matchduplicates: duplicate f%d skip %d new f%d skip %d at dist %2.2g, max is now f%d f%d\n",
                    facet->id, skip, newfacet->id, newskip, mindist,
                    maxmatch->id, maxmatch2->id));
          }else {
            nextfacet= facet;
            nextskip= skip;
          }
        }
        if (makematch && !facet
        && SETelemt_(facet->neighbors, skip, facetT) == qh_DUPLICATEridge) {
          qh_fprintf(qh, qh->ferr, 6156, "qhull internal error (qh_matchduplicates): no MERGEridge match for duplicate f%d skip %d at hash %d\n",
                     newfacet->id, newskip, hash);
          qh_errexit(qh, qh_ERRqhull, newfacet, NULL);
        }
      }
    }
    if (!makematch) {
      if (!maxmatch) {
        qh_fprintf(qh, qh->ferr, 6157, "qhull internal error (qh_matchduplicates): no maximum match at duplicate f%d skip %d at hash %d\n",
                     atfacet->id, atskip, hash);
        qh_errexit(qh, qh_ERRqhull, atfacet, NULL);
      }
      SETelem_(maxmatch->neighbors, maxskip)= maxmatch2;
      SETelem_(maxmatch2->neighbors, maxskip2)= maxmatch;
      *hashcount -= 2;
      zzinc_(Zmultiridge);
      trace0((qh, qh->ferr, 25, "qh_matchduplicates: duplicate f%d skip %d matched with new f%d skip %d keep\n",
              maxmatch->id, maxskip, maxmatch2->id, maxskip2));
      qh_precision(qh, "ridge with multiple neighbors");
      if (qh->IStracing >= 4)
        qh_errprint(qh, "DUPLICATED/MATCH", maxmatch, maxmatch2, NULL, NULL);
    }
  }
}
























void qh_nearcoplanar(qhT *qh  ) {
  facetT *facet;
  pointT *point, **pointp;
  int numpart;
  realT dist, innerplane;

  if (!qh->KEEPcoplanar && !qh->KEEPinside) {
    FORALLfacets {
      if (facet->coplanarset)
        qh_setfree(qh, &facet->coplanarset);
    }
  }else if (!qh->KEEPcoplanar || !qh->KEEPinside) {
    qh_outerinner(qh, NULL, NULL, &innerplane);
    if (qh->JOGGLEmax < REALmax/2)
      innerplane -= qh->JOGGLEmax * sqrt((realT)qh->hull_dim);
    numpart= 0;
    FORALLfacets {
      if (facet->coplanarset) {
        FOREACHpoint_(facet->coplanarset) {
          numpart++;
          qh_distplane(qh, point, facet, &dist);
          if (dist < innerplane) {
            if (!qh->KEEPinside)
              SETref_(point)= NULL;
          }else if (!qh->KEEPcoplanar)
            SETref_(point)= NULL;
        }
        qh_setcompact(qh, facet->coplanarset);
      }
    }
    zzadd_(Zcheckpart, numpart);
  }
}

















vertexT *qh_nearvertex(qhT *qh, facetT *facet, pointT *point, realT *bestdistp) {
  realT bestdist= REALmax, dist;
  vertexT *bestvertex= NULL, *vertex, **vertexp, *apex;
  coordT *center;
  facetT *neighbor, **neighborp;
  setT *vertices;
  int dim= qh->hull_dim;

  if (qh->DELAUNAY)
    dim--;
  if (facet->tricoplanar) {
    if (!qh->VERTEXneighbors || !facet->center) {
      qh_fprintf(qh, qh->ferr, 6158, "qhull internal error (qh_nearvertex): qh.VERTEXneighbors and facet->center required for tricoplanar facets\n");
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
    vertices= qh_settemp(qh, qh->TEMPsize);
    apex= SETfirstt_(facet->vertices, vertexT);
    center= facet->center;
    FOREACHneighbor_(apex) {
      if (neighbor->center == center) {
        FOREACHvertex_(neighbor->vertices)
          qh_setappend(qh, &vertices, vertex);
      }
    }
  }else
    vertices= facet->vertices;
  FOREACHvertex_(vertices) {
    dist= qh_pointdist(vertex->point, point, -dim);
    if (dist < bestdist) {
      bestdist= dist;
      bestvertex= vertex;
    }
  }
  if (facet->tricoplanar)
    qh_settempfree(qh, &vertices);
  *bestdistp= sqrt(bestdist);
  if (!bestvertex) {
      qh_fprintf(qh, qh->ferr, 6261, "qhull internal error (qh_nearvertex): did not find bestvertex for f%d p%d\n", facet->id, qh_pointid(qh, point));
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
  }
  trace3((qh, qh->ferr, 3019, "qh_nearvertex: v%d dist %2.2g for f%d p%d\n",
        bestvertex->id, *bestdistp, facet->id, qh_pointid(qh, point)));
  return bestvertex;
}












int qh_newhashtable(qhT *qh, int newsize) {
  int size;

  size= ((newsize+1)*qh_HASHfactor) | 0x1;
  while (True) {
    if (newsize<0 || size<0) {
        qh_fprintf(qh, qh->qhmem.ferr, 6236, "qhull error (qh_newhashtable): negative request (%d) or size (%d).  Did int overflow due to high-D?\n", newsize, size);
        qh_errexit(qh, qhmem_ERRmem, NULL, NULL);
    }
    if ((size%3) && (size%5))
      break;
    size += 2;

  }
  qh->hash_table= qh_setnew(qh, size);
  qh_setzero(qh, qh->hash_table, 0, size);
  return size;
}







vertexT *qh_newvertex(qhT *qh, pointT *point) {
  vertexT *vertex;

  zinc_(Ztotvertices);
  vertex= (vertexT *)qh_memalloc(qh, (int)sizeof(vertexT));
  memset((char *) vertex, (size_t)0, sizeof(vertexT));
  if (qh->vertex_id == UINT_MAX) {
    qh_memfree(qh, vertex, (int)sizeof(vertexT));
    qh_fprintf(qh, qh->ferr, 6159, "qhull error: more than 2^32 vertices.  vertexT.id field overflows.  Vertices would not be sorted correctly.\n");
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  if (qh->vertex_id == qh->tracevertex_id)
    qh->tracevertex= vertex;
  vertex->id= qh->vertex_id++;
  vertex->point= point;
  trace4((qh, qh->ferr, 4060, "qh_newvertex: vertex p%d(v%d) created\n", qh_pointid(qh, vertex->point),
          vertex->id));
  return(vertex);
}




















ridgeT *qh_nextridge3d(ridgeT *atridge, facetT *facet, vertexT **vertexp) {
  vertexT *atvertex, *vertex, *othervertex;
  ridgeT *ridge, **ridgep;

  if ((atridge->top == facet) ^ qh_ORIENTclock)
    atvertex= SETsecondt_(atridge->vertices, vertexT);
  else
    atvertex= SETfirstt_(atridge->vertices, vertexT);
  FOREACHridge_(facet->ridges) {
    if (ridge == atridge)
      continue;
    if ((ridge->top == facet) ^ qh_ORIENTclock) {
      othervertex= SETsecondt_(ridge->vertices, vertexT);
      vertex= SETfirstt_(ridge->vertices, vertexT);
    }else {
      vertex= SETsecondt_(ridge->vertices, vertexT);
      othervertex= SETfirstt_(ridge->vertices, vertexT);
    }
    if (vertex == atvertex) {
      if (vertexp)
        *vertexp= othervertex;
      return ridge;
    }
  }
  return NULL;
}
#else
void qh_matchduplicates(qhT *qh, facetT *atfacet, int atskip, int hashsize, int *hashcount) {
}
ridgeT *qh_nextridge3d(ridgeT *atridge, facetT *facet, vertexT **vertexp) {

  return NULL;
}
#endif















void qh_outcoplanar(qhT *qh  ) {
  pointT *point, **pointp;
  facetT *facet;
  realT dist;

  trace1((qh, qh->ferr, 1033, "qh_outcoplanar: move outsideset to coplanarset for qh->NARROWhull\n"));
  FORALLfacets {
    FOREACHpoint_(facet->outsideset) {
      qh->num_outside--;
      if (qh->KEEPcoplanar || qh->KEEPnearinside) {
        qh_distplane(qh, point, facet, &dist);
        zinc_(Zpartition);
        qh_partitioncoplanar(qh, point, facet, &dist);
      }
    }
    qh_setfree(qh, &facet->outsideset);
  }
}











pointT *qh_point(qhT *qh, int id) {

  if (id < 0)
    return NULL;
  if (id < qh->num_points)
    return qh->first_point + id * qh->hull_dim;
  id -= qh->num_points;
  if (id < qh_setsize(qh, qh->other_points))
    return SETelemt_(qh->other_points, id, pointT);
  return NULL;
}













void qh_point_add(qhT *qh, setT *set, pointT *point, void *elem) {
  int id, size;

  SETreturnsize_(set, size);
  if ((id= qh_pointid(qh, point)) < 0)
    qh_fprintf(qh, qh->ferr, 7067, "qhull internal warning (point_add): unknown point %p id %d\n",
      point, id);
  else if (id >= size) {
    qh_fprintf(qh, qh->ferr, 6160, "qhull internal errror(point_add): point p%d is out of bounds(%d)\n",
             id, size);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }else
    SETelem_(set, id)= elem;
}


























setT *qh_pointfacet(qhT *qh  ) {
  int numpoints= qh->num_points + qh_setsize(qh, qh->other_points);
  setT *facets;
  facetT *facet;
  vertexT *vertex, **vertexp;
  pointT *point, **pointp;

  facets= qh_settemp(qh, numpoints);
  qh_setzero(qh, facets, 0, numpoints);
  qh->vertex_visit++;
  FORALLfacets {
    FOREACHvertex_(facet->vertices) {
      if (vertex->visitid != qh->vertex_visit) {
        vertex->visitid= qh->vertex_visit;
        qh_point_add(qh, facets, vertex->point, facet);
      }
    }
    FOREACHpoint_(facet->coplanarset)
      qh_point_add(qh, facets, point, facet);
    FOREACHpoint_(facet->outsideset)
      qh_point_add(qh, facets, point, facet);
  }
  return facets;
}













setT *qh_pointvertex(qhT *qh  ) {
  int numpoints= qh->num_points + qh_setsize(qh, qh->other_points);
  setT *vertices;
  vertexT *vertex;

  vertices= qh_settemp(qh, numpoints);
  qh_setzero(qh, vertices, 0, numpoints);
  FORALLvertices
    qh_point_add(qh, vertices, vertex->point, vertex);
  return vertices;
}
















void qh_prependfacet(qhT *qh, facetT *facet, facetT **facetlist) {
  facetT *prevfacet, *list;


  trace4((qh, qh->ferr, 4061, "qh_prependfacet: prepend f%d before f%d\n",
          facet->id, getid_(*facetlist)));
  if (!*facetlist)
    (*facetlist)= qh->facet_tail;
  list= *facetlist;
  prevfacet= list->previous;
  facet->previous= prevfacet;
  if (prevfacet)
    prevfacet->next= facet;
  list->previous= facet;
  facet->next= *facetlist;
  if (qh->facet_list == list)
    qh->facet_list= facet;
  if (qh->facet_next == list)
    qh->facet_next= facet;
  *facetlist= facet;
  qh->num_facets++;
}

















void qh_printhashtable(qhT *qh, FILE *fp) {
  facetT *facet, *neighbor;
  int id, facet_i, facet_n, neighbor_i= 0, neighbor_n= 0;
  vertexT *vertex, **vertexp;

  FOREACHfacet_i_(qh, qh->hash_table) {
    if (facet) {
      FOREACHneighbor_i_(qh, facet) {
        if (!neighbor || neighbor == qh_MERGEridge || neighbor == qh_DUPLICATEridge)
          break;
      }
      if (neighbor_i == neighbor_n)
        continue;
      qh_fprintf(qh, fp, 9283, "hash %d f%d ", facet_i, facet->id);
      FOREACHvertex_(facet->vertices)
        qh_fprintf(qh, fp, 9284, "v%d ", vertex->id);
      qh_fprintf(qh, fp, 9285, "\n neighbors:");
      FOREACHneighbor_i_(qh, facet) {
        if (neighbor == qh_MERGEridge)
          id= -3;
        else if (neighbor == qh_DUPLICATEridge)
          id= -2;
        else
          id= getid_(neighbor);
        qh_fprintf(qh, fp, 9286, " %d", id);
      }
      qh_fprintf(qh, fp, 9287, "\n");
    }
  }
}








void qh_printlists(qhT *qh) {
  facetT *facet;
  vertexT *vertex;
  int count= 0;

  qh_fprintf(qh, qh->ferr, 8108, "qh_printlists: facets:");
  FORALLfacets {
    if (++count % 100 == 0)
      qh_fprintf(qh, qh->ferr, 8109, "\n     ");
    qh_fprintf(qh, qh->ferr, 8110, " %d", facet->id);
  }
  qh_fprintf(qh, qh->ferr, 8111, "\n  new facets %d visible facets %d next facet for qh_addpoint %d\n  vertices(new %d):",
     getid_(qh->newfacet_list), getid_(qh->visible_list), getid_(qh->facet_next),
     getid_(qh->newvertex_list));
  count = 0;
  FORALLvertices {
    if (++count % 100 == 0)
      qh_fprintf(qh, qh->ferr, 8112, "\n     ");
    qh_fprintf(qh, qh->ferr, 8113, " %d", vertex->id);
  }
  qh_fprintf(qh, qh->ferr, 8114, "\n");
}












void qh_resetlists(qhT *qh, boolT stats, boolT resetVisible  ) {
  vertexT *vertex;
  facetT *newfacet, *visible;
  int totnew=0, totver=0;

  if (stats) {
    FORALLvertex_(qh->newvertex_list)
      totver++;
    FORALLnew_facets
      totnew++;
    zadd_(Zvisvertextot, totver);
    zmax_(Zvisvertexmax, totver);
    zadd_(Znewfacettot, totnew);
    zmax_(Znewfacetmax, totnew);
  }
  FORALLvertex_(qh->newvertex_list)
    vertex->newlist= False;
  qh->newvertex_list= NULL;
  FORALLnew_facets
    newfacet->newfacet= False;
  qh->newfacet_list= NULL;
  if (resetVisible) {
    FORALLvisible_facets {
      visible->f.replace= NULL;
      visible->visible= False;
    }
    qh->num_visible= 0;
  }
  qh->visible_list= NULL;
  qh->NEWfacets= False;
}



















void qh_setvoronoi_all(qhT *qh) {
  facetT *facet;

  qh_clearcenters(qh, qh_ASvoronoi);
  qh_vertexneighbors(qh);

  FORALLfacets {
    if (!facet->normal || !facet->upperdelaunay || qh->UPPERdelaunay) {
      if (!facet->center)
        facet->center= qh_facetcenter(qh, facet->vertices);
    }
  }
}

#ifndef qh_NOmerge

















void qh_triangulate(qhT *qh  ) {
  facetT *facet, *nextfacet, *owner;
  int onlygood= qh->ONLYgood;
  facetT *neighbor, *visible= NULL, *facet1, *facet2, *new_facet_list= NULL;
  facetT *orig_neighbor= NULL, *otherfacet;
  vertexT *new_vertex_list= NULL;
  mergeT *merge;
  mergeType mergetype;
  int neighbor_i, neighbor_n;

  if (qh->hasTriangulation)
      return;
  trace1((qh, qh->ferr, 1034, "qh_triangulate: triangulate non-simplicial facets\n"));
  if (qh->hull_dim == 2)
    return;
  if (qh->VORONOI) {
    qh_clearcenters(qh, qh_ASvoronoi);
    qh_vertexneighbors(qh);
  }
  qh->ONLYgood= False;
  qh->visit_id++;
  qh->NEWfacets= True;
  qh->degen_mergeset= qh_settemp(qh, qh->TEMPsize);
  qh->newvertex_list= qh->vertex_tail;
  for (facet= qh->facet_list; facet && facet->next; facet= nextfacet) {
    nextfacet= facet->next;
    if (facet->visible || facet->simplicial)
      continue;

    if (!new_facet_list)
      new_facet_list= facet;
    qh_triangulate_facet(qh, facet, &new_vertex_list);
  }
  trace2((qh, qh->ferr, 2047, "qh_triangulate: delete null facets from f%d -- apex same as second vertex\n", getid_(new_facet_list)));
  for (facet= new_facet_list; facet && facet->next; facet= nextfacet) {
    nextfacet= facet->next;
    if (facet->visible)
      continue;
    if (facet->ridges) {
      if (qh_setsize(qh, facet->ridges) > 0) {
        qh_fprintf(qh, qh->ferr, 6161, "qhull error (qh_triangulate): ridges still defined for f%d\n", facet->id);
        qh_errexit(qh, qh_ERRqhull, facet, NULL);
      }
      qh_setfree(qh, &facet->ridges);
    }
    if (SETfirst_(facet->vertices) == SETsecond_(facet->vertices)) {
      zinc_(Ztrinull);
      qh_triangulate_null(qh, facet);
    }
  }
  trace2((qh, qh->ferr, 2048, "qh_triangulate: delete %d or more mirror facets -- same vertices and neighbors\n", qh_setsize(qh, qh->degen_mergeset)));
  qh->visible_list= qh->facet_tail;
  while ((merge= (mergeT*)qh_setdellast(qh->degen_mergeset))) {
    facet1= merge->facet1;
    facet2= merge->facet2;
    mergetype= merge->type;
    qh_memfree(qh, merge, (int)sizeof(mergeT));
    if (mergetype == MRGmirror) {
      zinc_(Ztrimirror);
      qh_triangulate_mirror(qh, facet1, facet2);
    }
  }
  qh_settempfree(qh, &qh->degen_mergeset);
  trace2((qh, qh->ferr, 2049, "qh_triangulate: update neighbor lists for vertices from v%d\n", getid_(new_vertex_list)));
  qh->newvertex_list= new_vertex_list;
  qh->visible_list= NULL;
  qh_updatevertices(qh  );
  qh_resetlists(qh, False, !qh_RESETvisible  );

  trace2((qh, qh->ferr, 2050, "qh_triangulate: identify degenerate tricoplanar facets from f%d\n", getid_(new_facet_list)));
  trace2((qh, qh->ferr, 2051, "qh_triangulate: and replace facet->f.triowner with tricoplanar facets that own center, normal, etc.\n"));
  FORALLfacet_(new_facet_list) {
    if (facet->tricoplanar && !facet->visible) {
      FOREACHneighbor_i_(qh, facet) {
        if (neighbor_i == 0) {
          if (neighbor->tricoplanar)
            orig_neighbor= neighbor->f.triowner;
          else
            orig_neighbor= neighbor;
        }else {
          if (neighbor->tricoplanar)
            otherfacet= neighbor->f.triowner;
          else
            otherfacet= neighbor;
          if (orig_neighbor == otherfacet) {
            zinc_(Ztridegen);
            facet->degenerate= True;
            break;
          }
        }
      }
    }
  }

  trace2((qh, qh->ferr, 2052, "qh_triangulate: delete visible facets -- non-simplicial, null, and mirrored facets\n"));
  owner= NULL;
  visible= NULL;
  for (facet= new_facet_list; facet && facet->next; facet= nextfacet) {
    nextfacet= facet->next;
    if (facet->visible) {
      if (facet->tricoplanar) {
        qh_delfacet(qh, facet);
        qh->num_visible--;
      }else {
        if (visible && !owner) {

          trace2((qh, qh->ferr, 2053, "qh_triangulate: all tricoplanar facets degenerate for non-simplicial facet f%d\n",
                       visible->id));
          qh_delfacet(qh, visible);
          qh->num_visible--;
        }
        visible= facet;
        owner= NULL;
      }
    }else if (facet->tricoplanar) {
      if (facet->f.triowner != visible || visible==NULL) {
        qh_fprintf(qh, qh->ferr, 6162, "qhull error (qh_triangulate): tricoplanar facet f%d not owned by its visible, non-simplicial facet f%d\n", facet->id, getid_(visible));
        qh_errexit2(qh, qh_ERRqhull, facet, visible);
      }
      if (owner)
        facet->f.triowner= owner;
      else if (!facet->degenerate) {
        owner= facet;
        nextfacet= visible->next;
        facet->keepcentrum= True;
        facet->coplanarset= visible->coplanarset;
        facet->outsideset= visible->outsideset;
        visible->coplanarset= NULL;
        visible->outsideset= NULL;
        if (!qh->TRInormals) {
          visible->center= NULL;
          visible->normal= NULL;
        }
        qh_delfacet(qh, visible);
        qh->num_visible--;
      }
    }
  }
  if (visible && !owner) {
    trace2((qh, qh->ferr, 2054, "qh_triangulate: all tricoplanar facets degenerate for last non-simplicial facet f%d\n",
                 visible->id));
    qh_delfacet(qh, visible);
    qh->num_visible--;
  }
  qh->NEWfacets= False;
  qh->ONLYgood= onlygood;
  if (qh->CHECKfrequently)
    qh_checkpolygon(qh, qh->facet_list);
  qh->hasTriangulation= True;
}





































void qh_triangulate_facet(qhT *qh, facetT *facetA, vertexT **first_vertex) {
  facetT *newfacet;
  facetT *neighbor, **neighborp;
  vertexT *apex;
  int numnew=0;

  trace3((qh, qh->ferr, 3020, "qh_triangulate_facet: triangulate facet f%d\n", facetA->id));

  if (qh->IStracing >= 4)
    qh_printfacet(qh, qh->ferr, facetA);
  FOREACHneighbor_(facetA) {
    neighbor->seen= False;
    neighbor->coplanar= False;
  }
  if (qh->CENTERtype == qh_ASvoronoi && !facetA->center
        && fabs_(facetA->normal[qh->hull_dim -1]) >= qh->ANGLEround * qh_ZEROdelaunay) {
    facetA->center= qh_facetcenter(qh, facetA->vertices);
  }
  qh_willdelete(qh, facetA, NULL);
  qh->newfacet_list= qh->facet_tail;
  facetA->visitid= qh->visit_id;
  apex= SETfirstt_(facetA->vertices, vertexT);
  qh_makenew_nonsimplicial(qh, facetA, apex, &numnew);
  SETfirst_(facetA->neighbors)= NULL;
  FORALLnew_facets {
    newfacet->tricoplanar= True;
    newfacet->f.trivisible= facetA;
    newfacet->degenerate= False;
    newfacet->upperdelaunay= facetA->upperdelaunay;
    newfacet->good= facetA->good;
    if (qh->TRInormals) {
      newfacet->keepcentrum= True;
      if(facetA->normal){
        newfacet->normal= qh_memalloc(qh, qh->normal_size);
        memcpy((char *)newfacet->normal, facetA->normal, qh->normal_size);
      }
      if (qh->CENTERtype == qh_AScentrum)
        newfacet->center= qh_getcentrum(qh, newfacet);
      else if (qh->CENTERtype == qh_ASvoronoi && facetA->center){
        newfacet->center= qh_memalloc(qh, qh->center_size);
        memcpy((char *)newfacet->center, facetA->center, qh->center_size);
      }
    }else {
      newfacet->keepcentrum= False;

      newfacet->normal= facetA->normal;
      newfacet->center= facetA->center;
    }
    newfacet->offset= facetA->offset;
#if qh_MAXoutside
    newfacet->maxoutside= facetA->maxoutside;
#endif
  }
  qh_matchnewfacets(qh  );
  zinc_(Ztricoplanar);
  zadd_(Ztricoplanartot, numnew);
  zmax_(Ztricoplanarmax, numnew);
  qh->visible_list= NULL;
  if (!(*first_vertex))
    (*first_vertex)= qh->newvertex_list;
  qh->newvertex_list= NULL;
  qh_updatevertices(qh  );
  qh_resetlists(qh, False, !qh_RESETvisible  );
}











void qh_triangulate_link(qhT *qh, facetT *oldfacetA, facetT *facetA, facetT *oldfacetB, facetT *facetB) {
  int errmirror= False;

  trace3((qh, qh->ferr, 3021, "qh_triangulate_link: relink old facets f%d and f%d between neighbors f%d and f%d\n",
         oldfacetA->id, oldfacetB->id, facetA->id, facetB->id));
  if (qh_setin(facetA->neighbors, facetB)) {
    if (!qh_setin(facetB->neighbors, facetA))
      errmirror= True;
    else
      qh_appendmergeset(qh, facetA, facetB, MRGmirror, NULL);
  }else if (qh_setin(facetB->neighbors, facetA))
    errmirror= True;
  if (errmirror) {
    qh_fprintf(qh, qh->ferr, 6163, "qhull error (qh_triangulate_link): mirror facets f%d and f%d do not match for old facets f%d and f%d\n",
       facetA->id, facetB->id, oldfacetA->id, oldfacetB->id);
    qh_errexit2(qh, qh_ERRqhull, facetA, facetB);
  }
  qh_setreplace(qh, facetB->neighbors, oldfacetB, facetA);
  qh_setreplace(qh, facetA->neighbors, oldfacetA, facetB);
}











void qh_triangulate_mirror(qhT *qh, facetT *facetA, facetT *facetB) {
  facetT *neighbor, *neighborB;
  int neighbor_i, neighbor_n;

  trace3((qh, qh->ferr, 3022, "qh_triangulate_mirror: delete mirrored facets f%d and f%d\n",
         facetA->id, facetB->id));
  FOREACHneighbor_i_(qh, facetA) {
    neighborB= SETelemt_(facetB->neighbors, neighbor_i, facetT);
    if (neighbor == neighborB)
      continue;
    qh_triangulate_link(qh, facetA, neighbor, facetB, neighborB);
  }
  qh_willdelete(qh, facetA, NULL);
  qh_willdelete(qh, facetB, NULL);
}














void qh_triangulate_null(qhT *qh, facetT *facetA) {
  facetT *neighbor, *otherfacet;

  trace3((qh, qh->ferr, 3023, "qh_triangulate_null: delete null facet f%d\n", facetA->id));
  neighbor= SETfirstt_(facetA->neighbors, facetT);
  otherfacet= SETsecondt_(facetA->neighbors, facetT);
  qh_triangulate_link(qh, facetA, neighbor, facetA, otherfacet);
  qh_willdelete(qh, facetA, NULL);
}

#else
void qh_triangulate(qhT *qh) {
}
#endif














void qh_vertexintersect(qhT *qh, setT **vertexsetA,setT *vertexsetB) {
  setT *intersection;

  intersection= qh_vertexintersect_new(qh, *vertexsetA, vertexsetB);
  qh_settempfree(qh, vertexsetA);
  *vertexsetA= intersection;
  qh_settemppush(qh, intersection);
}










setT *qh_vertexintersect_new(qhT *qh, setT *vertexsetA,setT *vertexsetB) {
  setT *intersection= qh_setnew(qh, qh->hull_dim - 1);
  vertexT **vertexA= SETaddr_(vertexsetA, vertexT);
  vertexT **vertexB= SETaddr_(vertexsetB, vertexT);

  while (*vertexA && *vertexB) {
    if (*vertexA  == *vertexB) {
      qh_setappend(qh, &intersection, *vertexA);
      vertexA++; vertexB++;
    }else {
      if ((*vertexA)->id > (*vertexB)->id)
        vertexA++;
      else
        vertexB++;
    }
  }
  return intersection;
}





















void qh_vertexneighbors(qhT *qh  ) {
  facetT *facet;
  vertexT *vertex, **vertexp;

  if (qh->VERTEXneighbors)
    return;
  trace1((qh, qh->ferr, 1035, "qh_vertexneighbors: determining neighboring facets for each vertex\n"));
  qh->vertex_visit++;
  FORALLfacets {
    if (facet->visible)
      continue;
    FOREACHvertex_(facet->vertices) {
      if (vertex->visitid != qh->vertex_visit) {
        vertex->visitid= qh->vertex_visit;
        vertex->neighbors= qh_setnew(qh, qh->hull_dim);
      }
      qh_setappend(qh, &vertex->neighbors, facet);
    }
  }
  qh->VERTEXneighbors= True;
}











boolT qh_vertexsubset(setT *vertexsetA, setT *vertexsetB) {
  vertexT **vertexA= (vertexT **) SETaddr_(vertexsetA, vertexT);
  vertexT **vertexB= (vertexT **) SETaddr_(vertexsetB, vertexT);

  while (True) {
    if (!*vertexA)
      return True;
    if (!*vertexB)
      return False;
    if ((*vertexA)->id > (*vertexB)->id)
      return False;
    if (*vertexA  == *vertexB)
      vertexA++;
    vertexB++;
  }
  return False;
}
