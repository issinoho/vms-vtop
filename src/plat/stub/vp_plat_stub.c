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
