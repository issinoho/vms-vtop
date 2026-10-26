/* vp_app.c - VTOP's state, keys, layout and panels. No I/O. */
#include <stdio.h>
#include <stdlib.h>
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
    a->sort = VP_S_CPU;
    vp_plist_init(&a->procs);
    vp_pview_init(&a->view);
    return vp_screen_init(&a->scr, rows, cols);
}

void vp_app_free(vp_app *a)
{
    vp_screen_free(&a->scr);
    vp_plist_free(&a->procs);
    vp_pview_free(&a->view);
}

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

int vp_app_procs(vp_app *a, const vp_proc *p, int n, const vp_scan *scan, uint64_t ms)
{
    return vp_plist_update(&a->procs, p, n, scan, ms, a->model.ncpu);
}

void vp_app_update_view(vp_app *a)
{
    vp_pview *v = &a->view;
    int k;
    vp_pview_build(v, &a->procs, a->sort, a->reverse, a->tree, a->filter);
    k = a->sel_pid ? vp_pview_find(v, &a->procs, a->sel_pid) : -1;
    if (k >= 0) a->sel = k;
    if (a->sel >= v->n) a->sel = v->n - 1;
    if (a->sel < 0) a->sel = 0;
    a->sel_pid = v->n ? a->procs.row[v->line[a->sel].row].p.pid : 0;
    if (a->detail && a->sel_pid && a->detail_pid != a->sel_pid) {
        a->detail_pid = a->sel_pid;
        a->detail_want = 1;
        a->det_ok = 0;
        a->det_err[0] = '\0';
    }
}

void vp_app_detail(vp_app *a, int ok, const vp_proc_detail *d, const char *err)
{
    a->detail_want = 0;
    a->det_ok = ok;
    if (ok && d) a->det = *d;
    vp_strlcpy(a->det_err, ok || !err ? "" : err, sizeof a->det_err);
}

static const char *act_verb(int act)
{
    switch (act) {
    case VP_ACT_SUSPEND: return "suspend";
    case VP_ACT_RESUME: return "resume";
    case VP_ACT_DELETE: return "delete";
    case VP_ACT_PRIORITY: return "set the priority of";
    default: return "?";
    }
}

void vp_app_action_done(vp_app *a, int ok, const char *msg)
{
    char t[256];
    static const char *done[] = { "", "suspended", "resumed", "deleted", "priority set:" };
    const vp_proc *p = &a->ask_proc;
    if (ok && a->ask_act == VP_ACT_PRIORITY)
        snprintf(t, sizeof t, "%08X %s: priority %d", (unsigned) p->pid, p->name, a->ask_arg);
    else if (ok)
        snprintf(t, sizeof t, "%08X %s %s", (unsigned) p->pid, p->name,
                 a->ask_act > 0 && a->ask_act <= 4 ? done[a->ask_act] : "done");
    else
        snprintf(t, sizeof t, "can't %s %08X: %s", act_verb(a->ask_act), (unsigned) p->pid,
                 msg && msg[0] ? msg : "failed");
    vp_app_message(a, t);
    a->ask_act = VP_ACT_NONE;
}

/* S R D P on the selected process: the question is about it as shown now (D4) */
static void ask(vp_app *a, int act)
{
    const vp_proc *p;
    char t[96];
    vp_app_update_view(a);
    if (a->view.n == 0) { vp_app_message(a, "no process selected"); return; }
    p = &a->procs.row[a->view.line[a->sel].row].p;
    if ((act == VP_ACT_SUSPEND || act == VP_ACT_DELETE) && p->pid == a->procs.scan.self) {
        snprintf(t, sizeof t, "VTOP won't %s its own process", act_verb(act));
        vp_app_message(a, t);
        return;
    }
    a->ask_proc = *p;
    a->ask_act = act;
    a->ask_arg = 0;
    a->field[0] = '\0';
    a->input = act == VP_ACT_PRIORITY ? VP_IN_PRIO : VP_IN_CONFIRM;
    a->flush = 1;
}

static void move_sel(vp_app *a, int by)
{
    vp_app_update_view(a);
    if (a->view.n == 0) return;
    a->sel += by;
    if (a->sel < 0) a->sel = 0;
    if (a->sel >= a->view.n) a->sel = a->view.n - 1;
    a->sel_pid = a->procs.row[a->view.line[a->sel].row].p.pid;
}

static int is_quit(int key)
{
    return key == 'q' || key == 'Q' || key == VK_F10 || key == VK_CTRL_Z || key == VK_QUIT;
}

/* typing into the filter, the priority, or answering Y/N */
static int input_key(vp_app *a, int key)
{
    size_t n;
    if (key == VK_QUIT) { a->quit = 1; return 1; }
    switch (a->input) {
    case VP_IN_FILTER:
        n = strlen(a->filter);
        if (key >= 32 && key < 127) {
            if (n + 1 < sizeof a->filter) { a->filter[n] = (char) key; a->filter[n + 1] = '\0'; }
        } else if (key == VK_DEL || key == VK_CTRL_H) {
            if (n) a->filter[n - 1] = '\0';
        } else if (key == VK_CTRL_U) {
            a->filter[0] = '\0';
        } else if (key == VK_RETURN || key == VK_ESC) {
            a->input = VP_IN_NONE;
        } else if (key == VK_UP || key == VK_DOWN || key == VK_PREV || key == VK_NEXT) {
            a->input = VP_IN_NONE;
            return 0;                   /* and move, too */
        }
        return 1;
    case VP_IN_PRIO:
        n = strlen(a->field);
        if (key >= '0' && key <= '9') {
            if (n < 2) { a->field[n] = (char) key; a->field[n + 1] = '\0'; }
        } else if (key == VK_DEL || key == VK_CTRL_H) {
            if (n) a->field[n - 1] = '\0';
        } else if (key == VK_RETURN && n) {
            int v = atoi(a->field);
            if (v > 31) {
                vp_app_message(a, "a priority is 0 to 31: nothing changed");
                a->input = VP_IN_NONE;
                a->ask_act = VP_ACT_NONE;
            } else {
                a->ask_arg = v;
                a->input = VP_IN_CONFIRM;
                a->flush = 1;
            }
        } else {
            vp_app_message(a, "nothing changed");
            a->input = VP_IN_NONE;
            a->ask_act = VP_ACT_NONE;
        }
        return 1;
    case VP_IN_CONFIRM:
        a->input = VP_IN_NONE;
        if (key == 'y' || key == 'Y') {
            a->act = a->ask_act;
        } else {
            vp_app_message(a, "nothing done");
            a->ask_act = VP_ACT_NONE;
        }
        return 1;
    default:
        return 0;
    }
}

void vp_app_key(vp_app *a, int key)
{
    int nn = a->model.cur.nnic, page = a->list_rows > 1 ? a->list_rows - 1 : 1;
    if (a->input != VP_IN_NONE && input_key(a, key)) return;
    if (a->help) {
        a->help = 0;
        if (key != VK_QUIT) return;     /* any key closes the help */
    }
    if (is_quit(key)) { a->quit = 1; return; }
    switch (key) {
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
    case 'h': case 'H': case '?': case VK_HELP: case VK_PF2:
        a->help = 1;
        break;
    /* the process list */
    case VK_UP: move_sel(a, -1); break;
    case VK_DOWN: move_sel(a, 1); break;
    case VK_PREV: move_sel(a, -page); break;
    case VK_NEXT: move_sel(a, page); break;
    case VK_LEFT: case '<': case ',':
        a->sort = (a->sort + VP_NSORT - 1) % VP_NSORT;
        break;
    case VK_RIGHT: case '>': case '.':
        a->sort = (a->sort + 1) % VP_NSORT;
        break;
    case 'r':
        a->reverse = !a->reverse;
        break;
    case 't': case 'T':
        a->tree = !a->tree;
        break;
    case '/': case 'f': case 'F':
        a->input = VP_IN_FILTER;
        break;
    case 'i': case 'I': case VK_RETURN: case VK_DO:
        a->detail = !a->detail;
        a->detail_pid = 0;              /* the next view update asks for it */
        a->det_ok = 0;
        break;
    /* actions (D4): capitals only, so a lowercase slip does nothing */
    case 'S': ask(a, VP_ACT_SUSPEND); break;
    case 'R': ask(a, VP_ACT_RESUME); break;
    case 'D': ask(a, VP_ACT_DELETE); break;
    case 'P': ask(a, VP_ACT_PRIORITY); break;
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

/* ---------- proc (D8) ---------- */

/* the columns, in the order shown; prio: the order they are dropped in, last first */
static const struct { int w, right, prio; } pcol[VP_NSORT] = {
    /* PID */ { 8, 0, 0 }, /* Name */ { 15, 0, 1 }, /* User */ { 12, 0, 5 },
    /* St */ { 5, 0, 3 }, /* Pri */ { 4, 1, 6 }, /* CPU% */ { 5, 1, 2 },
    /* CPU time */ { 11, 1, 7 }, /* Mem */ { 5, 1, 4 }, /* DIO/s */ { 6, 1, 8 },
    /* BIO/s */ { 6, 1, 9 }, /* Image */ { 8, 0, 10 }
};

/* which columns fit in w, and where each starts (-1: not shown) */
static void proc_columns(int w, int tree, int x0, int *x, int *cw)
{
    int used = 0, shown[VP_NSORT], p, c, n = 0;
    for (c = 0; c < VP_NSORT; c++) { shown[c] = 0; cw[c] = pcol[c].w; x[c] = -1; }
    if (tree) cw[VP_S_NAME] += 6;
    for (p = 0; p < VP_NSORT; p++)
        for (c = 0; c < VP_NSORT; c++)
            if (pcol[c].prio == p && used + (n ? 1 : 0) + cw[c] <= w) {
                shown[c] = 1;
                used += (n ? 1 : 0) + cw[c];
                n++;
            }
    if (shown[VP_S_IMAGE]) {
        cw[VP_S_IMAGE] += w - used;
        if (cw[VP_S_IMAGE] > 39) cw[VP_S_IMAGE] = 39;
    } else if (shown[VP_S_NAME]) {
        cw[VP_S_NAME] += w - used;
        if (cw[VP_S_NAME] > 15 + (tree ? 12 : 0)) cw[VP_S_NAME] = 15 + (tree ? 12 : 0);
    }
    for (c = 0; c < VP_NSORT; c++)
        if (shown[c]) { x[c] = x0; x0 += cw[c] + 1; }
}

static void fmt_rate(char *out, size_t size, float v)
{
    if (v < 0) snprintf(out, size, "-");
    else if (v < 9.95f && v > 0) snprintf(out, size, "%.1f", v);
    else if (v < 999.5f) snprintf(out, size, "%.0f", v);
    else snprintf(out, size, "%.1fk", v / 1000.0f);
}

static void put_cell(vp_screen *s, int row, int x, int w, int right, const char *t, int fg,
                     int attr)
{
    if (right) {
        int n = text_len(t);
        if (n > w) n = w;
        vp_put(s, row, x + w - n, t, fg, attr, n);
    } else {
        vp_put(s, row, x, t, fg, attr, w);
    }
}

static void tree_prefix(char *out, size_t size, const vp_pline *ln)
{
    int d;
    out[0] = '\0';
    for (d = 1; d <= ln->depth && strlen(out) + 8 < size; d++) {
        if (d < ln->depth) strcat(out, ln->more & (1u << d) ? "\xe2\x94\x82 " : "  ");
        else strcat(out, ln->last ? "\xe2\x94\x94\xe2\x94\x80" : "\xe2\x94\x9c\xe2\x94\x80");
    }
}

static void proc_line(vp_app *a, int row, int c1, int iw, const int *x, const int *cw,
                      const vp_pline *ln, int selected)
{
    vp_screen *s = &a->scr;
    const vp_prow *r = &a->procs.row[ln->row];
    const vp_proc *p = &r->p;
    int at = selected ? VP_A_REV : 0, c;
    char t[64], u[48];
    if (selected) vp_fill(s, row, c1 + 1, iw, ' ', VP_K_TEXT, 0, VP_A_REV);
    for (c = 0; c < VP_NSORT; c++) {
        int fg = VP_K_TEXT, attr = at;
        if (x[c] < 0) continue;
        t[0] = '\0';
        switch (c) {
        case VP_S_PID: snprintf(t, sizeof t, "%08X", (unsigned) p->pid); fg = VP_K_DIM; break;
        case VP_S_NAME:
            tree_prefix(u, sizeof u, ln);
            if (u[0]) {
                int n = vp_put(s, row, x[c], u, selected ? VP_K_TEXT : VP_K_DIM, at, cw[c]);
                vp_put(s, row, x[c] + n, p->name, selected ? VP_K_TEXT : VP_K_TITLE,
                       at | VP_A_BOLD, cw[c] - n);
                continue;
            }
            vp_strlcpy(t, p->name, sizeof t);
            fg = VP_K_TITLE;
            attr |= VP_A_BOLD;
            break;
        case VP_S_USER: vp_strlcpy(t, p->user, sizeof t); break;
        case VP_S_STATE:
            vp_strlcpy(t, vp_state_name(p->state), sizeof t);
            if (p->state == 9 || p->state == 10) { fg = VP_K_WARN; attr |= VP_A_BOLD; }
            else if (p->state == 14) fg = VP_K_HI;
            break;
        case VP_S_PRI: snprintf(t, sizeof t, "%d", p->pri); break;
        case VP_S_CPU:
            if (r->cpu < 0) { vp_strlcpy(t, "-", sizeof t); fg = VP_K_DIM; }
            else {
                snprintf(t, sizeof t, r->cpu < 9.95f ? "%.1f" : "%.0f", r->cpu);
                fg = r->cpu >= 0.05f ? vp_grad(r->cpu / 100.0) : VP_K_DIM;
            }
            break;
        case VP_S_TIME: vp_fmt_cputime(t, sizeof t, p->cputim); break;
        case VP_S_MEM: vp_fmt_bytes(t, sizeof t, (double) p->mem); break;
        case VP_S_DIO: fmt_rate(t, sizeof t, r->dio); if (r->dio <= 0) fg = VP_K_DIM; break;
        case VP_S_BIO: fmt_rate(t, sizeof t, r->bio); if (r->bio <= 0) fg = VP_K_DIM; break;
        case VP_S_IMAGE: vp_strlcpy(t, p->image, sizeof t); fg = VP_K_DIM; break;
        default: break;
        }
        if (selected) fg = VP_K_TEXT;       /* one colour across the line, reversed */
        put_cell(s, row, x[c], cw[c], pcol[c].right, t, fg, attr);
    }
}

/* "label value" pieces, flowed into lines r..rmax between c1 and c2 */
typedef struct { vp_screen *s; int r, c, c1, c2, rmax; } flow;

static void flow_break(flow *f) { if (f->c > f->c1) { f->r++; f->c = f->c1; } }

/* gap: the columns before it on the same line */
static void flow_gap(flow *f, const char *label, const char *value, int vfg, int vattr, int gap)
{
    int w = (label[0] ? text_len(label) + 1 : 0) + text_len(value);
    if (f->c > f->c1 && f->c + gap + w > f->c2) flow_break(f);
    else if (f->c > f->c1) f->c += gap;
    if (f->r > f->rmax) return;
    if (label[0]) {
        f->c += vp_put(f->s, f->r, f->c, label, VP_K_DIM, 0, f->c2 - f->c + 1);
        f->c += vp_put(f->s, f->r, f->c, " ", VP_K_DIM, 0, f->c2 - f->c + 1);
    }
    f->c += vp_put(f->s, f->r, f->c, value, vfg, vattr, f->c2 - f->c + 1);
}

static void flow_put(flow *f, const char *label, const char *value, int vfg, int vattr)
{
    flow_gap(f, label, value, vfg, vattr, 2);
}

static void draw_detail(vp_app *a, int r1, int r2, int c1, int c2)
{
    vp_screen *s = &a->scr;
    const vp_proc_detail *d = &a->det;
    int k = vp_plist_find(&a->procs, a->detail_pid);
    const vp_proc *p = k >= 0 ? &a->procs.row[k].p : 0;
    flow f;
    char t[96], u[32];
    f.s = s; f.r = r1; f.c = c1; f.c1 = c1; f.c2 = c2; f.rmax = r2;
    if (!p) { vp_put(s, r1, c1, "the process has gone", VP_K_DIM, 0, c2 - c1 + 1); return; }
    snprintf(t, sizeof t, "%08X", (unsigned) p->pid);
    flow_put(&f, "", t, VP_K_DIM, 0);
    flow_put(&f, "", p->name, VP_K_TITLE, VP_A_BOLD);
    flow_put(&f, "user", p->user, VP_K_TEXT, 0);
    vp_fmt_uic(u, sizeof u, p->uic);
    flow_put(&f, "", u, VP_K_TEXT, 0);
    if (a->det_ok && d->account[0]) flow_put(&f, "account", d->account, VP_K_TEXT, 0);
    flow_break(&f);
    if (!a->det_ok) {
        flow_put(&f, "", a->det_err[0] ? a->det_err : "reading the details...",
                 a->det_err[0] ? VP_K_WARN : VP_K_DIM, 0);
        return;
    }
    flow_put(&f, "image", d->image[0] ? d->image : "none (at DCL level)",
             d->image[0] ? VP_K_TEXT : VP_K_DIM, 0);
    flow_break(&f);
    flow_put(&f, "state", vp_state_name(p->state), VP_K_TEXT, 0);
    snprintf(t, sizeof t, "%d/%d", p->pri, p->prib);
    flow_put(&f, "pri", t, VP_K_TEXT, 0);
    flow_put(&f, "mode", vp_mode_name(p->mode), VP_K_TEXT, 0);
    snprintf(t, sizeof t, "%d", p->threads);
    flow_put(&f, "threads", t, VP_K_TEXT, 0);
    if (p->term[0]) flow_put(&f, "term", p->term, VP_K_TEXT, 0);
    if (p->owner) {
        snprintf(t, sizeof t, "%08X", (unsigned) p->owner);
        flow_put(&f, "owner", t, VP_K_TEXT, 0);
    }
    flow_put(&f, "login", d->login, VP_K_TEXT, 0);
    vp_fmt_cputime(t, sizeof t, p->cputim);
    flow_put(&f, "CPU", t, VP_K_TEXT, 0);
    {
        char ws[16], q[16], e[16];
        vp_fmt_bytes(ws, sizeof ws, (double) d->wssize);
        vp_fmt_bytes(q, sizeof q, (double) d->wsquota);
        vp_fmt_bytes(e, sizeof e, (double) d->wsextent);
        snprintf(t, sizeof t, "%s quota %s extent %s", ws, q, e);
        flow_put(&f, "WS", t, VP_K_TEXT, 0);
    }
    snprintf(t, sizeof t, "%lu", (unsigned long) p->faults);
    flow_put(&f, "faults", t, VP_K_TEXT, 0);
    flow_break(&f);
    for (k = 0; k < d->nquota; k++) {
        const vp_quota *q = &d->quota[k];
        int fg = q->limit && q->used >= q->limit - q->limit / 10 ? VP_K_WARN : VP_K_TEXT;
        if (q->bytes) {
            char x1[16], x2[16];
            vp_fmt_bytes(x1, sizeof x1, (double) q->used);
            vp_fmt_bytes(x2, sizeof x2, (double) q->limit);
            snprintf(t, sizeof t, "%s/%s", x1, x2);
        } else {
            snprintf(t, sizeof t, "%lu/%lu", (unsigned long) q->used, (unsigned long) q->limit);
        }
        flow_put(&f, q->name, t, fg, 0);
    }
    flow_break(&f);
    {
        /* privileges, one word at a time so that they wrap */
        char w[400];
        char *tok;
        int first = 1;
        vp_strlcpy(w, d->privs[0] ? d->privs : "none", sizeof w);
        for (tok = strtok(w, " "); tok; tok = strtok(0, " ")) {
            flow_gap(&f, first ? "privs" : "", tok, VP_K_TEXT, 0, first ? 2 : 1);
            first = 0;
        }
    }
}

static void draw_proc(vp_app *a, const vp_rect *b)
{
    vp_screen *s = &a->scr;
    const vp_plist *l = &a->procs;
    vp_pview *v = &a->view;
    int iw = b->c2 - b->c1 - 1, x[VP_NSORT], cw[VP_NSORT], c, r, list_top, i;
    char t[96];

    vp_app_update_view(a);
    c = vp_box(s, b->r1, b->c1, b->r2, b->c2, VP_K_PROC_BOX, "proc", VP_K_TITLE);
    if (l->scans) {
        static const char *scope[] = { "own only", "GROUP", "WORLD" };
        if (v->n != l->n) snprintf(t, sizeof t, "%d of %d", v->n, l->n);
        else snprintf(t, sizeof t, "%d", l->n);
        c = border_text(s, b->r1, c + 1, t, VP_K_TITLE, 0);
        snprintf(t, sizeof t, "%s", scope[l->scan.scope >= 0 && l->scan.scope <= 2 ? l->scan.scope : 0]);
        if (l->scan.hidden && l->scan.scope != VP_SCOPE_WORLD)
            snprintf(t, sizeof t, "%s, %d hidden", scope[l->scan.scope], l->scan.hidden);
        c = border_text(s, b->r1, c, t, l->scan.scope == VP_SCOPE_WORLD ? VP_K_DIM : VP_K_WARN, 0);
    }
    if (a->tree && c + 8 < b->c2) c = border_text(s, b->r1, c, "tree", VP_K_HI, VP_A_BOLD);
    if (a->filter[0] || a->input == VP_IN_FILTER) {
        int room = b->c2 - 2 - c - 9;
        if (room > 4) {
            vp_put(s, b->r1, c, " filter ", VP_K_DIM, 0, -1);
            c += 8;
            c += vp_put(s, b->r1, c, a->filter, VP_K_HI, VP_A_BOLD, room);
            if (a->input == VP_IN_FILTER) vp_putc(s, b->r1, c++, ' ', VP_K_HI, 0, VP_A_REV);
            vp_put(s, b->r1, c, " ", VP_K_DIM, 0, 1);
        }
    }
    /* the keys, in the bottom line */
    {
        int col = b->c1 + 2, end = b->c2 - 1;
        snprintf(t, sizeof t, "%s%s", vp_sort_name(a->sort),
                 vp_sort_descending(a->sort, a->reverse) ? " high" : " low");
        if (col + 6 + text_len(t) < end) {
            vp_put(s, b->r2, col, " < > ", VP_K_HI, VP_A_BOLD, -1);
            col = border_text(s, b->r2, col + 4, t, VP_K_TEXT, 0);
        }
        if (col + 10 < end) col = border_text(s, b->r2, col, "/ filter", VP_K_HI, 0);
        if (col + 8 < end) col = border_text(s, b->r2, col, "t tree", VP_K_HI, 0);
        if (col + 8 < end) col = border_text(s, b->r2, col, "i info", VP_K_HI, 0);
        if (col + 15 < end) border_text(s, b->r2, col, "S R D P act", VP_K_HI, 0);
    }

    r = b->r1 + 1;
    if (a->detail) {
        int ih = b->r2 - b->r1 - 1, dh = ih >= 16 ? 8 : ih;
        draw_detail(a, r, r + dh - 1, b->c1 + 2, b->c2 - 2);
        r += dh;
        if (r >= b->r2) { a->list_rows = 1; return; }
        vp_putc(s, r, b->c1, VP_U_LTEE, VP_K_PROC_BOX, 0, 0);
        vp_fill(s, r, b->c1 + 1, iw, VP_U_HLINE, VP_K_PROC_BOX, 0, 0);
        vp_putc(s, r, b->c2, VP_U_RTEE, VP_K_PROC_BOX, 0, 0);
        r++;
    }
    proc_columns(iw - 2, a->tree, b->c1 + 2, x, cw);
    for (c = 0; c < VP_NSORT; c++) {
        int fg = c == a->sort ? VP_K_HI : VP_K_DIM;
        if (x[c] < 0) continue;
        put_cell(s, r, x[c], cw[c], pcol[c].right, vp_sort_name(c), fg, VP_A_BOLD);
    }
    r++;
    list_top = r;
    a->list_rows = b->r2 - list_top;
    if (a->list_rows < 1) { a->list_rows = 1; return; }
    if (!l->scans) {
        vp_put(s, list_top, b->c1 + 2, "reading the processes...", VP_K_DIM, 0, iw - 2);
        return;
    }
    if (v->n == 0) {
        vp_put(s, list_top, b->c1 + 2, a->filter[0] ? "no process matches the filter"
               : "no processes", VP_K_DIM, 0, iw - 2);
        return;
    }
    if (a->sel < a->top) a->top = a->sel;
    if (a->sel >= a->top + a->list_rows) a->top = a->sel - a->list_rows + 1;
    if (a->top > v->n - a->list_rows) a->top = v->n - a->list_rows;
    if (a->top < 0) a->top = 0;
    for (i = 0; i < a->list_rows && a->top + i < v->n; i++)
        proc_line(a, list_top + i, b->c1, iw, x, cw, &v->line[a->top + i], a->top + i == a->sel);
}

/* ---------- boxes over the screen: a question, the help ---------- */

static void overlay(vp_screen *s, int r1, int c1, int h, int w, int fg, const char *title)
{
    int r;
    for (r = r1; r < r1 + h; r++) vp_fill(s, r, c1, w, ' ', VP_K_TEXT, 0, 0);
    vp_box(s, r1, c1, r1 + h - 1, c1 + w - 1, fg, title, VP_K_TITLE);
}

static void draw_question(vp_app *a)
{
    vp_screen *s = &a->scr;
    const vp_proc *p = &a->ask_proc;
    int w = s->cols - 4 < 64 ? s->cols - 4 : 64, h = 7, r1 = (s->rows - h) / 2,
        c1 = (s->cols - w) / 2, col;
    char t[128];
    static const char *title[] = { "", "suspend", "resume", "delete", "priority" };
    overlay(s, r1, c1, h, w, VP_K_WARN, title[a->ask_act > 0 && a->ask_act <= 4 ? a->ask_act : 0]);
    col = c1 + 2;
    col += vp_put(s, r1 + 1, col, "PID ", VP_K_DIM, 0, -1);
    snprintf(t, sizeof t, "%08X  ", (unsigned) p->pid);
    col += vp_put(s, r1 + 1, col, t, VP_K_TEXT, 0, -1);
    col += vp_put(s, r1 + 1, col, p->name, VP_K_TITLE, VP_A_BOLD, -1);
    col += vp_put(s, r1 + 1, col, "  user ", VP_K_DIM, 0, -1);
    vp_put(s, r1 + 1, col, p->user, VP_K_TEXT, 0, c1 + w - 2 - col);
    col = c1 + 2;
    col += vp_put(s, r1 + 2, col, "image ", VP_K_DIM, 0, -1);
    vp_put(s, r1 + 2, col, p->image[0] ? p->image : "none", VP_K_TEXT, 0, c1 + w - 2 - col);
    if (a->input == VP_IN_PRIO) {
        snprintf(t, sizeof t, "New priority, 0 to 31 (now %d, base %d): ", p->pri, p->prib);
        col = c1 + 2 + vp_put(s, r1 + 4, c1 + 2, t, VP_K_TEXT, 0, w - 4);
        col += vp_put(s, r1 + 4, col, a->field, VP_K_HI, VP_A_BOLD, -1);
        vp_putc(s, r1 + 4, col, ' ', VP_K_HI, 0, VP_A_REV);
        vp_put(s, r1 + 5, c1 + 2, "Return to go on, any other key to leave it", VP_K_DIM, 0, w - 4);
        return;
    }
    switch (a->ask_act) {
    case VP_ACT_SUSPEND: vp_strlcpy(t, "Suspend this process?", sizeof t); break;
    case VP_ACT_RESUME: vp_strlcpy(t, "Resume this process?", sizeof t); break;
    case VP_ACT_DELETE: vp_strlcpy(t, "Delete this process? It ends at once.", sizeof t); break;
    default: snprintf(t, sizeof t, "Set its priority to %d?", a->ask_arg); break;
    }
    col = c1 + 2 + vp_put(s, r1 + 4, c1 + 2, t, VP_K_TITLE, VP_A_BOLD, w - 4);
    vp_put(s, r1 + 4, col + 1, "Y/N [N]", VP_K_HI, VP_A_BOLD, c1 + w - 2 - col);
}

static void draw_help(vp_app *a)
{
    static const char *lines[][2] = {
        { "q  F10  Ctrl/Z", "quit (Ctrl/C and Ctrl/Y too)" },
        { "+ -", "refresh interval, 1 to 10 seconds" },
        { "g", "graphs: braille, block or VT" },
        { "c", "colours: 24-bit, 256, 8 or mono" },
        { "b n", "the network device shown" },
        { "Ctrl/W", "measure the terminal and redraw" },
        { "Up Down Prev Next", "select a process" },
        { "< > Left Right", "sort column;  r  reverses it" },
        { "t", "tree by owner process" },
        { "/  f", "filter: name, user, image or PID" },
        { "i  Return", "details of the process" },
        { "S  R", "suspend, resume (asks first)" },
        { "D  P", "delete, set priority (asks first)" },
        { "h  ?  Help", "this list; any key closes it" },
    };
    vp_screen *s = &a->scr;
    int n = (int) (sizeof lines / sizeof lines[0]), w = s->cols - 4 < 58 ? s->cols - 4 : 58,
        h = n + 2 < s->rows - 2 ? n + 2 : s->rows - 2, r1 = (s->rows - h) / 2,
        c1 = (s->cols - w) / 2, i;
    overlay(s, r1, c1, h, w, VP_K_HI, "keys");
    for (i = 0; i < n && i < h - 2; i++) {
        vp_put(s, r1 + 1 + i, c1 + 2, lines[i][0], VP_K_HI, VP_A_BOLD, 18);
        vp_put(s, r1 + 1 + i, c1 + 21, lines[i][1], VP_K_TEXT, 0, w - 23);
    }
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
    col = key_hint(s, row, col, "h", "help");
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
    if (a->input == VP_IN_PRIO || a->input == VP_IN_CONFIRM) draw_question(a);
    else if (a->help) draw_help(a);
}
