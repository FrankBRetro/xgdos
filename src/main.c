/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* main.c - XGDOS.PRG: install the resident GDOS (AUTO folder program)
 *
 * Memory layout kept by Ptermres (all inside our own TPA, so it can never
 * be freed by a client process exiting):
 *   text | data | bss | stack slots | scratch | heap
 */
#include "vdi.h"
#include "tos.h"
#include "heap.h"
#include "xstd.h"
#include "xos.h"
#include "xgapi.h"

#define SLOT_STACK   8192
#define SCRATCH      (24 * 1024)
#define MAXSLOTS     16

extern void *xg_old_trap2;
void xg_trap2(void);
s32 xg_vq_gdos(void);
extern u32 xg_old_resvec, xg_old_resvalid;
void xg_resvec(void);

char xg_ustack[4096];
u8   xg_slot_busy[MAXSLOTS];
s16  xg_nslots;
void *xg_slot_top[MAXSLOTS];

static XgApi api;
static struct { s32 type; s16 version; s16 quality; } fsmc = { 0x5F535044L, 0x0500, 0 };  /* '_SPD' */
static s32 jar_space[2 * 48];
static char assign_path[128];
static int bootdrv;

extern u32 xg_prun;
extern u8 xg_bypass, xg_hide_gdos;

/* TOS keeps a pointer to the current process basepage; its address is in
 * the OS header from TOS 1.02 on (fixed 0x602c in TOS 1.0) */
static s32 find_prun_s(void)
{
    u32 hdr = *(u32 *)0x4f2L;
    u16 ver;
    hdr = *(u32 *)(unsigned long)(hdr + 8);          /* os_beg: the ROM header */
    ver = *(u16 *)(unsigned long)(hdr + 2);
    return ver >= 0x0102 ? (s32)*(u32 *)(unsigned long)(hdr + 0x28) : 0x602cL;
}

static void say(const char *s) { Cconws(s); }
static void sayn(s32 v) { char b[12]; b[xitoa(b, v)] = 0; Cconws(b); }

/* ---------------- cookie jar ---------------- */

static u32 ck_id, ck_val;
static s32 add_cookie_s(void)
{
    s32 *jar = *(s32 **)0x5a0L;
    int n = 0, max = 0, i;
    if (jar) { while (jar[2 * n]) n++; max = jar[2 * n + 1]; }
    if (!jar || n + 2 > max) {
        s32 *nj = jar_space;
        int nmax = (int)(sizeof jar_space / 8);
        if (n + 2 > nmax) return 0;
        for (i = 0; i < n; i++) { nj[2 * i] = jar[2 * i]; nj[2 * i + 1] = jar[2 * i + 1]; }
        if (!jar) {        /* old TOS: remove our jar again on a warm reset */
            xg_old_resvalid = *(u32 *)0x426L;
            xg_old_resvec = *(u32 *)0x42aL;
            *(u32 *)0x42aL = (u32)(unsigned long)xg_resvec;
            *(u32 *)0x426L = 0x31415926L;
        }
        nj[2 * n] = 0; nj[2 * n + 1] = nmax;
        *(s32 **)0x5a0L = nj;
        jar = nj; max = nmax;
    }
    jar[2 * n] = (s32)ck_id;
    jar[2 * n + 1] = (s32)ck_val;
    jar[2 * n + 2] = 0;
    jar[2 * n + 3] = max;
    return 1;
}

static void add_cookie(u32 id, u32 val) { ck_id = id; ck_val = val; Supexec(add_cookie_s); }

/* ---------------- configuration ---------------- */

static s32 read_assign(char *buf, s32 max)
{
    static const char *const names[] = { "X:\\ASSIGN.SYS", "X:\\GEMSYS\\ASSIGN.SYS", "C:\\ASSIGN.SYS", NULL };
    int i;
    for (i = 0; names[i]; i++) {
        s32 h, n;
        xstrcpy(assign_path, names[i]);
        if (assign_path[0] == 'X') assign_path[0] = (char)('A' + bootdrv);
        h = Fopen(assign_path, 0);
        if (h < 0) continue;
        n = Fread((s16)h, max - 1, buf);
        Fclose((s16)h);
        if (n >= 0) { buf[n] = 0; return n; }
    }
    assign_path[0] = 0;
    return -1;
}

static s32 api_reload(void)
{
    XgConfig *c;
    char *buf;
    s32 h, n = 0;
    xg_lock();
    c = (XgConfig *)heap_alloc(sizeof(XgConfig));
    buf = (char *)heap_alloc(16384);
    if (c && buf && assign_path[0]) {
        h = Fopen(assign_path, 0);
        if (h >= 0) { n = Fread((s16)h, 16383, buf); Fclose((s16)h); }
        if (n > 0) {
            cfg_defaults(c, bootdrv);
            cfg_parse(c, buf, n);
            c->heap_kb = xg_cfg.heap_kb;      /* memory layout is fixed now */
            c->stacks = xg_cfg.stacks;
            xmemcpy(&xg_cfg, c, sizeof(XgConfig));
            fm_reload();
            fm_flush_unused();
            api.nfonts = (s16)xg_nfont;
        }
    }
    heap_free(c);
    heap_free(buf);
    xg_unlock();
    return xg_nfont;
}

static s32 api_heapfree(void) { s32 v; xg_lock(); v = heap_free_total(); xg_unlock(); return v; }

/* ---------------- main ---------------- */

void xg_main(BASEPAGE *bp)
{
    u8 *base, *top;
    s32 n, avail, heap, need, i, nslots;
    int enabled = 0;
    u32 v;

    say("\r\n\033p XGDOS " XGDOS_VSTRING " \033q  re-entrant Speedo GDOS\r\n");
    xg_mint = cookie_get(0x4D694E54L, &v);           /* 'MiNT' */
    XLOG("---- XGDOS " XGDOS_VSTRING " starting");
    XLOG(xg_mint ? " (MiNT present)\r\n" : " (no MiNT)\r\n");
    if (cookie_get(XGDS_COOKIE, NULL)) {
        XLOG("not installed: XGDOS is already resident\r\n");
        say("XGDOS is already installed.\r\n"); Pterm(0);
    }
    if (xg_vq_gdos() != -2) {
        XLOG("not installed: vq_gdos says another GDOS is present\r\n");
        say("Another GDOS is already installed - not loaded.\r\n"); Pterm(1);
    }
    bootdrv = xos_bootdrive();

    base = (u8 *)(((unsigned long)bp->p_bbase + bp->p_blen + 15) & ~15UL);
    top = (u8 *)bp->p_hitpa;
    avail = (s32)(top - base);

    /* read ASSIGN.SYS into the (still unused) free TPA */
    cfg_defaults(&xg_cfg, bootdrv);
    n = read_assign((char *)base, avail > 32768 ? 32768 : avail);
    if (n > 0) { cfg_parse(&xg_cfg, (char *)base, n); XLOG("read "); XLOG(assign_path); XLOG("\r\n"); }
    else { say("No ASSIGN.SYS found - using defaults.\r\n"); XLOG("no ASSIGN.SYS found\r\n"); }

    nslots = xg_cfg.stacks ? xg_cfg.stacks : (xg_mint ? 4 : 2);
    if (nslots > MAXSLOTS) nslots = MAXSLOTS;
    heap = (s32)xg_cfg.heap_kb * 1024;
    need = nslots * SLOT_STACK + SCRATCH + heap;
    if (need > avail - 32768) {
        heap = avail - 32768 - nslots * SLOT_STACK - SCRATCH;
        if (heap < 96 * 1024) { XLOG("not installed: not enough memory\r\n"); say("Not enough memory for XGDOS.\r\n"); Pterm(1); }
        say("HEAP reduced to "); sayn(heap / 1024); say(" KB.\r\n");
    }
    for (i = 0; i < nslots; i++) {
        base += SLOT_STACK;
        xg_slot_top[i] = base;
        xg_slot_busy[i] = 0;
    }
    xg_scratch = base;
    xg_scratch_size = SCRATCH;
    base += SCRATCH;
    heap_init(base, heap);
    base += heap;

    if (xg_cfg.cache_kb * 1024L > heap / 2) xg_cfg.cache_kb = (s16)(heap / 2048);
    fm_init();
    fnt_load_all();
    for (i = 0; i < xg_nfont; i++) if (xg_font[i].enabled) enabled++;

    api.magic = XGDS_COOKIE;
    api.version = XGDOS_VERSION;
    api.nfonts = (s16)xg_nfont;
    api.reload = api_reload;
    api.cfg = &xg_cfg;
    api.fonts = xg_font;
    api.heap_size = heap;
    api.heap_free = api_heapfree;
    xstrcpy(api.assign_path, assign_path);
    /* The SpeedoGDOS cookie is opt-in: with it, the MultiTOS AES expects
     * the real SpeedoGDOS and loses its desktop text. */
    if (xg_cfg.mode == MODE_FULL && xg_cfg.fsmc) {
        add_cookie(0x46534D43L, (u32)(unsigned long)&fsmc);   /* 'FSMC' */
        XLOG("FSMC cookie installed\r\n");
    }
    xg_bypass = (u8)(xg_cfg.mode == MODE_BYPASS);
    xg_hide_gdos = (u8)(xg_cfg.mode == MODE_NOGDOS || xg_cfg.mode == MODE_BYPASS);
    if (xg_cfg.mode != MODE_FULL) {
        say("Diagnostic MODE = "); say(xg_mode_names[xg_cfg.mode]); say("\r\n");
        XLOG("MODE = "); XLOG(xg_mode_names[xg_cfg.mode]); XLOG("\r\n");
    }
    add_cookie(XGDS_COOKIE, (u32)(unsigned long)&api);

    xg_prun = (u32)Supexec(find_prun_s);
    xg_nslots = (s16)nslots;
    xg_old_trap2 = Setexc(34, (void *)-1L);
    Setexc(34, (void *)xg_trap2);

    sayn(enabled); say(" Speedo fonts, ");
    sayn(heap / 1024); say(" KB heap, ");
    sayn(nslots); say(xg_mint ? " stacks (MiNT)\r\n" : " stacks\r\n");
    say("Printing: ");
    if (xg_cfg.printer == PRN_PDF) {
        char pp[XG_PATHLEN + 16], nm[16];
        xstrcpy(nm, xg_cfg.pdfname); xstrcat(nm, "nnnn.PDF");
        xpath_join(pp, xg_cfg.pdfpath, nm);
        say(pp); say(" at "); sayn(xg_cfg.pdfdpi); say(" dpi\r\n");
    } else say("GDOS drivers\r\n");

#ifdef XG_LOG
    {
        char t[80];
        int q = 0;
        xstrcpy(t, "installed: "); q = 11;
        q += xitoa(t + q, enabled); xstrcpy(t + q, " fonts enabled, "); q += 16;
        q += xitoa(t + q, xg_nfont); xstrcpy(t + q, " found, heap KB "); q += 16;
        q += xitoa(t + q, heap / 1024); xstrcpy(t + q, ", stacks "); q += 9;
        q += xitoa(t + q, nslots); xstrcpy(t + q, "\r\n");
        XLOG(t);
    }
#endif
    Ptermres((s32)(base - (u8 *)bp), 0);
}
