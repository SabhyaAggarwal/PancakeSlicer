/*<html><pre>  -<a                             href="qh-geom_r.htm"
  >-------------------------------</a><a name="TOP">-</a>


   geom2_r.c
   infrequently used geometric routines of qhull

   see qh-geom_r.htm and geom_r.h

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/geom2_r.c#6 $$Change: 2065 $
   $DateTime: 2016/01/18 13:51:04 $$Author: bbarber $

   frequently used code goes into geom_r.c
*/

#include "qhull_ra.h"












coordT *qh_copypoints(qhT *qh, coordT *points, int numpoints, int dimension) {
  int size;
  coordT *newpoints;

  size= numpoints * dimension * (int)sizeof(coordT);
  if (!(newpoints= (coordT*)qh_malloc((size_t)size))) {
    qh_fprintf(qh, qh->ferr, 6004, "qhull error: insufficient memory to copy %d points\n",
        numpoints);
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }
  memcpy((char *)newpoints, (char *)points, (size_t)size);
  return newpoints;
}












void qh_crossproduct(int dim, realT vecA[3], realT vecB[3], realT vecC[3]){

  if (dim == 3) {
    vecC[0]=   det2_(vecA[1], vecA[2],
                     vecB[1], vecB[2]);
    vecC[1]= - det2_(vecA[0], vecA[2],
                     vecB[0], vecB[2]);
    vecC[2]=   det2_(vecA[0], vecA[1],
                     vecB[0], vecB[1]);
  }
}

















realT qh_determinant(qhT *qh, realT **rows, int dim, boolT *nearzero) {
  realT det=0;
  int i;
  boolT sign= False;

  *nearzero= False;
  if (dim < 2) {
    qh_fprintf(qh, qh->ferr, 6005, "qhull internal error (qh_determinate): only implemented for dimension >= 2\n");
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }else if (dim == 2) {
    det= det2_(rows[0][0], rows[0][1],
                 rows[1][0], rows[1][1]);
    if (fabs_(det) < 10*qh->NEARzero[1])
      *nearzero= True;
  }else if (dim == 3) {
    det= det3_(rows[0][0], rows[0][1], rows[0][2],
                 rows[1][0], rows[1][1], rows[1][2],
                 rows[2][0], rows[2][1], rows[2][2]);
    if (fabs_(det) < 10*qh->NEARzero[2])
      *nearzero= True;
  }else {
    qh_gausselim(qh, rows, dim, dim, &sign, nearzero);
    det= 1.0;
    for (i=dim; i--; )
      det *= (rows[i])[i];
    if (sign)
      det= -det;
  }
  return det;
}

















realT qh_detjoggle(qhT *qh, pointT *points, int numpoints, int dimension) {
  realT abscoord, distround, joggle, maxcoord, mincoord;
  pointT *point, *pointtemp;
  realT maxabs= -REALmax;
  realT sumabs= 0;
  realT maxwidth= 0;
  int k;

  for (k=0; k < dimension; k++) {
    if (qh->SCALElast && k == dimension-1)
      abscoord= maxwidth;
    else if (qh->DELAUNAY && k == dimension-1)
      abscoord= 2 * maxabs * maxabs;
    else {
      maxcoord= -REALmax;
      mincoord= REALmax;
      FORALLpoint_(qh, points, numpoints) {
        maximize_(maxcoord, point[k]);
        minimize_(mincoord, point[k]);
      }
      maximize_(maxwidth, maxcoord-mincoord);
      abscoord= fmax_(maxcoord, -mincoord);
    }
    sumabs += abscoord;
    maximize_(maxabs, abscoord);
  }
  distround= qh_distround(qh, qh->hull_dim, maxabs, sumabs);
  joggle= distround * qh_JOGGLEdefault;
  maximize_(joggle, REALepsilon * qh_JOGGLEdefault);
  trace2((qh, qh->ferr, 2001, "qh_detjoggle: joggle=%2.2g maxwidth=%2.2g\n", joggle, maxwidth));
  return joggle;
}































void qh_detroundoff(qhT *qh) {

  qh_option(qh, "_max-width", NULL, &qh->MAXwidth);
  if (!qh->SETroundoff) {
    qh->DISTround= qh_distround(qh, qh->hull_dim, qh->MAXabs_coord, qh->MAXsumcoord);
    if (qh->RANDOMdist)
      qh->DISTround += qh->RANDOMfactor * qh->MAXabs_coord;
    qh_option(qh, "Error-roundoff", NULL, &qh->DISTround);
  }
  qh->MINdenom= qh->MINdenom_1 * qh->MAXabs_coord;
  qh->MINdenom_1_2= sqrt(qh->MINdenom_1 * qh->hull_dim) ;
  qh->MINdenom_2= qh->MINdenom_1_2 * qh->MAXabs_coord;

  qh->ANGLEround= 1.01 * qh->hull_dim * REALepsilon;
  if (qh->RANDOMdist)
    qh->ANGLEround += qh->RANDOMfactor;
  if (qh->premerge_cos < REALmax/2) {
    qh->premerge_cos -= qh->ANGLEround;
    if (qh->RANDOMdist)
      qh_option(qh, "Angle-premerge-with-random", NULL, &qh->premerge_cos);
  }
  if (qh->postmerge_cos < REALmax/2) {
    qh->postmerge_cos -= qh->ANGLEround;
    if (qh->RANDOMdist)
      qh_option(qh, "Angle-postmerge-with-random", NULL, &qh->postmerge_cos);
  }
  qh->premerge_centrum += 2 * qh->DISTround;
  qh->postmerge_centrum += 2 * qh->DISTround;
  if (qh->RANDOMdist && (qh->MERGEexact || qh->PREmerge))
    qh_option(qh, "Centrum-premerge-with-random", NULL, &qh->premerge_centrum);
  if (qh->RANDOMdist && qh->POSTmerge)
    qh_option(qh, "Centrum-postmerge-with-random", NULL, &qh->postmerge_centrum);
  {
    realT maxangle= 1.0, maxrho;

    minimize_(maxangle, qh->premerge_cos);
    minimize_(maxangle, qh->postmerge_cos);

    qh->ONEmerge= sqrt((realT)qh->hull_dim) * qh->MAXwidth *
      sqrt(1.0 - maxangle * maxangle) + qh->DISTround;
    maxrho= qh->hull_dim * qh->premerge_centrum + qh->DISTround;
    maximize_(qh->ONEmerge, maxrho);
    maxrho= qh->hull_dim * qh->postmerge_centrum + qh->DISTround;
    maximize_(qh->ONEmerge, maxrho);
    if (qh->MERGING)
      qh_option(qh, "_one-merge", NULL, &qh->ONEmerge);
  }
  qh->NEARinside= qh->ONEmerge * qh_RATIOnearinside;
  if (qh->JOGGLEmax < REALmax/2 && (qh->KEEPcoplanar || qh->KEEPinside)) {
    realT maxdist;
    qh->KEEPnearinside= True;
    maxdist= sqrt((realT)qh->hull_dim) * qh->JOGGLEmax + qh->DISTround;
    maxdist= 2*maxdist;
    maximize_(qh->NEARinside, maxdist);
  }
  if (qh->KEEPnearinside)
    qh_option(qh, "_near-inside", NULL, &qh->NEARinside);
  if (qh->JOGGLEmax < qh->DISTround) {
    qh_fprintf(qh, qh->ferr, 6006, "qhull error: the joggle for 'QJn', %.2g, is below roundoff for distance computations, %.2g\n",
         qh->JOGGLEmax, qh->DISTround);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (qh->MINvisible > REALmax/2) {
    if (!qh->MERGING)
      qh->MINvisible= qh->DISTround;
    else if (qh->hull_dim <= 3)
      qh->MINvisible= qh->premerge_centrum;
    else
      qh->MINvisible= qh_COPLANARratio * qh->premerge_centrum;
    if (qh->APPROXhull && qh->MINvisible > qh->MINoutside)
      qh->MINvisible= qh->MINoutside;
    qh_option(qh, "Visible-distance", NULL, &qh->MINvisible);
  }
  if (qh->MAXcoplanar > REALmax/2) {
    qh->MAXcoplanar= qh->MINvisible;
    qh_option(qh, "U-coplanar-distance", NULL, &qh->MAXcoplanar);
  }
  if (!qh->APPROXhull) {
    qh->MINoutside= 2 * qh->MINvisible;
    if (qh->premerge_cos < REALmax/2)
      maximize_(qh->MINoutside, (1- qh->premerge_cos) * qh->MAXabs_coord);
    qh_option(qh, "Width-outside", NULL, &qh->MINoutside);
  }
  qh->WIDEfacet= qh->MINoutside;
  maximize_(qh->WIDEfacet, qh_WIDEcoplanar * qh->MAXcoplanar);
  maximize_(qh->WIDEfacet, qh_WIDEcoplanar * qh->MINvisible);
  qh_option(qh, "_wide-facet", NULL, &qh->WIDEfacet);
  if (qh->MINvisible > qh->MINoutside + 3 * REALepsilon
  && !qh->BESToutside && !qh->FORCEoutput)
    qh_fprintf(qh, qh->ferr, 7001, "qhull input warning: minimum visibility V%.2g is greater than \nminimum outside W%.2g.  Flipped facets are likely.\n",
             qh->MINvisible, qh->MINoutside);
  qh->max_vertex= qh->DISTround;
  qh->min_vertex= -qh->DISTround;

}

















realT qh_detsimplex(qhT *qh, pointT *apex, setT *points, int dim, boolT *nearzero) {
  pointT *coorda, *coordp, *gmcoord, *point, **pointp;
  coordT **rows;
  int k,  i=0;
  realT det;

  zinc_(Zdetsimplex);
  gmcoord= qh->gm_matrix;
  rows= qh->gm_row;
  FOREACHpoint_(points) {
    if (i == dim)
      break;
    rows[i++]= gmcoord;
    coordp= point;
    coorda= apex;
    for (k=dim; k--; )
      *(gmcoord++)= *coordp++ - *coorda++;
  }
  if (i < dim) {
    qh_fprintf(qh, qh->ferr, 6007, "qhull internal error (qh_detsimplex): #points %d < dimension %d\n",
               i, dim);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  det= qh_determinant(qh, rows, dim, nearzero);
  trace2((qh, qh->ferr, 2002, "qh_detsimplex: det=%2.2g for point p%d, dim %d, nearzero? %d\n",
          det, qh_pointid(qh, apex), dim, *nearzero));
  return det;
}
















realT qh_distnorm(int dim, pointT *point, pointT *normal, realT *offsetp) {
  coordT *normalp= normal, *coordp= point;
  realT dist;
  int k;

  dist= *offsetp;
  for (k=dim; k--; )
    dist += *(coordp++) * *(normalp++);
  return dist;
}


















realT qh_distround(qhT *qh, int dimension, realT maxabs, realT maxsumabs) {
  realT maxdistsum, maxround;

  maxdistsum= sqrt((realT)dimension) * maxabs;
  minimize_( maxdistsum, maxsumabs);
  maxround= REALepsilon * (dimension * maxdistsum * 1.01 + maxabs);

  trace4((qh, qh->ferr, 4008, "qh_distround: %2.2g maxabs %2.2g maxsumabs %2.2g maxdistsum %2.2g\n",
                 maxround, maxabs, maxsumabs, maxdistsum));
  return maxround;
}






















realT qh_divzero(realT numer, realT denom, realT mindenom1, boolT *zerodiv) {
  realT temp, numerx, denomx;


  if (numer < mindenom1 && numer > -mindenom1) {
    numerx= fabs_(numer);
    denomx= fabs_(denom);
    if (numerx < denomx) {
      *zerodiv= False;
      return numer/denom;
    }else {
      *zerodiv= True;
      return 0.0;
    }
  }
  temp= denom/numer;
  if (temp > mindenom1 || temp < -mindenom1) {
    *zerodiv= False;
    return numer/denom;
  }else {
    *zerodiv= True;
    return 0.0;
  }
}
























realT qh_facetarea(qhT *qh, facetT *facet) {
  vertexT *apex;
  pointT *centrum;
  realT area= 0.0;
  ridgeT *ridge, **ridgep;

  if (facet->simplicial) {
    apex= SETfirstt_(facet->vertices, vertexT);
    area= qh_facetarea_simplex(qh, qh->hull_dim, apex->point, facet->vertices,
                    apex, facet->toporient, facet->normal, &facet->offset);
  }else {
    if (qh->CENTERtype == qh_AScentrum)
      centrum= facet->center;
    else
      centrum= qh_getcentrum(qh, facet);
    FOREACHridge_(facet->ridges)
      area += qh_facetarea_simplex(qh, qh->hull_dim, centrum, ridge->vertices,
                 NULL, (boolT)(ridge->top == facet),  facet->normal, &facet->offset);
    if (qh->CENTERtype != qh_AScentrum)
      qh_memfree(qh, centrum, qh->normal_size);
  }
  if (facet->upperdelaunay && qh->DELAUNAY)
    area= -area;
  trace4((qh, qh->ferr, 4009, "qh_facetarea: f%d area %2.2g\n", facet->id, area));
  return area;
}


































realT qh_facetarea_simplex(qhT *qh, int dim, coordT *apex, setT *vertices,
        vertexT *notvertex,  boolT toporient, coordT *normal, realT *offset) {
  pointT *coorda, *coordp, *gmcoord;
  coordT **rows, *normalp;
  int k,  i=0;
  realT area, dist;
  vertexT *vertex, **vertexp;
  boolT nearzero;

  gmcoord= qh->gm_matrix;
  rows= qh->gm_row;
  FOREACHvertex_(vertices) {
    if (vertex == notvertex)
      continue;
    rows[i++]= gmcoord;
    coorda= apex;
    coordp= vertex->point;
    normalp= normal;
    if (notvertex) {
      for (k=dim; k--; )
        *(gmcoord++)= *coordp++ - *coorda++;
    }else {
      dist= *offset;
      for (k=dim; k--; )
        dist += *coordp++ * *normalp++;
      if (dist < -qh->WIDEfacet) {
        zinc_(Znoarea);
        return 0.0;
      }
      coordp= vertex->point;
      normalp= normal;
      for (k=dim; k--; )
        *(gmcoord++)= (*coordp++ - dist * *normalp++) - *coorda++;
    }
  }
  if (i != dim-1) {
    qh_fprintf(qh, qh->ferr, 6008, "qhull internal error (qh_facetarea_simplex): #points %d != dim %d -1\n",
               i, dim);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  rows[i]= gmcoord;
  if (qh->DELAUNAY) {
    for (i=0; i < dim-1; i++)
      rows[i][dim-1]= 0.0;
    for (k=dim; k--; )
      *(gmcoord++)= 0.0;
    rows[dim-1][dim-1]= -1.0;
  }else {
    normalp= normal;
    for (k=dim; k--; )
      *(gmcoord++)= *normalp++;
  }
  zinc_(Zdetsimplex);
  area= qh_determinant(qh, rows, dim, &nearzero);
  if (toporient)
    area= -area;
  area *= qh->AREAfactor;
  trace4((qh, qh->ferr, 4010, "qh_facetarea_simplex: area=%2.2g for point p%d, toporient %d, nearzero? %d\n",
          area, qh_pointid(qh, apex), toporient, nearzero));
  return area;
}













pointT *qh_facetcenter(qhT *qh, setT *vertices) {
  setT *points= qh_settemp(qh, qh_setsize(qh, vertices));
  vertexT *vertex, **vertexp;
  pointT *center;

  FOREACHvertex_(vertices)
    qh_setappend(qh, &points, vertex->point);
  center= qh_voronoi_center(qh, qh->hull_dim-1, points);
  qh_settempfree(qh, &points);
  return center;
}




























facetT *qh_findgooddist(qhT *qh, pointT *point, facetT *facetA, realT *distp,
               facetT **facetlist) {
  realT bestdist= -REALmax, dist;
  facetT *neighbor, **neighborp, *bestfacet=NULL, *facet;
  boolT goodseen= False;

  if (facetA->good) {
    zzinc_(Zcheckpart);
    qh_distplane(qh, point, facetA, &bestdist);
    bestfacet= facetA;
    goodseen= True;
  }
  qh_removefacet(qh, facetA);
  qh_appendfacet(qh, facetA);
  *facetlist= facetA;
  facetA->visitid= ++qh->visit_id;
  FORALLfacet_(*facetlist) {
    FOREACHneighbor_(facet) {
      if (neighbor->visitid == qh->visit_id)
        continue;
      neighbor->visitid= qh->visit_id;
      if (goodseen && !neighbor->good)
        continue;
      zzinc_(Zcheckpart);
      qh_distplane(qh, point, neighbor, &dist);
      if (dist > 0) {
        qh_removefacet(qh, neighbor);
        qh_appendfacet(qh, neighbor);
        if (neighbor->good) {
          goodseen= True;
          if (dist > bestdist) {
            bestdist= dist;
            bestfacet= neighbor;
          }
        }
      }
    }
  }
  if (bestfacet) {
    *distp= bestdist;
    trace2((qh, qh->ferr, 2003, "qh_findgooddist: p%d is %2.2g above good facet f%d\n",
      qh_pointid(qh, point), bestdist, bestfacet->id));
    return bestfacet;
  }
  trace4((qh, qh->ferr, 4011, "qh_findgooddist: no good facet for p%d above f%d\n",
      qh_pointid(qh, point), facetA->id));
  return NULL;
}
























void qh_getarea(qhT *qh, facetT *facetlist) {
  realT area;
  realT dist;
  facetT *facet;

  if (qh->hasAreaVolume)
    return;
  if (qh->REPORTfreq)
    qh_fprintf(qh, qh->ferr, 8020, "computing area of each facet and volume of the convex hull\n");
  else
    trace1((qh, qh->ferr, 1001, "qh_getarea: computing volume and area for each facet\n"));
  qh->totarea= qh->totvol= 0.0;
  FORALLfacet_(facetlist) {
    if (!facet->normal)
      continue;
    if (facet->upperdelaunay && qh->ATinfinity)
      continue;
    if (!facet->isarea) {
      facet->f.area= qh_facetarea(qh, facet);
      facet->isarea= True;
    }
    area= facet->f.area;
    if (qh->DELAUNAY) {
      if (facet->upperdelaunay == qh->UPPERdelaunay)
        qh->totarea += area;
    }else {
      qh->totarea += area;
      qh_distplane(qh, qh->interior_point, facet, &dist);
      qh->totvol += -dist * area/ qh->hull_dim;
    }
    if (qh->PRINTstatistics) {
      wadd_(Wareatot, area);
      wmax_(Wareamax, area);
      wmin_(Wareamin, area);
    }
  }
  qh->hasAreaVolume= True;
}























boolT qh_gram_schmidt(qhT *qh, int dim, realT **row) {
  realT *rowi, *rowj, norm;
  int i, j, k;

  for (i=0; i < dim; i++) {
    rowi= row[i];
    for (norm= 0.0, k= dim; k--; rowi++)
      norm += *rowi * *rowi;
    norm= sqrt(norm);
    wmin_(Wmindenom, norm);
    if (norm == 0.0)
      return False;
    for (k=dim; k--; )
      *(--rowi) /= norm;
    for (j=i+1; j < dim; j++) {
      rowj= row[j];
      for (norm= 0.0, k=dim; k--; )
        norm += *rowi++ * *rowj++;
      for (k=dim; k--; )
        *(--rowj) -= *(--rowi) * norm;
    }
  }
  return True;
}
























boolT qh_inthresholds(qhT *qh, coordT *normal, realT *angle) {
  boolT within= True;
  int k;
  realT threshold;

  if (angle)
    *angle= 0.0;
  for (k=0; k < qh->hull_dim; k++) {
    threshold= qh->lower_threshold[k];
    if (threshold > -REALmax/2) {
      if (normal[k] < threshold)
        within= False;
      if (angle) {
        threshold -= normal[k];
        *angle += fabs_(threshold);
      }
    }
    if (qh->upper_threshold[k] < REALmax/2) {
      threshold= qh->upper_threshold[k];
      if (normal[k] > threshold)
        within= False;
      if (angle) {
        threshold -= normal[k];
        *angle += fabs_(threshold);
      }
    }
  }
  return within;
}


































void qh_joggleinput(qhT *qh) {
  int i, seed, size;
  coordT *coordp, *inputp;
  realT randr, randa, randb;

  if (!qh->input_points) {
    qh->input_points= qh->first_point;
    qh->input_malloc= qh->POINTSmalloc;
    size= qh->num_points * qh->hull_dim * sizeof(coordT);
    if (!(qh->first_point=(coordT*)qh_malloc((size_t)size))) {
      qh_fprintf(qh, qh->ferr, 6009, "qhull error: insufficient memory to joggle %d points\n",
          qh->num_points);
      qh_errexit(qh, qh_ERRmem, NULL, NULL);
    }
    qh->POINTSmalloc= True;
    if (qh->JOGGLEmax == 0.0) {
      qh->JOGGLEmax= qh_detjoggle(qh, qh->input_points, qh->num_points, qh->hull_dim);
      qh_option(qh, "QJoggle", NULL, &qh->JOGGLEmax);
    }
  }else {
    if (!qh->RERUN && qh->build_cnt > qh_JOGGLEretry) {
      if (((qh->build_cnt-qh_JOGGLEretry-1) % qh_JOGGLEagain) == 0) {
        realT maxjoggle= qh->MAXwidth * qh_JOGGLEmaxincrease;
        if (qh->JOGGLEmax < maxjoggle) {
          qh->JOGGLEmax *= qh_JOGGLEincrease;
          minimize_(qh->JOGGLEmax, maxjoggle);
        }
      }
    }
    qh_option(qh, "QJoggle", NULL, &qh->JOGGLEmax);
  }
  if (qh->build_cnt > 1 && qh->JOGGLEmax > fmax_(qh->MAXwidth/4, 0.1)) {
      qh_fprintf(qh, qh->ferr, 6010, "qhull error: the current joggle for 'QJn', %.2g, is too large for the width\nof the input.  If possible, recompile Qhull with higher-precision reals.\n",
                qh->JOGGLEmax);
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }

  seed= qh_RANDOMint;
  qh_option(qh, "_joggle-seed", &seed, NULL);
  trace0((qh, qh->ferr, 6, "qh_joggleinput: joggle input by %2.2g with seed %d\n",
    qh->JOGGLEmax, seed));
  inputp= qh->input_points;
  coordp= qh->first_point;
  randa= 2.0 * qh->JOGGLEmax/qh_RANDOMmax;
  randb= -qh->JOGGLEmax;
  size= qh->num_points * qh->hull_dim;
  for (i=size; i--; ) {
    randr= qh_RANDOMint;
    *(coordp++)= *(inputp++) + (randr * randa + randb);
  }
  if (qh->DELAUNAY) {
    qh->last_low= qh->last_high= qh->last_newhigh= REALmax;
    qh_setdelaunay(qh, qh->hull_dim, qh->num_points, qh->first_point);
  }
}








realT *qh_maxabsval(realT *normal, int dim) {
  realT maxval= -REALmax;
  realT *maxp= NULL, *colp, absval;
  int k;

  for (k=dim, colp= normal; k--; colp++) {
    absval= fabs_(*colp);
    if (absval > maxval) {
      maxval= absval;
      maxp= colp;
    }
  }
  return maxp;
}




























setT *qh_maxmin(qhT *qh, pointT *points, int numpoints, int dimension) {
  int k;
  realT maxcoord, temp;
  pointT *minimum, *maximum, *point, *pointtemp;
  setT *set;

  qh->max_outside= 0.0;
  qh->MAXabs_coord= 0.0;
  qh->MAXwidth= -REALmax;
  qh->MAXsumcoord= 0.0;
  qh->min_vertex= 0.0;
  qh->WAScoplanar= False;
  if (qh->ZEROcentrum)
    qh->ZEROall_ok= True;
  if (REALmin < REALepsilon && REALmin < REALmax && REALmin > -REALmax
  && REALmax > 0.0 && -REALmax < 0.0)
    ;
  else {
    qh_fprintf(qh, qh->ferr, 6011, "qhull error: floating point constants in user.h are wrong\n\
REALepsilon %g REALmin %g REALmax %g -REALmax %g\n",
             REALepsilon, REALmin, REALmax, -REALmax);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  set= qh_settemp(qh, 2*dimension);
  for (k=0; k < dimension; k++) {
    if (points == qh->GOODpointp)
      minimum= maximum= points + dimension;
    else
      minimum= maximum= points;
    FORALLpoint_(qh, points, numpoints) {
      if (point == qh->GOODpointp)
        continue;
      if (maximum[k] < point[k])
        maximum= point;
      else if (minimum[k] > point[k])
        minimum= point;
    }
    if (k == dimension-1) {
      qh->MINlastcoord= minimum[k];
      qh->MAXlastcoord= maximum[k];
    }
    if (qh->SCALElast && k == dimension-1)
      maxcoord= qh->MAXwidth;
    else {
      maxcoord= fmax_(maximum[k], -minimum[k]);
      if (qh->GOODpointp) {
        temp= fmax_(qh->GOODpointp[k], -qh->GOODpointp[k]);
        maximize_(maxcoord, temp);
      }
      temp= maximum[k] - minimum[k];
      maximize_(qh->MAXwidth, temp);
    }
    maximize_(qh->MAXabs_coord, maxcoord);
    qh->MAXsumcoord += maxcoord;
    qh_setappend(qh, &set, maximum);
    qh_setappend(qh, &set, minimum);




    qh->NEARzero[k]= 80 * qh->MAXsumcoord * REALepsilon;
  }
  if (qh->IStracing >=1)
    qh_printpoints(qh, qh->ferr, "qh_maxmin: found the max and min points(by dim):", set);
  return(set);
}





















realT qh_maxouter(qhT *qh) {
  realT dist;

  dist= fmax_(qh->max_outside, qh->DISTround);
  dist += qh->DISTround;
  trace4((qh, qh->ferr, 4012, "qh_maxouter: max distance from facet to outer plane is %2.2g max_outside is %2.2g\n", dist, qh->max_outside));
  return dist;
}
























void qh_maxsimplex(qhT *qh, int dim, setT *maxpoints, pointT *points, int numpoints, setT **simplex) {
  pointT *point, **pointp, *pointtemp, *maxpoint, *minx=NULL, *maxx=NULL;
  boolT nearzero, maxnearzero= False;
  int k, sizinit;
  realT maxdet= -REALmax, det, mincoord= REALmax, maxcoord= -REALmax;

  sizinit= qh_setsize(qh, *simplex);
  if (sizinit < 2) {
    if (qh_setsize(qh, maxpoints) >= 2) {
      FOREACHpoint_(maxpoints) {
        if (maxcoord < point[0]) {
          maxcoord= point[0];
          maxx= point;
        }
        if (mincoord > point[0]) {
          mincoord= point[0];
          minx= point;
        }
      }
    }else {
      FORALLpoint_(qh, points, numpoints) {
        if (point == qh->GOODpointp)
          continue;
        if (maxcoord < point[0]) {
          maxcoord= point[0];
          maxx= point;
        }
        if (mincoord > point[0]) {
          mincoord= point[0];
          minx= point;
        }
      }
    }
    qh_setunique(qh, simplex, minx);
    if (qh_setsize(qh, *simplex) < 2)
      qh_setunique(qh, simplex, maxx);
    sizinit= qh_setsize(qh, *simplex);
    if (sizinit < 2) {
      qh_precision(qh, "input has same x coordinate");
      if (zzval_(Zsetplane) > qh->hull_dim+1) {
        qh_fprintf(qh, qh->ferr, 6012, "qhull precision error (qh_maxsimplex for voronoi_center):\n%d points with the same x coordinate.\n",
                 qh_setsize(qh, maxpoints)+numpoints);
        qh_errexit(qh, qh_ERRprec, NULL, NULL);
      }else {
        qh_fprintf(qh, qh->ferr, 6013, "qhull input error: input is less than %d-dimensional since it has the same x coordinate\n", qh->hull_dim);
        qh_errexit(qh, qh_ERRinput, NULL, NULL);
      }
    }
  }
  for (k=sizinit; k < dim+1; k++) {
    maxpoint= NULL;
    maxdet= -REALmax;
    FOREACHpoint_(maxpoints) {
      if (!qh_setin(*simplex, point)) {
        det= qh_detsimplex(qh, point, *simplex, k, &nearzero);
        if ((det= fabs_(det)) > maxdet) {
          maxdet= det;
          maxpoint= point;
          maxnearzero= nearzero;
        }
      }
    }
    if (!maxpoint || maxnearzero) {
      zinc_(Zsearchpoints);
      if (!maxpoint) {
        trace0((qh, qh->ferr, 7, "qh_maxsimplex: searching all points for %d-th initial vertex.\n", k+1));
      }else {
        trace0((qh, qh->ferr, 8, "qh_maxsimplex: searching all points for %d-th initial vertex, better than p%d det %2.2g\n",
                k+1, qh_pointid(qh, maxpoint), maxdet));
      }
      FORALLpoint_(qh, points, numpoints) {
        if (point == qh->GOODpointp)
          continue;
        if (!qh_setin(*simplex, point)) {
          det= qh_detsimplex(qh, point, *simplex, k, &nearzero);
          if ((det= fabs_(det)) > maxdet) {
            maxdet= det;
            maxpoint= point;
            maxnearzero= nearzero;
          }
        }
      }
    }
    if (!maxpoint) {
      qh_fprintf(qh, qh->ferr, 6014, "qhull internal error (qh_maxsimplex): not enough points available\n");
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }
    qh_setappend(qh, simplex, maxpoint);
    trace1((qh, qh->ferr, 1002, "qh_maxsimplex: selected point p%d for %d`th initial vertex, det=%2.2g\n",
            qh_pointid(qh, maxpoint), k+1, maxdet));
  }
}







realT qh_minabsval(realT *normal, int dim) {
  realT minval= 0;
  realT maxval= 0;
  realT *colp;
  int k;

  for (k=dim, colp=normal; k--; colp++) {
    maximize_(maxval, *colp);
    minimize_(minval, *colp);
  }
  return fmax_(maxval, -minval);
}








int qh_mindiff(realT *vecA, realT *vecB, int dim) {
  realT mindiff= REALmax, diff;
  realT *vecAp= vecA, *vecBp= vecB;
  int k, mink= 0;

  for (k=0; k < dim; k++) {
    diff= *vecAp++ - *vecBp++;
    diff= fabs_(diff);
    if (diff < mindiff) {
      mindiff= diff;
      mink= k;
    }
  }
  return mink;
}












boolT qh_orientoutside(qhT *qh, facetT *facet) {
  int k;
  realT dist;

  qh_distplane(qh, qh->interior_point, facet, &dist);
  if (dist > 0) {
    for (k=qh->hull_dim; k--; )
      facet->normal[k]= -facet->normal[k];
    facet->offset= -facet->offset;
    return True;
  }
  return False;
}






















void qh_outerinner(qhT *qh, facetT *facet, realT *outerplane, realT *innerplane) {
  realT dist, mindist;
  vertexT *vertex, **vertexp;

  if (outerplane) {
    if (!qh_MAXoutside || !facet || !qh->maxoutdone) {
      *outerplane= qh_maxouter(qh);
    }else {
#if qh_MAXoutside
      *outerplane= facet->maxoutside + qh->DISTround;
#endif

    }
    if (qh->JOGGLEmax < REALmax/2)
      *outerplane += qh->JOGGLEmax * sqrt((realT)qh->hull_dim);
  }
  if (innerplane) {
    if (facet) {
      mindist= REALmax;
      FOREACHvertex_(facet->vertices) {
        zinc_(Zdistio);
        qh_distplane(qh, vertex->point, facet, &dist);
        minimize_(mindist, dist);
      }
      *innerplane= mindist - qh->DISTround;
    }else
      *innerplane= qh->min_vertex - qh->DISTround;
    if (qh->JOGGLEmax < REALmax/2)
      *innerplane -= qh->JOGGLEmax * sqrt((realT)qh->hull_dim);
  }
}










coordT qh_pointdist(pointT *point1, pointT *point2, int dim) {
  coordT dist, diff;
  int k;

  dist= 0.0;
  for (k= (dim > 0 ? dim : -dim); k--; ) {
    diff= *point1++ - *point2++;
    dist += diff * diff;
  }
  if (dim > 0)
    return(sqrt(dist));
  return dist;
}













void qh_printmatrix(qhT *qh, FILE *fp, const char *string, realT **rows, int numrow, int numcol) {
  realT *rowp;
  realT r;
  int i,k;

  qh_fprintf(qh, fp, 9001, "%s\n", string);
  for (i=0; i < numrow; i++) {
    rowp= rows[i];
    for (k=0; k < numcol; k++) {
      r= *rowp++;
      qh_fprintf(qh, fp, 9002, "%6.3g ", r);
    }
    qh_fprintf(qh, fp, 9003, "\n");
  }
}









void qh_printpoints(qhT *qh, FILE *fp, const char *string, setT *points) {
  pointT *point, **pointp;

  if (string) {
    qh_fprintf(qh, fp, 9004, "%s", string);
    FOREACHpoint_(points)
      qh_fprintf(qh, fp, 9005, " p%d", qh_pointid(qh, point));
    qh_fprintf(qh, fp, 9006, "\n");
  }else {
    FOREACHpoint_(points)
      qh_fprintf(qh, fp, 9007, " %d", qh_pointid(qh, point));
    qh_fprintf(qh, fp, 9008, "\n");
  }
}









































void qh_projectinput(qhT *qh) {
  int k,i;
  int newdim= qh->input_dim, newnum= qh->num_points;
  signed char *project;
  int projectsize= (qh->input_dim+1)*sizeof(*project);
  pointT *newpoints, *coord, *infinity;
  realT paraboloid, maxboloid= 0;

  project= (signed char*)qh_memalloc(qh, projectsize);
  memset((char*)project, 0, (size_t)projectsize);
  for (k=0; k < qh->input_dim; k++) {
    if (qh->lower_bound[k] == 0 && qh->upper_bound[k] == 0) {
      project[k]= -1;
      newdim--;
    }
  }
  if (qh->DELAUNAY) {
    project[k]= 1;
    newdim++;
    if (qh->ATinfinity)
      newnum++;
  }
  if (newdim != qh->hull_dim) {
    qh_memfree(qh, project, projectsize);
    qh_fprintf(qh, qh->ferr, 6015, "qhull internal error (qh_projectinput): dimension after projection %d != hull_dim %d\n", newdim, qh->hull_dim);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  if (!(newpoints= qh->temp_malloc= (coordT*)qh_malloc(newnum*newdim*sizeof(coordT)))){
    qh_memfree(qh, project, projectsize);
    qh_fprintf(qh, qh->ferr, 6016, "qhull error: insufficient memory to project %d points\n",
           qh->num_points);
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }

  qh_projectpoints(qh, project, qh->input_dim+1, qh->first_point,
                    qh->num_points, qh->input_dim, newpoints, newdim);
  trace1((qh, qh->ferr, 1003, "qh_projectinput: updating lower and upper_bound\n"));
  qh_projectpoints(qh, project, qh->input_dim+1, qh->lower_bound,
                    1, qh->input_dim+1, qh->lower_bound, newdim+1);
  qh_projectpoints(qh, project, qh->input_dim+1, qh->upper_bound,
                    1, qh->input_dim+1, qh->upper_bound, newdim+1);
  if (qh->HALFspace) {
    if (!qh->feasible_point) {
      qh_memfree(qh, project, projectsize);
      qh_fprintf(qh, qh->ferr, 6017, "qhull internal error (qh_projectinput): HALFspace defined without qh.feasible_point\n");
      qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }
    qh_projectpoints(qh, project, qh->input_dim, qh->feasible_point,
                      1, qh->input_dim, qh->feasible_point, newdim);
  }
  qh_memfree(qh, project, projectsize);
  if (qh->POINTSmalloc)
    qh_free(qh->first_point);
  qh->first_point= newpoints;
  qh->POINTSmalloc= True;
  qh->temp_malloc= NULL;
  if (qh->DELAUNAY && qh->ATinfinity) {
    coord= qh->first_point;
    infinity= qh->first_point + qh->hull_dim * qh->num_points;
    for (k=qh->hull_dim-1; k--; )
      infinity[k]= 0.0;
    for (i=qh->num_points; i--; ) {
      paraboloid= 0.0;
      for (k=0; k < qh->hull_dim-1; k++) {
        paraboloid += *coord * *coord;
        infinity[k] += *coord;
        coord++;
      }
      *(coord++)= paraboloid;
      maximize_(maxboloid, paraboloid);
    }

    for (k=qh->hull_dim-1; k--; )
      *(coord++) /= qh->num_points;
    *(coord++)= maxboloid * 1.1;
    qh->num_points++;
    trace0((qh, qh->ferr, 9, "qh_projectinput: projected points to paraboloid for Delaunay\n"));
  }else if (qh->DELAUNAY)
    qh_setdelaunay(qh, qh->hull_dim, qh->num_points, qh->first_point);
}



























void qh_projectpoints(qhT *qh, signed char *project, int n, realT *points,
        int numpoints, int dim, realT *newpoints, int newdim) {
  int testdim= dim, oldk=0, newk=0, i,j=0,k;
  realT *newp, *oldp;

  for (k=0; k < n; k++)
    testdim += project[k];
  if (testdim != newdim) {
    qh_fprintf(qh, qh->ferr, 6018, "qhull internal error (qh_projectpoints): newdim %d should be %d after projection\n",
      newdim, testdim);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  for (j=0; j<n; j++) {
    if (project[j] == -1)
      oldk++;
    else {
      newp= newpoints+newk++;
      if (project[j] == +1) {
        if (oldk >= dim)
          continue;
        oldp= points+oldk;
      }else
        oldp= points+oldk++;
      for (i=numpoints; i--; ) {
        *newp= *oldp;
        newp += newdim;
        oldp += dim;
      }
    }
    if (oldk >= dim)
      break;
  }
  trace1((qh, qh->ferr, 1004, "qh_projectpoints: projected %d points from dim %d to dim %d\n",
    numpoints, dim, newdim));
}


















void qh_rotateinput(qhT *qh, realT **rows) {

  if (!qh->POINTSmalloc) {
    qh->first_point= qh_copypoints(qh, qh->first_point, qh->num_points, qh->hull_dim);
    qh->POINTSmalloc= True;
  }
  qh_rotatepoints(qh, qh->first_point, qh->num_points, qh->hull_dim, rows);
}


















void qh_rotatepoints(qhT *qh, realT *points, int numpoints, int dim, realT **row) {
  realT *point, *rowi, *coord= NULL, sum, *newval;
  int i,j,k;

  if (qh->IStracing >= 1)
    qh_printmatrix(qh, qh->ferr, "qh_rotatepoints: rotate points by", row, dim, dim);
  for (point= points, j= numpoints; j--; point += dim) {
    newval= row[dim];
    for (i=0; i < dim; i++) {
      rowi= row[i];
      coord= point;
      for (sum= 0.0, k= dim; k--; )
        sum += *rowi++ * *coord++;
      *(newval++)= sum;
    }
    for (k=dim; k--; )
      *(--coord)= *(--newval);
  }
}

















void qh_scaleinput(qhT *qh) {

  if (!qh->POINTSmalloc) {
    qh->first_point= qh_copypoints(qh, qh->first_point, qh->num_points, qh->hull_dim);
    qh->POINTSmalloc= True;
  }
  qh_scalepoints(qh, qh->first_point, qh->num_points, qh->hull_dim,
       qh->lower_bound, qh->upper_bound);
}




















void qh_scalelast(qhT *qh, coordT *points, int numpoints, int dim, coordT low,
                   coordT high, coordT newhigh) {
  realT scale, shift;
  coordT *coord;
  int i;
  boolT nearzero= False;

  trace4((qh, qh->ferr, 4013, "qh_scalelast: scale last coordinate from [%2.2g, %2.2g] to [0,%2.2g]\n",
    low, high, newhigh));
  qh->last_low= low;
  qh->last_high= high;
  qh->last_newhigh= newhigh;
  scale= qh_divzero(newhigh, high - low,
                  qh->MINdenom_1, &nearzero);
  if (nearzero) {
    if (qh->DELAUNAY)
      qh_fprintf(qh, qh->ferr, 6019, "qhull input error: can not scale last coordinate.  Input is cocircular\n   or cospherical.   Use option 'Qz' to add a point at infinity.\n");
    else
      qh_fprintf(qh, qh->ferr, 6020, "qhull input error: can not scale last coordinate.  New bounds [0, %2.2g] are too wide for\nexisting bounds [%2.2g, %2.2g] (width %2.2g)\n",
                newhigh, low, high, high-low);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  shift= - low * newhigh / (high-low);
  coord= points + dim - 1;
  for (i=numpoints; i--; coord += dim)
    *coord= *coord * scale + shift;
}



















void qh_scalepoints(qhT *qh, pointT *points, int numpoints, int dim,
        realT *newlows, realT *newhighs) {
  int i,k;
  realT shift, scale, *coord, low, high, newlow, newhigh, mincoord, maxcoord;
  boolT nearzero= False;

  for (k=0; k < dim; k++) {
    newhigh= newhighs[k];
    newlow= newlows[k];
    if (newhigh > REALmax/2 && newlow < -REALmax/2)
      continue;
    low= REALmax;
    high= -REALmax;
    for (i=numpoints, coord=points+k; i--; coord += dim) {
      minimize_(low, *coord);
      maximize_(high, *coord);
    }
    if (newhigh > REALmax/2)
      newhigh= high;
    if (newlow < -REALmax/2)
      newlow= low;
    if (qh->DELAUNAY && k == dim-1 && newhigh < newlow) {
      qh_fprintf(qh, qh->ferr, 6021, "qhull input error: 'Qb%d' or 'QB%d' inverts paraboloid since high bound %.2g < low bound %.2g\n",
               k, k, newhigh, newlow);
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    scale= qh_divzero(newhigh - newlow, high - low,
                  qh->MINdenom_1, &nearzero);
    if (nearzero) {
      qh_fprintf(qh, qh->ferr, 6022, "qhull input error: %d'th dimension's new bounds [%2.2g, %2.2g] too wide for\nexisting bounds [%2.2g, %2.2g]\n",
              k, newlow, newhigh, low, high);
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    shift= (newlow * high - low * newhigh)/(high-low);
    coord= points+k;
    for (i=numpoints; i--; coord += dim)
      *coord= *coord * scale + shift;
    coord= points+k;
    if (newlow < newhigh) {
      mincoord= newlow;
      maxcoord= newhigh;
    }else {
      mincoord= newhigh;
      maxcoord= newlow;
    }
    for (i=numpoints; i--; coord += dim) {
      minimize_(*coord, maxcoord);
      maximize_(*coord, mincoord);
    }
    trace0((qh, qh->ferr, 10, "qh_scalepoints: scaled %d'th coordinate [%2.2g, %2.2g] to [%.2g, %.2g] for %d points by %2.2g and shifted %2.2g\n",
      k, low, high, newlow, newhigh, numpoints, scale, shift));
  }
}
































void qh_setdelaunay(qhT *qh, int dim, int count, pointT *points) {
  int i, k;
  coordT *coordp, coord;
  realT paraboloid;

  trace0((qh, qh->ferr, 11, "qh_setdelaunay: project %d points to paraboloid for Delaunay triangulation\n", count));
  coordp= points;
  for (i=0; i < count; i++) {
    coord= *coordp++;
    paraboloid= coord*coord;
    for (k=dim-2; k--; ) {
      coord= *coordp++;
      paraboloid += coord*coord;
    }
    *coordp++ = paraboloid;
  }
  if (qh->last_low < REALmax/2)
    qh_scalelast(qh, points, count, dim, qh->last_low, qh->last_high, qh->last_newhigh);
}



















boolT qh_sethalfspace(qhT *qh, int dim, coordT *coords, coordT **nextp,
         coordT *normal, coordT *offset, coordT *feasible) {
  coordT *normp= normal, *feasiblep= feasible, *coordp= coords;
  realT dist;
  realT r;
  int k;
  boolT zerodiv;

  dist= *offset;
  for (k=dim; k--; )
    dist += *(normp++) * *(feasiblep++);
  if (dist > 0)
    goto LABELerroroutside;
  normp= normal;
  if (dist < -qh->MINdenom) {
    for (k=dim; k--; )
      *(coordp++)= *(normp++) / -dist;
  }else {
    for (k=dim; k--; ) {
      *(coordp++)= qh_divzero(*(normp++), -dist, qh->MINdenom_1, &zerodiv);
      if (zerodiv)
        goto LABELerroroutside;
    }
  }
  *nextp= coordp;
  if (qh->IStracing >= 4) {
    qh_fprintf(qh, qh->ferr, 8021, "qh_sethalfspace: halfspace at offset %6.2g to point: ", *offset);
    for (k=dim, coordp=coords; k--; ) {
      r= *coordp++;
      qh_fprintf(qh, qh->ferr, 8022, " %6.2g", r);
    }
    qh_fprintf(qh, qh->ferr, 8023, "\n");
  }
  return True;
LABELerroroutside:
  feasiblep= feasible;
  normp= normal;
  qh_fprintf(qh, qh->ferr, 6023, "qhull input error: feasible point is not clearly inside halfspace\nfeasible point: ");
  for (k=dim; k--; )
    qh_fprintf(qh, qh->ferr, 8024, qh_REAL_1, r=*(feasiblep++));
  qh_fprintf(qh, qh->ferr, 8025, "\n     halfspace: ");
  for (k=dim; k--; )
    qh_fprintf(qh, qh->ferr, 8026, qh_REAL_1, r=*(normp++));
  qh_fprintf(qh, qh->ferr, 8027, "\n     at offset: ");
  qh_fprintf(qh, qh->ferr, 8028, qh_REAL_1, *offset);
  qh_fprintf(qh, qh->ferr, 8029, " and distance: ");
  qh_fprintf(qh, qh->ferr, 8030, qh_REAL_1, dist);
  qh_fprintf(qh, qh->ferr, 8031, "\n");
  return False;
}
























coordT *qh_sethalfspace_all(qhT *qh, int dim, int count, coordT *halfspaces, pointT *feasible) {
  int i, newdim;
  pointT *newpoints;
  coordT *coordp, *normalp, *offsetp;

  trace0((qh, qh->ferr, 12, "qh_sethalfspace_all: compute dual for halfspace intersection\n"));
  newdim= dim - 1;
  if (!(newpoints=(coordT*)qh_malloc(count*newdim*sizeof(coordT)))){
    qh_fprintf(qh, qh->ferr, 6024, "qhull error: insufficient memory to compute dual of %d halfspaces\n",
          count);
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }
  coordp= newpoints;
  normalp= halfspaces;
  for (i=0; i < count; i++) {
    offsetp= normalp + newdim;
    if (!qh_sethalfspace(qh, newdim, coordp, &coordp, normalp, offsetp, feasible)) {
      qh_free(newpoints);
      qh_fprintf(qh, qh->ferr, 8032, "The halfspace was at index %d\n", i);
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    normalp= offsetp + 1;
  }
  return newpoints;
}


















boolT qh_sharpnewfacets(qhT *qh) {
  facetT *facet;
  boolT issharp = False;
  int *quadrant, k;

  quadrant= (int*)qh_memalloc(qh, qh->hull_dim * sizeof(int));
  FORALLfacet_(qh->newfacet_list) {
    if (facet == qh->newfacet_list) {
      for (k=qh->hull_dim; k--; )
        quadrant[ k]= (facet->normal[ k] > 0);
    }else {
      for (k=qh->hull_dim; k--; ) {
        if (quadrant[ k] != (facet->normal[ k] > 0)) {
          issharp= True;
          break;
        }
      }
    }
    if (issharp)
      break;
  }
  qh_memfree(qh, quadrant, qh->hull_dim * sizeof(int));
  trace3((qh, qh->ferr, 3001, "qh_sharpnewfacets: %d\n", issharp));
  return issharp;
}


























pointT *qh_voronoi_center(qhT *qh, int dim, setT *points) {
  pointT *point, **pointp, *point0;
  pointT *center= (pointT*)qh_memalloc(qh, qh->center_size);
  setT *simplex;
  int i, j, k, size= qh_setsize(qh, points);
  coordT *gmcoord;
  realT *diffp, sum2, *sum2row, *sum2p, det, factor;
  boolT nearzero, infinite;

  if (size == dim+1)
    simplex= points;
  else if (size < dim+1) {
    qh_memfree(qh, center, qh->center_size);
    qh_fprintf(qh, qh->ferr, 6025, "qhull internal error (qh_voronoi_center):\n  need at least %d points to construct a Voronoi center\n",
             dim+1);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    simplex= points;
  }else {
    simplex= qh_settemp(qh, dim+1);
    qh_maxsimplex(qh, dim, points, NULL, 0, &simplex);
  }
  point0= SETfirstt_(simplex, pointT);
  gmcoord= qh->gm_matrix;
  for (k=0; k < dim; k++) {
    qh->gm_row[k]= gmcoord;
    FOREACHpoint_(simplex) {
      if (point != point0)
        *(gmcoord++)= point[k] - point0[k];
    }
  }
  sum2row= gmcoord;
  for (i=0; i < dim; i++) {
    sum2= 0.0;
    for (k=0; k < dim; k++) {
      diffp= qh->gm_row[k] + i;
      sum2 += *diffp * *diffp;
    }
    *(gmcoord++)= sum2;
  }
  det= qh_determinant(qh, qh->gm_row, dim, &nearzero);
  factor= qh_divzero(0.5, det, qh->MINdenom, &infinite);
  if (infinite) {
    for (k=dim; k--; )
      center[k]= (float)qh_INFINITE;
    if (qh->IStracing)
      qh_printpoints(qh, qh->ferr, "qh_voronoi_center: at infinity for ", simplex);
  }else {
    for (i=0; i < dim; i++) {
      gmcoord= qh->gm_matrix;
      sum2p= sum2row;
      for (k=0; k < dim; k++) {
        qh->gm_row[k]= gmcoord;
        if (k == i) {
          for (j=dim; j--; )
            *(gmcoord++)= *sum2p++;
        }else {
          FOREACHpoint_(simplex) {
            if (point != point0)
              *(gmcoord++)= point[k] - point0[k];
          }
        }
      }
      center[i]= qh_determinant(qh, qh->gm_row, dim, &nearzero)*factor + point0[i];
    }
#ifndef qh_NOtrace
    if (qh->IStracing >= 3) {
      qh_fprintf(qh, qh->ferr, 8033, "qh_voronoi_center: det %2.2g factor %2.2g ", det, factor);
      qh_printmatrix(qh, qh->ferr, "center:", &center, 1, dim);
      if (qh->IStracing >= 5) {
        qh_printpoints(qh, qh->ferr, "points", simplex);
        FOREACHpoint_(simplex)
          qh_fprintf(qh, qh->ferr, 8034, "p%d dist %.2g, ", qh_pointid(qh, point),
                   qh_pointdist(point, center, dim));
        qh_fprintf(qh, qh->ferr, 8035, "\n");
      }
    }
#endif
  }
  if (simplex != points)
    qh_settempfree(qh, &simplex);
  return center;
}
