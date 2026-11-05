DRIVERS - the GDOS driver folder (PATH in ASSIGN.SYS)
=====================================================

XGDOS needs nothing in here. It provides the screen, printer (numbered PDF
files), metafile and memory devices itself, so no SCREEN.SYS, printer
driver, META.SYS or MEMORY.SYS is required.

This folder is still where ASSIGN.SYS's PATH points, for anything you
want to add yourself:

  * bitmap .FNT fonts listed under a screen device line in ASSIGN.SYS;
  * a printer .SYS driver, used only with PRINTER = DRIVER;
  * drivers for other device numbers (plotters 11-20, cameras 41-50,
    tablets 51-60), which XGDOS loads if ASSIGN.SYS lists them.
