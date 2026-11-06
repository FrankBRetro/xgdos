# XGDOS 1.00 – re-entrant Speedo GDOS replacement

XGDOS replaces SpeedoGDOS on the Atari ST/STE/TT/Falcon. It renders
Bitstream Speedo (`.SPD`) outline fonts at any size, prints to numbered
PDF files, and is safe for several processes at once under MiNT/MultiTOS.

| File | Purpose |
|---|---|
| `XGDOS.PRG` | the resident GDOS – put it in `\AUTO` (after MiNT if you use it) |
| `XGDOSLOG.PRG` | same, but logs font loading and printing to `C:\XGDOS.LOG` |
| `XGDOSCFG.PRG` | preferences (GEM program; rename to `.ACC` for an accessory) |
| `ASSIGN.SYS` | default configuration – goes in the root of the boot drive |
| `GEMSYS\` | the Speedo font folder, with eight free Bitstream fonts (Charter, Courier 10 Pitch) – see below |
| `DRIVERS\` | the GDOS driver folder (`PATH`); XGDOS needs nothing in it |

The project is laid out like the boot drive it installs to: `ASSIGN.SYS`
in the root, fonts in `C:\GEMSYS`, drivers in `C:\DRIVERS`.

## Building

```
make                       # m68k-atari-mint-gcc, GNU make 3.81 is fine
make CROSS=m68k-atari-mintelf-
make host-test             # renders a test screen + PDFs on the build machine
```

No C library is used (own startup, OS bindings and `memcpy`), only `-lgcc`.
Do not build with `-mshort`. The Makefile passes
`-fno-tree-loop-distribute-patterns`; keep it, or GCC may turn the copy
loop inside `memcpy` into a call to itself.

`host-test` expects the fonts in `host/hostfs/C/FONTS/` and an `ASSIGN.SYS`
in `host/hostfs/C/` (the host build maps `C:\` there).

## Installing

1. Remove SpeedoGDOS/NVDI from `\AUTO`. XGDOS will not load over another GDOS.
2. Copy `XGDOS.PRG` to `C:\AUTO`, and `ASSIGN.SYS`, `GEMSYS\` and
   `DRIVERS\` to the root of `C:`. The default ASSIGN.SYS enables the
   eight free fonts in `GEMSYS\`; add your own `.SPD` files there too.
3. Run `XGDOSCFG.PRG` to enable your own fonts, add more font folders or
   change the PDF folder. If you keep your old ASSIGN.SYS instead, just run
   XGDOSCFG once and it adds the XGDOS block.
   XGDOS reads `X:\ASSIGN.SYS`, then `X:\GEMSYS\ASSIGN.SYS`, then
   `C:\ASSIGN.SYS` (X = boot drive). The default paths assume drive C:;
   change them in XGDOSCFG if you boot from another drive.

## ASSIGN.SYS

Classic lines (`PATH`, device lines such as `21 STYLUS.SYS`, bitmap `.FNT`
names under a device) keep their meaning. XGDOS adds:

```
FONTPATH = C:\GEMSYS    Speedo font folder (default: PATH); repeat the
FONTPATH = C:\DRIVERS   line for up to 4 folders, searched in order
PDFPATH  = C:\PRINTS    where printouts go (default: root of boot drive)
PDFNAME  = PRN          PRN0001.PDF, PRN0002.PDF ... (next free number)
PDFDPI   = 600          resolution reported to applications (72-2400)
PAPER    = A4           A4 / A5 / LETTER / LEGAL
CACHE    = 48           glyph bitmap cache, KB
HEAP     = 192          XGDOS memory, KB (fonts load into it on demand)
STACKS   = 0            concurrent VDI calls; 0 = 4 under MiNT, 2 under TOS
HINTING  = ON           even stem weights, true shapes; FULL fits every stem; OFF
PRINTER  = PDF          PDF, or DRIVER to use the .SYS printer drivers
FSMC     = OFF          ON adds the SpeedoGDOS 'FSMC' cookie (see below)
MODE     = FULL         diagnostics: NOGDOS, NOFSMC or BYPASS
+BX000043.SPD           enabled font
-BX000044.SPD           disabled font (kept so XGDOSCFG can re-enable it)
+D:\FONTS\X.SPD 5050    full path and optional VDI font ID
```

A font given by bare name is looked for in each font folder in turn.
Several folders can also go on one line, separated by `;`. If there are no
`+`/`-` lines, every `*.SPD` in the font folders is enabled. Without a
FONTPATH line the font folder is PATH, which is `C:\DRIVERS` in your
current ASSIGN.SYS.

## XGDOSCFG

This is a non-modal window that works under MultiTOS and as an accessory:

* Font list: click the box (or Space, or a double-click) to enable or
  disable a font. Fonts found in the folder but not yet listed show "new".
* **Add font...** picks an `.SPD` anywhere. Fonts outside the font folders
  are stored with their full path.
* **Font folder** rows (up to four): click a folder to change it or remove
  it, or click the row below the last one to add another. The font list
  is checked again straight away, so fonts that were "file not found"
  turn black once their folder is listed, and new fonts show up.
* **PDF folder** picks the folder printouts are written to.
* **Remove** removes the font from the list (the file is not deleted).
* Settings rows change when clicked: folders open a file selector, and the
  others cycle through their values.
* **Save & Apply** (Ctrl-S) rewrites ASSIGN.SYS, keeping every classic
  line and saving the old file as `ASSIGN.BAK`, then tells a running XGDOS
  to reload. Applications see the new font list on their next
  `vst_load_fonts`. HEAP and STACKS changes need a reboot.

## Printing

Every print job gets its own file, the next free `PRNnnnn.PDF`. The file
is created when the job prints something, so a program that opens the
printer only to ask about it (its page size, say) leaves no empty PDF
behind and uses up no number. Applications see a 600 dpi (PDFDPI) page, so
programs that lay out at device resolution get the higher resolution
automatically. Output is vector throughout:

* Speedo text is embedded as glyph outlines (each glyph stored once per
  file), so it stays sharp at any zoom or printer resolution.
* The system font prints as PDF Courier (Atari umlauts mapped).
* Lines, fills, arcs, circles, ellipses, rounded boxes and markers become
  PDF paths. Patterns are approximated as tints.
* `vro_cpyfm`/`vrt_cpyfm` from memory and `v_bit_image` (GEM `.IMG`) become
  embedded images.
* Printing is in colour: 256 colour indices, set with `vs_color`. Colour
  bitmaps (2, 4 or 8 planes) keep their colours. A colour `.IMG` prints
  with its own palette when the file has an `XIMG` header; without one,
  its pixel values are treated as ST colour registers and printed with the
  workstation's palette. `.IMG` files with more than 8 planes print in
  black and white.
* `v_clrwk` / `v_form_adv` start a new page, and `v_clswk` finishes the file.

## Devices

XGDOS runs stand-alone: it needs no SpeedoGDOS file and no `.SYS` driver.
Every device it supports is built in, so the driver names on the device
lines of ASSIGN.SYS are ignored for these:

| Device | ASSIGN.SYS line (yours) | What XGDOS does |
|---|---|---|
| 1–10 screen | `1P SCREEN.SYS` … | the TOS VDI draws; XGDOS adds Speedo text |
| 21–30 printer | `21 STYLUS.SYS` | numbered PDF files (`PRINTER = PDF`, the default) |
| 31–40 metafile | `31 META.SYS` | XGDOS's own GEM metafile writer |
| 61–70 memory | `61 MEMORY.SYS` | XGDOS's own off-screen bitmap renderer |

Other device numbers (plotters, cameras, tablets) and `PRINTER = DRIVER`
still load the `.SYS` file named in ASSIGN.SYS, for anyone who has one.

### Metafile (31–40)

`v_opnwk` creates `GEMFILE.GEM` in the program's current folder;
`vm_filename` changes the name (as with META.SYS, the program deletes
GEMFILE.GEM itself). Drawing and attribute calls are recorded as GEM
metafile items in the standard little-endian format; queries are answered
by XGDOS and not recorded. `v_meta_extents`, `vm_pagesize`, `vm_coords` and
`v_write_meta` work as documented, and `v_clswk` writes the header with the
extents of everything drawn. Device space is 0–32767 with 85 µm pixels,
like META.SYS. `v_ftext` is stored as `v_gtext`, and `vst_arbpt` also as
`vst_point`, so older readers such as GEM Draw understand the file.

### Memory (61–70)

As with MEMORY.SYS: set `contrl[1] = 1` and put the width and height in
`ptsin[0]`/`ptsin[1]` before `v_opnwk` (otherwise the bitmap is the size of
the screen). The one-plane bitmap is allocated in the program's own memory
and its address comes back in `contrl[0]` (high word) and `contrl[1]`;
`v_clrwk` and `v_updwk` clear it. XGDOS draws everything itself: lines with
all styles, widths and end styles; markers; filled polygons (even-odd, like
the VDI), bars, circles, ellipses, pies, arcs and rounded boxes with every
fill interior, pattern, hatch and user pattern; `v_contourfill`; all four
writing modes; clipping; `vro_cpyfm`, `vrt_cpyfm`, `vr_trnfm` and
`v_get_pixel`; Speedo text; and system font text with all effects and
90 degree rotation. Virtual workstations opened on it share the bitmap.

The system font comes from XGDOS itself, not the ROM: Line-A, the only way
to reach the ROM fonts, is deprecated on later TOS versions and missing
under some VDIs. XGDOS carries the EmuTOS system fonts in the full Atari ST
character set: 6×6 (8 pt), 8×8 (9 pt) and 8×16 (10 pt), each also
doubled (16, 18 and 20 pt), with the same metrics as the ST's own fonts.
As on the screen, `vst_point` takes the largest of these at or below the
size asked for, and `vst_height` the largest cell top (4, 6, 13, or
doubled 8, 12, 26). On every device font 1 is named "System Font" (TOS
itself calls it "6x6 system font", after its first size).

## Fonts and the VDI

Speedo fonts appear after `vst_load_fonts`, as in SpeedoGDOS. Their VDI
font ID is 5000 + the Bitstream font ID (BX000043 is 5043, BX000044 is
5044), unless ASSIGN.SYS gives one. `vqt_name` returns the full font name
and sets `intout[33] = 1` for outline fonts. `vq_gdos` returns `'_FSM'`, so
Speedo-aware programs recognise XGDOS. The SpeedoGDOS `FSMC` cookie
(`'_SPD'`) is only installed with `FSMC = ON`: the MultiTOS AES treats it as
"the real SpeedoGDOS is here" and its desktop text disappears.

Implemented calls: `vst_font`, `vst_point`, `vst_height`, `vst_arbpt(32)`,
`vst_setsize(32)`, `vst_skew`, `vst_rotation` (any angle), `vst_effects`
(bold, light, italic, underline, outline), `vst_alignment`, `vst_kern`
(track and pair kerning from the font), `vst_charmap`, `v_gtext`,
`v_ftext`, `v_ftext_offset`, `v_justified`, `vqt_extent`, `vqt_f_extent`,
`vqt_width`, `vqt_advance`, `vqt_fontinfo`, `vqt_attributes`, `vqt_name`,
`vqt_xfntinfo`, `vqt_fontheader`, `vqt_trackkern`, `vqt_pairkern`,
`v_getoutline`, `v_getbitmap_info`, `vqt_get_table`, `vqt_cachesize`,
`v_flushcache`, `vst_load_fonts`, `vst_unload_fonts`. All other calls go
to the screen VDI that was already installed (ROM VDI or XGEM).

Screen text is rasterised by XGDOS and blitted with `vrt_cpyfm` through
the existing VDI, so it works in every resolution and writing mode and
respects clipping. Bitmap `.FNT` fonts listed in ASSIGN.SYS are loaded,
converted from Intel order if needed, and passed to the screen VDI the
classic way (`contrl[7..8]`).

## Re-entrancy and MiNT

* Each VDI call into XGDOS runs on its own stack, taken from a pool (an
  XBRA `XGDS` trap #2 handler), so several processes can be inside XGDOS
  at once.
* No static scratch is used on the call path, and each workstation has
  its own state. Shared state (font table, glyph cache, heap) sits behind
  one lock. A process waiting for the lock calls `Syield`, and a lock held
  by a process that has died is taken back.
* All memory is inside XGDOS's own resident TPA, marked global
  (`--mglobal`), so nothing is lost when a client program exits.
  Screen-bound bitmaps are in ST-RAM for the blitter.
* Pure pass-through calls (most AES drawing) never take the lock.
* Resident size is about 350 KB with the defaults: roughly 110 KB of
  program, 16-32 KB of call stacks, 24 KB of rasteriser scratch and the
  192 KB `HEAP`. Fonts load into the heap when first used and keep only
  the glyphs the Atari character set reaches (about 206 of a Speedo
  font's 565), which roughly halves each font. A program that switches
  to Bitstream character IDs (`vst_charmap` 0) and asks for one of the
  others gets that font reloaded in full. Glyphs too complex for the
  scratch borrow heap memory or are drawn a strip at a time, and a print
  job empties the screen glyph cache before it runs short. The heap
  must hold the largest font file briefly while it loads, so keep
  `HEAP` at 192 KB or more if you use large fonts or many at once.

## The Speedo format (as implemented)

The BX fonts use Bitstream's keyed format. XGDOS works out the keys from
each font itself, so no customer key table is needed:

* Directory offsets are XOR'd per byte; the key comes from the font-size
  field.
* Character data words are little-endian, XOR'd with a 16-bit key (0x0056
  for these fonts); the key comes from the space glyph.
* Constraint bytes use a further key, found by trial; Atari fonts use 0xDA.

Composite glyphs (accents, fractions) are supported, as are pair and
track kerning. Screen glyphs are grid-fitted vertically using the fonts' controlled
edge tables (baseline, x-height and cap height land on whole pixels),
and strokes thinner than a pixel are kept at small sizes (dropout
control). The Atari character set maps to
Bitstream character IDs, so any font in the "BICS for Speedo" layout
works; Hebrew (194–220) is not in that layout.

## Status

* Verified here: the Speedo decoder against both of your fonts (every
  glyph, including all 218 composites); the full VDI path driven like a
  GEM program on a simulated screen; PDF output (validated with qpdf,
  rendered with poppler); and ASSIGN.SYS round-trips. All C compiles for
  the 68000 and every symbol resolves.
* Not yet run on an Atari or in Hatari: the 68000 assembly (`atari.S`,
  `traps.S`, startup files), the resident install, and XGDOSCFG on a real
  AES. Please report build errors and Hatari results.
* The metafile and memory devices are tested on the build machine: every
  primitive drawn into a memory bitmap and checked, and metafiles read
  back and checked.
* Optional: external `.SYS` drivers (other device numbers, or
  `PRINTER = DRIVER`) are entered at their text start with d0 = 115 and
  d1 = parameter block, the classic GDOS convention.
* Not implemented: `v_loadcache`/`v_savecache` (return -1), cell arrays,
  contour fill and NDC coordinates on the PDF device.

## Licence

XGDOS is free software, Copyright (C) 2026 Francis C Bullen. You can
redistribute it and/or modify it under the terms of the GNU General Public
License, version 2, as published by the Free Software Foundation; the full
text is in `LICENSE`. XGDOS is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

Anyone who passes XGDOS on, changed or not, must make its source code
available under the same licence. Every source file carries the notice
`SPDX-License-Identifier: GPL-2.0-only`.

Speedo font files are not part of XGDOS and keep their own copyright and
licence terms; the free ones bundled in `GEMSYS\` are described below.

### The built-in system fonts (EmuTOS)

`src/sysfont.c` holds the EmuTOS 6×6, 8×8 and 8×16 system fonts for the
Atari ST character set (`bios/fnt_st_6x6.c`, `fnt_st_8x8.c` and
`fnt_st_8x16.c` in the EmuTOS sources), converted by
`tools/mksysfont.py`:

> Copyright (C) 2001-2021 The EmuTOS development team.
> Distributed under the GNU General Public License, version 2 or (at your
> option) any later version.

EmuTOS: https://emutos.sourceforge.io/ (source: https://github.com/emutos/emutos).
To regenerate the file from an EmuTOS source tree:
`python3 tools/mksysfont.py path/to/emutos > src/sysfont.c`.

### The bundled fonts

`GEMSYS\` holds the eight Speedo fonts Bitstream Inc. donated to the X
Consortium (X11 package font-bitstream-speedo 1.0.0): Bitstream Charter,
Charter Italic, Charter Black and Charter Black Italic (`FONT0648`,
`FONT0649`, `FONT0709`, `FONT0710`), and Courier 10 Pitch, Italic, Bold and
Bold Italic (`FONT0419`, `FONT0582`, `FONT0583`, `FONT0611`). Bitstream's
licence, in `GEMSYS\COPYING.TXT`, lets anyone use, copy, modify, sublicense,
sell and redistribute them provided that notice stays with them.
BITSTREAM CHARTER is a registered trademark of Bitstream Inc.
