# Phase 0 probes

Bounded and read-only. Results are in `docs/NOTES.md` N1–N5.

    tools/vms.sh x86 put probes/*.c probes/*.h probes/probe_glyph_keys.txt
    tools/vms.sh x86 run probes/probes.com [seconds]

- `probe_jpi.c`: wildcard `$GETJPI`, cost per pass, with and without the image name,
  and with WORLD and GROUP off (N1–N3).
- `probe_dvi.c`: `DVI$_OPCNT` per disk, two samples apart, against MONITOR DISK (N4).
- `probe_glyph.c`: braille, block, DEC graphics and colour through the terminal
  driver, run on a pseudo-terminal by `ptydrive.c` (from vms-vtree) with
  `probe_glyph_keys.txt` (N5).
