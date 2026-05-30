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
│   │   ├── mock_cart.py                               # NEW (Chunk 6) — --mock-cart printer for VM test
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
│   │   ├── ctx.py                                     # NEW (Chunk 4) — TestContext primitives
│   │   ├── doctor.py                                  # NEW (Chunk 3) — preflight probes + fix-box rendering
│   │   ├── banner.py                                  # NEW (Chunk 5) — cart-wedged banner
│   │   └── junit.py                                   # NEW (Chunk 5) — JUnit XML emission
│   └── n64-hil-mcp/                                   # NEW (Chunk 6)
│       ├── pyproject.toml
│       ├── server.py
│       └── test_mcp_smoke.py
├── tests/
│   └── hil/
│       ├── __init__.py                                # NEW (Chunk 3)
│       ├── SETUP.md                                   # SKELETON in Chunk 1; expanded in Chunk 6
│       ├── README.md                                  # NEW (Chunk 6)
│       ├── _artifacts/                                # gitignored; populated at runtime
│       ├── _fixtures/
│       │   ├── build_wedge_rom.py                     # NEW (Chunk 5) — generates the wedge fixture deterministically
│       │   └── wedge_rom.z64                          # gitignored — output of build_wedge_rom.py
│       ├── _unit/
│       │   ├── test_ctx.py                            # NEW (Chunk 4)
│       │   ├── test_doctor.py                         # NEW (Chunk 3)
│       │   └── test_client.py                         # NEW (Chunk 3)
│       ├── test_boot_smoke.py                         # NEW (Chunk 4)
│       ├── test_isv_protocol_regression.py            # NEW (Chunk 5)
│       └── test_cart_wedge_detection.py               # NEW (Chunk 5)
├── .gitignore                                         # MODIFIED (Chunk 3) — add tests/hil/_artifacts/, _fixtures/wedge_rom.z64
├── CLAUDE.md                                          # MODIFIED (Chunk 6) — "HIL tests" section
└── Makefile                                           # MODIFIED (Chunk 3) — `make hil-test`, `make hil-doctor` convenience targets
```

---

## Chunk index

1. **Chunk 1: Foundation — Pi bring-up (SD image build + bootstrap + SETUP.md skeleton)** — get a freshly-flashed Pi reachable with the user's SSH key + a provisioned bearer token + `sc64deployer` running the `qw-local` branch. Ships a minimal SETUP.md skeleton aligned with spec §8 step 1.
2. **Chunk 2: Pi-side enriched `/status`** — `status_probes.py` (token-file mode, FTDI presence, deployer version, deployer-can-open-FTDI probe) wired into `/status`. End state: `curl /status` returns the enriched JSON shape with stub values for fields the Mac doctor will eventually consume.
3. **Chunk 3: Mac-side `hil doctor`** — `hil/client.py` + `hil/doctor.py` with the 10 probes from spec §10.2, `hil_test_runner.py doctor` subcommand. End state: `make hil-doctor` shows all-green against the live Pi.
4. **Chunk 4: Round-trip MVP** — `DebugConsumer` + `LogRing` + `/logs` + `/camera/snapshot` + `ctx.upload_rom/wait_for_log/snapshot` + `test_boot_smoke.py` green against the real cart.
5. **Chunk 5: Cart-wedge banner + assertion suite + JUnit + remaining tests** — `assert_log_contains/not_contains`, the wedge banner, the broken-ROM fixture + matching test, the IS-Viewer protocol regression test, JUnit emission.
6. **Chunk 6: NixOS mock-cart VM test + MCP server + docs** — regression net + Claude tools + SETUP.md expansion + README.md + CLAUDE.md addition.

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

Per spec §8 step 1: SETUP.md ships at the END of this milestone, written after the bootstrap script has been dogfooded once against a real cold-start. The skeleton goes in now; the polish + screenshots wait for Chunk 5.

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
