import json

import pytest

from cdtosonos.config import ConfigStore, DEFAULTS


def test_defaults_and_atomic_backup(tmp_path):
    store = ConfigStore(tmp_path / "config.json")
    cfg = store.load()
    assert cfg["capture"]["profile"] == "xc4953"
    cfg["audio"]["threshold_dbfs"] = -35
    store.save(cfg)
    assert store.load()["audio"]["threshold_dbfs"] == -35
    assert (tmp_path / "config.json.bak").exists()


def test_stereo_requires_two_channels(tmp_path):
    store = ConfigStore(tmp_path / "config.json")
    cfg = store.load()
    cfg["capture"]["stereo_verified"] = True
    with pytest.raises(ValueError, match="stereo"):
        store.save(cfg)


def test_invalid_codec_rejected(tmp_path):
    store = ConfigStore(tmp_path / "config.json")
    cfg = store.load()
    cfg["audio"]["codec_mode"] = "aac"
    with pytest.raises(ValueError):
        store.save(cfg)


def test_automation_requires_calibration(tmp_path):
    store = ConfigStore(tmp_path / "config.json")
    cfg = store.load()
    cfg["audio"]["automation_enabled"] = True
    with pytest.raises(ValueError, match="calibration"):
        store.save(cfg)
