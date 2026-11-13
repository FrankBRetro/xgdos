/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* text.c - outline text engine: metrics, layout, effects, rendering */
#include "text.h"
#include "gcache.h"
#include "heap.h"
#include "raster.h"
#include "scr.h"
#include "xstd.h"
#include "xos.h"

#define STRIP_BYTES 8192

/* pixels per inch (16.16) derived from the pixel size in micrometres */
static fix ppi(int um) { return um > 0 ? fixdiv(INT2FIX(25400), INT2FIX(um)) : INT2FIX(72); }

static fix ppy(Wk *w) { return w->ppiy ? w->ppiy : ppi(w->ph); }
static fix ppx(Wk *w) { return w->ppix ? w->ppix : ppi(w->pw); }
fix txt_pt2px_y(Wk *w, fix pt) { return muldiv(pt, ppy(w), INT2FIX(72)); }
fix txt_pt2px_x(Wk *w, fix pt) { return muldiv(pt, ppx(w), INT2FIX(72)); }
fix txt_px2pt_y(Wk *w, fix px) { return muldiv(px, INT2FIX(72), ppy(w)); }

static fix clampem(Wk *w, fix em)
{
    fix lim = (w->kind == WK_PDF) ? INT2FIX(20000) : INT2FIX(2000);
    if (em < FIX_ONE) em = FIX_ONE;
    if (em > lim) em = lim;
    return em;
}

int txt_metrics(Wk *w, TxtMetrics *m)
{
    XFont *f;
    s32 a;
    if (w->t.fi < 0) return 0;
    f = fm_get(w->t.fi);
    if (!f) return 0;
    m->f = f;
    m->emy = clampem(w, txt_pt2px_y(w, w->t.pt));
    m->emx = clampem(w, txt_pt2px_x(w, w->t.wpt ? w->t.wpt : w->t.pt));
    m->sy = m->emy / f->orupm;
    m->sx = m->emx / f->orupm;
    a = w->t.skew + ((w->t.effects & 4) ? 120 : 0);
    if (a > 800) a = 800;
    if (a < -800) a = -800;
    m->skew = a ? fixdiv(fixsin(a), fixcos(a)) : 0;
    m->top = FIXROUND(f->m_top * m->sy);
    m->asc = FIXROUND(f->m_asc * m->sy);
    m->half = FIXROUND(f->m_half * m->sy);
    m->desc = FIXROUND(f->m_desc * m->sy);
    m->bot = FIXROUND(f->m_bot * m->sy);
    m->bold = (w->t.effects & 1) ? XG_MAX(1, FIX2INT(m->emy) / 20) : 0;
    m->outl = (w->t.effects & 16) ? 1 : 0;
    m->cellw = FIXROUND(f->m_maxw * m->sx) + m->bold + 2 * m->outl;
    m->charw = m->cellw;
    m->charh = m->top;
    m->cellh = m->top + m->bot + 2 * m->outl;
    m->hint = ((w->kind != WK_PDF) && (w->t.rot % 900) == 0) ? xg_cfg.hint : 0;
    return 1;
}

int txt_layout(Wk *w, TxtMetrics *m, const s16 *codes, int n, int frac, TxtGlyph **out, fix *adv)
{
    XFont *f = m->f;
    TxtGlyph *g;
    fix pen = 0, track = 0, a;
    int i, prev = -1;
    *out = NULL;
    *adv = 0;
    if (n <= 0) return 0;
    g = (TxtGlyph *)heap_alloc((s32)n * (s32)sizeof(TxtGlyph));
    if (!g) return -1;
    if (w->t.track) {
        track = txt_pt2px_x(w, spd_track_kern(&f->spd, w->t.track, w->t.pt));
        if (!frac) track = INT2FIX(FIXROUND(track));
    }
    for (i = 0; i < n; i++) {
        int code = w->t.charmap ? (codes[i] & 0xff) : (codes[i] & 0xffff);
        int idx = fm_glyph(f, code, w->t.charmap);
        if (w->t.pairs && prev >= 0 && idx >= 0) {
            fix k = spd_pair_kern(&f->spd, prev, idx) * m->sx;
            pen += frac ? k : INT2FIX(FIXROUND(k));
        }
        g[i].idx = (s16)idx;
        g[i].code = (s16)code;
        g[i].x = pen;
        a = (idx >= 0) ? spd_char_width(&f->spd, idx) * m->sx : 0;
        if (a < 0) a = 0;
        if (!frac) a = INT2FIX(FIXROUND(a));
        a += INT2FIX(m->bold + 2 * m->outl) + track;
        pen += a;
        prev = idx;
    }
    *adv = pen;
    *out = g;
    return n;
}

void txt_justify(TxtGlyph *g, int n, fix *adv, fix want, int wordsp, int charsp)
{
    int i, gaps = 0, k = 0;
    fix extra = want - *adv;
    if (n < 2) return;
    if (wordsp) for (i = 0; i < n - 1; i++) if (g[i].code == ' ') gaps++;
    if (!gaps) { if (!charsp) return; wordsp = 0; gaps = n - 1; }
    for (i = 1; i < n; i++) {
        if (!wordsp || g[i - 1].code == ' ') k++;
        g[i].x += muldiv(extra, k, gaps);
    }
    *adv = want;
}

int txt_valign(Wk *w, TxtMetrics *m)
{
    switch (w->t.valign) {
    case 1: return m->half;
    case 2: return m->asc;
    case 3: return -m->bot;
    case 4: return -m->desc;
    case 5: return m->top;
    default: return 0;
    }
}

void txt_extent(Wk *w, TxtMetrics *m, fix adv, s16 *pts)
{
    s32 wd = FIXROUND(adv), ht = m->cellh;
    s32 cx[4], cy[4], minx, miny;
    fix c = fixcos(w->t.rot), s = fixsin(w->t.rot);
    int i;
    /* text space: x along baseline, y up; corners LL, LR, UR, UL */
    s32 tx[4], ty[4];
    tx[0] = 0;  ty[0] = 0;
    tx[1] = wd; ty[1] = 0;
    tx[2] = wd; ty[2] = ht;
    tx[3] = 0;  ty[3] = ht;
    for (i = 0; i < 4; i++) {
        cx[i] = FIXROUND(tx[i] * c - ty[i] * s);
        cy[i] = FIXROUND(tx[i] * s + ty[i] * c);
    }
    minx = XG_MIN(XG_MIN(cx[0], cx[1]), XG_MIN(cx[2], cx[3]));
    miny = XG_MIN(XG_MIN(cy[0], cy[1]), XG_MIN(cy[2], cy[3]));
    for (i = 0; i < 4; i++) { pts[2 * i] = (s16)(cx[i] - minx); pts[2 * i + 1] = (s16)(cy[i] - miny); }
}

/* ---------------- bit helpers ---------------- */

u8 txt_get8(const u8 *s, int nbytes, int bit)
{
    int b = bit >> 3, sh = bit & 7;
    u16 v = 0;
    if (b >= 0 && b < nbytes) v = (u16)(s[b] << 8);
    if (b + 1 >= 0 && b + 1 < nbytes) v |= s[b + 1];
    return (u8)(v >> (8 - sh));
}

/* OR sw source bits into d (dw bits wide) starting at bit dx */
static void or_row(u8 *d, int dw, int dx, const u8 *s, int srb, int sw)
{
    int x0 = dx, x1 = dx + sw, j, j0, j1;
    if (x0 < 0) x0 = 0;
    if (x1 > dw) x1 = dw;
    if (x1 <= x0) return;
    j0 = x0 >> 3; j1 = (x1 - 1) >> 3;
    for (j = j0; j <= j1; j++) {
        u8 v = txt_get8(s, srb, j * 8 - dx), mask = 0xff;
        if (j == j0) mask &= (u8)(0xff >> (x0 & 7));
        if (j == j1) mask &= (u8)(0xff << (7 - ((x1 - 1) & 7)));
        d[j] |= (u8)(v & mask);
    }
}

static void smear(u8 *row, int rb, int k)
{
    while (k-- > 0) {
        int i;
        for (i = rb - 1; i >= 0; i--)
            row[i] = (u8)(row[i] | (row[i] >> 1) | (i ? (row[i - 1] << 7) : 0));
    }
}

/* ---------------- flat (unrotated) text ---------------- */

typedef struct Box { int x0, y0, x1, y1; } Box;

static int clip_box(Wk *w, Box *b)
{
    int cx0 = 0, cy0 = 0, cx1 = w->xres + 1, cy1 = w->yres + 1;
    if (w->clip_on) {
        cx0 = XG_MAX(cx0, w->clip[0]); cy0 = XG_MAX(cy0, w->clip[1]);
        cx1 = XG_MIN(cx1, w->clip[2] + 1); cy1 = XG_MIN(cy1, w->clip[3] + 1);
    }
    if (b->x0 < cx0) b->x0 = cx0;
    if (b->y0 < cy0) b->y0 = cy0;
    if (b->x1 > cx1) b->x1 = cx1;
    if (b->y1 > cy1) b->y1 = cy1;
    return b->x1 > b->x0 && b->y1 > b->y0;
}

/* render a glyph that is too large for the cache straight into a strip */
static void direct_glyph(Wk *w, TxtMetrics *m, int idx, int px, int ph, int base,
                         u8 *bits, int rb, int bx, int by, int bw, int bh)
{
    Raster r;
    SpdXform xf;
    Scr sc;
    (void)w;
    xmemset(&xf, 0, sizeof xf);
    xf.sx = m->sx; xf.sy = m->sy; xf.skew = m->skew; xf.hint = (s16)m->hint;
    xf.phx = (fix)ph << 14;
    scr_init(&sc);
    for (;;) {
        raster_init(&r, sc.seg, sc.maxseg, INT2FIX(px), INT2FIX(base));
        r.dropout = FIX2INT(m->emy) <= 40;
        r.wy0 = (s32)by * 16 - 16;          /* only this strip's rows matter */
        r.wy1 = (s32)(by + bh) * 16 + 16;
        if (!spd_char_outline(&m->f->spd, idx, &xf, &r.sink)) { scr_done(&sc); return; }
        if (!r.overflow) break;
        if (sc.own || !scr_grow(&sc, r.need)) { scr_done(&sc); return; }
    }
    raster_fill(&r, bits, rb, bx, by, bw, bh, sc.tmp, sc.tmpsize);
    scr_done(&sc);
}

static void effects(Wk *w, TxtMetrics *m, u8 *buf, int rb, int bw, int rows, int y0,
                    int ulx0, int ulx1, int base)
{
    int r;
    /* rows: buf holds rows y0-1 .. y0+rows (one margin row each side) */
    if (m->bold)
        for (r = 0; r < rows + 2; r++) smear(buf + (s32)r * rb, rb, m->bold);
    if (m->outl) {
        u8 *tmp = (u8 *)heap_alloc((s32)rb * (rows + 2));
        if (tmp) {
            int i;
            xmemcpy(tmp, buf, (s32)rb * (rows + 2));
            for (r = 1; r <= rows; r++) {
                u8 *o = buf + (s32)r * rb;
                const u8 *a = tmp + (s32)(r - 1) * rb, *b = tmp + (s32)r * rb, *c = tmp + (s32)(r + 1) * rb;
                for (i = 0; i < rb; i++) {
                    u8 v = (u8)(a[i] | b[i] | c[i]);
                    u8 l = (u8)((v << 1) | (i + 1 < rb ? ((a[i + 1] | b[i + 1] | c[i + 1]) >> 7) : 0));
                    u8 rr = (u8)((v >> 1) | (i ? ((a[i - 1] | b[i - 1] | c[i - 1]) << 7) : 0));
                    o[i] = (u8)((v | l | rr) & ~b[i]);
                }
            }
            heap_free(tmp);
        }
    }
    if (w->t.effects & 8) {
        int ut = XG_MAX(1, FIXROUND(m->f->spd.ul_thk * m->sy));
        int uy = base + XG_MAX(1, FIXROUND(-m->f->spd.ul_pos * m->sy)) - ut / 2;
        for (r = 1; r <= rows; r++) {
            int y = y0 + r - 1;
            if (y >= uy && y < uy + ut && ulx1 > ulx0)
                raster_span(buf + (s32)r * rb, XG_MAX(0, ulx0), XG_MIN(bw, ulx1));
        }
    }
    if (w->t.effects & 2)
        for (r = 1; r <= rows; r++) {
            u8 *o = buf + (s32)r * rb, pat = ((y0 + r - 1) & 1) ? 0xaa : 0x55;
            int i;
            for (i = 0; i < rb; i++) o[i] &= pat;
        }
}

static int draw_flat(Wk *w, TxtMetrics *m, TxtGlyph *g, int n, fix adv, int x, int y,
                     fix ax, int ay, const s16 *offs, TxtBlit blit)
{
    Box b;
    int i, base = y + ay, rb, sh, sy, bw, opaque = (w->t.wrmode == 1);
    fix pen0 = INT2FIX(x) + ax;
    u8 *buf;
    s32 nbytes;

    /* glyph pixel positions and bounding box */
    b.x0 = FIXROUND(pen0);
    b.x1 = b.x0 + FIXROUND(adv);
    b.y0 = base - m->top - m->outl;
    b.y1 = base + m->bot + m->outl;
    for (i = 0; i < n; i++) {
        Glyph *gl;
        int px = FIXROUND(pen0 + g[i].x), py = base, ph = 0;
        if (m->hint) {
            /* keep the quarter-pixel phase: the glyph is fitted to the grid
             * from where it really sits, so spacing stays even */
            fix p = pen0 + g[i].x + 0x2000;
            px = FIX2INT(p); ph = (int)((p >> 14) & 3);
        }
        if (offs) { px += offs[2 * i]; py += offs[2 * i + 1]; }
        g[i].x = INT2FIX(px) | ((fix)ph << 14);   /* device x + phase */
        g[i].code = (s16)(py - base);      /* reuse: vertical offset */
        if (g[i].idx < 0) continue;
        gl = gc_get(m->f - xg_font, g[i].idx, m->sx, m->sy, m->skew, m->hint | (ph << 8));
        if (gl) {
            b.x0 = XG_MIN(b.x0, px + gl->left - m->outl);
            b.x1 = XG_MAX(b.x1, px + gl->left + gl->w + m->bold + m->outl);
            b.y0 = XG_MIN(b.y0, py + gl->top - m->outl);
            b.y1 = XG_MAX(b.y1, py + gl->top + gl->h + m->outl);
        } else {
            int ext = FIX2INT(m->emy) + 2;
            b.x0 = XG_MIN(b.x0, px - ext / 2);
            b.x1 = XG_MAX(b.x1, px + ext * 2);
            b.y0 = XG_MIN(b.y0, py - ext * 2);
            b.y1 = XG_MAX(b.y1, py + ext);
        }
    }
    if (!clip_box(w, &b)) { XLOG("text: clipped away\r\n"); return 1; }
    bw = b.x1 - b.x0;
    rb = ((bw + 15) >> 4) << 1;
    sh = (int)(STRIP_BYTES / rb) - 2;
    if (sh < 1) sh = 1;
    if (sh > b.y1 - b.y0) sh = b.y1 - b.y0;
    nbytes = (s32)rb * (sh + 2);
    buf = (u8 *)heap_alloc(nbytes);
    while (!buf && gc_shrink(nbytes)) buf = (u8 *)heap_alloc(nbytes);
    if (!buf) { XLOG("text: no memory for strip\r\n"); return 0; }
#ifdef XG_LOG
    {
        static int nl;
        if (nl < 40) {
            char t[64];
            int q = 0;
            nl++;
            xstrcpy(t, "text box "); q = 9;
            q += xitoa(t + q, b.x0); t[q++] = ','; q += xitoa(t + q, b.y0); t[q++] = ' ';
            q += xitoa(t + q, bw); t[q++] = 'x'; q += xitoa(t + q, b.y1 - b.y0);
            t[q++] = ' '; t[q++] = 'c'; q += xitoa(t + q, w->t.color);
            t[q++] = ' '; t[q++] = 'm'; q += xitoa(t + q, w->t.wrmode);
            t[q++] = '\r'; t[q++] = '\n'; t[q] = 0;
            XLOG(t);
        }
    }
#endif

    for (sy = b.y0; sy < b.y1; sy += sh) {
        int rows = XG_MIN(sh, b.y1 - sy);
        int ry0 = sy - 1;          /* first buffer row (margin) */
        xmemset(buf, 0, nbytes);
        for (i = 0; i < n; i++) {
            Glyph *gl;
            int px = FIX2INT(g[i].x), py = base + g[i].code, r;
            int ph = (int)((g[i].x >> 14) & 3);
            if (g[i].idx < 0) continue;
            gl = gc_get(m->f - xg_font, g[i].idx, m->sx, m->sy, m->skew, m->hint | (ph << 8));
            if (!gl) {
                direct_glyph(w, m, g[i].idx, px, ph, py, buf, rb, b.x0, ry0, bw, rows + 2);
                continue;
            }
            for (r = 0; r < gl->h; r++) {
                int dy = py + gl->top + r - ry0;
                if (dy < 0) continue;
                if (dy >= rows + 2) break;
                or_row(buf + (s32)dy * rb, bw, px + gl->left - b.x0, gl->bits + (s32)r * gl->rb, gl->rb, gl->w);
            }
        }
        effects(w, m, buf, rb, bw, rows, sy, FIXROUND(pen0) - b.x0, FIXROUND(pen0 + adv) - b.x0, base);
        blit(w, buf + rb, rb, b.x0, sy, bw, rows, opaque);
    }
    heap_free(buf);
    return 1;
}

/* ---------------- rotated text ---------------- */

static void rect_outline(SpdSink *s, fix x0, fix y0, fix x1, fix y1, fix c, fix sn, fix ox, fix oy)
{
    /* rectangle in text space (x along baseline, y up) rotated by (c,sn) */
    fix px[4], py[4];
    int i;
    px[0] = x0; py[0] = y0; px[1] = x1; py[1] = y0; px[2] = x1; py[2] = y1; px[3] = x0; py[3] = y1;
    for (i = 0; i < 4; i++) {
        fix X = fixmul(px[i], c) - fixmul(py[i], sn) + ox;
        fix Y = fixmul(px[i], sn) + fixmul(py[i], c) + oy;
        if (i == 0) s->move(s, X, Y); else s->line(s, X, Y);
    }
    s->close(s);
}

static int draw_rot(Wk *w, TxtMetrics *m, TxtGlyph *g, int n, fix adv, int x, int y,
                    fix ax, int ay, TxtBlit blit, int ul)
{
    Raster r;
    SpdXform xf;
    Box b;
    fix c = fixcos(w->t.rot), s = fixsin(w->t.rot);
    Scr sc;
    int i, rb, sh, sy, bw, pass;
    u8 *buf;
    s32 nbytes;

    xmemset(&xf, 0, sizeof xf);
    xf.sx = m->sx; xf.sy = m->sy; xf.skew = m->skew; xf.hint = (s16)m->hint;
    xf.rotated = 1;
    xf.m00 = c; xf.m01 = -s; xf.m10 = s; xf.m11 = c;
    scr_init(&sc);
    for (pass = 0; pass < 2; pass++) {
    raster_init(&r, sc.seg, sc.maxseg, INT2FIX(x), INT2FIX(y));
    r.dropout = FIX2INT(m->emy) <= 40;
    for (i = 0; i < n; i++) {
        fix tx = ax + g[i].x, ty = INT2FIX(-ay);   /* text space, y up */
        if (g[i].idx < 0) continue;
        xf.ox = fixmul(tx, c) - fixmul(ty, s);
        xf.oy = fixmul(tx, s) + fixmul(ty, c);
        if (m->hint) { xf.ox = (xf.ox + FIX_HALF) & ~0xffffL; xf.oy = (xf.oy + FIX_HALF) & ~0xffffL; }
        spd_char_outline(&m->f->spd, g[i].idx, &xf, &r.sink);
        if (m->bold) {   /* bold: overprint shifted copies along the baseline */
            int k;
            fix ox = xf.ox, oy = xf.oy;
            for (k = 1; k <= m->bold; k++) {
                xf.ox = ox + fixmul(INT2FIX(k), c);
                xf.oy = oy + fixmul(INT2FIX(k), s);
                spd_char_outline(&m->f->spd, g[i].idx, &xf, &r.sink);
            }
            xf.ox = ox; xf.oy = oy;
        }
    }
    if (ul && (w->t.effects & 8)) {
        fix ut = XG_MAX(FIX_ONE, m->f->spd.ul_thk * m->sy);
        fix uy = INT2FIX(-ay) + m->f->spd.ul_pos * m->sy;
        rect_outline(&r.sink, ax, uy - ut, ax + adv, uy, c, s, 0, 0);
    }
    if (!r.overflow || pass || !scr_grow(&sc, r.need)) break;
    }
    if (r.overflow) {
        /* too many edges even for a heap block: draw the halves separately */
        scr_done(&sc);
        if (n < 2) return 0;
        return draw_rot(w, m, g, n / 2, adv, x, y, ax, ay, blit, ul) &
               draw_rot(w, m, g + n / 2, n - n / 2, adv, x, y, ax, ay, blit, 0);
    }
    if (!raster_bounds(&r, &b.x0, &b.y0, &b.x1, &b.y1)) { scr_done(&sc); return 1; }
    if (!clip_box(w, &b)) { scr_done(&sc); return 1; }
    bw = b.x1 - b.x0;
    rb = ((bw + 15) >> 4) << 1;
    sh = (int)(STRIP_BYTES / rb);
    if (sh < 1) sh = 1;
    nbytes = (s32)rb * sh;
    buf = (u8 *)heap_alloc(nbytes);
    while (!buf && gc_shrink(nbytes)) buf = (u8 *)heap_alloc(nbytes);
    if (!buf) { scr_done(&sc); return 0; }
    for (sy = b.y0; sy < b.y1; sy += sh) {
        int rows = XG_MIN(sh, b.y1 - sy);
        xmemset(buf, 0, nbytes);
        raster_fill(&r, buf, rb, b.x0, sy, bw, rows, sc.tmp, sc.tmpsize);
        if (w->t.effects & 2) {
            int rr, k;
            for (rr = 0; rr < rows; rr++) {
                u8 pat = ((sy + rr) & 1) ? 0xaa : 0x55;
                for (k = 0; k < rb; k++) buf[(s32)rr * rb + k] &= pat;
            }
        }
        blit(w, buf, rb, b.x0, sy, bw, rows, 0);
    }
    heap_free(buf);
    scr_done(&sc);
    return 1;
}

int txt_draw(Wk *w, int x, int y, const s16 *codes, int n, int frac,
             int jlen, int wordsp, int charsp, const s16 *offs, TxtBlit blit)
{
    TxtMetrics m;
    TxtGlyph *g;
    fix adv, ax = 0;
    int ay, ok;
    if (!txt_metrics(w, &m)) { XLOG("text: font not loaded\r\n"); return 0; }
    if (txt_layout(w, &m, codes, n, frac, &g, &adv) < 0) { XLOG("text: no memory for layout\r\n"); return 0; }
    if (n <= 0) return 1;
    if (jlen > 0) txt_justify(g, n, &adv, INT2FIX(jlen), wordsp, charsp);
    if (w->t.halign == 1) ax = -adv / 2;
    else if (w->t.halign == 2) ax = -adv;
    ay = txt_valign(w, &m);
    if (w->t.rot == 0)
        ok = draw_flat(w, &m, g, n, adv, x, y, ax, ay, offs, blit);
    else
        ok = draw_rot(w, &m, g, n, adv, x, y, ax, ay, blit, 1);
    heap_free(g);
    return ok;
}
