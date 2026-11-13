; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 Francis C Bullen - see LICENSE
;
; pcentry.asm - XGDOS's entry points on PC GEM (Open Watcom wasm, 8086)
;
; INT EFh   GEM's VDI/GDOS vector. VDI calls (CX = 0473h) go first to
;           pc_need() on the caller's stack; XGDOS's own are then done by
;           pc_vdi() on XGDOS's stack, the rest go on to GEMVDI untouched.
;           Other INT EFh calls (the AES's) always go straight on.
; INT 21h   watched only for EXEC (4B00h): GEMVDI writes its INT EFh vector
;           straight into memory and then EXECs GEM.EXE, so when an EXEC
;           finds GEMVDI's entry ('GEMVDI23' after its first jump) on the
;           vector, XGDOS goes in front of it.
;
; The entry starts with a short jump and GEMVDI's own 'GEMVDI23',0 (GEM.EXE
; won't start without it), then 'XGDOS',0 at entry+11 so a program can see
; XGDOS is there.

        .8086
        name    pcentry

DGROUP  group   _DATA
_DATA   segment word public 'DATA'
_DATA   ends

        extrn   _pc_need:far            ; short __cdecl pc_need(VDIPB far *)
        extrn   _pc_vdi:far             ; void  __cdecl pc_vdi(VDIPB far *)

; XGDOS's own stack and the per-call state, in a segment of their own: kept
; out of the code segment, as writes next to running code look like
; self-modifying code to an emulator's recompiler (DOSBox-X's dynamic core)
XGSTACK segment para public 'FAR_DATA'
depth           dw      0
save_ss         dw      0
save_sp         dw      0
        even
xstack          db      8192 dup (0)
stack_top       label   word
XGSTACK ends

PCENTRY_TEXT segment para public 'CODE'
        assume  cs:PCENTRY_TEXT, ds:nothing, es:nothing, ss:nothing

        public  _pc_ef_entry, _pc_rom_call, _pc_hook21, _pc_unhook21, _pc_gemvdi_seen

old_ef          dd      0
old_21          dd      0
_pc_gemvdi_seen dw      0               ; times XGDOS has gone in front of GEMVDI

; ---- INT EFh ----------------------------------------------------------------
_pc_ef_entry proc far
        jmp     short ef_go
        db      'GEMVDI23', 0           ; GEM.EXE checks for GEMVDI's signature here
        db      'XGDOS', 0              ; at entry+11: XGDOS is in front
        dw      0102h                   ; version (XGDOS_VERSION)
ef_go:
        cmp     cx, 0473h
        je      ef_vdi
        jmp     dword ptr cs:old_ef     ; the AES and the rest: GEMVDI's

ef_vdi:
        push    ax
        push    bx
        push    cx
        push    dx                      ; parameter block offset
        push    si
        push    di
        push    bp
        push    ds                      ; parameter block segment
        push    es
        mov     si, ds                  ; SI:DI = parameter block
        mov     di, dx
        ; interrupts off from the depth test until depth is counted, and
        ; again from the uncount to the return: a VDI call from an interrupt
        ; in between would take the stack, and save_ss/save_sp, from under us
        cli
        mov     ax, XGSTACK
        mov     es, ax                  ; ES = XGDOS's state (GEM's ES is saved)
        cmp     es:depth, 0
        jne     ef_nested
        ; onto XGDOS's own stack for all of it, pc_need included: GEM's
        ; stack may be small
        mov     es:save_ss, ss
        mov     es:save_sp, sp
        mov     ss, ax
        mov     sp, offset stack_top
        inc     es:depth
        sti
        mov     ax, DGROUP
        mov     ds, ax
        push    si
        push    di
        call    _pc_need
        or      ax, ax
        jz      ef_back                 ; (pb still on the stack)
        call    _pc_vdi
        mov     al, 1
ef_back:
        add     sp, 4
        mov     bl, al                  ; ours? (BX is restored from GEM's stack)
        mov     ax, XGSTACK
        mov     es, ax
        cli                             ; stays off: iret or GEMVDI restores flags
        dec     es:depth
        mov     ss, es:save_ss
        mov     sp, es:save_sp
        or      bl, bl
        jnz     ef_done
ef_chain:                               ; not ours: on to GEMVDI as it came
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        jmp     dword ptr cs:old_ef
ef_nested:                              ; from inside XGDOS: already on its stack
        sti
        mov     ax, DGROUP
        mov     ds, ax
        push    si
        push    di
        call    _pc_need
        or      ax, ax
        jz      ef_nest_no
        call    _pc_vdi
        add     sp, 4
        jmp     short ef_done
ef_nest_no:
        add     sp, 4
        jmp     short ef_chain
ef_done:
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret
_pc_ef_entry endp

; ---- void __cdecl pc_rom_call(VDIPB far *pb): a VDI call to GEMVDI ---------
_pc_rom_call proc far
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        lds     dx, dword ptr [bp+6]
        mov     cx, 0473h
        pushf
        call    dword ptr cs:old_ef
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_pc_rom_call endp

; ---- INT 21h: watch for GEMVDI EXECing GEM.EXE ------------------------------
i21_entry proc far
        cmp     ax, 4B00h
        jne     i21_chain
        push    ax
        push    bx
        push    es
        xor     ax, ax
        mov     es, ax
        les     bx, dword ptr es:[03BCh]        ; INT EFh's vector
        mov     ax, es                          ; already XGDOS (GEM EXECing a program)?
        push    bx
        mov     bx, cs
        cmp     ax, bx
        pop     bx                              ; (flags kept)
        je      i21_pop
        cmp     word ptr es:[bx+2], 4547h       ; 'GE'
        jne     i21_pop
        cmp     word ptr es:[bx+4], 564Dh       ; 'MV'
        jne     i21_pop
        cmp     word ptr es:[bx+6], 4944h       ; 'DI'
        jne     i21_pop
        cli
        mov     word ptr cs:old_ef, bx
        mov     word ptr cs:old_ef+2, es
        xor     ax, ax
        mov     es, ax
        mov     word ptr es:[03BCh], offset _pc_ef_entry
        mov     word ptr es:[03BEh], cs
        sti
        inc     cs:_pc_gemvdi_seen
i21_pop:
        pop     es
        pop     bx
        pop     ax
i21_chain:
        jmp     dword ptr cs:old_21
i21_entry endp

; ---- void __cdecl pc_hook21(void) / pc_unhook21(void) -----------------------
_pc_hook21 proc far
        push    es
        xor     ax, ax
        mov     es, ax
        cli
        mov     ax, es:[0084h]
        mov     word ptr cs:old_21, ax
        mov     ax, es:[0086h]
        mov     word ptr cs:old_21+2, ax
        mov     word ptr es:[0084h], offset i21_entry
        mov     es:[0086h], cs
        sti
        pop     es
        ret
_pc_hook21 endp

_pc_unhook21 proc far
        push    es
        xor     ax, ax
        mov     es, ax
        cli
        mov     ax, cs
        cmp     es:[0086h], ax                  ; still ours? then put it back
        jne     unhook_done
        mov     ax, word ptr cs:old_21
        mov     es:[0084h], ax
        mov     ax, word ptr cs:old_21+2
        mov     es:[0086h], ax
unhook_done:
        sti
        pop     es
        ret
_pc_unhook21 endp

PCENTRY_TEXT ends
        end
