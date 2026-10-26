/* vp_screen.h - the screen model: a grid of cells, each a Unicode character (BMP),
   attributes and two palette colours. The panels draw into it; vp_term turns it into
   terminal output for the graph and colour modes (D6, D7). No I/O. */
#ifndef VP_SCREEN_H
#define VP_SCREEN_H

#define VP_A_BOLD  1
#define VP_A_REV   2
#define VP_A_UNDER 4
#define VP_A_BAR   8    /* a solid bar cell: its bg colour, or reverse video in MONO */

/* characters the renderer knows how to show in VT mode (DEC Special Graphics) */
#define VP_U_HLINE  0x2500
#define VP_U_VLINE  0x2502
#define VP_U_TL     0x256D      /* rounded corners: ╭ ╮ ╰ ╯ */
#define VP_U_TR     0x256E
#define VP_U_BL     0x2570
#define VP_U_BR     0x256F
#define VP_U_LTEE   0x251C
#define VP_U_RTEE   0x2524
#define VP_U_SCAN1  0x23BA      /* ⎺ ⎻ ─ ⎼ ⎽: scan lines 1, 3, 5, 7, 9 */
#define VP_U_SCAN3  0x23BB
#define VP_U_SCAN7  0x23BC
#define VP_U_SCAN9  0x23BD
#define VP_U_DOT    0x00B7      /* · */
#define VP_U_SQUARE 0x25A0      /* ■ */
#define VP_U_UP     0x25B2      /* ▲ */
#define VP_U_DOWN   0x25BC      /* ▼ */
#define VP_U_BRAILLE 0x2800
#define VP_U_LOWER1 0x2581      /* ▁ .. █ = 0x2581 .. 0x2588 */
#define VP_U_FULL   0x2588
#define VP_U_UPPER  0x2580      /* ▀ */

typedef struct { unsigned short ch; unsigned char attr, fg, bg; } vp_cell;

typedef struct {
    int rows, cols;
    vp_cell *cell;              /* rows * cols */
} vp_screen;

int  vp_screen_init(vp_screen *s, int rows, int cols);     /* 0: no memory */
void vp_screen_free(vp_screen *s);
void vp_screen_clear(vp_screen *s);
vp_cell *vp_at(vp_screen *s, int row, int col);            /* 0 outside */

void vp_putc(vp_screen *s, int row, int col, unsigned ch, int fg, int bg, int attr);
/* UTF-8 text at (row, col), at most max columns (max < 0: to the edge), bg left as it
   is. Control characters show as '?'. Returns the columns written. */
int  vp_put(vp_screen *s, int row, int col, const char *text, int fg, int attr, int max);
/* the same, right-aligned so that it ends at column col_end */
int  vp_put_right(vp_screen *s, int row, int col_end, const char *text, int fg, int attr);
void vp_fill(vp_screen *s, int row, int col, int count, unsigned ch, int fg, int bg, int attr);
/* a box with rounded corners and a title, shown as "╭─ cpu ─"; returns the column
   after the title's closing space */
int  vp_box(vp_screen *s, int r1, int c1, int r2, int c2, int fg, const char *title, int title_fg);

/* the grid as UTF-8, one line per row, trailing blanks dropped: for snapshot tests */
int  vp_screen_text(const vp_screen *s, char *out, int size);
/* one character as UTF-8 into out (at least 4 bytes); returns the length */
int  vp_utf8(unsigned ch, char *out);

#endif
