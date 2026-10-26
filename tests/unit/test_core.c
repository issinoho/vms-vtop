/* test_core.c - unit checks of the core: formatting, the screen model, the
   renderer, the model's arithmetic, graphs and meters, the process list. */
#include <stdio.h>
#include <string.h>
#include "vp_types.h"
#include "vp_util.h"
#include "vp_screen.h"
#include "vp_theme.h"
#include "vp_term.h"
#include "vp_model.h"
#include "vp_graph.h"
#include "vp_procs.h"
#include "vp_opts.h"
#include "vp_vms.h"
#include "vp_app.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static void test_format(void)
{
    char b[32];
    vp_fmt_bytes(b, sizeof b, 0);          CHECK_STR(b, "0B");
    vp_fmt_bytes(b, sizeof b, 1023);       CHECK_STR(b, "1023B");
    vp_fmt_bytes(b, sizeof b, 1536);       CHECK_STR(b, "1.5K");
    vp_fmt_bytes(b, sizeof b, 8.0 * 1073741824.0); CHECK_STR(b, "8.0G");
    vp_fmt_bytes(b, sizeof b, 23.04e9);    CHECK_STR(b, "21G");
    vp_fmt_bytes(b, sizeof b, 1048000);    CHECK_STR(b, "1023K");
    vp_fmt_bytes(b, sizeof b, 1048570);    CHECK_STR(b, "1.0M");
    vp_fmt_uptime(b, sizeof b, 2 * 86400 + 9 * 3600 + 12 * 60 + 34); CHECK_STR(b, "2d 09:12");
    vp_fmt_uptime(b, sizeof b, 3725);      CHECK_STR(b, "01:02:05");
    vp_fmt_pct(b, sizeof b, 41.4);         CHECK_STR(b, " 41%");
    vp_fmt_pct(b, sizeof b, 140);          CHECK_STR(b, "100%");
    CHECK(vp_abbrev("gra", "GRAPH", 1));
    CHECK(!vp_abbrev("graphs", "GRAPH", 1));
    CHECK(!vp_abbrev("", "GRAPH", 1));
    {
        int r = 0, c = 0;
        CHECK(vp_parse_cpr("\033[45;132R", &r, &c) && r == 45 && c == 132);
        CHECK(vp_parse_cpr("x\033[1;2R", &r, &c) && r == 1 && c == 2);   /* typeahead first */
        CHECK(!vp_parse_cpr("\033[45;132", &r, &c));
        CHECK(!vp_parse_cpr("", &r, &c));
    }
}

static void test_screen(void)
{
    vp_screen s;
    char text[512];
    CHECK(vp_screen_init(&s, 3, 10));
    CHECK(vp_put(&s, 0, 0, "ab\xe2\xa3\xbf" "c", 1, 0, -1) == 4);   /* UTF-8 counts as one */
    CHECK(vp_at(&s, 0, 2)->ch == 0x28FF);
    CHECK(vp_put(&s, 1, 8, "xyz", 1, 0, -1) == 2);                   /* cut at the edge */
    CHECK(vp_put(&s, 2, 0, "a\tb\x80", 1, 0, -1) == 4);              /* control, bad byte */
    CHECK(vp_at(&s, 2, 1)->ch == '?' && vp_at(&s, 2, 3)->ch == '?');
    vp_screen_text(&s, text, sizeof text);
    CHECK_STR(text, "ab\xe2\xa3\xbf" "c\n        xy\na?b?\n");
    vp_screen_clear(&s);
    vp_box(&s, 0, 0, 2, 9, 1, "cpu", 2);
    vp_screen_text(&s, text, sizeof text);
    CHECK_STR(text, "\xe2\x95\xad\xe2\x94\x80" " cpu " "\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae\n"
                    "\xe2\x94\x82        \xe2\x94\x82\n"
                    "\xe2\x95\xb0\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xaf\n");
    vp_screen_free(&s);
}

static char rendered[8192];
static int rlen;
static void collect(void *ctx, const char *b, int n)
{
    (void) ctx;
    if (rlen + n < (int) sizeof rendered) { memcpy(rendered + rlen, b, (size_t) n); rlen += n; }
    rendered[rlen] = '\0';
}

static void test_term(void)
{
    vp_rgb pal[VP_NPAL];
    vp_term_mode m;
    vp_screen s;
    vp_cell c, prev[2 * 4];
    vp_theme_init(pal);
    m.pal = pal;
    m.utf8 = 1;
    memset(&c, 0, sizeof c);
    c.ch = 'x';
    c.fg = VP_K_CPU_BOX;
    c.attr = VP_A_BOLD;
    m.colour = VP_C_24;   CHECK_STR(vp_term_sgr(&c, &m), "0;1;38;2;90;160;100");
    m.colour = VP_C_256;  CHECK_STR(vp_term_sgr(&c, &m), "0;1;38;5;71");
    m.colour = VP_C_8;    CHECK_STR(vp_term_sgr(&c, &m), "0;1;32");
    m.colour = VP_C_MONO; CHECK_STR(vp_term_sgr(&c, &m), "0;1");
    c.attr = VP_A_BAR; c.bg = VP_K_CPU_BOX;
    m.colour = VP_C_MONO; CHECK_STR(vp_term_sgr(&c, &m), "0;7");
    m.colour = VP_C_8;    CHECK_STR(vp_term_sgr(&c, &m), "0;7;32");    /* a block on mono terminals too */
    m.colour = VP_C_24;   CHECK_STR(vp_term_sgr(&c, &m), "0;7;38;2;90;160;100");
    c.attr = 0; c.bg = 0; c.fg = VP_K_METER_OFF;
    m.colour = VP_C_8;    CHECK_STR(vp_term_sgr(&c, &m), "0");     /* dark: the terminal's own */
    c.fg = VP_K_DIM;      CHECK_STR(vp_term_sgr(&c, &m), "0;37");
    CHECK(vp_term_dec(VP_U_TL) == 'l' && vp_term_dec(VP_U_SCAN9) == 's' && vp_term_dec('a') == 0);

    /* VT mode: box drawing as DEC graphics, braille as an ASCII stand-in */
    CHECK(vp_screen_init(&s, 2, 4));
    vp_putc(&s, 0, 0, VP_U_TL, 0, 0, 0);
    vp_putc(&s, 0, 1, VP_U_HLINE, 0, 0, 0);
    vp_putc(&s, 0, 2, 'A', 0, 0, 0);
    vp_putc(&s, 0, 3, 0x28FF, 0, 0, 0);
    m.utf8 = 0;
    m.colour = VP_C_MONO;
    rlen = 0;
    vp_term_render(&s, 0, &m, collect, 0);
    CHECK(strstr(rendered, "\033(0lq\033(BA:") != 0);
    /* unchanged: nothing but the frame's own on/off sequences */
    memcpy(prev, s.cell, sizeof prev);
    rlen = 0;
    vp_term_render(&s, prev, &m, collect, 0);
    CHECK_STR(rendered, "\033[?25l\033[0m");
    vp_putc(&s, 1, 1, 'Z', 0, 0, 0);
    rlen = 0;
    vp_term_render(&s, prev, &m, collect, 0);
    CHECK_STR(rendered, "\033[?25l\033[2;2H\033[0mZ\033[0m");
    CHECK(prev[5].ch == 'Z');
    /* the bottom-right cell is never written: it would scroll a VT */
    vp_putc(&s, 1, 3, 'Q', 0, 0, 0);
    rlen = 0;
    vp_term_render(&s, prev, &m, collect, 0);
    CHECK(strchr(rendered, 'Q') == 0);
    m.utf8 = 1;
    vp_putc(&s, 1, 0, 0x28FF, 0, 0, 0);
    rlen = 0;
    vp_term_render(&s, prev, &m, collect, 0);
    CHECK(strstr(rendered, "\xe2\xa3\xbf") != 0);
    vp_screen_free(&s);
}

static void cpu_ticks(vp_sample *s, int cpu, unsigned busy, unsigned idle)
{
    s->cpu[cpu].t[VP_M_USER] += busy;
    s->cpu[cpu].t[VP_M_IDLE] += idle;
}

static void test_model(void)
{
    static vp_model m;
    static vp_sample s;
    memset(&s, 0, sizeof s);
    vp_model_init(&m);
    s.ncpu = 2;
    s.ms = 1000;
    s.ndisk = 1;
    strcpy(s.disk[0].name, "DKA0:");
    s.disk[0].ops = 0xFFFFFFF0u;           /* about to wrap */
    s.nnic = 1;
    strcpy(s.nic[0].name, "EWA0");
    vp_model_update(&m, &s);
    CHECK(m.h_cpu.n == 0 && m.disk_ops[0] < 0 && m.nic_rx[0] < 0);
    s.ms = 3000;
    cpu_ticks(&s, 0, 150, 50);              /* 75% */
    cpu_ticks(&s, 1, 50, 150);              /* 25% */
    s.cpu[1].t[VP_M_KERNEL] += 0;
    s.disk[0].ops += 0x30;                  /* 48 ops over 2 s, across the wrap */
    s.nic[0].rx += 4096;
    s.nic[0].tx += 2048;
    vp_model_update(&m, &s);
    CHECK(m.secs == 2.0);
    CHECK(m.core_pct[0] == 75.0f && m.core_pct[1] == 25.0f);
    CHECK(m.cpu_pct == 50.0f);
    CHECK(m.mode_pct[VP_M_USER] == 50.0f && m.mode_pct[VP_M_IDLE] == 50.0f);
    CHECK(m.disk_ops[0] == 24.0f);
    CHECK(m.nic_rx[0] == 2048.0 && m.nic_tx[0] == 1024.0);
    CHECK(m.h_cpu.n == 1 && vp_hist_get(&m.h_cpu, 0) == 50.0f);
    /* a CPU comes online: no figures from mismatched counters */
    s.ncpu = 3;
    s.ms = 5000;
    vp_model_update(&m, &s);
    CHECK(m.h_cpu.n == 1);
    {
        vp_hist h;
        int i;
        memset(&h, 0, sizeof h);
        for (i = 0; i < VP_HIST + 5; i++) vp_hist_push(&h, (float) i);
        CHECK(h.n == VP_HIST);
        CHECK(vp_hist_get(&h, 0) == (float) (VP_HIST + 4));
        CHECK(vp_hist_get(&h, VP_HIST - 1) == 5.0f);
        CHECK(vp_hist_get(&h, VP_HIST) == 0.0f);
        CHECK(vp_hist_max(&h, 3) == (float) (VP_HIST + 4));
    }
}

static void test_graph(void)
{
    vp_screen s;
    vp_hist h;
    char text[512];
    memset(&h, 0, sizeof h);
    CHECK(vp_screen_init(&s, 2, 3));
    vp_hist_push(&h, 100);
    vp_hist_push(&h, 50);
    vp_hist_push(&h, 0);
    vp_hist_push(&h, 25);
    /* braille: 2 samples per column; oldest at the left; columns before the data blank */
    vp_graph(&s, 0, 0, 2, 3, &h, 100, VP_G_BRAILLE, VP_GRAPH_UP, vp_grad);
    vp_screen_text(&s, text, sizeof text);
    /* row 0: [ - , - ] [100, 50] [0, 25];  row 1 likewise lower halves */
    CHECK(vp_at(&s, 0, 0)->ch == ' ' && vp_at(&s, 1, 0)->ch == ' ');
    CHECK(vp_at(&s, 0, 1)->ch == VP_U_BRAILLE + (0x01 | 0x02 | 0x04 | 0x40));  /* left full */
    CHECK(vp_at(&s, 1, 1)->ch == VP_U_BRAILLE + 0xFF);                     /* both full */
    CHECK(vp_at(&s, 0, 2)->ch == ' ');
    CHECK(vp_at(&s, 1, 2)->ch == VP_U_BRAILLE + (0x80 | 0x20));            /* 25%: 2 dots */
    vp_screen_clear(&s);
    vp_graph(&s, 0, 0, 2, 3, &h, 100, VP_G_BLOCK, VP_GRAPH_UP, vp_grad);
    CHECK(vp_at(&s, 1, 2)->ch == VP_U_LOWER1 + 3);                         /* 4/8 */
    /* one sample per column: 50, 0, 25 */
    CHECK(vp_at(&s, 0, 0)->ch == ' ' && vp_at(&s, 1, 0)->ch == VP_U_FULL);
    CHECK(vp_at(&s, 0, 1)->ch == ' ' && vp_at(&s, 1, 1)->ch == ' ');
    vp_screen_clear(&s);
    vp_graph(&s, 0, 0, 2, 3, &h, 100, VP_G_VT, VP_GRAPH_UP, vp_grad);
    CHECK(vp_at(&s, 1, 0)->attr == VP_A_BAR && vp_at(&s, 0, 0)->ch == ' '); /* 50%: 5 of 10 */
    CHECK(vp_at(&s, 1, 2)->ch == VP_U_HLINE);                              /* 25%: 2.5 -> 3 of 5 */
    vp_screen_clear(&s);
    vp_graph(&s, 0, 0, 2, 3, &h, 100, VP_G_VT, VP_GRAPH_DOWN, vp_grad);
    CHECK(vp_at(&s, 0, 2)->ch == VP_U_HLINE && vp_at(&s, 1, 2)->ch == ' ');
    CHECK(vp_at(&s, 0, 0)->attr == VP_A_BAR && vp_at(&s, 1, 0)->ch == ' ');
    vp_screen_clear(&s);
    vp_meter(&s, 0, 0, 3, 50, VP_G_BRAILLE);
    CHECK(vp_at(&s, 0, 1)->ch == VP_U_SQUARE && vp_at(&s, 0, 1)->fg != VP_K_METER_OFF);
    CHECK(vp_at(&s, 0, 2)->fg == VP_K_METER_OFF);
    vp_meter(&s, 1, 0, 3, 1, VP_G_VT);                                     /* any use shows */
    CHECK(vp_at(&s, 1, 0)->attr == VP_A_BAR && vp_at(&s, 1, 1)->ch == VP_U_DOT);
    vp_screen_free(&s);
}


static vp_proc mk(uint32_t pid, uint32_t owner, const char *name, const char *user,
                  uint32_t cpu, uint64_t mem)
{
    vp_proc p;
    memset(&p, 0, sizeof p);
    p.pid = pid;
    p.owner = owner;
    strcpy(p.name, name);
    strcpy(p.user, user);
    p.cputim = cpu;
    p.mem = mem;
    p.state = 5;
    return p;
}

static void test_procs(void)
{
    char b[64];
    vp_plist l;
    vp_pview v;
    vp_proc p[6];
    vp_scan sc;
    int k;

    vp_image_short(b, sizeof b, "DKA0:[SYS0.SYSCOMMON.][SYSEXE]VIM.EXE;1"); CHECK_STR(b, "VIM");
    vp_image_short(b, sizeof b, "$1$DGA1:<TOOLS>CC.EXE");                    CHECK_STR(b, "CC");
    vp_image_short(b, sizeof b, "");                                         CHECK_STR(b, "");
    vp_fmt_cputime(b, sizeof b, 1976130);      CHECK_STR(b, "05:29:21.30");    /* N2 */
    vp_fmt_cputime(b, sizeof b, 0);            CHECK_STR(b, "00:00:00.00");
    vp_fmt_cputime(b, sizeof b, 12u * 8640000u + 360000u); CHECK_STR(b, "12 01:00:00");
    vp_fmt_uic(b, sizeof b, (0100u << 16) | 1u); CHECK_STR(b, "[100,1]");
    CHECK_STR(vp_state_name(14), "CUR");
    CHECK_STR(vp_state_name(9), "SUSP");
    CHECK_STR(vp_state_name(99), "?");

    /* rates: 2 CPUs, 2 s apart; 100 ticks = 1 s of CPU = 25% of the system */
    vp_plist_init(&l);
    vp_pview_init(&v);
    memset(&sc, 0, sizeof sc);
    p[0] = mk(0x30, 0, "DCL", "IAIN", 1000, 1u << 20);
    p[1] = mk(0x10, 0, "SWAPPER", "SYSTEM", 0, 0);
    p[2] = mk(0x31, 0x30, "IAIN_1", "IAIN", 5000, 9u << 20);
    p[3] = mk(0x32, 0x30, "IAIN_2", "IAIN", 7000, 2u << 20);
    p[4] = mk(0x33, 0x32, "IAIN_3", "IAIN", 100, 3u << 20);
    p[2].dirio = 0xFFFFFFF0u;
    CHECK(vp_plist_update(&l, p, 5, &sc, 1000, 2));
    CHECK(l.n == 5 && l.row[0].p.pid == 0x10 && l.row[4].p.pid == 0x33);    /* PID order */
    CHECK(l.row[0].cpu < 0);                                                /* not yet */
    p[2].cputim += 100;
    p[2].dirio += 0x20;                 /* across the 32-bit wrap: 32 in 2 s */
    p[3].cputim += 20;
    CHECK(vp_plist_update(&l, p, 5, &sc, 3000, 2));
    k = vp_plist_find(&l, 0x31);
    CHECK(k >= 0 && l.row[k].cpu > 24.9f && l.row[k].cpu < 25.1f);
    CHECK(l.row[k].dio > 15.9f && l.row[k].dio < 16.1f);
    CHECK(vp_plist_find(&l, 0x99) < 0);

    /* sorted by CPU%, highest first; then reversed; ties by PID */
    CHECK(vp_pview_build(&v, &l, VP_S_CPU, 0, 0, ""));
    CHECK(v.n == 5 && l.row[v.line[0].row].p.pid == 0x31 && l.row[v.line[1].row].p.pid == 0x32);
    CHECK(l.row[v.line[2].row].p.pid == 0x10);                              /* 0% ties: PID */
    CHECK(vp_pview_build(&v, &l, VP_S_CPU, 1, 0, ""));
    CHECK(l.row[v.line[4].row].p.pid == 0x31);
    CHECK(vp_pview_build(&v, &l, VP_S_NAME, 0, 0, ""));
    CHECK(l.row[v.line[0].row].p.pid == 0x30);                              /* DCL first */
    CHECK(vp_sort_descending(VP_S_MEM, 0) && !vp_sort_descending(VP_S_NAME, 0)
          && vp_sort_descending(VP_S_NAME, 1));

    /* the filter: name, user or PID, any case */
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 0, "iain_"));
    CHECK(v.n == 3);
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 0, "system"));
    CHECK(v.n == 1 && l.row[v.line[0].row].p.pid == 0x10);
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 0, "00000033"));
    CHECK(v.n == 1);
    CHECK(vp_pview_find(&v, &l, 0x33) == 0 && vp_pview_find(&v, &l, 0x30) < 0);

    /* tree by owner, by PID: SWAPPER, DCL ├ IAIN_1, └ IAIN_2 └ IAIN_3 */
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 1, ""));
    CHECK(v.n == 5);
    CHECK(l.row[v.line[0].row].p.pid == 0x10 && v.line[0].depth == 0);
    CHECK(l.row[v.line[1].row].p.pid == 0x30 && v.line[1].depth == 0);
    CHECK(l.row[v.line[2].row].p.pid == 0x31 && v.line[2].depth == 1 && !v.line[2].last);
    CHECK(l.row[v.line[3].row].p.pid == 0x32 && v.line[3].depth == 1 && v.line[3].last);
    CHECK(l.row[v.line[4].row].p.pid == 0x33 && v.line[4].depth == 2 && v.line[4].last);
    CHECK(v.line[4].more == 0);         /* IAIN_2 was last: no │ beside IAIN_3 */
    /* filtered: IAIN_3's parent hidden, so it is a root */
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 1, "IAIN_3"));
    CHECK(v.n == 1 && v.line[0].depth == 0);
    /* a loop of owners (PID reuse) still shows every process once */
    p[0].owner = 0x33;
    CHECK(vp_plist_update(&l, p, 5, &sc, 5000, 2));
    CHECK(vp_pview_build(&v, &l, VP_S_PID, 0, 1, ""));
    CHECK(v.n == 5);
    /* a process gone, an empty scan */
    CHECK(vp_plist_update(&l, p, 2, &sc, 7000, 2) && l.n == 2);
    CHECK(vp_plist_update(&l, p, 0, &sc, 9000, 2) && l.n == 0);
    CHECK(vp_pview_build(&v, &l, VP_S_CPU, 0, 1, "") && v.n == 0);
    vp_pview_free(&v);
    vp_plist_free(&l);
}

static void test_opts(void)
{
    vp_opts o, back;
    char err[120], text[1024];
    int bad;
    vp_opts_init(&o);
    bad = vp_opts_parse(&o, "! comment\n# also\n\n  Graph = vt \r\nCOLOUR=mono\ninterval = 4\n"
                        "sort = Mem\nreverse = yes\ntree=off\npanel = both\nnetwork = EWA0\n", err, sizeof err);
    CHECK(bad == 0 && err[0] == '\0');
    CHECK(o.graph == VP_G_VT && o.colour == VP_C_MONO && o.interval == 4 && o.sort == VP_S_MEM);
    CHECK(o.reverse == 1 && o.tree == 0 && o.panel == VP_P_BOTH && !strcmp(o.network, "EWA0"));
    /* bad lines are counted, the first named, the good ones still taken */
    vp_opts_init(&o);
    bad = vp_opts_parse(&o, "colr = 8\ninterval = 11\nsort = cpu\njunk\ntree = maybe", err, sizeof err);
    CHECK(bad == 4 && !strcmp(err, "line 1: unknown option \"colr\"") && o.sort == VP_S_CPU);
    CHECK(o.interval == -1 && o.tree == -1);
    vp_opts_init(&o);
    vp_opts_parse(&o, "interval = 1x", err, sizeof err);
    CHECK(strstr(err, "bad value for interval") != 0);
    /* format writes only what was given, and parses back the same */
    vp_opts_init(&o);
    o.interval = 3; o.sort = VP_S_IMAGE; o.panel = VP_P_AUTO;
    CHECK(vp_opts_format(&o, text, sizeof text) > 0);
    CHECK(strstr(text, "interval = 3\n") && strstr(text, "sort = image\n") && strstr(text, "panel = auto\n"));
    CHECK(!strstr(text, "graph") && !strstr(text, "tree"));
    vp_opts_init(&back);
    CHECK(vp_opts_parse(&back, text, err, sizeof err) == 0);
    CHECK(back.interval == 3 && back.sort == VP_S_IMAGE && back.panel == VP_P_AUTO && back.graph == -1);
    o.graph = VP_G_BRAILLE; o.colour = VP_C_256; o.reverse = 0; o.tree = 1;
    strcpy(o.network, "EWB0");
    vp_opts_format(&o, text, sizeof text);
    vp_opts_init(&back);
    CHECK(vp_opts_parse(&back, text, err, sizeof err) == 0);
    CHECK(back.graph == VP_G_BRAILLE && back.colour == VP_C_256 && back.reverse == 0
          && back.tree == 1 && !strcmp(back.network, "EWB0"));
    CHECK(vp_opts_format(&o, text, 20) == -1);
}

static void test_vms(void)
{
    static vp_vms v;
    int idx[VP_MAX_QUEUE];
    memset(&v, 0, sizeof v);
    CHECK(!strcmp(vp_queue_state(VP_QS_IDLE), "idle") && !strcmp(vp_queue_state(VP_QS_BUSY), "busy"));
    CHECK(!strcmp(vp_queue_state(VP_QS_STOPPED | VP_QS_IDLE), "stopped"));
    CHECK(!strcmp(vp_queue_state(VP_QS_STALLED | VP_QS_BUSY), "stalled"));
    CHECK(!strcmp(vp_queue_state(0), "-"));
    CHECK(vp_queue_problem(VP_QS_PAUSED) && !vp_queue_problem(VP_QS_BUSY | VP_QS_STARTING));
    CHECK(!strcmp(vp_queue_kind(VP_QK_GENERIC), "generic") && !strcmp(vp_queue_kind(9), "?"));
    v.nqueue = 4;
    strcpy(v.queue[0].name, "B_IDLE"); v.queue[0].status = VP_QS_IDLE;
    strcpy(v.queue[1].name, "A_IDLE"); v.queue[1].status = VP_QS_IDLE;
    strcpy(v.queue[2].name, "Z_JOBS"); v.queue[2].status = VP_QS_IDLE; v.queue[2].hold = 2; v.queue[2].timed = 1;
    strcpy(v.queue[3].name, "Y_STALLED"); v.queue[3].status = VP_QS_STALLED;
    CHECK(vp_queue_order(&v, idx) == 4);
    CHECK(idx[0] == 3 && idx[1] == 2 && idx[2] == 1 && idx[3] == 0);
    CHECK(vp_queue_jobs(&v) == 3);
}

int main(void)
{
    test_format();
    test_screen();
    test_term();
    test_model();
    test_graph();
    test_procs();
    test_opts();
    test_vms();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
