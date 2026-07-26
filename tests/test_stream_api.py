def test_stream_mount_names_are_stable():
    from cdtosonos.config import DEFAULTS
    assert DEFAULTS["network"]["icecast_port"] == 8000
