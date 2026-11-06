/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */
/*
** Author: Eric Veach, July 1994.
**
*/

#include "gluos.h"
#include <assert.h>
#include "mesh.h"
#include "geom.h"

int __gl_vertLeq( GLUvertex *u, GLUvertex *v )
{


  return VertLeq( u, v );
}

GLdouble __gl_edgeEval( GLUvertex *u, GLUvertex *v, GLUvertex *w )
{










  GLdouble gapL, gapR;

  assert( VertLeq( u, v ) && VertLeq( v, w ));

  gapL = v->s - u->s;
  gapR = w->s - v->s;

  if( gapL + gapR > 0 ) {
    if( gapL < gapR ) {
      return (v->t - u->t) + (u->t - w->t) * (gapL / (gapL + gapR));
    } else {
      return (v->t - w->t) + (w->t - u->t) * (gapR / (gapL + gapR));
    }
  }

  return 0;
}

GLdouble __gl_edgeSign( GLUvertex *u, GLUvertex *v, GLUvertex *w )
{




  GLdouble gapL, gapR;

  assert( VertLeq( u, v ) && VertLeq( v, w ));

  gapL = v->s - u->s;
  gapR = w->s - v->s;

  if( gapL + gapR > 0 ) {
    return (v->t - w->t) * gapL + (v->t - u->t) * gapR;
  }

  return 0;
}






GLdouble __gl_transEval( GLUvertex *u, GLUvertex *v, GLUvertex *w )
{










  GLdouble gapL, gapR;

  assert( TransLeq( u, v ) && TransLeq( v, w ));

  gapL = v->t - u->t;
  gapR = w->t - v->t;

  if( gapL + gapR > 0 ) {
    if( gapL < gapR ) {
      return (v->s - u->s) + (u->s - w->s) * (gapL / (gapL + gapR));
    } else {
      return (v->s - w->s) + (w->s - u->s) * (gapR / (gapL + gapR));
    }
  }

  return 0;
}

GLdouble __gl_transSign( GLUvertex *u, GLUvertex *v, GLUvertex *w )
{




  GLdouble gapL, gapR;

  assert( TransLeq( u, v ) && TransLeq( v, w ));

  gapL = v->t - u->t;
  gapR = w->t - v->t;

  if( gapL + gapR > 0 ) {
    return (v->s - w->s) * gapL + (v->s - u->s) * gapR;
  }

  return 0;
}


int __gl_vertCCW( GLUvertex *u, GLUvertex *v, GLUvertex *w )
{






  return (u->s*(v->t - w->t) + v->s*(w->t - u->t) + w->s*(u->t - v->t)) >= 0;
}









#define RealInterpolate(a,x,b,y)			\
  (a = (a < 0) ? 0 : a, b = (b < 0) ? 0 : b,		\
  ((a <= b) ? ((b == 0) ? ((x+y) / 2)			\
                        : (x + (y-x) * (a/(a+b))))	\
            : (y + (x-y) * (b/(a+b)))))

#ifndef FOR_TRITE_TEST_PROGRAM
#define Interpolate(a,x,b,y)	RealInterpolate(a,x,b,y)
#else




#include <stdlib.h>
extern int RandomInterpolate;

GLdouble Interpolate( GLdouble a, GLdouble x, GLdouble b, GLdouble y)
{
printf("*********************%d\n",RandomInterpolate);
  if( RandomInterpolate ) {
    a = 1.2 * drand48() - 0.1;
    a = (a < 0) ? 0 : ((a > 1) ? 1 : a);
    b = 1.0 - a;
  }
  return RealInterpolate(a,x,b,y);
}

#endif

#define Swap(a,b)	do { GLUvertex *t = a; a = b; b = t; } while (0)

void __gl_edgeIntersect( GLUvertex *o1, GLUvertex *d1,
			 GLUvertex *o2, GLUvertex *d2,
			 GLUvertex *v )




{
  GLdouble z1, z2;









  if( ! VertLeq( o1, d1 )) { Swap( o1, d1 ); }
  if( ! VertLeq( o2, d2 )) { Swap( o2, d2 ); }
  if( ! VertLeq( o1, o2 )) { Swap( o1, o2 ); Swap( d1, d2 ); }

  if( ! VertLeq( o2, d1 )) {

    v->s = (o2->s + d1->s) / 2;
  } else if( VertLeq( d1, d2 )) {

    z1 = EdgeEval( o1, o2, d1 );
    z2 = EdgeEval( o2, d1, d2 );
    if( z1+z2 < 0 ) { z1 = -z1; z2 = -z2; }
    v->s = Interpolate( z1, o2->s, z2, d1->s );
  } else {

    z1 = EdgeSign( o1, o2, d1 );
    z2 = -EdgeSign( o1, d2, d1 );
    if( z1+z2 < 0 ) { z1 = -z1; z2 = -z2; }
    v->s = Interpolate( z1, o2->s, z2, d2->s );
  }



  if( ! TransLeq( o1, d1 )) { Swap( o1, d1 ); }
  if( ! TransLeq( o2, d2 )) { Swap( o2, d2 ); }
  if( ! TransLeq( o1, o2 )) { Swap( o1, o2 ); Swap( d1, d2 ); }

  if( ! TransLeq( o2, d1 )) {

    v->t = (o2->t + d1->t) / 2;
  } else if( TransLeq( d1, d2 )) {

    z1 = TransEval( o1, o2, d1 );
    z2 = TransEval( o2, d1, d2 );
    if( z1+z2 < 0 ) { z1 = -z1; z2 = -z2; }
    v->t = Interpolate( z1, o2->t, z2, d1->t );
  } else {

    z1 = TransSign( o1, o2, d1 );
    z2 = -TransSign( o1, d2, d1 );
    if( z1+z2 < 0 ) { z1 = -z1; z2 = -z2; }
    v->t = Interpolate( z1, o2->t, z2, d2->t );
  }
}
