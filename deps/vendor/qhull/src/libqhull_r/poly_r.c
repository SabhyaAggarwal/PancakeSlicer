/*<html><pre>  -<a                             href="qh-poly_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   poly_r.c
   implements polygons and simplices

   see qh-poly_r.htm, poly_r.h and libqhull_r.h

   infrequent code is in poly2_r.c
   (all but top 50 and their callers 12/3/95)

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/poly_r.c#3 $$Change: 2064 $
   $DateTime: 2016/01/18 12:36:08 $$Author: bbarber $
*/

#include "qhull_ra.h"




















void qh_appendfacet(qhT *qh, facetT *facet) {
  facetT *tail= qh->facet_tail;

  if (tail == qh->newfacet_list)
    qh->newfacet_list= facet;
  if (tail == qh->facet_next)
    qh->facet_next= facet;
  facet->previous= tail->previous;
  facet->next= tail;
  if (tail->previous)
    tail->previous->next= facet;
  else
    qh->facet_list= facet;
  tail->previous= facet;
  qh->num_facets++;
  trace4((qh, qh->ferr, 4044, "qh_appendfacet: append f%d to facet_list\n", facet->id));
}

















void qh_appendvertex(qhT *qh, vertexT *vertex) {
  vertexT *tail= qh->vertex_tail;

  if (tail == qh->newvertex_list)
    qh->newvertex_list= vertex;
  vertex->newlist= True;
  vertex->previous= tail->previous;
  vertex->next= tail;
  if (tail->previous)
    tail->previous->next= vertex;
  else
    qh->vertex_list= vertex;
  tail->previous= vertex;
  qh->num_vertices++;
  trace4((qh, qh->ferr, 4045, "qh_appendvertex: append v%d to vertex_list\n", vertex->id));
}









































void qh_attachnewfacets(qhT *qh  ) {
  facetT *newfacet= NULL, *neighbor, **neighborp, *horizon, *visible;
  ridgeT *ridge, **ridgep;

  qh->NEWfacets= True;
  trace3((qh, qh->ferr, 3012, "qh_attachnewfacets: delete interior ridges\n"));
  qh->visit_id++;
  FORALLvisible_facets {
    visible->visitid= qh->visit_id;
    if (visible->ridges) {
      FOREACHridge_(visible->ridges) {
        neighbor= otherfacet_(ridge, visible);
        if (neighbor->visitid == qh->visit_id
            || (!neighbor->visible && neighbor->simplicial)) {
          if (!neighbor->visible)
            qh_setdel(neighbor->ridges, ridge);
          qh_setfree(qh, &(ridge->vertices));
          qh_memfree(qh, ridge, (int)sizeof(ridgeT));
        }
      }
      SETfirst_(visible->ridges)= NULL;
    }
    SETfirst_(visible->neighbors)= NULL;
  }
  trace1((qh, qh->ferr, 1017, "qh_attachnewfacets: attach horizon facets to new facets\n"));
  FORALLnew_facets {
    horizon= SETfirstt_(newfacet->neighbors, facetT);
    if (horizon->simplicial) {
      visible= NULL;
      FOREACHneighbor_(horizon) {
        if (neighbor->visible) {
          if (visible) {
            if (qh_setequal_skip(newfacet->vertices, 0, horizon->vertices,
                                  SETindex_(horizon->neighbors, neighbor))) {
              visible= neighbor;
              break;
            }
          }else
            visible= neighbor;
        }
      }
      if (visible) {
        visible->f.replace= newfacet;
        qh_setreplace(qh, horizon->neighbors, visible, newfacet);
      }else {
        qh_fprintf(qh, qh->ferr, 6102, "qhull internal error (qh_attachnewfacets): couldn't find visible facet for horizon f%d of newfacet f%d\n",
                 horizon->id, newfacet->id);
        qh_errexit2(qh, qh_ERRqhull, horizon, newfacet);
      }
    }else {
      FOREACHneighbor_(horizon) {
        if (neighbor->visible) {
          neighbor->f.replace= newfacet;
          qh_setdelnth(qh, horizon->neighbors,
                        SETindex_(horizon->neighbors, neighbor));
          neighborp--;
        }
      }
      qh_setappend(qh, &horizon->neighbors, newfacet);
      ridge= SETfirstt_(newfacet->ridges, ridgeT);
      if (ridge->top == horizon)
        ridge->bottom= newfacet;
      else
        ridge->top= newfacet;
      }
  }
  if (qh->PRINTstatistics) {
    FORALLvisible_facets {
      if (!visible->f.replace)
        zinc_(Zinsidevisible);
    }
  }
}
















boolT qh_checkflipped(qhT *qh, facetT *facet, realT *distp, boolT allerror) {
  realT dist;

  if (facet->flipped && !distp)
    return False;
  zzinc_(Zdistcheck);
  qh_distplane(qh, qh->interior_point, facet, &dist);
  if (distp)
    *distp= dist;
  if ((allerror && dist > -qh->DISTround)|| (!allerror && dist >= 0.0)) {
    facet->flipped= True;
    zzinc_(Zflippedfacets);
    trace0((qh, qh->ferr, 19, "qh_checkflipped: facet f%d is flipped, distance= %6.12g during p%d\n",
              facet->id, dist, qh->furthest_id));
    qh_precision(qh, "flipped facet");
    return False;
  }
  return True;
}










void qh_delfacet(qhT *qh, facetT *facet) {
  void **freelistp;

  trace4((qh, qh->ferr, 4046, "qh_delfacet: delete f%d\n", facet->id));
  if (facet == qh->tracefacet)
    qh->tracefacet= NULL;
  if (facet == qh->GOODclosest)
    qh->GOODclosest= NULL;
  qh_removefacet(qh, facet);
  if (!facet->tricoplanar || facet->keepcentrum) {
    qh_memfree_(qh, facet->normal, qh->normal_size, freelistp);
    if (qh->CENTERtype == qh_ASvoronoi) {
      qh_memfree_(qh, facet->center, qh->center_size, freelistp);
    }else   {
      qh_memfree_(qh, facet->center, qh->normal_size, freelistp);
    }
  }
  qh_setfree(qh, &(facet->neighbors));
  if (facet->ridges)
    qh_setfree(qh, &(facet->ridges));
  qh_setfree(qh, &(facet->vertices));
  if (facet->outsideset)
    qh_setfree(qh, &(facet->outsideset));
  if (facet->coplanarset)
    qh_setfree(qh, &(facet->coplanarset));
  qh_memfree_(qh, facet, (int)sizeof(facetT), freelistp);
}


















void qh_deletevisible(qhT *qh  ) {
  facetT *visible, *nextfacet;
  vertexT *vertex, **vertexp;
  int numvisible= 0, numdel= qh_setsize(qh, qh->del_vertices);

  trace1((qh, qh->ferr, 1018, "qh_deletevisible: delete %d visible facets and %d vertices\n",
         qh->num_visible, numdel));
  for (visible= qh->visible_list; visible && visible->visible;
                visible= nextfacet) {
    nextfacet= visible->next;
    numvisible++;
    qh_delfacet(qh, visible);
  }
  if (numvisible != qh->num_visible) {
    qh_fprintf(qh, qh->ferr, 6103, "qhull internal error (qh_deletevisible): qh->num_visible %d is not number of visible facets %d\n",
             qh->num_visible, numvisible);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
  qh->num_visible= 0;
  zadd_(Zvisfacettot, numvisible);
  zmax_(Zvisfacetmax, numvisible);
  zzadd_(Zdelvertextot, numdel);
  zmax_(Zdelvertexmax, numdel);
  FOREACHvertex_(qh->del_vertices)
    qh_delvertex(qh, vertex);
  qh_settruncate(qh, qh->del_vertices, 0);
}
























setT *qh_facetintersect(qhT *qh, facetT *facetA, facetT *facetB,
                         int *skipA,int *skipB, int prepend) {
  setT *intersect;
  int dim= qh->hull_dim, i, j;
  facetT **neighborsA, **neighborsB;

  neighborsA= SETaddr_(facetA->neighbors, facetT);
  neighborsB= SETaddr_(facetB->neighbors, facetT);
  i= j= 0;
  if (facetB == *neighborsA++)
    *skipA= 0;
  else if (facetB == *neighborsA++)
    *skipA= 1;
  else if (facetB == *neighborsA++)
    *skipA= 2;
  else {
    for (i=3; i < dim; i++) {
      if (facetB == *neighborsA++) {
        *skipA= i;
        break;
      }
    }
  }
  if (facetA == *neighborsB++)
    *skipB= 0;
  else if (facetA == *neighborsB++)
    *skipB= 1;
  else if (facetA == *neighborsB++)
    *skipB= 2;
  else {
    for (j=3; j < dim; j++) {
      if (facetA == *neighborsB++) {
        *skipB= j;
        break;
      }
    }
  }
  if (i >= dim || j >= dim) {
    qh_fprintf(qh, qh->ferr, 6104, "qhull internal error (qh_facetintersect): f%d or f%d not in others neighbors\n",
            facetA->id, facetB->id);
    qh_errexit2(qh, qh_ERRqhull, facetA, facetB);
  }
  intersect= qh_setnew_delnthsorted(qh, facetA->vertices, qh->hull_dim, *skipA, prepend);
  trace4((qh, qh->ferr, 4047, "qh_facetintersect: f%d skip %d matches f%d skip %d\n",
          facetA->id, *skipA, facetB->id, *skipB));
  return(intersect);
}















int qh_gethash(qhT *qh, int hashsize, setT *set, int size, int firstindex, void *skipelem) {
  void **elemp= SETelemaddr_(set, firstindex, void);
  ptr_intT hash = 0, elem;
  unsigned result;
  int i;
#ifdef _MSC_VER
#pragma warning( push)
#pragma warning( disable : 4311)
#endif

  switch (size-firstindex) {
  case 1:
    hash= (ptr_intT)(*elemp) - (ptr_intT) skipelem;
    break;
  case 2:
    hash= (ptr_intT)(*elemp) + (ptr_intT)elemp[1] - (ptr_intT) skipelem;
    break;
  case 3:
    hash= (ptr_intT)(*elemp) + (ptr_intT)elemp[1] + (ptr_intT)elemp[2]
      - (ptr_intT) skipelem;
    break;
  case 4:
    hash= (ptr_intT)(*elemp) + (ptr_intT)elemp[1] + (ptr_intT)elemp[2]
      + (ptr_intT)elemp[3] - (ptr_intT) skipelem;
    break;
  case 5:
    hash= (ptr_intT)(*elemp) + (ptr_intT)elemp[1] + (ptr_intT)elemp[2]
      + (ptr_intT)elemp[3] + (ptr_intT)elemp[4] - (ptr_intT) skipelem;
    break;
  case 6:
    hash= (ptr_intT)(*elemp) + (ptr_intT)elemp[1] + (ptr_intT)elemp[2]
      + (ptr_intT)elemp[3] + (ptr_intT)elemp[4]+ (ptr_intT)elemp[5]
      - (ptr_intT) skipelem;
    break;
  default:
    hash= 0;
    i= 3;
    do {
      if ((elem= (ptr_intT)*elemp++) != (ptr_intT)skipelem) {
        hash ^= (elem << i) + (elem >> (32-i));
        i += 3;
        if (i >= 32)
          i -= 32;
      }
    }while (*elemp);
    break;
  }
  if (hashsize<0) {
    qh_fprintf(qh, qh->ferr, 6202, "qhull internal error: negative hashsize %d passed to qh_gethash [poly.c]\n", hashsize);
    qh_errexit2(qh, qh_ERRqhull, NULL, NULL);
  }
  result= (unsigned)hash;
  result %= (unsigned)hashsize;

  return result;
#ifdef _MSC_VER
#pragma warning( pop)
#endif
}















facetT *qh_makenewfacet(qhT *qh, setT *vertices, boolT toporient,facetT *horizon) {
  facetT *newfacet;
  vertexT *vertex, **vertexp;

  FOREACHvertex_(vertices) {
    if (!vertex->newlist) {
      qh_removevertex(qh, vertex);
      qh_appendvertex(qh, vertex);
    }
  }
  newfacet= qh_newfacet(qh);
  newfacet->vertices= vertices;
  newfacet->toporient= (unsigned char)toporient;
  if (horizon)
    qh_setappend(qh, &(newfacet->neighbors), horizon);
  qh_appendfacet(qh, newfacet);
  return(newfacet);
}
















void qh_makenewplanes(qhT *qh  ) {
  facetT *newfacet;

  FORALLnew_facets {
    if (!newfacet->mergehorizon)
      qh_setfacetplane(qh, newfacet);
  }
  if (qh->JOGGLEmax < REALmax/2)
    minimize_(qh->min_vertex, -wwval_(Wnewvertexmax));
}







































#ifndef qh_NOmerge
facetT *qh_makenew_nonsimplicial(qhT *qh, facetT *visible, vertexT *apex, int *numnew) {
  void **freelistp;
  ridgeT *ridge, **ridgep;
  facetT *neighbor, *newfacet= NULL, *samecycle;
  setT *vertices;
  boolT toporient;
  int ridgeid;

  FOREACHridge_(visible->ridges) {
    ridgeid= ridge->id;
    neighbor= otherfacet_(ridge, visible);
    if (neighbor->visible) {
      if (!qh->ONLYgood) {
        if (neighbor->visitid == qh->visit_id) {
          qh_setfree(qh, &(ridge->vertices));
          qh_memfree_(qh, ridge, (int)sizeof(ridgeT), freelistp);
        }
      }
    }else {
      toporient= (ridge->top == visible);
      vertices= qh_setnew(qh, qh->hull_dim);
      qh_setappend(qh, &vertices, apex);
      qh_setappend_set(qh, &vertices, ridge->vertices);
      newfacet= qh_makenewfacet(qh, vertices, toporient, neighbor);
      (*numnew)++;
      if (neighbor->coplanar) {
        newfacet->mergehorizon= True;
        if (!neighbor->seen) {
          newfacet->f.samecycle= newfacet;
          neighbor->f.newcycle= newfacet;
        }else {
          samecycle= neighbor->f.newcycle;
          newfacet->f.samecycle= samecycle->f.samecycle;
          samecycle->f.samecycle= newfacet;
        }
      }
      if (qh->ONLYgood) {
        if (!neighbor->simplicial)
          qh_setappend(qh, &(newfacet->ridges), ridge);
      }else {
        if (neighbor->seen) {
          if (neighbor->simplicial) {
            qh_fprintf(qh, qh->ferr, 6105, "qhull internal error (qh_makenew_nonsimplicial): simplicial f%d sharing two ridges with f%d\n",
                   neighbor->id, visible->id);
            qh_errexit2(qh, qh_ERRqhull, neighbor, visible);
          }
          qh_setappend(qh, &(neighbor->neighbors), newfacet);
        }else
          qh_setreplace(qh, neighbor->neighbors, visible, newfacet);
        if (neighbor->simplicial) {
          qh_setdel(neighbor->ridges, ridge);
          qh_setfree(qh, &(ridge->vertices));
          qh_memfree(qh, ridge, (int)sizeof(ridgeT));
        }else {
          qh_setappend(qh, &(newfacet->ridges), ridge);
          if (toporient)
            ridge->top= newfacet;
          else
            ridge->bottom= newfacet;
        }
      trace4((qh, qh->ferr, 4048, "qh_makenew_nonsimplicial: created facet f%d from v%d and r%d of horizon f%d\n",
            newfacet->id, apex->id, ridgeid, neighbor->id));
      }
    }
    neighbor->seen= True;
  }
  if (!qh->ONLYgood)
    SETfirst_(visible->ridges)= NULL;
  return newfacet;
}
#else
facetT *qh_makenew_nonsimplicial(qhT *qh, facetT *visible, vertexT *apex, int *numnew) {
  return NULL;
}
#endif






















facetT *qh_makenew_simplicial(qhT *qh, facetT *visible, vertexT *apex, int *numnew) {
  facetT *neighbor, **neighborp, *newfacet= NULL;
  setT *vertices;
  boolT flip, toporient;
  int horizonskip= 0, visibleskip= 0;

  FOREACHneighbor_(visible) {
    if (!neighbor->seen && !neighbor->visible) {
      vertices= qh_facetintersect(qh, neighbor,visible, &horizonskip, &visibleskip, 1);
      SETfirst_(vertices)= apex;
      flip= ((horizonskip & 0x1) ^ (visibleskip & 0x1));
      if (neighbor->toporient)
        toporient= horizonskip & 0x1;
      else
        toporient= (horizonskip & 0x1) ^ 0x1;
      newfacet= qh_makenewfacet(qh, vertices, toporient, neighbor);
      (*numnew)++;
      if (neighbor->coplanar && (qh->PREmerge || qh->MERGEexact)) {
#ifndef qh_NOmerge
        newfacet->f.samecycle= newfacet;
        newfacet->mergehorizon= True;
#endif
      }
      if (!qh->ONLYgood)
        SETelem_(neighbor->neighbors, horizonskip)= newfacet;
      trace4((qh, qh->ferr, 4049, "qh_makenew_simplicial: create facet f%d top %d from v%d and horizon f%d skip %d top %d and visible f%d skip %d, flip? %d\n",
            newfacet->id, toporient, apex->id, neighbor->id, horizonskip,
              neighbor->toporient, visible->id, visibleskip, flip));
    }
  }
  return newfacet;
}
































void qh_matchneighbor(qhT *qh, facetT *newfacet, int newskip, int hashsize, int *hashcount) {
  boolT newfound= False;
  boolT same, ismatch;
  int hash, scan;
  facetT *facet, *matchfacet;
  int skip, matchskip;

  hash= qh_gethash(qh, hashsize, newfacet->vertices, qh->hull_dim, 1,
                     SETelem_(newfacet->vertices, newskip));
  trace4((qh, qh->ferr, 4050, "qh_matchneighbor: newfacet f%d skip %d hash %d hashcount %d\n",
          newfacet->id, newskip, hash, *hashcount));
  zinc_(Zhashlookup);
  for (scan= hash; (facet= SETelemt_(qh->hash_table, scan, facetT));
       scan= (++scan >= hashsize ? 0 : scan)) {
    if (facet == newfacet) {
      newfound= True;
      continue;
    }
    zinc_(Zhashtests);
    if (qh_matchvertices(qh, 1, newfacet->vertices, newskip, facet->vertices, &skip, &same)) {
      if (SETelem_(newfacet->vertices, newskip) ==
          SETelem_(facet->vertices, skip)) {
        qh_precision(qh, "two facets with the same vertices");
        qh_fprintf(qh, qh->ferr, 6106, "qhull precision error: Vertex sets are the same for f%d and f%d.  Can not force output.\n",
          facet->id, newfacet->id);
        qh_errexit2(qh, qh_ERRprec, facet, newfacet);
      }
      ismatch= (same == (boolT)((newfacet->toporient ^ facet->toporient)));
      matchfacet= SETelemt_(facet->neighbors, skip, facetT);
      if (ismatch && !matchfacet) {
        SETelem_(facet->neighbors, skip)= newfacet;
        SETelem_(newfacet->neighbors, newskip)= facet;
        (*hashcount)--;
        trace4((qh, qh->ferr, 4051, "qh_matchneighbor: f%d skip %d matched with new f%d skip %d\n",
           facet->id, skip, newfacet->id, newskip));
        return;
      }
      if (!qh->PREmerge && !qh->MERGEexact) {
        qh_precision(qh, "a ridge with more than two neighbors");
        qh_fprintf(qh, qh->ferr, 6107, "qhull precision error: facets f%d, f%d and f%d meet at a ridge with more than 2 neighbors.  Can not continue.\n",
                 facet->id, newfacet->id, getid_(matchfacet));
        qh_errexit2(qh, qh_ERRprec, facet, newfacet);
      }
      SETelem_(newfacet->neighbors, newskip)= qh_DUPLICATEridge;
      newfacet->dupridge= True;
      if (!newfacet->normal)
        qh_setfacetplane(qh, newfacet);
      qh_addhash(newfacet, qh->hash_table, hashsize, hash);
      (*hashcount)++;
      if (!facet->normal)
        qh_setfacetplane(qh, facet);
      if (matchfacet != qh_DUPLICATEridge) {
        SETelem_(facet->neighbors, skip)= qh_DUPLICATEridge;
        facet->dupridge= True;
        if (!facet->normal)
          qh_setfacetplane(qh, facet);
        if (matchfacet) {
          matchskip= qh_setindex(matchfacet->neighbors, facet);
          if (matchskip<0) {
              qh_fprintf(qh, qh->ferr, 6260, "qhull internal error (qh_matchneighbor): matchfacet f%d is in f%d neighbors but not vice versa.  Can not continue.\n",
                  matchfacet->id, facet->id);
              qh_errexit2(qh, qh_ERRqhull, matchfacet, facet);
          }
          SETelem_(matchfacet->neighbors, matchskip)= qh_DUPLICATEridge;
          matchfacet->dupridge= True;
          if (!matchfacet->normal)
            qh_setfacetplane(qh, matchfacet);
          qh_addhash(matchfacet, qh->hash_table, hashsize, hash);
          *hashcount += 2;
        }
      }
      trace4((qh, qh->ferr, 4052, "qh_matchneighbor: new f%d skip %d duplicates ridge for f%d skip %d matching f%d ismatch %d at hash %d\n",
           newfacet->id, newskip, facet->id, skip,
           (matchfacet == qh_DUPLICATEridge ? -2 : getid_(matchfacet)),
           ismatch, hash));
      return;
    }
  }
  if (!newfound)
    SETelem_(qh->hash_table, scan)= newfacet;
  (*hashcount)++;
  trace4((qh, qh->ferr, 4053, "qh_matchneighbor: no match for f%d skip %d at hash %d\n",
           newfacet->id, newskip, hash));
}

































void qh_matchnewfacets(qhT *qh  ) {
  int numnew=0, hashcount=0, newskip;
  facetT *newfacet, *neighbor;
  int dim= qh->hull_dim, hashsize, neighbor_i, neighbor_n;
  setT *neighbors;
#ifndef qh_NOtrace
  int facet_i, facet_n, numfree= 0;
  facetT *facet;
#endif

  trace1((qh, qh->ferr, 1019, "qh_matchnewfacets: match neighbors for new facets.\n"));
  FORALLnew_facets {
    numnew++;
    {
      neighbors= newfacet->neighbors;
      neighbors->e[neighbors->maxsize].i= dim+1;
      memset((char *)SETelemaddr_(neighbors, 1, void), 0, dim * SETelemsize);
    }
  }

  qh_newhashtable(qh, numnew*(qh->hull_dim-1));

  hashsize= qh_setsize(qh, qh->hash_table);
  FORALLnew_facets {
    for (newskip=1; newskip<qh->hull_dim; newskip++)

      qh_matchneighbor(qh, newfacet, newskip, hashsize, &hashcount);
#if 0
    {
      int count= 0, k;
      facetT *facet, *neighbor;

      count= 0;
      FORALLfacet_(qh->newfacet_list) {
        for (k=1; k < qh->hull_dim; k++) {
          neighbor= SETelemt_(facet->neighbors, k, facetT);
          if (!neighbor || neighbor == qh_DUPLICATEridge)
            count++;
        }
        if (facet == newfacet)
          break;
      }
      if (count != hashcount) {
        qh_fprintf(qh, qh->ferr, 8088, "qh_matchnewfacets: after adding facet %d, hashcount %d != count %d\n",
                 newfacet->id, hashcount, count);
        qh_errexit(qh, qh_ERRqhull, newfacet, NULL);
      }
    }
#endif
  }
  if (hashcount) {
    FORALLnew_facets {
      if (newfacet->dupridge) {
        FOREACHneighbor_i_(qh, newfacet) {
          if (neighbor == qh_DUPLICATEridge) {
            qh_matchduplicates(qh, newfacet, neighbor_i, hashsize, &hashcount);

          }
        }
      }
    }
  }
  if (hashcount) {
    qh_fprintf(qh, qh->ferr, 6108, "qhull internal error (qh_matchnewfacets): %d neighbors did not match up\n",
        hashcount);
    qh_printhashtable(qh, qh->ferr);
    qh_errexit(qh, qh_ERRqhull, NULL, NULL);
  }
#ifndef qh_NOtrace
  if (qh->IStracing >= 2) {
    FOREACHfacet_i_(qh, qh->hash_table) {
      if (!facet)
        numfree++;
    }
    qh_fprintf(qh, qh->ferr, 8089, "qh_matchnewfacets: %d new facets, %d unused hash entries .  hashsize %d\n",
             numnew, numfree, qh_setsize(qh, qh->hash_table));
  }
#endif
  qh_setfree(qh, &qh->hash_table);
  if (qh->PREmerge || qh->MERGEexact) {
    if (qh->IStracing >= 4)
      qh_printfacetlist(qh, qh->newfacet_list, NULL, qh_ALL);
    FORALLnew_facets {
      if (newfacet->normal)
        qh_checkflipped(qh, newfacet, NULL, qh_ALL);
    }
  }else if (qh->FORCEoutput)
    qh_checkflipped_all(qh, qh->newfacet_list);
}






















boolT qh_matchvertices(qhT *qh, int firstindex, setT *verticesA, int skipA,
       setT *verticesB, int *skipB, boolT *same) {
  vertexT **elemAp, **elemBp, **skipBp=NULL, **skipAp;

  elemAp= SETelemaddr_(verticesA, firstindex, vertexT);
  elemBp= SETelemaddr_(verticesB, firstindex, vertexT);
  skipAp= SETelemaddr_(verticesA, skipA, vertexT);
  do if (elemAp != skipAp) {
    while (*elemAp != *elemBp++) {
      if (skipBp)
        return False;
      skipBp= elemBp;
    }
  }while (*(++elemAp));
  if (!skipBp)
    skipBp= ++elemBp;
  *skipB= SETindex_(verticesB, skipB);
  *same= !((skipA & 0x1) ^ (*skipB & 0x1));
  trace4((qh, qh->ferr, 4054, "qh_matchvertices: matched by skip %d(v%d) and skip %d(v%d) same? %d\n",
          skipA, (*skipAp)->id, *skipB, (*(skipBp-1))->id, *same));
  return(True);
}











facetT *qh_newfacet(qhT *qh) {
  facetT *facet;
  void **freelistp;

  qh_memalloc_(qh, (int)sizeof(facetT), freelistp, facet, facetT);
  memset((char *)facet, (size_t)0, sizeof(facetT));
  if (qh->facet_id == qh->tracefacet_id)
    qh->tracefacet= facet;
  facet->id= qh->facet_id++;
  facet->neighbors= qh_setnew(qh, qh->hull_dim);
#if !qh_COMPUTEfurthest
  facet->furthestdist= 0.0;
#endif
#if qh_MAXoutside
  if (qh->FORCEoutput && qh->APPROXhull)
    facet->maxoutside= qh->MINoutside;
  else
    facet->maxoutside= qh->DISTround;
#endif
  facet->simplicial= True;
  facet->good= True;
  facet->newfacet= True;
  trace4((qh, qh->ferr, 4055, "qh_newfacet: created facet f%d\n", facet->id));
  return(facet);
}








ridgeT *qh_newridge(qhT *qh) {
  ridgeT *ridge;
  void **freelistp;

  qh_memalloc_(qh, (int)sizeof(ridgeT), freelistp, ridge, ridgeT);
  memset((char *)ridge, (size_t)0, sizeof(ridgeT));
  zinc_(Ztotridges);
  if (qh->ridge_id == UINT_MAX) {
    qh_fprintf(qh, qh->ferr, 7074, "\
qhull warning: more than 2^32 ridges.  Qhull results are OK.  Since the ridge ID wraps around to 0, two ridges may have the same identifier.\n");
  }
  ridge->id= qh->ridge_id++;
  trace4((qh, qh->ferr, 4056, "qh_newridge: created ridge r%d\n", ridge->id));
  return(ridge);
}




















int qh_pointid(qhT *qh, pointT *point) {
  ptr_intT offset, id;

  if (!point || !qh)
    return qh_IDnone;
  else if (point == qh->interior_point)
    return qh_IDinterior;
  else if (point >= qh->first_point
  && point < qh->first_point + qh->num_points * qh->hull_dim) {
    offset= (ptr_intT)(point - qh->first_point);
    id= offset / qh->hull_dim;
  }else if ((id= qh_setindex(qh->other_points, point)) != -1)
    id += qh->num_points;
  else
    return qh_IDunknown;
  return (int)id;
}














void qh_removefacet(qhT *qh, facetT *facet) {
  facetT *next= facet->next, *previous= facet->previous;

  if (facet == qh->newfacet_list)
    qh->newfacet_list= next;
  if (facet == qh->facet_next)
    qh->facet_next= next;
  if (facet == qh->visible_list)
    qh->visible_list= next;
  if (previous) {
    previous->next= next;
    next->previous= previous;
  }else {
    qh->facet_list= next;
    qh->facet_list->previous= NULL;
  }
  qh->num_facets--;
  trace4((qh, qh->ferr, 4057, "qh_removefacet: remove f%d from facet_list\n", facet->id));
}












void qh_removevertex(qhT *qh, vertexT *vertex) {
  vertexT *next= vertex->next, *previous= vertex->previous;

  if (vertex == qh->newvertex_list)
    qh->newvertex_list= next;
  if (previous) {
    previous->next= next;
    next->previous= previous;
  }else {
    qh->vertex_list= vertex->next;
    qh->vertex_list->previous= NULL;
  }
  qh->num_vertices--;
  trace4((qh, qh->ferr, 4058, "qh_removevertex: remove v%d from vertex_list\n", vertex->id));
}

























void qh_updatevertices(qhT *qh  ) {
  facetT *newfacet= NULL, *neighbor, **neighborp, *visible;
  vertexT *vertex, **vertexp;

  trace3((qh, qh->ferr, 3013, "qh_updatevertices: delete interior vertices and update vertex->neighbors\n"));
  if (qh->VERTEXneighbors) {
    FORALLvertex_(qh->newvertex_list) {
      FOREACHneighbor_(vertex) {
        if (neighbor->visible)
          SETref_(neighbor)= NULL;
      }
      qh_setcompact(qh, vertex->neighbors);
    }
    FORALLnew_facets {
      FOREACHvertex_(newfacet->vertices)
        qh_setappend(qh, &vertex->neighbors, newfacet);
    }
    FORALLvisible_facets {
      FOREACHvertex_(visible->vertices) {
        if (!vertex->newlist && !vertex->deleted) {
          FOREACHneighbor_(vertex) {
            if (!neighbor->visible)
              break;
          }
          if (neighbor)
            qh_setdel(vertex->neighbors, visible);
          else {
            vertex->deleted= True;
            qh_setappend(qh, &qh->del_vertices, vertex);
            trace2((qh, qh->ferr, 2041, "qh_updatevertices: delete vertex p%d(v%d) in f%d\n",
                  qh_pointid(qh, vertex->point), vertex->id, visible->id));
          }
        }
      }
    }
  }else {
    FORALLvisible_facets {
      FOREACHvertex_(visible->vertices) {
        if (!vertex->newlist && !vertex->deleted) {
          vertex->deleted= True;
          qh_setappend(qh, &qh->del_vertices, vertex);
          trace2((qh, qh->ferr, 2042, "qh_updatevertices: delete vertex p%d(v%d) in f%d\n",
                  qh_pointid(qh, vertex->point), vertex->id, visible->id));
        }
      }
    }
  }
}
