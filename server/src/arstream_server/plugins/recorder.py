"""Built-in plugin: records each session under captures/<session_id>/ --
video.h264 (Annex-B, SPS/PPS + frames in order) + meta.json (config,
rotation, timestamps, latest stats), plus one JSON-lines file per data
stream (frames.jsonl, imu.jsonl, poses.jsonl, points.jsonl) and a
single-snapshot intrinsics.json. All of these share the same device-clock
timestamp domain as video.h264's frames -- there's no muxing into the H264
bitstream (see docs/PROTOCOL.md §0); frames.jsonl is what lets a consumer
correlate a video.h264 byte range with a timestamp. The web layer zips the
whole folder up for download (see web/app.py)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import TextIO

from .. import protocol
from ..session import Session
from . import StreamPlugin


class RecorderPlugin(StreamPlugin):
    name = "recorder"

    def __init__(self, captures_dir: Path) -> None:
        self.captures_dir = captures_dir
        self._files: dict[str, object] = {}
        self._frames_files: dict[str, TextIO] = {}
        self._imu_files: dict[str, TextIO] = {}
        self._poses_files: dict[str, TextIO] = {}
        self._points_files: dict[str, TextIO] = {}
        # Running byte offset into video.h264 -- advanced by both VIDEO_CONFIG
        # (SPS/PPS, rewritten on every reconnect) and VIDEO_CHUNK, so
        # frames.jsonl's byte_offset always matches the real file position.
        self._video_offset: dict[str, int] = {}

    def session_dir(self, session_id: str) -> Path:
        return self.captures_dir / session_id

    async def on_session_start(self, session: Session) -> None:
        d = self.session_dir(session.id)
        d.mkdir(parents=True, exist_ok=True)
        self._files[session.id] = open(d / "video.h264", "wb")
        self._frames_files[session.id] = open(d / "frames.jsonl", "w", encoding="utf-8")
        self._imu_files[session.id] = open(d / "imu.jsonl", "w", encoding="utf-8")
        self._poses_files[session.id] = open(d / "poses.jsonl", "w", encoding="utf-8")
        self._points_files[session.id] = open(d / "points.jsonl", "w", encoding="utf-8")
        self._video_offset[session.id] = 0
        self._write_meta(session)

    async def on_video_config(self, session: Session, config: dict, sps_pps: bytes) -> None:
        f = self._files.get(session.id)
        if f is not None:
            f.write(sps_pps)
            f.flush()
        self._video_offset[session.id] = self._video_offset.get(session.id, 0) + len(sps_pps)
        self._write_meta(session)

    async def on_video_chunk(self, session: Session, timestamp_ns: int, is_keyframe: bool, nal: bytes) -> None:
        f = self._files.get(session.id)
        frames_f = self._frames_files.get(session.id)
        offset = self._video_offset.get(session.id, 0)
        if frames_f is not None:
            frames_f.write(json.dumps({
                "timestamp_ns": timestamp_ns,
                "is_keyframe": is_keyframe,
                "byte_offset": offset,
                "size": len(nal),
            }) + "\n")
            frames_f.flush()
        if f is not None:
            f.write(nal)
            f.flush()
        self._video_offset[session.id] = offset + len(nal)

    async def on_imu_batch(self, session: Session, samples: list[protocol.ImuSample]) -> None:
        f = self._imu_files.get(session.id)
        if f is None:
            return
        for s in samples:
            f.write(json.dumps({
                "sensor_type": s.sensor_type,
                "timestamp_ns": s.timestamp_ns,
                "x": s.x,
                "y": s.y,
                "z": s.z,
            }) + "\n")
        f.flush()

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
        f = self._poses_files.get(session.id)
        if f is None:
            return
        f.write(json.dumps({
            "timestamp_ns": timestamp_ns,
            "tracking_state": tracking_state,
            "x": x, "y": y, "z": z,
            "qx": qx, "qy": qy, "qz": qz, "qw": qw,
        }) + "\n")
        f.flush()

    async def on_point_cloud(self, session: Session, timestamp_ns: int, points: list[protocol.Point]) -> None:
        f = self._points_files.get(session.id)
        if f is None:
            return
        f.write(json.dumps({
            "timestamp_ns": timestamp_ns,
            "points": [[p.x, p.y, p.z, p.confidence] for p in points],
        }) + "\n")
        f.flush()

    async def on_camera_intrinsics(
        self, session: Session, fx: float, fy: float, cx: float, cy: float, width: int, height: int
    ) -> None:
        intrinsics_path = self.session_dir(session.id) / "intrinsics.json"
        intrinsics_path.write_text(
            json.dumps({"fx": fx, "fy": fy, "cx": cx, "cy": cy, "width": width, "height": height}, indent=2),
            encoding="utf-8",
        )

    async def on_session_end(self, session: Session) -> None:
        for files in (self._files, self._frames_files, self._imu_files, self._poses_files, self._points_files):
            f = files.pop(session.id, None)
            if f is not None:
                f.close()
        self._video_offset.pop(session.id, None)
        self._write_meta(session)

    def _write_meta(self, session: Session) -> None:
        meta_path = self.session_dir(session.id) / "meta.json"
        meta_path.write_text(json.dumps(session.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8")
