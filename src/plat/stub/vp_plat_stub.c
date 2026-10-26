/* vp_plat_stub.c - vp_platform.h on a Linux terminal, with an invented system
   (two CPUs, three disks, two NICs) whose figures move, for trying the screens
   out: `make demo`. Not part of the VMS build. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "vp_platform.h"
#include "vp_util.h"

static struct termios saved;
static int started;
static vp_cell *shown;
static int shown_rows, shown_cols;

static void out(void *ctx, const char *b, int n)
{
    (void) ctx;
    while (n > 0) {
        ssize_t w = write(1, b, (size_t) n);
        if (w <= 0) return;
        b += w;
        n -= (int) w;
    }
}

static int read_byte(int ms)
{
    struct pollfd p;
    unsigned char c;
    p.fd = 0;
    p.events = POLLIN;
    if (poll(&p, 1, ms) <= 0) return -1;
    if (read(0, &c, 1) != 1) return -1;
    return c;
}

/* D6: a braille character at column 1, then where is the cursor? */
static int ask_utf8(void)
{
    char buf[32];
    int n = 0, c, row, col;
    out(0, "\r\xe2\xa3\xbf\033[6n", 9);
    while (n < (int) sizeof buf - 1 && (c = read_byte(1000)) >= 0) {
        buf[n++] = (char) c;
        if (c == 'R') break;
    }
    buf[n] = '\0';
    out(0, "\r   \r", 5);
    if (sscanf(buf, "\033[%d;%dR", &row, &col) != 2) return 0;
    return col == 2;
}

int vpp_start(int *utf8, char *err, int errsize)
{
    struct termios t;
    if (!isatty(0) || tcgetattr(0, &saved) != 0) {
        vp_strlcpy(err, "not a terminal", (size_t) errsize);
        return 0;
    }
    t = saved;
    t.c_lflag &= ~(tcflag_t) (ICANON | ECHO | ISIG);
    t.c_iflag &= ~(tcflag_t) (IXON | ICRNL);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    started = 1;
    *utf8 = ask_utf8();
    vp_term_begin(out, 0);
    return 1;
}

void vpp_stop(void)
{
    if (!started) return;
    vp_term_end(out, 0);
    tcsetattr(0, TCSANOW, &saved);
    started = 0;
    free(shown);
    shown = 0;
}

void vpp_screen_size(int *rows, int *cols)
{
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
        *rows = w.ws_row;
        *cols = w.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

void vpp_draw(const vp_screen *s, const vp_term_mode *m)
{
    size_t n = (size_t) s->rows * (size_t) s->cols;
    if (!shown || shown_rows != s->rows || shown_cols != s->cols) {
        free(shown);
        shown = (vp_cell *) malloc(n * sizeof *shown);
        shown_rows = s->rows;
        shown_cols = s->cols;
        out(0, "\033[H\033[2J", 7);
        vp_term_render(s, 0, m, out, 0);
        if (shown) memcpy(shown, s->cell, n * sizeof *shown);
        return;
    }
    vp_term_render(s, shown, m, out, 0);
}

void vpp_repaint(void) { free(shown); shown = 0; }

void vpp_measure(void) { }               /* the size comes from the kernel here */

int vpp_key(int ms)
{
    int c = read_byte(ms);
    if (c < 0) return VK_TIMEOUT;
    if (c == 3) return VK_QUIT;
    if (c == 27) {
        int d = read_byte(30), e;
        if (d != '[' && d != 'O') return 27;
        e = read_byte(30);
        switch (e) {
        case 'A': return VK_UP;
        case 'B': return VK_DOWN;
        case 'C': return VK_RIGHT;
        case 'D': return VK_LEFT;
        case '5': case '6':
            if (read_byte(30) != '~') return VK_OTHER;
            return e == '5' ? VK_PREV : VK_NEXT;
        default: return VK_OTHER;
        }
    }
    return c;
}

int vpp_broadcast(char *buf, int size) { (void) buf; (void) size; return 0; }

uint64_t vpp_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000u + (uint64_t) (t.tv_nsec / 1000000);
}

int vpp_identity(vp_identity *id)
{
    vp_strlcpy(id->node, "STUBVMS", sizeof id->node);
    vp_strlcpy(id->version, "OpenVMS V9.2-3", sizeof id->version);
    vp_strlcpy(id->model, "an invented system", sizeof id->model);
    vp_strlcpy(id->arch, "x86_64", sizeof id->arch);
    return 1;
}

int vpp_option(const char *name, char *buf, int size)
{
    char env[64];
    const char *v;
    size_t i;
    vp_strlcpy(env, name, sizeof env);
    for (i = 0; env[i]; i++) if (env[i] == '$') env[i] = '_';   /* VTOP_GRAPH */
    v = getenv(env);
    if (!v) return 0;
    vp_strlcpy(buf, v, (size_t) size);
    return 1;
}

/* the invented system: smooth waves plus a little noise */
static double wave(double t, double period, double phase)
{
    return 0.5 + 0.5 * sin(t * 6.2831853 / period + phase);
}

int vpp_sample(vp_sample *s)
{
    static vp_sample acc;
    static uint64_t t0;
    static int init;
    uint64_t now = vpp_now_ms();
    double t;
    int i;
    if (!init) {
        memset(&acc, 0, sizeof acc);
        t0 = now;
        acc.ncpu = 2;
        acc.mem_total = 8ull << 30;
        acc.page_total = 2ull << 30;
        acc.swap_total = 512ull << 20;
        acc.ndisk = 3;
        vp_strlcpy(acc.disk[0].name, "DKA0:", VP_NAME);
        vp_strlcpy(acc.disk[0].label, "X86SYS", 16);
        acc.disk[0].total = 45000000ull * 512; acc.disk[0].free = 20299040ull * 512;
        vp_strlcpy(acc.disk[1].name, "DKA200:", VP_NAME);
        vp_strlcpy(acc.disk[1].label, "TOOLS", 16);
        acc.disk[1].total = 1974510ull * 512; acc.disk[1].free = 1135552ull * 512;
        vp_strlcpy(acc.disk[2].name, "DSA1:", VP_NAME);
        vp_strlcpy(acc.disk[2].label, "USERS", 16);
        acc.disk[2].total = 33554432ull * 512; acc.disk[2].free = 7608345ull * 512;
        acc.disk[2].shadow = 1;
        acc.nnic = 2;
        vp_strlcpy(acc.nic[0].name, "EWA0", VP_NAME);
        vp_strlcpy(acc.nic[1].name, "EWB0", VP_NAME);
        init = 1;
    }
    t = (double) (now - t0) / 1000.0;
    {
        static uint64_t last;
        double dt = last ? (double) (now - last) / 10.0 : 0;     /* 10 ms ticks */
        last = now;
        for (i = 0; i < acc.ncpu; i++) {
            double busy = 0.15 + 0.6 * wave(t, 40 + 13 * i, i) * wave(t, 7, 2 * i);
            acc.cpu[i].t[VP_M_USER] += (uint64_t) (dt * busy * 0.6);
            acc.cpu[i].t[VP_M_KERNEL] += (uint64_t) (dt * busy * 0.25);
            acc.cpu[i].t[VP_M_EXEC] += (uint64_t) (dt * busy * 0.1);
            acc.cpu[i].t[VP_M_INT] += (uint64_t) (dt * busy * 0.05);
            acc.cpu[i].t[VP_M_IDLE] += (uint64_t) (dt * (1 - busy));
        }
        for (i = 0; i < acc.ndisk; i++)
            acc.disk[i].ops += (uint32_t) (dt / 100.0 * (5 + 400 * wave(t, 20 + 9 * i, i) * (i == 2 ? 3 : 1)));
        acc.nic[0].rx += (uint64_t) (dt / 100.0 * 2e6 * wave(t, 30, 0) * wave(t, 5, 1));
        acc.nic[0].tx += (uint64_t) (dt / 100.0 * 2e5 * wave(t, 25, 2));
        acc.nic[1].rx += (uint64_t) (dt / 100.0 * 1e4 * wave(t, 60, 0));
        acc.nic[1].tx += (uint64_t) (dt / 100.0 * 5e3 * wave(t, 45, 1));
    }
    acc.mem_free = (uint64_t) ((double) acc.mem_total * (0.55 + 0.1 * wave(t, 90, 0)));
    acc.mem_modified = (uint64_t) ((double) acc.mem_total * 0.04);
    acc.page_free = (uint64_t) ((double) acc.page_total * 0.93);
    acc.swap_free = acc.swap_total;
    acc.processes = 46;
    acc.compute_queue = 2 * wave(t, 15, 0);
    acc.uptime = 2 * 86400 + 9 * 3600 + 12 * 60 + (int64_t) t;
    acc.ms = now;
    {
        time_t w = time(0);
        struct tm tm;
        localtime_r(&w, &tm);
        snprintf(acc.clock, sizeof acc.clock, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    *s = acc;
    return 1;
}

/* ---------- invented processes ---------- */

static struct sproc {
    uint32_t pid, owner;
    const char *name, *user, *image;
    int state, pri, prib, mode;
    double busy;                /* share of a CPU it tends to use */
    uint32_t dio_rate, bio_rate, pages;
    vp_proc now;
    int gone;
} sp[] = {
    { 0x20200101, 0, "SWAPPER", "SYSTEM", "", 7, 16, 16, 0, 0.0, 0, 0, 0, {0}, 0 },
    { 0x20200105, 0, "CLUSTER_SERVER", "SYSTEM", "CSP", 7, 9, 8, 0, 0.001, 0, 1, 520, {0}, 0 },
    { 0x20200106, 0, "OPCOM", "SYSTEM", "OPCOM", 7, 8, 8, 0, 0.0, 0, 0, 380, {0}, 0 },
    { 0x20200107, 0, "AUDIT_SERVER", "AUDIT$SERVER", "AUDIT_SERVER", 7, 10, 10, 0, 0.0, 1, 0, 900, {0}, 0 },
    { 0x20200108, 0, "JOB_CONTROL", "SYSTEM", "JOBCTL", 7, 10, 8, 0, 0.0005, 0, 2, 610, {0}, 0 },
    { 0x2020010A, 0, "SECURITY_SERVER", "SYSTEM", "SECURITY_SERVER", 7, 10, 8, 0, 0.0, 0, 0, 2400, {0}, 0 },
    { 0x2020010B, 0, "TP_SERVER", "SYSTEM", "TPSERV", 7, 10, 8, 0, 0.0, 0, 0, 700, {0}, 0 },
    { 0x20200110, 0, "LANACP", "SYSTEM", "LANACP", 7, 12, 8, 0, 0.002, 0, 3, 1300, {0}, 0 },
    { 0x20200112, 0, "TCPIP$INETACP", "TCPIP$AUX", "TCPIP$INETACP", 7, 10, 8, 0, 0.003, 0, 4, 450, {0}, 0 },
    { 0x20200115, 0, "TCPIP$SSH_BG0", "TCPIP$SSH", "TCPIP$SSH_SSHD2", 5, 8, 8, 0, 0.01, 1, 40, 3100, {0}, 0 },
    { 0x20200120, 0, "MARIADB", "MYSQL", "MYSQLD", 5, 4, 4, 0, 0.04, 30, 12, 21000, {0}, 0 },
    { 0x20200133, 0, "HASS_AGENT", "IAIN", "HASS_AGENT", 7, 4, 4, 2, 0.002, 2, 5, 1600, {0}, 0 },
    { 0x2020014A, 0, "_FTA697:", "IAIN", "", 5, 4, 4, 3, 0.002, 0, 3, 640, {0}, 0 },
    { 0x2020014B, 0x2020014A, "IAIN_1", "IAIN", "VIM", 14, 4, 4, 3, 0.45, 2, 600, 9800, {0}, 0 },
    { 0x2020014C, 0x2020014A, "IAIN_2", "IAIN", "", 7, 4, 4, 3, 0.0, 0, 0, 500, {0}, 0 },
    { 0x2020014D, 0x2020014C, "IAIN_3", "IAIN", "CC", 5, 4, 4, 3, 0.15, 120, 40, 7700, {0}, 0 },
    { 0x20200160, 0, "_FTA700:", "IAIN", "VTOP", 14, 4, 4, 3, 0.004, 0, 20, 900, {0}, 0 },
    { 0x20200171, 0, "BATCH_88", "IAIN", "BACKUP", 5, 3, 3, 2, 0.08, 400, 10, 5100, {0}, 0 },
    { 0x20200172, 0x20200171, "BACKUP_SUB", "IAIN", "", 9, 3, 3, 2, 0.0, 0, 0, 300, {0}, 0 },
    { 0x20200180, 0, "SMISERVER", "SYSTEM", "SMISERVER", 7, 9, 9, 0, 0.0, 0, 0, 800, {0}, 0 },
};
#define NSP ((int) (sizeof sp / sizeof sp[0]))
static const uint32_t self_pid = 0x20200160;

int vpp_procs(vp_proc *p, int max, vp_scan *info)
{
    static uint64_t last;
    uint64_t now = vpp_now_ms();
    double dt = last ? (double) (now - last) / 1000.0 : 0, t = (double) now / 1000.0;
    int i, n = 0;
    last = now;
    for (i = 0; i < NSP; i++) {
        struct sproc *s = &sp[i];
        vp_proc *v = &s->now;
        if (s->gone) continue;
        if (!v->pid) {
            v->pid = s->pid;
            v->owner = s->owner;
            v->uic = s->user[0] == 'I' ? (0100u << 16) | 1u : (1u << 16) | 4u;
            vp_strlcpy(v->name, s->name, sizeof v->name);
            vp_strlcpy(v->user, s->user, sizeof v->user);
            vp_strlcpy(v->image, s->image, sizeof v->image);
            v->state = s->state; v->pri = s->pri; v->prib = s->prib; v->mode = s->mode;
            v->threads = 1;
            v->cputim = (uint32_t) (s->busy * 360000.0 * 3 + i * 1234);
            if (s->owner == 0x2020014A) vp_strlcpy(v->term, "", sizeof v->term);
            if (s->name[0] == '_') vp_strlcpy(v->term, s->name + 1, sizeof v->term);
        }
        if (v->state != 9 && v->state != 10) {
            double b = s->busy * (0.6 + 0.8 * wave(t, 11 + i, i));
            v->cputim += (uint32_t) (dt * 100.0 * b);
            v->dirio += (uint32_t) (dt * s->dio_rate * wave(t, 7 + i, 1));
            v->bufio += (uint32_t) (dt * s->bio_rate * wave(t, 5 + i, 2));
            v->faults += (uint32_t) (dt * 3);
        }
        v->mem = (uint64_t) s->pages * 512u * 16u;
        if (n < max) p[n] = *v;
        n++;
    }
    info->total = n;
    info->hidden = 0;
    info->scope = VP_SCOPE_WORLD;
    info->self = self_pid;
    return n < max ? n : max;
}

static struct sproc *stub_find(uint32_t pid)
{
    int i;
    for (i = 0; i < NSP; i++) if (sp[i].pid == pid && !sp[i].gone) return &sp[i];
    return 0;
}

int vpp_proc_detail(uint32_t pid, vp_proc_detail *d, char *err, int errsize)
{
    struct sproc *s = stub_find(pid);
    static const char *qn[] = { "BYTLM", "DIOLM", "BIOLM", "ASTLM", "ENQLM", "FILLM",
                                "PGFLQUOTA", "PRCLM", "TQLM" };
    static const uint32_t ql[] = { 128000, 150, 150, 300, 4000, 300, 1048576, 10, 100 };
    int i;
    memset(d, 0, sizeof *d);
    if (!s) { vp_strlcpy(err, "%SYSTEM-W-NONEXPR, nonexistent process", (size_t) errsize); return 0; }
    d->pid = pid;
    if (s->image[0]) snprintf(d->image, sizeof d->image, "DSA1:[SYS0.SYSCOMMON.][SYSEXE]%s.EXE;1", s->image);
    vp_strlcpy(d->login, "24-OCT-2026 09:12:55.20", sizeof d->login);
    vp_strlcpy(d->account, s->user[0] == 'I' ? "STAFF" : "SYSTEM", sizeof d->account);
    vp_strlcpy(d->privs, s->user[0] == 'I' ? "GROUP TMPMBX WORLD NETMBX" : "CMKRNL CMEXEC SYSNAM "
               "DETACH LOG_IO GROUP PRMMBX ALTPRI SETPRV TMPMBX WORLD OPER NETMBX SYSPRV", sizeof d->privs);
    d->wssize = s->pages * 512u * 16u;
    d->wsquota = 16384u * 512u;
    d->wsextent = 65536u * 512u;
    for (i = 0; i < 9; i++) {
        vp_quota *q = &d->quota[d->nquota++];
        vp_strlcpy(q->name, qn[i], sizeof q->name);
        q->limit = ql[i];
        q->used = i == 0 ? 7360 : i == 6 ? s->pages * 512u : i == 5 ? 6 : i == 3 ? 2 : 0;
        q->bytes = i == 0 || i == 6;
        if (q->bytes && i == 6) q->limit *= 512u;
    }
    return 1;
}

int vpp_action(int act, uint32_t pid, int arg, char *msg, int msgsize)
{
    struct sproc *s = stub_find(pid);
    if (!s) { vp_strlcpy(msg, "%SYSTEM-W-NONEXPR, nonexistent process", (size_t) msgsize); return 0; }
    if (s->user[0] != 'I') {
        vp_strlcpy(msg, "%SYSTEM-F-NOPRIV, insufficient privilege or object protection violation",
                   (size_t) msgsize);
        return 0;
    }
    switch (act) {
    case VP_ACT_SUSPEND: s->now.state = 9; break;
    case VP_ACT_RESUME: if (s->now.state == 9) s->now.state = 5; break;
    case VP_ACT_DELETE: s->gone = 1; break;
    case VP_ACT_PRIORITY: s->now.pri = s->now.prib = arg; break;
    default: return 0;
    }
    return 1;
}

void vpp_flush_keys(void)
{
    while (read_byte(0) >= 0) ;
}
