"""Unit tests for hil.client — mocked httpx, no Pi required."""
from __future__ import annotations

import pytest
from unittest.mock import patch, MagicMock

import httpx

from tools.hil.client import (
    ClientConfig,
    HilClient,
    HilError,
    PiUnreachable,
    AuthFailed,
    TokenMissing,
    UploadConflict,
    load_token,
)


class TestLoadToken:
    def test_explicit_wins(self):
        assert load_token("explicit") == "explicit"

    def test_env(self, monkeypatch):
        monkeypatch.setenv("SC64_API_TOKEN", "from-env")
        assert load_token() == "from-env"

    def test_file_fallback(self, monkeypatch, tmp_path):
        monkeypatch.delenv("SC64_API_TOKEN", raising=False)
        token_path = tmp_path / ".sc64-api-token"
        token_path.write_text("from-file\n")
        monkeypatch.setenv("HOME", str(tmp_path))
        assert load_token() == "from-file"

    def test_missing_everywhere(self, monkeypatch, tmp_path):
        monkeypatch.delenv("SC64_API_TOKEN", raising=False)
        monkeypatch.setenv("HOME", str(tmp_path))
        with pytest.raises(TokenMissing):
            load_token()


class TestHilClient:
    def test_pi_unreachable_raises(self):
        cfg = ClientConfig(host="nonexistent.invalid", token="t")
        with HilClient(cfg) as c:
            with patch.object(c._client, "request", side_effect=httpx.ConnectError("nope")):
                with pytest.raises(PiUnreachable):
                    c.get_status()

    def test_403_raises_auth_failed(self):
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=403)
            with patch.object(c._client, "request", return_value=mock_resp):
                with pytest.raises(AuthFailed):
                    c.get_status()

    def test_health_returns_dict(self):
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=200)
            mock_resp.json.return_value = {"ok": True}
            mock_resp.raise_for_status.return_value = None
            with patch.object(c._client, "get", return_value=mock_resp):
                assert c.get_health() == {"ok": True}

    def test_read_timeout_raises_pi_unreachable(self):
        # ReadTimeout is a TransportError but NOT a ConnectError/
        # ConnectTimeout — regression for the incomplete exception map.
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            with patch.object(c._client, "request", side_effect=httpx.ReadTimeout("slow")):
                with pytest.raises(PiUnreachable):
                    c.get_status()

    def test_health_read_timeout_raises_pi_unreachable(self):
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            with patch.object(c._client, "get", side_effect=httpx.ReadTimeout("slow")):
                with pytest.raises(PiUnreachable):
                    c.get_health()

    def test_health_non_200_raises_hil_error(self):
        # Explicit mapping — a 500 must surface as HilError, not leak
        # httpx.HTTPStatusError through the doctor.
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=500)
            with patch.object(c._client, "get", return_value=mock_resp):
                with pytest.raises(HilError):
                    c.get_health()


class TestUploadRom:
    def test_409_raises_upload_conflict(self, tmp_path):
        rom = tmp_path / "x.z64"
        rom.write_bytes(b"\x00" * 1024)
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=409, text="busy")
            with patch.object(c._client, "request", return_value=mock_resp):
                with pytest.raises(UploadConflict):
                    c.upload_rom(str(rom))

    def test_200_returns_dict(self, tmp_path):
        rom = tmp_path / "x.z64"
        rom.write_bytes(b"\x00" * 1024)
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=200)
            mock_resp.json.return_value = {"ok": True, "upload_complete_ts": 12345}
            with patch.object(c._client, "request", return_value=mock_resp):
                r = c.upload_rom(str(rom))
                assert r["upload_complete_ts"] == 12345


class TestGetLogs:
    def test_returns_line_list(self):
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=200)
            mock_resp.json.return_value = {"lines": [
                {"ts_ms": 1, "line": "a"},
                {"ts_ms": 2, "line": "b"},
            ]}
            mock_resp.raise_for_status.return_value = None
            with patch.object(c._client, "request", return_value=mock_resp):
                lines = c.get_logs(since_ms=0)
                assert len(lines) == 2
                assert lines[0]["line"] == "a"
