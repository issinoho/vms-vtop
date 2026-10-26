/* vp_app.h - VTOP's state, keys and drawing, independent of the platform. No I/O:
   the main loop feeds it samples and keys and shows its screen. */
#ifndef VP_APP_H
#define VP_APP_H

#include "vp_types.h"
#include "vp_screen.h"
#include "vp_model.h"

#define VP_VERSION "0.1.0"
#define VP_MIN_ROWS 20
#define VP_MIN_COLS 70
#define VP_MIN_INTERVAL 1
#define VP_MAX_INTERVAL 10

typedef struct { int r1, c1, r2, c2; } vp_rect;

typedef struct {
    vp_rect cpu, mem, net, proc;
    int status_row;
} vp_layout;

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
} vp_app;

int  vp_app_init(vp_app *a, int rows, int cols, const vp_identity *id);  /* 0: no memory */
void vp_app_free(vp_app *a);
int  vp_app_resize(vp_app *a, int rows, int cols);                       /* 0: no memory */
void vp_app_sample(vp_app *a, const vp_sample *s);
void vp_app_key(vp_app *a, int key);
void vp_app_message(vp_app *a, const char *text);
void vp_app_draw(vp_app *a);

void vp_layout_for(int rows, int cols, vp_layout *l);
const char *vp_graph_name(int g);
const char *vp_colour_name(int c);
/* "BRAILLE", "block", "VT", ... -> VP_G_*; -1 if none */
int vp_graph_parse(const char *s);
int vp_colour_parse(const char *s);

#endif
