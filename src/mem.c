/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* mem.c - XGDOS's own memory device (GDOS devices 61-70, MEMORY.SYS).
 *
 * As with MEMORY.SYS: before v_opnwk the program may set contrl[1] = 1 and
 * put the width and height of the bitmap in ptsin[0] and ptsin[1]
 * (otherwise it gets the size of the screen). The device allocates a one
 * plane bitmap in the program's memory and returns its address in
 * contrl[0] (high word) and contrl[1] (low word). v_clrwk and v_updwk
 * clear it. Everything is drawn by XGDOS's software renderer (soft.c),
 * including Speedo text and XGDOS's own system fonts, so no driver file
 * and no ROM drawing routine is involved.
 *
 * Virtual workstations opened on it share the bitmap and have their own
 * attributes.
 */
#include "soft.h"
#include "text.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"
#include "sysfont.h"

typedef struct MemRas {
    u8  *bits;
    int  refs;
} MemRas;

typedef struct MemDev {
    SoftDev sd;
    MemRas *ras;
} MemDev;

#define CONTRL pb->contrl
#define INTIN  pb->intin
#define PTSIN  pb->ptsin
#define INTOUT pb->intout
#define PTSOUT pb->ptsout

/* the screen the program sees: size and pixel size of a tracked ROM
 * screen workstation, or ST high resolution */
static void screen_like(int *w, int *h, int *pw, int *ph)
{
    int i;
    *w = 640; *h = 400; *pw = 372; *ph = 372;
    for (i = 0; i < XG_MAXWK; i++)
        if (xg_wk[i].kind == WK_ROM && xg_wk[i].dev <= 10 && xg_wk[i].xres > 0) {
            *w = xg_wk[i].xres + 1; *h = xg_wk[i].yres + 1;
            *pw = xg_wk[i].pw; *ph = xg_wk[i].ph;
            return;
        }
}

static void work_out(Wk *w, VDIPB *pb)
{
    int i;
    for (i = 0; i < 45; i++) INTOUT[i] = 0;
    INTOUT[0] = w->xres; INTOUT[1] = w->yres;
    INTOUT[2] = 0;                  /* exact scaling */
    INTOUT[3] = w->pw; INTOUT[4] = w->ph;
    INTOUT[5] = 0;                  /* continuous text sizes (outline fonts) */
    INTOUT[6] = 7;                  /* line types */
    INTOUT[7] = 0;                  /* continuous line widths */
    INTOUT[8] = 6;                  /* marker types */
    INTOUT[9] = 0;
    INTOUT[10] = 1;                 /* faces before vst_load_fonts */
    INTOUT[11] = 24; INTOUT[12] = 12;
    INTOUT[13] = 2;                 /* colours */
    INTOUT[14] = 10;                /* GDPs */
    for (i = 0; i < 10; i++) INTOUT[15 + i] = (s16)(i + 1);
    INTOUT[25] = 3; INTOUT[26] = 0; INTOUT[27] = 3; INTOUT[28] = 3; INTOUT[29] = 3;
    INTOUT[30] = 0; INTOUT[31] = 3; INTOUT[32] = 0; INTOUT[33] = 3; INTOUT[34] = 2;
    INTOUT[35] = 0;                 /* monochrome */
    INTOUT[36] = 1;                 /* text rotation in 90 degree steps */
    INTOUT[37] = 1;                 /* fill area */
    INTOUT[38] = 0;
    INTOUT[39] = 2;
    INTOUT[44] = 0;
    for (i = 0; i < 12; i++) PTSOUT[i] = 0;
    PTSOUT[1] = 5; PTSOUT[2] = 0; PTSOUT[3] = 2000;
    PTSOUT[4] = 1; PTSOUT[6] = 99;
    PTSOUT[8] = 1; PTSOUT[9] = 1; PTSOUT[10] = 99; PTSOUT[11] = 99;
    CONTRL[2] = 6;
    CONTRL[4] = 45;
}

static int new_handle(void)
{
    int h, i;
    for (h = 450, i = 0; i < 50 && wk_find(h); h++, i++) ;
    return i < 50 ? h : 0;
}

void mem_open(Wk *w, VDIPB *pb)
{
    MemDev *m;
    int wd, ht, pw, ph;
    s32 rb, size;
    u8 *bits;
    screen_like(&wd, &ht, &pw, &ph);
    if (CONTRL[1] >= 1 && PTSIN[0] > 0 && PTSIN[1] > 0) { wd = PTSIN[0]; ht = PTSIN[1]; }
    if (wd > 8192) wd = 8192;
    if (ht > 8192) ht = 8192;
    rb = (s32)((wd + 15) >> 4) << 1;
    size = rb * ht;
    m = (MemDev *)heap_calloc(sizeof *m);
    if (m) m->ras = (MemRas *)heap_calloc(sizeof *m->ras);
    bits = (m && m->ras) ? (u8 *)xos_alloc(size) : NULL;
    if (!bits) {
        if (m) heap_free(m->ras);
        heap_free(m);
        CONTRL[6] = 0;
        XLOG("memory device: no memory for the bitmap\r\n");
        return;
    }
    xmemset(bits, 0, size);
    m->ras->bits = bits;
    m->ras->refs = 1;
    m->sd.s.bits = bits; m->sd.s.rb = rb; m->sd.s.w = wd; m->sd.s.h = ht;
    w->xdev = m;
    w->xres = (s16)(wd - 1); w->yres = (s16)(ht - 1);
    w->pw = (s16)pw; w->ph = (s16)ph;
    /* same text sizes as XGDOS gives the screen (91 dpi on ST high) */
    w->ppix = muldiv(INT2FIX(25400), INT2FIX(4), INT2FIX(3 * (s32)pw));
    w->ppiy = muldiv(INT2FIX(25400), INT2FIX(4), INT2FIX(3 * (s32)ph));
    w->ncol = 2;
    w->rom_nfonts = 1;
    ga_init(&m->sd.a, w, INTIN);
    work_out(w, pb);
    CONTRL[6] = w->handle;
    /* the raster address, as MEMORY.SYS returns it */
    SETPTR(CONTRL, 0, bits);
}

/* v_opnvwk on the memory device: shares the bitmap */
int mem_open_virtual(Wk *phys, VDIPB *pb)
{
    MemDev *pm = (MemDev *)phys->xdev, *m;
    Wk *w;
    int h = new_handle();
    if (!pm || !h) { CONTRL[6] = 0; return 0; }
    m = (MemDev *)heap_calloc(sizeof *m);
    w = m ? wk_alloc(h, WK_MEM) : NULL;
    if (!w) { heap_free(m); CONTRL[6] = 0; return 0; }
    m->sd.s = pm->sd.s;
    m->ras = pm->ras;
    m->ras->refs++;
    w->xdev = m;
    w->dev = phys->dev; w->phys = phys->handle;
    w->xres = phys->xres; w->yres = phys->yres;
    w->pw = phys->pw; w->ph = phys->ph;
    w->ppix = phys->ppix; w->ppiy = phys->ppiy;
    w->ncol = 2;
    w->rom_nfonts = 1;
    ga_init(&m->sd.a, w, INTIN);
    work_out(w, pb);
    CONTRL[6] = (s16)h;
    return 1;
}

int mem_new_handle(void) { return new_handle(); }

static void mem_close(Wk *w)
{
    MemDev *m = (MemDev *)w->xdev;
    if (m) {
        if (m->ras && --m->ras->refs <= 0) {
            xos_free(m->ras->bits);
            heap_free(m->ras);
        }
        heap_free(m);
    }
    w->xdev = NULL;
    wk_free(w);
}

/* system font sizes, as the ST VDI picks them: vst_point takes the
 * largest of 8, 9, 10, 16, 18, 20 points at or below the request;
 * vst_height the largest cell top (4, 6, 13, doubled 8, 12, 26) */
static void sys_size(Wk *w, VDIPB *pb, int by_height)
{
    TxtMetrics m;
    int i, best = 0;
    if (by_height) {
        int h = INTIN[0] > 0 ? INTIN[0] : 1;
        for (i = 0; i < 6; i++) {
            int top = xg_sysfonts[soft_sys_sizes[i][0]].top * soft_sys_sizes[i][1];
            int bt = xg_sysfonts[soft_sys_sizes[best][0]].top * soft_sys_sizes[best][1];
            if (top <= h && top >= bt) best = i;
        }
    } else {
        int p = INTIN[0] > 0 ? INTIN[0] : 1;
        for (i = 0; i < 6; i++)
            if (soft_sys_sizes[i][2] <= p && soft_sys_sizes[i][2] >= soft_sys_sizes[best][2]) best = i;
        INTOUT[0] = soft_sys_sizes[best][2];
        CONTRL[4] = 1;
    }
    w->t.pt = INT2FIX(soft_sys_sizes[best][2]);
    soft_sysfont_metrics(w, &m);
    PTSOUT[0] = (s16)m.charw; PTSOUT[1] = (s16)m.charh;
    PTSOUT[2] = (s16)m.cellw; PTSOUT[3] = (s16)m.cellh;
    CONTRL[2] = 2;
}

void mem_vdi(Wk *w, VDIPB *pb)
{
    MemDev *m = (MemDev *)w->xdev;
    int op = CONTRL[0];
    CONTRL[2] = 0;
    CONTRL[4] = 0;
    if (!m) return;
    switch (op) {
    case 2:     /* v_clswk */
    case 101:   /* v_clsvwk */
        mem_close(w);
        return;
    case 3:     /* v_clrwk */
    case 4:     /* v_updwk: MEMORY.SYS clears the raster here too */
        soft_clear(&m->sd);
        return;
    case 5:
        if (CONTRL[5] == 1) {       /* vq_chcells */
            TxtMetrics t;
            soft_sysfont_metrics(w, &t);
            INTOUT[0] = (s16)((w->yres + 1) / (t.cellh > 0 ? t.cellh : 16));
            INTOUT[1] = (s16)((w->xres + 1) / (t.cellw > 0 ? t.cellw : 8));
            CONTRL[4] = 2;
        }
        return;
    case 102:   /* vq_extnd */
        if (INTIN[0]) {
            int i;
            for (i = 0; i < 45; i++) INTOUT[i] = 0;
            INTOUT[0] = 4;          /* screen-like raster device */
            INTOUT[1] = 2; INTOUT[4] = 1; INTOUT[5] = 1;
            INTOUT[6] = 1000; INTOUT[8] = 2; INTOUT[9] = 4;
            INTOUT[19] = w->clip_on;
            for (i = 0; i < 12; i++) PTSOUT[i] = 0;
            PTSOUT[0] = w->clip[0]; PTSOUT[1] = w->clip[1]; PTSOUT[2] = w->clip[2]; PTSOUT[3] = w->clip[3];
            CONTRL[2] = 6; CONTRL[4] = 45;
        } else work_out(w, pb);
        return;
    case 12:    /* vst_height */
        if (w->t.fi < 0) { sys_size(w, pb, 1); return; }
        break;
    case 107:   /* vst_point */
        if (w->t.fi < 0) { sys_size(w, pb, 0); return; }
        break;
    }
    if (soft_vdi(&m->sd, w, pb)) return;
    if (xg_text_op(w, pb)) return;
    if (ga_op(&m->sd.a, w, pb)) return;
    pdf_builtin_query(w, pb);       /* system font queries (metrics from soft.c) */
}
