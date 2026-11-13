/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* vdi.h - VDI parameter block, per-workstation state */
#ifndef VDI_H
#define VDI_H
#include "xgdos.h"

typedef struct VDIPB {
    s16 *contrl, *intin, *ptsin, *intout, *ptsout;
} VDIPB;

enum { WK_FREE = 0, WK_ROM, WK_PDF, WK_DRV, WK_META, WK_MEM };

typedef struct TextAttr {
    s16 font_id;      /* last vst_font request */
    s16 fi;           /* outline font index, -1 = ROM/builtin font active */
    fix pt;           /* em size, points 16.16 */
    fix wpt;          /* vst_setsize width, 0 = same as pt */
    s16 skew, rot;    /* 1/10 degree */
    s16 halign, valign, effects, color, wrmode;
    s16 track, pairs, charmap;
} TextAttr;

struct PdfDoc;

typedef struct Wk {
    s16 kind, handle, dev, phys;
    s16 xres, yres;           /* max x / max y */
    s16 pw, ph;               /* pixel size in micrometres */
    fix ppix, ppiy;           /* exact pixels per inch (0 = from pw/ph) */
    s16 ncol;                 /* colours available (work_out[13]) */
    s16 clip_on, clip[4];
    s16 fonts_loaded;
    s16 rom_nfonts;           /* system + bitmap faces known to the ROM VDI */
    s16 snap_n;
    u8  snap[XG_MAXFONTS];    /* outline fonts visible on this workstation */
    TextAttr t;
    struct PdfDoc *pdf;
    s32 owner;                /* MiNT pid that opened it */
    void *drv_entry;          /* external driver (WK_DRV) */
    void *drvbp;
    s16 drvh;
    void *xdev;               /* metafile / memory device state */
} Wk;

typedef void (*TxtBlitFn)(struct Wk *w, const u8 *bits, int rb, int x, int y, int bw, int bh, int opaque);

#define XG_MAXWK 32
extern Wk xg_wk[XG_MAXWK];

Wk  *wk_find(int handle);
Wk  *wk_alloc(int handle, int kind);
void wk_free(Wk *w);
void wk_text_defaults(Wk *w);
void wk_snapshot_fonts(Wk *w);

/* call the VDI that was installed before XGDOS (ROM VDI, NVDI, XGEM...) */
void rom_vdi(VDIPB *pb);

typedef struct MFDB {
    void *fd_addr;
    s16 fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} MFDB;

/* 32 bit values in VDI word arrays: high word first on the Atari; on the
 * PC (far pointers and longs, as GEM and its bindings store them) low word
 * first, so a pointer's offset comes before its segment */
#ifdef XG_PC
#define GETLONG(a, i)    ((s32)(((u32)(u16)(a)[(i) + 1] << 16) | (u16)(a)[i]))
#define SETLONG(a, i, v) do { s32 _v = (s32)(v); (a)[(i) + 1] = (s16)(_v >> 16); (a)[i] = (s16)_v; } while (0)
#else
#define GETLONG(a, i)    ((s32)(((u32)(u16)(a)[i] << 16) | (u16)(a)[(i) + 1]))
#define SETLONG(a, i, v) do { s32 _v = (s32)(v); (a)[i] = (s16)(_v >> 16); (a)[(i) + 1] = (s16)_v; } while (0)
#endif
#ifdef XG_HOST   /* 64 bit host tests: pointers go through a handle table */
void *host_ptr_get(u32 key);
u32   host_ptr_put(const void *p);
#define GETPTR(a, i)     host_ptr_get((u32)GETLONG(a, i))
#define SETPTR(a, i, p)  SETLONG(a, i, host_ptr_put(p))
#else
#define GETPTR(a, i)     ((void *)(unsigned long)(u32)GETLONG(a, i))
#define SETPTR(a, i, p)  SETLONG(a, i, (s32)(unsigned long)(p))
#endif

/* vdi.c entry point, called by the trap #2 handler for d0 = 115 */
void xg_vdi(VDIPB *pb);
/* decides first (caller stack, no lock): 0 = pass the call on untouched */
int  xg_vdi_need(VDIPB *pb);
/* pdf.c */
void pdf_open(Wk *w, VDIPB *pb);
void pdf_vdi(Wk *w, VDIPB *pb);
/* shared text attribute/query handlers (vdi.c), used by pdf.c too.
 * Returns 1 if the call was completely handled. */
int  xg_text_op(Wk *w, VDIPB *pb);
struct TxtMetrics;
int  pdf_builtin_metrics(Wk *w, struct TxtMetrics *m);
void pdf_builtin_query(Wk *w, VDIPB *pb);
void sysfont_name(s16 *intout);           /* "System Font" into intout[1..32] */
/* fnt.c - bitmap .FNT chains from ASSIGN.SYS */
void *fnt_chain(int dev);
void  fnt_load_all(void);
/* meta.c - metafile device (31-40); mem.c - memory device (61-70) */
void  meta_open(Wk *w, VDIPB *pb);
void  meta_vdi(Wk *w, VDIPB *pb);
void  mem_open(Wk *w, VDIPB *pb);
int   mem_open_virtual(Wk *phys, VDIPB *pb);
int   mem_new_handle(void);
void  mem_vdi(Wk *w, VDIPB *pb);
/* drv.c - external .SYS drivers */
int   drv_open(VDIPB *pb, const char *drv);
void  drv_call(Wk *w, VDIPB *pb);

#endif
