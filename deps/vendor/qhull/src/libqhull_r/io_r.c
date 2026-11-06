/*<html><pre>  -<a                             href="qh-io_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   io_r.c
   Input/Output routines of qhull application

   see qh-io_r.htm and io_r.h

   see user_r.c for qh_errprint and qh_printfacetlist

   unix_r.c calls qh_readpoints and qh_produce_output

   unix_r.c and user_r.c are the only callers of io_r.c functions
   This allows the user to avoid loading io_r.o from qhull.a

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/io_r.c#4 $$Change: 2064 $
   $DateTime: 2016/01/18 12:36:08 $$Author: bbarber $
*/

#include "qhull_ra.h"

















void qh_produce_output(qhT *qh) {
    int tempsize= qh_setsize(qh, qh->qhmem.tempstack);

    qh_prepare_output(qh);
    qh_produce_output2(qh);
    if (qh_setsize(qh, qh->qhmem.tempstack) != tempsize) {
        qh_fprintf(qh, qh->ferr, 6206, "qhull internal error (qh_produce_output): temporary sets not empty(%d)\n",
            qh_setsize(qh, qh->qhmem.tempstack));
        qh_errexit(qh, qh_ERRqhull, NULL, NULL);
    }
}


void qh_produce_output2(qhT *qh) {
  int i, tempsize= qh_setsize(qh, qh->qhmem.tempstack), d_1;

  if (qh->PRINTsummary)
    qh_printsummary(qh, qh->ferr);
  else if (qh->PRINTout[0] == qh_PRINTnone)
    qh_printsummary(qh, qh->fout);
  for (i=0; i < qh_PRINTEND; i++)
    qh_printfacets(qh, qh->fout, qh->PRINTout[i], qh->facet_list, NULL, !qh_ALL);
  qh_allstatistics(qh);
  if (qh->PRINTprecision && !qh->MERGING && (qh->JOGGLEmax > REALmax/2 || qh->RERUN))
    qh_printstats(qh, qh->ferr, qh->qhstat.precision, NULL);
  if (qh->VERIFYoutput && (zzval_(Zridge) > 0 || zzval_(Zridgemid) > 0))
    qh_printstats(qh, qh->ferr, qh->qhstat.vridges, NULL);
  if (qh->PRINTstatistics) {
    qh_printstatistics(qh, qh->ferr, "");
    qh_memstatistics(qh, qh->ferr);
    d_1= sizeof(setT) + (qh->hull_dim - 1) * SETelemsize;
    qh_fprintf(qh, qh->ferr, 8040, "\
    size in bytes: merge %d ridge %d vertex %d facet %d\n\
         normal %d ridge vertices %d facet vertices or neighbors %d\n",
            (int)sizeof(mergeT), (int)sizeof(ridgeT),
            (int)sizeof(vertexT), (int)sizeof(facetT),
            qh->normal_size, d_1, d_1 + SETelemsize);
  }
  if (qh_setsize(qh, qh->qhmem.tempstack) != tempsize) {
    qh_fprintf(qh, qh->ferr, 6065, "qhull internal error (qh_produce_output2): temporary sets not empty(%d)\n",
             qh_setsize(qh, qh->qhmem.tempstack));
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
}








void qh_dfacet(qhT *qh, unsigned id) {
  facetT *facet;

  FORALLfacets {
    if (facet->id == id) {
      qh_printfacet(qh, qh->fout, facet);
      break;
    }
  }
}








void qh_dvertex(qhT *qh, unsigned id) {
  vertexT *vertex;

  FORALLvertices {
    if (vertex->id == id) {
      qh_printvertex(qh, qh->fout, vertex);
      break;
    }
  }
}








int qh_compare_facetarea(const void *p1, const void *p2) {
  const facetT *a= *((facetT *const*)p1), *b= *((facetT *const*)p2);

  if (!a->isarea)
    return -1;
  if (!b->isarea)
    return 1;
  if (a->f.area > b->f.area)
    return 1;
  else if (a->f.area == b->f.area)
    return 0;
  return -1;
}







int qh_compare_facetmerge(const void *p1, const void *p2) {
  const facetT *a= *((facetT *const*)p1), *b= *((facetT *const*)p2);

  return(a->nummerge - b->nummerge);
}







int qh_compare_facetvisit(const void *p1, const void *p2) {
  const facetT *a= *((facetT *const*)p1), *b= *((facetT *const*)p2);
  int i,j;

  if (!(i= a->visitid))
    i= 0 - a->id;
  if (!(j= b->visitid))
    j= 0 - b->id;
  return(i - j);
}
























void qh_copyfilename(qhT *qh, char *filename, int size, const char* source, int length) {
  char c= *source;

  if (length > size + 1) {
      qh_fprintf(qh, qh->ferr, 6040, "qhull error: filename is more than %d characters, %s\n",  size-1, source);
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  strncpy(filename, source, length);
  filename[length]= '\0';
  if (c == '\'' || c == '"') {
    char *s= filename + 1;
    char *t= filename;
    while (*s) {
      if (*s == c) {
          if (s[-1] == '\\')
              t[-1]= c;
      }else
          *t++= *s;
      s++;
    }
    *t= '\0';
  }
}




























void qh_countfacets(qhT *qh, facetT *facetlist, setT *facets, boolT printall,
    int *numfacetsp, int *numsimplicialp, int *totneighborsp, int *numridgesp, int *numcoplanarsp, int *numtricoplanarsp) {
  facetT *facet, **facetp;
  int numfacets= 0, numsimplicial= 0, numridges= 0, totneighbors= 0, numcoplanars= 0, numtricoplanars= 0;

  FORALLfacet_(facetlist) {
    if ((facet->visible && qh->NEWfacets)
    || (!printall && qh_skipfacet(qh, facet)))
      facet->visitid= 0;
    else {
      facet->visitid= ++numfacets;
      totneighbors += qh_setsize(qh, facet->neighbors);
      if (facet->simplicial) {
        numsimplicial++;
        if (facet->keepcentrum && facet->tricoplanar)
          numtricoplanars++;
      }else
        numridges += qh_setsize(qh, facet->ridges);
      if (facet->coplanarset)
        numcoplanars += qh_setsize(qh, facet->coplanarset);
    }
  }

  FOREACHfacet_(facets) {
    if ((facet->visible && qh->NEWfacets)
    || (!printall && qh_skipfacet(qh, facet)))
      facet->visitid= 0;
    else {
      facet->visitid= ++numfacets;
      totneighbors += qh_setsize(qh, facet->neighbors);
      if (facet->simplicial){
        numsimplicial++;
        if (facet->keepcentrum && facet->tricoplanar)
          numtricoplanars++;
      }else
        numridges += qh_setsize(qh, facet->ridges);
      if (facet->coplanarset)
        numcoplanars += qh_setsize(qh, facet->coplanarset);
    }
  }
  qh->visit_id += numfacets+1;
  *numfacetsp= numfacets;
  *numsimplicialp= numsimplicial;
  *totneighborsp= totneighbors;
  *numridgesp= numridges;
  *numcoplanarsp= numcoplanars;
  *numtricoplanarsp= numtricoplanars;
}










































pointT *qh_detvnorm(qhT *qh, vertexT *vertex, vertexT *vertexA, setT *centers, realT *offsetp) {
  facetT *facet, **facetp;
  int  i, k, pointid, pointidA, point_i, point_n;
  setT *simplex= NULL;
  pointT *point, **pointp, *point0, *midpoint, *normal, *inpoint;
  coordT *coord, *gmcoord, *normalp;
  setT *points= qh_settemp(qh, qh->TEMPsize);
  boolT nearzero= False;
  boolT unbounded= False;
  int numcenters= 0;
  int dim= qh->hull_dim - 1;
  realT dist, offset, angle, zero= 0.0;

  midpoint= qh->gm_matrix + qh->hull_dim * qh->hull_dim;
  for (k=0; k < dim; k++)
    midpoint[k]= (vertex->point[k] + vertexA->point[k])/2;
  FOREACHfacet_(centers) {
    numcenters++;
    if (!facet->visitid)
      unbounded= True;
    else {
      if (!facet->center)
        facet->center= qh_facetcenter(qh, facet->vertices);
      qh_setappend(qh, &points, facet->center);
    }
  }
  if (numcenters > dim) {
    simplex= qh_settemp(qh, qh->TEMPsize);
    qh_setappend(qh, &simplex, vertex->point);
    if (unbounded)
      qh_setappend(qh, &simplex, midpoint);
    qh_maxsimplex(qh, dim, points, NULL, 0, &simplex);
    qh_setdelnth(qh, simplex, 0);
  }else if (numcenters == dim) {
    if (unbounded)
      qh_setappend(qh, &points, midpoint);
    simplex= points;
  }else {
    qh_fprintf(qh, qh->ferr, 6216, "qhull internal error (qh_detvnorm): too few points(%d) to compute separating plane\n", numcenters);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  i= 0;
  gmcoord= qh->gm_matrix;
  point0= SETfirstt_(simplex, pointT);
  FOREACHpoint_(simplex) {
    if (qh->IStracing >= 4)
      qh_printmatrix(qh, qh->ferr, "qh_detvnorm: Voronoi vertex or midpoint",
                              &point, 1, dim);
    if (point != point0) {
      qh->gm_row[i++]= gmcoord;
      coord= point0;
      for (k=dim; k--; )
        *(gmcoord++)= *point++ - *coord++;
    }
  }
  qh->gm_row[i]= gmcoord;
  normal= gmcoord;
  qh_sethyperplane_gauss(qh, dim, qh->gm_row, point0, True,
                normal, &offset, &nearzero);
  if (qh->GOODvertexp == vertexA->point)
    inpoint= vertexA->point;
  else
    inpoint= vertex->point;
  zinc_(Zdistio);
  dist= qh_distnorm(dim, inpoint, normal, &offset);
  if (dist > 0) {
    offset= -offset;
    normalp= normal;
    for (k=dim; k--; ) {
      *normalp= -(*normalp);
      normalp++;
    }
  }
  if (qh->VERIFYoutput || qh->PRINTstatistics) {
    pointid= qh_pointid(qh, vertex->point);
    pointidA= qh_pointid(qh, vertexA->point);
    if (!unbounded) {
      zinc_(Zdiststat);
      dist= qh_distnorm(dim, midpoint, normal, &offset);
      if (dist < 0)
        dist= -dist;
      zzinc_(Zridgemid);
      wwmax_(Wridgemidmax, dist);
      wwadd_(Wridgemid, dist);
      trace4((qh, qh->ferr, 4014, "qh_detvnorm: points %d %d midpoint dist %2.2g\n",
                 pointid, pointidA, dist));
      for (k=0; k < dim; k++)
        midpoint[k]= vertexA->point[k] - vertex->point[k];
      qh_normalize(qh, midpoint, dim, False);
      angle= qh_distnorm(dim, midpoint, normal, &zero);
      if (angle < 0.0)
        angle= angle + 1.0;
      else
        angle= angle - 1.0;
      if (angle < 0.0)
        angle -= angle;
      trace4((qh, qh->ferr, 4015, "qh_detvnorm: points %d %d angle %2.2g nearzero %d\n",
                 pointid, pointidA, angle, nearzero));
      if (nearzero) {
        zzinc_(Zridge0);
        wwmax_(Wridge0max, angle);
        wwadd_(Wridge0, angle);
      }else {
        zzinc_(Zridgeok)
        wwmax_(Wridgeokmax, angle);
        wwadd_(Wridgeok, angle);
      }
    }
    if (simplex != points) {
      FOREACHpoint_i_(qh, points) {
        if (!qh_setin(simplex, point)) {
          facet= SETelemt_(centers, point_i, facetT);
          zinc_(Zdiststat);
          dist= qh_distnorm(dim, point, normal, &offset);
          if (dist < 0)
            dist= -dist;
          zzinc_(Zridge);
          wwmax_(Wridgemax, dist);
          wwadd_(Wridge, dist);
          trace4((qh, qh->ferr, 4016, "qh_detvnorm: points %d %d Voronoi vertex %d dist %2.2g\n",
                             pointid, pointidA, facet->visitid, dist));
        }
      }
    }
  }
  *offsetp= offset;
  if (simplex != points)
    qh_settempfree(qh, &simplex);
  qh_settempfree(qh, &points);
  return normal;
}












setT *qh_detvridge(qhT *qh, vertexT *vertex) {
  setT *centers= qh_settemp(qh, qh->TEMPsize);
  setT *tricenters= qh_settemp(qh, qh->TEMPsize);
  facetT *neighbor, **neighborp;
  boolT firstinf= True;

  FOREACHneighbor_(vertex) {
    if (neighbor->seen) {
      if (neighbor->visitid) {
        if (!neighbor->tricoplanar || qh_setunique(qh, &tricenters, neighbor->center))
          qh_setappend(qh, &centers, neighbor);
      }else if (firstinf) {
        firstinf= False;
        qh_setappend(qh, &centers, neighbor);
      }
    }
  }
  qsort(SETaddr_(centers, facetT), (size_t)qh_setsize(qh, centers),
             sizeof(facetT *), qh_compare_facetvisit);
  qh_settempfree(qh, &tricenters);
  return centers;
}




















setT *qh_detvridge3(qhT *qh, vertexT *atvertex, vertexT *vertex) {
  setT *centers= qh_settemp(qh, qh->TEMPsize);
  setT *tricenters= qh_settemp(qh, qh->TEMPsize);
  facetT *neighbor, **neighborp, *facet= NULL;
  boolT firstinf= True;

  FOREACHneighbor_(atvertex)
    neighbor->seen2= False;
  FOREACHneighbor_(vertex) {
    if (!neighbor->seen2) {
      facet= neighbor;
      break;
    }
  }
  while (facet) {
    facet->seen2= True;
    if (neighbor->seen) {
      if (facet->visitid) {
        if (!facet->tricoplanar || qh_setunique(qh, &tricenters, facet->center))
          qh_setappend(qh, &centers, facet);
      }else if (firstinf) {
        firstinf= False;
        qh_setappend(qh, &centers, facet);
      }
    }
    FOREACHneighbor_(facet) {
      if (!neighbor->seen2) {
        if (qh_setin(vertex->neighbors, neighbor))
          break;
        else
          neighbor->seen2= True;
      }
    }
    facet= neighbor;
  }
  if (qh->CHECKfrequently) {
    FOREACHneighbor_(vertex) {
      if (!neighbor->seen2) {
          qh_fprintf(qh, qh->ferr, 6217, "qhull internal error (qh_detvridge3): neighbors of vertex p%d are not connected at facet %d\n",
                 qh_pointid(qh, vertex->point), neighbor->id);
        qh_errexit(qh, qh_ERRqhull, neighbor, NULL);
      }
    }
  }
  FOREACHneighbor_(atvertex)
    neighbor->seen2= True;
  qh_settempfree(qh, &tricenters);
  return centers;
}


















































int qh_eachvoronoi(qhT *qh, FILE *fp, printvridgeT printvridge, vertexT *atvertex, boolT visitall, qh_RIDGE innerouter, boolT inorder) {
  boolT unbounded;
  int count;
  facetT *neighbor, **neighborp, *neighborA, **neighborAp;
  setT *centers;
  setT *tricenters= qh_settemp(qh, qh->TEMPsize);

  vertexT *vertex, **vertexp;
  boolT firstinf;
  unsigned int numfacets= (unsigned int)qh->num_facets;
  int totridges= 0;

  qh->vertex_visit++;
  atvertex->seen= True;
  if (visitall) {
    FORALLvertices
      vertex->seen= False;
  }
  FOREACHneighbor_(atvertex) {
    if (neighbor->visitid < numfacets)
      neighbor->seen= True;
  }
  FOREACHneighbor_(atvertex) {
    if (neighbor->seen) {
      FOREACHvertex_(neighbor->vertices) {
        if (vertex->visitid != qh->vertex_visit && !vertex->seen) {
          vertex->visitid= qh->vertex_visit;
          count= 0;
          firstinf= True;
          qh_settruncate(qh, tricenters, 0);
          FOREACHneighborA_(vertex) {
            if (neighborA->seen) {
              if (neighborA->visitid) {
                if (!neighborA->tricoplanar || qh_setunique(qh, &tricenters, neighborA->center))
                  count++;
              }else if (firstinf) {
                count++;
                firstinf= False;
              }
            }
          }
          if (count >= qh->hull_dim - 1) {
            if (firstinf) {
              if (innerouter == qh_RIDGEouter)
                continue;
              unbounded= False;
            }else {
              if (innerouter == qh_RIDGEinner)
                continue;
              unbounded= True;
            }
            totridges++;
            trace4((qh, qh->ferr, 4017, "qh_eachvoronoi: Voronoi ridge of %d vertices between sites %d and %d\n",
                  count, qh_pointid(qh, atvertex->point), qh_pointid(qh, vertex->point)));
            if (printvridge && fp) {
              if (inorder && qh->hull_dim == 3+1)
                centers= qh_detvridge3(qh, atvertex, vertex);
              else
                centers= qh_detvridge(qh, vertex);
              (*printvridge)(qh, fp, atvertex, vertex, centers, unbounded);
              qh_settempfree(qh, &centers);
            }
          }
        }
      }
    }
  }
  FOREACHneighbor_(atvertex)
    neighbor->seen= False;
  qh_settempfree(qh, &tricenters);
  return totridges;
}






























int qh_eachvoronoi_all(qhT *qh, FILE *fp, printvridgeT printvridge, boolT isUpper, qh_RIDGE innerouter, boolT inorder) {
  facetT *facet;
  vertexT *vertex;
  int numcenters= 1;
  int totridges= 0;

  qh_clearcenters(qh, qh_ASvoronoi);
  qh_vertexneighbors(qh);
  maximize_(qh->visit_id, (unsigned) qh->num_facets);
  FORALLfacets {
    facet->visitid= 0;
    facet->seen= False;
    facet->seen2= True;
  }
  FORALLfacets {
    if (facet->upperdelaunay == isUpper)
      facet->visitid= numcenters++;
  }
  FORALLvertices
    vertex->seen= False;
  FORALLvertices {
    if (qh->GOODvertex > 0 && qh_pointid(qh, vertex->point)+1 != qh->GOODvertex)
      continue;
    totridges += qh_eachvoronoi(qh, fp, printvridge, vertex,
                   !qh_ALL, innerouter, inorder);
  }
  return totridges;
}












void qh_facet2point(qhT *qh, facetT *facet, pointT **point0, pointT **point1, realT *mindist) {
  vertexT *vertex0, *vertex1;
  realT dist;

  if (facet->toporient ^ qh_ORIENTclock) {
    vertex0= SETfirstt_(facet->vertices, vertexT);
    vertex1= SETsecondt_(facet->vertices, vertexT);
  }else {
    vertex1= SETfirstt_(facet->vertices, vertexT);
    vertex0= SETsecondt_(facet->vertices, vertexT);
  }
  zadd_(Zdistio, 2);
  qh_distplane(qh, vertex0->point, facet, &dist);
  *mindist= dist;
  *point0= qh_projectpoint(qh, vertex0->point, facet, dist);
  qh_distplane(qh, vertex1->point, facet, &dist);
  minimize_(*mindist, dist);
  *point1= qh_projectpoint(qh, vertex1->point, facet, dist);
}






















setT *qh_facetvertices(qhT *qh, facetT *facetlist, setT *facets, boolT allfacets) {
  setT *vertices;
  facetT *facet, **facetp;
  vertexT *vertex, **vertexp;

  qh->vertex_visit++;
  if (facetlist == qh->facet_list && allfacets && !facets) {
    vertices= qh_settemp(qh, qh->num_vertices);
    FORALLvertices {
      vertex->visitid= qh->vertex_visit;
      qh_setappend(qh, &vertices, vertex);
    }
  }else {
    vertices= qh_settemp(qh, qh->TEMPsize);
    FORALLfacet_(facetlist) {
      if (!allfacets && qh_skipfacet(qh, facet))
        continue;
      FOREACHvertex_(facet->vertices) {
        if (vertex->visitid != qh->vertex_visit) {
          vertex->visitid= qh->vertex_visit;
          qh_setappend(qh, &vertices, vertex);
        }
      }
    }
  }
  FOREACHfacet_(facets) {
    if (!allfacets && qh_skipfacet(qh, facet))
      continue;
    FOREACHvertex_(facet->vertices) {
      if (vertex->visitid != qh->vertex_visit) {
        vertex->visitid= qh->vertex_visit;
        qh_setappend(qh, &vertices, vertex);
      }
    }
  }
  return vertices;
}











void qh_geomplanes(qhT *qh, facetT *facet, realT *outerplane, realT *innerplane) {
  realT radius;

  if (qh->MERGING || qh->JOGGLEmax < REALmax/2) {
    qh_outerinner(qh, facet, outerplane, innerplane);
    radius= qh->PRINTradius;
    if (qh->JOGGLEmax < REALmax/2)
      radius -= qh->JOGGLEmax * sqrt((realT)qh->hull_dim);
    *outerplane += radius;
    *innerplane -= radius;
    if (qh->PRINTcoplanar || qh->PRINTspheres) {
      *outerplane += qh->MAXabs_coord * qh_GEOMepsilon;
      *innerplane -= qh->MAXabs_coord * qh_GEOMepsilon;
    }
  }else
    *innerplane= *outerplane= 0;
}

























void qh_markkeep(qhT *qh, facetT *facetlist) {
  facetT *facet, **facetp;
  setT *facets= qh_settemp(qh, qh->num_facets);
  int size, count;

  trace2((qh, qh->ferr, 2006, "qh_markkeep: only keep %d largest and/or %d most merged facets and/or min area %.2g\n",
          qh->KEEParea, qh->KEEPmerge, qh->KEEPminArea));
  FORALLfacet_(facetlist) {
    if (!facet->visible && facet->good)
      qh_setappend(qh, &facets, facet);
  }
  size= qh_setsize(qh, facets);
  if (qh->KEEParea) {
    qsort(SETaddr_(facets, facetT), (size_t)size,
             sizeof(facetT *), qh_compare_facetarea);
    if ((count= size - qh->KEEParea) > 0) {
      FOREACHfacet_(facets) {
        facet->good= False;
        if (--count == 0)
          break;
      }
    }
  }
  if (qh->KEEPmerge) {
    qsort(SETaddr_(facets, facetT), (size_t)size,
             sizeof(facetT *), qh_compare_facetmerge);
    if ((count= size - qh->KEEPmerge) > 0) {
      FOREACHfacet_(facets) {
        facet->good= False;
        if (--count == 0)
          break;
      }
    }
  }
  if (qh->KEEPminArea < REALmax/2) {
    FOREACHfacet_(facets) {
      if (!facet->isarea || facet->f.area < qh->KEEPminArea)
        facet->good= False;
    }
  }
  qh_settempfree(qh, &facets);
  count= 0;
  FORALLfacet_(facetlist) {
    if (facet->good)
      count++;
  }
  qh->num_good= count;
}

























setT *qh_markvoronoi(qhT *qh, facetT *facetlist, setT *facets, boolT printall, boolT *isLowerp, int *numcentersp) {
  int numcenters=0;
  facetT *facet, **facetp;
  setT *vertices;
  boolT isLower= False;

  qh->printoutnum++;
  qh_clearcenters(qh, qh_ASvoronoi);
  qh_vertexneighbors(qh);
  vertices= qh_pointvertex(qh);
  if (qh->ATinfinity)
    SETelem_(vertices, qh->num_points-1)= NULL;
  qh->visit_id++;
  maximize_(qh->visit_id, (unsigned) qh->num_facets);
  FORALLfacet_(facetlist) {
    if (printall || !qh_skipfacet(qh, facet)) {
      if (!facet->upperdelaunay) {
        isLower= True;
        break;
      }
    }
  }
  FOREACHfacet_(facets) {
    if (printall || !qh_skipfacet(qh, facet)) {
      if (!facet->upperdelaunay) {
        isLower= True;
        break;
      }
    }
  }
  FORALLfacets {
    if (facet->normal && (facet->upperdelaunay == isLower))
      facet->visitid= 0;
    else
      facet->visitid= qh->visit_id;
    facet->seen= False;
    facet->seen2= True;
  }
  numcenters++;
  FORALLfacet_(facetlist) {
    if (printall || !qh_skipfacet(qh, facet))
      facet->visitid= numcenters++;
  }
  FOREACHfacet_(facets) {
    if (printall || !qh_skipfacet(qh, facet))
      facet->visitid= numcenters++;
  }
  *isLowerp= isLower;
  *numcentersp= numcenters;
  trace2((qh, qh->ferr, 2007, "qh_markvoronoi: isLower %d numcenters %d\n", isLower, numcenters));
  return vertices;
}
















void qh_order_vertexneighbors(qhT *qh, vertexT *vertex) {
  setT *newset;
  facetT *facet, *neighbor, **neighborp;

  trace4((qh, qh->ferr, 4018, "qh_order_vertexneighbors: order neighbors of v%d for 3-d\n", vertex->id));
  newset= qh_settemp(qh, qh_setsize(qh, vertex->neighbors));
  facet= (facetT*)qh_setdellast(vertex->neighbors);
  qh_setappend(qh, &newset, facet);
  while (qh_setsize(qh, vertex->neighbors)) {
    FOREACHneighbor_(vertex) {
      if (qh_setin(facet->neighbors, neighbor)) {
        qh_setdel(vertex->neighbors, neighbor);
        qh_setappend(qh, &newset, neighbor);
        facet= neighbor;
        break;
      }
    }
    if (!neighbor) {
      qh_fprintf(qh, qh->ferr, 6066, "qhull internal error (qh_order_vertexneighbors): no neighbor of v%d for f%d\n",
        vertex->id, facet->id);
      qh_errexit(qh, qh_ERRqhull, facet, NULL);
    }
  }
  qh_setfree(qh, &vertex->neighbors);
  qh_settemppop(qh);
  vertex->neighbors= newset;
}












void qh_prepare_output(qhT *qh) {
  if (qh->VORONOI) {
    qh_clearcenters(qh, qh_ASvoronoi);
    qh_vertexneighbors(qh);
  }
  if (qh->TRIangulate && !qh->hasTriangulation) {
    qh_triangulate(qh);
    if (qh->VERIFYoutput && !qh->CHECKfrequently)
      qh_checkpolygon(qh, qh->facet_list);
  }
  qh_findgood_all(qh, qh->facet_list);
  if (qh->GETarea)
    qh_getarea(qh, qh->facet_list);
  if (qh->KEEParea || qh->KEEPmerge || qh->KEEPminArea < REALmax/2)
    qh_markkeep(qh, qh->facet_list);
  if (qh->PRINTstatistics)
    qh_collectstatistics(qh);
}
























void qh_printafacet(qhT *qh, FILE *fp, qh_PRINT format, facetT *facet, boolT printall) {
  realT color[4], offset, dist, outerplane, innerplane;
  boolT zerodiv;
  coordT *point, *normp, *coordp, **pointp, *feasiblep;
  int k;
  vertexT *vertex, **vertexp;
  facetT *neighbor, **neighborp;

  if (!printall && qh_skipfacet(qh, facet))
    return;
  if (facet->visible && qh->NEWfacets && format != qh_PRINTfacets)
    return;
  qh->printoutnum++;
  switch (format) {
  case qh_PRINTarea:
    if (facet->isarea) {
      qh_fprintf(qh, fp, 9009, qh_REAL_1, facet->f.area);
      qh_fprintf(qh, fp, 9010, "\n");
    }else
      qh_fprintf(qh, fp, 9011, "0\n");
    break;
  case qh_PRINTcoplanars:
    qh_fprintf(qh, fp, 9012, "%d", qh_setsize(qh, facet->coplanarset));
    FOREACHpoint_(facet->coplanarset)
      qh_fprintf(qh, fp, 9013, " %d", qh_pointid(qh, point));
    qh_fprintf(qh, fp, 9014, "\n");
    break;
  case qh_PRINTcentrums:
    qh_printcenter(qh, fp, format, NULL, facet);
    break;
  case qh_PRINTfacets:
    qh_printfacet(qh, fp, facet);
    break;
  case qh_PRINTfacets_xridge:
    qh_printfacetheader(qh, fp, facet);
    break;
  case qh_PRINTgeom:
    if (!facet->normal)
      break;
    for (k=qh->hull_dim; k--; ) {
      color[k]= (facet->normal[k]+1.0)/2.0;
      maximize_(color[k], -1.0);
      minimize_(color[k], +1.0);
    }
    qh_projectdim3(qh, color, color);
    if (qh->PRINTdim != qh->hull_dim)
      qh_normalize2(qh, color, 3, True, NULL, NULL);
    if (qh->hull_dim <= 2)
      qh_printfacet2geom(qh, fp, facet, color);
    else if (qh->hull_dim == 3) {
      if (facet->simplicial)
        qh_printfacet3geom_simplicial(qh, fp, facet, color);
      else
        qh_printfacet3geom_nonsimplicial(qh, fp, facet, color);
    }else {
      if (facet->simplicial)
        qh_printfacet4geom_simplicial(qh, fp, facet, color);
      else
        qh_printfacet4geom_nonsimplicial(qh, fp, facet, color);
    }
    break;
  case qh_PRINTids:
    qh_fprintf(qh, fp, 9015, "%d\n", facet->id);
    break;
  case qh_PRINTincidences:
  case qh_PRINToff:
  case qh_PRINTtriangles:
    if (qh->hull_dim == 3 && format != qh_PRINTtriangles)
      qh_printfacet3vertex(qh, fp, facet, format);
    else if (facet->simplicial || qh->hull_dim == 2 || format == qh_PRINToff)
      qh_printfacetNvertex_simplicial(qh, fp, facet, format);
    else
      qh_printfacetNvertex_nonsimplicial(qh, fp, facet, qh->printoutvar++, format);
    break;
  case qh_PRINTinner:
    qh_outerinner(qh, facet, NULL, &innerplane);
    offset= facet->offset - innerplane;
    goto LABELprintnorm;
    break;
  case qh_PRINTmerges:
    qh_fprintf(qh, fp, 9016, "%d\n", facet->nummerge);
    break;
  case qh_PRINTnormals:
    offset= facet->offset;
    goto LABELprintnorm;
    break;
  case qh_PRINTouter:
    qh_outerinner(qh, facet, &outerplane, NULL);
    offset= facet->offset - outerplane;
  LABELprintnorm:
    if (!facet->normal) {
      qh_fprintf(qh, fp, 9017, "no normal for facet f%d\n", facet->id);
      break;
    }
    if (qh->CDDoutput) {
      qh_fprintf(qh, fp, 9018, qh_REAL_1, -offset);
      for (k=0; k < qh->hull_dim; k++)
        qh_fprintf(qh, fp, 9019, qh_REAL_1, -facet->normal[k]);
    }else {
      for (k=0; k < qh->hull_dim; k++)
        qh_fprintf(qh, fp, 9020, qh_REAL_1, facet->normal[k]);
      qh_fprintf(qh, fp, 9021, qh_REAL_1, offset);
    }
    qh_fprintf(qh, fp, 9022, "\n");
    break;
  case qh_PRINTmathematica:
  case qh_PRINTmaple:
    if (qh->hull_dim == 2)
      qh_printfacet2math(qh, fp, facet, format, qh->printoutvar++);
    else
      qh_printfacet3math(qh, fp, facet, format, qh->printoutvar++);
    break;
  case qh_PRINTneighbors:
    qh_fprintf(qh, fp, 9023, "%d", qh_setsize(qh, facet->neighbors));
    FOREACHneighbor_(facet)
      qh_fprintf(qh, fp, 9024, " %d",
               neighbor->visitid ? neighbor->visitid - 1: 0 - neighbor->id);
    qh_fprintf(qh, fp, 9025, "\n");
    break;
  case qh_PRINTpointintersect:
    if (!qh->feasible_point) {
      qh_fprintf(qh, qh->ferr, 6067, "qhull input error (qh_printafacet): option 'Fp' needs qh->feasible_point\n");
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    if (facet->offset > 0)
      goto LABELprintinfinite;
    point= coordp= (coordT*)qh_memalloc(qh, qh->normal_size);
    normp= facet->normal;
    feasiblep= qh->feasible_point;
    if (facet->offset < -qh->MINdenom) {
      for (k=qh->hull_dim; k--; )
        *(coordp++)= (*(normp++) / - facet->offset) + *(feasiblep++);
    }else {
      for (k=qh->hull_dim; k--; ) {
        *(coordp++)= qh_divzero(*(normp++), facet->offset, qh->MINdenom_1,
                                 &zerodiv) + *(feasiblep++);
        if (zerodiv) {
          qh_memfree(qh, point, qh->normal_size);
          goto LABELprintinfinite;
        }
      }
    }
    qh_printpoint(qh, fp, NULL, point);
    qh_memfree(qh, point, qh->normal_size);
    break;
  LABELprintinfinite:
    for (k=qh->hull_dim; k--; )
      qh_fprintf(qh, fp, 9026, qh_REAL_1, qh_INFINITE);
    qh_fprintf(qh, fp, 9027, "\n");
    break;
  case qh_PRINTpointnearest:
    FOREACHpoint_(facet->coplanarset) {
      int id, id2;
      vertex= qh_nearvertex(qh, facet, point, &dist);
      id= qh_pointid(qh, vertex->point);
      id2= qh_pointid(qh, point);
      qh_fprintf(qh, fp, 9028, "%d %d %d " qh_REAL_1 "\n", id, id2, facet->id, dist);
    }
    break;
  case qh_PRINTpoints:
    if (qh->CDDoutput)
      qh_fprintf(qh, fp, 9029, "1 ");
    qh_printcenter(qh, fp, format, NULL, facet);
    break;
  case qh_PRINTvertices:
    qh_fprintf(qh, fp, 9030, "%d", qh_setsize(qh, facet->vertices));
    FOREACHvertex_(facet->vertices)
      qh_fprintf(qh, fp, 9031, " %d", qh_pointid(qh, vertex->point));
    qh_fprintf(qh, fp, 9032, "\n");
    break;
  default:
    break;
  }
}






















void qh_printbegin(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetlist, setT *facets, boolT printall) {
  int numfacets, numsimplicial, numridges, totneighbors, numcoplanars, numtricoplanars;
  int i, num;
  facetT *facet, **facetp;
  vertexT *vertex, **vertexp;
  setT *vertices;
  pointT *point, **pointp, *pointtemp;

  qh->printoutnum= 0;
  qh_countfacets(qh, facetlist, facets, printall, &numfacets, &numsimplicial,
      &totneighbors, &numridges, &numcoplanars, &numtricoplanars);
  switch (format) {
  case qh_PRINTnone:
    break;
  case qh_PRINTarea:
    qh_fprintf(qh, fp, 9033, "%d\n", numfacets);
    break;
  case qh_PRINTcoplanars:
    qh_fprintf(qh, fp, 9034, "%d\n", numfacets);
    break;
  case qh_PRINTcentrums:
    if (qh->CENTERtype == qh_ASnone)
      qh_clearcenters(qh, qh_AScentrum);
    qh_fprintf(qh, fp, 9035, "%d\n%d\n", qh->hull_dim, numfacets);
    break;
  case qh_PRINTfacets:
  case qh_PRINTfacets_xridge:
    if (facetlist)
      qh_printvertexlist(qh, fp, "Vertices and facets:\n", facetlist, facets, printall);
    break;
  case qh_PRINTgeom:
    if (qh->hull_dim > 4)
      goto LABELnoformat;
    if (qh->VORONOI && qh->hull_dim > 3)
      goto LABELnoformat;
    if (qh->hull_dim == 2 && (qh->PRINTridges || qh->DOintersections))
      qh_fprintf(qh, qh->ferr, 7049, "qhull warning: output for ridges and intersections not implemented in 2-d\n");
    if (qh->hull_dim == 4 && (qh->PRINTinner || qh->PRINTouter ||
                             (qh->PRINTdim == 4 && qh->PRINTcentrums)))
      qh_fprintf(qh, qh->ferr, 7050, "qhull warning: output for outer/inner planes and centrums not implemented in 4-d\n");
    if (qh->PRINTdim == 4 && (qh->PRINTspheres))
      qh_fprintf(qh, qh->ferr, 7051, "qhull warning: output for vertices not implemented in 4-d\n");
    if (qh->PRINTdim == 4 && qh->DOintersections && qh->PRINTnoplanes)
      qh_fprintf(qh, qh->ferr, 7052, "qhull warning: 'Gnh' generates no output in 4-d\n");
    if (qh->PRINTdim == 2) {
      qh_fprintf(qh, fp, 9036, "{appearance {linewidth 3} LIST # %s | %s\n",
              qh->rbox_command, qh->qhull_command);
    }else if (qh->PRINTdim == 3) {
      qh_fprintf(qh, fp, 9037, "{appearance {+edge -evert linewidth 2} LIST # %s | %s\n",
              qh->rbox_command, qh->qhull_command);
    }else if (qh->PRINTdim == 4) {
      qh->visit_id++;
      num= 0;
      FORALLfacet_(facetlist)
        qh_printend4geom(qh, NULL, facet, &num, printall);
      FOREACHfacet_(facets)
        qh_printend4geom(qh, NULL, facet, &num, printall);
      qh->ridgeoutnum= num;
      qh->printoutvar= 0;
      qh_fprintf(qh, fp, 9038, "LIST # %s | %s\n", qh->rbox_command, qh->qhull_command);
    }

    if (qh->PRINTdots) {
      qh->printoutnum++;
      num= qh->num_points + qh_setsize(qh, qh->other_points);
      if (qh->DELAUNAY && qh->ATinfinity)
        num--;
      if (qh->PRINTdim == 4)
        qh_fprintf(qh, fp, 9039, "4VECT %d %d 1\n", num, num);
      else
        qh_fprintf(qh, fp, 9040, "VECT %d %d 1\n", num, num);

      for (i=num; i--; ) {
        if (i % 20 == 0)
          qh_fprintf(qh, fp, 9041, "\n");
        qh_fprintf(qh, fp, 9042, "1 ");
      }
      qh_fprintf(qh, fp, 9043, "# 1 point per line\n1 ");
      for (i=num-1; i--; ) {
        if (i % 20 == 0)
          qh_fprintf(qh, fp, 9044, "\n");
        qh_fprintf(qh, fp, 9045, "0 ");
      }
      qh_fprintf(qh, fp, 9046, "# 1 color for all\n");
      FORALLpoints {
        if (!qh->DELAUNAY || !qh->ATinfinity || qh_pointid(qh, point) != qh->num_points-1) {
          if (qh->PRINTdim == 4)
            qh_printpoint(qh, fp, NULL, point);
            else
              qh_printpoint3(qh, fp, point);
        }
      }
      FOREACHpoint_(qh->other_points) {
        if (qh->PRINTdim == 4)
          qh_printpoint(qh, fp, NULL, point);
        else
          qh_printpoint3(qh, fp, point);
      }
      qh_fprintf(qh, fp, 9047, "0 1 1 1  # color of points\n");
    }

    if (qh->PRINTdim == 4  && !qh->PRINTnoplanes)

      qh_fprintf(qh, fp, 9048, "4OFF %d %d 1\n", 3*qh->ridgeoutnum, qh->ridgeoutnum);
    qh->PRINTcradius= 2 * qh->DISTround;
    if (qh->PREmerge) {
      maximize_(qh->PRINTcradius, qh->premerge_centrum + qh->DISTround);
    }else if (qh->POSTmerge)
      maximize_(qh->PRINTcradius, qh->postmerge_centrum + qh->DISTround);
    qh->PRINTradius= qh->PRINTcradius;
    if (qh->PRINTspheres + qh->PRINTcoplanar)
      maximize_(qh->PRINTradius, qh->MAXabs_coord * qh_MINradius);
    if (qh->premerge_cos < REALmax/2) {
      maximize_(qh->PRINTradius, (1- qh->premerge_cos) * qh->MAXabs_coord);
    }else if (!qh->PREmerge && qh->POSTmerge && qh->postmerge_cos < REALmax/2) {
      maximize_(qh->PRINTradius, (1- qh->postmerge_cos) * qh->MAXabs_coord);
    }
    maximize_(qh->PRINTradius, qh->MINvisible);
    if (qh->JOGGLEmax < REALmax/2)
      qh->PRINTradius += qh->JOGGLEmax * sqrt((realT)qh->hull_dim);
    if (qh->PRINTdim != 4 &&
        (qh->PRINTcoplanar || qh->PRINTspheres || qh->PRINTcentrums)) {
      vertices= qh_facetvertices(qh, facetlist, facets, printall);
      if (qh->PRINTspheres && qh->PRINTdim <= 3)
        qh_printspheres(qh, fp, vertices, qh->PRINTradius);
      if (qh->PRINTcoplanar || qh->PRINTcentrums) {
        qh->firstcentrum= True;
        if (qh->PRINTcoplanar&& !qh->PRINTspheres) {
          FOREACHvertex_(vertices)
            qh_printpointvect2(qh, fp, vertex->point, NULL, qh->interior_point, qh->PRINTradius);
        }
        FORALLfacet_(facetlist) {
          if (!printall && qh_skipfacet(qh, facet))
            continue;
          if (!facet->normal)
            continue;
          if (qh->PRINTcentrums && qh->PRINTdim <= 3)
            qh_printcentrum(qh, fp, facet, qh->PRINTcradius);
          if (!qh->PRINTcoplanar)
            continue;
          FOREACHpoint_(facet->coplanarset)
            qh_printpointvect2(qh, fp, point, facet->normal, NULL, qh->PRINTradius);
          FOREACHpoint_(facet->outsideset)
            qh_printpointvect2(qh, fp, point, facet->normal, NULL, qh->PRINTradius);
        }
        FOREACHfacet_(facets) {
          if (!printall && qh_skipfacet(qh, facet))
            continue;
          if (!facet->normal)
            continue;
          if (qh->PRINTcentrums && qh->PRINTdim <= 3)
            qh_printcentrum(qh, fp, facet, qh->PRINTcradius);
          if (!qh->PRINTcoplanar)
            continue;
          FOREACHpoint_(facet->coplanarset)
            qh_printpointvect2(qh, fp, point, facet->normal, NULL, qh->PRINTradius);
          FOREACHpoint_(facet->outsideset)
            qh_printpointvect2(qh, fp, point, facet->normal, NULL, qh->PRINTradius);
        }
      }
      qh_settempfree(qh, &vertices);
    }
    qh->visit_id++;
    break;
  case qh_PRINTids:
    qh_fprintf(qh, fp, 9049, "%d\n", numfacets);
    break;
  case qh_PRINTincidences:
    if (qh->VORONOI && qh->PRINTprecision)
      qh_fprintf(qh, qh->ferr, 7053, "qhull warning: writing Delaunay.  Use 'p' or 'o' for Voronoi centers\n");
    qh->printoutvar= qh->vertex_id;
    if (qh->hull_dim <= 3)
      qh_fprintf(qh, fp, 9050, "%d\n", numfacets);
    else
      qh_fprintf(qh, fp, 9051, "%d\n", numsimplicial+numridges);
    break;
  case qh_PRINTinner:
  case qh_PRINTnormals:
  case qh_PRINTouter:
    if (qh->CDDoutput)
      qh_fprintf(qh, fp, 9052, "%s | %s\nbegin\n    %d %d real\n", qh->rbox_command,
            qh->qhull_command, numfacets, qh->hull_dim+1);
    else
      qh_fprintf(qh, fp, 9053, "%d\n%d\n", qh->hull_dim+1, numfacets);
    break;
  case qh_PRINTmathematica:
  case qh_PRINTmaple:
    if (qh->hull_dim > 3)
      goto LABELnoformat;
    if (qh->VORONOI)
      qh_fprintf(qh, qh->ferr, 7054, "qhull warning: output is the Delaunay triangulation\n");
    if (format == qh_PRINTmaple) {
      if (qh->hull_dim == 2)
        qh_fprintf(qh, fp, 9054, "PLOT(CURVES(\n");
      else
        qh_fprintf(qh, fp, 9055, "PLOT3D(POLYGONS(\n");
    }else
      qh_fprintf(qh, fp, 9056, "{\n");
    qh->printoutvar= 0;
    break;
  case qh_PRINTmerges:
    qh_fprintf(qh, fp, 9057, "%d\n", numfacets);
    break;
  case qh_PRINTpointintersect:
    qh_fprintf(qh, fp, 9058, "%d\n%d\n", qh->hull_dim, numfacets);
    break;
  case qh_PRINTneighbors:
    qh_fprintf(qh, fp, 9059, "%d\n", numfacets);
    break;
  case qh_PRINToff:
  case qh_PRINTtriangles:
    if (qh->VORONOI)
      goto LABELnoformat;
    num = qh->hull_dim;
    if (format == qh_PRINToff || qh->hull_dim == 2)
      qh_fprintf(qh, fp, 9060, "%d\n%d %d %d\n", num,
        qh->num_points+qh_setsize(qh, qh->other_points), numfacets, totneighbors/2);
    else {
      qh->printoutvar= qh->num_points+qh_setsize(qh, qh->other_points);
      if (qh->DELAUNAY)
        num--;
      qh_fprintf(qh, fp, 9061, "%d\n%d %d %d\n", num, qh->printoutvar
        + numfacets - numsimplicial, numsimplicial + numridges, totneighbors/2);
    }
    FORALLpoints
      qh_printpointid(qh, qh->fout, NULL, num, point, qh_IDunknown);
    FOREACHpoint_(qh->other_points)
      qh_printpointid(qh, qh->fout, NULL, num, point, qh_IDunknown);
    if (format == qh_PRINTtriangles && qh->hull_dim > 2) {
      FORALLfacets {
        if (!facet->simplicial && facet->visitid)
          qh_printcenter(qh, qh->fout, format, NULL, facet);
      }
    }
    break;
  case qh_PRINTpointnearest:
    qh_fprintf(qh, fp, 9062, "%d\n", numcoplanars);
    break;
  case qh_PRINTpoints:
    if (!qh->VORONOI)
      goto LABELnoformat;
    if (qh->CDDoutput)
      qh_fprintf(qh, fp, 9063, "%s | %s\nbegin\n%d %d real\n", qh->rbox_command,
           qh->qhull_command, numfacets, qh->hull_dim);
    else
      qh_fprintf(qh, fp, 9064, "%d\n%d\n", qh->hull_dim-1, numfacets);
    break;
  case qh_PRINTvertices:
    qh_fprintf(qh, fp, 9065, "%d\n", numfacets);
    break;
  case qh_PRINTsummary:
  default:
  LABELnoformat:
    qh_fprintf(qh, qh->ferr, 6068, "qhull internal error (qh_printbegin): can not use this format for dimension %d\n",
         qh->hull_dim);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
}
















void qh_printcenter(qhT *qh, FILE *fp, qh_PRINT format, const char *string, facetT *facet) {
  int k, num;

  if (qh->CENTERtype != qh_ASvoronoi && qh->CENTERtype != qh_AScentrum)
    return;
  if (string)
    qh_fprintf(qh, fp, 9066, string);
  if (qh->CENTERtype == qh_ASvoronoi) {
    num= qh->hull_dim-1;
    if (!facet->normal || !facet->upperdelaunay || !qh->ATinfinity) {
      if (!facet->center)
        facet->center= qh_facetcenter(qh, facet->vertices);
      for (k=0; k < num; k++)
        qh_fprintf(qh, fp, 9067, qh_REAL_1, facet->center[k]);
    }else {
      for (k=0; k < num; k++)
        qh_fprintf(qh, fp, 9068, qh_REAL_1, qh_INFINITE);
    }
  }else   {
    num= qh->hull_dim;
    if (format == qh_PRINTtriangles && qh->DELAUNAY)
      num--;
    if (!facet->center)
      facet->center= qh_getcentrum(qh, facet);
    for (k=0; k < num; k++)
      qh_fprintf(qh, fp, 9069, qh_REAL_1, facet->center[k]);
  }
  if (format == qh_PRINTgeom && num == 2)
    qh_fprintf(qh, fp, 9070, " 0\n");
  else
    qh_fprintf(qh, fp, 9071, "\n");
}












void qh_printcentrum(qhT *qh, FILE *fp, facetT *facet, realT radius) {
  pointT *centrum, *projpt;
  boolT tempcentrum= False;
  realT xaxis[4], yaxis[4], normal[4], dist;
  realT green[3]={0, 1, 0};
  vertexT *apex;
  int k;

  if (qh->CENTERtype == qh_AScentrum) {
    if (!facet->center)
      facet->center= qh_getcentrum(qh, facet);
    centrum= facet->center;
  }else {
    centrum= qh_getcentrum(qh, facet);
    tempcentrum= True;
  }
  qh_fprintf(qh, fp, 9072, "{appearance {-normal -edge normscale 0} ");
  if (qh->firstcentrum) {
    qh->firstcentrum= False;
    qh_fprintf(qh, fp, 9073, "{INST geom { define centrum CQUAD  # f%d\n\
-0.3 -0.3 0.0001     0 0 1 1\n\
 0.3 -0.3 0.0001     0 0 1 1\n\
 0.3  0.3 0.0001     0 0 1 1\n\
-0.3  0.3 0.0001     0 0 1 1 } transform { \n", facet->id);
  }else
    qh_fprintf(qh, fp, 9074, "{INST geom { : centrum } transform { # f%d\n", facet->id);
  apex= SETfirstt_(facet->vertices, vertexT);
  qh_distplane(qh, apex->point, facet, &dist);
  projpt= qh_projectpoint(qh, apex->point, facet, dist);
  for (k=qh->hull_dim; k--; ) {
    xaxis[k]= projpt[k] - centrum[k];
    normal[k]= facet->normal[k];
  }
  if (qh->hull_dim == 2) {
    xaxis[2]= 0;
    normal[2]= 0;
  }else if (qh->hull_dim == 4) {
    qh_projectdim3(qh, xaxis, xaxis);
    qh_projectdim3(qh, normal, normal);
    qh_normalize2(qh, normal, qh->PRINTdim, True, NULL, NULL);
  }
  qh_crossproduct(3, xaxis, normal, yaxis);
  qh_fprintf(qh, fp, 9075, "%8.4g %8.4g %8.4g 0\n", xaxis[0], xaxis[1], xaxis[2]);
  qh_fprintf(qh, fp, 9076, "%8.4g %8.4g %8.4g 0\n", yaxis[0], yaxis[1], yaxis[2]);
  qh_fprintf(qh, fp, 9077, "%8.4g %8.4g %8.4g 0\n", normal[0], normal[1], normal[2]);
  qh_printpoint3(qh, fp, centrum);
  qh_fprintf(qh, fp, 9078, "1 }}}\n");
  qh_memfree(qh, projpt, qh->normal_size);
  qh_printpointvect(qh, fp, centrum, facet->normal, NULL, radius, green);
  if (tempcentrum)
    qh_memfree(qh, centrum, qh->normal_size);
}











void qh_printend(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetlist, setT *facets, boolT printall) {
  int num;
  facetT *facet, **facetp;

  if (!qh->printoutnum)
    qh_fprintf(qh, qh->ferr, 7055, "qhull warning: no facets printed\n");
  switch (format) {
  case qh_PRINTgeom:
    if (qh->hull_dim == 4 && qh->DROPdim < 0  && !qh->PRINTnoplanes) {
      qh->visit_id++;
      num= 0;
      FORALLfacet_(facetlist)
        qh_printend4geom(qh, fp, facet,&num, printall);
      FOREACHfacet_(facets)
        qh_printend4geom(qh, fp, facet, &num, printall);
      if (num != qh->ridgeoutnum || qh->printoutvar != qh->ridgeoutnum) {
        qh_fprintf(qh, qh->ferr, 6069, "qhull internal error (qh_printend): number of ridges %d != number printed %d and at end %d\n", qh->ridgeoutnum, qh->printoutvar, num);
        qh_errexit(qh, qh_ERRqhull, NULL, NULL);
      }
    }else
      qh_fprintf(qh, fp, 9079, "}\n");
    break;
  case qh_PRINTinner:
  case qh_PRINTnormals:
  case qh_PRINTouter:
    if (qh->CDDoutput)
      qh_fprintf(qh, fp, 9080, "end\n");
    break;
  case qh_PRINTmaple:
    qh_fprintf(qh, fp, 9081, "));\n");
    break;
  case qh_PRINTmathematica:
    qh_fprintf(qh, fp, 9082, "}\n");
    break;
  case qh_PRINTpoints:
    if (qh->CDDoutput)
      qh_fprintf(qh, fp, 9083, "end\n");
    break;
  default:
    break;
  }
}



















void qh_printend4geom(qhT *qh, FILE *fp, facetT *facet, int *nump, boolT printall) {
  realT color[3];
  int i, num= *nump;
  facetT *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;

  if (!printall && qh_skipfacet(qh, facet))
    return;
  if (qh->PRINTnoplanes || (facet->visible && qh->NEWfacets))
    return;
  if (!facet->normal)
    return;
  if (fp) {
    for (i=0; i < 3; i++) {
      color[i]= (facet->normal[i]+1.0)/2.0;
      maximize_(color[i], -1.0);
      minimize_(color[i], +1.0);
    }
  }
  facet->visitid= qh->visit_id;
  if (facet->simplicial) {
    FOREACHneighbor_(facet) {
      if (neighbor->visitid != qh->visit_id) {
        if (fp)
          qh_fprintf(qh, fp, 9084, "3 %d %d %d %8.4g %8.4g %8.4g 1 # f%d f%d\n",
                 3*num, 3*num+1, 3*num+2, color[0], color[1], color[2],
                 facet->id, neighbor->id);
        num++;
      }
    }
  }else {
    FOREACHridge_(facet->ridges) {
      neighbor= otherfacet_(ridge, facet);
      if (neighbor->visitid != qh->visit_id) {
        if (fp)
          qh_fprintf(qh, fp, 9085, "3 %d %d %d %8.4g %8.4g %8.4g 1 #r%d f%d f%d\n",
                 3*num, 3*num+1, 3*num+2, color[0], color[1], color[2],
                 ridge->id, facet->id, neighbor->id);
        num++;
      }
    }
  }
  *nump= num;
}













void qh_printextremes(qhT *qh, FILE *fp, facetT *facetlist, setT *facets, boolT printall) {
  setT *vertices, *points;
  pointT *point;
  vertexT *vertex, **vertexp;
  int id;
  int numpoints=0, point_i, point_n;
  int allpoints= qh->num_points + qh_setsize(qh, qh->other_points);

  points= qh_settemp(qh, allpoints);
  qh_setzero(qh, points, 0, allpoints);
  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  FOREACHvertex_(vertices) {
    id= qh_pointid(qh, vertex->point);
    if (id >= 0) {
      SETelem_(points, id)= vertex->point;
      numpoints++;
    }
  }
  qh_settempfree(qh, &vertices);
  qh_fprintf(qh, fp, 9086, "%d\n", numpoints);
  FOREACHpoint_i_(qh, points) {
    if (point)
      qh_fprintf(qh, fp, 9087, "%d\n", point_i);
  }
  qh_settempfree(qh, &points);
}













void qh_printextremes_2d(qhT *qh, FILE *fp, facetT *facetlist, setT *facets, boolT printall) {
  int numfacets, numridges, totneighbors, numcoplanars, numsimplicial, numtricoplanars;
  setT *vertices;
  facetT *facet, *startfacet, *nextfacet;
  vertexT *vertexA, *vertexB;

  qh_countfacets(qh, facetlist, facets, printall, &numfacets, &numsimplicial,
      &totneighbors, &numridges, &numcoplanars, &numtricoplanars);
  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  qh_fprintf(qh, fp, 9088, "%d\n", qh_setsize(qh, vertices));
  qh_settempfree(qh, &vertices);
  if (!numfacets)
    return;
  facet= startfacet= facetlist ? facetlist : SETfirstt_(facets, facetT);
  qh->vertex_visit++;
  qh->visit_id++;
  do {
    if (facet->toporient ^ qh_ORIENTclock) {
      vertexA= SETfirstt_(facet->vertices, vertexT);
      vertexB= SETsecondt_(facet->vertices, vertexT);
      nextfacet= SETfirstt_(facet->neighbors, facetT);
    }else {
      vertexA= SETsecondt_(facet->vertices, vertexT);
      vertexB= SETfirstt_(facet->vertices, vertexT);
      nextfacet= SETsecondt_(facet->neighbors, facetT);
    }
    if (facet->visitid == qh->visit_id) {
      qh_fprintf(qh, qh->ferr, 6218, "Qhull internal error (qh_printextremes_2d): loop in facet list.  facet %d nextfacet %d\n",
                 facet->id, nextfacet->id);
      qh_errexit2(qh, qh_ERRqhull, facet, nextfacet);
    }
    if (facet->visitid) {
      if (vertexA->visitid != qh->vertex_visit) {
        vertexA->visitid= qh->vertex_visit;
        qh_fprintf(qh, fp, 9089, "%d\n", qh_pointid(qh, vertexA->point));
      }
      if (vertexB->visitid != qh->vertex_visit) {
        vertexB->visitid= qh->vertex_visit;
        qh_fprintf(qh, fp, 9090, "%d\n", qh_pointid(qh, vertexB->point));
      }
    }
    facet->visitid= qh->visit_id;
    facet= nextfacet;
  }while (facet && facet != startfacet);
}












void qh_printextremes_d(qhT *qh, FILE *fp, facetT *facetlist, setT *facets, boolT printall) {
  setT *vertices;
  vertexT *vertex, **vertexp;
  boolT upperseen, lowerseen;
  facetT *neighbor, **neighborp;
  int numpoints=0;

  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  qh_vertexneighbors(qh);
  FOREACHvertex_(vertices) {
    upperseen= lowerseen= False;
    FOREACHneighbor_(vertex) {
      if (neighbor->upperdelaunay)
        upperseen= True;
      else
        lowerseen= True;
    }
    if (upperseen && lowerseen) {
      vertex->seen= True;
      numpoints++;
    }else
      vertex->seen= False;
  }
  qh_fprintf(qh, fp, 9091, "%d\n", numpoints);
  FOREACHvertex_(vertices) {
    if (vertex->seen)
      qh_fprintf(qh, fp, 9092, "%d\n", qh_pointid(qh, vertex->point));
  }
  qh_settempfree(qh, &vertices);
}










void qh_printfacet(qhT *qh, FILE *fp, facetT *facet) {

  qh_printfacetheader(qh, fp, facet);
  if (facet->ridges)
    qh_printfacetridges(qh, fp, facet);
}













void qh_printfacet2geom(qhT *qh, FILE *fp, facetT *facet, realT color[3]) {
  pointT *point0, *point1;
  realT mindist, innerplane, outerplane;
  int k;

  qh_facet2point(qh, facet, &point0, &point1, &mindist);
  qh_geomplanes(qh, facet, &outerplane, &innerplane);
  if (qh->PRINTouter || (!qh->PRINTnoplanes && !qh->PRINTinner))
    qh_printfacet2geom_points(qh, fp, point0, point1, facet, outerplane, color);
  if (qh->PRINTinner || (!qh->PRINTnoplanes && !qh->PRINTouter &&
                outerplane - innerplane > 2 * qh->MAXabs_coord * qh_GEOMepsilon)) {
    for (k=3; k--; )
      color[k]= 1.0 - color[k];
    qh_printfacet2geom_points(qh, fp, point0, point1, facet, innerplane, color);
  }
  qh_memfree(qh, point1, qh->normal_size);
  qh_memfree(qh, point0, qh->normal_size);
}








void qh_printfacet2geom_points(qhT *qh, FILE *fp, pointT *point1, pointT *point2,
                               facetT *facet, realT offset, realT color[3]) {
  pointT *p1= point1, *p2= point2;

  qh_fprintf(qh, fp, 9093, "VECT 1 2 1 2 1 # f%d\n", facet->id);
  if (offset != 0.0) {
    p1= qh_projectpoint(qh, p1, facet, -offset);
    p2= qh_projectpoint(qh, p2, facet, -offset);
  }
  qh_fprintf(qh, fp, 9094, "%8.4g %8.4g %8.4g\n%8.4g %8.4g %8.4g\n",
           p1[0], p1[1], 0.0, p2[0], p2[1], 0.0);
  if (offset != 0.0) {
    qh_memfree(qh, p1, qh->normal_size);
    qh_memfree(qh, p2, qh->normal_size);
  }
  qh_fprintf(qh, fp, 9095, "%8.4g %8.4g %8.4g 1.0\n", color[0], color[1], color[2]);
}













void qh_printfacet2math(qhT *qh, FILE *fp, facetT *facet, qh_PRINT format, int notfirst) {
  pointT *point0, *point1;
  realT mindist;
  const char *pointfmt;

  qh_facet2point(qh, facet, &point0, &point1, &mindist);
  if (notfirst)
    qh_fprintf(qh, fp, 9096, ",");
  if (format == qh_PRINTmaple)
    pointfmt= "[[%16.8f, %16.8f], [%16.8f, %16.8f]]\n";
  else
    pointfmt= "Line[{{%16.8f, %16.8f}, {%16.8f, %16.8f}}]\n";
  qh_fprintf(qh, fp, 9097, pointfmt, point0[0], point0[1], point1[0], point1[1]);
  qh_memfree(qh, point1, qh->normal_size);
  qh_memfree(qh, point0, qh->normal_size);
}












void qh_printfacet3geom_nonsimplicial(qhT *qh, FILE *fp, facetT *facet, realT color[3]) {
  ridgeT *ridge, **ridgep;
  setT *projectedpoints, *vertices;
  vertexT *vertex, **vertexp, *vertexA, *vertexB;
  pointT *projpt, *point, **pointp;
  facetT *neighbor;
  realT dist, outerplane, innerplane;
  int cntvertices, k;
  realT black[3]={0, 0, 0}, green[3]={0, 1, 0};

  qh_geomplanes(qh, facet, &outerplane, &innerplane);
  vertices= qh_facet3vertex(qh, facet);
  cntvertices= qh_setsize(qh, vertices);
  projectedpoints= qh_settemp(qh, cntvertices);
  FOREACHvertex_(vertices) {
    zinc_(Zdistio);
    qh_distplane(qh, vertex->point, facet, &dist);
    projpt= qh_projectpoint(qh, vertex->point, facet, dist);
    qh_setappend(qh, &projectedpoints, projpt);
  }
  if (qh->PRINTouter || (!qh->PRINTnoplanes && !qh->PRINTinner))
    qh_printfacet3geom_points(qh, fp, projectedpoints, facet, outerplane, color);
  if (qh->PRINTinner || (!qh->PRINTnoplanes && !qh->PRINTouter &&
                outerplane - innerplane > 2 * qh->MAXabs_coord * qh_GEOMepsilon)) {
    for (k=3; k--; )
      color[k]= 1.0 - color[k];
    qh_printfacet3geom_points(qh, fp, projectedpoints, facet, innerplane, color);
  }
  FOREACHpoint_(projectedpoints)
    qh_memfree(qh, point, qh->normal_size);
  qh_settempfree(qh, &projectedpoints);
  qh_settempfree(qh, &vertices);
  if ((qh->DOintersections || qh->PRINTridges)
  && (!facet->visible || !qh->NEWfacets)) {
    facet->visitid= qh->visit_id;
    FOREACHridge_(facet->ridges) {
      neighbor= otherfacet_(ridge, facet);
      if (neighbor->visitid != qh->visit_id) {
        if (qh->DOintersections)
          qh_printhyperplaneintersection(qh, fp, facet, neighbor, ridge->vertices, black);
        if (qh->PRINTridges) {
          vertexA= SETfirstt_(ridge->vertices, vertexT);
          vertexB= SETsecondt_(ridge->vertices, vertexT);
          qh_printline3geom(qh, fp, vertexA->point, vertexB->point, green);
        }
      }
    }
  }
}









void qh_printfacet3geom_points(qhT *qh, FILE *fp, setT *points, facetT *facet, realT offset, realT color[3]) {
  int k, n= qh_setsize(qh, points), i;
  pointT *point, **pointp;
  setT *printpoints;

  qh_fprintf(qh, fp, 9098, "{ OFF %d 1 1 # f%d\n", n, facet->id);
  if (offset != 0.0) {
    printpoints= qh_settemp(qh, n);
    FOREACHpoint_(points)
      qh_setappend(qh, &printpoints, qh_projectpoint(qh, point, facet, -offset));
  }else
    printpoints= points;
  FOREACHpoint_(printpoints) {
    for (k=0; k < qh->hull_dim; k++) {
      if (k == qh->DROPdim)
        qh_fprintf(qh, fp, 9099, "0 ");
      else
        qh_fprintf(qh, fp, 9100, "%8.4g ", point[k]);
    }
    if (printpoints != points)
      qh_memfree(qh, point, qh->normal_size);
    qh_fprintf(qh, fp, 9101, "\n");
  }
  if (printpoints != points)
    qh_settempfree(qh, &printpoints);
  qh_fprintf(qh, fp, 9102, "%d ", n);
  for (i=0; i < n; i++)
    qh_fprintf(qh, fp, 9103, "%d ", i);
  qh_fprintf(qh, fp, 9104, "%8.4g %8.4g %8.4g 1.0 }\n", color[0], color[1], color[2]);
}
















void qh_printfacet3geom_simplicial(qhT *qh, FILE *fp, facetT *facet, realT color[3]) {
  setT *points, *vertices;
  vertexT *vertex, **vertexp, *vertexA, *vertexB;
  facetT *neighbor, **neighborp;
  realT outerplane, innerplane;
  realT black[3]={0, 0, 0}, green[3]={0, 1, 0};
  int k;

  qh_geomplanes(qh, facet, &outerplane, &innerplane);
  vertices= qh_facet3vertex(qh, facet);
  points= qh_settemp(qh, qh->TEMPsize);
  FOREACHvertex_(vertices)
    qh_setappend(qh, &points, vertex->point);
  if (qh->PRINTouter || (!qh->PRINTnoplanes && !qh->PRINTinner))
    qh_printfacet3geom_points(qh, fp, points, facet, outerplane, color);
  if (qh->PRINTinner || (!qh->PRINTnoplanes && !qh->PRINTouter &&
              outerplane - innerplane > 2 * qh->MAXabs_coord * qh_GEOMepsilon)) {
    for (k=3; k--; )
      color[k]= 1.0 - color[k];
    qh_printfacet3geom_points(qh, fp, points, facet, innerplane, color);
  }
  qh_settempfree(qh, &points);
  qh_settempfree(qh, &vertices);
  if ((qh->DOintersections || qh->PRINTridges)
  && (!facet->visible || !qh->NEWfacets)) {
    facet->visitid= qh->visit_id;
    FOREACHneighbor_(facet) {
      if (neighbor->visitid != qh->visit_id) {
        vertices= qh_setnew_delnthsorted(qh, facet->vertices, qh->hull_dim,
                          SETindex_(facet->neighbors, neighbor), 0);
        if (qh->DOintersections)
           qh_printhyperplaneintersection(qh, fp, facet, neighbor, vertices, black);
        if (qh->PRINTridges) {
          vertexA= SETfirstt_(vertices, vertexT);
          vertexB= SETsecondt_(vertices, vertexT);
          qh_printline3geom(qh, fp, vertexA->point, vertexB->point, green);
        }
        qh_setfree(qh, &vertices);
      }
    }
  }
}












void qh_printfacet3math(qhT *qh, FILE *fp, facetT *facet, qh_PRINT format, int notfirst) {
  vertexT *vertex, **vertexp;
  setT *points, *vertices;
  pointT *point, **pointp;
  boolT firstpoint= True;
  realT dist;
  const char *pointfmt, *endfmt;

  if (notfirst)
    qh_fprintf(qh, fp, 9105, ",\n");
  vertices= qh_facet3vertex(qh, facet);
  points= qh_settemp(qh, qh_setsize(qh, vertices));
  FOREACHvertex_(vertices) {
    zinc_(Zdistio);
    qh_distplane(qh, vertex->point, facet, &dist);
    point= qh_projectpoint(qh, vertex->point, facet, dist);
    qh_setappend(qh, &points, point);
  }
  if (format == qh_PRINTmaple) {
    qh_fprintf(qh, fp, 9106, "[");
    pointfmt= "[%16.8f, %16.8f, %16.8f]";
    endfmt= "]";
  }else {
    qh_fprintf(qh, fp, 9107, "Polygon[{");
    pointfmt= "{%16.8f, %16.8f, %16.8f}";
    endfmt= "}]";
  }
  FOREACHpoint_(points) {
    if (firstpoint)
      firstpoint= False;
    else
      qh_fprintf(qh, fp, 9108, ",\n");
    qh_fprintf(qh, fp, 9109, pointfmt, point[0], point[1], point[2]);
  }
  FOREACHpoint_(points)
    qh_memfree(qh, point, qh->normal_size);
  qh_settempfree(qh, &points);
  qh_settempfree(qh, &vertices);
  qh_fprintf(qh, fp, 9110, "%s", endfmt);
}












void qh_printfacet3vertex(qhT *qh, FILE *fp, facetT *facet, qh_PRINT format) {
  vertexT *vertex, **vertexp;
  setT *vertices;

  vertices= qh_facet3vertex(qh, facet);
  if (format == qh_PRINToff)
    qh_fprintf(qh, fp, 9111, "%d ", qh_setsize(qh, vertices));
  FOREACHvertex_(vertices)
    qh_fprintf(qh, fp, 9112, "%d ", qh_pointid(qh, vertex->point));
  qh_fprintf(qh, fp, 9113, "\n");
  qh_settempfree(qh, &vertices);
}














void qh_printfacet4geom_nonsimplicial(qhT *qh, FILE *fp, facetT *facet, realT color[3]) {
  facetT *neighbor;
  ridgeT *ridge, **ridgep;
  vertexT *vertex, **vertexp;
  pointT *point;
  int k;
  realT dist;

  facet->visitid= qh->visit_id;
  if (qh->PRINTnoplanes || (facet->visible && qh->NEWfacets))
    return;
  FOREACHridge_(facet->ridges) {
    neighbor= otherfacet_(ridge, facet);
    if (neighbor->visitid == qh->visit_id)
      continue;
    if (qh->PRINTtransparent && !neighbor->good)
      continue;
    if (qh->DOintersections)
      qh_printhyperplaneintersection(qh, fp, facet, neighbor, ridge->vertices, color);
    else {
      if (qh->DROPdim >= 0)
        qh_fprintf(qh, fp, 9114, "OFF 3 1 1 # f%d\n", facet->id);
      else {
        qh->printoutvar++;
        qh_fprintf(qh, fp, 9115, "# r%d between f%d f%d\n", ridge->id, facet->id, neighbor->id);
      }
      FOREACHvertex_(ridge->vertices) {
        zinc_(Zdistio);
        qh_distplane(qh, vertex->point,facet, &dist);
        point=qh_projectpoint(qh, vertex->point,facet, dist);
        for (k=0; k < qh->hull_dim; k++) {
          if (k != qh->DROPdim)
            qh_fprintf(qh, fp, 9116, "%8.4g ", point[k]);
        }
        qh_fprintf(qh, fp, 9117, "\n");
        qh_memfree(qh, point, qh->normal_size);
      }
      if (qh->DROPdim >= 0)
        qh_fprintf(qh, fp, 9118, "3 0 1 2 %8.4g %8.4g %8.4g\n", color[0], color[1], color[2]);
    }
  }
}












void qh_printfacet4geom_simplicial(qhT *qh, FILE *fp, facetT *facet, realT color[3]) {
  setT *vertices;
  facetT *neighbor, **neighborp;
  vertexT *vertex, **vertexp;
  int k;

  facet->visitid= qh->visit_id;
  if (qh->PRINTnoplanes || (facet->visible && qh->NEWfacets))
    return;
  FOREACHneighbor_(facet) {
    if (neighbor->visitid == qh->visit_id)
      continue;
    if (qh->PRINTtransparent && !neighbor->good)
      continue;
    vertices= qh_setnew_delnthsorted(qh, facet->vertices, qh->hull_dim,
                          SETindex_(facet->neighbors, neighbor), 0);
    if (qh->DOintersections)
      qh_printhyperplaneintersection(qh, fp, facet, neighbor, vertices, color);
    else {
      if (qh->DROPdim >= 0)
        qh_fprintf(qh, fp, 9119, "OFF 3 1 1 # ridge between f%d f%d\n",
                facet->id, neighbor->id);
      else {
        qh->printoutvar++;
        qh_fprintf(qh, fp, 9120, "# ridge between f%d f%d\n", facet->id, neighbor->id);
      }
      FOREACHvertex_(vertices) {
        for (k=0; k < qh->hull_dim; k++) {
          if (k != qh->DROPdim)
            qh_fprintf(qh, fp, 9121, "%8.4g ", vertex->point[k]);
        }
        qh_fprintf(qh, fp, 9122, "\n");
      }
      if (qh->DROPdim >= 0)
        qh_fprintf(qh, fp, 9123, "3 0 1 2 %8.4g %8.4g %8.4g\n", color[0], color[1], color[2]);
    }
    qh_setfree(qh, &vertices);
  }
}









void qh_printfacetNvertex_nonsimplicial(qhT *qh, FILE *fp, facetT *facet, int id, qh_PRINT format) {
  vertexT *vertex, **vertexp;
  ridgeT *ridge, **ridgep;

  if (facet->visible && qh->NEWfacets)
    return;
  FOREACHridge_(facet->ridges) {
    if (format == qh_PRINTtriangles)
      qh_fprintf(qh, fp, 9124, "%d ", qh->hull_dim);
    qh_fprintf(qh, fp, 9125, "%d ", id);
    if ((ridge->top == facet) ^ qh_ORIENTclock) {
      FOREACHvertex_(ridge->vertices)
        qh_fprintf(qh, fp, 9126, "%d ", qh_pointid(qh, vertex->point));
    }else {
      FOREACHvertexreverse12_(ridge->vertices)
        qh_fprintf(qh, fp, 9127, "%d ", qh_pointid(qh, vertex->point));
    }
    qh_fprintf(qh, fp, 9128, "\n");
  }
}











void qh_printfacetNvertex_simplicial(qhT *qh, FILE *fp, facetT *facet, qh_PRINT format) {
  vertexT *vertex, **vertexp;

  if (format == qh_PRINToff || format == qh_PRINTtriangles)
    qh_fprintf(qh, fp, 9129, "%d ", qh_setsize(qh, facet->vertices));
  if ((facet->toporient ^ qh_ORIENTclock)
  || (qh->hull_dim > 2 && !facet->simplicial)) {
    FOREACHvertex_(facet->vertices)
      qh_fprintf(qh, fp, 9130, "%d ", qh_pointid(qh, vertex->point));
  }else {
    FOREACHvertexreverse12_(facet->vertices)
      qh_fprintf(qh, fp, 9131, "%d ", qh_pointid(qh, vertex->point));
  }
  qh_fprintf(qh, fp, 9132, "\n");
}












void qh_printfacetheader(qhT *qh, FILE *fp, facetT *facet) {
  pointT *point, **pointp, *furthest;
  facetT *neighbor, **neighborp;
  realT dist;

  if (facet == qh_MERGEridge) {
    qh_fprintf(qh, fp, 9133, " MERGEridge\n");
    return;
  }else if (facet == qh_DUPLICATEridge) {
    qh_fprintf(qh, fp, 9134, " DUPLICATEridge\n");
    return;
  }else if (!facet) {
    qh_fprintf(qh, fp, 9135, " NULLfacet\n");
    return;
  }
  qh->old_randomdist= qh->RANDOMdist;
  qh->RANDOMdist= False;
  qh_fprintf(qh, fp, 9136, "- f%d\n", facet->id);
  qh_fprintf(qh, fp, 9137, "    - flags:");
  if (facet->toporient)
    qh_fprintf(qh, fp, 9138, " top");
  else
    qh_fprintf(qh, fp, 9139, " bottom");
  if (facet->simplicial)
    qh_fprintf(qh, fp, 9140, " simplicial");
  if (facet->tricoplanar)
    qh_fprintf(qh, fp, 9141, " tricoplanar");
  if (facet->upperdelaunay)
    qh_fprintf(qh, fp, 9142, " upperDelaunay");
  if (facet->visible)
    qh_fprintf(qh, fp, 9143, " visible");
  if (facet->newfacet)
    qh_fprintf(qh, fp, 9144, " new");
  if (facet->tested)
    qh_fprintf(qh, fp, 9145, " tested");
  if (!facet->good)
    qh_fprintf(qh, fp, 9146, " notG");
  if (facet->seen)
    qh_fprintf(qh, fp, 9147, " seen");
  if (facet->coplanar)
    qh_fprintf(qh, fp, 9148, " coplanar");
  if (facet->mergehorizon)
    qh_fprintf(qh, fp, 9149, " mergehorizon");
  if (facet->keepcentrum)
    qh_fprintf(qh, fp, 9150, " keepcentrum");
  if (facet->dupridge)
    qh_fprintf(qh, fp, 9151, " dupridge");
  if (facet->mergeridge && !facet->mergeridge2)
    qh_fprintf(qh, fp, 9152, " mergeridge1");
  if (facet->mergeridge2)
    qh_fprintf(qh, fp, 9153, " mergeridge2");
  if (facet->newmerge)
    qh_fprintf(qh, fp, 9154, " newmerge");
  if (facet->flipped)
    qh_fprintf(qh, fp, 9155, " flipped");
  if (facet->notfurthest)
    qh_fprintf(qh, fp, 9156, " notfurthest");
  if (facet->degenerate)
    qh_fprintf(qh, fp, 9157, " degenerate");
  if (facet->redundant)
    qh_fprintf(qh, fp, 9158, " redundant");
  qh_fprintf(qh, fp, 9159, "\n");
  if (facet->isarea)
    qh_fprintf(qh, fp, 9160, "    - area: %2.2g\n", facet->f.area);
  else if (qh->NEWfacets && facet->visible && facet->f.replace)
    qh_fprintf(qh, fp, 9161, "    - replacement: f%d\n", facet->f.replace->id);
  else if (facet->newfacet) {
    if (facet->f.samecycle && facet->f.samecycle != facet)
      qh_fprintf(qh, fp, 9162, "    - shares same visible/horizon as f%d\n", facet->f.samecycle->id);
  }else if (facet->tricoplanar  ) {
    if (facet->f.triowner)
      qh_fprintf(qh, fp, 9163, "    - owner of normal & centrum is facet f%d\n", facet->f.triowner->id);
  }else if (facet->f.newcycle)
    qh_fprintf(qh, fp, 9164, "    - was horizon to f%d\n", facet->f.newcycle->id);
  if (facet->nummerge)
    qh_fprintf(qh, fp, 9165, "    - merges: %d\n", facet->nummerge);
  qh_printpointid(qh, fp, "    - normal: ", qh->hull_dim, facet->normal, qh_IDunknown);
  qh_fprintf(qh, fp, 9166, "    - offset: %10.7g\n", facet->offset);
  if (qh->CENTERtype == qh_ASvoronoi || facet->center)
    qh_printcenter(qh, fp, qh_PRINTfacets, "    - center: ", facet);
#if qh_MAXoutside
  if (facet->maxoutside > qh->DISTround)
    qh_fprintf(qh, fp, 9167, "    - maxoutside: %10.7g\n", facet->maxoutside);
#endif
  if (!SETempty_(facet->outsideset)) {
    furthest= (pointT*)qh_setlast(facet->outsideset);
    if (qh_setsize(qh, facet->outsideset) < 6) {
      qh_fprintf(qh, fp, 9168, "    - outside set(furthest p%d):\n", qh_pointid(qh, furthest));
      FOREACHpoint_(facet->outsideset)
        qh_printpoint(qh, fp, "     ", point);
    }else if (qh_setsize(qh, facet->outsideset) < 21) {
      qh_printpoints(qh, fp, "    - outside set:", facet->outsideset);
    }else {
      qh_fprintf(qh, fp, 9169, "    - outside set:  %d points.", qh_setsize(qh, facet->outsideset));
      qh_printpoint(qh, fp, "  Furthest", furthest);
    }
#if !qh_COMPUTEfurthest
    qh_fprintf(qh, fp, 9170, "    - furthest distance= %2.2g\n", facet->furthestdist);
#endif
  }
  if (!SETempty_(facet->coplanarset)) {
    furthest= (pointT*)qh_setlast(facet->coplanarset);
    if (qh_setsize(qh, facet->coplanarset) < 6) {
      qh_fprintf(qh, fp, 9171, "    - coplanar set(furthest p%d):\n", qh_pointid(qh, furthest));
      FOREACHpoint_(facet->coplanarset)
        qh_printpoint(qh, fp, "     ", point);
    }else if (qh_setsize(qh, facet->coplanarset) < 21) {
      qh_printpoints(qh, fp, "    - coplanar set:", facet->coplanarset);
    }else {
      qh_fprintf(qh, fp, 9172, "    - coplanar set:  %d points.", qh_setsize(qh, facet->coplanarset));
      qh_printpoint(qh, fp, "  Furthest", furthest);
    }
    zinc_(Zdistio);
    qh_distplane(qh, furthest, facet, &dist);
    qh_fprintf(qh, fp, 9173, "      furthest distance= %2.2g\n", dist);
  }
  qh_printvertices(qh, fp, "    - vertices:", facet->vertices);
  qh_fprintf(qh, fp, 9174, "    - neighboring facets:");
  FOREACHneighbor_(facet) {
    if (neighbor == qh_MERGEridge)
      qh_fprintf(qh, fp, 9175, " MERGE");
    else if (neighbor == qh_DUPLICATEridge)
      qh_fprintf(qh, fp, 9176, " DUP");
    else
      qh_fprintf(qh, fp, 9177, " f%d", neighbor->id);
  }
  qh_fprintf(qh, fp, 9178, "\n");
  qh->RANDOMdist= qh->old_randomdist;
}














void qh_printfacetridges(qhT *qh, FILE *fp, facetT *facet) {
  facetT *neighbor, **neighborp;
  ridgeT *ridge, **ridgep;
  int numridges= 0;


  if (facet->visible && qh->NEWfacets) {
    qh_fprintf(qh, fp, 9179, "    - ridges(ids may be garbage):");
    FOREACHridge_(facet->ridges)
      qh_fprintf(qh, fp, 9180, " r%d", ridge->id);
    qh_fprintf(qh, fp, 9181, "\n");
  }else {
    qh_fprintf(qh, fp, 9182, "    - ridges:\n");
    FOREACHridge_(facet->ridges)
      ridge->seen= False;
    if (qh->hull_dim == 3) {
      ridge= SETfirstt_(facet->ridges, ridgeT);
      while (ridge && !ridge->seen) {
        ridge->seen= True;
        qh_printridge(qh, fp, ridge);
        numridges++;
        ridge= qh_nextridge3d(ridge, facet, NULL);
        }
    }else {
      FOREACHneighbor_(facet) {
        FOREACHridge_(facet->ridges) {
          if (otherfacet_(ridge,facet) == neighbor) {
            ridge->seen= True;
            qh_printridge(qh, fp, ridge);
            numridges++;
          }
        }
      }
    }
    if (numridges != qh_setsize(qh, facet->ridges)) {
      qh_fprintf(qh, fp, 9183, "     - all ridges:");
      FOREACHridge_(facet->ridges)
        qh_fprintf(qh, fp, 9184, " r%d", ridge->id);
        qh_fprintf(qh, fp, 9185, "\n");
    }
    FOREACHridge_(facet->ridges) {
      if (!ridge->seen)
        qh_printridge(qh, fp, ridge);
    }
  }
}











void qh_printfacets(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetlist, setT *facets, boolT printall) {
  int numfacets, numsimplicial, numridges, totneighbors, numcoplanars, numtricoplanars;
  facetT *facet, **facetp;
  setT *vertices;
  coordT *center;
  realT outerplane, innerplane;

  qh->old_randomdist= qh->RANDOMdist;
  qh->RANDOMdist= False;
  if (qh->CDDoutput && (format == qh_PRINTcentrums || format == qh_PRINTpointintersect || format == qh_PRINToff))
    qh_fprintf(qh, qh->ferr, 7056, "qhull warning: CDD format is not available for centrums, halfspace\nintersections, and OFF file format.\n");
  if (format == qh_PRINTnone)
    ;
  else if (format == qh_PRINTaverage) {
    vertices= qh_facetvertices(qh, facetlist, facets, printall);
    center= qh_getcenter(qh, vertices);
    qh_fprintf(qh, fp, 9186, "%d 1\n", qh->hull_dim);
    qh_printpointid(qh, fp, NULL, qh->hull_dim, center, qh_IDunknown);
    qh_memfree(qh, center, qh->normal_size);
    qh_settempfree(qh, &vertices);
  }else if (format == qh_PRINTextremes) {
    if (qh->DELAUNAY)
      qh_printextremes_d(qh, fp, facetlist, facets, printall);
    else if (qh->hull_dim == 2)
      qh_printextremes_2d(qh, fp, facetlist, facets, printall);
    else
      qh_printextremes(qh, fp, facetlist, facets, printall);
  }else if (format == qh_PRINToptions)
    qh_fprintf(qh, fp, 9187, "Options selected for Qhull %s:\n%s\n", qh_version, qh->qhull_options);
  else if (format == qh_PRINTpoints && !qh->VORONOI)
    qh_printpoints_out(qh, fp, facetlist, facets, printall);
  else if (format == qh_PRINTqhull)
    qh_fprintf(qh, fp, 9188, "%s | %s\n", qh->rbox_command, qh->qhull_command);
  else if (format == qh_PRINTsize) {
    qh_fprintf(qh, fp, 9189, "0\n2 ");
    qh_fprintf(qh, fp, 9190, qh_REAL_1, qh->totarea);
    qh_fprintf(qh, fp, 9191, qh_REAL_1, qh->totvol);
    qh_fprintf(qh, fp, 9192, "\n");
  }else if (format == qh_PRINTsummary) {
    qh_countfacets(qh, facetlist, facets, printall, &numfacets, &numsimplicial,
      &totneighbors, &numridges, &numcoplanars, &numtricoplanars);
    vertices= qh_facetvertices(qh, facetlist, facets, printall);
    qh_fprintf(qh, fp, 9193, "10 %d %d %d %d %d %d %d %d %d %d\n2 ", qh->hull_dim,
                qh->num_points + qh_setsize(qh, qh->other_points),
                qh->num_vertices, qh->num_facets - qh->num_visible,
                qh_setsize(qh, vertices), numfacets, numcoplanars,
                numfacets - numsimplicial, zzval_(Zdelvertextot),
                numtricoplanars);
    qh_settempfree(qh, &vertices);
    qh_outerinner(qh, NULL, &outerplane, &innerplane);
    qh_fprintf(qh, fp, 9194, qh_REAL_2n, outerplane, innerplane);
  }else if (format == qh_PRINTvneighbors)
    qh_printvneighbors(qh, fp, facetlist, facets, printall);
  else if (qh->VORONOI && format == qh_PRINToff)
    qh_printvoronoi(qh, fp, format, facetlist, facets, printall);
  else if (qh->VORONOI && format == qh_PRINTgeom) {
    qh_printbegin(qh, fp, format, facetlist, facets, printall);
    qh_printvoronoi(qh, fp, format, facetlist, facets, printall);
    qh_printend(qh, fp, format, facetlist, facets, printall);
  }else if (qh->VORONOI
  && (format == qh_PRINTvertices || format == qh_PRINTinner || format == qh_PRINTouter))
    qh_printvdiagram(qh, fp, format, facetlist, facets, printall);
  else {
    qh_printbegin(qh, fp, format, facetlist, facets, printall);
    FORALLfacet_(facetlist)
      qh_printafacet(qh, fp, format, facet, printall);
    FOREACHfacet_(facets)
      qh_printafacet(qh, fp, format, facet, printall);
    qh_printend(qh, fp, format, facetlist, facets, printall);
  }
  qh->RANDOMdist= qh->old_randomdist;
}








void qh_printhyperplaneintersection(qhT *qh, FILE *fp, facetT *facet1, facetT *facet2,
                   setT *vertices, realT color[3]) {
  realT costheta, denominator, dist1, dist2, s, t, mindenom, p[4];
  vertexT *vertex, **vertexp;
  int i, k;
  boolT nearzero1, nearzero2;

  costheta= qh_getangle(qh, facet1->normal, facet2->normal);
  denominator= 1 - costheta * costheta;
  i= qh_setsize(qh, vertices);
  if (qh->hull_dim == 3)
    qh_fprintf(qh, fp, 9195, "VECT 1 %d 1 %d 1 ", i, i);
  else if (qh->hull_dim == 4 && qh->DROPdim >= 0)
    qh_fprintf(qh, fp, 9196, "OFF 3 1 1 ");
  else
    qh->printoutvar++;
  qh_fprintf(qh, fp, 9197, "# intersect f%d f%d\n", facet1->id, facet2->id);
  mindenom= 1 / (10.0 * qh->MAXabs_coord);
  FOREACHvertex_(vertices) {
    zadd_(Zdistio, 2);
    qh_distplane(qh, vertex->point, facet1, &dist1);
    qh_distplane(qh, vertex->point, facet2, &dist2);
    s= qh_divzero(-dist1 + costheta * dist2, denominator,mindenom,&nearzero1);
    t= qh_divzero(-dist2 + costheta * dist1, denominator,mindenom,&nearzero2);
    if (nearzero1 || nearzero2)
      s= t= 0.0;
    for (k=qh->hull_dim; k--; )
      p[k]= vertex->point[k] + facet1->normal[k] * s + facet2->normal[k] * t;
    if (qh->PRINTdim <= 3) {
      qh_projectdim3(qh, p, p);
      qh_fprintf(qh, fp, 9198, "%8.4g %8.4g %8.4g # ", p[0], p[1], p[2]);
    }else
      qh_fprintf(qh, fp, 9199, "%8.4g %8.4g %8.4g %8.4g # ", p[0], p[1], p[2], p[3]);
    if (nearzero1+nearzero2)
      qh_fprintf(qh, fp, 9200, "p%d(coplanar facets)\n", qh_pointid(qh, vertex->point));
    else
      qh_fprintf(qh, fp, 9201, "projected p%d\n", qh_pointid(qh, vertex->point));
  }
  if (qh->hull_dim == 3)
    qh_fprintf(qh, fp, 9202, "%8.4g %8.4g %8.4g 1.0\n", color[0], color[1], color[2]);
  else if (qh->hull_dim == 4 && qh->DROPdim >= 0)
    qh_fprintf(qh, fp, 9203, "3 0 1 2 %8.4g %8.4g %8.4g 1.0\n", color[0], color[1], color[2]);
}












void qh_printline3geom(qhT *qh, FILE *fp, pointT *pointA, pointT *pointB, realT color[3]) {
  int k;
  realT pA[4], pB[4];

  qh_projectdim3(qh, pointA, pA);
  qh_projectdim3(qh, pointB, pB);
  if ((fabs(pA[0] - pB[0]) > 1e-3) ||
      (fabs(pA[1] - pB[1]) > 1e-3) ||
      (fabs(pA[2] - pB[2]) > 1e-3)) {
    qh_fprintf(qh, fp, 9204, "VECT 1 2 1 2 1\n");
    for (k=0; k < 3; k++)
       qh_fprintf(qh, fp, 9205, "%8.4g ", pB[k]);
    qh_fprintf(qh, fp, 9206, " # p%d\n", qh_pointid(qh, pointB));
  }else
    qh_fprintf(qh, fp, 9207, "VECT 1 1 1 1 1\n");
  for (k=0; k < 3; k++)
    qh_fprintf(qh, fp, 9208, "%8.4g ", pA[k]);
  qh_fprintf(qh, fp, 9209, " # p%d\n", qh_pointid(qh, pointA));
  qh_fprintf(qh, fp, 9210, "%8.4g %8.4g %8.4g 1\n", color[0], color[1], color[2]);
}











void qh_printneighborhood(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetA, facetT *facetB, boolT printall) {
  facetT *neighbor, **neighborp, *facet;
  setT *facets;

  if (format == qh_PRINTnone)
    return;
  qh_findgood_all(qh, qh->facet_list);
  if (facetA == facetB)
    facetB= NULL;
  facets= qh_settemp(qh, 2*(qh_setsize(qh, facetA->neighbors)+1));
  qh->visit_id++;
  for (facet= facetA; facet; facet= ((facet == facetA) ? facetB : NULL)) {
    if (facet->visitid != qh->visit_id) {
      facet->visitid= qh->visit_id;
      qh_setappend(qh, &facets, facet);
    }
    FOREACHneighbor_(facet) {
      if (neighbor->visitid == qh->visit_id)
        continue;
      neighbor->visitid= qh->visit_id;
      if (printall || !qh_skipfacet(qh, neighbor))
        qh_setappend(qh, &facets, neighbor);
    }
  }
  qh_printfacets(qh, fp, format, NULL, facets, printall);
  qh_settempfree(qh, &facets);
}
















void qh_printpoint(qhT *qh, FILE *fp, const char *string, pointT *point) {
  int id= qh_pointid(qh, point);

  qh_printpointid(qh, fp, string, qh->hull_dim, point, id);
}

void qh_printpointid(qhT *qh, FILE *fp, const char *string, int dim, pointT *point, int id) {
  int k;
  realT r;

  if (!point)
    return;
  if (string) {
    qh_fprintf(qh, fp, 9211, "%s", string);
    if (id != qh_IDunknown && id != qh_IDnone)
      qh_fprintf(qh, fp, 9212, " p%d: ", id);
  }
  for (k=dim; k--; ) {
    r= *point++;
    if (string)
      qh_fprintf(qh, fp, 9213, " %8.4g", r);
    else
      qh_fprintf(qh, fp, 9214, qh_REAL_1, r);
  }
  qh_fprintf(qh, fp, 9215, "\n");
}







void qh_printpoint3(qhT *qh, FILE *fp, pointT *point) {
  int k;
  realT p[4];

  qh_projectdim3(qh, point, p);
  for (k=0; k < 3; k++)
    qh_fprintf(qh, fp, 9216, "%8.4g ", p[k]);
  qh_fprintf(qh, fp, 9217, " # p%d\n", qh_pointid(qh, point));
}


















void qh_printpoints_out(qhT *qh, FILE *fp, facetT *facetlist, setT *facets, boolT printall) {
  int allpoints= qh->num_points + qh_setsize(qh, qh->other_points);
  int numpoints=0, point_i, point_n;
  setT *vertices, *points;
  facetT *facet, **facetp;
  pointT *point, **pointp;
  vertexT *vertex, **vertexp;
  int id;

  points= qh_settemp(qh, allpoints);
  qh_setzero(qh, points, 0, allpoints);
  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  FOREACHvertex_(vertices) {
    id= qh_pointid(qh, vertex->point);
    if (id >= 0)
      SETelem_(points, id)= vertex->point;
  }
  if (qh->KEEPinside || qh->KEEPcoplanar || qh->KEEPnearinside) {
    FORALLfacet_(facetlist) {
      if (!printall && qh_skipfacet(qh, facet))
        continue;
      FOREACHpoint_(facet->coplanarset) {
        id= qh_pointid(qh, point);
        if (id >= 0)
          SETelem_(points, id)= point;
      }
    }
    FOREACHfacet_(facets) {
      if (!printall && qh_skipfacet(qh, facet))
        continue;
      FOREACHpoint_(facet->coplanarset) {
        id= qh_pointid(qh, point);
        if (id >= 0)
          SETelem_(points, id)= point;
      }
    }
  }
  qh_settempfree(qh, &vertices);
  FOREACHpoint_i_(qh, points) {
    if (point)
      numpoints++;
  }
  if (qh->CDDoutput)
    qh_fprintf(qh, fp, 9218, "%s | %s\nbegin\n%d %d real\n", qh->rbox_command,
             qh->qhull_command, numpoints, qh->hull_dim + 1);
  else
    qh_fprintf(qh, fp, 9219, "%d\n%d\n", qh->hull_dim, numpoints);
  FOREACHpoint_i_(qh, points) {
    if (point) {
      if (qh->CDDoutput)
        qh_fprintf(qh, fp, 9220, "1 ");
      qh_printpoint(qh, fp, NULL, point);
    }
  }
  if (qh->CDDoutput)
    qh_fprintf(qh, fp, 9221, "end\n");
  qh_settempfree(qh, &points);
}








void qh_printpointvect(qhT *qh, FILE *fp, pointT *point, coordT *normal, pointT *center, realT radius, realT color[3]) {
  realT diff[4], pointA[4];
  int k;

  for (k=qh->hull_dim; k--; ) {
    if (center)
      diff[k]= point[k]-center[k];
    else if (normal)
      diff[k]= normal[k];
    else
      diff[k]= 0;
  }
  if (center)
    qh_normalize2(qh, diff, qh->hull_dim, True, NULL, NULL);
  for (k=qh->hull_dim; k--; )
    pointA[k]= point[k]+diff[k] * radius;
  qh_printline3geom(qh, fp, point, pointA, color);
}







void qh_printpointvect2(qhT *qh, FILE *fp, pointT *point, coordT *normal, pointT *center, realT radius) {
  realT red[3]={1, 0, 0}, yellow[3]={1, 1, 0};

  qh_printpointvect(qh, fp, point, normal, center, radius, red);
  qh_printpointvect(qh, fp, point, normal, center, -radius, yellow);
}











void qh_printridge(qhT *qh, FILE *fp, ridgeT *ridge) {

  qh_fprintf(qh, fp, 9222, "     - r%d", ridge->id);
  if (ridge->tested)
    qh_fprintf(qh, fp, 9223, " tested");
  if (ridge->nonconvex)
    qh_fprintf(qh, fp, 9224, " nonconvex");
  qh_fprintf(qh, fp, 9225, "\n");
  qh_printvertices(qh, fp, "           vertices:", ridge->vertices);
  if (ridge->top && ridge->bottom)
    qh_fprintf(qh, fp, 9226, "           between f%d and f%d\n",
            ridge->top->id, ridge->bottom->id);
}










void qh_printspheres(qhT *qh, FILE *fp, setT *vertices, realT radius) {
  vertexT *vertex, **vertexp;

  qh->printoutnum++;
  qh_fprintf(qh, fp, 9227, "{appearance {-edge -normal normscale 0} {\n\
INST geom {define vsphere OFF\n\
18 32 48\n\
\n\
0 0 1\n\
1 0 0\n\
0 1 0\n\
-1 0 0\n\
0 -1 0\n\
0 0 -1\n\
0.707107 0 0.707107\n\
0 -0.707107 0.707107\n\
0.707107 -0.707107 0\n\
-0.707107 0 0.707107\n\
-0.707107 -0.707107 0\n\
0 0.707107 0.707107\n\
-0.707107 0.707107 0\n\
0.707107 0.707107 0\n\
0.707107 0 -0.707107\n\
0 0.707107 -0.707107\n\
-0.707107 0 -0.707107\n\
0 -0.707107 -0.707107\n\
\n\
3 0 6 11\n\
3 0 7 6 \n\
3 0 9 7 \n\
3 0 11 9\n\
3 1 6 8 \n\
3 1 8 14\n\
3 1 13 6\n\
3 1 14 13\n\
3 2 11 13\n\
3 2 12 11\n\
3 2 13 15\n\
3 2 15 12\n\
3 3 9 12\n\
3 3 10 9\n\
3 3 12 16\n\
3 3 16 10\n\
3 4 7 10\n\
3 4 8 7\n\
3 4 10 17\n\
3 4 17 8\n\
3 5 14 17\n\
3 5 15 14\n\
3 5 16 15\n\
3 5 17 16\n\
3 6 13 11\n\
3 7 8 6\n\
3 9 10 7\n\
3 11 12 9\n\
3 14 8 17\n\
3 15 13 14\n\
3 16 12 15\n\
3 17 10 16\n} transforms { TLIST\n");
  FOREACHvertex_(vertices) {
    qh_fprintf(qh, fp, 9228, "%8.4g 0 0 0 # v%d\n 0 %8.4g 0 0\n0 0 %8.4g 0\n",
      radius, vertex->id, radius, radius);
    qh_printpoint3(qh, fp, vertex->point);
    qh_fprintf(qh, fp, 9229, "1\n");
  }
  qh_fprintf(qh, fp, 9230, "}}}\n");
}





























void qh_printvdiagram(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetlist, setT *facets, boolT printall) {
  setT *vertices;
  int totcount, numcenters;
  boolT isLower;
  qh_RIDGE innerouter= qh_RIDGEall;
  printvridgeT printvridge= NULL;

  if (format == qh_PRINTvertices) {
    innerouter= qh_RIDGEall;
    printvridge= qh_printvridge;
  }else if (format == qh_PRINTinner) {
    innerouter= qh_RIDGEinner;
    printvridge= qh_printvnorm;
  }else if (format == qh_PRINTouter) {
    innerouter= qh_RIDGEouter;
    printvridge= qh_printvnorm;
  }else {
    qh_fprintf(qh, qh->ferr, 6219, "Qhull internal error (qh_printvdiagram): unknown print format %d.\n", format);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  vertices= qh_markvoronoi(qh, facetlist, facets, printall, &isLower, &numcenters);
  totcount= qh_printvdiagram2(qh, NULL, NULL, vertices, innerouter, False);
  qh_fprintf(qh, fp, 9231, "%d\n", totcount);
  totcount= qh_printvdiagram2(qh, fp, printvridge, vertices, innerouter, True  );
  qh_settempfree(qh, &vertices);
#if 0
  qh_fprintf(qh, fp, 9232, "\n");
  totcount= qh_eachvoronoi_all(qh, fp, printvridge, qh->UPPERdelaunay, innerouter, True  );
  qh_fprintf(qh, fp, 9233, "%d\n", totcount);
#endif
}






























int qh_printvdiagram2(qhT *qh, FILE *fp, printvridgeT printvridge, setT *vertices, qh_RIDGE innerouter, boolT inorder) {
  int totcount= 0;
  int vertex_i, vertex_n;
  vertexT *vertex;

  FORALLvertices
    vertex->seen= False;
  FOREACHvertex_i_(qh, vertices) {
    if (vertex) {
      if (qh->GOODvertex > 0 && qh_pointid(qh, vertex->point)+1 != qh->GOODvertex)
        continue;
      totcount += qh_eachvoronoi(qh, fp, printvridge, vertex, !qh_ALL, innerouter, inorder);
    }
  }
  return totcount;
}








void qh_printvertex(qhT *qh, FILE *fp, vertexT *vertex) {
  pointT *point;
  int k, count= 0;
  facetT *neighbor, **neighborp;
  realT r;

  if (!vertex) {
    qh_fprintf(qh, fp, 9234, "  NULLvertex\n");
    return;
  }
  qh_fprintf(qh, fp, 9235, "- p%d(v%d):", qh_pointid(qh, vertex->point), vertex->id);
  point= vertex->point;
  if (point) {
    for (k=qh->hull_dim; k--; ) {
      r= *point++;
      qh_fprintf(qh, fp, 9236, " %5.2g", r);
    }
  }
  if (vertex->deleted)
    qh_fprintf(qh, fp, 9237, " deleted");
  if (vertex->delridge)
    qh_fprintf(qh, fp, 9238, " ridgedeleted");
  qh_fprintf(qh, fp, 9239, "\n");
  if (vertex->neighbors) {
    qh_fprintf(qh, fp, 9240, "  neighbors:");
    FOREACHneighbor_(vertex) {
      if (++count % 100 == 0)
        qh_fprintf(qh, fp, 9241, "\n     ");
      qh_fprintf(qh, fp, 9242, " f%d", neighbor->id);
    }
    qh_fprintf(qh, fp, 9243, "\n");
  }
}









void qh_printvertexlist(qhT *qh, FILE *fp, const char* string, facetT *facetlist,
                         setT *facets, boolT printall) {
  vertexT *vertex, **vertexp;
  setT *vertices;

  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  qh_fprintf(qh, fp, 9244, "%s", string);
  FOREACHvertex_(vertices)
    qh_printvertex(qh, fp, vertex);
  qh_settempfree(qh, &vertices);
}









void qh_printvertices(qhT *qh, FILE *fp, const char* string, setT *vertices) {
  vertexT *vertex, **vertexp;

  qh_fprintf(qh, fp, 9245, "%s", string);
  FOREACHvertex_(vertices)
    qh_fprintf(qh, fp, 9246, " p%d(v%d)", qh_pointid(qh, vertex->point), vertex->id);
  qh_fprintf(qh, fp, 9247, "\n");
}


















void qh_printvneighbors(qhT *qh, FILE *fp, facetT* facetlist, setT *facets, boolT printall) {
  int numfacets, numsimplicial, numridges, totneighbors, numneighbors, numcoplanars, numtricoplanars;
  setT *vertices, *vertex_points, *coplanar_points;
  int numpoints= qh->num_points + qh_setsize(qh, qh->other_points);
  vertexT *vertex, **vertexp;
  int vertex_i, vertex_n;
  facetT *facet, **facetp, *neighbor, **neighborp;
  pointT *point, **pointp;

  qh_countfacets(qh, facetlist, facets, printall, &numfacets, &numsimplicial,
      &totneighbors, &numridges, &numcoplanars, &numtricoplanars);
  qh_fprintf(qh, fp, 9248, "%d\n", numpoints);
  qh_vertexneighbors(qh);
  vertices= qh_facetvertices(qh, facetlist, facets, printall);
  vertex_points= qh_settemp(qh, numpoints);
  coplanar_points= qh_settemp(qh, numpoints);
  qh_setzero(qh, vertex_points, 0, numpoints);
  qh_setzero(qh, coplanar_points, 0, numpoints);
  FOREACHvertex_(vertices)
    qh_point_add(qh, vertex_points, vertex->point, vertex);
  FORALLfacet_(facetlist) {
    FOREACHpoint_(facet->coplanarset)
      qh_point_add(qh, coplanar_points, point, facet);
  }
  FOREACHfacet_(facets) {
    FOREACHpoint_(facet->coplanarset)
      qh_point_add(qh, coplanar_points, point, facet);
  }
  FOREACHvertex_i_(qh, vertex_points) {
    if (vertex) {
      numneighbors= qh_setsize(qh, vertex->neighbors);
      qh_fprintf(qh, fp, 9249, "%d", numneighbors);
      if (qh->hull_dim == 3)
        qh_order_vertexneighbors(qh, vertex);
      else if (qh->hull_dim >= 4)
        qsort(SETaddr_(vertex->neighbors, facetT), (size_t)numneighbors,
             sizeof(facetT *), qh_compare_facetvisit);
      FOREACHneighbor_(vertex)
        qh_fprintf(qh, fp, 9250, " %d",
                 neighbor->visitid ? neighbor->visitid - 1 : 0 - neighbor->id);
      qh_fprintf(qh, fp, 9251, "\n");
    }else if ((facet= SETelemt_(coplanar_points, vertex_i, facetT)))
      qh_fprintf(qh, fp, 9252, "1 %d\n",
                  facet->visitid ? facet->visitid - 1 : 0 - facet->id);
    else
      qh_fprintf(qh, fp, 9253, "0\n");
  }
  qh_settempfree(qh, &coplanar_points);
  qh_settempfree(qh, &vertex_points);
  qh_settempfree(qh, &vertices);
}
























void qh_printvoronoi(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetlist, setT *facets, boolT printall) {
  int k, numcenters, numvertices= 0, numneighbors, numinf, vid=1, vertex_i, vertex_n;
  facetT *facet, **facetp, *neighbor, **neighborp;
  setT *vertices;
  vertexT *vertex;
  boolT isLower;
  unsigned int numfacets= (unsigned int) qh->num_facets;

  vertices= qh_markvoronoi(qh, facetlist, facets, printall, &isLower, &numcenters);
  FOREACHvertex_i_(qh, vertices) {
    if (vertex) {
      numvertices++;
      numneighbors = numinf = 0;
      FOREACHneighbor_(vertex) {
        if (neighbor->visitid == 0)
          numinf= 1;
        else if (neighbor->visitid < numfacets)
          numneighbors++;
      }
      if (numinf && !numneighbors) {
        SETelem_(vertices, vertex_i)= NULL;
        numvertices--;
      }
    }
  }
  if (format == qh_PRINTgeom)
    qh_fprintf(qh, fp, 9254, "{appearance {+edge -face} OFF %d %d 1 # Voronoi centers and cells\n",
                numcenters, numvertices);
  else
    qh_fprintf(qh, fp, 9255, "%d\n%d %d 1\n", qh->hull_dim-1, numcenters, qh_setsize(qh, vertices));
  if (format == qh_PRINTgeom) {
    for (k=qh->hull_dim-1; k--; )
      qh_fprintf(qh, fp, 9256, qh_REAL_1, 0.0);
    qh_fprintf(qh, fp, 9257, " 0 # infinity not used\n");
  }else {
    for (k=qh->hull_dim-1; k--; )
      qh_fprintf(qh, fp, 9258, qh_REAL_1, qh_INFINITE);
    qh_fprintf(qh, fp, 9259, "\n");
  }
  FORALLfacet_(facetlist) {
    if (facet->visitid && facet->visitid < numfacets) {
      if (format == qh_PRINTgeom)
        qh_fprintf(qh, fp, 9260, "# %d f%d\n", vid++, facet->id);
      qh_printcenter(qh, fp, format, NULL, facet);
    }
  }
  FOREACHfacet_(facets) {
    if (facet->visitid && facet->visitid < numfacets) {
      if (format == qh_PRINTgeom)
        qh_fprintf(qh, fp, 9261, "# %d f%d\n", vid++, facet->id);
      qh_printcenter(qh, fp, format, NULL, facet);
    }
  }
  FOREACHvertex_i_(qh, vertices) {
    numneighbors= 0;
    numinf=0;
    if (vertex) {
      if (qh->hull_dim == 3)
        qh_order_vertexneighbors(qh, vertex);
      else if (qh->hull_dim >= 4)
        qsort(SETaddr_(vertex->neighbors, facetT),
             (size_t)qh_setsize(qh, vertex->neighbors),
             sizeof(facetT *), qh_compare_facetvisit);
      FOREACHneighbor_(vertex) {
        if (neighbor->visitid == 0)
          numinf= 1;
        else if (neighbor->visitid < numfacets)
          numneighbors++;
      }
    }
    if (format == qh_PRINTgeom) {
      if (vertex) {
        qh_fprintf(qh, fp, 9262, "%d", numneighbors);
        FOREACHneighbor_(vertex) {
          if (neighbor->visitid && neighbor->visitid < numfacets)
            qh_fprintf(qh, fp, 9263, " %d", neighbor->visitid);
        }
        qh_fprintf(qh, fp, 9264, " # p%d(v%d)\n", vertex_i, vertex->id);
      }else
        qh_fprintf(qh, fp, 9265, " # p%d is coplanar or isolated\n", vertex_i);
    }else {
      if (numinf)
        numneighbors++;
      qh_fprintf(qh, fp, 9266, "%d", numneighbors);
      if (vertex) {
        FOREACHneighbor_(vertex) {
          if (neighbor->visitid == 0) {
            if (numinf) {
              numinf= 0;
              qh_fprintf(qh, fp, 9267, " %d", neighbor->visitid);
            }
          }else if (neighbor->visitid < numfacets)
            qh_fprintf(qh, fp, 9268, " %d", neighbor->visitid);
        }
      }
      qh_fprintf(qh, fp, 9269, "\n");
    }
  }
  if (format == qh_PRINTgeom)
    qh_fprintf(qh, fp, 9270, "}\n");
  qh_settempfree(qh, &vertices);
}


















void qh_printvnorm(qhT *qh, FILE *fp, vertexT *vertex, vertexT *vertexA, setT *centers, boolT unbounded) {
  pointT *normal;
  realT offset;
  int k;
  QHULL_UNUSED(unbounded);

  normal= qh_detvnorm(qh, vertex, vertexA, centers, &offset);
  qh_fprintf(qh, fp, 9271, "%d %d %d ",
      2+qh->hull_dim, qh_pointid(qh, vertex->point), qh_pointid(qh, vertexA->point));
  for (k=0; k< qh->hull_dim-1; k++)
    qh_fprintf(qh, fp, 9272, qh_REAL_1, normal[k]);
  qh_fprintf(qh, fp, 9273, qh_REAL_1, offset);
  qh_fprintf(qh, fp, 9274, "\n");
}















void qh_printvridge(qhT *qh, FILE *fp, vertexT *vertex, vertexT *vertexA, setT *centers, boolT unbounded) {
  facetT *facet, **facetp;
  QHULL_UNUSED(unbounded);

  qh_fprintf(qh, fp, 9275, "%d %d %d", qh_setsize(qh, centers)+2,
       qh_pointid(qh, vertex->point), qh_pointid(qh, vertexA->point));
  FOREACHfacet_(centers)
    qh_fprintf(qh, fp, 9276, " %d", facet->visitid);
  qh_fprintf(qh, fp, 9277, "\n");
}












void qh_projectdim3(qhT *qh, pointT *source, pointT *destination) {
  int i,k;

  for (k=0, i=0; k < qh->hull_dim; k++) {
    if (qh->hull_dim == 4) {
      if (k != qh->DROPdim)
        destination[i++]= source[k];
    }else if (k == qh->DROPdim)
      destination[i++]= 0;
    else
      destination[i++]= source[k];
  }
  while (i < 3)
    destination[i++]= 0.0;
}


















int qh_readfeasible(qhT *qh, int dim, const char *curline) {
  boolT isfirst= True;
  int linecount= 0, tokcount= 0;
  const char *s;
  char *t, firstline[qh_MAXfirst+1];
  coordT *coords, value;

  if (!qh->HALFspace) {
    qh_fprintf(qh, qh->ferr, 6070, "qhull input error: feasible point(dim 1 coords) is only valid for halfspace intersection\n");
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (qh->feasible_string)
    qh_fprintf(qh, qh->ferr, 7057, "qhull input warning: feasible point(dim 1 coords) overrides 'Hn,n,n' feasible point for halfspace intersection\n");
  if (!(qh->feasible_point= (coordT*)qh_malloc(dim* sizeof(coordT)))) {
    qh_fprintf(qh, qh->ferr, 6071, "qhull error: insufficient memory for feasible point\n");
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }
  coords= qh->feasible_point;
  while ((s= (isfirst ?  curline : fgets(firstline, qh_MAXfirst, qh->fin)))) {
    if (isfirst)
      isfirst= False;
    else
      linecount++;
    while (*s) {
      while (isspace(*s))
        s++;
      value= qh_strtod(s, &t);
      if (s == t)
        break;
      s= t;
      *(coords++)= value;
      if (++tokcount == dim) {
        while (isspace(*s))
          s++;
        qh_strtod(s, &t);
        if (s != t) {
          qh_fprintf(qh, qh->ferr, 6072, "qhull input error: coordinates for feasible point do not finish out the line: %s\n",
               s);
          qh_errexit(qh, qh_ERRinput, NULL, NULL);
        }
        return linecount;
      }
    }
  }
  qh_fprintf(qh, qh->ferr, 6073, "qhull input error: only %d coordinates.  Could not read %d-d feasible point.\n",
           tokcount, dim);
  qh_errexit(qh, qh_ERRinput, NULL, NULL);
  return 0;
}

































coordT *qh_readpoints(qhT *qh, int *numpoints, int *dimension, boolT *ismalloc) {
  coordT *points, *coords, *infinity= NULL;
  realT paraboloid, maxboloid= -REALmax, value;
  realT *coordp= NULL, *offsetp= NULL, *normalp= NULL;
  char *s= 0, *t, firstline[qh_MAXfirst+1];
  int diminput=0, numinput=0, dimfeasible= 0, newnum, k, tempi;
  int firsttext=0, firstshort=0, firstlong=0, firstpoint=0;
  int tokcount= 0, linecount=0, maxcount, coordcount=0;
  boolT islong, isfirst= True, wasbegin= False;
  boolT isdelaunay= qh->DELAUNAY && !qh->PROJECTinput;

  if (qh->CDDinput) {
    while ((s= fgets(firstline, qh_MAXfirst, qh->fin))) {
      linecount++;
      if (qh->HALFspace && linecount == 1 && isdigit(*s)) {
        dimfeasible= qh_strtol(s, &s);
        while (isspace(*s))
          s++;
        if (qh_strtol(s, &s) == 1)
          linecount += qh_readfeasible(qh, dimfeasible, s);
        else
          dimfeasible= 0;
      }else if (!memcmp(firstline, "begin", (size_t)5) || !memcmp(firstline, "BEGIN", (size_t)5))
        break;
      else if (!*qh->rbox_command)
        strncat(qh->rbox_command, s, sizeof(qh->rbox_command)-1);
    }
    if (!s) {
      qh_fprintf(qh, qh->ferr, 6074, "qhull input error: missing \"begin\" for cdd-formated input\n");
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
  }
  while (!numinput && (s= fgets(firstline, qh_MAXfirst, qh->fin))) {
    linecount++;
    if (!memcmp(s, "begin", (size_t)5) || !memcmp(s, "BEGIN", (size_t)5))
      wasbegin= True;
    while (*s) {
      while (isspace(*s))
        s++;
      if (!*s)
        break;
      if (!isdigit(*s)) {
        if (!*qh->rbox_command) {
          strncat(qh->rbox_command, s, sizeof(qh->rbox_command)-1);
          firsttext= linecount;
        }
        break;
      }
      if (!diminput)
        diminput= qh_strtol(s, &s);
      else {
        numinput= qh_strtol(s, &s);
        if (numinput == 1 && diminput >= 2 && qh->HALFspace && !qh->CDDinput) {
          linecount += qh_readfeasible(qh, diminput, s);
          dimfeasible= diminput;
          diminput= numinput= 0;
        }else
          break;
      }
    }
  }
  if (!s) {
    qh_fprintf(qh, qh->ferr, 6075, "qhull input error: short input file.  Did not find dimension and number of points\n");
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (diminput > numinput) {
    tempi= diminput;
    diminput= numinput;
    numinput= tempi;
  }
  if (diminput < 2) {
    qh_fprintf(qh, qh->ferr, 6220,"qhull input error: dimension %d(first number) should be at least 2\n",
            diminput);
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (isdelaunay) {
    qh->PROJECTdelaunay= False;
    if (qh->CDDinput)
      *dimension= diminput;
    else
      *dimension= diminput+1;
    *numpoints= numinput;
    if (qh->ATinfinity)
      (*numpoints)++;
  }else if (qh->HALFspace) {
    *dimension= diminput - 1;
    *numpoints= numinput;
    if (diminput < 3) {
      qh_fprintf(qh, qh->ferr, 6221,"qhull input error: dimension %d(first number, includes offset) should be at least 3 for halfspaces\n",
            diminput);
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    if (dimfeasible) {
      if (dimfeasible != *dimension) {
        qh_fprintf(qh, qh->ferr, 6222,"qhull input error: dimension %d of feasible point is not one less than dimension %d for halfspaces\n",
          dimfeasible, diminput);
        qh_errexit(qh, qh_ERRinput, NULL, NULL);
      }
    }else
      qh_setfeasible(qh, *dimension);
  }else {
    if (qh->CDDinput)
      *dimension= diminput-1;
    else
      *dimension= diminput;
    *numpoints= numinput;
  }
  qh->normal_size= *dimension * sizeof(coordT);
  if (qh->HALFspace) {
    qh->half_space= coordp= (coordT*)qh_malloc(qh->normal_size + sizeof(coordT));
    if (qh->CDDinput) {
      offsetp= qh->half_space;
      normalp= offsetp + 1;
    }else {
      normalp= qh->half_space;
      offsetp= normalp + *dimension;
    }
  }
  qh->maxline= diminput * (qh_REALdigits + 5);
  maximize_(qh->maxline, 500);
  qh->line= (char*)qh_malloc((qh->maxline+1) * sizeof(char));
  *ismalloc= True;
  coords= points= qh->temp_malloc=
        (coordT*)qh_malloc((*numpoints)*(*dimension)*sizeof(coordT));
  if (!coords || !qh->line || (qh->HALFspace && !qh->half_space)) {
    qh_fprintf(qh, qh->ferr, 6076, "qhull error: insufficient memory to read %d points\n",
            numinput);
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }
  if (isdelaunay && qh->ATinfinity) {
    infinity= points + numinput * (*dimension);
    for (k= (*dimension) - 1; k--; )
      infinity[k]= 0.0;
  }
  maxcount= numinput * diminput;
  paraboloid= 0.0;
  while ((s= (isfirst ?  s : fgets(qh->line, qh->maxline, qh->fin)))) {
    if (!isfirst) {
      linecount++;
      if (*s == 'e' || *s == 'E') {
        if (!memcmp(s, "end", (size_t)3) || !memcmp(s, "END", (size_t)3)) {
          if (qh->CDDinput )
            break;
          else if (wasbegin)
            qh_fprintf(qh, qh->ferr, 7058, "qhull input warning: the input appears to be in cdd format.  If so, use 'Fd'\n");
        }
      }
    }
    islong= False;
    while (*s) {
      while (isspace(*s))
        s++;
      value= qh_strtod(s, &t);
      if (s == t) {
        if (!*qh->rbox_command)
         strncat(qh->rbox_command, s, sizeof(qh->rbox_command)-1);
        if (*s && !firsttext)
          firsttext= linecount;
        if (!islong && !firstshort && coordcount)
          firstshort= linecount;
        break;
      }
      if (!firstpoint)
        firstpoint= linecount;
      s= t;
      if (++tokcount > maxcount)
        continue;
      if (qh->HALFspace) {
        if (qh->CDDinput)
          *(coordp++)= -value;
        else
          *(coordp++)= value;
      }else {
        *(coords++)= value;
        if (qh->CDDinput && !coordcount) {
          if (value != 1.0) {
            qh_fprintf(qh, qh->ferr, 6077, "qhull input error: for cdd format, point at line %d does not start with '1'\n",
                   linecount);
            qh_errexit(qh, qh_ERRinput, NULL, NULL);
          }
          coords--;
        }else if (isdelaunay) {
          paraboloid += value * value;
          if (qh->ATinfinity) {
            if (qh->CDDinput)
              infinity[coordcount-1] += value;
            else
              infinity[coordcount] += value;
          }
        }
      }
      if (++coordcount == diminput) {
        coordcount= 0;
        if (isdelaunay) {
          *(coords++)= paraboloid;
          maximize_(maxboloid, paraboloid);
          paraboloid= 0.0;
        }else if (qh->HALFspace) {
          if (!qh_sethalfspace(qh, *dimension, coords, &coords, normalp, offsetp, qh->feasible_point)) {
            qh_fprintf(qh, qh->ferr, 8048, "The halfspace was on line %d\n", linecount);
            if (wasbegin)
              qh_fprintf(qh, qh->ferr, 8049, "The input appears to be in cdd format.  If so, you should use option 'Fd'\n");
            qh_errexit(qh, qh_ERRinput, NULL, NULL);
          }
          coordp= qh->half_space;
        }
        while (isspace(*s))
          s++;
        if (*s) {
          islong= True;
          if (!firstlong)
            firstlong= linecount;
        }
      }
    }
    if (!islong && !firstshort && coordcount)
      firstshort= linecount;
    if (!isfirst && s - qh->line >= qh->maxline) {
      qh_fprintf(qh, qh->ferr, 6078, "qhull input error: line %d contained more than %d characters\n",
              linecount, (int) (s - qh->line));
      qh_errexit(qh, qh_ERRinput, NULL, NULL);
    }
    isfirst= False;
  }
  if (tokcount != maxcount) {
    newnum= fmin_(numinput, tokcount/diminput);
    qh_fprintf(qh, qh->ferr, 7073,"\
qhull warning: instead of %d %d-dimensional points, input contains\n\
%d points and %d extra coordinates.  Line %d is the first\npoint",
       numinput, diminput, tokcount/diminput, tokcount % diminput, firstpoint);
    if (firsttext)
      qh_fprintf(qh, qh->ferr, 8051, ", line %d is the first comment", firsttext);
    if (firstshort)
      qh_fprintf(qh, qh->ferr, 8052, ", line %d is the first short\nline", firstshort);
    if (firstlong)
      qh_fprintf(qh, qh->ferr, 8053, ", line %d is the first long line", firstlong);
    qh_fprintf(qh, qh->ferr, 8054, ".  Continue with %d points.\n", newnum);
    numinput= newnum;
    if (isdelaunay && qh->ATinfinity) {
      for (k= tokcount % diminput; k--; )
        infinity[k] -= *(--coords);
      *numpoints= newnum+1;
    }else {
      coords -= tokcount % diminput;
      *numpoints= newnum;
    }
  }
  if (isdelaunay && qh->ATinfinity) {
    for (k= (*dimension) -1; k--; )
      infinity[k] /= numinput;
    if (coords == infinity)
      coords += (*dimension) -1;
    else {
      for (k=0; k < (*dimension) -1; k++)
        *(coords++)= infinity[k];
    }
    *(coords++)= maxboloid * 1.1;
  }
  if (qh->rbox_command[0]) {
    qh->rbox_command[strlen(qh->rbox_command)-1]= '\0';
    if (!strcmp(qh->rbox_command, "./rbox D4"))
      qh_fprintf(qh, qh->ferr, 8055, "\n\
This is the qhull test case.  If any errors or core dumps occur,\n\
recompile qhull with 'make new'.  If errors still occur, there is\n\
an incompatibility.  You should try a different compiler.  You can also\n\
change the choices in user.h.  If you discover the source of the problem,\n\
please send mail to qhull_bug@qhull.org.\n\
\n\
Type 'qhull' for a short list of options.\n");
  }
  qh_free(qh->line);
  qh->line= NULL;
  if (qh->half_space) {
    qh_free(qh->half_space);
    qh->half_space= NULL;
  }
  qh->temp_malloc= NULL;
  trace1((qh, qh->ferr, 1008,"qh_readpoints: read in %d %d-dimensional points\n",
          numinput, diminput));
  return(points);
}













void qh_setfeasible(qhT *qh, int dim) {
  int tokcount= 0;
  char *s;
  coordT *coords, value;

  if (!(s= qh->feasible_string)) {
    qh_fprintf(qh, qh->ferr, 6223, "\
qhull input error: halfspace intersection needs a feasible point.\n\
Either prepend the input with 1 point or use 'Hn,n,n'.  See manual.\n");
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (!(qh->feasible_point= (pointT*)qh_malloc(dim * sizeof(coordT)))) {
    qh_fprintf(qh, qh->ferr, 6079, "qhull error: insufficient memory for 'Hn,n,n'\n");
    qh_errexit(qh, qh_ERRmem, NULL, NULL);
  }
  coords= qh->feasible_point;
  while (*s) {
    value= qh_strtod(s, &s);
    if (++tokcount > dim) {
      qh_fprintf(qh, qh->ferr, 7059, "qhull input warning: more coordinates for 'H%s' than dimension %d\n",
          qh->feasible_string, dim);
      break;
    }
    *(coords++)= value;
    if (*s)
      s++;
  }
  while (++tokcount <= dim)
    *(coords++)= 0.0;
}










boolT qh_skipfacet(qhT *qh, facetT *facet) {
  facetT *neighbor, **neighborp;

  if (qh->PRINTneighbors) {
    if (facet->good)
      return !qh->PRINTgood;
    FOREACHneighbor_(facet) {
      if (neighbor->good)
        return False;
    }
    return True;
  }else if (qh->PRINTgood)
    return !facet->good;
  else if (!facet->normal)
    return True;
  return(!qh_inthresholds(qh, facet->normal, NULL));
}













char *qh_skipfilename(qhT *qh, char *filename) {
  char *s= filename;
  char c;

  while (*s && isspace(*s))
    s++;
  c= *s++;
  if (c == '\0') {
    qh_fprintf(qh, qh->ferr, 6204, "qhull input error: filename expected, none found.\n");
    qh_errexit(qh, qh_ERRinput, NULL, NULL);
  }
  if (c == '\'' || c == '"') {
    while (*s !=c || s[-1] == '\\') {
      if (!*s) {
        qh_fprintf(qh, qh->ferr, 6203, "qhull input error: missing quote after filename -- %s\n", filename);
        qh_errexit(qh, qh_ERRinput, NULL, NULL);
      }
      s++;
    }
    s++;
  }
  else while (*s && !isspace(*s))
      s++;
  return s;
}
