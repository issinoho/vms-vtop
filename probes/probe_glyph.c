/* probe_glyph.c - do graph characters reach the terminal unchanged? (Phase 0, read-only)

   Reports TT:'s characteristics, then writes, with IO$_WRITEVBLK|IO$M_NOFORMAT (as
   vtree's renderer does), one line each of:
     DEC Special Graphics line drawing and the scan-line characters,
     UTF-8 block elements U+2581..U+2588,
     UTF-8 braille U+2800..U+28FF samples,
     ANSI 16, 256 and 24-bit colour.
   The host captures the raw bytes (ssh -tt) and checks that each arrived intact. */
#include <stdio.h>
#include <string.h>
#include <iodef.h>
#include <dvidef.h>
#include <ttdef.h>
#include <tt2def.h>
#include <starlet.h>
#include "probe_common.h"

static unsigned short chan;

static void out(const char *s)
{
    unsigned short iosb[4];
    unsigned int st = sys$qiow(0, chan, IO$_WRITEVBLK | IO$M_NOFORMAT, iosb, 0, 0,
                               (void *) s, strlen(s), 0, 0, 0, 0);
    if ((st & 1) && !(iosb[0] & 1)) st = iosb[0];
    if (!(st & 1)) printf("write: %s\n", msg(st));
}

int main(void)
{
    struct dsc$descriptor_s tt;
    unsigned int dd = 0, dd2 = 0, type = 0, width = 0, page = 0, st;
    unsigned short iosb[4];
    ile3 it[6];
    desc_set(&tt, "TT:", 3);
    ile3_set(&it[0], DVI$_DEVDEPEND, &dd, 4, 0);
    ile3_set(&it[1], DVI$_DEVDEPEND2, &dd2, 4, 0);
    ile3_set(&it[2], DVI$_DEVTYPE, &type, 4, 0);
    ile3_set(&it[3], DVI$_DEVBUFSIZ, &width, 4, 0);
    ile3_set(&it[4], DVI$_TT_PAGE, &page, 4, 0);
    ile3_end(&it[5]);
    st = sys$getdviw(0, 0, &tt, it, iosb, 0, 0, 0);
    report("$GETDVI TT:", st);
    printf("devtype %u width %u page %u eightbit %d ansicrt %d deccrt %d deccrt2 %d deccrt3 %d deccrt4 %d\n",
           type, width, page, (dd & TT$M_EIGHTBIT) != 0, (dd2 & TT2$M_ANSICRT) != 0,
           (dd2 & TT2$M_DECCRT) != 0, (dd2 & TT2$M_DECCRT2) != 0, (dd2 & TT2$M_DECCRT3) != 0,
           (dd2 & TT2$M_DECCRT4) != 0);
    fflush(stdout);
    st = sys$assign(&tt, &chan, 0, 0);
    report("$ASSIGN TT:", st);
    if (!(st & 1)) return st;
    out("\r\n<DEC>\033(0lqqwqqk x x mqqvqqj opqrs a `\033(B</DEC>\r\n");
    out("<BLK>\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86"
        "\xE2\x96\x87\xE2\x96\x88</BLK>\r\n");
    out("<BRL>\xE2\xA0\x80\xE2\xA1\x80\xE2\xA3\x80\xE2\xA3\xA4\xE2\xA3\xB6\xE2\xA3\xBF</BRL>\r\n");
    out("<C16>\033[31mred\033[32mgreen\033[1;34mblue\033[0m</C16>\r\n");
    out("<C256>\033[38;5;208morange\033[48;5;236m on grey\033[0m</C256>\r\n");
    out("<C24>\033[38;2;255;100;0mtruecolour\033[0m</C24>\r\n");
    sys$dassgn(chan);
    return 1;
}
