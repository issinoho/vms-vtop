$! build_vms.com - build VTOP on OpenVMS (x86-64 or IA64), always from clean.
$!   @build_vms          build [.build]vtop.exe
$!   @build_vms TEST     also build and run the unit and snapshot tests (TEST-OK)
$! Any compiler warning stops the build (as -Werror does on Linux).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps the rest.
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" -
         + "/INCLUDE=([.src.core],[.src.plat])"
$ core = "vp_util,vp_screen,vp_theme,vp_term,vp_model,vp_graph,vp_procs,vp_app"
$ i = 0
$ core_loop:
$   m = f$element(i, ",", core)
$   if m .eqs. "," then goto core_done
$   cc'cflags'/object=[.build]'m'.obj [.src.core]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto core_loop
$ core_done:
$! NMADEF (the LAN counter layout) comes from SYS$LIB_C.TLB
$ cc'cflags'/object=[.build]vp_plat_vms.obj [.src.plat.vms]vp_plat_vms.c+SYS$LIBRARY:SYS$LIB_C/LIBRARY
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vp_main.obj [.src.main]vp_main.c
$ if $severity .ne. 1 then goto failed
$ set default [.build]
$ link/executable=vtop.exe vp_main.obj,vp_plat_vms.obj,'core'
$ link_sev = $severity
$ set default [-]
$ if link_sev .ne. 1 then goto failed
$ write sys$output "build: [.build]vtop.exe"
$ if p1 .nes. "TEST" then exit 1
$!
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_app.obj [.tests.unit]test_app.c
$ if $severity .ne. 1 then goto failed
$ set default [.build]
$ link/executable=test_core.exe test_core.obj,'core'
$ link_sev = $severity
$ link/executable=test_app.exe test_app.obj,'core'
$ if $severity .ne. 1 then link_sev = $severity
$ set default [-]
$ if link_sev .ne. 1 then goto failed
$ run [.build]test_core
$ if .not. $status then goto failed
$! the snapshots are read relative to the default directory (tests/snap)
$ run [.build]test_app
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$ failed:
$ write sys$output "BUILD-FAILED"
$ exit 44
