/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* spd.h - Bitstream Speedo (.SPD) outline font decoder
 *
 * Supports the keyed ("encrypted") Speedo fonts shipped with Atari
 * SpeedoGDOS as well as unkeyed Speedo fonts. Keys are derived from the
 * font itself, so no customer key table is needed.
 */
#ifndef SPD_H
#define SPD_H
#include "xtypes.h"
#include "xfix.h"

#define SPD_HDR_MIN  448   /* bytes needed by spd_probe() */

typedef struct SpdFont {
    const u8 *d;         /* whole file in memory */
    s32  size;
    s32  dir, cons, trk, pairk, chars;   /* section offsets */
    u16  nchars, first;
    u8   dirfmt, km, kh, kl, k7, ok_keys, err;
    u16  k32;
    u16  orupm;          /* outline resolution units per em */
    s16  xmin, ymin, xmax, ymax;
    s16  ul_pos, ul_thk;
    s16  wordspace, emspace;
    s16  bsid;           /* Bitstream source font id */
    u16  ntracks, npairs;
    u16 *idmap;          /* optional sorted (id,index) pairs, 2*nchars words */
} SpdFont;

typedef struct SpdInfo {
    char name[72];       /* full font name */
    char sname[34];      /* short font name */
    char face[18];       /* short face name */
    s16  bsid;
    u16  orupm;
    s16  ymin, ymax;
    s32  fsize;          /* total file size */
} SpdInfo;

/* Outline consumer. Coordinates are 16.16 device pixels, y up. */
typedef struct SpdSink {
    void (*move)(struct SpdSink *, fix x, fix y);
    void (*line)(struct SpdSink *, fix x, fix y);
    void (*curve)(struct SpdSink *, fix x1, fix y1, fix x2, fix y2, fix x3, fix y3);
    void (*close)(struct SpdSink *);
} SpdSink;

/* Outline transform: oru -> px (y up). */
typedef struct SpdXform {
    fix sx, sy;           /* pixels per oru (16.16) */
    fix skew;             /* x += y * skew (tangent, 16.16) */
    fix m00, m01, m10, m11; /* rotation applied after skew (16.16) */
    fix ox, oy;           /* translation added at the end (16.16 px) */
    fix phx;              /* sub-pixel x position, applied before hinting */
    s16 rotated;          /* non-zero: apply m?? matrix */
    s16 hint;             /* grid-fit controlled edges (screen) */
    s16 raw;              /* output orus untransformed (PDF glyph forms) */
} SpdXform;

int  spd_probe(const u8 *d, s32 len, SpdInfo *info);   /* header only */
int  spd_open(SpdFont *f, const u8 *d, s32 len);       /* whole file */
/* on failure f->err: 1 too short, 2 not Speedo, 4 bad header,
 * 5 bad directory, 6 directory too large, 7 no working key */
int  spd_index_of_id(const SpdFont *f, u16 id);       /* -1 if missing; needs idmap */
int  spd_build_idmap(SpdFont *f, u16 *buf);           /* buf: 2*nchars words */
u16  spd_char_id(const SpdFont *f, int idx);
s32  spd_char_width(const SpdFont *f, int idx);       /* orus, <0 on error */
int  spd_char_outline(const SpdFont *f, int idx, const SpdXform *xf, SpdSink *s);
int  spd_char_bbox(const SpdFont *f, int idx, s16 *bb);  /* xmin,ymin,xmax,ymax orus */
s32  spd_pair_kern(const SpdFont *f, int left, int right);  /* orus */
s32  spd_track_kern(const SpdFont *f, int track, s32 ptsize_fix); /* 1/65536 pt */
/* memory saving: keep only the glyphs an application can reach */
void spd_mark_glyph(const SpdFont *f, int idx, u8 *bits);   /* + composite parts */
int  spd_has_glyph(const SpdFont *f, int idx);              /* data present */
s32  spd_compact(SpdFont *f, u8 *data, const u8 *keep);    /* new size, 0 = not done */

#endif
