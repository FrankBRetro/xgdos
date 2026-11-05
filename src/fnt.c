/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* fnt.c - GEM bitmap fonts (.FNT) listed in ASSIGN.SYS for screen devices.
 * They are loaded once into the XGDOS heap, converted to Motorola byte
 * order, relocated and chained; vst_load_fonts hands the chain to the
 * screen VDI in contrl[7..8], exactly as classic GDOS does.
 */
#include "vdi.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"

#define MAXCHAIN 10

static void *chain[MAXCHAIN + 1];   /* index: device id 1..10 */

static u16 rw16(const u8 *p, int intel) { return intel ? (u16)(p[0] | (p[1] << 8)) : (u16)((p[0] << 8) | p[1]); }
static u32 rd32(const u8 *p, int intel)
{
    return intel ? ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24))
                 : (((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]);
}
static void ww16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void ww32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static u8 *load_fnt(const char *path)
{
    s32 h, n = xos_filesize(path), i;
    u8 *b;
    int intel;
    u16 first, last, flags, fw, fh;
    u32 hor, off, dat;
    if (n < 88) return NULL;
    b = (u8 *)heap_alloc(n);
    if (!b) return NULL;
    h = xos_open(path, 0);
    if (h < 0 || xos_read(h, n, b) != n) { xos_close(h); heap_free(b); return NULL; }
    xos_close(h);
    /* Motorola files have bit 2 of flags set; check plausibility */
    intel = !(b[67] & 4);
    first = rw16(b + 36, intel); last = rw16(b + 38, intel);
    if (first > last || last > 255) { intel = !intel; first = rw16(b + 36, intel); last = rw16(b + 38, intel); }
    if (first > last || last > 255) { heap_free(b); return NULL; }
    if (intel) {
        for (i = 0; i < 4; i += 2) ww16(b + i, rw16(b + i, 1));
        for (i = 36; i < 68; i += 2) ww16(b + i, rw16(b + i, 1));
        ww16(b + 66, (u16)(rw16(b + 66, 0) | 4));
        for (i = 68; i < 80; i += 4) ww32(b + i, rd32(b + i, 1));
        for (i = 80; i < 84; i += 2) ww16(b + i, rw16(b + i, 1));
    }
    flags = rw16(b + 66, 0);
    hor = rd32(b + 68, 0); off = rd32(b + 72, 0); dat = rd32(b + 76, 0);
    fw = rw16(b + 80, 0); fh = rw16(b + 82, 0);
    if (off + 2u * (last - first + 2) > (u32)n || dat + (u32)fw * fh > (u32)n) { heap_free(b); return NULL; }
    if (intel) {
        for (i = 0; i < last - first + 2; i++) ww16(b + off + 2 * i, rw16(b + off + 2 * i, 1));
        if ((flags & 2) && hor && hor + 2u * (last - first + 1) <= (u32)n)
            for (i = 0; i < last - first + 1; i++) ww16(b + hor + 2 * i, rw16(b + hor + 2 * i, 1));
        for (i = 0; i + 1 < (s32)fw * fh; i += 2) { u8 t = b[dat + i]; b[dat + i] = b[dat + i + 1]; b[dat + i + 1] = t; }
    }
    ww32(b + 68, hor ? (u32)(unsigned long)(b + hor) : 0);
    ww32(b + 72, (u32)(unsigned long)(b + off));
    ww32(b + 76, (u32)(unsigned long)(b + dat));
    ww32(b + 84, 0);
    ww16(b + 66, (u16)(flags & ~1));     /* never a system font */
    return b;
}

void fnt_load_all(void)
{
    int i, k;
    char path[XG_PATHLEN];
    for (i = 0; i < xg_cfg.ndev; i++) {
        const XgDev *d = &xg_cfg.dev[i];
        u8 *last = NULL;
        if (d->id < 1 || d->id > MAXCHAIN || chain[d->id]) continue;
        for (k = 0; k < d->fnt_count; k++) {
            u8 *f;
            xpath_join(path, xg_cfg.path, xg_cfg.fnt[d->fnt_first + k]);
            f = load_fnt(path);
            if (!f) continue;
            if (last) ww32(last + 84, (u32)(unsigned long)f);
            else chain[d->id] = f;
            last = f;
        }
    }
}

void *fnt_chain(int dev)
{
    return (dev >= 1 && dev <= MAXCHAIN) ? chain[dev] : NULL;
}
