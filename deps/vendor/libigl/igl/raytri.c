/* Ray-Triangle Intersection Test Routines          */
/* Different optimizations of my and Ben Trumbore's */
/* code from journals of graphics tools (JGT)       */
/* http://www.acm.org/jgt/                          */
/* by Tomas Moller, May 2000                        */


// Alec: this file is listed as "Public Domain"
// http://fileadmin.cs.lth.se/cs/Personal/Tomas_Akenine-Moller/code/

// Alec: I've added an include guard, made all functions inline and added
// IGL_RAY_TRI_ to #define macros
#ifndef IGL_RAY_TRI_C
#define IGL_RAY_TRI_C

#include <math.h>

#define IGL_RAY_TRI_EPSILON 0.000001
#define IGL_RAY_TRI_CROSS(dest,v1,v2) \
          dest[0]=v1[1]*v2[2]-v1[2]*v2[1]; \
          dest[1]=v1[2]*v2[0]-v1[0]*v2[2]; \
          dest[2]=v1[0]*v2[1]-v1[1]*v2[0];
#define IGL_RAY_TRI_DOT(v1,v2) (v1[0]*v2[0]+v1[1]*v2[1]+v1[2]*v2[2])
#define IGL_RAY_TRI_SUB(dest,v1,v2) \
          dest[0]=v1[0]-v2[0]; \
          dest[1]=v1[1]-v2[1]; \
          dest[2]=v1[2]-v2[2];


inline int intersect_triangle(double orig[3], double dir[3],
		       double vert0[3], double vert1[3], double vert2[3],
		       double *t, double *u, double *v)
{
   double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];
   double det,inv_det;


   IGL_RAY_TRI_SUB(edge1, vert1, vert0);
   IGL_RAY_TRI_SUB(edge2, vert2, vert0);


   IGL_RAY_TRI_CROSS(pvec, dir, edge2);


   det = IGL_RAY_TRI_DOT(edge1, pvec);

   if (det > -IGL_RAY_TRI_EPSILON && det < IGL_RAY_TRI_EPSILON)
     return 0;
   inv_det = 1.0 / det;


   IGL_RAY_TRI_SUB(tvec, orig, vert0);


   *u = IGL_RAY_TRI_DOT(tvec, pvec) * inv_det;
   if (*u < 0.0 || *u > 1.0)
     return 0;


   IGL_RAY_TRI_CROSS(qvec, tvec, edge1);


   *v = IGL_RAY_TRI_DOT(dir, qvec) * inv_det;
   if (*v < 0.0 || *u + *v > 1.0)
     return 0;


   *t = IGL_RAY_TRI_DOT(edge2, qvec) * inv_det;

   return 1;
}




inline int intersect_triangle1(double orig[3], double dir[3],
			double vert0[3], double vert1[3], double vert2[3],
			double *t, double *u, double *v)
{
   double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];
   double det,inv_det;


   IGL_RAY_TRI_SUB(edge1, vert1, vert0);
   IGL_RAY_TRI_SUB(edge2, vert2, vert0);


   IGL_RAY_TRI_CROSS(pvec, dir, edge2);


   det = IGL_RAY_TRI_DOT(edge1, pvec);

   if (det > IGL_RAY_TRI_EPSILON)
   {

      IGL_RAY_TRI_SUB(tvec, orig, vert0);


      *u = IGL_RAY_TRI_DOT(tvec, pvec);
      if (*u < 0.0 || *u > det)
	 return 0;


      IGL_RAY_TRI_CROSS(qvec, tvec, edge1);


      *v = IGL_RAY_TRI_DOT(dir, qvec);
      if (*v < 0.0 || *u + *v > det)
	 return 0;

   }
   else if(det < -IGL_RAY_TRI_EPSILON)
   {

      IGL_RAY_TRI_SUB(tvec, orig, vert0);


      *u = IGL_RAY_TRI_DOT(tvec, pvec);


      if (*u > 0.0 || *u < det)
	 return 0;


      IGL_RAY_TRI_CROSS(qvec, tvec, edge1);


      *v = IGL_RAY_TRI_DOT(dir, qvec) ;
      if (*v > 0.0 || *u + *v < det)
	 return 0;
   }
   else return 0;


   inv_det = 1.0 / det;


   *t = IGL_RAY_TRI_DOT(edge2, qvec) * inv_det;
   (*u) *= inv_det;
   (*v) *= inv_det;

   return 1;
}



inline int intersect_triangle2(double orig[3], double dir[3],
			double vert0[3], double vert1[3], double vert2[3],
			double *t, double *u, double *v)
{
   double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];
   double det,inv_det;


   IGL_RAY_TRI_SUB(edge1, vert1, vert0);
   IGL_RAY_TRI_SUB(edge2, vert2, vert0);


   IGL_RAY_TRI_CROSS(pvec, dir, edge2);


   det = IGL_RAY_TRI_DOT(edge1, pvec);


   IGL_RAY_TRI_SUB(tvec, orig, vert0);
   inv_det = 1.0 / det;

   if (det > IGL_RAY_TRI_EPSILON)
   {

      *u = IGL_RAY_TRI_DOT(tvec, pvec);
      if (*u < 0.0 || *u > det)
	 return 0;


      IGL_RAY_TRI_CROSS(qvec, tvec, edge1);


      *v = IGL_RAY_TRI_DOT(dir, qvec);
      if (*v < 0.0 || *u + *v > det)
	 return 0;

   }
   else if(det < -IGL_RAY_TRI_EPSILON)
   {

      *u = IGL_RAY_TRI_DOT(tvec, pvec);
      if (*u > 0.0 || *u < det)
	 return 0;


      IGL_RAY_TRI_CROSS(qvec, tvec, edge1);


      *v = IGL_RAY_TRI_DOT(dir, qvec) ;
      if (*v > 0.0 || *u + *v < det)
	 return 0;
   }
   else return 0;


   *t = IGL_RAY_TRI_DOT(edge2, qvec) * inv_det;
   (*u) *= inv_det;
   (*v) *= inv_det;

   return 1;
}




inline int intersect_triangle3(double orig[3], double dir[3],
			double vert0[3], double vert1[3], double vert2[3],
			double *t, double *u, double *v)
{
   double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];
   double det,inv_det;


   IGL_RAY_TRI_SUB(edge1, vert1, vert0);
   IGL_RAY_TRI_SUB(edge2, vert2, vert0);


   IGL_RAY_TRI_CROSS(pvec, dir, edge2);


   det = IGL_RAY_TRI_DOT(edge1, pvec);


   IGL_RAY_TRI_SUB(tvec, orig, vert0);
   inv_det = 1.0 / det;

   IGL_RAY_TRI_CROSS(qvec, tvec, edge1);

   if (det > IGL_RAY_TRI_EPSILON)
   {
      *u = IGL_RAY_TRI_DOT(tvec, pvec);
      if (*u < 0.0 || *u > det)
	 return 0;


      *v = IGL_RAY_TRI_DOT(dir, qvec);
      if (*v < 0.0 || *u + *v > det)
	 return 0;

   }
   else if(det < -IGL_RAY_TRI_EPSILON)
   {

      *u = IGL_RAY_TRI_DOT(tvec, pvec);
      if (*u > 0.0 || *u < det)
	 return 0;


      *v = IGL_RAY_TRI_DOT(dir, qvec) ;
      if (*v > 0.0 || *u + *v < det)
	 return 0;
   }
   else return 0;

   *t = IGL_RAY_TRI_DOT(edge2, qvec) * inv_det;
   (*u) *= inv_det;
   (*v) *= inv_det;

   return 1;
}
#endif
