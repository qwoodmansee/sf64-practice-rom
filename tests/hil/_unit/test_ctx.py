"""Unit tests for ctx.TestContext — mocked client."""
from __future__ import annotations

import pytest
from unittest.mock import MagicMock

from tools.hil.ctx import TestContext, CartWedgedError, LogWaitTimeout
from tools.hil.client import HilClient, SnapshotUnavailable


def make_ctx(tmp_path, mock_client) -> TestContext:
    return TestContext(
        client=mock_client,
        artifacts_dir=tmp_path / "art",
        test_name="t",
        cart_alive_timeout_ms=100,
    )


def test_upload_rom_records_ts(tmp_path):
    rom = tmp_path / "x.z64"
    rom.write_bytes(b"\x00")
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 999}
    client.get_logs.return_value = [{"ts_ms": 1000, "line": "boot"}]
    ctx = make_ctx(tmp_path, client)
    ctx.upload_rom(str(rom))
    assert ctx.upload_complete_ts == 999


def test_upload_rom_raises_cart_wedged_on_silence(tmp_path):
    rom = tmp_path / "x.z64"
    rom.write_bytes(b"\x00")
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 999}
    client.get_logs.return_value = []  # no lines ever
    ctx = make_ctx(tmp_path, client)
    with pytest.raises(CartWedgedError):
        ctx.upload_rom(str(rom))


def test_wait_for_log_returns_match(tmp_path):
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    client.get_logs.return_value = [
        {"ts_ms": 1, "line": "boot"},
        {"ts_ms": 2, "line": "ISViewer init OK"},
    ]
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    match = ctx.wait_for_log(r"ISViewer init OK", timeout_ms=100)
    assert "ISViewer init OK" in match["line"]


def test_wait_for_log_raises_on_timeout(tmp_path):
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    client.get_logs.return_value = [{"ts_ms": 1, "line": "boot"}]
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    with pytest.raises(LogWaitTimeout):
        ctx.wait_for_log(r"never gonna match", timeout_ms=100)


def test_snapshot_writes_file(tmp_path):
    client = MagicMock(spec=HilClient)
    client.get_camera_snapshot.return_value = b"\xff\xd8\xff\xd9"  # tiny JPEG-ish
    ctx = make_ctx(tmp_path, client)
    p = ctx.snapshot()
    assert p.exists()
    assert p.read_bytes() == b"\xff\xd8\xff\xd9"


def test_snapshot_unavailable_returns_none_by_default(tmp_path):
    client = MagicMock(spec=HilClient)
    client.get_camera_snapshot.side_effect = SnapshotUnavailable("502")
    ctx = make_ctx(tmp_path, client)
    assert ctx.snapshot() is None
    # No partial file left behind
    assert list((tmp_path / "art").glob("*.jpg")) == []


def test_snapshot_unavailable_raises_when_required(tmp_path):
    client = MagicMock(spec=HilClient)
    client.get_camera_snapshot.side_effect = SnapshotUnavailable("502")
    ctx = make_ctx(tmp_path, client)
    with pytest.raises(SnapshotUnavailable):
        ctx.snapshot(required=True)


def test_wait_for_log_survives_ring_eviction(tmp_path):
    """Dedupe must key on last-seen ts_ms, not list index.

    Simulate a ring buffer that evicted the early lines between polls:
    querying from the upload anchor only ever returns the first line,
    but querying from the last-seen ts returns the newer match. The old
    index-based dedupe would re-query the anchor forever and time out.
    """
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    calls = {"n": 0}

    def fake_get_logs(since_ms, **kw):
        calls["n"] += 1
        if since_ms < 100:
            return [{"ts_ms": 100, "line": "boot"}]
        return [{"ts_ms": 100, "line": "boot"},
                {"ts_ms": 200, "line": "MATCH ME"}]

    client.get_logs.side_effect = fake_get_logs
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    match = ctx.wait_for_log(r"MATCH ME", timeout_ms=2000)
    assert match["ts_ms"] == 200


def test_assert_log_contains_records_pass_fail(tmp_path):
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    client.get_logs.return_value = [{"ts_ms": 1, "line": "PRACTICE READY"}]
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    ctx.assert_log_contains(r"PRACTICE", "practice ready emitted")
    assert "practice ready emitted" in ctx.passes
    ctx.assert_log_contains(r"NOT THERE", "missing")
    assert any("missing" in f for f in ctx.failures)


def test_assert_log_not_contains_passes_on_clean_log(tmp_path):
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    client.get_logs.return_value = [{"ts_ms": 1, "line": "READY"}]
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    ctx.assert_log_not_contains(r"PANIC", "no panic")
    assert "no panic" in ctx.passes


def test_assert_log_not_contains_fails_on_match(tmp_path):
    client = MagicMock(spec=HilClient)
    client.upload_rom.return_value = {"upload_complete_ts": 0}
    client.get_logs.return_value = [{"ts_ms": 1, "line": "PANIC at 0xdead"}]
    ctx = make_ctx(tmp_path, client)
    rom = tmp_path / "x.z64"; rom.write_bytes(b"\x00")
    ctx.upload_rom(str(rom))
    ctx.assert_log_not_contains(r"PANIC", "no panic")
    assert any("PANIC" in f for f in ctx.failures)


def test_assert_eq_records_pass_fail(tmp_path):
    client = MagicMock(spec=HilClient)
    ctx = make_ctx(tmp_path, client)
    ctx.assert_eq(1, 1, "ones equal")
    ctx.assert_eq(1, 2, "should fail")
    assert "ones equal" in ctx.passes
    assert any("should fail" in f for f in ctx.failures)
