"""RecorderPlugin -- verifies captures/<session_id>/video.h264 + meta.json
are written in the right order, with the right content."""

import json
from pathlib import Path

import pytest

from arstream_server import protocol
from arstream_server.plugins.recorder import RecorderPlugin
from arstream_server.session import Session


def _read_jsonl(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


@pytest.mark.asyncio
async def test_recorder_writes_sps_pps_then_chunks_in_order(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")

    await plugin.on_session_start(session)
    await plugin.on_video_config(session, {"codec": "h264"}, b"SPSPPS")
    await plugin.on_video_chunk(session, 0, True, b"NAL1")
    await plugin.on_video_chunk(session, 1, False, b"NAL2")
    await plugin.on_session_end(session)

    video_bytes = (tmp_path / session.id / "video.h264").read_bytes()
    assert video_bytes == b"SPSPPSNAL1NAL2"


@pytest.mark.asyncio
async def test_recorder_meta_json_reflects_session_state(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")
    session.frame_count = 3
    session.keyframe_count = 1

    await plugin.on_session_start(session)
    session.ended_at = session.started_at + 1.0  # same as what ingest.py does before dispatch_session_end
    await plugin.on_session_end(session)

    meta = json.loads((tmp_path / session.id / "meta.json").read_text(encoding="utf-8"))
    assert meta["id"] == session.id
    assert meta["frame_count"] == 3
    assert meta["keyframe_count"] == 1
    assert meta["is_live"] is False


@pytest.mark.asyncio
async def test_recorder_closes_file_handle_on_session_end(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")
    await plugin.on_session_start(session)
    assert session.id in plugin._files
    assert session.id in plugin._frames_files
    assert session.id in plugin._imu_files
    assert session.id in plugin._poses_files
    assert session.id in plugin._points_files
    await plugin.on_session_end(session)
    assert session.id not in plugin._files
    assert session.id not in plugin._frames_files
    assert session.id not in plugin._imu_files
    assert session.id not in plugin._poses_files
    assert session.id not in plugin._points_files
    assert session.id not in plugin._video_offset


@pytest.mark.asyncio
async def test_recorder_handles_concurrent_sessions_independently(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    s1 = Session.new(peer_address="1.2.3.4:1")
    s2 = Session.new(peer_address="1.2.3.4:2")

    await plugin.on_session_start(s1)
    await plugin.on_session_start(s2)
    await plugin.on_video_chunk(s1, 0, True, b"S1")
    await plugin.on_video_chunk(s2, 0, True, b"S2")
    await plugin.on_session_end(s1)
    await plugin.on_session_end(s2)

    assert (tmp_path / s1.id / "video.h264").read_bytes() == b"S1"
    assert (tmp_path / s2.id / "video.h264").read_bytes() == b"S2"


@pytest.mark.asyncio
async def test_frames_jsonl_byte_offsets_match_video_file(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")

    await plugin.on_session_start(session)
    await plugin.on_video_config(session, {"codec": "h264"}, b"SPSPPS")  # 6 bytes, no frames.jsonl line
    await plugin.on_video_chunk(session, 100, True, b"NAL1")  # 4 bytes, starts at offset 6
    await plugin.on_video_chunk(session, 200, False, b"NAL22")  # 5 bytes, starts at offset 10
    await plugin.on_session_end(session)

    rows = _read_jsonl(tmp_path / session.id / "frames.jsonl")
    assert rows == [
        {"timestamp_ns": 100, "is_keyframe": True, "byte_offset": 6, "size": 4},
        {"timestamp_ns": 200, "is_keyframe": False, "byte_offset": 10, "size": 5},
    ]

    video_bytes = (tmp_path / session.id / "video.h264").read_bytes()
    for row in rows:
        chunk = video_bytes[row["byte_offset"]:row["byte_offset"] + row["size"]]
        assert len(chunk) == row["size"]


@pytest.mark.asyncio
async def test_frames_jsonl_offset_survives_reconnect_config_resend(tmp_path: Path):
    # docs/PROTOCOL.md: VIDEO_CONFIG (SPS/PPS) is resent on every reconnect and
    # rewritten into video.h264 each time -- the offset counter must account
    # for that, or frames.jsonl would drift from the real file position.
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")

    await plugin.on_session_start(session)
    await plugin.on_video_config(session, {"codec": "h264"}, b"SPSPPS")  # 6 bytes
    await plugin.on_video_chunk(session, 0, True, b"NAL1")  # 4 bytes, offset 6
    await plugin.on_video_config(session, {"codec": "h264"}, b"SPSPPS")  # reconnect resend, 6 bytes
    await plugin.on_video_chunk(session, 1, False, b"NAL2")  # offset 6+4+6=16
    await plugin.on_session_end(session)

    rows = _read_jsonl(tmp_path / session.id / "frames.jsonl")
    assert rows[0]["byte_offset"] == 6
    assert rows[1]["byte_offset"] == 16
    assert (tmp_path / session.id / "video.h264").read_bytes() == b"SPSPPSNAL1SPSPPSNAL2"


@pytest.mark.asyncio
async def test_imu_batch_writes_one_line_per_sample(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")
    samples = [
        protocol.ImuSample(sensor_type=1, timestamp_ns=10, x=0.1, y=0.2, z=9.8, w=0.0),
        protocol.ImuSample(sensor_type=15, timestamp_ns=11, x=0.0, y=0.0, z=0.7, w=0.7),  # game rotation vector, quaternion
    ]

    await plugin.on_session_start(session)
    await plugin.on_imu_batch(session, samples)
    await plugin.on_session_end(session)

    rows = _read_jsonl(tmp_path / session.id / "imu.jsonl")
    assert rows == [
        {"sensor_type": 1, "timestamp_ns": 10, "x": 0.1, "y": 0.2, "z": 9.8, "w": 0.0},
        {"sensor_type": 15, "timestamp_ns": 11, "x": 0.0, "y": 0.0, "z": 0.7, "w": 0.7},
    ]


@pytest.mark.asyncio
async def test_pose_sample_writes_to_poses_jsonl(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")

    await plugin.on_session_start(session)
    await plugin.on_pose_sample(session, 500, 2, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0)
    await plugin.on_session_end(session)

    rows = _read_jsonl(tmp_path / session.id / "poses.jsonl")
    assert rows == [{
        "timestamp_ns": 500, "tracking_state": 2,
        "x": 1.0, "y": 2.0, "z": 3.0,
        "qx": 0.0, "qy": 0.0, "qz": 0.0, "qw": 1.0,
    }]


@pytest.mark.asyncio
async def test_point_cloud_writes_one_line_per_message(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")
    points = [protocol.Point(1.0, 2.0, 3.0, 0.9), protocol.Point(4.0, 5.0, 6.0, 0.8)]

    await plugin.on_session_start(session)
    await plugin.on_point_cloud(session, 700, points)
    await plugin.on_session_end(session)

    rows = _read_jsonl(tmp_path / session.id / "points.jsonl")
    assert rows == [{"timestamp_ns": 700, "points": [[1.0, 2.0, 3.0, 0.9], [4.0, 5.0, 6.0, 0.8]]}]


@pytest.mark.asyncio
async def test_camera_intrinsics_overwrites_snapshot_not_append(tmp_path: Path):
    plugin = RecorderPlugin(tmp_path)
    session = Session.new(peer_address="1.2.3.4:9")

    await plugin.on_session_start(session)
    await plugin.on_camera_intrinsics(session, 600.0, 600.0, 320.0, 240.0, 640, 480)
    await plugin.on_camera_intrinsics(session, 610.0, 610.0, 320.0, 240.0, 640, 480)  # changed mid-session
    await plugin.on_session_end(session)

    intrinsics_path = tmp_path / session.id / "intrinsics.json"
    data = json.loads(intrinsics_path.read_text(encoding="utf-8"))
    assert data == {"fx": 610.0, "fy": 610.0, "cx": 320.0, "cy": 240.0, "width": 640, "height": 480}
    # single JSON object, not JSON-lines -- confirm it wasn't appended
    assert intrinsics_path.read_text(encoding="utf-8").count("{") == 1
