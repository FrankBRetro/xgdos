/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xfix.h - 16.16 fixed point helpers that avoid 64-bit arithmetic
 * (68000 has only 16x16 multiply and 32/16 divide).
 */
#ifndef XFIX_H
#define XFIX_H
#include "xtypes.h"

typedef s32 fix;
#define FIX_ONE  65536
#define FIX_HALF 32768
#define INT2FIX(i) ((fix)(i) * 65536)
#define FIX2INT(f) ((s32)(f) >> 16)                /* floor */
#define FIXROUND(f) (((s32)(f) + FIX_HALF) >> 16)  /* round to nearest int */

fix  fixmul(fix a, fix b);          /* (a*b) >> 16 */
fix  fixdiv(fix a, fix b);          /* (a<<16) / b */
s32  muldiv(s32 a, s32 b, s32 c);   /* a*b/c without overflow (truncates) */
fix  fixsin(s32 tenths);            /* sin of angle in 1/10 degree */
fix  fixcos(s32 tenths);
u32  isqrt(u32 v);

#endif
