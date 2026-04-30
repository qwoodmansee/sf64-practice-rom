# HW_VERIFY Phase 7 - SD Slot Persistence

## Prerequisites
- SC64 v2 with FAT32 SD card
- IS-Viewer session running (`sc64deployer debug --isv 0x03FF0000`)
- Latest ROM built and uploaded via `./tools/sc64dev`
- Phase 6 HW_VERIFY passed (OSK and file browser render correctly)

## T1 - Save to SD card
1. Launch any level (e.g. Corneria).
2. Open practice menu (hold L+R). Press Z.
3. OSK opens with "SD SAVE NAME:" prompt.
4. Type "CORNERIA" using D-pad + A.
5. Press START to confirm.
6. HUD shows "SD SAVE OK".
7. IS-Viewer shows no error prints.

## T2 - Verify file exists on SD
1. Remove SD card, insert in PC.
2. Verify /sf64-practice/states/CORNERIA.SF64ST exists.
3. No CORNERIA.SF64ST.tmp file should exist (atomic write cleanup).
4. File size should be non-zero.

## T3 - Load from SD after reboot
1. Reboot N64 (physical reset button).
2. Launch the same level.
3. Open practice menu. Press Z+B.
4. File browser shows CORNERIA.SF64ST.
5. Navigate to it, press A.
6. HUD shows "SD LOAD OK".
7. Game state matches the saved state (verify player position, shields, etc.).

## T4 - Load wrong-version file (negative test)
1. On PC: open CORNERIA.SF64ST in a hex editor.
2. Change bytes at offset 0x04-0x05 (lib_version) to 0xFF 0xFF.
3. Re-insert SD, load the file.
4. HUD shows "SD LOAD FAIL" (version mismatch detected).
5. Game state is NOT corrupted.

## T5 - SD not present (graceful fallback)
1. Boot without SD card inserted.
2. Open practice menu. Press Z.
3. HUD shows "NO SD CART" immediately.
4. Press Z+B. HUD shows "NO SD CART".
5. No crash.

## PASS criteria
- T1: SD SAVE OK in HUD, no crashes
- T2: File exists, no .tmp residue
- T3: SD LOAD OK in HUD, state restored
- T4: Bad version rejected, game state intact
- T5: Graceful degradation, no crash
