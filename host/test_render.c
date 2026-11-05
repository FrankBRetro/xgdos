/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* host test: render strings with the Speedo decoder + rasteriser to PGM */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/spd.h"
#include "../src/raster.h"
#include "../src/charmap.h"

static u8 *load(const char *fn, s32 *len)
{
    FILE *f = fopen(fn, "rb"); u8 *b; long n;
    if (!f) { perror(fn); exit(1); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(n); fread(b, 1, n, f); fclose(f); *len = (s32)n; return b;
}

int main(int argc, char **argv)
{
    s32 len; u8 *d = load(argv[1], &len);
    SpdFont f; u16 *idm;
    static s32 segs[4 * 20000];
    static u8 tmp[400000];
    int W = 900, H = 0, y = 0, i;
    int sizes[10] = { 8, 10, 12, 14, 16, 20, 24, 32, 48, 72 }, ns = 10;
    const char *txt = argc > 3 ? argv[3] : "The quick brown fox jumps over Lazy Dogs! \x84\x94\x81\x9e \x82\x8a 1/2 \x80";
    u8 *img; FILE *o;
    int hint = argc > 4 ? atoi(argv[4]) : 1;
    if (argc > 5) { char *q = argv[5]; ns = 0; while (*q && ns < 10) { sizes[ns++] = (int)strtol(q, &q, 10); if (*q == ',') q++; } }
    if (!spd_open(&f, d, len)) { printf("open failed\n"); return 1; }
    printf("k32=%04x k7=%02x km=%02x nchars=%d orupm=%d dir=%d chars=%d\n", f.k32, f.k7, f.km, f.nchars, f.orupm, f.dir, f.chars);
    idm = malloc(4 * f.nchars); spd_build_idmap(&f, idm);
    for (i = 0; i < ns; i++) H += sizes[i] * 3 / 2 + 4;
    img = calloc(W, H);
    for (i = 0; i < ns; i++) {
        int px = sizes[i]; fix pen = INT2FIX(4); const char *s;
        int base = y + px;
        for (s = txt; *s; s++) {
            int id = xg_atari_to_bcid[(u8)*s], idx = spd_index_of_id(&f, id);
            Raster r; SpdXform xf; int x0, y0, x1, y1;
            if (idx < 0) continue;
            memset(&xf, 0, sizeof xf);
            xf.sx = xf.sy = px * FIX_ONE / f.orupm; xf.hint = hint;
            if (hint) {     /* as text.c: whole pixel + quarter-pixel phase */
                fix p = pen + 0x2000;
                xf.phx = (p >> 14 & 3) << 14;
                raster_init(&r, segs, 20000, p & ~0xffff, INT2FIX(base));
            } else
                raster_init(&r, segs, 20000, (pen + FIX_HALF) & ~0xffff, INT2FIX(base));
            r.dropout = px <= 40;
            spd_char_outline(&f, idx, &xf, &r.sink);
            if (raster_bounds(&r, &x0, &y0, &x1, &y1)) {
                int w = x1 - x0, h = y1 - y0, rb = (w + 15) / 16 * 2, yy, xx;
                u8 *bits = calloc(rb, h);
                raster_fill(&r, bits, rb, x0, y0, w, h, tmp, sizeof tmp);
                for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++)
                    if (bits[yy * rb + xx / 8] & (0x80 >> (xx & 7))) {
                        int X = x0 + xx, Y = y0 + yy;
                        if (X >= 0 && X < W && Y >= 0 && Y < H) img[Y * W + X] = 1;
                    }
                free(bits);
            }
            pen += spd_char_width(&f, idx) * (px * FIX_ONE / f.orupm);
            if (pen > INT2FIX(W)) break;
        }
        y += px * 3 / 2 + 4;
    }
    o = fopen(argv[2], "wb"); fprintf(o, "P5 %d %d 255\n", W, H);
    for (i = 0; i < W * H; i++) fputc(img[i] ? 0 : 255, o);
    fclose(o);
    return 0;
}
