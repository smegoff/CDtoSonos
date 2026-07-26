from __future__ import annotations

import asyncio
import ipaddress
import json
import os
import tempfile
import time
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import Cookie, Depends, FastAPI, Header, HTTPException, Request, Response, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, HTMLResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from . import __version__, system
from .audio import AudioSupervisor
from .config import ConfigStore
from .security import SecurityStore
from .sonos import SonosController

DATA_DIR = Path(os.environ.get("CDTOSONOS_DATA", "/var/lib/cdtosonos" if os.name != "nt" else "data"))
CONFIG_PATH = Path(os.environ.get("CDTOSONOS_CONFIG", DATA_DIR / "config.json"))
STATIC = Path(__file__).parent / "static"

config = ConfigStore(CONFIG_PATH)
security = SecurityStore(DATA_DIR / "state.db")
audio = AudioSupervisor(config)
sonos = SonosController(config)


@asynccontextmanager
async def lifespan(app: FastAPI):
    automation_task = asyncio.create_task(automation_worker())
    yield
    automation_task.cancel()
    await audio.stop()


app = FastAPI(title="CD to Sonos", version=__version__, lifespan=lifespan)
app.mount("/static", StaticFiles(directory=STATIC), name="static")


async def automation_worker() -> None:
    """Signal/silence state machine, guarded by explicit calibration approval."""
    signal_since = silence_since = None
    sonos_active = False
    failures = 0
    while True:
        try:
            cfg = config.load()
            auto = cfg["audio"]
            if not auto["automation_enabled"]:
                signal_since = silence_since = None
                await asyncio.sleep(1)
                continue
            codec = "flac" if auto["codec_mode"] in {"automatic", "flac"} else "mp3"
            if not audio.metrics["connected"]:
                try:
                    await audio.start(codec)
                    failures = 0
                except Exception as exc:
                    failures += 1
                    if auto["codec_mode"] == "automatic" and failures >= auto["flac_failures_before_fallback"]:
                        codec = "mp3"
                        audio.metrics["fallback_reason"] = f"FLAC pipeline failed {failures} times: {exc}"
                        await audio.start(codec)
                    await asyncio.sleep(min(30, failures * 3))
                    continue
            now = time.monotonic()
            if not audio.metrics["silent"]:
                silence_since = None
                signal_since = signal_since or now
                if not sonos_active and now - signal_since >= auto["start_delay_seconds"]:
                    await sonos.play_stream(audio.metrics["active_codec"] or codec)
                    sonos_active = True
            else:
                signal_since = None
                silence_since = silence_since or now
                if sonos_active and now - silence_since >= auto["silence_timeout_seconds"]:
                    await sonos.stop_and_restore()
                    sonos_active = False
            if sonos_active and auto["stop_on_clipping"] and audio.metrics["clipping"]:
                await sonos.stop_and_restore()
                sonos_active = False
            audio.metrics["sonos_active"] = sonos_active
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            sonos.last_error = str(exc)
        await asyncio.sleep(0.5)


class LoginBody(BaseModel):
    username: str = "admin"
    password: str


class PlaybackBody(BaseModel):
    codec: str | None = None


def _allowed(request: Request) -> bool:
    host = request.client.host if request.client else "127.0.0.1"
    try:
        addr = ipaddress.ip_address(host)
        return any(addr in ipaddress.ip_network(net) for net in config.load()["network"]["allowed_subnets"])
    except ValueError:
        return False


@app.middleware("http")
async def lan_only(request: Request, call_next):
    if not _allowed(request):
        return Response("LAN access only", status_code=403)
    return await call_next(request)


def current_user(session: str | None = Cookie(default=None)) -> tuple[str, str]:
    if not config.load().get("dashboard", {}).get("authentication_required", False):
        return "local-lan", ""
    found = security.session(session)
    if not found:
        raise HTTPException(401, "authentication required")
    return found


def mutation_user(user=Depends(current_user), x_csrf_token: str | None = Header(default=None)) -> str:
    if not config.load().get("dashboard", {}).get("authentication_required", False):
        return user[0]
    if not x_csrf_token or x_csrf_token != user[1]:
        raise HTTPException(403, "invalid CSRF token")
    return user[0]


@app.get("/", response_class=HTMLResponse)
def index():
    return FileResponse(STATIC / "index.html")


@app.get("/api/bootstrap")
def bootstrap(session: str | None = Cookie(default=None)):
    if not config.load().get("dashboard", {}).get("authentication_required", False):
        return {"setup_required": False, "authenticated": True, "csrf": None, "version": __version__, "authentication_required": False}
    found = security.session(session)
    return {"setup_required": not security.has_user(), "authenticated": bool(found), "csrf": found[1] if found else None, "version": __version__, "authentication_required": True}


@app.post("/api/setup")
def setup(body: LoginBody, request: Request):
    try:
        security.create_user(body.username.strip() or "admin", body.password)
        token, csrf = security.login(body.username.strip() or "admin", body.password, request.client.host)
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc
    response = Response(json.dumps({"ok": True, "csrf": csrf}), media_type="application/json")
    response.set_cookie("session", token, httponly=True, samesite="strict", max_age=43200)
    return response


@app.post("/api/login")
def login(body: LoginBody, request: Request):
    try:
        token, csrf = security.login(body.username, body.password, request.client.host)
    except PermissionError as exc:
        raise HTTPException(401, str(exc)) from exc
    response = Response(json.dumps({"ok": True, "csrf": csrf}), media_type="application/json")
    response.set_cookie("session", token, httponly=True, samesite="strict", max_age=43200)
    return response


@app.post("/api/logout")
def logout(session: str | None = Cookie(default=None)):
    security.logout(session)
    response = Response(json.dumps({"ok": True}), media_type="application/json")
    response.delete_cookie("session")
    return response


@app.get("/api/status")
def get_status(user=Depends(current_user)):
    cfg = config.load()
    return {"audio": audio.status(), "sonos": sonos.status(), "system": system.status(), "capture": cfg["capture"],
            "prototype": cfg["capture"]["profile"] == "xc4953",
            "stereo_label": "Stereo" if cfg["capture"]["stereo_verified"] else "Mono source duplicated to stereo"}


@app.get("/api/settings")
def get_settings(user=Depends(current_user)):
    return config.load()


@app.put("/api/settings")
def put_settings(body: dict, username=Depends(mutation_user)):
    try:
        config.save(body)
    except (ValueError, TypeError) as exc:
        raise HTTPException(422, str(exc)) from exc
    security.audit(username, "settings.update")
    return {"ok": True}


@app.get("/api/audio/capabilities")
def capabilities(user=Depends(current_user)):
    return audio.capabilities()


@app.get("/api/sonos/rooms")
async def rooms(user=Depends(current_user)):
    try:
        return await sonos.discover()
    except Exception as exc:
        raise HTTPException(503, f"Sonos discovery failed: {exc}") from exc


@app.post("/api/playback/start")
async def playback_start(body: PlaybackBody, username=Depends(mutation_user)):
    cfg = config.load()["audio"]
    codec = body.codec or ("flac" if cfg["codec_mode"] == "automatic" else cfg["codec_mode"])
    if codec not in {"flac", "mp3"}:
        raise HTTPException(422, "codec must be flac or mp3")
    try:
        await audio.start(codec)
        await asyncio.sleep(1)
        await sonos.play_stream(codec)
    except Exception as exc:
        await audio.stop()
        raise HTTPException(503, str(exc)) from exc
    security.audit(username, "playback.start", codec)
    return {"ok": True, "codec": codec}


@app.post("/api/stream/start")
async def stream_start(body: PlaybackBody, username=Depends(mutation_user)):
    """Publish an Icecast mount without taking control of a Sonos room."""
    cfg = config.load()["audio"]
    codec = body.codec or ("flac" if cfg["codec_mode"] == "automatic" else cfg["codec_mode"])
    if codec not in {"flac", "mp3"}:
        raise HTTPException(422, "codec must be flac or mp3")
    try:
        await audio.start(codec)
    except Exception as exc:
        raise HTTPException(503, str(exc)) from exc
    security.audit(username, "stream.start", codec)
    mount = "cd-lossless.flac" if codec == "flac" else "cd-compatible.mp3"
    host = config.load()["network"]["public_host"]
    return {"ok": True, "codec": codec, "url": f"http://{host}/icecast/{mount}", "direct_url": f"http://{host}:8000/{mount}"}


@app.post("/api/stream/stop")
async def stream_stop(username=Depends(mutation_user)):
    await audio.stop()
    security.audit(username, "stream.stop")
    return {"ok": True}


@app.post("/api/playback/stop")
async def playback_stop(username=Depends(mutation_user)):
    try:
        await sonos.stop_and_restore()
    finally:
        await audio.stop()
    security.audit(username, "playback.stop")
    return {"ok": True}


@app.post("/api/playback/test")
async def playback_test(username=Depends(mutation_user)):
    security.audit(username, "playback.test")
    return {"ok": True, "message": "Use the guided loopback: set output to zero, connect output to mic input, then increase cautiously."}


@app.get("/api/diagnostics")
def diagnostics(user=Depends(current_user)):
    return {"system": system.status(), "audio": audio.status(), "alsa": audio.capabilities(), "sonos": sonos.status(),
            "redaction": "Credentials and sessions excluded"}


@app.post("/api/services/{service}/restart")
def restart_service(service: str, username=Depends(mutation_user)):
    try:
        system.service_action(service, "restart")
    except Exception as exc:
        raise HTTPException(500, str(exc)) from exc
    security.audit(username, "service.restart", service)
    return {"ok": True}


@app.post("/api/system/reboot")
def reboot(username=Depends(mutation_user)):
    security.audit(username, "system.reboot")
    system.reboot()
    return {"ok": True}


@app.get("/api/events")
async def events(user=Depends(current_user)):
    async def stream():
        while True:
            payload = {"audio": audio.status(), "system": system.status()}
            yield f"data: {json.dumps(payload)}\n\n"
            await asyncio.sleep(1)
    return StreamingResponse(stream(), media_type="text/event-stream", headers={"Cache-Control": "no-cache"})


@app.websocket("/ws/audio")
async def websocket_audio(websocket: WebSocket):
    """Decode the active Icecast mount to stereo float PCM for browser monitoring."""
    host = websocket.client.host if websocket.client else "127.0.0.1"
    try:
        addr = ipaddress.ip_address(host)
        if not any(addr in ipaddress.ip_network(net) for net in config.load()["network"]["allowed_subnets"]):
            await websocket.close(code=1008)
            return
    except ValueError:
        await websocket.close(code=1008)
        return
    await websocket.accept()
    port = config.load()["network"]["icecast_port"]
    codec = audio.status().get("active_codec")
    if codec not in {"flac", "mp3"}:
        await websocket.close(code=1013)
        return
    mount = "cd-lossless.flac" if codec == "flac" else "cd-compatible.mp3"
    process = await asyncio.create_subprocess_exec(
        "ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
        "-i", f"http://127.0.0.1:{port}/{mount}",
        "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "2", "-ar", "44100", "pipe:1",
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
    )
    try:
        assert process.stdout
        while chunk := await process.stdout.read(16384):
            await websocket.send_bytes(chunk)
    except (WebSocketDisconnect, RuntimeError):
        pass
    finally:
        if process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), 2)
            except asyncio.TimeoutError:
                process.kill()


def run():
    import uvicorn
    uvicorn.run("cdtosonos.main:app", host="0.0.0.0", port=8080)
