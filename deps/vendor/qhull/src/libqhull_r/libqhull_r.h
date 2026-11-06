/*<html><pre>  -<a                             href="qh-qhull_r.htm"
  >-------------------------------</a><a name="TOP">-</a>

   libqhull_r.h
   user-level header file for using qhull.a library

   see qh-qhull_r.htm, qhull_ra.h

   Copyright (c) 1993-2015 The Geometry Center.
   $Id: //main/2015/qhull/src/libqhull_r/libqhull_r.h#8 $$Change: 2079 $
   $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $

   includes function prototypes for libqhull_r.c, geom_r.c, global_r.c, io_r.c, user.c

   use mem_r.h for mem_r.c
   use qset_r.h for qset_r.c

   see unix_r.c for an example of using libqhull_r.h

   recompile qhull if you change this file
*/

#ifndef qhDEFlibqhull
#define qhDEFlibqhull 1




#include "user_r.h"

#include "mem_r.h"
#include "qset_r.h"


#include <setjmp.h>
#include <float.h>
#include <time.h>
#include <stdio.h>

#ifndef __STDC__
#ifndef __cplusplus
#if     !_MSC_VER
#error  Neither __STDC__ nor __cplusplus is defined.  Please use strict ANSI C or C++ to compile
#error  Qhull.  You may need to turn off compiler extensions in your project configuration.  If
#error  your compiler is a standard C compiler, you can delete this warning from libqhull_r.h
#endif
#endif
#endif



extern const char qh_version[];
extern const char qh_version2[];














#define coordT realT















#define pointT coordT
typedef enum
{
    qh_IDnone = -3, qh_IDinterior = -2, qh_IDunknown = -1
}
qh_pointT;







#define flagT unsigned int











#define boolT unsigned int
#ifdef False
#undef False
#endif
#ifdef True
#undef True
#endif
#define False 0
#define True 1
#define qh_False 0
#define qh_True 1

#include "stat_r.h"







typedef enum
{
    qh_ASnone = 0,
    qh_ASvoronoi,
    qh_AScentrum
}
qh_CENTER;













typedef enum {qh_PRINTnone= 0,
  qh_PRINTarea, qh_PRINTaverage,
  qh_PRINTcoplanars, qh_PRINTcentrums,
  qh_PRINTfacets, qh_PRINTfacets_xridge,
  qh_PRINTgeom, qh_PRINTids, qh_PRINTinner, qh_PRINTneighbors,
  qh_PRINTnormals, qh_PRINTouter, qh_PRINTmaple,
  qh_PRINTincidences, qh_PRINTmathematica, qh_PRINTmerges, qh_PRINToff,
  qh_PRINToptions, qh_PRINTpointintersect,
  qh_PRINTpointnearest, qh_PRINTpoints, qh_PRINTqhull, qh_PRINTsize,
  qh_PRINTsummary, qh_PRINTtriangles,
  qh_PRINTvertices, qh_PRINTvneighbors, qh_PRINTextremes,
  qh_PRINTEND} qh_PRINT;







#define qh_ALL      True
#define qh_NOupper  True
#define qh_IScheckmax  True
#define qh_ISnewfacets  True
#define qh_RESETvisible  True








#define qh_ERRnone  0
#define qh_ERRinput 1
#define qh_ERRsingular 2
#define qh_ERRprec  3
#define qh_ERRmem   4
#define qh_ERRqhull 5








#define qh_FILEstderr ((FILE*)1)












typedef struct vertexT vertexT;
typedef struct ridgeT ridgeT;
typedef struct facetT facetT;

#ifndef DEFqhT
#define DEFqhT 1
typedef struct qhT qhT;
#endif

#ifndef DEFsetT
#define DEFsetT 1
typedef struct setT setT;
#endif





































struct facetT {
#if !qh_COMPUTEfurthest
  coordT   furthestdist;
#endif
#if qh_MAXoutside
  coordT   maxoutside;




#endif
  coordT   offset;
  coordT  *normal;

  union {
   realT   area;
   facetT *replace;

   facetT *samecycle;

   facetT *newcycle;
   facetT *trivisible;
   facetT *triowner;
  }f;
  coordT  *center;






  facetT  *previous;
  facetT  *next;
  setT    *vertices;

  setT    *ridges;

  setT    *neighbors;


  setT    *outsideset;


  setT    *coplanarset;



  unsigned visitid;

  unsigned id;
  unsigned nummerge:9;
#define qh_MAXnummerge 511
  flagT    tricoplanar:1;





  flagT    newfacet:1;
  flagT    visible:1;
  flagT    toporient:1;

  flagT    simplicial:1;
  flagT    seen:1;
  flagT    seen2:1;
  flagT    flipped:1;
  flagT    upperdelaunay:1;
  flagT    notfurthest:1;


  flagT    good:1;
  flagT    isarea:1;


  flagT    dupridge:1;
  flagT    mergeridge:1;

  flagT    mergeridge2:1;
  flagT    coplanar:1;
  flagT     mergehorizon:1;
  flagT     cycledone:1;
  flagT    tested:1;
  flagT    keepcentrum:1;
  flagT    newmerge:1;
  flagT    degenerate:1;
  flagT    redundant:1;
};






















struct ridgeT {
  setT    *vertices;

  facetT  *top;
  facetT  *bottom;
  unsigned id;
  flagT    seen:1;
  flagT    tested:1;
  flagT    nonconvex:1;

};














struct vertexT {
  vertexT *next;
  vertexT *previous;
  pointT  *point;
  setT    *neighbors;

  unsigned id;
  unsigned visitid;
  flagT    seen:1;
  flagT    seen2:1;
  flagT    delridge:1;
  flagT    deleted:1;
  flagT    newlist:1;
};

















#define QHULL_NON_REENTRANT 0
#define QHULL_QH_POINTER 1
#define QHULL_REENTRANT 2

#define QHULL_LIB_TYPE QHULL_REENTRANT

#define QHULL_LIB_CHECK qh_lib_check(QHULL_LIB_TYPE, sizeof(qhT), sizeof(vertexT), sizeof(ridgeT), sizeof(facetT), sizeof(setT), sizeof(qhmemT));
#define QHULL_LIB_CHECK_RBOX qh_lib_check(QHULL_LIB_TYPE, sizeof(qhT), sizeof(vertexT), sizeof(ridgeT), sizeof(facetT), 0, 0);

struct qhT {











  boolT ALLpoints;
  boolT ANGLEmerge;
  boolT APPROXhull;
  realT   MINoutside;
  boolT ANNOTATEoutput;
  boolT ATinfinity;

  boolT AVOIDold;
  boolT BESToutside;
  boolT CDDinput;
  boolT CDDoutput;
  boolT CHECKfrequently;
  realT premerge_cos;
  realT postmerge_cos;
  boolT DELAUNAY;
  boolT DOintersections;
  int   DROPdim;
  boolT FORCEoutput;
  int   GOODpoint;
  pointT *GOODpointp;
  boolT GOODthreshold;

  int   GOODvertex;
  pointT *GOODvertexp;
  boolT HALFspace;
  boolT ISqhullQh;
  int   IStracing;
  int   KEEParea;
  boolT KEEPcoplanar;
  boolT KEEPinside;

  int   KEEPmerge;
  realT KEEPminArea;
  realT MAXcoplanar;
  boolT MERGEexact;
  boolT MERGEindependent;
  boolT MERGING;
  realT   premerge_centrum;
  realT   postmerge_centrum;
  boolT MERGEvertices;
  realT MINvisible;
  boolT NOnarrow;
  boolT NOnearinside;
  boolT NOpremerge;
  boolT NOwide;
  boolT ONLYgood;
  boolT ONLYmax;
  boolT PICKfurthest;
  boolT POSTmerge;
  boolT PREmerge;

  boolT PRINTcentrums;
  boolT PRINTcoplanar;
  int   PRINTdim;
  boolT PRINTdots;
  boolT PRINTgood;
  boolT PRINTinner;
  boolT PRINTneighbors;
  boolT PRINTnoplanes;
  boolT PRINToptions1st;
  boolT PRINTouter;
  boolT PRINTprecision;
  qh_PRINT PRINTout[qh_PRINTEND];
  boolT PRINTridges;
  boolT PRINTspheres;
  boolT PRINTstatistics;
  boolT PRINTsummary;
  boolT PRINTtransparent;
  boolT PROJECTdelaunay;

  int   PROJECTinput;
  boolT QUICKhelp;
  boolT RANDOMdist;
  realT RANDOMfactor;
  realT RANDOMa;
  realT RANDOMb;
  boolT RANDOMoutside;
  int   REPORTfreq;
  int   REPORTfreq2;
  int   RERUN;
  int   ROTATErandom;
  boolT SCALEinput;
  boolT SCALElast;
  boolT SETroundoff;
  boolT SKIPcheckmax;
  boolT SKIPconvex;
  boolT SPLITthresholds;

  int   STOPcone;

  int   STOPpoint;

  int   TESTpoints;
  boolT TESTvneighbors;
  int   TRACElevel;
  int   TRACElastrun;
  int   TRACEpoint;
  realT TRACEdist;
  int   TRACEmerge;
  boolT TRIangulate;
  boolT TRInormals;
  boolT UPPERdelaunay;
  boolT USEstdout;
  boolT VERIFYoutput;
  boolT VIRTUALmemory;
  boolT VORONOI;


  realT AREAfactor;
  boolT DOcheckmax;
  char  *feasible_string;
  coordT *feasible_point;
  boolT GETarea;
  boolT KEEPnearinside;
  int   hull_dim;
  int   input_dim;
  int   num_points;
  pointT *first_point;
  boolT POINTSmalloc;
  pointT *input_points;
  boolT input_malloc;
  char  qhull_command[256];
  int   qhull_commandsiz2;
  char  rbox_command[256];
  char  qhull_options[512];
  int   qhull_optionlen;
  int   qhull_optionsiz;
  int   qhull_optionsiz2;
  int   run_id;
  boolT VERTEXneighbors;
  boolT ZEROcentrum;
  realT *upper_threshold;


  realT *lower_threshold;
  realT *upper_bound;
  realT *lower_bound;













  realT ANGLEround;
  realT centrum_radius;
  realT cos_max;
  realT DISTround;
  realT MAXabs_coord;
  realT MAXlastcoord;
  realT MAXsumcoord;
  realT MAXwidth;
  realT MINdenom_1;
  realT MINdenom;
  realT MINdenom_1_2;
  realT MINdenom_2;
  realT MINlastcoord;
  boolT NARROWhull;
  realT *NEARzero;
  realT NEARinside;
  realT ONEmerge;
  realT outside_err;

  realT WIDEfacet;








  char qhull[sizeof("qhull")];
  jmp_buf errexit;
  char jmpXtra[40];
  jmp_buf restartexit;
  char jmpXtra2[40];
  FILE *fin;
  FILE *fout;
  FILE *ferr;
  pointT *interior_point;
  int normal_size;
  int center_size;
  int   TEMPsize;










  facetT *facet_list;
  facetT  *facet_tail;
  facetT *facet_next;


  facetT *newfacet_list;
  facetT *visible_list;

  int       num_visible;
  unsigned tracefacet_id;
  facetT *tracefacet;
  unsigned tracevertex_id;
  vertexT *tracevertex;
  vertexT *vertex_list;
  vertexT  *vertex_tail;
  vertexT *newvertex_list;

  int   num_facets;

  int   num_vertices;
  int   num_outside;

  int   num_good;
  unsigned facet_id;
  unsigned ridge_id;
  unsigned vertex_id;









  unsigned long hulltime;

  boolT ALLOWrestart;
  int   build_cnt;
  qh_CENTER CENTERtype;
  int   furthest_id;
  facetT *GOODclosest;
  boolT hasAreaVolume;
  boolT hasTriangulation;
  realT JOGGLEmax;
  boolT maxoutdone;
  realT max_outside;



  realT max_vertex;

  realT min_vertex;



  boolT NEWfacets;

  boolT findbestnew;
  boolT findbest_notsharp;
  boolT NOerrexit;
  realT PRINTcradius;
  realT PRINTradius;
  boolT POSTmerging;
  int   printoutvar;
  int   printoutnum;
  boolT QHULLfinished;
  realT totarea;
  realT totvol;
  unsigned int visit_id;
  unsigned int vertex_visit;
  boolT ZEROall_ok;
  boolT WAScoplanar;








  setT *facet_mergeset;
  setT *degen_mergeset;
  setT *hash_table;

  setT *other_points;
  setT *del_vertices;








  coordT *gm_matrix;
  coordT **gm_row;
  char* line;
  int maxline;
  coordT *half_space;
  coordT *temp_malloc;













  boolT ERREXITcalled;
  boolT firstcentrum;
  boolT old_randomdist;
  setT *coplanarfacetset;
  realT last_low;
  realT last_high;
  realT last_newhigh;
  unsigned lastreport;
  int mergereport;
  setT *old_tempstack;
  int   ridgeoutnum;








  int     last_random;
  jmp_buf rbox_errexit;
  char    jmpXtra3[40];
  int     rbox_isinteger;
  double  rbox_out_offset;
  void *  cpp_object;


  qhmemT  qhmem;

  qhstatT qhstat;
};









#define otherfacet_(ridge, facet) \
                        (((ridge)->top == (facet)) ? (ridge)->bottom : (ridge)->top)








#define getid_(p)       ((p) ? (int)((p)->id) : qh_IDunknown)

















#define FORALLfacets for (facet=qh->facet_list;facet && facet->next;facet=facet->next)













#define FORALLpoints FORALLpoint_(qh, qh->first_point, qh->num_points)










#define FORALLpoint_(qh, points, num) for (point= (points), \
      pointtemp= (points)+qh->hull_dim*(num); point < pointtemp; point += qh->hull_dim)














#define FORALLvertices for (vertex=qh->vertex_list;vertex && vertex->next;vertex= vertex->next)













#define FOREACHfacet_(facets)    FOREACHsetelement_(facetT, facets, facet)
















#define FOREACHneighbor_(facet)  FOREACHsetelement_(facetT, facet->neighbors, neighbor)













#define FOREACHpoint_(points)    FOREACHsetelement_(pointT, points, point)













#define FOREACHridge_(ridges)    FOREACHsetelement_(ridgeT, ridges, ridge)













#define FOREACHvertex_(vertices) FOREACHsetelement_(vertexT, vertices,vertex)














#define FOREACHfacet_i_(qh, facets)    FOREACHsetelement_i_(qh, facetT, facets, facet)

















#define FOREACHneighbor_i_(qh, facet)  FOREACHsetelement_i_(qh, facetT, facet->neighbors, neighbor)














#define FOREACHpoint_i_(qh, points)    FOREACHsetelement_i_(qh, pointT, points, point)














#define FOREACHridge_i_(qh, ridges)    FOREACHsetelement_i_(qh, ridgeT, ridges, ridge)














#define FOREACHvertex_i_(qh, vertices) FOREACHsetelement_i_(qh, vertexT, vertices,vertex)

#ifdef __cplusplus
extern "C" {
#endif



void    qh_qhull(qhT *qh);
boolT   qh_addpoint(qhT *qh, pointT *furthest, facetT *facet, boolT checkdist);
void    qh_printsummary(qhT *qh, FILE *fp);



void    qh_errexit(qhT *qh, int exitcode, facetT *facet, ridgeT *ridge);
void    qh_errprint(qhT *qh, const char* string, facetT *atfacet, facetT *otherfacet, ridgeT *atridge, vertexT *atvertex);
int     qh_new_qhull(qhT *qh, int dim, int numpoints, coordT *points, boolT ismalloc,
                char *qhull_cmd, FILE *outfile, FILE *errfile);
void    qh_printfacetlist(qhT *qh, facetT *facetlist, setT *facets, boolT printall);
void    qh_printhelp_degenerate(qhT *qh, FILE *fp);
void    qh_printhelp_narrowhull(qhT *qh, FILE *fp, realT minangle);
void    qh_printhelp_singular(qhT *qh, FILE *fp);
void    qh_user_memsizes(qhT *qh);


void    qh_exit(int exitcode);
void    qh_fprintf_stderr(int msgcode, const char *fmt, ... );
void    qh_free(void *mem);
void   *qh_malloc(size_t size);


void    qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... );
void    qh_fprintf_rbox(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... );



facetT *qh_findbest(qhT *qh, pointT *point, facetT *startfacet,
                     boolT bestoutside, boolT newfacets, boolT noupper,
                     realT *dist, boolT *isoutside, int *numpart);
facetT *qh_findbestnew(qhT *qh, pointT *point, facetT *startfacet,
                     realT *dist, boolT bestoutside, boolT *isoutside, int *numpart);
boolT   qh_gram_schmidt(qhT *qh, int dim, realT **rows);
void    qh_outerinner(qhT *qh, facetT *facet, realT *outerplane, realT *innerplane);
void    qh_printsummary(qhT *qh, FILE *fp);
void    qh_projectinput(qhT *qh);
void    qh_randommatrix(qhT *qh, realT *buffer, int dim, realT **row);
void    qh_rotateinput(qhT *qh, realT **rows);
void    qh_scaleinput(qhT *qh);
void    qh_setdelaunay(qhT *qh, int dim, int count, pointT *points);
coordT  *qh_sethalfspace_all(qhT *qh, int dim, int count, coordT *halfspaces, pointT *feasible);



unsigned long qh_clock(qhT *qh);
void    qh_checkflags(qhT *qh, char *command, char *hiddenflags);
void    qh_clear_outputflags(qhT *qh);
void    qh_freebuffers(qhT *qh);
void    qh_freeqhull(qhT *qh, boolT allmem);
void    qh_init_A(qhT *qh, FILE *infile, FILE *outfile, FILE *errfile, int argc, char *argv[]);
void    qh_init_B(qhT *qh, coordT *points, int numpoints, int dim, boolT ismalloc);
void    qh_init_qhull_command(qhT *qh, int argc, char *argv[]);
void    qh_initbuffers(qhT *qh, coordT *points, int numpoints, int dim, boolT ismalloc);
void    qh_initflags(qhT *qh, char *command);
void    qh_initqhull_buffers(qhT *qh);
void    qh_initqhull_globals(qhT *qh, coordT *points, int numpoints, int dim, boolT ismalloc);
void    qh_initqhull_mem(qhT *qh);
void    qh_initqhull_outputflags(qhT *qh);
void    qh_initqhull_start(qhT *qh, FILE *infile, FILE *outfile, FILE *errfile);
void    qh_initqhull_start2(qhT *qh, FILE *infile, FILE *outfile, FILE *errfile);
void    qh_initthresholds(qhT *qh, char *command);
void    qh_lib_check(int qhullLibraryType, int qhTsize, int vertexTsize, int ridgeTsize, int facetTsize, int setTsize, int qhmemTsize);
void    qh_option(qhT *qh, const char *option, int *i, realT *r);
void    qh_zero(qhT *qh, FILE *errfile);



void    qh_dfacet(qhT *qh, unsigned id);
void    qh_dvertex(qhT *qh, unsigned id);
void    qh_printneighborhood(qhT *qh, FILE *fp, qh_PRINT format, facetT *facetA, facetT *facetB, boolT printall);
void    qh_produce_output(qhT *qh);
coordT *qh_readpoints(qhT *qh, int *numpoints, int *dimension, boolT *ismalloc);




void qh_meminit(qhT *qh, FILE *ferr);
void qh_memfreeshort(qhT *qh, int *curlong, int *totlong);



void    qh_check_output(qhT *qh);
void    qh_check_points(qhT *qh);
setT   *qh_facetvertices(qhT *qh, facetT *facetlist, setT *facets, boolT allfacets);
facetT *qh_findbestfacet(qhT *qh, pointT *point, boolT bestoutside,
           realT *bestdist, boolT *isoutside);
vertexT *qh_nearvertex(qhT *qh, facetT *facet, pointT *point, realT *bestdistp);
pointT *qh_point(qhT *qh, int id);
setT   *qh_pointfacet(qhT *qh  );
int     qh_pointid(qhT *qh, pointT *point);
setT   *qh_pointvertex(qhT *qh  );
void    qh_setvoronoi_all(qhT *qh);
void    qh_triangulate(qhT *qh  );


int     qh_rboxpoints(qhT *qh, char* rbox_command);
void    qh_errexit_rbox(qhT *qh, int exitcode);



void    qh_collectstatistics(qhT *qh);
void    qh_printallstatistics(qhT *qh, FILE *fp, const char *string);

#ifdef __cplusplus
}
#endif

#endif
