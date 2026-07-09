"""Minimal httpx wrapper around sc64-api.

Reads bearer token from SC64_API_TOKEN env or ~/.sc64-api-token.
Exposes a small surface: get_status, get_health, get_logs, upload_rom,
get_camera_snapshot. Methods raise typed exceptions on transport
failures so the doctor and runner can map them to fix boxes.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx


class HilError(Exception):
    """Base for all HIL client errors."""


class PiUnreachable(HilError):
    """Network-level failure: connect timeout or refused connection."""


class AuthFailed(HilError):
    """401 or 403 from sc64-api."""


class TokenMissing(HilError):
    """No SC64_API_TOKEN env and no ~/.sc64-api-token file."""


class UploadConflict(HilError):
    """409 from /upload — another upload in progress."""


class SnapshotUnavailable(HilError):
    """502 or similar from /camera/snapshot."""


@dataclass(frozen=True)
class ClientConfig:
    host: str
    port: int = 8064
    timeout_s: float = 5.0
    token: str | None = None  # if None, loaded lazily from env/file


def load_token(explicit: str | None = None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("SC64_API_TOKEN")
    if env:
        return env
    path = Path.home() / ".sc64-api-token"
    if path.exists():
        return path.read_text().strip()
    raise TokenMissing(
        "No bearer token found. Set SC64_API_TOKEN or write "
        "~/.sc64-api-token (mode 600)."
    )


class HilClient:
    def __init__(self, cfg: ClientConfig):
        self.cfg = cfg
        self._token: str | None = cfg.token
        self._client = httpx.Client(timeout=cfg.timeout_s)

    def close(self) -> None:
        self._client.close()

    def __enter__(self) -> HilClient:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def _url(self, path: str) -> str:
        return f"http://{self.cfg.host}:{self.cfg.port}{path}"

    def _auth_headers(self) -> dict[str, str]:
        if not self._token:
            self._token = load_token()
        return {"Authorization": f"Bearer {self._token}"}

    def _request(self, method: str, path: str, **kw: Any) -> httpx.Response:
        url = self._url(path)
        try:
            resp = self._client.request(method, url, **kw)
        except httpx.TransportError as e:
            # TransportError is the parent of ConnectError, ConnectTimeout,
            # ReadTimeout, WriteTimeout, PoolTimeout, ReadError, ... — any
            # transport-level failure maps to PiUnreachable.
            raise PiUnreachable(f"Cannot reach {url}: {e}") from e
        if resp.status_code in (401, 403):
            raise AuthFailed(f"sc64-api rejected token: {resp.status_code}")
        return resp

    # Public API ---------------------------------------------------------

    def get_health(self) -> dict[str, Any]:
        # /health is unauthenticated.
        try:
            resp = self._client.get(self._url("/health"))
        except httpx.TransportError as e:
            raise PiUnreachable(f"Cannot reach {self.cfg.host}: {e}") from e
        if resp.status_code != 200:
            # Explicit mapping instead of raise_for_status(): callers (the
            # doctor) handle HilError, not httpx.HTTPStatusError.
            raise HilError(f"/health returned HTTP {resp.status_code}")
        return resp.json()

    def get_status(self) -> dict[str, Any]:
        resp = self._request("GET", "/status", headers=self._auth_headers())
        resp.raise_for_status()
        return resp.json()

    def upload_rom(self, path: str) -> dict[str, Any]:
        """Upload a ROM. Returns the server response including upload_complete_ts."""
        with open(path, "rb") as f:
            files = {"file": (Path(path).name, f, "application/octet-stream")}
            resp = self._request(
                "POST", "/upload",
                headers=self._auth_headers(),
                files=files,
                timeout=180.0,  # uploads can be slow
            )
        if resp.status_code == 409:
            raise UploadConflict("another upload in progress")
        if resp.status_code != 200:
            raise HilError(f"upload failed: {resp.status_code} {resp.text}")
        return resp.json()

    def get_logs(self, since_ms: int, until_ms: int | None = None,
                 limit: int = 10_000) -> list[dict[str, Any]]:
        params: dict[str, int] = {"since": since_ms, "limit": limit}
        if until_ms is not None:
            params["until"] = until_ms
        resp = self._request("GET", "/logs",
                             headers=self._auth_headers(),
                             params=params)
        resp.raise_for_status()
        return resp.json()["lines"]

    def get_camera_snapshot(self) -> bytes:
        resp = self._request("GET", "/camera/snapshot",
                             headers=self._auth_headers(),
                             timeout=10.0)
        if resp.status_code != 200:
            raise SnapshotUnavailable(f"snapshot failed: {resp.status_code}")
        return resp.content
