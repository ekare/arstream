"""web/app.py -- behavioral tests for the FastAPI dashboard's HTTP +
WebSocket endpoints. Doesn't open a real TCP socket; uses starlette's
in-ASGI TestClient (these tests are the automated form of the manual smoke
verification done earlier in the session)."""

import io
import zipfile

import pytest
from starlette.testclient import TestClient

from arstream_server.context import AppContext
from arstream_server.events import EventBus
from arstream_server.plugins import PluginManager
from arstream_server.session import Session
from arstream_server.web.app import create_app


@pytest.fixture
def ctx(tmp_path):
    return AppContext(bus=EventBus(), plugin_manager=PluginManager([]), captures_dir=tmp_path)


@pytest.fixture
def client(ctx):
    return TestClient(create_app(ctx))


def test_dashboard_root_serves_html(client):
    res = client.get("/")
    assert res.status_code == 200
    assert "arstream dashboard" in res.text


def test_list_sessions_empty(client):
    res = client.get("/api/sessions")
    assert res.status_code == 200
    assert res.json() == {"sessions": []}


def test_list_sessions_returns_added_session(ctx, client):
    s = Session.new(peer_address="1.2.3.4:1")
    ctx.sessions[s.id] = s
    res = client.get("/api/sessions")
    assert res.json()["sessions"][0]["id"] == s.id


def test_get_session_by_id(ctx, client):
    s = Session.new(peer_address="1.2.3.4:1")
    ctx.sessions[s.id] = s
    res = client.get(f"/api/sessions/{s.id}")
    assert res.status_code == 200
    assert res.json()["id"] == s.id


def test_get_session_missing_returns_404(client):
    res = client.get("/api/sessions/does-not-exist")
    assert res.status_code == 404


def test_download_missing_session_returns_404(client):
    res = client.get("/api/sessions/does-not-exist/download")
    assert res.status_code == 404


def test_download_produces_valid_zip_with_expected_files(ctx, client, tmp_path):
    session_id = "abcd1234"
    session_dir = ctx.captures_dir / session_id
    session_dir.mkdir()
    (session_dir / "video.h264").write_bytes(b"\x00\x00\x00\x01fake-nal")
    (session_dir / "meta.json").write_text('{"id": "abcd1234"}', encoding="utf-8")

    res = client.get(f"/api/sessions/{session_id}/download")

    assert res.status_code == 200
    assert res.headers["content-type"] == "application/zip"
    assert "arstream-abcd1234.zip" in res.headers["content-disposition"]

    zf = zipfile.ZipFile(io.BytesIO(res.content))
    assert zf.testzip() is None
    assert set(zf.namelist()) == {"video.h264", "meta.json"}
    assert zf.read("video.h264") == b"\x00\x00\x00\x01fake-nal"


def test_ws_live_delivers_published_events(ctx):
    # `with client:` exposes the same event loop TestClient runs the ASGI
    # app on as client.portal -- this lets us call ctx.bus.publish (which
    # would otherwise run on a different thread/loop) on that same loop and
    # verify the WebSocket actually receives it.
    client = TestClient(create_app(ctx))
    with client:
        with client.websocket_connect("/ws/live") as ws:
            client.portal.call(ctx.bus.publish, {"type": "session_start", "session": {"id": "x"}})
            received = ws.receive_json()
    assert received == {"type": "session_start", "session": {"id": "x"}}


def test_ws_live_unsubscribes_on_disconnect(ctx):
    client = TestClient(create_app(ctx))
    with client:
        with client.websocket_connect("/ws/live"):
            assert len(ctx.bus._subscribers) == 1
        assert len(ctx.bus._subscribers) == 0
