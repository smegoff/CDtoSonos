from __future__ import annotations

import hashlib
import secrets
import sqlite3
import time
from pathlib import Path

from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatchError


class SecurityStore:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.executescript("""
          CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL);
          CREATE TABLE IF NOT EXISTS sessions (token_hash TEXT PRIMARY KEY, username TEXT, csrf TEXT, expires REAL);
          CREATE TABLE IF NOT EXISTS attempts (address TEXT, attempted REAL);
          CREATE TABLE IF NOT EXISTS audit (created REAL, username TEXT, action TEXT, detail TEXT);
        """)
        self.db.commit()
        self.hasher = PasswordHasher()

    @staticmethod
    def _digest(token: str) -> str:
        return hashlib.sha256(token.encode()).hexdigest()

    def has_user(self) -> bool:
        return self.db.execute("SELECT 1 FROM users LIMIT 1").fetchone() is not None

    def create_user(self, username: str, password: str) -> None:
        if self.has_user():
            raise ValueError("initial administrator already exists")
        if len(password) < 12:
            raise ValueError("password must contain at least 12 characters")
        self.db.execute("INSERT INTO users VALUES (?, ?)", (username, self.hasher.hash(password)))
        self.db.commit()

    def login(self, username: str, password: str, address: str) -> tuple[str, str]:
        cutoff = time.time() - 900
        self.db.execute("DELETE FROM attempts WHERE attempted < ?", (cutoff,))
        count = self.db.execute("SELECT COUNT(*) FROM attempts WHERE address=?", (address,)).fetchone()[0]
        if count >= 8:
            raise PermissionError("too many login attempts; retry later")
        row = self.db.execute("SELECT password_hash FROM users WHERE username=?", (username,)).fetchone()
        try:
            if not row:
                raise VerifyMismatchError
            self.hasher.verify(row[0], password)
        except VerifyMismatchError:
            self.db.execute("INSERT INTO attempts VALUES (?, ?)", (address, time.time()))
            self.db.commit()
            raise PermissionError("invalid credentials")
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(24)
        self.db.execute("DELETE FROM attempts WHERE address=?", (address,))
        self.db.execute("INSERT INTO sessions VALUES (?, ?, ?, ?)", (self._digest(token), username, csrf, time.time() + 43200))
        self.db.commit()
        return token, csrf

    def session(self, token: str | None) -> tuple[str, str] | None:
        if not token:
            return None
        row = self.db.execute("SELECT username, csrf, expires FROM sessions WHERE token_hash=?", (self._digest(token),)).fetchone()
        if not row or row[2] < time.time():
            return None
        return row[0], row[1]

    def logout(self, token: str | None) -> None:
        if token:
            self.db.execute("DELETE FROM sessions WHERE token_hash=?", (self._digest(token),))
            self.db.commit()

    def audit(self, username: str, action: str, detail: str = "") -> None:
        self.db.execute("INSERT INTO audit VALUES (?, ?, ?, ?)", (time.time(), username, action, detail[:500]))
        self.db.commit()
