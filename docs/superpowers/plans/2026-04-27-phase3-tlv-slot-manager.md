# Phase 3: TLV serial + RAM slot manager Completion Notes

> **Status:** Complete as of 2026-04-27.

## Goal

Add the portable serialization and RAM-slot layer required before the full
`practice_save.c` rewrite in Phase 4.

## Delivered

- `lib/serial.{c,h}` implements the Phase 3 TLV codec:
  - `u16 tag`
  - `u16 flags` reserved as zero
  - `u32 length`
  - payload bytes
  - all multi-byte fields little-endian
- `lib/slot_manager.{c,h}` implements RAM-only slots with:
  - versioned `SF64` headers
  - injected save/load callbacks
  - caller-owned RAM storage via `slot_manager_set_ram_storage`
  - explicit unsupported results for SD save/load APIs until Phase 7
- `src/practice/practice_slot_test.c` runs a boot-time fake-state save/load
  smoke test inside the practice ROM, then resets the slot manager so Phase 4
  can initialize it for real save-state wiring.
- Host tests cover TLV round-trip/error handling and slot manager state,
  version, corruption, overflow, clear, and slot-cycle behavior.
- `tests/test_slot_manager_fake_state.lua` verifies the in-ROM fake-state smoke
  test through exported symbols.
- Linker and symbol extraction are wired through:
  - `tools/patch_linker_script.py`
  - `tools/extract_symbols.py`

## Design note: caller-owned storage

The original design described slot allocation with `malloc/free`, but the ROM
does not expose a general freeable allocator and `lib/` must not depend on the
game-specific `Memory_Allocate` arena. Phase 3 therefore uses caller-owned RAM
storage. Phase 4 owns the real slot buffer sizing after the heap audit.

## Verification run

```bash
make lib-test TEST_DISKIO_IMG=/Users/qwoodmansee/code/sf64-practice-rom/.claude/worktrees/user-requests/lib/test/test_diskio.img
python3 tools/practice_invariants.py
make practice -j4
python3 tools/run_tests.py test_slot_manager_fake_state
```

Results:

- Host library tests passed.
- Static invariants passed.
- Practice ROM build passed.
- BizHawk test command skipped locally when `BIZHAWK_PATH` was unavailable.

## Exit criteria

- [x] TLV codec builds as portable `lib/` code.
- [x] RAM-only slot manager builds as portable `lib/` code.
- [x] SD save/load entry points are present but return unsupported.
- [x] Host tests cover success and corrupt/error paths.
- [x] Practice ROM links the new library objects.
- [x] In-ROM fake-state smoke test exists for BizHawk/manual verification.
