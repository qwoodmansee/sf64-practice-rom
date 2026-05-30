# N64 Hardware-in-the-Loop (HIL) Testing — Design

**Status:** Draft for review
**Date:** 2026-05-30
**Repos affected:** `pi-sc64`, `sf64-practice-rom`

---

## 1. Problem

The SF64 practice ROM has a strong headless test suite (`tests/test_*.py` via
`tools/m64p_test_runner.py`, mupen64plus-backed). It catches a large class of
regressions but is blind to anything that only manifests on real hardware:

- Boot-time crashes (e.g. main_ROM_END exceeding the 0xFD000 boot-safe limit
  manifesting as a blue screen)
- SC64 protocol regressions (IS-Viewer `IS64` token, atomic `rp/wp`, cart-bus
  IO_READ drain — all documented as hard-won gotchas in
  `sf64-practice-rom/CLAUDE.md`)
- Hardware-only init paths (`iodev_sd_init` on EverDrive, FatFs interactions)
- Visual regressions (HIT64 logo, HUD layout, text rendering)

A Raspberry Pi 3B (`sc64pi.local`) already sits between the dev machine and
the cart: it runs `sc64-server` (SC64 deployer on `:9064`), `sc64-api`
(FastAPI upload endpoint on `:8064`), and `camera-stream` (ustreamer MJPEG
of a Pi Camera pointed at the TV, on `:8080`). The hardware capability for
an automated round trip already exists; only the orchestration layer is
missing.

## 2. Goals (v1)

Build a passive HIL test harness that lets a developer (or Claude) run a
sequence of:

1. Upload ROM build to the SC64 (cart auto-reboots the console)
2. Capture the IS-Viewer printf stream from the booting/running ROM
3. Capture a still frame from the Pi camera
4. Assert on log content and attach the screenshot to the test result

The harness exposes the same `def run(ctx): ...` test shape as the mupen
runner, so test authors keep their muscle memory. Tests live alongside
existing tests at `sf64-practice-rom/tests/hil/test_*.py`.

## 3. Non-goals (v1, explicit)

- **Controller input emulation.** The N64 joybus protocol requires
  cycle-accurate 1-wire serial that the RPi 3B cannot bit-bang reliably
  from userspace. This is a known phase-2 hardware-procurement problem
  (RP2040 joybus slave or equivalent) and is consciously deferred. v1 is a
  passive observer.
- **Automatic power-cycle of the N64.** If the cart wedges past what
  `sc64deployer --reboot` can recover, a human walks over and resets it.
  A USB-controllable AC relay is a future enhancement.
- **Golden-image perceptual diff for screenshots.** v1 attaches snapshots
  to test results for human inspection; automated visual diff against
  golden images is deferred until lighting/camera-angle stability is
  established.
- **Pre-commit hook integration.** HIL runs are on-demand only. The Pi
  isn't always reachable from every dev environment, and the existing
  mupen suite already provides fast pre-commit feedback.
- **Test retry on flake.** A failing HIL test fails; we don't paper over
  hardware flake with retries. A wedged-cart outcome is a distinct exit
  code (`EX_TEMPFAIL`, 75) so CI can choose to retry the *session*, not
  the individual test.

## 4. Architecture

Three deliverable units across two repos:

### 4.1 Pi-side (`pi-sc64`)

The existing `sc64-api` FastAPI service is extended to own the cart's debug
subprocess and a timestamped log ring buffer. The previously-considered
separate `isv-tap` service is **rejected** because the deployer's server
(`sw/deployer/src/sc64/server.rs:163`) is single-threaded and serializes
TCP clients — a long-lived debug client would block upload connections
indefinitely. Merging the two responsibilities into `sc64-api` eliminates
the cross-process coordination problem.

```
┌──────────────────────────────────────┐
│  sc64-api (FastAPI, port 8064)       │
│                                      │
│  ┌────────────────────────────────┐  │
│  │ DebugConsumer                  │  │
│  │   subprocess.Popen(            │  │
│  │     ["sc64deployer", "-r",     │  │
│  │      "localhost:9064", "debug",│  │
│  │      "--isv", "0x03FF0000"])   │  │
│  │   pipe stdout → LogRing        │  │
│  │   respawn on exit (backoff)    │  │
│  └────────┬───────────────────────┘  │
│           │                          │
│  ┌────────▼───────────────────────┐  │
│  │ LogRing                        │  │
│  │   in-memory deque(maxlen=50k)  │  │
│  │   rotating file (7d retention) │  │
│  │   query: read_window(s, e)     │  │
│  └────────────────────────────────┘  │
│                                      │
│  Endpoints (all bearer-token auth    │
│  except /health):                    │
│    GET  /health                      │
│    GET  /status                      │
│    POST /upload                      │
│    GET  /logs?since=&until=&limit=   │
│    GET  /camera/snapshot             │
│                                      │
│  Upload lock semantics:              │
│    acquire lock                      │
│    debug_consumer.stop()             │
│    run sc64deployer upload           │
│    debug_consumer.start()            │
│    release lock                      │
└──────────────┬───────────────────────┘
               │ TCP :9064 (single client at a time)
               ▼
        ┌─────────────────┐
        │  sc64-server    │
        │  (unmodified)   │
        └────────┬────────┘
                 │ USB FTDI
                 ▼
            SummerCart64 ─► N64
```

The `camera-stream` module is unchanged. `sc64-api` proxies camera
snapshots through `GET /camera/snapshot` so callers have a single
authenticated surface; the camera-stream `:8080` MJPEG remains available
for live monitoring.

### 4.2 Mac-side test runner (`sf64-practice-rom`)

```
sf64-practice-rom/
├── tools/
│   ├── hil_test_runner.py     # mirrors m64p_test_runner.py shape
│   └── hil/
│       ├── __init__.py
│       ├── ctx.py             # TestContext primitives
│       ├── client.py          # httpx wrapper for sc64-api
│       └── banner.py          # cart-wedged banner rendering
├── tests/
│   └── hil/
│       ├── _artifacts/        # gitignored, per-run output dir
│       ├── _fixtures/         # broken-ROM fixture etc.
│       ├── _unit/             # local unit tests for ctx itself
│       ├── test_boot_smoke.py
│       ├── test_isv_protocol_regression.py
│       └── test_cart_wedge_detection.py
```

Test file shape (mirrors `tests/test_*.py`):

```python
def run(ctx):
    ctx.upload_rom("build/starfox64.us.rev1.uncompressed.z64")
    ctx.wait_for_log(r"IS-Viewer init OK", timeout_ms=5000)
    ctx.advance_seconds(3)
    shot = ctx.snapshot()
    ctx.assert_log_contains(r"PRACTICE READY")
    ctx.assert_log_not_contains(r"PANIC")
```

`ctx` primitives (v1):

| Method | Semantics |
|---|---|
| `upload_rom(path)` | POST to `/upload`; on 200, waits for first IS-Viewer line within `cart_alive_timeout_ms` (default 10s); raises `CartWedgedError` on timeout. Returns when the cart is confirmed alive. |
| `wait_for_log(pattern, timeout_ms=10000)` | Polls `/logs?since=<upload_complete_ts>`; returns the first matching line; raises `LogWaitTimeout` on miss. Pattern is `re.search`. |
| `advance_seconds(s)` | Wall-clock `time.sleep(s)`. No frame sync available without RAM peek. Test authors must budget slack for hardware boot variance — a cold boot takes longer than a warm reset. |
| `snapshot(name=None)` | GET `/camera/snapshot`; writes to `tests/hil/_artifacts/<run_id>/<test>-<seq>.jpg`; returns path. |
| `assert_log_contains(pattern)` | Records pass/fail in `ctx.passes`/`ctx.failures`. Queries `/logs` over `[upload_complete_ts, now]`. |
| `assert_log_not_contains(pattern)` | Inverse; useful for "no PANIC line emitted". |
| `assert_true(cond, msg)` / `assert_eq(a, b, msg)` | General-purpose, identical to mupen `ctx`. |

Auth: bearer token loaded from `SC64_API_TOKEN` env var, falling back to
`~/.sc64-api-token` file (mode 600). Runner fails fast with a clear
message if neither is present.

### 4.3 MCP server (`sf64-practice-rom/tools/n64-hil-mcp`)

Thin wrapper exposing the same primitives as Claude tools:

| Tool | Description |
|---|---|
| `upload_rom_and_watch(rom_path, watch_seconds, log_pattern?)` | One-shot: upload + wait for optional log pattern + return logs + snapshot path |
| `run_hil_test(test_name)` | Invokes `hil_test_runner.py tests/hil/test_<name>.py`, returns parsed JUnit result + artifact paths |
| `snapshot()` | One-off camera snapshot, returns path |
| `tail_log(seconds)` | Returns the last `seconds` of log lines for ad-hoc inspection |

Implementation: Python MCP SDK, depends only on `httpx` and the shared
`hil/client.py` module. Distributed via local stdio MCP, registered in
`~/.claude/mcp.json` or per-project equivalent.

## 5. Data flow (one test run)

```
1. Runner discovers tests/hil/test_boot_smoke.py
2. Builds the ROM (or accepts a pre-built path via --rom)
3. Creates TestContext(ctx) and calls test.run(ctx)

   ctx.upload_rom(path):
     - upload_start_ts = now()
     - POST /upload (multipart) with bearer token
     - Pi: acquire _upload_lock
            debug_consumer.stop()  (SIGTERM, 2s grace, SIGKILL fallback)
            run sc64deployer upload --direct --reboot
            on success: debug_consumer.start()
            release lock
       returns 200 + {upload_complete_ts, ...}
     - upload_complete_ts is the anchor for all subsequent log queries
     - poll GET /logs?since=upload_complete_ts until ANY line appears,
       or cart_alive_timeout_ms expires → CartWedgedError

   ctx.wait_for_log(pattern, timeout_ms):
     - loop: GET /logs?since=upload_complete_ts&until=now()
     - re.search(pattern) on each line
     - returns matched line or raises LogWaitTimeout

   ctx.snapshot():
     - GET /camera/snapshot
     - writes JPEG to tests/hil/_artifacts/<run_id>/<test>-<seq>.jpg

   ctx.assert_log_contains(pattern):
     - re-queries /logs over [upload_complete_ts, now()]
     - records pass/fail; does not raise

4. Runner emits:
   - JUnit XML at tests/hil/_artifacts/<run_id>/junit.xml
   - Per-test JSON with passes[], failures[], log_window[], artifact paths
   - Exit code: 0 if all green, 1 on test failure, 75 (EX_TEMPFAIL) on
     cart-wedged (CI can choose to retry the session)
```

### Concurrency contract

`/upload` is serialized by an asyncio lock. A second concurrent POST
receives `409 Conflict`. The Mac runner does **not** retry on 409; it
fails the session with a clear "another upload in progress" message.
Running concurrent test sessions against one physical N64 is nonsensical
and we prefer the loud failure.

## 6. Failure modes and error handling

| Failure | Detection | Behavior |
|---|---|---|
| Pi unreachable | `httpx.ConnectError` | Runner aborts session: "Pi unreachable at `<host>`" |
| Token missing/wrong | 401 / 403 from `/upload` | Runner aborts: "set `SC64_API_TOKEN` or `~/.sc64-api-token`" |
| Upload conflict (concurrent run) | 409 from `/upload` | Fail fast: "another upload in progress — check sc64-api logs" |
| Upload subprocess fails (cart unplugged, FTDI gone) | 502 from `/upload` + stderr | Test fails with deployer stderr surfaced |
| Debug consumer dies transiently | Auto-respawn w/ exponential backoff (1s, 2s, 4s, cap 10s); `consecutive_failures` tracked | `/status` exposes `debug_consumer_running` and `consecutive_failures`; runner checks before each test |
| Debug consumer cannot start at all | Respawn loop hits a "stuck failing" threshold | `/status` shows `debug_consumer_running: false`; runner aborts with "debug stream down on Pi" |
| Camera unavailable | 502 from `/camera/snapshot` | `ctx.snapshot()` returns `None`; `ctx.snapshot(required=True)` raises. Tests without visual asserts continue. |
| `wait_for_log` timeout | Pattern not seen in window | Test fails with: pattern, timeout, last N log lines for triage |
| Ring buffer overflow on slow test | Oldest in-memory lines evicted; file rotation retains history | In-memory window of ~50k lines (~25min at 30 lines/sec) sized for typical tests; file is for forensics not test path |
| Pi runs stock deployer without the `qw-local` stdout-flush patch | IS-Viewer lines sit in Rust's stdout buffer until newline-terminated chunks force a flush, so `wait_for_log` appears intermittent | Validated at sc64-api boot: log first line ts and warn if no lines arrive within 5s of consumer start. Resolution is to rebuild `sc64deployer` from the `qw-local` branch on the Pi (already documented in the `pi_sc64` entity memory and in `CLAUDE.md`'s "Hard-won SC64 protocol gotchas" section). |
| Panic-loop ROM emits hundreds of lines/sec | In-memory ring evicts in seconds | File-rotation writer (separate from the in-memory deque) captures the full stream to `/var/lib/sc64-api/logs/isv-YYYY-MM-DD.log.gz` so forensics survive even when the test-path window has rolled over |
| Cart wedged (uploads succeed but no IS-Viewer output within `cart_alive_timeout_ms`) | `upload_rom()` cannot see any post-upload line | Runner emits the cart-wedged banner (below); interactive mode waits for Enter to retry; CI mode exits 75 |

### Cart-wedged banner

```
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║   ⚠   CART NOT RESPONDING                                    ║
║                                                              ║
║   The ROM uploaded successfully but the N64 has not emitted  ║
║   any IS-Viewer output within 10s.                           ║
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
```

- **Interactive mode** (stdin is a TTY): blocks waiting for Enter, retries the
  test once after the human presses Enter; fails the test if it wedges again.
- **CI / non-interactive mode**: prints the same banner to stderr, exits
  the runner with code 75 (`EX_TEMPFAIL`).
- **MCP tool path**: `run_hil_test` propagates `cart_wedged` as a distinct
  error type so Claude can speak plainly: "Please power-cycle the N64."

## 7. Testing strategy

| Layer | What | Where | Hardware needed |
|---|---|---|---|
| Unit | `LogRing` append/query/overflow/rotation | `pi-sc64/packages/sc64-api/tests/test_log_ring.py` | none |
| Unit | `DebugConsumer` lifecycle (mocked `subprocess.Popen`) | `pi-sc64/packages/sc64-api/tests/test_debug_consumer.py` | none |
| Unit | `ctx` primitive semantics + cart-wedge detection | `sf64-practice-rom/tests/hil/_unit/test_ctx.py` | none |
| Pi integration | NixOS VM test with `--mock-cart` flag → sc64-api spawns a fake printer instead of real deployer; verify `/logs`, upload-cycle, snapshot proxy | `pi-sc64/tests/nixos-mock-cart.nix` | none |
| End-to-end | `test_boot_smoke.py` against real Pi + cart | `sf64-practice-rom/tests/hil/` | Pi + cart |
| End-to-end | `test_isv_protocol_regression.py` — IS64 token + osSyncPrintf line | same | Pi + cart |
| End-to-end | `test_cart_wedge_detection.py` — broken-ROM fixture, asserts banner + exit 75 | same | Pi + cart |
| MCP smoke | start server, call each tool with mocked HTTP client | `sf64-practice-rom/tools/n64-hil-mcp/test_mcp_smoke.py` | none |

**Validation milestones (the order we earn trust, optimized for fastest
real-hardware feedback given Pi is reachable):**

1. Layer 1 unit tests pass locally on Mac
2. `test_boot_smoke.py` green against real Pi + cart — primary feedback loop
3. NixOS VM mock-cart test green — regression safety net for future PRs
4. `test_cart_wedge_detection.py` produces the banner and exits 75
5. Manual verification that, during a real upload, the debug-consumer
   stop/start boundary loses no IS-Viewer lines on either side

## 8. Implementation order

Front-load the real-hardware round trip; defer mock infrastructure.

1. **Round-trip MVP** — Pi-side: add `DebugConsumer` + `LogRing` + `/logs`
   + `/camera/snapshot`. Mac-side: minimal `ctx` with `upload_rom`,
   `wait_for_log`, `snapshot`. One test (`test_boot_smoke.py`) green
   against the real cart.
2. **Cart-wedge detection and banner** + the broken-ROM fixture + matching test.
3. **JUnit emission**, `_artifacts/` layout, full assertion API, on-disk
   ring buffer rotation, `/status` enrichment.
4. **NixOS VM mock-cart test** + `--mock-cart` flag on sc64-api.
5. **MCP server** with the four tools + smoke tests.
6. **Documentation**: `tests/hil/README.md` mirroring the existing
   `tests/README.md`; addition to `sf64-practice-rom/CLAUDE.md` under a
   new "HIL tests" section.

## 9. Open questions deferred to implementation

- **ustreamer snapshot endpoint shape.** `ustreamer` exposes `/snapshot`
  in some build configurations but not all. If the current Pi build
  doesn't expose it, the snapshot proxy reads one MJPEG frame from
  `:8080/stream` and slices it; we'll know which path applies on first
  Pi deployment.
- **Log line filtering on the Pi side.** `/logs` returns raw lines for
  v1. If volume causes test-side regex CPU pressure, we'll add an
  optional `pattern=` server-side filter later.
- **MCP transport.** Stdio for v1 (local agent), HTTP MCP comes later if
  remote Claude sessions need it.

## 10. References

- SF64 ROM repo: `sf64-practice-rom/` (this repo)
- Pi NixOS flake: `pi-sc64/`
- Existing mupen runner: `sf64-practice-rom/tools/m64p_test_runner.py`
- SC64 deployer source: `~/code/SummerCart64/sw/deployer/`
- Deployer server multiplexing limit:
  `~/code/SummerCart64/sw/deployer/src/sc64/server.rs:163` (single-threaded
  `listener.incoming()` loop, one TCP client at a time)
- IS-Viewer protocol gotchas: `sf64-practice-rom/CLAUDE.md` ("Hard-won
  SC64 protocol gotchas")
- Existing pi-sc64 entity memory: `viking://user/qwoodmansee/memories/entities/projects/pi_sc64.md`
