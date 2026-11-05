/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* crt0app.S - startup for XGDOSCFG (program or desk accessory) */
#ifdef __ELF__
#define C(x) x
#else
#define C(x) _##x
#endif
    .text
    .globl  C(_start)
C(_start):
    move.l  %a0,%d0                 /* a0 = basepage when loaded as ACC */
    bne.s   1f
    move.l  4(%sp),%a0              /* program: basepage on the stack */
    moveq   #0,%d1
    bra.s   2f
1:  moveq   #1,%d1
2:  lea     C(app_stack)+8192,%sp
    move.l  %d1,-(%sp)
    move.l  %a0,-(%sp)
    jsr     C(app_main)
    clr.w   -(%sp)                  /* Pterm0 */
    trap    #1
3:  bra.s   3b
