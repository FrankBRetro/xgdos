/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xgdoscfg.c - XGDOS preferences (GEM application or desk accessory)
 *
 * Non-modal window: Speedo font list with enable/disable check boxes,
 * Add/Remove, up to four font folders, the PDF folder, printer and
 * rendering settings, Save & Apply. Writes
 * ASSIGN.SYS (keeping all classic GDOS lines, backup in ASSIGN.BAK) and
 * asks a resident XGDOS to reload the font list immediately.
 */
#include "gem.h"
#include "../src/tos.h"
#include "../src/xstd.h"
#include "../src/xos.h"
#include "../src/config.h"
#include "../src/spd.h"
#include "../src/xgapi.h"

#define COLS     60
#define MAXLIST  10
#define MAXTEXT  16384

typedef struct Ent {
    char file[XG_PATHLEN];
    char name[40];
    s16  enabled, vdi_id, missing, isnew;
} Ent;

char app_stack[8192];

static XgConfig cfg;
static Ent ent[XG_MAXSPD];
static int nent, sel = -1, top;
static int listrows = MAXLIST;   /* visible font rows, fitted to the screen */
static char orig[MAXTEXT];
static s32 origlen;
static char outbuf[MAXTEXT + 4096];
static char assign_path[XG_PATHLEN];
static XgApi *api;
static int dirty, is_acc;
static s16 vh, wh = -1, cw, ch, ncol;
static s16 wx, wy, ww, whh;           /* work area */
static char title[64];
static char status[96];

static const s16 dpis[] = { 150, 180, 300, 360, 600, 720, 1200 };
static const s16 caches[] = { 32, 64, 96, 128, 192, 256, 512 };
static const char *const names[] = { "PRN", "PRT", "PDF", "DOC" };

enum { R_FONT0, R_FONT1, R_FONT2, R_FONT3,      /* XG_MAXFDIR font folder rows */
       R_PDFDIR, R_PDFNAME, R_DPI, R_PAPER, R_HINT, R_CACHE, R_PRINTER, NSET };

/* ---------------- model ---------------- */

static void probe_name(Ent *e)
{
    char full[XG_PATHLEN];
    u8 hdr[SPD_HDR_MIN];
    SpdInfo inf;
    XgSpd s;
    s32 h, n;
    xstrncpy0(s.file, e->file, XG_PATHLEN);
    cfg_spd_fullpath(&cfg, &s, full);
    h = Fopen(full, 0);
    e->missing = 1;
    xstrcpy(e->name, "(file not found)");
    if (h < 0) return;
    n = Fread((s16)h, SPD_HDR_MIN, hdr);
    Fclose((s16)h);
    if (n > 360 && spd_probe(hdr, n, &inf)) {
        xstrncpy0(e->name, inf.name[0] ? inf.name : inf.sname, sizeof e->name);
        e->missing = 0;
    } else xstrcpy(e->name, "(not a Speedo font)");
}

static int find_ent(const char *file)
{
    int i;
    for (i = 0; i < nent; i++) if (!xstricmp(xpath_base(ent[i].file), xpath_base(file))) return i;
    return -1;
}

static void add_ent(const char *file, int enabled, int id, int isnew)
{
    Ent *e;
    if (nent >= XG_MAXSPD || find_ent(file) >= 0) return;
    e = &ent[nent++];
    xstrncpy0(e->file, file, XG_PATHLEN);
    e->enabled = (s16)enabled;
    e->vdi_id = (s16)id;
    e->isnew = (s16)isnew;
    probe_name(e);
}

static void scan_dir(void)
{
    XosFind f;
    char pat[XG_PATHLEN];
    int d;
    for (d = 0; d < cfg.nfontdir; d++) {
        xpath_join(pat, cfg.fontdir[d], "*.SPD");
        if (xos_findfirst(&f, pat)) {
            do add_ent(f.name, 1, 0, cfg.have_spd_lines);
            while (xos_findnext(&f));
        }
        xos_findend(&f);
    }
}

/* the font folders changed: look for every font again */
static void rescan(void)
{
    int i = 0;
    while (i < nent) { if (ent[i].isnew) ent[i] = ent[--nent]; else i++; }
    for (i = 0; i < nent; i++) probe_name(&ent[i]);
    scan_dir();
    if (sel >= nent) sel = nent - 1;
    if (top > 0 && top + listrows > nent) top = nent > listrows ? nent - listrows : 0;
}

static void load(void)
{
    s32 h;
    int i;
    cfg_defaults(&cfg, xos_bootdrive());
    origlen = 0;
    h = Fopen(assign_path, 0);
    if (h >= 0) {
        origlen = Fread((s16)h, MAXTEXT - 1, orig);
        Fclose((s16)h);
        if (origlen < 0) origlen = 0;
    }
    orig[origlen] = 0;
    cfg_parse(&cfg, orig, origlen);
    nent = 0;
    for (i = 0; i < cfg.nspd; i++) add_ent(cfg.spd[i].file, cfg.spd[i].enabled, cfg.spd[i].vdi_id, 0);
    scan_dir();
    sel = nent ? 0 : -1;
    top = 0;
    dirty = 0;
}

static void set_status(void)
{
    char b[16];
    if (api) {
        s32 fr = Supexec(api->heap_free);
        xstrcpy(status, "XGDOS ");
        {   /* version word 0xMMmm, e.g. 0x0101 -> "1.01" */
            char v[8];
            int n = xitoa(v, (api->version >> 8) & 0xff);
            v[n++] = '.';
            v[n++] = (char)('0' + ((api->version & 0xff) / 10) % 10);
            v[n++] = (char)('0' + (api->version & 0xff) % 10);
            v[n] = 0;
            xstrcat(status, v);
        }
        xstrcat(status, " resident: ");
        b[xitoa(b, api->nfonts)] = 0; xstrcat(status, b);
        xstrcat(status, " fonts, ");
        b[xitoa(b, fr / 1024)] = 0; xstrcat(status, b);
        xstrcat(status, " KB free");
    } else xstrcpy(status, "XGDOS not resident - changes apply at next boot");
}

static int save(void)
{
    s32 n, h;
    int i;
    char bak[XG_PATHLEN];
    cfg.nspd = 0;
    for (i = 0; i < nent; i++) {
        xstrcpy(cfg.spd[cfg.nspd].file, ent[i].file);
        cfg.spd[cfg.nspd].enabled = ent[i].enabled;
        cfg.spd[cfg.nspd].vdi_id = ent[i].vdi_id;
        cfg.nspd++;
        ent[i].isnew = 0;
    }
    cfg.have_spd_lines = 1;
    n = cfg_write(&cfg, orig, origlen, outbuf, sizeof outbuf);
    if (n < 0) { form_alert(1, "[3][ASSIGN.SYS would be too large.][ OK ]"); return 0; }
    /* backup the old file */
    if (origlen > 0) {
        xstrcpy(bak, assign_path);
        xstrcpy(xstrrchr(bak, '.') ? xstrrchr(bak, '.') : bak + xstrlen(bak), ".BAK");
        h = Fcreate(bak, 0);
        if (h >= 0) { Fwrite((s16)h, origlen, orig); Fclose((s16)h); }
    }
    h = Fcreate(assign_path, 0);
    if (h < 0 || Fwrite((s16)h, n, outbuf) != n) {
        if (h >= 0) Fclose((s16)h);
        form_alert(1, "[3][Could not write ASSIGN.SYS.][ OK ]");
        return 0;
    }
    Fclose((s16)h);
    xmemcpy(orig, outbuf, n);
    origlen = n;
    dirty = 0;
    if (api) Supexec(api->reload);
    set_status();
    return 1;
}

/* ---------------- drawing ---------------- */

static s16 bg(void) { return ncol >= 16 ? 8 : 0; }

static void box(s16 x0, s16 y0, s16 x1, s16 y1, s16 col)
{
    s16 p[4];
    p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
    vsf_interior(vh, 1); vsf_color(vh, col);
    vr_recfl(vh, p);
}

static void frame(s16 x0, s16 y0, s16 x1, s16 y1, s16 tl, s16 br)
{
    s16 p[6];
    vsl_color(vh, tl);
    p[0] = x0; p[1] = y1; p[2] = x0; p[3] = y0; p[4] = x1; p[5] = y0; v_pline(vh, 3, p);
    vsl_color(vh, br);
    p[0] = x1; p[1] = y0; p[2] = x1; p[3] = y1; p[4] = x0; p[5] = y1; v_pline(vh, 3, p);
}

static void text(int col, int row, const char *s)
{
    v_gtext(vh, (s16)(wx + cw / 2 + col * cw), (s16)(wy + row * ch + ch / 4), s);
}

static int row_list0(void) { return 1; }
static int row_set0(void) { return 2 + listrows; }
static int row_buttons(void) { return 3 + listrows + NSET; }
static int row_status(void) { return 5 + listrows + NSET; }

static void setting_line(int r, char *b)
{
    char t[16];
    static const char *const lbl[NSET] = {
        "Font folder  ", "Font folder  ", "Font folder  ", "Font folder  ",
        "PDF folder   ", "PDF names    ", "Resolution   ",
        "Paper        ", "Hinting      ", "Glyph cache  ", "Printing     " };
    if (r <= R_FONT3) {
        int i = r - R_FONT0;
        b[0] = 0;
        if (i < cfg.nfontdir) {
            xstrcpy(b, lbl[r]);
            xstrcat(b, cfg.fontdir[i]);
        } else if (i == cfg.nfontdir) {
            xstrcpy(b, lbl[r]);
            xstrcat(b, "(click to add another folder)");
        }
        b[COLS - 2] = 0;
        return;
    }
    xstrcpy(b, lbl[r]);
    switch (r) {
    case R_PDFDIR: xstrcat(b, cfg.pdfpath[0] ? cfg.pdfpath : "(root of the boot drive)"); break;
    case R_PDFNAME: xstrcat(b, cfg.pdfname); xstrcat(b, "0001.PDF, "); xstrcat(b, cfg.pdfname); xstrcat(b, "0002.PDF ..."); break;
    case R_DPI: t[xitoa(t, cfg.pdfdpi)] = 0; xstrcat(b, t); xstrcat(b, " dpi"); break;
    case R_PAPER: xstrcat(b, xg_paper_names[cfg.paper]); break;
    case R_HINT: xstrcat(b, cfg.hint == 2 ? "Full (fit every stem)" : cfg.hint ? "On (even weight)" : "Off"); break;
    case R_CACHE: t[xitoa(t, cfg.cache_kb)] = 0; xstrcat(b, t); xstrcat(b, " KB"); break;
    case R_PRINTER: xstrcat(b, cfg.printer == PRN_PDF ? "Numbered PDF files" : "GDOS printer drivers"); break;
    }
    b[COLS - 2] = 0;
}

static void button(int col, int w, const char *s, int enabled)
{
    s16 x0 = (s16)(wx + cw / 2 + col * cw - 2), y0 = (s16)(wy + row_buttons() * ch);
    s16 x1 = (s16)(x0 + w * cw + 4), y1 = (s16)(y0 + ch + ch / 2);
    box(x0, y0, x1, y1, ncol >= 16 ? 8 : 0);
    if (ncol >= 16) { frame(x0, y0, x1, y1, 0, 9); frame((s16)(x0 - 1), (s16)(y0 - 1), (s16)(x1 + 1), (s16)(y1 + 1), 1, 1); }
    else frame(x0, y0, x1, y1, 1, 1);
    vst_color(vh, enabled ? 1 : 9);
    v_gtext(vh, (s16)(x0 + 2 + (w - (int)xstrlen(s)) * cw / 2), (s16)(y0 + ch / 4), s);
    vst_color(vh, 1);
}

static void draw_all(void)
{
    char b[COLS + 80];
    int i, r;
    box(wx, wy, (s16)(wx + ww - 1), (s16)(wy + whh - 1), bg());
    vswr_mode(vh, 2);
    vst_alignment(vh, 0, 5);
    vst_color(vh, 1);
    text(0, 0, "Speedo fonts (click the box to enable or disable)");
    text(0, row_set0() - 1, "Settings (click one to change it)");
    /* list */
    {
        s16 x0 = (s16)(wx + cw / 2 - 2), y0 = (s16)(wy + row_list0() * ch - 2);
        s16 x1 = (s16)(wx + cw / 2 + (COLS - 1) * cw + 2), y1 = (s16)(wy + (row_list0() + listrows) * ch + 2);
        box(x0, y0, x1, y1, 0);
        frame(x0, y0, x1, y1, 1, ncol >= 16 ? 0 : 1);
        for (i = 0; i < listrows; i++) {
            int k = top + i;
            if (k >= nent) break;
            r = row_list0() + i;
            if (k == sel) box((s16)(x0 + 1), (s16)(wy + r * ch), (s16)(x1 - 1), (s16)(wy + r * ch + ch - 1), ncol >= 16 ? 9 : 0);
            xstrcpy(b, ent[k].enabled ? "[\x08] " : "[ ] ");
            xstrncpy0(b + 4, ent[k].name, 34);
            while (xstrlen(b) < 40) xstrcat(b, " ");
            xstrncpy0(b + 40, xpath_base(ent[k].file), 14);
            if (ent[k].isnew) xstrcat(b, " new");
            vst_color(vh, ent[k].missing ? 2 : 1);
            if (k == sel && ncol < 16) vswr_mode(vh, 3);
            text(0, r, b);
            vswr_mode(vh, 2);
        }
        vst_color(vh, 1);
        if (top > 0) text(COLS - 3, row_list0(), "\x01");
        if (top + listrows < nent) text(COLS - 3, row_list0() + listrows - 1, "\x02");
    }
    /* settings */
    for (i = 0; i < NSET; i++) { setting_line(i, b); text(0, row_set0() + i, b); }
    button(0, 13, "Add font...", 1);
    button(15, 10, "Remove", sel >= 0);
    button(27, 13, "Enable all", nent > 0);
    button(42, 15, dirty ? "Save & Apply" : "Saved", dirty);
    text(0, row_status(), status);
}

static void redraw(s16 x, s16 y, s16 w, s16 h)
{
    s16 rx, ry, rw, rh, p[4];
    wind_update(1);
    graf_mouse(256, NULL);
    wind_get(wh, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
    while (rw && rh) {
        s16 x0 = XG_MAX(rx, x), y0 = XG_MAX(ry, y);
        s16 x1 = XG_MIN(rx + rw, x + w), y1 = XG_MIN(ry + rh, y + h);
        if (x1 > x0 && y1 > y0) {
            p[0] = x0; p[1] = y0; p[2] = (s16)(x1 - 1); p[3] = (s16)(y1 - 1);
            vs_clip(vh, 1, p);
            draw_all();
        }
        wind_get(wh, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }
    vs_clip(vh, 0, NULL);
    graf_mouse(257, NULL);
    wind_update(0);
}

static void update_title(void)
{
    xstrcpy(title, dirty ? " XGDOS Settings * " : " XGDOS Settings ");
    wind_set_str(wh, WF_NAME, title);
}

static void refresh(void)
{
    update_title();
    redraw(wx, wy, ww, whh);
}

/* ---------------- actions ---------------- */

static int choose_folder(char *dst, const char *title_s)
{
    char path[XG_PATHLEN + 16], file[16];
    s16 btn = 0;
    char *s;
    if (dst[0]) xstrcpy(path, dst);
    else { path[0] = (char)('A' + xos_bootdrive()); xstrcpy(path + 1, ":\\"); }
    if (path[xstrlen(path) - 1] != '\\') xstrcat(path, "\\");
    xstrcat(path, "*.*");
    xstrcpy(file, "");
    wind_update(1);
    fsel_exinput(path, file, &btn, title_s);
    wind_update(0);
    if (!btn) return 0;
    s = xstrrchr(path, '\\');
    if (s) *s = 0;
    if (xstrlen(path) == 2) xstrcat(path, "\\");
    xstrncpy0(dst, path, XG_PATHLEN);
    return 1;
}

static void add_font(void)
{
    char path[XG_PATHLEN + 16], file[16], full[XG_PATHLEN + 16];
    s16 btn = 0;
    char *s;
    xstrcpy(path, cfg.fontdir[0]);
    xstrcat(path, "\\*.SPD");
    file[0] = 0;
    wind_update(1);
    fsel_exinput(path, file, &btn, "Add Speedo font");
    wind_update(0);
    if (!btn || !file[0]) return;
    s = xstrrchr(path, '\\');
    if (s) *s = 0;
    if (cfg_find_fontdir(&cfg, path) >= 0) xstrcpy(full, file);   /* found by name */
    else xpath_join(full, path, file);
    if (find_ent(full) >= 0) { form_alert(1, "[1][This font is already in the list.][ OK ]"); return; }
    add_ent(full, 1, 0, 1);
    if (ent[nent - 1].missing) {
        nent--;
        form_alert(1, "[3][That file is not a Speedo font.][ OK ]");
        return;
    }
    sel = nent - 1;
    if (sel >= top + listrows) top = sel - listrows + 1;
    dirty = 1;
}

/* click on font folder row i: change or remove it, or add a new one */
static void font_folder(int i)
{
    char d[XG_PATHLEN];
    if (i > cfg.nfontdir || i >= XG_MAXFDIR) return;
    if (i == cfg.nfontdir) {                               /* the "add" row */
        d[0] = 0;
        if (!choose_folder(d, "Add a folder with Speedo fonts")) return;
        if (cfg_find_fontdir(&cfg, d) >= 0) { form_alert(1, "[1][That folder is already in the list.][ OK ]"); return; }
        cfg_add_fontdir(&cfg, d);
    } else {
        int a = cfg.nfontdir > 1
              ? form_alert(1, "[2][Change this font folder,|or remove it from the list?][Change...|Remove|Cancel]")
              : form_alert(1, "[2][Change the font folder?][Change...|Cancel]");
        if ((cfg.nfontdir > 1 && a == 3) || (cfg.nfontdir == 1 && a == 2)) return;
        if (a == 2) cfg_remove_fontdir(&cfg, i);
        else {
            xstrcpy(d, cfg.fontdir[i]);
            if (!choose_folder(d, "Folder with Speedo fonts")) return;
            if (cfg_find_fontdir(&cfg, d) >= 0 && cfg_find_fontdir(&cfg, d) != i) {
                form_alert(1, "[1][That folder is already in the list.][ OK ]");
                return;
            }
            xstrncpy0(cfg.fontdir[i], d, XG_PATHLEN);
        }
    }
    rescan();
    dirty = 1;
}

static int next_in(const s16 *tab, int n, int cur)
{
    int i;
    for (i = 0; i < n; i++) if (tab[i] > cur) return tab[i];
    return tab[0];
}

static void click(s16 mx, s16 my, s16 nclicks)
{
    int r = (my - wy) / ch, c = (mx - wx - cw / 2) / cw;
    if (r >= row_list0() && r < row_list0() + listrows) {
        int k = top + r - row_list0();
        if (c >= COLS - 4) {                     /* scroll arrows */
            if (r == row_list0() && top > 0) top--;
            else if (r == row_list0() + listrows - 1 && top + listrows < nent) top++;
        } else if (k < nent) {
            if (c <= 3 || nclicks > 1) { ent[k].enabled = (s16)!ent[k].enabled; dirty = 1; }
            sel = k;
        }
    } else if (r >= row_set0() && r < row_set0() + NSET) {
        switch (r - row_set0()) {
        case R_FONT0: case R_FONT1: case R_FONT2: case R_FONT3:
            font_folder(r - row_set0() - R_FONT0);
            break;
        case R_PDFDIR: if (choose_folder(cfg.pdfpath, "Folder for printed PDF files")) dirty = 1; break;
        case R_PDFNAME: { int i, k = 0; for (i = 0; i < 4; i++) if (!xstricmp(cfg.pdfname, names[i])) k = i + 1; xstrcpy(cfg.pdfname, names[k & 3]); dirty = 1; break; }
        case R_DPI: cfg.pdfdpi = (s16)next_in(dpis, 7, cfg.pdfdpi); dirty = 1; break;
        case R_PAPER: cfg.paper = (s16)((cfg.paper + 1) % PAPER_COUNT); dirty = 1; break;
        case R_HINT: cfg.hint = (s16)((cfg.hint + 1) % 3); dirty = 1; break;
        case R_CACHE: cfg.cache_kb = (s16)next_in(caches, 7, cfg.cache_kb); dirty = 1; break;
        case R_PRINTER: cfg.printer = (s16)(cfg.printer == PRN_PDF ? PRN_DRIVER : PRN_PDF); dirty = 1; break;
        }
    } else if (r >= row_buttons() && r <= row_buttons() + 1) {
        if (c < 14) add_font();
        else if (c >= 15 && c < 26 && sel >= 0) {
            int i;
            for (i = sel; i < nent - 1; i++) ent[i] = ent[i + 1];
            nent--;
            if (sel >= nent) sel = nent - 1;
            if (top > 0 && top + listrows > nent) top--;
            dirty = 1;
        } else if (c >= 27 && c < 41) {
            int i;
            for (i = 0; i < nent; i++) if (!ent[i].missing) ent[i].enabled = 1;
            dirty = 1;
        } else if (c >= 42 && dirty) save();
    }
    refresh();
}

static int ask_close(void)
{
    if (!dirty) return 1;
    switch (form_alert(1, "[2][Save the changed settings|to ASSIGN.SYS?][Save|Discard|Cancel]")) {
    case 1: return save();
    case 2: load(); return 1;
    default: return 0;
    }
}

static void open_window(void)
{
    s16 kind = NAME | CLOSER | MOVER, x, y, w, h, dx, dy, dw, dh;
    if (wh >= 0) { wind_set(wh, WF_TOP, wh, 0, 0, 0); return; }
    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    /* fit the font list to the screen (medium resolution has 200 lines) */
    {
        s16 fx, fy, fw, fh;
        int rows;
        wind_calc(1, kind, dx, dy, dw, dh, &fx, &fy, &fw, &fh);      /* work area of a full window */
        rows = (fh - ch / 2) / ch - (row_status() + 1 - listrows);
        listrows = rows < 2 ? 2 : rows > MAXLIST ? MAXLIST : rows;
    }
    wind_calc(0, kind, 0, 0, (s16)(COLS * cw), (s16)((row_status() + 1) * ch + ch / 2), &x, &y, &w, &h);
    if (w > dw) w = dw;
    if (h > dh) h = dh;
    x = (s16)(dx + (dw - w) / 2); y = (s16)(dy + (dh - h) / 2);
    wh = wind_create(kind, x, y, w, h);
    if (wh < 0) { form_alert(1, "[3][No more windows available.][ OK ]"); return; }
    load();
    set_status();
    update_title();
    wind_open(wh, x, y, w, h);
    wind_get(wh, WF_WORKXYWH, &wx, &wy, &ww, &whh);
}

static void close_window(void)
{
    if (wh < 0) return;
    wind_close(wh);
    wind_delete(wh);
    wh = -1;
}

void app_main(BASEPAGE *bp, s32 acc)
{
    s16 msg[8], mx, my, mb, ks, key, nc, ev, wi[11], wo[57], bw, bh, i;
    s32 v;
    is_acc = (int)acc;
    if (!is_acc) Mshrink(bp, 256 + bp->p_tlen + bp->p_dlen + bp->p_blen);
    if (appl_init() < 0) return;
    xg_mint = cookie_get(0x4D694E54L, NULL);
    api = cookie_get(XGDS_COOKIE, (u32 *)&v) ? (XgApi *)(unsigned long)v : NULL;
    if (api && api->magic != XGDS_COOKIE) api = NULL;
    if (api && api->assign_path[0]) xstrcpy(assign_path, api->assign_path);
    else { assign_path[0] = (char)('A' + xos_bootdrive()); xstrcpy(assign_path + 1, ":\\ASSIGN.SYS"); }

    vh = graf_handle(&cw, &ch, &bw, &bh);
    for (i = 0; i < 10; i++) wi[i] = 1;
    wi[10] = 2;
    v_opnvwk(wi, &vh, wo);
    ncol = wo[13];
    if (vh <= 0) { appl_exit(); return; }
    {   /* same system font size as the AES: 8x16 (ch >= 16) or 8x8 */
        s16 a, b, c2, d2;
        vst_height(vh, (s16)(ch >= 16 ? 13 : ch >= 8 ? 6 : 4), &a, &b, &c2, &d2);
        if (c2 > 0) cw = c2;
        if (d2 > 0) ch = d2;
    }

    if (is_acc) menu_register(gl_apid, "  XGDOS Settings");
    else { graf_mouse(0, NULL); open_window(); }

    for (;;) {
        ev = evnt_multi(MU_MESAG | MU_BUTTON | MU_KEYBD, 2, 1, 1, msg, 0, &mx, &my, &mb, &ks, &key, &nc);
        if (ev & MU_MESAG) {
            switch (msg[0]) {
            case WM_REDRAW: if (msg[3] == wh) redraw(msg[4], msg[5], msg[6], msg[7]); break;
            case WM_TOPPED: if (msg[3] == wh) wind_set(wh, WF_TOP, wh, 0, 0, 0); break;
            case WM_MOVED:
                if (msg[3] == wh) {
                    wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                    wind_get(wh, WF_WORKXYWH, &wx, &wy, &ww, &whh);
                }
                break;
            case WM_CLOSED:
                if (msg[3] == wh && ask_close()) {
                    close_window();
                    if (!is_acc) goto quit;
                }
                break;
            case AC_OPEN: open_window(); break;
            case AC_CLOSE: wh = -1; break;
            case AP_TERM: if (!is_acc) goto quit; break;
            }
        }
        if ((ev & MU_BUTTON) && wh >= 0 && mx >= wx && my >= wy && mx < wx + ww && my < wy + whh) {
            s16 top_w;
            wind_get(0, WF_TOP, &top_w, NULL, NULL, NULL);
            if (top_w == wh) click(mx, my, nc);
        }
        if ((ev & MU_KEYBD) && wh >= 0) {
            int k = key & 0xff, sc = (key >> 8) & 0xff;
            if (sc == 0x48 && sel > 0) { sel--; if (sel < top) top = sel; refresh(); }
            else if (sc == 0x50 && sel < nent - 1) { sel++; if (sel >= top + listrows) top++; refresh(); }
            else if (k == ' ' && sel >= 0) { ent[sel].enabled = (s16)!ent[sel].enabled; dirty = 1; refresh(); }
            else if (k == 0x13) { if (save()) refresh(); }                       /* ^S */
            else if (k == 0x11 || k == 0x1b || k == 0x15) {                       /* ^Q, Esc, ^U */
                if (ask_close()) { close_window(); if (!is_acc) goto quit; }
            }
        }
    }
quit:
    v_clsvwk(vh);
    appl_exit();
}
