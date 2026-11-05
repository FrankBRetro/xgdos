/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xstd.h - tiny freestanding C library subset */
#ifndef XSTD_H
#define XSTD_H
#include "xtypes.h"

void *xmemcpy(void *d, const void *s, s32 n);
void *xmemset(void *d, int c, s32 n);
void *xmemmove(void *d, const void *s, s32 n);
int   xmemcmp(const void *a, const void *b, s32 n);
s32   xstrlen(const char *s);
char *xstrcpy(char *d, const char *s);
char *xstrncpy0(char *d, const char *s, s32 n);   /* always terminates */
char *xstrcat(char *d, const char *s);
int   xstrcmp(const char *a, const char *b);
int   xstricmp(const char *a, const char *b);
int   xstrnicmp(const char *a, const char *b, s32 n);
char *xstrchr(const char *s, int c);
char *xstrrchr(const char *s, int c);
int   xtoupper(int c);
s32   xatoi(const char *s);
/* format a signed integer; returns chars written (no terminator added) */
int   xitoa(char *buf, s32 v);
/* format 16.16 fixed with up to 3 decimals, trimmed; returns length */
int   xftoa(char *buf, s32 fix16);
/* path helpers */
void  xpath_join(char *out, const char *dir, const char *file);
const char *xpath_base(const char *p);

#endif
