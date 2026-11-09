$! test_live4.com - the process list and its actions under PTYDRIVE (live4_keys.txt).
$! Build first (@build_vms). Output: the transcript, for tools/vtshot.py --ptydrive.
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ write sys$output "==== privileges WORLD ", f$privilege("WORLD")
$! Alpha's default is G_FLOAT; PTYDRIVE passes LIB$WAIT an IEEE single.
$ ieee = ""
$ if f$edit(f$getsyi("ARCH_NAME"), "UPCASE") .eqs. "ALPHA" then -
      ieee = "/float=ieee_float/ieee_mode=denorm_results"
$ cc'ieee'/standard=c99/pointer_size=32/prefix_library_entries=all_entries/include=[.tests.vms]/object=[.build]ptydrive.obj [.tests.vms]ptydrive.c
$ link/executable=[.build]ptydrive.exe [.build]ptydrive.obj
$ drive := $'f$parse("[.build]ptydrive.exe")'
$ drive [.tests.vms]live4_keys.txt
$ exit 1
