#!/usr/bin/env bash
# Creates a virtual environment (if missing) and installs arstream-server into it.
# Usage: ./install.sh [--dev]
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
SERVER_DIR="$(dirname "$SCRIPT_DIR")"
VENV_DIR="${ARSTREAM_VENV:-$SERVER_DIR/.venv}"

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment at $VENV_DIR..."
    python3 -m venv "$VENV_DIR"
fi

TARGET="$SERVER_DIR"
if [ "${1:-}" = "--dev" ]; then
    TARGET="$SERVER_DIR[dev]"
fi

"$VENV_DIR/bin/python" -m pip install --upgrade pip
"$VENV_DIR/bin/python" -m pip install -e "$TARGET"

echo ""
echo "Done. Activate the environment with:"
echo "  source $VENV_DIR/bin/activate"
echo "Then run:"
echo "  arstream-server --help"
