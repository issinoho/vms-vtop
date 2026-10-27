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

## N6. VTOP's figures agree with MONITOR (2026-10-26, x86 and IA64)

`tests/vms/t_sample.c` (VTOP's sampler and model, no terminal), two samples 3 s apart,
then `MONITOR MODES` over the next 3 s: IA64 100% busy, kernel 67 / interrupt 22 /
user 10 in both; x86 between 50% (one CPU in kernel) and 100%, the same in both.
Disk op/s agree with `MONITOR DISK` (N4). The load was real: a `vim.exe` from the
vms-vim work directory spinning in another session's ssh login on each node (x86 since
01:01, IA64 since 01:31), which a SHOW SYSTEM diff missed at first because CUR lines
have an extra CPU column. VTOP itself used 0.06 s of CPU in 22 s at a 2 s interval.

## N7. The terminal on the nodes (2026-10-26, x86 and IA64, PTYDRIVE pseudo-terminal)

- With nothing answering DSR (PTYDRIVE doesn't), the UTF-8 check times out after 1 s
  and VTOP chooses VT and 8 colours (D6, D7). The check's answering path was tested
  on Linux only (`tools/vtshot.py` answers both ways); on the nodes it waits for a
  real terminal.
- Ctrl/C and Ctrl/Y end VTOP through the channel ASTs; DCL answers straight after, and
  Ctrl/Y at DCL gives "Interrupt" again (the CLI's Ctrl/Y and Ctrl/T are put back).
- A REPLY to the terminal while VTOP runs shows in the status line ("Reply received on
  X86VMS from user IAIN ... VTOP broadcast test"), its CR/LFs made spaces.
- `SET TERMINAL/WIDTH=100/PAGE=30` from a subprocess while VTOP runs: the next sample
  polls `DVI$_DEVBUFSIZ`/`DVI$_TT_PAGE`, and VTOP redraws the whole screen at 100x30.
- SHOW TERMINAL after VTOP: the characteristics as before (keypad numeric, no
  eightbit).
- `IO$M_CTRLCAST`'s AST address needs an integer cast: `(void *)` of a function
  pointer is `%CC-I-NONSTANDCAST` on IA64, and the build stops on any message.

## N8. The terminal's real size (2026-10-26, x86 and IA64; the user's terminals)

An SSH session in a large xterm window still had VMS width 80 and page 24, so VTOP
drew 80x24 in the corner (the user's screenshot). VTOP now asks the terminal: cursor
to 999;999, DSR 6, cursor back (ESC 7 / ESC 8); the answer is the size. VMS's settings
win again only if they change while VTOP runs (SET TERMINAL/WIDTH); Ctrl/W asks
again after a window resize. The logical name `VTOP$SIZE` ("132x40") sets it for a
terminal that doesn't answer. With `VTOP$SIZE` 100x30 on an 80x24 pseudo-terminal
(`tests/vms/test_live3.com`) VTOP drew all 100 columns and 30 rows with no wrapping
(`IO$M_NOFORMAT` output is not wrapped at the VMS width), and left the terminal at
80x24. Seen by the user: the VT525 and VT520 emulators answer the UTF-8 check as
non-UTF-8 (VT mode, 8 colours) and the xterm-type terminal as UTF-8 (braille, 24-bit).
A monochrome VT520 ignored bar cells drawn as background colour: bars are now reverse
video in their colour, which shows on both.

## N9. The process list and its actions on the nodes (2026-10-26, x86 and IA64)

`tests/vms/test_live4.com` (`live4_keys.txt`) on an 80x24 pseudo-terminal. It spawns
VPTEST1 (a subprocess that only waits) and acts on nothing else (CLAUDE.md):
- The list shows every process with WORLD (44 on x86, 47 on IA64); the filter
  "VPTEST1" leaves one; its details read back (UIC, account, "none (at DCL level)"
  as the image, mode network, owner PID, login time, quotas).
- "Sy" typed together: the `y` is thrown away with the typeahead and the question
  still waits; the `n` that follows answers it ("nothing done").
- Suspend, then outside VTOP `F$GETJPI`: state SUSP. Resume: LEF. Priority 2: PRIB 2,
  while PRI (the current priority, as in SHOW SYSTEM's column) stays 6 for a while:
  VMS's boost after the wait, not a failure. Delete: the process is gone.
- Delete answered `n` leaves it alone; D on VTOP's own process (image VTOP) is
  refused before any question.
- A suspended process still answers the scan (`JPI$M_IGNORE_TARGET_STATUS`), so it
  stays in the list as SUSP and can be resumed.
- `$SETPRI`, `$SUSPND`, `$RESUME` and `$DELPRC` (the `__NEW_STARLET` prototypes, the
  same on both compilers) need no privilege for the caller's own subprocess.

VTOP's own cost with the process scan, `tests/vms/test_cpu.com` (20 s at
`/INTERVAL=1`, the worst case): x86 0.26 s of CPU (about 1.2% of one CPU; 0.06 s at
2 s before the scan, N6). IA64's accounting showed no change from 0.01 s although
VTOP drew for the whole 20 s: its short bursts fall between the clock ticks that
charge CPU time, so take the x86 figure.

## N10. What the vms panel can read without privilege (2026-10-26, x86 and IA64)

`probes/probe_vms.c` (`probe_vms.com`), as the user's account:
- A wildcard `$GETQUI` (`QUI$_DISPLAY_QUEUE`, `QUI$M_SEARCH_WILDCARD`, "*") lists all
  7 queues on each node with status, flags and job counts; a pass costs 2 ms on x86
  and 0.5 ms on IA64. The end is `JBC$_NOMOREQUE` in the IOSB's first *longword*:
  reading it as a word gives 81AA and an unknown message. The kind is in the flags
  (`QUI$M_QUEUE_BATCH` 1, `GENERIC` 200, `TERMINAL` 40000, `PRINTER` 1000000), the
  state in the status (`IDLE` 2, `BUSY` 10000, `STOPPED` 1000, `SERVER` 200, ...):
  the two sets reuse mask values, so they must not be mixed.
- Jobs waiting with /AFTER are timed release (`QUI$_TIMED_RELEASE_JOB_COUNT`), not
  holding: SYS$BATCH's three housekeeping jobs show as timed 3, hold 0.
- Neither node is in a cluster: `SYI$_CLUSTER_MEMBER` 0, and a wildcard `$GETSYI`
  (csid -1) ends at once with NOMORENODE, without even this node.
- Neither node has shadow sets; the `DVI$_SHDW_*` items read 0 on a plain disk.
- `SYI$_PAGFILCNT` and `SYI$_SWPFILCNT` read 0, though x86 has two page files and a
  swap file (`SHOW MEMORY/FILES`, which is installed with privilege).

So the cluster and shadow set lines are tested on the stub only; on these nodes the
panel shows "standalone" and the queues.

## N11. The vms panel and the options file on the nodes (2026-10-26, x86 and IA64)

`tests/vms/test_live5.com` (`live5_keys.txt`) on an 80x24 pseudo-terminal. It holds a
job of its own (VPTEST_Q, in SYS$BATCH, deleted at the end) and points `VTOP$OPTIONS`
at VPTEST.INI in the work directory:
- `v` shows the vms panel in the net panel's place: "standalone", "queues 7 jobs 4",
  SYS$BATCH first with hold 4 (three timed-release housekeeping jobs and the test's
  held one), then the rest by name, "+5 more". IA64's SYS$BATCH is an execution queue
  and shows run 0/4; x86's is generic and shows none. `v` again: both (the vms panel
  has one queue line at 80x24), then net.
- `w` wrote VPTEST.INI;1 with interval, sort, reverse, tree, panel, network and graph
  (given as `/GRAPH=VT`, so chosen in this session; colour wasn't, so it is left
  out), and the status line named the file with its version.
- VTOP started again with that file: the vms panel, sorted by Mem, as a tree. With a
  line `colr = 8` added, the status line says "...VPTEST.INI line 10: unknown option
  "colr"" and the rest of the file still applies.

Cost, `test_cpu.com cpu5_keys.txt` (20 s at `/INTERVAL=1` with the vms panel): x86
VTOP 0.13 s of CPU, the QUEUE_MANAGER process one 10 ms tick on each node. IA64's
accounting again shows no change for VTOP (N9).

## N12. PTYDRIVE's limits, seen making the README banner (2026-10-26, x86)

`tests/vms/banner_keys.txt` runs VTOP at 132x40 in braille and 24-bit colour, with a
subprocess of its own (VPBUSY, `busy.com`, priority 1) keeping one CPU busy.
- PTYDRIVE caps a step's wait at 30 s and takes a longer one as 1.5 s, so "45 shot"
  sent the next keys at once. Typed while VTOP asked the terminal its cursor position
  at start-up, `ESC [ C` went to that question (harmless: it times out) and the `q`
  to the first key wait, which threw its key away, so that VTOP never quit. Now a key
  typed during start-up counts (`vp_main.c`).
- Its transcript held 64 KB a step and dropped the rest without a word: a 30 s step at
  132x40 in braille sends some 100 KB, so a frame was cut inside an SGR sequence and
  `vtshot.py` showed a stray "l". Now 1 MB, with `{TRUNCATED}` if it is ever full.
  The 80x24 live tests stay well under the old limit (no cut sequences in them).
- It ends each read with a newline, which can fall inside a sequence; `vtshot.py`
  now drops every newline that isn't the terminal's own `<LF>`.
- DCL delta times want seconds below 60: `+0:0:70` is no time at all.

`tests/vms/vt_keys.txt` (run by `test_shot.com`) takes the README's second picture the
same way, at 80x24 in VT mode and 8 colours, as a VT525 shows it (2026-10-27). Its last
step is `logout`, so PTYDRIVE's child ends by itself rather than being deleted.

## N13. The T0.1-0 kits installed on both nodes (2026-10-26, x86 and IA64)

PRODUCT INSTALL (the user agreed: install on both, keep it installed) with
`tests/vms/pcsi_install.com`: ISSINOHO X86VMS VTOP T0.1-0 and ISSINOHO I64VMS VTOP
T0.1-0 installed; VTOP$ROOT (concealed, SYS$COMMON:[VTOP.]) and VTOP defined in the
system table; VTOP.EXE, VTOP$SETUP.COM, README.VMS and LICENSE. in [VTOP],
VTOP$STARTUP.COM in SYS$STARTUP; `@VTOP$ROOT:[000000]VTOP$SETUP.COM` defines the
command and the installed image says "VTOP 0.1.0". `inst_keys.txt` runs it on a
pseudo-terminal: the screen, the vms panel, help, quit, back at DCL. PCSI notes that it
cannot validate the unsigned kits, as for every family kit. No SYSTARTUP_VMS or SYLOGIN
line was added.
