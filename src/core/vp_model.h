/* vp_model.h - figures derived from successive samples: CPU percentages, rates,
   and the history the graphs draw. No I/O. */
#ifndef VP_MODEL_H
#define VP_MODEL_H

#include "vp_types.h"

#define VP_HIST 400     /* samples kept: a braille graph of 200 columns */

typedef struct {
    float v[VP_HIST];
    int n, head;        /* head: where the next value goes */
} vp_hist;

void  vp_hist_push(vp_hist *h, float v);
float vp_hist_get(const vp_hist *h, int age);           /* age 0: newest; 0 beyond n */
float vp_hist_max(const vp_hist *h, int count);         /* largest of the newest count */

typedef struct {
    int samples;                /* how many updates so far */
    vp_sample prev, cur;
    double secs;                /* between prev and cur */
    /* CPU, percentages of the time between the last two samples */
    int ncpu;
    float cpu_pct;              /* all CPUs */
    float core_pct[VP_MAX_CPU];
    float mode_pct[VP_NMODES];  /* all CPUs, by mode; VP_M_IDLE included */
    vp_hist h_cpu;
    vp_hist h_core[VP_MAX_CPU];
    vp_hist h_mem;              /* % in use */
    /* rates per second; -1 when not known yet */
    float disk_ops[VP_MAX_DISK];
    double nic_rx[VP_MAX_NIC], nic_tx[VP_MAX_NIC];
    double nic_rx_top[VP_MAX_NIC], nic_tx_top[VP_MAX_NIC];
    vp_hist h_rx[VP_MAX_NIC], h_tx[VP_MAX_NIC];
} vp_model;

void vp_model_init(vp_model *m);
void vp_model_update(vp_model *m, const vp_sample *s);

/* memory in use: total less the free and modified lists */
double vp_mem_used(const vp_sample *s);

#endif
