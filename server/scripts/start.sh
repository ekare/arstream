#!/usr/bin/env bash
# Starts arstream-server in the background and records its PID.
# Prefers the venv created by install.sh, falls back to PATH.
# Usage: ./start.sh [extra arstream-server args...]
# Env overrides: ARSTREAM_VENV, ARSTREAM_PID_FILE, ARSTREAM_LOG_FILE
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
SERVER_DIR="$(dirname "$SCRIPT_DIR")"
VENV_DIR="${ARSTREAM_VENV:-$SERVER_DIR/.venv}"
PID_FILE="${ARSTREAM_PID_FILE:-$SERVER_DIR/arstream-server.pid}"
LOG_FILE="${ARSTREAM_LOG_FILE:-$SERVER_DIR/arstream-server.log}"

if [ -x "$VENV_DIR/bin/arstream-server" ]; then
    BIN="$VENV_DIR/bin/arstream-server"
else
    BIN="arstream-server"
fi

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "arstream-server is already running (PID $(cat "$PID_FILE"))." >&2
    exit 1
fi

cd "$SERVER_DIR"
nohup "$BIN" "$@" > "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"
sleep 1

if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "arstream-server started (PID $(cat "$PID_FILE")). Logs: $LOG_FILE"
else
    echo "arstream-server failed to start -- check $LOG_FILE" >&2
    rm -f "$PID_FILE"
    exit 1
fi
