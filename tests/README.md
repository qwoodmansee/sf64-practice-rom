# E2E Tests for SF64 Practice ROM

Basic end-to-end tests that verify the practice ROM boots without crashing.

## Current Status

- ✅ **Boot tests**: Verify ROM boots and runs without immediate crash
- ⚠️ **Level entry tests**: Currently just boot tests, not verifying actual level selection/loading

## Running Tests

```bash
python3 tests/test_e2e_levels.py
```

Results:
- Each level runs for 30 seconds (safe timeout to let the emulator start)
- Pass = ROM didn't crash during that window
- Fail = ROM crashed or exited with error code

## Next Steps: Enhanced Testing

To test actual **level entry** (not just boot), you have two options:

### Option A: Use DTM (Movie) Files
Generate mupen64plus movie files with input sequences, then play them back.
- Pros: Deterministic, fast, good for CI/automation
- Cons: Need to get command-line DTM playback working on your Unraid setup
- Current status: DTM files are generated (`tests/dtm/`), but playback needs platform-specific command-line flags

```bash
# On Linux (Unraid): mupen64plus supports --movie flag
mupen64plus --noosd --nospeedlimit --movie tests/dtm/test_level_0.dtm rom.z64

# macOS: May not have DTM playback in CLI, would need to test via GUI or other means
```

### Option B: Use Your Mupen64Plus Harness
Your existing C test harness can:
1. Boot the ROM via mupen64plus GDB stub
2. Feed inputs to navigate menus + select levels
3. Read game memory to verify level loaded
4. Check game state / memory markers

This is more complex but gives precise control over timing and state checking.

## Input Sequence Reference

Based on your SF64 menu structure:

```
Boot → Wait 5s (intro/menu load)
     → D-pad Down N times (select level N)
     → A button (confirm)
     → Wait 5s (level load)
```

Level mapping (0-indexed):
- 0 = Corneria
- 1 = Meteo
- 2 = Sector X
- 3 = Fortess Corneria
- 4 = Venom 1
- 5 = Venom 2

## Files

- `dtm_generator.py` - Generates DTM movie files with input sequences
- `test_e2e_levels.py` - Runs tests, checks for crashes/hangs
- `dtm/` - Generated DTM files (committed for reproducibility)

## Debugging

If a test fails:

1. **ROM doesn't boot**: Try running manually:
   ```bash
   mupen64plus ~/Desktop/current-practice-rom/*.z64
   ```
   Look for graphics/plugin errors.

2. **DTM playback doesn't work**: Check mupen64plus version and plugins:
   ```bash
   mupen64plus --version
   ```
   On Unraid, may need to install headless video/audio plugins.

3. **Level not loading**: If you implement level-entry tests, check:
   - Menu timing (maybe needs more frames between inputs)
   - Game memory addresses for level state verification
   - Whether the harness can read memory during playback
