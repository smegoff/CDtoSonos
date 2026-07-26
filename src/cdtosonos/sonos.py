from __future__ import annotations

import asyncio
from dataclasses import asdict, dataclass


@dataclass
class SavedState:
    uri: str = ""
    metadata: str = ""
    state: str = "STOPPED"
    volume: int = 0
    mute: bool = False


class SonosController:
    def __init__(self, config_store):
        self.config_store = config_store
        self.saved: SavedState | None = None
        self.last_error: str | None = None

    async def discover(self) -> list[dict]:
        return await asyncio.to_thread(self._discover)

    @staticmethod
    def _discover() -> list[dict]:
        import soco
        devices = soco.discover(timeout=4) or []
        rooms = {}
        for device in devices:
            coordinator = device.group.coordinator
            rooms[coordinator.uid] = {"uid": coordinator.uid, "name": coordinator.player_name, "ip": coordinator.ip_address,
                                               "members": [member.player_name for member in coordinator.group.members]}
        return sorted(rooms.values(), key=lambda x: x["name"])

    async def _target(self):
        uid = self.config_store.load()["sonos"]["coordinator_uid"]
        rooms = await asyncio.to_thread(lambda: __import__("soco").discover(timeout=4) or [])
        for device in rooms:
            if device.group.coordinator.uid == uid:
                return device.group.coordinator
        raise RuntimeError("configured Sonos coordinator is unavailable")

    async def play_stream(self, codec: str) -> None:
        target = await self._target()
        await asyncio.to_thread(self._save_and_play, target, codec)

    def _save_and_play(self, target, codec: str) -> None:
        info = target.get_current_transport_info()
        media = target.avTransport.GetMediaInfo([("InstanceID", 0)])
        self.saved = SavedState(uri=media.get("CurrentURI", ""), metadata=media.get("CurrentURIMetaData", ""),
                                state=info.get("current_transport_state", "STOPPED"), volume=target.volume, mute=target.mute)
        cfg = self.config_store.load()
        mount = "cd-lossless.flac" if codec == "flac" else "cd-compatible.mp3"
        url = f"http://{cfg['network']['public_host']}:{cfg['network']['icecast_port']}/{mount}"
        target.play_uri(url, title="CD Input")

    async def stop_and_restore(self) -> None:
        target = await self._target()
        await asyncio.to_thread(self._restore, target)

    def _restore(self, target) -> None:
        target.stop()
        cfg = self.config_store.load()["sonos"]
        if not self.saved or not cfg["restore_previous"]:
            return
        if cfg["preserve_volume"]:
            target.volume, target.mute = self.saved.volume, self.saved.mute
        if self.saved.uri:
            target.avTransport.SetAVTransportURI([("InstanceID", 0), ("CurrentURI", self.saved.uri), ("CurrentURIMetaData", self.saved.metadata)])
            if self.saved.state == "PLAYING":
                target.play()

    def status(self) -> dict:
        return {"saved_state": asdict(self.saved) if self.saved else None, "last_error": self.last_error}
