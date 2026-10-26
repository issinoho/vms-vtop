/* probe_vms.c - what the Phase 3 "vms" panel can read without privilege (read-only).

   1. A wildcard $GETQUI over every queue: name, status, flags, job counts, and the
      cost per pass (20 passes).
   2. $GETSYI: cluster membership, votes, quorum, the page and swap file counts, and a
      wildcard pass over the cluster's nodes (csid -1).
   3. $GETDVI's shadowing items on the system disk and on every DSA device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ssdef.h>
#include <quidef.h>
#include <jbcmsgdef.h>
#include <syidef.h>
#include <dvidef.h>
#include <dcdef.h>
#include <dvsdef.h>
#include <lib$routines.h>
#include <starlet.h>
#include "probe_common.h"

struct q {
    char name[32], node[16];
    unsigned int status, flags, exec, pend, hold, timed, ret, limit;
};

static int queues(struct q *out, int max, int show)
{
    unsigned int ctx = (unsigned int) -1, sflags = QUI$M_SEARCH_WILDCARD, st;
    unsigned short iosb[4], nl, ndl;
    char pat[] = "*";
    int n = 0;
    for (;;) {
        ile3 it[12];
        struct q *e = &out[n < max ? n : max - 1];
        memset(e, 0, sizeof *e);
        nl = ndl = 0;
        ile3_set(&it[0], QUI$_SEARCH_NAME, pat, 1, 0);
        ile3_set(&it[1], QUI$_SEARCH_FLAGS, &sflags, 4, 0);
        ile3_set(&it[2], QUI$_QUEUE_NAME, e->name, sizeof e->name - 1, &nl);
        ile3_set(&it[3], QUI$_QUEUE_STATUS, &e->status, 4, 0);
        ile3_set(&it[4], QUI$_QUEUE_FLAGS, &e->flags, 4, 0);
        ile3_set(&it[5], QUI$_EXECUTING_JOB_COUNT, &e->exec, 4, 0);
        ile3_set(&it[6], QUI$_PENDING_JOB_COUNT, &e->pend, 4, 0);
        ile3_set(&it[7], QUI$_HOLDING_JOB_COUNT, &e->hold, 4, 0);
        ile3_set(&it[8], QUI$_TIMED_RELEASE_JOB_COUNT, &e->timed, 4, 0);
        ile3_set(&it[9], QUI$_JOB_LIMIT, &e->limit, 4, 0);
        ile3_set(&it[10], QUI$_SCSNODE_NAME, e->node, sizeof e->node - 1, &ndl);
        ile3_end(&it[11]);
        st = sys$getquiw(0, QUI$_DISPLAY_QUEUE, &ctx, it, iosb, 0, 0);
        if (st & 1) st = iosb[0];
        if (!(st & 1)) {
            if (show && st != JBC$_NOMOREQUE) report("$GETQUI ends", st);
            break;
        }
        e->name[nl] = '\0';
        e->node[ndl] = '\0';
        if (show)
            printf("  %-24s %-8s status %08X flags %08X exec %u pend %u hold %u timed %u limit %u\n",
                   e->name, e->node, e->status, e->flags, e->exec, e->pend, e->hold,
                   e->timed, e->limit);
        if (n < max) n++;
    }
    sys$getquiw(0, QUI$_CANCEL_OPERATION, &ctx, 0, iosb, 0, 0);
    return n;
}

static void cluster(void)
{
    unsigned int member = 0, votes = 0, quorum = 0, nodes = 0, pf = 0, sf = 0, st;
    unsigned int csid = (unsigned int) -1;
    ile3 it[8];
    unsigned short iosb[4];
    ile3_set(&it[0], SYI$_CLUSTER_MEMBER, &member, 4, 0);
    ile3_set(&it[1], SYI$_CLUSTER_VOTES, &votes, 4, 0);
    ile3_set(&it[2], SYI$_CLUSTER_QUORUM, &quorum, 4, 0);
    ile3_set(&it[3], SYI$_CLUSTER_NODES, &nodes, 4, 0);
    ile3_set(&it[4], SYI$_PAGFILCNT, &pf, 4, 0);
    ile3_set(&it[5], SYI$_SWPFILCNT, &sf, 4, 0);
    ile3_end(&it[6]);
    st = sys$getsyiw(0, 0, 0, it, iosb, 0, 0);
    if (st & 1) st = iosb[0];
    report("$GETSYI cluster", st);
    printf("  member %u (byte %u) nodes %u votes %u quorum %u pagfilcnt %u swpfilcnt %u\n",
           member, member & 0xFF, nodes, votes, quorum, pf, sf);
    for (;;) {
        char node[16], ver[9];
        unsigned short nl = 0, vl = 0;
        unsigned int nv = 0, nq = 0, ncsid = 0;
        ile3_set(&it[0], SYI$_NODENAME, node, 15, &nl);
        ile3_set(&it[1], SYI$_NODE_SWVERS, ver, 8, &vl);
        ile3_set(&it[2], SYI$_NODE_VOTES, &nv, 4, 0);
        ile3_set(&it[3], SYI$_NODE_QUORUM, &nq, 4, 0);
        ile3_set(&it[4], SYI$_NODE_CSID, &ncsid, 4, 0);
        ile3_end(&it[5]);
        st = sys$getsyiw(0, &csid, 0, it, iosb, 0, 0);
        if (st & 1) st = iosb[0];
        if (!(st & 1)) { report("wildcard $GETSYI ends", st); break; }
        node[nl] = '\0';
        ver[vl] = '\0';
        printf("  node %-8s ver %-8s votes %u quorum %u csid %08X\n", node, ver, nv, nq, ncsid);
    }
}

static void shadow(const char *name, unsigned short len)
{
    unsigned int master = 0, member = 0, mcount = 0, copying = 0, merging = 0,
                 cdone = 0, mdone = 0, status = 0, st;
    char next[65];
    unsigned short nl = 0, iosb[4];
    struct dsc$descriptor_s d;
    ile3 it[10];
    desc_set(&d, name, len);
    ile3_set(&it[0], DVI$_SHDW_MASTER, &master, 4, 0);
    ile3_set(&it[1], DVI$_SHDW_MEMBER, &member, 4, 0);
    ile3_set(&it[2], DVI$_SHDW_MBR_COUNT, &mcount, 4, 0);
    ile3_set(&it[3], DVI$_SHDW_CATCHUP_COPYING, &copying, 4, 0);
    ile3_set(&it[4], DVI$_SHDW_MERGE_COPYING, &merging, 4, 0);
    ile3_set(&it[5], DVI$_SHDW_MBR_COPY_DONE, &cdone, 4, 0);
    ile3_set(&it[6], DVI$_SHDW_MBR_MERGE_DONE, &mdone, 4, 0);
    ile3_set(&it[7], DVI$_SHDW_STATUS, &status, 4, 0);
    ile3_set(&it[8], DVI$_SHDW_NEXT_MBR_NAME, next, 64, &nl);
    ile3_end(&it[9]);
    st = sys$getdviw(0, 0, &d, it, iosb, 0, 0, 0);
    if (st & 1) st = iosb[0];
    next[nl] = '\0';
    printf("  %.*s: %s master %u member %u mbrs %u copying %u merging %u copy_done %u "
           "merge_done %u status %08X next '%s'\n", (int) len, name, (st & 1) ? "" : msg(st),
           master, member, mcount, copying, merging, cdone, mdone, status, next);
}

int main(void)
{
    static struct q qs[256];
    unsigned int t0[2], t1[2], devclass = DC$_DISK, ctx[2] = { 0, 0 };
    int n, i;
    char name[65];
    unsigned short nl = 0;
    struct dsc$descriptor_s nd, pat;
    ile3 sc[2];

    printf("== queues\n");
    n = queues(qs, 256, 1);
    printf("  %d queues\n", n);
    sys$gettim((void *) t0);
    for (i = 0; i < 20; i++) queues(qs, 256, 0);
    sys$gettim((void *) t1);
    printf("  20 passes: %.2f ms each\n",
           ((double) t1[0] - (double) t0[0] + 4294967296.0 * ((double) t1[1] - t0[1]))
           / 10000.0 / 20.0);

    printf("== cluster and page files\n");
    cluster();

    printf("== shadowing\n");
    shadow("SYS$SYSDEVICE", 13);
    ile3_set(&sc[0], DVS$_DEVCLASS, &devclass, 4, 0);
    ile3_end(&sc[1]);
    desc_set(&pat, "DSA*", 4);
    for (;;) {
        desc_set(&nd, name, 64);
        if (!(sys$device_scan(&nd, &nl, &pat, (void *) sc, (void *) ctx) & 1)) break;
        shadow(name, nl);
    }
    printf("  (end of DSA devices)\n");
    return 1;
}
