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

    /* a big screen with many CPUs */
    world(24);
    vp_app_resize(&app, 40, 132);
    run(120);
    snap("07_24cpu_132x40");

    /* too small */
    vp_app_resize(&app, 18, 60);
    snap("08_too_small");

    vp_app_free(&app);
    printf("test_app: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
