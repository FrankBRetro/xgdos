/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* crt0.S - startup for XGDOS.PRG (no C library). Must be linked first. */
#ifdef __ELF__
#define C(x) x
#else
#define C(x) _##x
#endif
    .text
    .globl  C(_start)
C(_start):
    move.l  4(%sp),%a0              /* basepage */
    lea     C(xg_ustack)+4096,%sp
    move.l  %a0,-(%sp)
    jsr     C(xg_main)
    clr.w   -(%sp)                  /* Pterm0, not reached */
    trap    #1
