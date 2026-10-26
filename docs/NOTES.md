# Notes: platform behaviour, and how it was observed

## N1. A wildcard $GETJPI scan is cheap (2026-10-26, x86 E9.2-4 and IA64 V8.4-2L3)

`probes/probe_jpi.c`, 46 processes on x86 and 48 on IA64, with WORLD on:
- Cheap items only (PID, owner, master PID, state, priorities, CPU time, page counts,
  faults, DIO, BIO, mode, thread count, process name, user, terminal): 1–3 ms of wall
  time per full pass, 0–1 ticks of our own CPU.
- With `JPI$_IMAGNAME` too (an AST into each process): 15 ms on the first pass on x86,
  then 2–4 ms on both nodes. Every process answered, none suspended.
- `JPI$_GETJPI_CONTROL_FLAGS` with `NO_TARGET_INSWAP | IGNORE_TARGET_STATUS` is in the
  headers on both compilers.
So a full scan every second, image names included, costs VTOP well under 1% of a CPU.

## N2. What the $GETJPI items mean, checked against SHOW SYSTEM (2026-10-26, both)

- `PPGCNT + GPGCNT` are **pagelets** (512 bytes); divided by 16 they equal SHOW
  SYSTEM's "Pages" exactly (e.g. 169456 + 7296 = 176752 / 16 = 11047).
- `CPUTIM` is 10 ms ticks (1976130 = 5:29:21.30).
- SHOW SYSTEM's "I/O" is `DIRIO + BUFIO`.
- `STATE` is the `SCH$C_` code: 14 CUR, 7 HIB, 5 LEF.
- `MODE`: 0 other (detached), 1 network (the ssh-spawned DCL sessions), 3 interactive.
- A process with several kernel threads (MariaDB, `KT_COUNT` 2) comes back **once**:
  no duplicate PIDs in any pass.
- `IMAGNAME` is a full file spec, sometimes with `;version`, sometimes without; empty
  for a process at DCL level. The display takes the name part.
- `TERMINAL` was empty for every process seen, including network-mode DCL.

## N3. Without WORLD and GROUP the caller sees only its own processes (2026-10-26, both)

`$SETPRV` off WORLD and GROUP for the image: 5 processes on x86 and 4 on IA64 (the
caller's own: its DCL, subprocesses and ssh session); every other process is
`SS$_NOPRIV`, which is counted, not shown (D5). The user's account has both privileges
on by default, so a normal VTOP run here shows everything.

## N4. DVI$_OPCNT gives per-disk I/O rates that agree with MONITOR (2026-10-26, both)

`probes/probe_dvi.c`, two `$DEVICE_SCAN` passes over class DISK 5 s apart, against
`MONITOR DISK/ITEM=OPERATION_RATE` over the same 5 s (not the same instant):
x86 busy disk 5352 op/s vs 5390, others within a few op/s; IA64 63 vs 45. VMS keeps no
per-disk byte count, so the disk panel shows operations per second, not MB/s (btop
shows bytes). Unmounted devices (`DVI$_MNT` 0) and shadow members are left out, as in
vms-hass.

## N5. UTF-8 and colour reach the terminal unchanged (2026-10-26, both)

`probes/probe_glyph.c` under PTYDRIVE (a pseudo-terminal, VT100 type 96, 80x24),
writing with `IO$_WRITEVBLK|IO$M_NOFORMAT`: braille (U+2800–28FF), block elements
(U+2581–2588), DEC Special Graphics, and 16-, 256- and 24-bit colour SGR all arrived
byte for byte, **with the terminal's EIGHTBIT off as well as on**. So the terminal
driver is no obstacle; whether glyphs and colours show is up to the emulator, and
nothing on the VMS side says which emulator it is (D6).

`MONITOR DISK/OPERATION_RATE` is not a qualifier; it is `/ITEM=OPERATION_RATE`.
`F$GETJPI("","CURPRIV")` overflows DCL's token limit for this account (beszel N17).
