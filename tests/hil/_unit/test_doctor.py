"""Unit tests for hil.doctor — probe semantics with mocked /status."""
from __future__ import annotations

from tools.hil.doctor import (
    probe_token_file_mode,
    probe_ftdi_present,
    probe_deployer_can_open,
    probe_debug_consumer,
    probe_camera,
    probe_qw_local_marker,
    render_report,
    ProbeReport,
    ProbeResult,
)


class TestTokenFileMode:
    def test_correct(self):
        status = {"tokens_file": {"exists": True, "owner": "root", "group": "sc64api", "mode": "0640"}}
        assert probe_token_file_mode(status).passed

    def test_wrong_group(self):
        status = {"tokens_file": {"exists": True, "owner": "root", "group": "root", "mode": "0640"}}
        r = probe_token_file_mode(status)
        assert not r.passed
        assert "root:sc64api" in r.fix

    def test_wrong_mode(self):
        status = {"tokens_file": {"exists": True, "owner": "root", "group": "sc64api", "mode": "0600"}}
        r = probe_token_file_mode(status)
        assert not r.passed
        assert "0600" in r.fix

    def test_missing_file(self):
        status = {"tokens_file": {"exists": False, "path": "/x"}}
        r = probe_token_file_mode(status)
        assert not r.passed


class TestFtdi:
    def test_present(self):
        status = {"cart": {"ftdi_present": True, "ftdi_serial": "ABC"}}
        r = probe_ftdi_present(status)
        assert r.passed
        assert "ABC" in r.detail

    def test_absent(self):
        status = {"cart": {"ftdi_present": False}}
        assert not probe_ftdi_present(status).passed


class TestDeployerProbe:
    def test_ok(self):
        status = {"deployer": {"probe_ok": True}}
        assert probe_deployer_can_open(status).passed

    def test_fail_includes_error(self):
        status = {"deployer": {"probe_ok": False, "probe_last_error": "Permission denied"}}
        r = probe_deployer_can_open(status)
        assert not r.passed
        assert "Permission denied" in r.fix

    def test_ok_via_debug_consumer_surfaces_detail(self):
        # When the Pi infers openability from the live consumer (avoiding the
        # single-client-slot contention), probe 8 passes and says so.
        status = {"deployer": {"probe_ok": True, "probe_via": "debug_consumer"}}
        r = probe_deployer_can_open(status)
        assert r.passed
        assert r.detail and "debug consumer" in r.detail

    def test_ok_via_info_has_no_consumer_detail(self):
        status = {"deployer": {"probe_ok": True, "probe_via": "info"}}
        r = probe_deployer_can_open(status)
        assert r.passed
        assert r.detail is None


class TestDebugConsumer:
    def test_stub_passes_when_key_absent(self):
        # The Chunk-2 stub omits debug_consumer entirely — key absence,
        # not any value pattern, marks the stub.
        r = probe_debug_consumer({"cart": {}})
        assert r.passed
        assert "stubbed" in r.detail

    def test_present_but_not_running_fails(self):
        # running=False with zero failures is a reachable production state
        # (consumer crashed before recording a failure) — must FAIL.
        status = {"debug_consumer": {"running": False, "consecutive_failures": 0}}
        assert not probe_debug_consumer(status).passed

    def test_running_passes(self):
        status = {"debug_consumer": {"running": True, "consecutive_failures": 0}}
        assert probe_debug_consumer(status).passed

    def test_too_many_failures_fails(self):
        status = {"debug_consumer": {"running": True, "consecutive_failures": 5}}
        assert not probe_debug_consumer(status).passed


class TestCamera:
    def test_stub_passes_when_key_absent(self):
        r = probe_camera({"cart": {}})
        assert r.passed
        assert "stubbed" in r.detail

    def test_present_but_none_fails(self):
        # Key present + stream_reachable=None is a real "unreachable /
        # unknown" production state, not the stub — must FAIL.
        status = {"camera": {"stream_reachable": None}}
        assert not probe_camera(status).passed

    def test_true_passes(self):
        status = {"camera": {"stream_reachable": True}}
        assert probe_camera(status).passed

    def test_false_fails(self):
        status = {"camera": {"stream_reachable": False}}
        assert not probe_camera(status).passed


class TestQwLocalMarker:
    def test_has_patch(self):
        status = {"deployer": {"has_qw_local_flush_patch": True}}
        assert probe_qw_local_marker(status).passed

    def test_warn_only_on_miss(self):
        status = {"deployer": {"has_qw_local_flush_patch": False}}
        r = probe_qw_local_marker(status)
        assert not r.passed
        assert r.warn_only


class TestRenderReport:
    def test_passing_report_no_fix_box(self):
        rep = ProbeReport(host="x")
        rep.results.append(ProbeResult("1", "Network", True))
        out = render_report(rep)
        assert "✅" in out
        assert "Fix" not in out

    def test_first_failure_short_circuits(self):
        rep = ProbeReport(host="x")
        rep.results.append(ProbeResult("1", "Network", True))
        rep.results.append(ProbeResult("2", "SSH", False, fix="install your key"))
        rep.results.append(ProbeResult("3", "API", False, fix="never rendered"))
        out = render_report(rep)
        assert "install your key" in out
        assert "never rendered" not in out
