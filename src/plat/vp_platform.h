/* vp_platform.h - the whole OS surface of VTOP (D3). No VMS types.
   src/plat/vms/ implements it with system services and SMG$ (keys only);
   src/plat/stub/ with a Linux terminal and an invented system, for trying the
   screens out. VTOP enables no privilege (D5). */
#ifndef VP_PLATFORM_H
#define VP_PLATFORM_H

#include "vp_types.h"
#include "vp_screen.h"
#include "vp_term.h"

/* exit statuses: VMS condition values there, the usual codes elsewhere */
#ifdef __VMS
#define VP_EXIT_OK   1          /* SS$_NORMAL */
#define VP_EXIT_BAD  20         /* SS$_BADPARAM */
#define VP_EXIT_FAIL 44         /* SS$_ABORT */
#else
#define VP_EXIT_OK   0
#define VP_EXIT_BAD  2
#define VP_EXIT_FAIL 1
#endif

/* Take the terminal. *utf8 is set from asking the terminal (D6): 1 if it showed a
   UTF-8 character as one column. Returns 1, or 0 with a reason in err. */
int  vpp_start(int *utf8, char *err, int errsize);
void vpp_stop(void);                    /* give the terminal back as it was */

void vpp_screen_size(int *rows, int *cols);
void vpp_draw(const vp_screen *s, const vp_term_mode *m);   /* only what changed */
void vpp_repaint(void);                 /* the next draw sends everything */
void vpp_measure(void);                 /* find the terminal's size again (Ctrl/W) */
/* a key, waiting at most ms milliseconds (0: only look); VK_TIMEOUT if none */
int  vpp_key(int ms);
/* a broadcast message that arrived, as one line; 0 if none */
int  vpp_broadcast(char *buf, int size);

uint64_t vpp_now_ms(void);
int  vpp_identity(vp_identity *id);
int  vpp_sample(vp_sample *s);          /* 0: couldn't read the CPU figures */
/* a logical name (VTOP$GRAPH) or environment variable; 0 if not defined */
int  vpp_option(const char *name, char *buf, int size);

#endif
