"""Plugin system -- anything that processes incoming stream data (recording,
stats, future frame preview/analysis) subclasses StreamPlugin. Built-in and
externally loaded plugins use the SAME interface, no special code path.

To write a new plugin: server/README.md#writing-a-plugin.
"""

from __future__ import annotations

import importlib.util
import inspect
import logging
from pathlib import Path

from .. import protocol
from ..session import Session

logger = logging.getLogger("arstream_server.plugins")


class StreamPlugin:
    """Base class -- don't override the callbacks you don't care about,
    the default bodies are no-ops."""

    name: str = "plugin"

    async def on_session_start(self, session: Session) -> None:
        pass

    async def on_video_config(self, session: Session, config: dict, sps_pps: bytes) -> None:
        pass

    async def on_video_chunk(self, session: Session, timestamp_ns: int, is_keyframe: bool, nal: bytes) -> None:
        pass

    async def on_imu_batch(self, session: Session, samples: list[protocol.ImuSample]) -> None:
        pass

    async def on_pose_sample(
        self,
        session: Session,
        timestamp_ns: int,
        tracking_state: int,
        x: float,
        y: float,
        z: float,
        qx: float,
        qy: float,
        qz: float,
        qw: float,
    ) -> None:
        pass

    async def on_point_cloud(self, session: Session, timestamp_ns: int, points: list[protocol.Point]) -> None:
        pass

    async def on_camera_intrinsics(
        self, session: Session, fx: float, fy: float, cx: float, cy: float, width: int, height: int
    ) -> None:
        pass

    async def on_session_end(self, session: Session) -> None:
        pass


class PluginManager:
    def __init__(self, plugins: list[StreamPlugin]) -> None:
        self._plugins = plugins

    @property
    def plugins(self) -> list[StreamPlugin]:
        return list(self._plugins)

    def add(self, plugin: StreamPlugin) -> None:
        self._plugins.append(plugin)

    async def dispatch_session_start(self, session: Session) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_session_start(session))

    async def dispatch_video_config(self, session: Session, config: dict, sps_pps: bytes) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_video_config(session, config, sps_pps))

    async def dispatch_video_chunk(self, session: Session, timestamp_ns: int, is_keyframe: bool, nal: bytes) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_video_chunk(session, timestamp_ns, is_keyframe, nal))

    async def dispatch_imu_batch(self, session: Session, samples: list[protocol.ImuSample]) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_imu_batch(session, samples))

    async def dispatch_pose_sample(
        self,
        session: Session,
        timestamp_ns: int,
        tracking_state: int,
        x: float,
        y: float,
        z: float,
        qx: float,
        qy: float,
        qz: float,
        qw: float,
    ) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_pose_sample(session, timestamp_ns, tracking_state, x, y, z, qx, qy, qz, qw))

    async def dispatch_point_cloud(self, session: Session, timestamp_ns: int, points: list[protocol.Point]) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_point_cloud(session, timestamp_ns, points))

    async def dispatch_camera_intrinsics(
        self, session: Session, fx: float, fy: float, cx: float, cy: float, width: int, height: int
    ) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_camera_intrinsics(session, fx, fy, cx, cy, width, height))

    async def dispatch_session_end(self, session: Session) -> None:
        for p in self._plugins:
            await self._call_safely(p, p.on_session_end(session))

    @staticmethod
    async def _call_safely(plugin: StreamPlugin, coro) -> None:
        # A plugin's error must not stop the others or the ingest itself --
        # it's logged, and the stream continues.
        try:
            await coro
        except Exception:
            logger.exception("plugin '%s' raised in a callback", plugin.name)


def load_plugins_from_dir(directory: Path) -> list[StreamPlugin]:
    """Imports every .py file in directory, finds StreamPlugin subclasses
    inside, and instantiates one of each. There's no file-name/class-name
    convention -- it just has to derive from StreamPlugin."""
    plugins: list[StreamPlugin] = []
    if not directory.is_dir():
        return plugins

    for py_file in sorted(directory.glob("*.py")):
        if py_file.name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(f"arstream_external_plugin_{py_file.stem}", py_file)
        if spec is None or spec.loader is None:
            continue
        module = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(module)
        except Exception:
            logger.exception("could not load external plugin: %s", py_file)
            continue
        for _, obj in inspect.getmembers(module, inspect.isclass):
            if issubclass(obj, StreamPlugin) and obj is not StreamPlugin:
                plugins.append(obj())
                logger.info("external plugin loaded: %s (%s)", obj.__name__, py_file.name)
    return plugins
