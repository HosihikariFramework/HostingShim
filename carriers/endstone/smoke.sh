#!/usr/bin/env bash
# Live smoke test for the Endstone carrier on Linux.
#
#   ./smoke.sh <folder containing bedrock_server>
#
# Needs a BDS folder with Endstone installed, and the framework package already
# installed with lip (or otherwise laid out under plugins/). It starts the
# server through the Endstone CLI, asks for the plugin list, stops the server,
# and checks that the CLR came up and the carrier took over.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <folder containing bedrock_server>" >&2
    exit 1
fi

server=$(cd "$1" && pwd)
endstone=${ENDSTONE_EXE:-}
if [ -z "$endstone" ]; then
    endstone=$(command -v endstone || true)
fi
if [ -z "$endstone" ] || [ ! -x "$endstone" ]; then
    echo "endstone CLI not found; set ENDSTONE_EXE or put endstone on PATH" >&2
    exit 1
fi

if pgrep -x bedrock_server > /dev/null; then
    echo "a server is already running; stop it first" >&2
    exit 1
fi

log="$server/smoke-linux.log"

# BDS reads commands from stdin. sleep is the portable pause here.
(
    sleep 60
    echo plugins
    sleep 8
    echo stop
) | timeout 240s "$endstone" -s "$server" -y > "$log" 2>&1 || true

fail() {
    echo "FAIL  $1"
    echo "see $log"
    tail -n 120 "$log" || true
    exit 1
}

grep -q "正在把 CLR 托管进 BDS 进程" "$log" || fail "carrier did not load the CLR"
grep -q "Hosihikari: 已就绪" "$log" || fail "core did not report ready"
grep -q "已接管" "$log" || fail "carrier did not take over the plugin set"
grep -q "Enabling hosihikari" "$log" || fail "Endstone did not enable the carrier"
grep -qE "Server stop requested|Stopping server" "$log" || fail "server did not stop cleanly"

if grep -qE "Unhandled exception|Segmentation fault" "$log"; then
    fail "the server died"
fi

echo "PASS  Linux carrier live smoke; log at $log"
