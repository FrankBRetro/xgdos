/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* raster.h - 1 bit/pixel scanline rasteriser (non-zero winding)
 * Input: SpdSink outline in 16.16 pixels, y up, relative to an origin.
 * Output: rows of a monochrome bitmap (MSB = leftmost pixel).
 * All memory comes from caller supplied buffers (re-entrant).
 */
#ifndef RASTER_H
#define RASTER_H
#include "spd.h"

typedef struct Raster {
    SpdSink sink;
    s32 *seg;          /* x0,y0,x1,y1 quads, 1/16 px, y down */
    int nseg, maxseg;
    int need;            /* segments the outline produced (even past maxseg) */
    s32 wy0, wy1;        /* only keep segments crossing these rows (1/16 px) */
    int evenodd;         /* even-odd fill rule instead of non-zero */
    s32 cx, cy, sx, sy;
    int open, overflow;
    int dropout;         /* keep thin strokes at small sizes */
    s32 x0, y0, x1, y1;  /* bounds, 1/16 px */
    fix orgx, orgy;      /* device position of outline origin (16.16) */
} Raster;

void raster_init(Raster *r, s32 *segbuf, int maxseg, fix orgx, fix orgy);
/* pixel bounds of everything drawn: [*px0,*px1) x [*py0,*py1); 0 if empty */
int  raster_bounds(const Raster *r, int *px0, int *py0, int *px1, int *py1);
/* Fill device pixels [bx,bx+w) x [by,by+h) into bits (pixel (bx,by) = bit 7
 * of bits[0]). bits must be cleared by the caller (fill ORs). tmp must hold
 * at least raster_tmp_size(r) bytes. Returns 0 on failure. */
int  raster_fill(const Raster *r, u8 *bits, int rowbytes, int bx, int by, int w, int h,
                 void *tmp, s32 tmpsize);
s32  raster_tmp_size(const Raster *r);
/* bytes of scratch one segment needs in all: its slot plus fill tables */
s32  raster_seg_bytes(void);
void raster_span(u8 *row, int p0, int p1);   /* set bits [p0,p1) */

#endif
