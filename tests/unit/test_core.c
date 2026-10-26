/* test_core.c - unit checks of the core: formatting, the screen model, the
   renderer, the model's arithmetic, graphs and meters. */
#include <stdio.h>
#include <string.h>
#include "vp_types.h"
#include "vp_util.h"
#include "vp_screen.h"
#include "vp_theme.h"
#include "vp_term.h"
#include "vp_model.h"
#include "vp_graph.h"

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

int main(void)
{
    test_format();
    test_screen();
    test_term();
    test_model();
    test_graph();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
