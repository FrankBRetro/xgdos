/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* raster.c - scanline rasteriser for outline glyphs */
#include "raster.h"

#define SUB 16   /* sub-pixel units per pixel */
/* non-zero winding, or even-odd (any crossing toggles inside/outside) */
#define WIND(r, w, d) ((r)->evenodd ? !(w) : (w) + (d))

static void add_seg(Raster *r, s32 x0, s32 y0, s32 x1, s32 y1)
{
    s32 *q;
    if (y0 == y1) return;            /* horizontal edges never cross a centre */
    if ((y0 > y1 ? y0 : y1) <= r->wy0 || (y0 < y1 ? y0 : y1) >= r->wy1) return;   /* outside the window */
    r->need++;
    if (r->nseg >= r->maxseg) { r->overflow = 1; return; }
    q = r->seg + 4 * r->nseg++;
    q[0] = x0; q[1] = y0; q[2] = x1; q[3] = y1;
}

static void track(Raster *r, s32 x, s32 y)
{
    if (x < r->x0) r->x0 = x; if (x > r->x1) r->x1 = x;
    if (y < r->y0) r->y0 = y; if (y > r->y1) r->y1 = y;
}

static s32 cvx(const Raster *r, fix x) { return (r->orgx + x) >> 12; }
static s32 cvy(const Raster *r, fix y) { return (r->orgy - y) >> 12; }

static void lineto(Raster *r, s32 x, s32 y)
{
    add_seg(r, r->cx, r->cy, x, y);
    r->cx = x; r->cy = y;
    track(r, x, y);
}

static void rs_close(SpdSink *s)
{
    Raster *r = (Raster *)s;
    if (r->open && (r->cx != r->sx || r->cy != r->sy)) lineto(r, r->sx, r->sy);
    r->open = 0;
}

static void rs_move(SpdSink *s, fix x, fix y)
{
    Raster *r = (Raster *)s;
    rs_close(s);
    r->cx = r->sx = cvx(r, x);
    r->cy = r->sy = cvy(r, y);
    r->open = 1;
    track(r, r->cx, r->cy);
}

static void rs_line(SpdSink *s, fix x, fix y)
{
    Raster *r = (Raster *)s;
    lineto(r, cvx(r, x), cvy(r, y));
}

static void bez(Raster *r, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, s32 x3, s32 y3, int depth)
{
    s32 d1x = x0 - 2 * x1 + x2, d1y = y0 - 2 * y1 + y2;
    s32 d2x = x1 - 2 * x2 + x3, d2y = y1 - 2 * y2 + y3;
    s32 d = XG_MAX(XG_MAX(XG_ABS(d1x), XG_ABS(d1y)), XG_MAX(XG_ABS(d2x), XG_ABS(d2y)));
    if (depth >= 10 || d <= SUB / 4) {
        lineto(r, x3, y3);
        return;
    }
    {
        s32 ax = (x0 + x1) >> 1, ay = (y0 + y1) >> 1;
        s32 bx = (x1 + x2) >> 1, by = (y1 + y2) >> 1;
        s32 cx = (x2 + x3) >> 1, cy = (y2 + y3) >> 1;
        s32 abx = (ax + bx) >> 1, aby = (ay + by) >> 1;
        s32 bcx = (bx + cx) >> 1, bcy = (by + cy) >> 1;
        s32 mx = (abx + bcx) >> 1, my = (aby + bcy) >> 1;
        bez(r, x0, y0, ax, ay, abx, aby, mx, my, depth + 1);
        bez(r, mx, my, bcx, bcy, cx, cy, x3, y3, depth + 1);
    }
}

static void rs_curve(SpdSink *s, fix x1, fix y1, fix x2, fix y2, fix x3, fix y3)
{
    Raster *r = (Raster *)s;
    bez(r, r->cx, r->cy, cvx(r, x1), cvy(r, y1), cvx(r, x2), cvy(r, y2), cvx(r, x3), cvy(r, y3), 0);
}

void raster_init(Raster *r, s32 *segbuf, int maxseg, fix orgx, fix orgy)
{
    r->sink.move = rs_move;
    r->sink.line = rs_line;
    r->sink.curve = rs_curve;
    r->sink.close = rs_close;
    r->seg = segbuf;
    r->maxseg = maxseg;
    r->nseg = 0;
    r->need = 0;
    r->evenodd = 0;
    r->wy0 = -0x7fffffff;
    r->wy1 = 0x7fffffff;
    r->open = 0;
    r->overflow = 0;
    r->dropout = 0;
    r->x0 = r->y0 = 0x7fffffff;
    r->x1 = r->y1 = -0x7fffffff;
    r->orgx = orgx;
    r->orgy = orgy;
    r->cx = r->cy = r->sx = r->sy = 0;
}

static s32 fdiv(s32 a, s32 b)   /* floor(a/b), b > 0 */
{
    s32 q = a / b;
    if ((a % b) && (a < 0)) q--;
    return q;
}

int raster_bounds(const Raster *r, int *px0, int *py0, int *px1, int *py1)
{
    if (r->x0 > r->x1) { *px0 = *py0 = *px1 = *py1 = 0; return 0; }
    *px0 = fdiv(r->x0, SUB);
    *py0 = fdiv(r->y0, SUB);
    *px1 = fdiv(r->x1 + SUB - 1, SUB) + 1;
    *py1 = fdiv(r->y1 + SUB - 1, SUB) + 1;
    return 1;
}

typedef struct Edge {
    s32 x, step, rem, err, dy;  /* x in 1/16 px at current row centre */
    s32 r0, r1;                 /* first row, last row + 1 */
    s32 dir;
} Edge;

typedef struct Cross { s32 x; s32 dir; } Cross;

s32 raster_seg_bytes(void) { return 16 + (s32)(sizeof(Edge) + sizeof(Cross)); }

s32 raster_tmp_size(const Raster *r)
{
    return (s32)r->nseg * (s32)(sizeof(Edge) + sizeof(Cross)) + 16;
}

void raster_span(u8 *row, int p0, int p1)
{
    int b0, b1;
    if (p1 <= p0) return;
    b0 = p0 >> 3; b1 = (p1 - 1) >> 3;
    if (b0 == b1) {
        row[b0] |= (u8)((0xff >> (p0 & 7)) & (0xff << (7 - ((p1 - 1) & 7))));
        return;
    }
    row[b0] |= (u8)(0xff >> (p0 & 7));
    for (b0++; b0 < b1; b0++) row[b0] = 0xff;
    row[b1] |= (u8)(0xff << (7 - ((p1 - 1) & 7)));
}

int raster_fill(const Raster *r, u8 *bits, int rowbytes, int bx, int by, int w, int h,
                void *tmp, s32 tmpsize)
{
    Edge *e = (Edge *)tmp;
    Cross *cr;
    int ne = 0, i, row;
    s32 ry0 = by, ry1 = by + h;

    if (raster_tmp_size(r) > tmpsize) return 0;
    cr = (Cross *)(e + r->nseg);

    for (i = 0; i < r->nseg; i++) {
        const s32 *q = r->seg + 4 * i;
        s32 x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], dir = 1, dx, dy, ra, rb, yc, num;
        Edge *E;
        if (y0 > y1) { s32 t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
        /* rows whose centre (row*16+8) lies in [y0, y1) */
        ra = fdiv(y0 - SUB / 2 + SUB - 1, SUB);
        rb = fdiv(y1 - SUB / 2 + SUB - 1, SUB);
        if (ra < ry0) ra = ry0;
        if (rb > ry1) rb = ry1;
        if (ra >= rb) continue;
        dx = x1 - x0; dy = y1 - y0;
        E = e + ne++;
        E->r0 = ra; E->r1 = rb; E->dir = dir; E->dy = dy;
        yc = ra * SUB + SUB / 2;
        num = (yc - y0) * dx;
        E->x = x0 + fdiv(num, dy);
        E->err = num - fdiv(num, dy) * dy;
        num = dx * SUB;
        E->step = fdiv(num, dy);
        E->rem = num - E->step * dy;
    }

    for (row = by; row < by + h; row++) {
        int nc = 0, wind = 0, j;
        u8 *line = bits + (s32)(row - by) * rowbytes;
        for (i = 0; i < ne; i++) {
            Edge *E = e + i;
            if (row < E->r0 || row >= E->r1) continue;
            /* insertion into sorted crossing list */
            for (j = nc; j > 0 && cr[j - 1].x > E->x; j--) cr[j] = cr[j - 1];
            cr[j].x = E->x; cr[j].dir = E->dir;
            nc++;
            E->x += E->step;
            E->err += E->rem;
            if (E->err >= E->dy) { E->x++; E->err -= E->dy; }
        }
        for (j = 0; j < nc; j++) {
            int was = wind;
            wind = WIND(r, wind, cr[j].dir);
            if (was == 0 && wind != 0 && j + 1 < nc) {
                /* span starts here; find where winding returns to zero */
                int k = j + 1, ww = wind;
                s32 xa = cr[j].x, xb, p0, p1;
                while (k < nc) { ww = WIND(r, ww, cr[k].dir); if (ww == 0) break; k++; }
                if (k >= nc) k = nc - 1;
                xb = cr[k].x;
                p0 = fdiv(xa - SUB / 2 + SUB - 1, SUB) - bx;
                p1 = fdiv(xb - SUB / 2 + SUB - 1, SUB) - bx;
                if (p1 <= p0 && r->dropout) {
                    /* stroke thinner than a pixel: keep the nearest pixel */
                    p0 = fdiv((xa + xb) >> 1, SUB) - bx;
                    p1 = p0 + 1;
                }
                if (p0 < 0) p0 = 0;
                if (p1 > w) p1 = w;
                if (p1 > p0) raster_span(line, (int)p0, (int)p1);
                wind = ww;
                j = k;
            }
        }
    }
    if (r->dropout) {
        /* the same for horizontal strokes: scan the pixel columns */
        int col;
        for (col = bx; col < bx + w; col++) {
            s32 xc = col * SUB + SUB / 2;
            int nc = 0, j, wind = 0;
            for (i = 0; i < r->nseg; i++) {
                const s32 *q = r->seg + 4 * i;
                s32 x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], y, dir;
                if (x0 == x1) continue;
                if (x0 < x1) { if (xc < x0 || xc >= x1) continue; dir = 1; }
                else { if (xc < x1 || xc >= x0) continue; dir = -1; }
                y = y0 + (xc - x0) * (y1 - y0) / (x1 - x0);
                for (j = nc; j > 0 && cr[j - 1].x > y; j--) cr[j] = cr[j - 1];
                cr[j].x = y; cr[j].dir = dir;
                nc++;
            }
            for (j = 0; j < nc; j++) {
                int was = wind;
                wind = WIND(r, wind, cr[j].dir);
                if (was == 0 && wind != 0 && j + 1 < nc) {
                    int k = j + 1, ww = wind;
                    s32 ya = cr[j].x, yb, r0, r1;
                    while (k < nc) { ww = WIND(r, ww, cr[k].dir); if (ww == 0) break; k++; }
                    if (k >= nc) k = nc - 1;
                    yb = cr[k].x;
                    r0 = fdiv(ya - SUB / 2 + SUB - 1, SUB);
                    r1 = fdiv(yb - SUB / 2 + SUB - 1, SUB);
                    if (r1 <= r0) {
                        s32 row = fdiv((ya + yb) >> 1, SUB);
                        if (row >= by && row < by + h) {
                            u8 *line = bits + (s32)(row - by) * rowbytes;
                            int px = col - bx;
                            line[px >> 3] |= (u8)(0x80 >> (px & 7));
                        }
                    }
                    wind = ww;
                    j = k;
                }
            }
        }
    }
    return 1;
}
