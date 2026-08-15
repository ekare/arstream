"""Built-in plugin: updates session.frame_count/byte_count/fps and
publishes periodic ("stats") events to the dashboard's WebSocket -- the
same method as the fps calculation in mobile/native/src/ar_capture.cpp
(a measurement from actual elapsed time, not an assumed default)."""

from __future__ import annotations

import time

from ..events import EventBus
from ..session import Session
from . import StreamPlugin

_PUBLISH_EVERY_N_FRAMES = 15


class StatsPlugin(StreamPlugin):
    name = "stats"

    def __init__(self, bus: EventBus) -> None:
        self.bus = bus
        self._start_times: dict[str, float] = {}

    async def on_session_start(self, session: Session) -> None:
        self._start_times[session.id] = time.monotonic()
        await self.bus.publish({"type": "session_start", "session": session.to_dict()})

    async def on_video_config(self, session: Session, config: dict, sps_pps: bytes) -> None:
        session.video_config = config
        session.rotation_degrees = int(config.get("rotation", 0))
        await self.bus.publish({"type": "session_update", "session": session.to_dict()})

    async def on_video_chunk(self, session: Session, timestamp_ns: int, is_keyframe: bool, nal: bytes) -> None:
        session.frame_count += 1
        session.byte_count += len(nal)
        if is_keyframe:
            session.keyframe_count += 1

        if session.frame_count % _PUBLISH_EVERY_N_FRAMES == 0:
            elapsed = time.monotonic() - self._start_times.get(session.id, time.monotonic())
            session.fps = session.frame_count / elapsed if elapsed > 0 else 0.0
            await self.bus.publish({"type": "session_update", "session": session.to_dict()})

    async def on_session_end(self, session: Session) -> None:
        self._start_times.pop(session.id, None)
        await self.bus.publish({"type": "session_end", "session": session.to_dict()})
