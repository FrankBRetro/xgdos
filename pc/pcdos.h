/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pcdos.h - XGDOS.EXE's DOS calls (pcdos.asm). XGDOS stays in memory while
 * GEM runs, so it has no C library: these are all it uses of DOS.
 * Include after #undef int: the types here are DOS's 16-bit ones. */
#ifndef PCDOS_H
#define PCDOS_H

#define PD_FP(s, o)   ((void __far *)(((unsigned long)(unsigned)(s) << 16) | (unsigned)(o)))
#define PD_SEG(p)     ((unsigned)((unsigned long)(void __far *)(p) >> 16))

extern unsigned short pc_psp;           /* XGDOS's PSP */

int      __cdecl pd_open(const char __far *path, int mode);     /* -1: error */
int      __cdecl pd_creat(const char __far *path);
unsigned __cdecl pd_read(int h, void __far *buf, unsigned n);   /* 0xFFFF: error */
unsigned __cdecl pd_write(int h, const void __far *buf, unsigned n);
void     __cdecl pd_close(int h);
long     __cdecl pd_seek(int h, long off, int whence);          /* -1: error */
int      __cdecl pd_unlink(const char __far *path);
void     __cdecl pd_setdta(void __far *dta);
int      __cdecl pd_findfirst(const char __far *pattern, int attr);  /* 0: found */
int      __cdecl pd_findnext(void);
int      __cdecl pd_getdrive(void);                             /* 0 = A: */
int      __cdecl pd_getcwd(int drive, char __far *buf);         /* 64 bytes */
unsigned __cdecl pd_ax(unsigned ax, unsigned bx);
int      __cdecl pd_bootdrive(void);                            /* 1 = A:, 0 unknown */
unsigned __cdecl pd_alloc(unsigned paras, unsigned __far *largest); /* 0: none */
void     __cdecl pd_free(unsigned seg);
int      __cdecl pd_exec(const char __far *path, void __far *params);  /* 0, or DOS error */
int      __cdecl pd_retcode(void);

#endif
