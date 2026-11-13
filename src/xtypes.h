/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xtypes.h - basic types for XGDOS (portable: m68k-atari-mint-gcc and host gcc)
 * int is assumed 32 bit (do NOT build with -mshort).
 */
#ifndef XTYPES_H
#define XTYPES_H

/* PC (16-bit Open Watcom, see pc/Makefile): XGDOS's code assumes a 32-bit
 * int, so in its own sources int is long. The core uses no C library, so
 * nothing else sees this. */
#if defined(__I86__) && defined(XG_INT32)
#define int long
#endif

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef short          s16;
typedef unsigned int   u32;
typedef int            s32;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define XG_MIN(a,b) ((a) < (b) ? (a) : (b))
#define XG_MAX(a,b) ((a) > (b) ? (a) : (b))
#define XG_ABS(a)   ((a) < 0 ? -(a) : (a))

#endif
