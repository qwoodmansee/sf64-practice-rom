"""Unit tests for hil_test_runner.cmd_run — mocked client, no Pi required.

Covers the runner wiring that otherwise only gets exercised against real
hardware: JUnit emission, the CI cart-wedge EX_TEMPFAIL exit, test
discovery edge cases, and direct-script invocation (sys.path bootstrap).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import xml.dom.minidom
from pathlib import Path
from unittest.mock import MagicMock, patch

import tools.hil.banner as banner_mod
import tools.hil.client as client_mod
import tools.hil_test_runner as runner_mod
from tools.hil.banner import EX_TEMPFAIL
from tools.hil_test_runner import cmd_run

REPO_ROOT = Path(__file__).resolve().parents[3]


def make_args(path: str, host: str = "x") -> argparse.Namespace:
    return argparse.Namespace(path=path, host=host, skip_preflight=True)


def write_test(tmp_path: Path, name: str, body: str) -> Path:
    p = tmp_path / name
    p.write_text(body)
    return p


def _patch_artifacts_root(monkeypatch, tmp_path: Path) -> None:
    """Redirect the runner's repo-root artifact anchor into tmp."""
    monkeypatch.setattr(runner_mod, "REPO_ROOT", tmp_path)


def test_run_missing_path_exits_2(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    assert cmd_run(make_args(str(tmp_path / "nope.py"))) == 2


def test_run_passing_test_exits_0_and_writes_junit(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    tp = write_test(tmp_path, "test_ok.py", (
        "def run(ctx):\n"
        "    ctx.assert_true(True, 'ok')\n"
    ))
    with patch.object(client_mod, "HilClient", MagicMock()):
        rc = cmd_run(make_args(str(tp)))
    assert rc == 0
    junits = list((tmp_path / "tests/hil/_artifacts").glob("*/junit.xml"))
    assert len(junits) == 1
    doc = xml.dom.minidom.parse(str(junits[0]))  # raises if invalid XML
    suite = doc.documentElement
    assert suite.getAttribute("failures") == "0"
    assert suite.getAttribute("tests") == "1"


def test_run_artifacts_anchored_to_repo_root_not_cwd(tmp_path, monkeypatch):
    """Artifacts land under REPO_ROOT even when cwd is somewhere else."""
    _patch_artifacts_root(monkeypatch, tmp_path)
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(elsewhere)
    tp = write_test(tmp_path, "test_ok.py", (
        "def run(ctx):\n"
        "    ctx.assert_true(True, 'ok')\n"
    ))
    with patch.object(client_mod, "HilClient", MagicMock()):
        rc = cmd_run(make_args(str(tp)))
    assert rc == 0
    assert list((tmp_path / "tests/hil/_artifacts").glob("*/junit.xml"))
    assert not (elsewhere / "tests").exists()


def test_run_failing_test_exits_1(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    tp = write_test(tmp_path, "test_bad.py", (
        "def run(ctx):\n"
        "    ctx.assert_true(False, 'nope')\n"
    ))
    with patch.object(client_mod, "HilClient", MagicMock()):
        rc = cmd_run(make_args(str(tp)))
    assert rc == 1


def test_run_skips_module_without_run(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    tp = write_test(tmp_path, "test_norun.py", "X = 1\n")
    with patch.object(client_mod, "HilClient", MagicMock()):
        rc = cmd_run(make_args(str(tp)))
    assert rc == 0
    junits = list((tmp_path / "tests/hil/_artifacts").glob("*/junit.xml"))
    assert 'skipped="1"' in junits[0].read_text()


def test_cart_wedge_in_ci_mode_exits_tempfail(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    tp = write_test(tmp_path, "test_wedge.py", (
        "from tools.hil.ctx import CartWedgedError\n"
        "def run(ctx):\n"
        "    raise CartWedgedError('silent cart')\n"
    ))
    with patch.object(client_mod, "HilClient", MagicMock()), \
         patch.object(banner_mod, "is_interactive", return_value=False):
        rc = cmd_run(make_args(str(tp)))
    assert rc == EX_TEMPFAIL
    junits = list((tmp_path / "tests/hil/_artifacts").glob("*/junit.xml"))
    assert len(junits) == 1
    assert "cart wedged" in junits[0].read_text()


def test_cart_wedge_interactive_retry_succeeds(tmp_path, monkeypatch):
    _patch_artifacts_root(monkeypatch, tmp_path)
    # First execution wedges; the retry passes (module-level counter).
    tp = write_test(tmp_path, "test_flaky_wedge.py", (
        "from tools.hil.ctx import CartWedgedError\n"
        "CALLS = {'n': 0}\n"
        "def run(ctx):\n"
        "    CALLS['n'] += 1\n"
        "    if CALLS['n'] == 1:\n"
        "        raise CartWedgedError('first boot silent')\n"
        "    ctx.assert_true(True, 'booted on retry')\n"
    ))
    with patch.object(client_mod, "HilClient", MagicMock()), \
         patch.object(banner_mod, "is_interactive", return_value=True), \
         patch.object(banner_mod, "prompt_retry", return_value=True):
        rc = cmd_run(make_args(str(tp)))
    assert rc == 0


def test_script_direct_invocation_no_modulenotfounderror(tmp_path):
    """`python3 tools/hil_test_runner.py ...` must work without PYTHONPATH.

    Regression for the sys.path bug: Python puts the script dir (tools/)
    on sys.path, not the repo root, so `import tools.hil.*` failed with
    ModuleNotFoundError until the runner bootstrapped REPO_ROOT itself.
    Runs from an unrelated cwd with PYTHONPATH stripped to prove it.
    """
    script = REPO_ROOT / "tools" / "hil_test_runner.py"
    env = {k: v for k, v in os.environ.items() if k != "PYTHONPATH"}
    proc = subprocess.run(
        [sys.executable, str(script), "run",
         str(tmp_path / "no_such_test.py"), "--skip-preflight"],
        capture_output=True, text=True, cwd=tmp_path, env=env, timeout=60,
    )
    assert "ModuleNotFoundError" not in proc.stderr
    assert proc.returncode == 2  # "No tests found" — imports all succeeded
