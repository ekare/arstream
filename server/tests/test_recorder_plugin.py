"""RecorderPlugin -- verifies captures/<session_id>/video.h264 + meta.json
are written in the right order, with the right content."""

import json
from pathlib import Path

import pytest

from arstream_server.plugins.recorder import RecorderPlugin
from arstream_server.session import Session


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
    await plugin.on_session_end(session)
    assert session.id not in plugin._files


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
