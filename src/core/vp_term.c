/* vp_term.c - screen model to terminal sequences. No I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vp_types.h"
#include "vp_term.h"

int vp_term_dec(unsigned ch)
{
    switch (ch) {
    case VP_U_HLINE: return 'q';
    case VP_U_VLINE: return 'x';
    case VP_U_TL: case 0x250C: return 'l';
    case VP_U_TR: case 0x2510: return 'k';
    case VP_U_BL: case 0x2514: return 'm';
    case VP_U_BR: case 0x2518: return 'j';
    case VP_U_LTEE: return 't';
    case VP_U_RTEE: return 'u';
    case 0x252C: return 'w';
    case 0x2534: return 'v';
    case 0x253C: return 'n';
    case VP_U_SCAN1: return 'o';
    case VP_U_SCAN3: return 'p';
    case VP_U_SCAN7: return 'r';
    case VP_U_SCAN9: return 's';
    case VP_U_DOT: return '~';
    case 0x2592: return 'a';            /* ▒ checkerboard */
    default: return 0;
    }
}

/* an ASCII stand-in for a character VT mode can't show */
static char ascii_for(unsigned ch)
{
    if (ch == VP_U_UP) return '^';
    if (ch == VP_U_DOWN) return 'v';
    if (ch == VP_U_SQUARE || ch == VP_U_FULL || (ch >= VP_U_LOWER1 && ch < VP_U_FULL)) return '#';
    if (ch >= VP_U_BRAILLE && ch <= 0x28FF) return ch == VP_U_BRAILLE ? ' ' : ':';
    return '?';
}

int vp_term_256(vp_rgb c)
{
    static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
    int r = 0, g = 0, b = 0, i, best, grey, gi, dc, dg;
    for (i = 0; i < 6; i++) {
        if (abs(lv[i] - c.r) < abs(lv[r] - c.r)) r = i;
        if (abs(lv[i] - c.g) < abs(lv[g] - c.g)) g = i;
        if (abs(lv[i] - c.b) < abs(lv[b] - c.b)) b = i;
    }
    best = 16 + 36 * r + 6 * g + b;
    grey = (c.r + c.g + c.b) / 3;
    gi = grey < 8 ? 0 : grey > 238 ? 23 : (grey - 8) / 10;
    dc = abs(lv[r] - c.r) + abs(lv[g] - c.g) + abs(lv[b] - c.b);
    dg = abs(8 + gi * 10 - c.r) + abs(8 + gi * 10 - c.g) + abs(8 + gi * 10 - c.b);
    return dg < dc ? 232 + gi : best;
}

int vp_term_8(vp_rgb c)
{
    /* dark colours are black; greys are white; otherwise the channels that are bright */
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int half = (mx + mn) / 2, k = 0;
    if (mx < 80) return 0;
    if (mx - mn < 40) return 7;
    if (c.r > half) k |= 1;
    if (c.g > half) k |= 2;
    if (c.b > half) k |= 4;
    return k ? k : 7;
}

static void colour_sgr(char *p, size_t size, int fore, int idx, const vp_term_mode *m)
{
    vp_rgb c = m->pal[idx];
    p[0] = '\0';
    if (idx == 0) return;                   /* the terminal's own */
    switch (m->colour) {
    case VP_C_24:
        snprintf(p, size, ";%d;2;%d;%d;%d", fore ? 38 : 48, c.r, c.g, c.b);
        break;
    case VP_C_256:
        snprintf(p, size, ";%d;5;%d", fore ? 38 : 48, vp_term_256(c));
        break;
    case VP_C_8:
        /* black stays the terminal's own colour, so nothing vanishes on a dark screen */
        if (vp_term_8(c)) snprintf(p, size, ";%d", (fore ? 30 : 40) + vp_term_8(c));
        break;
    default:
        break;
    }
}

const char *vp_term_sgr(const vp_cell *c, const vp_term_mode *m)
{
    static char out[64];
    char f[24], b[24];
    int a = c->attr;
    strcpy(out, "0");
    if (a & VP_A_BOLD) strcat(out, ";1");
    if (a & VP_A_UNDER) strcat(out, ";4");
    if (m->colour == VP_C_MONO) {
        if (a & (VP_A_REV | VP_A_BAR)) strcat(out, ";7");
        return out;
    }
    /* A bar is reverse video in its colour (the colour as foreground): a coloured
       block on a colour terminal, and still a block on a monochrome one, which
       ignores the colour (as vtree D13). */
    if (a & VP_A_BAR) {
        strcat(out, ";7");
        colour_sgr(f, sizeof f, 1, c->bg ? c->bg : c->fg, m);
        strcat(out, f);
        return out;
    }
    if (a & VP_A_REV) strcat(out, ";7");
    colour_sgr(f, sizeof f, 1, c->fg, m);
    colour_sgr(b, sizeof b, 0, c->bg, m);
    strcat(out, f);
    strcat(out, b);
    return out;
}

typedef struct {
    vp_emit_fn emit;
    void *ctx;
    char buf[8192];
    int n;
    char sgr[64];
    int gfx;
} out_t;

static void flush(out_t *o) { if (o->n) o->emit(o->ctx, o->buf, o->n); o->n = 0; }

static void put(out_t *o, const char *s, int len)
{
    if (o->n + len > (int) sizeof o->buf) flush(o);
    memcpy(o->buf + o->n, s, (size_t) len);
    o->n += len;
}

static void puts_(out_t *o, const char *s) { put(o, s, (int) strlen(s)); }

static void cell_out(out_t *o, const vp_cell *c, const vp_term_mode *m)
{
    const char *sgr = vp_term_sgr(c, m);
    int gfx = 0;
    char ch[4];
    int n;
    if (m->utf8) {
        n = vp_utf8(c->ch, ch);
    } else if (c->ch < 0x80) {
        ch[0] = (char) c->ch;
        n = 1;
    } else {
        int d = vp_term_dec(c->ch);
        if (d) { gfx = 1; ch[0] = (char) d; }
        else ch[0] = ascii_for(c->ch);
        n = 1;
    }
    if (strcmp(sgr, o->sgr)) {
        char s[80];
        sprintf(s, "\033[%sm", sgr);
        puts_(o, s);
        strcpy(o->sgr, sgr);
    }
    if (gfx != o->gfx) { puts_(o, gfx ? "\033(0" : "\033(B"); o->gfx = gfx; }
    put(o, ch, n);
}

void vp_term_render(const vp_screen *cur, vp_cell *prev, const vp_term_mode *m,
                    vp_emit_fn emit, void *ctx)
{
    static out_t o;
    int r, c;
    o.emit = emit;
    o.ctx = ctx;
    o.n = 0;
    o.sgr[0] = '\0';
    o.gfx = 0;                          /* every frame starts and ends in ASCII */
    puts_(&o, "\033[?25l");
    for (r = 0; r < cur->rows; r++) {
        int at = -1;                    /* the terminal cursor's column, if known */
        for (c = 0; c < cur->cols; c++) {
            const vp_cell *n = &cur->cell[r * cur->cols + c];
            vp_cell *p = prev ? &prev[r * cur->cols + c] : 0;
            if (p && p->ch == n->ch && p->attr == n->attr && p->fg == n->fg && p->bg == n->bg)
                continue;
            /* the last column of the last row would scroll some terminals: leave it */
            if (r == cur->rows - 1 && c == cur->cols - 1) continue;
            if (at != c) {
                char s[32];
                sprintf(s, "\033[%d;%dH", r + 1, c + 1);
                puts_(&o, s);
            }
            cell_out(&o, n, m);
            at = c + 1;
            if (p) *p = *n;
        }
    }
    if (o.gfx) puts_(&o, "\033(B");
    puts_(&o, "\033[0m");
    flush(&o);
}

void vp_term_begin(vp_emit_fn emit, void *ctx)
{
    const char *s = "\033[0m\033(B\033[?25l\033[H\033[2J";
    emit(ctx, s, (int) strlen(s));
}

void vp_term_end(vp_emit_fn emit, void *ctx)
{
    const char *s = "\033[0m\033(B\033[?25h\033[H\033[2J";
    emit(ctx, s, (int) strlen(s));
}
