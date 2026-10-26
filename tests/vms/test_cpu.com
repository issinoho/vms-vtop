$! test_live.com - VTOP on a pseudo-terminal under PTYDRIVE (cpu_keys.txt).
$! Build first (@build_vms). Output: the transcript, for tools/vtshot.py --ptydrive.
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ write sys$output "==== privileges WORLD ", f$privilege("WORLD")
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries/include=[.tests.vms]/object=[.build]ptydrive.obj [.tests.vms]ptydrive.c
$ link/executable=[.build]ptydrive.exe [.build]ptydrive.obj
$ drive := $'f$parse("[.build]ptydrive.exe")'
$ drive [.tests.vms]cpu_keys.txt
$ exit 1
