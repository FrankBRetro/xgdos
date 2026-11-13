/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* vdi.c - XGDOS VDI dispatcher
 *
 * Every trap #2 VDI call arrives in xg_vdi(). Calls on screen workstations
 * are passed to the previously installed VDI unless they concern outline
 * text; printer workstations are served by the PDF device (pdf.c).
 *
 * Re-entrancy: no static scratch is used on the call path; per workstation
 * state lives in xg_wk[]; shared font/cache/heap state is protected by
 * xg_lock(). Calls that are pure pass-through never take the lock.
 */
#include "vdi.h"
#include "text.h"
#include "gcache.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"
#include "charmap.h"

Wk xg_wk[XG_MAXWK];

/* address of TOS's "current process basepage" variable (set by main.c);
 * 0 when unknown. Used to tie a tracked workstation to its program. */
u32 xg_prun;
static s32 cur_owner(void)
{
#ifdef XG_HOST
    return 0;
#else
    return xg_prun ? *(volatile s32 *)(unsigned long)xg_prun : 0;
#endif
}

static void logn(const char *a, int v, const char *b)
{
#ifdef XG_LOG
    char t[12];
    t[xitoa(t, v)] = 0;
    XLOG(a); XLOG(t); XLOG(b);
#else
    (void)a; (void)v; (void)b;
#endif
}


/* PC bring-up checkpoints in the log */
#if defined(XG_LOG) && defined(XG_PC)
#define CP(s) XLOG(s)
#else
#define CP(s) ((void)0)
#endif

#define CONTRL pb->contrl
#define INTIN  pb->intin
#define PTSIN  pb->ptsin
#define INTOUT pb->intout
#define PTSOUT pb->ptsout

/* handle -> slot+1; every VDI call in the system goes through wk_find() */
#define HMAP 512
static u8 hmap[HMAP];

Wk *wk_find(int h)
{
    int i;
    if (h <= 0) return NULL;
    if (h < HMAP) {
        i = hmap[h];
        return (i && xg_wk[i - 1].kind != WK_FREE && xg_wk[i - 1].handle == h) ? &xg_wk[i - 1] : NULL;
    }
    for (i = 0; i < XG_MAXWK; i++)
        if (xg_wk[i].kind != WK_FREE && xg_wk[i].handle == h) return &xg_wk[i];
    return NULL;
}

Wk *wk_alloc(int h, int kind)
{
    int i;
    Wk *w = wk_find(h);
    if (!w)
        for (i = 0; i < XG_MAXWK; i++)
            if (xg_wk[i].kind == WK_FREE) { w = &xg_wk[i]; break; }
    if (!w) return NULL;
    xmemset(w, 0, sizeof *w);
    w->kind = (s16)kind;
    w->handle = (s16)h;
    w->owner = cur_owner();
    wk_text_defaults(w);
    if (h > 0 && h < HMAP) hmap[h] = (u8)(w - xg_wk + 1);
    return w;
}

void wk_free(Wk *w)
{
    if (!w) return;
    if (w->handle > 0 && w->handle < HMAP) hmap[w->handle] = 0;
    w->kind = WK_FREE;
}

void wk_text_defaults(Wk *w)
{
    w->t.font_id = 1;
    w->t.fi = -1;
    w->t.pt = INT2FIX(10);
    w->t.wpt = 0;
    w->t.skew = w->t.rot = 0;
    w->t.halign = w->t.valign = 0;
    w->t.effects = 0;
    w->t.color = 1;
    w->t.wrmode = 1;
    w->t.track = 0;
    w->t.pairs = 1;
    w->t.charmap = 1;
}

void wk_snapshot_fonts(Wk *w)
{
    int i;
    w->snap_n = 0;
    for (i = 0; i < xg_nfont && w->snap_n < XG_MAXFONTS; i++)
        if (xg_font[i].enabled && xg_font[i].present) w->snap[w->snap_n++] = (u8)i;
}

/* vst_point sizes for outline fonts, as SpeedoGDOS's EXTEND.SYS POINTS
 * default. Like SpeedoGDOS, vst_point picks the largest of these at or
 * below the request (the smallest if the request is below all of them)
 * and returns it; vst_arbpt gives any size. Programs that list a font's
 * sizes by calling vst_point with ever smaller values until the answer
 * stops going down depend on this: echoing the request back made them
 * loop for ever. */
static const s16 pt_sizes[10] = { 8, 9, 10, 11, 12, 14, 18, 24, 36, 48 };

static int snap_point(int req)
{
    int k, best = pt_sizes[0];
    for (k = 0; k < 10; k++) if (pt_sizes[k] <= req) best = pt_sizes[k];
    return best;
}

static int visible_font(Wk *w, int id)
{
    int i;
    for (i = 0; i < w->snap_n; i++)
        if (xg_font[w->snap[i]].vdi_id == id && xg_font[w->snap[i]].present) return w->snap[i];
    return -1;
}

static void rom2(s16 *contrl, s16 *intin, s16 *ptsin, s16 *intout, s16 *ptsout)
{
    VDIPB pb;
    pb.contrl = contrl; pb.intin = intin; pb.ptsin = ptsin; pb.intout = intout; pb.ptsout = ptsout;
    rom_vdi(&pb);
}

/* ---------------- screen output of rendered text ---------------- */

static void scr_blit(Wk *w, const u8 *bits, int rb, int x, int y, int bw, int bh, int opaque)
{
    s16 contrl[16], intin[4], ptsin[8], intout[4], ptsout[4];
    MFDB src, dst;
    int mode;
    src.fd_addr = (void *)bits;
    src.fd_w = (s16)(rb * 8);
    src.fd_h = (s16)bh;
    src.fd_wdwidth = (s16)(rb / 2);
    src.fd_stand = 0;
    src.fd_nplanes = 1;
    src.fd_r1 = src.fd_r2 = src.fd_r3 = 0;
    xmemset(&dst, 0, sizeof dst);
    switch (w->t.wrmode) {
    case 3: mode = 3; break;
    case 4: mode = 4; break;
    case 2: mode = 2; break;
    default: mode = opaque ? 1 : 2; break;
    }
    intin[0] = (s16)mode;
    intin[1] = (w->t.color >= 0 && w->t.color < w->ncol) ? w->t.color : 1;
    intin[2] = (mode == 4) ? intin[1] : 0;
    ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = (s16)(bw - 1); ptsin[3] = (s16)(bh - 1);
    ptsin[4] = (s16)x; ptsin[5] = (s16)y; ptsin[6] = (s16)(x + bw - 1); ptsin[7] = (s16)(y + bh - 1);
    xmemset(contrl, 0, sizeof contrl);
    contrl[0] = 121; contrl[1] = 4; contrl[3] = 3; contrl[6] = w->handle;
    SETPTR(contrl, 7, &src);
    SETPTR(contrl, 9, &dst);
    rom2(contrl, intin, ptsin, intout, ptsout);
}

/* ---------------- shared text attribute and query handling ---------------- */

/* vqt_name of font 1 on every device */
void sysfont_name(s16 *io)
{
    static const char nm[] = "System Font";
    int i;
    for (i = 0; i < 32; i++) io[1 + i] = (s16)(i < (int)sizeof nm - 1 ? nm[i] : 0);
}

static int metrics(Wk *w, TxtMetrics *m)
{
    if (w->t.fi >= 0) return txt_metrics(w, m);
    if (w->kind != WK_ROM) return pdf_builtin_metrics(w, m);
    return 0;
}

static void size_out(VDIPB *pb, TxtMetrics *m)
{
    PTSOUT[0] = (s16)m->charw; PTSOUT[1] = (s16)m->charh;
    PTSOUT[2] = (s16)m->cellw; PTSOUT[3] = (s16)m->cellh;
    CONTRL[2] = 2;
}

static void emit_name(s16 *out, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) out[i] = (s16)(u8)(*s ? *s++ : 0);
}

static void outline_name(XFont *f, s16 *intout)
{
    char nm[33];
    xstrncpy0(nm, f->name, 33);
    emit_name(intout + 1, nm, 32);
}

typedef struct OlSink {
    SpdSink s;
    s16 *xy;
    u8 *fl;          /* bezarray is a char array */
    int n, max;
    fix sx, sy;      /* first point of contour */
    s16 lastx, lasty;
} OlSink;

static void ol_put(OlSink *o, fix x, fix y, int flag)
{
    if (o->n >= o->max) return;
    o->xy[2 * o->n] = (s16)FIXROUND(x);
    o->xy[2 * o->n + 1] = (s16)-FIXROUND(y);
    o->fl[o->n] = (u8)flag;
    o->n++;
}
static void ol_move(SpdSink *s, fix x, fix y) { OlSink *o = (OlSink *)s; o->sx = x; o->sy = y; ol_put(o, x, y, 2); }
static void ol_line(SpdSink *s, fix x, fix y) { ol_put((OlSink *)s, x, y, 0); }
static void ol_curve(SpdSink *s, fix a, fix b, fix c, fix d, fix e, fix f)
{
    OlSink *o = (OlSink *)s;
    if (o->n > 0) o->fl[o->n - 1] |= 1;     /* previous point starts a Bezier */
    ol_put(o, a, b, 0); ol_put(o, c, d, 0); ol_put(o, e, f, 0);
}
static void ol_close(SpdSink *s) { OlSink *o = (OlSink *)s; ol_put(o, o->sx, o->sy, 0); }

static void xfnt_info(Wk *w, VDIPB *pb)
{
    /* vqt_xfntinfo: intin[0] flags, intin[1] id, intin[2] index, intin[3..4] ptr */
    s32 *p = (s32 *)GETPTR(INTIN, 3);
    u8 *x = (u8 *)p;
    int fi = -1, idx = INTIN[2], id = INTIN[1];
    XFont *f;
    if (!x) return;
    if (idx > w->rom_nfonts && idx <= w->rom_nfonts + w->snap_n) fi = w->snap[idx - w->rom_nfonts - 1];
    else if (idx == 0 && id) fi = visible_font(w, id);
    if (fi < 0) {
        INTOUT[0] = 0; INTOUT[1] = 0; CONTRL[4] = 2;
        return;
    }
    f = &xg_font[fi];
    /* XFNT_INFO: long size; short format,id,index; char font_name[50],
       family_name[50], style_name[50], file_name1[200], file_name2[200],
       file_name3[200]; short pt_cnt, pt_sizes[64] */
    {
        s16 *hw = (s16 *)(x + 4);
        char *fn = (char *)(x + 10);
        hw[0] = 2;                                /* Speedo */
        hw[1] = f->vdi_id;
        hw[2] = (s16)(w->rom_nfonts + 1 + (int)(fi < 0 ? 0 : 0));
        {
            int k;
            for (k = 0; k < w->snap_n; k++) if (w->snap[k] == fi) hw[2] = (s16)(w->rom_nfonts + 1 + k);
        }
        if (INTIN[0] & 1) xstrncpy0(fn, f->name, 50);
        if (INTIN[0] & 2) xstrncpy0(fn + 50, f->sname, 50);
        if (INTIN[0] & 4) xstrncpy0(fn + 100, f->face, 50);
        if (INTIN[0] & 8) xstrncpy0(fn + 150, f->path, 200);
        if (INTIN[0] & 0x100) {
            s16 *pc = (s16 *)(x + 10 + 750);
            int k;
            pc[0] = 10;
            for (k = 0; k < 10; k++) pc[1 + k] = pt_sizes[k];
        }
        INTOUT[0] = f->vdi_id;
        INTOUT[1] = hw[2];
        CONTRL[4] = 2;
    }
}

/* returns 1 if handled */
int xg_text_op(Wk *w, VDIPB *pb)
{
    int op = CONTRL[0], rom = (w->kind == WK_ROM);
    TxtMetrics m;
    int ok;

    switch (op) {
    case 119: {     /* vst_load_fonts */
        int add = 0;
        if (rom && !w->fonts_loaded) {
            void *ch = fnt_chain(w->dev);
            if (ch) {
                SETPTR(CONTRL, 7, ch);
                rom_vdi(pb);
                add = INTOUT[0];
                w->rom_nfonts = (s16)(w->rom_nfonts + add);
            }
        }
        if (!w->fonts_loaded) wk_snapshot_fonts(w);
        w->fonts_loaded = 1;
        INTOUT[0] = (s16)(w->rom_nfonts - 1 + w->snap_n);
        logn("handle ", w->handle, ": vst_load_fonts");
        logn(" -> ", INTOUT[0], " additional fonts\r\n");
        CONTRL[4] = 1;
        return 1;
    }
    case 120:       /* vst_unload_fonts */
        if (rom && fnt_chain(w->dev) && w->fonts_loaded) rom_vdi(pb);
        if (w->fonts_loaded && rom) w->rom_nfonts = 1;
        w->fonts_loaded = 0;
        w->snap_n = 0;
        if (w->t.fi >= 0) w->t.fi = -1;
        CONTRL[4] = 0;
        return 1;

    case 21: {      /* vst_font */
        int fi = visible_font(w, INTIN[0]);
        w->t.font_id = INTIN[0];
        if (fi >= 0) {
            logn("handle ", w->handle, ": vst_font");
            logn(" ", INTIN[0], " (Speedo)\r\n");
            w->t.fi = (s16)fi;
            INTOUT[0] = INTIN[0];
            CONTRL[4] = 1;
            return 1;
        }
        w->t.fi = -1;
        if (rom) { rom_vdi(pb); w->t.font_id = INTOUT[0]; return 1; }
        return 0;
    }
    case 107: {     /* vst_point */
        int pt = snap_point(INTIN[0]);
        w->t.pt = INT2FIX(pt);
        if (rom) rom_vdi(pb);
        if (w->t.fi >= 0 || !rom) {
            if (!metrics(w, &m)) return rom;
            INTOUT[0] = (s16)pt;
            CONTRL[4] = 1;
            size_out(pb, &m);
        }
        return 1;
    }
    case 12:        /* vst_height */
        if (rom) rom_vdi(pb);
        if (w->t.fi >= 0 && txt_metrics(w, &m)) {
            XFont *f = m.f;
            fix em = muldiv(INT2FIX(INTIN[0] > 0 ? INTIN[0] : 1), f->orupm, f->m_top > 0 ? f->m_top : f->orupm);
            w->t.pt = txt_px2pt_y(w, em);
            txt_metrics(w, &m);
            size_out(pb, &m);
        } else if (!rom) {
            w->t.pt = txt_px2pt_y(w, INT2FIX(INTIN[0]) * 4 / 3);
            if (metrics(w, &m)) size_out(pb, &m);
        } else {
            w->t.pt = txt_px2pt_y(w, INT2FIX(INTIN[0]) * 4 / 3);
        }
        return 1;
    case 246: {     /* vst_arbpt / vst_arbpt32 */
        fix pt = (CONTRL[3] >= 2) ? GETLONG(INTIN, 0) : INT2FIX(INTIN[0]);
        if (pt < FIX_ONE) pt = FIX_ONE;
        w->t.pt = pt;
        if (rom) {
            s16 c[12], ii[2], io[4], po[4];
            xmemset(c, 0, sizeof c);
            c[0] = 107; c[3] = 1; c[6] = w->handle;
            ii[0] = (s16)FIXROUND(pt);
            rom2(c, ii, NULL, io, po);
            if (w->t.fi < 0) {
                PTSOUT[0] = po[0]; PTSOUT[1] = po[1]; PTSOUT[2] = po[2]; PTSOUT[3] = po[3];
                CONTRL[2] = 2;
            }
        }
        if (metrics(w, &m)) size_out(pb, &m);
        if (CONTRL[3] >= 2) { SETLONG(INTOUT, 0, pt); CONTRL[4] = 2; }
        else { INTOUT[0] = (s16)FIXROUND(pt); CONTRL[4] = 1; }
        return 1;
    }
    case 252: {     /* vst_setsize / vst_setsize32 */
        fix wpt = (CONTRL[3] >= 2) ? GETLONG(INTIN, 0) : INT2FIX(INTIN[0]);
        w->t.wpt = (wpt == w->t.pt) ? 0 : wpt;
        if (metrics(w, &m)) size_out(pb, &m);
        else if (rom) {     /* system font: report its real sizes */
            s16 c[12], io[8], po[4];
            xmemset(c, 0, sizeof c);
            c[0] = 38; c[6] = w->handle;
            rom2(c, NULL, NULL, io, po);
            PTSOUT[0] = po[0]; PTSOUT[1] = po[1]; PTSOUT[2] = po[2]; PTSOUT[3] = po[3];
            CONTRL[2] = 2;
        }
        if (CONTRL[3] >= 2) { SETLONG(INTOUT, 0, wpt); CONTRL[4] = 2; }
        else { INTOUT[0] = (s16)FIXROUND(wpt); CONTRL[4] = 1; }
        return 1;
    }
    case 253:       /* vst_skew */
        w->t.skew = (s16)XG_MAX(-900, XG_MIN(900, INTIN[0]));
        INTOUT[0] = w->t.skew;
        CONTRL[4] = 1;
        return 1;
    case 13:        /* vst_rotation */
        if (rom) rom_vdi(pb);
        w->t.rot = (s16)(((INTIN[0] % 3600) + 3600) % 3600);
        if (w->t.fi >= 0 || !rom) { INTOUT[0] = w->t.rot; CONTRL[4] = 1; }
        return 1;
    case 39:        /* vst_alignment */
        if (rom) {
            rom_vdi(pb);
            w->t.halign = INTOUT[0]; w->t.valign = INTOUT[1];
        } else {
            w->t.halign = (s16)((INTIN[0] >= 0 && INTIN[0] <= 2) ? INTIN[0] : 0);
            w->t.valign = (s16)((INTIN[1] >= 0 && INTIN[1] <= 5) ? INTIN[1] : 0);
            INTOUT[0] = w->t.halign; INTOUT[1] = w->t.valign; CONTRL[4] = 2;
        }
        return 1;
    case 22:        /* vst_color */
        if (rom) { rom_vdi(pb); w->t.color = INTOUT[0]; return 1; }
        return 0;
    case 106:       /* vst_effects */
        if (rom) rom_vdi(pb);
        w->t.effects = (s16)(INTIN[0] & 0x3f);
        if (w->t.fi >= 0 || !rom) { INTOUT[0] = w->t.effects; CONTRL[4] = 1; }
        return 1;
    case 32:        /* vswr_mode */
        if (rom) { rom_vdi(pb); w->t.wrmode = INTOUT[0]; return 1; }
        return 0;
    case 129:       /* vs_clip */
        if (rom) rom_vdi(pb);
        w->clip_on = INTIN[0];
        if (w->clip_on) {
            w->clip[0] = XG_MIN(PTSIN[0], PTSIN[2]); w->clip[2] = XG_MAX(PTSIN[0], PTSIN[2]);
            w->clip[1] = XG_MIN(PTSIN[1], PTSIN[3]); w->clip[3] = XG_MAX(PTSIN[1], PTSIN[3]);
        }
        return rom;
    case 236:       /* vst_charmap */
        w->t.charmap = (s16)(INTIN[0] ? 1 : 0);
        INTOUT[0] = w->t.charmap; CONTRL[4] = 1;
        return 1;
    case 237:       /* vst_kern */
        w->t.track = (s16)((INTIN[0] >= 0 && INTIN[0] <= 3) ? INTIN[0] : 0);
        w->t.pairs = (s16)(INTIN[1] ? 1 : 0);
        INTOUT[0] = w->t.track;
        INTOUT[1] = 0;
        if (w->t.fi >= 0) { XFont *f = fm_get(w->t.fi); if (f) INTOUT[1] = (s16)f->spd.npairs; }
        CONTRL[4] = 2;
        return 1;
    case 244: case 245:     /* vst_scratch, vst_error */
        CONTRL[4] = 0;
        return 1;
    case 251:       /* v_flushcache */
        gc_flush();
        CONTRL[4] = 0;
        return 1;
    case 249: case 250:     /* v_savecache, v_loadcache */
        INTOUT[0] = -1; CONTRL[4] = 1;
        return 1;
    case 255:       /* vqt_cachesize */
        SETLONG(INTOUT, 0, INTIN[0] == 0 ? gc_budget() - gc_used() : heap_largest());
        CONTRL[4] = 2;
        return 1;
    case 254:       /* vqt_get_table */
        SETPTR(INTOUT, 0, xg_atari_to_bcid);
        CONTRL[4] = 2;
        return 1;

    case 130: {     /* vqt_name */
        int idx = INTIN[0];
        if (idx > w->rom_nfonts && idx <= w->rom_nfonts + w->snap_n) {
            XFont *f = &xg_font[w->snap[idx - w->rom_nfonts - 1]];
            INTOUT[0] = f->vdi_id;
            outline_name(f, INTOUT);
            INTOUT[33] = 1;
            CONTRL[4] = 34;
            return 1;
        }
        if (rom) {
            CP("  130: to GEM\r\n");
            rom_vdi(pb);
            CP("  130: back\r\n");
            /* TOS names the whole system font after its first size,
             * "6x6 system font"; it is one face in 6x6, 8x8 and 8x16 */
            if (idx == 1) sysfont_name(INTOUT);
#ifdef XG_PC
            /* GEM/3's vqt_name is 33 words: no outline flag word */
            CONTRL[4] = 33;
#else
            INTOUT[33] = 0;
            CONTRL[4] = 34;
#endif
            CP("  130: named\r\n");
            return 1;
        }
        return 0;
    }
    case 229:       /* vqt_xfntinfo */
        xfnt_info(w, pb);
        return 1;
    }

    /* ---- queries that depend on the active outline font ---- */
    if (w->t.fi >= 0 && !fm_get(w->t.fi)) {
        /* the font cannot be used: behave as the system font from now on */
        logn("handle ", w->handle, ": Speedo font unusable, using the system font\r\n");
        w->t.fi = -1;
    }
    if (w->t.fi < 0) {
        if (!rom) return 0;
        switch (op) {
        case 240:   /* vqt_f_extent -> vqt_extent */
            CONTRL[0] = 116; rom_vdi(pb); CONTRL[0] = 240;
            return 1;
        case 247:   /* vqt_advance -> vqt_width */
            CONTRL[0] = 117; rom_vdi(pb); CONTRL[0] = 247;
            PTSOUT[1] = 0; PTSOUT[2] = PTSOUT[3] = 0; CONTRL[2] = 2;
            return 1;
        case 241: { /* v_ftext -> v_gtext */
            s16 n1 = CONTRL[1];
            CONTRL[0] = 8; CONTRL[1] = 1; rom_vdi(pb); CONTRL[0] = 241; CONTRL[1] = n1;
            return 1;
        }
        }
        rom_vdi(pb);
        return 1;
    }

    ok = txt_metrics(w, &m);
    switch (op) {
    case 38:        /* vqt_attributes */
        if (rom) rom_vdi(pb);
        INTOUT[0] = w->t.font_id; INTOUT[1] = w->t.color; INTOUT[2] = w->t.rot;
        INTOUT[3] = w->t.halign; INTOUT[4] = w->t.valign; INTOUT[5] = w->t.wrmode;
        CONTRL[4] = 6;
        if (ok) size_out(pb, &m);
        return 1;
    case 116: case 240: {   /* vqt_extent, vqt_f_extent */
        TxtGlyph *g = NULL;
        fix adv = 0;
        int i;
        for (i = 0; i < 8; i++) PTSOUT[i] = 0;
        if (ok && txt_layout(w, &m, INTIN, CONTRL[3], op == 240, &g, &adv) >= 0) {
            if (g) {
                /* italic overhang */
                adv += fixmul(INT2FIX(m.top), m.skew > 0 ? m.skew : 0);
                txt_extent(w, &m, adv, PTSOUT);
            }
            heap_free(g);
        }
        CONTRL[2] = 4;
        CONTRL[4] = 0;
        return 1;
    }
    case 117: {     /* vqt_width */
        int idx;
        INTOUT[0] = -1;
        PTSOUT[0] = PTSOUT[2] = PTSOUT[4] = 0;
        PTSOUT[1] = PTSOUT[3] = PTSOUT[5] = 0;
        if (ok && (idx = fm_glyph(m.f, INTIN[0] & 0xff, w->t.charmap)) >= 0) {
            INTOUT[0] = INTIN[0];
            PTSOUT[0] = (s16)(FIXROUND(spd_char_width(&m.f->spd, idx) * m.sx) + m.bold + 2 * m.outl);
            PTSOUT[4] = (s16)FIXROUND(fixmul(INT2FIX(m.top), m.skew > 0 ? m.skew : 0));
        }
        CONTRL[2] = 3; CONTRL[4] = 1;
        return 1;
    }
    case 247: {     /* vqt_advance (32) */
        int idx;
        fix a = 0;
        if (ok && (idx = fm_glyph(m.f, INTIN[0] & 0xff, w->t.charmap)) >= 0)
            a = spd_char_width(&m.f->spd, idx) * m.sx + INT2FIX(m.bold + 2 * m.outl);
        {
            fix ax = fixmul(a, fixcos(w->t.rot)), ay = -fixmul(a, fixsin(w->t.rot));
            PTSOUT[0] = (s16)FIX2INT(ax); PTSOUT[1] = (s16)FIX2INT(ay);
            PTSOUT[2] = (s16)((ax & 0xffff) >> 2); PTSOUT[3] = (s16)((ay & 0xffff) >> 2);
            CONTRL[2] = 2;
            if (CONTRL[3] > 1 || CONTRL[5] == 1) {   /* 32 bit variant */
                SETLONG(PTSOUT, 0, ax); SETLONG(PTSOUT, 2, ay); CONTRL[2] = 2;
            }
        }
        return 1;
    }
    case 131:       /* vqt_fontinfo */
        if (!ok) return rom ? (rom_vdi(pb), 1) : 0;
        INTOUT[0] = 32; INTOUT[1] = 255;
        PTSOUT[0] = (s16)m.cellw; PTSOUT[1] = (s16)m.bot; PTSOUT[2] = 0; PTSOUT[3] = (s16)m.desc;
        PTSOUT[4] = (s16)FIXROUND(fixmul(INT2FIX(m.top), m.skew > 0 ? m.skew : 0));
        PTSOUT[5] = (s16)m.half; PTSOUT[6] = 0; PTSOUT[7] = (s16)m.asc; PTSOUT[8] = 0; PTSOUT[9] = (s16)m.top;
        CONTRL[2] = 5; CONTRL[4] = 2;
        return 1;
    case 234: {     /* vqt_trackkern */
        fix k = ok ? txt_pt2px_x(w, spd_track_kern(&m.f->spd, w->t.track, w->t.pt)) : 0;
        SETLONG(INTOUT, 0, k); SETLONG(INTOUT, 2, 0); CONTRL[4] = 4;
        return 1;
    }
    case 235: {     /* vqt_pairkern */
        fix k = 0;
        if (ok) {
            int a = fm_glyph(m.f, INTIN[0], w->t.charmap), b = fm_glyph(m.f, INTIN[1], w->t.charmap);
            if (a >= 0 && b >= 0) k = spd_pair_kern(&m.f->spd, a, b) * m.sx;
        }
        SETLONG(INTOUT, 0, k); SETLONG(INTOUT, 2, 0); CONTRL[4] = 4;
        return 1;
    }
    case 232: {     /* vqt_fontheader */
        u8 *buf = (u8 *)GETPTR(INTIN, 0);
        XFont *f = ok ? m.f : NULL;
        int i, n;
        if (f && buf) {
            n = (f->data[18] << 8) | f->data[19];
            if (n > 512) n = 512;
            xmemcpy(buf, f->data, n);
        }
        n = f ? (int)xstrlen(f->path) : 0;
        for (i = 0; i < n && i < 127; i++) INTOUT[i] = (s16)(u8)f->path[i];
        INTOUT[i] = 0;
        CONTRL[4] = (s16)(i + 1);
        return 1;
    }
    case 243: {     /* v_getoutline */
        OlSink o;
        SpdXform xf;
        int idx;
        o.s.move = ol_move; o.s.line = ol_line; o.s.curve = ol_curve; o.s.close = ol_close;
        /* SpeedoGDOS / gemlib layout: intin[0] char, intin[1] maxverts,
         * intin[2-3] xyarray, intin[4-5] bezarray (char) */
        o.max = INTIN[1];
        o.xy = (s16 *)GETPTR(INTIN, 2);
        o.fl = (u8 *)GETPTR(INTIN, 4);
        o.n = 0;
        if (CONTRL[3] < 6 || o.max <= 0) o.xy = NULL;
        if (ok && o.xy && o.fl && (idx = fm_glyph(m.f, INTIN[0], w->t.charmap)) >= 0) {
            xmemset(&xf, 0, sizeof xf);
            xf.sx = m.sx; xf.sy = m.sy; xf.skew = m.skew;
            spd_char_outline(&m.f->spd, idx, &xf, &o.s);
        }
        INTOUT[0] = (s16)o.n;
        CONTRL[4] = 1;
        return 1;
    }
    case 239: {     /* v_getbitmap_info */
        Glyph *gl = NULL;
        int idx;
        fix a = 0;
        if (ok && (idx = fm_glyph(m.f, INTIN[0], w->t.charmap)) >= 0) {
            gl = gc_get(w->t.fi, idx, m.sx, m.sy, m.skew, m.hint);
            a = spd_char_width(&m.f->spd, idx) * m.sx;
        }
        INTOUT[0] = gl ? gl->w : 0;
        INTOUT[1] = gl ? gl->h : 0;
        SETLONG(INTOUT, 2, a);
        SETLONG(INTOUT, 4, 0);
        SETLONG(INTOUT, 6, gl ? INT2FIX(gl->left) : 0);
        SETLONG(INTOUT, 8, gl ? INT2FIX(-gl->top) : 0);
        SETPTR(INTOUT, 10, gl ? gl->bits : NULL);
        CONTRL[4] = 12;
        return 1;
    }
    }
    return rom ? (rom_vdi(pb), 1) : 0;
}

/* Atari screen fonts (and SpeedoGDOS) size text on the screen at 4/3 of the
 * pixel density the ROM VDI reports: mono reports 372um (68 dpi) but its
 * fonts are 91 dpi. Keep points the size users and applications expect. */
static void screen_ppi(Wk *w)
{
    if (w->dev > 10) return;                 /* printers etc: true density */
    w->ppix = muldiv(INT2FIX(25400), INT2FIX(4), INT2FIX(3 * (s32)w->pw));
    w->ppiy = muldiv(INT2FIX(25400), INT2FIX(4), INT2FIX(3 * (s32)w->ph));
}


/* ---------------- screen workstations ---------------- */

/* A screen workstation seen for the first time through a GDOS call: ask
 * the VDI for its properties and current text attributes. */
static Wk *lazy_wk(int h)
{
    s16 c[12], ii[2], io[48], po[12];
    Wk *w;
    if (h <= 0) return NULL;
    xmemset(c, 0, sizeof c);
    xmemset(io, 0, sizeof io);
    c[0] = 102; c[3] = 1; c[6] = (s16)h; ii[0] = 0;          /* vq_extnd(h, 0) */
    rom2(c, ii, NULL, io, po);
    if (io[0] <= 0 || io[1] <= 0) return NULL;
    w = wk_alloc(h, WK_ROM);
    if (!w) return NULL;
    w->dev = (s16)xos_screen_dev();
    w->xres = io[0];
    w->yres = io[1];
    w->pw = io[3] > 0 ? io[3] : 278;
    w->ph = io[4] > 0 ? io[4] : 278;
    screen_ppi(w);
    w->rom_nfonts = io[10] > 0 ? io[10] : 1;
    w->ncol = io[13] > 0 ? io[13] : 2;
    xmemset(c, 0, sizeof c);
    c[0] = 38; c[6] = (s16)h;                                 /* vqt_attributes */
    rom2(c, NULL, NULL, io, po);
    w->t.font_id = io[0];
    w->t.color = (io[1] >= 0 && io[1] < w->ncol) ? io[1] : 1;
    w->t.rot = io[2];
    w->t.halign = io[3]; w->t.valign = io[4];
    w->t.wrmode = (io[5] >= 1 && io[5] <= 4) ? io[5] : 1;
    logn("tracking screen workstation ", h, "\r\n");
    return w;
}

static void track_open(VDIPB *pb, int dev, int phys)
{
    Wk *w, *pw;
    int h = CONTRL[6];
    if (h <= 0) return;
    w = wk_alloc(h, WK_ROM);
    if (!w) return;
    pw = phys ? wk_find(phys) : NULL;
    w->dev = (s16)(pw ? pw->dev : dev);
    w->phys = (s16)phys;
    w->xres = INTOUT[0];
    w->yres = INTOUT[1];
    w->pw = INTOUT[3] > 0 ? INTOUT[3] : 278;
    w->ph = INTOUT[4] > 0 ? INTOUT[4] : 278;
    screen_ppi(w);
    w->rom_nfonts = INTOUT[10] > 0 ? INTOUT[10] : 1;
    w->ncol = INTOUT[13] > 0 ? INTOUT[13] : 2;
    /* initial text colour from work_in[6], clamped like the VDI does */
    w->t.color = (INTIN[6] >= 0 && INTIN[6] < w->ncol) ? INTIN[6] : 1;
}

static void screen_text(Wk *w, VDIPB *pb)
{
    int op = CONTRL[0], ok;
    if (op == 8)
        ok = txt_draw(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 0, 0, 0, 0, NULL, scr_blit);
    else if (op == 241)
        ok = txt_draw(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 1, 0, 0, 0,
                      CONTRL[1] > 1 ? PTSIN + 2 : NULL, scr_blit);
    else    /* v_justified */
        ok = txt_draw(w, PTSIN[0], PTSIN[1], INTIN + 2, CONTRL[3] - 2, 0, PTSIN[2], INTIN[0], INTIN[1], NULL, scr_blit);
    if (!ok) {
        /* never lose text: draw it with the screen VDI's current font */
        s16 n1 = CONTRL[1];
        logn("handle ", w->handle, ": outline text failed, using the system font\r\n");
        if (op == 241) { CONTRL[0] = 8; CONTRL[1] = 1; }
        rom_vdi(pb);
        CONTRL[0] = (s16)op; CONTRL[1] = n1;
        return;
    }
    CONTRL[2] = 0;
    CONTRL[4] = 0;
}

static int is_text_op(int op, int sub)
{
    switch (op) {
    case 8: case 12: case 13: case 21: case 22: case 32: case 38: case 39:
    case 106: case 107: case 116: case 117: case 119: case 120: case 129:
    case 130: case 131: case 229: case 232: case 234: case 235: case 236:
    case 237: case 239: case 240: case 241: case 243: case 244: case 245:
    case 246: case 247: case 249: case 250: case 251: case 252: case 253:
    case 254: case 255:
        return 1;
    case 11:
        return sub == 10;
    }
    return 0;
}

/* Opcodes the screen VDI does not know: always answered by XGDOS */
static int gdos_only(int op)
{
    switch (op) {
    case 229: case 232: case 234: case 235: case 236: case 237: case 239:
    case 240: case 241: case 243: case 244: case 245: case 246: case 247:
    case 249: case 250: case 251: case 252: case 253: case 254: case 255:
        return 1;
    }
    return 0;
}

/* Called first by the trap handler, on the caller's stack, without the
 * lock: must not call the OS or the screen VDI. Returns 0 when the call
 * can go to the previous VDI completely untouched; attribute values are
 * noted on the way past so outline text can use them later. */
#ifdef XG_LOG
static int seen_first;      /* log the first call seen by the handler */
#endif

int xg_vdi_need(VDIPB *pb)
{
    int op = CONTRL[0];
    Wk *w;
#ifdef XG_LOG
    if (!seen_first && op != 1 && op != 100) { seen_first = 1; return 1; }
#endif
    /* screen workstations open straight through the VDI; XGDOS only
     * takes printers and other devices it implements itself */
    if (op == 1) return INTIN[0] > 10;
    if (op == 100) { w = wk_find(CONTRL[6]); return w && w->kind != WK_ROM; }
    w = wk_find(CONTRL[6]);
    if (w && w->kind == WK_ROM && w->owner != cur_owner()) {
        wk_free(w);             /* handle now belongs to another program */
        w = NULL;
    }
    if (!w) return op == 119 || gdos_only(op) ||   /* start tracking on first GDOS use */
                   (op == 130 && INTIN[0] == 1);  /* or to name the system font */
    if (w->kind != WK_ROM) return 1;
    if (op == 2 || op == 101) { wk_free(w); return 0; }
    if (gdos_only(op)) return 1;
    if (w->t.fi >= 0 && is_text_op(op, CONTRL[5])) return 1;
    switch (op) {
    case 119: return 1;                                   /* vst_load_fonts */
    case 120: return w->fonts_loaded;                     /* vst_unload_fonts */
    case 21:                                              /* vst_font */
        if (visible_font(w, INTIN[0]) >= 0) return 1;
        w->t.font_id = INTIN[0];
        return 0;
    case 130: return INTIN[0] == 1 || INTIN[0] > w->rom_nfonts;   /* vqt_name */
    case 107: w->t.pt = INT2FIX(snap_point(INTIN[0])); return 0;
    case 12:  w->t.pt = txt_px2pt_y(w, INT2FIX(INTIN[0] > 0 ? INTIN[0] : 1) * 4 / 3); return 0;
    case 13:  w->t.rot = (s16)(((INTIN[0] % 3600) + 3600) % 3600); return 0;
    case 39:
        w->t.halign = (s16)((INTIN[0] >= 0 && INTIN[0] <= 2) ? INTIN[0] : 0);
        w->t.valign = (s16)((INTIN[1] >= 0 && INTIN[1] <= 5) ? INTIN[1] : 0);
        return 0;
    case 22:  w->t.color = (s16)((INTIN[0] >= 0 && INTIN[0] < w->ncol) ? INTIN[0] : 1); return 0;
    case 106: w->t.effects = (s16)(INTIN[0] & 0x3f); return 0;
    case 32:  w->t.wrmode = (s16)((INTIN[0] >= 1 && INTIN[0] <= 4) ? INTIN[0] : 1); return 0;
    case 129:
        w->clip_on = INTIN[0];
        if (w->clip_on) {
            w->clip[0] = XG_MIN(PTSIN[0], PTSIN[2]); w->clip[2] = XG_MAX(PTSIN[0], PTSIN[2]);
            w->clip[1] = XG_MIN(PTSIN[1], PTSIN[3]); w->clip[3] = XG_MAX(PTSIN[1], PTSIN[3]);
        }
        return 0;
    }
    return 0;
}

void xg_vdi(VDIPB *pb)
{
    int op = CONTRL[0];
    Wk *w;
#ifdef XG_LOG
    if (seen_first == 1) {
        seen_first = 2;
        logn("trap #2 handler active: first VDI call op ", op, "");
        logn(" handle ", CONTRL[6], "\r\n");
        if (!wk_find(CONTRL[6])) { rom_vdi(pb); return; }
    }
#endif
#ifdef XG_LOG
    static int nlog;
    if (nlog < 400) {
        nlog++;
        logn("call ", op, "");
        logn(".", CONTRL[5], "");
        logn(" h", CONTRL[6], "");
        logn(" nin ", CONTRL[3], "");
        logn(" in0 ", CONTRL[3] ? INTIN[0] : 0, "\r\n");
    }
#endif

    if (op == 1) {                          /* v_opnwk */
        int dev = INTIN[0];
        const XgDev *d = cfg_device(&xg_cfg, dev);
        if (dev >= 21 && dev <= 30 && xg_cfg.printer == PRN_PDF) {
            xg_lock();
            {
                int h, i;
                for (h = 200, i = 0; i < 100 && wk_find(h); h++, i++) ;
                w = wk_alloc(h, WK_PDF);
                if (w) {
                    w->dev = (s16)dev;
                    pdf_open(w, pb);
                    if (CONTRL[6] == 0) wk_free(w);
                } else CONTRL[6] = 0;
            }
            xg_unlock();
            return;
        }
        if ((dev >= 31 && dev <= 40) || (dev >= 61 && dev <= 70)) {
            /* XGDOS's own metafile and memory devices: no driver file */
            xg_lock();
            {
                int h, i, base = dev >= 61 ? 450 : 400;
                for (h = base, i = 0; i < 50 && wk_find(h); h++, i++) ;
                w = i < 50 ? wk_alloc(h, dev >= 61 ? WK_MEM : WK_META) : NULL;
                if (w) {
                    w->dev = (s16)dev;
                    if (dev >= 61) mem_open(w, pb); else meta_open(w, pb);
                    if (CONTRL[6] == 0) wk_free(w);
                } else CONTRL[6] = 0;
            }
            xg_unlock();
            logn("v_opnwk device ", dev, "");
            logn(" (XGDOS) -> handle ", CONTRL[6], "\r\n");
            return;
        }
        if (dev > 10 && d) {                /* external .SYS driver */
            drv_open(pb, d->drv);
            return;
        }
        rom_vdi(pb);
        if (dev <= 10) track_open(pb, dev, 0);
        logn("v_opnwk device ", dev, "");
        logn(" -> handle ", CONTRL[6], "\r\n");
        return;
    }
    if (op == 100) {                        /* v_opnvwk */
        int phys = CONTRL[6];
        Wk *pw = wk_find(phys);
        if (pw && pw->kind == WK_MEM) {     /* shares the memory device's bitmap */
            xg_lock();
            mem_open_virtual(pw, pb);
            xg_unlock();
            return;
        }
        if (pw && pw->kind != WK_ROM) {     /* virtual on a printer: not supported */
            CONTRL[6] = 0;
            return;
        }
        rom_vdi(pb);
        track_open(pb, pw ? pw->dev : 1, phys);
        logn("v_opnvwk on ", phys, "");
        logn(" -> handle ", CONTRL[6], "\r\n");
        return;
    }

    w = wk_find(CONTRL[6]);
    if (!w) w = lazy_wk(CONTRL[6]);
    if (!w) { rom_vdi(pb); return; }
    CP("  have wk\r\n");

    switch (w->kind) {
    case WK_PDF:
        xg_lock();
        pdf_vdi(w, pb);
        xg_unlock();
        return;
    case WK_DRV:
        drv_call(w, pb);
        return;
    case WK_META:
        xg_lock();
        meta_vdi(w, pb);
        xg_unlock();
        return;
    case WK_MEM:
        xg_lock();
        mem_vdi(w, pb);
        xg_unlock();
        return;
    }

    /* ROM screen workstation */
    if (op == 2 || op == 101) {
        rom_vdi(pb);
        wk_free(w);
        return;
    }
    if (!is_text_op(op, CONTRL[5])) { rom_vdi(pb); return; }

    /* cheap attribute calls without an outline font: just track them */
    if (w->t.fi < 0 && (op == 8 || op == 116 || op == 117 || op == 131 || op == 38 || op == 11)) {
        rom_vdi(pb);
        return;
    }
    CP("  text op\r\n");
    xg_lock();
    if (op == 8 || op == 241 || op == 11) {
        if (w->t.fi >= 0) screen_text(w, pb);
        else xg_text_op(w, pb);
    } else {
        xg_text_op(w, pb);
    }
    xg_unlock();
}
