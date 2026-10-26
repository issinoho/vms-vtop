#!/usr/bin/env bash
# vms.sh - run things on the VMS build nodes listed in tools/nodes.conf.
#
#   vms.sh <node> put    <local-file>... [-- <subdir>]  upload files (to workdir or workdir/subdir)
#   vms.sh <node> putdir <local-dir> <subdir>           upload every file in a directory (flat)
#   vms.sh <node> get    <remote-relpath> <local-file>  download a file relative to the workdir
#   vms.sh <node> dcl    '<dcl command>'                run one DCL command, print its output
#   vms.sh <node> run    <local.com> [params...]        upload + run a procedure interactively
#   vms.sh <node> batch  <local.com> [params...]        upload + SUBMIT, wait for the log, print it
#
# Output from `ssh host <cmd>` is unreliable on these nodes, so everything runs
# with /OUTPUT (or a batch log) into the workdir and the log is fetched by sftp.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
conf=$here/nodes.conf
key=${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}

usage() { sed -n '2,12p' "$0" >&2; exit 2; }
[ $# -ge 2 ] || usage
node=$1 op=$2; shift 2

read -r _ ARCH HOST PORT USER WORKDIR SFTPDIR < <(awk -v n="$node" '$1==n' "$conf") || true
[ -n "${HOST:-}" ] || { echo "vms.sh: unknown node '$node' (see $conf)" >&2; exit 2; }

ssh_opts=(-i "$key" -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=30)

sftp_batch() {  # sftp_batch <commands...>
    printf '%s\n' "$@" | sftp -P "$PORT" "${ssh_opts[@]}" -b - "$USER@$HOST" 2>&1 >/dev/null |
        { grep -vE '^ *Welcome to|^ *$' >&2 || true; }
    return "${PIPESTATUS[1]}"
}

# DISK$USER:[USERNAME.VMS_STEWARD] + sub/dir -> DISK$USER:[USERNAME.VMS_STEWARD.SUB.DIR]
vms_dir() {
    local sub=${1:-}
    if [ -z "$sub" ]; then echo "$WORKDIR"; return; fi
    echo "${WORKDIR%]}.$(echo "$sub" | tr '/' '.' | tr '[:lower:]' '[:upper:]')]"
}

# Run DCL command(s) with output captured to a log, then print the log.
# The procedure writes <tag>.DONE as its last act; the host waits for that
# marker rather than for ssh to exit, because the ssh session sometimes stays
# open after the VMS process has finished.
dcl_logged() {
    local tag=vmsrun_$$_$RANDOM
    local com=$(mktemp) out=$(mktemp) done_f=$(mktemp) sshlog=$(mktemp)
    { echo '$ set noon'; echo "\$ set default $WORKDIR"; printf '%s\n' "$@"
      echo '$ write sys$output "VMSRUN-END"'
      echo "\$ open/write vmsrun_done ${WORKDIR}${tag}.DONE"
      echo '$ close vmsrun_done'; } > "$com"
    sftp_batch "cd $SFTPDIR" "put $com $tag.com"
    # ssh itself in the background, so that $pid is ssh's and the kill below ends it.
    # (It was a { ssh; echo; } group: the kill ended the subshell and left ssh running,
    # still connected, holding an SSHD process on the node: 45 orphans by 2026-10-17.)
    # The node's sshd does not always close the session when the command ends, so ssh
    # is always ended here, also if vms.sh itself is ended (timeout, Ctrl-C).
    start_ssh() {
        ssh -p "$PORT" "${ssh_opts[@]}" "$USER@$HOST" \
            "@${WORKDIR}${tag}.COM/OUTPUT=${WORKDIR}${tag}.LOG" </dev/null >"$sshlog" 2>&1 &
        pid=$!
    }
    local pid waited=0 limit=${VMS_TIMEOUT:-600} starts=1
    trap 'kill "${pid:-}" 2>/dev/null || true' EXIT
    trap 'kill "${pid:-}" 2>/dev/null; exit 143' TERM INT HUP
    start_ssh
    while :; do
        sleep 2; waited=$((waited + 2))
        if ! kill -0 "$pid" 2>/dev/null || [ $((waited % 10)) -eq 0 ]; then
            sftp_batch "cd $SFTPDIR" "get $tag.DONE $done_f" 2>/dev/null && break
            if ! kill -0 "$pid" 2>/dev/null; then
                sleep 3
                sftp_batch "cd $SFTPDIR" "get $tag.DONE $done_f" 2>/dev/null && break
                # ssh ended with no marker. If the procedure left no log it never ran
                # (seen on IA64, 2026-10-16: ssh gone in 2 s, only the .com there), so
                # starting it again cannot run anything twice.
                if [ "$starts" -lt 3 ] && ! sftp_batch "cd $SFTPDIR" "get $tag.LOG $out" 2>/dev/null; then
                    echo "vms.sh: $tag did not start; ssh said: $(tr -d '\r' < "$sshlog" | grep -avE '^ *Welcome to|^ *$' | head -2 | tr '\n' ' ')- starting it again" >&2
                    starts=$((starts + 1)); sleep 2; start_ssh; continue
                fi
                break
            fi
        fi
        [ "$waited" -ge "$limit" ] && { echo "vms.sh: timed out after ${limit}s" >&2; break; }
    done
    # The log is closed only when the process exits, after the DONE marker:
    # fetch until it holds the end line written just before the marker.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        sftp_batch "cd $SFTPDIR" "get $tag.LOG $out" 2>/dev/null || true
        grep -aq '^VMSRUN-END' "$out" 2>/dev/null && break
        sleep 3
    done
    kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true
    sftp_batch "cd $SFTPDIR" "-rm $tag.com" "-rm $tag.LOG" "-rm $tag.DONE" || true
    # No end line: the procedure never ran, or never finished. Say so, with what ssh
    # said, rather than return an empty result the caller takes for an answer.
    grep -aq '^VMSRUN-END' "$out" 2>/dev/null ||
        { echo "vms.sh: no complete output from $tag; ssh said:" >&2
          tr -d '\r' < "$sshlog" | grep -avE '^ *Welcome to|^ *$' | head -5 >&2; }
    tr -d '\r' < "$out" | grep -av '^VMSRUN-END' || true
    rm -f "$com" "$out" "$done_f" "$sshlog"
}

case $op in
put)
    files=() sub=
    while [ $# -gt 0 ]; do
        if [ "$1" = -- ]; then sub=$2; break; fi
        files+=("$1"); shift
    done
    cmds=("cd $SFTPDIR")
    [ -n "$sub" ] && cmds+=("-mkdir $sub" "cd $sub")
    for f in "${files[@]}"; do cmds+=("put $f"); done
    sftp_batch "${cmds[@]}"
    ;;
putdir)
    [ $# -eq 2 ] || usage
    sftp_batch "cd $SFTPDIR" "-mkdir $2" "cd $2" "lcd $1" "mput *"
    ;;
get)
    [ $# -eq 2 ] || usage
    sftp_batch "cd $SFTPDIR" "get $1 $2"
    ;;
dcl)
    [ $# -ge 1 ] || usage
    cmds=()
    for c in "$@"; do cmds+=("\$ $c"); done
    dcl_logged "${cmds[@]}"
    ;;
run)
    [ $# -ge 1 ] || usage
    com=$1; shift
    sftp_batch "cd $SFTPDIR" "put $com"
    params=""
    for p in "$@"; do params+=" \"$p\""; done
    dcl_logged "\$ @${WORKDIR}$(basename "$com")$params"
    ;;
batch)
    [ $# -ge 1 ] || usage
    com=$1; shift
    name=$(basename "$com" .com)
    log=${name}_batch.log
    sftp_batch "cd $SFTPDIR" "put $com" "-rm $log"
    plist=""
    for p in "$@"; do plist+="${plist:+,}\"$p\""; done
    dcl_logged "\$ submit/noprint/log_file=${WORKDIR}${log}${plist:+/parameters=($plist)} ${WORKDIR}$(basename "$com")" >&2
    # The log is held open by the batch job until it ends; sftp cannot read it until then.
    out=$(mktemp)
    for _ in $(seq 1 ${VMS_BATCH_POLLS:-360}); do
        if sftp_batch "cd $SFTPDIR" "get $log $out" 2>/dev/null && [ -s "$out" ]; then
            tr -d '\r' < "$out"; rm -f "$out"; exit 0
        fi
        sleep ${VMS_BATCH_POLL_SECS:-20}
    done
    echo "vms.sh: timed out waiting for batch log $log" >&2; rm -f "$out"; exit 1
    ;;
*) usage ;;
esac
