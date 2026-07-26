import importlib


def test_dashboard_authentication_disabled_by_default():
    from cdtosonos.config import DEFAULTS
    assert DEFAULTS["dashboard"]["authentication_required"] is False
