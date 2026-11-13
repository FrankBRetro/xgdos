/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* spd.c - Bitstream Speedo outline font decoder
 *
 * File layout (all header words big-endian):
 *   0      "D1.0\r\n\0\0" (or D4.0)
 *   8      font size (long)          12  min font buffer (long)
 *   18     header size (word)        20  source font id
 *   24     font name [70]            252 chars in layout, 254 chars in font
 *   256    first char index          258 kern tracks, 260 kern pairs
 *   266    short font name [32]      298 short face name [16]
 *   330    orus per em               332 word/em/en/thin/fig space widths
 *   342    xmin ymin xmax ymax       350 underline pos, thickness
 *   hdrsize: private header: orumx, pixmx, custno, then 3-byte offsets
 *          char dir, constraints, track kern, pair kern, char data, size
 *
 * Keyed fonts: 3-byte offsets are stored (mid^km, high^kh, low^kl);
 * character data words are little-endian XOR k32; variable length
 * constraint bytes XOR k7. km/kh/kl come from the private "font size"
 * copy, k32 from the space glyph, k7 by trial.
 */
#include "spd.h"

static void xg_clear16(u16 *p, int n) { while (n-- > 0) *p++ = 0; }
static void xmemset_(void *p, s32 n) { u8 *q = (u8 *)p; while (n-- > 0) *q++ = 0; }

#define HW(p)  ((u16)(((u16)d[p] << 8) | d[p + 1]))
#define HS(p)  ((s16)HW(p))

static s32 rl3(const SpdFont *f, s32 p)
{
    const u8 *d = f->d;
    return ((s32)(d[p] ^ f->km) << 8) | ((s32)(d[p + 1] ^ f->kh) << 16) | (s32)(d[p + 2] ^ f->kl);
}

static void hcopy(char *dst, const u8 *src, int n)
{
    int i;
    for (i = 0; i < n && src[i]; i++) dst[i] = (char)src[i];
    dst[i] = 0;
    while (i > 0 && dst[i - 1] == ' ') dst[--i] = 0;
}

int spd_probe(const u8 *d, s32 len, SpdInfo *info)
{
    if (len < 360) return 0;
    if (d[0] != 'D' || d[2] != '.' || d[4] != 13 || d[5] != 10) return 0;
    hcopy(info->name, d + 24, 70);
    hcopy(info->sname, d + 266, 32);
    hcopy(info->face, d + 298, 16);
    /* menus show at most 32 characters: drop a trailing "(TM)", and if
     * the full name is still too long use the short name with spaces
     * ("Transitional 801 Bitstream Charter Black Italic (TM)" ->
     * "Bitstream Charter Black Italic") */
    {
        char *n = info->name;
        int k = 0;
        while (n[k]) k++;
        if (k >= 5 && n[k - 1] == ')' && n[k - 2] == 'M' && n[k - 3] == 'T' && n[k - 4] == '(') {
            k -= 4;
            while (k > 0 && n[k - 1] == ' ') k--;
            n[k] = 0;
        }
        if (k > 32 && info->sname[0]) {
            for (k = 0; info->sname[k] && k < 71; k++) n[k] = info->sname[k] == '-' ? ' ' : info->sname[k];
            n[k] = 0;
        }
    }
    info->bsid = HS(20);
    info->orupm = HW(330);
    info->ymin = HS(344);
    info->ymax = HS(348);
    info->fsize = ((s32)HW(8) << 16) | HW(10);
    if (info->orupm == 0) return 0;
    return 1;
}

/* ---------------- character data reader ---------------- */

typedef struct Rd {
    const SpdFont *f;
    const u8 *p, *end;
    int err;
} Rd;

static int rb(Rd *r)
{
    if (r->p >= r->end) { r->err = 1; return 0; }
    return *r->p++;
}

static s32 rw(Rd *r)
{
    s32 v;
    if (r->p + 2 > r->end) { r->err = 1; return 0; }
    v = (s32)(u16)((r->p[0] | ((u16)r->p[1] << 8)) ^ r->f->k32);
    r->p += 2;
    return (v & 0x8000) ? v - 0x10000 : v;
}

static s32 rsb(Rd *r)
{
    s32 v = rb(r);
    return (v & 0x80) ? v - 256 : v;
}

static void rskip(Rd *r, int n)
{
    if (r->p + n > r->end) { r->err = 1; r->p = r->end; }
    else r->p += n;
}

static const u8 *char_ptr(const SpdFont *f, int idx, const u8 **end)
{
    s32 a, b;
    if (idx < 0 || idx >= f->nchars) return NULL;
    if (f->dirfmt) {
        a = rl3(f, f->dir + 1 + 3 * idx);
        b = rl3(f, f->dir + 1 + 3 * (idx + 1));
    } else {
        const u8 *d = f->d;
        s32 p = f->dir + 1 + 2 * idx;
        a = (u16)((d[p] | (d[p + 1] << 8)) ^ f->k32);
        b = (u16)((d[p + 2] | (d[p + 3] << 8)) ^ f->k32);
    }
    if (a <= 0 || b <= a || b > f->size) return NULL;
    *end = f->d + b;
    return f->d + a;
}

#define MAXEDGE 48

typedef struct Axis {
    s16 orus[MAXEDGE];
    int n;                 /* entries in orus[] */
    s16 se[MAXEDGE];       /* sorted unique edges */
    fix sp[MAXEDGE];       /* hinted pixel positions for se[] */
    int ns;
    fix scale;
    int hint;
    s32 stemmax;           /* gaps up to this (orus) keep a rounded width */
    fix ph;                /* sub-pixel offset applied before rounding */
} Axis;

static void axis_read(Rd *r, Axis *a, int cnt)
{
    int k, zin = 1;
    a->n = 0;
    for (k = 0; k < cnt; k++) {
        s32 o = rw(r);
        if (zin && o >= 0) {
            if (a->n < MAXEDGE) a->orus[a->n++] = 0;
            zin = 0;
        }
        if (a->n < MAXEDGE) a->orus[a->n++] = (s16)o;
    }
    if (zin && a->n < MAXEDGE) a->orus[a->n++] = 0;
}

/* Build the grid-fitting table: controlled edges are rounded to whole
 * pixels; everything between them is interpolated (Speedo "plaid"). */
static void axis_prepare(Axis *a, fix scale, int hint, fix ph)
{
    int i, j;
    a->scale = scale;
    a->hint = hint != 0;
    a->ph = ph;
    a->ns = 0;
    if (!hint) return;
    for (i = 0; i < a->n; i++) {
        s16 v = a->orus[i];
        for (j = 0; j < a->ns && a->se[j] < v; j++) ;
        if (j < a->ns && a->se[j] == v) continue;
        if (a->ns >= MAXEDGE) break;
        {
            int k;
            for (k = a->ns; k > j; k--) a->se[k] = a->se[k - 1];
        }
        a->se[j] = v;
        a->ns++;
    }
    /* Round edges anchored at the zero edge (baseline / origin), working
     * outwards so overshoots never move the baseline. A short gap (a stem,
     * or the narrow counter beside one) gets its width rounded once and is
     * placed relative to its neighbour, so equal stems always come out the
     * same number of pixels: no letter looks heavier than the next. Long
     * gaps snap to the absolute grid so overall widths never drift. */
    if (hint == 1) {
        /* shape-safe: move the whole outline by under half a pixel so the
         * first stem starts on a pixel boundary; it then always fills
         * round(width) pixels, whatever its position in the line */
        fix dx = 0;
        int z = 0;
        for (i = 0; i < a->ns; i++) if (a->se[i] == 0) z = i;
        for (i = z + 2; i < a->ns; i++) {
            s32 g = a->se[i] - a->se[i - 1];
            if (g <= a->stemmax && (fix)g * scale >= FIX_HALF) {
                fix e = (fix)a->se[i - 1] * scale + ph;
                dx = ((e + FIX_HALF) & ~0xffffL) - e;
                break;
            }
        }
        if (dx == 0 && i >= a->ns) {        /* no stem: plain rounding */
            fix e = ph;
            dx = ((e + FIX_HALF) & ~0xffffL) - e;
        }
        for (i = 0; i < a->ns; i++) a->sp[i] = (fix)a->se[i] * scale + ph + dx;
        return;
    }
    {
        int z = 0, paired = 0;
        for (i = 0; i < a->ns; i++) {
            a->sp[i] = ((fix)a->se[i] * scale + ph + FIX_HALF) & ~0xffffL;
            if (a->se[i] == 0) z = i;
        }
        for (i = z + 1; i < a->ns; i++) {
            fix prev = a->sp[i - 1];
            s32 g = a->se[i] - a->se[i - 1];
            fix d = (fix)g * scale;
            if (g <= a->stemmax && !paired && i - 1 != z) {
                fix w = (d + FIX_HALF) & ~0xffffL;
                if (w < FIX_ONE && d >= FIX_ONE / 4) w = FIX_ONE;
                a->sp[i] = prev + w;
                paired = 1;
            } else {
                paired = 0;
                if (a->sp[i] < prev) a->sp[i] = prev;
                if (a->sp[i] == prev && d >= FIX_HALF) a->sp[i] = prev + FIX_ONE;
            }
        }
        paired = 0;
        for (i = z - 1; i >= 0; i--) {
            fix next = a->sp[i + 1];
            s32 g = a->se[i + 1] - a->se[i];
            fix d = (fix)g * scale;
            if (g <= a->stemmax && !paired && i + 1 != z) {
                fix w = (d + FIX_HALF) & ~0xffffL;
                if (w < FIX_ONE && d >= FIX_ONE / 4) w = FIX_ONE;
                a->sp[i] = next - w;
                paired = 1;
            } else {
                paired = 0;
                if (a->sp[i] > next) a->sp[i] = next;
                if (a->sp[i] == next && d >= FIX_HALF) a->sp[i] = next - FIX_ONE;
            }
        }
    }
}

static fix axis_map(const Axis *a, s32 v)
{
    int i;
    if (!a->hint || a->ns == 0) return v * a->scale + a->ph;
    if (v <= a->se[0]) return a->sp[0] + (v - a->se[0]) * a->scale;
    for (i = 1; i < a->ns; i++) {
        if (v <= a->se[i]) {
            s32 e0 = a->se[i - 1], e1 = a->se[i];
            fix p0 = a->sp[i - 1], p1 = a->sp[i];
            if (e1 == e0) return p0;
            return p0 + muldiv(p1 - p0, v - e0, e1 - e0);
        }
    }
    return a->sp[a->ns - 1] + (v - a->se[a->ns - 1]) * a->scale;
}

typedef struct Ctx {
    Rd r;
    Axis ax, ay;
    s32 lx, ly;                /* last decoded orus */
    const SpdXform *xf;
    SpdSink *sink;
    fix offx, offy;            /* composite sub-glyph offset (px) */
    s32 cscale_x, cscale_y;    /* composite scale (4096 = 1.0) */
    fix lastx, lasty;          /* last output point */
    s32 stemmax;               /* stem-width threshold in orus */
    u8 *mark;                  /* if set: bit per glyph index decoded */
} Ctx;

static void get_args(Ctx *c, int fmt, s32 *px, s32 *py)
{
    s32 x = c->lx, y = c->ly;
    int i;
    switch (fmt & 3) {
    case 0: i = rb(&c->r); x = (i < c->ax.n) ? c->ax.orus[i] : 0; if (i >= c->ax.n) c->r.err = 1; break;
    case 1: x = rw(&c->r); break;
    case 2: x = c->lx + rsb(&c->r); break;
    default: break;
    }
    switch ((fmt >> 2) & 3) {
    case 0: i = rb(&c->r); y = (i < c->ay.n) ? c->ay.orus[i] : 0; if (i >= c->ay.n) c->r.err = 1; break;
    case 1: y = rw(&c->r); break;
    case 2: y = c->ly + rsb(&c->r); break;
    default: break;
    }
    c->lx = x; c->ly = y;
    *px = x; *py = y;
}

static void xform(Ctx *c, s32 x, s32 y, fix *ox, fix *oy)
{
    const SpdXform *xf = c->xf;
    fix X, Y, t;
    if (xf->raw) {
        if (c->cscale_x != 4096) x = x * c->cscale_x / 4096;
        if (c->cscale_y != 4096) y = y * c->cscale_y / 4096;
        *ox = INT2FIX(x) + c->offx;
        *oy = INT2FIX(y) + c->offy;
        return;
    }
    if (c->cscale_x != 4096 || c->cscale_y != 4096) {
        X = muldiv(x * xf->sx, c->cscale_x, 4096);
        Y = muldiv(y * xf->sy, c->cscale_y, 4096);
    } else {
        X = axis_map(&c->ax, x);
        Y = axis_map(&c->ay, y);
    }
    X += c->offx;
    Y += c->offy;
    if (xf->skew) X += fixmul(Y, xf->skew);
    if (xf->rotated) {
        t = fixmul(X, xf->m00) + fixmul(Y, xf->m01);
        Y = fixmul(X, xf->m10) + fixmul(Y, xf->m11);
        X = t;
    }
    *ox = X + xf->ox;
    *oy = Y + xf->oy;
}

static void bbox_skip(Ctx *c)
{
    int f1, f2;
    s32 x, y;
    f1 = rb(&c->r);
    get_args(c, f1 & 15, &x, &y);
    get_args(c, ((f1 >> 4) & 3) | 0x0c, &x, &y);
    f2 = rb(&c->r);
    get_args(c, f2 & 15, &x, &y);
    get_args(c, f2 >> 4, &x, &y);
}

static const u8 intsize[9] = { 1, 2, 3, 1, 2, 1, 2, 0, 0 };

static int read_plaid(Ctx *c, int fmt)
{
    int nx, ny, n, i, ni;
    nx = (fmt & 4) ? rb(&c->r) : 0;
    ny = (fmt & 8) ? rb(&c->r) : 0;
    axis_read(&c->r, &c->ax, nx);
    axis_read(&c->r, &c->ay, ny);
    n = c->ax.n + c->ay.n - 2;
    for (i = 0; i < n; i++) {
        int v;
        rskip(&c->r, (fmt & 0x10) ? 1 : 2);
        v = rb(&c->r) ^ c->r.f->k7;
        if (v >= 248) rskip(&c->r, 1);
    }
    ni = ((fmt & 0x40) ? rb(&c->r) : 0);
    ni += ((fmt & 0x80) ? rb(&c->r) : 0);
    for (i = 0; i < ni; i++) {
        int g = rb(&c->r);
        if (g & 0x80) rskip(&c->r, 1);
        else rskip(&c->r, intsize[g & 7] + intsize[(g >> 3) & 7]);
    }
    return !c->r.err;
}

static int do_char(const SpdFont *f, int idx, Ctx *c, int depth);

static int do_simple(Ctx *c, int fmt)
{
    int op, open = 0;
    s32 x, y, x1, y1, x2, y2;
    fix X, Y, X1, Y1, X2, Y2;
    const SpdXform *xf = c->xf;
    int hint = xf->hint && !xf->raw && c->cscale_x == 4096 && c->cscale_y == 4096;

    if (!read_plaid(c, fmt)) return 0;
    c->ax.stemmax = c->ay.stemmax = c->stemmax;
    /* x: ON aligns the first stem (shape-safe), FULL fits every stem pair */
    axis_prepare(&c->ax, xf->sx, hint ? (xf->hint >= 2 ? 2 : 1) : 0, xf->phx);
    axis_prepare(&c->ay, xf->sy, hint ? 2 : 0, 0);
    c->lx = c->ly = 0;
    bbox_skip(c);
    c->lx = c->ly = 0;
    for (;;) {
        if (c->r.err) return 0;
        op = rb(&c->r);
        switch (op >> 4) {
        case 0:
            get_args(c, op, &x, &y);
            if (open) { xform(c, x, y, &X, &Y); c->sink->line(c->sink, X, Y); }
            break;
        case 1: case 2:
            break;
        case 3:
            if ((op & 15) == 0) {
                if (open) c->sink->close(c->sink);
                return !c->r.err;
            }
            if ((op & 15) == 1 || (op & 15) == 2) rb(&c->r);
            break;
        case 4: case 5:
            get_args(c, op, &x, &y);
            if (open) c->sink->close(c->sink);
            xform(c, x, y, &X, &Y);
            c->sink->move(c->sink, X, Y);
            open = 1;
            break;
        case 6: case 7:
            break;
        default: {
            int f2 = rb(&c->r);
            get_args(c, op & 15, &x1, &y1);
            get_args(c, f2 & 15, &x2, &y2);
            get_args(c, f2 >> 4, &x, &y);
            if (open) {
                xform(c, x1, y1, &X1, &Y1);
                xform(c, x2, y2, &X2, &Y2);
                xform(c, x, y, &X, &Y);
                c->sink->curve(c->sink, X1, Y1, X2, Y2, X, Y);
            }
            break;
        }
        }
    }
}

static int do_composite(const SpdFont *f, Ctx *c, int depth)
{
    int fmt;
    c->ax.n = 1; c->ax.orus[0] = 0;
    c->ay.n = 1; c->ay.orus[0] = 0;
    c->lx = c->ly = 0;
    bbox_skip(c);
    for (;;) {
        s32 xp = 0, yp = 0, xs = 4096, ys = 4096;
        int sub;
        Ctx sc;
        if (c->r.err) return 0;
        fmt = rb(&c->r);
        if (!(fmt & 0x80)) break;
        switch (fmt & 3) { case 1: xp = rw(&c->r); break; case 2: xp = rsb(&c->r); break; }
        switch ((fmt >> 2) & 3) { case 1: yp = rw(&c->r); break; case 2: yp = rsb(&c->r); break; }
        if (fmt & 0x10) xs = rw(&c->r);
        if (fmt & 0x20) ys = rw(&c->r);
        sub = (fmt & 0x40) ? (int)(u16)rw(&c->r) : rb(&c->r);
        if (c->r.err) return 0;
        sc = *c;
        if (c->xf->raw) {
            sc.offx = c->offx + INT2FIX(xp);
            sc.offy = c->offy + INT2FIX(yp);
        } else {
            fix ox = xp * c->xf->sx, oy = yp * c->xf->sy;
            if (c->xf->hint) { ox = (ox + FIX_HALF) & ~0xffffL; oy = (oy + FIX_HALF) & ~0xffffL; }
            sc.offx = c->offx + ox;
            sc.offy = c->offy + oy;
        }
        sc.cscale_x = c->cscale_x * xs / 4096;
        sc.cscale_y = c->cscale_y * ys / 4096;
        if (!do_char(f, sub, &sc, depth + 1)) return 0;
    }
    return 1;
}

static int do_char(const SpdFont *f, int idx, Ctx *c, int depth)
{
    const u8 *end, *p;
    int fmt;
    if (depth > 4) return 0;
    if (c->mark && idx >= 0 && idx < f->nchars) c->mark[idx >> 3] |= (u8)(1 << (idx & 7));
    p = char_ptr(f, idx, &end);
    if (!p) return 0;
    c->r.f = f; c->r.p = p; c->r.end = end; c->r.err = 0;
    rw(&c->r);              /* char id */
    rw(&c->r);              /* set width */
    fmt = rb(&c->r);
    if (fmt & 2) rskip(&c->r, rb(&c->r));
    if (fmt & 1) return do_composite(f, c, depth);
    return do_simple(c, fmt);
}

int spd_char_outline(const SpdFont *f, int idx, const SpdXform *xf, SpdSink *s)
{
    Ctx c;
    c.xf = xf;
    c.sink = s;
    c.stemmax = (s32)f->orupm * 3 / 20;      /* 0.15 em */
    c.mark = NULL;
    c.offx = c.offy = 0;
    c.cscale_x = c.cscale_y = 4096;
    return do_char(f, idx, &c, 0);
}

/* ---------------- null sink used for validation and bbox ------------- */

typedef struct BBSink {
    SpdSink s;
    fix x0, y0, x1, y1;
    int any;
} BBSink;

static void bb_pt(BBSink *b, fix x, fix y)
{
    if (!b->any) { b->x0 = b->x1 = x; b->y0 = b->y1 = y; b->any = 1; return; }
    if (x < b->x0) b->x0 = x; if (x > b->x1) b->x1 = x;
    if (y < b->y0) b->y0 = y; if (y > b->y1) b->y1 = y;
}
static void bb_move(SpdSink *s, fix x, fix y) { bb_pt((BBSink *)s, x, y); }
static void bb_curve(SpdSink *s, fix a, fix b, fix c2, fix d, fix e, fix f)
{
    bb_pt((BBSink *)s, a, b); bb_pt((BBSink *)s, c2, d); bb_pt((BBSink *)s, e, f);
}
static void bb_close(SpdSink *s) { (void)s; }

int spd_char_bbox(const SpdFont *f, int idx, s16 *bb)
{
    BBSink b;
    SpdXform xf;
    b.s.move = bb_move; b.s.line = bb_move; b.s.curve = bb_curve; b.s.close = bb_close;
    b.any = 0;
    xf.raw = 1; xf.hint = 0; xf.rotated = 0; xf.skew = 0; xf.phx = 0;
    xf.sx = xf.sy = FIX_ONE; xf.ox = xf.oy = 0;
    if (!spd_char_outline(f, idx, &xf, &b.s)) return 0;
    if (!b.any) { bb[0] = bb[1] = bb[2] = bb[3] = 0; return 1; }
    bb[0] = (s16)FIX2INT(b.x0); bb[1] = (s16)FIX2INT(b.y0);
    bb[2] = (s16)FIX2INT(b.x1); bb[3] = (s16)FIX2INT(b.y1);
    return 1;
}

/* ---------------- compaction ---------------- */

/* mark idx and every glyph it is built from (composites) */
void spd_mark_glyph(const SpdFont *f, int idx, u8 *bits)
{
    BBSink b;
    SpdXform xf;
    Ctx c;
    if (idx < 0 || idx >= f->nchars) return;
    b.s.move = bb_move; b.s.line = bb_move; b.s.curve = bb_curve; b.s.close = bb_close;
    b.any = 0;
    xmemset_(&xf, sizeof xf);
    xf.raw = 1; xf.sx = xf.sy = FIX_ONE;
    xmemset_(&c, sizeof c);
    c.xf = &xf; c.sink = &b.s; c.cscale_x = c.cscale_y = 4096; c.mark = bits;
    do_char(f, idx, &c, 0);
}

int spd_has_glyph(const SpdFont *f, int idx)
{
    const u8 *end;
    return char_ptr(f, idx, &end) != NULL;
}

static void wdir(SpdFont *f, u8 *d, int idx, s32 v)
{
    if (f->dirfmt) {
        s32 p = f->dir + 1 + 3 * idx;
        d[p] = (u8)(((v >> 8) & 0xff) ^ f->km);
        d[p + 1] = (u8)(((v >> 16) & 0xff) ^ f->kh);
        d[p + 2] = (u8)((v & 0xff) ^ f->kl);
    } else {
        s32 p = f->dir + 1 + 2 * idx;
        u16 w = (u16)(v ^ f->k32);
        d[p] = (u8)w; d[p + 1] = (u8)(w >> 8);
    }
}

/* Drop the glyphs not marked in keep, in place: everything before the
 * glyph data (header, directory, kerning) stays, kept glyphs move down
 * in index order and the directory is rewritten with the font's own key.
 * Returns the new size, or 0 (font untouched) if the layout is unusual. */
s32 spd_compact(SpdFont *f, u8 *data, const u8 *keep)
{
    s32 pos, a, b, prev = 0, i;
    if (data != f->d || f->chars <= f->dir || f->trk > f->chars || f->pairk > f->chars ||
        f->cons > f->chars) return 0;
    if (!f->dirfmt && f->size > 65535L) return 0;
    for (i = 0; i <= f->nchars; i++) {            /* offsets must ascend */
        a = f->dirfmt ? rl3(f, f->dir + 1 + 3 * i)
                      : (u16)((data[f->dir + 1 + 2 * i] | (data[f->dir + 2 + 2 * i] << 8)) ^ f->k32);
        if (a < prev || a > f->size || (i == 0 && a < f->chars)) return 0;
        prev = a;
    }
    pos = f->chars;
    a = f->dirfmt ? rl3(f, f->dir + 1) : (u16)((data[f->dir + 1] | (data[f->dir + 2] << 8)) ^ f->k32);
    for (i = 0; i < f->nchars; i++) {
        b = f->dirfmt ? rl3(f, f->dir + 1 + 3 * (i + 1))
                      : (u16)((data[f->dir + 3 + 2 * i] | (data[f->dir + 4 + 2 * i] << 8)) ^ f->k32);
        wdir(f, data, i, pos);
        if (keep[i >> 3] & (1 << (i & 7))) {
            s32 k;
            for (k = 0; k < b - a; k++) data[pos + k] = data[a + k];   /* pos <= a */
            pos += b - a;
        }
        a = b;
    }
    wdir(f, data, f->nchars, pos);
    f->size = pos;
    return pos;
}

/* ---------------- open / metrics ---------------- */

u16 spd_char_id(const SpdFont *f, int idx)
{
    const u8 *end, *p = char_ptr(f, idx, &end);
    Rd r;
    if (!p) return 0xffff;
    r.f = f; r.p = p; r.end = end; r.err = 0;
    return (u16)rw(&r);
}

s32 spd_char_width(const SpdFont *f, int idx)
{
    const u8 *end, *p = char_ptr(f, idx, &end);
    Rd r;
    if (!p) return -1;
    r.f = f; r.p = p; r.end = end; r.err = 0;
    rw(&r);
    return rw(&r);
}

/* The constraint byte key: in every font the most common constraint is 0,
 * so the most frequent raw byte of the first control zone entry of each
 * glyph is the key. */
static int guess_k7(SpdFont *f)
{
    u16 cnt[256];
    int i, best = -1, n = f->nchars < 300 ? f->nchars : 300;
    xg_clear16(cnt, 256);
    for (i = 1; i < n; i++) {
        const u8 *end, *p = char_ptr(f, i, &end);
        int fmt, nx, ny;
        if (!p || end - p < 12) continue;
        p += 4;
        fmt = *p++;
        if (fmt & 1) continue;                 /* composite */
        if (fmt & 2) p += 1 + *p;
        nx = (fmt & 4) ? *p++ : 0;
        ny = (fmt & 8) ? *p++ : 0;
        if (nx + ny == 0) continue;
        p += 2 * (nx + ny) + ((fmt & 0x10) ? 1 : 2);
        if (p < end) cnt[*p]++;
    }
    for (i = 0; i < 256; i++) if (best < 0 || cnt[i] > cnt[best]) best = i;
    return (best >= 0 && cnt[best]) ? best : -1;
}

/* Check a key by decoding a sample of glyphs spread over the whole font:
 * every glyph program must end exactly at the next directory entry. */
static int check_keys(SpdFont *f)
{
    int i, step = f->nchars > 240 ? f->nchars / 120 : 1;
    BBSink b;
    SpdXform xf;
    b.s.move = bb_move; b.s.line = bb_move; b.s.curve = bb_curve; b.s.close = bb_close;
    xf.raw = 1; xf.hint = 0; xf.rotated = 0; xf.skew = 0; xf.phx = 0;
    xf.sx = xf.sy = FIX_ONE; xf.ox = xf.oy = 0;
    for (i = 1; i < f->nchars; i += (i < 96 ? 1 : step)) {
        Ctx c;
        const u8 *end;
        b.any = 0;
        c.stemmax = 0; c.mark = NULL; c.xf = &xf; c.sink = &b.s; c.offx = c.offy = 0; c.cscale_x = c.cscale_y = 4096;
        if (!char_ptr(f, i, &end)) {
            /* an empty slot (start == end) is a glyph the font leaves out */
            s32 x, y;
            if (f->dirfmt) { x = rl3(f, f->dir + 1 + 3 * i); y = rl3(f, f->dir + 4 + 3 * i); }
            else {
                x = (u16)((f->d[f->dir + 1 + 2 * i] | (f->d[f->dir + 2 + 2 * i] << 8)) ^ f->k32);
                y = (u16)((f->d[f->dir + 3 + 2 * i] | (f->d[f->dir + 4 + 2 * i] << 8)) ^ f->k32);
            }
            if (x == y && x > 0 && x <= f->size) continue;
            return 0;
        }
        if (!do_char(f, i, &c, 0)) return 0;
        if (!char_ptr(f, i, &end) || c.r.p != end) return 0;
    }
    return 1;
}

static int validate_k7(SpdFont *f, int full)
{
    static const u8 tries[] = { 0xda, 0x77, 0x00 };
    int t, g = guess_k7(f);
    if (g >= 0) { f->k7 = (u8)g; if (check_keys(f)) return 1; }
    for (t = 0; t < 3; t++) {
        f->k7 = tries[t];
        if (check_keys(f)) return 1;
    }
    if (full)
        for (t = 0; t < 256; t++) {
            f->k7 = (u8)t;
            if (check_keys(f)) return 1;
        }
    f->k7 = 0xda;
    return 0;
}

/* offset of character 0 for a candidate data key (2 byte directories are
 * keyed like character data) */
static s32 char0(SpdFont *f, u16 k32)
{
    const u8 *d = f->d;
    if (f->dirfmt) return rl3(f, f->dir + 1);
    return (u16)((d[f->dir + 1] | (d[f->dir + 2] << 8)) ^ k32);
}

int spd_open(SpdFont *f, const u8 *d, s32 len)
{
    s32 size, h2, p;
    u16 rawid, raww, k;
    int i;
    f->err = 1;
    if (len < SPD_HDR_MIN) return 0;
    f->err = 2;
    if (d[0] != 'D' || d[4] != 13 || d[5] != 10) return 0;
    f->d = d;
    size = ((s32)HW(8) << 16) | HW(10);
    f->size = (size > 0 && size <= len) ? size : len;   /* tolerate padding/truncation */
    f->err = 4;
    h2 = HW(18);
    if (h2 < 360 || h2 + 24 > len) return 0;
    /* offset keys: the private header repeats the font size ... */
    p = h2 + 21;
    f->km = d[p] ^ (u8)(size >> 8);
    f->kh = d[p + 1] ^ (u8)(size >> 16);
    f->kl = d[p + 2] ^ (u8)size;
    f->dir = rl3(f, h2 + 6);
    if (f->dir != h2 + 24 && (f->dir <= h2 || f->dir >= f->size)) {
        /* ... or, if that copy differs, the character directory follows
         * the private header directly */
        s32 want = h2 + 24;
        f->km = d[h2 + 6] ^ (u8)(want >> 8);
        f->kh = d[h2 + 7] ^ (u8)(want >> 16);
        f->kl = d[h2 + 8] ^ (u8)want;
        f->dir = rl3(f, h2 + 6);
    }
    size = f->size;
    f->cons = rl3(f, h2 + 9);
    f->trk = rl3(f, h2 + 12);
    f->pairk = rl3(f, h2 + 15);
    f->chars = rl3(f, h2 + 18);
    f->nchars = HW(254);
    f->first = HW(256);
    f->ntracks = HW(258);
    f->npairs = HW(260);
    f->orupm = HW(330);
    f->wordspace = HS(332);
    f->emspace = HS(334);
    f->xmin = HS(342); f->ymin = HS(344); f->xmax = HS(346); f->ymax = HS(348);
    f->ul_pos = HS(350); f->ul_thk = HS(352);
    f->bsid = HS(20);
    f->idmap = NULL;
    f->err = 5;
    if (f->dir <= 0 || f->dir >= size || f->nchars == 0 || f->orupm == 0) return 0;
    f->dirfmt = d[f->dir];
    f->err = 6;
    if (f->dir + 1 + (f->dirfmt ? 3 : 2) * (s32)(f->nchars + 1) > size) return 0;
    f->err = 7;
    /* Find the data word key: from the space glyph if possible, else the
     * Atari key, else no key. Each candidate must decode the first glyphs
     * exactly; a font that decodes with none of them is rejected rather
     * than drawn as garbage. */
    {
        u16 cand[4];
        int nc = 0, c;
        s32 a;
        /* 2 byte directories are keyed like the data: entry 0 must be the
         * character data offset from the private header */
        if (!f->dirfmt && f->chars > 0)
            cand[nc++] = (u16)((d[f->dir + 1] | (d[f->dir + 2] << 8)) ^ (u16)f->chars);
        a = char0(f, nc ? cand[0] : 0x0056);
        if (a > 0 && a + 4 <= size) {
            rawid = (u16)(d[a] | (d[a + 1] << 8));
            raww = (u16)(d[a + 2] | (d[a + 3] << 8));
            k = raww ^ (u16)f->wordspace;
            if ((u16)(rawid ^ k) == 32 && !(nc && cand[0] == k)) cand[nc++] = k;
        }
        cand[nc++] = 0x0056;
        cand[nc++] = 0x0000;
        f->ok_keys = 0;
        for (c = 0; c < nc && !f->ok_keys; c++) {
            f->k32 = cand[c];
            a = char0(f, f->k32);
            if (a <= 0 || a >= size) continue;
            if (validate_k7(f, c == 0)) f->ok_keys = 1;
        }
        if (!f->ok_keys) return 0;
    }
    f->err = 0;
    (void)i;
    return 1;
}

int spd_build_idmap(SpdFont *f, u16 *buf)
{
    int i, j, n = f->nchars;
    for (i = 0; i < n; i++) {
        u16 id = spd_char_id(f, i);
        /* insertion sort by id; fonts are nearly sorted already */
        for (j = i; j > 0 && buf[2 * (j - 1)] > id; j--) {
            buf[2 * j] = buf[2 * (j - 1)];
            buf[2 * j + 1] = buf[2 * (j - 1) + 1];
        }
        buf[2 * j] = id;
        buf[2 * j + 1] = (u16)i;
    }
    f->idmap = buf;
    return 1;
}

int spd_index_of_id(const SpdFont *f, u16 id)
{
    int lo = 0, hi = f->nchars - 1;
    if (!f->idmap) return (id < f->nchars) ? id : -1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        u16 v = f->idmap[2 * m];
        if (v == id) return f->idmap[2 * m + 1];
        if (v < id) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

/* Pair kerning: format byte, then entries (left,right,adjust). bit0 set:
 * char indices are words, bit1 set: adjustment is a word. Entries are
 * sorted by left then right index. */
s32 spd_pair_kern(const SpdFont *f, int left, int right)
{
    Rd r;
    int fmt, esz, lo, hi;
    if (!f->npairs || f->pairk <= 0) return 0;
    r.f = f; r.err = 0;
    fmt = f->d[f->pairk];
    esz = ((fmt & 1) ? 4 : 2) + ((fmt & 2) ? 2 : 1);
    lo = 0; hi = f->npairs - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1, a, b;
        r.p = f->d + f->pairk + 1 + m * esz;
        r.end = r.p + esz;
        if (fmt & 1) { a = (u16)rw(&r); b = (u16)rw(&r); }
        else { a = rb(&r); b = rb(&r); }
        if (a == left && b == right) return (fmt & 2) ? rw(&r) : rsb(&r);
        if (a < left || (a == left && b < right)) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

/* Track kerning: per track a format byte followed by min point size,
 * min kern, max point size, max kern; bit n of the format makes field n
 * a word, otherwise a byte. Kern values are 1/256 point. */
s32 spd_track_kern(const SpdFont *f, int track, s32 pt)
{
    Rd r;
    int t, fmt, i;
    s32 v[4];
    if (track < 1 || track > f->ntracks || f->trk <= 0) return 0;
    r.f = f; r.err = 0;
    r.p = f->d + f->trk;
    r.end = f->d + f->pairk;
    for (t = 1; t <= track; t++) {
        fmt = rb(&r);
        for (i = 0; i < 4; i++)
            v[i] = (fmt & (1 << i)) ? rw(&r) : ((i & 1) ? rsb(&r) : rb(&r));
    }
    if (r.err) return 0;
    {
        fix p0 = INT2FIX(v[0]), p1 = INT2FIX(v[2]);
        s32 k;
        if (pt <= p0) k = v[1];
        else if (pt >= p1) k = v[3];
        else k = v[1] + muldiv(v[3] - v[1], pt - p0, p1 - p0);
        return k << 8;    /* 1/256 pt -> 16.16 pt */
    }
}
