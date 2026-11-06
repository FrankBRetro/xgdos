/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pdf.c - VDI printer device writing numbered PDF files
 *
 * Every v_opnwk on a printer device (21-30) creates PDFPATH\PRNnnnn.PDF
 * with the next free number. The device reports PDFDPI (default 600) so
 * applications lay out at high resolution; drawing is emitted as PDF
 * vectors in device pixel units (one "cm" scales to points), Speedo text
 * as vector glyph forms, the system font as the PDF base font Courier.
 */
#include "pdf.h"
#include "text.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"
#include "charmap.h"
#include "gcache.h"
#include "soft.h"

#define CONTRL pb->contrl
#define INTIN  pb->intin
#define PTSIN  pb->ptsin
#define INTOUT pb->intout
#define PTSOUT pb->ptsout

#define OBJ_CATALOG 1
#define OBJ_PAGES   2
#define OBJ_RES     3
#define OBJ_FONT0   4      /* 4..7: Courier, -Bold, -Oblique, -BoldOblique */
#define WBUF        8192

/* A print job comes first: when the heap is short, the screen glyph cache
 * gives its memory back before an allocation is allowed to fail. */
static void *p_alloc(s32 n)
{
    void *p = heap_alloc(n);
    while (!p && gc_shrink(n)) p = heap_alloc(n);
    return p;
}
static void *p_calloc(s32 n)
{
    void *p = heap_calloc(n);
    while (!p && gc_shrink(n)) p = heap_calloc(n);
    return p;
}
static void *p_realloc(void *o, s32 n)
{
    void *p = heap_realloc(o, n);
    while (!p && gc_shrink(n)) p = heap_realloc(o, n);
    return p;
}

static const s16 defpal[16][3] = {
    {1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
    {1000,1000,0},{1000,0,1000},{752,752,752},{501,501,501},{501,0,0},{0,501,0},
    {0,0,501},{0,501,501},{501,501,0},{501,0,501}
};

/* ---------------- low level output ---------------- */

static void make_path(PdfDoc *d);

static void wflush(PdfDoc *d)
{
    s32 h;
    if (!d->wn || d->err) { d->wn = 0; return; }
    if (!d->created) {
        /* first output: take the number that is free now */
        make_path(d);
        h = xos_create(d->path);
        if (h >= 0) { d->created = 1; XLOG("printing to "); XLOG(d->path); XLOG("\r\n"); }
    } else {
        h = xos_open(d->path, 1);
        if (h >= 0) xos_seek(h, 0, 2);
    }
    if (h < 0) { d->err = 1; d->wn = 0; return; }
    if (xos_write(h, d->wn, d->wbuf) != d->wn) d->err = 1;
    xos_close(h);
    d->wn = 0;
}

static void wbytes(PdfDoc *d, const void *p, s32 n)
{
    const u8 *s = (const u8 *)p;
    d->pos += n;
    while (n > 0) {
        s32 k = WBUF - d->wn;
        if (k > n) k = n;
        xmemcpy(d->wbuf + d->wn, s, k);
        d->wn += k; s += k; n -= k;
        if (d->wn == WBUF) wflush(d);
    }
}

static void ws(PdfDoc *d, const char *s) { wbytes(d, s, xstrlen(s)); }
static void wnum(PdfDoc *d, s32 v) { char b[12]; wbytes(d, b, xitoa(b, v)); }

static int grow(void **p, s32 *max, s32 need, s32 esz)
{
    void *q;
    s32 nm;
    if (need <= *max) return 1;
    nm = *max ? *max * 2 : 16;
    while (nm < need) nm *= 2;
    q = p_realloc(*p, nm * esz);
    if (!q) return 0;
    *p = q;
    *max = nm;
    return 1;
}

static s32 obj_new(PdfDoc *d)
{
    if (!grow((void **)&d->xref, &d->maxobj, d->nobj + 1, 4)) { d->err = 1; return 0; }
    d->xref[d->nobj] = 0;
    return d->nobj++;
}

static void obj_begin(PdfDoc *d, s32 n)
{
    if (n < d->maxobj) d->xref[n] = d->pos;
    wnum(d, n);
    ws(d, " 0 obj\n");
}

/* Page content: buffered in pieces of at most PCHUNK bytes; each full
 * piece goes to the file as its own content stream (a page may list
 * several), so a dense page never needs a large block of memory. */
#define PCHUNK 8192        /* page buffer, allocated once per job */

static void chunk_flush(PdfDoc *d)
{
    s32 obj;
    if (d->plen <= 0 || d->err) return;
    if (!grow((void **)&d->cont, &d->maxcont, d->ncont + 1, 4)) { d->err = 1; return; }
    obj = obj_new(d);
    if (d->err) return;
    obj_begin(d, obj);
    ws(d, "<< /Length "); wnum(d, d->plen); ws(d, " >>\nstream\n");
    wbytes(d, d->page, d->plen);
    ws(d, "\nendstream\nendobj\n");
    d->cont[d->ncont++] = obj;
    if (d->plen > d->pstart) d->pcont = 1;
    d->plen = 0;
    d->pstart = 0;
}

static void pcn(PdfDoc *d, const char *s, s32 n)
{
    if (d->plen > 0 && d->plen + n + 1 > XG_MAX(d->pcap, PCHUNK) && !d->nosplit) chunk_flush(d);
    if (d->plen + n + 1 > d->pcap) {
        s32 nc = d->pcap ? d->pcap * 2 : 4096;
        char *q;
        while (nc < d->plen + n + 1) nc *= 2;
        q = (char *)p_realloc(d->page, nc);
        if (!q) { d->err = 1; return; }
        d->page = q; d->pcap = nc;
    }
    xmemcpy(d->page + d->plen, s, n);
    d->plen += n;
}
static void pc(PdfDoc *d, const char *s) { pcn(d, s, xstrlen(s)); }
static void pn(PdfDoc *d, s32 v) { char b[14]; int n = xitoa(b, v); b[n++] = ' '; b[n] = 0; pc(d, b); }
static void pf(PdfDoc *d, fix v) { char b[20]; int n = xftoa(b, v); b[n++] = ' '; b[n] = 0; pc(d, b); }

/* ---------------- document structure ---------------- */

static void doc_free(PdfDoc *d)
{
    heap_free(d->xref); heap_free(d->wbuf); heap_free(d->page);
    heap_free(d->kids); heap_free(d->xo); heap_free(d->img); heap_free(d->cont);
    heap_free(d);
}

static int next_number(void)
{
    XosFind f;
    char pat[XG_PATHLEN];
    int best = 0, plen = (int)xstrlen(xg_cfg.pdfname);
    char name[16];
    xstrcpy(name, xg_cfg.pdfname);
    xstrcat(name, "*.PDF");
    xpath_join(pat, xg_cfg.pdfpath, name);
    if (xos_findfirst(&f, pat)) {
        do {
            int v = (int)xatoi(f.name + plen);
            if (v > best) best = v;
        } while (xos_findnext(&f));
    }
    xos_findend(&f);
    return best + 1;
}

/* PRNnnnn.PDF with the next free number */
static void make_path(PdfDoc *d)
{
    char name[16], b[8];
    int k, n = next_number() % 10000;
    xstrcpy(name, xg_cfg.pdfname);
    for (k = 3; k >= 0; k--) { b[k] = (char)('0' + n % 10); n /= 10; }
    b[4] = 0;
    xstrcat(name, b);
    xstrcat(name, ".PDF");
    xpath_join(d->path, xg_cfg.pdfpath, name);
}

static void page_begin(PdfDoc *d)
{
    fix a = fixdiv(INT2FIX(72), INT2FIX(d->dpi));
    d->plen = 0;
    d->ncont = 0;
    d->pcont = 0;
    d->updated = 0;
    pc(d, "q ");
    pf(d, a); pc(d, "0 0 "); pf(d, -a); pc(d, "0 "); pf(d, d->hpt); pc(d, "cm\n");
    d->pstart = d->plen;
}

static void page_end(PdfDoc *d)
{
    s32 pobj, i;
    if (d->plen <= 0 && !d->ncont) return;
    pc(d, "Q\n");
    chunk_flush(d);
    pobj = obj_new(d);
    if (d->err) return;
    obj_begin(d, pobj);
    ws(d, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    { char b[20]; wbytes(d, b, xftoa(b, d->wpt)); ws(d, " "); wbytes(d, b, xftoa(b, d->hpt)); }
    ws(d, "] /Resources 3 0 R /Contents [");
    for (i = 0; i < d->ncont; i++) { wnum(d, d->cont[i]); ws(d, " 0 R "); }
    ws(d, "] >>\nendobj\n");
    if (grow((void **)&d->kids, &d->maxkids, d->nkids + 1, 4)) d->kids[d->nkids++] = pobj;
    else d->err = 1;
    d->plen = 0;
    d->ncont = 0;
    d->pcont = 0;
}

static int page_has_content(PdfDoc *d)
{
    /* the header written by page_begin is not content */
    return d->pcont || d->plen > d->pstart;
}

static void doc_finish(PdfDoc *d)
{
    s32 i, xpos;
    static const char *const fn[4] = { "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique" };
    if (page_has_content(d) || d->updated) page_end(d);
    if (!d->nkids) {             /* never write a file without pages */
        if (d->plen <= 0 && !d->ncont) page_begin(d);
        page_end(d);
    }
    obj_begin(d, OBJ_CATALOG);
    ws(d, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj_begin(d, OBJ_PAGES);
    ws(d, "<< /Type /Pages /Count "); wnum(d, d->nkids); ws(d, " /Kids [");
    for (i = 0; i < d->nkids; i++) { wnum(d, d->kids[i]); ws(d, " 0 R "); }
    ws(d, "] >>\nendobj\n");
    for (i = 0; i < 4; i++) {
        obj_begin(d, OBJ_FONT0 + i);
        ws(d, "<< /Type /Font /Subtype /Type1 /BaseFont /"); ws(d, fn[i]);
        ws(d, " /Encoding /WinAnsiEncoding >>\nendobj\n");
    }
    obj_begin(d, OBJ_RES);
    ws(d, "<< /ProcSet [/PDF /Text /ImageB /ImageI] /Font << /F0 4 0 R /F1 5 0 R /F2 6 0 R /F3 7 0 R >>");
    if (d->nxo || d->nimg) {
        ws(d, " /XObject <<");
        for (i = 0; i < d->nxo; i++) { ws(d, " /G"); wnum(d, i); ws(d, " "); wnum(d, d->xo[i].obj); ws(d, " 0 R"); }
        for (i = 0; i < d->nimg; i++) { ws(d, " /I"); wnum(d, i); ws(d, " "); wnum(d, d->img[i]); ws(d, " 0 R"); }
        ws(d, " >>");
    }
    ws(d, " >>\nendobj\n");
    xpos = d->pos;
    ws(d, "xref\n0 "); wnum(d, d->nobj); ws(d, "\n0000000000 65535 f \n");
    for (i = 1; i < d->nobj; i++) {
        char b[24];
        s32 v = d->xref[i];
        int k;
        for (k = 9; k >= 0; k--) { b[k] = (char)('0' + v % 10); v /= 10; }
        xstrcpy(b + 10, " 00000 n \n");
        wbytes(d, b, 20);
    }
    ws(d, "trailer\n<< /Size "); wnum(d, d->nobj); ws(d, " /Root 1 0 R >>\nstartxref\n");
    wnum(d, xpos); ws(d, "\n%%EOF\n");
    wflush(d);
}

/* ---------------- open ---------------- */

static void work_out(PdfDoc *d, VDIPB *pb, int um)
{
    int i;
    static const s16 gdp[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    for (i = 0; i < 45; i++) INTOUT[i] = 0;
    INTOUT[0] = (s16)(d->wpx - 1);
    INTOUT[1] = (s16)(d->hpx - 1);
    INTOUT[2] = 0;
    INTOUT[3] = (s16)um; INTOUT[4] = (s16)um;
    INTOUT[5] = 0;      /* continuous text sizes */
    INTOUT[6] = 7;      /* line types */
    INTOUT[7] = 0;      /* continuous line widths */
    INTOUT[8] = 6;      /* marker types */
    INTOUT[9] = 0;
    INTOUT[10] = 1;     /* faces before vst_load_fonts */
    INTOUT[11] = 24; INTOUT[12] = 12;
    INTOUT[13] = 256;   /* predefined colours */
    INTOUT[14] = 10;
    for (i = 0; i < 10; i++) { INTOUT[15 + i] = gdp[i]; INTOUT[25 + i] = (i < 2) ? 0 : 3; }
    INTOUT[25] = 3; INTOUT[26] = 0; INTOUT[27] = 3; INTOUT[28] = 3; INTOUT[29] = 3;
    INTOUT[30] = 0; INTOUT[31] = 3; INTOUT[32] = 0; INTOUT[33] = 3; INTOUT[34] = 2;
    INTOUT[35] = 1;     /* colour */
    INTOUT[36] = 2;     /* arbitrary text rotation */
    INTOUT[37] = 1;     /* fill area */
    INTOUT[38] = 0;
    INTOUT[39] = 0;     /* > 32767 palette entries (RGB) */
    INTOUT[40] = 0; INTOUT[41] = 0; INTOUT[42] = 0; INTOUT[43] = 0;
    INTOUT[44] = 0;     /* output only */
    PTSOUT[0] = 1; PTSOUT[1] = 1;
    PTSOUT[2] = (s16)d->wpx; PTSOUT[3] = (s16)d->hpx;
    PTSOUT[4] = 1; PTSOUT[5] = 0; PTSOUT[6] = (s16)d->dpi; PTSOUT[7] = 0;
    PTSOUT[8] = 1; PTSOUT[9] = 1; PTSOUT[10] = (s16)d->dpi; PTSOUT[11] = (s16)d->dpi;
    CONTRL[2] = 6;
    CONTRL[4] = 45;
}

void pdf_open(Wk *w, VDIPB *pb)
{
    PdfDoc *d = (PdfDoc *)p_calloc(sizeof(PdfDoc));
    s16 wmm, hmm;
    s32 h;
    int i, um;
    CONTRL[6] = 0;
    if (!d) return;
    d->wbuf = (u8 *)p_alloc(WBUF);
    if (!d->wbuf) { heap_free(d); return; }
    d->page = (char *)p_alloc(PCHUNK);     /* fixed size: never needs to grow */
    d->pcap = d->page ? PCHUNK : 0;
    d->dpi = xg_cfg.pdfdpi;
    cfg_paper_size(xg_cfg.paper, &wmm, &hmm);
    d->wpx = (s16)muldiv(wmm, d->dpi, 254);
    d->hpx = (s16)muldiv(hmm, d->dpi, 254);
    d->wpt = muldiv(INT2FIX(wmm), 72, 254);
    d->hpt = muldiv(INT2FIX(hmm), 72, 254);
    for (i = 0; i < 256; i++) {
        if (i < 16) { d->pal[i][0] = defpal[i][0]; d->pal[i][1] = defpal[i][1]; d->pal[i][2] = defpal[i][2]; }
        else d->pal[i][0] = d->pal[i][1] = d->pal[i][2] = (s16)(1000 - (i - 16) * 1000 / 239);
    }
    /* attributes from intin */
    d->l_type = (INTIN[1] >= 1 && INTIN[1] <= 7) ? INTIN[1] : 1;
    d->l_color = INTIN[2] & 0xff;
    d->m_type = (INTIN[3] >= 1 && INTIN[3] <= 6) ? INTIN[3] : 3;
    d->m_color = INTIN[4] & 0xff;
    w->t.color = INTIN[6] & 0xff;
    d->f_interior = INTIN[7];
    d->f_style = INTIN[8];
    d->f_color = INTIN[9] & 0xff;
    d->f_perim = 1;
    d->l_width = 1;
    d->m_height = (s16)(d->dpi / 12);
    d->nobj = OBJ_FONT0 + 4;
    if (!grow((void **)&d->xref, &d->maxobj, d->nobj, 4)) { doc_free(d); return; }
    /* The file itself is only made when the first output is written, so a
     * program that opens the printer just to ask about it leaves no empty
     * PDF behind. Check now that the folder takes files, so such a
     * program still learns when there is no usable printer. */
    make_path(d);
    h = xos_create(d->path);
    if (h < 0) { XLOG("cannot create "); XLOG(d->path); XLOG("\r\n"); doc_free(d); return; }
    xos_close(h);
    xos_delete(d->path);
    ws(d, "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    w->pdf = d;
    um = (int)(25400 / d->dpi);
    w->pw = w->ph = (s16)um;
    w->ppix = w->ppiy = INT2FIX(d->dpi);
    w->xres = (s16)(d->wpx - 1);
    w->yres = (s16)(d->hpx - 1);
    w->rom_nfonts = 1;
    w->t.pt = INT2FIX(10);
    work_out(d, pb, um);
    page_begin(d);
    CONTRL[6] = w->handle;
}

/* ---------------- drawing helpers ---------------- */

static void colour(PdfDoc *d, int idx, int stroke, int light)
{
    s16 *c = d->pal[idx & 0xff];
    int k;
    for (k = 0; k < 3; k++) {
        s32 v = c[k];
        if (light) v = (v + 1000) / 2;
        pf(d, muldiv(INT2FIX(v), 1, 1000));
    }
    pc(d, stroke ? "RG\n" : "rg\n");
}

static void tint(PdfDoc *d, int idx, s32 cover)   /* cover 0..1000 */
{
    s16 *c = d->pal[idx & 0xff];
    int k;
    for (k = 0; k < 3; k++) {
        s32 v = 1000 - muldiv(1000 - c[k], cover, 1000);
        pf(d, muldiv(INT2FIX(v), 1, 1000));
    }
    pc(d, "rg\n");
}

static void clip(Wk *w)
{
    PdfDoc *d = w->pdf;
    if (!w->clip_on) return;
    pn(d, w->clip[0]); pn(d, w->clip[1]);
    pn(d, w->clip[2] - w->clip[0] + 1); pn(d, w->clip[3] - w->clip[1] + 1);
    pc(d, "re W n\n");
}

static void pt2(PdfDoc *d, s32 x, s32 y, const char *op)
{
    pf(d, INT2FIX(x) + FIX_HALF); pf(d, INT2FIX(y) + FIX_HALF); pc(d, op);
}

static s32 unit(PdfDoc *d) { return d->dpi >= 150 ? d->dpi / 100 : 1; }

static void line_style(Wk *w)
{
    PdfDoc *d = w->pdf;
    s32 u = unit(d) * (d->l_width > 1 ? 1 : 1);
    pn(d, d->l_width > 0 ? d->l_width : 1); pc(d, "w ");
    pc(d, d->l_beg == 2 || d->l_end == 2 ? "1 J 1 j\n" : "0 J 0 j\n");
    colour(d, d->l_color, 1, 0);
    switch (d->l_type) {
    case 2: pc(d, "["); pn(d, 12 * u); pn(d, 4 * u); pc(d, "] 0 d\n"); break;
    case 3: pc(d, "["); pn(d, 2 * u); pn(d, 6 * u); pc(d, "] 0 d\n"); break;
    case 4: pc(d, "["); pn(d, 8 * u); pn(d, 3 * u); pn(d, 2 * u); pn(d, 3 * u); pc(d, "] 0 d\n"); break;
    case 5: pc(d, "["); pn(d, 8 * u); pn(d, 8 * u); pc(d, "] 0 d\n"); break;
    case 6: pc(d, "["); pn(d, 8 * u); pn(d, 2 * u); pn(d, 2 * u); pn(d, 2 * u); pn(d, 2 * u); pn(d, 2 * u); pc(d, "] 0 d\n"); break;
    case 7: {
        u16 p = d->l_udsty;
        int i, run = 0, on = (p & 0x8000) ? 1 : 0, first = on;
        pc(d, "[");
        if (!first) pn(d, 0);
        for (i = 0; i < 16; i++) {
            int b = (p >> (15 - i)) & 1;
            if (b == on) run++;
            else { pn(d, run * u); run = 1; on = b; }
        }
        pn(d, run * u);
        pc(d, "] 0 d\n");
        break;
    }
    default: break;
    }
}

/* returns 0 if nothing should be filled */
static int fill_style(Wk *w, int col)
{
    PdfDoc *d = w->pdf;
    switch (d->f_interior) {
    case 0: return 0;
    case 1: colour(d, col, 0, 0); return 1;
    case 2:
        if (d->f_style >= 1 && d->f_style <= 8) tint(d, col, d->f_style == 8 ? 1000 : d->f_style * 110);
        else tint(d, col, 500);
        return 1;
    case 3: tint(d, col, 300); return 1;
    case 4: tint(d, col, 500); return 1;
    default: colour(d, col, 0, 0); return 1;
    }
}

static void poly_path(PdfDoc *d, const s16 *p, int n, int close)
{
    int i;
    for (i = 0; i < n; i++) pt2(d, p[2 * i], p[2 * i + 1], i ? "l\n" : "m\n");
    if (close) pc(d, "h\n");
}

static void perimeter(Wk *w)
{
    PdfDoc *d = w->pdf;
    if (!d->f_perim) return;
    pc(d, "1 w [] 0 d ");
    colour(d, d->f_color, 1, 0);
}

/* elliptical arc as Bezier segments; angles in 1/10 degree, y down */
static void arc_path(PdfDoc *d, s32 cx, s32 cy, s32 rx, s32 ry, s32 a0, s32 a1, int start)
{
    s32 span, nseg, i;
    fix fcx = INT2FIX(cx) + FIX_HALF, fcy = INT2FIX(cy) + FIX_HALF;
    while (a1 <= a0) a1 += 3600;
    span = a1 - a0;
    if (span > 3600) span = 3600;
    nseg = (span + 899) / 900;
    for (i = 0; i < nseg; i++) {
        s32 t0 = a0 + span * i / nseg, t1 = a0 + span * (i + 1) / nseg, dl = t1 - t0;
        fix k = muldiv(fixdiv(fixsin(dl / 4), fixcos(dl / 4)), 4, 3);
        fix c0 = fixcos(t0), s0 = fixsin(t0), c1 = fixcos(t1), s1 = fixsin(t1);
        fix x0 = fcx + rx * c0, y0 = fcy - ry * s0;
        fix x3 = fcx + rx * c1, y3 = fcy - ry * s1;
        fix x1 = x0 - rx * fixmul(k, s0), y1 = y0 - ry * fixmul(k, c0);
        fix x2 = x3 + rx * fixmul(k, s1), y2 = y3 + ry * fixmul(k, c1);
        if (i == 0) { pf(d, x0); pf(d, y0); pc(d, start ? "m\n" : "l\n"); }
        pf(d, x1); pf(d, y1); pf(d, x2); pf(d, y2); pf(d, x3); pf(d, y3); pc(d, "c\n");
    }
}

static void filled_shape_end(Wk *w, int closepath)
{
    PdfDoc *d = w->pdf;
    int f = fill_style(w, d->f_color);
    if (closepath) pc(d, "h ");
    if (f && d->f_perim) { perimeter(w); pc(d, "B\n"); }
    else if (f) pc(d, "f\n");
    else if (d->f_perim) { perimeter(w); pc(d, "S\n"); }
    else pc(d, "n\n");
}

static void rbox_path(PdfDoc *d, const s16 *p)
{
    s32 x0 = XG_MIN(p[0], p[2]), x1 = XG_MAX(p[0], p[2]);
    s32 y0 = XG_MIN(p[1], p[3]), y1 = XG_MAX(p[1], p[3]);
    s32 r = XG_MIN(x1 - x0, y1 - y0) / 4;
    if (r > d->dpi / 10) r = d->dpi / 10;
    arc_path(d, x1 - r, y0 + r, r, r, 0, 900, 1);
    arc_path(d, x0 + r, y0 + r, r, r, 900, 1800, 0);
    arc_path(d, x0 + r, y1 - r, r, r, 1800, 2700, 0);
    arc_path(d, x1 - r, y1 - r, r, r, 2700, 3600, 0);
    pc(d, "h\n");
}

static void markers(Wk *w, const s16 *p, int n)
{
    PdfDoc *d = w->pdf;
    s32 h = XG_MAX(d->m_height, 3) / 2, i;
    pc(d, "q ");
    pn(d, XG_MAX(1, d->dpi / 150)); pc(d, "w [] 0 d 0 J ");
    colour(d, d->m_color, 1, 0);
    colour(d, d->m_color, 0, 0);
    for (i = 0; i < n; i++) {
        s32 x = p[2 * i], y = p[2 * i + 1];
        switch (d->m_type) {
        case 1: pn(d, x - h / 4); pn(d, y - h / 4); pn(d, h / 2 + 1); pn(d, h / 2 + 1); pc(d, "re f\n"); break;
        case 2: pt2(d, x - h, y, "m "); pt2(d, x + h, y, "l "); pt2(d, x, y - h, "m "); pt2(d, x, y + h, "l S\n"); break;
        case 3: pt2(d, x - h, y, "m "); pt2(d, x + h, y, "l "); pt2(d, x, y - h, "m "); pt2(d, x, y + h, "l ");
                pt2(d, x - h, y - h, "m "); pt2(d, x + h, y + h, "l "); pt2(d, x - h, y + h, "m "); pt2(d, x + h, y - h, "l S\n"); break;
        case 4: pn(d, x - h); pn(d, y - h); pn(d, 2 * h); pn(d, 2 * h); pc(d, "re S\n"); break;
        case 5: pt2(d, x - h, y - h, "m "); pt2(d, x + h, y + h, "l "); pt2(d, x - h, y + h, "m "); pt2(d, x + h, y - h, "l S\n"); break;
        default: pt2(d, x, y - h, "m "); pt2(d, x + h, y, "l "); pt2(d, x, y + h, "l "); pt2(d, x - h, y, "l h S\n"); break;
        }
    }
    pc(d, "Q\n");
}

static void gdp(Wk *w, VDIPB *pb)
{
    PdfDoc *d = w->pdf;
    s16 *p = PTSIN;
    pc(d, "q ");
    clip(w);
    switch (CONTRL[5]) {
    case 1: {   /* v_bar */
        s32 x0 = XG_MIN(p[0], p[2]), x1 = XG_MAX(p[0], p[2]), y0 = XG_MIN(p[1], p[3]), y1 = XG_MAX(p[1], p[3]);
        pn(d, x0); pn(d, y0); pn(d, x1 - x0 + 1); pn(d, y1 - y0 + 1); pc(d, "re\n");
        filled_shape_end(w, 0);
        break;
    }
    case 2:     /* v_arc */
        line_style(w);
        arc_path(d, p[0], p[1], p[6], p[6], INTIN[0], INTIN[1], 1);
        pc(d, "S\n");
        break;
    case 3:     /* v_pieslice */
        pt2(d, p[0], p[1], "m\n");
        arc_path(d, p[0], p[1], p[6], p[6], INTIN[0], INTIN[1], 0);
        filled_shape_end(w, 1);
        break;
    case 4:     /* v_circle */
        arc_path(d, p[0], p[1], p[4], p[4], 0, 3600, 1);
        filled_shape_end(w, 1);
        break;
    case 5:     /* v_ellipse */
        arc_path(d, p[0], p[1], p[2], p[3], 0, 3600, 1);
        filled_shape_end(w, 1);
        break;
    case 6:     /* v_ellarc */
        line_style(w);
        arc_path(d, p[0], p[1], p[2], p[3], INTIN[0], INTIN[1], 1);
        pc(d, "S\n");
        break;
    case 7:     /* v_ellpie */
        pt2(d, p[0], p[1], "m\n");
        arc_path(d, p[0], p[1], p[2], p[3], INTIN[0], INTIN[1], 0);
        filled_shape_end(w, 1);
        break;
    case 8:     /* v_rbox */
        line_style(w);
        rbox_path(d, p);
        pc(d, "S\n");
        break;
    case 9:     /* v_rfbox */
        rbox_path(d, p);
        filled_shape_end(w, 0);
        break;
    }
    pc(d, "Q\n");
}

/* ---------------- text ---------------- */

static const u8 atari_to_ansi[128] = {
    0xc7,0xfc,0xe9,0xe2,0xe4,0xe0,0xe5,0xe7,0xea,0xeb,0xe8,0xef,0xee,0xec,0xc4,0xc5,
    0xc9,0xe6,0xc6,0xf4,0xf6,0xf2,0xfb,0xf9,0xff,0xd6,0xdc,0xa2,0xa3,0xa5,0xdf,0x83,
    0xe1,0xed,0xf3,0xfa,0xf1,0xd1,0xaa,0xba,0xbf,0x3f,0xac,0xbd,0xbc,0xa1,0xab,0xbb,
    0xe3,0xf5,0xd8,0xf8,0x9c,0x8c,0xc0,0xc3,0xd5,0xa8,0xb4,0x86,0xb6,0xa9,0xae,0x99,
    0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,
    0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0xa7,0x3f,0x3f,
    0x3f,0xdf,0x3f,0x3f,0x3f,0x3f,0xb5,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,
    0x3f,0xb1,0x3f,0x3f,0x3f,0x3f,0xf7,0x3f,0xb0,0x95,0xb7,0x3f,0x3f,0xb2,0xb3,0xaf
};

int pdf_builtin_metrics(Wk *w, TxtMetrics *m)
{
    if (w->kind == WK_MEM) return soft_sysfont_metrics(w, m);   /* real bitmap fonts */
    fix em = txt_pt2px_y(w, w->t.pt);
    if (em < FIX_ONE) em = FIX_ONE;
    xmemset(m, 0, sizeof *m);
    m->emx = m->emy = em;
    m->charw = m->cellw = FIXROUND(muldiv(em, 600, 1000));
    m->top = FIXROUND(muldiv(em, 800, 1000));
    m->asc = FIXROUND(muldiv(em, 629, 1000));
    m->half = FIXROUND(muldiv(em, 426, 1000));
    m->desc = FIXROUND(muldiv(em, 157, 1000));
    m->bot = FIXROUND(muldiv(em, 220, 1000));
    m->charh = m->top;
    m->cellh = m->top + m->bot;
    return 1;
}

void pdf_builtin_query(Wk *w, VDIPB *pb)
{
    TxtMetrics m;
    int op = CONTRL[0], i;
    pdf_builtin_metrics(w, &m);
    switch (op) {
    case 116: case 240: {
        s32 n = CONTRL[3];
        fix adv = INT2FIX(n * m.cellw);
        txt_extent(w, &m, adv, PTSOUT);
        CONTRL[2] = 4; CONTRL[4] = 0;
        break;
    }
    case 117: case 247:
        INTOUT[0] = INTIN[0];
        for (i = 0; i < 6; i++) PTSOUT[i] = 0;
        PTSOUT[0] = (s16)m.cellw;
        CONTRL[2] = (op == 117) ? 3 : 2; CONTRL[4] = 1;
        break;
    case 131:
        INTOUT[0] = 32; INTOUT[1] = 255;
        PTSOUT[0] = (s16)m.cellw; PTSOUT[1] = (s16)m.bot; PTSOUT[2] = 0; PTSOUT[3] = (s16)m.desc;
        PTSOUT[4] = 0; PTSOUT[5] = (s16)m.half; PTSOUT[6] = 0; PTSOUT[7] = (s16)m.asc; PTSOUT[8] = 0; PTSOUT[9] = (s16)m.top;
        CONTRL[2] = 5; CONTRL[4] = 2;
        break;
    case 38:
        INTOUT[0] = 1; INTOUT[1] = w->t.color; INTOUT[2] = w->t.rot;
        INTOUT[3] = w->t.halign; INTOUT[4] = w->t.valign; INTOUT[5] = w->t.wrmode;
        PTSOUT[0] = (s16)m.charw; PTSOUT[1] = (s16)m.charh; PTSOUT[2] = (s16)m.cellw; PTSOUT[3] = (s16)m.cellh;
        CONTRL[2] = 2; CONTRL[4] = 6;
        break;
    case 130:
        INTOUT[0] = 1;
        sysfont_name(INTOUT);       /* printed as Courier, named like the screen's */
        INTOUT[33] = 0;
        CONTRL[4] = 34;
        break;
    case 21:
        INTOUT[0] = 1; CONTRL[4] = 1;
        break;
    default:
        CONTRL[2] = 0; CONTRL[4] = 0;
        break;
    }
}

/* text matrix for direction rot and em size (both fix) */
static void text_matrix(PdfDoc *d, Wk *w, fix sx, fix sy, fix skew, fix x, fix y)
{
    fix c = fixcos(w->t.rot), s = fixsin(w->t.rot);
    pf(d, fixmul(sx, c)); pf(d, -fixmul(sx, s));
    pf(d, fixmul(fixmul(sy, skew), c) - fixmul(sy, s));
    pf(d, -fixmul(fixmul(sy, skew), s) - fixmul(sy, c));
    pf(d, x); pf(d, y);
}

static void builtin_text(Wk *w, int x, int y, const s16 *codes, int n, int jlen, int wordsp)
{
    PdfDoc *d = w->pdf;
    TxtMetrics m;
    fix adv, ax = 0, c = fixcos(w->t.rot), s = fixsin(w->t.rot), px, py, tw = 0;
    int ay, i, fnt = ((w->t.effects & 1) ? 1 : 0) + ((w->t.effects & 4) ? 2 : 0), spaces = 0;
    if (n <= 0) return;
    pdf_builtin_metrics(w, &m);
    adv = INT2FIX(n * m.cellw);
    if (jlen > 0) {
        for (i = 0; i < n; i++) if ((codes[i] & 0xff) == ' ') spaces++;
        if (wordsp && spaces) tw = (INT2FIX(jlen) - adv) / spaces;
        adv = INT2FIX(jlen);
    }
    if (w->t.halign == 1) ax = -adv / 2; else if (w->t.halign == 2) ax = -adv;
    ay = txt_valign(w, &m);
    px = INT2FIX(x) + fixmul(ax, c) + fixmul(INT2FIX(ay), s);
    py = INT2FIX(y) - fixmul(ax, s) + fixmul(INT2FIX(ay), c);
    pc(d, "q ");
    clip(w);
    colour(d, w->t.color, 0, w->t.effects & 2);
    if (w->t.effects & 16) { colour(d, w->t.color, 1, 0); pn(d, XG_MAX(1, FIX2INT(m.emy) / 30)); pc(d, "w 1 Tr\n"); }
    pc(d, "BT /F"); pn(d, fnt); pf(d, m.emy); pc(d, "Tf ");
    if (tw) { pf(d, tw); pc(d, "Tw "); }
    text_matrix(d, w, FIX_ONE, FIX_ONE, 0, px, py);
    pc(d, "Tm ");
    d->nosplit = 1;                 /* the string literal is one token */
    pc(d, "(");
    for (i = 0; i < n; i++) {
        int ch = codes[i] & 0xff;
        char b[5];
        if (ch >= 128) ch = atari_to_ansi[ch - 128];
        if (ch < 32) ch = '?';
        if (ch == '(' || ch == ')' || ch == '\\') { b[0] = '\\'; b[1] = (char)ch; b[2] = 0; }
        else if (ch >= 127) { b[0] = '\\'; b[1] = (char)('0' + (ch >> 6)); b[2] = (char)('0' + ((ch >> 3) & 7)); b[3] = (char)('0' + (ch & 7)); b[4] = 0; }
        else { b[0] = (char)ch; b[1] = 0; }
        pc(d, b);
    }
    pc(d, ")");
    d->nosplit = 0;
    pc(d, " Tj ET\n");
    if (w->t.effects & 8) {
        /* text space is y up: the baseline is at -ay */
        fix ut = XG_MAX(FIX_ONE, muldiv(m.emy, 50, 1000));
        fix uy = -INT2FIX(ay) - muldiv(m.emy, 100, 1000);
        pc(d, "q "); text_matrix(d, w, FIX_ONE, FIX_ONE, 0, INT2FIX(x), INT2FIX(y)); pc(d, "cm ");
        pf(d, ax); pf(d, uy - ut); pf(d, adv); pf(d, ut); pc(d, "re f Q\n");
    }
    pc(d, "Q\n");
}

/* outline glyph path writer (orus) */
typedef struct PathSink { SpdSink s; PdfDoc *d; char *buf; s32 n, cap; int err; } PathSink;

static void ps_put(PathSink *p, const char *s)
{
    s32 n = xstrlen(s);
    if (p->n + n + 1 > p->cap) {
        s32 nc = p->cap ? p->cap * 2 : 1024;
        char *q;
        while (nc < p->n + n + 1) nc *= 2;
        q = (char *)p_realloc(p->buf, nc);
        if (!q) { p->err = 1; return; }
        p->buf = q; p->cap = nc;
    }
    xmemcpy(p->buf + p->n, s, n);
    p->n += n;
}
static void ps_xy(PathSink *p, fix x, fix y)
{
    char b[16];
    int n = xitoa(b, FIXROUND(x)); b[n++] = ' '; b[n] = 0; ps_put(p, b);
    n = xitoa(b, FIXROUND(y)); b[n++] = ' '; b[n] = 0; ps_put(p, b);
}
static void ps_move(SpdSink *s, fix x, fix y) { ps_xy((PathSink *)s, x, y); ps_put((PathSink *)s, "m\n"); }
static void ps_line(SpdSink *s, fix x, fix y) { ps_xy((PathSink *)s, x, y); ps_put((PathSink *)s, "l\n"); }
static void ps_curve(SpdSink *s, fix a, fix b, fix c, fix d, fix e, fix f)
{
    PathSink *p = (PathSink *)s;
    ps_xy(p, a, b); ps_xy(p, c, d); ps_xy(p, e, f); ps_put(p, "c\n");
}
static void ps_close(SpdSink *s) { ps_put((PathSink *)s, "h\n"); }

static int glyph_path(XFont *f, int idx, PathSink *p)
{
    SpdXform xf;
    xmemset(&xf, 0, sizeof xf);
    xf.raw = 1; xf.sx = xf.sy = FIX_ONE;
    p->s.move = ps_move; p->s.line = ps_line; p->s.curve = ps_curve; p->s.close = ps_close;
    p->buf = NULL; p->n = p->cap = 0; p->err = 0;
    spd_char_outline(&f->spd, idx, &xf, &p->s);
    return !p->err;
}

static int glyph_xobject(PdfDoc *d, XFont *f, int fi, int idx)
{
    s32 i, obj;
    PathSink p;
    s16 bb[4];
    for (i = 0; i < d->nxo; i++) if (d->xo[i].font == fi && d->xo[i].idx == idx) return (int)i;
    if (!grow((void **)&d->xo, &d->maxxo, d->nxo + 1, (s32)sizeof(PdfXo))) return -1;
    if (!glyph_path(f, idx, &p)) { heap_free(p.buf); return -1; }
    if (!spd_char_bbox(&f->spd, idx, bb)) bb[0] = bb[1] = bb[2] = bb[3] = 0;
    ps_put(&p, "f\n");
    obj = obj_new(d);
    obj_begin(d, obj);
    ws(d, "<< /Type /XObject /Subtype /Form /BBox [");
    wnum(d, bb[0] - 1); ws(d, " "); wnum(d, bb[1] - 1); ws(d, " ");
    wnum(d, bb[2] + 1); ws(d, " "); wnum(d, bb[3] + 1);
    ws(d, "] /Length "); wnum(d, p.n); ws(d, " >>\nstream\n");
    wbytes(d, p.buf, p.n);
    ws(d, "\nendstream\nendobj\n");
    heap_free(p.buf);
    d->xo[d->nxo].font = (s16)fi;
    d->xo[d->nxo].idx = (s16)idx;
    d->xo[d->nxo].obj = obj;
    return (int)d->nxo++;
}

static void outline_text(Wk *w, int x, int y, const s16 *codes, int n, int frac,
                         int jlen, int wordsp, int charsp, const s16 *offs)
{
    PdfDoc *d = w->pdf;
    TxtMetrics m;
    TxtGlyph *g;
    fix adv, ax = 0, c, s;
    int ay, i, inl;
    if (!txt_metrics(w, &m)) return;
    if (txt_layout(w, &m, codes, n, frac, &g, &adv) <= 0) return;
    if (jlen > 0) txt_justify(g, n, &adv, INT2FIX(jlen), wordsp, charsp);
    if (w->t.halign == 1) ax = -adv / 2; else if (w->t.halign == 2) ax = -adv;
    ay = txt_valign(w, &m);
    c = fixcos(w->t.rot); s = fixsin(w->t.rot);
    inl = (w->t.effects & (1 | 16)) != 0;
    pc(d, "q ");
    clip(w);
    colour(d, w->t.color, 0, w->t.effects & 2);
    if (inl) {
        colour(d, w->t.color, 1, w->t.effects & 2);
        pn(d, XG_MAX(1, FIX2INT(m.emy) / ((w->t.effects & 1) ? 18 : 40))); pc(d, "w 1 j\n");
    }
    for (i = 0; i < n; i++) {
        fix tx = ax + g[i].x, gx, gy;
        if (g[i].idx < 0) continue;
        gx = INT2FIX(x) + fixmul(tx, c) + fixmul(INT2FIX(ay), s);
        gy = INT2FIX(y) - fixmul(tx, s) + fixmul(INT2FIX(ay), c);
        if (offs) { gx += INT2FIX(offs[2 * i]); gy += INT2FIX(offs[2 * i + 1]); }
        pc(d, "q ");
        text_matrix(d, w, m.sx, m.sy, m.skew, gx, gy);
        pc(d, "cm ");
        if (inl) {
            PathSink p;
            if (glyph_path(m.f, g[i].idx, &p) && p.n) {
                pcn(d, p.buf, p.n);
                /* line width is given in device px; undo the glyph scale */
                pf(d, fixdiv(INT2FIX(XG_MAX(1, FIX2INT(m.emy) / ((w->t.effects & 1) ? 18 : 40))), m.sy));
                pc(d, (w->t.effects & 1) ? "w B\n" : "w S\n");
            }
            heap_free(p.buf);
        } else {
            int k = glyph_xobject(d, m.f, w->t.fi, g[i].idx);
            if (k >= 0) { pc(d, "/G"); pn(d, k); pc(d, "Do "); }
        }
        pc(d, "Q\n");
    }
    if (w->t.effects & 8) {
        fix ut = XG_MAX(FIX_ONE, m.f->spd.ul_thk * m.sy);
        fix uy = INT2FIX(-ay) + m.f->spd.ul_pos * m.sy;
        pc(d, "q "); text_matrix(d, w, FIX_ONE, FIX_ONE, 0, INT2FIX(x), INT2FIX(y)); pc(d, "cm ");
        pf(d, ax); pf(d, uy - ut); pf(d, adv); pf(d, ut); pc(d, "re f Q\n");
    }
    pc(d, "Q\n");
    heap_free(g);
}

/* ---------------- images ---------------- */

static const u8 st16[16] = { 0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1 };
static const u8 st4[4] = { 0, 2, 3, 1 };

static u8 bits_at(const u8 *row, int x)
{
    return (u8)((row[x >> 3] >> (7 - (x & 7))) & 1);
}

/* PackBits rows for /RunLengthDecode: printed bitmaps are mostly white,
 * so a 600 dpi A4 page shrinks from about 4 MB to tens of KB */
static void img_row(PdfDoc *d, const u8 *s, s32 n)
{
    u8 b[130];
    s32 i = 0;
    while (i < n) {
        s32 run = 1;
        while (i + run < n && run < 128 && s[i + run] == s[i]) run++;
        if (run >= 2) {
            b[0] = (u8)(257 - run); b[1] = s[i];
            wbytes(d, b, 2);
            i += run;
        } else {
            s32 k = 0;
            while (i < n && k < 128 && !(i + 1 < n && s[i] == s[i + 1])) b[1 + k++] = s[i++];
            b[0] = (u8)(k - 1);
            wbytes(d, b, k + 1);
        }
    }
}

static s32 image_begin_pal(PdfDoc *d, int wd, int ht, int mode, const u8 *rgb, int nrgb);
static s32 image_begin(PdfDoc *d, int wd, int ht, int mode)
{
    return image_begin_pal(d, wd, ht, mode, NULL, 0);
}

/* mode 0: mask (1 = paint), 1: gray 1 bit (1 = black), 2: indexed 8 bit
 * with the workstation palette, or with nrgb RGB byte triplets from rgb */
static s32 image_begin_pal(PdfDoc *d, int wd, int ht, int mode, const u8 *rgb, int nrgb)
{
    s32 obj = obj_new(d), lobj = obj_new(d), i;
    if (d->err) return -1;
    if (!grow((void **)&d->img, &d->maximg, d->nimg + 1, 4)) return -1;
    obj_begin(d, obj);
    ws(d, "<< /Type /XObject /Subtype /Image /Width "); wnum(d, wd);
    ws(d, " /Height "); wnum(d, ht);
    if (mode == 0) ws(d, " /ImageMask true /BitsPerComponent 1 /Decode [1 0]");
    else if (mode == 1) ws(d, " /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0]");
    else {
        int n = (rgb && nrgb > 0) ? nrgb : 256;
        ws(d, " /ColorSpace [/Indexed /DeviceRGB "); wnum(d, n - 1); ws(d, "<");
        for (i = 0; i < n; i++) {
            static const char hx[] = "0123456789abcdef";
            int k;
            for (k = 0; k < 3; k++) {
                int v = rgb ? rgb[i * 3 + k] : d->pal[i][k] * 255 / 1000;
                char b[3];
                b[0] = hx[(v >> 4) & 15]; b[1] = hx[v & 15]; b[2] = 0;
                ws(d, b);
            }
        }
        ws(d, ">] /BitsPerComponent 8");
    }
    ws(d, " /Filter /RunLengthDecode /Length "); wnum(d, lobj); ws(d, " 0 R >>\nstream\n");
    d->ilen = lobj;
    d->istart = d->pos;
    d->img[d->nimg] = obj;
    return d->nimg++;
}

static void image_end(PdfDoc *d)
{
    static const u8 eod = 128;
    s32 len;
    wbytes(d, &eod, 1);
    len = d->pos - d->istart;
    ws(d, "\nendstream\nendobj\n");
    obj_begin(d, d->ilen);
    wnum(d, len);
    ws(d, "\nendobj\n");
}

static void place_image(Wk *w, int k, s32 x, s32 y, s32 wd, s32 ht)
{
    PdfDoc *d = w->pdf;
    pc(d, "q ");
    clip(w);
    pn(d, wd); pc(d, "0 0 "); pn(d, -ht); pn(d, x); pn(d, y + ht); pc(d, "cm /I"); pn(d, k); pc(d, "Do Q\n");
}

static void cpyfm(Wk *w, VDIPB *pb, int transparent)
{
    PdfDoc *d = w->pdf;
    MFDB *src = (MFDB *)GETPTR(CONTRL, 7);
    MFDB *dst = (MFDB *)GETPTR(CONTRL, 9);
    s16 *p = PTSIN;
    s32 sx0, sy0, wd, ht, r, x, k, srb, plane;
    int planes, mode;
    if (!src || !src->fd_addr || (dst && dst->fd_addr)) return;   /* only memory -> device */
    sx0 = XG_MIN(p[0], p[2]); sy0 = XG_MIN(p[1], p[3]);
    wd = XG_ABS(p[2] - p[0]) + 1; ht = XG_ABS(p[3] - p[1]) + 1;
    planes = src->fd_nplanes;
    srb = (s32)src->fd_wdwidth * 2;
    if (wd <= 0 || ht <= 0 || planes < 1 || planes > 8) return;
    if (planes == 1) {
        s32 rb = (wd + 7) >> 3;
        u8 *row = (u8 *)p_alloc(rb);
        if (!row) return;
        mode = transparent ? 0 : 1;
        if (transparent) {          /* nothing to paint: skip blank bands */
            int any = 0;
            for (r = 0; r < ht && !any; r++) {
                const u8 *s = (const u8 *)src->fd_addr + (sy0 + r) * srb;
                for (x = 0; x < rb; x++) {
                    u8 v = txt_get8(s, (int)srb, (int)(sx0 + x * 8));
                    if (x == rb - 1 && (wd & 7)) v &= (u8)(0xff << (8 - (wd & 7)));
                    if (v) { any = 1; break; }
                }
            }
            if (!any) { heap_free(row); return; }
        }
        k = image_begin(d, (int)wd, (int)ht, mode);
        if (k < 0) { heap_free(row); return; }
        for (r = 0; r < ht; r++) {
            const u8 *s = (const u8 *)src->fd_addr + (sy0 + r) * srb;
            for (x = 0; x < rb; x++) row[x] = txt_get8(s, (int)srb, (int)(sx0 + x * 8));
            if (wd & 7) row[rb - 1] &= (u8)(0xff << (8 - (wd & 7)));
            if (!transparent && INTIN[0] == 12) for (x = 0; x < rb; x++) row[x] = (u8)~row[x];  /* NOT_S */
            img_row(d, row, rb);
        }
        image_end(d);
        heap_free(row);
        if (transparent) {
            pc(d, "q "); colour(d, INTIN[1], 0, 0);
            pn(d, wd); pc(d, "0 0 "); pn(d, -ht); pn(d, p[4]); pn(d, p[5] + ht); pc(d, "cm /I"); pn(d, k); pc(d, "Do Q\n");
        } else place_image(w, (int)k, p[4], p[5], wd, ht);
        return;
    }
    {
        u8 *row = (u8 *)p_alloc(wd);
        s32 plsize = srb * src->fd_h;
        if (!row) return;
        k = image_begin(d, (int)wd, (int)ht, 2);
        if (k < 0) { heap_free(row); return; }
        for (r = 0; r < ht; r++) {
            for (x = 0; x < wd; x++) {
                int v = 0;
                for (plane = 0; plane < planes; plane++) {
                    const u8 *s = (const u8 *)src->fd_addr + plane * plsize + (sy0 + r) * srb;
                    v |= bits_at(s, (int)(sx0 + x)) << plane;
                }
                if (planes == 2) v = st4[v & 3];
                else if (v < 16) v = st16[v];
                else if (v == 255) v = 1;
                row[x] = (u8)v;
            }
            img_row(d, row, wd);
        }
        image_end(d);
        heap_free(row);
        place_image(w, (int)k, p[4], p[5], wd, ht);
    }
}

/* GEM .IMG (v_bit_image) */
typedef struct Rdr { s32 h; u8 buf[512]; s32 n, p; int eof; u8 pal[256 * 3]; int npal; } Rdr;
static int rd(Rdr *r)
{
    if (r->p >= r->n) {
        if (r->eof) return -1;
        r->n = xos_read(r->h, 512, r->buf);
        r->p = 0;
        if (r->n <= 0) { r->eof = 1; return -1; }
    }
    return r->buf[r->p++];
}

static void bit_image(Wk *w, VDIPB *pb)
{
    PdfDoc *d = w->pdf;
    char fn[XG_PATHLEN];
    u8 hdr[16];
    Rdr *rr;
    s32 i, n = CONTRL[3] - 5, wd, ht, planes, plen, rb, y, x0, y0, x1, y1, dw, dh, k;
    u8 *line, *pat;
    if (n <= 0) return;
    for (i = 0; i < n && i < XG_PATHLEN - 1; i++) fn[i] = (char)INTIN[5 + i];
    fn[i] = 0;
    rr = (Rdr *)p_calloc(sizeof(Rdr));
    if (!rr) return;
    rr->h = xos_open(fn, 0);
    if (rr->h < 0) { heap_free(rr); return; }
    for (i = 0; i < 16; i++) { int c = rd(rr); hdr[i] = (u8)(c < 0 ? 0 : c); }
    plen = (hdr[2] << 8 | hdr[3]) * 2;
    planes = hdr[4] << 8 | hdr[5];
    wd = hdr[12] << 8 | hdr[13];
    ht = hdr[14] << 8 | hdr[15];
    {   /* extended header: "XIMG", colour model 0 (RGB), then one
         * red, green, blue triplet (0-1000) per pixel value */
        u8 tag[6];
        int hi = 0, ximg = 0;
        for (i = 16; i < plen; i++) {
            int c = rd(rr);
            if (c < 0) c = 0;
            if (i < 22) {
                tag[i - 16] = (u8)c;
                if (i == 21) ximg = tag[0] == 'X' && tag[1] == 'I' && tag[2] == 'M' && tag[3] == 'G'
                                    && tag[4] == 0 && tag[5] == 0;
            } else if (ximg && i - 22 < 256 * 6) {
                if (!((i - 22) & 1)) hi = c;
                else {
                    s32 v = (s32)(hi << 8 | c);
                    if (v > 1000) v = 1000;
                    rr->pal[(i - 22) >> 1] = (u8)((v * 255 + 500) / 1000);
                }
            }
        }
        if (ximg && planes >= 2 && planes <= 8) {
            rr->npal = (int)((plen - 22) / 6);
            if (rr->npal > (1 << planes)) rr->npal = 1 << planes;
            if (rr->npal < 2) rr->npal = 0;
        }
    }
    if (wd <= 0 || ht <= 0 || planes < 1) { xos_close(rr->h); heap_free(rr); return; }
    rb = (wd + 7) >> 3;
    /* planes, pattern, and (colour) one index byte per pixel */
    line = (u8 *)p_alloc(rb * planes + 64 + (planes > 1 && planes <= 8 ? wd : 0));
    if (!line) { xos_close(rr->h); heap_free(rr); return; }
    pat = line + rb * planes;
    if (planes > 1 && planes <= 8) {
        /* colour: the file's own palette (XIMG), or else the pixel values
         * are colour registers, printed with the workstation palette */
        if (rr->npal) k = image_begin_pal(d, (int)wd, (int)ht, 2, rr->pal, 1 << planes);
        else k = image_begin(d, (int)wd, (int)ht, 2);
    } else k = image_begin(d, (int)wd, (int)ht, 1);
    y = 0;
    while (y < ht && k >= 0) {
        int rep = 1, pl, c;
        xmemset(line, 0, rb * planes);
        for (pl = 0; pl < planes; pl++) {
            s32 pos = 0;
            u8 *L = line + pl * rb;
            while (pos < rb) {
                c = rd(rr);
                if (c < 0) break;
                if (c == 0) {
                    int cnt = rd(rr), j, plen2;
                    if (cnt == 0) { rd(rr); rep = rd(rr); if (rep < 1) rep = 1; continue; }
                    plen2 = (hdr[6] << 8 | hdr[7]);
                    if (plen2 < 1 || plen2 > 64) plen2 = 1;
                    for (j = 0; j < plen2; j++) pat[j] = (u8)rd(rr);
                    while (cnt-- > 0) for (j = 0; j < plen2 && pos < rb; j++) L[pos++] = pat[j];
                } else if (c == 0x80) {
                    int cnt = rd(rr);
                    while (cnt-- > 0) { int v = rd(rr); if (pos < rb) L[pos++] = (u8)v; }
                } else {
                    int cnt = c & 0x7f;
                    u8 v = (c & 0x80) ? 0xff : 0;
                    while (cnt-- > 0 && pos < rb) L[pos++] = v;
                }
            }
        }
        if (planes > 1 && planes <= 8) {
            u8 *ix = pat + 64;
            for (x0 = 0; x0 < wd; x0++) {
                int v = 0;
                for (pl = 0; pl < planes; pl++) v |= bits_at(line + pl * rb, (int)x0) << pl;
                if (!rr->npal) {
                    if (planes == 2) v = st4[v & 3];
                    else if (v < 16) v = st16[v];
                    else if (v == 255) v = 1;
                }
                ix[x0] = (u8)v;
            }
            while (rep-- > 0 && y < ht) { img_row(d, ix, wd); y++; }
            if (rr->eof) {
                /* the rest of the image is background: pixel value 0 */
                for (x0 = 0; x0 < wd; x0++) ix[x0] = 0;
                while (y < ht) { img_row(d, ix, wd); y++; }
            }
        } else {
            /* more than 8 planes: no colour table, any set bit prints */
            for (pl = 1; pl < planes; pl++) for (x0 = 0; x0 < rb; x0++) line[x0] |= line[pl * rb + x0];
            while (rep-- > 0 && y < ht) { img_row(d, line, rb); y++; }
            if (rr->eof) {
                xmemset(line, 0, rb);
                while (y < ht) { img_row(d, line, rb); y++; }
            }
        }
    }
    if (k >= 0) image_end(d);
    xos_close(rr->h);
    heap_free(rr);
    heap_free(line);
    if (k < 0) return;
    x0 = XG_MIN(PTSIN[0], PTSIN[2]); x1 = XG_MAX(PTSIN[0], PTSIN[2]);
    y0 = XG_MIN(PTSIN[1], PTSIN[3]); y1 = XG_MAX(PTSIN[1], PTSIN[3]);
    dw = x1 - x0 + 1; dh = y1 - y0 + 1;
    if (INTIN[0]) {         /* keep aspect ratio */
        s32 pw = hdr[8] << 8 | hdr[9], ph = hdr[10] << 8 | hdr[11];
        s32 iw, ih;
        if (pw <= 0) pw = 1; if (ph <= 0) ph = 1;
        iw = wd * pw; ih = ht * ph;
        if ((s32)muldiv(dw, ih, iw) <= dh) { s32 nh = muldiv(dw, ih, iw);
            y0 += (INTIN[4] == 1) ? (dh - nh) / 2 : (INTIN[4] == 2) ? dh - nh : 0; dh = nh; }
        else { s32 nw = muldiv(dh, iw, ih);
            x0 += (INTIN[3] == 1) ? (dw - nw) / 2 : (INTIN[3] == 2) ? dw - nw : 0; dw = nw; }
    }
    place_image(w, (int)k, x0, y0, dw, dh);
}

/* ---------------- dispatcher ---------------- */

void pdf_vdi(Wk *w, VDIPB *pb)
{
    PdfDoc *d = w->pdf;
    int op = CONTRL[0], i, n;

    CONTRL[2] = 0;
    CONTRL[4] = 0;
    if (!d) return;
    switch (op) {
    case 2:     /* v_clswk */
        if (!d->nkids && !d->created && !d->updated && !page_has_content(d))
            XLOG("printer closed without output: no PDF written\r\n");
        else {
            doc_finish(d);
            XLOG(d->err ? "PDF write error\r\n" : "PDF finished\r\n");
        }
        doc_free(d);
        w->pdf = NULL;
        wk_free(w);
        return;
    case 3:     /* v_clrwk: new page */
        /* a page the program printed (v_updwk) is kept even if blank;
         * a stray v_clrwk before anything was drawn makes no page */
        if (page_has_content(d) || d->updated) { page_end(d); page_begin(d); }
        return;
    case 4:     /* v_updwk */
        d->updated = 1;
        return;
    case 5:     /* escapes */
        switch (CONTRL[5]) {
        case 1: INTOUT[0] = (s16)(d->hpx / 50); INTOUT[1] = (s16)(d->wpx / 30); CONTRL[4] = 2; break;
        case 20: if (page_has_content(d) || d->updated) { page_end(d); page_begin(d); } break;   /* v_form_adv */
        case 22: page_begin(d); break;                                          /* v_clear_disp_list */
        case 23: bit_image(w, pb); break;
        }
        return;
    case 6:     /* v_pline */
        n = CONTRL[1];
        if (n < 2) return;
        pc(d, "q "); clip(w); line_style(w);
        poly_path(d, PTSIN, n, 0);
        pc(d, "S Q\n");
        return;
    case 7:
        markers(w, PTSIN, CONTRL[1]);
        return;
    case 8:
        if (w->t.fi >= 0) outline_text(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 0, 0, 0, 0, NULL);
        else builtin_text(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 0, 0);
        return;
    case 241:
        if (w->t.fi >= 0) outline_text(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 1, 0, 0, 0, CONTRL[1] > 1 ? PTSIN + 2 : NULL);
        else builtin_text(w, PTSIN[0], PTSIN[1], INTIN, CONTRL[3], 0, 0);
        return;
    case 9:     /* v_fillarea */
        n = CONTRL[1];
        if (n < 2) return;
        pc(d, "q "); clip(w);
        poly_path(d, PTSIN, n, 0);
        filled_shape_end(w, 1);
        pc(d, "Q\n");
        return;
    case 11:
        if (CONTRL[5] == 10) {
            if (w->t.fi >= 0) outline_text(w, PTSIN[0], PTSIN[1], INTIN + 2, CONTRL[3] - 2, 0, PTSIN[2], INTIN[0], INTIN[1], NULL);
            else builtin_text(w, PTSIN[0], PTSIN[1], INTIN + 2, CONTRL[3] - 2, PTSIN[2], INTIN[0]);
        } else gdp(w, pb);
        return;
    case 114: { /* vr_recfl */
        s16 *p = PTSIN;
        s32 x0 = XG_MIN(p[0], p[2]), x1 = XG_MAX(p[0], p[2]), y0 = XG_MIN(p[1], p[3]), y1 = XG_MAX(p[1], p[3]);
        pc(d, "q "); clip(w);
        if (fill_style(w, d->f_color)) { pn(d, x0); pn(d, y0); pn(d, x1 - x0 + 1); pn(d, y1 - y0 + 1); pc(d, "re f\n"); }
        pc(d, "Q\n");
        return;
    }
    case 14:    /* vs_color */
        if (INTIN[0] >= 0 && INTIN[0] < 256)
            for (i = 0; i < 3; i++) d->pal[INTIN[0]][i] = (s16)XG_MAX(0, XG_MIN(1000, INTIN[1 + i]));
        return;
    case 26:    /* vq_color */
        i = INTIN[0] & 0xff;
        INTOUT[0] = INTIN[0];
        INTOUT[1] = d->pal[i][0]; INTOUT[2] = d->pal[i][1]; INTOUT[3] = d->pal[i][2];
        CONTRL[4] = 4;
        return;
    case 15: d->l_type = (INTIN[0] >= 1 && INTIN[0] <= 7) ? INTIN[0] : 1; INTOUT[0] = d->l_type; CONTRL[4] = 1; return;
    case 16: d->l_width = (PTSIN[0] < 1) ? 1 : (PTSIN[0] | 1); PTSOUT[0] = d->l_width; PTSOUT[1] = 0; CONTRL[2] = 1; return;
    case 17: d->l_color = INTIN[0] & 0xff; INTOUT[0] = d->l_color; CONTRL[4] = 1; return;
    case 108: d->l_beg = INTIN[0]; d->l_end = INTIN[1]; return;
    case 113: d->l_udsty = (u16)INTIN[0]; return;
    case 18: d->m_type = (INTIN[0] >= 1 && INTIN[0] <= 6) ? INTIN[0] : 3; INTOUT[0] = d->m_type; CONTRL[4] = 1; return;
    case 19: d->m_height = (PTSIN[1] < 1) ? 1 : PTSIN[1]; PTSOUT[0] = d->m_height; PTSOUT[1] = d->m_height; CONTRL[2] = 1; return;
    case 20: d->m_color = INTIN[0] & 0xff; INTOUT[0] = d->m_color; CONTRL[4] = 1; return;
    case 23: d->f_interior = (INTIN[0] >= 0 && INTIN[0] <= 4) ? INTIN[0] : 0; INTOUT[0] = d->f_interior; CONTRL[4] = 1; return;
    case 24: d->f_style = INTIN[0]; INTOUT[0] = d->f_style; CONTRL[4] = 1; return;
    case 25: d->f_color = INTIN[0] & 0xff; INTOUT[0] = d->f_color; CONTRL[4] = 1; return;
    case 104: d->f_perim = INTIN[0] ? 1 : 0; INTOUT[0] = d->f_perim; CONTRL[4] = 1; return;
    case 112: return;   /* vsf_udpat */
    case 22: w->t.color = INTIN[0] & 0xff; INTOUT[0] = w->t.color; CONTRL[4] = 1; return;
    case 32: w->t.wrmode = (INTIN[0] >= 1 && INTIN[0] <= 4) ? INTIN[0] : 1; INTOUT[0] = w->t.wrmode; CONTRL[4] = 1; return;
    case 35:    /* vql_attributes */
        INTOUT[0] = d->l_type; INTOUT[1] = d->l_color; INTOUT[2] = w->t.wrmode; INTOUT[3] = d->l_beg; INTOUT[4] = d->l_end;
        PTSOUT[0] = d->l_width; PTSOUT[1] = 0; CONTRL[2] = 1; CONTRL[4] = 5;
        return;
    case 36:    /* vqm_attributes */
        INTOUT[0] = d->m_type; INTOUT[1] = d->m_color; INTOUT[2] = w->t.wrmode;
        PTSOUT[0] = d->m_height; PTSOUT[1] = d->m_height; CONTRL[2] = 1; CONTRL[4] = 3;
        return;
    case 37:    /* vqf_attributes */
        INTOUT[0] = d->f_interior; INTOUT[1] = d->f_color; INTOUT[2] = d->f_style; INTOUT[3] = w->t.wrmode; INTOUT[4] = d->f_perim;
        CONTRL[4] = 5;
        return;
    case 102:   /* vq_extnd */
        if (INTIN[0]) {
            for (i = 0; i < 45; i++) INTOUT[i] = 0;
            INTOUT[0] = 0; INTOUT[1] = 256; INTOUT[2] = 0x3f; INTOUT[3] = 0; INTOUT[4] = 8; INTOUT[5] = 1;
            INTOUT[6] = 1000; INTOUT[8] = 2; INTOUT[9] = 4; INTOUT[11] = 1; INTOUT[14] = -1; INTOUT[15] = -1;
            INTOUT[19] = w->clip_on;
            for (i = 0; i < 12; i++) PTSOUT[i] = 0;
            PTSOUT[0] = w->clip[0]; PTSOUT[1] = w->clip[1]; PTSOUT[2] = w->clip[2]; PTSOUT[3] = w->clip[3];
            CONTRL[2] = 6; CONTRL[4] = 45;
        } else work_out(d, pb, w->pw);
        return;
    case 109:   /* vro_cpyfm */
        cpyfm(w, pb, 0);
        return;
    case 121:   /* vrt_cpyfm */
        cpyfm(w, pb, 1);
        return;
    case 110: { /* vr_trnfm: device format == standard format here */
        MFDB *s = (MFDB *)GETPTR(CONTRL, 7), *t = (MFDB *)GETPTR(CONTRL, 9);
        if (s && t && s->fd_addr && t->fd_addr && s != t)
            xmemcpy(t->fd_addr, s->fd_addr, (s32)s->fd_wdwidth * 2 * s->fd_h * s->fd_nplanes);
        if (t) t->fd_stand = (s16)!(s && s->fd_stand);
        return;
    }
    case 105:   /* v_get_pixel */
        INTOUT[0] = 0; INTOUT[1] = 0; CONTRL[4] = 2;
        return;
    }
    if (xg_text_op(w, pb)) return;
    pdf_builtin_query(w, pb);
}
