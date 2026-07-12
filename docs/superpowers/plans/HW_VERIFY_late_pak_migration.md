# Hardware Verification: .practice_late_pak migration (scene-window fix)

**Build:** release/v0.7.0 after the 2026-07-11 scene-window/z-buffer fix
(practice feature code migrated from `main` to `.practice_late_pak` at
0x80730000; `ovl_i1_VRAM` back to 0x8018c270; all scene stacks fit below
0x80281000 with worst-case margin ~0x59C0).

**Why hardware-only:** the bug class this fixes (RDP z-writeback shredding
level assets) is invisible to HLE emulators, and the SD save/load path is
timing-sensitive to layout changes. mupen64plus passing means nothing for
either. Every item below must run on the real console.

## Checklist

Boot & core:
- [ ] Cold boot to level select (logo + owl textures render; they were
      already Pak-resident and the loader now DMAs two segments)
- [ ] Practice menu opens; options menu (sOvlmenu_Option overflowed by
      0xaf20 pre-fix) renders with no garbage/wedge
- [ ] Frame advance, input display, HUD overlays all draw

The Katt freeze (primary regression target):
- [ ] Play Zoness from level start through Katt's first radio line and at
      least 60 s beyond — no freeze, portrait renders correctly, her ship
      visible (her portrait + ship DL previously sat inside gZBuffer)
- [ ] Load a Zoness savestate near the Katt trigger; same result

Other previously-overflowing scenes (were +0x3120..+0x11800 over the line):
- [ ] Titania (setup 5 reachable via normal play/checkpoint): plays clean
- [ ] Macbeth: full run (also exercises the old boot-staging fix)
- [ ] Aquas: plays clean (historic crasher)
- [ ] Solar, Area 6, Andross: spot-check a few minutes each

SD save/load (layout near the SD stack changed: ff/diskio/slot_manager
moved from main to late_pak; iodev_sc64/sd_host untouched in late_core):
- [ ] SD SAVE from pause menu: file written, no wedge
- [ ] SD LOAD of that file: restores correctly
- [ ] RAM slot save/load hotkeys work
- [ ] LOAD VERSION refusal still behaves on a stale file (older build's save)

Reset diagnostics (kept permanent):
- [ ] Press reset during normal gameplay with the deployer attached:
      `[prenmi]` thread dump prints, and the console still NMI-resets
      cleanly afterwards (gStartNMI is now set by the fault thread)

If SD save/load misbehaves: this migration deliberately did NOT touch
`.practice_late_core` contents/order or `iodev_sc64.c`, and late_pak is
ROM-placed after late_core, so late_core's ROM/RAM layout is byte-identical
to the previous hardware-good build. Suspect the moved FatFs/diskio layer
first, and compare against commit before this migration by reverting only
the late_pak membership of ff/diskio/slot_manager (they can temporarily go
back to main — the scene invariant has ~23 KB of slack).
