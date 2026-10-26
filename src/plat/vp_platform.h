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
/* Every process the caller may see (D5): at most max into p, in any order. info->total
   says how many there were, so the caller can make room. Returns the number filled,
   or -1 if the scan failed. */
int  vpp_procs(vp_proc *p, int max, vp_scan *info);
/* one process's details; 0 with the system's message in err */
int  vpp_proc_detail(uint32_t pid, vp_proc_detail *d, char *err, int errsize);
/* VP_ACT_* on pid (arg: the priority); 1 done, else 0 with the system's message */
int  vpp_action(int act, uint32_t pid, int arg, char *msg, int msgsize);
void vpp_flush_keys(void);              /* throw typeahead away, before a question (D4) */
/* the cluster, shadow sets and queues (D9); 0 if none of it could be read */
int  vpp_vms(vp_vms *v);
/* the options file (D10): its default name, reading it whole (bytes, or -1 if there is
   none), writing a new one (1, and the name it got in msg; else 0 and why) */
void vpp_options_default(char *name, int size);
int  vpp_read_file(const char *name, char *buf, int size);
int  vpp_write_file(const char *name, const char *text, char *msg, int msgsize);
/* a logical name (VTOP$GRAPH) or environment variable; 0 if not defined */
int  vpp_option(const char *name, char *buf, int size);

#endif
