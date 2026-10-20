#!/usr/bin/env bash
# Live smoke test for the self-hosted Linux carrier.
#
#   ./smoke.sh <folder containing bedrock_server and hosihikari-server>
#
# Starts through the launcher, asks the server to stop, and checks that the CLR
# came up and the core scanned plugins/.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <folder containing bedrock_server and hosihikari-server>" >&2
    exit 1
fi

server=$(cd "$1" && pwd)
launcher="$server/hosihikari-server"
if [ ! -x "$launcher" ]; then
    echo "hosihikari-server is missing or not executable in $server" >&2
    exit 1
fi

if command -v pgrep > /dev/null 2>&1 && pgrep -x bedrock_server > /dev/null; then
    echo "a server is already running; stop it first" >&2
    exit 1
fi

log="$server/smoke-selfhosted.log"
wait_before=${HOSIHIKARI_SMOKE_DELAY:-60}
rm -f "$log"

set +e
(
    sleep "$wait_before"
    echo stop
) | timeout 240s "$launcher" > "$log" 2>&1
server_status=$?
set -e

fail() {
    echo "FAIL  $1"
    echo "see $log"
    tail -n 120 "$log" || true
    exit 1
}

[ "$server_status" -eq 0 ] || fail "server exited with status $server_status"
grep -q "Server started" "$log" || fail "server did not start"
grep -q "已加载" "$log" || fail "core did not scan plugins/"
grep -q "启动失败" "$log" && fail "core reported a startup failure"
grep -qE "Server stop requested|Stopping server" "$log" || fail "server did not stop cleanly"

if grep -qE "Unhandled exception|Segmentation fault" "$log"; then
    fail "the server died"
fi

echo "PASS  self-hosted Linux live smoke; log at $log"
