"""State shared between the ingest (TCP) server and the web (dashboard)
server -- they run in a single process, in the same asyncio loop (see
cli.py), with no other connection between them."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .events import EventBus
from .plugins import PluginManager
from .session import Session


@dataclass
class AppContext:
    bus: EventBus
    plugin_manager: PluginManager
    captures_dir: Path
    sessions: dict[str, Session] = field(default_factory=dict)
