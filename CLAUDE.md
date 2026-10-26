# CLAUDE.md

Working notes for vms-vtop: a btop-style full-screen system monitor for OpenVMS (x86-64
and IA64). `docs/PLAN.md` is the phased plan; `docs/DECISIONS.md` records every decision
and the alternatives considered; `docs/NOTES.md` records platform behaviour and how it
was observed. Read them before starting work. Work phase by phase, and stop and ask at
each phase exit.

Green-field code, not a port (D2). Copy no btop code, themes or artwork (D1).

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work. Never raw `ssh host cmd`, never `WAIT` over
  ssh. Run long jobs with `run_in_background`. Pass DCL specs to it in single quotes.
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), credentials or passwords. Write `<x86-host>` instead.
- **Nothing destructive or system-wide on the nodes without asking**: no PRODUCT
  INSTALL, no SYLOGIN or startup edits, no system logical names, no UAF changes.
  Process actions are tested only on processes the test itself created.
- Prefer a 20-line probe in `probes/` over speculation. Don't claim something works
  until a test on the node shows it. Probes and tests are bounded.

## Hard constraints

- **C99, VSI C, 32-bit pointers.** No C11, no VLAs, no anonymous unions. The Linux
  build uses `-std=c99 -pedantic -Wall -Wextra -Werror`.
- **`src/core/` does no I/O** (D3). `src/plat/vp_platform.h` is the whole OS surface
  and has no VMS types.
- **VTOP enables no privilege** (D5).
- **The screen goes through the core renderer**, never SMG$ output calls: SMG$ is for
  keys and broadcast trapping only (vtree D11).

## Testing

`make test` (Linux: core checks and screen snapshots in `tests/snap`; `make snapshots`
rewrites them, then read every changed one). `make demo` runs VTOP on this terminal
with an invented system. `tools/vtshot.py` runs it on a pseudo-terminal (or replays a
PTYDRIVE transcript) and saves a PNG: look at the screens, not just the text.
On the nodes: `tools/push.sh <node>` then `tools/vms.sh <node> dcl '@build_vms TEST'`;
`tools/live.sh <node>` for the live test (`tests/vms/live_keys.txt`), and
`tests/vms/test_live2.com` for broadcast, resize and Ctrl/Y. `tests/vms/t_sample.com`
compares the sampler with MONITOR.

When the nodes look busy, check SHOW SYSTEM before blaming VTOP: other sessions run
tests there too (N6).

## OpenVMS

Undefined symbols are *warnings* on VMS: check `$SEVERITY` after every LINK. x86 has
VSI C V7.7, IA64 V7.4: take item codes and sizes from the headers on the build node.
From the siblings: `#include "../x.h"` doesn't work in VSI C; the C RTL lowercases
unquoted `argv`; `LIB$WAIT` needs the float type argument (4, `LIB$K_IEEE_S`);
`RMI$_MODES`'s return length is wrong, size it from the CPU count (beszel N3, N5).
