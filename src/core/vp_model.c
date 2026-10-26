/* vp_model.c - derived figures and history. No I/O. */
#include <string.h>
#include "vp_model.h"

void vp_hist_push(vp_hist *h, float v)
{
    h->v[h->head] = v;
    h->head = (h->head + 1) % VP_HIST;
    if (h->n < VP_HIST) h->n++;
}

float vp_hist_get(const vp_hist *h, int age)
{
    if (age < 0 || age >= h->n) return 0;
    return h->v[(h->head - 1 - age + VP_HIST) % VP_HIST];
}

float vp_hist_max(const vp_hist *h, int count)
{
    float m = 0;
    int i;
    for (i = 0; i < count && i < h->n; i++) {
        float v = vp_hist_get(h, i);
        if (v > m) m = v;
    }
    return m;
}

void vp_model_init(vp_model *m) { memset(m, 0, sizeof *m); }

double vp_mem_used(const vp_sample *s)
{
    double used = (double) s->mem_total - (double) s->mem_free - (double) s->mem_modified;
    return used > 0 ? used : 0;
}

static uint64_t d64(uint64_t now, uint64_t before) { return now >= before ? now - before : 0; }

static void cpu(vp_model *m)
{
    const vp_sample *p = &m->prev, *c = &m->cur;
    double all_busy = 0, all_total = 0, by_mode[VP_NMODES];
    int i, k;
    memset(by_mode, 0, sizeof by_mode);
    m->ncpu = c->ncpu;
    for (i = 0; i < c->ncpu && i < VP_MAX_CPU; i++) {
        double t = 0, idle;
        for (k = 0; k < VP_NMODES; k++) {
            double d = (double) d64(c->cpu[i].t[k], p->cpu[i].t[k]);
            by_mode[k] += d;
            t += d;
        }
        idle = (double) d64(c->cpu[i].t[VP_M_IDLE], p->cpu[i].t[VP_M_IDLE]);
        m->core_pct[i] = t > 0 ? (float) (100.0 * (t - idle) / t) : 0;
        all_busy += t - idle;
        all_total += t;
        vp_hist_push(&m->h_core[i], m->core_pct[i]);
    }
    m->cpu_pct = all_total > 0 ? (float) (100.0 * all_busy / all_total) : 0;
    for (k = 0; k < VP_NMODES; k++)
        m->mode_pct[k] = all_total > 0 ? (float) (100.0 * by_mode[k] / all_total) : 0;
    vp_hist_push(&m->h_cpu, m->cpu_pct);
}

static const vp_disk *find_disk(const vp_sample *s, const char *name)
{
    int i;
    for (i = 0; i < s->ndisk; i++) if (!strcmp(s->disk[i].name, name)) return &s->disk[i];
    return 0;
}

static const vp_nic *find_nic(const vp_sample *s, const char *name)
{
    int i;
    for (i = 0; i < s->nnic; i++) if (!strcmp(s->nic[i].name, name)) return &s->nic[i];
    return 0;
}

void vp_model_update(vp_model *m, const vp_sample *s)
{
    /* histories are kept by position: the platform lists disks and NICs in the
       same order every time (device scan order, NICs opened once) */
    int i, first = m->samples == 0;
    m->prev = first ? *s : m->cur;
    m->cur = *s;
    m->samples++;
    m->secs = (double) d64(s->ms, m->prev.ms) / 1000.0;
    /* a new CPU set (or the first sample): no deltas to take */
    if (first || m->prev.ncpu != s->ncpu) {
        m->prev = *s;
        m->secs = 0;
    }
    if (m->secs > 0) cpu(m);
    if (s->mem_total) vp_hist_push(&m->h_mem, (float) (100.0 * vp_mem_used(s) / (double) s->mem_total));

    for (i = 0; i < s->ndisk; i++) {
        const vp_disk *b = find_disk(&m->prev, s->disk[i].name);
        m->disk_ops[i] = (b && m->secs > 0)
            ? (float) ((uint32_t) (s->disk[i].ops - b->ops) / m->secs) : -1;
    }
    for (i = 0; i < s->nnic; i++) {
        const vp_nic *b = find_nic(&m->prev, s->nic[i].name);
        if (b && m->secs > 0) {
            m->nic_rx[i] = (double) d64(s->nic[i].rx, b->rx) / m->secs;
            m->nic_tx[i] = (double) d64(s->nic[i].tx, b->tx) / m->secs;
            vp_hist_push(&m->h_rx[i], (float) m->nic_rx[i]);
            vp_hist_push(&m->h_tx[i], (float) m->nic_tx[i]);
            if (m->nic_rx[i] > m->nic_rx_top[i]) m->nic_rx_top[i] = m->nic_rx[i];
            if (m->nic_tx[i] > m->nic_tx_top[i]) m->nic_tx_top[i] = m->nic_tx[i];
        } else {
            m->nic_rx[i] = m->nic_tx[i] = -1;
        }
    }
}
