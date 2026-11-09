$! p_keys.com - build p_keys and PTYDRIVE, then drive p_keys with arrow keys.
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ set default [-]
$ ieee = ""
$ if f$edit(f$getsyi("ARCH_NAME"), "UPCASE") .eqs. "ALPHA" then -
      ieee = "/float=ieee_float/ieee_mode=denorm_results"
$ cc'ieee'/object=[.build]p_keys.obj [.probes]p_keys.c
$ link/executable=[.build]p_keys.exe [.build]p_keys.obj
$ cc'ieee'/standard=c99/pointer_size=32/prefix_library_entries=all_entries/include=[.tests.vms]/object=[.build]ptydrive.obj [.tests.vms]ptydrive.c
$ link/executable=[.build]ptydrive.exe [.build]ptydrive.obj
$ drive := $'f$parse("[.build]ptydrive.exe")'
$ drive [.probes]p_keys.txt
