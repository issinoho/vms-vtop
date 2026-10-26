$! t_sample.com - build and run t_sample, then MONITOR MODES and DISK over the next seconds.
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/INCLUDE=([.src.core],[.src.plat])"
$ cc'cflags'/object=[.build]t_sample.obj [.tests.vms]t_sample.c
$ set default [.build]
$ link/executable=t_sample.exe t_sample.obj,vp_plat_vms.obj,vp_model.obj,vp_util.obj,vp_screen.obj,vp_term.obj,vp_theme.obj
$ set default [-]
$ ts := $'f$parse("[.build]t_sample.exe")'
$ ts 3
$ monitor modes/interval=3/ending="+0:00:04"/nodisplay/summary=sys$output
$ ts 3
$ exit 1
