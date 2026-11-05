/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* drv.c - external GDOS device drivers (.SYS) for devices XGDOS does not
 * implement itself (metafile 31, memory 61, or printers when
 * PRINTER = DRIVER). The driver is loaded with Pexec(3) into the calling
 * application's memory and entered at its text start with d0 = 115 and
 * d1 = VDI parameter block, the classic GDOS convention.
 * Driver handles are remapped so they can never clash with screen handles.
 */
#include "vdi.h"
#include "tos.h"
#include "xstd.h"

s32 drv_jump(void *entry, VDIPB *pb);

int drv_open(VDIPB *pb, const char *drv)
{
    char path[XG_PATHLEN];
    s32 bp;
    BASEPAGE *b;
    Wk *w;
    int h, i;
    pb->contrl[6] = 0;
    xpath_join(path, xg_cfg.path, drv);
    bp = Pexec(3, path, "", NULL);
    if (bp <= 0) return 0;
    b = (BASEPAGE *)(unsigned long)bp;
    drv_jump(b->p_tbase, pb);
    if (pb->contrl[6] <= 0) {
        if (b->p_env) Mfree(b->p_env);
        Mfree(b);
        return 0;
    }
    for (h = 300, i = 0; i < 100 && wk_find(h); h++, i++) ;
    w = wk_alloc(h, WK_DRV);
    if (!w) { pb->contrl[6] = 0; return 0; }
    w->drv_entry = b->p_tbase;
    w->drvh = pb->contrl[6];
    w->drvbp = b;
    w->dev = pb->intin[0];
    pb->contrl[6] = (s16)h;
    return 1;
}

void drv_call(Wk *w, VDIPB *pb)
{
    s16 h = pb->contrl[6];
    int op = pb->contrl[0];
    pb->contrl[6] = w->drvh;
    drv_jump(w->drv_entry, pb);
    pb->contrl[6] = h;
    if (op == 2) {
        BASEPAGE *b = (BASEPAGE *)w->drvbp;
        if (b) { if (b->p_env) Mfree(b->p_env); Mfree(b); }
        wk_free(w);
    }
}
