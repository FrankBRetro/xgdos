/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xstd.c - tiny freestanding C library subset */
#include "xstd.h"

void *xmemcpy(void *d, const void *s, s32 n)
{
    u8 *a = (u8 *)d;
    const u8 *b = (const u8 *)s;
    if (n >= 16 && !(((unsigned long)a | (unsigned long)b) & 1)) {
        u32 *la = (u32 *)a;
        const u32 *lb = (const u32 *)b;
        while (n >= 4) { *la++ = *lb++; n -= 4; }
        a = (u8 *)la; b = (const u8 *)lb;
    }
    while (n-- > 0) *a++ = *b++;
    return d;
}

void *xmemmove(void *d, const void *s, s32 n)
{
    u8 *a = (u8 *)d;
    const u8 *b = (const u8 *)s;
    if (a <= b || a >= b + n) return xmemcpy(d, s, n);
    a += n; b += n;
    while (n-- > 0) *--a = *--b;
    return d;
}

void *xmemset(void *d, int c, s32 n)
{
    u8 *a = (u8 *)d;
    if (n >= 16 && !((unsigned long)a & 1)) {
        u32 v = (u8)c;
        u32 *la = (u32 *)a;
        v |= v << 8; v |= v << 16;
        while (n >= 4) { *la++ = v; n -= 4; }
        a = (u8 *)la;
    }
    while (n-- > 0) *a++ = (u8)c;
    return d;
}

int xmemcmp(const void *a, const void *b, s32 n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (; n > 0; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

s32 xstrlen(const char *s) { const char *p = s; while (*p) p++; return (s32)(p - s); }
char *xstrcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) {} return r; }
char *xstrncpy0(char *d, const char *s, s32 n)
{
    char *r = d;
    if (n <= 0) return d;
    while (--n > 0 && *s) *d++ = *s++;
    *d = 0;
    return r;
}
char *xstrcat(char *d, const char *s) { xstrcpy(d + xstrlen(d), s); return d; }
int xstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
int xtoupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int xstricmp(const char *a, const char *b)
{
    while (*a && xtoupper((u8)*a) == xtoupper((u8)*b)) { a++; b++; }
    return xtoupper((u8)*a) - xtoupper((u8)*b);
}
int xstrnicmp(const char *a, const char *b, s32 n)
{
    for (; n > 0; n--, a++, b++) {
        int c = xtoupper((u8)*a) - xtoupper((u8)*b);
        if (c || !*a) return c;
    }
    return 0;
}
char *xstrchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c ? NULL : (char *)s;
}
char *xstrrchr(const char *s, int c)
{
    const char *r = NULL;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)r;
}

s32 xatoi(const char *s)
{
    s32 v = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

int xitoa(char *buf, s32 v)
{
    char t[12];
    int n = 0, i = 0;
    u32 u;
    if (v < 0) { buf[i++] = '-'; u = (u32)-v; } else u = (u32)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) buf[i++] = t[--n];
    return i;
}

int xftoa(char *buf, s32 f)
{
    int i = 0, d;
    u32 u, frac;
    if (f < 0) { buf[i++] = '-'; u = (u32)-f; } else u = (u32)f;
    u += 32;   /* round to 1/1000 */
    i += xitoa(buf + i, (s32)(u >> 16));
    frac = ((u & 0xffff) * 1000) >> 16;
    if (frac) {
        buf[i++] = '.';
        d = (int)(frac / 100); buf[i++] = (char)('0' + d); frac %= 100;
        if (frac) {
            d = (int)(frac / 10); buf[i++] = (char)('0' + d); frac %= 10;
            if (frac) buf[i++] = (char)('0' + frac);
        }
    }
    return i;
}

void xpath_join(char *out, const char *dir, const char *file)
{
    s32 n;
    if (!dir || !*dir || (file[0] && file[1] == ':') || file[0] == '\\') { xstrcpy(out, file); return; }
    xstrcpy(out, dir);
    n = xstrlen(out);
    if (n && out[n - 1] != '\\') { out[n++] = '\\'; out[n] = 0; }
    xstrcat(out, file);
}

const char *xpath_base(const char *p)
{
    const char *b = xstrrchr(p, '\\');
    if (!b) b = xstrrchr(p, ':');
    return b ? b + 1 : p;
}

#ifndef XG_HOST
/* the compiler may emit calls to these for structure copies */
void *memcpy(void *d, const void *s, __SIZE_TYPE__ n) { return xmemcpy(d, s, (s32)n); }
void *memset(void *d, int c, __SIZE_TYPE__ n) { return xmemset(d, c, (s32)n); }
void *memmove(void *d, const void *s, __SIZE_TYPE__ n) { return xmemmove(d, s, (s32)n); }
#endif
