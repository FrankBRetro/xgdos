; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 Francis C Bullen - see LICENSE
;
; pcdos.asm - XGDOS.EXE's start-up code and its DOS calls (wasm, 8086)
;
; XGDOS stays in memory while GEM runs, so it does without the C library:
; this start-up code sets up DGROUP, the stack and BSS, gives the memory
; beyond the program back to DOS, and calls xg_main(). The rest are the
; DOS calls the launcher (pcmain.c) and the DOS layer (xos_dos.c) need.
; All far __cdecl: arguments from [bp+6]; SI, DI, BP, DS and ES kept.
;
; Linked first, so the segment classes come in this order: code, far data,
; DGROUP (data, BSS, stack), and XGEND, which marks the program's end.

        .8086
        name    pcdos

PCDOS_TEXT segment word public 'CODE'
PCDOS_TEXT ends
XGFAR   segment para public 'FAR_DATA'
XGFAR   ends
CONST   segment word public 'DATA'
CONST   ends
CONST2  segment word public 'DATA'
CONST2  ends
_DATA   segment word public 'DATA'
_DATA   ends
_BSS    segment word public 'BSS'
_BSS    ends
_BSSEND segment byte public 'BSS'
_BSSEND ends
STACK   segment para stack 'STACK'
STACK   ends
XGEND   segment para public 'XGEND'
XGEND   ends

DGROUP  group   CONST, CONST2, _DATA, _BSS, _BSSEND, STACK

_DATA   segment
        public  _pc_psp
_pc_psp dw      0                       ; XGDOS's PSP
_DATA   ends

_BSS    segment
bss_start label byte
_BSS    ends
_BSSEND segment
bss_end label   byte
_BSSEND ends

STACK   segment
        db      6144 dup (?)
stack_top label word
STACK   ends

XGEND   segment
prog_end label  byte
XGEND   ends

        extrn   _xg_main:far            ; int __cdecl xg_main(void)

        public  _big_code_
_big_code_ equ  0                       ; the compiler's memory model marker

PCDOS_TEXT segment
        assume  cs:PCDOS_TEXT, ds:DGROUP, es:nothing, ss:nothing

; ---- start-up ---------------------------------------------------------------
_xg_start proc far
        mov     ax, DGROUP
        mov     ds, ax
        mov     _pc_psp, es
        cli
        mov     ss, ax
        mov     sp, offset DGROUP:stack_top
        sti
        mov     bx, seg prog_end        ; give back everything after the program
        sub     bx, _pc_psp
        mov     ah, 4Ah                 ; ES = PSP
        int     21h
        push    ds
        pop     es
        mov     di, offset DGROUP:bss_start
        mov     cx, offset DGROUP:bss_end
        sub     cx, di
        xor     al, al
        cld
        rep     stosb
        call    _xg_main
        mov     ah, 4Ch
        int     21h
_xg_start endp

        public  _pd_open, _pd_creat, _pd_read, _pd_write, _pd_close, _pd_seek
        public  _pd_unlink, _pd_setdta, _pd_findfirst, _pd_findnext
        public  _pd_getdrive, _pd_getcwd, _pd_ax, _pd_bootdrive
        public  _pd_alloc, _pd_free, _pd_exec, _pd_retcode

; int pd_open(const char far *path, int mode): handle, or -1
_pd_open proc far
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, dword ptr [bp+6]
        mov     al, [bp+10]
        mov     ah, 3Dh
        int     21h
        jnc     po_ok
        mov     ax, -1
po_ok:  pop     ds
        pop     bp
        ret
_pd_open endp

; int pd_creat(const char far *path): handle, or -1
_pd_creat proc far
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, dword ptr [bp+6]
        xor     cx, cx
        mov     ah, 3Ch
        int     21h
        jnc     pc_ok
        mov     ax, -1
pc_ok:  pop     ds
        pop     bp
        ret
_pd_creat endp

; unsigned pd_read(int h, void far *buf, unsigned n): bytes, or 0FFFFh
_pd_read proc far
        mov     ah, 3Fh
        push    bp
        mov     bp, sp
        push    ds
        mov     bx, [bp+6]
        lds     dx, dword ptr [bp+8]
        mov     cx, [bp+12]
        int     21h
        jnc     rd_ok
        mov     ax, 0FFFFh
rd_ok:   pop     ds
        pop     bp
        ret
_pd_read endp

; unsigned pd_write(int h, const void far *buf, unsigned n)
_pd_write proc far
        mov     ah, 40h
        push    bp
        mov     bp, sp
        push    ds
        mov     bx, [bp+6]
        lds     dx, dword ptr [bp+8]
        mov     cx, [bp+12]
        int     21h
        jnc     wr_ok
        mov     ax, 0FFFFh
wr_ok:   pop     ds
        pop     bp
        ret
_pd_write endp

; void pd_close(int h)
_pd_close proc far
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ah, 3Eh
        int     21h
        pop     bp
        ret
_pd_close endp

; long pd_seek(int h, long off, int whence): new position, or -1
_pd_seek proc far
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     dx, [bp+8]
        mov     cx, [bp+10]
        mov     al, [bp+12]
        mov     ah, 42h
        int     21h
        jnc     ps_ok
        mov     ax, -1
        mov     dx, ax
ps_ok:  pop     bp
        ret
_pd_seek endp

; int pd_unlink(const char far *path): 0, or -1
_pd_unlink proc far
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, dword ptr [bp+6]
        mov     ah, 41h
        int     21h
        mov     ax, 0
        jnc     pu_ok
        dec     ax
pu_ok:  pop     ds
        pop     bp
        ret
_pd_unlink endp

; void pd_setdta(void far *dta)
_pd_setdta proc far
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, dword ptr [bp+6]
        mov     ah, 1Ah
        int     21h
        pop     ds
        pop     bp
        ret
_pd_setdta endp

; int pd_findfirst(const char far *pattern, int attr): 0 found, -1 not
_pd_findfirst proc far
        push    bp
        mov     bp, sp
        push    ds
        lds     dx, dword ptr [bp+6]
        mov     cx, [bp+10]
        mov     ah, 4Eh
        int     21h
        mov     ax, 0
        jnc     ff_ok
        dec     ax
ff_ok:  pop     ds
        pop     bp
        ret
_pd_findfirst endp

; int pd_findnext(void): 0 found, -1 not
_pd_findnext proc far
        mov     ah, 4Fh
        int     21h
        mov     ax, 0
        jnc     fn_ok
        dec     ax
fn_ok:  ret
_pd_findnext endp

; int pd_getdrive(void): 0 = A:
_pd_getdrive proc far
        mov     ah, 19h
        int     21h
        xor     ah, ah
        ret
_pd_getdrive endp

; int pd_getcwd(int drive, char far *buf): drive 0 = current; 0, or -1.
; The path comes back without drive or leading backslash (64 bytes).
_pd_getcwd proc far
        push    bp
        mov     bp, sp
        push    si
        push    ds
        mov     dl, [bp+6]
        lds     si, dword ptr [bp+8]
        mov     ah, 47h
        int     21h
        mov     ax, 0
        jnc     gc_ok
        dec     ax
gc_ok:  pop     ds
        pop     si
        pop     bp
        ret
_pd_getcwd endp

; unsigned pd_ax(unsigned ax, unsigned bx): INT 21h with AX and BX, AX back
; (the UMB link and allocation strategy calls, 58xxh)
_pd_ax proc far
        push    bp
        mov     bp, sp
        mov     ax, [bp+6]
        mov     bx, [bp+8]
        int     21h
        pop     bp
        ret
_pd_ax endp

; int pd_bootdrive(void): DOS 4+ boot drive, 1 = A:, 0 unknown
_pd_bootdrive proc far
        mov     ax, 3305h
        xor     dl, dl
        int     21h
        mov     al, dl
        xor     ah, ah
        ret
_pd_bootdrive endp

; unsigned pd_alloc(unsigned paras, unsigned far *largest): segment, or 0
; with the largest free block in *largest
_pd_alloc proc far
        push    bp
        mov     bp, sp
        push    di
        push    es
        mov     bx, [bp+6]
        mov     ah, 48h
        int     21h
        jnc     pa_ok
        les     di, dword ptr [bp+8]
        mov     ax, es
        or      ax, di
        jz      pa_none
        mov     es:[di], bx
pa_none:
        xor     ax, ax
pa_ok:  pop     es
        pop     di
        pop     bp
        ret
_pd_alloc endp

; void pd_free(unsigned seg)
_pd_free proc far
        push    bp
        mov     bp, sp
        push    es
        mov     es, [bp+6]
        mov     ah, 49h
        int     21h
        pop     es
        pop     bp
        ret
_pd_free endp

; int pd_exec(const char far *path, void far *params): 0, or the DOS error.
; Older DOSes keep only CS:IP across EXEC: everything else is put back here.
ex_ss   dw      0
ex_sp   dw      0
_pd_exec proc far
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        lds     dx, dword ptr [bp+6]
        les     bx, dword ptr [bp+10]
        mov     cs:ex_ss, ss
        mov     cs:ex_sp, sp
        mov     ax, 4B00h
        int     21h
        cli
        mov     ss, cs:ex_ss
        mov     sp, cs:ex_sp
        sti
        jc      ex_err
        xor     ax, ax
ex_err: pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        cld
        ret
_pd_exec endp

; int pd_retcode(void): the last child's exit code
_pd_retcode proc far
        mov     ah, 4Dh
        int     21h
        xor     ah, ah
        ret
_pd_retcode endp

PCDOS_TEXT ends
        end     _xg_start
