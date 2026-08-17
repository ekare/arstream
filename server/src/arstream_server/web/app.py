"""FastAPI dashboard -- reads the AppContext that ingest.py fills in, has no
protocol/socket logic of its own. The auto-generated OpenAPI UI at `/docs`
(FastAPI gives this for free) is the API's own documentation."""

from __future__ import annotations

import io
import json
import zipfile
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, StreamingResponse

from ..context import AppContext

_STATIC_DIR = Path(__file__).parent / "static"


def create_app(ctx: AppContext) -> FastAPI:
    app = FastAPI(
        title="arstream dashboard",
        description="Live monitoring and recording-download interface for the reference receiver speaking the arstream wire protocol (docs/PROTOCOL.md).",
        version="0.1.0",
    )

    @app.get("/", response_class=HTMLResponse, summary="Dashboard page")
    async def dashboard() -> HTMLResponse:
        return HTMLResponse((_STATIC_DIR / "dashboard.html").read_text(encoding="utf-8"))

    @app.get("/api/sessions", summary="List all sessions (live + past)")
    async def list_sessions() -> JSONResponse:
        return JSONResponse({"sessions": [s.to_dict() for s in ctx.sessions.values()]})

    @app.get("/api/sessions/{session_id}", summary="Detail of a single session")
    async def get_session(session_id: str) -> JSONResponse:
        session = ctx.sessions.get(session_id)
        if session is None:
            return JSONResponse({"error": "session not found"}, status_code=404)
        return JSONResponse(session.to_dict())

    @app.get("/api/sessions/{session_id}/download", summary="Download the session as a .zip (video.h264, frames/imu/poses/points.jsonl, intrinsics.json, meta.json)")
    async def download_session(session_id: str):
        session_dir = ctx.captures_dir / session_id
        if not session_dir.is_dir():
            return JSONResponse({"error": "recording not found"}, status_code=404)

        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
            for path in session_dir.iterdir():
                if path.is_file():
                    zf.write(path, arcname=path.name)
        buf.seek(0)
        return StreamingResponse(
            buf,
            media_type="application/zip",
            headers={"Content-Disposition": f'attachment; filename="arstream-{session_id[:8]}.zip"'},
        )

    @app.websocket("/ws/live")
    async def ws_live(websocket: WebSocket) -> None:
        await websocket.accept()
        queue = ctx.bus.subscribe()
        try:
            while True:
                event = await queue.get()
                await websocket.send_text(json.dumps(event))
        except WebSocketDisconnect:
            pass
        finally:
            ctx.bus.unsubscribe(queue)

    return app
