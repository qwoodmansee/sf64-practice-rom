"""HIL doctor — preflight probes with actionable fix boxes.

Probes are phased:
  Phase A (1-3): network + SSH + sc64-api responding.
  Phase B (4-10): token, services, hardware via /status.
  Warn-only (W1-W2): degraded-but-runnable states.

A failure short-circuits dependent probes. Each ❌ row is paired with a
Fix box that contains the exact next command the user should run.

This module exposes two entry points:
  - probe_all(host, ...) -> ProbeReport  : run all probes
  - render_report(report) -> str          : pretty-print for terminal output
"""
from __future__ import annotations

import socket
import subprocess
import time
from dataclasses import dataclass, field
from typing import Any

from tools.hil.client import (
    ClientConfig,
    HilClient,
    HilError,
    PiUnreachable,
    AuthFailed,
    TokenMissing,
    load_token,
)


@dataclass
class ProbeResult:
    number: str           # "1", "2", ..., "W1", "W2"
    name: str
    passed: bool
    warn_only: bool = False
    fix: str | None = None     # human-readable fix box content
    detail: str | None = None  # extra debug info on pass or fail


@dataclass
class ProbeReport:
    host: str
    results: list[ProbeResult] = field(default_factory=list)

    @property
    def blocking_failed(self) -> list[ProbeResult]:
        return [r for r in self.results if not r.passed and not r.warn_only]

    @property
    def all_blocking_passed(self) -> bool:
        return len(self.blocking_failed) == 0


# -- Phase A: local + low-level Pi connectivity ----------------------------

def probe_network(host: str, http_port: int = 8064, ssh_port: int = 22) -> ProbeResult:
    failures = []
    for port in (ssh_port, http_port):
        try:
            with socket.create_connection((host, port), timeout=3):
                pass
        except OSError as e:
            failures.append(f"{host}:{port} ({e})")
    if not failures:
        return ProbeResult("1", "Network reachable", True)
    return ProbeResult(
        "1", "Network reachable", False,
        fix=(
            f"Cannot reach {host} (failed: {', '.join(failures)}).\n\n"
            f"Most common cause: the Pi is powered off or not on the LAN.\n\n"
            f"Do this:\n"
            f"  1. Plug the Pi into power and Ethernet.\n"
            f"  2. Wait ~30s for boot + mDNS.\n"
            f"  3. Re-run this doctor command.\n\n"
            f"On macOS, confirm mDNS:\n"
            f"  dscacheutil -q host -a name {host}"
        ),
    )


def probe_ssh(host: str) -> ProbeResult:
    try:
        proc = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3",
             f"root@{host}", "true"],
            capture_output=True, text=True, timeout=10,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        return ProbeResult(
            "2", "SSH works without password", False,
            fix=f"ssh subprocess failed: {e}",
        )
    if proc.returncode == 0:
        return ProbeResult("2", "SSH works without password", True)
    return ProbeResult(
        "2", "SSH works without password", False,
        fix=(
            f"`ssh root@{host}` requires a password (or refused).\n\n"
            f"The cold-start path bakes your SSH key into the SD image.\n"
            f"Re-flash with your key:\n\n"
            f"  pi-sc64/scripts/build-sd-image.sh \\\n"
            f"    --ssh-key ~/.ssh/id_ed25519.pub\n\n"
            f"(See tests/hil/SETUP.md §2.)\n\n"
            f"There is no default root password — this is intentional."
        ),
        detail=proc.stderr.strip(),
    )


def probe_sc64api_responding(host: str) -> ProbeResult:
    cfg = ClientConfig(host=host, token="dummy")
    try:
        with HilClient(cfg) as c:
            health = c.get_health()
    except HilError as e:
        # HilError covers PiUnreachable (transport) and the explicit
        # non-200 mapping from get_health.
        return ProbeResult(
            "3", "sc64-api responding", False,
            fix=(
                f"GET http://{host}:8064/health failed: {e}\n\n"
                f"Check the service on the Pi:\n"
                f"  ssh root@{host} systemctl status sc64-api\n"
                f"  ssh root@{host} journalctl -u sc64-api -n 50"
            ),
        )
    if health.get("ok"):
        return ProbeResult("3", "sc64-api responding", True)
    return ProbeResult(
        "3", "sc64-api responding", False,
        fix=f"/health returned unexpected payload: {health}",
    )


# -- Phase B: token, services, hardware (require /status) ------------------

def probe_token_local(host: str = "sc64pi.local") -> ProbeResult:
    try:
        token = load_token()
    except TokenMissing:
        return ProbeResult(
            "4", "Bearer token configured locally", False,
            fix=(
                "No bearer token found on this machine.\n\n"
                "Run:\n"
                f"  pi-sc64/scripts/bootstrap-pi.sh token-only {host}\n\n"
                "This provisions a fresh token on the Pi (correct ownership\n"
                "+ mode), saves it to ~/.sc64-api-token locally (mode 600),\n"
                "and prints it once for your records."
            ),
        )
    if not token:
        return ProbeResult("4", "Bearer token configured locally", False, fix="Token file exists but is empty.")
    return ProbeResult("4", "Bearer token configured locally", True)


def probe_status(host: str) -> tuple[ProbeResult, dict[str, Any] | None]:
    """Fetch /status. Returns (probe 5 result, parsed status JSON or None)."""
    cfg = ClientConfig(host=host)
    try:
        with HilClient(cfg) as c:
            status = c.get_status()
    except AuthFailed:
        return (
            ProbeResult(
                "5", "Token accepted by Pi", False,
                fix=(
                    "Pi rejected the bearer token (401/403).\n\n"
                    "Regenerate via:\n"
                    f"  pi-sc64/scripts/bootstrap-pi.sh token-only {host}"
                ),
            ),
            None,
        )
    except PiUnreachable as e:
        return (
            ProbeResult(
                "5", "Token accepted by Pi", False,
                fix=f"Transport error: {e}",
            ),
            None,
        )
    except TokenMissing:
        return (
            ProbeResult("5", "Token accepted by Pi", False, fix="Token missing locally."),
            None,
        )
    return ProbeResult("5", "Token accepted by Pi", True), status


def probe_token_file_mode(status: dict[str, Any]) -> ProbeResult:
    tf = status.get("tokens_file", {})
    if not tf.get("exists"):
        return ProbeResult(
            "6", "Token file mode/owner on Pi correct", False,
            fix="/etc/sc64-api/tokens does not exist on Pi.",
        )
    ok = (tf.get("owner") == "root" and tf.get("group") == "sc64api" and tf.get("mode") == "0640")
    if ok:
        return ProbeResult("6", "Token file mode/owner on Pi correct", True)
    return ProbeResult(
        "6", "Token file mode/owner on Pi correct", False,
        fix=(
            f"/etc/sc64-api/tokens is {tf.get('owner')}:{tf.get('group')} "
            f"mode {tf.get('mode')} (expected root:sc64api 0640).\n\n"
            f"Fix:\n"
            f"  ssh root@<host> 'chown root:sc64api /etc/sc64-api/tokens && \\\n"
            f"                    chmod 640 /etc/sc64-api/tokens'"
        ),
    )


def probe_ftdi_present(status: dict[str, Any]) -> ProbeResult:
    cart = status.get("cart", {})
    if cart.get("ftdi_present"):
        return ProbeResult("7", "Cart FTDI device present at USB", True,
                           detail=f"serial: {cart.get('ftdi_serial', '?')}")
    return ProbeResult(
        "7", "Cart FTDI device present at USB", False,
        fix=(
            "SC64 USB device not detected on the Pi.\n\n"
            "Plug the SC64 USB cable into the Pi. Verify:\n"
            "  ssh root@<host> lsusb | grep 0403"
        ),
    )


def probe_deployer_can_open(status: dict[str, Any]) -> ProbeResult:
    d = status.get("deployer", {})
    if d.get("probe_ok"):
        # "via" tells us whether the Pi confirmed openability with a standalone
        # `info` probe or inferred it from the live DebugConsumer holding the
        # single client slot (server.rs:163). Both are valid proof.
        via = d.get("probe_via")
        detail = "via debug consumer (holds the slot)" if via == "debug_consumer" else None
        return ProbeResult("8", "sc64-server can open FTDI device", True, detail=detail)
    return ProbeResult(
        "8", "sc64-server can open FTDI device", False,
        fix=(
            f"sc64-server reports it cannot open the cart: "
            f"{d.get('probe_last_error', 'unknown')}\n\n"
            f"If the debug consumer (probe 9) is healthy this should not fail —\n"
            f"the Pi infers openability from the running consumer. A failure\n"
            f"here means the slot is free AND `info` could not open the cart.\n\n"
            f"Most likely cause: udev rule did not apply OR the sc64 group\n"
            f"is misconfigured. Fix:\n"
            f"  ssh root@<host> 'systemctl restart sc64-server && \\\n"
            f"                    journalctl -u sc64-server -n 50'"
        ),
    )


def probe_debug_consumer(status: dict[str, Any]) -> ProbeResult:
    # The Chunk-2 /status stub omits the "debug_consumer" key entirely;
    # key ABSENCE (not a value pattern) marks the stub. A present key with
    # running=False is a reachable production state (consumer crashed
    # before recording any failure) and must FAIL.
    if "debug_consumer" not in status:
        return ProbeResult(
            "9", "Debug consumer running", True,
            detail="(stubbed /status — no debug_consumer field; real implementation in Chunk 4)",
        )
    dc = status["debug_consumer"]
    if dc.get("running") and dc.get("consecutive_failures", 0) < 3:
        return ProbeResult("9", "Debug consumer running", True)
    return ProbeResult(
        "9", "Debug consumer running", False,
        fix=(
            f"Debug consumer not healthy: running={dc.get('running')}, "
            f"failures={dc.get('consecutive_failures')}.\n\n"
            f"Check sc64-api logs:\n"
            f"  ssh root@<host> journalctl -u sc64-api -n 100 | grep DebugConsumer"
        ),
    )


def probe_camera(status: dict[str, Any]) -> ProbeResult:
    # Same stub rule as probe 9: the Chunk-2 /status stub omits the
    # "camera" key. Once the key is present, anything other than
    # stream_reachable=True (including None) is a real failure.
    if "camera" not in status:
        return ProbeResult(
            "10", "Camera responding", True,
            detail="(stubbed /status — no camera field; real probe lands in Chunk 4)",
        )
    cam = status["camera"]
    if cam.get("stream_reachable") is True:
        return ProbeResult("10", "Camera responding", True)
    return ProbeResult(
        "10", "Camera responding", False,
        fix=(
            "Camera stream not reachable.\n\n"
            "  ssh root@<host> systemctl status camera-stream\n\n"
            "Camera ribbon cable orientation: blue stripe faces the\n"
            "Ethernet jack on the Pi 3B."
        ),
    )


# -- Warn-only -------------------------------------------------------------

def probe_qw_local_marker(status: dict[str, Any]) -> ProbeResult:
    d = status.get("deployer", {})
    if d.get("has_qw_local_flush_patch"):
        return ProbeResult("W1", "Deployer has qw-local flush patch", True, warn_only=True)
    return ProbeResult(
        "W1", "Deployer has qw-local flush patch", False, warn_only=True,
        fix=(
            "Pi is running upstream sc64deployer without the qw-local\n"
            "stdout-flush patch. IS-Viewer lines may arrive in bursts and\n"
            "wait_for_log() will appear intermittent.\n\n"
            "Rebuild the Pi's deployer from the qw-local branch by editing\n"
            "pi-sc64/flake.nix's summercart input to the qw-local ref and\n"
            "running bootstrap-pi.sh full."
        ),
    )


def probe_recent_line(status: dict[str, Any]) -> ProbeResult:
    dc = status.get("debug_consumer", {})
    last = dc.get("last_line_ts_ms")
    if last is None:
        return ProbeResult(
            "W2", "Recent IS-Viewer line seen", False, warn_only=True,
            fix="Press the N64 reset button if the cart is plugged in.",
        )
    age_s = (time.time() * 1000 - last) / 1000
    if age_s < 60:
        return ProbeResult("W2", "Recent IS-Viewer line seen", True, warn_only=True)
    return ProbeResult(
        "W2", "Recent IS-Viewer line seen", False, warn_only=True,
        fix=f"Last IS-Viewer line was {age_s:.0f}s ago. Press the N64 reset button.",
    )


# -- Orchestration ---------------------------------------------------------

def probe_all(host: str) -> ProbeReport:
    rep = ProbeReport(host=host)

    p1 = probe_network(host)
    rep.results.append(p1)
    if not p1.passed:
        return rep  # short-circuit

    p2 = probe_ssh(host)
    rep.results.append(p2)
    if not p2.passed:
        return rep

    p3 = probe_sc64api_responding(host)
    rep.results.append(p3)
    if not p3.passed:
        return rep

    p4 = probe_token_local(host)
    rep.results.append(p4)
    if not p4.passed:
        return rep

    p5, status = probe_status(host)
    rep.results.append(p5)
    if not p5.passed or status is None:
        return rep

    rep.results.append(probe_token_file_mode(status))
    rep.results.append(probe_ftdi_present(status))
    rep.results.append(probe_deployer_can_open(status))
    rep.results.append(probe_debug_consumer(status))
    rep.results.append(probe_camera(status))

    # Warn-only — never block
    rep.results.append(probe_qw_local_marker(status))
    rep.results.append(probe_recent_line(status))

    return rep


def render_report(report: ProbeReport) -> str:
    lines = []
    lines.append(f"  HIL Doctor — diagnosing {report.host}")
    lines.append(f"  {'─' * 50}")
    for r in report.results:
        mark = "✅" if r.passed else ("⚠ " if r.warn_only else "❌")
        suffix = ""
        if r.passed and r.detail:
            suffix = f"  ({r.detail})"
        elif not r.passed:
            suffix = "  FAIL"
        lines.append(f"  [{r.number:>2}]  {r.name:<42}  {mark}{suffix}")

        if not r.passed and r.fix:
            lines.append("")
            lines.append("  ╭─ Fix " + "─" * 56 + "╮")
            for ln in r.fix.splitlines():
                lines.append(f"  │ {ln:<63} │")
            lines.append("  ╰" + "─" * 63 + "╯")
            lines.append("")

        # Stop after first blocking failure — that's the only thing the
        # user should read per the spec's contract.
        if not r.passed and not r.warn_only:
            lines.append("")
            lines.append("  Skipping remaining probes (first failure short-circuits).")
            break
    return "\n".join(lines)
