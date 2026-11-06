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

#ifndef __mesh_h_
#define __mesh_h_

#include "glu-libtess.h"

typedef struct GLUmesh GLUmesh;

typedef struct GLUvertex GLUvertex;
typedef struct GLUface GLUface;
typedef struct GLUhalfEdge GLUhalfEdge;

typedef struct ActiveRegion ActiveRegion;



































































struct GLUvertex {
  GLUvertex	*next;
  GLUvertex	*prev;
  GLUhalfEdge	*anEdge;
  void		*data;


  GLdouble	coords[3];
  GLdouble	s, t;
  long		pqHandle;
};

struct GLUface {
  GLUface	*next;
  GLUface	*prev;
  GLUhalfEdge	*anEdge;
  void		*data;


  GLUface	*trail;
  GLboolean	marked;
  GLboolean	inside;
};

struct GLUhalfEdge {
  GLUhalfEdge	*next;
  GLUhalfEdge	*Sym;
  GLUhalfEdge	*Onext;
  GLUhalfEdge	*Lnext;
  GLUvertex	*Org;
  GLUface	*Lface;


  ActiveRegion	*activeRegion;
  int		winding;

};

#define	Rface	Sym->Lface
#define Dst	Sym->Org

#define Oprev	Sym->Lnext
#define Lprev   Onext->Sym
#define Dprev	Lnext->Sym
#define Rprev	Sym->Onext
#define Dnext	Rprev->Sym
#define Rnext	Oprev->Sym


struct GLUmesh {
  GLUvertex	vHead;
  GLUface	fHead;
  GLUhalfEdge	eHead;
  GLUhalfEdge	eHeadSym;
};














































































GLUhalfEdge	*__gl_meshMakeEdge( GLUmesh *mesh );
int		__gl_meshSplice( GLUhalfEdge *eOrg, GLUhalfEdge *eDst );
int		__gl_meshDelete( GLUhalfEdge *eDel );

GLUhalfEdge	*__gl_meshAddEdgeVertex( GLUhalfEdge *eOrg );
GLUhalfEdge	*__gl_meshSplitEdge( GLUhalfEdge *eOrg );
GLUhalfEdge	*__gl_meshConnect( GLUhalfEdge *eOrg, GLUhalfEdge *eDst );

GLUmesh		*__gl_meshNewMesh( void );
GLUmesh		*__gl_meshUnion( GLUmesh *mesh1, GLUmesh *mesh2 );
void		__gl_meshDeleteMesh( GLUmesh *mesh );
void		__gl_meshZapFace( GLUface *fZap );

#ifdef NDEBUG
#define		__gl_meshCheckMesh( mesh )
#else
void		__gl_meshCheckMesh( GLUmesh *mesh );
#endif

#endif
