/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* gem.h - minimal AES/VDI bindings (no GEM library needed) */
#ifndef GEM_H
#define GEM_H
#include "../src/xtypes.h"

extern s16 aes_global[15];
extern s16 gl_apid;

/* AES */
s16  appl_init(void);
s16  appl_exit(void);
s16  graf_handle(s16 *cw, s16 *ch, s16 *bw, s16 *bh);
s16  graf_mouse(s16 m, void *form);
s16  wind_create(s16 kind, s16 x, s16 y, s16 w, s16 h);
s16  wind_open(s16 h, s16 x, s16 y, s16 w, s16 hh);
s16  wind_close(s16 h);
s16  wind_delete(s16 h);
s16  wind_get(s16 h, s16 what, s16 *a, s16 *b, s16 *c, s16 *d);
s16  wind_set(s16 h, s16 what, s16 a, s16 b, s16 c, s16 d);
s16  wind_set_str(s16 h, s16 what, const char *s);
s16  wind_update(s16 m);
s16  wind_calc(s16 type, s16 kind, s16 x, s16 y, s16 w, s16 h, s16 *ox, s16 *oy, s16 *ow, s16 *oh);
s16  evnt_multi(s16 flags, s16 clicks, s16 mask, s16 state, s16 *msg, s32 timer,
                s16 *mx, s16 *my, s16 *mb, s16 *ks, s16 *key, s16 *nclicks);
s16  form_alert(s16 def, const char *s);
s16  fsel_exinput(char *path, char *file, s16 *button, const char *title);
s16  menu_register(s16 apid, const char *name);

/* VDI */
s16  v_opnvwk(s16 *work_in, s16 *handle, s16 *work_out);
void v_clsvwk(s16 h);
void vs_clip(s16 h, s16 on, const s16 *pxy);
void vswr_mode(s16 h, s16 m);
void vsf_color(s16 h, s16 c);
void vsf_interior(s16 h, s16 i);
void vsl_color(s16 h, s16 c);
void vst_color(s16 h, s16 c);
void vr_recfl(s16 h, const s16 *pxy);
void v_pline(s16 h, s16 n, const s16 *pxy);
void v_gtext(s16 h, s16 x, s16 y, const char *s);
void vst_alignment(s16 h, s16 ha, s16 va);
void vst_height(s16 h, s16 height, s16 *cw, s16 *ch, s16 *cellw, s16 *cellh);

#define WM_REDRAW 20
#define WM_TOPPED 21
#define WM_CLOSED 22
#define WM_FULLED 23
#define WM_ARROWED 24
#define WM_VSLID 26
#define WM_SIZED 27
#define WM_MOVED 28
#define AC_OPEN 40
#define AC_CLOSE 41
#define AP_TERM 50

#define MU_KEYBD 1
#define MU_BUTTON 2
#define MU_MESAG 16

#define NAME 1
#define CLOSER 2
#define FULLER 4
#define MOVER 8
#define SIZER 32
#define UPARROW 64
#define DNARROW 128
#define VSLIDE 256

#define WF_WORKXYWH 4
#define WF_CURRXYWH 5
#define WF_FIRSTXYWH 11
#define WF_NEXTXYWH 12
#define WF_VSLIDE 9
#define WF_VSLSIZE 16
#define WF_TOP 10
#define WF_NAME 2
#define WF_INFO 3

#endif
