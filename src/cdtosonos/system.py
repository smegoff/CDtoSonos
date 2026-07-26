from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path

import psutil


ALLOWED_SERVICES = {"cdtosonos", "icecast2"}


def status() -> dict:
    disk = psutil.disk_usage("/")
    temp = None
    thermal = Path("/sys/class/thermal/thermal_zone0/temp")
    if thermal.exists():
        temp = round(int(thermal.read_text()) / 1000, 1)
    return {"hostname": platform.node(), "platform": platform.platform(), "cpu_percent": psutil.cpu_percent(),
            "memory_percent": psutil.virtual_memory().percent, "disk_percent": disk.percent, "temperature_c": temp,
            "ffmpeg": shutil.which("ffmpeg") is not None, "arecord": shutil.which("arecord") is not None,
            "icecast": shutil.which("icecast2") is not None}


def service_action(service: str, action: str) -> None:
    if service not in ALLOWED_SERVICES or action != "restart":
        raise ValueError("service action not allowed")
    subprocess.run(["sudo", "systemctl", action, service], check=True, timeout=20)


def reboot() -> None:
    subprocess.Popen(["sudo", "systemctl", "reboot"])
