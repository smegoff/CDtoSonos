from __future__ import annotations

import asyncio
import json
import math
import re
import shutil
import subprocess
import time
from collections import deque
from pathlib import Path


class AudioSupervisor:
    def __init__(self, config_store):
        self.config_store = config_store
        self.started = time.monotonic()
        self.processes: dict[str, asyncio.subprocess.Process] = {}
        self.metrics = {
            "connected": False, "dbfs": -90.0, "peak_dbfs": -90.0, "noise_floor_dbfs": -90.0,
            "clipping": False, "silent": True, "active_codec": None, "fallback_reason": None,
            "dropouts": 0, "sample_rate": None, "sample_format": None, "channels": None,
        }
        self.history = deque(maxlen=180)
        self.reader_tasks: list[asyncio.Task] = []

    def capabilities(self) -> dict:
        cfg = self.config_store.load()["capture"]
        result = {"configured": cfg, "devices": [], "raw": ""}
        if not shutil.which("arecord"):
            result["raw"] = "arecord is not installed"
            return result
        proc = subprocess.run(["arecord", "-l"], capture_output=True, text=True, timeout=5)
        result["raw"] = (proc.stdout + proc.stderr)[:10000]
        for line in proc.stdout.splitlines():
            match = re.search(r"card (\d+): ([^\[]+).*device (\d+):", line)
            if match:
                result["devices"].append({"card": int(match.group(1)), "name": match.group(2).strip(), "device": int(match.group(3))})
        return result

    def ffmpeg_command(self, codec: str) -> list[str]:
        cfg = self.config_store.load()
        cap, net = cfg["capture"], cfg["network"]
        channels = int(cap["channels"])
        base = ["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "info", "-f", "alsa",
                "-ac", str(channels), "-ar", str(cap["sample_rate"]), "-i", cap["alsa_device"]]
        filters = ["ebur128=peak=true"]
        if channels == 1:
            filters.insert(0, "pan=stereo|c0=c0|c1=c0")
        base += ["-af", ",".join(filters)]
        mount = "cd-lossless.flac" if codec == "flac" else "cd-compatible.mp3"
        password = Path("/etc/cdtosonos/icecast-source-password").read_text().strip() if Path("/etc/cdtosonos/icecast-source-password").exists() else "change-me"
        if codec == "flac":
            base += ["-ac", "2", "-ar", "44100", "-c:a", "flac", "-compression_level", "5", "-content_type", "audio/flac", "-f", "flac"]
        else:
            base += ["-ac", "2", "-ar", "44100", "-c:a", "libmp3lame", "-b:a", "320k", "-content_type", "audio/mpeg", "-f", "mp3"]
        return base + [f"icecast://source:{password}@127.0.0.1:{net['icecast_port']}/{mount}"]

    async def start(self, codec: str) -> None:
        await self.stop()
        if not shutil.which("ffmpeg"):
            raise RuntimeError("ffmpeg is not installed")
        proc = await asyncio.create_subprocess_exec(*self.ffmpeg_command(codec), stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
        self.processes[codec] = proc
        self.reader_tasks.append(asyncio.create_task(self._read_levels(proc)))
        self.metrics.update(connected=True, active_codec=codec, fallback_reason=None)

    async def _read_levels(self, proc: asyncio.subprocess.Process) -> None:
        pattern = re.compile(r"\bM:\s*(-?inf|[-\d.]+)")
        assert proc.stderr
        while line := await proc.stderr.readline():
            text = line.decode(errors="replace")
            match = pattern.search(text)
            if match:
                value = -90.0 if match.group(1) == "-inf" else float(match.group(1))
                self.update_level(max(-90.0, value))
        if proc.returncode not in (None, 0):
            self.metrics["dropouts"] += 1
            self.metrics["connected"] = False

    async def stop(self) -> None:
        for proc in self.processes.values():
            if proc.returncode is None:
                proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), 3)
                except asyncio.TimeoutError:
                    proc.kill()
        self.processes.clear()
        for task in self.reader_tasks:
            task.cancel()
        self.reader_tasks.clear()
        self.metrics.update(connected=False, active_codec=None)

    def update_level(self, dbfs: float) -> None:
        cfg = self.config_store.load()["audio"]
        self.metrics["dbfs"] = round(dbfs, 1)
        self.metrics["peak_dbfs"] = max(self.metrics["peak_dbfs"] - 0.4, dbfs)
        self.metrics["clipping"] = dbfs >= cfg["clip_threshold_dbfs"]
        self.metrics["silent"] = dbfs < cfg["threshold_dbfs"]
        self.history.append({"time": time.time(), "dbfs": dbfs})

    def status(self) -> dict:
        result = dict(self.metrics)
        result["uptime_seconds"] = int(time.monotonic() - self.started)
        result["history"] = list(self.history)[-60:]
        return result
