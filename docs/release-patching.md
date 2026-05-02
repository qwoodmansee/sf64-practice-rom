# ROM Patch Release Workflow

The practice ROM is distributed as a BPS patch against a legally owned Star Fox
64 US Rev 1.1 ROM. Do not distribute base ROMs or patched ROMs.

## User Flow

CLI users can patch their own ROM with:

```bash
npx sf64-practice-patcher patch /path/to/starfox64.us.rev1.z64 --out starfox64-practice.z64
```

The patcher validates the input ROM before writing output. The expected base ROM
is Star Fox 64 US Rev 1.1:

```text
MD5: 741a94eee093c4c8684e66b89f8685e8
```

Inputs in `.z64`, `.v64`, or `.n64` byte order are accepted and normalized in
memory. Output is always `.z64`.

## Release Build

After building the practice ROM, generate release assets with:

```bash
make practice-patch PATCH_VERSION=0.1.0
```

This produces:

```text
tools/patcher/src/assets/manifest.json
tools/patcher/src/assets/sf64-practice-v0.1.0.bps
```

`manifest.json` records source, patch, and target hashes. The BPS file contains
only binary differences and source/target checksums; it does not contain a
complete ROM.

## Website Handoff

A web app should run patching fully client-side:

1. Host the web app, `manifest.json`, and `.bps` patch.
2. Let the user select their own ROM with a file input or drag-and-drop.
3. Read the file locally with browser APIs.
4. Validate the normalized ROM hash against the manifest.
5. Apply the BPS patch in memory.
6. Offer the patched `.z64` as a download.

The user's ROM should never be uploaded to the server. The shared patching core
in `tools/patcher/src/core/` uses `Uint8Array` inputs and has no Node filesystem
dependency, so it can be reused by a browser bundle.

## Developer Checks

Run these before publishing release assets:

```bash
cd tools/patcher
npm test
npm run typecheck
npm run build
cd ../..
python3 tools/practice_invariants.py
make practice-patch PATCH_VERSION=<version>
```
