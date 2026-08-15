"""Minimal asyncio referans alicisi -- docs/PROTOCOL.md'yi konusur.

Bilinçli olarak kucuk tutuldu: su an yalniz VIDEO_CONFIG/VIDEO_CHUNK
gonderiyor olan StreamSink'i (mobile/native/src/sink/stream_sink.cpp)
dogrulamak icin yeterli -- HELLO/HELLO_ACK el sikismasi, CLOCK_SYNC ve
IMU_BATCH henuz yok (bkz. docs/ROADMAP.md). Amac: transport + "bellek-once/
disk-tasma" tamponunun gercek bir TCP baglantisinda calistigini kanitlamak.
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from . import protocol

logger = logging.getLogger("arstream_server")


class ClientSession:
    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, out_dir: Path | None):
        self.reader = reader
        self.writer = writer
        self.out_dir = out_dir
        self.frame_count = 0
        self.byte_count = 0
        self.keyframe_count = 0
        self._video_file = None

    async def run(self) -> None:
        peer = self.writer.get_extra_info("peername")
        logger.info("baglanti acildi: %s", peer)
        buffer = b""
        try:
            while True:
                chunk = await self.reader.read(65536)
                if not chunk:
                    break
                buffer += chunk
                buffer = self._consume(buffer)
        except (ConnectionResetError, asyncio.IncompleteReadError) as exc:
            logger.warning("baglanti hata ile kapandi: %s (%s)", peer, exc)
        finally:
            if self._video_file:
                self._video_file.close()
            logger.info(
                "baglanti kapandi: %s -- %d kare (%d keyframe), %.1f KB",
                peer,
                self.frame_count,
                self.keyframe_count,
                self.byte_count / 1024.0,
            )
            self.writer.close()

    def _consume(self, buffer: bytes) -> bytes:
        while True:
            result = protocol.next_message(buffer)
            if result is None:
                return buffer
            msg, consumed = result
            buffer = buffer[consumed:]
            self._handle(msg)

    def _handle(self, msg: protocol.Message) -> None:
        mt = msg.header.msg_type
        if mt == protocol.MsgType.VIDEO_CONFIG:
            body, sps_pps = protocol.decode_video_config(msg.payload)
            logger.info("VIDEO_CONFIG: %s, sps/pps %d byte", body, len(sps_pps))
            if self.out_dir:
                self.out_dir.mkdir(parents=True, exist_ok=True)
                self._video_file = open(self.out_dir / "received.h264", "wb")
                self._video_file.write(sps_pps)
        elif mt == protocol.MsgType.VIDEO_CHUNK:
            ts, is_key, nal = protocol.decode_video_chunk(msg.payload)
            self.frame_count += 1
            self.byte_count += len(nal)
            if is_key:
                self.keyframe_count += 1
            if self._video_file:
                self._video_file.write(nal)
            if self.frame_count % 30 == 0:
                logger.info(
                    "%d kare, %.1f KB, son ts=%d, keyframe=%s",
                    self.frame_count,
                    self.byte_count / 1024.0,
                    ts,
                    is_key,
                )
        else:
            logger.info("islenmeyen msg_type=0x%02x, %d byte", mt, len(msg.payload))


async def run_server(host: str, port: int, out_dir: Path | None) -> None:
    async def handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        await ClientSession(reader, writer, out_dir).run()

    server = await asyncio.start_server(handler, host, port)
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    logger.info("dinleniyor: %s", addrs)
    async with server:
        await server.serve_forever()
