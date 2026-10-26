/* vp_screen.c - the screen model. No I/O. */
#include <stdlib.h>
#include <string.h>
#include "vp_screen.h"

int vp_screen_init(vp_screen *s, int rows, int cols)
{
    memset(s, 0, sizeof *s);
    if (rows < 1 || cols < 1) return 0;
    s->cell = (vp_cell *) malloc((size_t) rows * (size_t) cols * sizeof (vp_cell));
    if (!s->cell) return 0;
    s->rows = rows;
    s->cols = cols;
    vp_screen_clear(s);
    return 1;
}

void vp_screen_free(vp_screen *s) { free(s->cell); s->cell = 0; s->rows = s->cols = 0; }

void vp_screen_clear(vp_screen *s)
{
    int i;
    for (i = 0; i < s->rows * s->cols; i++) {
        s->cell[i].ch = ' ';
        s->cell[i].attr = 0;
        s->cell[i].fg = 0;
        s->cell[i].bg = 0;
    }
}

vp_cell *vp_at(vp_screen *s, int row, int col)
{
    if (row < 0 || col < 0 || row >= s->rows || col >= s->cols) return 0;
    return &s->cell[row * s->cols + col];
}

void vp_putc(vp_screen *s, int row, int col, unsigned ch, int fg, int bg, int attr)
{
    vp_cell *c = vp_at(s, row, col);
    if (!c) return;
    c->ch = (unsigned short) ch;
    c->fg = (unsigned char) fg;
    c->bg = (unsigned char) bg;
    c->attr = (unsigned char) attr;
}

/* next character of UTF-8 text; malformed bytes come back as '?' */
static unsigned next_char(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned c = *s++;
    if (c >= 0xE0 && c < 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((unsigned) (s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
    } else if (c >= 0xC2 && c < 0xE0 && (s[0] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
        s += 1;
    } else if (c >= 0x80) {
        c = '?';
        while ((*s & 0xC0) == 0x80) s++;
    }
    *p = s;
    if (c < 32 || c == 127 || (c >= 0x80 && c < 0xA0)) c = '?';
    return c;
}

static int text_cols(const char *text)
{
    const unsigned char *p = (const unsigned char *) text;
    int n = 0;
    while (*p) { next_char(&p); n++; }
    return n;
}

int vp_put(vp_screen *s, int row, int col, const char *text, int fg, int attr, int max)
{
    const unsigned char *p = (const unsigned char *) text;
    int n = 0;
    while (*p && (max < 0 || n < max)) {
        unsigned ch = next_char(&p);
        vp_cell *c = vp_at(s, row, col + n);
        if (!c) break;
        c->ch = (unsigned short) ch;
        c->fg = (unsigned char) fg;
        c->attr = (unsigned char) attr;
        n++;
    }
    return n;
}

int vp_put_right(vp_screen *s, int row, int col_end, const char *text, int fg, int attr)
{
    int n = text_cols(text);
    return vp_put(s, row, col_end - n + 1, text, fg, attr, -1);
}

void vp_fill(vp_screen *s, int row, int col, int count, unsigned ch, int fg, int bg, int attr)
{
    int i;
    for (i = 0; i < count; i++) vp_putc(s, row, col + i, ch, fg, bg, attr);
}

int vp_box(vp_screen *s, int r1, int c1, int r2, int c2, int fg, const char *title, int title_fg)
{
    int r, col = c1 + 2;
    if (r2 <= r1 || c2 <= c1) return col;
    vp_fill(s, r1, c1 + 1, c2 - c1 - 1, VP_U_HLINE, fg, 0, 0);
    vp_fill(s, r2, c1 + 1, c2 - c1 - 1, VP_U_HLINE, fg, 0, 0);
    for (r = r1 + 1; r < r2; r++) {
        vp_putc(s, r, c1, VP_U_VLINE, fg, 0, 0);
        vp_putc(s, r, c2, VP_U_VLINE, fg, 0, 0);
    }
    vp_putc(s, r1, c1, VP_U_TL, fg, 0, 0);
    vp_putc(s, r1, c2, VP_U_TR, fg, 0, 0);
    vp_putc(s, r2, c1, VP_U_BL, fg, 0, 0);
    vp_putc(s, r2, c2, VP_U_BR, fg, 0, 0);
    if (title && *title && c2 - c1 > 6) {
        vp_putc(s, r1, col++, ' ', 0, 0, 0);
        col += vp_put(s, r1, col, title, title_fg, VP_A_BOLD, c2 - col - 2);
        vp_putc(s, r1, col++, ' ', 0, 0, 0);
    }
    return col;
}

int vp_utf8(unsigned ch, char *out)
{
    if (ch < 0x80) { out[0] = (char) ch; return 1; }
    if (ch < 0x800) {
        out[0] = (char) (0xC0 | (ch >> 6));
        out[1] = (char) (0x80 | (ch & 0x3F));
        return 2;
    }
    out[0] = (char) (0xE0 | (ch >> 12));
    out[1] = (char) (0x80 | ((ch >> 6) & 0x3F));
    out[2] = (char) (0x80 | (ch & 0x3F));
    return 3;
}

int vp_screen_text(const vp_screen *s, char *out, int size)
{
    int r, c, o = 0;
    if (size < 1) return 0;
    for (r = 0; r < s->rows; r++) {
        int last = s->cols - 1;
        while (last >= 0 && s->cell[r * s->cols + last].ch == ' '
               && !(s->cell[r * s->cols + last].attr & (VP_A_BAR | VP_A_REV))) last--;
        for (c = 0; c <= last; c++) {
            const vp_cell *cl = &s->cell[r * s->cols + c];
            char u[4];
            int n;
            /* a solid bar shows as a full block, so the snapshot shows the shape */
            n = (cl->attr & VP_A_BAR) && cl->ch == ' ' ? vp_utf8(VP_U_FULL, u) : vp_utf8(cl->ch, u);
            if (o + n >= size - 2) { out[o] = '\0'; return o; }
            memcpy(out + o, u, (size_t) n);
            o += n;
        }
        out[o++] = '\n';
    }
    out[o] = '\0';
    return o;
}
