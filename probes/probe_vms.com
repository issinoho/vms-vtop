$! probe_vms.com - build and run probe_vms (Phase 3, read-only)
$ set noon
$ write sys$output "==== ", f$getsyi("NODENAME"), " ", f$getsyi("VERSION"), " ", f$getsyi("ARCH_NAME")
$ write sys$output "==== WORLD ", f$privilege("WORLD"), " OPER ", f$privilege("OPER")
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries probe_vms.c
$ if $severity .ne. 1 then exit 44
$ link probe_vms
$ if $severity .ne. 1 then exit 44
$ run probe_vms
$ exit 1
