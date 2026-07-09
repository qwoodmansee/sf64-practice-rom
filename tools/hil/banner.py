"""Cart-wedge banner — interactive vs CI mode per spec §6."""
from __future__ import annotations

import sys

EX_TEMPFAIL = 75  # /usr/include/sysexits.h


CART_WEDGED_BANNER = """
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║   ⚠   CART NOT RESPONDING                                    ║
║                                                              ║
║   The ROM uploaded successfully but the N64 has not emitted  ║
║   any IS-Viewer output within the cart-alive timeout.        ║
║                                                              ║
║   👉  Please walk over and POWER-CYCLE the N64 (off → on).   ║
║                                                              ║
║   After power-cycling:                                       ║
║     • Interactive mode: press [Enter] to retry this test     ║
║     • CI mode: rerun the failed test once cart is back       ║
║                                                              ║
║   Press Ctrl+C to abort the whole session.                   ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
"""


def is_interactive() -> bool:
    """True iff stdin is a TTY (Enter-to-retry mode)."""
    return sys.stdin.isatty()


def render_banner() -> str:
    return CART_WEDGED_BANNER


def prompt_retry() -> bool:
    """Print banner and wait for Enter (retry) or EOF / Ctrl+C (abort).

    Returns True if user pressed Enter, False if abort.
    """
    print(render_banner(), file=sys.stderr)
    try:
        input("> ")  # block on stdin
        return True
    except (EOFError, KeyboardInterrupt):
        return False


def emit_ci_fail() -> None:
    """Print banner to stderr; caller exits with EX_TEMPFAIL."""
    print(render_banner(), file=sys.stderr)
