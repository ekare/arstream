#!/usr/bin/env bash
# Stops the arstream-server instance started by start.sh.
# Env overrides: ARSTREAM_PID_FILE
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
SERVER_DIR="$(dirname "$SCRIPT_DIR")"
PID_FILE="${ARSTREAM_PID_FILE:-$SERVER_DIR/arstream-server.pid}"

if [ ! -f "$PID_FILE" ]; then
    echo "No PID file found ($PID_FILE) -- is arstream-server running?" >&2
    exit 1
fi

PID="$(cat "$PID_FILE")"
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Process $PID is not running; removing stale PID file." >&2
    rm -f "$PID_FILE"
    exit 1
fi

kill "$PID"
for _ in $(seq 1 20); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.5
done

if kill -0 "$PID" 2>/dev/null; then
    echo "arstream-server (PID $PID) did not stop in time, sending SIGKILL." >&2
    kill -9 "$PID"
fi

rm -f "$PID_FILE"
echo "arstream-server stopped."
