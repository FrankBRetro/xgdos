/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
/* pcstubs.c - XGDOS's own devices (PDF printer, metafile, memory) aren't
 * in the PC build yet: they'd cost about 90K of conventional memory. GEM's
 * own drivers do printing meanwhile (pcvdi.c leaves workstation opens to
 * GEM). These keep the VDI layer's calls to them harmless.
 */
#include "vdi.h"
#include "text.h"
#undef int

void pdf_open(Wk *w, VDIPB *pb) { (void)w; pb->contrl[6] = 0; }
void pdf_vdi(Wk *w, VDIPB *pb) { (void)w; (void)pb; }
long pdf_builtin_metrics(Wk *w, struct TxtMetrics *m) { (void)w; (void)m; return 0; }
void meta_open(Wk *w, VDIPB *pb) { (void)w; pb->contrl[6] = 0; }
void meta_vdi(Wk *w, VDIPB *pb) { (void)w; (void)pb; }
void mem_open(Wk *w, VDIPB *pb) { (void)w; pb->contrl[6] = 0; }
long mem_open_virtual(Wk *phys, VDIPB *pb) { (void)phys; (void)pb; return 0; }
void mem_vdi(Wk *w, VDIPB *pb) { (void)w; (void)pb; }
