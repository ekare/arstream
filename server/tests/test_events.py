"""EventBus's pub-sub behavior -- especially the drop-oldest-when-full
behavior (a slow dashboard tab must not block the publish line)."""

import asyncio

import pytest

from arstream_server.events import EventBus


@pytest.mark.asyncio
async def test_subscribe_receives_published_event():
    bus = EventBus()
    q = bus.subscribe()
    await bus.publish({"type": "session_start", "session": {"id": "x"}})
    event = q.get_nowait()
    assert event["type"] == "session_start"


@pytest.mark.asyncio
async def test_multiple_subscribers_each_get_the_event():
    bus = EventBus()
    q1, q2 = bus.subscribe(), bus.subscribe()
    await bus.publish({"type": "session_end"})
    assert q1.get_nowait()["type"] == "session_end"
    assert q2.get_nowait()["type"] == "session_end"


@pytest.mark.asyncio
async def test_unsubscribe_stops_delivery():
    bus = EventBus()
    q = bus.subscribe()
    bus.unsubscribe(q)
    await bus.publish({"type": "session_end"})
    assert q.empty()


@pytest.mark.asyncio
async def test_full_queue_drops_oldest_not_newest():
    bus = EventBus()
    q = bus.subscribe()
    for i in range(100):
        await bus.publish({"n": i})
    assert q.full()
    await bus.publish({"n": "newest"})  # the queue is already full -- the oldest should be dropped
    assert q.qsize() == 100
    drained = [q.get_nowait()["n"] for _ in range(100)]
    assert drained[0] == 1  # 0 was dropped
    assert drained[-1] == "newest"  # the newest event wasn't lost
