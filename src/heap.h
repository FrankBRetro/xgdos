/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* heap.h - XGDOS private heap (memory owned by the resident XGDOS, so it
 * is never freed when a client process exits). Callers must hold the
 * XGDOS lock (see lock.h).
 */
#ifndef HEAP_H
#define HEAP_H
#include "xtypes.h"

void  heap_init(void *base, s32 size);
void *heap_alloc(s32 n);
void *heap_calloc(s32 n);
void  heap_free(void *p);
void *heap_realloc(void *p, s32 n);
void  heap_shrink(void *p, s32 n);     /* keep only the first n bytes */
s32   heap_free_total(void);
s32   heap_largest(void);

#endif
