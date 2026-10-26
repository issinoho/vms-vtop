$! pcsi_install.com - install the VTOP PCSI kit on this node and verify it.
$! First installation. Changes nothing else: no SYSTARTUP_VMS or SYLOGIN line, no
$! symbols for other users. Run from the work directory after tools/kit.sh.
$! Approved by the user 2026-10-26 ("install on both", "keep it installed").
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM,CMKRNL,SYSLCK,BYPASS,WORLD")
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ kitdir = f$environment("DEFAULT") - "]" + ".KIT_''arch']"
$ write sys$output "=== already installed?"
$ product show product VTOP /producer=ISSINOHO
$ write sys$output "=== INSTALL from ", kitdir
$ product install VTOP /producer=ISSINOHO /base_system='base' /source='kitdir' /options=noconfirm /log
$ write sys$output "=== install status ", $status
$ dummy = f$setprv(old_privs)
$ product show product VTOP /producer=ISSINOHO
$ write sys$output "=== VERIFY"
$ show logical/system VTOP*
$ directory/protection/owner/size VTOP:,SYS$STARTUP:VTOP$STARTUP.COM
$ @VTOP$ROOT:[000000]VTOP$SETUP.COM
$ show symbol vtop
$ vtop /version
$ write sys$output "=== PCSI-INSTALL-DONE"
$ exit 1
