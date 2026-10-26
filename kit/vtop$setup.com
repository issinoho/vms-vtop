$! VTOP$SETUP.COM - define the VTOP command for a user
$!
$! Add to LOGIN.COM (or SYS$MANAGER:SYLOGIN.COM for everyone):
$!     $ @VTOP$ROOT:[000000]VTOP$SETUP.COM
$!
$ if f$trnlnm("VTOP$ROOT") .eqs. ""
$ then
$   write sys$error "VTOP$SETUP: VTOP$ROOT is not defined; run VTOP$STARTUP.COM first"
$   exit 44
$ endif
$ vtop :== $VTOP$ROOT:[000000]VTOP.EXE
$ exit 1
