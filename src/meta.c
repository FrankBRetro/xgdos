/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* meta.c - XGDOS's own metafile device (GDOS devices 31-40, META.SYS).
 *
 * v_opnwk creates GEMFILE.GEM in the program's current folder (vm_filename
 * changes the name, as with META.SYS the program deletes GEMFILE.GEM
 * itself). Every drawing and attribute call is recorded as a GEM metafile
 * item; queries are answered by XGDOS. v_clswk ends the file and writes
 * the header: extents of everything drawn (or those from v_meta_extents),
 * the page size (vm_pagesize) and coordinate window (vm_coords).
 *
 * File layout (all words little-endian, as GEM metafiles are on every
 * platform): a 24 word header starting -1, 24; then items of
 *   opcode, point count, integer count, sub-opcode, points, integers;
 * and a final -1.
 *
 * Device space is 0..32767 in both directions with 85 micron pixels,
 * like META.SYS.
 */
#include "soft.h"
#include "text.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"

#define MBUF 4096
#define HDR_WORDS 24

typedef struct MetaDev {
    char path[XG_PATHLEN];
    u8  buf[MBUF];
    s32 n, size;
    s16 x0, y0, x1, y1;       /* extents of what was drawn */
    s16 any, user_ext;
    s16 ux0, uy0, ux1, uy1;   /* v_meta_extents */
    s16 pgw, pgh;             /* vm_pagesize, 1/10 mm */
    s16 cw[4];                /* vm_coords */
    s16 rc;                   /* coordinate system: 2 = raster, 0 = NDC */
    s16 err;
    GAttr a;
} MetaDev;

#define CONTRL pb->contrl
#define INTIN  pb->intin
#define PTSIN  pb->ptsin
#define INTOUT pb->intout
#define PTSOUT pb->ptsout

/* ---------------- file output ---------------- */

static void mflush(MetaDev *m)
{
    s32 h;
    if (!m->n || m->err) { m->n = 0; return; }
    h = xos_open(m->path, 1);
    if (h < 0) { m->err = 1; m->n = 0; return; }
    xos_seek(h, 0, 2);
    if (xos_write(h, m->n, m->buf) != m->n) m->err = 1;
    xos_close(h);
    m->n = 0;
}

static void mword(MetaDev *m, s16 v)
{
    if (m->n + 2 > MBUF) mflush(m);
    m->buf[m->n++] = (u8)v;
    m->buf[m->n++] = (u8)((u16)v >> 8);
    m->size += 2;
}

static void header(MetaDev *m, u8 *out)
{
    s16 h[HDR_WORDS];
    int i;
    xmemset(h, 0, sizeof h);
    h[0] = -1;
    h[1] = HDR_WORDS;
    h[2] = 101;                         /* readable by GEM Draw and later */
    h[3] = m->rc;
    if (m->user_ext) { h[4] = m->ux0; h[5] = m->uy0; h[6] = m->ux1; h[7] = m->uy1; }
    else if (m->any) { h[4] = m->x0; h[5] = m->y0; h[6] = m->x1; h[7] = m->y1; }
    h[8] = m->pgw; h[9] = m->pgh;
    h[10] = m->cw[0]; h[11] = m->cw[1]; h[12] = m->cw[2]; h[13] = m->cw[3];
    for (i = 0; i < HDR_WORDS; i++) { out[2 * i] = (u8)h[i]; out[2 * i + 1] = (u8)((u16)h[i] >> 8); }
}

static int start_file(MetaDev *m)
{
    u8 hb[HDR_WORDS * 2];
    s32 h = xos_create(m->path);
    if (h < 0) return 0;
    header(m, hb);
    if (xos_write(h, sizeof hb, hb) != (s32)sizeof hb) m->err = 1;
    xos_close(h);
    m->size = sizeof hb;
    return 1;
}

static void finish(MetaDev *m)
{
    u8 hb[HDR_WORDS * 2];
    s32 h;
    mword(m, -1);
    mflush(m);
    h = xos_open(m->path, 1);
    if (h < 0) { m->err = 1; return; }
    header(m, hb);
    xos_seek(h, 0, 0);
    if (xos_write(h, sizeof hb, hb) != (s32)sizeof hb) m->err = 1;
    xos_close(h);
}

/* vm_filename: carry on in a new file (what is recorded so far moves too) */
static void rename_to(MetaDev *m, const char *name)
{
    char np[XG_PATHLEN];
    s32 hs, hd, k;
    u8 *tmp;
    if (name[0] && name[1] == ':') xstrncpy0(np, name, XG_PATHLEN);
    else if (name[0] == '\\') { np[0] = m->path[0]; np[1] = ':'; xstrncpy0(np + 2, name, XG_PATHLEN - 2); }
    else { xos_getcwd(np, XG_PATHLEN); xstrcat(np, name); }
    if (!xstricmp(np, m->path)) return;
    mflush(m);
    hd = xos_create(np);
    if (hd < 0) { XLOG("metafile: cannot create "); XLOG(np); XLOG("\r\n"); return; }
    tmp = (u8 *)heap_alloc(2048);
    hs = xos_open(m->path, 0);
    if (tmp && hs >= 0)
        while ((k = xos_read(hs, 2048, tmp)) > 0) if (xos_write(hd, k, tmp) != k) { m->err = 1; break; }
    if (hs >= 0) xos_close(hs);
    xos_close(hd);
    heap_free(tmp);
    xstrcpy(m->path, np);
}

/* ---------------- recording ---------------- */

static void grow_ext(MetaDev *m, int x, int y)
{
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x > 32767) x = 32767; if (y > 32767) y = 32767;
    if (!m->any) { m->x0 = m->x1 = (s16)x; m->y0 = m->y1 = (s16)y; m->any = 1; return; }
    if (x < m->x0) m->x0 = (s16)x; if (x > m->x1) m->x1 = (s16)x;
    if (y < m->y0) m->y0 = (s16)y; if (y > m->y1) m->y1 = (s16)y;
}

static void extents(MetaDev *m, VDIPB *pb)
{
    int op = CONTRL[0], i, n = CONTRL[1];
    if (op == 11) {
        int cx = PTSIN[0], cy = PTSIN[1], rx = 0, ry = 0;
        switch (CONTRL[5]) {
        case 2: case 3: rx = ry = PTSIN[6]; break;
        case 4: rx = ry = PTSIN[4]; break;
        case 5: case 6: case 7: rx = PTSIN[2]; ry = PTSIN[3]; break;
        case 10: grow_ext(m, cx, cy); grow_ext(m, cx + PTSIN[2], cy); return;
        default:
            for (i = 0; i < n && i < 2; i++) grow_ext(m, PTSIN[2 * i], PTSIN[2 * i + 1]);
            return;
        }
        grow_ext(m, cx - rx, cy - ry); grow_ext(m, cx + rx, cy + ry);
        return;
    }
    for (i = 0; i < n; i++) grow_ext(m, PTSIN[2 * i], PTSIN[2 * i + 1]);
}

static void record(MetaDev *m, int op, int np, int ni, int sub, const s16 *pts, const s16 *ints)
{
    int i;
    mword(m, (s16)op); mword(m, (s16)np); mword(m, (s16)ni); mword(m, (s16)sub);
    for (i = 0; i < 2 * np; i++) mword(m, pts[i]);
    for (i = 0; i < ni; i++) mword(m, ints[i]);
}

/* calls that only ask something, or carry pointers, are not recorded */
static int recordable(int op, int sub)
{
    switch (op) {
    case 1: case 2: case 26: case 35: case 36: case 37: case 38: case 102:
    case 105: case 109: case 110: case 116: case 117: case 119: case 120:
    case 121: case 130: case 131: case 229: case 232: case 234: case 235:
    case 239: case 240: case 243: case 247: case 249: case 250: case 251:
    case 254: case 255:
        return 0;
    case 5:     /* escapes: graphics ones only */
        return sub == 20 || sub == 22 || sub == 23 || sub == 99;
    }
    return 1;
}

static int draws(int op)
{
    return op == 6 || op == 7 || op == 8 || op == 9 || op == 11 || op == 103 || op == 114 || op == 241;
}

/* ---------------- device ---------------- */

static void work_out(Wk *w, VDIPB *pb)
{
    int i;
    for (i = 0; i < 45; i++) INTOUT[i] = 0;
    INTOUT[0] = 32767; INTOUT[1] = 32767;
    INTOUT[2] = 0;
    INTOUT[3] = 85; INTOUT[4] = 85;
    INTOUT[5] = 0; INTOUT[6] = 7; INTOUT[7] = 0; INTOUT[8] = 6; INTOUT[9] = 0;
    INTOUT[10] = 1; INTOUT[11] = 24; INTOUT[12] = 12;
    INTOUT[13] = 16; INTOUT[14] = 10;
    for (i = 0; i < 10; i++) INTOUT[15 + i] = (s16)(i + 1);
    INTOUT[25] = 3; INTOUT[26] = 0; INTOUT[27] = 3; INTOUT[28] = 3; INTOUT[29] = 3;
    INTOUT[30] = 0; INTOUT[31] = 3; INTOUT[32] = 0; INTOUT[33] = 3; INTOUT[34] = 2;
    INTOUT[35] = 1; INTOUT[36] = 2; INTOUT[37] = 1; INTOUT[38] = 0; INTOUT[39] = 0;
    INTOUT[44] = 0;
    for (i = 0; i < 12; i++) PTSOUT[i] = 0;
    PTSOUT[1] = 1; PTSOUT[3] = 32767; PTSOUT[4] = 1; PTSOUT[6] = 32767;
    PTSOUT[8] = 1; PTSOUT[9] = 1; PTSOUT[10] = 32767; PTSOUT[11] = 32767;
    CONTRL[2] = 6;
    CONTRL[4] = 45;
    (void)w;
}

void meta_open(Wk *w, VDIPB *pb)
{
    MetaDev *m = (MetaDev *)heap_calloc(sizeof *m);
    if (!m) { CONTRL[6] = 0; return; }
    xos_getcwd(m->path, XG_PATHLEN);
    xstrcat(m->path, "GEMFILE.GEM");
    m->rc = (s16)(INTIN[10] == 2 ? 2 : 0);
    if (!start_file(m)) {
        XLOG("metafile: cannot create "); XLOG(m->path); XLOG("\r\n");
        heap_free(m);
        CONTRL[6] = 0;
        return;
    }
    XLOG("metafile: recording to "); XLOG(m->path); XLOG("\r\n");
    w->xdev = m;
    w->xres = 32767; w->yres = 32767;
    w->pw = w->ph = 85;
    w->ppix = w->ppiy = 0;
    w->ncol = 16;
    w->rom_nfonts = 1;
    ga_init(&m->a, w, INTIN);
    work_out(w, pb);
    CONTRL[6] = w->handle;
}

void meta_vdi(Wk *w, VDIPB *pb)
{
    MetaDev *m = (MetaDev *)w->xdev;
    int op = CONTRL[0], sub = CONTRL[5];
    if (!m) return;
    /* metafile escapes */
    if (op == 5 && sub == 98) {                         /* v_meta_extents */
        m->ux0 = XG_MIN(PTSIN[0], PTSIN[2]); m->uy0 = XG_MIN(PTSIN[1], PTSIN[3]);
        m->ux1 = XG_MAX(PTSIN[0], PTSIN[2]); m->uy1 = XG_MAX(PTSIN[1], PTSIN[3]);
        m->user_ext = 1;
        CONTRL[2] = CONTRL[4] = 0;
        return;
    }
    if (op == 5 && sub == 100) {                        /* vm_filename */
        char name[XG_PATHLEN];
        int i;
        for (i = 0; i < CONTRL[3] && i < XG_PATHLEN - 1; i++) name[i] = (char)INTIN[i];
        name[i] = 0;
        if (name[0]) rename_to(m, name);
        CONTRL[2] = CONTRL[4] = 0;
        return;
    }
    if (op == 5 && sub == 99 && CONTRL[3] >= 3 && INTIN[0] == 0) { m->pgw = INTIN[1]; m->pgh = INTIN[2]; }
    if (op == 5 && sub == 99 && CONTRL[3] >= 5 && INTIN[0] == 1) {
        m->cw[0] = INTIN[1]; m->cw[1] = INTIN[2]; m->cw[2] = INTIN[3]; m->cw[3] = INTIN[4];
    }

    if (recordable(op, sub)) {
        if (draws(op)) extents(m, pb);
        if (op == 241)          /* v_ftext: plain v_gtext for every reader */
            record(m, 8, 1, CONTRL[3], 0, PTSIN, INTIN);
        else if (op == 246) {   /* vst_arbpt: also as vst_point for older readers */
            s16 p = (s16)(CONTRL[3] >= 2 ? FIXROUND(GETLONG(INTIN, 0)) : INTIN[0]);
            record(m, 107, 0, 1, 0, NULL, &p);
            record(m, op, CONTRL[1], CONTRL[3], sub, PTSIN, INTIN);
        } else record(m, op, CONTRL[1], CONTRL[3], sub, PTSIN, INTIN);
    }

    CONTRL[2] = 0;
    CONTRL[4] = 0;
    switch (op) {
    case 2:     /* v_clswk */
        finish(m);
        XLOG(m->err ? "metafile: write error\r\n" : "metafile: finished\r\n");
        heap_free(m);
        w->xdev = NULL;
        wk_free(w);
        return;
    case 3: case 4: case 6: case 7: case 8: case 9: case 11: case 103: case 114: case 241:
        return;
    case 5:
        if (sub == 1) { INTOUT[0] = 64; INTOUT[1] = 80; CONTRL[4] = 2; }
        return;
    case 102:
        if (INTIN[0]) {
            int i;
            for (i = 0; i < 45; i++) INTOUT[i] = 0;
            INTOUT[0] = 0; INTOUT[1] = 16; INTOUT[4] = 4; INTOUT[5] = 1;
            INTOUT[6] = 1000; INTOUT[19] = w->clip_on;
            for (i = 0; i < 12; i++) PTSOUT[i] = 0;
            PTSOUT[0] = w->clip[0]; PTSOUT[1] = w->clip[1]; PTSOUT[2] = w->clip[2]; PTSOUT[3] = w->clip[3];
            CONTRL[2] = 6; CONTRL[4] = 45;
        } else work_out(w, pb);
        return;
    case 105:
        INTOUT[0] = INTOUT[1] = 0; CONTRL[4] = 2;
        return;
    }
    if (xg_text_op(w, pb)) return;
    if (ga_op(&m->a, w, pb)) return;
    pdf_builtin_query(w, pb);
}
