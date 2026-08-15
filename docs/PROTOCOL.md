# arstream Wire Protocol — v1.0

This document defines the **canonical** wire format between the mobile client (phone) and the destination server (and any adapter behind it). Two independent implementations of the protocol live in this repo — `mobile/native/src/net/protocol_writer.cpp` (C++, the writer side) and `server/src/arstream_server/protocol.py` (Python, the reader side) — both must be a faithful mirror of this document. If you're writing a third implementation (an external adapter), this is the single source of truth.

**Status:** v1.0 — first pre-production release with fixed, documented limits.

---

## 0. Design principles

1. **A single persistent TCP connection.** The phone is the **client** (connects out to the server), the server listens. When the connection drops, the client restarts from `HELLO`.
2. **Every message carries the same 12-byte header.** Even an unknown `msg_type` can be safely skipped thanks to `payload_length` — this makes the protocol **forward-compatible** by design: an old reader can skip message types it doesn't know about from a newer writer without breaking.
3. **Raw data always flows; ARCore fields are optional.** `capabilities` in `HELLO` and `negotiated` in `HELLO_ACK` negotiate this explicitly.
4. **Timestamps are kept strictly separate**: the device's own hardware clock (for relative ordering/deltas — the one that actually matters) is never mixed with the coarse absolute-clock offset computed via `CLOCK_SYNC` (for logging/alignment).
5. **No encryption/authentication in v1.** A trusted network is assumed (see §6). This is a deliberate boundary, not an oversight.

---

## 1. Transport layer

- **Protocol:** TCP, single connection, opened client → server.
- **Byte order:** All multi-byte integers are **big-endian** (network byte order).
- **String encoding:** All text fields are UTF-8.
- **JSON payloads:** Uncompressed, single-line, UTF-8.

---

## 2. Message header (every message, 12 bytes, fixed)

| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `payload_length` | `uint32` | Number of bytes following the header |
| 4 | `msg_type` | `uint8` | See table below |
| 5 | `flags` | `uint8` | Reserved in v1, `0x00` |
| 6 | `protocol_version` | `uint16` | `0x0001` for v1 |
| 8 | `sequence_number` | `uint32` | Monotonically increasing counter per connection, per `msg_type` (for diagnostics + future UDP/v2 compatibility) |

Total: 12-byte header + `payload_length` bytes of payload.

**Rule for handling unknown `msg_type`:** If a receiver sees an unrecognized `msg_type`, it skips `payload_length` bytes and continues reading the next header. It never closes the connection.

---

## 3. Message types

### 3.1 Handshake

**`0x01 HELLO`** (client→server) — once, immediately after the connection opens.

```json
{
  "device_id": "string (persistent, device-specific UUID)",
  "device_model": "string (e.g. SM-A307FN)",
  "os_version": "string (e.g. Android 11 / API 30)",
  "app_version": "string (semver)",
  "capabilities": {
    "arcore_available": false,
    "point_cloud_available": false,
    "intrinsics_available": false
  },
  "video": {
    "codec": "h264",
    "profile": "baseline|main|high",
    "width": 1280,
    "height": 720,
    "fps": 30,
    "bitrate_kbps": 4000
  },
  "imu": {
    "accel_hz": 100,
    "gyro_hz": 100
  }
}
```

**`0x02 HELLO_ACK`** (server→client) — response to `HELLO`.

```json
{
  "accepted": true,
  "session_id": "uuid",
  "server_time_ns": 1234567890123456789,
  "reason": "string (only if accepted=false)",
  "negotiated": {
    "video": { "codec": "h264", "width": 1280, "height": 720, "fps": 30, "bitrate_kbps": 4000 },
    "imu": true,
    "pose": false,
    "point_cloud": false,
    "intrinsics": false
  }
}
```
`negotiated` is the **AND** of what the client offered and what the server accepted — if the server doesn't want a field, it returns `false`/a reduced value, and the client must comply.

### 3.2 Clock synchronization

**`0x03 CLOCK_SYNC_REQUEST`** (client→server): `int64 client_send_time_ns` (8-byte payload).

**`0x04 CLOCK_SYNC_RESPONSE`** (server→client): `int64 client_send_time_ns, int64 server_recv_time_ns, int64 server_send_time_ns` (24-byte payload).

The client computes the offset as:
```
offset = ((server_recv_time_ns - client_send_time_ns) + (server_send_time_ns - client_recv_time_ns)) / 2
```
This offset is only for **absolute** time alignment/logging — timestamps inside `VIDEO_CHUNK`/`IMU_BATCH` are the RAW device clock and are never corrected with this offset (so relative delta calculations stay intact).

### 3.3 Video

**`0x05 VIDEO_CONFIG`** (client→server) — sent once after `HELLO_ACK`, and resent on every reconnect or mid-stream reconfiguration.

Payload: `uint16 json_len` + UTF-8 JSON (`codec, profile, width, height, fps, bitrate_kbps, rotation`) + raw SPS/PPS NAL bytes (Annex-B, including start code).

`rotation` (int, degrees — 0/90/180/270): the camera sensor's `ACAMERA_SENSOR_ORIENTATION` value. The frame data **itself** is never rotated (so the encoder's zero-copy, Surface-input path stays intact) — this is metadata only; the receiving side must rotate the frame clockwise by this amount before displaying it. Same problem standard video containers solve (MP4 "rotation matrix" etc.), same method: metadata, not pixels, gets rotated.

**`0x10 VIDEO_CHUNK`** (client→server) — one per video frame.

Payload: `int64 capture_timestamp_ns` (8) + `uint8 flags` (1, bit0=keyframe) + all remaining bytes are an Annex-B start-code-delimited H.264 NAL unit.

Default keyframe interval is **2 seconds** (~60 frames @ 30fps), negotiable in `VIDEO_CONFIG`.

### 3.4 IMU

**`0x20 IMU_BATCH`** (client→server) — sent batched every ~50–100ms (NOT per frame).

Payload: `uint16 sample_count` + `sample_count` repetitions of:
| Field | Type |
|---|---|
| `sensor_type` | `uint8` (`0`=accelerometer, `1`=gyroscope) |
| `timestamp_ns` | `int64` |
| `x, y, z` | `float32` × 3 |

### 3.5 Optional ARCore fields

**`0x30 POSE_SAMPLE`** (client→server, optional): `int64 timestamp_ns, uint8 tracking_state, float32 x,y,z, float32 qx,qy,qz,qw` (37 bytes).

**`0x31 POINT_CLOUD`** (client→server, optional): `int64 timestamp_ns, uint32 point_count` + `point_count` repetitions of `float32 x,y,z,confidence` (16 bytes/point).

**`0x32 CAMERA_INTRINSICS`** (client→server, optional, once or on change): `float32 fx,fy,cx,cy, uint32 width,height` (24 bytes).

### 3.6 Control

**`0x40 STATUS`** (bidirectional): JSON `{"level": "info|warn|error", "code": "string", "message": "string"}` — e.g. the server may ask to lower the bitrate.

**`0xF0 GOODBYE`** (bidirectional): JSON `{"reason": "string"}` — sent before gracefully closing the connection.

---

## 4. Connection lifecycle

```
client connects (TCP connect)
  → sends HELLO
  ← waits for HELLO_ACK (connection closes if accepted=false)
  → CLOCK_SYNC_REQUEST / ← CLOCK_SYNC_RESPONSE (optional but recommended, once per connection is enough)
  → sends VIDEO_CONFIG
  → (loop) VIDEO_CHUNK, IMU_BATCH, [POSE_SAMPLE, POINT_CLOUD, CAMERA_INTRINSICS]
  → GOODBYE (on graceful close) or the connection drops (client retries, restarting from HELLO)
```

---

## 5. Versioning

`protocol_version` is carried in every header. Same major version = wire-compatible. Since unknown `msg_type`s can be safely skipped by the rule in §2, a client/server pair on the same major version won't deadlock each other even as new message types get added.

**Planned v2 (not yet built, `docs/ROADMAP.md`):** UDP transport + a lightweight/fast encryption scheme (not full TLS — a low-latency handshake + AEAD, e.g. ChaCha20-Poly1305; QUIC is also a candidate that solves both at once) — this only changes the *transport*, not the message vocabulary/semantics above. The `sequence_number` field is already reserved for this.

---

## 6. Security — a deliberate v1 boundary

v1 has no TLS, no authentication. Assumption: client and server are on a trusted network (same LAN, or behind a VPN/tunnel). If internet-facing use is needed, the recommended path for v1 is not to bolt protocol-specific crypto on top, but to set up **a tunnel like WireGuard/Tailscale/SSH port-forwarding** — consistent with the "keep it simple" design choice and the decision that network topology should be solved at the deployment layer. In v2 (see §5) this boundary is planned to be addressed alongside UDP transport with a lightweight encryption layer — not yet designed.

---

## 7. Example byte sequences (golden fixtures)

Both `protocol_writer_test.cpp` and `server/tests/test_protocol.py` are tested against the hex-encoded example messages in this section (or under `fixtures/`), so the two independent implementations can't silently drift apart. Filled in at M4.

---

## 8. Changelog

- **v1.0** — initial release. Message types 0x01–0x05, 0x10, 0x20, 0x30–0x32, 0x40, 0xF0.
