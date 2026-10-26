$! VTOP$STARTUP.COM - system startup for VTOP (a btop-style system monitor)
$!
$! Installed into SYS$STARTUP. Defines the system logical names VTOP$ROOT
$! (the installed [VTOP] directory) and VTOP (its top level, where
$! VTOP.EXE is). To run it at every boot, add it to
$! SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:VTOP$STARTUP.COM
$!
$! P1 = "INSTALL": also print the post-installation tasks (PCSI runs it so).
$! P1 = "REMOVE":  deassign the logical names instead (PCSI runs it so at removal).
$!
$! Users then need the command: in SYS$MANAGER:SYLOGIN.COM or their LOGIN.COM,
$!     $ @VTOP$ROOT:[000000]VTOP$SETUP.COM
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("VTOP", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VTOP
$   if f$trnlnm("VTOP$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VTOP$ROOT
$   exit 1
$ endif
$!
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[VTOP]. Rooted logicals need the physical form (as VTREE$STARTUP).
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "VTOP.]"
$ if root .eqs. dir + "VTOP.]"
$ then
$   write sys$error "VTOP$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ define/system/executive_mode/translation_attributes=concealed VTOP$ROOT 'dev''root'
$ define/system/executive_mode VTOP VTOP$ROOT:[000000]
$ if f$search("VTOP:VTOP.EXE") .eqs. ""
$ then
$   write sys$error "VTOP$STARTUP: VTOP.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for VTOP"
$ say ""
$ say "    At system startup: to define VTOP$ROOT and VTOP at every boot, add to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:VTOP$STARTUP.COM"
$ say "    For users: the VTOP command, in SYS$MANAGER:SYLOGIN.COM or their LOGIN.COM:"
$ say "    $ @VTOP$ROOT:[000000]VTOP$SETUP.COM"
$ say ""
$ say "    VTOP enables no privilege: it shows the processes your privileges let you"
$ say "    see (WORLD for all), and its actions are checked by VMS as if you used DCL."
$ say "    PRODUCT REMOVE VTOP removes the product and deassigns the logical names."
$ say ""
$ exit 1
