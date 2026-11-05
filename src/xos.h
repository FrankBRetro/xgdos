/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xos.h - operating system layer used by the portable XGDOS modules.
 * Atari implementation: xos_tos.c (GEMDOS/BIOS/XBIOS, MiNT aware).
 * Host implementation (tests): host/xos_host.c (stdio).
 */
#ifndef XOS_H
#define XOS_H
#include "xtypes.h"

s32  xos_open(const char *path, int mode);       /* 0 read, 1 write, 2 r/w */
s32  xos_create(const char *path);
s32  xos_read(s32 h, s32 n, void *buf);
s32  xos_write(s32 h, s32 n, const void *buf);
s32  xos_seek(s32 h, s32 off, int whence);       /* 0 set, 1 cur, 2 end */
void xos_close(s32 h);
s32  xos_filesize(const char *path);             /* <0 if missing */
s32  xos_delete(const char *path);

typedef struct XosFind {
    u8   dta[44];                                /* GEMDOS DTA */
    void *olddta;
    char name[14];
    s32  size;
    u8   attr;
} XosFind;
int  xos_findfirst(XosFind *f, const char *pattern);  /* 1 = found */
int  xos_findnext(XosFind *f);
void xos_findend(XosFind *f);

void *xos_alloc(s32 n);                          /* memory owned by the calling program */
void  xos_free(void *p);
void  xos_getcwd(char *out, int max);            /* "C:\FOLDER\" of the calling program */
void xos_yield(void);                            /* Syield under MiNT */
s32  xos_getpid(void);                           /* 0 when not MiNT */
int  xos_pid_alive(s32 pid);
int  xos_bootdrive(void);                        /* 0 = A: */
int  xos_screen_dev(void);                       /* GDOS screen device id (Getrez()+2) */
void xos_log(const char *s);                     /* debug build only */
#ifdef XG_LOG
#define XLOG(s) xos_log(s)
#else
#define XLOG(s) ((void)0)
#endif

#endif
