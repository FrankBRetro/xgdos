/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xos_tos.c - XGDOS OS layer on GEMDOS (TOS, MagiC, MiNT/MultiTOS) */
#include "xos.h"
#include "tos.h"
#include "xstd.h"

int xg_mint;

s32 xos_open(const char *p, int mode) { return Fopen(p, (s16)mode); }
s32 xos_create(const char *p) { return Fcreate(p, 0); }
s32 xos_read(s32 h, s32 n, void *b) { return Fread((s16)h, n, b); }
s32 xos_write(s32 h, s32 n, const void *b) { return Fwrite((s16)h, n, b); }
s32 xos_seek(s32 h, s32 off, int wh) { return Fseek(off, (s16)h, (s16)wh); }
void xos_close(s32 h) { if (h >= 0) Fclose((s16)h); }
s32 xos_delete(const char *p) { return Fdelete(p); }

s32 xos_filesize(const char *p)
{
    s32 h = Fopen(p, 0), n;
    if (h < 0) return h;
    n = Fseek(0, (s16)h, 2);
    Fclose((s16)h);
    return n;
}

static void fill(XosFind *f)
{
    xstrncpy0(f->name, (const char *)f->dta + 30, 14);
    f->size = ((s32)f->dta[26] << 24) | ((s32)f->dta[27] << 16) | ((s32)f->dta[28] << 8) | f->dta[29];
    f->attr = f->dta[21];
}

int xos_findfirst(XosFind *f, const char *pattern)
{
    f->olddta = Fgetdta();
    Fsetdta(f->dta);
    if (Fsfirst(pattern, 0) != 0) return 0;
    fill(f);
    return 1;
}

int xos_findnext(XosFind *f)
{
    Fsetdta(f->dta);
    if (Fsnext() != 0) return 0;
    fill(f);
    return 1;
}

void xos_findend(XosFind *f)
{
    if (f->olddta) Fsetdta(f->olddta);
    f->olddta = NULL;
}

/* memory for a program's own use (the memory device's bitmap): taken in
 * the calling program's context, so it goes when the program does.
 * Mxalloc (TOS 2 / MiNT) prefers ST-RAM; old TOS only has Malloc. */
void *xos_alloc(s32 n)
{
    s32 p = Mxalloc(n, 3);
    if (p == -32 || p == 0) p = Malloc(n);
    return p > 0 ? (void *)(unsigned long)p : 0;
}
void xos_free(void *p) { if (p) Mfree(p); }

void xos_getcwd(char *out, int max)
{
    char b[256];
    s32 d = Dgetdrv();
    int n;
    b[0] = 0;
    if (Dgetpath(b, 0) < 0) b[0] = 0;
    out[0] = (char)('A' + (d & 31)); out[1] = ':'; out[2] = 0;
    if (b[0] != '\\') xstrcat(out, "\\");
    n = (int)xstrlen(out);
    xstrncpy0(out + n, b, max - n - 1);
    n = (int)xstrlen(out);
    if (n == 0 || out[n - 1] != '\\') { out[n] = '\\'; out[n + 1] = 0; }
}

void xos_yield(void) { if (xg_mint) Syield(); }
s32 xos_getpid(void) { return xg_mint ? Pgetpid() : 0; }
int xos_pid_alive(s32 pid) { return !xg_mint || pid <= 0 || Pkill((s16)pid, 0) >= 0; }

static u32 peek_addr;
static s32 peekl_s(void) { return *(volatile s32 *)(unsigned long)peek_addr; }
static s32 peekw_s(void) { return *(volatile s16 *)(unsigned long)peek_addr; }
s32 xg_peekl(u32 a) { peek_addr = a; return Supexec(peekl_s); }
s16 xg_peekw(u32 a) { peek_addr = a; return (s16)Supexec(peekw_s); }

int xos_bootdrive(void) { return xg_peekw(0x446) & 31; }

int xos_screen_dev(void)
{
    struct PK { s16 op; } a = { 4 };                 /* XBIOS Getrez */
    s32 r = xg_trap14(&a, sizeof a);
    return (r >= 0 && r < 8) ? (int)r + 2 : 1;
}

int cookie_get(u32 id, u32 *val)
{
    u32 jar = (u32)xg_peekl(0x5a0);
    if (!jar) return 0;
    for (;;) {
        u32 k = (u32)xg_peekl(jar), v = (u32)xg_peekl(jar + 4);
        if (!k) return 0;
        if (k == id) { if (val) *val = v; return 1; }
        jar += 8;
    }
}

#ifdef XG_LOG
void xos_log(const char *s)
{
    s32 h = Fopen("C:\\XGDOS.LOG", 1);
    if (h < 0) h = Fcreate("C:\\XGDOS.LOG", 0);
    if (h < 0) return;
    Fseek(0, (s16)h, 2);
    Fwrite((s16)h, xstrlen(s), s);
    Fclose((s16)h);
}
#else
void xos_log(const char *s) { (void)s; }
#endif
