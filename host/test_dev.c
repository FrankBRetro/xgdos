/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* test_dev.c - XGDOS's own memory (61) and metafile (31) devices, driven
 * through xg_vdi() like a GEM application. Writes out_memory.pgm and
 * hostfs/C/TEST.GEM, then reads the metafile back and checks it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vdi.h"
#include "../src/heap.h"
#include "../src/xos.h"
#include "../src/xstd.h"

static s16 contrl[16], intin[1024], ptsin[1024], intout[512], ptsout[64];
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void vdi(int op, int sub, int h, int np, int ni)
{
    VDIPB pb = { contrl, intin, ptsin, intout, ptsout };
    memset(contrl, 0, sizeof contrl);
    contrl[0] = (s16)op; contrl[1] = (s16)np; contrl[3] = (s16)ni; contrl[5] = (s16)sub; contrl[6] = (s16)h;
    if (xg_vdi_need(&pb)) xg_vdi(&pb); else rom_vdi(&pb);
}
static int str(const char *s, s16 *d) { int n = 0; while (*s) d[n++] = (u8)*s++; return n; }
static void text(int h, int x, int y, const char *s) { int n = str(s, intin); ptsin[0] = x; ptsin[1] = y; vdi(8, 0, h, 1, n); }
static void i1(int op, int h, int v) { intin[0] = v; vdi(op, 0, h, 0, 1); }
static void box(int h, int sub, int x0, int y0, int x1, int y1) { ptsin[0] = x0; ptsin[1] = y0; ptsin[2] = x1; ptsin[3] = y1; vdi(11, sub, h, 2, 0); }

static void save_pgm(const char *fn, const u8 *b, int w, int h, int rb)
{
    FILE *f = fopen(fn, "wb");
    int x, y;
    fprintf(f, "P5 %d %d 255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) fputc((b[y * rb + (x >> 3)] & (0x80 >> (x & 7))) ? 0 : 255, f);
    fclose(f);
}

static int count(const u8 *b, int rb, int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) n += (b[y * rb + (x >> 3)] >> (7 - (x & 7))) & 1;
    return n;
}

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

int main(void)
{
    static char assign[4096];
    FILE *f;
    int n, h, i, nf, sp, ids[8] = { 0 };
    u8 *bits;
    const int W = 640, H = 400, RB = 80;

    cfg_defaults(&xg_cfg, 2);
    f = fopen("hostfs/C/ASSIGN.SYS", "rb"); n = (int)fread(assign, 1, sizeof assign, f); fclose(f);
    cfg_parse(&xg_cfg, assign, n);
    heap_init(malloc((s32)xg_cfg.heap_kb * 1024), (s32)xg_cfg.heap_kb * 1024);
    xg_scratch_size = 24 * 1024; xg_scratch = malloc(xg_scratch_size);
    printf("fonts: %d\n", fm_init());

    /* ---------------- memory device ---------------- */
    for (i = 0; i < 11; i++) intin[i] = 1;
    intin[0] = 61; intin[10] = 2;
    memset(contrl, 0, sizeof contrl);
    {
        VDIPB pb = { contrl, intin, ptsin, intout, ptsout };
        contrl[0] = 1; contrl[1] = 1; contrl[3] = 11; ptsin[0] = W; ptsin[1] = H;
        xg_vdi(&pb);
        h = contrl[6];
        bits = (u8 *)GETPTR(contrl, 0);
    }
    printf("memory device: handle %d, %dx%d, %d colours, pixel %dx%d um, raster %s\n",
           h, intout[0] + 1, intout[1] + 1, intout[13], intout[3], intout[4], bits ? "returned" : "MISSING");
    CHECK(h > 0 && bits && intout[0] == W - 1 && intout[1] == H - 1, "memory device open");
    if (!(h > 0 && bits)) return 1;
    vdi(119, 0, h, 0, 0); nf = intout[0];
    for (i = 0; i < nf && i < 6; i++) { intin[0] = i + 2; vdi(130, 0, h, 0, 1); ids[i] = intout[0]; }
    printf("  vst_load_fonts: %d outline fonts\n", nf);

    /* lines: styles, widths, ends */
    for (i = 1; i <= 6; i++) {
        i1(15, h, i);
        ptsin[0] = 10; ptsin[1] = 10 + i * 6; ptsin[2] = 150; ptsin[3] = 10 + i * 6; vdi(6, 0, h, 2, 0);
    }
    i1(15, h, 1);
    { int wd[3] = { 3, 7, 11 };
      for (i = 0; i < 3; i++) {
          ptsin[0] = wd[i]; ptsin[1] = 0; vdi(16, 0, h, 1, 0);
          intin[0] = i; intin[1] = (i + 1) % 3; vdi(108, 0, h, 0, 2);
          ptsin[0] = 20; ptsin[1] = 70 + i * 22; ptsin[2] = 90; ptsin[3] = 60 + i * 22; ptsin[4] = 150; ptsin[5] = 75 + i * 22;
          vdi(6, 0, h, 3, 0);
      } }
    ptsin[0] = 1; vdi(16, 0, h, 1, 0); intin[0] = 1; intin[1] = 1; vdi(108, 0, h, 0, 2);
    ptsin[0] = 20; ptsin[1] = 140; ptsin[2] = 150; ptsin[3] = 140; vdi(6, 0, h, 2, 0);    /* thin with arrows */
    intin[0] = 0; intin[1] = 0; vdi(108, 0, h, 0, 2);

    /* markers */
    for (i = 1; i <= 6; i++) {
        i1(18, h, i); ptsin[0] = 0; ptsin[1] = 9; vdi(19, 0, h, 1, 0);
        ptsin[0] = 20 + i * 20; ptsin[1] = 160; vdi(7, 0, h, 1, 0);
    }

    /* fills: solid, dither, pattern, hatch, user, hollow */
    { int inter[6] = { 1, 2, 2, 3, 4, 0 }, sty[6] = { 1, 4, 9, 3, 1, 1 };
      static const u16 ud[16] = { 0x8001,0x4002,0x2004,0x1008,0x0810,0x0420,0x0240,0x0180,0x0180,0x0240,0x0420,0x0810,0x1008,0x2004,0x4002,0x8001 };
      for (i = 0; i < 16; i++) intin[i] = (s16)ud[i];
      vdi(112, 0, h, 0, 16);
      for (i = 0; i < 6; i++) {
          i1(23, h, inter[i]); i1(24, h, sty[i]);
          box(h, 1, 170 + i * 45, 10, 205 + i * 45, 60);
      } }
    i1(23, h, 2); i1(24, h, 4);
    /* polygon (a star) */
    { static const s16 star[10] = { 230, 70, 245, 120, 200, 90, 260, 90, 215, 120 };
      memcpy(ptsin, star, sizeof star); vdi(9, 0, h, 5, 0); }
    i1(23, h, 1);
    ptsin[0] = 320; ptsin[1] = 100; ptsin[4] = 28; vdi(11, 4, h, 3, 0);                  /* v_circle */
    ptsin[0] = 400; ptsin[1] = 100; ptsin[2] = 40; ptsin[3] = 20; vdi(11, 5, h, 2, 0);  /* v_ellipse */
    i1(23, h, 2); i1(24, h, 2);
    ptsin[0] = 480; ptsin[1] = 100; ptsin[6] = 30; intin[0] = 300; intin[1] = 1500; vdi(11, 3, h, 4, 2);   /* pie */
    ptsin[0] = 560; ptsin[1] = 100; ptsin[2] = 40; ptsin[3] = 25; intin[0] = 0; intin[1] = 2700; vdi(11, 7, h, 2, 2);  /* ellpie */
    ptsin[0] = 2; vdi(16, 0, h, 1, 0);
    ptsin[0] = 320; ptsin[1] = 170; ptsin[6] = 25; intin[0] = 0; intin[1] = 1800; vdi(11, 2, h, 4, 2);    /* arc */
    ptsin[0] = 400; ptsin[1] = 170; ptsin[2] = 35; ptsin[3] = 18; intin[0] = 900; intin[1] = 3600; vdi(11, 6, h, 2, 2);
    ptsin[0] = 1; vdi(16, 0, h, 1, 0);
    box(h, 8, 450, 145, 520, 190);                                                          /* v_rbox */
    i1(23, h, 3); i1(24, h, 5);
    box(h, 9, 540, 145, 620, 190);                                                          /* v_rfbox */

    /* contour fill: a closed ring, then fill inside it */
    i1(23, h, 0);
    ptsin[0] = 600; ptsin[1] = 260; ptsin[4] = 25; vdi(11, 4, h, 3, 0);
    i1(23, h, 2); i1(24, h, 6);
    ptsin[0] = 600; ptsin[1] = 260; intin[0] = 1; vdi(103, 0, h, 1, 1);
    { int inside = count(bits, RB, 590, 250, 610, 270);
      CHECK(inside > 100 && inside < 441, "contour fill filled the ring with a pattern"); }

    /* system font text */
    i1(32, h, 2);
    i1(107, h, 8); text(h, 10, 200, "8 pt small font: Hello \x81\x84\x94");
    printf("  system 8pt cell %dx%d\n", ptsout[2], ptsout[3]);
    i1(107, h, 9); text(h, 10, 214, "9 pt 8x8 font: Gr\x81\x9e Gott \x9c 12");
    i1(107, h, 10); text(h, 10, 234, "10 pt 8x16 font: The quick brown fox");
    printf("  system 10pt cell %dx%d\n", ptsout[2], ptsout[3]);
    CHECK(ptsout[2] == 8 && ptsout[3] == 16, "10 pt is the 8x16 font");
    i1(106, h, 1); text(h, 10, 254, "Bold"); i1(106, h, 4); text(h, 70, 254, "Italic");
    i1(106, h, 8); text(h, 150, 254, "Underlined"); i1(106, h, 16); text(h, 250, 254, "Outline");
    i1(106, h, 2); text(h, 340, 254, "Light"); i1(106, h, 0);
    i1(107, h, 16); printf("  system 16pt cell %dx%d (6x6 doubled)\n", ptsout[2], ptsout[3]);
    CHECK(ptsout[2] == 12 && ptsout[3] == 12, "16 pt is the 6x6 font doubled, as on TOS");
    i1(107, h, 30); printf("  system 30pt -> %d pt, cell %dx%d\n", intout[0], ptsout[2], ptsout[3]);
    CHECK(intout[0] == 20 && ptsout[3] == 32, "30 pt falls back to 20 pt (8x16 doubled)");
    i1(12, h, 8); printf("  vst_height 8 -> cell %dx%d\n", ptsout[2], ptsout[3]);
    i1(107, h, 20); text(h, 10, 290, "20 pt");
    i1(13, h, 900); i1(107, h, 10); text(h, 630, 380, "Up"); i1(13, h, 0);
    /* outline font text */
    if (ids[0]) {
        i1(21, h, ids[0]); i1(107, h, 18); text(h, 120, 290, "Speedo text in memory");
        i1(21, h, ids[1] ? ids[1] : ids[0]); i1(107, h, 12); i1(106, h, 8); text(h, 120, 312, "Underlined 12 pt"); i1(106, h, 0);
        i1(21, h, 1);
    }
    /* raster copy into the device (memory form -> device, transparent) */
    { static u16 img[32 * 2]; MFDB src, dst; int y;
      for (y = 0; y < 32; y++) { img[y * 2] = (u16)(y & 1 ? 0xAAAA : 0x5555); img[y * 2 + 1] = (u16)(0xffff >> (y / 2)); }
      for (y = 0; y < 64; y++) img[y] = (u16)((img[y] >> 8) | (img[y] << 8));
      src.fd_addr = img; src.fd_w = 32; src.fd_h = 32; src.fd_wdwidth = 2; src.fd_stand = 0; src.fd_nplanes = 1;
      memset(&dst, 0, sizeof dst);
      { VDIPB pb = { contrl, intin, ptsin, intout, ptsout };
        memset(contrl, 0, sizeof contrl);
        contrl[0] = 109; contrl[1] = 4; contrl[3] = 1; contrl[6] = h;
        ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = 31; ptsin[3] = 31; ptsin[4] = 450; ptsin[5] = 300; ptsin[6] = 481; ptsin[7] = 331;
        intin[0] = 3; SETPTR(contrl, 7, &src); SETPTR(contrl, 9, &dst); xg_vdi(&pb);
        memset(contrl, 0, sizeof contrl);
        contrl[0] = 121; contrl[1] = 4; contrl[3] = 3; contrl[6] = h;
        ptsin[4] = 500; ptsin[5] = 300; ptsin[6] = 531; ptsin[7] = 331;
        intin[0] = 2; intin[1] = 1; intin[2] = 0; SETPTR(contrl, 7, &src); SETPTR(contrl, 9, &dst); xg_vdi(&pb);
        /* device -> device copy of the circle */
        memset(contrl, 0, sizeof contrl);
        contrl[0] = 109; contrl[1] = 4; contrl[3] = 1; contrl[6] = h;
        ptsin[0] = 290; ptsin[1] = 70; ptsin[2] = 350; ptsin[3] = 130; ptsin[4] = 370; ptsin[5] = 300; ptsin[6] = 430; ptsin[7] = 360;
        intin[0] = 3; SETPTR(contrl, 7, &dst); SETPTR(contrl, 9, &dst); xg_vdi(&pb); }
      CHECK(count(bits, RB, 450, 300, 481, 331) > 300, "vro_cpyfm drew the image");
      CHECK(count(bits, RB, 370, 300, 430, 360) == count(bits, RB, 290, 70, 350, 130), "device to device copy is exact"); }
    /* clipping */
    ptsin[0] = 20; ptsin[1] = 330; ptsin[2] = 120; ptsin[3] = 360; intin[0] = 1; vdi(129, 0, h, 2, 1);
    i1(23, h, 1); box(h, 1, 0, 320, 300, 399);
    intin[0] = 0; vdi(129, 0, h, 0, 1);
    CHECK(count(bits, RB, 0, 320, 19, 399) == 0 && count(bits, RB, 20, 330, 120, 360) == 101 * 31, "clipping");
    /* v_get_pixel */
    ptsin[0] = 50; ptsin[1] = 340; vdi(105, 0, h, 1, 0);
    CHECK(intout[0] == 1, "v_get_pixel");
    /* a virtual workstation shares the bitmap */
    for (i = 0; i < 11; i++) intin[i] = 1;
    intin[10] = 2;
    { VDIPB pb = { contrl, intin, ptsin, intout, ptsout };
      memset(contrl, 0, sizeof contrl); contrl[0] = 100; contrl[3] = 11; contrl[6] = h;
      if (xg_vdi_need(&pb)) xg_vdi(&pb); }
    { int vh = contrl[6];
      CHECK(vh > 0 && vh != h, "virtual workstation on the memory device");
      i1(23, vh, 1); box(vh, 1, 200, 370, 260, 395);
      CHECK(count(bits, RB, 200, 370, 260, 395) == 61 * 26, "virtual workstation draws into the same bitmap");
      vdi(101, 0, vh, 0, 0); }
    save_pgm("out_memory.pgm", bits, W, H, RB);
    sp = count(bits, RB, 0, 0, W - 1, H - 1);
    printf("  %d pixels set\n", sp);
    vdi(2, 0, h, 0, 0);

    /* ---------------- metafile device ---------------- */
    for (i = 0; i < 11; i++) intin[i] = 1;
    intin[0] = 31; intin[10] = 2;
    vdi(1, 0, 0, 0, 11); h = contrl[6];
    printf("metafile device: handle %d, %dx%d, pixel %d um\n", h, intout[0], intout[1], intout[3]);
    CHECK(h > 0 && intout[0] == 32767 && intout[3] == 85, "metafile open");
    n = str("TEST.GEM", intin); vdi(5, 100, h, 0, n);                 /* vm_filename */
    intin[0] = 0; intin[1] = 2100; intin[2] = 2970; vdi(5, 99, h, 0, 3);  /* vm_pagesize */
    i1(17, h, 1);
    ptsin[0] = 1000; ptsin[1] = 2000; ptsin[2] = 30000; ptsin[3] = 2000; vdi(6, 0, h, 2, 0);
    ptsin[0] = 16000; ptsin[1] = 16000; ptsin[4] = 5000; vdi(11, 4, h, 3, 0);
    i1(107, h, 12); printf("  vst_point 12 -> cell %dx%d\n", ptsout[2], ptsout[3]);
    text(h, 2000, 25000, "Metafile text");
    ptsin[0] = 100; ptsin[1] = 0; vdi(116, 0, h, 0, str("Metafile text", intin));   /* query: not recorded */
    vdi(2, 0, h, 0, 0);
    {
        static u8 buf[8192];
        int len, p, items = 0, ok = 1, ops[16], nops = 0;
        f = fopen("hostfs/C/TEST.GEM", "rb");
        CHECK(f != NULL, "TEST.GEM written");
        if (f) {
            len = (int)fread(buf, 1, sizeof buf, f); fclose(f);
            CHECK(rd16(buf) == 0xffff && rd16(buf + 2) == 24, "metafile header");
            printf("  header: version %d, coords %d, extents %d,%d-%d,%d, page %dx%d\n",
                   rd16(buf + 4), rd16(buf + 6), (s16)rd16(buf + 8), (s16)rd16(buf + 10), (s16)rd16(buf + 12), (s16)rd16(buf + 14),
                   rd16(buf + 16), rd16(buf + 18));
            CHECK((s16)rd16(buf + 8) == 1000 && (s16)rd16(buf + 12) == 30000 && rd16(buf + 16) == 2100, "metafile extents and page size");
            p = 48;
            while (p + 2 <= len) {
                int op = (s16)rd16(buf + p), np, ni;
                if (op == -1) break;
                if (p + 8 > len) { ok = 0; break; }
                np = rd16(buf + p + 2); ni = rd16(buf + p + 4);
                if (nops < 16) ops[nops++] = op;
                p += 8 + 4 * np + 2 * ni;
                items++;
            }
            CHECK(ok && p + 2 == len, "metafile items end with -1 at the end of the file");
            printf("  %d items:", items);
            for (i = 0; i < nops; i++) printf(" %d", ops[i]);
            printf("\n");
            CHECK(items >= 6, "metafile items recorded");
            for (i = 0; i < nops; i++) CHECK(ops[i] != 116, "queries are not recorded");
        }
    }
    printf("heap free %d largest %d\n", heap_free_total(), heap_largest());
    printf(fails ? "%d FAILED\n" : "all device tests passed\n", fails);
    return fails != 0;
}
