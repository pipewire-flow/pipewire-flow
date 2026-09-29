#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Measures what a capture process pulls in while it runs — threads, file
# descriptors, sockets, mapped shared objects, memory and PipeWire clients —
# and prints one Markdown table row per run. The standard matrix then adds
# bench_idle's view of one process before its first stream, while it
# streams, and after its last stream is gone.
#
#   bench/footprint.sh [-B builddir] [-s settle]            run the standard matrix
#   bench/footprint.sh [-s settle] -- <label> <cmd> [args]  measure one command
#
# The command must keep running until SIGINT and print lines ending in
# "received <n> buffers" on exit (bench_tpw/bench_raw do); the smallest
# count goes in the "buffers" column so a run where nothing flowed stands out.
# Needs a running PipeWire daemon plus pw-dump and jq.

set -euo pipefail

builddir=build
settle=3

usage() {
    sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while getopts "B:s:h" opt; do
    case $opt in
    B) builddir=$OPTARG ;;
    s) settle=$OPTARG ;;
    h) usage 0 ;;
    *) usage 1 ;;
    esac
done
shift $((OPTIND - 1))
[ "${1:-}" = "--" ] && shift

for tool in pw-dump jq; do
    command -v "$tool" >/dev/null || { echo "footprint.sh: $tool not found" >&2; exit 1; }
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

header() {
    echo "| run | threads | fds | sockets | .so mapped | PipeWire modules/plugins | RSS KiB | PSS KiB | daemon clients | buffers |"
    echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
}

measure() {
    local label=$1
    shift
    "$@" >"$tmp/out" 2>"$tmp/err" &
    local pid=$!
    sleep "$settle"

    if ! kill -0 "$pid" 2>/dev/null; then
        echo "footprint.sh: '$label' exited early:" >&2
        cat "$tmp/err" >&2
        return 1
    fi

    local threads fds sockets sos plugins rss pss clients
    threads=$(ls "/proc/$pid/task" | wc -l)
    fds=$(ls "/proc/$pid/fd" | wc -l)
    sockets=$(ls -l "/proc/$pid/fd" | grep -c 'socket:' || true)
    grep -o '/[^ ]*\.so[^ ]*' "/proc/$pid/maps" | sort -u >"$tmp/sos"
    sos=$(wc -l <"$tmp/sos")
    plugins=$(grep -c -e '/spa-0\.2/' -e '/pipewire-0\.3/' "$tmp/sos" || true)
    rss=$(awk '/^Rss:/ { print $2 }' "/proc/$pid/smaps_rollup")
    pss=$(awk '/^Pss:/ { print $2 }' "/proc/$pid/smaps_rollup")
    clients=$(pw-dump | jq --argjson p "$pid" \
        '[.[] | select(.type == "PipeWire:Interface:Client")
              | select(.info.props["application.process.id"] == $p)] | length')

    kill -INT "$pid"
    wait "$pid" || true
    local buffers
    buffers=$(awk '/received [0-9]+ buffers/ { n = $(NF - 1); if (min == "" || n < min) min = n }
                   END { print (min == "" ? "?" : min) }' "$tmp/out")

    echo "| $label | $threads | $fds | $sockets | $sos | $plugins | $rss | $pss | $clients | $buffers |"
}

if [ $# -gt 0 ]; then
    header
    measure "$@"
    exit
fi

tpw=$builddir/bench/bench_tpw
raw=$builddir/bench/bench_raw
idle=$builddir/bench/bench_idle
[ -x "$tpw" ] && [ -x "$raw" ] && [ -x "$idle" ] || { echo "footprint.sh: build $builddir first (bench programs missing)" >&2; exit 1; }

header
measure "tpw audio x1" "$tpw" -t audio -n 1
measure "raw audio x1" "$raw" -t audio -n 1
for n in 1 3 6; do
    measure "tpw video x$n" "$tpw" -t video -n "$n"
    measure "raw video x$n (shared)" "$raw" -t video -n "$n"
    if [ "$n" -gt 1 ]; then
        measure "raw video x$n (--separate)" "$raw" -t video -n "$n" --separate
    fi
done

for type in audio video; do
    echo
    "$idle" -t "$type"
done
