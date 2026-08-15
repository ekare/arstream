"""TCP ingest server speaking docs/PROTOCOL.md -- binds each connection to a
Session and dispatches every decoded message to PluginManager. This used to
(M5) log directly; now that's the plugins' job (see plugins/recorder.py,
plugins/stats.py) -- this file just decodes the protocol and dispatches."""

from __future__ import annotations

import asyncio
import logging
import time

from . import protocol
from .context import AppContext
from .session import Session

logger = logging.getLogger("arstream_server.ingest")


class ClientSession:
    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, ctx: AppContext):
        self.reader = reader
        self.writer = writer
        self.ctx = ctx
        peer = writer.get_extra_info("peername")
        self.session = Session.new(peer_address=f"{peer[0]}:{peer[1]}" if peer else "unknown")

    async def run(self) -> None:
        ctx = self.ctx
        ctx.sessions[self.session.id] = self.session
        logger.info("connection opened: %s (session=%s)", self.session.peer_address, self.session.id)
        await ctx.plugin_manager.dispatch_session_start(self.session)

        buffer = b""
        try:
            while True:
                chunk = await self.reader.read(65536)
                if not chunk:
                    break
                buffer += chunk
                buffer = await self._consume(buffer)
        except (ConnectionResetError, asyncio.IncompleteReadError) as exc:
            logger.warning("connection closed with an error: %s (%s)", self.session.peer_address, exc)
        finally:
            self.session.ended_at = time.time()
            await ctx.plugin_manager.dispatch_session_end(self.session)
            logger.info(
                "connection closed: %s -- %d frames (%d keyframes), %.1f KB",
                self.session.peer_address,
                self.session.frame_count,
                self.session.keyframe_count,
                self.session.byte_count / 1024.0,
            )
            self.writer.close()

    async def _consume(self, buffer: bytes) -> bytes:
        while True:
            result = protocol.next_message(buffer)
            if result is None:
                return buffer
            msg, consumed = result
            buffer = buffer[consumed:]
            await self._handle(msg)

    async def _handle(self, msg: protocol.Message) -> None:
        mt = msg.header.msg_type
        pm = self.ctx.plugin_manager
        if mt == protocol.MsgType.VIDEO_CONFIG:
            body, sps_pps = protocol.decode_video_config(msg.payload)
            await pm.dispatch_video_config(self.session, body, sps_pps)
        elif mt == protocol.MsgType.VIDEO_CHUNK:
            ts, is_key, nal = protocol.decode_video_chunk(msg.payload)
            await pm.dispatch_video_chunk(self.session, ts, is_key, nal)
        else:
            logger.info("unhandled msg_type=0x%02x, %d bytes", mt, len(msg.payload))


async def run_ingest_server(host: str, port: int, ctx: AppContext) -> asyncio.base_events.Server:
    async def handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        await ClientSession(reader, writer, ctx).run()

    server = await asyncio.start_server(handler, host, port)
    addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
    logger.info("ingest server listening: %s", addrs)
    return server
