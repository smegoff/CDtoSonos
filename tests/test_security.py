import pytest

from cdtosonos.security import SecurityStore


def test_password_session_and_csrf(tmp_path):
    sec = SecurityStore(tmp_path / "state.db")
    with pytest.raises(ValueError):
        sec.create_user("admin", "short")
    sec.create_user("admin", "a sufficiently long password")
    token, csrf = sec.login("admin", "a sufficiently long password", "127.0.0.1")
    assert sec.session(token) == ("admin", csrf)
    sec.logout(token)
    assert sec.session(token) is None


def test_wrong_password_rejected(tmp_path):
    sec = SecurityStore(tmp_path / "state.db")
    sec.create_user("admin", "a sufficiently long password")
    with pytest.raises(PermissionError):
        sec.login("admin", "wrong", "127.0.0.1")
