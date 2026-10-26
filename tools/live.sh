#!/usr/bin/env bash
# live.sh <node> - upload, build, run VTOP on a pseudo-terminal (tests/vms/test_live.com
# under PTYDRIVE). The transcript is out/live-<node>.txt; see a step's screen with
#   tools/vtshot.py --rows 24 --cols 80 --ptydrive out/live-<node>.txt --step <label> --png x.png
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live.sh <node>}
mkdir -p "$top/out"
"$top/tools/push.sh" "$node" > /dev/null
"$top/tools/vms.sh" "$node" put "$top"/tests/vms/* -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" dcl '@build_vms' > "$top/out/build-$node.log" 2>&1
grep -q 'build: \[.build\]vtop.exe' "$top/out/build-$node.log" || { tail -30 "$top/out/build-$node.log"; exit 1; }
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com > "$top/out/live-$node.txt" 2>&1
grep -E '^---- |DCL-OK|VTOP:' "$top/out/live-$node.txt" | tr -d '\r' | cut -c1-120
