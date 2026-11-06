/*<html><pre>  -<a                             href="qh-geom_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   geom_r.c
   geometric routines of qhull

   see qh-geom_r.htm and geom_r.h

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/geom_r.c#2 $$Change: 1995 $
   $DateTime: 2015/10/13 21:59:42 $$Author: bbarber $

   infrequent code goes into geom2_r.c
*/

#include "qhull_ra.h"



















void qh_distplane(qhT *qh, pointT *point, facetT *facet, realT *dist) {
  coordT *normal= facet->normal, *coordp, randr;
  int k;

  switch (qh->hull_dim){
  case 2:
    *dist= facet->offset + point[0] * normal[0] + point[1] * normal[1];
    break;
  case 3:
    *dist= facet->offset + point[0] * normal[0] + point[1] * normal[1] + point[2] * normal[2];
    break;
  case 4:
    *dist= facet->offset+point[0]*normal[0]+point[1]*normal[1]+point[2]*normal[2]+point[3]*normal[3];
    break;
  case 5:
    *dist= facet->offset+point[0]*normal[0]+point[1]*normal[1]+point[2]*normal[2]+point[3]*normal[3]+point[4]*normal[4];
    break;
  case 6:
    *dist= facet->offset+point[0]*normal[0]+point[1]*normal[1]+point[2]*normal[2]+point[3]*normal[3]+point[4]*normal[4]+point[5]*normal[5];
    break;
  case 7:
    *dist= facet->offset+point[0]*normal[0]+point[1]*normal[1]+point[2]*normal[2]+point[3]*normal[3]+point[4]*normal[4]+point[5]*normal[5]+point[6]*normal[6];
    break;
  case 8:
    *dist= facet->offset+point[0]*normal[0]+point[1]*normal[1]+point[2]*normal[2]+point[3]*normal[3]+point[4]*normal[4]+point[5]*normal[5]+point[6]*normal[6]+point[7]*normal[7];
    break;
  default:
    *dist= facet->offset;
    coordp= point;
    for (k=qh->hull_dim; k--; )
      *dist += *coordp++ * *normal++;
    break;
  }
  zinc_(Zdistplane);
  if (!qh->RANDOMdist && qh->IStracing < 4)
    return;
  if (qh->RANDOMdist) {
    randr= qh_RANDOMint;
    *dist += (2.0 * randr / qh_RANDOMmax - 1.0) *
      qh->RANDOMfactor * qh->MAXabs_coord;
  }
  if (qh->IStracing >= 4) {
    qh_fprintf(qh, qh->ferr, 8001, "qh_distplane: ");
    qh_fprintf(qh, qh->ferr, 8002, qh_REAL_1, *dist);
    qh_fprintf(qh, qh->ferr, 8003, "from p%d to f%d\n", qh_pointid(qh, point), facet->id);
  }
  return;
}
























































facetT *qh_findbest(qhT *qh, pointT *point, facetT *startfacet,
                     boolT bestoutside, boolT isnewfacets, boolT noupper,
                     realT *dist, boolT *isoutside, int *numpart) {
  realT bestdist= -REALmax/2  ;
  facetT *facet, *neighbor, **neighborp;
  facetT *bestfacet= NULL, *lastfacet= NULL;
  int oldtrace= qh->IStracing;
  unsigned int visitid= ++qh->visit_id;
  int numpartnew=0;
  boolT testhorizon = True;

  zinc_(Zfindbest);
  if (qh->IStracing >= 3 || (qh->TRACElevel && qh->TRACEpoint >= 0 && qh->TRACEpoint == qh_pointid(qh, point))) {
    if (qh->TRACElevel > qh->IStracing)
      qh->IStracing= qh->TRACElevel;
    qh_fprintf(qh, qh->ferr, 8004, "qh_findbest: point p%d starting at f%d isnewfacets? %d, unless %d exit if > %2.2g\n",
             qh_pointid(qh, point), startfacet->id, isnewfacets, bestoutside, qh->MINoutside);
    qh_fprintf(qh, qh->ferr, 8005, "  testhorizon? %d noupper? %d", testhorizon, noupper);
    qh_fprintf(qh, qh->ferr, 8006, "  Last point added was p%d.", qh->furthest_id);
    qh_fprintf(qh, qh->ferr, 8007, "  Last merge was #%d.  max_outside %2.2g\n", zzval_(Ztotmerge), qh->max_outside);
  }
  if (isoutside)
    *isoutside= True;
  if (!startfacet->flipped) {
    *numpart= 1;
    qh_distplane(qh, point, startfacet, dist);
    if (!bestoutside && *dist >= qh->MINoutside
    && (!startfacet->upperdelaunay || !noupper)) {
      bestfacet= startfacet;
      goto LABELreturn_best;
    }
    bestdist= *dist;
    if (!startfacet->upperdelaunay) {
      bestfacet= startfacet;
    }
  }else
    *numpart= 0;
  startfacet->visitid= visitid;
  facet= startfacet;
  while (facet) {
    trace4((qh, qh->ferr, 4001, "qh_findbest: neighbors of f%d, bestdist %2.2g f%d\n",
                facet->id, bestdist, getid_(bestfacet)));
    lastfacet= facet;
    FOREACHneighbor_(facet) {
      if (!neighbor->newfacet && isnewfacets)
        continue;
      if (neighbor->visitid == visitid)
        continue;
      neighbor->visitid= visitid;
      if (!neighbor->flipped) {
        (*numpart)++;
        qh_distplane(qh, point, neighbor, dist);
        if (*dist > bestdist) {
          if (!bestoutside && *dist >= qh->MINoutside
          && (!neighbor->upperdelaunay || !noupper)) {
            bestfacet= neighbor;
            goto LABELreturn_best;
          }
          if (!neighbor->upperdelaunay) {
            bestfacet= neighbor;
            bestdist= *dist;
            break;
          }else if (!bestfacet) {
            bestdist= *dist;
            break;
          }
        }
      }
    }
    facet= neighbor;
  }
  if (isnewfacets) {
    if (!bestfacet) {
      bestdist= -REALmax/2;
      bestfacet= qh_findbestnew(qh, point, startfacet->next, &bestdist, bestoutside, isoutside, &numpartnew);
      testhorizon= False;
    }else if (!qh->findbest_notsharp && bestdist < - qh->DISTround) {
      if (qh_sharpnewfacets(qh)) {

        zinc_(Zfindnewsharp);
        bestfacet= qh_findbestnew(qh, point, bestfacet, &bestdist, bestoutside, isoutside, &numpartnew);
        testhorizon= False;
        qh->findbestnew= True;
      }else
        qh->findbest_notsharp= True;
    }
  }
  if (!bestfacet)
    bestfacet= qh_findbestlower(qh, lastfacet, point, &bestdist, numpart);
  if (testhorizon)
    bestfacet= qh_findbesthorizon(qh, !qh_IScheckmax, point, bestfacet, noupper, &bestdist, &numpartnew);
  *dist= bestdist;
  if (isoutside && bestdist < qh->MINoutside)
    *isoutside= False;
LABELreturn_best:
  zadd_(Zfindbesttot, *numpart);
  zmax_(Zfindbestmax, *numpart);
  (*numpart) += numpartnew;
  qh->IStracing= oldtrace;
  return bestfacet;
}








































facetT *qh_findbesthorizon(qhT *qh, boolT ischeckmax, pointT* point, facetT *startfacet, boolT noupper, realT *bestdist, int *numpart) {
  facetT *bestfacet= startfacet;
  realT dist;
  facetT *neighbor, **neighborp, *facet;
  facetT *nextfacet= NULL;
  int numpartinit= *numpart, coplanarfacetset_size;
  unsigned int visitid= ++qh->visit_id;
  boolT newbest= False;
  realT minsearch, searchdist;

  if (!ischeckmax) {
    zinc_(Zfindhorizon);
  }else {
#if qh_MAXoutside
    if ((!qh->ONLYgood || startfacet->good) && *bestdist > startfacet->maxoutside)
      startfacet->maxoutside= *bestdist;
#endif
  }
  searchdist= qh_SEARCHdist;
  minsearch= *bestdist - searchdist;
  if (ischeckmax) {

    minimize_(minsearch, -searchdist);
  }
  coplanarfacetset_size= 0;
  facet= startfacet;
  while (True) {
    trace4((qh, qh->ferr, 4002, "qh_findbesthorizon: neighbors of f%d bestdist %2.2g f%d ischeckmax? %d noupper? %d minsearch %2.2g searchdist %2.2g\n",
                facet->id, *bestdist, getid_(bestfacet), ischeckmax, noupper,
                minsearch, searchdist));
    FOREACHneighbor_(facet) {
      if (neighbor->visitid == visitid)
        continue;
      neighbor->visitid= visitid;
      if (!neighbor->flipped) {
        qh_distplane(qh, point, neighbor, &dist);
        (*numpart)++;
        if (dist > *bestdist) {
          if (!neighbor->upperdelaunay || ischeckmax || (!noupper && dist >= qh->MINoutside)) {
            bestfacet= neighbor;
            *bestdist= dist;
            newbest= True;
            if (!ischeckmax) {
              minsearch= dist - searchdist;
              if (dist > *bestdist + searchdist) {
                zinc_(Zfindjump);
                coplanarfacetset_size= 0;
              }
            }
          }
        }else if (dist < minsearch)
          continue;
#if qh_MAXoutside
        if (ischeckmax && dist > neighbor->maxoutside)
          neighbor->maxoutside= dist;
#endif
      }
      if (nextfacet) {
        if (!coplanarfacetset_size++) {
          SETfirst_(qh->coplanarfacetset)= nextfacet;
          SETtruncate_(qh->coplanarfacetset, 1);
        }else
          qh_setappend(qh, &qh->coplanarfacetset, nextfacet);

      }
      nextfacet= neighbor;
    }
    facet= nextfacet;
    if (facet)
      nextfacet= NULL;
    else if (!coplanarfacetset_size)
      break;
    else if (!--coplanarfacetset_size) {
      facet= SETfirstt_(qh->coplanarfacetset, facetT);
      SETtruncate_(qh->coplanarfacetset, 0);
    }else
      facet= (facetT*)qh_setdellast(qh->coplanarfacetset);
  }
  if (!ischeckmax) {
    zadd_(Zfindhorizontot, *numpart - numpartinit);
    zmax_(Zfindhorizonmax, *numpart - numpartinit);
    if (newbest)
      zinc_(Zparthorizon);
  }
  trace4((qh, qh->ferr, 4003, "qh_findbesthorizon: newbest? %d bestfacet f%d bestdist %2.2g\n", newbest, getid_(bestfacet), *bestdist));
  return bestfacet;
}












































facetT *qh_findbestnew(qhT *qh, pointT *point, facetT *startfacet,
           realT *dist, boolT bestoutside, boolT *isoutside, int *numpart) {
  realT bestdist= -REALmax/2;
  facetT *bestfacet= NULL, *facet;
  int oldtrace= qh->IStracing, i;
  unsigned int visitid= ++qh->visit_id;
  realT distoutside= 0.0;
  boolT isdistoutside;
  boolT testhorizon = True;

  if (!startfacet) {
    if (qh->MERGING)
      qh_fprintf(qh, qh->ferr, 6001, "qhull precision error (qh_findbestnew): merging has formed and deleted a cone of new facets.  Can not continue.\n");
    else
      qh_fprintf(qh, qh->ferr, 6002, "qhull internal error (qh_findbestnew): no new facets for point p%d\n",
              qh->furthest_id);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  zinc_(Zfindnew);
  if (qh->BESToutside || bestoutside)
    isdistoutside= False;
  else {
    isdistoutside= True;
    distoutside= qh_DISToutside;
  }
  if (isoutside)
    *isoutside= True;
  *numpart= 0;
  if (qh->IStracing >= 3 || (qh->TRACElevel && qh->TRACEpoint >= 0 && qh->TRACEpoint == qh_pointid(qh, point))) {
    if (qh->TRACElevel > qh->IStracing)
      qh->IStracing= qh->TRACElevel;
    qh_fprintf(qh, qh->ferr, 8008, "qh_findbestnew: point p%d facet f%d. Stop? %d if dist > %2.2g\n",
             qh_pointid(qh, point), startfacet->id, isdistoutside, distoutside);
    qh_fprintf(qh, qh->ferr, 8009, "  Last point added p%d visitid %d.",  qh->furthest_id, visitid);
    qh_fprintf(qh, qh->ferr, 8010, "  Last merge was #%d.\n", zzval_(Ztotmerge));
  }

  for (i=0, facet=startfacet; i < 2; i++, facet= qh->newfacet_list) {
    FORALLfacet_(facet) {
      if (facet == startfacet && i)
        break;
      facet->visitid= visitid;
      if (!facet->flipped) {
        qh_distplane(qh, point, facet, dist);
        (*numpart)++;
        if (*dist > bestdist) {
          if (!facet->upperdelaunay || *dist >= qh->MINoutside) {
            bestfacet= facet;
            if (isdistoutside && *dist >= distoutside)
              goto LABELreturn_bestnew;
            bestdist= *dist;
          }
        }
      }
    }
  }
  if (testhorizon || !bestfacet)
    bestfacet= qh_findbesthorizon(qh, !qh_IScheckmax, point, bestfacet ? bestfacet : startfacet,
                                        !qh_NOupper, &bestdist, numpart);
  *dist= bestdist;
  if (isoutside && *dist < qh->MINoutside)
    *isoutside= False;
LABELreturn_bestnew:
  zadd_(Zfindnewtot, *numpart);
  zmax_(Zfindnewmax, *numpart);
  trace4((qh, qh->ferr, 4004, "qh_findbestnew: bestfacet f%d bestdist %2.2g\n", getid_(bestfacet), *dist));
  qh->IStracing= oldtrace;
  return bestfacet;
}








































void qh_backnormal(qhT *qh, realT **rows, int numrow, int numcol, boolT sign,
        coordT *normal, boolT *nearzero) {
  int i, j;
  coordT *normalp, *normal_tail, *ai, *ak;
  realT diagonal;
  boolT waszero;
  int zerocol= -1;

  normalp= normal + numcol - 1;
  *normalp--= (sign ? -1.0 : 1.0);
  for (i=numrow; i--; ) {
    *normalp= 0.0;
    ai= rows[i] + i + 1;
    ak= normalp+1;
    for (j=i+1; j < numcol; j++)
      *normalp -= *ai++ * *ak++;
    diagonal= (rows[i])[i];
    if (fabs_(diagonal) > qh->MINdenom_2)
      *(normalp--) /= diagonal;
    else {
      waszero= False;
      *normalp= qh_divzero(*normalp, diagonal, qh->MINdenom_1_2, &waszero);
      if (waszero) {
        zerocol= i;
        *(normalp--)= (sign ? -1.0 : 1.0);
        for (normal_tail= normalp+2; normal_tail < normal + numcol; normal_tail++)
          *normal_tail= 0.0;
      }else
        normalp--;
    }
  }
  if (zerocol != -1) {
    zzinc_(Zback0);
    *nearzero= True;
    trace4((qh, qh->ferr, 4005, "qh_backnormal: zero diagonal at column %d.\n", i));
    qh_precision(qh, "zero diagonal on back substitution");
  }
}






















void qh_gausselim(qhT *qh, realT **rows, int numrow, int numcol, boolT *sign, boolT *nearzero) {
  realT *ai, *ak, *rowp, *pivotrow;
  realT n, pivot, pivot_abs= 0.0, temp;
  int i, j, k, pivoti, flip=0;

  *nearzero= False;
  for (k=0; k < numrow; k++) {
    pivot_abs= fabs_((rows[k])[k]);
    pivoti= k;
    for (i=k+1; i < numrow; i++) {
      if ((temp= fabs_((rows[i])[k])) > pivot_abs) {
        pivot_abs= temp;
        pivoti= i;
      }
    }
    if (pivoti != k) {
      rowp= rows[pivoti];
      rows[pivoti]= rows[k];
      rows[k]= rowp;
      *sign ^= 1;
      flip ^= 1;
    }
    if (pivot_abs <= qh->NEARzero[k]) {
      *nearzero= True;
      if (pivot_abs == 0.0) {
        if (qh->IStracing >= 4) {
          qh_fprintf(qh, qh->ferr, 8011, "qh_gausselim: 0 pivot at column %d. (%2.2g < %2.2g)\n", k, pivot_abs, qh->DISTround);
          qh_printmatrix(qh, qh->ferr, "Matrix:", rows, numrow, numcol);
        }
        zzinc_(Zgauss0);
        qh_precision(qh, "zero pivot for Gaussian elimination");
        goto LABELnextcol;
      }
    }
    pivotrow= rows[k] + k;
    pivot= *pivotrow++;
    for (i=k+1; i < numrow; i++) {
      ai= rows[i] + k;
      ak= pivotrow;
      n= (*ai++)/pivot;
      for (j= numcol - (k+1); j--; )
        *ai++ -= n * *ak++;
    }
  LABELnextcol:
    ;
  }
  wmin_(Wmindenom, pivot_abs);
  if (qh->IStracing >= 5)
    qh_printmatrix(qh, qh->ferr, "qh_gausselem: result", rows, numrow, numcol);
}













realT qh_getangle(qhT *qh, pointT *vect1, pointT *vect2) {
  realT angle= 0, randr;
  int k;

  for (k=qh->hull_dim; k--; )
    angle += *vect1++ * *vect2++;
  if (qh->RANDOMdist) {
    randr= qh_RANDOMint;
    angle += (2.0 * randr / qh_RANDOMmax - 1.0) *
      qh->RANDOMfactor;
  }
  trace4((qh, qh->ferr, 4006, "qh_getangle: %2.2g\n", angle));
  return(angle);
}











pointT *qh_getcenter(qhT *qh, setT *vertices) {
  int k;
  pointT *center, *coord;
  vertexT *vertex, **vertexp;
  int count= qh_setsize(qh, vertices);

  if (count < 2) {
    qh_fprintf(qh, qh->ferr, 6003, "qhull internal error (qh_getcenter): not defined for %d points\n", count);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  center= (pointT *)qh_memalloc(qh, qh->normal_size);
  for (k=0; k < qh->hull_dim; k++) {
    coord= center+k;
    *coord= 0.0;
    FOREACHvertex_(vertices)
      *coord += vertex->point[k];
    *coord /= count;
  }
  return(center);
}











pointT *qh_getcentrum(qhT *qh, facetT *facet) {
  realT dist;
  pointT *centrum, *point;

  point= qh_getcenter(qh, facet->vertices);
  zzinc_(Zcentrumtests);
  qh_distplane(qh, point, facet, &dist);
  centrum= qh_projectpoint(qh, point, facet, dist);
  qh_memfree(qh, point, qh->normal_size);
  trace4((qh, qh->ferr, 4007, "qh_getcentrum: for f%d, %d vertices dist= %2.2g\n",
          facet->id, qh_setsize(qh, facet->vertices), dist));
  return centrum;
}















realT qh_getdistance(qhT *qh, facetT *facet, facetT *neighbor, realT *mindist, realT *maxdist) {
  vertexT *vertex, **vertexp;
  realT dist, maxd, mind;

  FOREACHvertex_(facet->vertices)
    vertex->seen= False;
  FOREACHvertex_(neighbor->vertices)
    vertex->seen= True;
  mind= 0.0;
  maxd= 0.0;
  FOREACHvertex_(facet->vertices) {
    if (!vertex->seen) {
      zzinc_(Zbestdist);
      qh_distplane(qh, vertex->point, neighbor, &dist);
      if (dist < mind)
        mind= dist;
      else if (dist > maxd)
        maxd= dist;
    }
  }
  *mindist= mind;
  *maxdist= maxd;
  mind= -mind;
  if (maxd > mind)
    return maxd;
  else
    return mind;
}












void qh_normalize(qhT *qh, coordT *normal, int dim, boolT toporient) {
  qh_normalize2(qh, normal, dim, toporient, NULL, NULL);
}

































void qh_normalize2(qhT *qh, coordT *normal, int dim, boolT toporient,
            realT *minnorm, boolT *ismin) {
  int k;
  realT *colp, *maxp, norm= 0, temp, *norm1, *norm2, *norm3;
  boolT zerodiv;

  norm1= normal+1;
  norm2= normal+2;
  norm3= normal+3;
  if (dim == 2)
    norm= sqrt((*normal)*(*normal) + (*norm1)*(*norm1));
  else if (dim == 3)
    norm= sqrt((*normal)*(*normal) + (*norm1)*(*norm1) + (*norm2)*(*norm2));
  else if (dim == 4) {
    norm= sqrt((*normal)*(*normal) + (*norm1)*(*norm1) + (*norm2)*(*norm2)
               + (*norm3)*(*norm3));
  }else if (dim > 4) {
    norm= (*normal)*(*normal) + (*norm1)*(*norm1) + (*norm2)*(*norm2)
               + (*norm3)*(*norm3);
    for (k=dim-4, colp=normal+4; k--; colp++)
      norm += (*colp) * (*colp);
    norm= sqrt(norm);
  }
  if (minnorm) {
    if (norm < *minnorm)
      *ismin= True;
    else
      *ismin= False;
  }
  wmin_(Wmindenom, norm);
  if (norm > qh->MINdenom) {
    if (!toporient)
      norm= -norm;
    *normal /= norm;
    *norm1 /= norm;
    if (dim == 2)
      ;
    else if (dim == 3)
      *norm2 /= norm;
    else if (dim == 4) {
      *norm2 /= norm;
      *norm3 /= norm;
    }else if (dim >4) {
      *norm2 /= norm;
      *norm3 /= norm;
      for (k=dim-4, colp=normal+4; k--; )
        *colp++ /= norm;
    }
  }else if (norm == 0.0) {
    temp= sqrt(1.0/dim);
    for (k=dim, colp=normal; k--; )
      *colp++ = temp;
  }else {
    if (!toporient)
      norm= -norm;
    for (k=dim, colp=normal; k--; colp++) {
      temp= qh_divzero(*colp, norm, qh->MINdenom_1, &zerodiv);
      if (!zerodiv)
        *colp= temp;
      else {
        maxp= qh_maxabsval(normal, dim);
        temp= ((*maxp * norm >= 0.0) ? 1.0 : -1.0);
        for (k=dim, colp=normal; k--; colp++)
          *colp= 0.0;
        *maxp= temp;
        zzinc_(Znearlysingular);
        trace0((qh, qh->ferr, 1, "qh_normalize: norm=%2.2g too small during p%d\n",
               norm, qh->furthest_id));
        return;
      }
    }
  }
}
















pointT *qh_projectpoint(qhT *qh, pointT *point, facetT *facet, realT dist) {
  pointT *newpoint, *np, *normal;
  int normsize= qh->normal_size;
  int k;
  void **freelistp;

  qh_memalloc_(qh, normsize, freelistp, newpoint, pointT);
  np= newpoint;
  normal= facet->normal;
  for (k=qh->hull_dim; k--; )
    *(np++)= *point++ - dist * *normal++;
  return(newpoint);
}























void qh_setfacetplane(qhT *qh, facetT *facet) {
  pointT *point;
  vertexT *vertex, **vertexp;
  int normsize= qh->normal_size;
  int k,i, oldtrace= 0;
  realT dist;
  void **freelistp;
  coordT *coord, *gmcoord;
  pointT *point0= SETfirstt_(facet->vertices, vertexT)->point;
  boolT nearzero= False;

  zzinc_(Zsetplane);
  if (!facet->normal)
    qh_memalloc_(qh, normsize, freelistp, facet->normal, coordT);
  if (facet == qh->tracefacet) {
    oldtrace= qh->IStracing;
    qh->IStracing= 5;
    qh_fprintf(qh, qh->ferr, 8012, "qh_setfacetplane: facet f%d created.\n", facet->id);
    qh_fprintf(qh, qh->ferr, 8013, "  Last point added to hull was p%d.", qh->furthest_id);
    if (zzval_(Ztotmerge))
      qh_fprintf(qh, qh->ferr, 8014, "  Last merge was #%d.", zzval_(Ztotmerge));
    qh_fprintf(qh, qh->ferr, 8015, "\n\nCurrent summary is:\n");
      qh_printsummary(qh, qh->ferr);
  }
  if (qh->hull_dim <= 4) {
    i= 0;
    if (qh->RANDOMdist) {
      gmcoord= qh->gm_matrix;
      FOREACHvertex_(facet->vertices) {
        qh->gm_row[i++]= gmcoord;
        coord= vertex->point;
        for (k=qh->hull_dim; k--; )
          *(gmcoord++)= *coord++ * qh_randomfactor(qh, qh->RANDOMa, qh->RANDOMb);
      }
    }else {
      FOREACHvertex_(facet->vertices)
       qh->gm_row[i++]= vertex->point;
    }
    qh_sethyperplane_det(qh, qh->hull_dim, qh->gm_row, point0, facet->toporient,
                facet->normal, &facet->offset, &nearzero);
  }
  if (qh->hull_dim > 4 || nearzero) {
    i= 0;
    gmcoord= qh->gm_matrix;
    FOREACHvertex_(facet->vertices) {
      if (vertex->point != point0) {
        qh->gm_row[i++]= gmcoord;
        coord= vertex->point;
        point= point0;
        for (k=qh->hull_dim; k--; )
          *(gmcoord++)= *coord++ - *point++;
      }
    }
    qh->gm_row[i]= gmcoord;
    if (qh->RANDOMdist) {
      gmcoord= qh->gm_matrix;
      for (i=qh->hull_dim-1; i--; ) {
        for (k=qh->hull_dim; k--; )
          *(gmcoord++) *= qh_randomfactor(qh, qh->RANDOMa, qh->RANDOMb);
      }
    }
    qh_sethyperplane_gauss(qh, qh->hull_dim, qh->gm_row, point0, facet->toporient,
                facet->normal, &facet->offset, &nearzero);
    if (nearzero) {
      if (qh_orientoutside(qh, facet)) {
        trace0((qh, qh->ferr, 2, "qh_setfacetplane: flipped orientation after testing interior_point during p%d\n", qh->furthest_id));










      }
    }
  }
  facet->upperdelaunay= False;
  if (qh->DELAUNAY) {
    if (qh->UPPERdelaunay) {
      if (facet->normal[qh->hull_dim -1] >= qh->ANGLEround * qh_ZEROdelaunay)
        facet->upperdelaunay= True;
    }else {
      if (facet->normal[qh->hull_dim -1] > -qh->ANGLEround * qh_ZEROdelaunay)
        facet->upperdelaunay= True;
    }
  }
  if (qh->PRINTstatistics || qh->IStracing || qh->TRACElevel || qh->JOGGLEmax < REALmax) {
    qh->old_randomdist= qh->RANDOMdist;
    qh->RANDOMdist= False;
    FOREACHvertex_(facet->vertices) {
      if (vertex->point != point0) {
        boolT istrace= False;
        zinc_(Zdiststat);
        qh_distplane(qh, vertex->point, facet, &dist);
        dist= fabs_(dist);
        zinc_(Znewvertex);
        wadd_(Wnewvertex, dist);
        if (dist > wwval_(Wnewvertexmax)) {
          wwval_(Wnewvertexmax)= dist;
          if (dist > qh->max_outside) {
            qh->max_outside= dist;
            if (dist > qh->TRACEdist)
              istrace= True;
          }
        }else if (-dist > qh->TRACEdist)
          istrace= True;
        if (istrace) {
          qh_fprintf(qh, qh->ferr, 8016, "qh_setfacetplane: ====== vertex p%d(v%d) increases max_outside to %2.2g for new facet f%d last p%d\n",
                qh_pointid(qh, vertex->point), vertex->id, dist, facet->id, qh->furthest_id);
          qh_errprint(qh, "DISTANT", facet, NULL, NULL, NULL);
        }
      }
    }
    qh->RANDOMdist= qh->old_randomdist;
  }
  if (qh->IStracing >= 3) {
    qh_fprintf(qh, qh->ferr, 8017, "qh_setfacetplane: f%d offset %2.2g normal: ",
             facet->id, facet->offset);
    for (k=0; k < qh->hull_dim; k++)
      qh_fprintf(qh, qh->ferr, 8018, "%2.2g ", facet->normal[k]);
    qh_fprintf(qh, qh->ferr, 8019, "\n");
  }
  if (facet == qh->tracefacet)
    qh->IStracing= oldtrace;
}














































void qh_sethyperplane_det(qhT *qh, int dim, coordT **rows, coordT *point0,
          boolT toporient, coordT *normal, realT *offset, boolT *nearzero) {
  realT maxround, dist;
  int i;
  pointT *point;


  if (dim == 2) {
    normal[0]= dY(1,0);
    normal[1]= dX(0,1);
    qh_normalize2(qh, normal, dim, toporient, NULL, NULL);
    *offset= -(point0[0]*normal[0]+point0[1]*normal[1]);
    *nearzero= False;
  }else if (dim == 3) {
    normal[0]= det2_(dY(2,0), dZ(2,0),
                     dY(1,0), dZ(1,0));
    normal[1]= det2_(dX(1,0), dZ(1,0),
                     dX(2,0), dZ(2,0));
    normal[2]= det2_(dX(2,0), dY(2,0),
                     dX(1,0), dY(1,0));
    qh_normalize2(qh, normal, dim, toporient, NULL, NULL);
    *offset= -(point0[0]*normal[0] + point0[1]*normal[1]
               + point0[2]*normal[2]);
    maxround= qh->DISTround;
    for (i=dim; i--; ) {
      point= rows[i];
      if (point != point0) {
        dist= *offset + (point[0]*normal[0] + point[1]*normal[1]
               + point[2]*normal[2]);
        if (dist > maxround || dist < -maxround) {
          *nearzero= True;
          break;
        }
      }
    }
  }else if (dim == 4) {
    normal[0]= - det3_(dY(2,0), dZ(2,0), dW(2,0),
                        dY(1,0), dZ(1,0), dW(1,0),
                        dY(3,0), dZ(3,0), dW(3,0));
    normal[1]=   det3_(dX(2,0), dZ(2,0), dW(2,0),
                        dX(1,0), dZ(1,0), dW(1,0),
                        dX(3,0), dZ(3,0), dW(3,0));
    normal[2]= - det3_(dX(2,0), dY(2,0), dW(2,0),
                        dX(1,0), dY(1,0), dW(1,0),
                        dX(3,0), dY(3,0), dW(3,0));
    normal[3]=   det3_(dX(2,0), dY(2,0), dZ(2,0),
                        dX(1,0), dY(1,0), dZ(1,0),
                        dX(3,0), dY(3,0), dZ(3,0));
    qh_normalize2(qh, normal, dim, toporient, NULL, NULL);
    *offset= -(point0[0]*normal[0] + point0[1]*normal[1]
               + point0[2]*normal[2] + point0[3]*normal[3]);
    maxround= qh->DISTround;
    for (i=dim; i--; ) {
      point= rows[i];
      if (point != point0) {
        dist= *offset + (point[0]*normal[0] + point[1]*normal[1]
               + point[2]*normal[2] + point[3]*normal[3]);
        if (dist > maxround || dist < -maxround) {
          *nearzero= True;
          break;
        }
      }
    }
  }
  if (*nearzero) {
    zzinc_(Zminnorm);
    trace0((qh, qh->ferr, 3, "qh_sethyperplane_det: degenerate norm during p%d.\n", qh->furthest_id));
    zzinc_(Znearlysingular);
  }
}




























void qh_sethyperplane_gauss(qhT *qh, int dim, coordT **rows, pointT *point0,
                boolT toporient, coordT *normal, coordT *offset, boolT *nearzero) {
  coordT *pointcoord, *normalcoef;
  int k;
  boolT sign= toporient, nearzero2= False;

  qh_gausselim(qh, rows, dim-1, dim, &sign, nearzero);
  for (k=dim-1; k--; ) {
    if ((rows[k])[k] < 0)
      sign ^= 1;
  }
  if (*nearzero) {
    zzinc_(Znearlysingular);
    trace0((qh, qh->ferr, 4, "qh_sethyperplane_gauss: nearly singular or axis parallel hyperplane during p%d.\n", qh->furthest_id));
    qh_backnormal(qh, rows, dim-1, dim, sign, normal, &nearzero2);
  }else {
    qh_backnormal(qh, rows, dim-1, dim, sign, normal, &nearzero2);
    if (nearzero2) {
      zzinc_(Znearlysingular);
      trace0((qh, qh->ferr, 5, "qh_sethyperplane_gauss: singular or axis parallel hyperplane at normalization during p%d.\n", qh->furthest_id));
    }
  }
  if (nearzero2)
    *nearzero= True;
  qh_normalize2(qh, normal, dim, True, NULL, NULL);
  pointcoord= point0;
  normalcoef= normal;
  *offset= -(*pointcoord++ * *normalcoef++);
  for (k=dim-1; k--; )
    *offset -= *pointcoord++ * *normalcoef++;
}
