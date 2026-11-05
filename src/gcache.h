/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* gcache.h - rendered glyph bitmap cache (lives in the XGDOS heap) */
#ifndef GCACHE_H
#define GCACHE_H
#include "xtypes.h"
#include "xfix.h"

typedef struct Glyph {
    struct Glyph *next;
    u32  lru;
    s32  bytes;
    fix  sx, sy, skew;
    s16  font, idx, hint;
    s16  left, top;     /* bitmap (0,0) is at pen + (left, top); y down */
    s16  w, h, rb;      /* size and bytes per row (even) */
    u8   bits[2];
} Glyph;

Glyph *gc_get(int fi, int idx, fix sx, fix sy, fix skew, int hint);
void   gc_flush(void);
void   gc_flush_font(int fi);
int    gc_shrink(s32 need);   /* free cache memory; 1 if something freed */
s32    gc_used(void);
s32    gc_budget(void);

#endif
