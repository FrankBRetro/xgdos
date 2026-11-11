/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* test_vdi.c - drive XGDOS through xg_vdi() like a GEM application */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vdi.h"
#include "../src/heap.h"
#include "../src/xos.h"
#include "../src/xstd.h"

void fb_save(const char *fn);
extern int rom_text_calls;
static s16 contrl[16], intin[1024], ptsin[1024], intout[512], ptsout[64];

static void vdi(int op, int sub, int h, int np, int ni)
{
    VDIPB pb = { contrl, intin, ptsin, intout, ptsout };
    memset(contrl, 0, sizeof contrl);
    contrl[0] = (s16)op; contrl[1] = (s16)np; contrl[3] = (s16)ni; contrl[5] = (s16)sub; contrl[6] = (s16)h;
    if (xg_vdi_need(&pb)) xg_vdi(&pb); else rom_vdi(&pb);
}
static int str(const char *s, s16 *dst) { int n = 0; while (*s) dst[n++] = (u8)*s++; return n; }
static void gtext(int h, int x, int y, const char *s) { int n = str(s, intin); ptsin[0] = x; ptsin[1] = y; vdi(8, 0, h, 1, n); }
static void ftext(int h, int x, int y, const char *s) { int n = str(s, intin); ptsin[0] = x; ptsin[1] = y; vdi(241, 0, h, 1, n); }
static void point(int h, int pt) { intin[0] = pt; vdi(107, 0, h, 0, 1); }
static void font(int h, int id) { intin[0] = id; vdi(21, 0, h, 0, 1); }
static void effects(int h, int e) { intin[0] = e; vdi(106, 0, h, 0, 1); }
static void rotation(int h, int r) { intin[0] = r; vdi(13, 0, h, 0, 1); }
static void align(int h, int a, int b) { intin[0] = a; intin[1] = b; vdi(39, 0, h, 0, 2); }

int main(void)
{
    static char assign[4096];
    FILE *f;
    int n, h, i, nf, ids[16], p;
    cfg_defaults(&xg_cfg, 2);
    f = fopen("hostfs/C/ASSIGN.SYS", "rb"); n = (int)fread(assign, 1, sizeof assign, f); fclose(f);
    cfg_parse(&xg_cfg, assign, n);
    /* the same memory as on the Atari: HEAP from ASSIGN.SYS, 24 KB scratch */
    heap_init(malloc((s32)xg_cfg.heap_kb * 1024), (s32)xg_cfg.heap_kb * 1024);
    if (xg_cfg.cache_kb * 1024L > xg_cfg.heap_kb * 512L) xg_cfg.cache_kb = (s16)(xg_cfg.heap_kb / 2);
    xg_scratch_size = 24 * 1024; xg_scratch = malloc(xg_scratch_size);
    printf("heap %d KB, cache %d KB, scratch 24 KB\n", xg_cfg.heap_kb, xg_cfg.cache_kb);
    printf("fontpath=%s pdfpath=%s dpi=%d nspd=%d ndev=%d\n", xg_cfg.fontdir[0], xg_cfg.pdfpath, xg_cfg.pdfdpi, xg_cfg.nspd, xg_cfg.ndev);
    printf("fonts: %d\n", fm_init());

    /* ---------------- screen ---------------- */
    for (i = 0; i < 11; i++) intin[i] = 1;
    intin[0] = 4; intin[10] = 2;
    vdi(1, 0, 0, 0, 11); p = contrl[6];
    /* what an AES does on its own workstation: system font text */
    intin[0] = 13; vdi(12, 0, p, 0, 1);
    printf("AES vst_height -> cell %dx%d (from ROM)\n", ptsout[2], ptsout[3]);
    vdi(119, 0, p, 0, 0); printf("AES vst_load_fonts -> %d\n", intout[0]);
    intin[0] = 1; vdi(21, 0, p, 0, 1);
    intin[0] = 2; vdi(32, 0, p, 0, 1);
    gtext(p, 10, 10, "Desk  File  View  Options");
    printf("AES system font text reached the ROM: %d call(s)\n", rom_text_calls);
    vdi(100, 0, p, 0, 11); h = contrl[6];
    printf("phys %d virt %d\n", p, h);
    vdi(119, 0, h, 0, 0); nf = intout[0];
    printf("vst_load_fonts -> %d additional\n", nf);
    { char nm[33]; int k; intin[0] = 1; vdi(130, 0, h, 0, 1);
      for (k = 0; k < 32; k++) nm[k] = (char)intout[1 + k]; nm[32] = 0;
      printf("vqt_name(1) -> \"%s\"\n", nm); }
    for (i = 1; i <= nf + 1; i++) {
        char nm[40]; int k;
        intin[0] = i; vdi(130, 0, h, 0, 1);
        for (k = 0; k < 32; k++) nm[k] = (char)intout[1 + k]; nm[32] = 0;
        printf("  vqt_name %d: id %d '%s' outline=%d\n", i, intout[0], nm, intout[33]);
        ids[i] = intout[0];
    }
    font(h, ids[2]); printf("vst_font -> %d\n", intout[0]);
    { int y = 20, sizes[] = { 8, 10, 12, 14, 18, 24, 36 }, k;
      for (k = 0; k < 7; k++) {
          point(h, sizes[k]);
          printf("  vst_point %d -> %d cw %d ch %d cellw %d cellh %d\n", sizes[k], intout[0], ptsout[0], ptsout[1], ptsout[2], ptsout[3]);
          align(h, 0, 5);
          gtext(h, 4, y, "XGDOS Speedo: Hamburgefonstiv \x84\x94\x81\x9e \x82\x8a\x87");
          y += ptsout[3] + 2;
      }
      /* list the sizes the way GEM programs do: ask for one point less
       * until the answer stops going down (it must end, at 8 pt) */
      { int last = 999, n = 0, sz;
        for (;;) { point(h, last - 1); sz = intout[0]; if (sz >= last || n > 2000) break; last = sz; n++; }
        printf("  vst_point size scan: %d sizes, smallest %d\n", n, last);
        if (n != 10 || last != 8) { printf("FAIL: vst_point size scan\n"); return 1; }
        point(h, 13); k = intout[0]; point(h, 5);
        if (k != 12 || intout[0] != 8) { printf("FAIL: vst_point 13 -> %d, 5 -> %d\n", k, intout[0]); return 1; }
        intin[0] = 13; vdi(246, 0, h, 0, 1);
        if (intout[0] != 13) { printf("FAIL: vst_arbpt 13 -> %d\n", intout[0]); return 1; } }
      font(h, ids[3]); point(h, 24); align(h, 0, 0);
      effects(h, 0); gtext(h, 4, 300, "Italic");
      effects(h, 1); gtext(h, 100, 300, "Bold");
      effects(h, 8); gtext(h, 200, 300, "Under");
      effects(h, 16); gtext(h, 300, 300, "Outline");
      effects(h, 2); gtext(h, 450, 300, "Light");
      effects(h, 0); font(h, ids[2]);
      intin[0] = str("Extent test", intin); vdi(116, 0, h, 0, intin[0]);
      printf("  vqt_extent: %d,%d %d,%d %d,%d %d,%d\n", ptsout[0], ptsout[1], ptsout[2], ptsout[3], ptsout[4], ptsout[5], ptsout[6], ptsout[7]);
      point(h, 18); rotation(h, 900); gtext(h, 620, 390, "Rotated 90");
      rotation(h, 450); gtext(h, 450, 395, "Rotated 45");
      rotation(h, 0);
      /* writing mode replace */
      intin[0] = 1; vdi(32, 0, h, 0, 1);
      point(h, 14); gtext(h, 4, 340, "Replace mode background");
      /* clipping */
      intin[0] = 1; ptsin[0] = 0; ptsin[1] = 350; ptsin[2] = 200; ptsin[3] = 399; vdi(129, 0, h, 2, 1);
      point(h, 36); gtext(h, 4, 390, "Clipped at x=200");
      intin[0] = 0; vdi(129, 0, h, 0, 1);
      /* kerning on/off comparison */
      point(h, 18); intin[0] = 0; intin[1] = 0; vdi(237, 0, h, 0, 2); gtext(h, 330, 340, "AVAWAY To");
      intin[0] = 0; intin[1] = 1; vdi(237, 0, h, 0, 2); printf("  vst_kern pairs=%d\n", intout[1]); gtext(h, 330, 362, "AVAWAY To");
    }
    /* a glyph far too complex for the 24 KB scratch: drawn via a heap block */
    { extern int fb_count(int x0, int y0, int x1, int y1);
      font(h, ids[3]); intin[0] = 900; vdi(246, 0, h, 0, 1); intin[0] = 2; vdi(32, 0, h, 0, 1);
      ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = 639; ptsin[3] = 399; intin[0] = 1; vdi(129, 0, h, 2, 1);
      { int before = fb_count(0, 0, 640, 400); intin[0] = 0x40; ptsin[0] = 340; ptsin[1] = 390; vdi(8, 0, h, 1, 1);
        printf("huge '@' at 900pt: %d pixels drawn\n", fb_count(0, 0, 640, 400) - before); }
      intin[0] = 0; vdi(129, 0, h, 0, 1); }
    /* BICS charmap: a glyph outside the Atari set loads the whole font */
    { int k, fi = -1; s32 before = 0;
      for (k = 0; k < xg_nfont; k++) if (xg_font[k].vdi_id == ids[3]) fi = k;
      if (fi >= 0) before = xg_font[fi].spd.size;
      int want = 0;
      font(h, ids[3]); point(h, 12); intin[0] = 0; vdi(236, 0, h, 0, 1);
      if (fi >= 0 && xg_font[fi].loaded)          /* an id the compact copy dropped */
          for (k = 0; k < xg_font[fi].spd.nchars && !want; k++)
              if (!spd_has_glyph(&xg_font[fi].spd, xg_font[fi].idmap[2 * k + 1])) want = xg_font[fi].idmap[2 * k];
      intin[0] = (s16)want; ptsin[0] = 600; ptsin[1] = 20; vdi(8, 0, h, 1, 1);
      if (fi >= 0) printf("BICS glyph %d:", want); if (fi >= 0) printf(" font %d bytes -> %d bytes in memory (full=%d)\n", before, xg_font[fi].spd.size, xg_font[fi].full);
      intin[0] = 1; vdi(236, 0, h, 0, 1); }
    fb_save("out_screen.pgm");
    /* v_getoutline as gemlib / EdWord call it: intin[0] char, [1] max,
     * [2-3] xyarray, [4-5] bezarray (chars); guard bytes catch overruns */
    { static s16 xy[2 * 400 + 16]; static u8 bz[400 + 16]; int k, bad = 0, mn = 9999, mx = -9999;
      for (k = 0; k < 16; k++) { xy[800 + k] = 0x5a5a; bz[400 + k] = 0xa5; }
      font(h, ids[2]); intin[0] = 144; vdi(246, 0, h, 0, 1);
      intin[0] = 'H'; intin[1] = 400; SETPTR(intin, 2, xy); SETPTR(intin, 4, bz);
      vdi(243, 0, h, 0, 6);
      for (k = 0; k < 16; k++) if (xy[800 + k] != 0x5a5a || bz[400 + k] != 0xa5) bad = 1;
      for (k = 0; k < intout[0]; k++) { if (xy[2 * k + 1] < mn) mn = xy[2 * k + 1]; if (xy[2 * k + 1] > mx) mx = xy[2 * k + 1]; }
      printf("v_getoutline 'H' 144pt: %d points, y %d..%d, flags[0]=%d, overrun %s\n", intout[0], mn, mx, bz[0], bad ? "YES" : "no");
      intin[0] = 'H'; intin[1] = 10; vdi(243, 0, h, 0, 6);
      for (k = 0; k < 16; k++) if (xy[800 + k] != 0x5a5a || bz[400 + k] != 0xa5) bad = 1;
      printf("v_getoutline max 10: %d points, overrun %s\n", intout[0], bad ? "YES" : "no");
    }
    vdi(101, 0, h, 0, 0);

    /* ---------------- printer (PDF) ---------------- */
    for (int job = 0; job < 2; job++) {
        for (i = 0; i < 11; i++) intin[i] = 1;
        intin[0] = 21; intin[10] = 2;
        vdi(1, 0, 0, 0, 11); h = contrl[6];
        printf("printer handle %d: %dx%d px, pixel %dx%d um, colours %d\n", h, intout[0] + 1, intout[1] + 1, intout[3], intout[4], intout[13]);
        vdi(119, 0, h, 0, 0); nf = intout[0];
        printf("  printer fonts +%d\n", nf);
        intin[0] = 2; vdi(130, 0, h, 0, 1); ids[2] = intout[0];
        intin[0] = 3; vdi(130, 0, h, 0, 1); ids[3] = intout[0];
        font(h, ids[2]); point(h, 36); gtext(h, 300, 400, job ? "Second print job" : "XGDOS prints to PDF");
        point(h, 12);
        for (i = 0; i < 6; i++) { char b[80]; sprintf(b, "Line %d: The quick brown fox jumps over the lazy dog. \x84\x94\x81\x9e", i + 1); gtext(h, 300, 600 + i * 120, b); }
        font(h, ids[3]); point(h, 24); effects(h, 8); gtext(h, 300, 1500, "Italic underlined, 24 pt"); effects(h, 1); gtext(h, 300, 1700, "Italic bold"); effects(h, 0);
        font(h, 1); point(h, 12); gtext(h, 300, 1900, "System font -> Courier (\x84\x94\x81)");
        /* graphics */
        intin[0] = 2; vdi(25, 0, h, 0, 1); intin[0] = 1; vdi(23, 0, h, 0, 1);
        ptsin[0] = 300; ptsin[1] = 2100; ptsin[2] = 1300; ptsin[3] = 2500; vdi(11, 1, h, 2, 0);
        intin[0] = 4; vdi(25, 0, h, 0, 1); ptsin[0] = 2000; ptsin[1] = 2300; ptsin[4] = 300; vdi(11, 4, h, 3, 0);
        ptsin[0] = 3200; ptsin[1] = 2300; ptsin[2] = 500; ptsin[3] = 250; intin[0] = 2; vdi(23, 0, h, 0, 1); intin[0] = 4; vdi(24, 0, h, 0, 1); vdi(11, 5, h, 2, 0);
        ptsin[0] = 300; ptsin[1] = 2700; ptsin[2] = 4600; ptsin[3] = 2700; ptsin[4] = 4600; ptsin[5] = 3200; vdi(6, 0, h, 3, 0);
        intin[0] = 3; vdi(15, 0, h, 0, 1); ptsin[0] = 9; vdi(16, 0, h, 1, 0);
        ptsin[0] = 300; ptsin[1] = 3400; ptsin[2] = 4600; ptsin[3] = 3400; vdi(6, 0, h, 2, 0);
        ptsin[0] = 300; ptsin[1] = 3600; ptsin[2] = 1500; ptsin[3] = 4200; vdi(11, 8, h, 2, 0);
        intin[0] = 0; intin[1] = 2700; ptsin[0] = 2500; ptsin[1] = 3900; ptsin[6] = 300; vdi(11, 3, h, 4, 2);
        ptsin[0] = 3600; ptsin[1] = 3900; ptsin[2] = 4000; ptsin[3] = 3700; ptsin[4] = 4400; ptsin[5] = 4100; vdi(7, 0, h, 3, 0);
        /* rotated outline text */
        font(h, ids[2]); point(h, 20); rotation(h, 900); gtext(h, 200, 6800, "Rotated 90 degrees");
        rotation(h, 300); gtext(h, 1500, 6800, "Rotated 30 degrees"); rotation(h, 0);
        /* a mono image via vro_cpyfm */
        { static u16 img[32 * 2]; MFDB src, dst; int y;
          for (y = 0; y < 32; y++) { img[y * 2] = (u16)(y & 1 ? 0xAAAA : 0x5555); img[y * 2 + 1] = (u16)(0xffff >> (y / 2)); }
          for (y = 0; y < 64; y++) img[y] = (u16)((img[y] >> 8) | (img[y] << 8));  /* to big-endian bytes */
          src.fd_addr = img; src.fd_w = 32; src.fd_h = 32; src.fd_wdwidth = 2; src.fd_stand = 0; src.fd_nplanes = 1;
          memset(&dst, 0, sizeof dst);
          ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = 31; ptsin[3] = 31; ptsin[4] = 4200; ptsin[5] = 4400; ptsin[6] = 4231; ptsin[7] = 4431;
          intin[0] = 3;
          memset(contrl, 0, sizeof contrl);
          { VDIPB pb = { contrl, intin, ptsin, intout, ptsout }; contrl[0] = 109; contrl[1] = 4; contrl[3] = 1; contrl[6] = h;
            SETPTR(contrl, 7, &src); SETPTR(contrl, 9, &dst); xg_vdi(&pb); }
        }
        vdi(4, 0, h, 0, 0); vdi(3, 0, h, 0, 0);            /* updwk + clrwk = page break */
        font(h, ids[2]); point(h, 48); align(h, 1, 5); gtext(h, 2480, 300, "Page two, centred");
        vdi(2, 0, h, 0, 0);
    }
    /* a program that opens the printer only to ask about it: no PDF */
    {
        int before = 0, after = 0;
        XosFind fd;
        if (xos_findfirst(&fd, "C:\\PRINTS\\*.PDF")) do before++; while (xos_findnext(&fd));
        xos_findend(&fd);
        for (i = 0; i < 11; i++) intin[i] = 1;
        intin[0] = 21; intin[10] = 2;
        vdi(1, 0, 0, 0, 11); h = contrl[6];
        intin[0] = 0; vdi(102, 0, h, 0, 1);
        vdi(2, 0, h, 0, 0);
        if (xos_findfirst(&fd, "C:\\PRINTS\\*.PDF")) do after++; while (xos_findnext(&fd));
        xos_findend(&fd);
        printf("printer opened and closed without output: %d new PDF files (expect 0)\n", after - before);
    }
    /* EdWord-style print: a whole A4 page sent as 128-row vrt_cpyfm bands */
    {
        int y0, W, H, rb, r, x;
        static u8 band[620 * 128];
        MFDB src, dst;
        for (i = 0; i < 11; i++) intin[i] = 1;
        intin[0] = 21; intin[10] = 2;
        vdi(1, 0, 0, 0, 11); h = contrl[6];
        W = intout[0] + 1; H = intout[1] + 1; rb = ((W + 15) / 16) * 2;
        intin[0] = 0; vdi(102, 0, h, 0, 1);
        printf("printer vq_extnd(0): %dx%d px, pixel %dx%d um\n", intout[0] + 1, intout[1] + 1, intout[3], intout[4]);
        for (y0 = 0; y0 < H; y0 += 128) {
            int rows = H - y0 < 128 ? H - y0 : 128;
            memset(band, 0, sizeof band);
            if (y0 >= 512 && y0 < 2048)       /* "text": blocks and stripes */
                for (r = 0; r < rows; r++) for (x = 60; x < 560; x++)
                    if (((x / 6) & 1) && ((y0 + r) / 32) % 3 != 2) band[r * rb + x] = (u8)(0xF0 >> (r & 3));
            src.fd_addr = band; src.fd_w = (s16)(rb * 8); src.fd_h = (s16)rows; src.fd_wdwidth = (s16)(rb / 2);
            src.fd_stand = 0; src.fd_nplanes = 1; memset(&dst, 0, sizeof dst);
            ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = (s16)(W - 1); ptsin[3] = (s16)(rows - 1);
            ptsin[4] = 0; ptsin[5] = (s16)y0; ptsin[6] = (s16)(W - 1); ptsin[7] = (s16)(y0 + rows - 1);
            intin[0] = 2; intin[1] = 1; intin[2] = 0;
            memset(contrl, 0, sizeof contrl);
            { VDIPB pb = { contrl, intin, ptsin, intout, ptsout }; contrl[0] = 121; contrl[1] = 4; contrl[3] = 3; contrl[6] = (s16)h;
              SETPTR(contrl, 7, &src); SETPTR(contrl, 9, &dst); xg_vdi(&pb); }
        }
        vdi(4, 0, h, 0, 0); vdi(3, 0, h, 0, 0);
        vdi(4, 0, h, 0, 0); vdi(3, 0, h, 0, 0);       /* a blank page, printed on purpose */
        vdi(2, 0, h, 0, 0);
        printf("banded print job done (%dx%d), expect 2 pages\n", W, H);
    }
    printf("heap free %d largest %d\n", heap_free_total(), heap_largest());
    return 0;
}
