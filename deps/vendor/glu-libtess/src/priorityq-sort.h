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

#ifndef __priorityq_sort_h_
#define __priorityq_sort_h_

#include "priorityq-heap.h"

#undef PQkey
#undef PQhandle
#undef PriorityQ
#undef pqNewPriorityQ
#undef pqDeletePriorityQ
#undef pqInit
#undef pqInsert
#undef pqMinimum
#undef pqExtractMin
#undef pqDelete
#undef pqIsEmpty



#define PQkey			PQSortKey
#define PQhandle		PQSortHandle
#define PriorityQ		PriorityQSort

#define pqNewPriorityQ(leq)	__gl_pqSortNewPriorityQ(leq)
#define pqDeletePriorityQ(pq)	__gl_pqSortDeletePriorityQ(pq)














#define pqInit(pq)		__gl_pqSortInit(pq)
#define pqInsert(pq,key)	__gl_pqSortInsert(pq,key)
#define pqMinimum(pq)		__gl_pqSortMinimum(pq)
#define pqExtractMin(pq)	__gl_pqSortExtractMin(pq)
#define pqDelete(pq,handle)	__gl_pqSortDelete(pq,handle)
#define pqIsEmpty(pq)		__gl_pqSortIsEmpty(pq)













typedef PQHeapKey PQkey;
typedef PQHeapHandle PQhandle;
typedef struct PriorityQ PriorityQ;

struct PriorityQ {
  PriorityQHeap	*heap;
  PQkey		*keys;
  PQkey		**order;
  PQhandle	size, max;
  int		initialized;
  int		(*leq)(PQkey key1, PQkey key2);
};

PriorityQ	*pqNewPriorityQ( int (*leq)(PQkey key1, PQkey key2) );
void		pqDeletePriorityQ( PriorityQ *pq );

int		pqInit( PriorityQ *pq );
PQhandle	pqInsert( PriorityQ *pq, PQkey key );
PQkey		pqExtractMin( PriorityQ *pq );
void		pqDelete( PriorityQ *pq, PQhandle handle );

PQkey		pqMinimum( PriorityQ *pq );
int		pqIsEmpty( PriorityQ *pq );

#endif
