/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* gem.c - minimal AES/VDI bindings */
#include "gem.h"
#include "../src/xstd.h"

void aes_trap(void *pb);
void vdi_trap(void *pb);

s16 aes_global[15];
s16 gl_apid;

static s16 control[5], int_in[16], int_out[8];
static void *addr_in[3], *addr_out[1];
static void *aespb[6] = { control, aes_global, int_in, int_out, addr_in, addr_out };

static s16 aes(s16 op, s16 ni, s16 no, s16 ai)
{
    control[0] = op; control[1] = ni; control[2] = no; control[3] = ai; control[4] = 0;
    aes_trap(aespb);
    return int_out[0];
}

s16 appl_init(void) { gl_apid = aes(10, 0, 1, 0); return gl_apid; }
s16 appl_exit(void) { return aes(19, 0, 1, 0); }
s16 graf_handle(s16 *cw, s16 *ch, s16 *bw, s16 *bh)
{
    aes(77, 0, 5, 0);
    *cw = int_out[1]; *ch = int_out[2]; *bw = int_out[3]; *bh = int_out[4];
    return int_out[0];
}
s16 graf_mouse(s16 m, void *form) { int_in[0] = m; addr_in[0] = form; return aes(78, 1, 1, 1); }
s16 wind_create(s16 k, s16 x, s16 y, s16 w, s16 h) { int_in[0] = k; int_in[1] = x; int_in[2] = y; int_in[3] = w; int_in[4] = h; return aes(100, 5, 1, 0); }
s16 wind_open(s16 hd, s16 x, s16 y, s16 w, s16 h) { int_in[0] = hd; int_in[1] = x; int_in[2] = y; int_in[3] = w; int_in[4] = h; return aes(101, 5, 1, 0); }
s16 wind_close(s16 h) { int_in[0] = h; return aes(102, 1, 1, 0); }
s16 wind_delete(s16 h) { int_in[0] = h; return aes(103, 1, 1, 0); }
s16 wind_get(s16 h, s16 what, s16 *a, s16 *b, s16 *c, s16 *d)
{
    int_in[0] = h; int_in[1] = what;
    aes(104, 2, 5, 0);
    if (a) *a = int_out[1]; if (b) *b = int_out[2]; if (c) *c = int_out[3]; if (d) *d = int_out[4];
    return int_out[0];
}
s16 wind_set(s16 h, s16 what, s16 a, s16 b, s16 c, s16 d)
{
    int_in[0] = h; int_in[1] = what; int_in[2] = a; int_in[3] = b; int_in[4] = c; int_in[5] = d;
    return aes(105, 6, 1, 0);
}
s16 wind_set_str(s16 h, s16 what, const char *s)
{
    u32 p = (u32)(unsigned long)s;
    return wind_set(h, what, (s16)(p >> 16), (s16)p, 0, 0);
}
s16 wind_update(s16 m) { int_in[0] = m; return aes(107, 1, 1, 0); }
s16 wind_calc(s16 t, s16 k, s16 x, s16 y, s16 w, s16 h, s16 *ox, s16 *oy, s16 *ow, s16 *oh)
{
    int_in[0] = t; int_in[1] = k; int_in[2] = x; int_in[3] = y; int_in[4] = w; int_in[5] = h;
    aes(108, 6, 5, 0);
    *ox = int_out[1]; *oy = int_out[2]; *ow = int_out[3]; *oh = int_out[4];
    return int_out[0];
}
s16 evnt_multi(s16 fl, s16 clicks, s16 mask, s16 state, s16 *msg, s32 timer,
               s16 *mx, s16 *my, s16 *mb, s16 *ks, s16 *key, s16 *nc)
{
    xmemset(int_in, 0, sizeof int_in);
    int_in[0] = fl; int_in[1] = clicks; int_in[2] = mask; int_in[3] = state;
    int_in[14] = (s16)timer; int_in[15] = (s16)(timer >> 16);
    addr_in[0] = msg;
    aes(25, 16, 7, 1);
    *mx = int_out[1]; *my = int_out[2]; *mb = int_out[3]; *ks = int_out[4]; *key = int_out[5]; *nc = int_out[6];
    return int_out[0];
}
s16 form_alert(s16 def, const char *s) { int_in[0] = def; addr_in[0] = (void *)s; return aes(52, 1, 1, 1); }
s16 fsel_exinput(char *path, char *file, s16 *button, const char *title)
{
    s16 r;
    addr_in[0] = path; addr_in[1] = file; addr_in[2] = (void *)title;
    if (aes_global[0] >= 0x0140) r = aes(91, 0, 2, 3);
    else r = aes(90, 0, 2, 2);
    *button = int_out[1];
    return r;
}
s16 menu_register(s16 apid, const char *name) { int_in[0] = apid; addr_in[0] = (void *)name; return aes(35, 1, 1, 1); }

/* ---------------- VDI ---------------- */

static s16 vcontrl[12], vintin[128], vptsin[128], vintout[128], vptsout[128];
static void *vdipb[5] = { vcontrl, vintin, vptsin, vintout, vptsout };

static void vdi(s16 op, s16 np, s16 ni, s16 h)
{
    vcontrl[0] = op; vcontrl[1] = np; vcontrl[3] = ni; vcontrl[5] = 0; vcontrl[6] = h;
    vdi_trap(vdipb);
}

s16 v_opnvwk(s16 *wi, s16 *handle, s16 *wo)
{
    int i;
    for (i = 0; i < 11; i++) vintin[i] = wi[i];
    vdi(100, 0, 11, *handle);
    *handle = vcontrl[6];
    for (i = 0; i < 45; i++) wo[i] = vintout[i];
    for (i = 0; i < 12; i++) wo[45 + i] = vptsout[i];
    return *handle;
}
void v_clsvwk(s16 h) { vdi(101, 0, 0, h); }
void vs_clip(s16 h, s16 on, const s16 *p)
{
    int i;
    vintin[0] = on;
    for (i = 0; i < 4; i++) vptsin[i] = p ? p[i] : 0;
    vdi(129, 2, 1, h);
}
void vswr_mode(s16 h, s16 m) { vintin[0] = m; vdi(32, 0, 1, h); }
void vsf_color(s16 h, s16 c) { vintin[0] = c; vdi(25, 0, 1, h); }
void vsf_interior(s16 h, s16 i) { vintin[0] = i; vdi(23, 0, 1, h); }
void vsl_color(s16 h, s16 c) { vintin[0] = c; vdi(17, 0, 1, h); }
void vst_color(s16 h, s16 c) { vintin[0] = c; vdi(22, 0, 1, h); }
void vst_alignment(s16 h, s16 a, s16 b) { vintin[0] = a; vintin[1] = b; vdi(39, 0, 2, h); }
void vst_height(s16 h, s16 height, s16 *cw, s16 *ch, s16 *cellw, s16 *cellh)
{
    vptsin[0] = 0; vptsin[1] = height;
    vptsout[0] = vptsout[1] = vptsout[2] = vptsout[3] = 0;
    vdi(12, 1, 0, h);
    *cw = vptsout[0]; *ch = vptsout[1]; *cellw = vptsout[2]; *cellh = vptsout[3];
}
void vr_recfl(s16 h, const s16 *p) { int i; for (i = 0; i < 4; i++) vptsin[i] = p[i]; vdi(114, 2, 0, h); }
void v_pline(s16 h, s16 n, const s16 *p) { int i; for (i = 0; i < 2 * n; i++) vptsin[i] = p[i]; vdi(6, n, 0, h); }
void v_gtext(s16 h, s16 x, s16 y, const char *s)
{
    int n = 0;
    while (s[n] && n < 127) { vintin[n] = (u8)s[n]; n++; }
    vptsin[0] = x; vptsin[1] = y;
    vdi(8, 1, (s16)n, h);
}
