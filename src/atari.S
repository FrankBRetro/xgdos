/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* atari.S - XGDOS low level glue (68000, Motorola syntax with % registers)
 *
 * xg_trap2      trap #2 handler (XBRA 'XGDS'). d0 = 115: VDI, d0 = -2:
 *               vq_gdos (answers '_FSM' like SpeedoGDOS), else pass on.
 *               xg_vdi_need() (tiny, on the caller's stack) decides first;
 *               calls XGDOS does not need go to the previous VDI with the
 *               original exception frame and registers. The others run on
 *               a private stack taken from a pool, so several processes can
 *               be inside XGDOS at once (MiNT).
 * rom_vdi       call the previous trap #2 handler with a fake exception
 *               frame (format word on 68010+ via _longframe).
 * drv_jump      enter an external GDOS driver.
 * xg_resvec     reset handler (only when XGDOS created the cookie jar).
 * The OS trap wrappers live in traps.S.
 */

#ifdef __ELF__
#define C(x) x
#else
#define C(x) _##x
#endif

    .text

/* ------------------------------------------------------------------ */
    .globl  C(xg_trap2)
    .globl  C(xg_old_trap2)
    .even
    .long   0x58425241          /* 'XBRA' */
    .long   0x58474453          /* 'XGDS' */
C(xg_old_trap2):
    .long   0
C(xg_trap2):
    tst.b   C(xg_bypass)        /* MODE = BYPASS: pass everything on */
    bne.s   pass_on
    cmp.w   #115,%d0
    beq.s   vdi_call
    cmp.w   #-2,%d0
    beq.s   gdos_query
pass_on:
    move.l  C(xg_old_trap2),-(%sp)
    rts

gdos_query:
    tst.b   C(xg_hide_gdos)     /* MODE = NOGDOS */
    bne.s   pass_on
    move.l  #0x5F46534D,%d0     /* '_FSM' */
    rte

vdi_call:
    movem.l %d1-%d7/%a0-%a6,-(%sp)
    move.l  %d1,-(%sp)          /* does XGDOS need this call? */
    jsr     C(xg_vdi_need)
    addq.l  #4,%sp
    tst.l   %d0
    bne.s   vdi_ours
    movem.l (%sp)+,%d1-%d7/%a0-%a6
    moveq   #115,%d0            /* untouched: original frame and registers */
    move.l  C(xg_old_trap2),-(%sp)
    rts

vdi_ours:
    move.l  %sp,%a6             /* caller's supervisor stack */
    move.l  (%sp),%d5           /* VDIPB (saved d1) */
    lea     C(xg_slot_busy),%a0
    move.w  C(xg_nslots),%d2
    moveq   #0,%d3
find_slot:
    cmp.w   %d2,%d3
    bge.s   no_slot
    tas     0(%a0,%d3.w)
    beq.s   got_slot
    addq.w  #1,%d3
    bra.s   find_slot

no_slot:                        /* run on the caller's stack */
    move.l  %d5,-(%sp)
    jsr     C(xg_vdi)
    addq.l  #4,%sp
    bra.s   vdi_done

got_slot:
    move.w  %d3,%d4
    add.w   %d3,%d3
    add.w   %d3,%d3
    lea     C(xg_slot_top),%a1
    move.l  0(%a1,%d3.w),%sp
    move.l  %a6,-(%sp)          /* old stack pointer */
    move.w  %d4,-(%sp)          /* slot number */
    move.l  %d5,-(%sp)
    jsr     C(xg_vdi)
    addq.l  #4,%sp
    move.w  (%sp)+,%d4
    move.l  (%sp)+,%a6
    move.l  %a6,%sp
    lea     C(xg_slot_busy),%a0
    clr.b   0(%a0,%d4.w)
vdi_done:
    movem.l (%sp)+,%d1-%d7/%a0-%a6
    moveq   #115,%d0
    rte

/* ------------------------------------------------------------------ */
    .data
    .globl  C(xg_bypass)
    .globl  C(xg_hide_gdos)
C(xg_bypass):
    .byte   0
C(xg_hide_gdos):
    .byte   0
    .even
    .text

/* ------------------------------------------------------------------ */
    .globl  C(rom_vdi)
C(rom_vdi):
    move.l  4(%sp),%d1
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    moveq   #115,%d0
    tst.w   0x59e               /* _longframe: 68010+ stack frames */
    beq.s   1f
    move.w  #0x0088,-(%sp)      /* format 0, vector offset of trap #2 */
1:  pea     rom_back(%pc)
    move.w  %sr,-(%sp)
    move.l  C(xg_old_trap2),-(%sp)
    rts
rom_back:
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

/* long drv_jump(void *entry, void *pb): call an external GDOS driver */
    .globl  C(drv_jump)
C(drv_jump):
    move.l  4(%sp),%a0
    move.l  8(%sp),%d1
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    moveq   #115,%d0
    jsr     (%a0)
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

/* ------------------------------------------------------------------ */
/* reset handler: only installed when XGDOS had to create the cookie
 * jar (TOS < 1.06 without MiNT); clears _p_cookies on a warm reset.   */
    .globl  C(xg_resvec)
    .globl  C(xg_old_resvec)
    .globl  C(xg_old_resvalid)
    .even
C(xg_old_resvalid):
    .long   0
    .long   0x58425241
    .long   0x58474453
C(xg_old_resvec):
    .long   0
C(xg_resvec):
    clr.l   0x5a0
    move.l  C(xg_old_resvalid),0x426
    move.l  C(xg_old_resvec),0x42a
    jmp     (%a6)
