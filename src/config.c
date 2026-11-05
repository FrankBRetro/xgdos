/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* config.c - ASSIGN.SYS reader/writer */
#include "config.h"
#include "xstd.h"
#include "xos.h"

const char *const xg_paper_names[PAPER_COUNT] = { "A4", "A5", "LETTER", "LEGAL" };
const char *const xg_mode_names[MODE_COUNT] = { "FULL", "NOGDOS", "NOFSMC", "BYPASS" };

static const char *const xkeys[] = {
    "FONTPATH", "PDFPATH", "PDFNAME", "PDFDPI", "PAPER", "CACHE", "HEAP",
    "STACKS", "HINTING", "PRINTER", "MODE", "FSMC", NULL
};

void cfg_defaults(XgConfig *c, int bootdrv)
{
    xmemset(c, 0, sizeof *c);
    c->path[0] = (char)('A' + bootdrv);
    xstrcpy(c->path + 1, ":\\GEMSYS");
    c->pdfpath[0] = (char)('A' + bootdrv);
    xstrcpy(c->pdfpath + 1, ":\\");
    xstrcpy(c->pdfname, "PRN");
    c->pdfdpi = 600;
    c->paper = PAPER_A4;
    c->cache_kb = 48;
    c->heap_kb = 192;
    c->stacks = 0;
    c->hint = 1;
    c->printer = PRN_PDF;
}

static int is_space(char ch) { return ch == ' ' || ch == '\t'; }

static void trim(char *s)
{
    s32 n = xstrlen(s);
    while (n && (is_space(s[n - 1]) || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static int key_index(const char *line, char *val, s32 vmax)
{
    int i;
    for (i = 0; xkeys[i]; i++) {
        s32 kl = xstrlen(xkeys[i]);
        const char *p = line;
        if (xstrnicmp(p, xkeys[i], kl)) continue;
        p += kl;
        while (is_space(*p)) p++;
        if (*p != '=') continue;
        p++;
        while (is_space(*p)) p++;
        xstrncpy0(val, p, vmax);
        trim(val);
        return i;
    }
    return -1;
}

static int is_path_key(const char *line, char *val, s32 vmax)
{
    const char *p = line;
    if (xstrnicmp(p, "PATH", 4)) return 0;
    p += 4;
    while (is_space(*p)) p++;
    if (*p != '=') return 0;
    p++;
    while (is_space(*p)) p++;
    xstrncpy0(val, p, vmax);
    trim(val);
    return 1;
}

static void strip_slash(char *p)
{
    s32 n = xstrlen(p);
    if (n > 3 && p[n - 1] == '\\') p[n - 1] = 0;
}

static void apply_key(XgConfig *c, int k, const char *v)
{
    s32 n;
    switch (k) {
    case 0: {                   /* FONTPATH: one or more folders, ; separated */
        char one[XG_PATHLEN];
        while (*v) {
            s32 k = 0;
            while (*v && *v != ';' && k < XG_PATHLEN - 1) one[k++] = *v++;
            one[k] = 0;
            if (*v == ';') v++;
            trim(one);
            { char *t = one; while (is_space(*t)) t++; if (*t) cfg_add_fontdir(c, t); }
        }
        break;
    }
    case 1: xstrncpy0(c->pdfpath, v, XG_PATHLEN); break;
    case 2: xstrncpy0(c->pdfname, v, 5); break;
    case 3: n = xatoi(v); if (n >= 72 && n <= 2400) c->pdfdpi = (s16)n; break;
    case 4:
        for (n = 0; n < PAPER_COUNT; n++)
            if (!xstricmp(v, xg_paper_names[n])) c->paper = (s16)n;
        break;
    case 5: n = xatoi(v); if (n >= 8 && n <= 4096) c->cache_kb = (s16)n; break;
    case 6: n = xatoi(v); if (n >= 128 && n <= 16384) c->heap_kb = (s16)n; break;
    case 7: n = xatoi(v); if (n >= 0 && n <= 16) c->stacks = (s16)n; break;
    case 8:
        c->hint = (s16)(!xstricmp(v, "FULL") ? 2 :
                        (!xstricmp(v, "ON") || !xstricmp(v, "YES") || !xstricmp(v, "1")) ? 1 : 0);
        break;
    case 9: c->printer = (s16)(!xstricmp(v, "DRIVER") ? PRN_DRIVER : PRN_PDF); break;
    case 11: c->fsmc = (s16)(!xstricmp(v, "ON") || !xstricmp(v, "YES") || !xstricmp(v, "1")); break;
    case 10:
        for (n = 0; n < MODE_COUNT; n++)
            if (!xstricmp(v, xg_mode_names[n])) c->mode = (s16)n;
        break;
    }
}

/* "21 STYLUS.SYS", "1P SCREEN.SYS", "01p screen.sys" */
static int parse_device(const char *s, XgDev *d)
{
    s32 id = 0;
    int nd = 0;
    char letter = 0;
    while (*s >= '0' && *s <= '9') { id = id * 10 + (*s++ - '0'); nd++; }
    if (!nd || id <= 0 || id > 99) return 0;
    if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) letter = (char)xtoupper(*s++);
    if (!is_space(*s)) return 0;
    while (is_space(*s)) s++;
    if (!*s) return 0;
    d->id = (s16)id;
    d->letter = letter;
    xstrncpy0(d->drv, s, sizeof d->drv);
    trim(d->drv);
    {
        char *sp = d->drv;
        while (*sp && !is_space(*sp) && *sp != ';') sp++;
        *sp = 0;
    }
    d->fnt_first = 0;
    d->fnt_count = 0;
    return 1;
}

static void parse_spd(XgConfig *c, const char *s, int enabled)
{
    XgSpd *e;
    char *sp;
    if (c->nspd >= XG_MAXSPD) return;
    while (is_space(*s)) s++;
    if (!*s) return;
    e = &c->spd[c->nspd];
    xstrncpy0(e->file, s, XG_PATHLEN);
    trim(e->file);
    e->vdi_id = 0;
    for (sp = e->file; *sp && !is_space(*sp); sp++) ;
    if (*sp) {
        *sp++ = 0;
        while (is_space(*sp)) sp++;
        if (*sp >= '0' && *sp <= '9') e->vdi_id = (s16)xatoi(sp);
    }
    e->enabled = (s16)enabled;
    c->nspd++;
    c->have_spd_lines = 1;
}

void cfg_parse(XgConfig *c, const char *text, s32 len)
{
    char line[200], val[XG_PATHLEN];
    s32 pos = 0;
    XgDev *cur = NULL;
    int k;
    while (pos < len) {
        s32 n = 0;
        char *s;
        while (pos < len && text[pos] != '\n' && text[pos] != '\r') {
            if (n < (s32)sizeof line - 1) line[n++] = text[pos];
            pos++;
        }
        while (pos < len && (text[pos] == '\n' || text[pos] == '\r')) pos++;
        line[n] = 0;
        s = line;
        while (is_space(*s)) s++;
        trim(s);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '+' || *s == '-') {
            const char *dot = xstrrchr(s, '.');
            if (dot && !xstrnicmp(dot, ".SPD", 4)) { parse_spd(c, s + 1, *s == '+'); continue; }
        }
        if (is_path_key(s, val, XG_PATHLEN)) { xstrcpy(c->path, val); strip_slash(c->path); continue; }
        if ((k = key_index(s, val, XG_PATHLEN)) >= 0) { apply_key(c, k, val); continue; }
        if (*s >= '0' && *s <= '9') {
            if (c->ndev < XG_MAXDEV && parse_device(s, &c->dev[c->ndev])) {
                cur = &c->dev[c->ndev++];
                cur->fnt_first = c->nfnt;
                continue;
            }
        }
        /* anything else after a device line is a bitmap font file name */
        if (cur && c->nfnt < XG_MAXFNT) {
            const char *dot = xstrrchr(s, '.');
            if (dot && !xstrnicmp(dot, ".SPD", 4)) { parse_spd(c, s, 1); continue; }
            xstrncpy0(c->fnt[c->nfnt], s, 14);
            {
                char *sp = c->fnt[c->nfnt];
                while (*sp && !is_space(*sp) && *sp != ';') sp++;
                *sp = 0;
            }
            c->nfnt++;
            cur->fnt_count++;
        }
    }
    if (!c->nfontdir) cfg_add_fontdir(c, c->path);
}

int cfg_find_fontdir(const XgConfig *c, const char *dir)
{
    char d[XG_PATHLEN];
    int i;
    xstrncpy0(d, dir, XG_PATHLEN);
    strip_slash(d);
    for (i = 0; i < c->nfontdir; i++) if (!xstricmp(c->fontdir[i], d)) return i;
    return -1;
}

int cfg_add_fontdir(XgConfig *c, const char *dir)
{
    if (!dir[0] || c->nfontdir >= XG_MAXFDIR || cfg_find_fontdir(c, dir) >= 0) return 0;
    xstrncpy0(c->fontdir[c->nfontdir], dir, XG_PATHLEN);
    strip_slash(c->fontdir[c->nfontdir]);
    c->nfontdir++;
    return 1;
}

void cfg_remove_fontdir(XgConfig *c, int i)
{
    if (i < 0 || i >= c->nfontdir) return;
    for (; i < c->nfontdir - 1; i++) xstrcpy(c->fontdir[i], c->fontdir[i + 1]);
    c->nfontdir--;
    c->fontdir[c->nfontdir][0] = 0;
}

const XgDev *cfg_device(const XgConfig *c, int id)
{
    int i;
    for (i = 0; i < c->ndev; i++) if (c->dev[i].id == id) return &c->dev[i];
    return NULL;
}

void cfg_spd_fullpath(const XgConfig *c, const XgSpd *s, char *out)
{
    int i;
    if (xstrchr(s->file, ':') || s->file[0] == '\\') { xstrncpy0(out, s->file, XG_PATHLEN); return; }
    for (i = 0; i < c->nfontdir; i++) {
        xpath_join(out, c->fontdir[i], s->file);
        if (c->nfontdir == 1 || xos_filesize(out) >= 0) return;
    }
    xpath_join(out, c->nfontdir ? c->fontdir[0] : c->path, s->file);
}

void cfg_paper_size(int paper, s16 *w, s16 *h)
{
    switch (paper) {
    case PAPER_A5:     *w = 1480; *h = 2100; break;
    case PAPER_LETTER: *w = 2159; *h = 2794; break;
    case PAPER_LEGAL:  *w = 2159; *h = 3556; break;
    default:           *w = 2100; *h = 2970; break;
    }
}

/* ------------------------------------------------------------------ */

typedef struct Out { char *p; s32 n, max; int err; } Out;

static void put(Out *o, const char *s)
{
    while (*s) {
        if (o->n >= o->max - 1) { o->err = 1; return; }
        o->p[o->n++] = *s++;
    }
}

static void putkey(Out *o, const char *k, const char *v)
{
    put(o, k); put(o, " = "); put(o, v); put(o, "\r\n");
}

static void putnum(Out *o, const char *k, s32 v)
{
    char b[12];
    b[xitoa(b, v)] = 0;
    putkey(o, k, b);
}

s32 cfg_write(const XgConfig *c, const char *orig, s32 len, char *buf, s32 max)
{
    Out o;
    char line[200], val[XG_PATHLEN];
    s32 pos = 0;
    int i;
    o.p = buf; o.n = 0; o.max = max; o.err = 0;
    /* copy every classic line, drop old XGDOS lines and the marker */
    while (pos < len) {
        s32 n = 0;
        char *s;
        while (pos < len && orig[pos] != '\n' && orig[pos] != '\r') {
            if (n < (s32)sizeof line - 1) line[n++] = orig[pos];
            pos++;
        }
        while (pos < len && (orig[pos] == '\n' || orig[pos] == '\r')) pos++;
        line[n] = 0;
        s = line;
        while (is_space(*s)) s++;
        if (!xstrnicmp(s, "; ---- XGDOS", 12)) continue;
        if (*s == '+' || *s == '-') {
            const char *dot = xstrrchr(s, '.');
            if (dot && !xstrnicmp(dot, ".SPD", 4)) continue;
        }
        if (key_index(s, val, XG_PATHLEN) >= 0) continue;
        {
            const char *dot = xstrrchr(s, '.');
            if (dot && !xstrnicmp(dot, ".SPD", 4) && *s != ';') continue;
        }
        put(&o, line);
        put(&o, "\r\n");
    }
    /* drop trailing blank lines before our block */
    while (o.n >= 4 && o.p[o.n - 1] == '\n' && o.p[o.n - 3] == '\n') o.n -= 2;
    put(&o, "; ---- XGDOS settings (edit with XGDOSCFG.PRG) ----\r\n");
    for (i = 0; i < c->nfontdir; i++) putkey(&o, "FONTPATH", c->fontdir[i]);
    putkey(&o, "PDFPATH", c->pdfpath);
    putkey(&o, "PDFNAME", c->pdfname);
    putnum(&o, "PDFDPI", c->pdfdpi);
    putkey(&o, "PAPER", xg_paper_names[c->paper]);
    putnum(&o, "CACHE", c->cache_kb);
    putnum(&o, "HEAP", c->heap_kb);
    putnum(&o, "STACKS", c->stacks);
    putkey(&o, "HINTING", c->hint == 2 ? "FULL" : c->hint ? "ON" : "OFF");
    putkey(&o, "PRINTER", c->printer == PRN_DRIVER ? "DRIVER" : "PDF");
    if (c->fsmc) putkey(&o, "FSMC", "ON");
    if (c->mode != MODE_FULL) putkey(&o, "MODE", xg_mode_names[c->mode]);
    for (i = 0; i < c->nspd; i++) {
        put(&o, c->spd[i].enabled ? "+" : "-");
        put(&o, c->spd[i].file);
        if (c->spd[i].vdi_id) {
            char b[12];
            b[xitoa(b, c->spd[i].vdi_id)] = 0;
            put(&o, " ");
            put(&o, b);
        }
        put(&o, "\r\n");
    }
    if (o.err) return -1;
    o.p[o.n] = 0;
    return o.n;
}
