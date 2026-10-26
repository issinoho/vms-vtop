$! test_live5.com - the vms panel and the options file under PTYDRIVE (live5_keys.txt).
$! Build first (@build_vms). Uses only its own things: a held job VPTEST_Q of its own
$! in SYS$BATCH (deleted at the end), and VPTEST.INI in this directory through
$! VTOP$OPTIONS, so the user's SYS$LOGIN:VTOP.INI is never touched (D10).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ if f$search("vptest.ini") .nes. "" then delete vptest.ini;*
$ open/write j vptest_q.com
$ write j "$ exit 1"
$ close j
$ submit/hold/noprint/nolog/name=VPTEST_Q/queue=SYS$BATCH vptest_q.com
$ entry = $entry
$ write sys$output "==== held entry ", entry
$ define/job vtop$options 'here'vptest.ini
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries/include=[.tests.vms]/object=[.build]ptydrive.obj [.tests.vms]ptydrive.c
$ link/executable=[.build]ptydrive.exe [.build]ptydrive.obj
$ drive := $'f$parse("[.build]ptydrive.exe")'
$ drive [.tests.vms]live5_keys.txt
$ write sys$output "==== VPTEST.INI as VTOP wrote it"
$ type vptest.ini
$ write sys$output "==== versions ", f$search("vptest.ini")
$ deassign/job vtop$options
$ delete/entry='entry'
$ write sys$output "==== entry ", entry, " deleted ", f$getqui("DISPLAY_ENTRY","JOB_NAME",entry) .eqs. ""
$ delete vptest.ini;*, vptest_q.com;*
$ exit 1
