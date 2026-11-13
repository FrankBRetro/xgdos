/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xgdos.h - XGDOS shared state */
#ifndef XGDOS_H
#define XGDOS_H
#include "xtypes.h"
#include "xfix.h"
#include "spd.h"
#include "config.h"

#define XGDOS_VERSION   0x0102
#define XGDOS_VSTRING   "1.02"

#ifndef XG_MAXFONTS
#define XG_MAXFONTS 96
#endif

/* ---- outline fonts ---- */
typedef struct XFont {
    char  path[XG_PATHLEN];
    char  name[72];         /* "American Garamond Italic" */
    char  sname[34];        /* "AmericanGaramondBT-Italic" */
    char  face[18];
    s16   bsid, vdi_id;
    u16   orupm;
    s16   ymin, ymax;       /* font bbox (orus) */
    s32   fsize;
    u8    enabled, present, loaded, cfgslot;
    u8    compact;          /* only the Atari character set is in memory */
    u8    full;             /* a program used other glyphs: keep them all */
    u8   *data;
    u16  *idmap;
    SpdFont spd;
    u32   lru;
    /* metrics in orus, valid once loaded (or estimated from header) */
    s16   m_top, m_asc, m_half, m_desc, m_bot, m_maxw, m_qidx;
} XFont;

extern XgConfig  xg_cfg;
extern XFont    *xg_font;
extern int       xg_nfont;
extern u32       xg_tick;

/* fontmgr.c */
int    fm_init(void);                 /* build font table from xg_cfg */
int    fm_reload(void);               /* re-read config, keep indices stable */
XFont *fm_get(int fi);                /* load on demand, NULL on failure */
int    fm_find_id(int vdi_id);        /* -1 if none */
int    fm_glyph(XFont *f, int code, int charmap);  /* char -> glyph index */
void   fm_flush_unused(void);

/* lock.c - one lock for all shared XGDOS state */
void   xg_lock(void);
void   xg_unlock(void);

/* scratch memory for the current (locked) call */
extern u8  *xg_scratch;
extern s32  xg_scratch_size;

#endif
