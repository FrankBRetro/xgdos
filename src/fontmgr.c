/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* fontmgr.c - Speedo font table, lazy loading with LRU eviction */
#include "xgdos.h"
#include "xos.h"
#include "xstd.h"
#include "heap.h"
#include "charmap.h"
#include "gcache.h"

XgConfig xg_cfg;
XFont   *xg_font;
int      xg_nfont;
u32      xg_tick;

static int unique_id(int want, int self)
{
    int i, again = 1;
    while (again) {
        again = 0;
        for (i = 0; i < xg_nfont; i++)
            if (i != self && xg_font[i].vdi_id == want) { want++; again = 1; }
    }
    return want;
}

static int probe(XFont *f)
{
    u8 hdr[SPD_HDR_MIN];
    SpdInfo inf;
    s32 h = xos_open(f->path, 0), n;
    f->present = 0;
    if (h < 0) return 0;
    n = xos_read(h, SPD_HDR_MIN, hdr);
    xos_close(h);
    if (n < 360 || !spd_probe(hdr, n, &inf)) return 0;
    xstrcpy(f->name, inf.name);
    xstrcpy(f->sname, inf.sname);
    xstrcpy(f->face, inf.face);
    if (!f->name[0]) xstrcpy(f->name, inf.sname);
    f->bsid = inf.bsid;
    f->orupm = inf.orupm;
    f->ymin = inf.ymin;
    f->ymax = inf.ymax;
    f->fsize = inf.fsize;
    /* estimates until the font is loaded */
    f->m_top = inf.ymax;
    f->m_bot = (s16)-inf.ymin;
    f->m_asc = (s16)(inf.orupm * 72 / 100);
    f->m_half = (s16)(inf.orupm * 45 / 100);
    f->m_desc = (s16)(inf.orupm * 22 / 100);
    f->m_maxw = (s16)inf.orupm;
    f->present = 1;
    return 1;
}

static int find_path(const char *path)
{
    int i;
    for (i = 0; i < xg_nfont; i++) if (!xstricmp(xg_font[i].path, path)) return i;
    return -1;
}

static int add_font(const char *path, int enabled, int vdi_id, int cfgslot)
{
    XFont *f;
    int i = find_path(path);
    if (i >= 0) {
        f = &xg_font[i];
        f->enabled = (u8)enabled;
        f->cfgslot = (u8)cfgslot;
        if (!f->loaded) probe(f);
        if (vdi_id && vdi_id != f->vdi_id) f->vdi_id = (s16)unique_id(vdi_id, i);
        return i;
    }
    if (xg_nfont >= XG_MAXFONTS) return -1;
    f = &xg_font[xg_nfont];
    xmemset(f, 0, sizeof *f);
    xstrncpy0(f->path, path, XG_PATHLEN);
    f->enabled = (u8)enabled;
    f->cfgslot = (u8)cfgslot;
    if (!probe(f)) f->enabled = 0;
    i = xg_nfont++;
    f->vdi_id = (s16)unique_id(vdi_id ? vdi_id : 5000 + (f->bsid > 0 ? f->bsid : i), i);
    return i;
}

static void build(void)
{
    char full[XG_PATHLEN];
    int i;
    for (i = 0; i < xg_nfont; i++) xg_font[i].enabled = 0;
    if (xg_cfg.have_spd_lines) {
        for (i = 0; i < xg_cfg.nspd; i++) {
            cfg_spd_fullpath(&xg_cfg, &xg_cfg.spd[i], full);
            add_font(full, xg_cfg.spd[i].enabled, xg_cfg.spd[i].vdi_id, i);
        }
    } else {
        /* no font lines: every *.SPD in the font folders is enabled
         * (a file name met again in a later folder is skipped) */
        XosFind fd;
        char pat[XG_PATHLEN];
        int d, k;
        for (d = 0; d < xg_cfg.nfontdir; d++) {
            xpath_join(pat, xg_cfg.fontdir[d], "*.SPD");
            if (xos_findfirst(&fd, pat)) {
                do {
                    int dup = 0;
                    for (k = 0; k < xg_nfont; k++)
                        if (!xstricmp(xpath_base(xg_font[k].path), fd.name)) dup = 1;
                    if (dup) continue;
                    xpath_join(full, xg_cfg.fontdir[d], fd.name);
                    add_font(full, 1, 0, 255);
                } while (xos_findnext(&fd));
            }
            xos_findend(&fd);
        }
    }
}

int fm_init(void)
{
    if (!xg_font) {
        xg_font = (XFont *)heap_calloc((s32)sizeof(XFont) * XG_MAXFONTS);
        if (!xg_font) return 0;
    }
    xg_nfont = 0;
    build();
    return xg_nfont;
}

int fm_reload(void)
{
    build();
    gc_flush();
    return xg_nfont;
}

static void unload(XFont *f)
{
    int fi = (int)(f - xg_font);
    gc_flush_font(fi);
    heap_free(f->data);
    heap_free(f->idmap);
    f->data = NULL;
    f->idmap = NULL;
    f->loaded = 0;
}

void fm_flush_unused(void)
{
    int i;
    for (i = 0; i < xg_nfont; i++)
        if (xg_font[i].loaded && !xg_font[i].enabled) unload(&xg_font[i]);
}

static void *alloc_evict(s32 n, XFont *keep)
{
    void *p;
    for (;;) {
        int i, best = -1;
        p = heap_alloc(n);
        if (p) return p;
        if (gc_shrink(n)) continue;     /* glyph cache gives memory back first */
        for (i = 0; i < xg_nfont; i++) {
            XFont *f = &xg_font[i];
            if (!f->loaded || f == keep) continue;
            if (best < 0 || f->lru < xg_font[best].lru) best = i;
        }
        if (best < 0) return NULL;
        unload(&xg_font[best]);
    }
}

static s16 gtop(XFont *f, int id, int top)
{
    s16 bb[4];
    int idx = spd_index_of_id(&f->spd, (u16)id);
    if (idx < 0 || !spd_char_bbox(&f->spd, idx, bb)) return 0;
    return top ? bb[3] : bb[1];
}

/* Keep only what the Atari character set can reach (plus composite parts
 * and the '?' fallback): a Speedo font holds ~565 glyphs, the Atari code
 * page uses about 220, so this roughly halves the memory a font takes. */
static void compact(XFont *f)
{
    int n = f->spd.nchars, i;
    s32 sz, before = f->spd.size;
    u8 *keep = (u8 *)heap_calloc((n + 7) / 8);
    if (!keep) return;
    for (i = 0; i < 256; i++) {
        u16 id = xg_atari_to_bcid[i];
        if (id) spd_mark_glyph(&f->spd, spd_index_of_id(&f->spd, id), keep);
    }
    spd_mark_glyph(&f->spd, f->m_qidx, keep);
    sz = spd_compact(&f->spd, f->data, keep);
    heap_free(keep);
    if (!sz) return;
    heap_shrink(f->data, sz);
    f->compact = 1;
#ifdef XG_LOG
    {
        char t[48];
        int q = 0;
        xstrcpy(t, " kept "); q = 6;
        q += xitoa(t + q, sz / 1024); xstrcpy(t + q, " KB of "); q += 7;
        q += xitoa(t + q, before / 1024); xstrcpy(t + q, " KB\r\n");
        XLOG(f->name); XLOG(t);
    }
#else
    (void)before;
#endif
}

XFont *fm_get(int fi)
{
    XFont *f;
    s32 h, n;
    if (fi < 0 || fi >= xg_nfont) return NULL;
    f = &xg_font[fi];
    f->lru = ++xg_tick;
    if (f->loaded) return f;
    if (!f->present) return NULL;
    {   /* use the real file size: the header value may differ */
        s32 real = xos_filesize(f->path);
        if (real > 0) f->fsize = real;
    }
    f->data = (u8 *)alloc_evict(f->fsize, f);
    if (!f->data) { XLOG("out of heap loading "); XLOG(f->path); XLOG("\r\n"); return NULL; }
    XLOG("loading "); XLOG(f->path); XLOG("\r\n");
    h = xos_open(f->path, 0);
    if (h < 0) { XLOG("font open failed: "); XLOG(f->path); XLOG("\r\n"); heap_free(f->data); f->data = NULL; return NULL; }
    n = xos_read(h, f->fsize, f->data);
    xos_close(h);
    if (n <= 0 || !spd_open(&f->spd, f->data, n)) {
#ifdef XG_LOG
        {
            char t[40];
            int q;
            xstrcpy(t, " (read ");
            q = 7 + xitoa(t + 7, n);
            xstrcpy(t + q, " bytes, reason ");
            q += 15;
            q += xitoa(t + q, n > 0 ? f->spd.err : 1);
            t[q++] = ')'; t[q] = 0;
            XLOG("not a usable Speedo font: "); XLOG(f->path); XLOG(t); XLOG("\r\n");
        }
#endif
        heap_free(f->data); f->data = NULL; f->present = 0;
        return NULL;
    }
#ifdef XG_LOG
    {
        char t[48];
        int q = 0;
        static const char hx[] = "0123456789ABCDEF";
        t[q++] = ' '; t[q++] = 'k'; t[q++] = '=';
        t[q++] = hx[(f->spd.k32 >> 12) & 15]; t[q++] = hx[(f->spd.k32 >> 8) & 15];
        t[q++] = hx[(f->spd.k32 >> 4) & 15]; t[q++] = hx[f->spd.k32 & 15];
        t[q++] = '/'; t[q++] = hx[f->spd.k7 >> 4]; t[q++] = hx[f->spd.k7 & 15];
        t[q++] = ' '; q += xitoa(t + q, f->spd.nchars); t[q] = 0;
        XLOG("loaded "); XLOG(f->name); XLOG(t); XLOG(" glyphs\r\n");
    }
#endif
    f->idmap = (u16 *)alloc_evict((s32)f->spd.nchars * 4, f);
    if (f->idmap) spd_build_idmap(&f->spd, f->idmap);
    f->loaded = 1;
    /* real metrics from the glyphs */
    {
        s16 v;
        f->m_top = f->spd.ymax;
        f->m_bot = (s16)-f->spd.ymin;
        if ((v = gtop(f, 'd', 1)) > 0) f->m_asc = v;
        else if ((v = gtop(f, 'H', 1)) > 0) f->m_asc = v;
        if ((v = gtop(f, 'x', 1)) > 0) f->m_half = v;
        if ((v = gtop(f, 'p', 0)) < 0) f->m_desc = (s16)-v;
        f->m_maxw = (s16)(f->spd.xmax - (f->spd.xmin < 0 ? f->spd.xmin : 0));
        f->m_qidx = (s16)spd_index_of_id(&f->spd, '?');
    }
    f->compact = 0;
    if (!f->full) compact(f);
    return f;
}

int fm_find_id(int vdi_id)
{
    int i;
    for (i = 0; i < xg_nfont; i++)
        if (xg_font[i].vdi_id == vdi_id && xg_font[i].present) return i;
    return -1;
}

/* charmap 1: Atari code page; 0: Bitstream character id (BICS) */
int fm_glyph(XFont *f, int code, int charmap)
{
    int idx;
    u16 id;
    if (charmap) {
        id = xg_atari_to_bcid[code & 0xff];
        if (!id) return f->m_qidx;
    } else id = (u16)code;
    idx = spd_index_of_id(&f->spd, id);
    if (idx >= 0 && f->compact && !spd_has_glyph(&f->spd, idx)) {
        /* a glyph outside the Atari set (BICS charmap): load the whole font */
        int fi = (int)(f - xg_font);
        f->full = 1;
        unload(f);
        if (!fm_get(fi)) {          /* no room: the compact copy fits again */
            f->full = 0;
            fm_get(fi);
            return f->m_qidx;
        }
        idx = spd_index_of_id(&f->spd, id);
    }
    return idx < 0 ? f->m_qidx : idx;
}
