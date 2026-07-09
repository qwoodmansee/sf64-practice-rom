#!/usr/bin/env python3
"""HIL test runner.

Subcommands:
  doctor              — run preflight probes against the Pi, print results
  run <path>          — run a HIL test file or directory of tests

Auth:
  bearer token from $SC64_API_TOKEN or ~/.sc64-api-token

Usage:
  python3 tools/hil_test_runner.py doctor
  python3 tools/hil_test_runner.py doctor --host sc64pi.local
  python3 tools/hil_test_runner.py run tests/hil/test_boot_smoke.py
"""
from __future__ import annotations

import argparse
import glob
import importlib.util
import sys
import time
import traceback
import uuid
from pathlib import Path
from types import ModuleType

# When invoked as `python3 tools/hil_test_runner.py`, Python puts the script
# dir (tools/) on sys.path — not the repo root — so `import tools.hil.*`
# would fail with ModuleNotFoundError. Anchor the repo root explicitly
# before any tools.hil import.
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

DEFAULT_HOST = "sc64pi.local"


def _check_deps() -> None:
    """Fail fast with an actionable message if HTTP deps are missing."""
    try:
        import httpx  # noqa: F401
    except ImportError:
        print(
            "error: the `httpx` package is required for HIL testing but is\n"
            "not importable in this Python environment.\n\n"
            "Install it with:\n"
            "  python3 -m pip install --user httpx\n\n"
            "or use a virtualenv that has `httpx` (and `pytest` for the\n"
            "unit tests under tests/hil/_unit/).",
            file=sys.stderr,
        )
        raise SystemExit(2)


def cmd_doctor(args: argparse.Namespace) -> int:
    from tools.hil.doctor import probe_all, render_report

    report = probe_all(args.host)
    print(render_report(report))
    print()
    blocking_fails = [r for r in report.results if not r.passed and not r.warn_only]
    if blocking_fails:
        return 1
    warns = [r for r in report.results if not r.passed and r.warn_only]
    if warns:
        print(f"  Note: {len(warns)} warn-only probes failed (not blocking).")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    from tools.hil.banner import (
        EX_TEMPFAIL,
        emit_ci_fail,
        is_interactive,
        prompt_retry,
    )
    from tools.hil.client import ClientConfig, HilClient
    from tools.hil.ctx import CartWedgedError, TestContext
    from tools.hil.doctor import probe_all, render_report
    from tools.hil.junit import JUnitSuite, JUnitTestCase
    from tools.hil.junit import write as write_junit

    # Optional preflight
    if not args.skip_preflight:
        report = probe_all(args.host)
        if report.blocking_failed:
            print(render_report(report))
            print("\n  Preflight failed. Aborting (use --skip-preflight to bypass).")
            return 1

    test_paths = _discover_tests(args.path)
    if not test_paths:
        print(f"No tests found at {args.path}", file=sys.stderr)
        return 2

    run_id = uuid.uuid4().hex[:8]
    # Anchor artifacts to the repo root, not the caller's cwd, so the
    # runner behaves the same when invoked from any directory.
    artifacts_root = REPO_ROOT / "tests" / "hil" / "_artifacts" / run_id
    artifacts_root.mkdir(parents=True, exist_ok=True)
    print(f"  Run ID: {run_id}")
    print(f"  Artifacts: {artifacts_root}")

    cfg = ClientConfig(host=args.host)
    suite = JUnitSuite(name="hil")

    for tp in test_paths:
        test_name = Path(tp).stem
        print(f"\n  >> {test_name}")
        mod = _load_test_module(tp)
        if not hasattr(mod, "run"):
            print("     SKIP (no `def run(ctx)`)")
            suite.cases.append(JUnitTestCase(
                name=test_name, duration_s=0.0,
                skipped=True, skip_reason="no run() function",
            ))
            continue

        def _execute() -> tuple[list[str], list[str], float]:
            start = time.time()
            with HilClient(cfg) as client:
                ctx = TestContext(
                    client=client,
                    artifacts_dir=artifacts_root,
                    test_name=test_name,
                )
                try:
                    mod.run(ctx)
                except CartWedgedError as e:
                    ctx.failures.append(f"cart wedged: {e}")
                    raise
                except Exception as e:
                    traceback.print_exc()
                    ctx.failures.append(f"exception: {e}")
                return list(ctx.passes), list(ctx.failures), time.time() - start

        try:
            passes, failures, dur = _execute()
        except CartWedgedError as e:
            if is_interactive():
                if prompt_retry():
                    print("  Retrying after power-cycle...")
                    try:
                        passes, failures, dur = _execute()
                    except CartWedgedError as e2:
                        passes, failures, dur = [], [f"cart wedged twice: {e2}"], 0.0
                else:
                    print("  Aborted by user.")
                    return EX_TEMPFAIL
            else:
                # CI mode: print banner once, exit EX_TEMPFAIL
                emit_ci_fail()
                suite.cases.append(JUnitTestCase(
                    name=test_name, duration_s=0.0,
                    failures=[f"cart wedged: {e}"],
                ))
                write_junit(suite, artifacts_root / "junit.xml")
                return EX_TEMPFAIL

        for p in passes:
            print(f"     PASS: {p}")
        for f in failures:
            print(f"     FAIL: {f}")
        suite.cases.append(JUnitTestCase(
            name=test_name, duration_s=dur,
            passes=passes, failures=failures,
        ))

    write_junit(suite, artifacts_root / "junit.xml")
    print(f"\n  JUnit: {artifacts_root}/junit.xml")
    if suite.n_failures > 0:
        return 1
    return 0


def _discover_tests(path: str) -> list[str]:
    p = Path(path)
    if p.is_file():
        return [str(p)]
    if p.is_dir():
        return sorted(glob.glob(str(p / "test_*.py")))
    return []


def _load_test_module(path: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location("hil_test_module", path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    parser = argparse.ArgumentParser(prog="hil_test_runner.py")
    subs = parser.add_subparsers(dest="cmd", required=True)

    doctor = subs.add_parser("doctor", help="run preflight probes")
    doctor.add_argument("--host", default=DEFAULT_HOST,
                        help=f"Pi hostname (default: {DEFAULT_HOST})")
    doctor.set_defaults(func=cmd_doctor)

    run = subs.add_parser("run", help="run a HIL test or directory of tests")
    run.add_argument("path", help="test file or directory")
    run.add_argument("--host", default=DEFAULT_HOST,
                     help=f"Pi hostname (default: {DEFAULT_HOST})")
    run.add_argument("--skip-preflight", action="store_true",
                     help="skip the inline doctor preflight")
    run.set_defaults(func=cmd_run)

    args = parser.parse_args()
    _check_deps()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
