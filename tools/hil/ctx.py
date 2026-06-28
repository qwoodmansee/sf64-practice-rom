"""TestContext for HIL tests.

Test files define `def run(ctx): ...` and call methods like:
    ctx.upload_rom("build/foo.z64")
    ctx.wait_for_log(r"ISViewer init OK")
    shot = ctx.snapshot()
    ctx.assert_log_contains(r"PRACTICE READY")

ctx manages: upload anchor timestamp, polling for log matches,
artifact paths, assertion bookkeeping. The runner owns the cart-wedge
banner + JUnit emission (see tools/hil/banner.py, tools/hil/junit.py).
"""
from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from tools.hil.client import HilClient, HilError, SnapshotUnavailable


class LogWaitTimeout(HilError):
    pass


class CartWedgedError(HilError):
    """upload_rom() saw no IS-Viewer line within cart_alive_timeout_ms."""


@dataclass
class TestContext:
    __test__ = False  # not a pytest class (silences PytestCollectionWarning)

    client: HilClient
    artifacts_dir: Path
    test_name: str
    cart_alive_timeout_ms: int = 10_000

    upload_complete_ts: int | None = None
    _snapshot_seq: int = 0
    passes: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)

    def upload_rom(self, rom_path: str) -> None:
        """Upload + wait for first IS-Viewer line to prove the cart is alive."""
        result = self.client.upload_rom(rom_path)
        self.upload_complete_ts = int(result["upload_complete_ts"])

        # Cart-alive check: poll /logs until ANY line appears since upload.
        deadline = time.time() * 1000 + self.cart_alive_timeout_ms
        while time.time() * 1000 < deadline:
            lines = self.client.get_logs(since_ms=self.upload_complete_ts)
            if lines:
                return
            time.sleep(0.1)
        raise CartWedgedError(
            f"No IS-Viewer line within {self.cart_alive_timeout_ms}ms after upload"
        )

    def wait_for_log(self, pattern: str, timeout_ms: int = 10_000) -> dict[str, Any]:
        """Poll /logs since upload_complete_ts. Return first matching line.

        Dedupe is keyed on the last-seen line timestamp (passed back as
        `since`), not on list index: the Pi's ring buffer may evict early
        lines between polls, so positional indexing into successive
        responses would silently skip or re-scan the wrong window.
        Re-querying from the last-seen ts may re-return same-ts lines;
        re-checking them against the regex is harmless.
        """
        if self.upload_complete_ts is None:
            raise HilError("wait_for_log called before upload_rom")
        regex = re.compile(pattern)
        deadline = time.time() * 1000 + timeout_ms
        since = self.upload_complete_ts
        while time.time() * 1000 < deadline:
            lines = self.client.get_logs(since_ms=since)
            for entry in lines:
                if regex.search(entry["line"]):
                    return entry
            if lines:
                since = max(since, lines[-1]["ts_ms"])
            time.sleep(0.1)
        raise LogWaitTimeout(
            f"pattern {pattern!r} not seen within {timeout_ms}ms"
        )

    def advance_seconds(self, seconds: float) -> None:
        """Wall-clock wait. Test authors must budget slack for hardware boot variance."""
        time.sleep(seconds)

    def snapshot(self, name: str | None = None, required: bool = False) -> Path | None:
        """GET /camera/snapshot, save to artifacts dir, return the path.

        Per spec §6, a snapshot is best-effort by default: if the camera
        endpoint is unavailable, return None so the test can continue
        (and assert on the None if it cares). Pass required=True to make
        an unavailable camera raise SnapshotUnavailable instead.
        """
        self._snapshot_seq += 1
        suffix = f"{self._snapshot_seq}" if name is None else name
        path = self.artifacts_dir / f"{self.test_name}-{suffix}.jpg"
        try:
            data = self.client.get_camera_snapshot()
        except SnapshotUnavailable:
            if required:
                raise
            return None
        path.write_bytes(data)
        return path

    def assert_log_contains(self, pattern: str, msg: str = "") -> None:
        """Re-query /logs and search for pattern. Record pass/fail."""
        if self.upload_complete_ts is None:
            self.failures.append("assert_log_contains called before upload_rom")
            return
        lines = self.client.get_logs(since_ms=self.upload_complete_ts)
        regex = re.compile(pattern)
        if any(regex.search(l["line"]) for l in lines):
            self.passes.append(msg or pattern)
        else:
            self.failures.append(f"{msg or pattern}: no match in {len(lines)} lines")

    def assert_log_not_contains(self, pattern: str, msg: str = "") -> None:
        """Re-query /logs and search for pattern. Record pass if NO match."""
        if self.upload_complete_ts is None:
            self.failures.append("assert_log_not_contains called before upload_rom")
            return
        lines = self.client.get_logs(since_ms=self.upload_complete_ts)
        regex = re.compile(pattern)
        matches = [l for l in lines if regex.search(l["line"])]
        if not matches:
            self.passes.append(msg or f"no match for {pattern!r}")
        else:
            sample = matches[0]["line"]
            self.failures.append(
                f"{msg or pattern}: unexpected match in {len(matches)} lines, "
                f"first: {sample!r}"
            )

    def assert_eq(self, actual: Any, expected: Any, msg: str = "") -> None:
        if actual == expected:
            self.passes.append(msg)
        else:
            self.failures.append(f"{msg}: expected {expected!r}, got {actual!r}")
            print(f"  FAIL: {msg}: expected {expected!r}, got {actual!r}")

    def assert_neq(self, actual: Any, expected: Any, msg: str = "") -> None:
        if actual != expected:
            self.passes.append(msg)
        else:
            self.failures.append(f"{msg}: expected NOT {expected!r}")
            print(f"  FAIL: {msg}: expected NOT {expected!r}")

    def assert_true(self, cond: bool, msg: str = "") -> None:
        if cond:
            self.passes.append(msg)
        else:
            self.failures.append(msg)
            print(f"  FAIL: {msg}")
