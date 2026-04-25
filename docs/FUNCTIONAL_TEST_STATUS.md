# Functional Test Infrastructure Status

## What Works

### Static invariants (`tools/practice_invariants.py`)
- Config field init checks, function declaration checks, engine hook checks
- Runs on pre-commit via `.git/hooks/pre-commit`
- Fully working

### Build smoke test
- `make practice -j4` on pre-commit
- Fully working

### C test harness (`tools/m64p_harness.c`)
- Compiles and links against mupen64plus (Homebrew install)
- Boots the practice ROM with video (Rice plugin)
- Protocol: ADVANCE/READ32/WRITE32/SLEEP/WAIT/QUIT via stdin/stdout
- Memory reading confirmed working (saw correct config defaults: bombCount=3, lifeCount=2)
- `--headed` flag for video window, headless mode stalls (game needs GFX plugin)
- Audio plugin skipped (silent operation)

## What Needs Fixing

### Memory read consistency
One run showed perfect values, most runs show zeros or garbage at the same addresses.
Root cause: race condition between PAUSE command and emulator thread. PAUSE is async —
the emulator may still be mid-frame when we read memory.

**Recommended fix approaches (try in order):**
1. After sending PAUSE, wait for the emulator state callback to confirm M64EMU_PAUSED
   before reading memory
2. Use frame-counting ADVANCE instead of wall-clock SLEEP — ADVANCE synchronizes on
   frame boundaries via the frame callback
3. Add a small usleep(50000) after PAUSE before reading

### Python test runner rewrite
`tools/m64p_test_runner.py` currently uses ctypes (broken). Needs rewrite to:
1. Compile `tools/m64p_harness.c` if binary is missing
2. Launch harness as subprocess
3. Send/receive protocol commands via pipes
4. TestContext class wraps protocol into `read_s32()`, `write_s32()`, `advance()` etc.

Test files in `tests/test_*.py` should work with minimal changes — they already use
a `ctx` object with `config_field()`, `advance()`, etc.

### extract_symbols.py
Symbol addresses shift between builds. The script reads from the map file but the
hardcoded CONFIG_OFFSETS dict needs verification against current `include/practice.h`.

## Architecture

```
pre-commit hook
  ├── practice_invariants.py  (grep-based static checks)
  ├── make practice -j4       (build smoke test)
  └── m64p_test_runner.py     (Python test orchestrator)
        └── m64p_harness      (C binary, subprocess)
              └── libmupen64plus + plugins (Homebrew)
                    └── practice ROM (build/starfox64.us.rev1.uncompressed.z64)
```

## Key mupen64plus API Notes

1. ROM must be opened BEFORE attaching plugins (otherwise error 10)
2. Emulator must run on main thread (macOS Cocoa/SDL requirement)
3. RDRAM stored in host byte order — use native uint32_t reads
4. DebugMemRead32 not available (core not built with debugger) — use DebugMemGetPointer
5. Headless (no GFX plugin) stalls at gGameState=1; headed mode required
6. Symbol addresses shift between builds — always parse from map file

## Build the harness

```bash
cc -O2 -o tools/m64p_harness tools/m64p_harness.c \
   -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 \
   -L/opt/homebrew/lib -lmupen64plus -lSDL2
```

## Quick manual test

```bash
printf "SLEEP 20000\nREAD32 0x17D964\nQUIT\n" | \
  timeout 40 tools/m64p_harness --headed build/starfox64.us.rev1.uncompressed.z64
```
