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
│   │   ├── log_ring.py                                # NEW (Chunk 3) — in-memory ring + rotating file writer
│   │   ├── debug_consumer.py                          # NEW (Chunk 3) — sc64deployer debug subprocess manager
│   │   ├── status_probes.py                           # NEW (Chunk 2) — fills enriched /status fields (tokens_file, cart, deployer)
│   │   ├── mock_cart.py                               # NEW (Chunk 5) — --mock-cart printer for VM test
│   │   └── tests/
│   │       ├── test_log_ring.py                       # NEW (Chunk 3)
│   │       ├── test_debug_consumer.py                 # NEW (Chunk 3)
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
│   ├── hil_test_runner.py                             # NEW (Chunk 2) — subcommands: run (default), doctor
│   ├── hil/
│   │   ├── __init__.py                                # NEW (Chunk 2)
│   │   ├── client.py                                  # NEW (Chunk 2) — httpx wrapper for sc64-api
│   │   ├── ctx.py                                     # NEW (Chunk 3) — TestContext primitives
│   │   ├── doctor.py                                  # NEW (Chunk 2) — preflight probes + fix-box rendering
│   │   ├── banner.py                                  # NEW (Chunk 4) — cart-wedged banner
│   │   └── junit.py                                   # NEW (Chunk 4) — JUnit XML emission
│   └── n64-hil-mcp/                                   # NEW (Chunk 5)
│       ├── pyproject.toml
│       ├── server.py
│       └── test_mcp_smoke.py
├── tests/
│   └── hil/
│       ├── __init__.py                                # NEW (Chunk 2)
│       ├── SETUP.md                                   # SKELETON in Chunk 1; expanded in Chunk 5
│       ├── README.md                                  # NEW (Chunk 5)
│       ├── _artifacts/                                # gitignored; populated at runtime
│       ├── _fixtures/
│       │   ├── build_wedge_rom.py                     # NEW (Chunk 4) — generates the wedge fixture deterministically
│       │   └── wedge_rom.z64                          # gitignored — output of build_wedge_rom.py
│       ├── _unit/
│       │   ├── test_ctx.py                            # NEW (Chunk 3)
│       │   ├── test_doctor.py                         # NEW (Chunk 2)
│       │   └── test_client.py                         # NEW (Chunk 2)
│       ├── test_boot_smoke.py                         # NEW (Chunk 3)
│       ├── test_isv_protocol_regression.py            # NEW (Chunk 4)
│       └── test_cart_wedge_detection.py               # NEW (Chunk 4)
├── .gitignore                                         # MODIFIED (Chunk 2) — add tests/hil/_artifacts/, _fixtures/wedge_rom.z64
├── CLAUDE.md                                          # MODIFIED (Chunk 5) — "HIL tests" section
└── Makefile                                           # MODIFIED (Chunk 2) — `make hil-test`, `make hil-doctor` convenience targets
```

---

## Chunk index

1. **Chunk 1: Foundation — Pi bring-up (SD image build + bootstrap + SETUP.md skeleton)** — get a freshly-flashed Pi reachable with the user's SSH key + a provisioned bearer token + `sc64deployer` running the `qw-local` branch. Ships a minimal SETUP.md skeleton aligned with spec §8 step 1.
2. **Chunk 2: Enriched `/status` + `hil doctor`** — Pi-side status enrichment and the Mac-side doctor with 10 probes. End state: `python3 tools/hil_test_runner.py doctor` shows all-green against the live Pi.
3. **Chunk 3: Round-trip MVP** — `DebugConsumer` + `LogRing` + `/logs` + `/camera/snapshot` + `ctx.upload_rom/wait_for_log/snapshot` + `test_boot_smoke.py` green against the real cart.
4. **Chunk 4: Cart-wedge banner + assertion suite + JUnit + remaining tests** — `assert_log_contains/not_contains`, the wedge banner, the broken-ROM fixture + matching test, the IS-Viewer protocol regression test, JUnit emission.
5. **Chunk 5: NixOS mock-cart VM test + MCP server + docs** — regression net + Claude tools + SETUP.md expansion + README.md + CLAUDE.md addition.

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
