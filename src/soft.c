/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* soft.c - software VDI renderer for one-plane bitmaps, used by XGDOS's
 * own memory device (61). Everything is drawn by XGDOS itself: no ROM
 * VDI, no Line-A, no external driver.
 *
 * Polygons, thick lines, discs and ellipses go through the outline
 * rasteriser (raster.c) a strip at a time; thin lines, markers and spans
 * are drawn directly. All painting goes through paint_row(), which applies
 * the clip rectangle, the colour, the fill pattern and the writing mode.
 */
#include "soft.h"
#include "text.h"
#include "raster.h"
#include "scr.h"
#include "heap.h"
#include "gcache.h"
#include "sysfont.h"
#include "xstd.h"

#define MASK_BYTES 8192

/* ---------------- patterns ---------------- */

static const u16 pat_dither[8][4] = {     /* fill style 1-8: grey levels */
    { 0x8000, 0x0000, 0x0800, 0x0000 }, { 0x8080, 0x0000, 0x0808, 0x0000 },
    { 0x8888, 0x0000, 0x2222, 0x0000 }, { 0x8888, 0x2222, 0x8888, 0x2222 },
    { 0xaaaa, 0x4444, 0xaaaa, 0x1111 }, { 0xaaaa, 0x5555, 0xaaaa, 0x5555 },
    { 0x5555, 0xbbbb, 0x5555, 0xeeee }, { 0xffff, 0xffff, 0xffff, 0xffff },
};
static const u16 pat_deco[16][16] = {     /* fill style 9-24 */
    { 0xffff,0x8080,0x8080,0x8080,0xffff,0x0808,0x0808,0x0808,0xffff,0x8080,0x8080,0x8080,0xffff,0x0808,0x0808,0x0808 },
    { 0x8080,0x4141,0x2222,0x1414,0x0808,0x1010,0x2020,0x4040,0x8080,0x4141,0x2222,0x1414,0x0808,0x1010,0x2020,0x4040 },
    { 0x0000,0x0000,0x1010,0x2828,0x0000,0x0000,0x0101,0x8282,0x0000,0x0000,0x1010,0x2828,0x0000,0x0000,0x0101,0x8282 },
    { 0x0000,0x1c1c,0x2222,0x2222,0x2222,0x1c1c,0x0000,0x0000,0x0000,0xc1c1,0x2222,0x2222,0x2222,0xc1c1,0x0000,0x0000 },
    { 0x8888,0x4444,0x2222,0x1111,0x8888,0x4444,0x2222,0x1111,0x8888,0x4444,0x2222,0x1111,0x8888,0x4444,0x2222,0x1111 },
    { 0xf0f0,0xf0f0,0xf0f0,0xf0f0,0x0f0f,0x0f0f,0x0f0f,0x0f0f,0xf0f0,0xf0f0,0xf0f0,0xf0f0,0x0f0f,0x0f0f,0x0f0f,0x0f0f },
    { 0x8181,0x4242,0x2424,0x1818,0x1818,0x2424,0x4242,0x8181,0x8181,0x4242,0x2424,0x1818,0x1818,0x2424,0x4242,0x8181 },
    { 0xfefe,0x8282,0x8282,0x8282,0x8282,0x8282,0xfefe,0x0000,0xfefe,0x8282,0x8282,0x8282,0x8282,0x8282,0xfefe,0x0000 },
    { 0x0808,0x1c1c,0x3e3e,0x7f7f,0x0808,0x0808,0x0000,0x0000,0x0808,0x1c1c,0x3e3e,0x7f7f,0x0808,0x0808,0x0000,0x0000 },
    { 0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000,0xaaaa,0x0000 },
    { 0xffff,0x0000,0x0000,0x0000,0xffff,0x0000,0x0000,0x0000,0xffff,0x0000,0x0000,0x0000,0xffff,0x0000,0x0000,0x0000 },
    { 0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888,0x8888 },
    { 0x1010,0x3838,0x7c7c,0xfefe,0x7c7c,0x3838,0x1010,0x0000,0x1010,0x3838,0x7c7c,0xfefe,0x7c7c,0x3838,0x1010,0x0000 },
    { 0x0000,0x6666,0x6666,0x0000,0x0000,0x6666,0x6666,0x0000,0x0000,0x6666,0x6666,0x0000,0x0000,0x6666,0x6666,0x0000 },
    { 0xc3c3,0x6666,0x3c3c,0x1818,0x3c3c,0x6666,0xc3c3,0x8181,0xc3c3,0x6666,0x3c3c,0x1818,0x3c3c,0x6666,0xc3c3,0x8181 },
    { 0x0303,0x0606,0x0c0c,0x1818,0x3030,0x6060,0xc0c0,0x8181,0x0303,0x0606,0x0c0c,0x1818,0x3030,0x6060,0xc0c0,0x8181 },
};

/* hatch 1-12: angle (tenths), spacing, thickness */
static const s16 hatch_def[12][3] = {
    { 450, 8, 1 }, { 1350, 8, 1 }, { 450, 4, 1 }, { 0, 4, 1 }, { 900, 4, 1 }, { 0, 8, 2 },
    { 450, 6, 2 }, { 1350, 6, 2 }, { 0, 8, 1 }, { 900, 8, 1 }, { 1350, 4, 1 }, { 900, 8, 2 },
};

static void make_hatch(u16 *p, int style)
{
    int a = hatch_def[(style - 1) % 12][0], sp = hatch_def[(style - 1) % 12][1], th = hatch_def[(style - 1) % 12][2];
    int x, y;
    for (y = 0; y < 16; y++) {
        u16 v = 0;
        for (x = 0; x < 16; x++) {
            int t;
            if (a == 0) t = y;
            else if (a == 900) t = x;
            else if (a == 450) t = x + y;
            else t = x - y + 16;
            if ((t % sp) < th) v |= (u16)(0x8000 >> x);
        }
        p[y] = v;
    }
}

/* ---------------- attributes ---------------- */

static const s16 defpal16[16][3] = {
    {1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
    {1000,1000,0},{1000,0,1000},{752,752,752},{501,501,501},{501,0,0},{0,501,0},
    {0,0,501},{0,501,501},{501,501,0},{501,0,501}
};

static int clampc(Wk *w, int c) { return (c >= 0 && c < w->ncol) ? c : 1; }

void ga_init(GAttr *a, Wk *w, const s16 *wi)
{
    int i;
    xmemset(a, 0, sizeof *a);
    a->l_type = (wi && wi[1] >= 1 && wi[1] <= 7) ? wi[1] : 1;
    a->l_color = wi ? (s16)clampc(w, wi[2]) : 1;
    a->m_type = (wi && wi[3] >= 1 && wi[3] <= 6) ? wi[3] : 3;
    a->m_color = wi ? (s16)clampc(w, wi[4]) : 1;
    a->f_interior = (wi && wi[7] >= 0 && wi[7] <= 4) ? wi[7] : 0;
    a->f_style = wi ? wi[8] : 1;
    a->f_color = wi ? (s16)clampc(w, wi[9]) : 1;
    a->f_perim = 1;
    a->l_width = 1;
    a->m_height = 11;
    a->l_udsty = 0xffff;
    for (i = 0; i < 16; i++) {
        a->udpat[i] = (u16)((i & 1) ? 0xaaaa : 0x5555);
        a->pal[i][0] = defpal16[i][0]; a->pal[i][1] = defpal16[i][1]; a->pal[i][2] = defpal16[i][2];
    }
    if (wi) w->t.color = (s16)clampc(w, wi[6]);
}

#define CONTRL pb->contrl
#define INTIN  pb->intin
#define PTSIN  pb->ptsin
#define INTOUT pb->intout
#define PTSOUT pb->ptsout

int ga_op(GAttr *a, Wk *w, VDIPB *pb)
{
    int op = CONTRL[0], i;
    switch (op) {
    case 14:    /* vs_color */
        i = INTIN[0];
        if (i >= 0 && i < 16) { a->pal[i][0] = INTIN[1]; a->pal[i][1] = INTIN[2]; a->pal[i][2] = INTIN[3]; }
        return 1;
    case 26:    /* vq_color */
        i = INTIN[0];
        INTOUT[0] = (s16)((i >= 0 && i < w->ncol) ? i : -1);
        if (i < 0 || i >= 16) i = 1;
        INTOUT[1] = a->pal[i][0]; INTOUT[2] = a->pal[i][1]; INTOUT[3] = a->pal[i][2];
        CONTRL[4] = 4;
        return 1;
    case 15: a->l_type = (INTIN[0] >= 1 && INTIN[0] <= 7) ? INTIN[0] : 1; INTOUT[0] = a->l_type; CONTRL[4] = 1; return 1;
    case 16: a->l_width = (PTSIN[0] < 1) ? 1 : (PTSIN[0] | 1); PTSOUT[0] = a->l_width; PTSOUT[1] = 0; CONTRL[2] = 1; return 1;
    case 17: a->l_color = (s16)clampc(w, INTIN[0]); INTOUT[0] = a->l_color; CONTRL[4] = 1; return 1;
    case 108: a->l_beg = (INTIN[0] >= 0 && INTIN[0] <= 2) ? INTIN[0] : 0; a->l_end = (INTIN[1] >= 0 && INTIN[1] <= 2) ? INTIN[1] : 0;
              INTOUT[0] = a->l_beg; INTOUT[1] = a->l_end; CONTRL[4] = 2; return 1;
    case 113: a->l_udsty = (u16)INTIN[0]; return 1;
    case 18: a->m_type = (INTIN[0] >= 1 && INTIN[0] <= 6) ? INTIN[0] : 3; INTOUT[0] = a->m_type; CONTRL[4] = 1; return 1;
    case 19: a->m_height = (PTSIN[1] < 1) ? 1 : PTSIN[1]; PTSOUT[0] = a->m_height; PTSOUT[1] = a->m_height; CONTRL[2] = 1; return 1;
    case 20: a->m_color = (s16)clampc(w, INTIN[0]); INTOUT[0] = a->m_color; CONTRL[4] = 1; return 1;
    case 23: a->f_interior = (INTIN[0] >= 0 && INTIN[0] <= 4) ? INTIN[0] : 0; INTOUT[0] = a->f_interior; CONTRL[4] = 1; return 1;
    case 24:
        a->f_style = INTIN[0];
        if (a->f_interior == 2 && (a->f_style < 1 || a->f_style > 24)) a->f_style = 1;
        if (a->f_interior == 3 && (a->f_style < 1 || a->f_style > 12)) a->f_style = 1;
        INTOUT[0] = a->f_style; CONTRL[4] = 1; return 1;
    case 25: a->f_color = (s16)clampc(w, INTIN[0]); INTOUT[0] = a->f_color; CONTRL[4] = 1; return 1;
    case 104: a->f_perim = INTIN[0] ? 1 : 0; INTOUT[0] = a->f_perim; CONTRL[4] = 1; return 1;
    case 112:   /* vsf_udpat: first plane only on a mono device */
        for (i = 0; i < 16 && i < CONTRL[3]; i++) a->udpat[i] = (u16)INTIN[i];
        return 1;
    case 22: w->t.color = (s16)clampc(w, INTIN[0]); INTOUT[0] = w->t.color; CONTRL[4] = 1; return 1;
    case 32: w->t.wrmode = (INTIN[0] >= 1 && INTIN[0] <= 4) ? INTIN[0] : 1; INTOUT[0] = w->t.wrmode; CONTRL[4] = 1; return 1;
    case 129:   /* vs_clip (xg_text_op stored it already) */
        w->clip_on = INTIN[0];
        if (w->clip_on) {
            w->clip[0] = XG_MIN(PTSIN[0], PTSIN[2]); w->clip[2] = XG_MAX(PTSIN[0], PTSIN[2]);
            w->clip[1] = XG_MIN(PTSIN[1], PTSIN[3]); w->clip[3] = XG_MAX(PTSIN[1], PTSIN[3]);
        }
        return 1;
    case 35:    /* vql_attributes */
        INTOUT[0] = a->l_type; INTOUT[1] = a->l_color; INTOUT[2] = w->t.wrmode;
        INTOUT[3] = a->l_beg; INTOUT[4] = a->l_end;
        PTSOUT[0] = a->l_width; PTSOUT[1] = 0;
        CONTRL[2] = 1; CONTRL[4] = 5;
        return 1;
    case 36:    /* vqm_attributes */
        INTOUT[0] = a->m_type; INTOUT[1] = a->m_color; INTOUT[2] = w->t.wrmode;
        PTSOUT[0] = a->m_height; PTSOUT[1] = a->m_height;
        CONTRL[2] = 1; CONTRL[4] = 3;
        return 1;
    case 37:    /* vqf_attributes */
        INTOUT[0] = a->f_interior; INTOUT[1] = a->f_color; INTOUT[2] = a->f_style;
        INTOUT[3] = w->t.wrmode; INTOUT[4] = a->f_perim;
        CONTRL[4] = 5;
        return 1;
    }
    return 0;
}

/* ---------------- painting ---------------- */

static int bit_of(int c) { return c != 0; }

void soft_clip(SoftDev *d, Wk *w)
{
    d->cx0 = 0; d->cy0 = 0; d->cx1 = d->s.w - 1; d->cy1 = d->s.h - 1;
    if (w->clip_on) {
        d->cx0 = XG_MAX(d->cx0, w->clip[0]); d->cy0 = XG_MAX(d->cy0, w->clip[1]);
        d->cx1 = XG_MIN(d->cx1, w->clip[2]); d->cy1 = XG_MIN(d->cy1, w->clip[3]);
    }
}

void soft_clear(SoftDev *d)
{
    xmemset(d->s.bits, 0, d->s.rb * d->s.h);
}

/* Paint pixels x0..x1 of row y. m: optional mask row whose bit 0 (MSB of
 * m[0]) is pixel mx. With mpat set the mask is the pattern (text, raster
 * copies: the whole span is covered); otherwise the mask is the coverage
 * and k->pat the pattern (filled shapes). */
static void paint_row(SoftDev *d, int y, int x0, int x1, const u8 *m, int mrb, int mx, int mpat, const Ink *k)
{
    u8 *row, fgb, bgb;
    int bx, b0, b1;
    u16 pw;
    if (y < d->cy0 || y > d->cy1) return;
    if (x0 < d->cx0) x0 = d->cx0;
    if (x1 > d->cx1) x1 = d->cx1;
    if (x0 > x1) return;
    row = d->s.bits + (s32)y * d->s.rb;
    pw = k->pat ? k->pat[y & 15] : 0xffff;
    fgb = k->fg ? 0xff : 0;
    bgb = k->bg ? 0xff : 0;
    b0 = x0 >> 3; b1 = x1 >> 3;
    for (bx = b0; bx <= b1; bx++) {
        u8 edge = 0xff, cov, P, M, dv = row[bx];
        if (bx == b0) edge &= (u8)(0xff >> (x0 & 7));
        if (bx == b1) edge &= (u8)(0xff << (7 - (x1 & 7)));
        M = m ? txt_get8(m, mrb, bx * 8 - mx) : 0xff;
        P = (u8)((bx & 1) ? pw : (pw >> 8));
        if (mpat) { cov = edge; P = M; }
        else cov = (u8)(edge & M);
        switch (k->mode) {
        default:    /* replace */
            dv = (u8)((dv & ~cov) | (cov & ((P & fgb) | (~P & bgb))));
            break;
        case 2:     /* transparent */
            dv = fgb ? (u8)(dv | (cov & P)) : (u8)(dv & ~(cov & P));
            break;
        case 3:     /* xor */
            dv ^= (u8)(cov & P);
            break;
        case 4:     /* reverse transparent */
            dv = fgb ? (u8)(dv | (cov & ~P)) : (u8)(dv & ~(cov & ~P));
            break;
        }
        row[bx] = dv;
    }
}

static void plot(SoftDev *d, int x, int y, int on, const Ink *k)
{
    u8 *p, b;
    if (x < d->cx0 || x > d->cx1 || y < d->cy0 || y > d->cy1) return;
    p = d->s.bits + (s32)y * d->s.rb + (x >> 3);
    b = (u8)(0x80 >> (x & 7));
    switch (k->mode) {
    default: if (on ? k->fg : k->bg) *p |= b; else *p &= (u8)~b; break;
    case 2: if (on) { if (k->fg) *p |= b; else *p &= (u8)~b; } break;
    case 3: if (on) *p ^= b; break;
    case 4: if (!on) { if (k->fg) *p |= b; else *p &= (u8)~b; } break;
    }
}

static void ink_line(Wk *w, SoftDev *d, Ink *k, int col)
{
    k->fg = bit_of(col); k->bg = 0; k->mode = w->t.wrmode; k->pat = NULL;
}

static void ink_fill(Wk *w, SoftDev *d, Ink *k, u16 *tmp)
{
    GAttr *a = &d->a;
    int i;
    k->fg = bit_of(a->f_color); k->bg = 0; k->mode = w->t.wrmode; k->pat = NULL;
    switch (a->f_interior) {
    case 0: k->fg = 0; break;                          /* hollow: background */
    case 1: break;
    case 2:
        if (a->f_style >= 1 && a->f_style <= 8) {
            for (i = 0; i < 16; i++) tmp[i] = pat_dither[a->f_style - 1][i & 3];
            k->pat = (a->f_style == 8) ? NULL : tmp;
        } else if (a->f_style >= 9 && a->f_style <= 24) k->pat = pat_deco[a->f_style - 9];
        break;
    case 3: make_hatch(tmp, a->f_style >= 1 && a->f_style <= 12 ? a->f_style : 1); k->pat = tmp; break;
    case 4: k->pat = a->udpat; break;
    }
}

/* ---------------- paths (polygons, thick lines, discs) ---------------- */

typedef struct Path {
    s32 *p;              /* x, y (16.16 px) and a move flag per point */
    int n, cap;
    int start;           /* first point of the open contour */
    int err;
} Path;

static void path_init(Path *P) { P->p = NULL; P->n = P->cap = P->start = 0; P->err = 0; }
static void path_free(Path *P) { heap_free(P->p); P->p = NULL; P->n = P->cap = 0; }

static void path_pt(Path *P, fix x, fix y, int move)
{
    if (P->n >= P->cap) {
        int nc = P->cap ? P->cap * 2 : 64;
        s32 *q = (s32 *)heap_realloc(P->p, (s32)nc * 12);
        while (!q && gc_shrink((s32)nc * 12)) q = (s32 *)heap_realloc(P->p, (s32)nc * 12);
        if (!q) { P->err = 1; return; }
        P->p = q; P->cap = nc;
    }
    if (move) P->start = P->n;
    P->p[3 * P->n] = x; P->p[3 * P->n + 1] = y; P->p[3 * P->n + 2] = move;
    P->n++;
}

/* end the open contour, turning it so all contours wind the same way:
 * overlapping pieces (line segments, joins) then add up instead of
 * cancelling under the non-zero rule */
static void path_close(Path *P, int orient)
{
    int i, j, s = P->start, n = P->n - s, sh = 0;
    s32 area = 0, ox, oy;
    u32 ext = 0;
    if (P->err || n < 3 || !orient) return;
    /* shoelace sum relative to the first point, scaled so every product
     * (and the sum, at most twice the bounding box) fits 32 bits */
    ox = P->p[3 * s]; oy = P->p[3 * s + 1];
    for (i = 0; i < n; i++) {
        u32 ex = (u32)XG_ABS(P->p[3 * (s + i)] - ox), ey = (u32)XG_ABS(P->p[3 * (s + i) + 1] - oy);
        if (ex > ext) ext = ex;
        if (ey > ext) ext = ey;
    }
    while (ext >= 16384) { ext >>= 1; sh++; }
    for (i = 0; i < n; i++) {
        s32 xi, yi, xj, yj;
        j = (i + 1) % n;
        xi = (P->p[3 * (s + i)] - ox) >> sh; yi = (P->p[3 * (s + i) + 1] - oy) >> sh;
        xj = (P->p[3 * (s + j)] - ox) >> sh; yj = (P->p[3 * (s + j) + 1] - oy) >> sh;
        area += xi * yj - xj * yi;
    }
    if (area < 0)
        for (i = 0, j = n - 1; i < j; i++, j--) {
            s32 tx = P->p[3 * (s + i)], ty = P->p[3 * (s + i) + 1];
            P->p[3 * (s + i)] = P->p[3 * (s + j)]; P->p[3 * (s + i) + 1] = P->p[3 * (s + j) + 1];
            P->p[3 * (s + j)] = tx; P->p[3 * (s + j) + 1] = ty;
        }
    P->p[3 * s + 2] = 1;
    for (i = 1; i < n; i++) P->p[3 * (s + i) + 2] = 0;
}

/* polygon through pixel centres */
static void path_poly(Path *P, const s16 *xy, int n, int orient)
{
    int i;
    for (i = 0; i < n; i++) path_pt(P, INT2FIX(xy[2 * i]) + FIX_HALF, INT2FIX(xy[2 * i + 1]) + FIX_HALF, i == 0);
    path_close(P, orient);
}

static int segs_for(s32 r)
{
    int n = (int)(r / 2) + 12;
    return n > 180 ? 180 : n;
}

/* ellipse or part of it (angles in tenths, anticlockwise from 3 o'clock);
 * pie adds the centre */
static void path_ellipse(Path *P, fix cx, fix cy, fix rx, fix ry, int a0, int a1, int pie, int orient)
{
    int n, i, full = (a0 == 0 && a1 == 3600);
    s32 span;
    while (a1 <= a0) a1 += 3600;
    span = a1 - a0;
    n = segs_for(FIX2INT(XG_MAX(rx, ry)));
    n = (int)(n * span / 3600) + 2;
    if (pie && !full) path_pt(P, cx, cy, 1);
    for (i = 0; i <= n; i++) {
        s32 a = a0 + span * i / n;
        path_pt(P, cx + fixmul(rx, fixcos(a)), cy - fixmul(ry, fixsin(a)), i == 0 && !(pie && !full));
    }
    path_close(P, orient);
}

static void path_disc(Path *P, fix cx, fix cy, fix r)
{
    path_ellipse(P, cx, cy, r, r, 0, 3600, 0, 1);
}

static void emit(Raster *r, const Path *P)
{
    int i;
    for (i = 0; i < P->n; i++) {
        fix x = P->p[3 * i], y = -P->p[3 * i + 1];
        if (P->p[3 * i + 2]) { if (i) r->sink.close(&r->sink); r->sink.move(&r->sink, x, y); }
        else r->sink.line(&r->sink, x, y);
    }
    if (P->n) r->sink.close(&r->sink);
}

static int fill_evenodd;    /* v_fillarea: the VDI fills polygons even-odd */

static void path_fill(SoftDev *d, const Path *P, const Ink *k)
{
    s32 x0 = 0x7fffffff, y0 = 0x7fffffff, x1 = -0x7fffffff, y1 = -0x7fffffff;
    int i, bx0, by0, bx1, by1, bw, mrb, sh, sy;
    u8 *mask;
    if (P->err || P->n < 3) return;
    for (i = 0; i < P->n; i++) {
        s32 x = P->p[3 * i], y = P->p[3 * i + 1];
        if (x < x0) x0 = x; if (x > x1) x1 = x;
        if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    bx0 = XG_MAX(FIX2INT(x0) - 1, d->cx0); bx1 = XG_MIN(FIX2INT(x1) + 1, d->cx1);
    by0 = XG_MAX(FIX2INT(y0) - 1, d->cy0); by1 = XG_MIN(FIX2INT(y1) + 1, d->cy1);
    if (bx0 > bx1 || by0 > by1) return;
    bw = bx1 - bx0 + 1;
    mrb = ((bw + 15) >> 4) << 1;
    sh = (int)(MASK_BYTES / mrb);
    if (sh < 1) sh = 1;
    mask = (u8 *)heap_alloc((s32)mrb * sh);
    while (!mask && gc_shrink((s32)mrb * sh)) mask = (u8 *)heap_alloc((s32)mrb * sh);
    if (!mask) return;
    for (sy = by0; sy <= by1; sy += sh) {
        int rows = XG_MIN(sh, by1 - sy + 1), r2;
        Raster r;
        Scr sc;
        scr_init(&sc);
        for (;;) {
            raster_init(&r, sc.seg, sc.maxseg, 0, 0);
            r.evenodd = fill_evenodd;
            r.wy0 = (s32)sy * 16 - 16;
            r.wy1 = (s32)(sy + rows) * 16 + 16;
            emit(&r, P);
            if (!r.overflow || sc.own || !scr_grow(&sc, r.need)) break;
        }
        if (!r.overflow) {
            xmemset(mask, 0, (s32)mrb * rows);
            raster_fill(&r, mask, mrb, bx0, sy, bw, rows, sc.tmp, sc.tmpsize);
            for (r2 = 0; r2 < rows; r2++) paint_row(d, sy + r2, bx0, bx1, mask + (s32)r2 * mrb, mrb, bx0, 0, k);
        }
        scr_done(&sc);
    }
    heap_free(mask);
}

/* ---------------- lines ---------------- */

static u16 line_style(GAttr *a)
{
    static const u16 st[6] = { 0xffff, 0xfff0, 0xc0c0, 0xff18, 0xff00, 0xf191 };
    return a->l_type == 7 ? a->l_udsty : st[(a->l_type >= 1 && a->l_type <= 6) ? a->l_type - 1 : 0];
}

static void thin_line(SoftDev *d, int x0, int y0, int x1, int y1, u16 style, int *phase, int skip_first, const Ink *k)
{
    int dx = XG_ABS(x1 - x0), dy = -XG_ABS(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, first = 1;
    for (;;) {
        if (!(first && skip_first)) {
            plot(d, x0, y0, (style >> (15 - (*phase & 15))) & 1, k);
            (*phase)++;
        }
        first = 0;
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

/* length of a vector (16.16), without overflow for screen sizes */
static fix vlen(fix dx, fix dy)
{
    u32 ax = (u32)XG_ABS(dx), ay = (u32)XG_ABS(dy);
    int sh = 0;
    while ((ax | ay) >= 32768) { ax >>= 1; ay >>= 1; sh++; }   /* squares fit 32 bits */
    return (fix)(isqrt(ax * ax + ay * ay) << sh);
}

/* arrow head at (x1,y1) pointing away from (x0,y0); returns how far the
 * line itself must stop short of the tip */
static fix arrow(Path *P, fix x0, fix y0, fix x1, fix y1, int width)
{
    fix dx = x1 - x0, dy = y1 - y0, len = vlen(dx, dy), L, H, ux, uy;
    if (len < FIX_ONE) return 0;
    L = INT2FIX(6 + 3 * width);
    if (L > len) L = len;
    H = L / 2;
    ux = fixdiv(dx, len); uy = fixdiv(dy, len);
    path_pt(P, x1, y1, 1);
    path_pt(P, x1 - fixmul(ux, L) - fixmul(uy, H), y1 - fixmul(uy, L) + fixmul(ux, H), 0);
    path_pt(P, x1 - fixmul(ux, L) + fixmul(uy, H), y1 - fixmul(uy, L) - fixmul(ux, H), 0);
    path_close(P, 1);
    return L - FIX_ONE / 2;
}

/* polyline with the line attributes; closed: join the last point to the first */
static void polyline(SoftDev *d, Wk *w, const s16 *xy, int n, int col, int closed)
{
    GAttr *a = &d->a;
    Ink k;
    int i, width = a->l_width;
    if (n < 2) {
        if (n == 1) { ink_line(w, d, &k, col); plot(d, xy[0], xy[1], 1, &k); }
        return;
    }
    ink_line(w, d, &k, col);
    if (width <= 1 && a->l_beg != 1 && a->l_end != 1) {
        int ph = 0;
        u16 st = line_style(a);
        for (i = 0; i + 1 < n; i++) thin_line(d, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], st, &ph, i > 0, &k);
        if (closed && n > 2) thin_line(d, xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], st, &ph, 1, &k);
        return;
    }
    {
        Path P;
        fix hw = INT2FIX(width) / 2;
        fix sx0, sy0, sx1, sy1;
        int last = closed ? n : n - 1;
        path_init(&P);
        sx0 = INT2FIX(xy[0]) + FIX_HALF; sy0 = INT2FIX(xy[1]) + FIX_HALF;
        sx1 = INT2FIX(xy[2 * n - 2]) + FIX_HALF; sy1 = INT2FIX(xy[2 * n - 1]) + FIX_HALF;
        /* arrows shorten the line ends */
        if (!closed && a->l_beg == 1) {
            fix nx = INT2FIX(xy[2]) + FIX_HALF, ny = INT2FIX(xy[3]) + FIX_HALF;
            fix L = arrow(&P, nx, ny, sx0, sy0, width), len = vlen(sx0 - nx, sy0 - ny);
            if (len > 0) { sx0 -= muldiv(sx0 - nx, L, len); sy0 -= muldiv(sy0 - ny, L, len); }
        }
        if (!closed && a->l_end == 1) {
            fix px = INT2FIX(xy[2 * n - 4]) + FIX_HALF, py = INT2FIX(xy[2 * n - 3]) + FIX_HALF;
            fix L = arrow(&P, px, py, sx1, sy1, width), len = vlen(sx1 - px, sy1 - py);
            if (len > 0) { sx1 -= muldiv(sx1 - px, L, len); sy1 -= muldiv(sy1 - py, L, len); }
        }
        if (width <= 1) {
            /* thin line with arrow heads: the line itself stays thin */
            int ph = 0;
            u16 st = line_style(a);
            int ax0 = FIX2INT(sx0), ay0 = FIX2INT(sy0), ax1 = FIX2INT(sx1), ay1 = FIX2INT(sy1);
            for (i = 0; i + 1 < n; i++) {
                int x0 = i == 0 ? ax0 : xy[2 * i], y0 = i == 0 ? ay0 : xy[2 * i + 1];
                int x1 = i == n - 2 ? ax1 : xy[2 * i + 2], y1 = i == n - 2 ? ay1 : xy[2 * i + 3];
                thin_line(d, x0, y0, x1, y1, st, &ph, i > 0, &k);
            }
            path_fill(d, &P, &k);
            path_free(&P);
            return;
        }
        for (i = 0; i < last; i++) {
            int j = (i + 1) % n;
            fix x0 = INT2FIX(xy[2 * i]) + FIX_HALF, y0 = INT2FIX(xy[2 * i + 1]) + FIX_HALF;
            fix x1 = INT2FIX(xy[2 * j]) + FIX_HALF, y1 = INT2FIX(xy[2 * j + 1]) + FIX_HALF;
            fix dx, dy, len, nx, ny;
            if (!closed && i == 0) { x0 = sx0; y0 = sy0; }
            if (!closed && i == n - 2) { x1 = sx1; y1 = sy1; }
            dx = x1 - x0; dy = y1 - y0; len = vlen(dx, dy);
            if (len < FIX_ONE / 4) continue;
            nx = -muldiv(dy, hw, len); ny = muldiv(dx, hw, len);
            path_pt(&P, x0 + nx, y0 + ny, 1);
            path_pt(&P, x1 + nx, y1 + ny, 0);
            path_pt(&P, x1 - nx, y1 - ny, 0);
            path_pt(&P, x0 - nx, y0 - ny, 0);
            path_close(&P, 1);
            /* round joins between segments */
            if (i + 1 < n - 1 || closed) path_disc(&P, x1, y1, hw);
        }
        if (!closed && a->l_beg == 2) path_disc(&P, sx0, sy0, hw);
        if (!closed && a->l_end == 2) path_disc(&P, sx1, sy1, hw);
        path_fill(d, &P, &k);
        path_free(&P);
    }
}

/* ---------------- markers ---------------- */

static void markers(SoftDev *d, Wk *w, const s16 *xy, int n)
{
    GAttr *a = &d->a;
    Ink k;
    int i, h = a->m_height, r = h / 2;
    GAttr save = *a;
    ink_line(w, d, &k, a->m_color);
    a->l_width = 1; a->l_type = 1; a->l_beg = a->l_end = 0;
    for (i = 0; i < n; i++) {
        int x = xy[2 * i], y = xy[2 * i + 1];
        s16 p[10];
        switch (a->m_type) {
        case 1: plot(d, x, y, 1, &k); break;
        case 2:
            p[0] = (s16)(x - r); p[1] = (s16)y; p[2] = (s16)(x + r); p[3] = (s16)y; polyline(d, w, p, 2, a->m_color, 0);
            p[0] = (s16)x; p[1] = (s16)(y - r); p[2] = (s16)x; p[3] = (s16)(y + r); polyline(d, w, p, 2, a->m_color, 0);
            break;
        case 3:
            p[0] = (s16)(x - r); p[1] = (s16)y; p[2] = (s16)(x + r); p[3] = (s16)y; polyline(d, w, p, 2, a->m_color, 0);
            p[0] = (s16)x; p[1] = (s16)(y - r); p[2] = (s16)x; p[3] = (s16)(y + r); polyline(d, w, p, 2, a->m_color, 0);
            p[0] = (s16)(x - r * 2 / 3); p[1] = (s16)(y - r * 2 / 3); p[2] = (s16)(x + r * 2 / 3); p[3] = (s16)(y + r * 2 / 3); polyline(d, w, p, 2, a->m_color, 0);
            p[0] = (s16)(x - r * 2 / 3); p[1] = (s16)(y + r * 2 / 3); p[2] = (s16)(x + r * 2 / 3); p[3] = (s16)(y - r * 2 / 3); polyline(d, w, p, 2, a->m_color, 0);
            break;
        case 4:
            p[0] = (s16)(x - r); p[1] = (s16)(y - r); p[2] = (s16)(x + r); p[3] = (s16)(y - r);
            p[4] = (s16)(x + r); p[5] = (s16)(y + r); p[6] = (s16)(x - r); p[7] = (s16)(y + r);
            polyline(d, w, p, 4, a->m_color, 1);
            break;
        case 5:
            p[0] = (s16)(x - r); p[1] = (s16)(y - r); p[2] = (s16)(x + r); p[3] = (s16)(y + r); polyline(d, w, p, 2, a->m_color, 0);
            p[0] = (s16)(x - r); p[1] = (s16)(y + r); p[2] = (s16)(x + r); p[3] = (s16)(y - r); polyline(d, w, p, 2, a->m_color, 0);
            break;
        default:
            p[0] = (s16)x; p[1] = (s16)(y - r); p[2] = (s16)(x + r); p[3] = (s16)y;
            p[4] = (s16)x; p[5] = (s16)(y + r); p[6] = (s16)(x - r); p[7] = (s16)y;
            polyline(d, w, p, 4, a->m_color, 1);
            break;
        }
    }
    *a = save;
}

/* ---------------- filled areas ---------------- */

static void fill_rect(SoftDev *d, int x0, int y0, int x1, int y1, const Ink *k)
{
    int y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) paint_row(d, y, x0, x1, NULL, 0, 0, 0, k);
}

static void perimeter(SoftDev *d, Wk *w, const s16 *xy, int n)
{
    GAttr save = d->a;
    d->a.l_width = 1; d->a.l_type = 1; d->a.l_beg = d->a.l_end = 0;
    polyline(d, w, xy, n, d->a.f_color, 1);
    d->a = save;
}

static void fill_path_attr(SoftDev *d, Wk *w, Path *P)
{
    Ink k;
    u16 tmp[16];
    ink_fill(w, d, &k, tmp);
    path_fill(d, P, &k);
}

/* outline of a path as a closed polyline (perimeter or arc outline) */
static void path_outline(SoftDev *d, Wk *w, const Path *P, int col, int perim)
{
    int i, s = 0;
    s16 *xy;
    if (P->err || P->n < 2) return;
    xy = (s16 *)heap_alloc((s32)P->n * 4);
    while (!xy && gc_shrink((s32)P->n * 4)) xy = (s16 *)heap_alloc((s32)P->n * 4);
    if (!xy) return;
    for (i = 0; i <= P->n; i++) {
        if (i == P->n || (i > s && P->p[3 * i + 2])) {
            int k, m = i - s;
            for (k = 0; k < m; k++) {
                xy[2 * k] = (s16)FIX2INT(P->p[3 * (s + k)]);
                xy[2 * k + 1] = (s16)FIX2INT(P->p[3 * (s + k) + 1]);
            }
            if (perim) perimeter(d, w, xy, m);
            else polyline(d, w, xy, m, col, 0);
            s = i;
        }
    }
    heap_free(xy);
}

static void rbox_path(Path *P, int x0, int y0, int x1, int y1)
{
    int t, r;
    fix X0, Y0, X1, Y1, R;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    r = XG_MIN((x1 - x0) / 2, (y1 - y0) / 2);
    if (r > 8) r = 8;
    X0 = INT2FIX(x0) + FIX_HALF; Y0 = INT2FIX(y0) + FIX_HALF;
    X1 = INT2FIX(x1) + FIX_HALF; Y1 = INT2FIX(y1) + FIX_HALF; R = INT2FIX(r);
    {
        int i, n = 6;
        static const s16 a0[4] = { 0, 900, 1800, 2700 };
        fix cx[4], cy[4];
        cx[0] = X1 - R; cy[0] = Y0 + R; cx[1] = X0 + R; cy[1] = Y0 + R;
        cx[2] = X0 + R; cy[2] = Y1 - R; cx[3] = X1 - R; cy[3] = Y1 - R;
        for (t = 0; t < 4; t++)
            for (i = 0; i <= n; i++) {
                s32 a = a0[t] + 900 * i / n;
                path_pt(P, cx[t] + fixmul(R, fixcos(a)), cy[t] - fixmul(R, fixsin(a)), t == 0 && i == 0);
            }
        path_close(P, 1);
    }
}

static void gdp(SoftDev *d, Wk *w, VDIPB *pb)
{
    Path P;
    fix cx = INT2FIX(PTSIN[0]) + FIX_HALF, cy = INT2FIX(PTSIN[1]) + FIX_HALF;
    int sub = CONTRL[5];
    path_init(&P);
    switch (sub) {
    case 1: {   /* v_bar */
        Ink k;
        u16 tmp[16];
        s16 p[8];
        ink_fill(w, d, &k, tmp);
        fill_rect(d, PTSIN[0], PTSIN[1], PTSIN[2], PTSIN[3], &k);
        if (d->a.f_perim) {
            p[0] = PTSIN[0]; p[1] = PTSIN[1]; p[2] = PTSIN[2]; p[3] = PTSIN[1];
            p[4] = PTSIN[2]; p[5] = PTSIN[3]; p[6] = PTSIN[0]; p[7] = PTSIN[3];
            perimeter(d, w, p, 4);
        }
        break;
    }
    case 2:     /* v_arc */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[6]), INT2FIX(PTSIN[6]), INTIN[0], INTIN[1], 0, 0);
        path_outline(d, w, &P, d->a.l_color, 0);
        break;
    case 3:     /* v_pieslice */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[6]), INT2FIX(PTSIN[6]), INTIN[0], INTIN[1], 1, 1);
        fill_path_attr(d, w, &P);
        if (d->a.f_perim) path_outline(d, w, &P, 0, 1);
        break;
    case 4:     /* v_circle */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[4]), INT2FIX(PTSIN[4]), 0, 3600, 0, 1);
        fill_path_attr(d, w, &P);
        if (d->a.f_perim) path_outline(d, w, &P, 0, 1);
        break;
    case 5:     /* v_ellipse */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[2]), INT2FIX(PTSIN[3]), 0, 3600, 0, 1);
        fill_path_attr(d, w, &P);
        if (d->a.f_perim) path_outline(d, w, &P, 0, 1);
        break;
    case 6:     /* v_ellarc */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[2]), INT2FIX(PTSIN[3]), INTIN[0], INTIN[1], 0, 0);
        path_outline(d, w, &P, d->a.l_color, 0);
        break;
    case 7:     /* v_ellpie */
        path_ellipse(&P, cx, cy, INT2FIX(PTSIN[2]), INT2FIX(PTSIN[3]), INTIN[0], INTIN[1], 1, 1);
        fill_path_attr(d, w, &P);
        if (d->a.f_perim) path_outline(d, w, &P, 0, 1);
        break;
    case 8:     /* v_rbox */
        rbox_path(&P, PTSIN[0], PTSIN[1], PTSIN[2], PTSIN[3]);
        {   /* closed outline with the line attributes */
            int i;
            s16 *xy = (s16 *)heap_alloc((s32)P.n * 4);
            if (xy && !P.err) {
                for (i = 0; i < P.n; i++) { xy[2 * i] = (s16)FIX2INT(P.p[3 * i]); xy[2 * i + 1] = (s16)FIX2INT(P.p[3 * i + 1]); }
                polyline(d, w, xy, P.n, d->a.l_color, 1);
            }
            heap_free(xy);
        }
        break;
    case 9:     /* v_rfbox */
        rbox_path(&P, PTSIN[0], PTSIN[1], PTSIN[2], PTSIN[3]);
        fill_path_attr(d, w, &P);
        if (d->a.f_perim) path_outline(d, w, &P, 0, 1);
        break;
    }
    path_free(&P);
}

/* ---------------- contour fill ---------------- */

static int pix(SoftDev *d, int x, int y)
{
    return (d->s.bits[(s32)y * d->s.rb + (x >> 3)] >> (7 - (x & 7))) & 1;
}

/* v_contourfill: fill the region around (x,y). index < 0: everything the
 * colour of the seed; otherwise up to pixels of that colour index */
static void contour_fill(SoftDev *d, Wk *w, int x, int y, int index)
{
    s32 mrb = d->s.rb, sz = mrb * d->s.h;
    u8 *m;
    s16 *st;
    int sp = 0, cap = 512, seed, yy;
    Ink k;
    u16 tmp[16];
    if (x < d->cx0 || x > d->cx1 || y < d->cy0 || y > d->cy1) return;
    seed = pix(d, x, y);
    if (index >= 0 && seed == bit_of(index)) return;      /* started on the border */
    m = (u8 *)heap_calloc(sz);
    while (!m && gc_shrink(sz)) m = (u8 *)heap_calloc(sz);
    st = (s16 *)heap_alloc((s32)cap * 4);
    if (!m || !st) { heap_free(m); heap_free(st); return; }
#define INSIDE(X, Y) (!(m[(s32)(Y) * mrb + ((X) >> 3)] & (0x80 >> ((X) & 7))) && \
                      (index < 0 ? pix(d, X, Y) == seed : pix(d, X, Y) != bit_of(index)))
    st[0] = (s16)x; st[1] = (s16)y; sp = 1;
    while (sp > 0) {
        int lx, rx, i, up;
        sp--;
        x = st[2 * sp]; y = st[2 * sp + 1];
        if (!INSIDE(x, y)) continue;
        lx = x; rx = x;
        while (lx > d->cx0 && INSIDE(lx - 1, y)) lx--;
        while (rx < d->cx1 && INSIDE(rx + 1, y)) rx++;
        for (i = lx; i <= rx; i++) m[(s32)y * mrb + (i >> 3)] |= (u8)(0x80 >> (i & 7));
        for (up = -1; up <= 1; up += 2) {
            int ny = y + up, in = 0;
            if (ny < d->cy0 || ny > d->cy1) continue;
            for (i = lx; i <= rx; i++) {
                if (INSIDE(i, ny)) {
                    if (!in) {
                        if (sp >= cap) {
                            s16 *q = (s16 *)heap_realloc(st, (s32)cap * 8);
                            if (!q) break;
                            st = q; cap *= 2;
                        }
                        st[2 * sp] = (s16)i; st[2 * sp + 1] = (s16)ny; sp++;
                        in = 1;
                    }
                } else in = 0;
            }
        }
    }
#undef INSIDE
    heap_free(st);
    ink_fill(w, d, &k, tmp);
    for (yy = d->cy0; yy <= d->cy1; yy++) paint_row(d, yy, d->cx0, d->cx1, m + (s32)yy * mrb, (int)mrb, 0, 0, &k);
    heap_free(m);
}

/* ---------------- raster copies ---------------- */

static u8 rop(int mode, u8 s, u8 dv)
{
    switch (mode & 15) {
    case 0: return 0;
    case 1: return (u8)(s & dv);
    case 2: return (u8)(s & ~dv);
    case 3: return s;
    case 4: return (u8)(~s & dv);
    case 5: return dv;
    case 6: return (u8)(s ^ dv);
    case 7: return (u8)(s | dv);
    case 8: return (u8)~(s | dv);
    case 9: return (u8)~(s ^ dv);
    case 10: return (u8)~dv;
    case 11: return (u8)(s | ~dv);
    case 12: return (u8)~s;
    case 13: return (u8)(~s | dv);
    case 14: return (u8)~(s & dv);
    default: return 0xff;
    }
}

/* resolve an MFDB: fd_addr NULL is the device surface */
static void form(SoftDev *d, const MFDB *f, u8 **bits, s32 *rb, int *w, int *h, int *planes)
{
    if (!f || !f->fd_addr) { *bits = d->s.bits; *rb = d->s.rb; *w = d->s.w; *h = d->s.h; *planes = 1; return; }
    *bits = (u8 *)f->fd_addr; *rb = (s32)f->fd_wdwidth * 2; *w = f->fd_w; *h = f->fd_h;
    *planes = f->fd_nplanes > 0 ? f->fd_nplanes : 1;
}

void soft_cpyfm(SoftDev *d, VDIPB *pb, int transparent)
{
    MFDB *src = (MFDB *)GETPTR(CONTRL, 7), *dst = (MFDB *)GETPTR(CONTRL, 9);
    u8 *sb, *db;
    s32 srb, drb;
    int sw, shh, dw, dh, sp, dp, sx, sy, dx, dy, wd, ht, r, todev = !dst || !dst->fd_addr;
    form(d, src, &sb, &srb, &sw, &shh, &sp);
    form(d, dst, &db, &drb, &dw, &dh, &dp);
    sx = XG_MIN(PTSIN[0], PTSIN[2]); sy = XG_MIN(PTSIN[1], PTSIN[3]);
    dx = XG_MIN(PTSIN[4], PTSIN[6]); dy = XG_MIN(PTSIN[5], PTSIN[7]);
    wd = XG_ABS(PTSIN[2] - PTSIN[0]) + 1; ht = XG_ABS(PTSIN[3] - PTSIN[1]) + 1;
    /* clip to both forms (and to the clip rectangle on the device) */
    if (sx < 0) { dx -= sx; wd += sx; sx = 0; }
    if (sy < 0) { dy -= sy; ht += sy; sy = 0; }
    if (sx + wd > sw) wd = sw - sx;
    if (sy + ht > shh) ht = shh - sy;
    {
        int cx0 = 0, cy0 = 0, cx1 = dw - 1, cy1 = dh - 1;
        if (todev) { cx0 = d->cx0; cy0 = d->cy0; cx1 = d->cx1; cy1 = d->cy1; }
        if (dx < cx0) { sx += cx0 - dx; wd -= cx0 - dx; dx = cx0; }
        if (dy < cy0) { sy += cy0 - dy; ht -= cy0 - dy; dy = cy0; }
        if (dx + wd - 1 > cx1) wd = cx1 - dx + 1;
        if (dy + ht - 1 > cy1) ht = cy1 - dy + 1;
    }
    if (wd <= 0 || ht <= 0) return;
    if (transparent) {
        /* mono source drawn in colours intin[1] (set) and intin[2] (clear) */
        Ink k;
        SoftDev tmp = *d;
        k.mode = (INTIN[0] >= 1 && INTIN[0] <= 4) ? INTIN[0] : 1;
        k.fg = bit_of(INTIN[1]); k.bg = bit_of(INTIN[2]); k.pat = NULL;
        if (!todev) { tmp.s.bits = db; tmp.s.rb = drb; tmp.s.w = dw; tmp.s.h = dh; tmp.cx0 = 0; tmp.cy0 = 0; tmp.cx1 = dw - 1; tmp.cy1 = dh - 1; }
        for (r = 0; r < ht; r++) {
            /* mask bit 0 (pixel dx) is source pixel sx of this row */
            const u8 *s = sb + (s32)(sy + r) * srb;
            u8 rowbuf[64];
            int n = (wd + 7) >> 3, i;
            if (n <= (int)sizeof rowbuf) {
                for (i = 0; i < n; i++) rowbuf[i] = txt_get8(s, (int)srb, sx + i * 8);
                paint_row(&tmp, dy + r, dx, dx + wd - 1, rowbuf, n, dx, 1, &k);
            } else {
                int x;
                for (x = 0; x < wd; x += 256) {
                    int cw = XG_MIN(256, wd - x), cn = (cw + 7) >> 3;
                    for (i = 0; i < cn; i++) rowbuf[i] = txt_get8(s, (int)srb, sx + x + i * 8);
                    paint_row(&tmp, dy + r, dx + x, dx + x + cw - 1, rowbuf, cn, dx + x, 1, &k);
                }
            }
        }
        return;
    }
    {
        int mode = INTIN[0] & 15, pl, planes = XG_MIN(sp, dp);
        int rev = (sb == db && (dy > sy || (dy == sy && dx > sx)));   /* overlapping: copy backwards */
        if (planes < 1) planes = 1;
        for (pl = 0; pl < planes; pl++) {
            u8 *sbase = sb + (s32)pl * srb * shh, *dbase = db + (s32)pl * drb * dh;
            int rr;
            for (rr = 0; rr < ht; rr++) {
                int rrow = rev ? ht - 1 - rr : rr;
                const u8 *s = sbase + (s32)(sy + rrow) * srb;
                u8 *dr = dbase + (s32)(dy + rrow) * drb;
                int b0 = dx >> 3, b1 = (dx + wd - 1) >> 3, bx;
                if (rev && sb == db) {
                    for (bx = b1; bx >= b0; bx--) {
                        u8 edge = 0xff, sv = txt_get8(s, (int)srb, sx + (bx * 8 - dx));
                        if (bx == b0) edge &= (u8)(0xff >> (dx & 7));
                        if (bx == b1) edge &= (u8)(0xff << (7 - ((dx + wd - 1) & 7)));
                        dr[bx] = (u8)((dr[bx] & ~edge) | (rop(mode, sv, dr[bx]) & edge));
                    }
                } else {
                    for (bx = b0; bx <= b1; bx++) {
                        u8 edge = 0xff, sv = txt_get8(s, (int)srb, sx + (bx * 8 - dx));
                        if (bx == b0) edge &= (u8)(0xff >> (dx & 7));
                        if (bx == b1) edge &= (u8)(0xff << (7 - ((dx + wd - 1) & 7)));
                        dr[bx] = (u8)((dr[bx] & ~edge) | (rop(mode, sv, dr[bx]) & edge));
                    }
                }
            }
        }
    }
}

/* ---------------- text ---------------- */

static SoftDev *blit_dev;   /* text blits: set while txt_draw runs (under the lock) */

static void soft_blit(Wk *w, const u8 *bits, int rb, int x, int y, int bw, int bh, int opaque)
{
    Ink k;
    int r;
    k.fg = bit_of(w->t.color); k.bg = 0; k.pat = NULL;
    k.mode = (w->t.wrmode == 1 && !opaque) ? 2 : w->t.wrmode;
    for (r = 0; r < bh; r++) paint_row(blit_dev, y + r, x, x + bw - 1, bits + (s32)r * rb, rb, x, 1, &k);
}

/* System font sizes as the ST VDI offers them: the 6x6, 8x8 and 8x16
 * fonts at 8, 9 and 10 point, and each of them doubled (16, 18, 20). */
const s16 soft_sys_sizes[6][3] = {        /* font, scale, points */
    { 0, 1, 8 }, { 1, 1, 9 }, { 2, 1, 10 }, { 0, 2, 16 }, { 1, 2, 18 }, { 2, 2, 20 }
};

/* the largest size at or below the requested point size (or the smallest) */
static int sys_pick(Wk *w, int *scale)
{
    int i, best = 0;
    fix pt = w->t.pt > 0 ? w->t.pt : INT2FIX(10);
    for (i = 0; i < 6; i++)
        if (INT2FIX(soft_sys_sizes[i][2]) <= pt && soft_sys_sizes[i][2] >= soft_sys_sizes[best][2]) best = i;
    *scale = soft_sys_sizes[best][1];
    return soft_sys_sizes[best][0];
}

int soft_sysfont_metrics(Wk *w, TxtMetrics *m)
{
    int s, f = sys_pick(w, &s);
    const SysFont *F = &xg_sysfonts[f];
    xmemset(m, 0, sizeof *m);
    m->cellw = m->charw = F->w * s;
    m->cellh = F->h * s;
    m->top = F->top * s;            /* the ST font header values */
    m->asc = F->asc * s;
    m->half = F->half * s;
    m->desc = F->desc * s;
    m->bot = F->bot * s;
    m->charh = m->top;
    m->emx = m->emy = INT2FIX(F->h * s);
    return 1;
}

/* draw system font text: build the string as a mask, then paint it */
static void sys_text(SoftDev *d, Wk *w, int x, int y, const s16 *codes, int n, int jlen, int wordsp)
{
    int s, f = sys_pick(w, &s), i, r, cw, ch, bw, bh, mrb, ay, ax = 0, spaces = 0, extra = 0, eff = w->t.effects;
    const SysFont *F = &xg_sysfonts[f];
    TxtMetrics m;
    u8 *mask;
    int *xs;
    Ink k;
    if (n <= 0) return;
    soft_sysfont_metrics(w, &m);
    cw = F->w * s; ch = F->h * s;
    xs = (int *)heap_alloc((s32)(n + 1) * sizeof(int));
    if (!xs) return;
    if (jlen > 0) {
        for (i = 0; i < n; i++) if ((codes[i] & 0xff) == ' ') spaces++;
        extra = jlen - n * cw;
    }
    for (i = 0, r = 0; i <= n; i++) {
        xs[i] = i * cw + r;
        if (i < n && jlen > 0) {
            if (wordsp && spaces) { if ((codes[i] & 0xff) == ' ') r = extra * ++ax / spaces; }
            else if (n > 1) r = extra * (i + 1) / (n - 1 > 0 ? n - 1 : 1);
        }
    }
    bw = xs[n] + ((eff & 1) ? s : 0) + ((eff & 4) ? ch / 2 : 0) + ((eff & 16) ? 2 : 0);
    bh = ch + ((eff & 16) ? 2 : 0);
    mrb = ((bw + 15) >> 4) << 1;
    mask = (u8 *)heap_calloc((s32)mrb * bh);
    while (!mask && gc_shrink((s32)mrb * bh)) mask = (u8 *)heap_calloc((s32)mrb * bh);
    if (!mask) { heap_free(xs); return; }
    for (i = 0; i < n; i++) {
        const u8 *g = F->bits + (s32)(codes[i] & 0xff) * F->h;
        int gy, gx, sx2, sy2, ox = xs[i] + ((eff & 16) ? 1 : 0), oy = (eff & 16) ? 1 : 0;
        for (gy = 0; gy < F->h; gy++)
            for (gx = 0; gx < F->w; gx++)
                if (g[gy] & (0x80 >> gx))
                    for (sy2 = 0; sy2 < s; sy2++)
                        for (sx2 = 0; sx2 < s; sx2++) {
                            int px = ox + gx * s + sx2, py = oy + gy * s + sy2;
                            if (eff & 4) px += (F->top * s - py) / 2 + ch / 4;   /* italic */
                            if (px >= 0 && px < bw && py < bh) mask[(s32)py * mrb + (px >> 3)] |= (u8)(0x80 >> (px & 7));
                        }
    }
    if (eff & 1)                                       /* bold: smear right */
        for (r = 0; r < bh; r++) {
            u8 *row = mask + (s32)r * mrb;
            int b, t;
            for (t = 0; t < s; t++) {
                u8 carry = 0;
                for (b = 0; b < mrb; b++) { u8 v = row[b]; row[b] = (u8)(v | (v >> 1) | carry); carry = (u8)(v << 7); }
            }
        }
    if (eff & 16) {                                    /* outlined: dilate, then cut out the inside */
        u8 *o = (u8 *)heap_calloc((s32)mrb * bh);
        if (o) {
            int yy, xx;
            for (yy = 0; yy < bh; yy++)
                for (xx = 0; xx < bw; xx++) {
                    int any = 0, dy2, dx2, self = (mask[(s32)yy * mrb + (xx >> 3)] >> (7 - (xx & 7))) & 1;
                    for (dy2 = -1; dy2 <= 1 && !any; dy2++)
                        for (dx2 = -1; dx2 <= 1; dx2++) {
                            int X = xx + dx2, Y = yy + dy2;
                            if (X >= 0 && X < bw && Y >= 0 && Y < bh && ((mask[(s32)Y * mrb + (X >> 3)] >> (7 - (X & 7))) & 1)) { any = 1; break; }
                        }
                    if (any && !self) o[(s32)yy * mrb + (xx >> 3)] |= (u8)(0x80 >> (xx & 7));
                }
            heap_free(mask);
            mask = o;
        }
    }
    if (eff & 2)                                       /* light: half the pixels */
        for (r = 0; r < bh; r++) {
            u8 pat = (u8)((r & 1) ? 0xaa : 0x55), *row = mask + (s32)r * mrb;
            int b;
            for (b = 0; b < mrb; b++) row[b] &= pat;
        }
    if (eff & 8) {                                     /* underline below the baseline */
        int uy = XG_MIN(bh - 1, F->top * s + s + ((eff & 16) ? 1 : 0)), t;
        for (t = 0; t < s && uy + t < bh; t++) {
            int xx;
            for (xx = 0; xx < bw; xx++) mask[(s32)(uy + t) * mrb + (xx >> 3)] |= (u8)(0x80 >> (xx & 7));
        }
    }
    /* alignment */
    ay = txt_valign(w, &m);
    if (w->t.halign == 1) x -= xs[n] / 2;
    else if (w->t.halign == 2) x -= xs[n];
    y += ay - F->top * s - ((eff & 16) ? 1 : 0);
    k.fg = bit_of(w->t.color); k.bg = 0; k.pat = NULL; k.mode = w->t.wrmode;
    if (w->t.rot == 0) {
        for (r = 0; r < bh; r++) paint_row(d, y + r, x, x + bw - 1, mask + (s32)r * mrb, mrb, x, 1, &k);
    } else {
        /* 90 degree steps, about the text position */
        int rot = (w->t.rot + 450) / 900 % 4, xx, ox = x, oy = y - ay + F->top * s;
        for (r = 0; r < bh; r++)
            for (xx = 0; xx < bw; xx++) {
                int on = (mask[(s32)r * mrb + (xx >> 3)] >> (7 - (xx & 7))) & 1, px, py, rx = xx + (x - ox), ry = r + (y - oy);
                if (!on && k.mode != 1) continue;
                switch (rot) {
                case 1: px = ox + ry; py = oy - rx; break;
                case 2: px = ox - rx; py = oy - ry; break;
                case 3: px = ox - ry; py = oy + rx; break;
                default: px = ox + rx; py = oy + ry; break;
                }
                plot(d, px, py, on, &k);
            }
    }
    heap_free(mask);
    heap_free(xs);
}

static void text_out(SoftDev *d, Wk *w, int x, int y, const s16 *codes, int n, int frac,
                     int jlen, int wordsp, int charsp, const s16 *offs)
{
    if (w->t.fi >= 0) {
        blit_dev = d;
        if (txt_draw(w, x, y, codes, n, frac, jlen, wordsp, charsp, offs, soft_blit)) return;
    }
    sys_text(d, w, x, y, codes, n, jlen, wordsp);
}

/* ---------------- dispatcher ---------------- */

int soft_vdi(SoftDev *d, Wk *w, VDIPB *pb)
{
    int op = CONTRL[0];
    soft_clip(d, w);
    switch (op) {
    case 6:     /* v_pline */
        polyline(d, w, PTSIN, CONTRL[1], d->a.l_color, 0);
        return 1;
    case 7:     /* v_pmarker */
        markers(d, w, PTSIN, CONTRL[1]);
        return 1;
    case 8:     /* v_gtext */
        text_out(d, w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 0, 0, 0, 0, NULL);
        return 1;
    case 241:   /* v_ftext, v_ftext_offset */
        text_out(d, w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 1, 0, 0, 0, CONTRL[1] > 1 ? PTSIN + 2 : NULL);
        return 1;
    case 9: {   /* v_fillarea */
        Path P;
        int n = CONTRL[1];
        if (n < 2) return 1;
        path_init(&P);
        path_poly(&P, PTSIN, n, 0);
        fill_evenodd = 1;
        fill_path_attr(d, w, &P);
        fill_evenodd = 0;
        path_free(&P);
        if (d->a.f_perim) perimeter(d, w, PTSIN, n);
        return 1;
    }
    case 11:
        if (CONTRL[5] == 10) {      /* v_justified */
            text_out(d, w, PTSIN[0], PTSIN[1], INTIN + 2, CONTRL[3] - 2, 0, PTSIN[2], INTIN[0], INTIN[1], NULL);
            return 1;
        }
        gdp(d, w, pb);
        return 1;
    case 114: { /* vr_recfl: no perimeter */
        Ink k;
        u16 tmp[16];
        ink_fill(w, d, &k, tmp);
        fill_rect(d, PTSIN[0], PTSIN[1], PTSIN[2], PTSIN[3], &k);
        return 1;
    }
    case 103:   /* v_contourfill */
        contour_fill(d, w, PTSIN[0], PTSIN[1], INTIN[0]);
        return 1;
    case 105:   /* v_get_pixel */
        if (PTSIN[0] >= 0 && PTSIN[0] < d->s.w && PTSIN[1] >= 0 && PTSIN[1] < d->s.h) {
            INTOUT[0] = (s16)pix(d, PTSIN[0], PTSIN[1]);
            INTOUT[1] = INTOUT[0];
        } else INTOUT[0] = INTOUT[1] = 0;
        CONTRL[4] = 2;
        return 1;
    case 109:   /* vro_cpyfm */
        soft_cpyfm(d, pb, 0);
        return 1;
    case 121:   /* vrt_cpyfm */
        soft_cpyfm(d, pb, 1);
        return 1;
    case 110: { /* vr_trnfm: one plane, standard == device format */
        MFDB *s = (MFDB *)GETPTR(CONTRL, 7), *t = (MFDB *)GETPTR(CONTRL, 9);
        if (s && t && s->fd_addr && t->fd_addr && s->fd_addr != t->fd_addr)
            xmemcpy(t->fd_addr, s->fd_addr, (s32)s->fd_wdwidth * 2 * s->fd_h * (s->fd_nplanes > 0 ? s->fd_nplanes : 1));
        if (t) t->fd_stand = (s16)(s && s->fd_stand ? 0 : 1);
        return 1;
    }
    }
    return 0;
}
