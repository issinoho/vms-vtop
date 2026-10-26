/* vp_opts.h - the options file (D10): "name = value" lines to settings and back.
   No I/O: the platform reads and writes the file. */
#ifndef VP_OPTS_H
#define VP_OPTS_H

#include <stddef.h>
#include "vp_types.h"

/* the bottom-left slot (D9) */
enum { VP_P_AUTO, VP_P_NET, VP_P_VMS, VP_P_BOTH, VP_NPANEL };

typedef struct {
    int graph, colour, interval, sort, reverse, tree, panel;   /* -1: not given */
    char network[VP_NAME];                                      /* "": not given */
} vp_opts;

void vp_opts_init(vp_opts *o);
/* the file's text into o. Returns the number of lines that were wrong, and the first
   one in err ("line 3: unknown option \"colr\""). */
int  vp_opts_parse(vp_opts *o, const char *text, char *err, size_t errsize);
/* o as a file, only what was given; returns the length, or -1 if size is too small */
int  vp_opts_format(const vp_opts *o, char *out, size_t size);

const char *vp_panel_name(int p);           /* "net", "vms", "both", "auto" */

#endif
