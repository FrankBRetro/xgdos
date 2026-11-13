/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xos_dos.c - XGDOS OS layer on DOS (16-bit Open Watcom, huge model)
 *
 * No C library: XGDOS stays in memory while GEM runs, so every K counts.
 * The DOS calls are in pcdos.asm. XGDOS's own headers make int 32-bit for
 * the core (XG_INT32); int is put back after them, and where XGDOS's
 * prototypes say int the definitions here say long.
 */
#include "xos.h"
#include "xstd.h"
#undef int
#include "pcdos.h"

/* DOS reads and writes at most 64K-1 at once: in pieces, through huge
 * pointers so a buffer may cross 64K */
#define PIECE 32768U

s32 xos_open(const char *p, long mode)
{
    int h = pd_open(p, mode == 0 ? 0 : 2);
    return h < 0 ? -33 : h;
}

s32 xos_create(const char *p)
{
    int h = pd_creat(p);
    return h < 0 ? -36 : h;
}

s32 xos_read(s32 h, s32 n, void *buf)
{
    unsigned char __huge *b = (unsigned char __huge *)buf;
    s32 done = 0;
    while (n > 0) {
        unsigned want = n > PIECE ? PIECE : (unsigned)n;
        unsigned got = pd_read((int)h, (void __far *)b, want);
        if (got == 0xFFFFU) return done ? done : -1;
        done += got; b += got; n -= got;
        if (got < want) break;
    }
    return done;
}

s32 xos_write(s32 h, s32 n, const void *buf)
{
    const unsigned char __huge *b = (const unsigned char __huge *)buf;
    s32 done = 0;
    while (n > 0) {
        unsigned want = n > PIECE ? PIECE : (unsigned)n;
        unsigned put = pd_write((int)h, (const void __far *)b, want);
        if (put == 0xFFFFU) return done ? done : -1;
        done += put; b += put; n -= put;
        if (put < want) break;
    }
    return done;
}

s32 xos_seek(s32 h, s32 off, long wh)
{
    return pd_seek((int)h, off, (int)wh);
}

void xos_close(s32 h) { if (h >= 0) pd_close((int)h); }

s32 xos_filesize(const char *p)
{
    s32 h = xos_open(p, 0), n;
    if (h < 0) return h;
    n = xos_seek(h, 0, 2);
    xos_close(h);
    return n;
}

s32 xos_delete(const char *p) { return pd_unlink(p) ? -33 : 0; }

/* DOS's find block, kept in XosFind's dta[] (43 bytes of 44) */
#pragma pack(push, 1)
typedef struct {
    char          res[21];
    unsigned char attr;
    unsigned short time, date;
    unsigned long size;
    char          name[13];
} DosDta;
#pragma pack(pop)
typedef char dta_fits[sizeof(DosDta) <= sizeof(((XosFind *)0)->dta) ? 1 : -1];

static void fill(XosFind *f)
{
    DosDta *d = (DosDta *)f->dta;
    xstrncpy0(f->name, d->name, 14);
    f->size = (s32)d->size;
    f->attr = d->attr;
}

long xos_findfirst(XosFind *f, const char *pattern)
{
    pd_setdta(f->dta);
    if (pd_findfirst(pattern, 0x21)) return 0;    /* normal, read-only, archive */
    fill(f);
    return 1;
}

long xos_findnext(XosFind *f)
{
    pd_setdta(f->dta);
    if (pd_findnext()) return 0;
    fill(f);
    return 1;
}

void xos_findend(XosFind *f) { (void)f; }

/* DOS blocks of any size (in paragraphs), conventional memory */
void *xos_alloc(s32 n)
{
    unsigned seg = n > 0 && n < 0x100000L ? pd_alloc((unsigned)((n + 15) >> 4), 0) : 0;
    return seg ? PD_FP(seg, 0) : 0;
}
void xos_free(void *p) { if (p) pd_free(PD_SEG(p)); }

void xos_getcwd(char *out, long max)
{
    char buf[72];
    int n;
    buf[0] = (char)('A' + pd_getdrive());
    buf[1] = ':';
    buf[2] = '\\';
    if (pd_getcwd(0, buf + 3)) buf[3] = 0;
    n = (int)xstrlen(buf);
    if (buf[n - 1] != '\\') { buf[n] = '\\'; buf[n + 1] = 0; }
    xstrncpy0(out, buf, max);
}

void xos_yield(void) { }                    /* DOS: one program at a time */
s32  xos_getpid(void) { return 0; }
long xos_pid_alive(s32 pid) { (void)pid; return 1; }

long xos_bootdrive(void)
{
    int d = pd_bootdrive();                 /* DOS 4+, 1 = A: */
    return d ? d - 1 : 2;
}

long xos_screen_dev(void) { return 1; }      /* PC GEM's screen is workstation 1 */

void xos_log(const char *s)
{
    int h = pd_open("C:\\XGDOS.LOG", 1);
    if (h < 0) h = pd_creat("C:\\XGDOS.LOG");
    if (h < 0) return;
    pd_seek(h, 0, 2);
    pd_write(h, s, (unsigned)xstrlen(s));
    pd_close(h);
}
