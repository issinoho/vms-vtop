/* vp_term.h - the screen model as terminal output (D6, D7). No I/O: bytes go to a
   callback. Only the cells that differ from what the terminal shows are sent.

   utf8: characters go out as UTF-8. Otherwise box drawing, scan lines and the dot
   are DEC Special Graphics (ESC ( 0), and any other non-ASCII character an ASCII
   stand-in: VT mode's panels use none, this is a safety net. */
#ifndef VP_TERM_H
#define VP_TERM_H

#include "vp_screen.h"
#include "vp_theme.h"

typedef void (*vp_emit_fn)(void *ctx, const char *bytes, int len);

typedef struct {
    int utf8;
    int colour;                 /* VP_C_* */
    const vp_rgb *pal;          /* VP_NPAL entries */
} vp_term_mode;

/* Send cur. prev is what the terminal shows (cur's size), or 0 for everything; it is
   updated to cur. */
void vp_term_render(const vp_screen *cur, vp_cell *prev, const vp_term_mode *m,
                    vp_emit_fn emit, void *ctx);
void vp_term_begin(vp_emit_fn emit, void *ctx);     /* cursor off, clear */
void vp_term_end(vp_emit_fn emit, void *ctx);       /* normal rendition, ASCII, cursor on, clear */

/* the SGR parameters a cell gets ("0;1;38;2;90;160;100"), for tests */
const char *vp_term_sgr(const vp_cell *c, const vp_term_mode *m);
/* the DEC Special Graphics character for ch, or 0 if it has none */
int vp_term_dec(unsigned ch);
/* the 256-colour index and the SGR 30-37 colour (0-7) nearest to an RGB colour */
int vp_term_256(vp_rgb c);
int vp_term_8(vp_rgb c);

#endif
