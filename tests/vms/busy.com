$! busy.com - keep one CPU busy at priority 1 for P1 seconds (default 50), then end.
$! For the README banner only: a subprocess of the test's own, which ends by itself.
$ set noon
$ set process/priority=1
$ secs = f$integer(p1)
$ if secs .le. 0 then secs = 50
$ mins = secs / 60
$ secs = secs - mins * 60
$ stop_at = f$cvtime("+0:''mins':''secs'",,"TIME")       ! delta seconds must be < 60
$ loop:
$   n = 0
$ inner:
$   n = n + 1
$   if n .lt. 2000 then goto inner
$   if f$cvtime(,,"TIME") .lts. stop_at then goto loop
$ exit 1
