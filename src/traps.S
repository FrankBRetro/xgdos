/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* traps.S - GEMDOS/BIOS/XBIOS/AES/VDI trap wrappers shared by XGDOS.PRG
 * and XGDOSCFG.PRG (GNU as, Motorola syntax with % registers).
 *
 * long xg_trapN(const void *args, long nbytes): copy an argument block
 * onto the stack and trap. d2-d7/a2-a6 are preserved.
 */
#ifdef __ELF__
#define C(x) x
#else
#define C(x) _##x
#endif

    .text
/* ------------------------------------------------------------------ */
/* long xg_trapN(const void *args, long nbytes)                        */
    .globl  C(xg_trap1)
C(xg_trap1):
    move.l  4(%sp),%a0
    move.l  8(%sp),%d0
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l  %sp,%a3
    sub.l   %d0,%sp
    move.l  %sp,%a1
    lsr.l   #1,%d0
    bra.s   2f
1:  move.w  (%a0)+,(%a1)+
2:  dbra    %d0,1b
    trap    #1
    move.l  %a3,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .globl  C(xg_trap13)
C(xg_trap13):
    move.l  4(%sp),%a0
    move.l  8(%sp),%d0
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l  %sp,%a3
    sub.l   %d0,%sp
    move.l  %sp,%a1
    lsr.l   #1,%d0
    bra.s   2f
1:  move.w  (%a0)+,(%a1)+
2:  dbra    %d0,1b
    trap    #13
    move.l  %a3,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .globl  C(xg_trap14)
C(xg_trap14):
    move.l  4(%sp),%a0
    move.l  8(%sp),%d0
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l  %sp,%a3
    sub.l   %d0,%sp
    move.l  %sp,%a1
    lsr.l   #1,%d0
    bra.s   2f
1:  move.w  (%a0)+,(%a1)+
2:  dbra    %d0,1b
    trap    #14
    move.l  %a3,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

/* ------------------------------------------------------------------ */
    .globl  C(xg_vq_gdos)
C(xg_vq_gdos):
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    moveq   #-2,%d0
    trap    #2
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

/* void vdi_trap(void *pb) / void aes_trap(void *pb) */
    .globl  C(vdi_trap)
C(vdi_trap):
    move.l  4(%sp),%d1
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    moveq   #115,%d0
    trap    #2
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .globl  C(aes_trap)
C(aes_trap):
    move.l  4(%sp),%d1
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l  #200,%d0
    trap    #2
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

