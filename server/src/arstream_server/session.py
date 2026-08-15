"""Data structure representing the lifecycle of a client (phone) connection.

Plugins (see plugins/__init__.py) receive this object in their event
callbacks -- they keep their own state keyed by session.id, without
touching ingest.py's internals.
"""

from __future__ import annotations

import time
import uuid
from dataclasses import dataclass, field


@dataclass
class Session:
    id: str
    peer_address: str
    started_at: float = field(default_factory=time.time)
    ended_at: float | None = None

    # From VIDEO_CONFIG -- None until the first frame arrives.
    video_config: dict | None = None
    rotation_degrees: int = 0

    # Live stats -- StatsPlugin updates these, the dashboard reads them.
    frame_count: int = 0
    keyframe_count: int = 0
    byte_count: int = 0
    fps: float = 0.0

    @staticmethod
    def new(peer_address: str) -> "Session":
        return Session(id=str(uuid.uuid4()), peer_address=peer_address)

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "peer_address": self.peer_address,
            "started_at": self.started_at,
            "ended_at": self.ended_at,
            "is_live": self.ended_at is None,
            "video_config": self.video_config,
            "rotation_degrees": self.rotation_degrees,
            "frame_count": self.frame_count,
            "keyframe_count": self.keyframe_count,
            "byte_count": self.byte_count,
            "fps": round(self.fps, 1),
        }
