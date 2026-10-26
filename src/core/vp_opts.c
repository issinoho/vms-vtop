/* vp_opts.c - the options file (D10): parse and format. No I/O. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vp_opts.h"
#include "vp_app.h"
#include "vp_util.h"

static const char *sort_words[VP_NSORT] = {
    "pid", "name", "user", "state", "pri", "cpu", "time", "mem", "dio", "bio", "image"
};
static const char *panel_words[VP_NPANEL] = { "auto", "net", "vms", "both" };

const char *vp_panel_name(int p) { return p >= 0 && p < VP_NPANEL ? panel_words[p] : "?"; }

void vp_opts_init(vp_opts *o)
{
    o->graph = o->colour = o->interval = o->sort = o->reverse = o->tree = o->panel = -1;
    o->network[0] = '\0';
}

static int word(const char *v, const char *const *words, int n)
{
    int i;
    for (i = 0; i < n; i++) if (!vp_strcasecmp(v, words[i])) return i;
    return -1;
}

static int yes_no(const char *v)
{
    static const char *yes[] = { "yes", "on", "true", "1" }, *no[] = { "no", "off", "false", "0" };
    if (word(v, yes, 4) >= 0) return 1;
    if (word(v, no, 4) >= 0) return 0;
    return -1;
}

/* blanks off both ends, in place */
static char *strip(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
    return s;
}

/* one line; 0 and a reason if it is wrong */
static int one(vp_opts *o, char *line, char *why, size_t size)
{
    char *eq = strchr(line, '='), *name, *val;
    int v;
    if (!eq) { snprintf(why, size, "no \"=\""); return 0; }
    *eq = '\0';
    name = strip(line);
    val = strip(eq + 1);
    if (!vp_strcasecmp(name, "graph")) {
        if ((v = vp_graph_parse(val)) < 0) goto bad;
        o->graph = v;
    } else if (!vp_strcasecmp(name, "colour") || !vp_strcasecmp(name, "color")) {
        if ((v = vp_colour_parse(val)) < 0) goto bad;
        o->colour = v;
    } else if (!vp_strcasecmp(name, "interval")) {
        char *end;
        long n = strtol(val, &end, 10);
        if (end == val || *end || n < VP_MIN_INTERVAL || n > VP_MAX_INTERVAL) goto bad;
        o->interval = (int) n;
    } else if (!vp_strcasecmp(name, "sort")) {
        if ((v = word(val, sort_words, VP_NSORT)) < 0) goto bad;
        o->sort = v;
    } else if (!vp_strcasecmp(name, "reverse")) {
        if ((v = yes_no(val)) < 0) goto bad;
        o->reverse = v;
    } else if (!vp_strcasecmp(name, "tree")) {
        if ((v = yes_no(val)) < 0) goto bad;
        o->tree = v;
    } else if (!vp_strcasecmp(name, "panel")) {
        if ((v = word(val, panel_words, VP_NPANEL)) < 0) goto bad;
        o->panel = v;
    } else if (!vp_strcasecmp(name, "network")) {
        if (!*val || strlen(val) >= sizeof o->network) goto bad;
        vp_strlcpy(o->network, val, sizeof o->network);
    } else {
        snprintf(why, size, "unknown option \"%.20s\"", name);
        return 0;
    }
    return 1;
bad:
    snprintf(why, size, "bad value for %.12s: \"%.20s\"", name, val);
    return 0;
}

int vp_opts_parse(vp_opts *o, const char *text, char *err, size_t errsize)
{
    char line[160], why[80], *s;
    int bad = 0, no = 0;
    if (errsize) err[0] = '\0';
    while (*text) {
        size_t n = strcspn(text, "\n");
        no++;
        vp_strlcpy(line, text, n + 1 < sizeof line ? n + 1 : sizeof line);
        text += n;
        if (*text) text++;
        s = strip(line);
        if (!*s || *s == '!' || *s == '#') continue;
        if (!one(o, s, why, sizeof why)) {
            if (!bad++ && errsize) snprintf(err, errsize, "line %d: %s", no, why);
        }
    }
    return bad;
}

/* printf onto the end of out; 0 when it no longer fits */
static int add(char *out, size_t size, size_t *n, const char *fmt, ...)
{
    va_list ap;
    int k;
    if (*n >= size) return 0;
    va_start(ap, fmt);
    k = vsnprintf(out + *n, size - *n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t) k >= size - *n) { *n = size; return 0; }
    *n += (size_t) k;
    return 1;
}

int vp_opts_format(const vp_opts *o, char *out, size_t size)
{
    size_t n = 0;
    if (!size) return -1;
    out[0] = '\0';
    add(out, size, &n, "! VTOP options, written by VTOP's w key. One \"name = value\" a line;\n");
    add(out, size, &n, "! the /GRAPH, /COLOUR and /INTERVAL qualifiers win over these.\n");
    if (o->graph >= 0) add(out, size, &n, "graph = %s\n", vp_graph_name(o->graph));
    if (o->colour >= 0) add(out, size, &n, "colour = %s\n", vp_colour_name(o->colour));
    if (o->interval >= 0) add(out, size, &n, "interval = %d\n", o->interval);
    if (o->sort >= 0 && o->sort < VP_NSORT) add(out, size, &n, "sort = %s\n", sort_words[o->sort]);
    if (o->reverse >= 0) add(out, size, &n, "reverse = %s\n", o->reverse ? "yes" : "no");
    if (o->tree >= 0) add(out, size, &n, "tree = %s\n", o->tree ? "yes" : "no");
    if (o->panel >= 0) add(out, size, &n, "panel = %s\n", vp_panel_name(o->panel));
    if (o->network[0]) add(out, size, &n, "network = %s\n", o->network);
    return n < size ? (int) n : -1;
}
