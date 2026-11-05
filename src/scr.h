/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* scr.h - rasteriser working memory (in gcache.c): the scratch buffer, or for a
 * glyph too complex for it a temporary block from the XGDOS heap */
#ifndef SCR_H
#define SCR_H
#include "xtypes.h"

typedef struct Scr {
    s32 *seg;  int maxseg;     /* segment slots for raster_init */
    u8  *tmp;  s32 tmpsize;    /* fill tables for raster_fill */
    u8  *own;                  /* heap block in use, or NULL */
} Scr;

void scr_init(Scr *s);              /* the shared scratch buffer */
int  scr_grow(Scr *s, int nseg);    /* room for nseg segments; 0 if no memory */
void scr_done(Scr *s);              /* give back any heap block */

#endif
