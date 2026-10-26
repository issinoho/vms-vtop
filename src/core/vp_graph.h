/* vp_graph.h - history graphs and meters in the three graph modes (D6). No I/O.

   BRAILLE: two samples per column, four dots per row.
   BLOCK:   one sample per column, eighth blocks.
   VT:      one sample per column, solid bar cells topped by a DEC scan line.
   The newest sample is in the rightmost column. Colour follows height. */
#ifndef VP_GRAPH_H
#define VP_GRAPH_H

#include "vp_screen.h"
#include "vp_model.h"

#define VP_GRAPH_UP   0         /* bars grow from the bottom */
#define VP_GRAPH_DOWN 1         /* bars hang from the top (btop's upload graph) */

/* a graph of h rows by w columns at (row, col); values are scaled so max fills it.
   grad(f) gives the colour index for a height fraction. */
void vp_graph(vp_screen *s, int row, int col, int h, int w, const vp_hist *hist,
              double max, int mode, int dir, int (*grad)(double));

/* how many samples a graph of w columns shows */
int vp_graph_span(int w, int mode);

/* a meter of w cells for pct (0..100), coloured by position */
void vp_meter(vp_screen *s, int row, int col, int w, double pct, int mode);

#endif
