#!/usr/bin/env python3
"""Golden-byte fixture generator -- produces docs/PROTOCOL.md's byte layout
DIRECTLY with struct.pack, without calling either protocol.py or
protocol.cpp. This way both implementations get tested against these files
-- neither is the other's "reference".

Run: python fixtures/generate_fixtures.py
"""

import struct
from pathlib import Path

OUT = Path(__file__).parent


def header(payload_len: int, msg_type: int, seq: int, flags: int = 0, version: int = 1) -> bytes:
    return struct.pack(">IBBHI", payload_len, msg_type, flags, version, seq)


def write(name: str, data: bytes) -> None:
    (OUT / name).write_bytes(data)
    print(f"{name}: {len(data)} bytes")


def main() -> None:
    # clock_sync_request: seq=1, client_send_time_ns=1234567890123
    payload = struct.pack(">q", 1234567890123)
    write("clock_sync_request.bin", header(len(payload), 0x03, 1) + payload)

    # clock_sync_response: seq=2, (1000, 1050, 1060)
    payload = struct.pack(">qqq", 1000, 1050, 1060)
    write("clock_sync_response.bin", header(len(payload), 0x04, 2) + payload)

    # imu_batch: seq=3, two samples: (accel, t=100, 1.5,-2.5,9.8,w=0),
    # (game_rotation_vector sensor_type=15, t=200, 0.1,0.2,0.3,w=0.9) -- the
    # second sample exercises the quaternion-scalar `w` field non-trivially.
    payload = struct.pack(">H", 2)
    payload += struct.pack(">Bqffff", 0, 100, 1.5, -2.5, 9.8, 0.0)
    payload += struct.pack(">Bqffff", 15, 200, 0.1, 0.2, 0.3, 0.9)
    write("imu_batch.bin", header(len(payload), 0x20, 3) + payload)

    # pose_sample: seq=4, t=5000, tracking_state=2, pos(1,2,3), quat(0,0,0,1)
    payload = struct.pack(">qBfffffff", 5000, 2, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0)
    write("pose_sample.bin", header(len(payload), 0x30, 4) + payload)

    # camera_intrinsics: seq=5, fx=600.5 fy=601.5 cx=320 cy=240 w=640 h=480
    payload = struct.pack(">ffffII", 600.5, 601.5, 320.0, 240.0, 640, 480)
    write("camera_intrinsics.bin", header(len(payload), 0x32, 5) + payload)

    # point_cloud: seq=6, t=7000, two points
    payload = struct.pack(">qI", 7000, 2)
    payload += struct.pack(">ffff", 1.0, 2.0, 3.0, 0.9)
    payload += struct.pack(">ffff", 4.0, 5.0, 6.0, 0.8)
    write("point_cloud.bin", header(len(payload), 0x31, 6) + payload)


if __name__ == "__main__":
    main()
