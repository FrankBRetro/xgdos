/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* config.h - ASSIGN.SYS reader/writer (shared by XGDOS and XGDOSCFG)
 *
 * Classic GDOS lines are understood and preserved:
 *   ; comment
 *   PATH = C:\GEMSYS
 *   21 STYLUS.SYS            device id (+ optional letter) and driver
 *   SWISS10.FNT              bitmap font for the preceding device
 *
 * XGDOS additions (ignored/harmless for the rest of the file):
 *   FONTPATH = C:\GEMSYS     a folder with Speedo fonts (default: PATH);
 *                            repeat the line (or use ;) for up to 4 folders,
 *                            searched in order for fonts given by bare name
 *   PDFPATH  = C:\PRINTS     where printed PDF files go (default: boot root)
 *   PDFNAME  = PRN           file name prefix -> PRN0001.PDF, PRN0002.PDF ...
 *   PDFDPI   = 600           printer resolution reported to applications
 *   PAPER    = A4            A4 / A5 / LETTER / LEGAL
 *   CACHE    = 48            glyph cache in KB
 *   HEAP     = 192           XGDOS memory pool in KB (fonts load into it)
 *   STACKS   = 4             concurrent VDI calls (MiNT); 0 = automatic
 *   HINTING  = ON            grid-fit screen glyphs: baseline, x-height
 *                            and caps snap to pixels, and each glyph moves
 *                            by under half a pixel so its stems keep an
 *                            even weight; FULL also fits every vertical
 *                            stem (crisper large text, may bend small
 *                            serif text); OFF disables it
 *   PRINTER  = PDF           PDF: devices 21-30 print to PDF files
 *                            DRIVER: load the .SYS printer drivers instead
 *   FSMC     = OFF           ON: also install the SpeedoGDOS 'FSMC' cookie
 *                            (some programs want it; it makes the MultiTOS
 *                            AES lose its desktop text, so OFF by default)
 *   MODE     = FULL          diagnostics: NOGDOS (do not announce a GDOS),
 *                            NOFSMC (no FSMC cookie), BYPASS (pass all on)
 *   +BX000043.SPD            Speedo font, enabled
 *   -BX000044.SPD            Speedo font, disabled (kept for XGDOSCFG)
 *   +D:\FONTS\X.SPD 5050     full path and optional VDI font id
 */
#ifndef CONFIG_H
#define CONFIG_H
#include "xtypes.h"

#define XG_MAXDEV  40
#define XG_MAXFNT  64
#define XG_MAXSPD  64
#define XG_PATHLEN 128
#define XG_MAXFDIR 4             /* font folders */

enum { PAPER_A4, PAPER_A5, PAPER_LETTER, PAPER_LEGAL, PAPER_COUNT };
enum { PRN_PDF, PRN_DRIVER };
enum { MODE_FULL, MODE_NOGDOS, MODE_NOFSMC, MODE_BYPASS, MODE_COUNT };
extern const char *const xg_mode_names[MODE_COUNT];

typedef struct XgDev {
    s16  id;
    char letter;
    char drv[14];
    s16  fnt_first, fnt_count;   /* bitmap fonts in XgConfig.fnt[] */
} XgDev;

typedef struct XgSpd {
    char file[XG_PATHLEN];       /* as written: bare name or full path */
    s16  enabled;
    s16  vdi_id;                 /* 0 = automatic */
} XgSpd;

typedef struct XgConfig {
    char path[XG_PATHLEN];
    char fontdir[XG_MAXFDIR][XG_PATHLEN];   /* font folders, searched in order */
    s16  nfontdir;
    char pdfpath[XG_PATHLEN];
    char pdfname[10];
    s16  pdfdpi, paper, cache_kb, heap_kb, stacks, hint, printer;
    s16  mode;                   /* MODE_FULL, ... (diagnostics) */
    s16  fsmc;                   /* install the SpeedoGDOS FSMC cookie */
    s16  have_spd_lines;         /* any +/- lines present */
    s16  ndev;
    XgDev dev[XG_MAXDEV];
    s16  nfnt;
    char fnt[XG_MAXFNT][14];
    s16  nspd;
    XgSpd spd[XG_MAXSPD];
} XgConfig;

extern const char *const xg_paper_names[PAPER_COUNT];

void cfg_defaults(XgConfig *c, int bootdrv);
void cfg_parse(XgConfig *c, const char *text, s32 len);
/* Rewrite ASSIGN.SYS text: classic lines kept in place, the XGDOS block
 * regenerated at the end. Returns length written or -1 if out too small. */
s32  cfg_write(const XgConfig *c, const char *orig, s32 origlen, char *out, s32 outsize);
const XgDev *cfg_device(const XgConfig *c, int id);
/* full path of a font line: a bare name is looked for in each font folder
 * in turn (the first folder if it is in none of them) */
void cfg_spd_fullpath(const XgConfig *c, const XgSpd *s, char *out);
/* font folder list: add (0 if full or already there), find (-1), remove */
int  cfg_add_fontdir(XgConfig *c, const char *dir);
int  cfg_find_fontdir(const XgConfig *c, const char *dir);
void cfg_remove_fontdir(XgConfig *c, int i);
/* paper size in 1/10 mm */
void cfg_paper_size(int paper, s16 *w, s16 *h);

#endif
