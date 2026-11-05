/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* lock.c - the XGDOS lock
 *
 * XGDOS code runs in supervisor mode inside trap #2, where MiNT does not
 * preempt; a process only loses the CPU when it blocks in GEMDOS (file
 * I/O while loading a font or writing a PDF). So a plain flag is atomic
 * here. A waiting process yields; a lock held by a process that has died
 * meanwhile is recovered. The same process may nest (depth count).
 */
#include "xgdos.h"
#include "xos.h"

static volatile s16 depth;
static volatile s32 owner;

u8  *xg_scratch;
s32  xg_scratch_size;

void xg_lock(void)
{
    s32 me = xos_getpid();
    u32 n = 0;
    for (;;) {
        if (depth == 0) { depth = 1; owner = me; return; }
        if (owner == me) { depth++; return; }
        xos_yield();
        if ((++n & 63) == 0 && !xos_pid_alive(owner)) depth = 0;
    }
}

void xg_unlock(void)
{
    if (depth > 0) depth--;
}
