/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* hostvdi.c - fake "ROM VDI" for host tests: 640x400 mono screen,
 * system font only; vrt_cpyfm renders into a framebuffer. */
#include <stdio.h>
#include <string.h>
#include "../src/vdi.h"

#define FW 640
#define FH 400
unsigned char fb[FH][FW];
static int nexth = 1;
int rom_text_calls;

void *fnt_chain(int dev) { (void)dev; return NULL; }
void fnt_load_all(void) {}
int drv_open(VDIPB *pb, const char *drv) { (void)drv; pb->contrl[6] = 0; return 0; }
void drv_call(Wk *w, VDIPB *pb) { (void)w; (void)pb; }

void rom_vdi(VDIPB *pb)
{
    s16 *c = pb->contrl;
    int i;
    switch (c[0]) {
    case 102:
        for (i = 0; i < 45; i++) pb->intout[i] = 0;
        pb->intout[0] = FW - 1; pb->intout[1] = FH - 1; pb->intout[3] = 372; pb->intout[4] = 372;
        pb->intout[10] = 1; pb->intout[13] = 2;
        break;
    case 38: pb->intout[0] = 1; pb->intout[1] = 1; pb->intout[2] = 0; pb->intout[3] = 0; pb->intout[4] = 0; pb->intout[5] = 1; break;
    case 1: case 100:
        for (i = 0; i < 45; i++) pb->intout[i] = 0;
        pb->intout[0] = FW - 1; pb->intout[1] = FH - 1; pb->intout[3] = 372; pb->intout[4] = 372;
        pb->intout[10] = 1; pb->intout[13] = 2;
        c[6] = (s16)nexth++;
        break;
    case 121: {
        MFDB *s = (MFDB *)GETPTR(c, 7);
        s16 *p = pb->ptsin;
        int mode = pb->intin[0], x, y;
        int w = p[2] - p[0] + 1, h = p[3] - p[1] + 1;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            const unsigned char *row = (const unsigned char *)s->fd_addr + (y + p[1]) * s->fd_wdwidth * 2;
            int bit = (row[(x + p[0]) >> 3] >> (7 - ((x + p[0]) & 7))) & 1;
            int X = p[4] + x, Y = p[5] + y;
            if (X < 0 || Y < 0 || X >= FW || Y >= FH) continue;
            if (mode == 1) fb[Y][X] = bit ? 1 : 2;      /* 2 = background painted */
            else if (mode == 2) { if (bit) fb[Y][X] = 1; }
            else if (mode == 3) { if (bit) fb[Y][X] ^= 1; }
        }
        break;
    }
    case 8: rom_text_calls++; break;
    case 12: pb->ptsout[0] = 7; pb->ptsout[1] = 13; pb->ptsout[2] = 8; pb->ptsout[3] = 16; break;
    case 107: pb->intout[0] = pb->intin[0]; pb->ptsout[0] = 8; pb->ptsout[1] = 13; pb->ptsout[2] = 8; pb->ptsout[3] = 16; break;
    case 21: pb->intout[0] = 1; break;
    case 22: case 32: pb->intout[0] = pb->intin[0]; break;
    case 39: pb->intout[0] = pb->intin[0]; pb->intout[1] = pb->intin[1]; break;
    case 130: pb->intout[0] = 1; memset(pb->intout + 1, 0, 64); { const char *n = "6x6 system font"; for (i = 0; n[i]; i++) pb->intout[1 + i] = n[i]; } break;
    default: break;
    }
}

void fb_save(const char *fn)
{
    FILE *f = fopen(fn, "wb");
    int x, y;
    fprintf(f, "P5 %d %d 255\n", FW, FH);
    for (y = 0; y < FH; y++) for (x = 0; x < FW; x++) fputc(fb[y][x] == 1 ? 0 : fb[y][x] == 2 ? 230 : 255, f);
    fclose(f);
}

static const void *ptab[4096];
static unsigned pnext = 1;
u32 host_ptr_put(const void *p)
{
    unsigned i;
    if (!p) return 0;
    for (i = 1; i < pnext; i++) if (ptab[i] == p) return 0x40000000u | i;
    ptab[pnext] = p;
    return 0x40000000u | pnext++;
}
void *host_ptr_get(u32 k) { return k ? (void *)ptab[k & 0xfff] : NULL; }

int fb_count(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = y0; y < y1 && y < FH; y++) for (x = x0; x < x1 && x < FW; x++) n += fb[y][x] == 1;
    return n;
}
