$! vptest_show.com - state, current and base priority of the test's VPTEST1 (live4).
$ ctx = ""
$ found = 0
$loop:
$ p = f$pid(ctx)
$ if p .eqs. "" then goto done
$ if f$getjpi(p, "PRCNAM") .nes. "VPTEST1" then goto loop
$ found = 1
$ write sys$output "VPTEST1 ", p, " state ", f$getjpi(p, "STATE"), " pri ", -
    f$getjpi(p, "PRI"), " prib ", f$getjpi(p, "PRIB")
$ goto loop
$done:
$ if .not. found then write sys$output "VPTEST1 none"
$ exit 1
