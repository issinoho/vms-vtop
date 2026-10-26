/* vp_main.c - VTOP's main loop, the same on every platform.

   VTOP [/GRAPH=BRAILLE|BLOCK|VT] [/COLOUR=24|256|8|MONO] [/INTERVAL=n] [/VERSION]
   Qualifiers may be abbreviated; "-graph=vt" works too (for the Linux stub). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vp_app.h"
#include "vp_platform.h"
#include "vp_theme.h"
#include "vp_util.h"

static vp_app app;
static vp_proc *pbuf;
static int pcap;

/* a process scan into the app, with room made when the system has more */
static void scan_procs(void)
{
    vp_scan info;
    int n, tries;
    for (tries = 0; tries < 2; tries++) {
        if (!pbuf) {
            pcap = pcap ? pcap : 256;
            pbuf = (vp_proc *) malloc((size_t) pcap * sizeof *pbuf);
            if (!pbuf) return;
        }
        memset(&info, 0, sizeof info);
        n = vpp_procs(pbuf, pcap, &info);
        if (n < 0) return;
        if (info.total <= pcap || tries) break;
        free(pbuf);
        pbuf = 0;
        pcap = info.total + 64;
    }
    vp_app_procs(&app, pbuf, n, &info, vpp_now_ms());
}

static int value_of(const char *arg, const char *word, size_t min, const char **val)
{
    char name[32];
    const char *eq;
    if (arg[0] == '-' && arg[1] == '-') arg++;          /* --graph=vt as well as -graph=vt */
    eq = strchr(arg, '=');
    size_t n = eq ? (size_t) (eq - arg) : strlen(arg);
    if (n >= sizeof name || n < 2) return 0;
    memcpy(name, arg + 1, n - 1);
    name[n - 1] = '\0';
    if (!vp_abbrev(name, word, min)) return 0;
    *val = eq ? eq + 1 : "";
    return 1;
}

int main(int argc, char **argv)
{
    int graph = -1, colour = -1, interval = 2, utf8 = 0, i, rows, cols;
    char err[200], opt[64];
    const char *v;
    vp_identity id;
    vp_sample sample;
    vp_rgb pal[VP_NPAL];
    vp_term_mode mode;
    uint64_t next;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') {
            fprintf(stderr, "VTOP: unexpected parameter %s\n", a);
            return VP_EXIT_BAD;
        }
        if (value_of(a, "VERSION", 1, &v)) { printf("VTOP %s\n", VP_VERSION); return VP_EXIT_OK; }
        else if (value_of(a, "GRAPH", 1, &v) && (graph = vp_graph_parse(v)) >= 0) ;
        else if ((value_of(a, "COLOUR", 1, &v) || value_of(a, "COLOR", 5, &v))
                 && (colour = vp_colour_parse(v)) >= 0) ;
        else if (value_of(a, "INTERVAL", 1, &v) && atoi(v) >= VP_MIN_INTERVAL
                 && atoi(v) <= VP_MAX_INTERVAL) interval = atoi(v);
        else {
            fprintf(stderr, "VTOP: bad qualifier %s (/GRAPH=BRAILLE|BLOCK|VT, "
                    "/COLOUR=24|256|8|MONO, /INTERVAL=1..10)\n", a);
            return VP_EXIT_BAD;
        }
    }
    if (graph < 0 && vpp_option("VTOP$GRAPH", opt, sizeof opt)) graph = vp_graph_parse(opt);
    if (colour < 0 && vpp_option("VTOP$COLOUR", opt, sizeof opt)) colour = vp_colour_parse(opt);

    if (!vpp_start(&utf8, err, sizeof err)) {
        fprintf(stderr, "VTOP: %s\n", err);
        return VP_EXIT_FAIL;
    }
    if (graph < 0) graph = utf8 ? VP_G_BRAILLE : VP_G_VT;          /* D6 */
    if (colour < 0) colour = graph != VP_G_VT ? VP_C_24 : VP_C_8;  /* D7 */

    memset(&id, 0, sizeof id);
    vpp_identity(&id);
    vpp_screen_size(&rows, &cols);
    if (!vp_app_init(&app, rows, cols, &id)) {
        vpp_stop();
        fprintf(stderr, "VTOP: no memory for a %dx%d screen\n", cols, rows);
        return VP_EXIT_FAIL;
    }
    app.graph = graph;
    app.colour = colour;
    app.interval = interval;
    vp_theme_init(pal);

    /* two samples close together, so the first screen already has figures */
    if (vpp_sample(&sample)) vp_app_sample(&app, &sample);
    scan_procs();
    vpp_key(250);
    next = vpp_now_ms();
    while (!app.quit) {
        char msg[256];
        uint64_t now = vpp_now_ms();
        int sampled = 0;
        if (now >= next || app.resample) {
            if (vpp_sample(&sample)) vp_app_sample(&app, &sample);
            scan_procs();
            sampled = 1;
            app.resample = 0;
            next = now + (uint64_t) app.interval * 1000u;
        }
        if (app.act) {
            /* confirmed (D4): to the PID the question showed */
            int ok = vpp_action(app.act, app.ask_proc.pid, app.ask_arg, msg, sizeof msg);
            app.act = 0;
            vp_app_action_done(&app, ok, msg);
            scan_procs();
        }
        if (app.flush) { vpp_flush_keys(); app.flush = 0; }
        vp_app_update_view(&app);
        if (app.detail && app.detail_pid && (app.detail_want || sampled)) {
            vp_proc_detail d;
            char err[160];
            err[0] = '\0';
            vp_app_detail(&app, vpp_proc_detail(app.detail_pid, &d, err, sizeof err), &d, err);
        }
        while (vpp_broadcast(msg, sizeof msg)) vp_app_message(&app, msg);
        if (app.measure) { vpp_measure(); app.measure = 0; }
        vpp_screen_size(&rows, &cols);
        if (!vp_app_resize(&app, rows, cols)) break;
        if (app.repaint) { vpp_repaint(); app.repaint = 0; }
        vp_app_draw(&app);
        mode.utf8 = app.graph != VP_G_VT;
        mode.colour = app.colour;
        mode.pal = pal;
        vpp_draw(&app.scr, &mode);
        /* keys until the next sample is due; each one redraws at once */
        now = vpp_now_ms();
        {
            int k = vpp_key(next > now ? (int) (next - now) : 0);
            if (k != VK_TIMEOUT) vp_app_key(&app, k);
        }
    }
    vpp_stop();
    vp_app_free(&app);
    free(pbuf);
    return VP_EXIT_OK;
}
