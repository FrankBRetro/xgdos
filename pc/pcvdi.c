/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pcvdi.c - the VDI side of XGDOS on PC GEM
 *
 * pcentry.asm takes INT EFh (in front of GEMVDI's GDOS) and calls:
 *   pc_need(pb)  on the caller's stack: is this call XGDOS's at all?
 *   pc_vdi(pb)   on XGDOS's own stack: do it
 * and pc_rom_call(pb) hands a call to GEMVDI, as rom_vdi() on the Atari
 * hands one to the ROM VDI. pb is GEM's own parameter block: five far
 * pointers, the same layout as XGDOS's VDIPB in the huge model.
 *
 * GEM's VDI does its own bitmap fonts and printer drivers, so XGDOS has
 * no font chain or drivers of its own to offer here.
 */
#include "vdi.h"
#include "xgdos.h"
#include "xos.h"
#undef int

void __cdecl pc_rom_call(VDIPB *pb);            /* pcentry.asm */

/* single-tasking DOS: a nesting count is the whole lock */
static short depth;
static short busy;          /* in pc_vdi or the log: no logging from nested calls */
void xg_lock(void) { depth++; }
void xg_unlock(void) { if (depth > 0) depth--; }

u8  *xg_scratch;
s32  xg_scratch_size;

void rom_vdi(VDIPB *pb) { pc_rom_call(pb); }

/* every workstation open goes to GEM: its drivers do the printers (XGDOS's
 * own devices aren't in the PC build, see pcstubs.c); XGDOS starts tracking
 * a workstation at its first GDOS call, as on the Atari */
short pc_pass;              /* SET XGPASS=1: hand every call to GEM (testing) */
static short nested_seen;   /* calls that arrived while XGDOS was busy */
short __cdecl pc_need(VDIPB *pb)
{
    short r;
#ifdef XG_LOG
    static short n;
    char t[8];
    short v, i;
#endif
    if (pb->contrl[0] == 1) return 0;
    r = pc_pass ? 0 : (xg_vdi_need(pb) ? 1 : 0);
#ifdef XG_LOG
    if (!busy && n < 600) { /* every call, briefly: op and handle */
        busy++;
        n++;
        v = pb->contrl[0];
        i = 0;
        t[i++] = 'n'; t[i++] = (char)('0' + v / 100 % 10);
        t[i++] = (char)('0' + v / 10 % 10); t[i++] = (char)('0' + v % 10);
        t[i++] = r ? '*' : ' '; t[i++] = ' '; t[i] = 0;
        XLOG(t);
        if (n % 12 == 0) XLOG("\r\n");
        busy--;
    } else if (busy && n < 600) {
        n++;
        nested_seen++;
    }
#endif
    return r;
}
void  __cdecl pc_vdi(VDIPB *pb)
{
    busy++;
    xg_vdi(pb);
#ifdef XG_LOG
    XLOG("  done\r\n");
    if (nested_seen) { XLOG("  (calls arrived while busy)\r\n"); nested_seen = 0; }
#endif
    busy--;
}

/* the Atari's GDOS bitmap font chain and device drivers: not on the PC */
void *fnt_chain(long dev) { (void)dev; return 0; }
void  fnt_load_all(void) { }
long  drv_open(VDIPB *pb, const char *drv) { (void)drv; pb->contrl[6] = 0; return 0; }
void  drv_call(Wk *w, VDIPB *pb) { (void)w; (void)pb; }
