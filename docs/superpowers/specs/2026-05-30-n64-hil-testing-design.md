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

**Operability requirement (v1, hard):** the user is starting from a
vibe-coded Pi that is currently unplugged. The harness must (a) detect
every realistic setup failure in the chain — Pi off, network gone, token
missing, sc64-api not running, deployer subprocess dead, cart unplugged,
camera dead — and (b) for each, surface a single, unambiguous next-step
with the exact command to run. The user should never be left wondering
"why isn't this working." Cold-start to first-green-test must be
trivially scriptable from `tests/hil/SETUP.md`.

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
│    GET  /status (enriched, see §4.4) │
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

#### 4.1.1 Enriched `/status` for preflight diagnostics

`GET /status` returns enough state for the Mac-side `hil doctor` and
inline preflight to diagnose every setup failure in one round trip:

```json
{
  "ok": true,
  "version": "0.2.0",
  "sc64_server": "localhost:9064",
  "upload_busy": false,
  "user_id": null,

  "debug_consumer": {
    "running": true,
    "pid": 1234,
    "started_at_ms": 1716800000000,
    "consecutive_failures": 0,
    "last_line_ts_ms": 1716800015234,
    "last_line_preview": "ISViewer init OK"
  },

  "ring_buffer": {
    "in_memory_lines": 1287,
    "in_memory_max": 50000,
    "file_path": "/var/lib/sc64-api/logs/isv-2026-05-30.log.gz",
    "file_bytes": 14387
  },

  "deployer": {
    "binary": "/nix/store/.../bin/sc64deployer",
    "version_string": "2.20.0",
    "version_checked_at_ms": 1716800000000,
    "has_qw_local_flush_patch": true,
    "probe_ok": true,
    "probe_last_run_ms": 1716800015000,
    "probe_last_error": null
  },

  "tokens_file": {
    "path": "/etc/sc64-api/tokens",
    "owner": "root",
    "group": "sc64api",
    "mode": "0640",
    "exists": true
  },

  "cart": {
    "ftdi_present": true,
    "ftdi_serial": "SC649T0HH2",
    "ftdi_checked_at_ms": 1716800015234
  },

  "camera": {
    "stream_reachable": true,
    "last_snapshot_ms": 1716800010000
  }
}
```

Each field maps 1:1 to a doctor probe (§10). Notes:

- `cart.ftdi_present` is derived from `/sys/bus/usb/devices/` lookup
  of VID 0x0403 / PID 0x6014, run on every `/status` call (cheap).
- `tokens_file.{owner,group,mode}` is read via `os.stat` on every
  `/status` call.
- `deployer.version_string` is captured by running
  `sc64deployer --version` once at sc64-api boot AND refreshed every
  60s by a background task — covers the case where the deployer is
  rebuilt out from under sc64-api. `version_checked_at_ms` exposes
  freshness so doctor can warn if stale.
- `deployer.has_qw_local_flush_patch` is a tri-state computed at boot from
  the version string's known marker: `true` or `false` when the marker can
  be evaluated, or the string `"unknown"` if the binary changes shape and the
  marker can't be matched. Consumers (doctor probes, test runner) must treat
  any value other than `true` as "patch not confirmed" rather than coercing
  it to a strict boolean.
- `deployer.probe_ok` is the result of the most recent `sc64deployer
  -r localhost:9064 info` invocation (run every 30s by a background
  task) — this is what catches "FTDI device present but sc64-server
  can't open it" failure modes that lsusb-level checks miss.

### 4.2 Mac-side test runner (`sf64-practice-rom`)

```
sf64-practice-rom/
├── tools/
│   ├── hil_test_runner.py     # mirrors m64p_test_runner.py shape;
│   │                          #   subcommands: run (default), doctor
│   └── hil/
│       ├── __init__.py
│       ├── ctx.py             # TestContext primitives
│       ├── client.py          # httpx wrapper for sc64-api
│       ├── doctor.py          # preflight probes + actionable diagnostics
│       └── banner.py          # cart-wedged banner rendering
├── tests/
│   └── hil/
│       ├── SETUP.md           # cold-start: "I have a Pi" → "test is green"
│       ├── README.md          # test-author docs (mirrors tests/README.md)
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

**Inline preflight runs automatically before every test session** — the
runner calls `doctor.probe_all()` first; if any probe fails, the session
aborts with the doctor's actionable output rather than letting the test
fail in a misleading way. To skip (e.g. when iterating on the doctor
itself): `--skip-preflight`.

**Standalone subcommand**: `python3 tools/hil_test_runner.py doctor`
runs the same probes and prints a check-by-check report with fix
commands. Intended for ad-hoc diagnostics: "is my Pi healthy right now?"
See §10 for the full UX walkthrough.

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

Front-load: (a) the cold-start UX, because the Pi is currently
unplugged and the user must be able to bring it back from nothing, and
(b) the real-hardware round trip. Defer mock infrastructure and MCP.

1. **Pi bring-up automation** — `pi-sc64/scripts/build-sd-image.sh`
   (bakes SSH key into image; optional WiFi) and
   `pi-sc64/scripts/bootstrap-pi.sh` (full / token-only / deployer
   modes). Both must be idempotent against repeat runs. The
   `nixos-rebuild --target-host` step depends on SSH being available,
   which is why SD image build comes first. `tests/hil/SETUP.md`
   ships at the END of this milestone (not in parallel) — written
   after the bootstrap script has been dogfooded once against a real
   cold-start, so SETUP.md describes what actually works rather than
   what was planned.
2. **Enriched `/status` + `hil doctor`** — implement the enriched
   `/status` endpoint per §4.1.1, then build `tools/hil/doctor.py` with
   all probes from §10. `doctor` must produce useful output even when
   the Pi is completely unreachable (network probe is the first check
   and has a meaningful "fix" line). Doctor is built BEFORE the test
   primitives because preflight is the entry point to every test run.
3. **Round-trip MVP** — Pi-side: `DebugConsumer` + `LogRing` + `/logs`
   + `/camera/snapshot`. Mac-side: minimal `ctx` with `upload_rom`,
   `wait_for_log`, `snapshot`. One test (`test_boot_smoke.py`) green
   against the real cart. Inline preflight via `doctor.probe_all()`
   wired in.
4. **Cart-wedge detection and banner** + the broken-ROM fixture +
   matching test.
5. **JUnit emission**, `_artifacts/` layout, full assertion API, on-disk
   ring buffer rotation.
6. **NixOS VM mock-cart test** + `--mock-cart` flag on sc64-api.
7. **MCP server** with the four tools + smoke tests, including a
   `hil_doctor()` tool that proxies the same diagnostics to Claude.
8. **Documentation**: `tests/hil/README.md` mirroring the existing
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

## 10. User experience from cold start

The Pi is unplugged. The user has the box on their desk, the cart in a
drawer, and a clone of this repo. This section is the contract for
"what happens next."

### 10.1 The cold-start journey

```
$ cd ~/code/sf64-practice-rom
$ python3 tools/hil_test_runner.py doctor

  HIL Doctor — diagnosing sc64pi.local
  ─────────────────────────────────────
  [1/7]  Network reachable                  ❌  FAIL

  ╭─ Fix ──────────────────────────────────────────────────────────╮
  │ Pi is unreachable at sc64pi.local.                             │
  │                                                                │
  │ Most common cause: the Pi is powered off or not on the LAN.    │
  │                                                                │
  │ Do this:                                                       │
  │   1. Plug the Pi into power and Ethernet (or confirm WiFi).    │
  │   2. Wait ~30s for boot + mDNS.                                │
  │   3. Re-run: python3 tools/hil_test_runner.py doctor           │
  │                                                                │
  │ If still failing after 60s, see tests/hil/SETUP.md §"Network". │
  ╰────────────────────────────────────────────────────────────────╯

  Skipping remaining probes (network is a prerequisite).
  exit 1
```

The user plugs the Pi in, waits, re-runs:

```
$ python3 tools/hil_test_runner.py doctor

  [1/7]  Network reachable                  ✅
  [2/7]  sc64-api responding (port 8064)    ✅
  [3/7]  Bearer token configured locally    ❌  FAIL

  ╭─ Fix ──────────────────────────────────────────────────────────╮
  │ No bearer token found on this machine.                         │
  │                                                                │
  │ Run:                                                           │
  │                                                                │
  │   pi-sc64/scripts/bootstrap-pi.sh token-only sc64pi.local      │
  │                                                                │
  │ This provisions a fresh token on the Pi (correct ownership +   │
  │ mode), saves it to ~/.sc64-api-token locally (mode 600), and   │
  │ prints it once for your records.                               │
  │                                                                │
  │ Then re-run: python3 tools/hil_test_runner.py doctor           │
  ╰────────────────────────────────────────────────────────────────╯
```

…and so on for each probe. The contract: **the user only ever has to
read the first ❌ row and the Fix box under it.** They never have to
read the spec, the source, or any other doc to know what to do next.

### 10.2 The probes

Probes are split into two phases. **Phase A** runs locally on the Mac
and requires no Pi side state beyond network reachability and SSH.
**Phase B** runs against a working sc64-api and uses the enriched
`/status` endpoint.

The split matters because Phase A's failures (especially SSH) must be
diagnosable without making any sc64-api call — that's what makes the
cold-start contract real.

**Phase A — local + low-level Pi connectivity:**

| # | Probe | Pass criterion | Fix on fail |
|---|---|---|---|
| 1 | Network reachable | TCP connect to `host:22` (SSH) and `host:8064` (HTTP) both succeed within 3s | Power-cycle / plug in Pi via **Ethernet** (WiFi setup is deferred to post-bootstrap); check LAN; verify mDNS resolves with `dscacheutil -q host -a name <host>` on macOS |
| 2 | SSH works without password | `ssh -o BatchMode=yes -o ConnectTimeout=3 root@<host> true` exits 0 | Re-flash the SD card with your SSH key baked in: `pi-sc64/scripts/build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub` (this is the only supported cold-start path — the SD image does NOT ship with a default root password). If the Pi is already provisioned but you've changed keys, edit `pi-sc64/hosts/pi/configuration.nix` (`users.users.root.openssh.authorizedKeys.keys`) and run `bootstrap-pi.sh full <host>` from the previous Mac. |
| 3 | sc64-api responding | `GET /health` returns `{"ok": true}` | `ssh root@<host> systemctl status sc64-api` then `journalctl -u sc64-api -n 50` |

**Phase B — token, services, hardware (all queryable via `/status`):**

| # | Probe | Pass criterion | Fix on fail |
|---|---|---|---|
| 4 | Bearer token configured locally | `SC64_API_TOKEN` env OR `~/.sc64-api-token` (mode 600, non-empty) | Run `pi-sc64/scripts/bootstrap-pi.sh token-only <host>` — provisions a fresh token on the Pi + saves to `~/.sc64-api-token` in one command (requires probe 2 ✅) |
| 5 | Token accepted by Pi | `GET /status` with bearer returns 200 | Regenerate via `bootstrap-pi.sh token-only` |
| 6 | Token file mode/owner on Pi correct | `/status.tokens_file.owner == "root"` AND `.group == "sc64api"` AND `.mode == "0640"` | `ssh root@<host> 'chown root:sc64api /etc/sc64-api/tokens && chmod 640 /etc/sc64-api/tokens'` (this is the single most documented pi-sc64 footgun — must be its own probe, not buried in #5's fix line) |
| 7 | Cart FTDI device present at USB | `/status.cart.ftdi_present == true` (sysfs lookup of VID 0x0403 / PID 0x6014) | Plug in SC64 USB to Pi; verify with `ssh root@<host> lsusb \| grep 0403`; check udev rule from `sc64-server.nix` |
| 8 | sc64-server can actually open the FTDI device | `/status.deployer.probe_ok == true` (Pi-side probe runs `sc64deployer -r localhost:9064 info` with 5s timeout) | Likely cause: `sc64-server` systemd unit user not in `sc64` group OR udev rule didn't apply (reboot Pi); fix command: `ssh root@<host> 'systemctl restart sc64-server && journalctl -u sc64-server -n 50'` |
| 9 | Debug consumer running | `/status.debug_consumer.running == true` AND `consecutive_failures < 3` | If consecutive_failures ≥ 3: `ssh root@<host> journalctl -u sc64-api -n 100 \| grep DebugConsumer` for root cause |
| 10 | Camera responding | `/status.camera.stream_reachable == true` | `ssh root@<host> systemctl status camera-stream`. Also: verify ribbon cable orientation — on Pi 3B, the **blue stripe faces the Ethernet jack** (this is the camera footgun) |

**Warn-only (do not block test runs):**

| # | Probe | Pass criterion | Fix on fail |
|---|---|---|---|
| W1 | Deployer has `qw-local` stdout-flush patch | `/status.deployer.has_qw_local_flush_patch == true` (computed from running `sc64deployer --version` at boot and matching a known marker) | Rebuild deployer from `qw-local` branch on Pi. Without this, IS-Viewer lines may arrive in bursts and `wait_for_log` will appear intermittent. |
| W2 | A recent IS-Viewer line was seen | `/status.debug_consumer.last_line_ts_ms` within last 60s (only meaningful if the cart has booted recently) | Press N64 reset to reboot the ROM. Purely informational. |

**Dependency graph:** failures in 1 short-circuit 2–10; failures in 2
short-circuit 3–10 (since their fixes all require SSH); failures in 3
short-circuit 4–10. Phases A and B are reported separately in the
doctor output so the user can see exactly where the chain breaks.

### 10.3 SD image build and bootstrap scripts

The bootstrap problem is resolved by **moving SSH key + WiFi config into
the SD image itself**. There is no way to make a `nixos-rebuild
--target-host` flow idempotent against a freshly-flashed Pi without
pre-shared SSH access. So we bake the keys at image build time.

**`pi-sc64/scripts/build-sd-image.sh`** — builds the SD image with the
user's SSH key embedded:

```
build-sd-image.sh [--ssh-key <path>] [--wifi-ssid <ssid> --wifi-psk <psk>]
```

Reads `~/.ssh/id_ed25519.pub` by default, embeds into
`users.users.root.openssh.authorizedKeys.keys` via a per-build Nix
module overlay. Optionally embeds WiFi credentials (but the cold-start
default is **Ethernet only** — see §10.4). Outputs the SD image path
and the `dd` command to flash.

**`pi-sc64/scripts/bootstrap-pi.sh`** — runs *after* the user has
flashed and booted the Pi. Modes:

```
bootstrap-pi.sh full <host>         # all of the below, idempotent
bootstrap-pi.sh token-only <host>   # just provision the bearer token
bootstrap-pi.sh deployer <host>     # rebuild deployer from qw-local
```

The `full` mode performs:

1. SSH connectivity check (Phase A probe 2 equivalent). Fails with the
   exact `build-sd-image.sh` command to re-flash with the right key.
2. `nixos-rebuild switch --flake .#pi --target-host root@<host>` —
   safe because SSH was guaranteed by step 1.
3. Generates a fresh bearer token, writes to `/etc/sc64-api/tokens` on
   the Pi (mode 640, `root:sc64api`) and to `~/.sc64-api-token` on the
   Mac (mode 600). The token is generated locally (`openssl rand -hex
   32`) and sent over SSH; no `sudo bash -c '...'` chains.
4. Rebuilds `sc64deployer` from `qw-local` branch on the Pi, restarts
   `sc64-server` and `sc64-api`.
5. Final probe sweep using the same doctor probes A1–B10 against the
   Pi; surfaces any remaining issues with the same fix boxes.
6. Tells the user: "All set. Plug the cart in and run
   `python3 tools/hil_test_runner.py doctor` from sf64-practice-rom."

`token-only` is the smallest unit and is what doctor probe 4's fix
line tells the user to run — it does not require a full
`nixos-rebuild`.

### 10.4 `tests/hil/SETUP.md`

A linear page with one explicitly acknowledged branch (Ethernet vs
WiFi at first boot). Cold-start steps:

1. **Hardware checklist**: Pi 3B + SD card (8GB+), Pi Camera v1/v2/v3,
   SC64 cart, N64, video-capture path (camera pointed at TV — diagram).
   Camera ribbon orientation: blue stripe faces the Pi's Ethernet jack.
2. **Build and flash the SD image**: `pi-sc64/scripts/build-sd-image.sh
   --ssh-key ~/.ssh/id_ed25519.pub`. The script's output includes the
   exact `dd` command (Mac users) or `nix run nixpkgs#zstd` +
   Raspberry Pi Imager workflow. Inlines the six steps from the
   existing `NixOS Pi SD image build on macOS` pattern memory
   (linux-builder VM, generic kernel, redistributable firmware, WiFi
   country code, `nix build`, flash).
3. **First boot**: cold-start uses **Ethernet only** (no WiFi). Plug Pi
   into your router via Ethernet. Wait ~30s for boot + mDNS. SSH should
   work immediately: `ssh root@sc64pi.local`. WiFi-only setups: see
   §10.5 (optional post-bootstrap).
4. **Run the bootstrap script**: `pi-sc64/scripts/bootstrap-pi.sh full
   sc64pi.local`. This handles nixos-rebuild, token provisioning,
   deployer build from `qw-local`. ~5–10 min on first run.
5. **Plug in the cart and camera.**
6. **Run the doctor**: `python3 tools/hil_test_runner.py doctor` —
   expect all 10 blocking probes green (W1/W2 may show as warnings
   until the cart is booted).
7. **Run your first test**: `python3 tools/hil_test_runner.py run
   tests/hil/test_boot_smoke.py`.
8. **If anything fails**: the doctor's first ❌ row and its Fix box are
   authoritative. SETUP.md does not duplicate troubleshooting.

### 10.5 WiFi switchover (optional, post-bootstrap)

After step 7 succeeds on Ethernet, users who want headless WiFi can
either (a) re-flash with `build-sd-image.sh --wifi-ssid X --wifi-psk Y`
or (b) edit `pi-sc64/hosts/pi/configuration.nix` to add wireless config
and re-bootstrap. This is intentionally separate from cold-start
because the WiFi country-code footgun (from prior pattern memory) is
real and Ethernet eliminates that class of failure during initial
bring-up.

This split (§10.4 = "no WiFi for cold start" / §10.5 = "WiFi later")
is the one acknowledged branch in SETUP.md. It is acknowledged
explicitly rather than hidden, per the spec's operability commitment.

### 10.6 Overlap with §6

Section 6 covers **runtime failures of a working, bootstrapped
system**: cart wedged mid-session, debug consumer crashes, etc.
Section 10 covers **cold-start failures** before the system is
bootstrapped: SSH not configured, token never provisioned, image
never flashed. A reader who encounters "Pi unreachable" in both
sections should consult §10 if they're doing first-time setup and §6
if they have a previously-working setup that just stopped responding.

## 11. References

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
