/* probe_jpi.c - what a wildcard $GETJPI scan sees, and what it costs (Phase 0, read-only).

   Three passes over every process:
     A  the cheap items only (all in the PCB/PHD: no AST into the target),
     B  A plus JPI$_IMAGNAME (which needs an AST into each target process),
     C  A again with WORLD and GROUP turned off for this image, to see what an
        unprivileged user would get.
   Each pass reports processes seen, the statuses met, wall time and our own CPU time.
   Pass B also prints one line per process, to compare with SHOW SYSTEM.

   The control flags ask $GETJPI not to inswap a target, and not to fail on a
   suspended one, when the headers define them. */
#include <stdio.h>
#include <string.h>
#include <ssdef.h>
#include <jpidef.h>
#include <prvdef.h>
#include <starlet.h>
#include "probe_common.h"

#define MAXP 2000

struct row {
    unsigned int pid, owner, master, state, pri, prib, cputim, ppg, gpg, pagefl, dirio,
                 bufio, mode, kt;
    char prcnam[16], user[13], image[256], term[9];
    unsigned short prcnam_l, user_l, image_l, term_l;
};

static struct row rows[MAXP];

static unsigned int self_cpu(void)
{
    unsigned int t = 0;
    ile3 it[2];
    unsigned short iosb[4];
    ile3_set(&it[0], JPI$_CPUTIM, &t, 4, 0);
    ile3_end(&it[1]);
    sys$getjpiw(0, 0, 0, it, iosb, 0, 0);
    return t;
}

static unsigned long long now_100ns(void)
{
    unsigned long long t = 0;
    sys$gettim((void *) &t);
    return t;
}

static int pass(const char *name, int with_image, int print)
{
    int n = 0, nopriv = 0, susp = 0, nonexpr = 0, other = 0, multi = 0, dup = 0, i, j;
    unsigned int ctx = 0xFFFFFFFF, st, flags = 0, last_other = 0;
    unsigned int cpu0 = self_cpu();
    unsigned long long t0 = now_100ns(), t1;
    ile3 it[20];
    unsigned short iosb[4];

#ifdef JPI$M_NO_TARGET_INSWAP
    flags |= JPI$M_NO_TARGET_INSWAP;
#endif
#ifdef JPI$M_IGNORE_TARGET_STATUS
    flags |= JPI$M_IGNORE_TARGET_STATUS;
#endif
    for (;;) {
        struct row *r = &rows[n < MAXP ? n : MAXP - 1];
        int k = 0;
        memset(r, 0, sizeof *r);
#ifdef JPI$_GETJPI_CONTROL_FLAGS
        ile3_set(&it[k++], JPI$_GETJPI_CONTROL_FLAGS, &flags, 4, 0);
#endif
        ile3_set(&it[k++], JPI$_PID, &r->pid, 4, 0);
        ile3_set(&it[k++], JPI$_OWNER, &r->owner, 4, 0);
        ile3_set(&it[k++], JPI$_MASTER_PID, &r->master, 4, 0);
        ile3_set(&it[k++], JPI$_STATE, &r->state, 4, 0);
        ile3_set(&it[k++], JPI$_PRI, &r->pri, 4, 0);
        ile3_set(&it[k++], JPI$_PRIB, &r->prib, 4, 0);
        ile3_set(&it[k++], JPI$_CPUTIM, &r->cputim, 4, 0);
        ile3_set(&it[k++], JPI$_PPGCNT, &r->ppg, 4, 0);
        ile3_set(&it[k++], JPI$_GPGCNT, &r->gpg, 4, 0);
        ile3_set(&it[k++], JPI$_PAGEFLTS, &r->pagefl, 4, 0);
        ile3_set(&it[k++], JPI$_DIRIO, &r->dirio, 4, 0);
        ile3_set(&it[k++], JPI$_BUFIO, &r->bufio, 4, 0);
        ile3_set(&it[k++], JPI$_MODE, &r->mode, 4, 0);
        ile3_set(&it[k++], JPI$_KT_COUNT, &r->kt, 4, 0);
        ile3_set(&it[k++], JPI$_PRCNAM, r->prcnam, 15, &r->prcnam_l);
        ile3_set(&it[k++], JPI$_USERNAME, r->user, 12, &r->user_l);
        ile3_set(&it[k++], JPI$_TERMINAL, r->term, 8, &r->term_l);
        if (with_image)
            ile3_set(&it[k++], JPI$_IMAGNAME, r->image, 255, &r->image_l);
        ile3_end(&it[k]);
        st = sys$getjpiw(0, &ctx, 0, it, iosb, 0, 0);
        if (st & 1) st = iosb[0];
        if (st == SS$_NOMOREPROC) break;
        if (st == SS$_NOPRIV) { nopriv++; continue; }
        if (st == SS$_SUSPENDED) { susp++; continue; }
        if (st == SS$_NONEXPR) { nonexpr++; continue; }
        if (!(st & 1)) { other++; last_other = st; if (other > 50) break; continue; }
        if (r->kt > 1) multi++;
        if (n < MAXP) n++;
    }
    t1 = now_100ns();
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (rows[i].pid == rows[j].pid) dup++;
    printf("pass %s: %d processes, %d NOPRIV, %d SUSPENDED, %d NONEXPR, %d other",
           name, n, nopriv, susp, nonexpr, other);
    if (other) printf(" (last %s)", msg(last_other));
    printf("\n  wall %.1f ms, own CPU %u ticks (10 ms), multi-thread %d, duplicate PIDs %d\n",
           (double) (t1 - t0) / 10000.0, self_cpu() - cpu0, multi, dup);
    if (print) {
        printf("  %-8s %-8s %-15s %-12s St Pr/B %10s %7s %6s %9s %8s %8s Md KT %-8s Image\n",
               "PID", "Owner", "Process", "User", "CPU", "PPG", "GPG", "Faults", "DIO", "BIO",
               "Term");
        for (i = 0; i < n; i++) {
            struct row *r = &rows[i];
            r->prcnam[r->prcnam_l] = 0; r->user[r->user_l] = 0;
            r->image[r->image_l] = 0; r->term[r->term_l] = 0;
            rtrim(r->user);
            printf("  %08X %08X %-15s %-12s %2u %2u/%-2u %10u %7u %6u %9u %8u %8u %2u %2u %-8s %s\n",
                   r->pid, r->owner, r->prcnam, r->user, r->state, r->pri, r->prib, r->cputim,
                   r->ppg, r->gpg, r->pagefl, r->dirio, r->bufio, r->mode, r->kt, r->term,
                   r->image);
        }
    }
    return n;
}

int main(void)
{
    unsigned int mask[2] = { PRV$M_WORLD | PRV$M_GROUP, 0 }, prev[2] = { 0, 0 }, st;
    int i;
#ifndef JPI$_GETJPI_CONTROL_FLAGS
    printf("note: no JPI$_GETJPI_CONTROL_FLAGS in the headers\n");
#endif
    for (i = 0; i < 3; i++) pass("A", 0, 0);     /* three times: the first warms caches */
    pass("B", 1, 1);
    pass("B", 1, 0);
    st = sys$setprv(0, (void *) mask, 0, (void *) prev);
    report("$SETPRV off WORLD, GROUP", st);
    pass("C", 0, 0);
    pass("C", 1, 0);
    mask[0] &= prev[0];                         /* put back only what was on */
    if (mask[0]) sys$setprv(1, (void *) mask, 0, 0);
    return 1;
}
