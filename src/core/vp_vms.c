/* vp_vms.c - queue states, kinds and order for the vms panel (D9). No I/O. */
#include <stdlib.h>
#include <string.h>
#include "vp_vms.h"

/* first match wins: the state a person most needs to see */
static const struct { unsigned bit; const char *name; } states[] = {
    { VP_QS_STOPPED, "stopped" }, { VP_QS_STOPPING, "stopping" },
    { VP_QS_STALLED, "stalled" }, { VP_QS_PAUSED, "paused" },
    { VP_QS_PAUSING, "pausing" }, { VP_QS_UNAVAILABLE, "unavail" },
    { VP_QS_DISABLED, "disabled" }, { VP_QS_CLOSED, "closed" },
    { VP_QS_STARTING, "starting" }, { VP_QS_RESETTING, "reset" },
    { VP_QS_RESUMING, "resuming" }, { VP_QS_ALIGNING, "aligning" },
    { VP_QS_BUSY, "busy" }, { VP_QS_IDLE, "idle" },
};

const char *vp_queue_state(unsigned status)
{
    size_t i;
    for (i = 0; i < sizeof states / sizeof states[0]; i++)
        if (status & states[i].bit) return states[i].name;
    return "-";
}

int vp_queue_problem(unsigned status)
{
    return (status & (VP_QS_STOPPED | VP_QS_STOPPING | VP_QS_STALLED | VP_QS_PAUSED
                      | VP_QS_PAUSING | VP_QS_UNAVAILABLE | VP_QS_DISABLED
                      | VP_QS_CLOSED)) != 0;
}

const char *vp_queue_kind(int kind)
{
    static const char *k[] = { "batch", "print", "server", "term", "generic" };
    return kind >= 0 && kind <= VP_QK_GENERIC ? k[kind] : "?";
}

static int group(const vp_queue *q)
{
    if (vp_queue_problem(q->status)) return 0;
    if ((q->status & VP_QS_BUSY) || q->exec || q->pend || q->hold || q->timed) return 1;
    return 2;
}

static const vp_vms *order_v;           /* qsort has no context argument */

static int cmp_queue(const void *a, const void *b)
{
    const vp_queue *x = &order_v->queue[*(const int *) a],
                   *y = &order_v->queue[*(const int *) b];
    int gx = group(x), gy = group(y);
    if (gx != gy) return gx - gy;
    return strcmp(x->name, y->name);
}

int vp_queue_order(const vp_vms *v, int *idx)
{
    int i;
    for (i = 0; i < v->nqueue; i++) idx[i] = i;
    order_v = v;
    if (v->nqueue > 1) qsort(idx, (size_t) v->nqueue, sizeof *idx, cmp_queue);
    return v->nqueue;
}

int vp_queue_jobs(const vp_vms *v)
{
    int i, n = 0;
    for (i = 0; i < v->nqueue; i++)
        n += v->queue[i].exec + v->queue[i].pend + v->queue[i].hold + v->queue[i].timed;
    return n;
}
