/* vp_procs.c - the process list: rates from successive scans, filter, sort, tree (D8).
   No I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vp_procs.h"
#include "vp_util.h"

#define TREE_DEPTH 24           /* deeper lines are shown, but no deeper */

void vp_plist_init(vp_plist *l) { memset(l, 0, sizeof *l); }

void vp_plist_free(vp_plist *l)
{
    free(l->row);
    free(l->old);
    memset(l, 0, sizeof *l);
}

static int by_pid(const void *a, const void *b)
{
    uint32_t x = ((const vp_prow *) a)->p.pid, y = ((const vp_prow *) b)->p.pid;
    return x < y ? -1 : x > y;
}

static int find_in(const vp_prow *r, int n, uint32_t pid)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (r[mid].p.pid == pid) return mid;
        if (r[mid].p.pid < pid) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int vp_plist_find(const vp_plist *l, uint32_t pid) { return find_in(l->row, l->n, pid); }

int vp_plist_update(vp_plist *l, const vp_proc *p, int n, const vp_scan *scan,
                    uint64_t ms, int ncpu)
{
    vp_prow *t;
    int tc, i;
    double dt;
    /* the current scan becomes the old one */
    t = l->old; l->old = l->row; l->row = t;
    tc = l->oldcap; l->oldcap = l->cap; l->cap = tc;
    l->nold = l->n;
    l->old_ms = l->ms;
    if (n > l->cap) {
        int want = n + 64;
        t = (vp_prow *) realloc(l->row, (size_t) want * sizeof *t);
        if (!t) { l->n = 0; return 0; }
        l->row = t;
        l->cap = want;
    }
    for (i = 0; i < n; i++) {
        l->row[i].p = p[i];
        l->row[i].cpu = l->row[i].dio = l->row[i].bio = -1;
    }
    l->n = n;
    qsort(l->row, (size_t) n, sizeof *l->row, by_pid);
    l->ms = ms;
    if (scan) l->scan = *scan;
    dt = l->scans && ms > l->old_ms ? (double) (ms - l->old_ms) : 0;
    if (ncpu < 1) ncpu = 1;
    for (i = 0; dt > 0 && i < n; i++) {
        vp_prow *r = &l->row[i];
        int k = find_in(l->old, l->nold, r->p.pid);
        if (k < 0) continue;
        if (r->p.cputim >= l->old[k].p.cputim) {
            /* ticks are 10 ms: ticks x 10 / (ms x ncpu) x 100 */
            r->cpu = (float) ((double) (r->p.cputim - l->old[k].p.cputim) * 1000.0 / (dt * ncpu));
            if (r->cpu > 100) r->cpu = 100;
        }
        /* 32-bit counters: the unsigned difference is right across a wrap */
        r->dio = (float) ((double) (uint32_t) (r->p.dirio - l->old[k].p.dirio) * 1000.0 / dt);
        r->bio = (float) ((double) (uint32_t) (r->p.bufio - l->old[k].p.bufio) * 1000.0 / dt);
    }
    l->scans++;
    return 1;
}

/* ---------- names and formats ---------- */

const char *vp_state_name(int state)
{
    static const char *n[] = { "?", "COLPG", "MWAIT", "CEF", "PFW", "LEF", "LEFO", "HIB",
                               "HIBO", "SUSP", "SUSPO", "FPG", "COM", "COMO", "CUR" };
    return state >= 1 && state <= 14 ? n[state] : "?";
}

const char *vp_mode_name(int mode)
{
    static const char *n[] = { "other", "network", "batch", "interactive" };
    return mode >= 0 && mode <= 3 ? n[mode] : "?";
}

static const char *sort_names[VP_NSORT] = {
    "PID", "Name", "User", "St", "Pri", "CPU%", "CPU time", "Mem", "DIO/s", "BIO/s", "Image"
};

const char *vp_sort_name(int s) { return s >= 0 && s < VP_NSORT ? sort_names[s] : "?"; }

void vp_fmt_cputime(char *out, size_t size, uint32_t ticks)
{
    unsigned long s = ticks / 100, cs = ticks % 100, d = s / 86400;
    if (d == 0)
        snprintf(out, size, "%02lu:%02lu:%02lu.%02lu", s / 3600, s % 3600 / 60, s % 60, cs);
    else if (d < 100)
        snprintf(out, size, "%lu %02lu:%02lu:%02lu", d, s % 86400 / 3600, s % 3600 / 60, s % 60);
    else
        snprintf(out, size, "%lud", d);
}

void vp_fmt_uic(char *out, size_t size, uint32_t uic)
{
    snprintf(out, size, "[%o,%o]", (unsigned) (uic >> 16), (unsigned) (uic & 0xFFFF));
}

void vp_image_short(char *out, size_t size, const char *spec)
{
    const char *p = spec, *q;
    size_t n;
    for (q = spec; *q; q++) if (*q == ']' || *q == '>' || *q == ':') p = q + 1;
    for (n = 0; p[n] && p[n] != '.' && p[n] != ';'; n++) ;
    if (size == 0) return;
    if (n > size - 1) n = size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

/* ---------- filter ---------- */

static int ci_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle), i;
    if (n == 0) return 1;
    for (; *hay; hay++) {
        for (i = 0; i < n; i++)
            if (!hay[i] || toupper((unsigned char) hay[i]) != toupper((unsigned char) needle[i]))
                break;
        if (i == n) return 1;
    }
    return 0;
}

int vp_proc_match(const vp_proc *p, const char *filter)
{
    char pid[12];
    if (!filter || !filter[0]) return 1;
    snprintf(pid, sizeof pid, "%08X", (unsigned) p->pid);
    return ci_contains(p->name, filter) || ci_contains(p->user, filter)
        || ci_contains(p->image, filter) || ci_contains(pid, filter);
}

/* ---------- sort ---------- */

static const vp_plist *s_list;          /* qsort has no context argument */
static int s_key, s_reverse;

/* numbers highest first, names A to Z, before reverse */
static int descending(int key)
{
    return key == VP_S_PRI || key == VP_S_CPU || key == VP_S_TIME || key == VP_S_MEM
        || key == VP_S_DIO || key == VP_S_BIO;
}

int vp_sort_descending(int sort, int reverse) { return descending(sort) != (reverse != 0); }

static int cmp_num(double a, double b) { return a < b ? -1 : a > b; }

static int cmp_rows(const void *pa, const void *pb)
{
    const vp_prow *a = &s_list->row[*(const int *) pa], *b = &s_list->row[*(const int *) pb];
    int c = 0;
    switch (s_key) {
    case VP_S_PID:   c = cmp_num(a->p.pid, b->p.pid); break;
    case VP_S_NAME:  c = vp_strcasecmp(a->p.name, b->p.name); break;
    case VP_S_USER:  c = vp_strcasecmp(a->p.user, b->p.user); break;
    case VP_S_STATE: c = vp_strcasecmp(vp_state_name(a->p.state), vp_state_name(b->p.state)); break;
    case VP_S_PRI:   c = cmp_num(a->p.pri, b->p.pri); break;
    case VP_S_CPU:   c = cmp_num(a->cpu, b->cpu); break;
    case VP_S_TIME:  c = cmp_num(a->p.cputim, b->p.cputim); break;
    case VP_S_MEM:   c = cmp_num((double) a->p.mem, (double) b->p.mem); break;
    case VP_S_DIO:   c = cmp_num(a->dio, b->dio); break;
    case VP_S_BIO:   c = cmp_num(a->bio, b->bio); break;
    case VP_S_IMAGE: c = vp_strcasecmp(a->p.image, b->p.image); break;
    default: break;
    }
    if (descending(s_key)) c = -c;
    if (s_reverse) c = -c;
    if (c == 0) c = cmp_num(a->p.pid, b->p.pid);         /* always the same order */
    return c;
}

/* ---------- view ---------- */

void vp_pview_init(vp_pview *v) { memset(v, 0, sizeof *v); }

void vp_pview_free(vp_pview *v)
{
    free(v->line);
    memset(v, 0, sizeof *v);
}

typedef struct {
    vp_pview *v;
    const int *order;           /* rows, sorted */
    const int *first, *next;    /* children, as positions in order */
    char *done;
} tree_ctx;

static void emit(tree_ctx *t, int k, int depth, int last, uint32_t more)
{
    vp_pline *ln = &t->v->line[t->v->n++];
    int c;
    uint32_t child_more;
    t->done[k] = 1;
    ln->row = t->order[k];
    ln->depth = (unsigned char) depth;
    ln->last = (unsigned char) last;
    ln->more = more;
    if (depth >= TREE_DEPTH) return;    /* children left over become roots below */
    child_more = more | (depth >= 1 && !last ? 1u << depth : 0u);
    for (c = t->first[k]; c >= 0; c = t->next[c])
        if (!t->done[c]) {
            int n2 = t->next[c];
            while (n2 >= 0 && t->done[n2]) n2 = t->next[n2];
            emit(t, c, depth + 1, n2 < 0, child_more);
        }
}

int vp_pview_build(vp_pview *v, const vp_plist *l, int sort, int reverse, int tree,
                   const char *filter)
{
    int *order, m = 0, i;
    v->n = 0;
    if (l->n > v->cap) {
        vp_pline *t = (vp_pline *) realloc(v->line, (size_t) l->n * sizeof *t);
        if (!t) return 0;
        v->line = t;
        v->cap = l->n;
    }
    if (l->n == 0) return 1;
    order = (int *) malloc((size_t) l->n * sizeof *order);
    if (!order) return 0;
    for (i = 0; i < l->n; i++)
        if (vp_proc_match(&l->row[i].p, filter)) order[m++] = i;
    s_list = l;
    s_key = sort;
    s_reverse = reverse;
    qsort(order, (size_t) m, sizeof *order, cmp_rows);
    if (!tree) {
        for (i = 0; i < m; i++) {
            vp_pline *ln = &v->line[v->n++];
            memset(ln, 0, sizeof *ln);
            ln->row = order[i];
        }
        free(order);
        return 1;
    }
    {
        /* children lists in sort order: parents found by owner PID among the shown */
        int *pos = (int *) malloc((size_t) l->n * sizeof *pos);
        int *parent = (int *) malloc((size_t) (m ? m : 1) * sizeof *parent);
        int *first = (int *) malloc((size_t) (m ? m : 1) * sizeof *first);
        int *next = (int *) malloc((size_t) (m ? m : 1) * sizeof *next);
        char *done = (char *) calloc((size_t) (m ? m : 1), 1);
        tree_ctx t;
        int ok = pos && parent && first && next && done;
        if (ok) {
            for (i = 0; i < l->n; i++) pos[i] = -1;
            for (i = 0; i < m; i++) pos[order[i]] = i;
            for (i = 0; i < m; i++) {
                const vp_proc *p = &l->row[order[i]].p;
                int r = p->owner && p->owner != p->pid ? vp_plist_find(l, p->owner) : -1;
                parent[i] = r >= 0 ? pos[r] : -1;
                first[i] = next[i] = -1;
            }
            for (i = m - 1; i >= 0; i--)
                if (parent[i] >= 0) { next[i] = first[parent[i]]; first[parent[i]] = i; }
            t.v = v; t.order = order; t.first = first; t.next = next; t.done = done;
            for (i = 0; i < m; i++) if (parent[i] < 0) emit(&t, i, 0, 0, 0);
            for (i = 0; i < m; i++) if (!done[i]) emit(&t, i, 0, 0, 0);    /* loops, too deep */
        }
        free(pos); free(parent); free(first); free(next); free(done);
        free(order);
        return ok;
    }
}

int vp_pview_find(const vp_pview *v, const vp_plist *l, uint32_t pid)
{
    int i;
    for (i = 0; i < v->n; i++) if (l->row[v->line[i].row].p.pid == pid) return i;
    return -1;
}
