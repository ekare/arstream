"""`arstream-server` komut satiri girisi -- bkz. server.py."""

from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

from .server import run_server


def main() -> None:
    parser = argparse.ArgumentParser(prog="arstream-server", description="arstream referans alicisi (docs/PROTOCOL.md)")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--out", type=Path, default=None, help="Alinan video/IMU'nun yazilacagi dizin (verilmezse diske yazilmaz)")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    try:
        asyncio.run(run_server(args.host, args.port, args.out))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
