/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pcmain.c - XGDOS.EXE for PC GEM: the launcher
 *
 *   XGDOS GEMVDI [args]      (in GEM.BAT, in place of "GEMVDI [args]")
 *
 * Reads XGDOS.SYS (next to XGDOS.EXE, else C:\XGDOS.SYS) - the same
 * settings and font lines as the Atari's ASSIGN.SYS - sets up the heap and
 * the font table, watches DOS's EXEC call, and runs GEMVDI. When GEMVDI
 * EXECs GEM.EXE, XGDOS goes in front of its INT EFh entry (pcentry.asm).
 * When GEM ends, so does XGDOS: nothing stays resident.
 *
 * XGDOS stays in memory all the time GEM runs, and GEM needs most of the
 * 640K, so there is no C library (pcdos.asm starts XGDOS and makes its DOS
 * calls) and the heap and scratch area go in upper memory when they fit.
 *
 * Testing switches (SET name=1 before running XGDOS):
 *   XGPASS    XGDOS in front of GEMVDI, but every call goes straight on
 *   XGNOHOOK  XGDOS in memory but not in front: GEM runs on GEMVDI alone
 */
#include "xgdos.h"
#include "config.h"
#include "heap.h"
#include "xos.h"
#include "xstd.h"
#undef int
#include "pcdos.h"

#define SCRATCH (24L * 1024)
#define CFGMAX  16384

void __cdecl pc_hook21(void);
void __cdecl pc_unhook21(void);
extern unsigned short __far pc_gemvdi_seen;
extern short pc_pass;

static char cfg_path[128];
static char prog[128];          /* XGDOS.EXE's own path, from the environment */
static char tail[130];          /* the command line, split up in place */
static char runpath[128];
static char *run[16];

/* ---- output (standard output: GEM.BAT may redirect it) ---- */
static void out(const char *s) { pd_write(1, s, (unsigned)xstrlen(s)); }
static void outn(long v) { char t[12]; t[xitoa(t, v)] = 0; out(t); }

/* ---- the environment ---- */
static const char __far *env_block(void)
{
    return (const char __far *)PD_FP(*(unsigned short __far *)PD_FP(pc_psp, 0x2C), 0);
}

/* "NAME=value": the value, or NULL */
static const char __far *env_get(const char *name)
{
    const char __far *e = env_block();
    while (*e) {
        const char *n = name;
        const char __far *v = e;
        while (*n && *v == *n) { n++; v++; }
        if (!*n && *v == '=') return v + 1;
        while (*e) e++;
        e++;
    }
    return 0;
}

/* the program's own path, after the environment's last string (DOS 3+) */
static void own_path(void)
{
    const char __far *e = env_block();
    int i = 0;
    while (*e) { while (*e) e++; e++; }
    e++;
    if (*(const unsigned short __far *)e >= 1) {
        e += 2;
        while (*e && i < (int)sizeof prog - 1) prog[i++] = *e++;
    }
    prog[i] = 0;
}

/* the command line (PSP:80h) into run[] */
static int split_args(void)
{
    const unsigned char __far *p = (const unsigned char __far *)PD_FP(pc_psp, 0x80);
    int n = p[0], i, argc = 0;
    if (n > 127) n = 127;
    for (i = 0; i < n; i++) tail[i] = (char)p[1 + i];
    tail[n] = 0;
    for (i = 0; i < n && argc < 15; ) {
        while (i < n && (tail[i] == ' ' || tail[i] == '\t')) tail[i++] = 0;
        if (i >= n) break;
        run[argc++] = tail + i;
        while (i < n && tail[i] != ' ' && tail[i] != '\t') i++;
    }
    run[argc] = 0;
    return argc;
}

/* ---- memory ---- */

/* DOS memory in upper memory (UMBs, high = 1) or conventional memory
 * (high = 0). Returns the segment, 0: none. */
static unsigned dos_mem(long bytes, int high)
{
    unsigned strat, link, seg, paras = (unsigned)((bytes + 15) / 16);
    strat = pd_ax(0x5800, 0);
    link = pd_ax(0x5802, 0) & 0xFF;
    pd_ax(0x5803, 1);                       /* link the UMBs in */
    pd_ax(0x5801, high ? 0x41 : 0x01);      /* upper only / conventional, best fit */
    seg = pd_alloc(paras, 0);
    pd_ax(0x5801, strat);
    pd_ax(0x5803, link);
    return seg;
}

/* ---- XGDOS.SYS next to the program, else C:\XGDOS.SYS ---- */
static long read_cfg(char *buf, long max)
{
    int h = -1, i, slash = -1;
    long n;
    for (i = 0; prog[i]; i++) if (prog[i] == '\\' || prog[i] == ':') slash = i;
    if (slash >= 0 && slash < (int)sizeof cfg_path - 12) {
        for (i = 0; i <= slash; i++) cfg_path[i] = prog[i];
        xstrcpy(cfg_path + slash + 1, "XGDOS.SYS");
        h = pd_open(cfg_path, 0);
    }
    if (h < 0) { xstrcpy(cfg_path, "C:\\XGDOS.SYS"); h = pd_open(cfg_path, 0); }
    if (h < 0) { cfg_path[0] = 0; return -1; }
    n = xos_read(h, max - 1, buf);
    pd_close(h);
    buf[n > 0 ? n : 0] = 0;
    return n;
}

/* ---- running GEMVDI ---- */

static int exists(const char *p)
{
    int h = pd_open(p, 0);
    if (h < 0) return 0;
    pd_close(h);
    return 1;
}

/* name as given, with .EXE / .COM when it has no extension; then along
 * PATH when it has no drive or folder */
static int find_prog(const char *name)
{
    static const char *const ext[] = { ".EXE", ".COM" };
    int dot = 0, dir = 0, i, e, n;
    const char __far *path;
    for (i = 0; name[i]; i++) {
        if (name[i] == '.') dot = 1;
        if (name[i] == '\\' || name[i] == ':') { dir = 1; dot = 0; }
    }
    path = dir ? 0 : env_get("PATH");
    for (;;) {
        for (e = 0; e < (dot ? 1 : 2); e++) {
            n = (int)xstrlen(runpath);
            if (n + xstrlen(name) + 5 >= (long)sizeof runpath) return 0;
            xstrcpy(runpath + n, name);
            if (!dot) xstrcpy(runpath + xstrlen(runpath), ext[e]);
            if (exists(runpath)) return 1;
            runpath[n] = 0;
        }
        if (!path || !*path) return 0;
        n = 0;                              /* the next PATH entry */
        while (*path && *path != ';' && n < (int)sizeof runpath - 14) runpath[n++] = *path++;
        if (*path == ';') path++;
        if (n && runpath[n - 1] != '\\') runpath[n++] = '\\';
        runpath[n] = 0;
    }
}

#pragma pack(push, 1)
typedef struct {
    unsigned short env;                     /* 0: a copy of ours */
    char __far *tail;
    void __far *fcb1, __far *fcb2;
} ExecParams;
#pragma pack(pop)

/* returns the program's exit code, or -1 if it couldn't be run */
static int run_prog(char **argv)
{
    static char cmd[128];
    ExecParams ep;
    int i, n = 0;
    const char *a;
    runpath[0] = 0;
    if (!find_prog(argv[0])) return -1;
    for (i = 1; argv[i]; i++) {             /* " arg arg...\r" */
        a = argv[i];
        if (n + xstrlen(a) + 2 > 126) break;
        cmd[1 + n++] = ' ';
        while (*a) cmd[1 + n++] = *a++;
    }
    cmd[0] = (char)n;
    cmd[1 + n] = '\r';
    ep.env = 0;
    ep.tail = cmd;
    ep.fcb1 = PD_FP(pc_psp, 0x5C);
    ep.fcb2 = PD_FP(pc_psp, 0x6C);
    if (pd_exec(runpath, &ep)) return -1;
    return pd_retcode();
}

int __cdecl xg_main(void)
{
    unsigned text_seg, heap_seg = 0, scr_seg, largest;
    char *text;
    int heap_high, scr_high;
    long heapsize, n;
    int i, enabled = 0, rc;

    own_path();
    out("XGDOS " XGDOS_VSTRING " for PC GEM - Speedo outline fonts\r\n");

    cfg_defaults(&xg_cfg, xos_bootdrive());
    text_seg = pd_alloc(CFGMAX / 16, 0);    /* only while starting: given back */
    text = text_seg ? (char *)PD_FP(text_seg, 0) : 0;
    n = text ? read_cfg(text, CFGMAX) : -1;
    if (n > 0) { cfg_parse(&xg_cfg, text, n); out("Settings: "); out(cfg_path); out("\r\n"); }
    else out("No XGDOS.SYS - using the defaults.\r\n");
    if (text_seg) pd_free(text_seg);

    /* upper memory for the scratch area, then as big a heap as fits beside
     * it (down to 64K): conventional memory only when they don't fit, as
     * GEM and its programs need all of that they can get */
    xg_scratch_size = SCRATCH;
    scr_high = 1;
    if (!(scr_seg = dos_mem(SCRATCH, 1))) { scr_high = 0; scr_seg = dos_mem(SCRATCH, 0); }
    xg_scratch = scr_seg ? (u8 *)PD_FP(scr_seg, 0) : 0;
    heapsize = (long)xg_cfg.heap_kb * 1024L;
    if (heapsize < 64L * 1024) heapsize = 64L * 1024;
    heap_high = 1;
    for (n = heapsize; n >= 64L * 1024 && !(heap_seg = dos_mem(n, 1)); n -= 8L * 1024) ;
    if (heap_seg) heapsize = n;
    else {
        heap_high = 0;
        while (!(heap_seg = dos_mem(heapsize, 0)) && heapsize > 64L * 1024) heapsize -= 16L * 1024;
    }
    if (!heap_seg || !xg_scratch) { out("Not enough memory for XGDOS.\r\n"); return 1; }
    heap_init(PD_FP(heap_seg, 0), heapsize);
    if (xg_cfg.cache_kb * 1024L > heapsize / 2) xg_cfg.cache_kb = (s16)(heapsize / 2048);

    fm_init();
    for (i = 0; i < xg_nfont; i++) if (xg_font[i].enabled) enabled++;
    outn(enabled); out(" Speedo fonts, "); outn(heapsize / 1024);
    out(" KB heap in "); out(heap_high ? "upper" : "conventional");
    out(" memory, scratch in "); out(scr_high ? "upper" : "conventional"); out("\r\n");

    /* what to run: the arguments, else GEMVDI */
    if (!split_args()) { run[0] = "GEMVDI"; run[1] = 0; }

    largest = 0;
    pd_alloc(0xFFFF, &largest);             /* fails, and says the largest block */
    out("Conventional memory left for GEM: "); outn(largest / 64); out(" KB\r\n");
    if (env_get("XGPASS")) {
        pc_pass = 1;
        out("XGPASS set: every VDI call goes straight to GEM\r\n");
    }
#ifdef XG_LOG
    XLOG("---- XGDOS start\r\n");
#endif
    if (env_get("XGNOHOOK")) {
        out("XGNOHOOK set: GEM runs without XGDOS in front\r\n");
        rc = run_prog(run);
    } else {
        pc_hook21();
        rc = run_prog(run);
        pc_unhook21();
    }
    if (rc < 0) { out("XGDOS: couldn't run "); out(run[0]); out("\r\n"); rc = 1; }
    else if (!pc_gemvdi_seen) out("XGDOS: GEMVDI's INT EFh entry was never seen - no outline fonts this time\r\n");

    pd_free(heap_seg);
    pd_free(scr_seg);
    return rc;
}
