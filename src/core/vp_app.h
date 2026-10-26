/* vp_app.h - VTOP's state, keys and drawing, independent of the platform. No I/O:
   the main loop feeds it samples and keys and shows its screen. */
#ifndef VP_APP_H
#define VP_APP_H

#include "vp_types.h"
#include "vp_screen.h"
#include "vp_model.h"
#include "vp_procs.h"
#include "vp_opts.h"
#include "vp_vms.h"

#define VP_VERSION "0.1.0"
#define VP_MIN_ROWS 20
#define VP_MIN_COLS 70
#define VP_MIN_INTERVAL 1
#define VP_MAX_INTERVAL 10

typedef struct { int r1, c1, r2, c2; } vp_rect;

typedef struct {
    vp_rect cpu, mem, net, vms, proc;
    int show_net, show_vms;     /* the bottom-left slot (D9) */
    int status_row;
} vp_layout;

/* what the keyboard is typing into */
enum { VP_IN_NONE, VP_IN_FILTER, VP_IN_PRIO, VP_IN_CONFIRM };

typedef struct {
    vp_screen scr;
    vp_model model;
    vp_identity id;
    int graph;                  /* VP_G_* */
    int colour;                 /* VP_C_*: only shown in the status line here */
    int interval;               /* seconds between samples */
    int nic;                    /* the NIC the net panel shows */
    char message[256];          /* a broadcast message or a note, in the status line */
    int message_ticks;          /* samples left to show it */
    int quit;
    int repaint;                /* the terminal should be redrawn from scratch */
    int measure;                /* ask the terminal its size again (Ctrl/W) */
    int resample;               /* take a sample now (the interval changed) */
    int help;                   /* the key list is showing */

    /* the process list (D8) */
    vp_plist procs;
    vp_pview view;
    int sort, reverse, tree;
    char filter[32];
    uint32_t sel_pid;           /* the selection follows the PID, not the line */
    int sel, top;               /* the selected line, the first line shown */
    int list_rows;              /* lines the list had at the last draw (Prev, Next) */
    int input;                  /* VP_IN_* */
    char field[8];              /* the priority being typed */

    /* a question about one process (D4): asked about ask_proc as it was shown, and
       done by the main loop to ask_proc.pid when act is set */
    int ask_act, ask_arg;
    vp_proc ask_proc;
    int act;
    int flush;                  /* throw typeahead away now, a question is coming */

    /* the details page: the main loop fetches detail_pid when detail_want is set */
    int detail;
    uint32_t detail_pid;
    int detail_want;
    int det_ok;
    vp_proc_detail det;
    char det_err[160];

    /* the vms panel (D9): the main loop reads vms while it shows, at once when
       vms_want is set */
    int panel;                  /* VP_P_*, AUTO until v is pressed */
    int have_vms, vms_want;
    vp_vms vms;
    int vms_order[VP_MAX_QUEUE];

    /* the options file (D10) */
    int save;                   /* w was pressed: the main loop writes the file */
    int graph_chosen, colour_chosen;    /* by g or c (or a qualifier) in this session */
    char want_nic[VP_NAME];     /* from the file: the NIC to show once it is seen */
} vp_app;

int  vp_app_init(vp_app *a, int rows, int cols, const vp_identity *id);  /* 0: no memory */
void vp_app_free(vp_app *a);
int  vp_app_resize(vp_app *a, int rows, int cols);                       /* 0: no memory */
void vp_app_sample(vp_app *a, const vp_sample *s);
void vp_app_key(vp_app *a, int key);
void vp_app_message(vp_app *a, const char *text);
/* a process scan (n processes) taken at ms; 0: no memory */
int  vp_app_procs(vp_app *a, const vp_proc *p, int n, const vp_scan *scan, uint64_t ms);
/* the list as the next draw shows it, and the selection on it; drawing does this too */
void vp_app_update_view(vp_app *a);
void vp_app_detail(vp_app *a, int ok, const vp_proc_detail *d, const char *err);
/* the result of an action the main loop did: ok, or the system's message */
void vp_app_action_done(vp_app *a, int ok, const char *msg);
void vp_app_draw(vp_app *a);

/* the vms panel */
int  vp_app_panel(const vp_app *a);         /* what the slot shows now: NET, VMS or BOTH */
int  vp_app_shows_vms(const vp_app *a);
void vp_app_vms(vp_app *a, const vp_vms *v);
/* the options file: settings in, and the settings now (all of them) out */
void vp_app_apply(vp_app *a, const vp_opts *o);
void vp_app_options(const vp_app *a, vp_opts *o);

/* panel: VP_P_NET, VMS or BOTH (as vp_app_panel gives it) */
void vp_layout_for(int rows, int cols, int panel, vp_layout *l);
int  vp_layout_fits_both(int rows);
const char *vp_graph_name(int g);
const char *vp_colour_name(int c);
/* "BRAILLE", "block", "VT", ... -> VP_G_*; -1 if none */
int vp_graph_parse(const char *s);
int vp_colour_parse(const char *s);

#endif
