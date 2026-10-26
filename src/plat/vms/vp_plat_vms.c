/* vp_plat_vms.c - vp_platform.h on OpenVMS (x86-64 and IA64).

   Terminal: a channel to TT: for output (vp_term's sequences, IO$M_NOFORMAT, as
   vtree D11) and for the UTF-8 check (D6); SMG$ for keys and broadcast trapping only.
   Ctrl/C and Ctrl/Y: the CLI's Ctrl/Y and Ctrl/T are off while VTOP runs, and ASTs
   on the channel end it cleanly; an exit handler gives the terminal back.

   Sampler: from vms-hass 47dbcf8 ha_plat_vms.c (itself from vms-beszel), "beszel N"
   being vms-beszel's docs/NOTES.md. None of it needs privilege, and VTOP enables
   none (D5):
     $GETSYI   identity, memory size, page and swap files, boot time
     $GETRMI   CPU mode times per CPU (RMI$_MODES), free/modified lists, processes
     $GETDVI   mounted disks: size, free, errors, state, operation count (N4)
     $QIO      LAN read-counters on each LAN device's unit 0 (beszel N12-13);
               once a 32-bit counter sticks at 4 GiB, LANCP's 64-bit figures

   Built with SYS$LIB_C.TLB for NMADEF. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <descrip.h>
#include <dcdef.h>
#include <dvidef.h>
#include <dvsdef.h>
#include <efndef.h>
#include <iodef.h>
#include <libclidef.h>
#include <lnmdef.h>
#include <nmadef.h>
#include <rmidef.h>
#include <smgdef.h>
#include <smgmsg.h>
#include <smg$routines.h>
#include <lib$routines.h>
#include <ssdef.h>
#include <starlet.h>
#include <syidef.h>

#include "vp_platform.h"
#include "vp_util.h"

/* POSIX, in the C RTL, but hidden by VSI C under /STANDARD=C99 (beszel N26) */
FILE *popen(const char *command, const char *mode);
int pclose(FILE *stream);

/* item lists: our own types, as the default STARLET headers have no IOSB (beszel N16) */
struct item {
    unsigned short len, code;
    void *buf;
    unsigned short *retlen;
};
struct iosb {
    unsigned short st, cnt;
    unsigned int dev;
};

static void dsc(struct dsc$descriptor_s *d, const char *p, unsigned short n)
{
    d->dsc$w_length = n;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *) p;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\0')) s[--n] = '\0';
    while (s[i] == ' ') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

/* ---------- terminal ---------- */

static unsigned int pb, kb, ctrl_saved;
static unsigned short ttchan;
static int started, have_pb, have_kb, ctrl_off;
static volatile int quit_flag;
static vp_cell *shown;
static int shown_rows, shown_cols, tt_rows = 24, tt_cols = 80;

static void tt_write(void *ctx, const char *bytes, int len)
{
    struct iosb iosb;
    (void) ctx;
    while (len > 0) {
        int n = len > 2048 ? 2048 : len;
        sys$qiow(EFN$C_ENF, ttchan, IO$_WRITEVBLK | IO$M_NOFORMAT, (void *) &iosb, 0, 0,
                 (void *) bytes, n, 0, 0, 0, 0);
        bytes += n;
        len -= n;
    }
}

/* Write a question and read the terminal's cursor position report: timed, unechoed,
   uninterpreted one-character reads, up to the R. 0 if nothing answers in a second. */
static int ask_cpr(const char *question, int *row, int *col)
{
    char buf[32];
    int n = 0;
    tt_write(0, question, (int) strlen(question));
    while (n < (int) sizeof buf - 1) {
        struct iosb iosb;
        char c = 0;
        unsigned int st = sys$qiow(EFN$C_ENF, ttchan,
                                   IO$_TTYREADALL | IO$M_NOECHO | IO$M_TIMED, (void *) &iosb,
                                   0, 0, &c, 1, 1, 0, 0, 0);
        if (!(st & 1) || !(iosb.st & 1) || iosb.cnt + (iosb.dev >> 16) == 0) break;
        buf[n++] = c;
        if (c == 'R') break;
    }
    buf[n] = '\0';
    return vp_parse_cpr(buf, row, col);
}

/* D6: a braille character at column 1, then where is the cursor? Column 2 is UTF-8. */
static int answers;                     /* the terminal answered: it can be asked again */
static int ask_utf8(void)
{
    int row = 0, col = 0;
    answers = ask_cpr("\r\xe2\xa3\xbf\033[6n", &row, &col);
    tt_write(0, "\r   \r", 5);
    return answers && col == 2;
}

/* The terminal's real size: the cursor sent to the far corner, then asked where it is.
   VMS's own idea (TT width and page) is often 80x24 for an SSH session in a bigger
   window. */
static int meas_rows, meas_cols, dvi_rows, dvi_cols;
static void ask_size(void)
{
    int row = 0, col = 0;
    if (!answers) return;
    if (ask_cpr("\0337\033[999;999H\033[6n\0338", &row, &col) && row >= 5 && col >= 10
        && row <= 255 && col <= 511) {
        meas_rows = row;
        meas_cols = col;
    }
}

static void on_ctrl(void)
{
    quit_flag = 1;
    if (have_kb) smg$cancel_input(&kb);
}

static struct { unsigned int link; void (*handler)(void); unsigned int argc; unsigned int *status;
                unsigned int st; } exit_block;

static void on_exit(void) { vpp_stop(); }

/* VMS's terminal width and page; the measured size wins unless they change while
   VTOP runs (SET TERMINAL/WIDTH, or a resize the SSH server passes on) */
static void tt_size(void)
{
    unsigned int w = 0, p = 0;
    struct item il[3];
    struct iosb iosb;
    memset(il, 0, sizeof il);
    il[0].len = 4; il[0].code = DVI$_DEVBUFSIZ; il[0].buf = &w;
    il[1].len = 4; il[1].code = DVI$_TT_PAGE;   il[1].buf = &p;
    if (!(sys$getdviw(EFN$C_ENF, ttchan, 0, il, (void *) &iosb, 0, 0, 0) & 1) || !(iosb.st & 1)
        || w < 10 || p < 5 || w > 511 || p > 255)
        return;
    if (dvi_cols && ((int) w != dvi_cols || (int) p != dvi_rows)) meas_rows = meas_cols = 0;
    dvi_cols = (int) w;
    dvi_rows = (int) p;
    tt_cols = meas_cols ? meas_cols : dvi_cols;
    tt_rows = meas_rows ? meas_rows : dvi_rows;
}

int vpp_start(int *utf8, char *err, int errsize)
{
    struct dsc$descriptor_s ttd;
    unsigned int st, one = 1, mask;
    int rows = 0, cols = 0;
    struct iosb iosb;

    dsc(&ttd, "TT:", 3);
    st = sys$assign(&ttd, &ttchan, 0, 0, 0);
    if (!(st & 1)) {
        snprintf(err, (size_t) errsize, "can't assign TT: (status %%X%08X)", st);
        return 0;
    }
    {
        unsigned int dc = 0;
        struct item il[2];
        memset(il, 0, sizeof il);
        il[0].len = 4; il[0].code = DVI$_DEVCLASS; il[0].buf = &dc;
        sys$getdviw(EFN$C_ENF, ttchan, 0, il, (void *) &iosb, 0, 0, 0);
        if (dc != DC$_TERM) {
            sys$dassgn(ttchan);
            vp_strlcpy(err, "SYS$COMMAND is not a terminal", (size_t) errsize);
            return 0;
        }
    }
    *utf8 = ask_utf8();
    ask_size();
    {   /* VTOP$SIZE "132x40": for a terminal that doesn't answer, and for tests */
        char v[32];
        int c = 0, r = 0;
        if (vpp_option("VTOP$SIZE", v, sizeof v) && sscanf(v, "%dx%d", &c, &r) == 2
            && r >= 5 && c >= 10 && r <= 255 && c <= 511) {
            meas_rows = r;
            meas_cols = c;
        }
    }
    st = smg$create_pasteboard(&pb, 0, &rows, &cols, 0, 0, 0);
    if (!(st & 1)) {
        sys$dassgn(ttchan);
        snprintf(err, (size_t) errsize, "SMG$CREATE_PASTEBOARD: status %%X%08X", st);
        return 0;
    }
    have_pb = 1;
    st = smg$create_virtual_keyboard(&kb, 0, 0, 0, 0);
    if (!(st & 1)) {
        smg$delete_pasteboard(&pb, &one);
        have_pb = 0;
        sys$dassgn(ttchan);
        snprintf(err, (size_t) errsize, "SMG$CREATE_VIRTUAL_KEYBOARD: status %%X%08X", st);
        return 0;
    }
    have_kb = 1;
    smg$set_broadcast_trapping(&pb, 0, 0);
    if (rows > 0 && cols > 0) { tt_rows = rows; tt_cols = cols; }
    tt_size();

    /* Ctrl/Y and Ctrl/T to us, not to DCL, until vpp_stop; Ctrl/C and Ctrl/Y end VTOP */
    mask = LIB$M_CLI_CTRLY | LIB$M_CLI_CTRLT;
    if (lib$disable_ctrl(&mask, &ctrl_saved) & 1) ctrl_off = 1;
    sys$qiow(EFN$C_ENF, ttchan, IO$_SETMODE | IO$M_CTRLCAST, (void *) &iosb, 0, 0,
             (unsigned long) on_ctrl, 0, 0, 0, 0, 0);
    sys$qiow(EFN$C_ENF, ttchan, IO$_SETMODE | IO$M_CTRLYAST, (void *) &iosb, 0, 0,
             (unsigned long) on_ctrl, 0, 0, 0, 0, 0);

    started = 1;
    vp_term_begin(tt_write, 0);
    exit_block.handler = on_exit;
    exit_block.argc = 1;
    exit_block.status = &exit_block.st;
    sys$dclexh((void *) &exit_block);
    return 1;
}

void vpp_stop(void)
{
    unsigned int one = 1;
    struct iosb iosb;
    if (!started) return;
    started = 0;
    vp_term_end(tt_write, 0);
    /* cancel the Ctrl/C and Ctrl/Y ASTs (no AST address) */
    sys$qiow(EFN$C_ENF, ttchan, IO$_SETMODE | IO$M_CTRLCAST, (void *) &iosb, 0, 0, 0, 0, 0, 0, 0, 0);
    sys$qiow(EFN$C_ENF, ttchan, IO$_SETMODE | IO$M_CTRLYAST, (void *) &iosb, 0, 0, 0, 0, 0, 0, 0, 0);
    if (ctrl_off) {
        unsigned int back = ctrl_saved & (LIB$M_CLI_CTRLY | LIB$M_CLI_CTRLT);
        if (back) lib$enable_ctrl(&back, 0);
        ctrl_off = 0;
    }
    if (have_pb) {
        smg$disable_broadcast_trapping(&pb);
        smg$delete_pasteboard(&pb, &one);
        have_pb = 0;
    }
    if (have_kb) {
        have_kb = 0;
        smg$delete_virtual_keyboard(&kb);
    }
    vp_term_end(tt_write, 0);           /* SMG$ may have written: leave a clean screen */
    sys$dassgn(ttchan);
    free(shown);
    shown = 0;
}

void vpp_screen_size(int *rows, int *cols)
{
    if (started) tt_size();             /* SET TERMINAL/WIDTH, or a resize the SSH server passed on */
    *rows = tt_rows;
    *cols = tt_cols;
}

void vpp_draw(const vp_screen *s, const vp_term_mode *m)
{
    size_t n = (size_t) s->rows * (size_t) s->cols;
    if (!started) return;
    if (!shown || shown_rows != s->rows || shown_cols != s->cols) {
        free(shown);
        shown = (vp_cell *) malloc(n * sizeof *shown);
        shown_rows = s->rows;
        shown_cols = s->cols;
        tt_write(0, "\033[0m\033[H\033[2J", 11);
        vp_term_render(s, 0, m, tt_write, 0);
        if (shown) memcpy(shown, s->cell, n * sizeof *shown);
        return;
    }
    vp_term_render(s, shown, m, tt_write, 0);
}

void vpp_repaint(void) { free(shown); shown = 0; }

void vpp_measure(void)
{
    if (!started) return;
    ask_size();
    tt_size();
}

int vpp_key(int ms)
{
    unsigned short term = 0;
    unsigned int st;
    int secs = ms <= 0 ? 0 : (ms + 999) / 1000;
    if (quit_flag) return VK_QUIT;
    if (!started) return VK_TIMEOUT;
    st = smg$read_keystroke(&kb, &term, 0, &secs, 0, 0, 0);
    if (quit_flag) return VK_QUIT;
    if (st == SS$_TIMEOUT) return VK_TIMEOUT;
    if (st == SMG$_EOF) return VK_CTRL_Z;
    if (!(st & 1)) return VK_TIMEOUT;
    switch (term) {
    case SMG$K_TRM_UP: return VK_UP;
    case SMG$K_TRM_DOWN: return VK_DOWN;
    case SMG$K_TRM_LEFT: return VK_LEFT;
    case SMG$K_TRM_RIGHT: return VK_RIGHT;
    case SMG$K_TRM_PREV_SCREEN: return VK_PREV;
    case SMG$K_TRM_NEXT_SCREEN: return VK_NEXT;
    case SMG$K_TRM_F10: return VK_F10;
    case SMG$K_TRM_HELP: return VK_HELP;
    case SMG$K_TRM_DO: return VK_DO;
    case SMG$K_TRM_PF1: return VK_PF1;
    case SMG$K_TRM_PF2: return VK_PF2;
    case SMG$K_TRM_PF3: return VK_PF3;
    case SMG$K_TRM_PF4: return VK_PF4;
    case 3: case 25: return VK_QUIT;     /* Ctrl/C, Ctrl/Y if they arrive as keys */
    default: break;
    }
    if (term < 256) return term;
    return VK_OTHER;
}

int vpp_broadcast(char *buf, int size)
{
    struct dsc$descriptor_s d;
    unsigned short len = 0;
    unsigned int st;
    int i;
    if (!started) return 0;
    dsc(&d, buf, (unsigned short) (size - 1));
    st = smg$get_broadcast_message(&pb, &d, &len, 0);
    if (st != SS$_NORMAL || len == 0) return 0;
    buf[len] = '\0';
    for (i = 0; i < len; i++) if ((unsigned char) buf[i] < 32) buf[i] = ' ';
    trim(buf);
    return buf[0] != '\0';
}

/* ---------- time, identity, options ---------- */

static uint64_t vms_time(void)
{
    uint64_t q;
    sys$gettim((void *) &q);            /* 100 ns units since 17-NOV-1858, local time */
    return q;
}

uint64_t vpp_now_ms(void)
{
    /* the clock can be set back: intervals must not run backwards (beszel review I7) */
    static uint64_t last, adjust;
    uint64_t now = vms_time() / 10000 + adjust;
    if (now < last) {
        adjust += last - now;
        now = last;
    }
    last = now;
    return now;
}

static int syi(int code, void *buf, unsigned short len, unsigned short *rl)
{
    struct item il[2];
    struct iosb iosb;
    unsigned short dummy;
    int st;
    memset(il, 0, sizeof il);
    il[0].len = len;
    il[0].code = (unsigned short) code;
    il[0].buf = buf;
    il[0].retlen = rl ? rl : &dummy;
    st = sys$getsyiw(EFN$C_ENF, 0, 0, il, (void *) &iosb, 0, 0);
    return (st & 1) ? iosb.st & 1 : 0;
}

int vpp_identity(vp_identity *id)
{
    char ver[9], hw[96], arch[16], node[16];
    unsigned short vl = 0, hl = 0, al = 0, nl = 0;
    memset(id, 0, sizeof *id);
    syi(SYI$_VERSION, ver, 8, &vl);
    syi(SYI$_HW_NAME, hw, sizeof hw - 1, &hl);
    syi(SYI$_ARCH_NAME, arch, sizeof arch - 1, &al);
    syi(SYI$_NODENAME, node, sizeof node - 1, &nl);
    ver[vl] = '\0';
    hw[hl] = '\0';
    arch[al] = '\0';
    node[nl] = '\0';
    trim(ver);
    vp_strlcpy(id->node, node, sizeof id->node);
    snprintf(id->version, sizeof id->version, "OpenVMS %s", ver);
    vp_strlcpy(id->model, hw, sizeof id->model);
    vp_strlcpy(id->arch, arch, sizeof id->arch);
    return 1;
}

int vpp_option(const char *name, char *buf, int size)
{
    struct dsc$descriptor_s tab, lnm;
    unsigned short vl = 0;
    struct item il[2];
    unsigned int attr = LNM$M_CASE_BLIND;
    char val[256];
    dsc(&tab, "LNM$FILE_DEV", 12);
    dsc(&lnm, name, (unsigned short) strlen(name));
    memset(il, 0, sizeof il);
    il[0].len = sizeof val - 1;
    il[0].code = LNM$_STRING;
    il[0].buf = val;
    il[0].retlen = &vl;
    if (!(sys$trnlnm(&attr, &tab, &lnm, 0, il) & 1)) return 0;
    val[vl] = '\0';
    trim(val);
    vp_strlcpy(buf, val, (size_t) size);
    return 1;
}

/* ---------- the sampler (from vms-hass) ---------- */

/* one $GETRMI item, at its exact size (beszel N1). The return length gets 8 bytes:
   RMI$_MODES writes more than the 16-bit word an item list provides. */
static int rmi(int code, void *buf, unsigned short len)
{
    struct item il[2];
    struct iosb iosb;
    uint64_t rl = 0;
    int st;
    memset(il, 0, sizeof il);
    il[0].len = len;
    il[0].code = (unsigned short) code;
    il[0].buf = buf;
    il[0].retlen = (unsigned short *) &rl;
    st = sys$getrmi(EFN$C_ENF, 0, 0, il, (void *) &iosb, 0, 0);
    if (st & 1) {
        sys$synch(EFN$C_ENF, (void *) &iosb);
        st = iosb.st;
    }
    return st & 1;
}

static uint64_t rmi_u(int code, int size)
{
    if (size == 8) {
        uint64_t v = 0;
        return rmi(code, &v, 8) ? v : 0;
    } else {
        unsigned int w = 0;
        return rmi(code, &w, 4) ? w : 0;
    }
}

/* "_X86VMS$DKA0:" -> "DKA0:", "_$1$DGA100:" -> "$1$DGA100:" */
static void clean_dev(char *out, size_t cap, const char *name, int keep_colon)
{
    const char *s = name, *e;
    size_t n;
    if (*s == '_') s++;
    if (*s != '$' && (e = strchr(s, '$')) != NULL) s = e + 1;   /* a node name */
    e = strchr(s, ':');
    if (e && keep_colon) e++;
    n = e ? (size_t) (e - s) : strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
}

/* LAN devices: channels kept open, 32-bit counters extended */
struct nic {
    char name[32];
    unsigned short chan;
    int have;
    unsigned int raw_tx, raw_rx;
    uint64_t tx, rx;
    int stuck, lancp_failed;
    uint64_t lancp_ms;
};

#define LANCP_INTERVAL_MS 30000
static struct nic nics[VP_MAX_NIC];
static int nnic, nic_opened;
static unsigned char cbuf[4096];

static void nic_open_all(void)
{
    char name[65];
    struct dsc$descriptor_s nd, pat, dev;
    unsigned short nl;
    uint64_t ctx = 0;
    int devclass = DC$_SCOM;
    struct item scan[2];
    memset(scan, 0, sizeof scan);
    scan[0].len = 4;
    scan[0].code = DVS$_DEVCLASS;
    scan[0].buf = &devclass;
    dsc(&nd, name, 64);
    dsc(&pat, "*", 1);
    while (nnic < VP_MAX_NIC) {
        unsigned int unit = 1;
        struct item il[2];
        struct iosb iosb;
        unsigned short chan = 0;
        int st = sys$device_scan(&nd, &nl, &pat, scan, (void *) &ctx);
        if (!(st & 1)) break;
        dsc(&dev, name, nl);
        memset(il, 0, sizeof il);
        il[0].len = 4;
        il[0].code = DVI$_UNIT;
        il[0].buf = &unit;
        if (!(sys$getdviw(EFN$C_ENF, 0, &dev, il, (void *) &iosb, 0, 0, 0) & 1) || unit != 0)
            continue;           /* template units (0) carry the device counters */
        if (!(sys$assign(&dev, &chan, 0, 0, 0) & 1)) continue;    /* beszel N15 */
        name[nl] = 0;
        clean_dev(nics[nnic].name, sizeof nics[0].name, name, 0);
        nics[nnic].chan = chan;
        nnic++;
    }
}

static int nic_read(struct nic *n, unsigned int *tx, unsigned int *rx)
{
    struct dsc$descriptor_s bd;
    struct iosb iosb;
    int st, i, got = 0;
    dsc(&bd, (char *) cbuf, sizeof cbuf);
    st = sys$qiow(EFN$C_ENF, n->chan, IO$_SENSEMODE | IO$M_CTRL | IO$M_RD_COUNT, (void *) &iosb,
                  0, 0, 0, (unsigned long) &bd, 0, 0, 0, 0);
    if (!(st & 1) || !(iosb.st & 1)) return 0;
    /* NMA counters: a 16-bit header (type, width), then the value */
    for (i = 0; i + 2 <= (int) iosb.cnt;) {
        unsigned short h;
        int type, w, bytes;
        unsigned int v = 0;
        memcpy(&h, cbuf + i, 2);
        i += 2;
        type = h & NMA$M_CNT_TYP;
        w = (h & NMA$M_CNT_WID) >> 13;
        bytes = w == 1 ? 1 : w == 2 ? 2 : w == 3 ? 4 : 8;
        if (h & NMA$M_CNT_MAP) bytes += 2;
        if (i + bytes > (int) iosb.cnt) break;
        if (bytes == 4) memcpy(&v, cbuf + i, 4);
        if (type == NMA$C_CTLIN_BSN && bytes == 4) { *tx = v; got |= 1; }
        else if (type == NMA$C_CTLIN_BRC && bytes == 4) { *rx = v; got |= 2; }
        i += bytes;
    }
    return got == 3;
}

static void extend(unsigned int raw, unsigned int *last, uint64_t *total)
{
    *total += (unsigned int) (raw - *last);     /* mod 2^32 */
    *last = raw;
}

/* LANCP's 64-bit figures, readable without privilege (beszel N41) */
static int lancp_read(const char *dev, uint64_t *tx, uint64_t *rx)
{
    char cmd[96], line[256];
    FILE *p;
    int got = 0;
    snprintf(cmd, sizeof cmd, "mcr lancp show device %s/counters", dev);
    p = popen(cmd, "r");
    if (!p) return 0;
    while (fgets(line, sizeof line, p)) {
        unsigned long long v;
        int off = 0;
        const char *rest;
        if (sscanf(line, " %llu %n", &v, &off) != 1 || off == 0) continue;
        rest = line + off;
        if (strncmp(rest, "Bytes received", 14) == 0) { *rx = v; got |= 2; }
        else if (strncmp(rest, "Bytes sent", 10) == 0) { *tx = v; got |= 1; }
    }
    pclose(p);
    return got == 3;
}

static void nic_refresh(void)
{
    int i;
    for (i = 0; i < nnic; i++) {
        struct nic *n = &nics[i];
        unsigned int tx, rx;
        if (!nic_read(n, &tx, &rx)) continue;
        if (!n->have) {
            n->have = 1;
            n->raw_tx = tx; n->raw_rx = rx;
            n->tx = tx; n->rx = rx;
            continue;
        }
        if (!n->stuck && (tx == 0xFFFFFFFFu || rx == 0xFFFFFFFFu)) {
            n->stuck = 1;               /* stopped at the maximum (beszel N41) */
            n->lancp_ms = 0;
        }
        if (n->stuck) {
            uint64_t now = vpp_now_ms(), ltx, lrx;
            if (n->lancp_ms && now - n->lancp_ms < LANCP_INTERVAL_MS) continue;
            n->lancp_ms = now;
            if (lancp_read(n->name, &ltx, &lrx)) {
                if (ltx > n->tx) n->tx = ltx;
                if (lrx > n->rx) n->rx = lrx;
            }
            continue;
        }
        extend(tx, &n->raw_tx, &n->tx);
        extend(rx, &n->raw_rx, &n->rx);
    }
}

/* UCB$L_STS bits as DVI$_STS returns them (vms-hass NOTES 8) */
#define UCB_M_ONLINE   0x10
#define UCB_M_MNTVERIP 0x4000

static void disks(vp_sample *s)
{
    char name[65];
    struct dsc$descriptor_s nd, pat, dev;
    unsigned short nl;
    uint64_t ctx = 0;
    int devclass = DC$_DISK;
    struct item scan[2];
    memset(scan, 0, sizeof scan);
    scan[0].len = 4;
    scan[0].code = DVS$_DEVCLASS;
    scan[0].buf = &devclass;
    dsc(&nd, name, 64);
    dsc(&pat, "*", 1);
    while (s->ndisk < VP_MAX_DISK) {
        unsigned int mnt = 0, foreign = 0, member = 0, master = 0, maxb = 0, freeb = 0;
        unsigned int errs = 0, sts = 0, ops = 0;
        char vol[13];
        unsigned short vl = 0;
        struct item il[11];
        struct iosb iosb;
        vp_disk *d = &s->disk[s->ndisk];
        int st = sys$device_scan(&nd, &nl, &pat, scan, (void *) &ctx);
        if (!(st & 1)) break;
        dsc(&dev, name, nl);
        memset(il, 0, sizeof il);
        il[0].len = 4;  il[0].code = DVI$_MNT;         il[0].buf = &mnt;
        il[1].len = 4;  il[1].code = DVI$_FOR;         il[1].buf = &foreign;
        il[2].len = 4;  il[2].code = DVI$_SHDW_MEMBER; il[2].buf = &member;
        il[3].len = 4;  il[3].code = DVI$_MAXBLOCK;    il[3].buf = &maxb;
        il[4].len = 4;  il[4].code = DVI$_FREEBLOCKS;  il[4].buf = &freeb;
        il[5].len = 12; il[5].code = DVI$_VOLNAM;      il[5].buf = vol; il[5].retlen = &vl;
        il[6].len = 4;  il[6].code = DVI$_ERRCNT;      il[6].buf = &errs;
        il[7].len = 4;  il[7].code = DVI$_STS;         il[7].buf = &sts;
        il[8].len = 4;  il[8].code = DVI$_SHDW_MASTER; il[8].buf = &master;
        il[9].len = 4;  il[9].code = DVI$_OPCNT;       il[9].buf = &ops;
        if (!(sys$getdviw(EFN$C_ENF, 0, &dev, il, (void *) &iosb, 0, 0, 0) & 1) || !(iosb.st & 1))
            continue;
        /* a shadow member's space is its virtual unit's; a foreign mount has no files */
        if (!mnt || foreign || member || maxb == 0) continue;
        memset(d, 0, sizeof *d);
        name[nl] = 0;
        vol[vl < sizeof vol ? vl : sizeof vol - 1] = 0;
        trim(vol);
        clean_dev(d->name, sizeof d->name, name, 1);
        vp_strlcpy(d->label, vol, sizeof d->label);
        d->total = (uint64_t) maxb * 512;
        d->free = (uint64_t) (freeb < maxb ? freeb : maxb) * 512;
        d->errors = errs;
        d->ops = ops;
        d->mntver = (sts & UCB_M_MNTVERIP) != 0;
        d->offline = !(sts & UCB_M_ONLINE);
        d->shadow = master != 0;
        s->ndisk++;
    }
}

#define MAX_CPU VP_MAX_CPU
static unsigned int cpu_prev[MAX_CPU][8];
static vp_modes cpu_acc[MAX_CPU];
static int cpu_n = -1, cpu_have;
static unsigned int page_size = 8192;

int vpp_sample(vp_sample *s)
{
    static unsigned char modes[4 + MAX_CPU * 33 + 512];
    unsigned int ncpu = 0, i;
    uint64_t memsize = 0, boot = 0;
    unsigned int pf_page = 0, pf_free = 0, sw_page = 0, sw_free = 0;
    unsigned short n[7];
    uint64_t now;
    memset(s, 0, sizeof *s);
    if (!nic_opened) {
        nic_opened = 1;
        syi(SYI$_PAGE_SIZE, &page_size, 4, NULL);
        nic_open_all();
    }

    /* RMI$_MODES: a CPU count, then per CPU an id byte and eight longwords of 10 ms
       ticks: [0] interrupt+idle, [1] MP synch, [2] kernel, [3] exec, [4] super,
       [5] user, [6] compat, [7] idle. The return length is wrong on both
       architectures, so the count decides (beszel N3, N5). */
    memset(modes, 0, sizeof modes);
    if (!rmi(RMI$_MODES, modes, (unsigned short) sizeof modes)) return 0;
    memcpy(&ncpu, modes, 4);
    if (ncpu > MAX_CPU) ncpu = MAX_CPU;
    if ((int) ncpu != cpu_n) {          /* the CPU set changed: new baselines */
        cpu_n = (int) ncpu;
        cpu_have = 0;
        memset(cpu_acc, 0, sizeof cpu_acc);
    }
    for (i = 0; i < ncpu; i++) {
        unsigned int l[8], d[8];
        int k;
        memcpy(l, modes + 4 + i * 33 + 1, sizeof l);
        /* deltas mod 2^32, in 64 bits. [0] includes idle, and on x86 [7] can run
           ahead of [0] (beszel N4, N29): interrupt is [0] - [7], at least 0. */
        for (k = 0; k < 8; k++) d[k] = cpu_have ? l[k] - cpu_prev[i][k] : 0;
        memcpy(cpu_prev[i], l, sizeof l);
        cpu_acc[i].t[VP_M_INT] += d[0] > d[7] ? d[0] - d[7] : 0;
        cpu_acc[i].t[VP_M_MPSYNC] += d[1];
        cpu_acc[i].t[VP_M_KERNEL] += d[2];
        cpu_acc[i].t[VP_M_EXEC] += d[3];
        cpu_acc[i].t[VP_M_SUPER] += d[4];
        cpu_acc[i].t[VP_M_USER] += (uint64_t) d[5] + d[6];
        cpu_acc[i].t[VP_M_IDLE] += d[7];
        s->cpu[i] = cpu_acc[i];
    }
    cpu_have = 1;
    s->ncpu = (int) ncpu;

    syi(SYI$_MEMSIZE, &memsize, 8, NULL);
    s->mem_total = memsize * page_size;
    s->mem_free = rmi_u(RMI$_FRLIST, 8) * page_size;
    s->mem_modified = rmi_u(RMI$_MODLIST, 8) * page_size;
    syi(SYI$_PAGEFILE_PAGE, &pf_page, 4, NULL);
    syi(SYI$_PAGEFILE_FREE, &pf_free, 4, NULL);
    syi(SYI$_SWAPFILE_PAGE, &sw_page, 4, NULL);
    syi(SYI$_SWAPFILE_FREE, &sw_free, 4, NULL);
    s->page_total = (uint64_t) pf_page * page_size;
    s->page_free = (uint64_t) (pf_free < pf_page ? pf_free : pf_page) * page_size;
    s->swap_total = (uint64_t) sw_page * page_size;
    s->swap_free = (uint64_t) (sw_free < sw_page ? sw_free : sw_page) * page_size;

    now = vms_time();
    syi(SYI$_BOOTTIME, &boot, 8, NULL);
    s->uptime = boot && now > boot ? (int64_t) ((now - boot) / 10000000u) : 0;
    if (sys$numtim(n, (void *) &now) & 1)
        snprintf(s->clock, sizeof s->clock, "%02u:%02u:%02u", n[3], n[4], n[5]);

    s->processes = (int) rmi_u(RMI$_PROCS, 4);
    s->compute_queue = (double) (rmi_u(RMI$_COM, 4) + rmi_u(RMI$_COMO, 4));
    disks(s);
    nic_refresh();
    for (i = 0; i < (unsigned) nnic; i++) {
        /* a device that has carried nothing since boot is configured but unused
           (beszel N15): left out until it has traffic */
        if (!nics[i].have || (nics[i].rx == 0 && nics[i].tx == 0)) continue;
        vp_strlcpy(s->nic[s->nnic].name, nics[i].name, sizeof s->nic[0].name);
        s->nic[s->nnic].tx = nics[i].tx;
        s->nic[s->nnic].rx = nics[i].rx;
        s->nnic++;
    }
    s->ms = vpp_now_ms();
    return s->ncpu > 0;
}
