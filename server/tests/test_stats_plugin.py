"""StatsPlugin -- updates session counters, publishes
session_start/session_update/session_end events to the WebSocket (via EventBus)."""

import pytest

from arstream_server.events import EventBus
from arstream_server.plugins.stats import StatsPlugin
from arstream_server.session import Session


@pytest.mark.asyncio
async def test_session_start_publishes_immediately():
    bus = EventBus()
    q = bus.subscribe()
    plugin = StatsPlugin(bus)
    session = Session.new(peer_address="1.2.3.4:1")

    await plugin.on_session_start(session)

    event = q.get_nowait()
    assert event["type"] == "session_start"
    assert event["session"]["id"] == session.id


@pytest.mark.asyncio
async def test_video_config_sets_rotation_and_publishes():
    bus = EventBus()
    q = bus.subscribe()
    plugin = StatsPlugin(bus)
    session = Session.new(peer_address="1.2.3.4:1")

    await plugin.on_video_config(session, {"codec": "h264", "rotation": 90}, b"")

    assert session.rotation_degrees == 90
    assert session.video_config == {"codec": "h264", "rotation": 90}
    assert q.get_nowait()["type"] == "session_update"


@pytest.mark.asyncio
async def test_video_chunk_updates_counters_every_chunk():
    bus = EventBus()
    plugin = StatsPlugin(bus)
    session = Session.new(peer_address="1.2.3.4:1")
    await plugin.on_session_start(session)

    await plugin.on_video_chunk(session, 0, True, b"1234")
    await plugin.on_video_chunk(session, 1, False, b"56")

    assert session.frame_count == 2
    assert session.keyframe_count == 1
    assert session.byte_count == 6


@pytest.mark.asyncio
async def test_video_chunk_only_publishes_every_15_frames():
    bus = EventBus()
    q = bus.subscribe()
    plugin = StatsPlugin(bus)
    session = Session.new(peer_address="1.2.3.4:1")
    await plugin.on_session_start(session)
    q.get_nowait()  # drain the session_start event

    for _ in range(14):
        await plugin.on_video_chunk(session, 0, False, b"x")
    assert q.empty()

    await plugin.on_video_chunk(session, 0, False, b"x")  # 15th frame
    event = q.get_nowait()
    assert event["type"] == "session_update"
    assert event["session"]["frame_count"] == 15


@pytest.mark.asyncio
async def test_session_end_publishes_and_clears_start_time():
    bus = EventBus()
    q = bus.subscribe()
    plugin = StatsPlugin(bus)
    session = Session.new(peer_address="1.2.3.4:1")
    await plugin.on_session_start(session)
    q.get_nowait()

    await plugin.on_session_end(session)

    assert session.id not in plugin._start_times
    assert q.get_nowait()["type"] == "session_end"
