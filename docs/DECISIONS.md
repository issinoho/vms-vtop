# Decisions

## D1. Name: VTOP, "inspired by btop" (2026-10-26, agreed with the user)

Matches VTREE, VLMF and VFTP. Repo `vms-vtop`, command `VTOP`, PCSI product `VTOP`.
btop (Apache-2.0) gives the ideas: boxed panels, history graphs, a sortable process
list. No btop code, themes or artwork is copied.

## D2. Green-field C, not a port of btop (2026-10-26, agreed with the user)

Upstream btop needs C++20 (GCC 11 / Clang 16 or later). x86 has VSI's clang-based C++,
IA64 only HP C++, and neither is C++20. btop's data collectors are written per OS, so a
VMS collector would be new code even in a port; what a port would keep is the drawing,
which is the easy part to rebuild. Considered: porting btop to VSI C++ on x86 only
(no IA64, a large C++20-to-C++17 back-port to maintain against upstream).

## D3. Code layout and reuse (2026-10-26)

The family layout: C99, VSI C, 32-bit pointers. `src/core/` does no I/O: history rings,
rates from counters, process sorting, filtering and tree, the screen model, the
renderer, the panels' layout and drawing. `src/plat/vp_platform.h` is the whole OS
surface, without VMS types; `src/plat/vms/` implements it, `src/plat/stub/` fakes a
changing system so the screens are snapshot-tested on Linux. Prefix `vp_` (core) and
`vpp_` (platform).

Reused, copied and adapted (not shared libraries):
- vms-vtree: the screen model, the ANSI renderer (`vt_ansi.c`, D11 there) and the
  SMG$ keyboard and broadcast trapping.
- vms-hass (sampler, from vms-beszel): CPU modes per CPU (`RMI$_MODES`, beszel N3),
  memory and page/swap files, disks and shadow sets, NICs, queues, cluster, uptime.

## D4. Process actions are in the first release (2026-10-26, agreed with the user)

Suspend, resume, delete (STOP/ID) and set priority, in Phase 2. Each one names the
process (PID, name, user, image), asks once, `Y` or `N` with default `N` and typeahead
thrown away first (vtree D10, N12), and acts on the PID it showed, never on "the
selected line" read again later.

## D5. Show only what the caller may see (2026-10-26, agreed with the user)

No privilege is enabled by VTOP. The process list is what `$GETJPI` gives the caller:
WORLD sees every process, GROUP the group's, otherwise only the caller's own. The
header says which. Actions rely on the system's own checks, with the exact status
shown on failure.

## D6. Graph characters by terminal (2026-10-26, agreed with the user; detection amended)

btop draws graphs in braille. Three modes: `BRAILLE` and `BLOCK` (UTF-8, for
xterm-type terminals) and `VT` (DEC Special Graphics scan lines, reverse-video bars and
ASCII, for real VTs and DECterm). The driver passes all of them (N5). Chosen by the
`/GRAPH=` qualifier, else the logical name `VTOP$GRAPH`, else detected: BRAILLE on a
UTF-8 terminal, VT otherwise.

Detection (amended 2026-10-26, Phase 1): the agreed rule was "not a DEC VT terminal type
means UTF-8", but VMS has no device type for xterm-type emulators: they show as a VT
type (from SET TERMINAL/INQUIRE or the SSH server's default) or UNKNOWN, just as a real
VT does, so the type says nothing. Instead VTOP writes one braille character at column 1 and asks
for the cursor position (DSR 6, which every VT and emulator answers): column 2 means
UTF-8, column 4 means not. No answer within a second: VT. This is the standard check
for UTF-8 terminals.

## D7. Colour palette (2026-10-26, agreed with the user)

A btop-like look of our own (no btop theme copied): terminal background, green-yellow-red
gradients on meters and graphs, a colour per panel frame, written as ANSI colour by the
renderer. Modes: `24` (24-bit), `256`, `8` (SGR 30-37, which DECterm shows) and `MONO`
(bold and reverse only). Chosen by `/COLOUR=`, else `VTOP$COLOUR`, else 24 on a UTF-8
terminal and 8 otherwise: a real VT would read the numbers in a 24-bit sequence as
other renditions (1 bold, 4 underline, 5 blink, 7 reverse). The proposal said 16
colours; the bright half (SGR 90-97) is not known to DECterm, so it is 8.

## D8. The process list and its keys (2026-10-26, Phase 2; signed off by the user at the exit)

- **Scan**: one wildcard `$GETJPI` pass per sample, image names included (N1). Rows
  are kept in PID order; rates come from the previous scan of the same PID.
- **Columns**, shown in this order and dropped from the right of this priority list
  when the panel is narrow: PID, Name, CPU%, St, Mem, User, Pri, CPU time, DIO/s,
  BIO/s, Image (the rest of the width). St comes before Mem so that an 80-column
  screen still shows SUSP. CPU% is of the whole system, so the column
  adds up to the CPU panel's figure (btop's default; `top` uses one CPU). Mem is the
  working set in use, (PPGCNT + GPGCNT) x 512 bytes (N2).
- **Keys**: Up/Down/Prev/Next select; Left/Right or `<` `>` change the sort column, `r`
  reverses it (default CPU%, highest first); `t` a tree by owner (creator) PID; `/` or
  `f` a filter (name, user, image or PID, any case; Return keeps it, an empty one
  clears it); `i` or Return the details (image, quotas, privileges, login time); `h`,
  `?` or Help lists every key.
- **Actions (D4)** are capitals so that a lowercase slip does nothing: `S` suspend, `R`
  resume, `D` delete, `P` priority (typed, 0-31). The question box names PID, process,
  user and image, typeahead is thrown away first, `Y` alone confirms, and the action
  goes to that PID. VTOP refuses to suspend or delete its own process.
- The selection follows the PID across scans and re-sorts, not the line.

## D9. The vms panel (2026-10-26, Phase 3; signed off by the user at the exit)

What only VMS has, read without privilege (D5, N10): the cluster, shadow sets and
queues. Page and swap files stay as the totals in the mem panel: per-file figures
need privilege (`SHOW MEMORY/FILES` has it, `SYI$_PAGFILCNT` reads 0 without).

- **Where**: the bottom-left slot shows the net panel, the vms panel, or both stacked.
  `v` steps net, vms, both. Unless the options file says otherwise, both when the
  screen has 40 rows or more, else net (at 80x24 the slot has 4 lines inside).
- **Title**: "standalone", or "cluster N nodes" with the votes and quorum (votes below
  quorum in warning colour).
- **Lines**, in this order, as many as fit, each with "+N more" when cut:
  cluster members (name, version, votes; this node bold), shadow sets (virtual unit,
  members, "steady", or "copying 45%" / "merging 12%" in warning colour), then a
  "queues" divider with the queue and job counts and one line per queue: name, kind
  (wide panels only), state, run (executing/limit), pend, hold (holding + timed
  release).
- **Queue order**: queues that need someone first (stopped, stalled, paused,
  unavailable, disabled, closed, stopping), then queues with jobs or busy, then the
  rest, by name within each. Problem states in warning colour.
- **Cost**: one wildcard `$GETQUI`, a `$GETSYI` node pass and a `$GETDVI` per shadow
  set per sample, only while the panel shows (a queue pass is 0.5-2 ms, N10).

## D10. The options file (2026-10-26, Phase 3; signed off by the user at the exit)

- `SYS$LOGIN:VTOP.INI`, or the file the logical name `VTOP$OPTIONS` names (tests use
  this, so they never touch the user's file). Lines `name = value`, `!` or `#`
  comments, names and values any case.
- Options: `graph`, `colour`, `interval`, `sort` (pid name user state pri cpu time
  mem dio bio image), `reverse`, `tree` (yes/no), `panel` (net vms both auto),
  `network` (a device name).
- Precedence: qualifier, then logical name (`VTOP$GRAPH`, `VTOP$COLOUR`), then the
  file, then what VTOP detects. A bad line is skipped and named in the status line.
- VTOP writes the file only when you press `w` (a new version, each time; nothing is
  saved on exit). `graph` and `colour` are written only if they were chosen in this
  session (qualifier or `g`/`c`); otherwise what the file had is kept, so saving from
  an xterm doesn't put braille on a VT520 next time.
