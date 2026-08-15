"""Tests for protocol.py against the golden-byte fixtures/*.bin files (see
fixtures/generate_fixtures.py). The same fixtures are also used by
mobile/native/tests/protocol_test.cpp -- if the two drift apart here, CI breaks."""

from pathlib import Path

import pytest

from arstream_server import protocol

FIXTURES = Path(__file__).parent.parent.parent / "fixtures"


def _read_fixture(name: str) -> bytes:
    return (FIXTURES / name).read_bytes()


def test_clock_sync_request_matches_fixture():
    encoded = protocol.encode_clock_sync_request(seq=1, client_send_time_ns=1234567890123)
    assert encoded == _read_fixture("clock_sync_request.bin")


def test_clock_sync_response_matches_fixture():
    encoded = protocol.encode_clock_sync_response(seq=2, client_send_time_ns=1000, server_recv_time_ns=1050, server_send_time_ns=1060)
    assert encoded == _read_fixture("clock_sync_response.bin")


def test_imu_batch_matches_fixture():
    samples = [
        protocol.ImuSample(sensor_type=0, timestamp_ns=100, x=1.5, y=-2.5, z=9.8),
        protocol.ImuSample(sensor_type=1, timestamp_ns=200, x=0.1, y=0.2, z=0.3),
    ]
    encoded = protocol.encode_imu_batch(seq=3, samples=samples)
    assert encoded == _read_fixture("imu_batch.bin")


def test_pose_sample_matches_fixture():
    encoded = protocol.encode_pose_sample(seq=4, timestamp_ns=5000, tracking_state=2, x=1.0, y=2.0, z=3.0, qx=0.0, qy=0.0, qz=0.0, qw=1.0)
    assert encoded == _read_fixture("pose_sample.bin")


def test_camera_intrinsics_matches_fixture():
    encoded = protocol.encode_camera_intrinsics(seq=5, fx=600.5, fy=601.5, cx=320.0, cy=240.0, width=640, height=480)
    assert encoded == _read_fixture("camera_intrinsics.bin")


def test_point_cloud_matches_fixture():
    points = [protocol.Point(1.0, 2.0, 3.0, 0.9), protocol.Point(4.0, 5.0, 6.0, 0.8)]
    encoded = protocol.encode_point_cloud(seq=6, timestamp_ns=7000, points=points)
    assert encoded == _read_fixture("point_cloud.bin")


# -- Round-trip tests (encode -> decode -> original value) --


def test_header_round_trip():
    h = protocol.Header(payload_length=42, msg_type=0x20, flags=0, protocol_version=1, sequence_number=7)
    assert protocol.decode_header(protocol.encode_header(h)) == h


def test_clock_sync_response_round_trip():
    encoded = protocol.encode_clock_sync_response(seq=1, client_send_time_ns=10, server_recv_time_ns=20, server_send_time_ns=30)
    msg, consumed = protocol.next_message(encoded)
    assert consumed == len(encoded)
    assert msg.header.msg_type == protocol.MsgType.CLOCK_SYNC_RESPONSE
    assert protocol.decode_clock_sync_response(msg.payload) == (10, 20, 30)


def test_imu_batch_round_trip():
    samples = [protocol.ImuSample(0, 1, 1.0, 2.0, 3.0), protocol.ImuSample(1, 2, 4.0, 5.0, 6.0)]
    encoded = protocol.encode_imu_batch(seq=1, samples=samples)
    msg, _ = protocol.next_message(encoded)
    decoded = protocol.decode_imu_batch(msg.payload)
    assert decoded == samples


def test_point_cloud_round_trip():
    points = [protocol.Point(1.0, 2.0, 3.0, 0.5)]
    encoded = protocol.encode_point_cloud(seq=1, timestamp_ns=99, points=points)
    msg, _ = protocol.next_message(encoded)
    ts, decoded_points = protocol.decode_point_cloud(msg.payload)
    assert ts == 99
    assert decoded_points == points


def test_hello_json_round_trip():
    body = {"device_id": "abc-123", "capabilities": {"arcore_available": True}}
    encoded = protocol.encode_hello(seq=1, body=body)
    msg, consumed = protocol.next_message(encoded)
    assert consumed == len(encoded)
    assert msg.header.msg_type == protocol.MsgType.HELLO
    assert protocol.decode_hello(msg.payload) == body


def test_video_config_round_trip():
    body = {"codec": "h264", "width": 1280, "height": 720}
    sps_pps = b"\x00\x00\x00\x01\x67fake-sps\x00\x00\x00\x01\x68fake-pps"
    encoded = protocol.encode_video_config(seq=1, body=body, sps_pps_annexb=sps_pps)
    msg, _ = protocol.next_message(encoded)
    decoded_body, decoded_sps_pps = protocol.decode_video_config(msg.payload)
    assert decoded_body == body
    assert decoded_sps_pps == sps_pps


def test_video_chunk_round_trip():
    nal = b"\x00\x00\x00\x01\x65fake-idr-data"
    encoded = protocol.encode_video_chunk(seq=1, capture_timestamp_ns=123456, is_keyframe=True, nal_data=nal)
    msg, _ = protocol.next_message(encoded)
    ts, is_key, decoded_nal = protocol.decode_video_chunk(msg.payload)
    assert ts == 123456
    assert is_key is True
    assert decoded_nal == nal


def test_camera_intrinsics_round_trip():
    encoded = protocol.encode_camera_intrinsics(seq=1, fx=1.0, fy=2.0, cx=3.0, cy=4.0, width=100, height=200)
    msg, _ = protocol.next_message(encoded)
    assert protocol.decode_camera_intrinsics(msg.payload) == pytest.approx((1.0, 2.0, 3.0, 4.0, 100, 200))


# -- Forward compatibility: an unknown msg_type is safely skipped via payload_length --


def test_unknown_msg_type_is_skippable():
    unknown = protocol.encode_header(protocol.Header(payload_length=4, msg_type=0x99, sequence_number=1)) + b"\xde\xad\xbe\xef"
    known = protocol.encode_clock_sync_request(seq=2, client_send_time_ns=42)
    buffer = unknown + known

    messages = list(protocol.iter_messages(buffer))
    assert len(messages) == 2
    assert messages[0].header.msg_type == 0x99
    assert messages[1].header.msg_type == protocol.MsgType.CLOCK_SYNC_REQUEST
    assert protocol.decode_clock_sync_request(messages[1].payload) == 42


def test_incomplete_buffer_returns_none():
    encoded = protocol.encode_clock_sync_request(seq=1, client_send_time_ns=1)
    assert protocol.next_message(encoded[:-1]) is None
    assert protocol.next_message(encoded[:5]) is None
