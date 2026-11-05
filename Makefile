# XGDOS - re-entrant Speedo GDOS replacement for Atari ST/STE/TT/Falcon
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Francis C Bullen - see LICENSE
# Build with the MiNT cross toolchain (works with GNU make 3.81):
#   make            -> XGDOS.PRG, XGDOSLOG.PRG (logs to C:\XGDOS.LOG), XGDOSCFG.PRG
#   make CROSS=m68k-atari-mintelf-     for the newer ELF based toolchain
#   make host-test  -> renders test output on the build machine
#
# Do not add -mshort: XGDOS assumes 32 bit int.

CROSS   ?= m68k-atari-mint-
CC       = $(CROSS)gcc
STRIP    = $(CROSS)strip
PRGFLAGS = $(CROSS)flags

CPU     ?= -m68000
CFLAGS   = $(CPU) -O2 -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-tree-loop-distribute-patterns -fno-strict-aliasing \
           -Wall -Wno-misleading-indentation
ASFLAGS  = $(CPU)
LDFLAGS  = $(CPU) -nostdlib -nostartfiles
LIBS     = -lgcc

CORE = xfix sintab charmap spd raster xstd heap config fontmgr gcache \
       text vdi pdf fnt drv lock xos_tos main soft mem meta sysfont
CFG  = gem xgdoscfg
SHARED_FOR_CFG = spd config xstd xos_tos xfix sintab

OBJ     = build/crt0.o build/atari.o build/traps.o $(addprefix build/,$(addsuffix .o,$(CORE)))
LOGOBJ  = build/crt0.o build/atari.o build/traps.o $(addprefix build/log/,$(addsuffix .o,$(CORE)))
CFGOBJ  = build/crt0app.o build/traps.o $(addprefix build/cfg/,$(addsuffix .o,$(CFG))) \
          $(addprefix build/,$(addsuffix .o,$(SHARED_FOR_CFG)))

all: XGDOS.PRG XGDOSLOG.PRG XGDOSCFG.PRG

XGDOS.PRG: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	-$(STRIP) $@
	-$(PRGFLAGS) --mglobal $@

XGDOSLOG.PRG: $(LOGOBJ)
	$(CC) $(LDFLAGS) -o $@ $(LOGOBJ) $(LIBS)
	-$(PRGFLAGS) --mglobal $@

XGDOSCFG.PRG: $(CFGOBJ)
	$(CC) $(LDFLAGS) -o $@ $(CFGOBJ) $(LIBS)
	-$(STRIP) $@

build/%.o: src/%.c src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/log/%.o: src/%.c src/*.h
	@mkdir -p build/log
	$(CC) $(CFLAGS) -DXG_LOG -c $< -o $@

build/cfg/%.o: cfg/%.c cfg/*.h src/*.h
	@mkdir -p build/cfg
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/crt0app.o: cfg/crt0app.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

# ---- tests on the build machine (needs a native cc; uses the attached
#      fonts copied to host/hostfs/C/FONTS) ----
HOSTSRC = $(addprefix src/,$(addsuffix .c,xfix sintab charmap spd raster xstd heap config fontmgr gcache text vdi pdf soft mem meta sysfont))
host-test:
	cc -O1 -DXG_HOST -w -o host/test_vdi host/test_vdi.c host/xos_host.c host/hostvdi.c $(HOSTSRC)
	cd host && ./test_vdi
	cc -O1 -DXG_HOST -w -o host/test_dev host/test_dev.c host/xos_host.c host/hostvdi.c $(HOSTSRC)
	cd host && ./test_dev

clean:
	rm -rf build XGDOS.PRG XGDOSLOG.PRG XGDOSCFG.PRG host/test_vdi host/test_dev

.PHONY: all clean host-test
