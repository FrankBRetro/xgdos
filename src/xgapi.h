/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* xgapi.h - public interface of the resident XGDOS ('XGDS' cookie).
 * Used by XGDOSCFG.PRG; other programs may use it as well.
 */
#ifndef XGAPI_H
#define XGAPI_H
#include "xtypes.h"

#define XGDS_COOKIE 0x58474453L   /* 'XGDS' */

typedef struct XgApi {
    s32  magic;                  /* 'XGDS' */
    s16  version;                /* 0x0100 */
    s16  nfonts;                 /* entries in the font table */
    s32  (*reload)(void);        /* re-read ASSIGN.SYS; call via Supexec() */
    void *cfg;                   /* XgConfig * (read only) */
    void *fonts;                 /* XFont * (read only) */
    s32  heap_size;
    s32  (*heap_free)(void);     /* call via Supexec() */
    char assign_path[128];       /* the ASSIGN.SYS that was read */
} XgApi;

#endif
