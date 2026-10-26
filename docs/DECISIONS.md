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
