$! qman_cpu.com - the QUEUE_MANAGER process's CPU time, in 10 ms ticks (read-only)
$ ctx = ""
$ loop:
$   pid = f$pid(ctx)
$   if pid .eqs. "" then goto done
$   if f$getjpi(pid,"PRCNAM") .eqs. "QUEUE_MANAGER" then write sys$output "QMAN CPUTIM ", f$getjpi(pid,"CPUTIM")
$   goto loop
$ done:
$ exit 1
