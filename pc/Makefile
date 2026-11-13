# XGDOS for PC GEM (FreeGEM / OpenGEM on DOS) - 16-bit, Open Watcom
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Francis C Bullen - see LICENSE
#
#   make -C pc            -> pc/XGDOS.EXE (GNU make; Open Watcom v2)
#
# In GEM.BAT, instead of "GEMVDI %1 %2 %3":   XGDOS GEMVDI %1 %2 %3
# with XGDOS.SYS (settings and fonts, as the Atari's ASSIGN.SYS) beside it.
#
# The core (../src, shared with the Atari) is built in the huge memory
# model - its heap and font buffers are bigger than 64K - with XG_INT32,
# which makes int 32-bit in XGDOS's own sources (src/xtypes.h).

OW      ?= $(HOME)/work/vibe/PC-GEM/tools/open-watcom-v2/rel
OWBIN   ?= $(OW)/armo64
WCC      = $(OWBIN)/wcc
export WATCOM  := $(OW)
export INCLUDE := $(OW)/h

# -mh huge model, -3 386 instructions, -s no stack checks (XGDOS runs on its
# own stack), -zu SS isn't DGROUP (called from GEM's stack)
CFLAGS   = -zq -za99 -mh -3 -os -s -zu -w4 -DXG_INT32 -DXG_PC -i=../src
# make LOG=1: XGDOS logs to C:\XGDOS.LOG (make clean first when switching)
ifdef LOG
CFLAGS  += -DXG_LOG
endif

WASM     = $(OWBIN)/wasm
WLINK    = $(OWBIN)/wlink

# not yet: pdf soft mem meta (XGDOS's own printer, memory and metafile
# devices, ~90K) - see pcstubs.c
CORE = xfix sintab charmap spd raster xstd heap config fontmgr gcache \
       text vdi sysfont
PC   = xos_dos pcvdi pcmain pcstubs

COREOBJ = $(addprefix obj/,$(addsuffix .obj,$(CORE)))
PCOBJ   = $(addprefix obj/,$(addsuffix .obj,$(PC))) obj/pcentry.obj

# no C library start-up or stdio: XGDOS stays in memory while GEM runs.
# pcdos.obj (start-up and DOS calls) goes first, as it sets the segment
# order; clibh is still searched, for the compiler's long arithmetic helpers.

XGDOS.EXE: obj/pcdos.obj $(COREOBJ) $(PCOBJ)
	$(WLINK) option quiet format dos libpath $(OW)/lib286 libpath $(OW)/lib286/dos \
	    option map=obj/xgdos.map \
	    name $@ file obj/pcdos.obj $(foreach o,$(COREOBJ) $(PCOBJ),file $(o)) library clibh

core: $(COREOBJ)
	@echo "core compiled: $(words $(COREOBJ)) objects"

obj/%.obj: ../src/%.c ../src/*.h
	@mkdir -p obj
	$(WCC) $(CFLAGS) -fo=$@ $<

obj/%.obj: %.c ../src/*.h pcdos.h
	@mkdir -p obj
	$(WCC) $(CFLAGS) -fo=$@ $<

obj/pcdos.obj: pcdos.asm
	@mkdir -p obj
	$(WASM) -zq -0 -fo=$@ $<

obj/pcentry.obj: pcentry.asm
	@mkdir -p obj
	$(WASM) -zq -0 -fo=$@ $<

# the PC GEM release zip: XGDOS-<version>-PCGEM.zip, with XGDOS.EXE, XGDOS.SYS,
# README.TXT, LICENSE and the Bitstream fonts (build without LOG for it)
VER  = $(shell sed -n 's/.*XGDOS_VSTRING *"\(.*\)".*/\1/p' ../src/xgdos.h)
DIST = dist/XGDOS
dist: XGDOS.EXE
	rm -rf dist XGDOS-$(VER)-PCGEM.zip
	mkdir -p $(DIST)/FONTS
	cp XGDOS.EXE XGDOS.SYS README.TXT $(DIST)/
	cp ../LICENSE $(DIST)/LICENSE.TXT
	cp ../GEMSYS/*.SPD ../GEMSYS/COPYING.TXT $(DIST)/FONTS/
	cd dist && zip -r -X ../XGDOS-$(VER)-PCGEM.zip XGDOS
	@echo "pc/XGDOS-$(VER)-PCGEM.zip ready"

clean:
	rm -rf obj dist

.PHONY: core clean dist
