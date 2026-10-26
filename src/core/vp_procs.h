/* vp_procs.h - the process list: rates from successive scans, and the view the proc
   panel shows, filtered and sorted or as a tree by owner (D8). No I/O. */
#ifndef VP_PROCS_H
#define VP_PROCS_H

#include <stddef.h>
#include "vp_types.h"

typedef struct {
    vp_proc p;
    float cpu;                  /* % of the whole system since the last scan; -1 not known */
    float dio, bio;             /* per second; -1 not known */
} vp_prow;

typedef struct {
    vp_prow *row;               /* the latest scan, in PID order */
    int n, cap;
    vp_prow *old;               /* the scan before */
    int nold, oldcap;
    uint64_t ms, old_ms;
    vp_scan scan;
    int scans;
} vp_plist;

/* sort columns, in the order the panel shows them */
enum { VP_S_PID, VP_S_NAME, VP_S_USER, VP_S_STATE, VP_S_PRI, VP_S_CPU, VP_S_TIME,
       VP_S_MEM, VP_S_DIO, VP_S_BIO, VP_S_IMAGE, VP_NSORT };

typedef struct {
    int row;                    /* index into vp_plist.row */
    unsigned char depth;        /* tree: 0 for a root */
    unsigned char last;         /* tree: the last of its siblings */
    uint32_t more;              /* tree: bit d set if the ancestor at depth d has a
                                   sibling still to come (draws its │) */
} vp_pline;

typedef struct {
    vp_pline *line;             /* in the order shown */
    int n, cap;
} vp_pview;

void vp_plist_init(vp_plist *l);
void vp_plist_free(vp_plist *l);
/* a new scan of n processes taken at ms, on a system of ncpu CPUs; 0: no memory */
int  vp_plist_update(vp_plist *l, const vp_proc *p, int n, const vp_scan *scan,
                     uint64_t ms, int ncpu);
int  vp_plist_find(const vp_plist *l, uint32_t pid);       /* row index, or -1 */

void vp_pview_init(vp_pview *v);
void vp_pview_free(vp_pview *v);
/* the lines to show; 0: no memory */
int  vp_pview_build(vp_pview *v, const vp_plist *l, int sort, int reverse, int tree,
                    const char *filter);
int  vp_pview_find(const vp_pview *v, const vp_plist *l, uint32_t pid);   /* line, or -1 */

/* the filter matches name, user, image or PID (hex), any case */
int  vp_proc_match(const vp_proc *p, const char *filter);
const char *vp_state_name(int state);                       /* "LEF", "CUR", "?" */
const char *vp_mode_name(int mode);                         /* "interactive" */
const char *vp_sort_name(int s);                            /* "CPU%" */
int  vp_sort_descending(int sort, int reverse);             /* highest first? */
/* 10 ms ticks -> "05:29:21.30", or "12 05:29:21" past a day (11 characters at most) */
void vp_fmt_cputime(char *out, size_t size, uint32_t ticks);
/* "[1,4]" or "[377,377]": a UIC's group and member, in octal */
void vp_fmt_uic(char *out, size_t size, uint32_t uic);
/* "DKA0:[SYS0.SYSCOMMON.][SYSEXE]VIM.EXE;1" -> "VIM" */
void vp_image_short(char *out, size_t size, const char *spec);

#endif
