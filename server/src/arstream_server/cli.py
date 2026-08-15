"""`arstream-server` command-line entry point.

A single command brings up two things at once, in the same asyncio loop:
  1. The TCP ingest server (docs/PROTOCOL.md, listens for the client in mobile/)
  2. The web dashboard (FastAPI/uvicorn) -- live stats + recording download

The only link between them is AppContext (see context.py): ingest.py
dispatches events to the plugins, plugins (stats.py) publish to EventBus,
and the dashboard's WebSocket listens to that feed. Both read/write the
same `ctx.sessions` dict.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

import uvicorn

from .context import AppContext
from .events import EventBus
from .ingest import run_ingest_server
from .plugins import PluginManager, load_plugins_from_dir
from .plugins.recorder import RecorderPlugin
from .plugins.stats import StatsPlugin
from .web.app import create_app

logger = logging.getLogger("arstream_server.cli")


async def _run(args: argparse.Namespace) -> None:
    captures_dir = args.out
    captures_dir.mkdir(parents=True, exist_ok=True)

    bus = EventBus()
    plugins = [RecorderPlugin(captures_dir), StatsPlugin(bus)]
    if args.plugins_dir:
        plugins.extend(load_plugins_from_dir(args.plugins_dir))
    plugin_manager = PluginManager(plugins)
    logger.info("active plugins: %s", ", ".join(p.name for p in plugin_manager.plugins))

    ctx = AppContext(bus=bus, plugin_manager=plugin_manager, captures_dir=captures_dir)

    ingest_server = await run_ingest_server(args.host, args.port, ctx)

    web_app = create_app(ctx)
    uvicorn_config = uvicorn.Config(web_app, host=args.web_host, port=args.web_port, log_level="warning")
    uvicorn_server = uvicorn.Server(uvicorn_config)
    logger.info("dashboard: http://%s:%d  (API documentation: /docs)", args.web_host, args.web_port)

    async with ingest_server:
        await asyncio.gather(ingest_server.serve_forever(), uvicorn_server.serve())


def main() -> None:
    parser = argparse.ArgumentParser(prog="arstream-server", description="arstream reference receiver + dashboard (docs/PROTOCOL.md)")
    parser.add_argument("--host", default="0.0.0.0", help="Address the TCP ingest server listens on")
    parser.add_argument("--port", type=int, default=9999, help="Port for the TCP ingest server (the phone connects here)")
    parser.add_argument("--web-host", default="0.0.0.0", help="Address the dashboard listens on")
    parser.add_argument("--web-port", type=int, default=8080, help="Dashboard port (open this from a browser)")
    parser.add_argument("--out", type=Path, default=Path("./captures"), help="Directory recordings are written to")
    parser.add_argument("--plugins-dir", type=Path, default=None, help="Directory containing additional (external) plugins -- see README#writing-a-plugin")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    try:
        asyncio.run(_run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
