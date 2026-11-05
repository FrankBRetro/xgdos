/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* gcache.c - glyph bitmap cache with LRU eviction */
#include "xgdos.h"
#include "gcache.h"
#include "heap.h"
#include "xstd.h"
#include "raster.h"
#include "scr.h"

#define NBUCKET 128

static Glyph *bucket[NBUCKET];
static s32 used;

s32 gc_used(void) { return used; }
s32 gc_budget(void) { return (s32)xg_cfg.cache_kb * 1024; }

static unsigned hashk(int fi, int idx, fix sx, fix sy, int hint)
{
    u32 h = (u32)fi * 31 + (u32)idx * 7 + (u32)(sx >> 6) + (u32)(sy >> 4) + (u32)hint * 13;
    return (unsigned)(h ^ (h >> 9)) & (NBUCKET - 1);
}

static void unlink_free(Glyph **pp)
{
    Glyph *g = *pp;
    *pp = g->next;
    used -= g->bytes;
    heap_free(g);
}

static int evict_one(void)
{
    Glyph **best = NULL, **pp;
    int b;
    for (b = 0; b < NBUCKET; b++)
        for (pp = &bucket[b]; *pp; pp = &(*pp)->next)
            if (!best || (*pp)->lru < (*best)->lru) best = pp;
    if (!best) return 0;
    unlink_free(best);
    return 1;
}

int gc_shrink(s32 need)
{
    int any = 0;
    s32 target = used - need;
    if (target < 0) target = 0;
    while (used > target && evict_one()) any = 1;
    return any;
}

void gc_flush(void)
{
    int b;
    for (b = 0; b < NBUCKET; b++) while (bucket[b]) unlink_free(&bucket[b]);
}

void gc_flush_font(int fi)
{
    int b;
    Glyph **pp;
    for (b = 0; b < NBUCKET; b++) {
        pp = &bucket[b];
        while (*pp) {
            if ((*pp)->font == fi) unlink_free(pp);
            else pp = &(*pp)->next;
        }
    }
}

Glyph *gc_get(int fi, int idx, fix sx, fix sy, fix skew, int hint)
{
    unsigned h = hashk(fi, idx, sx, sy, hint);
    Glyph *g;
    XFont *f;
    Raster r;
    SpdXform xf;
    s32 need;
    Scr sc;
    int x0, y0, x1, y1, w, ht, rb;

    for (g = bucket[h]; g; g = g->next)
        if (g->font == fi && g->idx == idx && g->sx == sx && g->sy == sy &&
            g->skew == skew && g->hint == hint) {
            g->lru = ++xg_tick;
            return g;
        }
    f = fm_get(fi);
    if (!f) return NULL;
    xmemset(&xf, 0, sizeof xf);
    /* hint: bits 0-7 hinting level, bits 8-9 quarter-pixel x phase */
    xf.sx = sx; xf.sy = sy; xf.skew = skew; xf.hint = (s16)(hint & 0xff);
    xf.phx = (fix)((hint >> 8) & 3) << 14;
    scr_init(&sc);
    for (;;) {
        raster_init(&r, sc.seg, sc.maxseg, 0, 0);
        r.dropout = (s32)(((u32)sy * f->orupm) >> 16) <= 40;   /* em <= 40 px */
        if (!spd_char_outline(&f->spd, idx, &xf, &r.sink)) { scr_done(&sc); return NULL; }
        if (!r.overflow) break;
        /* too complex for the scratch buffer: only borrow heap memory for
         * it if the bitmap would be small enough to cache at all */
        if (raster_bounds(&r, &x0, &y0, &x1, &y1)) {
            s32 bw = x1 - x0, bh = y1 - y0;
            if (bw > 4000 || bh > 4000 ||
                (s32)sizeof(Glyph) + (((bw + 15) >> 4) << 1) * bh > gc_budget() / 2) break;
        }
        if (sc.own || !scr_grow(&sc, r.need)) { scr_done(&sc); return NULL; }
    }
    if (r.overflow) { scr_done(&sc); return NULL; }   /* drawn directly instead */
    if (!raster_bounds(&r, &x0, &y0, &x1, &y1)) { x0 = y0 = 0; x1 = y1 = 0; }
    w = x1 - x0; ht = y1 - y0;
    rb = ((w + 15) >> 4) << 1;
    need = (s32)sizeof(Glyph) + (s32)rb * ht;
    if (w > 4000 || ht > 4000 || need > gc_budget() / 2) {   /* too big to cache */
        scr_done(&sc);
        return NULL;
    }
    while (used + need > gc_budget() && evict_one()) ;
    g = (Glyph *)heap_alloc(need);
    while (!g && evict_one()) g = (Glyph *)heap_alloc(need);
    if (!g) { scr_done(&sc); return NULL; }
    xmemset(g->bits, 0, (s32)rb * ht);
    if (ht && !raster_fill(&r, g->bits, rb, x0, y0, w, ht, sc.tmp, sc.tmpsize)) {
        heap_free(g);
        scr_done(&sc);
        return NULL;
    }
    scr_done(&sc);
    g->bytes = need;
    g->font = (s16)fi; g->idx = (s16)idx; g->hint = (s16)hint;
    g->sx = sx; g->sy = sy; g->skew = skew;
    g->left = (s16)x0; g->top = (s16)y0;
    g->w = (s16)w; g->h = (s16)ht; g->rb = (s16)rb;
    g->lru = ++xg_tick;
    g->next = bucket[h];
    bucket[h] = g;
    used += need;
    return g;
}

/* ---- rasteriser working memory (scr.h) ---- */

/* segments take 16 of every raster_seg_bytes(), fill tables the rest */
static void split(Scr *s, u8 *buf, s32 size)
{
    s32 per = raster_seg_bytes();
    s32 n = (size - 16) / per;
    s->seg = (s32 *)buf;
    s->maxseg = (int)n;
    s->tmp = buf + ((n * 16 + 3) & ~3);
    s->tmpsize = size - ((n * 16 + 3) & ~3);
}

void scr_init(Scr *s)
{
    s->own = NULL;
    split(s, xg_scratch, xg_scratch_size);
}

int scr_grow(Scr *s, int nseg)
{
    s32 size = (s32)nseg * raster_seg_bytes() + 64;
    u8 *p;
    if (nseg <= s->maxseg) return 1;
    scr_done(s);
    p = (u8 *)heap_alloc(size);
    while (!p && gc_shrink(size)) p = (u8 *)heap_alloc(size);
    if (!p) { scr_init(s); return 0; }
    s->own = p;
    split(s, p, size);
    return 1;
}

void scr_done(Scr *s)
{
    if (s->own) heap_free(s->own);
    s->own = NULL;
}
