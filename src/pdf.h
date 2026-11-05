/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pdf.h - PDF printer device */
#ifndef PDF_H
#define PDF_H
#include "vdi.h"

typedef struct PdfXo { s16 font, idx; s32 obj; } PdfXo;

typedef struct PdfDoc {
    char path[XG_PATHLEN];
    s32 *xref; s32 nobj, maxobj;
    s32 pos;
    u8  *wbuf; s32 wn;
    char *page; s32 plen, pcap, pstart;
    s32 *cont; s32 ncont, maxcont;   /* content streams already written */
    s16 pcont;             /* a written content stream had drawing in it */
    s16 nosplit;           /* inside a token built from several pieces */
    s32 *kids; s32 nkids, maxkids;
    PdfXo *xo; s32 nxo, maxxo;
    s32 *img; s32 nimg, maximg;
    s32 ilen, istart;      /* open image: length object, stream start */
    s16 created;           /* the PRNnnnn.PDF file exists (made on first output) */
    s16 updated;           /* v_updwk since the page began: keep it, even blank */
    s32 dpi;
    s16 wpx, hpx;          /* page size in device pixels */
    fix wpt, hpt;          /* page size in points */
    s16 l_type, l_width, l_color, l_beg, l_end;
    u16 l_udsty;
    s16 m_type, m_height, m_color;
    s16 f_interior, f_style, f_color, f_perim;
    s16 err;
    s16 pal[256][3];
} PdfDoc;

#endif
