/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* soft.h - software VDI renderer for one-plane bitmaps (memory device),
 * and the graphics attribute state shared by the native devices.
 *
 * Pixels are 1 bit, most significant bit first, rows padded to whole
 * words like the ST screen. Colour index 0 is white (bit clear), every
 * other index is black (bit set).
 */
#ifndef SOFT_H
#define SOFT_H
#include "vdi.h"

/* line, marker and fill attributes (text attributes live in Wk.t) */
typedef struct GAttr {
    s16 l_type, l_width, l_color, l_beg, l_end;
    u16 l_udsty;
    s16 m_type, m_height, m_color;
    s16 f_interior, f_style, f_color, f_perim;
    u16 udpat[16];
    s16 pal[16][3];
} GAttr;

void ga_init(GAttr *a, Wk *w, const s16 *work_in);
/* attribute setters and queries; 1 if the call was handled */
int  ga_op(GAttr *a, Wk *w, VDIPB *pb);

typedef struct Surf {
    u8  *bits;
    s32  rb;                  /* bytes per row (even) */
    int  w, h;
} Surf;

/* how a primitive paints: colours, writing mode, 16x16 pattern */
typedef struct Ink {
    int fg, bg;               /* 1 = black, 0 = white */
    int mode;                 /* VDI writing mode 1..4 */
    const u16 *pat;           /* NULL = solid */
} Ink;

typedef struct SoftDev {
    Surf s;
    GAttr a;
    int cx0, cy0, cx1, cy1;   /* clip rectangle, inclusive */
} SoftDev;

/* the whole VDI drawing set on a surface; 1 if the call was handled */
int  soft_vdi(SoftDev *d, Wk *w, VDIPB *pb);
void soft_clip(SoftDev *d, Wk *w);
void soft_clear(SoftDev *d);
/* raster copies with MFDB fd_addr == NULL meaning the device surface */
void soft_cpyfm(SoftDev *d, VDIPB *pb, int transparent);
/* system font text metrics for the memory device (ROM bitmap fonts) */
int  soft_sysfont_metrics(Wk *w, struct TxtMetrics *m);
extern const s16 soft_sys_sizes[6][3];   /* font, scale, points (8 9 10 16 18 20) */

/* Line-A font headers of the ROM (6x6, 8x8, 8x16); set at install */
extern const u8 *xg_sysfont[3];

#endif
