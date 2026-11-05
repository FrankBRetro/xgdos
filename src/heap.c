/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* heap.c - first fit allocator with an address ordered free list */
#include "heap.h"
#include "xstd.h"

typedef struct Blk {
    s32 size;            /* total block size including header */
    struct Blk *next;    /* next free block (free blocks only) */
} Blk;

#define HDR   ((s32)((sizeof(Blk) + 7) & ~7))
#define ALIGN(n) (((n) + 7) & ~7)

static Blk *freelist;

void heap_init(void *base, s32 size)
{
    Blk *b = (Blk *)(((unsigned long)base + 7) & ~7UL);
    size -= (s32)((u8 *)b - (u8 *)base);
    size &= ~7;
    if (size < 64) { freelist = NULL; return; }
    b->size = size;
    b->next = NULL;
    freelist = b;
}

void *heap_alloc(s32 n)
{
    Blk **pp, *b;
    if (n <= 0) n = 1;
    n = ALIGN(n + HDR);
    for (pp = &freelist; (b = *pp) != NULL; pp = &b->next) {
        if (b->size >= n) {
            if (b->size - n >= 32) {
                Blk *r = (Blk *)((u8 *)b + n);
                r->size = b->size - n;
                r->next = b->next;
                *pp = r;
                b->size = n;
            } else {
                *pp = b->next;
            }
            b->next = (Blk *)0x58474453;   /* 'XGDS' marks used blocks */
            return (u8 *)b + HDR;
        }
    }
    return NULL;
}

void *heap_calloc(s32 n)
{
    void *p = heap_alloc(n);
    if (p) xmemset(p, 0, n);
    return p;
}

void heap_free(void *p)
{
    Blk *b, **pp, *f;
    if (!p) return;
    b = (Blk *)((u8 *)p - HDR);
    if (b->next != (Blk *)0x58474453) return;   /* not ours / double free */
    for (pp = &freelist; (f = *pp) != NULL && f < b; pp = &f->next) ;
    b->next = f;
    *pp = b;
    /* merge with following block */
    if (f && (u8 *)b + b->size == (u8 *)f) { b->size += f->size; b->next = f->next; }
    /* merge with previous block */
    if (pp != &freelist) {
        Blk *prev = (Blk *)((u8 *)pp - ((unsigned long)&((Blk *)0)->next));
        if ((u8 *)prev + prev->size == (u8 *)b) { prev->size += b->size; prev->next = b->next; }
    }
}

void *heap_realloc(void *p, s32 n)
{
    Blk *b;
    void *q;
    s32 old;
    if (!p) return heap_alloc(n);
    b = (Blk *)((u8 *)p - HDR);
    old = b->size - HDR;
    if (old >= n) return p;
    q = heap_alloc(n);
    if (!q) return NULL;
    xmemcpy(q, p, old);
    heap_free(p);
    return q;
}

/* give the end of a block back to the heap; p stays where it is */
void heap_shrink(void *p, s32 n)
{
    Blk *b, *t;
    s32 nn;
    if (!p) return;
    b = (Blk *)((u8 *)p - HDR);
    if (b->next != (Blk *)0x58474453) return;
    nn = ALIGN((n > 0 ? n : 1) + HDR);
    if (b->size - nn < 32) return;
    t = (Blk *)((u8 *)b + nn);
    t->size = b->size - nn;
    t->next = (Blk *)0x58474453;
    b->size = nn;
    heap_free((u8 *)t + HDR);
}

s32 heap_free_total(void)
{
    Blk *b; s32 t = 0;
    for (b = freelist; b; b = b->next) t += b->size - HDR;
    return t;
}

s32 heap_largest(void)
{
    Blk *b; s32 t = 0;
    for (b = freelist; b; b = b->next) if (b->size - HDR > t) t = b->size - HDR;
    return t;
}
