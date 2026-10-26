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

## D6. Graph characters by terminal (2026-10-26, proposed)

btop draws graphs in braille. Three modes: `BRAILLE` and `BLOCK` (UTF-8, for
xterm-type terminals) and `VT` (DEC Special Graphics scan lines and ASCII, for real VTs
and DECterm). The driver passes all of them (N5) but can't tell which emulator is on
the other end, so: `/GRAPH=` qualifier, then the logical name `VTOP$GRAPH`, then the
options file; default `BRAILLE` when the terminal type is not a DEC VT, else `VT`.
(Considered: asking the terminal with DA1 or XTVERSION at start-up; it adds a timed
read and some emulators answer neither. Could come later.)

## D7. Colour palette (2026-10-26, proposed)

btop's default theme look (dark background, green/yellow/red gradients on meters),
written as ANSI colour by the renderer. 24-bit colour by default on non-VT terminal
types, 256 and 16 colour on request (all three pass the driver, N5), and a monochrome
mode for VT100-class terminals. Chosen like D6: `/COLOUR=`, `VTOP$COLOUR`, options file.
