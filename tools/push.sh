#!/usr/bin/env bash
# push.sh <node> - upload the sources, tests and build procedure to the node's work
# directory, keeping the directory layout ([.SRC.CORE] etc.).
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: push.sh <node>}
v=$top/tools/vms.sh
cd "$top"
$v "$node" put build_vms.com LICENSE
for d in src src/core src/main src/plat src/plat/vms tests tests/unit tests/snap tests/vms; do
    [ -d "$d" ] || continue
    files=$(find "$d" -maxdepth 1 -type f | sort)
    # shellcheck disable=SC2086
    $v "$node" put $files -- "$d"
done
echo "push: $node done"
