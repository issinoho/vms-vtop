# vms-vtop: a btop-style system monitor for OpenVMS

## Goal

A full-screen, live system monitor for OpenVMS x86-64 and IA64, in the style of btop:
boxed panels with history graphs for CPU, memory, disks and network, and a sortable,
filterable process list you can act on. Plus what only VMS has: processor modes,
page and swap files, shadow sets, queues and the cluster.

Green-field C (D2), the family layout (D3), PCSI kits for X86VMS and I64VMS, the family
release process.

## The screen (80x24 at least; more rows and columns give more of everything)

```
┌ cpu ─ X86VMS  V9.2-3  up 12d 04:10 ───────────────── 2s ┐┌ mem ──────────────────┐
│⣀⣀⣠⣤⣀⡀⢀⣀⣀⣤⣶⣿⣶⣤⣀⣀⡀⢀⣀⣀⣀⣀⣠⣤⣤⣀⣀⡀⢀⣀⣀⣀⣠⣤⣤⣶⣶⣤⣀⣀  CPU0 ■■■□□ 41%││ Total    8.0G         │
│⣀⣀⣀⣠⣤⣀⣀⣀⣀⣀⣠⣤⣶⣤⣀⣀⣀⣀⣀⣀⣀⣠⣤⣤⣀⣀⣀⣀⣀⣀⣀⣀⣠⣤⣤⣤⣀⣀  CPU1 ■■□□□ 22%││ Free     5.1G ■■■■■□□ │
│ user 24  super 1  exec 3  kern 6  int 1  mpsync 0    ││ Modified 0.2G ■□□□□□□ │
│ load 0.4  procs 42  users 3                          ││ Page     2.0G ■□□□□□□ │
└──────────────────────────────────────────────────────┘└───────────────────────┘
┌ disks ───────────────────────┐┌ net ─ EWA0 ─────────────────────────────────────┐
│ DKA0: SYSTEM 62% ■■■■□  12/s ││ ↓ 1.2 MB/s ⣀⣀⣠⣤⣶⣤⣀  ↑ 40 KB/s ⣀⣀⣀⣀⣠⣀⣀         │
│ DSA1: USER   18% ■□□□□ 140/s ││                                                 │
└──────────────────────────────┘└─────────────────────────────────────────────────┘
┌ proc ─ sort cpu ▼ ─ filter ─ WORLD ──────────────────────────────────────────────┐
│ PID      Process          User      St   Pri    CPU%   WS     DIO/s  Image       │
│ 00004D7D FTA1039_IAIN     IAIN      CUR  4/4    12.0   9.2M      30  VTOP        │
│ 0000040A BESZEL_AGENT     BESZEL    HIB  4/4     0.4   4.1M       0  BZ_AGENT    │
└──────────────────────────────────────────────────────────────────────────────────┘
 F1 help  F2 options  / filter  s sort  t tree  k delete  z suspend  p priority  q quit
```

## Phase 0: probes and scaffolding (started 2026-10-26)

- Repo, docs, `tools/vms.sh`, work directories on both nodes. Done.
- `probe_jpi`: a wildcard `$GETJPI` scan, cost per pass with and without the image
  name, what the caller sees with WORLD and GROUP off (D5), thread counts.
- `probe_dvi`: `DVI$_OPCNT` per disk against MONITOR DISK, for the disk panel's rates.
- `probe_glyph` (`tools/glyphs.sh`): braille, block elements, DEC graphics and colour
  through the terminal driver, bytes checked on the host (D6, D7).
- Already known from vms-beszel and vms-hass: everything else the panels need.

Status 2026-10-26: all three probes run on both nodes (NOTES N1-N5). A full process
scan with image names costs 2-4 ms; without WORLD the caller sees only its own
processes; disk op rates agree with MONITOR; UTF-8 and colour pass the driver.
The disk panel shows op/s, not MB/s (N4).

Exit: NOTES for each probe; the user signs off this plan and D1-D7. Done 2026-10-26.

## Phase 1: the panels, read-only (done 2026-10-26)

- Copy in vtree's screen model, renderer and keyboard; the stub platform and snapshot
  tests (`make test`).
- Sampler from vms-hass, plus per-CPU modes; history rings and rates in core.
- CPU, memory, disks, network panels; refresh interval (`+`/`-`), resize, broadcast
  messages, CTRL/C and CTRL/Y leave the terminal as it was.
- Live test on both nodes over a pseudo-terminal (vtree's `live.sh`, `vtscreen.py`).

Status 2026-10-26: done on both nodes (NOTES N6, N7). `make test` (73 core checks, 19
app checks and 8 snapshots) passes on Linux and, through `build_vms TEST`, on both
nodes, where the screens match Linux's. `tools/live.sh` and `test_live2.com` drive
VTOP on a pseudo-terminal: all graph and colour modes, keys, Ctrl/C, Ctrl/Y, a
broadcast message, a resize. `make demo` runs it on Linux with an invented system;
`tools/vtshot.py` makes PNG screenshots of either. Still to see: VTOP from a real
xterm and a real VT or DECterm, by the user.

Exit: the user runs VTOP on both nodes from their own terminal and signs off. Done
2026-10-26: a VT525 in colour, a monochrome VT520 and an xterm-type terminal (braille,
24-bit colour, the whole window: N8).

## Phase 2: the process list and its actions (done 2026-10-26)

- Scan, CPU% and DIO/BIO rates from deltas, sort by any column, `/` filter, tree by
  owner PID, details page for one process (quotas, privileges, image, terminal).
- Actions (D4): suspend, resume, delete, set priority, with the confirmation rules.

Status 2026-10-26: built to D8 on both nodes (NOTES N9). `make test` (121 core checks,
55 app checks, 15 snapshots) passes on Linux and through `build_vms TEST` on both
nodes. `test_live4.com` drives the list, filter, details and every action on a
subprocess it creates, and checks each change with `F$GETJPI` from outside VTOP.
The user tried it from their own terminal and signed off D8 on 2026-10-26.

Exit: the user tries the list and the actions from their own terminal (on a process
of their own) and signs off D8.

## Phase 3: VMS panels and options

- Queues, cluster members and votes, shadow set copy/merge progress, page/swap files.
- Graph modes (D6), palette and monochrome (D7), an options file in SYS$LOGIN.

## Phase 4: kit and release

- PCSI kit ISSINOHO VTOP, VTOP$STARTUP.COM, VTOP$SETUP.COM (defines the command), an
  install test on both nodes; tag v0.1.0, GitHub release, family site entry.
