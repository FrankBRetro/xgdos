/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* text.h - outline text engine (metrics, layout, rendering) */
#ifndef TEXT_H
#define TEXT_H
#include "vdi.h"

typedef struct TxtMetrics {
    XFont *f;
    fix sx, sy;            /* px per oru */
    fix emx, emy;          /* em in px */
    fix skew;              /* tangent */
    int top, asc, half, desc, bot;   /* px, distances from baseline */
    int cellw, cellh, charw, charh;
    int bold, outl;        /* effect widening in px */
    int hint;
} TxtMetrics;

typedef struct TxtGlyph {
    s16 idx, code;
    fix x;                 /* pen offset along the baseline (px) */
} TxtGlyph;

/* fill metrics for the current outline font of w; returns 0 if no font */
int  txt_metrics(Wk *w, TxtMetrics *m);
/* convert points to px along y / x for this workstation */
fix  txt_pt2px_y(Wk *w, fix pt);
fix  txt_pt2px_x(Wk *w, fix pt);
fix  txt_px2pt_y(Wk *w, fix px);
/* layout; returns number of glyphs (heap array in *out, caller frees) */
int  txt_layout(Wk *w, TxtMetrics *m, const s16 *codes, int n, int frac,
                TxtGlyph **out, fix *adv);
/* baseline offset (px, y down) for the vertical alignment */
int  txt_valign(Wk *w, TxtMetrics *m);
/* bounding box corners like vqt_extent (8 words) */
void txt_extent(Wk *w, TxtMetrics *m, fix adv, s16 *pts);

/* Render a string and hand monochrome strips to blit().
 * x,y: VDI text position; justify_len > 0 for v_justified.
 * offsets: optional per char x/y offsets (v_ftext_offset). */
typedef TxtBlitFn TxtBlit;
int  txt_draw(Wk *w, int x, int y, const s16 *codes, int n, int frac,
              int justify_len, int wordsp, int charsp, const s16 *offsets, TxtBlit blit);

u8   txt_get8(const u8 *s, int nbytes, int bit);

/* scale string advances for v_justified; modifies g[].x */
void txt_justify(TxtGlyph *g, int n, fix *adv, fix want, int wordsp, int charsp);

#endif
