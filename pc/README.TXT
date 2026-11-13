XGDOS 1.02 for PC GEM (FreeGEM / OpenGEM)
=========================================

XGDOS gives PC GEM programs Speedo outline fonts at any size, through the
GDOS calls SpeedoGDOS programs use on the Atari (vst_arbpt, vqt_advance,
v_ftext, v_getoutline and the rest). It is the same font engine as XGDOS
for the Atari ST, built as a 16-bit DOS program.

XGDOS does not stay resident on its own: it starts GEM itself and goes in
front of GEMVDI's VDI entry while GEM runs. When GEM ends, XGDOS ends.

Installing
----------
1. Copy XGDOS.EXE and XGDOS.SYS to GEM's system folder, the one with
   GEMVDI.EXE in it (OpenGEM: C:\GEMAPPS\GEMSYS).
2. Copy the FONTS folder there too (C:\GEMAPPS\GEMSYS\FONTS), or set
   FONTPATH in XGDOS.SYS to where the .SPD fonts are.
3. In GEM.BAT (OpenGEM: C:\GEM.BAT) change the line that starts GEM
   from
       GEMVDI %1 %2 %3
   to
       XGDOS GEMVDI %1 %2 %3

XGDOS prints a few lines when it starts: the fonts it found and how much
conventional memory is left for GEM. Upper memory (UMBs, e.g. DOS=UMB with
EMM386 or HIMEM + UMB provider) is used for its font memory when there is
some, which leaves more conventional memory for GEM and its programs.
GEM's desktop needs roughly 370 KB; programs need more.

XGDOS.SYS
---------
The same settings and font lines as the Atari's ASSIGN.SYS XGDOS block:
FONTPATH, CACHE (KB), HEAP (KB), HINTING (ON/OFF), and one +FILE.SPD line
for each font to offer (-FILE.SPD to leave one out). Programs see the
fonts after GEM's own bitmap fonts, with the ids the fonts carry.

Not on PC GEM (yet): XGDOS's own PDF printer, metafile and memory devices
- GEM's own printer and metafile drivers are used, as before.

For testing
-----------
  SET XGPASS=1     XGDOS in front of GEMVDI, but every call goes on to GEM
  SET XGNOHOOK=1   XGDOS in memory, but GEM runs on GEMVDI alone

The fonts
---------
The eight Speedo fonts in FONTS were donated by Bitstream Inc. to the X
Consortium; their licence is in FONTS\COPYING.TXT. BITSTREAM CHARTER is
a registered trademark of Bitstream Inc.

Licence
-------
XGDOS is free software, Copyright (C) 2026 Francis C Bullen, under the GNU
General Public License version 2 (LICENSE). The source code is at
https://github.com/FrankBRetro/xgdos (folder pc for the PC parts).
