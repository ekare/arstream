"""PluginManager dispatch + error isolation, and external (directory-based)
plugin loading (load_plugins_from_dir)."""

from pathlib import Path

import pytest

from arstream_server.plugins import PluginManager, StreamPlugin, load_plugins_from_dir
from arstream_server.session import Session


class _RecordingPlugin(StreamPlugin):
    name = "recording"

    def __init__(self):
        self.calls: list[str] = []

    async def on_session_start(self, session):
        self.calls.append("start")

    async def on_video_config(self, session, config, sps_pps):
        self.calls.append("config")

    async def on_video_chunk(self, session, timestamp_ns, is_keyframe, nal):
        self.calls.append("chunk")

    async def on_imu_batch(self, session, samples):
        self.calls.append("imu_batch")

    async def on_pose_sample(self, session, timestamp_ns, tracking_state, x, y, z, qx, qy, qz, qw):
        self.calls.append("pose_sample")

    async def on_point_cloud(self, session, timestamp_ns, points):
        self.calls.append("point_cloud")

    async def on_camera_intrinsics(self, session, fx, fy, cx, cy, width, height):
        self.calls.append("camera_intrinsics")

    async def on_session_end(self, session):
        self.calls.append("end")


class _ExplodingPlugin(StreamPlugin):
    name = "exploding"

    async def on_session_start(self, session):
        raise RuntimeError("deliberate test error")


@pytest.fixture
def session():
    return Session.new(peer_address="127.0.0.1:1")


@pytest.mark.asyncio
async def test_dispatch_calls_every_plugin_hook(session):
    p = _RecordingPlugin()
    pm = PluginManager([p])
    await pm.dispatch_session_start(session)
    await pm.dispatch_video_config(session, {}, b"")
    await pm.dispatch_video_chunk(session, 0, False, b"")
    await pm.dispatch_imu_batch(session, [])
    await pm.dispatch_pose_sample(session, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0)
    await pm.dispatch_point_cloud(session, 0, [])
    await pm.dispatch_camera_intrinsics(session, 0.0, 0.0, 0.0, 0.0, 0, 0)
    await pm.dispatch_session_end(session)
    assert p.calls == [
        "start", "config", "chunk", "imu_batch", "pose_sample", "point_cloud", "camera_intrinsics", "end",
    ]


@pytest.mark.asyncio
async def test_one_plugin_exception_does_not_stop_others(session):
    good = _RecordingPlugin()
    pm = PluginManager([_ExplodingPlugin(), good])
    await pm.dispatch_session_start(session)  # must not raise, good should still run
    assert good.calls == ["start"]


@pytest.mark.asyncio
async def test_add_appends_plugin(session):
    pm = PluginManager([])
    p = _RecordingPlugin()
    pm.add(p)
    await pm.dispatch_session_start(session)
    assert p.calls == ["start"]


def test_load_plugins_from_dir_finds_streamplugin_subclasses(tmp_path: Path):
    plugin_file = tmp_path / "my_plugin.py"
    plugin_file.write_text(
        "from arstream_server.plugins import StreamPlugin\n"
        "class MyPlugin(StreamPlugin):\n"
        "    name = 'my_plugin'\n",
        encoding="utf-8",
    )
    plugins = load_plugins_from_dir(tmp_path)
    assert [p.name for p in plugins] == ["my_plugin"]


def test_load_plugins_from_dir_skips_underscore_prefixed_files(tmp_path: Path):
    (tmp_path / "_helper.py").write_text(
        "from arstream_server.plugins import StreamPlugin\n"
        "class Helper(StreamPlugin):\n"
        "    name = 'helper'\n",
        encoding="utf-8",
    )
    assert load_plugins_from_dir(tmp_path) == []


def test_load_plugins_from_dir_missing_dir_returns_empty(tmp_path: Path):
    assert load_plugins_from_dir(tmp_path / "missing") == []


def test_load_plugins_from_dir_broken_plugin_is_skipped_not_fatal(tmp_path: Path):
    (tmp_path / "broken.py").write_text("raise RuntimeError('blows up at import time')\n", encoding="utf-8")
    (tmp_path / "ok_plugin.py").write_text(
        "from arstream_server.plugins import StreamPlugin\n"
        "class OkPlugin(StreamPlugin):\n"
        "    name = 'ok'\n",
        encoding="utf-8",
    )
    plugins = load_plugins_from_dir(tmp_path)
    assert [p.name for p in plugins] == ["ok"]
