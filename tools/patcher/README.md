# SF64 Practice Patcher

Patch a legally owned Star Fox 64 US Rev 1.1 ROM into the SF64 practice ROM.

```bash
npx sf64-practice-patcher patch ./starfox64.us.rev1.z64 --out ./starfox64-practice.z64
```

The package ships only a BPS patch and manifest. It validates the source ROM,
applies the patch locally, verifies the output hash, and writes a `.z64` ROM.

The core modules in `src/core/` are browser-safe and work with `Uint8Array`
inputs, so a web app can reuse them without uploading user ROMs to a server.
