# HW_VERIFY Phase 6 — OSK + File Browser

## Prerequisites
- SC64 v2 flashcart with SD card formatted FAT32
- IS-Viewer session running (`sc64deployer debug --isv 0x03FF0000`)
- Latest practice ROM built and uploaded (`./tools/sc64dev`)

## T1 — SD detection
Boot ROM. IS-Viewer should show:
```
[iodev] cart=1 sd_init=0
```
`cart=1` confirms SC64 detected. `sd_init=0` confirms SD init succeeded.

## T2 — OSK opens
1. Launch any level via practice menu.
2. Open practice menu (hold L+R).
3. Press Z (without B). OSK overlay should appear.
4. Verify: prompt "SD SAVE NAME:" visible, char grid rendered.

## T3 — OSK typing
1. With OSK open: D-pad to navigate to a character, press A. Text field updates.
2. Navigate to several more characters and press A each time.
3. Press B. Last character deleted (backspace works).
4. Type "CORNERIA" by navigating to each character.
5. Press START. HUD shows "SD SAVE OK" or "SD SAVE FAIL" (fail is expected until Phase 7 lands).

## T4 — File browser opens
1. Open practice menu. Press Z+B.
2. File browser overlay appears listing .SF64ST files in /sf64-practice/states/.
3. D-pad up/down moves cursor through the list.
4. Press B or Z — browser closes, HUD shows "SD CANCEL".

## T5 — Load from file browser
1. Save a state via T3 above.
2. Open file browser (Z+B in menu).
3. Navigate to the file, press A.
4. HUD shows "SD LOAD OK" (after Phase 7 lands) or "SD LOAD FAIL" (stub, expected before Phase 7).

## T6 — No SD cart (emulator)
1. Run ROM in BizHawk (no SD). Open menu, press Z.
2. HUD shows "NO SD CART" — no crash.

## PASS criteria
- T1: IS-Viewer shows cart=1, sd_init=0
- T2: OSK overlay renders with prompt and char grid
- T3: Typing works, backspace works, START triggers save attempt
- T4: File browser opens and closes cleanly
- T6: Graceful degradation on emulator/no-cart
- No crashes in any test scenario
