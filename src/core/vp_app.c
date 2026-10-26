/* vp_app.c - VTOP's state, keys, layout and panels. No I/O. */
#include <stdio.h>
#include <string.h>
#include "vp_app.h"
#include "vp_util.h"
#include "vp_theme.h"
#include "vp_graph.h"

static const char *graph_names[VP_NGRAPH] = { "BRAILLE", "BLOCK", "VT" };
static const char *colour_names[VP_NCOLOUR] = { "24", "256", "8", "MONO" };

const char *vp_graph_name(int g) { return g >= 0 && g < VP_NGRAPH ? graph_names[g] : "?"; }
const char *vp_colour_name(int c) { return c >= 0 && c < VP_NCOLOUR ? colour_names[c] : "?"; }

int vp_graph_parse(const char *s)
{
    /* BR and BL at least: B alone could be either */
    static const size_t min[VP_NGRAPH] = { 2, 2, 1 };
    int i;
    for (i = 0; i < VP_NGRAPH; i++) if (vp_abbrev(s, graph_names[i], min[i])) return i;
    return -1;
}

int vp_colour_parse(const char *s)
{
    int i;
    if (vp_abbrev(s, "COLOUR", 1) || vp_abbrev(s, "COLOR", 1)) return VP_C_24;
    for (i = 0; i < VP_NCOLOUR; i++) if (!vp_strcasecmp(s, colour_names[i])) return i;
    if (vp_abbrev(s, "MONOCHROME", 1)) return VP_C_MONO;
    return -1;
}

int vp_app_init(vp_app *a, int rows, int cols, const vp_identity *id)
{
    memset(a, 0, sizeof *a);
    vp_model_init(&a->model);
    if (id) a->id = *id;
    a->interval = 2;
    a->graph = VP_G_BRAILLE;
    a->colour = VP_C_24;
    return vp_screen_init(&a->scr, rows, cols);
}

void vp_app_free(vp_app *a) { vp_screen_free(&a->scr); }

int vp_app_resize(vp_app *a, int rows, int cols)
{
    if (rows == a->scr.rows && cols == a->scr.cols) return 1;
    vp_screen_free(&a->scr);
    a->repaint = 1;
    return vp_screen_init(&a->scr, rows, cols);
}

void vp_app_sample(vp_app *a, const vp_sample *s)
{
    vp_model_update(&a->model, s);
    if (a->message_ticks > 0) a->message_ticks--;
    if (a->nic >= s->nnic) a->nic = 0;
}

void vp_app_message(vp_app *a, const char *text)
{
    vp_strlcpy(a->message, text, sizeof a->message);
    a->message_ticks = 10 / (a->interval > 0 ? a->interval : 1) + 1;
}

void vp_app_key(vp_app *a, int key)
{
    int nn = a->model.cur.nnic;
    switch (key) {
    case 'q': case 'Q': case VK_F10: case VK_CTRL_Z: case VK_QUIT:
        a->quit = 1;
        break;
    case '+': case '=':
        if (a->interval < VP_MAX_INTERVAL) { a->interval++; a->resample = 1; }
        break;
    case '-': case '_':
        if (a->interval > VP_MIN_INTERVAL) { a->interval--; a->resample = 1; }
        break;
    case 'g': case 'G':
        a->graph = (a->graph + 1) % VP_NGRAPH;
        a->repaint = 1;
        break;
    case 'c': case 'C':
        a->colour = (a->colour + 1) % VP_NCOLOUR;
        a->repaint = 1;
        break;
    case 'n': case 'N':
        if (nn) a->nic = (a->nic + 1) % nn;
        break;
    case 'b': case 'B':
        if (nn) a->nic = (a->nic + nn - 1) % nn;
        break;
    case VK_CTRL_W:
        a->repaint = 1;
        a->measure = 1;
        break;
    default:
        break;
    }
}

/* ---------- layout ---------- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void vp_layout_for(int rows, int cols, vp_layout *l)
{
    int avail = rows - 1, cpu_h, lw, rest, net_h;
    cpu_h = clampi(avail / 3 + 1, 6, 16);
    lw = clampi(cols * 45 / 100, 38, 64);
    rest = avail - cpu_h;
    net_h = clampi(rest * 2 / 5, 5, 12);
    l->cpu.r1 = 0; l->cpu.c1 = 0; l->cpu.r2 = cpu_h - 1; l->cpu.c2 = cols - 1;
    l->mem.r1 = cpu_h; l->mem.c1 = 0; l->mem.r2 = avail - net_h - 1; l->mem.c2 = lw - 1;
    l->net.r1 = avail - net_h; l->net.c1 = 0; l->net.r2 = avail - 1; l->net.c2 = lw - 1;
    l->proc.r1 = cpu_h; l->proc.c1 = lw; l->proc.r2 = avail - 1; l->proc.c2 = cols - 1;
    l->status_row = rows - 1;
}

/* " text " in a box's top or bottom line at col; returns the column after it */
static int border_text(vp_screen *s, int row, int col, const char *text, int fg, int attr)
{
    vp_put(s, row, col, " ", VP_K_DEFAULT, 0, 1);
    col += 1 + vp_put(s, row, col + 1, text, fg, attr, -1);
    vp_put(s, row, col, " ", VP_K_DEFAULT, 0, 1);
    return col + 1;
}

static int text_len(const char *t)
{
    int n = 0;
    for (; *t; t++) if (((unsigned char) *t & 0xC0) != 0x80) n++;
    return n;
}

/* ---------- cpu ---------- */

static void draw_cpu(vp_app *a, const vp_rect *b)
{
    vp_screen *s = &a->scr;
    const vp_model *m = &a->model;
    const vp_sample *cur = &m->cur;
    int ih = b->r2 - b->r1 - 1, iw = b->c2 - b->c1 - 1;
    int ncpu = m->ncpu, per_col, ncols, mw, entry, core_w, cx, gw, i, col, uptime_col = b->c2;
    char t[96], u[32];

    col = vp_box(s, b->r1, b->c1, b->r2, b->c2, VP_K_CPU_BOX, "cpu", VP_K_TITLE) + 1;
    if (a->id.node[0]) col = border_text(s, b->r1, col, a->id.node, VP_K_TITLE, VP_A_BOLD) + 1;
    snprintf(t, sizeof t, "%s %s", a->id.version, a->id.arch);
    if (col + text_len(t) + 30 < b->c2) col = border_text(s, b->r1, col, t, VP_K_TEXT, 0);
    vp_fmt_uptime(u, sizeof u, cur->uptime);
    snprintf(t, sizeof t, "up %s", u);
    {
        char iv[16];
        int end;
        snprintf(iv, sizeof iv, "%ds", a->interval);
        end = b->c2 - 2 - text_len(iv) - 2;
        border_text(s, b->r1, end, iv, VP_K_HI, 0);
        end -= text_len(t) + 3;
        if (end > col) { border_text(s, b->r1, end, t, VP_K_TEXT, 0); uptime_col = end; }
    }

    /* the cores: a column of meters at the right, more columns if they don't fit */
    if (ncpu < 1) ncpu = 1;
    per_col = ih - 2 > 1 ? ih - 2 : 1;
    ncols = (ncpu + per_col - 1) / per_col;
    mw = 10;
    entry = 5 + 1 + mw + 1 + 4;
    while (ncols * (entry + 1) > iw / 2 && mw > 3) { mw--; entry--; }
    while (ncols > 1 && ncols * (entry + 1) > iw / 2) ncols--;
    core_w = ncols * (entry + 1) + 1;
    if (core_w < 5 + 1 + mw + 1 + 4 + 2) core_w = 5 + 1 + mw + 1 + 4 + 2;
    cx = b->c2 - core_w;
    gw = cx - (b->c1 + 1);
    /* the clock in the middle of the free part of the top line, clear of the divider */
    if (cur->clock[0] && uptime_col - col >= 14) {
        int cc = col + (uptime_col - col - 10) / 2;
        if (cc <= cx && cx <= cc + 10) cc = cx + 2 + 10 < uptime_col ? cx + 2 : cx - 11;
        if (cc > col) border_text(s, b->r1, cc, cur->clock, VP_K_TITLE, VP_A_BOLD);
    }
    vp_putc(s, b->r1, cx, 0x252C, VP_K_CPU_BOX, 0, 0);
    vp_putc(s, b->r2, cx, 0x2534, VP_K_CPU_BOX, 0, 0);
    for (i = b->r1 + 1; i < b->r2; i++) vp_putc(s, i, cx, VP_U_VLINE, VP_K_CPU_BOX, 0, 0);

    /* total, cores, load */
    {
        int x = cx + 2, r = b->r1 + 1;
        vp_put(s, r, x, "CPU", VP_K_TITLE, VP_A_BOLD, -1);
        vp_meter(s, r, x + 6, mw, m->cpu_pct, a->graph);
        vp_fmt_pct(t, sizeof t, m->cpu_pct);
        vp_put(s, r, x + 6 + mw + 1, t, VP_K_TITLE, VP_A_BOLD, -1);
        for (i = 0; i < m->ncpu && i < ncols * per_col; i++) {
            int rr = b->r1 + 2 + i % per_col, xx = x + (i / per_col) * (entry + 1);
            if (rr >= b->r2 - 1) continue;
            snprintf(t, sizeof t, "CPU%d", i);
            vp_put(s, rr, xx, t, VP_K_TEXT, 0, 5);
            vp_meter(s, rr, xx + 6, mw, m->core_pct[i], a->graph);
            vp_fmt_pct(t, sizeof t, m->core_pct[i]);
            vp_put(s, rr, xx + 6 + mw + 1, t, VP_K_TEXT, 0, -1);
        }
        if (m->ncpu > ncols * per_col) {
            snprintf(t, sizeof t, "+%d more", m->ncpu - ncols * per_col);
            vp_put(s, b->r2 - 2, x + (ncols - 1) * (entry + 1), t, VP_K_DIM, 0, entry);
        }
        snprintf(t, sizeof t, "load %.1f  procs %d", cur->compute_queue, cur->processes);
        vp_put(s, b->r2 - 1, x, t, VP_K_TEXT, 0, core_w - 3);
    }

    /* the graph of all CPUs, and the processor modes under it */
    vp_graph(s, b->r1 + 1, b->c1 + 1, ih - 1, gw, &m->h_cpu, 100.0, a->graph, VP_GRAPH_UP, vp_grad);
    {
        static const struct { int k; const char *n; } md[] = {
            { VP_M_USER, "user" }, { VP_M_SUPER, "super" }, { VP_M_EXEC, "exec" },
            { VP_M_KERNEL, "kernel" }, { VP_M_INT, "int" }, { VP_M_MPSYNC, "mpsync" }
        };
        int x = b->c1 + 2, k;
        for (k = 0; k < 6; k++) {
            int w;
            snprintf(t, sizeof t, "%.0f%%", m->mode_pct[md[k].k]);
            w = text_len(md[k].n) + 1 + text_len(t);
            if (x + w >= cx) break;
            vp_put(s, b->r2 - 1, x, md[k].n, VP_K_DIM, 0, -1);
            vp_put(s, b->r2 - 1, x + text_len(md[k].n) + 1, t, VP_K_TEXT, 0, -1);
            x += w + 2;
        }
    }
}

/* ---------- mem and disks ---------- */

static void mem_line(vp_app *a, int row, int c1, int iw, const char *label, double value,
                     double total)
{
    vp_screen *s = &a->scr;
    char v[16], p[8];
    double pct = total > 0 ? 100.0 * value / total : 0;
    int mw = iw - 22 - 1;
    vp_fmt_bytes(v, sizeof v, value);
    vp_fmt_pct(p, sizeof p, pct);
    vp_put(s, row, c1 + 1, label, VP_K_TEXT, 0, 9);
    vp_put_right(s, row, c1 + 1 + 9 + 5, v, VP_K_TITLE, 0);
    vp_put(s, row, c1 + 1 + 9 + 6 + 1, p, VP_K_TEXT, 0, -1);
    if (mw > 0) vp_meter(s, row, c1 + 1 + 9 + 6 + 6, mw, pct, a->graph);
}

static void fmt_ops(char *out, size_t size, float ops)
{
    if (ops < 0) snprintf(out, size, "-");
    else if (ops < 999.5f) snprintf(out, size, "%.0f", ops);
    else snprintf(out, size, "%.1fk", ops / 1000.0f);
}

static void draw_mem(vp_app *a, const vp_rect *b)
{
    vp_screen *s = &a->scr;
    const vp_sample *c = &a->model.cur;
    int ih = b->r2 - b->r1 - 1, iw = b->c2 - b->c1 - 1;
    int nd = c->ndisk, lines, shown, i, r;
    char t[32];
    double tot = (double) c->mem_total;
    /* by priority: what a short box keeps */
    struct { const char *n; double v, t; } item[5];
    item[0].n = "Used";     item[0].v = vp_mem_used(c);             item[0].t = tot;
    item[1].n = "Free";     item[1].v = (double) c->mem_free;       item[1].t = tot;
    item[2].n = "Page";     item[2].v = (double) (c->page_total - c->page_free); item[2].t = (double) c->page_total;
    item[3].n = "Modified"; item[3].v = (double) c->mem_modified;   item[3].t = tot;
    item[4].n = "Swap";     item[4].v = (double) (c->swap_total - c->swap_free); item[4].t = (double) c->swap_total;

    i = vp_box(s, b->r1, b->c1, b->r2, b->c2, VP_K_MEM_BOX, "mem", VP_K_TITLE);
    vp_fmt_bytes(t, sizeof t, tot);
    border_text(s, b->r1, i + 1, t, VP_K_TITLE, 0);

    lines = ih - 1 - nd;
    lines = clampi(lines, 2, 5);
    if (lines > ih) lines = ih;
    r = b->r1 + 1;
    {
        /* shown in a fixed order, whichever are kept */
        static const int order[5] = { 0, 1, 3, 2, 4 };
        int k;
        for (k = 0; k < 5; k++) {
            if (order[k] >= lines) continue;
            mem_line(a, r++, b->c1, iw, item[order[k]].n, item[order[k]].v, item[order[k]].t);
        }
    }
    if (r >= b->r2) return;
    /* disks: a divider with its own title, then one line each */
    vp_putc(s, r, b->c1, VP_U_LTEE, VP_K_MEM_BOX, 0, 0);
    vp_fill(s, r, b->c1 + 1, iw, VP_U_HLINE, VP_K_MEM_BOX, 0, 0);
    vp_putc(s, r, b->c2, VP_U_RTEE, VP_K_MEM_BOX, 0, 0);
    border_text(s, r, b->c1 + 2, "disks", VP_K_TITLE, VP_A_BOLD);
    border_text(s, r, b->c2 - 7, "op/s", VP_K_DIM, 0);
    r++;
    shown = b->r2 - r;
    if (nd == 0 && shown > 0) vp_put(s, r, b->c1 + 2, "no mounted disks", VP_K_DIM, 0, iw - 2);
    for (i = 0; i < nd && r < b->r2; i++, r++) {
        const vp_disk *d = &c->disk[i];
        double pct = d->total ? 100.0 * (double) (d->total - d->free) / (double) d->total : 0;
        /* a volume label has up to 12 characters: all of them when there is room */
        int lw = iw >= 44 ? 13 : 9, pc = 10 + lw, mw = iw - 9 - lw - 5 - 7;
        if (i == shown - 1 && nd > shown) {
            snprintf(t, sizeof t, "+%d more", nd - i);
            vp_put(s, r, b->c1 + 2, t, VP_K_DIM, 0, -1);
            break;
        }
        vp_put(s, r, b->c1 + 1, d->name, VP_K_TITLE, 0, 8);
        if (d->offline) vp_put(s, r, b->c1 + 10, "OFFLINE", VP_K_WARN, VP_A_BOLD, lw - 1);
        else if (d->mntver) vp_put(s, r, b->c1 + 10, "MNTVER", VP_K_WARN, VP_A_BOLD, lw - 1);
        else vp_put(s, r, b->c1 + 10, d->label, VP_K_TEXT, 0, lw - 1);
        vp_fmt_pct(t, sizeof t, pct);
        vp_put(s, r, b->c1 + pc, t, VP_K_TEXT, 0, -1);
        if (mw > 0) vp_meter(s, r, b->c1 + pc + 5, mw, pct, a->graph);
        fmt_ops(t, sizeof t, a->model.disk_ops[i]);
        vp_put_right(s, r, b->c2 - 1, t, VP_K_TEXT, 0);
    }
}

/* ---------- net ---------- */

static void rate(char *out, size_t size, double v)
{
    char b[16];
    if (v < 0) { snprintf(out, size, "-"); return; }
    vp_fmt_bytes(b, sizeof b, v);
    snprintf(out, size, "%s/s", b);
}

static void draw_net(vp_app *a, const vp_rect *b)
{
    vp_screen *s = &a->scr;
    const vp_model *m = &a->model;
    const vp_sample *c = &m->cur;
    int ih = b->r2 - b->r1 - 1, iw = b->c2 - b->c1 - 1, n = a->nic;
    int sw = 15, gw = iw - sw - 1, up_h = (ih + 1) / 2, down_h = ih - up_h, span;
    double mrx, mtx;
    char t[32], v[16];

    int tc = vp_box(s, b->r1, b->c1, b->r2, b->c2, VP_K_NET_BOX, "net", VP_K_TITLE);
    if (c->nnic == 0) {
        vp_put(s, b->r1 + 1, b->c1 + 2, "no network devices with traffic", VP_K_DIM, 0, iw - 2);
        return;
    }
    border_text(s, b->r1, tc + 1, c->nic[n].name, VP_K_TITLE, VP_A_BOLD);
    if (c->nnic > 1) border_text(s, b->r1, b->c2 - 9, "b n", VP_K_HI, 0);

    span = vp_graph_span(gw, a->graph);
    mrx = vp_hist_max(&m->h_rx[n], span);
    mtx = vp_hist_max(&m->h_tx[n], span);
    if (mrx < 1024) mrx = 1024;
    if (mtx < 1024) mtx = 1024;
    vp_graph(s, b->r1 + 1, b->c1 + 1, up_h, gw, &m->h_rx[n], mrx, a->graph, VP_GRAPH_UP, vp_netgrad);
    vp_graph(s, b->r1 + 1 + up_h, b->c1 + 1, down_h, gw, &m->h_tx[n], mtx, a->graph, VP_GRAPH_DOWN, vp_netgrad);

    {
        int x = b->c2 - sw, r = b->r1 + 1, rb = b->r2 - 1;
        rate(t, sizeof t, m->nic_rx[n]);
        vp_putc(s, r, x, VP_U_DOWN, VP_K_RX, 0, VP_A_BOLD);
        vp_put(s, r, x + 2, t, VP_K_TITLE, VP_A_BOLD, sw - 2);
        rate(t, sizeof t, m->nic_tx[n]);
        vp_putc(s, rb, x, VP_U_UP, VP_K_TX, 0, VP_A_BOLD);
        vp_put(s, rb, x + 2, t, VP_K_TITLE, VP_A_BOLD, sw - 2);
        if (ih >= 6) {
            vp_fmt_bytes(v, sizeof v, m->nic_rx_top[n]);
            snprintf(t, sizeof t, "top %s/s", v);
            vp_put(s, r + 1, x + 2, t, VP_K_DIM, 0, sw - 2);
            vp_fmt_bytes(v, sizeof v, (double) c->nic[n].rx);
            snprintf(t, sizeof t, "tot %s", v);
            vp_put(s, r + 2, x + 2, t, VP_K_DIM, 0, sw - 2);
            vp_fmt_bytes(v, sizeof v, m->nic_tx_top[n]);
            snprintf(t, sizeof t, "top %s/s", v);
            vp_put(s, rb - 1, x + 2, t, VP_K_DIM, 0, sw - 2);
            vp_fmt_bytes(v, sizeof v, (double) c->nic[n].tx);
            snprintf(t, sizeof t, "tot %s", v);
            vp_put(s, rb - 2, x + 2, t, VP_K_DIM, 0, sw - 2);
        }
    }
}

/* ---------- proc (Phase 2) ---------- */

static void draw_proc(vp_app *a, const vp_rect *b)
{
    vp_screen *s = &a->scr;
    const char *t = "process list: Phase 2";
    vp_box(s, b->r1, b->c1, b->r2, b->c2, VP_K_PROC_BOX, "proc", VP_K_TITLE);
    vp_put(s, (b->r1 + b->r2) / 2, b->c1 + (b->c2 - b->c1 - text_len(t)) / 2, t, VP_K_DIM, 0, -1);
}

/* ---------- status line ---------- */

static int key_hint(vp_screen *s, int row, int col, const char *key, const char *what)
{
    col += vp_put(s, row, col, key, VP_K_HI, VP_A_BOLD, -1);
    col += vp_put(s, row, col, " ", VP_K_TEXT, 0, -1);
    col += vp_put(s, row, col, what, VP_K_DIM, 0, -1);
    return col + 2;
}

static void draw_status(vp_app *a, int row)
{
    vp_screen *s = &a->scr;
    char t[64];
    int col = 1;
    if (a->message_ticks > 0) {
        vp_put(s, row, 1, a->message, VP_K_HI, VP_A_BOLD, s->cols - 2);
        return;
    }
    col = key_hint(s, row, col, "q", "quit");
    snprintf(t, sizeof t, "interval %ds", a->interval);
    col = key_hint(s, row, col, "+-", t);
    snprintf(t, sizeof t, "graph %s", vp_graph_name(a->graph));
    col = key_hint(s, row, col, "g", t);
    snprintf(t, sizeof t, "colour %s", vp_colour_name(a->colour));
    col = key_hint(s, row, col, "c", t);
    if (a->model.cur.nnic > 1 && col + 12 < s->cols) key_hint(s, row, col, "b n", "network");
    snprintf(t, sizeof t, "VTOP %s", VP_VERSION);
    if (col + text_len(t) + 14 < s->cols) vp_put_right(s, row, s->cols - 2, t, VP_K_DIM, 0);
}

void vp_app_draw(vp_app *a)
{
    vp_screen *s = &a->scr;
    vp_layout l;
    vp_screen_clear(s);
    if (s->rows < VP_MIN_ROWS || s->cols < VP_MIN_COLS) {
        char t[80];
        snprintf(t, sizeof t, "VTOP needs %dx%d at least (this is %dx%d)", VP_MIN_COLS,
                 VP_MIN_ROWS, s->cols, s->rows);
        vp_put(s, s->rows / 2, 0, t, VP_K_TITLE, VP_A_BOLD, s->cols);
        return;
    }
    vp_layout_for(s->rows, s->cols, &l);
    draw_cpu(a, &l.cpu);
    draw_mem(a, &l.mem);
    draw_net(a, &l.net);
    draw_proc(a, &l.proc);
    draw_status(a, l.status_row);
}
