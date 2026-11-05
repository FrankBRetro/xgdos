/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xfix.c - fixed point arithmetic without 64-bit types */
#include "xfix.h"

extern const fix xg_sintab[91];

/* unsigned 32x32 -> 64, returned as hi:lo */
static void umul64(u32 a, u32 b, u32 *hi, u32 *lo)
{
    u32 al = a & 0xffff, ah = a >> 16, bl = b & 0xffff, bh = b >> 16;
    u32 ll = (u32)(u16)al * (u16)bl;
    u32 lh = (u32)(u16)al * (u16)bh;
    u32 hl = (u32)(u16)ah * (u16)bl;
    u32 hh = (u32)(u16)ah * (u16)bh;
    u32 mid = lh + hl;
    u32 carry = (mid < lh) ? 0x10000 : 0;
    u32 l = ll + (mid << 16);
    hh += (mid >> 16) + carry + (l < ll ? 1 : 0);
    *hi = hh; *lo = l;
}

/* unsigned 64/32 -> 32 (quotient assumed to fit) */
static u32 udiv64(u32 hi, u32 lo, u32 d)
{
    u32 q = 0;
    int i;
    if (d == 0) return 0x7fffffff;
    if (hi >= d) return 0xffffffff;  /* overflow */
    for (i = 0; i < 32; i++) {
        u32 top = hi & 0x80000000;
        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        q <<= 1;
        if (top || hi >= d) { hi -= d; q |= 1; }
    }
    return q;
}

fix fixmul(fix a, fix b)
{
    int neg = 0;
    u32 ua, ub, hi, lo;
    if (a < 0) { ua = (u32)-a; neg = 1; } else ua = (u32)a;
    if (b < 0) { ub = (u32)-b; neg ^= 1; } else ub = (u32)b;
    umul64(ua, ub, &hi, &lo);
    lo = (lo >> 16) | (hi << 16);
    return neg ? -(fix)lo : (fix)lo;
}

s32 muldiv(s32 a, s32 b, s32 c)
{
    int neg = 0;
    u32 ua, ub, uc, hi, lo, q;
    if (c == 0) return 0;
    if (a < 0) { ua = (u32)-a; neg = 1; } else ua = (u32)a;
    if (b < 0) { ub = (u32)-b; neg ^= 1; } else ub = (u32)b;
    if (c < 0) { uc = (u32)-c; neg ^= 1; } else uc = (u32)c;
    umul64(ua, ub, &hi, &lo);
    q = udiv64(hi, lo, uc);
    if (q > 0x7fffffff) q = 0x7fffffff;
    return neg ? -(s32)q : (s32)q;
}

fix fixdiv(fix a, fix b)
{
    return muldiv(a, FIX_ONE, b);
}

fix fixsin(s32 t)
{
    s32 deg, frac, q;
    fix v;
    t %= 3600;
    if (t < 0) t += 3600;
    q = t / 900;
    t -= q * 900;
    if (q & 1) t = 900 - t;
    deg = t / 10;
    frac = t - deg * 10;
    v = xg_sintab[deg];
    if (frac && deg < 90)
        v += (xg_sintab[deg + 1] - v) * frac / 10;
    return (q >= 2) ? -v : v;
}

fix fixcos(s32 t)
{
    return fixsin(t + 900);
}

u32 isqrt(u32 v)
{
    u32 r = 0, b = 0x40000000;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
