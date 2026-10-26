$! probes.com - build and run the Phase 0 probes (bounded and read-only).
$! P1 = seconds between the two disk samples (default 5).
$! probe_glyph runs on a pseudo-terminal under PTYDRIVE (probe_glyph_keys.txt).
$ set noon
$ secs = p1
$ if secs .eqs. "" then secs = "5"
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES"
$ build: subroutine
$   cc'cflags' 'p1'.c
$   if $severity .ne. 1 then exit 44
$   link 'p1'
$   if $severity .ne. 1 then exit 44
$ endsubroutine
$ write sys$output "==== ", f$getsyi("NODENAME"), " ", f$getsyi("VERSION"), " ", f$getsyi("ARCH_NAME")
$ write sys$output "==== WORLD ", f$privilege("WORLD"), " GROUP ", f$privilege("GROUP")
$ call build probe_jpi
$ jpi_ok = $status
$ call build probe_dvi
$ dvi_ok = $status
$ call build probe_glyph
$ glyph_ok = $status
$ cc'cflags'/include=[] ptydrive.c
$ link ptydrive
$ write sys$output "==== probe_jpi"
$ if jpi_ok then run probe_jpi
$ write sys$output "==== SHOW SYSTEM, to compare"
$ show system
$ write sys$output "==== probe_dvi ", secs
$ dvi := $'f$environment("DEFAULT")'probe_dvi.exe
$ if dvi_ok then dvi 'secs'
$ write sys$output "==== MONITOR DISK, to compare"
$ monitor disk/item=operation_rate/interval='secs'/ending="+0:00:''secs'"/nodisplay/summary=sys$output
$ write sys$output "==== probe_glyph under ptydrive"
$ drive := $'f$environment("DEFAULT")'ptydrive.exe
$ if glyph_ok then drive probe_glyph_keys.txt
$ exit 1
