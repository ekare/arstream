"""Built-in plugin: records each session under captures/<session_id>/ --
video.h264 (Annex-B, SPS/PPS + frames in order) + meta.json (config,
rotation, timestamps, latest stats). The web layer zips this folder up for
download (see web/app.py)."""

from __future__ import annotations

import json
from pathlib import Path

from ..session import Session
from . import StreamPlugin


class RecorderPlugin(StreamPlugin):
    name = "recorder"

    def __init__(self, captures_dir: Path) -> None:
        self.captures_dir = captures_dir
        self._files: dict[str, object] = {}

    def session_dir(self, session_id: str) -> Path:
        return self.captures_dir / session_id

    async def on_session_start(self, session: Session) -> None:
        d = self.session_dir(session.id)
        d.mkdir(parents=True, exist_ok=True)
        self._files[session.id] = open(d / "video.h264", "wb")
        self._write_meta(session)

    async def on_video_config(self, session: Session, config: dict, sps_pps: bytes) -> None:
        f = self._files.get(session.id)
        if f is not None:
            f.write(sps_pps)
        self._write_meta(session)

    async def on_video_chunk(self, session: Session, timestamp_ns: int, is_keyframe: bool, nal: bytes) -> None:
        f = self._files.get(session.id)
        if f is not None:
            f.write(nal)

    async def on_session_end(self, session: Session) -> None:
        f = self._files.pop(session.id, None)
        if f is not None:
            f.close()
        self._write_meta(session)

    def _write_meta(self, session: Session) -> None:
        meta_path = self.session_dir(session.id) / "meta.json"
        meta_path.write_text(json.dumps(session.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8")
