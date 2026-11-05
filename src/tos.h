/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* tos.h - minimal GEMDOS/BIOS/XBIOS/MiNT bindings without a C library.
 * Arguments are packed into a block that atari.S copies onto the stack.
 */
#ifndef TOS_H
#define TOS_H
#include "xtypes.h"

#define PK __attribute__((packed))

s32 xg_trap1(const void *args, s32 nbytes);
s32 xg_trap13(const void *args, s32 nbytes);
s32 xg_trap14(const void *args, s32 nbytes);

typedef struct BASEPAGE {
    void *p_lowtpa, *p_hitpa;
    void *p_tbase; s32 p_tlen;
    void *p_dbase; s32 p_dlen;
    void *p_bbase; s32 p_blen;
    void *p_dta, *p_parent;
    s32 p_reserved;
    char *p_env;
    char p_stdfh[6], p_uft[2];
    s32 p_reserved2[19];
    char p_cmdlin[128];
} BASEPAGE;

static inline s32 Cconws(const char *s) { struct PK { s16 op; const char *s; } a = { 0x09, s }; return xg_trap1(&a, sizeof a); }
static inline s32 Fsetdta(void *d) { struct PK { s16 op; void *d; } a = { 0x1a, d }; return xg_trap1(&a, sizeof a); }
static inline void *Fgetdta(void) { struct PK { s16 op; } a = { 0x2f }; return (void *)xg_trap1(&a, sizeof a); }
static inline s32 Super(void *s) { struct PK { s16 op; void *s; } a = { 0x20, s }; return xg_trap1(&a, sizeof a); }
static inline s32 Sversion(void) { struct PK { s16 op; } a = { 0x30 }; return xg_trap1(&a, sizeof a); }
static inline s32 Ptermres(s32 keep, s16 rc) { struct PK { s16 op; s32 k; s16 rc; } a = { 0x31, keep, rc }; return xg_trap1(&a, sizeof a); }
static inline s32 Dgetdrv(void) { struct PK { s16 op; } a = { 0x19 }; return xg_trap1(&a, sizeof a); }
static inline s32 Dgetpath(char *b, s16 d) { struct PK { s16 op; char *b; s16 d; } a = { 0x47, b, d }; return xg_trap1(&a, sizeof a); }
static inline s32 Fcreate(const char *n, s16 at) { struct PK { s16 op; const char *n; s16 at; } a = { 0x3c, n, at }; return xg_trap1(&a, sizeof a); }
static inline s32 Fopen(const char *n, s16 m) { struct PK { s16 op; const char *n; s16 m; } a = { 0x3d, n, m }; return xg_trap1(&a, sizeof a); }
static inline s32 Fclose(s16 h) { struct PK { s16 op; s16 h; } a = { 0x3e, h }; return xg_trap1(&a, sizeof a); }
static inline s32 Fread(s16 h, s32 n, void *b) { struct PK { s16 op; s16 h; s32 n; void *b; } a = { 0x3f, h, n, b }; return xg_trap1(&a, sizeof a); }
static inline s32 Fwrite(s16 h, s32 n, const void *b) { struct PK { s16 op; s16 h; s32 n; const void *b; } a = { 0x40, h, n, b }; return xg_trap1(&a, sizeof a); }
static inline s32 Fdelete(const char *n) { struct PK { s16 op; const char *n; } a = { 0x41, n }; return xg_trap1(&a, sizeof a); }
static inline s32 Fseek(s32 off, s16 h, s16 m) { struct PK { s16 op; s32 off; s16 h; s16 m; } a = { 0x42, off, h, m }; return xg_trap1(&a, sizeof a); }
static inline s32 Mxalloc(s32 n, s16 m) { struct PK { s16 op; s32 n; s16 m; } a = { 0x44, n, m }; return xg_trap1(&a, sizeof a); }
static inline s32 Malloc(s32 n) { struct PK { s16 op; s32 n; } a = { 0x48, n }; return xg_trap1(&a, sizeof a); }
static inline s32 Mfree(void *p) { struct PK { s16 op; void *p; } a = { 0x49, p }; return xg_trap1(&a, sizeof a); }
static inline s32 Mshrink(void *p, s32 n) { struct PK { s16 op; s16 z; void *p; s32 n; } a = { 0x4a, 0, p, n }; return xg_trap1(&a, sizeof a); }
static inline s32 Pexec(s16 m, const char *n, const char *c, const char *e) { struct PK { s16 op; s16 m; const char *n, *c, *e; } a = { 0x4b, m, n, c, e }; return xg_trap1(&a, sizeof a); }
static inline s32 Pterm(s16 rc) { struct PK { s16 op; s16 rc; } a = { 0x4c, rc }; return xg_trap1(&a, sizeof a); }
static inline s32 Fsfirst(const char *n, s16 at) { struct PK { s16 op; const char *n; s16 at; } a = { 0x4e, n, at }; return xg_trap1(&a, sizeof a); }
static inline s32 Fsnext(void) { struct PK { s16 op; } a = { 0x4f }; return xg_trap1(&a, sizeof a); }
/* MiNT */
static inline s32 Syield(void) { struct PK { s16 op; } a = { 0xff }; return xg_trap1(&a, sizeof a); }
static inline s32 Pgetpid(void) { struct PK { s16 op; } a = { 0x10b }; return xg_trap1(&a, sizeof a); }
static inline s32 Pkill(s16 pid, s16 sig) { struct PK { s16 op; s16 pid; s16 sig; } a = { 0x111, pid, sig }; return xg_trap1(&a, sizeof a); }
/* BIOS / XBIOS */
static inline void *Setexc(s16 v, void *f) { struct PK { s16 op; s16 v; void *f; } a = { 5, v, f }; return (void *)xg_trap13(&a, sizeof a); }
static inline s32 Supexec(s32 (*f)(void)) { struct PK { s16 op; s32 (*f)(void); } a = { 38, f }; return xg_trap14(&a, sizeof a); }

/* xos_tos.c helpers */
extern int xg_mint;
int  cookie_get(u32 id, u32 *val);         /* uses Supexec */
s32  xg_peekl(u32 addr);                   /* supervisor peek */
s16  xg_peekw(u32 addr);

#endif
