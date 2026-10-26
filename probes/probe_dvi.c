/* probe_dvi.c - per-disk I/O counters for a disk panel (Phase 0, read-only).

   Lists every disk device, mounted or not, with DVI$_OPCNT and DVI$_ERRCNT, then
   samples again after P1 seconds (default 5) and prints the operations per second.
   btop shows read and write bytes per disk; VMS keeps only an operation count per
   device, so this shows whether that count moves the way MONITOR DISK says. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ssdef.h>
#include <dcdef.h>
#include <dvidef.h>
#include <dvsdef.h>
#include <lib$routines.h>
#include <starlet.h>
#include "probe_common.h"

#define MAXD 64

struct dsk {
    char name[65];
    unsigned int mnt, member, opcnt, errcnt, maxb, freeb, refcnt;
};

static struct dsk d0[MAXD], d1[MAXD];

static int scan(struct dsk *out)
{
    unsigned int devclass = DC$_DISK, ctx[2] = { 0, 0 };
    char name[65];
    unsigned short nl = 0;
    struct dsc$descriptor_s nd, pat, dev;
    ile3 sc[2];
    int n = 0;
    ile3_set(&sc[0], DVS$_DEVCLASS, &devclass, 4, 0);
    ile3_end(&sc[1]);
    desc_set(&pat, "*", 1);
    while (n < MAXD) {
        ile3 it[8];
        unsigned short iosb[4];
        struct dsk *d = &out[n];
        desc_set(&nd, name, 64);
        if (!(sys$device_scan(&nd, &nl, &pat, (void *) sc, (void *) ctx) & 1)) break;
        memset(d, 0, sizeof *d);
        memcpy(d->name, name, nl);
        desc_set(&dev, name, nl);
        ile3_set(&it[0], DVI$_MNT, &d->mnt, 4, 0);
        ile3_set(&it[1], DVI$_SHDW_MEMBER, &d->member, 4, 0);
        ile3_set(&it[2], DVI$_OPCNT, &d->opcnt, 4, 0);
        ile3_set(&it[3], DVI$_ERRCNT, &d->errcnt, 4, 0);
        ile3_set(&it[4], DVI$_MAXBLOCK, &d->maxb, 4, 0);
        ile3_set(&it[5], DVI$_FREEBLOCKS, &d->freeb, 4, 0);
        ile3_set(&it[6], DVI$_REFCNT, &d->refcnt, 4, 0);
        ile3_end(&it[7]);
        if (!(sys$getdviw(0, 0, &dev, it, iosb, 0, 0, 0) & 1) || !(iosb[0] & 1)) {
            report(d->name, iosb[0]);
            continue;
        }
        n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    float secs = argc > 1 ? (float) atof(argv[1]) : 5.0f;
    int lt = 4, n0, n1, i, j;                         /* LIB$K_IEEE_S */
    n0 = scan(d0);
    lib$wait(&secs, 0, &lt);
    n1 = scan(d1);
    printf("%d disks, then %d, %.0f s apart\n", n0, n1, secs);
    printf("  %-24s Mnt Mbr %12s %8s %6s %12s %12s\n", "Device", "OPCNT", "op/s", "Errors",
           "Blocks", "Free");
    for (i = 0; i < n1; i++) {
        struct dsk *b = &d1[i], *a = 0;
        for (j = 0; j < n0; j++) if (!strcmp(d0[j].name, b->name)) a = &d0[j];
        printf("  %-24s %3u %3u %12u %8.1f %6u %12u %12u\n", b->name, b->mnt != 0,
               b->member != 0, b->opcnt, a ? (b->opcnt - a->opcnt) / secs : -1.0,
               b->errcnt, b->maxb, b->freeb);
    }
    return 1;
}
