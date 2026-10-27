$! test_shot.com - VTOP on a pseudo-terminal under PTYDRIVE for a README picture.
$! P1 is the keys file in [.tests.vms] (banner_keys.txt or vt_keys.txt). Build first.
$ set noon
$ keys = p1
$ if keys .eqs. "" then keys = "vt_keys.txt"
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries/include=[.tests.vms]/object=[.build]ptydrive.obj [.tests.vms]ptydrive.c
$ link/executable=[.build]ptydrive.exe [.build]ptydrive.obj
$ drive := $'f$parse("[.build]ptydrive.exe")'
$ drive [.tests.vms]'keys'
$ exit 1
