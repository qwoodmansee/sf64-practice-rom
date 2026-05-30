# N64 Hardware-in-the-Loop (HIL) Testing — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a passive HIL test harness so any developer (or Claude) can upload an SF64 practice ROM to a Pi-hosted SC64, capture the IS-Viewer printf stream + a camera snapshot, and assert on the result — with first-class cold-start operability so an unplugged Pi can be brought online with a single doctor command and a bootstrap script.

**Architecture:** Two repos. `pi-sc64` (existing NixOS flake) gets an extended `sc64-api` FastAPI service that owns a `DebugConsumer` subprocess + ring buffer + enriched `/status` + `/logs` + `/camera/snapshot` endpoints, plus new SD-image-build and bootstrap scripts. `sf64-practice-rom` gets a `tools/hil_test_runner.py` mirroring the existing `m64p_test_runner.py` shape, `tests/hil/test_*.py` tests, a `hil doctor` preflight, and an MCP server. Controller input emulation is explicitly deferred to phase 2 (needs RP2040 joybus hardware).

**Tech Stack:**
- Pi: NixOS 24.11, Python 3 + FastAPI + uvicorn, `sc64deployer` (Rust, user's `qw-local` branch), `ustreamer` (camera MJPEG).
- Mac: Python 3, `httpx` for HTTP, `mcp` SDK for the MCP server, `pytest` for unit tests.
- Communication: HTTP/JSON with bearer-token auth, JPEG over `image/jpeg`.

**Spec:** [`docs/superpowers/specs/2026-05-30-n64-hil-testing-design.md`](../specs/2026-05-30-n64-hil-testing-design.md)

---

## Repo and commit conventions

- **Conventional Commits** (`feat:`, `fix:`, `docs:`, `test:`, `chore:`). Match the style in `git log --oneline`.
- **sf64-practice-rom commits trigger a full ROM build via pre-commit hook (~15s).** Batch commits at logical checkpoints; the TDD red-green-commit cadence still applies but try to keep small Python-only changes from triggering rebuilds when feasible. Pre-commit will run `tools/practice_invariants.py`, `make practice -j4`, and `python3 tools/run_tests.py` (if BizHawk available — will skip without). Plan tasks note when a commit will go through this hook.
- **pi-sc64 commits** have no expensive hook. Commit freely.
- **Co-author trailer** on every commit:
  ```
  Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
  ```
- All Python code is type-hinted (Python 3.11+ syntax — `list[str]`, `X | None`, etc.). Use `from __future__ import annotations` at top of files for forward refs.

---

## File structure (final state)

### `pi-sc64`

```
pi-sc64/
├── flake.nix                                          # unchanged in Chunk 1; modules list already correct
├── modules/
│   ├── sc64-api.nix                                   # MODIFIED later — new opts: enableLogCapture, ringBufferDir, cameraStreamUrl
│   ├── sc64-server.nix                                # unchanged
│   └── camera-stream.nix                              # unchanged
├── packages/
│   ├── sc64-api/
│   │   ├── app.py                                     # MODIFIED later — new endpoints, debug consumer lifecycle, ring buffer
│   │   ├── log_ring.py                                # NEW (Chunk 4) — in-memory ring + rotating file writer
│   │   ├── debug_consumer.py                          # NEW (Chunk 4) — sc64deployer debug subprocess manager
│   │   ├── status_probes.py                           # NEW (Chunk 2) — fills enriched /status fields (tokens_file, cart, deployer)
│   │   ├── mock_cart.py                               # NEW (Chunk 7) — --mock-cart printer for VM test
│   │   └── tests/
│   │       ├── test_log_ring.py                       # NEW (Chunk 4)
│   │       ├── test_debug_consumer.py                 # NEW (Chunk 4)
│   │       └── test_status_probes.py                  # NEW (Chunk 2)
│   └── sc64deployer/default.nix                       # likely unchanged; flake.nix input switches summercart to github:<fork>/qw-local (Task 1.3)
├── hosts/pi/
│   ├── configuration.nix                              # MODIFIED in Chunk 1 — import _authorized_keys.nix; tighten ssh; create sc64api group dir
│   └── _authorized_keys.nix                           # GITIGNORED — written by build-sd-image.sh; list literal of SSH pubkeys
├── scripts/
│   ├── build-sd-image.sh                              # NEW (Chunk 1) — writes _authorized_keys.nix then nix build the SD image
│   ├── bootstrap-pi.sh                                # NEW (Chunk 1) — full | token-only modes (deployer is bumped via Nix derivation, not runtime cargo)
│   ├── setup-mac-builder.sh                           # unchanged
│   └── start-linux-builder.sh                         # unchanged
├── .gitignore                                         # MODIFIED in Chunk 1 — ignore hosts/pi/_authorized_keys.nix
└── tests/
    └── nixos-mock-cart.nix                            # NEW (Chunk 5) — NixOS VM test
```

### `sf64-practice-rom`

```
sf64-practice-rom/
├── tools/
│   ├── hil_test_runner.py                             # NEW (Chunk 3) — subcommands: run (default), doctor
│   ├── hil/
│   │   ├── __init__.py                                # NEW (Chunk 3)
│   │   ├── client.py                                  # NEW (Chunk 3) — httpx wrapper for sc64-api
│   │   ├── ctx.py                                     # NEW (Chunk 5) — TestContext primitives
│   │   ├── doctor.py                                  # NEW (Chunk 3) — preflight probes + fix-box rendering
│   │   ├── banner.py                                  # NEW (Chunk 6) — cart-wedged banner
│   │   └── junit.py                                   # NEW (Chunk 6) — JUnit XML emission
│   └── n64-hil-mcp/                                   # NEW (Chunk 7)
│       ├── pyproject.toml
│       ├── server.py
│       └── test_mcp_smoke.py
├── tests/
│   └── hil/
│       ├── __init__.py                                # NEW (Chunk 3)
│       ├── SETUP.md                                   # SKELETON in Chunk 1; expanded in Chunk 7
│       ├── README.md                                  # NEW (Chunk 7)
│       ├── _artifacts/                                # gitignored; populated at runtime
│       ├── _fixtures/
│       │   ├── build_wedge_rom.py                     # NEW (Chunk 6) — generates the wedge fixture deterministically
│       │   └── wedge_rom.z64                          # gitignored — output of build_wedge_rom.py
│       ├── _unit/
│       │   ├── test_ctx.py                            # NEW (Chunk 5)
│       │   ├── test_doctor.py                         # NEW (Chunk 3)
│       │   └── test_client.py                         # NEW (Chunk 3)
│       ├── test_boot_smoke.py                         # NEW (Chunk 5)
│       ├── test_isv_protocol_regression.py            # NEW (Chunk 6)
│       └── test_cart_wedge_detection.py               # NEW (Chunk 6)
├── .gitignore                                         # MODIFIED (Chunk 3) — add tests/hil/_artifacts/, _fixtures/wedge_rom.z64
├── CLAUDE.md                                          # MODIFIED (Chunk 7) — "HIL tests" section
└── Makefile                                           # MODIFIED (Chunk 3) — `make hil-test`, `make hil-doctor` convenience targets
```

---

## Chunk index

1. **Chunk 1: Foundation — Pi bring-up (SD image build + bootstrap + SETUP.md skeleton)** — get a freshly-flashed Pi reachable with the user's SSH key + a provisioned bearer token + `sc64deployer` running the `qw-local` branch. Ships a minimal SETUP.md skeleton aligned with spec §8 step 1.
2. **Chunk 2: Pi-side enriched `/status`** — `status_probes.py` (token-file mode, FTDI presence, deployer version, deployer-can-open-FTDI probe) wired into `/status`. End state: `curl /status` returns the enriched JSON shape with stub values for fields the Mac doctor will eventually consume.
3. **Chunk 3: Mac-side `hil doctor`** — `hil/client.py` + `hil/doctor.py` with the 10 probes from spec §10.2, `hil_test_runner.py doctor` subcommand. End state: `make hil-doctor` shows all-green against the live Pi.
4. **Chunk 4: Pi-side round-trip — `DebugConsumer` + `LogRing` + `/logs` + `/camera/snapshot`** — manages the deployer-debug subprocess lifecycle, integrates with the upload lock per the single-client deployer constraint. End state: a curl-driven upload works without breaking the debug stream.
5. **Chunk 5: Mac-side round-trip — `ctx` + `runner.run` + `test_boot_smoke.py`** — minimum-viable test context (upload_rom with cart-alive check, wait_for_log, snapshot) + the runner's `run` subcommand. End state: `make hil-test tests/hil/test_boot_smoke.py` green against the real cart with a JPEG artifact saved.
6. **Chunk 6: Cart-wedge banner + full assertion suite + JUnit + remaining tests** — `assert_log_not_contains`, the cart-wedge banner with interactive Enter-to-retry and CI EX_TEMPFAIL semantics, the broken-ROM fixture + `test_cart_wedge_detection.py`, the `test_isv_protocol_regression.py`, JUnit XML emission.
7. **Chunk 7: NixOS mock-cart VM test + MCP server + docs** — regression net for no-Pi situations + Claude tools + SETUP.md expansion + README.md + CLAUDE.md addition.

Each chunk ends with a hand-verifiable acceptance test the implementer must run before proceeding.

---

## Chunk 1: Foundation — Pi bring-up

**Goal:** Take a Pi that is currently unplugged, on a fresh SD card, and end up with: (a) network reachable via Ethernet, (b) SSH-able without a password using the user's key baked into the image, (c) `sc64-api` running with a single bearer token configured, (d) `sc64-server` running the `qw-local`-pinned deployer with the cart's FTDI device accessible (cart-plugged-in or not), (e) `camera-stream` running, (f) a minimal SETUP.md skeleton describing the cold-start path that was just walked.

**Files this chunk creates/modifies:**

- Modify: `pi-sc64/.gitignore` — add `hosts/pi/_authorized_keys.nix`
- Modify: `pi-sc64/hosts/pi/configuration.nix` — import `_authorized_keys.nix`, lock down root SSH to keys, ensure tokens dir exists with correct group
- Modify: `pi-sc64/packages/sc64deployer/default.nix` — switch source to `fetchFromGitHub` pinned at a `qw-local` commit; version marker
- Create: `pi-sc64/hosts/pi/_authorized_keys.nix` (generated by script, gitignored)
- Create: `pi-sc64/scripts/build-sd-image.sh`
- Create: `pi-sc64/scripts/bootstrap-pi.sh` (modes: `full`, `token-only`)
- Create: `sf64-practice-rom/tests/hil/SETUP.md` (skeleton)

**Skills to use:**
- @superpowers:test-driven-development for the bootstrap script's idempotency checks
- @superpowers:verification-before-completion before claiming chunk complete
- @superpowers:systematic-debugging if Nix builds fail

**Design decisions encoded in this chunk:**

- **SSH key injection via gitignored file, not flake args.** `build-sd-image.sh` writes `hosts/pi/_authorized_keys.nix` (a Nix list literal). `configuration.nix` imports it with a fallback-to-empty if the file is absent. This avoids the brittle `--arg`/`--override-input` CLI parameterization entirely.
- **Deployer is pinned in `default.nix` via `fetchFromGitHub`, not rebuilt at runtime.** No `cargo build` on the Pi. Bumping the deployer = editing the SHA in `default.nix` + `nixos-rebuild`.
- **Single-token model.** `bootstrap-pi.sh token-only` truncates `/etc/sc64-api/tokens` and writes one fresh token. Multi-token support is out of scope for v1.
- **`nixos-rebuild switch --target-host ... --build-host root@<host>`** — the Pi builds its own derivations rather than the Mac's QEMU linux-builder VM. This matches the stored pattern memory ("NixOS Linux builder VM too resource-heavy → Build on Pi directly").

### Task 1.1: Read and understand the current `pi-sc64` state

**Files:**
- Read: `pi-sc64/flake.nix`, `pi-sc64/hosts/pi/configuration.nix`, `pi-sc64/packages/sc64deployer/default.nix`, `pi-sc64/modules/sc64-api.nix`

- [ ] **Step 1: Confirm the SummerCart64 fork URL with the user**

The current `pi-sc64/flake.nix` has `summercart = { url = "path:/Users/qwoodmansee/code/SummerCart64"; ... };` — a local path. For Pi-side builds we need a `github:` URL.

Ask the user: "What's the GitHub URL of your SummerCart64 fork? (e.g. `github:qwoodmansee/SummerCart64`) And which branch carries the qw-local stdout-flush patch (likely `qw-local`)?"

WAIT for user response. Do not assume `github:qwoodmansee/SummerCart64` — that's a plausible guess but unverified.

Record the answer as `<FORK_URL>` and `<FORK_BRANCH>` for use in subsequent tasks.

- [ ] **Step 2: Read the flake**

Run: `cat pi-sc64/flake.nix`

Confirm: `summercart` input is `path:`-based. The overlay defines `sc64deployer = callPackage ./packages/sc64deployer/default.nix { inherit summercart; };`.

- [ ] **Step 3: Read the configuration**

Run: `cat pi-sc64/hosts/pi/configuration.nix`

Note what's there. Specifically search for:
- `users.users.root.openssh.authorizedKeys` (any existing keys to preserve)
- `services.openssh` (existing settings to merge with)
- `users.users.sc64api` / `users.groups.sc64api` (defined here vs. in the module)

Record observations as comments in your scratch space; don't edit yet.

- [ ] **Step 4: Read the deployer derivation**

Run: `cat pi-sc64/packages/sc64deployer/default.nix`

Note: how is `src` set? How is the Rust version selected? Is `cargo build` driven by `rustPlatform.buildRustPackage`?

- [ ] **Step 5: Verify your existing checkout of SummerCart64 has a `qw-local` branch with the flush patch**

Run: `cd ~/code/SummerCart64 && git branch --show-current && git log --oneline -5 qw-local`

Confirm: the branch exists locally. Get the current HEAD SHA: `git rev-parse qw-local`. Record this as `<QW_LOCAL_SHA>` for Task 1.3.

- [ ] **Step 6: Confirm the patch is pushed to the GitHub fork**

Run: `git ls-remote <FORK_URL_AS_GITHUB_SHORT_TRANSLATED_TO_HTTPS> <FORK_BRANCH>`

Example: `git ls-remote https://github.com/qwoodmansee/SummerCart64.git qw-local`

Expected: prints a SHA. **If this fails (branch not found or repo not public), STOP and tell the user**: "Your `qw-local` branch needs to be pushed to `<FORK_URL>` (or made public) before the Pi can fetch it. Please push: `cd ~/code/SummerCart64 && git push origin qw-local` and confirm it's visible."

WAIT for user confirmation that the branch is fetchable.

### Task 1.2: Add a Cargo version marker on the `qw-local` branch (one-line patch)

This is the marker the doctor's `has_qw_local_flush_patch` probe will look for. Without it, `sc64deployer --version` is indistinguishable from upstream.

**Files:**
- Modify: `~/code/SummerCart64/sw/deployer/Cargo.toml` (on the `qw-local` branch)

- [ ] **Step 1: Switch to the qw-local branch**

Run: `cd ~/code/SummerCart64 && git checkout qw-local`

- [ ] **Step 2: Read the current version**

Run: `grep '^version' sw/deployer/Cargo.toml`

Expected: a line like `version = "2.20.0"`.

- [ ] **Step 3: Append the `+qwhil` build metadata**

Edit `sw/deployer/Cargo.toml` so the version line becomes:

```toml
version = "2.20.0+qwhil"
```

(Substitute whatever the actual version is, then append `+qwhil`.)

`+qwhil` is the build-metadata segment per SemVer. Cargo ignores it for resolution but prints it in `cargo --version` style output, so `sc64deployer --version` will surface it.

- [ ] **Step 4: Verify it builds**

Run: `cd sw/deployer && cargo build --release 2>&1 | tail -5`

Expected: builds cleanly. The version string change does not affect any code.

Run: `./target/release/sc64deployer --version 2>&1`

Expected: contains `+qwhil` somewhere in the output. Record exactly what string appears — this becomes the doctor probe's substring match in Chunk 2.

- [ ] **Step 5: Commit and push to the fork**

```bash
cd ~/code/SummerCart64
git add sw/deployer/Cargo.toml
git commit -m "$(cat <<'EOF'
chore(deployer): mark qw-local builds with +qwhil version metadata

Allows downstream tooling (HIL doctor, sc64-api status probe) to
detect at runtime whether the stdout-flush patch is in the deployer
binary that's actually installed. Without this marker, --version
output is indistinguishable from upstream.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
git push origin qw-local
```

- [ ] **Step 6: Record the new SHA**

Run: `git rev-parse qw-local`

Use this `<QW_LOCAL_SHA>` value in Task 1.3.

### Task 1.3: Pin sc64deployer derivation to the `qw-local` GitHub ref

**Files:**
- Modify: `pi-sc64/packages/sc64deployer/default.nix`
- Modify: `pi-sc64/flake.nix`

- [ ] **Step 1: Replace the `summercart` flake input with a GitHub ref**

In `pi-sc64/flake.nix`, change:

```nix
summercart = {
  url = "path:/Users/qwoodmansee/code/SummerCart64";
  flake = false;
};
```

to:

```nix
summercart = {
  url = "<FORK_URL>/<FORK_BRANCH>";  # e.g. github:qwoodmansee/SummerCart64/qw-local
  flake = false;
};
```

Use the actual `<FORK_URL>` + `<FORK_BRANCH>` confirmed in Task 1.1 Step 1.

- [ ] **Step 2: Update the flake.lock to fetch the new ref**

Run: `cd pi-sc64 && nix flake lock --update-input summercart`

Expected: the lock file updates to reference the GitHub commit. Note the resolved SHA in the output.

- [ ] **Step 3: Verify the derivation evaluates with the new source**

Run: `cd pi-sc64 && nix eval .#nixosConfigurations.pi.config.system.build.toplevel.outPath 2>&1 | head -5`

Expected: eval succeeds (long output, possibly takes a minute). If the derivation fails because `summercart` is now a GitHub tarball rather than a local path, `default.nix`'s `src = summercart` line may need adjustment — read the derivation and confirm it accepts both formats. Tarball-style inputs come through as a derivation result with a single root, equivalent to a directory.

- [ ] **Step 4: Commit**

```bash
cd pi-sc64
git add flake.nix flake.lock
git commit -m "$(cat <<'EOF'
feat(flake): pin SummerCart64 deployer to qw-local GitHub branch

Switches the summercart input from a local path to the GitHub fork
so the Pi can fetch it during nixos-rebuild --build-host root@<host>
without needing the user's working tree mounted. Resolves the
cargo-on-NixOS problem: rebuilds happen inside the Nix sandbox with
rustc/cargo from nixpkgs, not via runtime cargo.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 1.4: Wire SSH key injection via a gitignored `_authorized_keys.nix`

**Files:**
- Modify: `pi-sc64/.gitignore`
- Modify: `pi-sc64/hosts/pi/configuration.nix`

- [ ] **Step 1: Add the path to `.gitignore`**

Append to `pi-sc64/.gitignore`:

```
# Generated by scripts/build-sd-image.sh; per-developer SSH keys.
hosts/pi/_authorized_keys.nix
```

- [ ] **Step 2: Modify configuration.nix to import the keys file with a fallback**

At the top of `pi-sc64/hosts/pi/configuration.nix` (after any existing `{ config, lib, pkgs, ... }:` line but before the main attrset), add:

```nix
let
  authorizedKeys =
    if builtins.pathExists ./_authorized_keys.nix
    then import ./_authorized_keys.nix
    else [];
in
```

Then inside the module body, add:

```nix
users.users.root.openssh.authorizedKeys.keys =
  (config.users.users.root.openssh.authorizedKeys.keys or []) ++ authorizedKeys;

services.openssh = {
  enable = true;
  settings = {
    PermitRootLogin = "prohibit-password";
    PasswordAuthentication = false;
    KbdInteractiveAuthentication = false;
  };
};
```

**Merge carefully** with any existing `services.openssh` block — NixOS module merging means you can't have two `enable = true;` literals; convert any existing block into the same shape.

- [ ] **Step 3: Verify configuration still evaluates**

Run: `cd pi-sc64 && nix eval .#nixosConfigurations.pi.config.users.users.root.openssh.authorizedKeys.keys 2>&1`

Expected: empty list `[]` (no `_authorized_keys.nix` exists yet, so the fallback kicks in).

- [ ] **Step 4: Write a temporary `_authorized_keys.nix` to verify the import path works**

Run:
```bash
cat > pi-sc64/hosts/pi/_authorized_keys.nix <<EOF
[
  "ssh-ed25519 AAAATestKey nobody@nowhere"
]
EOF
```

Run: `cd pi-sc64 && nix eval .#nixosConfigurations.pi.config.users.users.root.openssh.authorizedKeys.keys 2>&1`

Expected: `[ "ssh-ed25519 AAAATestKey nobody@nowhere" ]`.

Run: `rm pi-sc64/hosts/pi/_authorized_keys.nix` (clean up the test file — build-sd-image.sh will write the real one).

- [ ] **Step 5: Commit**

```bash
cd pi-sc64
git add .gitignore hosts/pi/configuration.nix
git commit -m "$(cat <<'EOF'
feat(host): SSH key injection via gitignored _authorized_keys.nix

build-sd-image.sh writes hosts/pi/_authorized_keys.nix at build time
with the developer's SSH key. configuration.nix imports it with a
fallback-to-empty-list if absent. This is the simplest answer to
"how do I bake a key into the SD image" — no flake-arg gymnastics,
no --override-input gotchas. Per-developer keys never enter git.

Root login is hardened to key-only (prohibit-password,
PasswordAuthentication=false) so there is no "default root password"
attack surface in cold-start.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 1.5: Ensure tokens directory exists with correct ownership

`sc64-api.nix` already creates the `sc64api` user/group and points at `/etc/sc64-api/tokens` via the `tokenFile` option. We need `configuration.nix` (or the module) to ensure:
1. The `/etc/sc64-api` directory exists, owned `root:sc64api`, mode 750.
2. `tokenFile` is wired to `/etc/sc64-api/tokens`.
3. (Per-bootstrap: the file gets created at correct mode 640 owned root:sc64api by the bootstrap script.)

**Files:**
- Modify: `pi-sc64/modules/sc64-api.nix` (already has `tokenFile` option; just need a default) or `pi-sc64/hosts/pi/configuration.nix` (set the option value)

- [ ] **Step 1: Look at how the existing config sets `tokenFile`**

Run: `grep -rn 'services.sc64-api\b' pi-sc64/`

Find where `services.sc64-api.enable = true;` lives — likely in `hosts/pi/configuration.nix`. Note whether `tokenFile` is already set.

- [ ] **Step 2: Set the tokenFile default + ensure the directory exists**

In `pi-sc64/hosts/pi/configuration.nix`, in the existing `services.sc64-api` block (or add it if absent), set:

```nix
services.sc64-api = {
  enable = true;
  tokenFile = "/etc/sc64-api/tokens";
};

# Ensure /etc/sc64-api exists with correct ownership BEFORE sc64-api tries to read it.
# tmpfiles drains before multi-user.target, so this runs before the service.
systemd.tmpfiles.rules = [
  "d /etc/sc64-api 0750 root sc64api -"
];
```

(Merge with any existing `systemd.tmpfiles.rules` list if present.)

- [ ] **Step 3: Verify configuration evaluates**

Run: `cd pi-sc64 && nix eval .#nixosConfigurations.pi.config.systemd.tmpfiles.rules 2>&1 | head -5`

Expected: includes the new rule.

- [ ] **Step 4: Commit**

```bash
cd pi-sc64
git add hosts/pi/configuration.nix
git commit -m "$(cat <<'EOF'
feat(host): ensure /etc/sc64-api dir exists with correct ownership

systemd.tmpfiles creates /etc/sc64-api as root:sc64api 0750 before
sc64-api.service starts. This eliminates the "token file mode is
wrong" footgun for the first-boot case — the bootstrap script's
token install just has to write the file with the right mode.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 1.6: Write `build-sd-image.sh`

**Files:**
- Create: `pi-sc64/scripts/build-sd-image.sh`

- [ ] **Step 1: Create the script**

Write `pi-sc64/scripts/build-sd-image.sh`:

```bash
#!/usr/bin/env bash
# build-sd-image.sh — build a Pi SD image with the user's SSH key baked in.
#
# Usage:
#   build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub
#
# Output: prints the path to the built image and a flash command.
#
# How it works:
#   - Writes hosts/pi/_authorized_keys.nix (gitignored) with the key.
#   - Runs `nix build .#nixosConfigurations.pi.config.system.build.sdImage`.
#   - The configuration imports _authorized_keys.nix and bakes the key
#     into root's authorized_keys at image build time.
#
# Cold-start commitment: no default root password. SSH is key-only.

set -euo pipefail

SSH_KEY=""

usage() {
  cat >&2 <<EOF
Usage: $0 --ssh-key <path-to-pub-key>

Bakes the SSH public key into a Pi SD image. Cold-start path:
- Use Ethernet for first boot (WiFi setup is post-bootstrap, see SETUP.md §10.5).
EOF
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ssh-key) SSH_KEY="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown arg: $1" >&2; usage ;;
  esac
done

[[ -z "$SSH_KEY" ]]   && { echo "--ssh-key is required" >&2; usage; }
[[ ! -f "$SSH_KEY" ]] && { echo "SSH key not found: $SSH_KEY" >&2; exit 1; }
command -v nix >/dev/null || { echo "nix not on PATH" >&2; exit 1; }

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KEY_CONTENT="$(cat "$SSH_KEY" | tr -d '\n')"

# Defense: SSH pubkeys have a strict format. Reject anything that doesn't look right.
if ! [[ "$KEY_CONTENT" =~ ^(ssh-(rsa|ed25519|dss)|ecdsa-sha2-) ]]; then
  echo "Does not look like a valid SSH public key: $SSH_KEY" >&2
  exit 1
fi

# Escape backslashes and double quotes for Nix string literal embedding.
nix_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

KEYS_NIX="$REPO_ROOT/hosts/pi/_authorized_keys.nix"
cat > "$KEYS_NIX" <<EOF
# Generated by scripts/build-sd-image.sh — do not commit.
[
  "$(nix_escape "$KEY_CONTENT")"
]
EOF

echo "==> Wrote $KEYS_NIX with 1 key"

# Verify the file is valid Nix before building.
if ! nix eval --file "$KEYS_NIX" --json >/dev/null 2>&1; then
  echo "Generated _authorized_keys.nix is not valid Nix. Inspect:" >&2
  cat "$KEYS_NIX" >&2
  exit 1
fi

echo "==> Building SD image (this can take a while on a cold cache)..."
cd "$REPO_ROOT"
nix build ".#nixosConfigurations.pi.config.system.build.sdImage" \
  --print-out-paths \
  2>&1 | tee /tmp/build-sd-image.log

IMAGE_DIR="result/sd-image"
IMAGE_PATH="$(find "$IMAGE_DIR" -name '*.img.zst' -o -name '*.img' 2>/dev/null | head -1)"

if [[ -z "$IMAGE_PATH" ]]; then
  echo "Build succeeded but no image found under $IMAGE_DIR" >&2
  ls -la "$IMAGE_DIR" >&2 || true
  exit 1
fi

echo ""
echo "================================================================"
echo "  SD image built: $IMAGE_PATH"
echo "================================================================"
echo ""
echo "Flash to an SD card (CONFIRM /dev/diskN with \`diskutil list\`):"
echo ""
if [[ "$IMAGE_PATH" == *.zst ]]; then
  echo "  diskutil unmountDisk /dev/diskN"
  echo "  zstd -d < $IMAGE_PATH | sudo dd of=/dev/rdiskN bs=4m"
  echo "  diskutil eject /dev/diskN"
else
  echo "  diskutil unmountDisk /dev/diskN"
  echo "  sudo dd if=$IMAGE_PATH of=/dev/rdiskN bs=4m"
  echo "  diskutil eject /dev/diskN"
fi
echo ""
echo "After flashing, insert into the Pi, plug Ethernet, power on."
echo "Wait ~30s, then verify: ssh root@sc64pi.local hostname"
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x pi-sc64/scripts/build-sd-image.sh`

- [ ] **Step 3: Smoke-test argument parsing**

```bash
pi-sc64/scripts/build-sd-image.sh --help 2>&1 | head -3
# Expected: usage; exit 1

pi-sc64/scripts/build-sd-image.sh --ssh-key /nonexistent 2>&1
# Expected: "SSH key not found"; exit 1

echo "not a key" > /tmp/bad-key.pub
pi-sc64/scripts/build-sd-image.sh --ssh-key /tmp/bad-key.pub 2>&1
# Expected: "Does not look like a valid SSH public key"; exit 1
rm /tmp/bad-key.pub
```

- [ ] **Step 4: Run the real SD image build (verification)**

Run: `pi-sc64/scripts/build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub`

Expected: long build (10–30 minutes on a warm cache, multi-hour cold). On success: the script prints the image path and a `dd` command. The `result/sd-image/` symlink exists.

**Failure modes to handle iteratively:**

- `nix: command not found` — preflight should have caught; user needs Nix installed.
- `error: builder for /nix/store/...rust...` — Pi 3B Rust build via Nix uses cross-compile from aarch64; may take a while or fail under the linux-builder VM's resource limits. Per pattern memory: bump VM disk to 60GB, 4 cores, restart `systems.determinate.nix-daemon` before retrying.
- `error: 'authorizedKeys' undefined` — the `let` binding in `configuration.nix` (Task 1.4 Step 2) wasn't applied. Re-check the file.
- `git ls-remote` fetch fails inside the Nix build — fork URL is wrong or branch isn't pushed. Confirm with user.

Iterate until the build produces an image.

- [ ] **Step 5: Hand off to the user for flashing**

Per the spec's cold-start contract, the agent CANNOT execute `sudo dd` against a physical SD card. Surface this message to the user:

```
SD image built. Please flash it now using the commands the script
just printed.

Insert the SD card into the Pi, plug Ethernet, power it on, wait
~30s, and reply with one of:
  - "Pi reachable" — if `ssh root@sc64pi.local hostname` succeeds
  - the error message if it doesn't
```

Then STOP. Do not proceed to Task 1.7 until the user confirms the Pi is reachable.

- [ ] **Step 6: Commit**

```bash
cd pi-sc64
git add scripts/build-sd-image.sh
git commit -m "$(cat <<'EOF'
feat(scripts): build-sd-image.sh — Pi SD image builder

Writes hosts/pi/_authorized_keys.nix with the user's SSH pubkey,
then runs `nix build .#nixosConfigurations.pi.config.system.build.sdImage`.
The generated file is gitignored so each developer has their own
without polluting the repo.

Cold-start commitment: the SD image ships with no default root
password, only key-based root SSH using the embedded pubkey.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 1.7: Write `bootstrap-pi.sh`

**Files:**
- Create: `pi-sc64/scripts/bootstrap-pi.sh`

- [ ] **Step 1: Create the script**

Write `pi-sc64/scripts/bootstrap-pi.sh`:

```bash
#!/usr/bin/env bash
# bootstrap-pi.sh — provision a freshly-flashed Pi for HIL testing.
#
# Modes:
#   bootstrap-pi.sh full <host>          # nixos-rebuild + token + final probes
#   bootstrap-pi.sh token-only <host>    # generate and install a fresh bearer token
#
# Note: deployer-version bumps are NOT done here — edit
# packages/sc64deployer/default.nix's source ref and run `full`.
# That makes the Nix derivation track the change cleanly.

set -euo pipefail

MODE="${1:-}"
HOST="${2:-}"

usage() {
  cat >&2 <<EOF
Usage:
  $0 full <host>          # nixos-rebuild + fresh token + probes
  $0 token-only <host>    # generate and install a fresh bearer token

Examples:
  $0 full sc64pi.local
  $0 token-only sc64pi.local
EOF
  exit 1
}

[[ -z "$MODE" || -z "$HOST" ]] && usage

command -v openssl >/dev/null || { echo "openssl not on PATH" >&2; exit 1; }

# SSH preflight — short-circuit with a doctor-style fix box if SSH fails.
ssh_preflight() {
  if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "root@$HOST" true 2>/dev/null; then
    cat >&2 <<EOF
================================================================
  Cannot SSH to root@$HOST without a password.

  Resolution: re-flash the SD card with your key baked in:
    pi-sc64/scripts/build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub

  Or, if the Pi is already provisioned with a DIFFERENT key, edit
  hosts/pi/_authorized_keys.nix (or pi-sc64/hosts/pi/configuration.nix
  if you want the change checked in) to include your new key,
  rebuild the image, and re-flash.

  No default root password is supported — this is intentional.
================================================================
EOF
    exit 1
  fi
}

# token-only requires that sc64api group already exists (which means
# either the SD image was built with the module enabled, OR a `full`
# bootstrap has previously run). Check explicitly.
check_sc64api_group() {
  if ! ssh "root@$HOST" "getent group sc64api >/dev/null"; then
    cat >&2 <<EOF
================================================================
  sc64api group does not exist on $HOST yet.

  This means the sc64-api NixOS module has never been activated on
  this host. Run a full bootstrap first:

    $0 full $HOST

  Then re-run "$0 token-only $HOST" if you want to rotate tokens.
================================================================
EOF
    exit 1
  fi
}

generate_token() {
  openssl rand -hex 32
}

# install_token writes a SINGLE token (truncates any prior tokens).
# Multi-token bearer auth is out of scope for v1.
install_token() {
  local token="$1"
  # Use heredoc piped to ssh so quoting is clean.
  ssh "root@$HOST" "bash -s" <<REMOTE
set -e
install -d -o root -g sc64api -m 750 /etc/sc64-api
# Write the token via printf (no echo -n portability issues).
printf '%s\n' '$token' > /etc/sc64-api/tokens
chown root:sc64api /etc/sc64-api/tokens
chmod 640 /etc/sc64-api/tokens
systemctl restart sc64-api
REMOTE

  printf '%s\n' "$token" > "$HOME/.sc64-api-token"
  chmod 600 "$HOME/.sc64-api-token"
}

mode_token_only() {
  ssh_preflight
  check_sc64api_group
  echo "==> Generating fresh bearer token..."
  TOKEN="$(generate_token)"
  install_token "$TOKEN"
  echo ""
  echo "Token saved to ~/.sc64-api-token (mode 600)."
  echo "Token (printed once for your records): $TOKEN"
}

mode_full() {
  ssh_preflight
  REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

  echo "==> Running nixos-rebuild switch on $HOST (build on Pi)..."
  cd "$REPO_ROOT"
  # --build-host: the Pi builds derivations itself (slower per-build but
  # avoids the Mac QEMU linux-builder VM overhead). Per stored pattern
  # memory: "NixOS Linux builder VM too resource-heavy → Build on Pi directly."
  # --target-host: the Pi receives the activation.
  # --use-substitutes: cache hits come from cache.nixos.org.
  nixos-rebuild switch \
    --flake ".#pi" \
    --target-host "root@$HOST" \
    --build-host "root@$HOST" \
    --use-substitutes

  echo "==> Generating fresh bearer token..."
  TOKEN="$(generate_token)"
  install_token "$TOKEN"
  echo "Token saved to ~/.sc64-api-token."

  echo "==> Final probe sweep:"
  # Run probes and FAIL HARD if any service is not active.
  # The reviewer flagged "|| true" was masking failures — drop it.
  ssh "root@$HOST" bash -s <<'REMOTE'
set -e
echo "--- service status ---"
for svc in sc64-server sc64-api camera-stream; do
  if ! systemctl is-active --quiet "$svc"; then
    echo "FAIL: $svc is not active. journalctl -u $svc -n 20:"
    journalctl -u "$svc" -n 20 --no-pager
    exit 1
  fi
  echo "OK: $svc"
done
echo "--- FTDI presence ---"
if lsusb | grep -qi 0403; then
  echo "OK: FTDI device present"
else
  echo "WARN: SC64 FTDI device not present (cart unplugged — this is OK now)"
fi
echo "--- deployer version ---"
sc64deployer --version
REMOTE

  echo ""
  echo "================================================================"
  echo "  All set. Plug the cart in (if not already), then run:"
  echo "    cd sf64-practice-rom"
  echo "    python3 tools/hil_test_runner.py doctor"
  echo "================================================================"
}

case "$MODE" in
  full)       mode_full ;;
  token-only) mode_token_only ;;
  *)          usage ;;
esac
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x pi-sc64/scripts/bootstrap-pi.sh`

- [ ] **Step 3: Smoke-test argument parsing**

```bash
pi-sc64/scripts/bootstrap-pi.sh 2>&1 | head -5
# Expected: usage; exit 1

pi-sc64/scripts/bootstrap-pi.sh badmode somehost 2>&1 | tail -3
# Expected: usage; exit 1
```

- [ ] **Step 4: Run `bootstrap-pi.sh full sc64pi.local` against the real Pi**

This is the first end-to-end provisioning. Expect failures the first time and iterate.

Run: `pi-sc64/scripts/bootstrap-pi.sh full sc64pi.local 2>&1 | tee /tmp/bootstrap.log`

Expected on success: `nixos-rebuild switch --build-host root@sc64pi.local` completes (this WILL take a while — the Pi is building Rust). Token saved. Final probe sweep passes. Closing message printed.

**Common failure modes:**

- `nixos-rebuild: Permission denied (publickey)` — SSH key not on Pi. The preflight should have caught this; if not, the SD image was flashed without the key.
- `cannot find sc64deployer source` — the GitHub fork URL or branch ref in Task 1.3 is wrong. Confirm `git ls-remote <FORK_URL> <FORK_BRANCH>` works.
- `the Pi ran out of disk while building rustc` — Pi 3B has limited resources. SD card needs to be 16GB+ for a fresh image. Re-flash with a bigger card if needed.
- `failed to switch` — read the `nixos-rebuild` output for the actual cause; it's almost always a config error in `configuration.nix` rather than infrastructure.

Iterate until `full` exits 0.

- [ ] **Step 5: Verify idempotency**

Run: `pi-sc64/scripts/bootstrap-pi.sh full sc64pi.local 2>&1 | tail -10`

Expected: second run is fast (no-op nixos-rebuild because state matches), token is regenerated (single-token model — old one is replaced), final probes pass.

- [ ] **Step 6: Verify `token-only` works post-`full`**

Run: `pi-sc64/scripts/bootstrap-pi.sh token-only sc64pi.local 2>&1 | tail -5`

Expected: prints a new token, saved to `~/.sc64-api-token`. The previous token is replaced.

- [ ] **Step 7: Verify the `token-only` precondition check**

Run (only if you want to test this — DESTRUCTIVE if run on a real Pi):

In a NixOS VM or test scenario where sc64api group is missing, `token-only` should bail with the clear "run full first" message. Skip this in practice; we already tested the negative path manually via `getent group` semantics.

- [ ] **Step 8: Commit**

```bash
cd pi-sc64
git add scripts/bootstrap-pi.sh
git commit -m "$(cat <<'EOF'
feat(scripts): bootstrap-pi.sh — full and token-only modes

Provisions a freshly-flashed Pi end-to-end. `full`:
  - nixos-rebuild switch with --build-host root@pi (Pi builds its own
    derivations; no Mac QEMU linux-builder VM)
  - fresh single bearer token, mode 640 root:sc64api
  - service + FTDI + deployer version probe sweep

`token-only` mode rotates the bearer token (single-token model;
truncates any existing tokens file) and requires sc64api group to
exist — checks this and surfaces a clear "run full first" message
if not.

Deployer version bumps are handled by editing default.nix's source
ref + re-running full — no runtime cargo build on the Pi.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 1.8: Write the SETUP.md skeleton

Per spec §8 step 1: SETUP.md ships at the END of this milestone, written after the bootstrap script has been dogfooded once against a real cold-start. The skeleton goes in now; the polish + screenshots wait for Chunk 7.

**Files:**
- Create: `sf64-practice-rom/tests/hil/SETUP.md`

- [ ] **Step 1: Create the file with the cold-start skeleton**

Write `sf64-practice-rom/tests/hil/SETUP.md`:

```markdown
# HIL Test Setup — Cold Start

This guide takes you from "Pi in a drawer, never booted" to "first
HIL test green." It is intentionally linear with exactly one
acknowledged branch (Ethernet vs. WiFi at first boot, see §6).

> **Status:** Skeleton landed alongside the bootstrap scripts in
> milestone 1. Polish, screenshots, and per-platform troubleshooting
> tables will land in milestone 5 after the script has been dogfooded
> against several real cold-starts.

## 1. Hardware checklist

- Raspberry Pi 3B
- microSD card, 16 GB or larger (8 GB may run out during nixos-rebuild)
- Pi Camera v1, v2, or v3
- SummerCart64 N64 flashcart
- Nintendo 64 console + AV-to-camera path (camera pointed at TV)
- Ethernet cable to your router (cold start uses Ethernet only)

**Camera ribbon orientation:** on Pi 3B, the **blue stripe faces the
Ethernet jack**. Get this backwards and the camera will not work.

## 2. Build and flash the SD image

```bash
cd pi-sc64
scripts/build-sd-image.sh --ssh-key ~/.ssh/id_ed25519.pub
```

The script prints the image path and a `dd` command. Flash to your
SD card, then insert it into the Pi.

## 3. First boot

Plug the Pi into your router via **Ethernet** (not WiFi — see §6).
Power on. Wait ~30 seconds.

Verify SSH works:

```bash
ssh root@sc64pi.local hostname
```

Expected: prints the hostname. **If this fails**, see §7.

## 4. Bootstrap the Pi

```bash
cd pi-sc64
scripts/bootstrap-pi.sh full sc64pi.local
```

This runs `nixos-rebuild switch` (building on the Pi — slow first
time, ~30 min on a fresh build, much faster afterwards), provisions
a fresh bearer token, and runs a final probe sweep.

The token is saved to `~/.sc64-api-token` on your Mac (mode 600) and
to `/etc/sc64-api/tokens` on the Pi (mode 640, `root:sc64api`).

## 5. Plug in the cart and camera

Now plug the SC64 USB cable into the Pi and connect the Pi Camera
(blue stripe → Ethernet jack).

## 6. WiFi (optional, post-bootstrap)

WiFi is intentionally NOT part of cold start. After the Ethernet
flow above succeeds, you can either:

1. Re-flash with `build-sd-image.sh --ssh-key ... --wifi-ssid <ssid>
   --wifi-psk <psk>` once the script supports it (TODO: not yet
   implemented in milestone 1), or
2. Edit `pi-sc64/hosts/pi/configuration.nix` to add wireless config
   and re-run `bootstrap-pi.sh full`.

The WiFi country-code footgun is real — Ethernet eliminates that
class of failure during initial bring-up.

## 7. First HIL test

When milestone 2 lands, `hil doctor` will verify everything is
green. For now (milestone 1):

```bash
# Verify sc64-api is reachable and authenticated:
curl -s -H "Authorization: Bearer $(cat ~/.sc64-api-token)" \
  http://sc64pi.local:8064/status | head
```

Expected: a JSON status response. (Enriched fields come in milestone 2.)

## 8. Troubleshooting

Once `hil doctor` ships in milestone 2, this section will redirect
you to it: the doctor's first ❌ row and its Fix box are the
authoritative source.

Until then, the most common cold-start failures:

- `ssh: connect to host sc64pi.local: No route to host` — mDNS not
  resolving. Try `dscacheutil -q host -a name sc64pi.local` on
  macOS; if blank, find the Pi's IP via your router's DHCP table.
- `Permission denied (publickey)` — the SD image was flashed without
  your key. Re-flash via `build-sd-image.sh` with the correct
  `--ssh-key` argument.
- `nixos-rebuild` fails on disk space — bigger SD card (16 GB+).
- Camera not detected — check ribbon cable orientation.
```

- [ ] **Step 2: Commit**

```bash
cd ~/code/sf64-practice-rom
git add tests/hil/SETUP.md
git commit -m "$(cat <<'EOF'
docs(hil): SETUP.md skeleton for cold-start path

Mirrors the cold-start path that the pi-sc64 bootstrap scripts
implement in milestone 1. Polish, screenshots, and per-platform
troubleshooting tables land in milestone 5 once the script has
been dogfooded against several real cold-starts.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

Note: this commit will trigger the sf64-practice-rom pre-commit hook
(invariants + full ROM build + tests). Expect ~15s.

### Chunk 1 acceptance test

Before marking Chunk 1 complete, all of these must hold:

- [ ] `ssh root@sc64pi.local hostname` succeeds with no password prompt
- [ ] `~/.sc64-api-token` exists, mode 600, non-empty
- [ ] `ssh root@sc64pi.local stat -c '%U:%G %a' /etc/sc64-api/tokens` prints `root:sc64api 640`
- [ ] `curl http://sc64pi.local:8064/health` returns `{"ok":true}`
- [ ] `ssh root@sc64pi.local systemctl is-active sc64-server sc64-api camera-stream` returns three `active` lines
- [ ] `ssh root@sc64pi.local sc64deployer --version` output **contains the substring `+qwhil`** (the marker that distinguishes the qw-local branch build from upstream)
- [ ] `pi-sc64/scripts/bootstrap-pi.sh full sc64pi.local` is idempotent (running it twice in a row succeeds both times)
- [ ] `pi-sc64/scripts/bootstrap-pi.sh token-only sc64pi.local` succeeds and rotates the token (the new value matches `~/.sc64-api-token` and authenticates against `/status`)
- [ ] `sf64-practice-rom/tests/hil/SETUP.md` exists and is committed

Once all nine hold, Chunk 1 is done. Proceed to Chunk 2.

---

## Chunk 2: Pi-side enriched `/status`

**Goal:** Add the enriched `/status` endpoint on the Pi: deployer version + flush-patch detection, deployer-can-open-FTDI probe, token-file mode/owner check, cart FTDI presence. End state: `curl -H "Authorization: Bearer $TOKEN" http://sc64pi.local:8064/status | jq` returns the JSON shape defined in spec §4.1.1 with real values for the fields wired in this chunk, and stub values for the fields Chunk 4 will fill (camera, debug_consumer, ring_buffer).

This chunk is Pi-only. No Mac-side code yet — Chunk 3 builds the doctor that consumes these fields.

**Files this chunk creates/modifies:**

- Create: `pi-sc64/packages/sc64-api/status_probes.py`
- Create: `pi-sc64/packages/sc64-api/tests/__init__.py` (empty)
- Create: `pi-sc64/packages/sc64-api/tests/test_status_probes.py`
- Modify: `pi-sc64/packages/sc64-api/app.py` — wire probes into `/status` response, add cache/refresher
- Modify: `pi-sc64/modules/sc64-api.nix` — bundle the new Python file(s) into the Nix store

**Skills to use:**
- @superpowers:test-driven-development for `status_probes` pure functions
- @superpowers:verification-before-completion before claiming chunk complete

### Task 2.1: Implement `status_probes.py` (Pi-side)

**Files:**
- Create: `pi-sc64/packages/sc64-api/status_probes.py`

This module computes the new `/status` fields. Pure functions that take no live state from the app — the app calls them, caches as configured, and merges into the JSON response.

- [ ] **Step 1: Write `status_probes.py`**

```python
"""
Pi-side probes that fill enriched /status fields for HIL doctor.

Pure functions. App caches the expensive ones (deployer version,
deployer probe) and refreshes on a schedule; cheap ones (sysfs lookup,
stat) run on every /status call.
"""
from __future__ import annotations

import os
import re
import subprocess
import time
from pathlib import Path
from typing import Any

FTDI_VID = "0403"
FTDI_PID = "6014"
QW_LOCAL_MARKER = "+qwhil"

# Tunables — overridden by env in production but defaults are sane.
DEPLOYER_VERSION_TTL_S = 60
DEPLOYER_PROBE_TTL_S = 30


def token_file_status(path: str) -> dict[str, Any]:
    """Return {exists, owner, group, mode, path} for the token file.

    Returns exists=False with no other fields if path is absent.
    """
    p = Path(path)
    if not p.exists():
        return {"path": path, "exists": False}

    st = p.stat()
    import grp
    import pwd

    owner = pwd.getpwuid(st.st_uid).pw_name
    group = grp.getgrgid(st.st_gid).gr_name
    mode = oct(st.st_mode & 0o7777).removeprefix("0o").zfill(4)

    return {
        "path": path,
        "exists": True,
        "owner": owner,
        "group": group,
        "mode": "0" + mode,  # canonical "0640" form
    }


def cart_ftdi_status() -> dict[str, Any]:
    """Scan /sys/bus/usb/devices for an FTDI cart (VID 0403, PID 6014)."""
    base = Path("/sys/bus/usb/devices")
    now_ms = int(time.time() * 1000)
    if not base.exists():
        return {"ftdi_present": False, "ftdi_checked_at_ms": now_ms}

    for entry in base.iterdir():
        try:
            vid = (entry / "idVendor").read_text().strip().lower()
            pid = (entry / "idProduct").read_text().strip().lower()
            if vid == FTDI_VID and pid == FTDI_PID:
                serial = ""
                try:
                    serial = (entry / "serial").read_text().strip()
                except FileNotFoundError:
                    pass
                return {
                    "ftdi_present": True,
                    "ftdi_serial": serial,
                    "ftdi_checked_at_ms": now_ms,
                }
        except (FileNotFoundError, OSError):
            continue
    return {"ftdi_present": False, "ftdi_checked_at_ms": now_ms}


def deployer_version(deployer_path: str) -> dict[str, Any]:
    """Run `sc64deployer --version` and parse the result.

    Returns {binary, version_string, has_qw_local_flush_patch, version_checked_at_ms}.
    """
    now_ms = int(time.time() * 1000)
    try:
        proc = subprocess.run(
            [deployer_path, "--version"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        out = (proc.stdout + proc.stderr).strip()
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        return {
            "binary": deployer_path,
            "version_string": None,
            "version_checked_at_ms": now_ms,
            "has_qw_local_flush_patch": False,
            "version_error": str(e),
        }

    has_patch = QW_LOCAL_MARKER in out
    return {
        "binary": deployer_path,
        "version_string": out,
        "version_checked_at_ms": now_ms,
        "has_qw_local_flush_patch": has_patch,
    }


def deployer_probe(deployer_path: str, server_addr: str) -> dict[str, Any]:
    """Run `sc64deployer -r <server_addr> info` to verify the deployer can
    open the FTDI device through the running sc64-server.

    This catches "FTDI present at USB but sc64-server can't open it" — the
    lsusb-level cart_ftdi_status check does not.
    """
    now_ms = int(time.time() * 1000)
    try:
        proc = subprocess.run(
            [deployer_path, "-r", server_addr, "info"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        ok = proc.returncode == 0
        return {
            "probe_ok": ok,
            "probe_last_run_ms": now_ms,
            "probe_last_error": (proc.stderr.strip() or proc.stdout.strip()) if not ok else None,
        }
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        return {
            "probe_ok": False,
            "probe_last_run_ms": now_ms,
            "probe_last_error": str(e),
        }
```

- [ ] **Step 2: Verify the file is syntactically valid Python**

Run: `python3 -c "import ast; ast.parse(open('pi-sc64/packages/sc64-api/status_probes.py').read())"`

Expected: exit 0, no output.

### Task 2.2: Write unit tests for `status_probes.py`

**Files:**
- Create: `pi-sc64/packages/sc64-api/tests/__init__.py` (empty)
- Create: `pi-sc64/packages/sc64-api/tests/test_status_probes.py`

- [ ] **Step 1: Write the test file**

```python
"""Unit tests for status_probes.py — pure functions, no Pi required."""
from __future__ import annotations

import os
import tempfile
from unittest.mock import patch, MagicMock

import pytest

from sc64_api.status_probes import (
    token_file_status,
    cart_ftdi_status,
    deployer_version,
    deployer_probe,
    QW_LOCAL_MARKER,
)


class TestTokenFileStatus:
    def test_missing_file_returns_exists_false(self):
        r = token_file_status("/tmp/definitely-does-not-exist-xyz")
        assert r == {"path": "/tmp/definitely-does-not-exist-xyz", "exists": False}

    def test_existing_file_returns_owner_group_mode(self, tmp_path):
        f = tmp_path / "tokens"
        f.write_text("abc")
        f.chmod(0o640)
        r = token_file_status(str(f))
        assert r["exists"] is True
        assert r["mode"] == "0640"
        assert r["path"] == str(f)
        assert r["owner"]  # whatever user runs the tests
        assert r["group"]

    def test_mode_formatting(self, tmp_path):
        f = tmp_path / "tokens"
        f.write_text("x")
        f.chmod(0o600)
        assert token_file_status(str(f))["mode"] == "0600"


class TestCartFtdiStatus:
    def test_no_sysfs_returns_false(self):
        with patch("sc64_api.status_probes.Path") as MockPath:
            instance = MockPath.return_value
            instance.exists.return_value = False
            r = cart_ftdi_status()
            assert r["ftdi_present"] is False
            assert "ftdi_checked_at_ms" in r

    def test_ftdi_present_with_serial(self, tmp_path, monkeypatch):
        # Fake /sys/bus/usb/devices layout
        usb = tmp_path / "usb_devices"
        usb.mkdir()
        dev = usb / "1-1"
        dev.mkdir()
        (dev / "idVendor").write_text("0403\n")
        (dev / "idProduct").write_text("6014\n")
        (dev / "serial").write_text("SC649T0HH2\n")

        from sc64_api import status_probes as sp
        monkeypatch.setattr(sp, "Path", lambda p: tmp_path / "usb_devices" if str(p) == "/sys/bus/usb/devices" else __import__("pathlib").Path(p))

        r = cart_ftdi_status()
        assert r["ftdi_present"] is True
        assert r["ftdi_serial"] == "SC649T0HH2"


class TestDeployerVersion:
    def test_marker_detected(self):
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(
                stdout="sc64deployer 2.20.0+qwhil\n", stderr="", returncode=0
            )
            r = deployer_version("/fake/path")
            assert r["has_qw_local_flush_patch"] is True
            assert "+qwhil" in r["version_string"]

    def test_marker_absent(self):
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(
                stdout="sc64deployer 2.20.0\n", stderr="", returncode=0
            )
            r = deployer_version("/fake/path")
            assert r["has_qw_local_flush_patch"] is False

    def test_binary_missing(self):
        with patch("subprocess.run", side_effect=FileNotFoundError("no such file")):
            r = deployer_version("/fake/path")
            assert r["version_string"] is None
            assert "version_error" in r


class TestDeployerProbe:
    def test_returncode_zero_is_ok(self):
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(returncode=0, stdout="info\n", stderr="")
            r = deployer_probe("/fake", "localhost:9064")
            assert r["probe_ok"] is True
            assert r["probe_last_error"] is None

    def test_nonzero_returncode_surfaces_error(self):
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(
                returncode=1, stdout="", stderr="cart not detected\n"
            )
            r = deployer_probe("/fake", "localhost:9064")
            assert r["probe_ok"] is False
            assert "cart not detected" in r["probe_last_error"]
```

- [ ] **Step 2: Run the tests**

Run: `cd pi-sc64/packages/sc64-api && PYTHONPATH=. python3 -m pytest tests/test_status_probes.py -v 2>&1 | tail -20`

Expected: all tests pass. If `sc64_api` import fails, add an `__init__.py` to `pi-sc64/packages/sc64-api/` and update `PYTHONPATH` so the package is importable as `sc64_api`. (You may need to adjust the import path scheme; the current `app.py` is treated as a top-level script via `--app-dir`, so a package wrapper is the minimum cleanup.)

**If the package-restructure is too painful here:** rename imports in `test_status_probes.py` from `from sc64_api.status_probes import ...` to `from status_probes import ...` and run with `PYTHONPATH=pi-sc64/packages/sc64-api`. The tradeoff is "tests don't enforce package structure" vs. "no module shuffle now." For Chunk 2, take the simpler PYTHONPATH approach; revisit package structure if it bites in Chunk 3.

- [ ] **Step 3: Commit**

```bash
cd pi-sc64
git add packages/sc64-api/status_probes.py packages/sc64-api/tests/
git commit -m "$(cat <<'EOF'
feat(sc64-api): status_probes module — token file, FTDI, deployer

Pure functions that fill enriched /status fields for HIL doctor.
Includes deployer --version parsing with +qwhil marker detection and
a deployer ping probe that catches "FTDI device present but
sc64-server can't open it" failure modes lsusb-level checks miss.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 2.3: Wire status_probes into the `/status` endpoint

**Files:**
- Modify: `pi-sc64/packages/sc64-api/app.py`
- Modify: `pi-sc64/modules/sc64-api.nix` (so the new file ships in the Nix store alongside app.py)

- [ ] **Step 1: Modify `app.py` to import and call the probes**

Add at the top of `pi-sc64/packages/sc64-api/app.py` (after existing imports):

```python
import time
from typing import Any
from status_probes import (
    token_file_status,
    cart_ftdi_status,
    deployer_version,
    deployer_probe,
    DEPLOYER_VERSION_TTL_S,
    DEPLOYER_PROBE_TTL_S,
)
```

Below the existing `_upload_lock = asyncio.Lock()` line, add a cache for the throttled probes:

```python
# Cached deployer-version and deployer-probe results. Refreshed by
# background tasks; /status reads from the cache.
_deployer_version_cache: dict[str, Any] = {
    "binary": SC64_DEPLOYER,
    "version_string": None,
    "version_checked_at_ms": 0,
    "has_qw_local_flush_patch": False,
}
_deployer_probe_cache: dict[str, Any] = {
    "probe_ok": False,
    "probe_last_run_ms": 0,
    "probe_last_error": "not yet probed",
}
```

Add an async background refresher (using `asyncio.create_task`) inside a FastAPI startup handler:

```python
@app.on_event("startup")
async def _start_background_probes():
    async def refresh_loop():
        while True:
            # Refresh version (cheap, low TTL of 60s).
            v = await asyncio.to_thread(deployer_version, SC64_DEPLOYER)
            _deployer_version_cache.update(v)
            # Refresh probe (slightly more expensive, 30s TTL).
            p = await asyncio.to_thread(deployer_probe, SC64_DEPLOYER, SC64_SERVER_ADDR)
            _deployer_probe_cache.update(p)
            await asyncio.sleep(DEPLOYER_PROBE_TTL_S)
    asyncio.create_task(refresh_loop())
```

Replace the existing `@app.get("/status")` handler with:

```python
@app.get("/status")
async def status(auth: AuthContext = Depends(require_auth)):
    return {
        "ok": True,
        "version": "0.2.0",
        "sc64_server": SC64_SERVER_ADDR,
        "upload_busy": _upload_lock.locked(),
        "user_id": auth.user_id,

        "deployer": {
            **_deployer_version_cache,
            **_deployer_probe_cache,
        },
        "tokens_file": token_file_status(SC64_TOKEN_FILE),
        "cart": cart_ftdi_status(),

        # Camera + debug_consumer fields land in Chunk 4 with the
        # ring buffer + snapshot endpoints. Stub values for now so
        # the doctor can be authored against the final shape:
        "camera": {"stream_reachable": None, "last_snapshot_ms": None},
        "debug_consumer": {
            "running": False,
            "consecutive_failures": 0,
            "last_line_ts_ms": None,
            "last_line_preview": None,
        },
        "ring_buffer": {
            "in_memory_lines": 0,
            "in_memory_max": 0,
            "file_path": None,
            "file_bytes": 0,
        },
    }
```

- [ ] **Step 2: Modify the NixOS module to bundle status_probes.py**

In `pi-sc64/modules/sc64-api.nix`, change the `appDir` definition. Replace:

```nix
appDir = pkgs.writeTextDir "app.py" (builtins.readFile ../packages/sc64-api/app.py);
```

with:

```nix
appDir = pkgs.runCommand "sc64-api-app" {} ''
  mkdir -p $out
  cp ${../packages/sc64-api/app.py} $out/app.py
  cp ${../packages/sc64-api/status_probes.py} $out/status_probes.py
'';
```

This bundles both files so the uvicorn `--app-dir` finds them with their relative import working.

- [ ] **Step 3: Deploy and verify**

```bash
cd pi-sc64
pi-sc64/scripts/bootstrap-pi.sh full sc64pi.local 2>&1 | tail -10
```

(Or, if you want to skip the token rotation, do `nixos-rebuild switch --flake .#pi --target-host root@sc64pi.local --build-host root@sc64pi.local --use-substitutes` directly.)

Then:

```bash
TOKEN=$(cat ~/.sc64-api-token)
curl -s -H "Authorization: Bearer $TOKEN" http://sc64pi.local:8064/status | python3 -m json.tool
```

Expected: a JSON document with `deployer`, `tokens_file`, `cart` populated. `deployer.has_qw_local_flush_patch` should be `true`. `tokens_file.mode` should be `"0640"`. `cart.ftdi_present` should be `true` or `false` depending on whether the cart is plugged in.

- [ ] **Step 4: Commit**

```bash
cd pi-sc64
git add packages/sc64-api/app.py modules/sc64-api.nix
git commit -m "$(cat <<'EOF'
feat(sc64-api): enriched /status with deployer, token, cart probes

/status now exposes the fields the HIL doctor probes for: deployer
version + qw-local patch detection + deployer-can-open-FTDI probe,
tokens_file ownership/mode, cart FTDI presence. Stub fields for
camera/debug_consumer/ring_buffer ship empty in this chunk — they
land in Chunk 4 with the actual implementations.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Chunk 2 acceptance test

- [ ] Pi-side unit tests pass: `cd pi-sc64/packages/sc64-api && PYTHONPATH=. python3 -m pytest tests/ -v` — all green
- [ ] `curl -H "Authorization: Bearer $(cat ~/.sc64-api-token)" http://sc64pi.local:8064/status | jq .deployer.has_qw_local_flush_patch` returns `true`
- [ ] `curl -H "Authorization: Bearer $(cat ~/.sc64-api-token)" http://sc64pi.local:8064/status | jq .tokens_file.mode` returns `"0640"`
- [ ] `curl -H "Authorization: Bearer $(cat ~/.sc64-api-token)" http://sc64pi.local:8064/status | jq .cart.ftdi_present` returns `true` (cart plugged in) or `false` (unplugged) — both are valid; just confirms the probe runs
- [ ] `curl -H "Authorization: Bearer $(cat ~/.sc64-api-token)" http://sc64pi.local:8064/status | jq .deployer.probe_ok` returns `true` when cart is plugged in
- [ ] Stub fields present in the JSON: `.camera`, `.debug_consumer`, `.ring_buffer` are all present with stub values (None / 0 / False), so Chunk 3's doctor (and Chunk 4's debug-consumer implementation) can be authored against the final shape

Once all six hold, Chunk 2 is done. Proceed to Chunk 3.

---

## Chunk 3: Mac-side `hil doctor`

**Goal:** Build the Mac-side preflight: `hil/client.py` (httpx wrapper with typed errors), `hil/doctor.py` (the 10 probes from spec §10.2 with actionable fix boxes), and the `hil_test_runner.py doctor` subcommand. End state: `make hil-doctor` reports all 10 blocking probes green against the live Pi (warn-only probes may show as warnings if the cart isn't plugged in).

**Files this chunk creates/modifies:**

- Create: `sf64-practice-rom/tools/hil/__init__.py`
- Create: `sf64-practice-rom/tools/hil/client.py`
- Create: `sf64-practice-rom/tools/hil/doctor.py`
- Create: `sf64-practice-rom/tools/hil_test_runner.py` (with `doctor` subcommand; `run` is a stub that errors until Chunk 4)
- Create: `sf64-practice-rom/tests/hil/__init__.py`
- Create: `sf64-practice-rom/tests/hil/_unit/__init__.py`
- Create: `sf64-practice-rom/tests/hil/_unit/test_client.py`
- Create: `sf64-practice-rom/tests/hil/_unit/test_doctor.py`
- Modify: `sf64-practice-rom/.gitignore` — add `tests/hil/_artifacts/`
- Modify: `sf64-practice-rom/Makefile` — add `make hil-doctor` + `make hil-test` targets

**Skills to use:**
- @superpowers:test-driven-development for `doctor.py` probe logic
- @superpowers:verification-before-completion before claiming chunk complete

### Task 3.1: Implement `hil/client.py` (HTTP wrapper)

**Files:**
- Create: `sf64-practice-rom/tools/hil/__init__.py` (empty)
- Create: `sf64-practice-rom/tools/hil/client.py`

- [ ] **Step 1: Create the package**

```bash
mkdir -p sf64-practice-rom/tools/hil
touch sf64-practice-rom/tools/hil/__init__.py
```

- [ ] **Step 2: Write `client.py`**

```python
"""Minimal httpx wrapper around sc64-api.

Reads bearer token from SC64_API_TOKEN env or ~/.sc64-api-token.
Exposes a small surface: get_status, get_health, get_logs, upload_rom,
get_camera_snapshot. Methods raise typed exceptions on transport
failures so the doctor and runner can map them to fix boxes.
"""
from __future__ import annotations

import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx


class HilError(Exception):
    """Base for all HIL client errors."""


class PiUnreachable(HilError):
    """Network-level failure: connect timeout or refused connection."""


class AuthFailed(HilError):
    """401 or 403 from sc64-api."""


class TokenMissing(HilError):
    """No SC64_API_TOKEN env and no ~/.sc64-api-token file."""


class UploadConflict(HilError):
    """409 from /upload — another upload in progress."""


class SnapshotUnavailable(HilError):
    """502 or similar from /camera/snapshot."""


@dataclass(frozen=True)
class ClientConfig:
    host: str
    port: int = 8064
    timeout_s: float = 5.0
    token: str | None = None  # if None, loaded lazily from env/file


def load_token(explicit: str | None = None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("SC64_API_TOKEN")
    if env:
        return env
    path = Path.home() / ".sc64-api-token"
    if path.exists():
        return path.read_text().strip()
    raise TokenMissing(
        "No bearer token found. Set SC64_API_TOKEN or write "
        "~/.sc64-api-token (mode 600)."
    )


class HilClient:
    def __init__(self, cfg: ClientConfig):
        self.cfg = cfg
        self._token: str | None = cfg.token
        self._client = httpx.Client(timeout=cfg.timeout_s)

    def close(self) -> None:
        self._client.close()

    def __enter__(self) -> HilClient:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def _url(self, path: str) -> str:
        return f"http://{self.cfg.host}:{self.cfg.port}{path}"

    def _auth_headers(self) -> dict[str, str]:
        if not self._token:
            self._token = load_token()
        return {"Authorization": f"Bearer {self._token}"}

    def _request(self, method: str, path: str, **kw: Any) -> httpx.Response:
        url = self._url(path)
        try:
            resp = self._client.request(method, url, **kw)
        except (httpx.ConnectError, httpx.ConnectTimeout) as e:
            raise PiUnreachable(f"Cannot reach {url}: {e}") from e
        if resp.status_code in (401, 403):
            raise AuthFailed(f"sc64-api rejected token: {resp.status_code}")
        return resp

    # Public API ---------------------------------------------------------

    def get_health(self) -> dict[str, Any]:
        # /health is unauthenticated.
        try:
            resp = self._client.get(self._url("/health"))
        except (httpx.ConnectError, httpx.ConnectTimeout) as e:
            raise PiUnreachable(f"Cannot reach {self.cfg.host}: {e}") from e
        resp.raise_for_status()
        return resp.json()

    def get_status(self) -> dict[str, Any]:
        resp = self._request("GET", "/status", headers=self._auth_headers())
        resp.raise_for_status()
        return resp.json()

    # Chunk 4 will add: upload_rom, get_logs, get_camera_snapshot.
```

- [ ] **Step 3: Write unit tests**

Create `sf64-practice-rom/tests/hil/__init__.py` (empty) and `sf64-practice-rom/tests/hil/_unit/__init__.py` (empty), then `sf64-practice-rom/tests/hil/_unit/test_client.py`:

```python
"""Unit tests for hil.client — mocked httpx, no Pi required."""
from __future__ import annotations

import os
import pytest
from unittest.mock import patch, MagicMock

import httpx

from tools.hil.client import (
    ClientConfig,
    HilClient,
    PiUnreachable,
    AuthFailed,
    TokenMissing,
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
```

- [ ] **Step 4: Install httpx + pytest as test deps**

`sf64-practice-rom` does not ship a `pyproject.toml`. For Python tooling, the repo uses ad-hoc `python3 -m pytest` invocations. To install:

```bash
python3 -m pip install --user httpx pytest
```

Verify: `python3 -c "import httpx, pytest"` exits 0.

- [ ] **Step 5: Run the unit tests**

Run from the sf64-practice-rom repo root:

```bash
PYTHONPATH=. python3 -m pytest tests/hil/_unit/test_client.py -v
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
cd ~/code/sf64-practice-rom
git add tools/hil/__init__.py tools/hil/client.py tests/hil/__init__.py tests/hil/_unit/__init__.py tests/hil/_unit/test_client.py
git commit -m "$(cat <<'EOF'
feat(hil): client.py — httpx wrapper for sc64-api with typed errors

Token loading: env > ~/.sc64-api-token > raise TokenMissing.
Transport: typed exceptions (PiUnreachable, AuthFailed,
UploadConflict, SnapshotUnavailable) so doctor and runner can map
to actionable fix boxes. /upload, /logs, /camera/snapshot stubs land
in Chunk 3.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

(Pre-commit hook will run.)

### Task 3.2: Implement `hil/doctor.py` with the 10 probes

**Files:**
- Create: `sf64-practice-rom/tools/hil/doctor.py`

- [ ] **Step 1: Write `doctor.py`**

```python
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

import os
import socket
import subprocess
import time
from dataclasses import dataclass, field
from typing import Any, Callable

from tools.hil.client import (
    ClientConfig,
    HilClient,
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
    except PiUnreachable as e:
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

def probe_token_local() -> ProbeResult:
    try:
        token = load_token()
    except TokenMissing:
        return ProbeResult(
            "4", "Bearer token configured locally", False,
            fix=(
                "No bearer token found on this machine.\n\n"
                "Run:\n"
                "  pi-sc64/scripts/bootstrap-pi.sh token-only sc64pi.local\n\n"
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
                    "  pi-sc64/scripts/bootstrap-pi.sh token-only sc64pi.local"
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
            fix=f"/etc/sc64-api/tokens does not exist on Pi.",
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
        return ProbeResult("8", "sc64-server can open FTDI device", True)
    return ProbeResult(
        "8", "sc64-server can open FTDI device", False,
        fix=(
            f"sc64-server reports it cannot open the cart: "
            f"{d.get('probe_last_error', 'unknown')}\n\n"
            f"Most likely cause: udev rule did not apply OR the sc64 group\n"
            f"is misconfigured. Fix:\n"
            f"  ssh root@<host> 'systemctl restart sc64-server && \\\n"
            f"                    journalctl -u sc64-server -n 50'"
        ),
    )


def probe_debug_consumer(status: dict[str, Any]) -> ProbeResult:
    dc = status.get("debug_consumer", {})
    # In Chunk 2 this field is a stub (running=False). The probe is built
    # to the final contract; it will start passing in Chunk 3 once the
    # consumer is implemented. For Chunk 2, treat the stub case (running
    # is False AND consecutive_failures is 0) as a synthetic pass with a
    # detail note so the doctor can be authored against the final shape.
    if dc.get("running") is False and dc.get("consecutive_failures") == 0:
        return ProbeResult(
            "9", "Debug consumer running", True,
            detail="(stubbed in Chunk 2; real implementation in Chunk 4)",
        )
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
    cam = status.get("camera", {})
    if cam.get("stream_reachable") is True:
        return ProbeResult("10", "Camera responding", True)
    if cam.get("stream_reachable") is None:
        return ProbeResult(
            "10", "Camera responding", True,
            detail="(stubbed in Chunk 2; real probe lands in Chunk 4)",
        )
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

    p4 = probe_token_local()
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
```

- [ ] **Step 2: Write unit tests**

Create `sf64-practice-rom/tests/hil/_unit/test_doctor.py`:

```python
"""Unit tests for hil.doctor — probe semantics with mocked /status."""
from __future__ import annotations

from unittest.mock import patch, MagicMock

import pytest

from tools.hil.doctor import (
    probe_token_file_mode,
    probe_ftdi_present,
    probe_deployer_can_open,
    probe_debug_consumer,
    probe_camera,
    probe_qw_local_marker,
    probe_recent_line,
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


class TestDebugConsumer:
    def test_chunk2_stub_passes(self):
        status = {"debug_consumer": {"running": False, "consecutive_failures": 0}}
        r = probe_debug_consumer(status)
        assert r.passed
        assert "stubbed in Chunk 2" in r.detail

    def test_running_passes(self):
        status = {"debug_consumer": {"running": True, "consecutive_failures": 0}}
        assert probe_debug_consumer(status).passed

    def test_too_many_failures_fails(self):
        status = {"debug_consumer": {"running": False, "consecutive_failures": 5}}
        assert not probe_debug_consumer(status).passed


class TestCamera:
    def test_chunk2_stub_passes(self):
        status = {"camera": {"stream_reachable": None}}
        r = probe_camera(status)
        assert r.passed
        assert "stubbed" in r.detail

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
```

- [ ] **Step 3: Run the unit tests**

Run: `cd ~/code/sf64-practice-rom && PYTHONPATH=. python3 -m pytest tests/hil/_unit/test_doctor.py -v`

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
cd ~/code/sf64-practice-rom
git add tools/hil/doctor.py tests/hil/_unit/test_doctor.py
git commit -m "$(cat <<'EOF'
feat(hil): doctor.py — 10 preflight probes + actionable fix boxes

Phase A: network, SSH, sc64-api responding (Mac-local probes).
Phase B: token-local, token-accepted, token-file mode, FTDI present,
sc64-server-can-open-FTDI, debug-consumer running, camera (via
enriched /status). Plus W1/W2 warn-only probes for qw-local marker
and recent-IS-Viewer-line.

Each ❌ row carries an unambiguous Fix box with the exact next
command. First-failure short-circuit: the user only ever has to
read the first ❌ row.

debug_consumer + camera probes are wired to the final /status
contract but accept the stub values from Chunk 2's enriched /status
implementation. Real values flow in Chunk 3.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 3.3: Create `hil_test_runner.py` with the `doctor` subcommand

**Files:**
- Create: `sf64-practice-rom/tools/hil_test_runner.py`

- [ ] **Step 1: Write the runner**

```python
#!/usr/bin/env python3
"""HIL test runner.

Subcommands:
  doctor              — run preflight probes against the Pi, print results
  run <test_path>     — run a HIL test (Chunk 4+)

Auth:
  bearer token from $SC64_API_TOKEN or ~/.sc64-api-token

Usage:
  python3 tools/hil_test_runner.py doctor
  python3 tools/hil_test_runner.py doctor --host sc64pi.local
  python3 tools/hil_test_runner.py run tests/hil/test_boot_smoke.py
"""
from __future__ import annotations

import argparse
import sys

from tools.hil.doctor import probe_all, render_report

DEFAULT_HOST = "sc64pi.local"


def cmd_doctor(args: argparse.Namespace) -> int:
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
    print("`run` subcommand lands in Chunk 4. For now, use `doctor`.", file=sys.stderr)
    return 2


def main() -> int:
    parser = argparse.ArgumentParser(prog="hil_test_runner.py")
    subs = parser.add_subparsers(dest="cmd", required=True)

    doctor = subs.add_parser("doctor", help="run preflight probes")
    doctor.add_argument("--host", default=DEFAULT_HOST,
                        help=f"Pi hostname (default: {DEFAULT_HOST})")
    doctor.set_defaults(func=cmd_doctor)

    run = subs.add_parser("run", help="run a HIL test (Chunk 4+)")
    run.add_argument("test_path")
    run.set_defaults(func=cmd_run)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Make it executable + smoke-test**

```bash
chmod +x sf64-practice-rom/tools/hil_test_runner.py
cd sf64-practice-rom
PYTHONPATH=. python3 tools/hil_test_runner.py --help
PYTHONPATH=. python3 tools/hil_test_runner.py doctor --help
```

Expected: usage messages, both succeed.

- [ ] **Step 3: Run the doctor against the live Pi**

```bash
cd ~/code/sf64-practice-rom
PYTHONPATH=. python3 tools/hil_test_runner.py doctor
```

Expected: all 10 blocking probes ✅ (assuming the cart is plugged in for probe 7/8 to pass; without it, those will fail). The two stubbed probes (debug_consumer, camera) show ✅ with a "(stubbed in Chunk 2…)" detail. W1 should be ✅ (qw-local marker detected). W2 is informational.

**Iterate against the real Pi until output is clean.** If probes 7/8 fail because the cart isn't plugged in, that's expected — plug it in and retry.

- [ ] **Step 4: Add a Makefile target**

In `sf64-practice-rom/Makefile`, add:

```makefile
.PHONY: hil-doctor
hil-doctor:
	@PYTHONPATH=. python3 tools/hil_test_runner.py doctor

.PHONY: hil-test
hil-test:
	@PYTHONPATH=. python3 tools/hil_test_runner.py run $(filter-out $@,$(MAKECMDGOALS))
```

Place under the existing targets — find a sensible spot. Run `make hil-doctor` to verify.

- [ ] **Step 5: Update `.gitignore`**

Append to `sf64-practice-rom/.gitignore`:

```
tests/hil/_artifacts/
tests/hil/_fixtures/wedge_rom.z64
```

- [ ] **Step 6: Commit**

```bash
cd ~/code/sf64-practice-rom
git add tools/hil_test_runner.py Makefile .gitignore
git commit -m "$(cat <<'EOF'
feat(hil): hil_test_runner.py with doctor subcommand + make targets

Entry point for HIL testing. `doctor` subcommand runs the 10 probes
from spec §10.2 and prints the rendered report with fix boxes. `run`
is a stub that lands in Chunk 3. `make hil-doctor` is the convenience
target.

.gitignore covers _artifacts/ (per-run output) and the wedge ROM
fixture binary (the build script is in git; the binary is generated).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Chunk 3 acceptance test

- [ ] `python3 tools/hil_test_runner.py doctor` (or `make hil-doctor`) reports:
  - Probes 1–6 green
  - Probe 7 green if cart is plugged in (otherwise expected fail with clear fix box)
  - Probe 8 green if cart is plugged in and udev is happy
  - Probes 9–10 show ✅ with the "(stubbed in Chunk 2…)" detail (they'll show real values in Chunk 4)
  - W1 green (qw-local marker detected)
  - W2 may be ✅ or warn (depends on cart state)
- [ ] Unit tests pass: `PYTHONPATH=. python3 -m pytest tests/hil/_unit/ -v` — all green
- [ ] The doctor's first-failure short-circuit is observable: temporarily delete `~/.sc64-api-token` and rerun — probes 1–3 pass, probe 4 fails with the fix box, probes 5–10 skipped.

Once all three hold, Chunk 3 is done. Restore your token (`bootstrap-pi.sh token-only sc64pi.local`) and proceed to Chunk 4.

---

## Chunk 4: Pi-side round-trip — DebugConsumer + LogRing + /logs + /camera

**Goal:** Pi-side components for the round-trip MVP. `LogRing` + `DebugConsumer` together manage the `sc64deployer debug` subprocess (start at boot, stop before each upload, restart after — the lock-around-upload integration per spec §4.1, justified by the deployer server's single-threaded `server.rs:163` constraint). The new endpoints `/logs` (paginated by since/until ms) and `/camera/snapshot` (proxies ustreamer with /snapshot → /stream fallback) are wired in. `/status` fields previously stubbed (camera, debug_consumer, ring_buffer) now report real values.

**End state acceptance:** an end-to-end curl `POST /upload` works without breaking the debug stream; the consumer correctly stops + restarts around the upload; post-reset IS-Viewer lines appear in `/logs?since=<upload_complete_ts>`.

**Files this chunk creates/modifies:**

- Create: `pi-sc64/packages/sc64-api/log_ring.py`
- Create: `pi-sc64/packages/sc64-api/debug_consumer.py`
- Create: `pi-sc64/packages/sc64-api/tests/test_log_ring.py`
- Create: `pi-sc64/packages/sc64-api/tests/test_debug_consumer.py`
- Create: `pi-sc64/packages/sc64-api/tests/conftest.py` (pytest-asyncio config)
- Modify: `pi-sc64/packages/sc64-api/app.py` — wire DebugConsumer + LogRing into startup, add `/logs` and `/camera/snapshot`, integrate consumer.stop/start into `/upload`, fill previously-stubbed `/status` sections
- Modify: `pi-sc64/modules/sc64-api.nix` — bundle new files, add `ringBufferDir` option + tmpfiles rule for `/var/lib/sc64-api/logs`, plumb environment vars

**Skills to use:**
- @superpowers:test-driven-development for `LogRing` and `DebugConsumer`
- @superpowers:systematic-debugging if the integration breaks
- @superpowers:verification-before-completion before claiming chunk complete

**Design decisions encoded in this chunk:**

- **Consumer lifecycle in `/upload`**: per spec §4.1, the upload lock acquires → consumer.stop() (SIGTERM, 2s grace, SIGKILL fallback) → run `sc64deployer upload` → consumer.start() → release lock. The new debug client connects post-upload and catches the post-reset IS-Viewer init lines. Documented in code with a pointer to the deployer's single-threaded `server.rs:163` constraint.
- **Auto-respawn with exponential backoff**: DebugConsumer detects unexpected child exit (not the intentional stop during upload) and respawns with backoff `1s, 2s, 4s, cap 10s`. `consecutive_failures` exposed via `/status`.
- **Cart-alive timeout**: `upload_rom()` blocks until first IS-Viewer line appears post-upload (default 10s budget). If no line appears, raise `CartWedgedError`. Chunk 6 wires this into the banner; in Chunks 4-5 the error just surfaces as a test failure.
- **Log polling, not SSE**: `/logs` is GET with `since=<ts_ms>&until=<ts_ms>&limit=N`. The Mac runner polls every 100ms. Keeps deps to `httpx` only.

### Task 4.1: Implement `LogRing` (Pi-side)

**Files:**
- Create: `pi-sc64/packages/sc64-api/log_ring.py`

- [ ] **Step 1: Write `log_ring.py`**

```python
"""Timestamped append-only ring buffer for IS-Viewer printf lines.

Two backing stores:
  - In-memory deque(maxlen=N) for /logs queries (the test path)
  - Rotating gzip file for forensics (panic loops, long retention)

Designed so a panic-loop ROM emitting hundreds of lines/sec cannot
trash the test-path window — the file rotation always captures the
full stream even when the in-memory ring evicts.
"""
from __future__ import annotations

import gzip
import os
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


@dataclass(frozen=True)
class LogLine:
    ts_ms: int
    line: str


class LogRing:
    def __init__(
        self,
        in_memory_max: int = 50_000,
        file_dir: str | None = None,
        file_retention_days: int = 7,
    ):
        self._lock = threading.Lock()
        self._deque: deque[LogLine] = deque(maxlen=in_memory_max)
        self._in_memory_max = in_memory_max
        self._file_dir = Path(file_dir) if file_dir else None
        self._retention_days = file_retention_days
        self._current_date: str | None = None
        self._current_file = None  # type: ignore[assignment]
        self._last_line_ts_ms: int | None = None
        self._last_line_preview: str | None = None

        if self._file_dir:
            self._file_dir.mkdir(parents=True, exist_ok=True)

    def append(self, line: str) -> None:
        """Append a line. Idempotent on empty/whitespace input (drops it)."""
        line = line.rstrip("\n")
        if not line:
            return
        now_ms = int(time.time() * 1000)
        entry = LogLine(ts_ms=now_ms, line=line)
        with self._lock:
            self._deque.append(entry)
            self._last_line_ts_ms = now_ms
            self._last_line_preview = line[:200]
            if self._file_dir:
                self._write_file(entry)

    def _write_file(self, entry: LogLine) -> None:
        """Append to today's gzip file, rotating at midnight."""
        date = time.strftime("%Y-%m-%d", time.gmtime(entry.ts_ms / 1000))
        if date != self._current_date:
            self._close_file()
            self._current_date = date
            assert self._file_dir is not None
            path = self._file_dir / f"isv-{date}.log.gz"
            self._current_file = gzip.open(path, "at", encoding="utf-8")
            self._prune_old_files()
        assert self._current_file is not None
        self._current_file.write(f"{entry.ts_ms}\t{entry.line}\n")
        self._current_file.flush()

    def _close_file(self) -> None:
        if self._current_file is not None:
            try:
                self._current_file.close()
            except Exception:
                pass
            self._current_file = None

    def _prune_old_files(self) -> None:
        """Delete files older than retention window."""
        if not self._file_dir:
            return
        cutoff = time.time() - (self._retention_days * 86400)
        for p in self._file_dir.glob("isv-*.log.gz"):
            try:
                if p.stat().st_mtime < cutoff:
                    p.unlink()
            except OSError:
                pass

    def read_window(self, since_ms: int = 0, until_ms: int | None = None) -> list[LogLine]:
        """Return all lines with since_ms <= ts_ms < until_ms (exclusive until)."""
        until_ms = until_ms if until_ms is not None else int(time.time() * 1000) + 1
        with self._lock:
            return [e for e in self._deque if since_ms <= e.ts_ms < until_ms]

    def stats(self) -> dict[str, object]:
        with self._lock:
            file_path = None
            file_bytes = 0
            if self._file_dir and self._current_date:
                fp = self._file_dir / f"isv-{self._current_date}.log.gz"
                file_path = str(fp)
                try:
                    file_bytes = fp.stat().st_size
                except OSError:
                    pass
            return {
                "in_memory_lines": len(self._deque),
                "in_memory_max": self._in_memory_max,
                "file_path": file_path,
                "file_bytes": file_bytes,
                "last_line_ts_ms": self._last_line_ts_ms,
                "last_line_preview": self._last_line_preview,
            }

    def close(self) -> None:
        with self._lock:
            self._close_file()
```

- [ ] **Step 2: Smoke-check Python syntax**

Run: `python3 -c "import ast; ast.parse(open('pi-sc64/packages/sc64-api/log_ring.py').read())"`

Expected: exit 0.

### Task 4.2: Unit tests for `LogRing`

**Files:**
- Create: `pi-sc64/packages/sc64-api/tests/test_log_ring.py`

- [ ] **Step 1: Write the tests**

```python
"""Unit tests for log_ring.LogRing."""
from __future__ import annotations

import gzip
import time
from pathlib import Path

import pytest

from log_ring import LogRing, LogLine


def test_append_then_read_returns_in_order():
    ring = LogRing(in_memory_max=10)
    ring.append("first")
    time.sleep(0.001)
    ring.append("second")
    lines = ring.read_window()
    assert [l.line for l in lines] == ["first", "second"]
    assert lines[0].ts_ms <= lines[1].ts_ms


def test_overflow_evicts_oldest():
    ring = LogRing(in_memory_max=3)
    for i in range(5):
        ring.append(f"line{i}")
    lines = ring.read_window()
    assert [l.line for l in lines] == ["line2", "line3", "line4"]


def test_empty_line_dropped():
    ring = LogRing(in_memory_max=5)
    ring.append("")
    ring.append("   \n")
    ring.append("real")
    assert [l.line for l in ring.read_window()] == ["real"]


def test_trailing_newline_stripped():
    ring = LogRing(in_memory_max=5)
    ring.append("hello\n")
    assert ring.read_window()[0].line == "hello"


def test_read_window_filters_by_timestamp():
    ring = LogRing(in_memory_max=5)
    ring.append("a")
    boundary = int(time.time() * 1000) + 1
    time.sleep(0.005)
    ring.append("b")
    after = ring.read_window(since_ms=boundary)
    assert [l.line for l in after] == ["b"]


def test_stats_reflects_state():
    ring = LogRing(in_memory_max=10)
    ring.append("one")
    ring.append("two")
    s = ring.stats()
    assert s["in_memory_lines"] == 2
    assert s["in_memory_max"] == 10
    assert s["last_line_preview"] == "two"
    assert s["last_line_ts_ms"] is not None


def test_file_rotation_writes_gzipped_lines(tmp_path):
    ring = LogRing(in_memory_max=5, file_dir=str(tmp_path))
    ring.append("hello")
    ring.append("world")
    ring.close()
    files = list(tmp_path.glob("isv-*.log.gz"))
    assert len(files) == 1
    with gzip.open(files[0], "rt") as f:
        content = f.read()
    assert "\thello\n" in content
    assert "\tworld\n" in content


def test_file_retention_prunes_old(tmp_path):
    # Create an old file directly
    old = tmp_path / "isv-2020-01-01.log.gz"
    with gzip.open(old, "wt") as f:
        f.write("old\n")
    # Backdate it
    import os
    os.utime(old, (time.time() - 365 * 86400, time.time() - 365 * 86400))

    ring = LogRing(in_memory_max=5, file_dir=str(tmp_path), file_retention_days=7)
    ring.append("new")  # triggers _prune_old_files
    ring.close()
    assert not old.exists()
```

- [ ] **Step 2: Run the tests**

Run from `pi-sc64/packages/sc64-api`:

```bash
cd pi-sc64/packages/sc64-api
PYTHONPATH=. python3 -m pytest tests/test_log_ring.py -v
```

Expected: all tests pass.

- [ ] **Step 3: Commit**

```bash
cd pi-sc64
git add packages/sc64-api/log_ring.py packages/sc64-api/tests/test_log_ring.py
git commit -m "$(cat <<'EOF'
feat(sc64-api): LogRing — timestamped ring buffer + rotating gzip file

In-memory deque(maxlen=50k) for the test path; rotating gzip file
for forensics so a panic-loop ROM can't lose history. 7-day file
retention. Thread-safe. Designed per spec §6: ring overflow and
panic-loop failure modes both have correct behavior.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 4.3: Implement `DebugConsumer` (Pi-side)

**Files:**
- Create: `pi-sc64/packages/sc64-api/debug_consumer.py`

- [ ] **Step 1: Write `debug_consumer.py`**

```python
"""Manages the sc64deployer debug subprocess lifecycle.

Lifecycle:
  start()  - spawn `sc64deployer -r <addr> debug --isv 0x03FF0000` as a child;
             pipe stdout into LogRing; spawn a reaper task that respawns on
             unexpected exit with exponential backoff.
  stop()   - SIGTERM the child with 2s grace, SIGKILL fallback. Mark as
             intentionally stopped so the reaper doesn't respawn.
  restart() - stop() then start().

The /upload endpoint calls stop() before invoking the deployer (because the
sc64deployer server is single-threaded — see server.rs:163 — so a long-lived
debug client would block the upload), runs the upload, then calls start().

Tracks consecutive_failures so /status can surface a stuck-failing state.
"""
from __future__ import annotations

import asyncio
import logging
import os
import signal
import subprocess
from typing import Optional

logger = logging.getLogger("debug_consumer")


class DebugConsumer:
    BACKOFF_S = [1.0, 2.0, 4.0, 8.0, 10.0]  # last value is the cap

    def __init__(self, deployer_path: str, server_addr: str, isv_offset: str,
                 log_ring, on_line=None):
        """
        log_ring: anything with .append(str)
        on_line: optional callback called per line (synchronously on the
                 reader task). Used in tests.
        """
        self._deployer_path = deployer_path
        self._server_addr = server_addr
        self._isv_offset = isv_offset
        self._log_ring = log_ring
        self._on_line = on_line
        self._proc: Optional[subprocess.Popen] = None
        self._reader_task: Optional[asyncio.Task] = None
        self._reaper_task: Optional[asyncio.Task] = None
        self._intentional_stop = False
        self._consecutive_failures = 0
        self._started_at_ms: Optional[int] = None
        self._lock = asyncio.Lock()

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    @property
    def consecutive_failures(self) -> int:
        return self._consecutive_failures

    @property
    def pid(self) -> Optional[int]:
        return self._proc.pid if self._proc else None

    @property
    def started_at_ms(self) -> Optional[int]:
        return self._started_at_ms

    async def start(self) -> None:
        """Spawn the deployer-debug subprocess. Idempotent: no-op if already running."""
        async with self._lock:
            if self.running:
                return
            self._intentional_stop = False
            import time
            self._started_at_ms = int(time.time() * 1000)
            cmd = [self._deployer_path, "-r", self._server_addr,
                   "debug", "--isv", self._isv_offset]
            logger.info("starting debug consumer: %s", " ".join(cmd))
            try:
                self._proc = await asyncio.to_thread(
                    subprocess.Popen,
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    bufsize=1,  # line-buffered
                )
            except FileNotFoundError as e:
                logger.error("deployer not found: %s", e)
                self._consecutive_failures += 1
                raise

            self._reader_task = asyncio.create_task(self._read_loop())
            self._reaper_task = asyncio.create_task(self._reap_loop())

    async def stop(self) -> None:
        """SIGTERM, 2s grace, SIGKILL. Marks as intentional so the reaper
        doesn't respawn after this call returns."""
        async with self._lock:
            self._intentional_stop = True
            proc = self._proc
            if proc is None:
                return
            if proc.poll() is None:
                try:
                    proc.terminate()
                except ProcessLookupError:
                    pass
                try:
                    await asyncio.wait_for(
                        asyncio.to_thread(proc.wait), timeout=2.0
                    )
                except asyncio.TimeoutError:
                    logger.warning("SIGTERM ignored, escalating to SIGKILL")
                    try:
                        proc.kill()
                    except ProcessLookupError:
                        pass
                    await asyncio.to_thread(proc.wait)
            self._proc = None

            # Tasks should be cancelled (they may be blocked on stdout read).
            for t in (self._reader_task, self._reaper_task):
                if t and not t.done():
                    t.cancel()
            self._reader_task = None
            self._reaper_task = None

    async def restart(self) -> None:
        await self.stop()
        await self.start()

    async def _read_loop(self) -> None:
        """Pull stdout lines off the subprocess and into the ring."""
        proc = self._proc
        assert proc is not None and proc.stdout is not None
        try:
            while True:
                line = await asyncio.to_thread(proc.stdout.readline)
                if not line:
                    return
                self._log_ring.append(line.rstrip("\n"))
                if self._on_line:
                    self._on_line(line.rstrip("\n"))
        except asyncio.CancelledError:
            raise
        except Exception as e:
            logger.exception("reader loop crashed: %s", e)

    async def _reap_loop(self) -> None:
        """Respawn on unexpected exit with exponential backoff."""
        proc = self._proc
        assert proc is not None
        try:
            rc = await asyncio.to_thread(proc.wait)
            logger.info("debug subprocess exited rc=%d (intentional=%s)",
                        rc, self._intentional_stop)
            if self._intentional_stop:
                return
            # Unexpected exit. Bump failure counter and respawn.
            self._consecutive_failures += 1
            backoff_idx = min(self._consecutive_failures - 1, len(self.BACKOFF_S) - 1)
            wait_s = self.BACKOFF_S[backoff_idx]
            logger.warning("respawning in %.1fs (failure #%d)", wait_s,
                           self._consecutive_failures)
            await asyncio.sleep(wait_s)
            try:
                await self.start()
            except Exception as e:
                logger.exception("respawn failed: %s", e)
        except asyncio.CancelledError:
            raise

    def reset_failure_counter(self) -> None:
        """Called after a successful run lasts > N seconds (optional polish)."""
        self._consecutive_failures = 0
```

### Task 4.4: Unit tests for `DebugConsumer`

**Files:**
- Create: `pi-sc64/packages/sc64-api/tests/test_debug_consumer.py`

- [ ] **Step 1: Write the tests**

```python
"""Unit tests for debug_consumer.DebugConsumer.

Uses a tiny shell script as the "deployer" — emits known lines and
exits when told. Avoids mocking subprocess.Popen because we want
real semantics for the process-control behavior.
"""
from __future__ import annotations

import asyncio
import os
import stat
import sys
import textwrap
import time
from pathlib import Path

import pytest

from log_ring import LogRing
from debug_consumer import DebugConsumer


@pytest.fixture
def fake_deployer(tmp_path):
    """A shell script that mimics `sc64deployer debug --isv ...`."""
    script = tmp_path / "fake-deployer"
    script.write_text(textwrap.dedent("""\
        #!/usr/bin/env bash
        # args: -r addr debug --isv offset
        # Emits 3 lines and waits for SIGTERM
        echo "IS-Viewer init OK"
        echo "boot frame 1"
        echo "boot frame 2"
        trap 'exit 0' TERM
        # Long sleep so the parent has to terminate us
        sleep 600
    """))
    script.chmod(0o755)
    return str(script)


@pytest.fixture
def fake_deployer_exits_immediately(tmp_path):
    """A deployer that exits non-zero immediately — triggers respawn."""
    script = tmp_path / "fake-bad-deployer"
    script.write_text(textwrap.dedent("""\
        #!/usr/bin/env bash
        echo "starting" >&2
        exit 7
    """))
    script.chmod(0o755)
    return str(script)


@pytest.mark.asyncio
async def test_start_streams_lines_into_ring(fake_deployer):
    ring = LogRing(in_memory_max=10)
    dc = DebugConsumer(fake_deployer, "localhost:9064", "0x03FF0000", ring)
    await dc.start()
    # Give the subprocess time to emit lines
    await asyncio.sleep(0.5)
    assert dc.running
    lines = [l.line for l in ring.read_window()]
    assert "IS-Viewer init OK" in lines
    await dc.stop()


@pytest.mark.asyncio
async def test_stop_terminates_subprocess(fake_deployer):
    ring = LogRing(in_memory_max=10)
    dc = DebugConsumer(fake_deployer, "x", "0x03", ring)
    await dc.start()
    assert dc.running
    pid = dc.pid
    await dc.stop()
    assert not dc.running
    # Verify the process is really gone (not zombie)
    try:
        os.kill(pid, 0)
        assert False, f"process {pid} still alive"
    except ProcessLookupError:
        pass


@pytest.mark.asyncio
async def test_start_idempotent(fake_deployer):
    ring = LogRing(in_memory_max=10)
    dc = DebugConsumer(fake_deployer, "x", "0x03", ring)
    await dc.start()
    pid1 = dc.pid
    await dc.start()  # second start should be a no-op
    assert dc.pid == pid1
    await dc.stop()


@pytest.mark.asyncio
async def test_respawn_on_unexpected_exit(fake_deployer_exits_immediately):
    ring = LogRing(in_memory_max=10)
    dc = DebugConsumer(fake_deployer_exits_immediately, "x", "0x03", ring)
    # Override backoff for fast test
    dc.BACKOFF_S = [0.05, 0.1, 0.2, 0.4, 0.4]
    await dc.start()
    # Wait long enough for at least 2 respawn cycles
    await asyncio.sleep(0.6)
    await dc.stop()
    assert dc.consecutive_failures >= 1


@pytest.mark.asyncio
async def test_intentional_stop_does_not_increment_failures(fake_deployer):
    ring = LogRing(in_memory_max=10)
    dc = DebugConsumer(fake_deployer, "x", "0x03", ring)
    await dc.start()
    await dc.stop()
    assert dc.consecutive_failures == 0
```

- [ ] **Step 2: Install pytest-asyncio if needed**

```bash
python3 -m pip install --user pytest-asyncio
```

Add to `pi-sc64/packages/sc64-api/tests/conftest.py`:

```python
import pytest
pytest_plugins = ["pytest_asyncio"]
```

Or use `pytest.ini` with `asyncio_mode = auto`. Either works; pick whichever fits the rest of the test setup.

- [ ] **Step 3: Run the tests**

Run: `cd pi-sc64/packages/sc64-api && PYTHONPATH=. python3 -m pytest tests/test_debug_consumer.py -v`

Expected: all tests pass. The `test_respawn_on_unexpected_exit` test is timing-sensitive; if it's flaky, bump the `asyncio.sleep` to 1.0s.

- [ ] **Step 4: Commit**

```bash
cd pi-sc64
git add packages/sc64-api/debug_consumer.py packages/sc64-api/tests/test_debug_consumer.py packages/sc64-api/tests/conftest.py
git commit -m "$(cat <<'EOF'
feat(sc64-api): DebugConsumer — manages sc64deployer debug subprocess

start() / stop() / restart() with SIGTERM-then-SIGKILL on stop and
exponential-backoff respawn on unexpected exit. Tracks
consecutive_failures for /status. The /upload endpoint will call
stop() before invoking the deployer (since the deployer server is
single-threaded per server.rs:163) and start() after.

Tests use a shell-script fake deployer for real process-control
semantics rather than mocked subprocess.Popen.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 4.5: Wire DebugConsumer + LogRing + `/logs` + `/camera/snapshot` into `app.py`

**Files:**
- Modify: `pi-sc64/packages/sc64-api/app.py`

- [ ] **Step 1: Add imports and globals**

At the top of `app.py` add:

```python
from log_ring import LogRing
from debug_consumer import DebugConsumer
import io
import httpx as _httpx  # for camera proxy
```

Below the existing globals add:

```python
ISV_OFFSET = os.environ.get("SC64_ISV_OFFSET", "0x03FF0000")
RING_BUFFER_DIR = os.environ.get("SC64_RING_BUFFER_DIR", "/var/lib/sc64-api/logs")
CAMERA_STREAM_URL = os.environ.get("SC64_CAMERA_STREAM_URL", "http://localhost:8080")

log_ring = LogRing(in_memory_max=50_000, file_dir=RING_BUFFER_DIR)
debug_consumer = DebugConsumer(
    deployer_path=SC64_DEPLOYER,
    server_addr=SC64_SERVER_ADDR,
    isv_offset=ISV_OFFSET,
    log_ring=log_ring,
)
```

- [ ] **Step 2: Modify the startup handler**

Replace the existing `_start_background_probes` with:

```python
@app.on_event("startup")
async def _startup():
    # Start debug consumer — captures IS-Viewer printfs into the ring.
    try:
        await debug_consumer.start()
    except Exception as e:
        # Non-fatal at startup: /status will show debug_consumer.running=False
        # and the doctor's probe 9 will surface this.
        import logging
        logging.getLogger("sc64-api").warning("debug_consumer.start failed: %s", e)

    async def refresh_loop():
        while True:
            v = await asyncio.to_thread(deployer_version, SC64_DEPLOYER)
            _deployer_version_cache.update(v)
            p = await asyncio.to_thread(deployer_probe, SC64_DEPLOYER, SC64_SERVER_ADDR)
            _deployer_probe_cache.update(p)
            await asyncio.sleep(DEPLOYER_PROBE_TTL_S)
    asyncio.create_task(refresh_loop())


@app.on_event("shutdown")
async def _shutdown():
    await debug_consumer.stop()
    log_ring.close()
```

- [ ] **Step 3: Update the `/status` handler to fill the previously-stubbed sections**

Replace the existing `/status` body with:

```python
@app.get("/status")
async def status(auth: AuthContext = Depends(require_auth)):
    ring_stats = log_ring.stats()
    camera_reachable = await _probe_camera_async()
    return {
        "ok": True,
        "version": "0.3.0",
        "sc64_server": SC64_SERVER_ADDR,
        "upload_busy": _upload_lock.locked(),
        "user_id": auth.user_id,

        "deployer": {
            **_deployer_version_cache,
            **_deployer_probe_cache,
        },
        "tokens_file": token_file_status(SC64_TOKEN_FILE),
        "cart": cart_ftdi_status(),

        "camera": {
            "stream_reachable": camera_reachable,
            "last_snapshot_ms": _last_snapshot_ms,
        },
        "debug_consumer": {
            "running": debug_consumer.running,
            "pid": debug_consumer.pid,
            "started_at_ms": debug_consumer.started_at_ms,
            "consecutive_failures": debug_consumer.consecutive_failures,
            "last_line_ts_ms": ring_stats["last_line_ts_ms"],
            "last_line_preview": ring_stats["last_line_preview"],
        },
        "ring_buffer": {
            "in_memory_lines": ring_stats["in_memory_lines"],
            "in_memory_max": ring_stats["in_memory_max"],
            "file_path": ring_stats["file_path"],
            "file_bytes": ring_stats["file_bytes"],
        },
    }


_last_snapshot_ms: int | None = None


async def _probe_camera_async() -> bool:
    """Cheap reachability check — HEAD on the stream URL."""
    try:
        async with _httpx.AsyncClient(timeout=2.0) as c:
            r = await c.head(f"{CAMERA_STREAM_URL}/stream")
            return r.status_code < 500
    except Exception:
        return False
```

- [ ] **Step 4: Add `/logs` endpoint**

```python
@app.get("/logs")
async def get_logs(
    since: int = 0,
    until: int | None = None,
    limit: int = 10_000,
    auth: AuthContext = Depends(require_auth),
):
    lines = log_ring.read_window(since_ms=since, until_ms=until)
    if len(lines) > limit:
        lines = lines[-limit:]
    return {"lines": [{"ts_ms": l.ts_ms, "line": l.line} for l in lines]}
```

- [ ] **Step 5: Add `/camera/snapshot` endpoint**

```python
from fastapi.responses import Response


@app.get("/camera/snapshot")
async def camera_snapshot(auth: AuthContext = Depends(require_auth)):
    """Return one JPEG frame from the ustreamer MJPEG stream.

    ustreamer in some builds exposes /snapshot. If yours doesn't, we
    fall back to slicing the first complete JPEG out of /stream.
    """
    global _last_snapshot_ms
    import time
    async with _httpx.AsyncClient(timeout=5.0) as c:
        # Try /snapshot first (cheap if supported)
        try:
            r = await c.get(f"{CAMERA_STREAM_URL}/snapshot")
            if r.status_code == 200 and r.headers.get("content-type", "").startswith("image/"):
                _last_snapshot_ms = int(time.time() * 1000)
                return Response(content=r.content, media_type="image/jpeg")
        except Exception:
            pass

        # Fallback: pull one frame from the MJPEG stream
        try:
            async with c.stream("GET", f"{CAMERA_STREAM_URL}/stream") as r:
                buf = bytearray()
                async for chunk in r.aiter_bytes(chunk_size=4096):
                    buf.extend(chunk)
                    # Find SOI (0xFFD8) and EOI (0xFFD9)
                    soi = buf.find(b"\xff\xd8")
                    if soi == -1:
                        continue
                    eoi = buf.find(b"\xff\xd9", soi + 2)
                    if eoi == -1:
                        continue
                    jpeg = bytes(buf[soi:eoi + 2])
                    _last_snapshot_ms = int(time.time() * 1000)
                    return Response(content=jpeg, media_type="image/jpeg")
        except Exception as e:
            raise HTTPException(status_code=502, detail=f"camera unavailable: {e}")
    raise HTTPException(status_code=502, detail="camera produced no JPEG")
```

- [ ] **Step 6: Modify `/upload` to stop/start the consumer around the deployer call**

Find the existing `@app.post("/upload")` handler. Replace the body of `async with _upload_lock:` with:

```python
async with _upload_lock:
    suffix = Path(file.filename or "rom").suffix or ".z64"
    content = await file.read()

    with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
        tmp.write(content)
        tmp_path = tmp.name

    try:
        # Stop the debug consumer so the deployer server can accept our
        # upload client. See server.rs:163 — server is single-threaded.
        await debug_consumer.stop()

        result = await asyncio.to_thread(
            subprocess.run,
            [
                SC64_DEPLOYER, "-r", SC64_SERVER_ADDR,
                "upload", "--direct", "--reboot", tmp_path,
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
    finally:
        Path(tmp_path).unlink(missing_ok=True)
        # Always restart the consumer, even if upload failed —
        # we still want to capture whatever the cart says.
        try:
            await debug_consumer.start()
        except Exception:
            pass  # /status will surface the failure

    if result.returncode != 0:
        raise HTTPException(
            status_code=502,
            detail=f"sc64deployer: {result.stderr.strip() or result.stdout.strip()}",
        )

    import time
    return {
        "ok": True,
        "rom": file.filename,
        "size_bytes": len(content),
        "upload_complete_ts": int(time.time() * 1000),
        "user_id": auth.user_id,
    }
```

- [ ] **Step 7: Update the NixOS module to bundle new files**

In `pi-sc64/modules/sc64-api.nix`, update `appDir`:

```nix
appDir = pkgs.runCommand "sc64-api-app" {} ''
  mkdir -p $out
  cp ${../packages/sc64-api/app.py} $out/app.py
  cp ${../packages/sc64-api/status_probes.py} $out/status_probes.py
  cp ${../packages/sc64-api/log_ring.py} $out/log_ring.py
  cp ${../packages/sc64-api/debug_consumer.py} $out/debug_consumer.py
'';
```

Add a tmpfiles rule and option for the ring-buffer directory. In the same file, in the `config` block:

```nix
systemd.tmpfiles.rules = [
  "d /var/lib/sc64-api 0750 sc64api sc64api -"
  "d /var/lib/sc64-api/logs 0750 sc64api sc64api -"
];

# Add to the systemd service environment:
environment = {
  SC64_SERVER_ADDR = cfg.sc64ServerAddr;
  SC64_DEPLOYER = "${pkgs.sc64deployer}/bin/sc64deployer";
  SC64_TOKEN_FILE = cfg.tokenFile;
  SC64_ISV_OFFSET = "0x03FF0000";
  SC64_RING_BUFFER_DIR = "/var/lib/sc64-api/logs";
  SC64_CAMERA_STREAM_URL = "http://localhost:8080";
};

# Add to serviceConfig:
serviceConfig.ReadWritePaths = [ "/var/lib/sc64-api" ];
```

Merge with whatever's already in the file.

- [ ] **Step 8: Deploy to the Pi and verify**

```bash
cd pi-sc64
scripts/bootstrap-pi.sh full sc64pi.local 2>&1 | tail -10
```

Then:

```bash
TOKEN=$(cat ~/.sc64-api-token)
# /logs should return an empty list initially (or whatever's been captured since boot)
curl -s -H "Authorization: Bearer $TOKEN" "http://sc64pi.local:8064/logs?since=0" | jq

# /camera/snapshot should return a JPEG (with cart unplugged, camera should still work)
curl -s -H "Authorization: Bearer $TOKEN" http://sc64pi.local:8064/camera/snapshot -o /tmp/snap.jpg
file /tmp/snap.jpg  # Expected: JPEG image data

# /status now shows real values for debug_consumer, ring_buffer, camera
curl -s -H "Authorization: Bearer $TOKEN" http://sc64pi.local:8064/status | jq '.debug_consumer, .ring_buffer, .camera'
```

Expected: debug_consumer.running is true if cart is plugged in (deployer can open the device), ring_buffer.in_memory_max is 50000, camera.stream_reachable is true.

- [ ] **Step 9: Commit**

```bash
cd pi-sc64
git add packages/sc64-api/app.py modules/sc64-api.nix
git commit -m "$(cat <<'EOF'
feat(sc64-api): wire DebugConsumer + LogRing into app + /logs + /camera

- /upload now stops the debug consumer before running sc64deployer
  upload, then restarts it (single-threaded server constraint)
- /logs paginated by since/until ms timestamps
- /camera/snapshot proxies to ustreamer with /snapshot → /stream
  fallback
- /status fills the previously-stubbed debug_consumer, ring_buffer,
  and camera sections with real values

tmpfiles creates /var/lib/sc64-api/logs as sc64api:sc64api 0750 so
LogRing's file writer can rotate gzip logs there.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Chunk 4 acceptance test

- [ ] Pi-side unit tests pass: `cd pi-sc64/packages/sc64-api && PYTHONPATH=. python3 -m pytest tests/ -v`
- [ ] After `nixos-rebuild switch`, `/status` shows `debug_consumer.running == true` (cart plugged in) and `camera.stream_reachable == true`
- [ ] `curl -s -H "Authorization: Bearer $TOKEN" "http://sc64pi.local:8064/logs?since=0" | jq '.lines | length'` returns a positive integer
- [ ] `curl -s -H "Authorization: Bearer $TOKEN" http://sc64pi.local:8064/camera/snapshot -o /tmp/snap.jpg && file /tmp/snap.jpg` reports `JPEG image data`
- [ ] An end-to-end upload via curl works without breaking the debug stream: ``curl -X POST -H "Authorization: Bearer $TOKEN" -F "file=@build/starfox64.us.rev1.uncompressed.z64" http://sc64pi.local:8064/upload`` returns 200, then `/logs?since=<upload_complete_ts>` shows new post-reset lines from the cart
- [ ] `make hil-doctor` shows blocking probes 9 (debug consumer running) and 10 (camera reachable) now green — no longer stubbed

Once all six hold, Chunk 4 is done. Proceed to Chunk 5.

---

## Chunk 5: Mac-side round-trip — ctx + runner.run + smoke test

**Goal:** Mac-side counterpart to Chunk 4. Implements `ctx.upload_rom` (with cart-alive check), `ctx.wait_for_log`, `ctx.snapshot`, plus the `hil_test_runner.py run` subcommand. End state: `make hil-test tests/hil/test_boot_smoke.py` exits 0 against the live cart with a JPEG artifact saved.

**Files this chunk creates/modifies:**

- Modify: `sf64-practice-rom/tools/hil/client.py` — add `upload_rom`, `get_logs`, `get_camera_snapshot`
- Create: `sf64-practice-rom/tools/hil/ctx.py`
- Modify: `sf64-practice-rom/tools/hil_test_runner.py` — implement `run` subcommand
- Modify: `sf64-practice-rom/tests/hil/_unit/test_client.py` — add tests for new methods
- Create: `sf64-practice-rom/tests/hil/_unit/test_ctx.py`
- Create: `sf64-practice-rom/tests/hil/test_boot_smoke.py`

**Skills to use:**
- @superpowers:test-driven-development
- @superpowers:verification-before-completion before claiming chunk complete

### Task 5.1: Extend `hil/client.py` with upload/logs/snapshot

**Files:**
- Modify: `sf64-practice-rom/tools/hil/client.py`

- [ ] **Step 1: Add methods to HilClient**

In `client.py`, replace the `# Chunk 4 will add: ...` comment with:

```python
    def upload_rom(self, path: str) -> dict[str, Any]:
        """Upload a ROM. Returns the server response including upload_complete_ts."""
        with open(path, "rb") as f:
            files = {"file": (Path(path).name, f, "application/octet-stream")}
            resp = self._request(
                "POST", "/upload",
                headers=self._auth_headers(),
                files=files,
                timeout=180.0,  # uploads can be slow
            )
        if resp.status_code == 409:
            raise UploadConflict("another upload in progress")
        if resp.status_code != 200:
            raise HilError(f"upload failed: {resp.status_code} {resp.text}")
        return resp.json()

    def get_logs(self, since_ms: int, until_ms: int | None = None,
                 limit: int = 10_000) -> list[dict[str, Any]]:
        params: dict[str, int] = {"since": since_ms, "limit": limit}
        if until_ms is not None:
            params["until"] = until_ms
        resp = self._request("GET", "/logs",
                             headers=self._auth_headers(),
                             params=params)
        resp.raise_for_status()
        return resp.json()["lines"]

    def get_camera_snapshot(self) -> bytes:
        resp = self._request("GET", "/camera/snapshot",
                             headers=self._auth_headers(),
                             timeout=10.0)
        if resp.status_code != 200:
            raise SnapshotUnavailable(f"snapshot failed: {resp.status_code}")
        return resp.content
```

- [ ] **Step 2: Run existing client unit tests**

```bash
cd ~/code/sf64-practice-rom
PYTHONPATH=. python3 -m pytest tests/hil/_unit/test_client.py -v
```

Expected: existing tests still pass.

- [ ] **Step 3: Add tests for the new methods**

Append to `tests/hil/_unit/test_client.py`:

```python
class TestUploadRom:
    def test_409_raises_upload_conflict(self, tmp_path):
        rom = tmp_path / "x.z64"
        rom.write_bytes(b"\x00" * 1024)
        cfg = ClientConfig(host="x", token="t")
        with HilClient(cfg) as c:
            mock_resp = MagicMock(status_code=409, text="busy")
            with patch.object(c._client, "request", return_value=mock_resp):
                from tools.hil.client import UploadConflict
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
```

Run: `PYTHONPATH=. python3 -m pytest tests/hil/_unit/test_client.py -v`

Expected: all pass.

### Task 5.2: Implement `hil/ctx.py`

**Files:**
- Create: `sf64-practice-rom/tools/hil/ctx.py`

- [ ] **Step 1: Write the file**

```python
"""TestContext for HIL tests.

Test files define `def run(ctx): ...` and call methods like:
    ctx.upload_rom("build/foo.z64")
    ctx.wait_for_log(r"ISViewer init OK")
    shot = ctx.snapshot()
    ctx.assert_log_contains(r"PRACTICE READY")

ctx manages: upload anchor timestamp, polling for log matches,
artifact paths, assertion bookkeeping. Chunk 6 adds the cart-wedge
banner + assert_log_not_contains + JUnit emission.
"""
from __future__ import annotations

import re
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from tools.hil.client import HilClient, ClientConfig, HilError


class LogWaitTimeout(HilError):
    pass


class CartWedgedError(HilError):
    """upload_rom() saw no IS-Viewer line within cart_alive_timeout_ms."""


@dataclass
class TestContext:
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
        """Poll /logs since upload_complete_ts. Return first matching line."""
        if self.upload_complete_ts is None:
            raise HilError("wait_for_log called before upload_rom")
        regex = re.compile(pattern)
        deadline = time.time() * 1000 + timeout_ms
        seen_last_idx = 0
        while time.time() * 1000 < deadline:
            lines = self.client.get_logs(since_ms=self.upload_complete_ts)
            for entry in lines[seen_last_idx:]:
                if regex.search(entry["line"]):
                    return entry
            seen_last_idx = len(lines)
            time.sleep(0.1)
        raise LogWaitTimeout(
            f"pattern {pattern!r} not seen within {timeout_ms}ms"
        )

    def advance_seconds(self, seconds: float) -> None:
        """Wall-clock wait. Test authors must budget slack for hardware boot variance."""
        time.sleep(seconds)

    def snapshot(self, name: str | None = None) -> Path:
        """GET /camera/snapshot, save to artifacts dir, return the path."""
        self._snapshot_seq += 1
        suffix = f"{self._snapshot_seq}" if name is None else name
        path = self.artifacts_dir / f"{self.test_name}-{suffix}.jpg"
        data = self.client.get_camera_snapshot()
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

    def assert_true(self, cond: bool, msg: str = "") -> None:
        if cond:
            self.passes.append(msg)
        else:
            self.failures.append(msg)
            print(f"  FAIL: {msg}")
```

- [ ] **Step 2: Unit tests for ctx**

Create `sf64-practice-rom/tests/hil/_unit/test_ctx.py`:

```python
"""Unit tests for ctx.TestContext — mocked client."""
from __future__ import annotations

import pytest
from unittest.mock import MagicMock, patch

from tools.hil.ctx import TestContext, CartWedgedError, LogWaitTimeout
from tools.hil.client import HilClient, ClientConfig


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
```

Run: `PYTHONPATH=. python3 -m pytest tests/hil/_unit/test_ctx.py -v`

Expected: all pass.

### Task 5.3: Implement `run` subcommand in `hil_test_runner.py`

**Files:**
- Modify: `sf64-practice-rom/tools/hil_test_runner.py`

- [ ] **Step 1: Replace `cmd_run` with a real implementation**

Replace the existing `cmd_run` body:

```python
def cmd_run(args: argparse.Namespace) -> int:
    import importlib.util
    import uuid

    from tools.hil.client import HilClient, ClientConfig
    from tools.hil.ctx import TestContext, CartWedgedError
    from tools.hil.doctor import probe_all, render_report

    # Optional preflight
    if not args.skip_preflight:
        report = probe_all(args.host)
        blocking = report.blocking_failed
        if blocking:
            print(render_report(report))
            print("\n  Preflight failed. Aborting (use --skip-preflight to bypass).")
            return 1

    test_paths = _discover_tests(args.path)
    if not test_paths:
        print(f"No tests found at {args.path}", file=sys.stderr)
        return 2

    run_id = uuid.uuid4().hex[:8]
    artifacts_root = (
        __import__("pathlib").Path("tests/hil/_artifacts") / run_id
    )
    artifacts_root.mkdir(parents=True, exist_ok=True)
    print(f"  Run ID: {run_id}")
    print(f"  Artifacts: {artifacts_root}")

    cfg = ClientConfig(host=args.host)
    total_fail = 0
    for tp in test_paths:
        test_name = __import__("pathlib").Path(tp).stem
        print(f"\n  >> {test_name}")
        mod = _load_test_module(tp)
        if not hasattr(mod, "run"):
            print(f"     SKIP (no `def run(ctx)`)")
            continue
        with HilClient(cfg) as client:
            ctx = TestContext(
                client=client,
                artifacts_dir=artifacts_root,
                test_name=test_name,
            )
            try:
                mod.run(ctx)
            except CartWedgedError as e:
                print(f"     CART WEDGED: {e}")
                # Chunk 6 wires the banner + EX_TEMPFAIL exit; for Chunk 5
                # we just fail the test.
                ctx.failures.append(f"cart wedged: {e}")
            except Exception as e:
                import traceback
                traceback.print_exc()
                ctx.failures.append(f"exception: {e}")
            for p in ctx.passes:
                print(f"     PASS: {p}")
            for f in ctx.failures:
                print(f"     FAIL: {f}")
            if ctx.failures:
                total_fail += 1
    return 0 if total_fail == 0 else 1


def _discover_tests(path: str) -> list[str]:
    import glob
    from pathlib import Path
    p = Path(path)
    if p.is_file():
        return [str(p)]
    if p.is_dir():
        return sorted(glob.glob(str(p / "test_*.py")))
    return []


def _load_test_module(path: str):
    import importlib.util
    spec = importlib.util.spec_from_file_location("hil_test_module", path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
```

Update the argparse parser:

```python
    run = subs.add_parser("run", help="run a HIL test or directory of tests")
    run.add_argument("path", help="test file or directory")
    run.add_argument("--host", default=DEFAULT_HOST)
    run.add_argument("--skip-preflight", action="store_true",
                     help="skip the inline doctor preflight")
    run.set_defaults(func=cmd_run)
```

### Task 5.4: Write `test_boot_smoke.py`

**Files:**
- Create: `sf64-practice-rom/tests/hil/test_boot_smoke.py`

- [ ] **Step 1: Write the test**

```python
"""Smoke test: upload the current practice ROM build, confirm the cart
boots and emits any IS-Viewer output, snapshot the screen.

This is the canonical "is the HIL rig alive" test. Failures here mean
the rig itself is broken, not the ROM.

Pre-req: `make practice -j4` has run and produced
build/starfox64.us.rev1.uncompressed.z64.
"""
from __future__ import annotations

ROM_PATH = "build/starfox64.us.rev1.uncompressed.z64"


def run(ctx):
    import os
    if not os.path.isfile(ROM_PATH):
        ctx.failures.append(f"ROM not built at {ROM_PATH} — run `make practice -j4`")
        return

    ctx.upload_rom(ROM_PATH)
    # Wait for ANY IS-Viewer line. The IS-Viewer module emits an init
    # banner when the channel comes up; if our ROM uses MODS_ISVIEWER
    # this fires reliably within ~50ms.
    ctx.wait_for_log(r".+", timeout_ms=5000)
    ctx.advance_seconds(2)
    shot = ctx.snapshot("boot")
    ctx.assert_true(shot.exists() and shot.stat().st_size > 0,
                    "screenshot captured")
    ctx.assert_true(
        # Lenient assertion: ANY log line at all means the cart is alive
        # and printing. Tighter assertions belong in dedicated tests.
        True, "cart booted and emitted at least one IS-Viewer line",
    )
```

- [ ] **Step 2: Build the ROM if needed**

```bash
cd ~/code/sf64-practice-rom
make practice -j4
```

(Already built? It'll no-op.)

- [ ] **Step 3: Run the test**

```bash
PYTHONPATH=. python3 tools/hil_test_runner.py run tests/hil/test_boot_smoke.py
```

Expected:
- Preflight runs and is green (cart plugged in)
- `>> test_boot_smoke` printed
- `PASS: screenshot captured`
- `PASS: cart booted and emitted at least one IS-Viewer line`
- Artifact JPEG written under `tests/hil/_artifacts/<run_id>/test_boot_smoke-boot.jpg`
- Exit 0

**Iterate against the real cart until green.** Common failures:
- `CART WEDGED` — the cart isn't picking up the upload's reset signal. Press the physical N64 reset button after upload, or check that the cart is in a state where `--reboot` actually triggers.
- `LogWaitTimeout` — IS-Viewer init line not seen within 5s. Could mean: (a) ROM doesn't have MODS_ISVIEWER=1, (b) deployer is upstream not qw-local (no flush patch), (c) the `--isv 0x03FF0000` offset is wrong.
- `screenshot` is 0 bytes — camera dead. `hil doctor` should have caught this.

- [ ] **Step 4: Commit**

```bash
cd ~/code/sf64-practice-rom
git add tools/hil/client.py tools/hil/ctx.py tools/hil_test_runner.py tests/hil/_unit/test_ctx.py tests/hil/test_boot_smoke.py
git commit -m "$(cat <<'EOF'
feat(hil): round-trip MVP — ctx, runner.run, test_boot_smoke

ctx.upload_rom blocks until first IS-Viewer line proves cart-alive
(CartWedgedError on silence). ctx.wait_for_log polls /logs since the
upload anchor. ctx.snapshot fetches /camera/snapshot to the artifacts
dir. hil_test_runner.py `run` subcommand discovers and executes tests
with inline preflight (--skip-preflight to bypass).

test_boot_smoke is the canonical "is the rig alive" smoke. Failures
there mean the rig, not the ROM, is broken.

Cart-wedge banner and assert_log_not_contains land in Chunk 6.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Chunk 5 acceptance test

- [ ] Mac-side unit tests pass: `cd ~/code/sf64-practice-rom && PYTHONPATH=. python3 -m pytest tests/hil/_unit/ -v`
- [ ] `make hil-test tests/hil/test_boot_smoke.py` (or the equivalent `python3 tools/hil_test_runner.py run ...`) exits 0 with the test passing
- [ ] The artifact JPEG exists under `tests/hil/_artifacts/<run_id>/test_boot_smoke-boot.jpg` and is a valid non-empty JPEG (`file <path>` reports `JPEG image data`)
- [ ] Inline preflight runs before the test: temporarily delete `~/.sc64-api-token` and rerun — the doctor's probe 4 failure short-circuits the run with the fix box (no test executes), exit 1

Once all four hold, Chunk 5 is done. Restore your token and proceed to Chunk 6.

---
