/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Francis C Bullen - see LICENSE
 */
#include <string.h>
#include <stdio.h>
#include "../src/config.h"
int main(int argc, char **argv)
{
    static char in[16384], out[20000]; static XgConfig c; FILE *f = fopen(argv[1], "rb"); int n = (int)fread(in, 1, sizeof in, f), i;
    fclose(f);
    cfg_defaults(&c, 2); cfg_parse(&c, in, n);
    printf("path=%s fontpath=%s devices=%d:", c.path, c.fontdir[0], c.ndev);
    for (i = 0; i < c.ndev; i++) printf(" %d%c=%s", c.dev[i].id, c.dev[i].letter ? c.dev[i].letter : ' ', c.dev[i].drv);
    printf("\n");
    /* simulate the preferences app: two fonts, one disabled, custom PDF folder */
    sprintf(c.spd[0].file, "BX000043.SPD"); c.spd[0].enabled = 1; c.spd[0].vdi_id = 0;
    sprintf(c.spd[1].file, "BX000044.SPD"); c.spd[1].enabled = 0; c.spd[1].vdi_id = 0;
    c.nspd = 2; sprintf(c.pdfpath, "C:\\PRINTS"); c.pdfdpi = 300;
    n = cfg_write(&c, in, n, out, sizeof out);
    fwrite(out, 1, n, stdout);
    /* parse again: must give the same result */
    { static XgConfig d; cfg_defaults(&d, 2); cfg_parse(&d, out, n);
      printf("--- reparse: nspd=%d [%s %d] [%s %d] dpi=%d pdf=%s ndev=%d\n", d.nspd, d.spd[0].file, d.spd[0].enabled, d.spd[1].file, d.spd[1].enabled, d.pdfdpi, d.pdfpath, d.ndev);
      n = cfg_write(&d, out, n, in, sizeof in); printf("--- second write identical: %s\n", memcmp(in, out, n) ? "NO" : "yes"); }
    return 0;
}
