/* t_sample.c - VTOP's sampler and model without a terminal: two samples P1 seconds
   apart (default 3), then the figures, to compare with MONITOR MODES and DISK. */
#include <stdio.h>
#include <stdlib.h>
#include <lib$routines.h>
#include "vp_platform.h"
#include "vp_model.h"

static vp_model m;
static vp_sample s;

int main(int argc, char **argv)
{
    float secs = argc > 1 ? (float) atof(argv[1]) : 3.0f;
    int lt = 4, i, k;
    static const char *mn[VP_NMODES] = { "int", "mpsync", "kernel", "exec", "super", "user", "idle" };
    vp_model_init(&m);
    if (!vpp_sample(&s)) { printf("no sample\n"); return 44; }
    vp_model_update(&m, &s);
    lib$wait(&secs, 0, &lt);
    vpp_sample(&s);
    vp_model_update(&m, &s);
    printf("%.2f s, %d CPUs, all %.1f%%\n", m.secs, m.ncpu, m.cpu_pct);
    for (i = 0; i < m.ncpu; i++) {
        printf("  CPU%d %.1f%%  ticks:", i, m.core_pct[i]);
        for (k = 0; k < VP_NMODES; k++)
            printf(" %s %llu", mn[k], (unsigned long long) (m.cur.cpu[i].t[k] - m.prev.cpu[i].t[k]));
        printf("\n");
    }
    for (k = 0; k < VP_NMODES; k++) printf("  %s %.1f%%", mn[k], m.mode_pct[k]);
    printf("\n");
    for (i = 0; i < m.cur.ndisk; i++) printf("  %s %.1f op/s\n", m.cur.disk[i].name, m.disk_ops[i]);
    for (i = 0; i < m.cur.nnic; i++) printf("  %s rx %.0f tx %.0f B/s\n", m.cur.nic[i].name, m.nic_rx[i], m.nic_tx[i]);
    return 1;
}
