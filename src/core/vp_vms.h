/* vp_vms.h - the vms panel's figures (D9): queue states and kinds as words, and the
   order the queues are shown in. No I/O. */
#ifndef VP_VMS_H
#define VP_VMS_H

#include "vp_types.h"

/* "stopped", "busy", "idle", ...: the state that matters most */
const char *vp_queue_state(unsigned status);
/* stopped, stalled, paused, ...: someone should look at it */
int  vp_queue_problem(unsigned status);
const char *vp_queue_kind(int kind);        /* "batch", "print", ... */
/* indexes into v->queue in the order shown: problems, then busy or with jobs, then
   the rest, by name within each. Returns the count (v->nqueue). */
int  vp_queue_order(const vp_vms *v, int *idx);
/* all jobs in the queues: executing, pending, holding and timed release */
int  vp_queue_jobs(const vp_vms *v);

#endif
