/* test_app.c - the whole app from samples and keys to screens. Each case's screen
   is compared with tests/snap/<name>.txt; run with VP_UPDATE_SNAPSHOTS=1 to write
   them (then read every changed one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vp_app.h"
#include "vp_util.h"

static int checks, fails;
static const char *snapdir = "tests/snap";
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static vp_app app;
static vp_sample smp;
static vp_proc pr[8];
static int npr;
static vp_scan scan;

static void proc(uint32_t pid, uint32_t owner, const char *name, const char *user,
                 const char *image, int state, int pri)
{
    vp_proc *p = &pr[npr++];
    memset(p, 0, sizeof *p);
    p->pid = pid; p->owner = owner;
    strcpy(p->name, name); strcpy(p->user, user); strcpy(p->image, image);
    p->state = state; p->pri = p->prib = pri; p->mode = 3; p->threads = 1;
    p->uic = (0100u << 16) | 1u;
    p->mem = (uint64_t) (npr * 3 + 1) << 20;
}

static void snap(const char *name)
{
    static char text[65536], want[65536];
    char path[256];
    FILE *f;
    size_t n;
    vp_app_draw(&app);
    vp_screen_text(&app.scr, text, sizeof text);
    snprintf(path, sizeof path, "%s/%s.txt", snapdir, name);
    checks++;
    if (getenv("VP_UPDATE_SNAPSHOTS")) {
        f = fopen(path, "w");
        if (f) { fputs(text, f); fclose(f); }
        return;
    }
    f = fopen(path, "r");
    if (!f) { fails++; printf("FAIL no snapshot %s\n", path); return; }
    n = fread(want, 1, sizeof want - 1, f);
    want[n] = '\0';
    fclose(f);
    if (strcmp(text, want)) { fails++; printf("FAIL snapshot %s differs:\n%s", path, text); }
}

/* a system like the x86 node: 2 CPUs, three disks, a NIC with traffic */
static void world(int ncpu)
{
    vp_identity id;
    memset(&smp, 0, sizeof smp);
    memset(&id, 0, sizeof id);
    strcpy(id.node, "X86VMS");
    strcpy(id.version, "OpenVMS E9.2-4");
    strcpy(id.arch, "x86_64");
    vp_app_free(&app);
    vp_app_init(&app, 24, 80, &id);
    smp.ncpu = ncpu;
    smp.mem_total = 8ull << 30;
    smp.mem_free = 5ull << 30;
    smp.mem_modified = 200ull << 20;
    smp.page_total = 2ull << 30;
    smp.page_free = 1900ull << 20;
    smp.swap_total = 512ull << 20;
    smp.swap_free = 512ull << 20;
    smp.processes = 46;
    smp.compute_queue = 0.4;
    smp.uptime = 2 * 86400 + 9 * 3600 + 12 * 60;
    strcpy(smp.clock, "14:02:33");
    smp.ndisk = 3;
    strcpy(smp.disk[0].name, "DKA0:");  strcpy(smp.disk[0].label, "X86SYS");
    smp.disk[0].total = 45000000ull * 512; smp.disk[0].free = 20299040ull * 512;
    strcpy(smp.disk[1].name, "DKA200:"); strcpy(smp.disk[1].label, "TOOLS");
    smp.disk[1].total = 1974510ull * 512; smp.disk[1].free = 1135552ull * 512;
    strcpy(smp.disk[2].name, "DKA300:"); strcpy(smp.disk[2].label, "SYSDUMP");
    smp.disk[2].total = 33554432ull * 512; smp.disk[2].free = 7608345ull * 512;
    smp.disk[2].mntver = 1;
    smp.nnic = 2;
    strcpy(smp.nic[0].name, "EWA0");
    strcpy(smp.nic[1].name, "EWB0");
    npr = 0;
    proc(0x20200101, 0, "SWAPPER", "SYSTEM", "", 7, 16);
    proc(0x20200120, 0, "MARIADB", "MYSQL", "MYSQLD", 5, 4);
    proc(0x2020014A, 0, "_FTA697:", "IAIN", "", 5, 4);
    proc(0x2020014B, 0x2020014A, "IAIN_1", "IAIN", "VIM", 14, 4);
    proc(0x2020014C, 0x2020014A, "IAIN_2", "IAIN", "", 7, 4);
    proc(0x2020014D, 0x2020014C, "IAIN_3", "IAIN", "CC", 9, 4);
    proc(0x20200160, 0, "_FTA700:", "IAIN", "VTOP", 14, 4);
    memset(&scan, 0, sizeof scan);
    scan.scope = VP_SCOPE_WORLD;
    scan.self = 0x20200160;
}

/* the vms panel's figures (D9): like the nodes (standalone, 7 queues), or a cluster
   with shadow sets and a queue in trouble */
static vp_vms vms;

static void queue(const char *name, int kind, unsigned status, int exec, int limit,
                  int pend, int hold, int timed)
{
    vp_queue *q = &vms.queue[vms.nqueue++];
    memset(q, 0, sizeof *q);
    strcpy(q->name, name);
    q->kind = kind; q->status = status;
    q->exec = exec; q->limit = limit; q->pend = pend; q->hold = hold; q->timed = timed;
    vms.queue_total = vms.nqueue;
}

static void vms_world(int cluster)
{
    memset(&vms, 0, sizeof vms);
    vms.queues_ok = 1;
    queue("LPD_TS7500I", VP_QK_SERVER, VP_QS_IDLE, 0, 1, 0, 0, 0);
    queue("SYS$BATCH", VP_QK_GENERIC, VP_QS_IDLE, 0, 1, 0, 0, 3);
    queue("TCPIP$LPD_IN", VP_QK_GENERIC, VP_QS_IDLE, 0, 1, 0, 0, 0);
    queue("TCPIP$LPD_IN_X86VMS_1", VP_QK_SERVER, VP_QS_IDLE, 0, 1, 0, 0, 0);
    queue("TCPIP$SMTP", VP_QK_GENERIC, VP_QS_IDLE, 0, 1, 0, 0, 0);
    queue("TCPIP$SMTP_X86VMS_1", VP_QK_SERVER, VP_QS_IDLE, 0, 1, 0, 0, 0);
    queue("X86VMS_BATCH", VP_QK_BATCH, VP_QS_IDLE, 0, 4, 0, 0, 0);
    if (!cluster) return;
    vms.cluster = 1; vms.nodes = 3; vms.votes = 3; vms.quorum = 2; vms.nnode = 3;
    strcpy(vms.node[0].name, "MYI64"); strcpy(vms.node[0].version, "V8.4-2L3"); vms.node[0].votes = 1;
    strcpy(vms.node[1].name, "X86VMS"); strcpy(vms.node[1].version, "E9.2-4"); vms.node[1].votes = 1;
    vms.node[1].self = 1;
    strcpy(vms.node[2].name, "QUORUM"); strcpy(vms.node[2].version, "V9.2-3"); vms.node[2].votes = 1;
    vms.nshadow = 2;
    strcpy(vms.shadow[0].name, "DSA1:"); vms.shadow[0].members = 2; vms.shadow[0].devices = 2;
    vms.shadow[0].copy = vms.shadow[0].merge = -1;
    strcpy(vms.shadow[1].name, "DSA2:"); vms.shadow[1].members = 1; vms.shadow[1].devices = 2;
    vms.shadow[1].copy = 45; vms.shadow[1].merge = -1;
    vms.queue[6].status = VP_QS_BUSY; vms.queue[6].exec = 2; vms.queue[6].pend = 5;
    vms.queue[0].status = VP_QS_STOPPED;
}

/* n samples 2 s apart, CPU load rising and falling, a burst of network traffic */
static void run(int n)
{
    int i, c;
    for (i = 0; i < n; i++) {
        smp.ms += 2000;
        for (c = 0; c < smp.ncpu; c++) {
            unsigned busy = (unsigned) ((i * 7 + c * 30) % 100 + (c == 0 ? 0 : 0));
            smp.cpu[c].t[VP_M_USER] += busy * 2 * 6 / 10;
            smp.cpu[c].t[VP_M_KERNEL] += busy * 2 * 3 / 10;
            smp.cpu[c].t[VP_M_EXEC] += busy * 2 / 10;
            smp.cpu[c].t[VP_M_IDLE] += (100 - busy) * 2;
        }
        smp.disk[0].ops += 24;
        smp.disk[2].ops += 10700;
        smp.nic[0].rx += (uint64_t) (i % 10) * 300000;
        smp.nic[0].tx += (uint64_t) ((i + 5) % 10) * 20000;
        vp_app_sample(&app, &smp);
        /* 2 CPUs, 2 s: 200 ticks is 50% of the system */
        pr[3].cputim += 180; pr[3].dirio += 4; pr[3].bufio += 1200;
        pr[1].cputim += 40;  pr[1].dirio += 300;
        pr[5].cputim += 0;
        pr[6].cputim += 2;   pr[6].bufio += 40;
        vp_app_procs(&app, pr, npr, &scan, smp.ms);
    }
}

int main(void)
{
    world(2);
    run(1);
    snap("01_first_sample");
    run(60);
    snap("02_braille_80x24");
    app.graph = VP_G_BLOCK;
    snap("03_block_80x24");
    app.graph = VP_G_VT;
    snap("04_vt_80x24");

    /* keys */
    app.graph = VP_G_BRAILLE;
    vp_app_key(&app, '+');
    CHECK(app.interval == 3 && app.resample);
    app.resample = 0;
    vp_app_key(&app, '-'); vp_app_key(&app, '-'); vp_app_key(&app, '-');
    CHECK(app.interval == VP_MIN_INTERVAL);
    vp_app_key(&app, 'n');
    CHECK(app.nic == 1);
    vp_app_key(&app, 'n');
    CHECK(app.nic == 0);
    vp_app_key(&app, 'b');
    CHECK(app.nic == 1);
    vp_app_key(&app, 'g');
    CHECK(app.graph == VP_G_BLOCK && app.repaint);
    vp_app_key(&app, 'c');
    CHECK(app.colour == VP_C_256);
    CHECK(!app.quit);
    vp_app_key(&app, 'q');
    CHECK(app.quit);
    CHECK(vp_graph_parse("vt") == VP_G_VT && vp_graph_parse("bl") == VP_G_BLOCK && vp_graph_parse("B") < 0 && vp_graph_parse("x") < 0);
    CHECK(vp_colour_parse("mono") == VP_C_MONO && vp_colour_parse("256") == VP_C_256);

    /* a broadcast message takes the status line for a while */
    world(2);
    run(30);
    vp_app_message(&app, "Reply received from SYSTEM: backup starts at 03:00");
    snap("05_message");
    run(10);
    snap("06_message_gone");

    /* a big screen with many CPUs: the vms panel shows too (40 rows) */
    world(24);
    vp_app_resize(&app, 40, 132);
    run(120);
    CHECK(vp_app_panel(&app) == VP_P_BOTH && vp_app_shows_vms(&app));
    vms_world(0);
    vp_app_vms(&app, &vms);
    snap("07_24cpu_132x40");

    /* the process list (D8) */
    world(2);
    vp_app_resize(&app, 30, 110);
    run(10);
    snap("09_procs_110x30");
    CHECK(app.view.n == 7 && app.sel_pid == 0x2020014B);    /* top CPU, selected */
    vp_app_key(&app, VK_DOWN);
    CHECK(app.sel_pid == 0x20200120);
    vp_app_key(&app, '<');                                  /* CPU% to Pri */
    CHECK(app.sort == VP_S_PRI);
    app.sort = VP_S_PID;
    vp_app_update_view(&app);
    CHECK(app.sel_pid == 0x20200120 && app.sel == 1);       /* follows the PID */
    vp_app_key(&app, 'r');
    vp_app_update_view(&app);
    CHECK(app.sel == 5);
    app.reverse = 0;
    vp_app_key(&app, 't');
    snap("10_tree");
    vp_app_key(&app, 't');
    vp_app_key(&app, '/');
    vp_app_key(&app, 'i'); vp_app_key(&app, 'a'); vp_app_key(&app, 'n'); vp_app_key(&app, '_');
    CHECK(app.input == VP_IN_FILTER && !strcmp(app.filter, "ian_"));
    vp_app_key(&app, VK_DEL); vp_app_key(&app, VK_DEL);
    CHECK(!strcmp(app.filter, "ia"));
    vp_app_key(&app, VK_CTRL_U);
    CHECK(app.filter[0] == '\0');
    vp_app_key(&app, 'i'); vp_app_key(&app, 'a'); vp_app_key(&app, 'i'); vp_app_key(&app, 'n');
    vp_app_key(&app, '_');
    CHECK(!strcmp(app.filter, "iain_") && !app.quit);      /* typed letters aren't keys */
    snap("11_filter_typing");
    vp_app_key(&app, VK_RETURN);
    CHECK(app.input == VP_IN_NONE);
    vp_app_update_view(&app);
    CHECK(app.view.n == 3);
    app.filter[0] = '\0';

    /* details: the main loop is asked for the selected PID */
    app.sort = VP_S_CPU;
    app.sel_pid = 0x2020014B;
    vp_app_key(&app, 'i');
    vp_app_update_view(&app);
    CHECK(app.detail && app.detail_pid == 0x2020014B && app.detail_want);
    {
        vp_proc_detail d;
        memset(&d, 0, sizeof d);
        strcpy(d.image, "DSA1:[VIM.BIN]VIM.EXE;3");
        strcpy(d.login, "26-OCT-2026 01:31:56.10");
        strcpy(d.account, "STAFF");
        strcpy(d.privs, "GROUP TMPMBX WORLD NETMBX");
        d.wssize = 10u << 20; d.wsquota = 8u << 20; d.wsextent = 32u << 20;
        d.nquota = 2;
        strcpy(d.quota[0].name, "BYTLM"); d.quota[0].used = 7360; d.quota[0].limit = 128000; d.quota[0].bytes = 1;
        strcpy(d.quota[1].name, "DIOLM"); d.quota[1].used = 149; d.quota[1].limit = 150;
        vp_app_detail(&app, 1, &d, 0);
    }
    CHECK(!app.detail_want && app.det_ok);
    snap("12_details");
    vp_app_key(&app, 'i');
    CHECK(!app.detail);

    /* actions (D4): lowercase does nothing; the question is about the PID shown */
    vp_app_key(&app, 's');
    CHECK(app.input == VP_IN_NONE && app.ask_act == VP_ACT_NONE);
    vp_app_key(&app, 'D');
    CHECK(app.input == VP_IN_CONFIRM && app.flush && app.ask_proc.pid == 0x2020014B);
    snap("13_delete_question");
    app.flush = 0;
    vp_app_key(&app, 'n');
    CHECK(app.input == VP_IN_NONE && !app.act);
    vp_app_key(&app, 'S');
    app.sel_pid = 0x20200120;           /* the selection moves while asking */
    vp_app_update_view(&app);
    vp_app_key(&app, 'Y');
    CHECK(app.act == VP_ACT_SUSPEND && app.ask_proc.pid == 0x2020014B);
    app.act = 0;
    vp_app_action_done(&app, 1, "");
    CHECK(strstr(app.message, "2020014B IAIN_1 suspended") != 0);
    vp_app_key(&app, 'R');
    vp_app_key(&app, VK_RETURN);        /* only Y says yes */
    CHECK(!app.act && strstr(app.message, "nothing done"));
    /* priority: typed, checked, then asked */
    vp_app_key(&app, 'P');
    CHECK(app.input == VP_IN_PRIO);
    vp_app_key(&app, '6');
    snap("14_priority");
    vp_app_key(&app, VK_RETURN);
    CHECK(app.input == VP_IN_CONFIRM && app.ask_arg == 6);
    vp_app_key(&app, 'y');
    CHECK(app.act == VP_ACT_PRIORITY);
    app.act = 0;
    vp_app_action_done(&app, 0, "%SYSTEM-F-NOPRIV, insufficient privilege");
    CHECK(strstr(app.message, "can't set the priority of 20200120: %SYSTEM-F-NOPRIV") != 0);
    vp_app_key(&app, 'P'); vp_app_key(&app, '4'); vp_app_key(&app, '5'); vp_app_key(&app, VK_RETURN);
    CHECK(app.input == VP_IN_NONE && !app.act && strstr(app.message, "0 to 31"));
    /* never VTOP itself */
    app.sel_pid = 0x20200160;
    vp_app_update_view(&app);
    vp_app_key(&app, 'D');
    CHECK(app.input == VP_IN_NONE && strstr(app.message, "won't delete its own"));
    vp_app_key(&app, 'S');
    CHECK(app.input == VP_IN_NONE);
    /* Ctrl/C during a question still quits */
    vp_app_key(&app, 'R');
    vp_app_key(&app, VK_QUIT);
    CHECK(app.quit && !app.act);
    app.quit = 0;
    app.input = VP_IN_NONE;
    app.message_ticks = 0;
    vp_app_key(&app, 'h');
    snap("15_help");
    vp_app_key(&app, 'x');
    CHECK(!app.help && !app.quit);

    /* the vms panel (D9) at 80x24: v steps net, vms, both */
    world(2);
    run(30);
    CHECK(vp_app_panel(&app) == VP_P_NET && !vp_app_shows_vms(&app));
    vp_app_key(&app, 'v');
    CHECK(vp_app_panel(&app) == VP_P_VMS && app.vms_want);
    snap("16_vms_reading");
    vms_world(0);
    vp_app_vms(&app, &vms);
    CHECK(!app.vms_want && app.have_vms);
    snap("17_vms_80x24");
    vp_app_key(&app, 'v');
    CHECK(vp_app_panel(&app) == VP_P_BOTH);
    snap("18_both_80x24");
    vp_app_key(&app, 'v');
    CHECK(vp_app_panel(&app) == VP_P_NET);
    vp_app_resize(&app, 20, 80);            /* no room for both: vms, then net */
    vp_app_key(&app, 'v'); vp_app_key(&app, 'v');
    CHECK(vp_app_panel(&app) == VP_P_NET);
    app.panel = VP_P_BOTH;                  /* from a file written on a bigger screen */
    CHECK(vp_app_panel(&app) == VP_P_NET);
    /* a cluster, shadow sets, a stopped and a busy queue, on a wide screen */
    vp_app_resize(&app, 44, 132);
    vms_world(1);
    vp_app_vms(&app, &vms);
    CHECK(!strcmp(app.vms.queue[app.vms_order[0]].name, "LPD_TS7500I"));   /* stopped first */
    CHECK(!strcmp(app.vms.queue[app.vms_order[1]].name, "SYS$BATCH"));     /* then with jobs */
    CHECK(!strcmp(app.vms.queue[app.vms_order[2]].name, "X86VMS_BATCH"));
    run(5);
    snap("19_cluster_132x44");
    vms.quorum = 4;
    vms.queues_ok = 0;
    strcpy(vms.queue_err, "%JBC-E-NOQUEMGR, queue manager not running");
    vp_app_vms(&app, &vms);
    snap("20_no_queue_manager");

    /* the options file (D10): applied, and read back for w */
    {
        vp_opts o;
        vp_opts_init(&o);
        o.sort = VP_S_MEM; o.tree = 1; o.panel = VP_P_VMS; o.interval = 5;
        strcpy(o.network, "ewb0");
        world(2);
        vp_app_apply(&app, &o);
        CHECK(app.sort == VP_S_MEM && app.tree && app.panel == VP_P_VMS && app.interval == 5);
        CHECK(app.graph == VP_G_BRAILLE);   /* not given: left alone */
        run(1);
        CHECK(app.nic == 1 && !app.want_nic[0]);
        vp_app_key(&app, 'w');
        CHECK(app.save);
        vp_app_options(&app, &o);
        CHECK(o.sort == VP_S_MEM && o.tree == 1 && o.reverse == 0 && o.panel == VP_P_VMS
              && !strcmp(o.network, "EWB0") && o.graph == VP_G_BRAILLE);
        CHECK(!app.graph_chosen);
        vp_app_key(&app, 'g');
        CHECK(app.graph_chosen);
    }

    /* too small */
    vp_app_resize(&app, 18, 60);
    snap("08_too_small");

    vp_app_free(&app);
    printf("test_app: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
