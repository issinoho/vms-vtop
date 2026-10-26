/* ptydrive.c - drive an interactive DCL on a pseudo-terminal from a script.

   Usage (as a foreign command):  PTYDRIVE script-file

   Creates a pseudo-terminal (VT100, ANSI, line editing), spawns DCL on it with
   LIB$SPAWN /NOWAIT, then for each script line types the text, waits, and prints what
   DCL sent back with control characters made visible (<CR>, <ESC>, <07>...).
   PTD$ event ASTs are marked inline as {CHAR}, {RESUME}.

   Script lines:   <seconds> <label> | <text>
   with \r, \e, \\ and \xHH escapes in <text>; blank lines and lines starting with
   # are skipped. Example:   1.5 recall previous | \e[A\r

   Bounded on purpose (NOTES N2): at most MAX_STEPS lines, each waits a fixed
   time, and the child is ended at the end whatever state it is in.
   Built from probes/probe_ptd.c (NOTES N4-N6). */
#define __NEW_STARLET 1
#include <stdio.h>
#include <string.h>
#include <descrip.h>
#include <starlet.h>
#include <lib$routines.h>
#include <ssdef.h>
#include <dvidef.h>
#include <dcdef.h>
#include <ttdef.h>
#include <tt2def.h>
#include <ptddef.h>
#include <gen64def.h>
#include <va_rangedef.h>
#include <stdlib.h>
#include "probe_common.h"

#define RBUF_OFF   0       /* read buffer: word status, word count, data */
#define WBUF_OFF   1024    /* write buffer: same layout */
#define DATA_MAX   508

#define MAX_STEPS  200     /* hard cap on script lines */

static unsigned short chan;
static char *region;
static char transcript[65536];
static unsigned int tlen;
static volatile int child_done;
static unsigned int child_status;

static void tput(const char *s, unsigned int n)
{
    if (tlen + n >= sizeof transcript) n = sizeof transcript - 1 - tlen;
    memcpy(transcript + tlen, s, n);
    tlen += n;
}

static void read_ast(int prm);

static void post_read(void)
{
    unsigned int st = ptd$read(0, chan, read_ast, 0, region + RBUF_OFF, DATA_MAX);
    if (!(st & 1)) { tput("{READ-POST-FAIL}", 16); }
}

static void read_ast(int prm)
{
    unsigned short *h = (unsigned short *) (region + RBUF_OFF);
    (void) prm;
    if (!(h[0] & 1)) {
        char m[48];
        sprintf(m, "{READ-STATUS %%X%04X}", h[0]);
        tput(m, (unsigned int) strlen(m));
        if (h[0] == SS$_ABORT || h[0] == SS$_CANCEL || h[0] == SS$_HANGUP) return;
    } else {
        tput(region + RBUF_OFF + 4, h[1]);
    }
    post_read();
}

static void event_ast(int type)
{
    static const char *names[] = { "XON", "BELL", "XOFF", "STOP", "RESUME", "CHAR",
        "ABORT", "SR", "MR", "ER", "ENR", "DISR" };
    char m[16];
    sprintf(m, "{%s}", (type >= 0 && type < 12) ? names[type] : "?");
    tput(m, (unsigned int) strlen(m));
    /* Notification is one-shot: enable it again. */
    ptd$set_event_notification(chan, event_ast, type, 0, (unsigned int) type);
}

static void done_ast(int prm) { (void) prm; child_done = 1; }

/* Print the transcript with control characters made visible, then empty it. */
static void flush_transcript(void)
{
    unsigned int i;
    sys$setast(0);
    for (i = 0; i < tlen; i++) {
        unsigned char c = (unsigned char) transcript[i];
        if (c == '\n') fputs("<LF>\n", stdout);
        else if (c == '\r') fputs("<CR>", stdout);
        else if (c == 27) fputs("<ESC>", stdout);
        else if (c < 32 || c == 127) printf("<%02X>", c);
        else putchar(c);
    }
    tlen = 0;
    sys$setast(1);
    putchar('\n');
}

static void type_text(const char *label, const char *s, unsigned int n, float secs)
{
    unsigned short *h = (unsigned short *) (region + WBUF_OFF);
    unsigned int st;
    int ftype = 4;                                  /* LIB$K_IEEE_S */
    printf("---- %s\n", label);
    if (n > DATA_MAX) n = DATA_MAX;
    memcpy(region + WBUF_OFF + 4, s, n);
    st = n ? ptd$write(chan, 0, 0, region + WBUF_OFF, n, 0, 0) : SS$_NORMAL;
    if (n && (!(st & 1) || !(h[0] & 1)))
        printf("  write: %s / buffer %%X%04X\n", msg(st), h[0]);
    lib$wait(&secs, 0, &ftype);
    flush_transcript();
}

/* Decode \r \e \\ \xHH in place; returns the decoded length. */
static unsigned int unescape(char *s)
{
    char *in = s, *out = s;
    while (*in) {
        if (*in == '\\' && in[1]) {
            in++;
            if (*in == 'r') *out++ = '\r';
            else if (*in == 'e') *out++ = 27;
            else if (*in == 'x' && in[1] && in[2]) {
                char hex[3]; hex[0] = in[1]; hex[1] = in[2]; hex[2] = '\0';
                *out++ = (char) strtol(hex, 0, 16);
                in += 2;
            } else *out++ = *in;
            in++;
        } else *out++ = *in++;
    }
    *out = '\0';
    return (unsigned int) (out - s);
}

int main(int argc, char **argv)
{
    struct _va_range got;
    struct { unsigned char cls, type; unsigned short width;
             unsigned int basic, ext; } tc;
    char devnam[64], line[1024];
    unsigned short devlen = 0;
    ile3 dvi[2];
    struct dsc$descriptor_s ddev, dprompt;
    unsigned int st, pid = 0, flags = 1, i, steps = 0;          /* CLI$M_NOWAIT */
    float secs = 2.0f;
    int ftype = 4;
    FILE *script;

    if (argc < 2) { printf("usage: PTYDRIVE script-file\n"); return SS$_INSFARG; }
    script = fopen(argv[1], "r");
    if (!script) { perror(argv[1]); return SS$_NOSUCHFILE; }

    st = sys$expreg(2, &got, 0, 0);               /* pages for the PTD$ buffers */
    if (!(st & 1)) { report("SYS$EXPREG", st); return st; }
    region = (char *) got.va_range$ps_start_va;

    memset(&tc, 0, sizeof tc);
    tc.cls = DC$_TERM;
    tc.type = TT$_VT100;
    tc.width = 80;
    tc.basic = TT$M_SCOPE | TT$M_LOWER | TT$M_TTSYNC | TT$M_WRAP | TT$M_MECHTAB
             | (24u << 24);
    tc.ext = TT2$M_ANSICRT | TT2$M_DECCRT | TT2$M_AVO | TT2$M_EDIT | TT2$M_EDITING;
    st = ptd$create(&chan, 0, &tc, sizeof tc, 0, 0, 0, &got);
    if (!(st & 1)) { report("PTD$CREATE", st); return st; }

    ile3_set(&dvi[0], DVI$_DEVNAM, devnam, sizeof devnam - 1, &devlen);
    ile3_end(&dvi[1]);
    sys$getdviw(0, chan, 0, dvi, 0, 0, 0, 0);
    devnam[devlen] = '\0';
    for (i = PTD$C_STOP_OUTPUT; i <= PTD$C_ABORT_OUTPUT; i++)
        ptd$set_event_notification(chan, event_ast, (int) i, 0, i);
    post_read();

    desc_set(&ddev, devnam, devlen);
    desc_set(&dprompt, "$ ", 2);
    st = lib$spawn(0, &ddev, &ddev, &flags, 0, &pid, &child_status, 0,
                   done_ast, 0, &dprompt, 0, 0);
    if (!(st & 1)) { report("LIB$SPAWN", st); ptd$delete(chan); return st; }
    lib$wait(&secs, 0, &ftype);
    printf("---- startup on %s\n", devnam);
    flush_transcript();

    while (!child_done && steps < MAX_STEPS && fgets(line, sizeof line, script)) {
        char *bar, *label, *end;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        bar = strchr(line, '|');
        if (!bar) { printf("---- bad script line: %s\n", line); continue; }
        *bar = '\0';
        secs = (float) strtod(line, &end);
        if (secs <= 0.0f || secs > 30.0f) secs = 1.5f;
        label = end;
        while (*label == ' ') label++;
        rtrim(label);
        bar++;
        if (*bar == ' ') bar++;
        steps++;
        type_text(label, bar, unescape(bar), secs);
    }
    fclose(script);

    printf("---- end: child %s, status %%X%08X\n",
           child_done ? "ended" : "STILL RUNNING, deleting it", child_status);
    if (!child_done) sys$delprc(&pid, 0, 0);
    ptd$delete(chan);
    return SS$_NORMAL;
}
