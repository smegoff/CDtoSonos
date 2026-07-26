from __future__ import annotations

import json
import os
import tempfile
from copy import deepcopy
from pathlib import Path
from threading import RLock

DEFAULTS = {
    "capture": {
        "profile": "xc4953",
        "alsa_device": "plughw:CARD=Device,DEV=0",
        "sample_rate": 44100,
        "sample_format": "s16le",
        "channels": 1,
        "stereo_verified": False,
        "input_type": "microphone",
        "channel_mapping": "mono_duplicate",
        "resampling": "only_when_required",
        "gain_notes": "Do not connect fixed-level RCA directly",
        "hardware_warning": "XC4953 prototype — mono microphone input",
    },
    "audio": {
        "automation_enabled": False,
        "calibration_approved": False,
        "threshold_dbfs": -42.0,
        "start_delay_seconds": 1.5,
        "silence_timeout_seconds": 30.0,
        "clip_threshold_dbfs": -0.5,
        "clip_stop_seconds": 3.0,
        "stop_on_clipping": False,
        "codec_mode": "automatic",
        "flac_failures_before_fallback": 2,
    },
    "sonos": {
        "coordinator_uid": "",
        "preserve_volume": True,
        "restore_previous": True,
    },
    "network": {
        "public_host": "cdtosonos.local",
        "icecast_port": 8000,
        "allowed_subnets": ["127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"],
    },
    "dashboard": {"authentication_required": False},
}


class ConfigStore:
    def __init__(self, path: Path):
        self.path = path
        self.lock = RLock()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.path.exists():
            self.save(DEFAULTS)

    def load(self) -> dict:
        with self.lock:
            data = deepcopy(DEFAULTS)
            with self.path.open(encoding="utf-8") as fh:
                supplied = json.load(fh)
            self._merge(data, supplied)
            return data

    def save(self, data: dict) -> None:
        self.validate(data)
        with self.lock:
            if self.path.exists():
                backup = self.path.with_suffix(self.path.suffix + ".bak")
                backup.write_bytes(self.path.read_bytes())
            fd, name = tempfile.mkstemp(prefix="config-", suffix=".json", dir=self.path.parent)
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as fh:
                    json.dump(data, fh, indent=2)
                    fh.flush()
                    os.fsync(fh.fileno())
                os.replace(name, self.path)
            finally:
                if os.path.exists(name):
                    os.unlink(name)

    @staticmethod
    def _merge(target: dict, supplied: dict) -> None:
        for key, value in supplied.items():
            if isinstance(value, dict) and isinstance(target.get(key), dict):
                ConfigStore._merge(target[key], value)
            else:
                target[key] = value

    @staticmethod
    def validate(data: dict) -> None:
        audio = data.get("audio", {})
        if not -90 <= float(audio.get("threshold_dbfs", -42)) <= -3:
            raise ValueError("threshold_dbfs must be between -90 and -3")
        if not 0.25 <= float(audio.get("start_delay_seconds", 1.5)) <= 30:
            raise ValueError("start delay must be between 0.25 and 30 seconds")
        if not 2 <= float(audio.get("silence_timeout_seconds", 30)) <= 600:
            raise ValueError("silence timeout must be between 2 and 600 seconds")
        if audio.get("codec_mode") not in {"automatic", "flac", "mp3"}:
            raise ValueError("codec_mode must be automatic, flac, or mp3")
        if audio.get("automation_enabled") and not audio.get("calibration_approved"):
            raise ValueError("automation requires an approved input calibration")
        capture = data.get("capture", {})
        if int(capture.get("channels", 1)) not in {1, 2}:
            raise ValueError("capture channels must be 1 or 2")
        if capture.get("stereo_verified") and int(capture.get("channels", 1)) != 2:
            raise ValueError("stereo cannot be verified on a mono capture profile")
