/* vp_graph.c - graphs and meters. No I/O. */
#include "vp_types.h"
#include "vp_theme.h"
#include "vp_graph.h"

int vp_graph_span(int w, int mode) { return mode == VP_G_BRAILLE ? w * 2 : w; }

/* braille dots, filled from the bottom (or from the top) of a cell's column */
static const unsigned char dot_left_up[4] = { 0x40, 0x04, 0x02, 0x01 };
static const unsigned char dot_right_up[4] = { 0x80, 0x20, 0x10, 0x08 };

/* height of a value in sub-cell units: anything above zero shows at least one */
static int units(double v, double max, int total)
{
    double f;
    int u;
    if (!(max > 0) || !(v > 0)) return 0;
    f = v / max;
    if (f > 1) f = 1;
    u = (int) (f * total + 0.5);
    return u < 1 ? 1 : u;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void vp_graph(vp_screen *s, int row, int col, int h, int w, const vp_hist *hist,
              double max, int mode, int dir, int (*grad)(double))
{
    int x, r;
    if (h < 1 || w < 1) return;
    for (x = 0; x < w; x++) {
        int age = (w - 1 - x) * (mode == VP_G_BRAILLE ? 2 : 1);
        for (r = 0; r < h; r++) {
            /* k: this cell's place counted from where the bars start */
            int k = dir == VP_GRAPH_UP ? h - 1 - r : r;
            int fg = grad((k + 0.5) / h);
            int rr = row + r, cc = col + x;
            if (mode == VP_G_BRAILLE) {
                int ul = units(vp_hist_get(hist, age + 1), max, h * 4) - k * 4;
                int ur = units(vp_hist_get(hist, age), max, h * 4) - k * 4;
                unsigned bits = 0;
                int i;
                ul = clampi(ul, 0, 4);
                ur = clampi(ur, 0, 4);
                if (age + 1 >= hist->n) ul = 0;
                if (age >= hist->n) ur = 0;
                for (i = 0; i < ul; i++) bits |= dir == VP_GRAPH_UP ? dot_left_up[i] : dot_left_up[3 - i];
                for (i = 0; i < ur; i++) bits |= dir == VP_GRAPH_UP ? dot_right_up[i] : dot_right_up[3 - i];
                vp_putc(s, rr, cc, bits ? VP_U_BRAILLE + bits : ' ', fg, 0, 0);
            } else if (mode == VP_G_BLOCK) {
                int u = age < hist->n ? units(vp_hist_get(hist, age), max, h * 8) - k * 8 : 0;
                u = clampi(u, 0, 8);
                if (u == 0) vp_putc(s, rr, cc, ' ', fg, 0, 0);
                else if (dir == VP_GRAPH_UP) vp_putc(s, rr, cc, VP_U_LOWER1 + (unsigned) (u - 1), fg, 0, 0);
                else vp_putc(s, rr, cc, u >= 6 ? VP_U_FULL : u >= 3 ? VP_U_UPPER : 0x2594, fg, 0, 0);
            } else {
                /* VT: 5 levels per cell; a full cell is a bar, a part is a scan line */
                static const unsigned up[5] = { VP_U_SCAN9, VP_U_SCAN7, VP_U_HLINE, VP_U_SCAN3, VP_U_SCAN1 };
                int u = age < hist->n ? units(vp_hist_get(hist, age), max, h * 5) - k * 5 : 0;
                u = clampi(u, 0, 5);
                if (u == 0) vp_putc(s, rr, cc, ' ', fg, 0, 0);
                else if (u == 5) vp_putc(s, rr, cc, ' ', fg, fg, VP_A_BAR);
                else vp_putc(s, rr, cc, dir == VP_GRAPH_UP ? up[u - 1] : up[5 - u], fg, 0, 0);
            }
        }
    }
}

void vp_meter(vp_screen *s, int row, int col, int w, double pct, int mode)
{
    int i, on;
    if (w < 1) return;
    if (!(pct > 0)) pct = 0;
    if (pct > 100) pct = 100;
    on = (int) (pct * w / 100.0 + 0.5);
    if (pct > 0 && on == 0) on = 1;
    for (i = 0; i < w; i++) {
        int fg = vp_grad((i + 0.5) / w);
        if (mode == VP_G_VT) {
            if (i < on) vp_putc(s, row, col + i, ' ', fg, fg, VP_A_BAR);
            else vp_putc(s, row, col + i, VP_U_DOT, VP_K_METER_OFF, 0, 0);
        } else {
            vp_putc(s, row, col + i, VP_U_SQUARE, i < on ? fg : VP_K_METER_OFF, 0, 0);
        }
    }
}
