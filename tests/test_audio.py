from pathlib import Path

from cdtosonos.audio import AudioSupervisor
from cdtosonos.config import ConfigStore


def test_mono_is_duplicated_and_flac_is_native(tmp_path, monkeypatch):
    store = ConfigStore(tmp_path / "config.json")
    audio = AudioSupervisor(store)
    command = audio.ffmpeg_command("flac")
    joined = " ".join(command)
    assert "pan=stereo|c0=c0|c1=c0" in joined
    assert "-ar 44100" in joined
    assert "cd-lossless.flac" in joined


def test_stereo_profile_does_not_duplicate(tmp_path):
    store = ConfigStore(tmp_path / "config.json")
    cfg = store.load()
    cfg["capture"].update(channels=2, stereo_verified=True)
    store.save(cfg)
    joined = " ".join(AudioSupervisor(store).ffmpeg_command("mp3"))
    assert "pan=stereo" not in joined
    assert "320k" in joined
