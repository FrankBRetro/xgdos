/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
#define _GNU_SOURCE
/* xos_host.c - stdio implementation of the XGDOS OS layer for host tests.
 * Atari paths like "C:\PRINTS\PRN0001.PDF" map to ./hostfs/C/PRINTS/... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <ctype.h>
#include "../src/xos.h"
#include "../src/xgdos.h"

static FILE *fh[64];
static char root[256] = "hostfs";

static void map(const char *p, char *out)
{
    char *o;
    if (p[0] && p[1] == ':') { sprintf(out, "%s/%c%s", root, toupper((unsigned char)p[0]), p + 2); }
    else sprintf(out, "%s/C/%s", root, p);
    for (o = out; *o; o++) if (*o == '\\') *o = '/';
}

static s32 slot(FILE *f) { int i; if (!f) return -33; for (i = 6; i < 64; i++) if (!fh[i]) { fh[i] = f; return i; } fclose(f); return -35; }
s32 xos_open(const char *p, int mode) { char b[512]; map(p, b); return slot(fopen(b, mode == 0 ? "rb" : "r+b")); }
s32 xos_create(const char *p) { char b[512]; map(p, b); return slot(fopen(b, "w+b")); }
s32 xos_read(s32 h, s32 n, void *buf) { return (s32)fread(buf, 1, n, fh[h]); }
s32 xos_write(s32 h, s32 n, const void *buf) { return (s32)fwrite(buf, 1, n, fh[h]); }
s32 xos_seek(s32 h, s32 off, int wh) { fseek(fh[h], off, wh == 0 ? SEEK_SET : wh == 1 ? SEEK_CUR : SEEK_END); return (s32)ftell(fh[h]); }
void xos_close(s32 h) { if (h >= 0 && fh[h]) { fclose(fh[h]); fh[h] = NULL; } }
s32 xos_filesize(const char *p) { char b[512]; FILE *f; long n; map(p, b); f = fopen(b, "rb"); if (!f) return -33; fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); return (s32)n; }
s32 xos_delete(const char *p) { char b[512]; map(p, b); return remove(b); }

typedef struct { DIR *d; char pat[64]; } HF;
int xos_findfirst(XosFind *f, const char *pattern)
{
    char b[512], *sl; HF *h = calloc(1, sizeof *h);
    map(pattern, b); sl = strrchr(b, '/');
    *sl = 0; strcpy(h->pat, sl + 1);
    h->d = opendir(b);
    *(HF **)f->dta = h;
    if (!h->d) return 0;
    return xos_findnext(f);
}
int xos_findnext(XosFind *f)
{
    HF *h = *(HF **)f->dta; struct dirent *e;
    if (!h || !h->d) return 0;
    while ((e = readdir(h->d))) if (!fnmatch(h->pat, e->d_name, FNM_CASEFOLD)) { strncpy(f->name, e->d_name, 13); f->name[13] = 0; return 1; }
    return 0;
}
void xos_findend(XosFind *f) { HF *h = *(HF **)f->dta; if (h) { if (h->d) closedir(h->d); free(h); } *(HF **)f->dta = NULL; }
void *xos_alloc(s32 n) { return malloc(n); }
void xos_free(void *p) { free(p); }
void xos_getcwd(char *out, int max) { (void)max; strcpy(out, "C:\\"); }
void xos_yield(void) {}
s32 xos_getpid(void) { return 0; }
int xos_pid_alive(s32 pid) { (void)pid; return 1; }
int xos_bootdrive(void) { return 2; }
int xos_screen_dev(void) { return 4; }
void xos_log(const char *s) { fputs(s, stderr); }

/* lock: single threaded host */
static int depth;
void xg_lock(void) { depth++; }
void xg_unlock(void) { depth--; }
u8 *xg_scratch; s32 xg_scratch_size;
