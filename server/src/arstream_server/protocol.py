"""arstream wire protocol v1 -- a faithful Python mirror of docs/PROTOCOL.md.

Speaks the exact same wire format as mobile/native/src/net/protocol.cpp.
Both were written independently against docs/PROTOCOL.md; the golden-byte
files under fixtures/ verify that the two haven't silently drifted apart
(see tests/test_protocol.py).

Unlike the C++ side, JSON-bodied messages (HELLO, VIDEO_CONFIG's json part,
STATUS, GOODBYE) are fully encoded/decoded here too -- stdlib json costs
nothing in Python, whereas on the C++ side it's deliberately left as opaque
bytes (see the note at the top of protocol.h).
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from enum import IntEnum

PROTOCOL_VERSION_1 = 0x0001
HEADER_SIZE = 12

_HEADER_STRUCT = struct.Struct(">IBBHI")  # payload_length, msg_type, flags, protocol_version, sequence_number


class MsgType(IntEnum):
    HELLO = 0x01
    HELLO_ACK = 0x02
    CLOCK_SYNC_REQUEST = 0x03
    CLOCK_SYNC_RESPONSE = 0x04
    VIDEO_CONFIG = 0x05
    VIDEO_CHUNK = 0x10
    IMU_BATCH = 0x20
    POSE_SAMPLE = 0x30
    POINT_CLOUD = 0x31
    CAMERA_INTRINSICS = 0x32
    STATUS = 0x40
    GOODBYE = 0xF0


@dataclass
class Header:
    payload_length: int
    msg_type: int
    flags: int = 0
    protocol_version: int = PROTOCOL_VERSION_1
    sequence_number: int = 0


@dataclass
class ImuSample:
    sensor_type: int  # 0=accelerometer, 1=gyroscope
    timestamp_ns: int
    x: float
    y: float
    z: float


@dataclass
class Point:
    x: float
    y: float
    z: float
    confidence: float


@dataclass
class Message:
    header: Header
    payload: bytes


class ProtocolError(ValueError):
    pass


def encode_header(h: Header) -> bytes:
    return _HEADER_STRUCT.pack(h.payload_length, h.msg_type, h.flags, h.protocol_version, h.sequence_number)


def decode_header(data: bytes) -> Header:
    if len(data) < HEADER_SIZE:
        raise ProtocolError(f"header requires at least {HEADER_SIZE} bytes, got {len(data)}")
    payload_length, msg_type, flags, protocol_version, sequence_number = _HEADER_STRUCT.unpack_from(data, 0)
    return Header(payload_length, msg_type, flags, protocol_version, sequence_number)


def _wrap(msg_type: MsgType, seq: int, payload: bytes) -> bytes:
    header = Header(payload_length=len(payload), msg_type=int(msg_type), sequence_number=seq)
    return encode_header(header) + payload


def next_message(buffer: bytes) -> tuple[Message, int] | None:
    """Safely skips unknown msg_types too -- returns None if the buffer
    doesn't contain a complete message yet (the caller should wait for more bytes)."""
    if len(buffer) < HEADER_SIZE:
        return None
    header = decode_header(buffer)
    total = HEADER_SIZE + header.payload_length
    if len(buffer) < total:
        return None
    payload = buffer[HEADER_SIZE:total]
    return Message(header, payload), total


def iter_messages(buffer: bytes):
    """Yields every complete message in buffer, in order (generator)."""
    offset = 0
    while True:
        result = next_message(buffer[offset:])
        if result is None:
            return
        message, consumed = result
        yield message
        offset += consumed


# -- HELLO / HELLO_ACK / STATUS / GOODBYE (JSON body) --


def encode_hello(seq: int, body: dict) -> bytes:
    return _wrap(MsgType.HELLO, seq, json.dumps(body).encode("utf-8"))


def decode_hello(payload: bytes) -> dict:
    return json.loads(payload.decode("utf-8"))


def encode_hello_ack(seq: int, body: dict) -> bytes:
    return _wrap(MsgType.HELLO_ACK, seq, json.dumps(body).encode("utf-8"))


def decode_hello_ack(payload: bytes) -> dict:
    return json.loads(payload.decode("utf-8"))


def encode_status(seq: int, body: dict) -> bytes:
    return _wrap(MsgType.STATUS, seq, json.dumps(body).encode("utf-8"))


def decode_status(payload: bytes) -> dict:
    return json.loads(payload.decode("utf-8"))


def encode_goodbye(seq: int, body: dict) -> bytes:
    return _wrap(MsgType.GOODBYE, seq, json.dumps(body).encode("utf-8"))


def decode_goodbye(payload: bytes) -> dict:
    return json.loads(payload.decode("utf-8"))


# -- Clock synchronization --


def encode_clock_sync_request(seq: int, client_send_time_ns: int) -> bytes:
    return _wrap(MsgType.CLOCK_SYNC_REQUEST, seq, struct.pack(">q", client_send_time_ns))


def decode_clock_sync_request(payload: bytes) -> int:
    (client_send_time_ns,) = struct.unpack_from(">q", payload, 0)
    return client_send_time_ns


def encode_clock_sync_response(seq: int, client_send_time_ns: int, server_recv_time_ns: int, server_send_time_ns: int) -> bytes:
    return _wrap(
        MsgType.CLOCK_SYNC_RESPONSE,
        seq,
        struct.pack(">qqq", client_send_time_ns, server_recv_time_ns, server_send_time_ns),
    )


def decode_clock_sync_response(payload: bytes) -> tuple[int, int, int]:
    return struct.unpack_from(">qqq", payload, 0)


# -- Video --


def encode_video_config(seq: int, body: dict, sps_pps_annexb: bytes) -> bytes:
    json_bytes = json.dumps(body).encode("utf-8")
    payload = struct.pack(">H", len(json_bytes)) + json_bytes + sps_pps_annexb
    return _wrap(MsgType.VIDEO_CONFIG, seq, payload)


def decode_video_config(payload: bytes) -> tuple[dict, bytes]:
    (json_len,) = struct.unpack_from(">H", payload, 0)
    json_bytes = payload[2 : 2 + json_len]
    sps_pps = payload[2 + json_len :]
    return json.loads(json_bytes.decode("utf-8")), sps_pps


def encode_video_chunk(seq: int, capture_timestamp_ns: int, is_keyframe: bool, nal_data: bytes) -> bytes:
    payload = struct.pack(">qB", capture_timestamp_ns, 0x01 if is_keyframe else 0x00) + nal_data
    return _wrap(MsgType.VIDEO_CHUNK, seq, payload)


def decode_video_chunk(payload: bytes) -> tuple[int, bool, bytes]:
    capture_timestamp_ns, flags = struct.unpack_from(">qB", payload, 0)
    return capture_timestamp_ns, bool(flags & 0x01), payload[9:]


# -- IMU --


def encode_imu_batch(seq: int, samples: list[ImuSample]) -> bytes:
    payload = bytearray(struct.pack(">H", len(samples)))
    for s in samples:
        payload += struct.pack(">Bqfff", s.sensor_type, s.timestamp_ns, s.x, s.y, s.z)
    return _wrap(MsgType.IMU_BATCH, seq, bytes(payload))


def decode_imu_batch(payload: bytes) -> list[ImuSample]:
    (count,) = struct.unpack_from(">H", payload, 0)
    offset = 2
    samples = []
    for _ in range(count):
        sensor_type, timestamp_ns, x, y, z = struct.unpack_from(">Bqfff", payload, offset)
        samples.append(ImuSample(sensor_type, timestamp_ns, x, y, z))
        offset += 21
    return samples


# -- Optional ARCore fields --


def encode_pose_sample(seq: int, timestamp_ns: int, tracking_state: int, x: float, y: float, z: float, qx: float, qy: float, qz: float, qw: float) -> bytes:
    payload = struct.pack(">qBfffffff", timestamp_ns, tracking_state, x, y, z, qx, qy, qz, qw)
    return _wrap(MsgType.POSE_SAMPLE, seq, payload)


def decode_pose_sample(payload: bytes):
    return struct.unpack_from(">qBfffffff", payload, 0)


def encode_point_cloud(seq: int, timestamp_ns: int, points: list[Point]) -> bytes:
    payload = bytearray(struct.pack(">qI", timestamp_ns, len(points)))
    for p in points:
        payload += struct.pack(">ffff", p.x, p.y, p.z, p.confidence)
    return _wrap(MsgType.POINT_CLOUD, seq, bytes(payload))


def decode_point_cloud(payload: bytes) -> tuple[int, list[Point]]:
    timestamp_ns, count = struct.unpack_from(">qI", payload, 0)
    offset = 12
    points = []
    for _ in range(count):
        x, y, z, confidence = struct.unpack_from(">ffff", payload, offset)
        points.append(Point(x, y, z, confidence))
        offset += 16
    return timestamp_ns, points


def encode_camera_intrinsics(seq: int, fx: float, fy: float, cx: float, cy: float, width: int, height: int) -> bytes:
    payload = struct.pack(">ffffII", fx, fy, cx, cy, width, height)
    return _wrap(MsgType.CAMERA_INTRINSICS, seq, payload)


def decode_camera_intrinsics(payload: bytes):
    return struct.unpack_from(">ffffII", payload, 0)
