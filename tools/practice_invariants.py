#!/usr/bin/env python3
"""Static invariant checks for the practice ROM.

Catches regressions that compile fine but break features at runtime.
Run as part of pre-commit hook.
"""
import os
import re
import sys

SRC_PRACTICE = "src/practice"
INCLUDE_PRACTICE = "include/practice.h"
PATCH_SCRIPT = "tools/patch_linker_script.py"
PRACTICE_SAVE_TAGS = os.path.join("src", "practice", "practice_save_tags.h")
PRACTICE_SAVE_C = os.path.join("src", "practice", "practice_save.c")
PRACTICE_SAVE_SLOTPOOL = os.path.join("src", "practice", "practice_save_slotpool.c")
PRACTICE_SAVE_CONFIG = os.path.join("src", "practice", "practice_save_config.h")
PRACTICE_OVERLAY_C = os.path.join("src", "practice", "practice_overlay.c")
PRACTICE_MAIN_INIT = os.path.join("src", "practice", "practice_main.c")
PRACTICE_LEVEL = os.path.join("src", "practice", "practice_level.c")
FOX_GAME = "src/engine/fox_game.c"
FOX_PLAY = "src/engine/fox_play.c"
FOX_DISPLAY = "src/engine/fox_display.c"
FOX_HUD = "src/engine/fox_hud.c"
MAKEFILE = "Makefile"
PATCHER_PACKAGE = "tools/patcher/package.json"
PATCHER_CREATE_RELEASE = "tools/patcher/src/create-release.ts"

errors = []
warnings = []

def error(msg):
    errors.append(msg)

def warning(msg):
    warnings.append(msg)

def read(path):
    with open(path) as f:
        return f.read()

def find_c_function(src, name):
    """Return a C function's full text using brace matching, or None."""
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", src, re.DOTALL)
    if not match:
        return None

    depth = 0
    for index in range(match.end() - 1, len(src)):
        if src[index] == "{":
            depth += 1
        elif src[index] == "}":
            depth -= 1
            if depth == 0:
                return src[match.start():index + 1]
    return None

def check_config_inits():
    """Every PracticeConfig field must have a default in Practice_Init."""
    header = read(INCLUDE_PRACTICE)
    main = read(os.path.join(SRC_PRACTICE, "practice_main.c"))

    struct_match = re.search(
        r"typedef struct PracticeConfig\s*\{(.*?)\}\s*PracticeConfig;",
        header, re.DOTALL
    )
    if not struct_match:
        error("Could not find PracticeConfig struct in practice.h")
        return

    fields = re.findall(r"\b(\w+)\s*;", struct_match.group(1))
    for field in fields:
        pattern = rf"gPracticeConfig\.{field}\s*="
        if not re.search(pattern, main):
            error(f"PracticeConfig.{field} declared but not initialized in Practice_Init")

def check_function_definitions():
    """Every function declared in practice.h must be defined in src/practice/."""
    header = read(INCLUDE_PRACTICE)
    decls = re.findall(r"^\w[\w\s\*]*\s+(Practice_\w+)\s*\(", header, re.MULTILINE)

    all_practice_src = ""
    for fname in os.listdir(SRC_PRACTICE):
        if fname.endswith(".c"):
            all_practice_src += read(os.path.join(SRC_PRACTICE, fname))

    for func in decls:
        pattern = rf"^[\w\s\*]+\b{func}\s*\("
        if not re.search(pattern, all_practice_src, re.MULTILINE):
            error(f"{func}() declared in practice.h but not defined in src/practice/")

def check_source_in_build():
    """Every .c file in src/practice/ must be in PRACTICE_OBJS."""
    patch_script = read(PATCH_SCRIPT)

    for fname in os.listdir(SRC_PRACTICE):
        if not fname.endswith(".c"):
            continue
        obj_name = fname.replace(".c", "")
        if f'"{obj_name}"' not in patch_script:
            error(f"{fname} exists in src/practice/ but '{obj_name}' not in PRACTICE_OBJS (patch_linker_script.py)")

def check_engine_hooks():
    """Critical engine hooks must exist inside PRACTICE_ROM guards."""
    hooks = [
        (FOX_GAME, "Practice_Init", "Practice_Init() must be called from fox_game.c"),
        (FOX_GAME, "Practice_Update", "Practice_Update() must be called from fox_game.c"),
        (FOX_GAME, "Practice_Draw", "Practice_Draw() must be called from fox_game.c"),
        (FOX_PLAY, "Practice_ApplyStartConditions", "Practice_ApplyStartConditions() must be called from fox_play.c"),
        (FOX_PLAY, "Practice_ChargeAssist_LockOnBegin", "Practice_ChargeAssist_LockOnBegin must exist in fox_play.c Player_UpdateLockOn"),
        (FOX_PLAY, "Practice_ChargeAssist_OnChargeShotFired", "Practice_ChargeAssist_OnChargeShotFired must exist in fox_play.c charge-shot hooks"),
        (
            FOX_PLAY,
            "Practice_ChargeAssist strips A from gControllerHold mid-frame",
            "Player_Shoot must suppress spurious tap-fire after auto-charge synthetic A release",
        ),
        (PRACTICE_MAIN_INIT, "Practice_ChargeMeter_Draw", "Practice_ChargeMeter_Draw must be called from practice_main.c Practice_Draw"),
        (PRACTICE_MAIN_INIT, "Practice_Cheats_Apply", "Practice_Cheats_Apply must be called from practice_main.c Practice_Update"),
        (FOX_PLAY, "gPracticeCheckpointProgress", "gPracticeCheckpointProgress checkpoint hook must exist in fox_play.c Player_Setup"),
        (FOX_PLAY, "gLevelObjectInits[gCurrentLevel]", "checkpoint scan must use gLevelObjectInits[gCurrentLevel], not gLevelObjects (NULL on first boot)"),
        (FOX_DISPLAY, "Practice_Hitbox_Draw", "Practice_Hitbox_Draw() must be called from fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_IsActive", "Practice_FreeCam_IsActive() hook must exist in fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_GetView", "Practice_FreeCam_GetView() hook must exist in fox_display.c"),
        (FOX_HUD, "showPauseMinimap", "showPauseMinimap minimap/portrait suppression hook must exist in fox_hud.c"),
        (PRACTICE_MAIN_INIT, "gLeveLClearStatus", "gLeveLClearStatus must be written in Practice_ApplyStartConditions"),
    ]
    for filepath, symbol, msg in hooks:
        src = read(filepath)
        if symbol not in src:
            error(msg)

def check_score_stats_hooks():
    """The PracticeStats counters depend on four engine hooks. If any of
    them silently disappear during a refactor, the HUD shows zeros forever
    and score-runners think they got a perfect run when they didn't.

    1. fox_enmy.c Actor_Despawn:  kills + directHits (laser) + teamKills
    2. fox_enmy.c Actor_Move:     escapes vs crashes (split by OBJ_DYING)
    3. fox_beam.c PlayerShot_UpdateShot: csBonus (sum of shot->bonus)
    4. fox_beam.c PlayerShot_ApplyDamageToActor: directHits (CS lock-on)
    """
    enmy = read("src/engine/fox_enmy.c")
    beam = read("src/engine/fox_beam.c")

    despawn_fn = find_c_function(enmy, "Actor_Despawn")
    if despawn_fn is None:
        error("Could not locate Actor_Despawn() in fox_enmy.c for stats hook check")
    else:
        if "gPracticeStats.kills" not in despawn_fn:
            error("fox_enmy.c Actor_Despawn must increment gPracticeStats.kills on Fox kills")
        if "gPracticeStats.directHits" not in despawn_fn or "DMG_BEAM" not in despawn_fn:
            error("fox_enmy.c Actor_Despawn must increment gPracticeStats.directHits on DMG_BEAM kills")
        if "gPracticeStats.teamKills" not in despawn_fn:
            error("fox_enmy.c Actor_Despawn must increment gPracticeStats.teamKills on teammate kills")

    move_fn = find_c_function(enmy, "Actor_Move")
    if move_fn is None:
        error("Could not locate Actor_Move() in fox_enmy.c for despawn-stats hook check")
    else:
        if "gPracticeStats.escapes" not in move_fn or "gPracticeStats.crashes" not in move_fn:
            error("fox_enmy.c Actor_Move despawn must split escapes vs crashes via gPracticeStats")
        if "OBJ_DYING" not in move_fn:
            error("fox_enmy.c Actor_Move despawn must check OBJ_DYING to split crash vs escape")
        # Cull-time escape accounting must delegate shootability and chase-window
        # filtering to practice helpers. Keeping those rules out of Actor_Move prevents
        # the cull hook from growing a brittle pile of per-actor exceptions.
        if "Actor_PracticeShouldCountCullEscape(this)" not in move_fn:
            error("fox_enmy.c Actor_Move escape counter must use Actor_PracticeShouldCountCullEscape")

    shot_path_fn = find_c_function(enmy, "Actor_PracticeHasShotDamagePath")
    if shot_path_fn is None:
        error("fox_enmy.c must define Actor_PracticeHasShotDamagePath")
    else:
        if "timer_0C2 >= 1000" not in shot_path_fn:
            error("Actor_PracticeHasShotDamagePath must exclude long timer_0C2 invuln")
        if "EVID_EVENT_HANDLER" not in shot_path_fn:
            error("Actor_PracticeHasShotDamagePath must exclude EVID_EVENT_HANDLER actors")
        if "scale < 0.5f" not in shot_path_fn:
            error("Actor_PracticeHasShotDamagePath must exclude event actors below the damage scale gate")
        if "OBJ_ACTOR_ME_MOLAR_ROCK" not in shot_path_fn:
            error("Actor_PracticeHasShotDamagePath must preserve ME_MOLAR_ROCK's poly-collision shot path")
        if "info.hitbox[0]" not in shot_path_fn:
            error("Actor_PracticeHasShotDamagePath must require a real hitbox for generic actors")

    chase_fn = find_c_function(enmy, "Actor_PracticeIsScriptedChaseEscape")
    if chase_fn is None:
        error("fox_enmy.c must define Actor_PracticeIsScriptedChaseEscape")
    else:
        if "EVA_GROUP_FLAG" not in chase_fn or "EVA_TEAM_ID" not in chase_fn:
            error("Actor_PracticeIsScriptedChaseEscape must exclude chase captains "
                  "(actors with EVA_GROUP_FLAG set whose group has a teammate sibling)")
        if "LEVEL_CORNERIA" not in chase_fn or "EVID_GRANGA_FIGHTER_2" not in chase_fn:
            error("Actor_PracticeIsScriptedChaseEscape must exclude Corneria Granga chase captains")

    update_shot_fn = find_c_function(beam, "PlayerShot_UpdateShot")
    if update_shot_fn is None:
        error("Could not locate PlayerShot_UpdateShot() in fox_beam.c for csBonus hook check")
    elif "gPracticeStats.csBonus" not in update_shot_fn:
        error("fox_beam.c PlayerShot_UpdateShot must accumulate gPracticeStats.csBonus when a Fox CS explodes")

    apply_damage_fn = find_c_function(beam, "PlayerShot_ApplyDamageToActor")
    if apply_damage_fn is None:
        error("Could not locate PlayerShot_ApplyDamageToActor() in fox_beam.c for directHits hook check")
    elif "gPracticeStats.directHits" not in apply_damage_fn:
        error("fox_beam.c PlayerShot_ApplyDamageToActor must increment gPracticeStats.directHits on CS lock-on direct hits")


def check_cutscene_skip_hook():
    """gCsWasNotSkipped must be set to false in Game_SetGameState's GSTATE_PLAY case.

    This hook runs after Play_Setup() resets gCsWasNotSkipped=true but before
    Play_Init() reads it 3 frames later. Without it, intro cutscenes play even
    when skipCutscenes is enabled.
    """
    src = read(FOX_GAME)
    set_game_state = re.search(
        r"void\s+Game_SetGameState\b.*?^}",
        src, re.DOTALL | re.MULTILINE
    )
    if not set_game_state:
        error("Could not find Game_SetGameState in fox_game.c")
        return

    body = set_game_state.group(0)
    if "gCsWasNotSkipped" not in body or "skipCutscenes" not in body:
        error(
            "Game_SetGameState must set gCsWasNotSkipped=false when skipCutscenes is on. "
            "Without this, Play_Setup() resets it to true and intro cutscenes play."
        )

def check_release_patch_workflow():
    """Release patch workflow must build a compressed practice ROM and never ship ROM bytes."""
    makefile = read(MAKEFILE)
    if "practice-compressed:" not in makefile:
        error("Makefile must define practice-compressed target for release builds")
    if "practice-patch:" not in makefile:
        error("Makefile must define practice-patch target for BPS release generation")
    if "npm --prefix tools/patcher run create-release" not in makefile:
        error("practice-patch target must call tools/patcher create-release script")

    package = read(PATCHER_PACKAGE)
    if '"sf64-practice-patcher"' not in package or '"bin"' not in package:
        error("tools/patcher/package.json must expose the sf64-practice-patcher npx binary")
    if '"*.z64"' in package or '"*.n64"' in package or '"*.v64"' in package:
        error("tools/patcher/package.json must not include ROM file globs")

    create_release = read(PATCHER_CREATE_RELEASE)
    if "baserom.us.rev1.z64" not in create_release or "build/starfox64.us.rev1.z64" not in create_release:
        error("create-release defaults must patch the US rev1 base ROM to the compressed practice ROM")

def check_isviewer_sc64():
    """IS-Viewer 64 must be enabled and use the SC64 protocol, not libdragon's emulator protocol.

    SC64 firmware ignores buffers without the magic token 0x49533634 ("IS64") at offset 0,
    so libdragon's 0x12345678 magic-check silently no-ops on real hardware. Each cart-bus
    write must be followed by a dummy IO_READ to drain the SC64 write FIFO; without it
    back-to-back writes get dropped after the first few.
    """
    mods = read("include/mods.h")
    if not re.search(r"#define\s+MODS_ISVIEWER\s+1\b", mods):
        error("MODS_ISVIEWER must be 1 to enable osSyncPrintf over IS-Viewer 64")

    isv = read("src/mods/isviewer.c")
    if "0x49533634" not in isv:
        error("isviewer.c must use SC64 token 0x49533634 ('IS64') at base+0; libdragon's 0x12345678 is silently dropped on hardware")
    if "PI_WRITE" not in isv:
        error("isviewer.c must use a PI_WRITE macro that flushes via IO_READ; back-to-back writes to SC64 cart space get dropped")
    if "__osDisableInt" not in isv or "__osRestoreInt" not in isv:
        error("isviewer.c rp/wp dance must run with interrupts masked (__osDisableInt/__osRestoreInt); without it a timer/SP/DP IRQ landing mid-dance leaves the firmware polling a torn (wp<rp) pair and triggers a 64KB garbage dump to USB ('ZZZZ' storm)")

def check_iodev_sc64():
    """SC64 iodev backend must preserve hard-won SC64 protocol invariants.

    These are exactly the gotchas documented in CLAUDE.md's
    'Hard-won SC64 protocol gotchas' section, applied to iodev rather
    than IS-Viewer. Without these, the channel silently fails on hardware.
    """
    path = "lib/iodev/iodev_sc64.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # Every cart-bus write needs a follow-up IO_READ to drain the PI bus.
    # Match the macro *definition* (and require it pair IO_WRITE with IO_READ),
    # not just any reference - bare callers would also satisfy a substring check.
    if not re.search(r"#define\s+PI_WRITE_FLUSH\b[^\n]*\\\s*\n[^\n]*IO_WRITE[^\n]*\\\s*\n[^\n]*IO_READ", src):
        error(f"{path}: PI_WRITE_FLUSH macro must be defined and pair IO_WRITE with a draining IO_READ (see CLAUDE.md SC64 gotchas)")

    # The four SD command bytes must remain literal - they're the SC64 wire protocol.
    for cmd_byte, name in [("'i'", "SD_CARD_OP"), ("'I'", "SD_SECTOR_SET"), ("'s'", "SD_READ"), ("'S'", "SD_WRITE")]:
        if cmd_byte not in src:
            error(f"{path}: SC64 command byte {cmd_byte} ({name}) missing - wire protocol broken")

    # The 128-sector cap must remain in both read and write - it's the 64 KiB DMA scratch limit.
    # A future "simplify" that drops it would silently corrupt memory past the scratch buffer.
    if src.count("count > 128") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce count > 128 -> ERR_PARAM (DMA scratch is 64 KiB / 128 sectors)")

    # sc64_detect MUST issue the unlock key sequence BEFORE reading IDENT.
    # In user-ROM mode (post-handoff from SC64 bootloader) the register
    # interface is locked and IDENT returns garbage; only the SC64's own
    # bootloader can read IDENT pre-unlock. Verified on hardware 2026-04-27.
    # Reference: ~/code/gz/src/gz/sc64.c probe() does the same.
    detect_match = re.search(r"sc64_detect\s*\([^)]*\)\s*\{(.*?)^\}", src,
                             re.DOTALL | re.MULTILINE)
    if not detect_match:
        error(f"{path}: could not locate sc64_detect function body")
    else:
        body = detect_match.group(1)
        unlock_pos = body.find("SC64_KEY_UNLOCK_2")
        ident_pos  = body.find("SC64_REG_IDENT")
        if unlock_pos < 0 or ident_pos < 0:
            error(f"{path}: sc64_detect must reference SC64_KEY_UNLOCK_2 and SC64_REG_IDENT")
        elif unlock_pos > ident_pos:
            error(f"{path}: sc64_detect must write the unlock key sequence BEFORE reading SC64_REG_IDENT (in user-ROM mode the register interface is locked and IDENT returns garbage until unlocked)")

def check_slot_manager_atomic_write():
    """slot_manager_save_sd_named must use temp+rename, not direct overwrite.

    A direct overwrite (f_open with FA_CREATE_ALWAYS at the destination
    path) leaves a half-written file behind on power loss / ROM crash --
    the user loses their prior save and gets nothing in return. The
    atomic temp+rename pattern keeps the prior save intact until the
    new one is fully written and closed.

    The matching unit test is test_slot_manager_sd.c::test_atomic_write_
    produces_final_file; that test verifies the post-condition (no tmp
    leftover, final file present). This invariant guards the call shape
    so the test stays honest. """
    path = "lib/slot_manager.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # The atomic path constructs <path>.tmp then renames it.
    if "f_rename" not in src:
        error(f"{path}: slot_manager_save_sd_named must use f_rename for atomic writes")
    if '".tmp"' not in src and "'.tmp'" not in src and ".tmp" not in src:
        error(f"{path}: slot_manager_save_sd_named must use a .tmp suffix for atomic writes")


def check_sd_host():
    """Generic SD-protocol engine (lib/sd_host) must keep its public shape.

    Wave 1 of the gz-mirror introduces lib/sd_host as the shared SD
    protocol state machine driven by ED64-X (and, in later waves, V1/V2
    SPI backends). The contract surface lives in two headers:
      - sd_host.h: function-pointer table (sd_host_t) + 5 public funcs.
      - sd_proto.h: SD-spec CMD numbers and response sizes.

    These checks guard against accidental API regressions that would
    silently break either the ED64 backend or the future V1/V2 backends.
    """
    h_path = "lib/sd_host/sd_host.h"
    c_path = "lib/sd_host/sd_host.c"
    p_path = "lib/sd_host/sd_proto.h"

    # If the gz-mirror waves haven't landed yet, skip silently.
    if not (os.path.isfile(h_path) and os.path.isfile(c_path)):
        return

    h = read(h_path)
    c = read(c_path)

    # Public functions every backend (and the diskio glue) depends on.
    for fn in ("sd_host_init", "sd_host_read_blocks", "sd_host_write_blocks",
               "sd_host_send_cmd_r1", "sd_host_send_cmd_r1b"):
        if fn not in h:
            error(f"{h_path}: missing public function declaration: {fn}")
        # The implementation must define them too.
        if fn not in c:
            error(f"{c_path}: missing implementation: {fn}")

    # The function-pointer table must expose the SDIO primitives the ED64
    # backend wires up. Removing or renaming any of these would silently
    # break iodev_ed64.c at compile time, but this catches subtler reorders
    # in the typedef block (IDO C89 cares about positional struct init).
    for ptr in ("sdio_cmd_tx_byte", "sdio_cmd_rx_byte", "sdio_cmd_rx_bit",
                "sdio_dat_tx_word", "sdio_dat_rx_word", "sdio_dat_idle_clks",
                "set_spd", "rx_mblk", "tx_mblk"):
        if ptr not in h:
            error(f"{h_path}: sd_host_t missing function pointer: {ptr}")

    # SPI hooks are reserved for Wave 4 -- the slots must be present in
    # the struct now so V2/V1 backends can land without a header revisit.
    for ptr in ("spi_io", "spi_tx_buf", "spi_rx_buf"):
        if ptr not in h:
            error(f"{h_path}: sd_host_t missing SPI slot reserved for Wave 4: {ptr}")

    # sd_proto.h must define every CMD/ACMD that sd_host actually uses.
    if os.path.isfile(p_path):
        p = read(p_path)
        for sym in ("SDP_CMD0", "SDP_CMD2", "SDP_CMD3", "SDP_CMD7", "SDP_CMD8",
                    "SDP_CMD12", "SDP_CMD16", "SDP_CMD17", "SDP_CMD18",
                    "SDP_CMD24", "SDP_CMD55", "SDP_ACMD6", "SDP_ACMD41",
                    "SDP_OCR_BUSY_DONE", "SDP_OCR_CCS_IS_HC",
                    "SDP_DATA_RESP_MASK", "SDP_DATA_RESP_ACCEPTED"):
            if sym not in p:
                error(f"{p_path}: missing SD-protocol constant: {sym}")

    # sd_host.c must reuse lib/sd_crc.c rather than carry its own CRC7.
    # If a future change inlines a hand-rolled CRC here, that's drift we
    # want surfaced (fixes belong in sd_crc.c so all callers benefit).
    if "sd_crc7(" not in c:
        error(f"{c_path}: must reuse sd_crc7() from lib/sd_crc.c (do not inline CRC7)")


def check_iodev_ed64():
    """ED64 X iodev backend must preserve cart + SDIO protocol invariants.

    The ED64-X SD path is native 4-bit SDIO (NOT SPI) driven through the
    FPGA registers. After the gz-mirror Wave 2 refactor most protocol
    code lives in lib/sd_host/sd_host.c; this backend supplies the
    register I/O primitives, detection, and the FPGA-DMA fast read path.

    The invariants below guard patterns that, if removed, would silently
    break SD I/O on real EverDrive X hardware.
    """
    path = "lib/iodev/iodev_ed64.c"
    if not os.path.isfile(path):
        return
    src = read(path)

    # Cart-bus writes must use PI_WRITE_FLUSH (same gotcha as SC64/IS-Viewer).
    # Match the macro definition, not just any reference.
    if not re.search(r"#define\s+PI_WRITE_FLUSH\b[^\n]*\\\s*\n[^\n]*IO_WRITE[^\n]*\\\s*\n[^\n]*IO_READ", src):
        error(f"{path}: PI_WRITE_FLUSH macro must be defined and pair IO_WRITE with a draining IO_READ")

    # Cart-unlock magic. The Krikzz X7/X8 hardware spec uses a single
    # 0xAA55 write to open the FPGA register window.
    if not re.search(r"#define\s+\w+\s+0xAA55(?:[uU]?[lL]{0,2})?(?!\w)", src):
        error(f"{path}: must define the cart-unlock magic 0xAA55 (#define <NAME> 0xAA55[u])")

    # 128-sector cap matches SC64; consistent caller contract.
    if src.count("count > 128") < 2 and "ED64_SD_MAX_SECTORS" not in src:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce count > 128 -> ERR_PARAM")

    # 8-byte buffer alignment check (matches iodev.h public contract).
    if src.count("& 7u") < 2:
        error(f"{path}: both sd_read_sectors and sd_write_sectors must enforce 8-byte buffer alignment")

    # Detection magic. Removing this would make the backend match every
    # cart on the bus.
    if "0xED64" not in src:
        error(f"{path}: detection must check REG_EDID for 0xED64 magic")

    # SDIO init: CMD8 echo check (0x01AA) distinguishes SDHC from SD v1/MMC.
    # Until Wave 2 lifts CMD8 into sd_host.c, the literal must appear here;
    # after Wave 2 the literal moves to lib/sd_host/sd_proto.h which has
    # its own check (check_sd_host_proto).
    if "0x01AA" not in src and "SD_CMD8_ARG" not in src:
        # Tolerate: post-Wave-2 the constant lives in sd_proto.h. The
        # sd_host check below catches that case.
        if not os.path.isfile("lib/sd_host/sd_proto.h"):
            error(f"{path}: ed64_sd_init must check CMD8 echo for SDHC detection (0x01AA)")

    # FPGA-DMA fast read: ED64-X reads sectors via REG_DMA_ADDR + REG_DMA_LEN
    # into a cart-bus staging area before doing a PI DMA into RDRAM. Without
    # this fast path, reads fall back to byte-by-byte DAT bus I/O which is
    # ~256x slower per sector.
    if "REG_DMA_ADDR" not in src and "ED64_REG_DMA_ADDR" not in src:
        error(f"{path}: read path must use FPGA DMA (REG_DMA_ADDR/REG_DMA_LEN), not byte-by-byte DAT reads")


def check_spawn_zone_typing():
    """Spawn zone draw loop must classify entries by type and respect per-type toggles."""
    hitbox = read("src/practice/practice_hitbox.c")
    for sym in ("OBJ_ACTOR_START", "OBJ_ACTOR_MAX", "OBJ_BOSS_START", "OBJ_BOSS_MAX",
                "OBJ_ITEM_START", "OBJ_ITEM_MAX", "OBJ_SCENERY_START", "OBJ_SCENERY_MAX"):
        if sym not in hitbox:
            error(f"Hitbox_DrawSpawnZones must use {sym} for type classification")
    for field in ("showSpawnActors", "showSpawnItems", "showSpawnScenery"):
        if field not in hitbox:
            error(f"Hitbox_DrawSpawnZones must respect gPracticeConfig.{field}")


def check_level_select_bgm_ready_gate():
    """Level-select BGM preview must not stack audio heap resets.

    Audio_SetAudioSpec queues SEQCMD_RESET_AUDIO_HEAP. Audio_Update skips
    queued seq commands while Audio_HandleReset() is not READY, so queuing a
    second reset from fast L/R input can wedge BGM on hardware.
    """
    src = read(PRACTICE_LEVEL)

    fn_match = re.search(
        r"static\s+void\s+Practice_ServiceLevelSelectBgm\s*\([^)]*\)\s*\{(.*?)^}",
        src, re.DOTALL | re.MULTILINE,
    )
    if not fn_match:
        error(f"{PRACTICE_LEVEL}: Practice_ServiceLevelSelectBgm missing")
        return

    body = fn_match.group(1)
    if "Audio_HandleReset() != 0" not in body:
        error(
            f"{PRACTICE_LEVEL}: Practice_ServiceLevelSelectBgm must gate preview "
            "audio on Audio_HandleReset() == 0"
        )
    if "sBgmPlayPending" not in body:
        error(
            f"{PRACTICE_LEVEL}: Practice_ServiceLevelSelectBgm must defer "
            "AUDIO_PLAY_BGM after cross-spec AUDIO_SET_SPEC"
        )
    if "gGameFrameCount" in body or "osGetTime" in body:
        error(
            f"{PRACTICE_LEVEL}: level-select BGM preview must use Audio_HandleReset, "
            "not frame/time throttles"
        )
    if "AUDIO_SET_SPEC" in body:
        first_gate = body.find("Audio_HandleReset() != 0")
        first_set = body.find("AUDIO_SET_SPEC")
        if first_gate < 0 or first_gate > first_set:
            error(
                f"{PRACTICE_LEVEL}: first level-select BGM AUDIO_SET_SPEC must be "
                "preceded by Audio_HandleReset()"
            )
    if not re.search(r"if\s*\([^)]*L_TRIG[^)]*\)\s*\{.*?\}\s*else\s+if\s*\([^)]*R_TRIG", src, re.DOTALL):
        error(
            f"{PRACTICE_LEVEL}: L/R BGM input must use else-if so same-frame "
            "shoulder presses apply once"
        )

    update_match = re.search(
        r"void\s+Practice_LevelSelect_Update\s*\([^)]*\)\s*\{(.*?)^}",
        src, re.DOTALL | re.MULTILINE,
    )
    if update_match:
        update_body = update_match.group(1)
        launch_pos = update_body.find("Practice_LaunchLevel")
        service_after_launch = update_body.find("Practice_ServiceLevelSelectBgm", launch_pos)
        return_after_launch = update_body.find("return", launch_pos, service_after_launch)
        if launch_pos >= 0 and service_after_launch >= 0 and return_after_launch < 0:
            error(
                f"{PRACTICE_LEVEL}: Practice_LevelSelect_Update must return after "
                "Practice_LaunchLevel so launch audio is not followed by preview audio"
            )

LIB_DIR = "lib"

# Headers/paths that lib/ code must NOT include (it must stay portable).
FORBIDDEN_LIB_INCLUDES = [
    "global.h",
    "practice.h",
    "variables.h",
    # Family patterns checked separately below.
]
FORBIDDEN_LIB_INCLUDE_PATTERNS = [
    r"sf64\w*\.h",   # sf64audio.h, sf64level.h, sf64thread.h, etc.
    r"fox_\w*\.h",   # fox_game.h, fox_play.h, etc.
    r"include/[\w./_-]+",  # any include with explicit include/ path prefix
]

def check_lib_isolation():
    """lib/ code must not include game/decomp headers."""
    if not os.path.isdir(LIB_DIR):
        return  # lib/ doesn't exist yet — nothing to check
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            src = read(path)
            for inc in FORBIDDEN_LIB_INCLUDES:
                if re.search(rf'#include\s*[<"]{re.escape(inc)}[>"]', src):
                    error(f"{path}: lib/ may not include game header '{inc}'")
            for pat in FORBIDDEN_LIB_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: lib/ may not include game header matching /{pat}/")

# Files allowed to include libultra headers (PI/cart-bus access).
LIBULTRA_ALLOWED = [
    "lib/iodev/iodev_sc64.c",
    "lib/iodev/iodev_ed64.c",     # X7/X8, Phase 1b
    "lib/iodev/iodev_ed64_v2.c",  # V2/V2.5, gz-mirror Wave 4
    "lib/iodev/iodev_ed64_v1.c",  # V1, gz-mirror Wave 5
    "lib/lib_types.h",         # toolchain shim — bridges <stdint.h> ↔ PR/ultratypes.h
]
LIBULTRA_INCLUDE_PATTERNS = [
    r"PR/[\w/]+\.h",
    r"ultra64\.h",
    r"libultra/[\w/]+\.h",
]

def check_lib_libultra_scope():
    """lib/ files outside the iodev backends must build host-portable.

    Forbid libultra includes everywhere except the explicit allowlist,
    so unit tests can build with native gcc.
    """
    if not os.path.isdir(LIB_DIR):
        return
    for root, _dirs, files in os.walk(LIB_DIR):
        for fname in files:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fname)
            if path in LIBULTRA_ALLOWED:
                continue
            src = read(path)
            for pat in LIBULTRA_INCLUDE_PATTERNS:
                if re.search(rf'#include\s*[<"]{pat}[>"]', src):
                    error(f"{path}: only iodev backends and lib_types.h may include libultra (matched /{pat}/)")

def check_overlay_table_complete():
    """Every LevelId enum value must appear in EITHER sLevelOverlayMap or
    sLevelExclusionList in src/practice/practice_overlay.c. If a future
    LevelId is added to include/sf64level.h and neither table is updated,
    drift becomes silent — practice_overlay_is_saveable() would return
    false for the new ID without anyone noticing. The invariant catches
    that drift at build time."""
    level_h = read("include/sf64level.h")
    overlay_path = os.path.join(SRC_PRACTICE, "practice_overlay.c")
    if not os.path.isfile(overlay_path):
        error(f"{overlay_path} missing — practice_overlay.c is required for Phase 4")
        return
    overlay_c = read(overlay_path)

    enum_match = re.search(
        r"typedef enum LevelId\s*\{(.*?)\}\s*LevelId;",
        level_h, re.DOTALL,
    )
    if not enum_match:
        enum_match = re.search(
            r"enum LevelId\s*\{(.*?)\}",
            level_h, re.DOTALL,
        )
    if not enum_match:
        error("Could not find LevelId enum in include/sf64level.h "
              "(check_overlay_table_complete)")
        return

    enum_body = enum_match.group(1)
    raw_levels = re.findall(r"\b(LEVEL_\w+)\b", enum_body)
    levels = []
    seen = set()
    for name in raw_levels:
        if name == "LevelId":
            continue
        if name.endswith("_MAX"):
            continue
        if name in seen:
            continue
        seen.add(name)
        levels.append(name)

    map_match = re.search(
        r"sLevelOverlayMap\s*\[\s*\]\s*=\s*\{(.*?)\};",
        overlay_c, re.DOTALL,
    )
    excl_match = re.search(
        r"sLevelExclusionList\s*\[\s*\]\s*=\s*\{(.*?)\};",
        overlay_c, re.DOTALL,
    )
    if not map_match:
        error("Could not locate sLevelOverlayMap[] in practice_overlay.c "
              "(check_overlay_table_complete)")
        return
    if not excl_match:
        error("Could not locate sLevelExclusionList[] in practice_overlay.c "
              "(check_overlay_table_complete)")
        return

    map_body = map_match.group(1)
    excl_body = excl_match.group(1)

    for level in levels:
        in_map = re.search(rf"\b{re.escape(level)}\b", map_body) is not None
        in_excl = re.search(rf"\b{re.escape(level)}\b", excl_body) is not None
        if not (in_map or in_excl):
            error(f"{level} not in sLevelOverlayMap or sLevelExclusionList "
                  f"in practice_overlay.c (check_overlay_table_complete)")

def check_fatfs_isolation():
    """FatFs source (vendored) must not include any project headers beyond its
    own and iodev's. Vendored FatFs is meant to be drop-in replaceable on
    upstream updates; coupling it to game-specific headers would block updates.

    Allowlist (relative names; checked against the literal string between
    `#include "..."` or `<...>`):
      - ff.h, ffconf.h, diskio.h, string.h  -- FatFs-internal + our shim
      - iodev/iodev.h                       -- the only legitimate cross-lib
                                                 dependency, used by diskio.c
      - any standard C header               -- <something.h> with simple name
    """
    fatfs_dir = os.path.join(LIB_DIR, "fatfs")
    if not os.path.isdir(fatfs_dir):
        return

    allowed_includes = {"ff.h", "ffconf.h", "diskio.h", "string.h",
                        "iodev/iodev.h", "libc/stddef.h"}
    # Standard C: a single lowercase name with .h extension.
    std_c_re = re.compile(r"^[a-z][a-z0-9_]*\.h$")

    for fname in sorted(os.listdir(fatfs_dir)):
        if not fname.endswith((".c", ".h")):
            continue
        path = os.path.join(fatfs_dir, fname)
        src = read(path)
        for m in re.finditer(r'#include\s+["<]([^">]+)[">]', src):
            inc = m.group(1)
            if inc in allowed_includes:
                continue
            if std_c_re.match(inc):
                continue
            error(f"{path}: forbidden include '{inc}' in vendored FatFs (only ff.h/ffconf.h/diskio.h/string.h/iodev/iodev.h/libc/stddef.h + std C allowed)")

def check_tag_registry():
    """Every TAG_* numeric value in practice_save_tags.h must be unique."""
    if not os.path.isfile(PRACTICE_SAVE_TAGS):
        return
    src = read(PRACTICE_SAVE_TAGS)
    matches = re.findall(r"\b(TAG_\w+)\s*=\s*(0x[0-9a-fA-F]+)", src)
    if not matches:
        error(f"{PRACTICE_SAVE_TAGS}: could not parse TAG_* = 0x... entries "
              f"(check_tag_registry)")
        return

    vals = []
    for name, hexv in matches:
        vals.append(int(hexv, 16))

    dupes = sorted({v for v in vals if vals.count(v) > 1})
    if dupes:
        error(f"{PRACTICE_SAVE_TAGS}: duplicate tag values "
              f"{[hex(v) for v in dupes]} (check_tag_registry)")

def check_serializer_parity():
    """Every TLV tag must appear in Practice_Save_Cb (emit) and Practice_Load_Cb (apply)."""
    if not os.path.isfile(PRACTICE_SAVE_TAGS):
        return

    hdr = read(PRACTICE_SAVE_TAGS)
    tag_names = re.findall(r"\b(TAG_\w+)\s*=\s*0x", hdr)
    if not tag_names:
        error(f"{PRACTICE_SAVE_TAGS}: could not enumerate TAG_* (check_serializer_parity)")
        return

    sb = read(PRACTICE_SAVE_C)
    save_impl = (
        "static uint32_t Practice_Save_Cb(void *buf, uint32_t buf_size) {"
    )
    load_impl = "static int Practice_Load_Cb(const void *buf, uint32_t size) {"

    cb_start = sb.find(save_impl)
    cb_end = sb.find(load_impl)

    emit = sb[cb_start:cb_end] if cb_start >= 0 and cb_end > cb_start else ""

    lb_start = cb_end if cb_end >= 0 else sb.find(load_impl)
    next_fn = sb.find("\n#ifdef", lb_start + 10) if lb_start >= 0 else -1
    if lb_start < 0:
        apply = ""
    elif next_fn > lb_start:
        apply = sb[lb_start:next_fn]
    else:
        brace = sb.find("\n}", lb_start + 800)
        apply = sb[lb_start:brace + 2] if brace > lb_start else sb[lb_start:]

    for tag in sorted(set(tag_names)):
        if tag not in emit:
            error(f"{PRACTICE_SAVE_C}: TLV {tag} must appear in Practice_Save_Cb "
                  f"(check_serializer_parity)")
        if f"case {tag}:" not in apply:
            error(f"{PRACTICE_SAVE_C}: TLV {tag} missing `case {tag}:` in "
                  f"Practice_Load_Cb (check_serializer_parity)")

def check_state_version_defined_once():
    """STATE_VERSION / MAX_STATE_SIZE must stay single-definition in practice_save_config.h."""
    cfg = PRACTICE_SAVE_CONFIG
    if not os.path.isfile(cfg):
        return

    proj = ""
    skip_dirs = frozenset({
        ".git", ".claude", "build", "node_modules",
        "asm", "bin", "baserom", "deps", "venv", ".venv",
        "torch", ".splat_cache",
    })
    exclude_cfg = lambda p: (
        os.path.normpath(os.path.abspath(p)).endswith(cfg.replace("/", os.sep))
    )

    for walk_root, dirs, filenames in os.walk("."):
        dirs[:] = sorted(d for d in dirs if d not in skip_dirs)
        depth = os.path.relpath(walk_root).count(os.sep)
        if depth > 14:
            dirs[:] = []

        for fname in filenames:
            if not fname.endswith((".c", ".h")):
                continue
            path = os.path.join(walk_root, fname)
            if exclude_cfg(path):
                continue
            try:
                proj += read(path) + "\n"
            except OSError:
                continue

    if "#define STATE_VERSION " in proj:
        error("#define STATE_VERSION must appear only in "
              f"{cfg}, not elsewhere (check_state_version_defined_once)")
    if "#define MAX_STATE_SIZE " in proj:
        error("#define MAX_STATE_SIZE must appear only in "
              f"{cfg}, not elsewhere (check_state_version_defined_once)")

    body = read(cfg)
    if "#define STATE_VERSION" not in body:
        error(f"{cfg}: STATE_VERSION macro missing (check_state_version_defined_once)")
    if "#define MAX_STATE_SIZE" not in body:
        error(f"{cfg}: MAX_STATE_SIZE macro missing (check_state_version_defined_once)")

def check_max_state_size_budget():
    """RAM-slot footprint must satisfy documented Expansion Pak budgeting."""
    cfg = PRACTICE_SAVE_CONFIG
    if not os.path.isfile(cfg):
        return

    txt = read(cfg)
    m_mx = re.search(r"#define\s+MAX_STATE_SIZE\s+(.+)", txt)
    m_rn = re.search(r"#define\s+MAX_RAM_SLOTS_NO_PAK\s+(.+)", txt)
    m_rp = re.search(r"#define\s+MAX_RAM_SLOTS_WITH_PAK\s+(.+)", txt)

    mx = None
    if m_mx:
        raw = re.sub(r"/\*.*?\*/", "", m_mx.group(1)).strip()
        mx = int(raw, 0)

    nopak_ceiling_n = None
    withpak_ceiling_n = None
    if m_rn:
        nopak_ceiling_n = int(m_rn.group(1).strip(), 0)
    if m_rp:
        withpak_ceiling_n = int(m_rp.group(1).strip(), 0)

    if mx is None or nopak_ceiling_n is None or withpak_ceiling_n is None:
        error(f"{cfg}: MAX_STATE_SIZE / MAX_RAM_SLOTS_* not parseable "
              f"(check_max_state_size_budget)")
        return

    nopak_budget = 1048576
    withpak_budget = 2621440

    if mx * nopak_ceiling_n > nopak_budget:
        error(f"{cfg}: worst-case NOPAK footprint {mx * nopak_ceiling_n} "
              f"(check_max_state_size_budget)")
    if mx * withpak_ceiling_n > withpak_budget:
        error(f"{cfg}: worst-case WITH_PAK footprint {mx * withpak_ceiling_n} "
              f"(check_max_state_size_budget)")

SYS_MEMORY = os.path.join("src", "sys", "sys_memory.c")


def check_radial_menu_save_allowed():
    """Radial-menu save runs while PMENU_OPEN_FROZEN, so CanSaveHere must not reject menus."""
    src = read(PRACTICE_SAVE_C)
    m = re.search(r"s32\s+Practice_CanSaveHere\s*\([^)]*\)\s*\{(.*?)^\}", src,
                  re.DOTALL | re.MULTILINE)
    if not m:
        error(
            f"{PRACTICE_SAVE_C}: Practice_CanSaveHere missing "
            "(check_radial_menu_save_allowed)"
        )
        return

    body = m.group(1)
    if "gPracticeMenuState" in body or "PMENU_" in body:
        error(
            f"{PRACTICE_SAVE_C}: Practice_CanSaveHere must not reject solely on "
            "gPracticeMenuState; radial save is intentionally allowed while "
            "PMENU_OPEN_FROZEN (check_radial_menu_save_allowed)"
        )


def check_snapshot_gplayers_use_cam_count():
    """gPlayer is MEM_ARRAY_ALLOCATE(Player, gCamCount)-sized — do not iterate 0..3 blindly."""
    src = read(PRACTICE_SAVE_C)

    fi = src.find("static void Snapshot_FillFromGame")
    aj = src.find("static void Snapshot_ApplyToGame")
    if fi < 0 or aj < 0:
        error(
            f"{PRACTICE_SAVE_C}: Snapshot_FillFromGame/Snapshot_ApplyToGame missing "
            "(check_snapshot_gplayers_use_cam_count)"
        )
        return

    fill_block = src[fi : fi + 900]
    apply_block = src[aj : aj + 500]
    if "gCamCount" not in fill_block or "gCamCount" not in apply_block:
        error(
            f"{PRACTICE_SAVE_C}: Snapshot_Fill/Apply must use gCamCount when copying Player[] "
            "(check_snapshot_gplayers_use_cam_count)"
        )


def check_overlay_build_id_no_rom_read():
    """Hardware save path must not dereference ROM/physical DMA table addresses."""
    src = read(PRACTICE_OVERLAY_C)
    m = re.search(r"u32\s+practice_overlay_build_id\s*\([^)]*\)\s*\{(.*?)^}",
                  src, re.DOTALL | re.MULTILINE)
    if not m:
        error(
            f"{PRACTICE_OVERLAY_C}: practice_overlay_build_id missing "
            "(check_overlay_build_id_no_rom_read)"
        )
        return

    body = m.group(1)
    scrubbed = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    scrubbed = re.sub(r"//.*", "", scrubbed)
    if re.search(r"(?:->|\.)\s*vRomAddress\b", scrubbed):
        error(
            f"{PRACTICE_OVERLAY_C}: practice_overlay_build_id must not read/hash "
            "DmaEntry.vRomAddress on hardware; use metadata/RDRAM only "
            "(check_overlay_build_id_no_rom_read)"
        )


def check_phase5_state_machine_lifecycle():
    """Phase 5: cross-scene state machine in practice_save.c is wired and
    polled. Three structural pieces:
      1. practice_save.c declares the state machine fields and the Tick.
      2. practice.h exposes Practice_Save_Tick as a public symbol.
      3. practice_main.c calls Practice_Save_Tick from Practice_Update so
         in-flight transitions actually advance.
    """
    save_src = read(PRACTICE_SAVE_C)
    for needle in (
        "gPracticeCrossLoadState",
        "gPracticeCrossLoadSlot",
        "gPracticeCrossLoadStartFrame",
        "gPracticeSlotMeta",
        "PRACTICE_XLOAD_TIMEOUT_FRAMES",
        "void Practice_Save_Tick(",
    ):
        if needle not in save_src:
            error(
                f"{PRACTICE_SAVE_C}: missing `{needle}` "
                "(check_phase5_state_machine_lifecycle)"
            )

    header = read(INCLUDE_PRACTICE)
    if "void Practice_Save_Tick(void)" not in header:
        error(
            f"{INCLUDE_PRACTICE}: Practice_Save_Tick declaration missing "
            "(check_phase5_state_machine_lifecycle)"
        )

    main_src = read(PRACTICE_MAIN_INIT)
    update_match = re.search(
        r"void\s+Practice_Update\s*\(\s*void\s*\)\s*\{(.*?)\n\}",
        main_src, re.DOTALL,
    )
    if not update_match:
        error(
            f"{PRACTICE_MAIN_INIT}: Practice_Update body not found "
            "(check_phase5_state_machine_lifecycle)"
        )
        return
    if "Practice_Save_Tick(" not in update_match.group(1):
        error(
            f"{PRACTICE_MAIN_INIT}: Practice_Update must call Practice_Save_Tick "
            "(check_phase5_state_machine_lifecycle)"
        )


def check_audio_spec_for_level_single_source():
    """Phase 5: Practice_AudioSpecForLevel is the single source of truth for
    LevelId -> packed (sfxLayout << 8) | spec dispatch. Defined exactly once
    in practice_level.c, used by Practice_LaunchLevel (same file) and by
    practice_save.c. The literal AUDIO_SET_SPEC table must NOT appear in
    practice_save.c (where the previous static helper lived) so the table
    cannot drift between save-side and launch-side.
    """
    level_src = read(PRACTICE_LEVEL)
    if "u16 Practice_AudioSpecForLevel(" not in level_src:
        error(
            f"{PRACTICE_LEVEL}: Practice_AudioSpecForLevel definition missing "
            "(check_audio_spec_for_level_single_source)"
        )
    if "Practice_AudioSpecForLevel(levelId)" not in level_src and \
       "Practice_AudioSpecForLevel(gCurrentLevel)" not in level_src:
        error(
            f"{PRACTICE_LEVEL}: Practice_LaunchLevel must call "
            "Practice_AudioSpecForLevel (check_audio_spec_for_level_single_source)"
        )

    save_src = read(PRACTICE_SAVE_C)
    # Save side must call the helper, not maintain its own table.
    if "Practice_AudioSpecForLevel(" not in save_src:
        error(
            f"{PRACTICE_SAVE_C}: must call Practice_AudioSpecForLevel for the "
            "audio spec TLV (check_audio_spec_for_level_single_source)"
        )
    # The old static helper name must not return.
    if re.search(r"\bPractice_AudioSpecPacked\b", save_src):
        error(
            f"{PRACTICE_SAVE_C}: leftover Practice_AudioSpecPacked reference; "
            "use Practice_AudioSpecForLevel "
            "(check_audio_spec_for_level_single_source)"
        )
    # AUDIO_SET_SPEC table must not be reintroduced in practice_save.c.
    if re.search(r"\bAUDIO_SET_SPEC\s*\(", save_src):
        error(
            f"{PRACTICE_SAVE_C}: AUDIO_SET_SPEC must not appear here; route "
            "through Practice_AudioSpecForLevel "
            "(check_audio_spec_for_level_single_source)"
        )


def check_overlay_build_id_eager_init():
    """Phase 5: practice_overlay_prime_build_ids must exist and be called from
    Practice_Init AFTER Practice_Save_Init. Cross-scene loads compare a saved
    overlay's build id against the current ROM's overlay before the source
    overlay is necessarily resident, so the cache must be populated at boot.
    """
    overlay_src = read(PRACTICE_OVERLAY_C)
    if "void practice_overlay_prime_build_ids" not in overlay_src:
        error(
            f"{PRACTICE_OVERLAY_C}: practice_overlay_prime_build_ids missing "
            "(check_overlay_build_id_eager_init)"
        )

    main_src = read(PRACTICE_MAIN_INIT)
    init_match = re.search(
        r"void\s+Practice_Init\s*\(\s*void\s*\)\s*\{(.*?)\n\}",
        main_src, re.DOTALL,
    )
    if not init_match:
        error(
            f"{PRACTICE_MAIN_INIT}: Practice_Init body not found "
            "(check_overlay_build_id_eager_init)"
        )
        return

    body = init_match.group(1)
    save_init_pos = body.find("Practice_Save_Init(")
    prime_pos = body.find("practice_overlay_prime_build_ids(")
    if save_init_pos < 0:
        error(
            f"{PRACTICE_MAIN_INIT}: Practice_Save_Init not called from Practice_Init "
            "(check_overlay_build_id_eager_init)"
        )
        return
    if prime_pos < 0:
        error(
            f"{PRACTICE_MAIN_INIT}: practice_overlay_prime_build_ids not called from "
            "Practice_Init (check_overlay_build_id_eager_init)"
        )
        return
    if prime_pos < save_init_pos:
        error(
            f"{PRACTICE_MAIN_INIT}: practice_overlay_prime_build_ids must be called "
            "AFTER Practice_Save_Init (check_overlay_build_id_eager_init)"
        )


def check_phase3_ram_detection():
    """Phase 3: Practice_Save_Init must detect osMemSize and gate save/load.

    On stock 4 MB (osMemSize != 0x800000): gPracticeSaveDisabled must be set
    and slot_manager_init must NOT be called. On Expansion Pak (0x800000):
    the Pak pool at 0x80400000 must be used.

    This check ensures the osMemSize gate is present and the pool selection
    uses the correct constants.
    """
    src = read(PRACTICE_SAVE_C)

    # osMemSize check must be present.
    if "osMemSize" not in src:
        error(f"{PRACTICE_SAVE_C}: osMemSize check missing in Practice_Save_Init "
              "(check_phase3_ram_detection)")

    # Expansion Pak: accept >= threshold in Practice_Save_Init.
    if "0x00800000" not in src and "0x800000" not in src:
        error(f"{PRACTICE_SAVE_C}: Expansion Pak threshold 0x00800000/0x800000 missing "
              "(check_phase3_ram_detection)")

    # gPracticeSaveDisabled must be set when stock.
    if "gPracticeSaveDisabled" not in src:
        error(f"{PRACTICE_SAVE_C}: gPracticeSaveDisabled not used in practice_save.c "
              "(check_phase3_ram_detection)")

    # gPracticeRamSlotCount must be set at boot.
    if "gPracticeRamSlotCount" not in src:
        error(f"{PRACTICE_SAVE_C}: gPracticeRamSlotCount not set in practice_save.c "
              "(check_phase3_ram_detection)")

    slot_src = read(PRACTICE_SAVE_SLOTPOOL)

    # Megabyte blob lives in practice_save_slotpool.o (.practice_pool_pak only).
    if "sSlotPoolPak" not in slot_src:
        error(f"{PRACTICE_SAVE_SLOTPOOL}: sSlotPoolPak pool buffer missing "
              "(check_phase3_ram_detection)")

    # The old sSlotPool name must not appear (regression guard).
    import re as _re
    combined = src + "\n" + slot_src
    if _re.search(r"\bsSlotPool\b(?!Pak)", combined):
        error("practice_save*: old bare sSlotPool variable still referenced; "
              "use sSlotPoolPak in practice_save_slotpool.c (check_phase3_ram_detection)")

    # Practice_SaveStateSlot must guard on gPracticeSaveDisabled.
    save_fn_match = _re.search(
        r"void\s+Practice_SaveStateSlot\s*\(.*?\)\s*\{(.*?)^void\s",
        src, _re.DOTALL | _re.MULTILINE,
    )
    if save_fn_match and "gPracticeSaveDisabled" not in save_fn_match.group(1):
        error(f"{PRACTICE_SAVE_C}: Practice_SaveStateSlot must check gPracticeSaveDisabled "
              "(check_phase3_ram_detection)")

    # Practice_LoadStateSlot must guard on gPracticeSaveDisabled.
    load_fn_match = _re.search(
        r"void\s+Practice_LoadStateSlot\s*\(.*?\)\s*\{(.*?)^void\s",
        src, _re.DOTALL | _re.MULTILINE,
    )
    if load_fn_match and "gPracticeSaveDisabled" not in load_fn_match.group(1):
        error(f"{PRACTICE_SAVE_C}: Practice_LoadStateSlot must check gPracticeSaveDisabled "
              "(check_phase3_ram_detection)")


def check_practice_pool_placement():
    """Phase 3: practice_save BSS stays in stock .main_bss; megabyte blob in .practice_pool_pak.

    practice_save_slotpool.o BSS is anchored at VMA 0x80400000 (Expansion Pak DRAM).
    practice_save.o BSS (globals, slot_manager visibility) stays below the overlay window
    so stock 4 MB never touches Expansion Pak registers for save-disabled state.
    """
    linker = "linker_scripts/us/rev1/starfox64.ld"
    if not os.path.isfile(linker):
        return
    ld = read(linker)

    # Must have the new Pak section.
    if ".practice_pool_pak" not in ld:
        error(
            f"{linker}: .practice_pool_pak section missing; "
            "run tools/patch_linker_script.py (check_practice_pool_placement)"
        )

    # Must be at 0x80400000 (above 4 MB stock ceiling).
    if "0x80400000" not in ld or (".practice_pool_pak" in ld and "0x80400000" not in ld):
        error(
            f"{linker}: .practice_pool_pak must be explicitly placed at VMA 0x80400000 "
            "(check_practice_pool_placement)"
        )

    # Old .practice_pool (stock window placement) must not coexist.
    import re as _re
    if _re.search(r"^\s*\.practice_pool\s+\(NOLOAD\)", ld, _re.MULTILINE):
        error(
            f"{linker}: old .practice_pool (NOLOAD) section still present; "
            "it must be replaced with .practice_pool_pak 0x80400000 "
            "(check_practice_pool_placement)"
        )

    # practice_save.o(.bss) must appear inside .main_bss (globals for stock-safe init).
    main_bss_start = ld.find(".main_bss")
    main_bss_end = ld.find("main_VRAM_END", main_bss_start) if main_bss_start >= 0 else -1
    if main_bss_start >= 0 and main_bss_end >= 0:
        main_bss_block = ld[main_bss_start:main_bss_end]
        if "build/src/practice/practice_save.o(.bss)" not in main_bss_block:
            error(
                f"{linker}: practice_save.o(.bss) must be inside .main_bss "
                "(check_practice_pool_placement)"
            )
        if "build/src/practice/practice_save_slotpool.o(.bss)" in main_bss_block:
            error(
                f"{linker}: practice_save_slotpool.o(.bss) must NOT be inside .main_bss; "
                "only in .practice_pool_pak "
                "(check_practice_pool_placement)"
            )

    # Anchor pool section header (substring also appears inside .main_bss comments).
    pool_section_anchor = ld.find(".practice_pool_pak 0x80400000")
    if pool_section_anchor >= 0:
        pool_section_block = ld[pool_section_anchor:pool_section_anchor + 600]
        if "practice_save_slotpool.o(.bss)" not in pool_section_block:
            error(
                f"{linker}: .practice_pool_pak section does not contain "
                "practice_save_slotpool.o(.bss) (check_practice_pool_placement)"
            )


def check_practice_pool_no_overlay_overlap():
    """Phase 3: practice BSS sections must not overlap the overlay load window.

    With .practice_pool_pak placed at 0x80400000 (above 4 MB), the pool is
    completely outside the overlay load window [0x8019ae40, 0x80281000) and
    no overlap is expected. Any overlap is now a hard error (not a warning):
    if it regresses, the save-state data would be silently clobbered by
    overlay loads.

    Logic delegated to tools/audit_ram_layout.py. Skips silently if the map
    file is missing (e.g. fresh checkout without a build).
    """
    map_path = "build/starfox64.us.rev1.map"
    if not os.path.isfile(map_path):
        return
    # Import via path manipulation so this works whether invoked from the repo
    # root or from a worktree with a different sys.path.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import audit_ram_layout
    except ImportError:
        return
    finally:
        # Restore sys.path.
        if sys.path and sys.path[0] == os.path.dirname(os.path.abspath(__file__)):
            sys.path.pop(0)

    regions = audit_ram_layout.parse_map(map_path)
    overlaps = audit_ram_layout.find_overlaps(regions)
    if not overlaps:
        return

    # Collapse to one error per practice section.
    by_practice = {}
    for p, o, n in overlaps:
        by_practice.setdefault(p.name, []).append((o.name, n))

    for pname, hits in by_practice.items():
        worst_overlay, worst_size = max(hits, key=lambda t: t[1])
        error(
            f"{pname} overlaps overlay/asset load region "
            f"(largest: {worst_overlay} by {audit_ram_layout._human_size(worst_size)}); "
            f"run `python3 tools/audit_ram_layout.py` for the full report "
            "(check_practice_pool_no_overlay_overlap)"
        )


def check_boot_main_rom_budget():
    """The boot-loaded main segment must stay below the IPL copy ceiling.

    Hardware and mupen both wedge on the solid blue boot fill if main_ROM_END
    creeps past this boundary: early code/data beyond it is not resident when
    boot threads start.
    """
    map_path = "build/starfox64.us.rev1.map"
    if not os.path.isfile(map_path):
        return

    src = read(map_path)
    match = re.search(r"0x([0-9a-fA-F]+)\s+main_ROM_END\s*=", src)
    if not match:
        error(f"{map_path}: could not locate main_ROM_END (check_boot_main_rom_budget)")
        return

    main_rom_end = int(match.group(1), 16)
    limit = 0xFD000
    if main_rom_end > limit:
        error(
            f"{map_path}: main_ROM_END 0x{main_rom_end:06X} exceeds boot-safe "
            f"limit 0x{limit:06X}; shrink/move main assets or boot hangs on blue screen "
            "(check_boot_main_rom_budget)"
        )


def check_late_segment_addresses():
    """The .practice_late_core segment must land at the spec's chosen
    RAM address. Catches accidental relocations from manual .ld edits
    or address-picker mistakes in the patcher."""
    map_path = "build/starfox64.us.rev1.map"
    if not os.path.isfile(map_path):
        return
    src = read(map_path)
    match = re.search(r"0x0*([0-9a-fA-F]+)\s+practice_late_core_VRAM\s*=", src)
    if not match:
        error(
            f"{map_path}: practice_late_core_VRAM symbol not found. Has the "
            "patcher emitted the segment? (check_late_segment_addresses)"
        )
        return
    vram = int(match.group(1), 16)
    expected = 0x801F4000
    if vram != expected:
        error(
            f"{map_path}: practice_late_core_VRAM = 0x{vram:08X} (expected "
            f"0x{expected:08X}); spec address violated "
            "(check_late_segment_addresses)"
        )


def check_late_segment_ram_caps():
    """The .practice_late_core BSS extent must stay below 0x80274000 to
    preserve the 52 KB cushion before .buffers at 0x80281000. ROM size
    alone (check_late_segment_rom_budgets) is insufficient because
    NOLOAD BSS doesn't appear in ROM but does claim RAM. This is the
    same failure class as the f165d0e Aquas crash -- a large BSS
    silently punching through into adjacent live regions."""
    map_path = "build/starfox64.us.rev1.map"
    if not os.path.isfile(map_path):
        return
    src = read(map_path)
    match = re.search(r"0x0*([0-9a-fA-F]+)\s+practice_late_core_BSS_END\s*=", src)
    if not match:
        error(
            f"{map_path}: practice_late_core_BSS_END not found "
            "(check_late_segment_ram_caps)"
        )
        return
    bss_end = int(match.group(1), 16)
    cap = 0x80274000
    if bss_end >= cap:
        error(
            f"{map_path}: practice_late_core_BSS_END = 0x{bss_end:08X} "
            f"reached/exceeded RAM cap 0x{cap:08X}; "
            "the cushion before .buffers at 0x80281000 is gone. Shrink the "
            "core segment's BSS or move some objects to _pak in Phase 2 "
            "(check_late_segment_ram_caps)"
        )


def check_late_segment_rom_budgets():
    """The .practice_late_core ROM image must stay under 512 KB. This is
    a separate concern from check_late_segment_ram_caps: ROM size grows
    with .text + .data + .rodata, RAM cap grows with everything plus
    .bss. Either can blow before the other."""
    map_path = "build/starfox64.us.rev1.map"
    if not os.path.isfile(map_path):
        return
    src = read(map_path)
    m_start = re.search(r"0x0*([0-9a-fA-F]+)\s+practice_late_core_ROM_START\s*=", src)
    m_end   = re.search(r"0x0*([0-9a-fA-F]+)\s+practice_late_core_ROM_END\s*=", src)
    if not (m_start and m_end):
        error(
            f"{map_path}: practice_late_core ROM bounds not both present "
            "(check_late_segment_rom_budgets)"
        )
        return
    size = int(m_end.group(1), 16) - int(m_start.group(1), 16)
    cap = 0x80000  # 512 KB
    if size > cap:
        error(
            f"{map_path}: practice_late_core_ROM_SIZE = 0x{size:X} "
            f"({size/1024:.1f} KB) exceeds 512 KB cap; bps and manifest "
            "regenerate downstream of this. Shrink or split into _pak "
            "(check_late_segment_rom_budgets)"
        )


def check_practice_text_glyphs():
    """Practice_DrawText* string literals must use only the glyphs supported
    by Graphics_DisplaySmallText. Per CLAUDE.md the renderer's table is
    `[A-Z 0-9 space ! : - .]`; lowercase, '<', '>', '^', 'v', '/', etc. all
    render as blanks. This catches the input-display class of bug where
    direction arrows were silently invisible."""
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !:-._")
    # Match Practice_DrawText, Practice_DrawTextColor, Practice_DrawTextOutline
    # call. Capture any "..." literal inside the call up to the matching ).
    # We find calls greedily across newlines, then scan each call site for
    # string literals.
    call_re = re.compile(r"Practice_DrawText[A-Za-z_]*\s*\(", re.MULTILINE)
    string_re = re.compile(r'"((?:\\.|[^"\\])*)"')
    if not os.path.isdir(SRC_PRACTICE):
        return
    for fn in sorted(os.listdir(SRC_PRACTICE)):
        if not fn.endswith(".c"):
            continue
        path = os.path.join(SRC_PRACTICE, fn)
        src = read(path)
        for m in call_re.finditer(src):
            # Find the matching close paren for this call.
            depth = 1
            i = m.end()
            n = len(src)
            in_str = False
            in_chr = False
            while i < n and depth > 0:
                ch = src[i]
                if in_str:
                    if ch == "\\":
                        i += 2
                        continue
                    if ch == '"':
                        in_str = False
                elif in_chr:
                    if ch == "\\":
                        i += 2
                        continue
                    if ch == "'":
                        in_chr = False
                else:
                    if ch == '"':
                        in_str = True
                    elif ch == "'":
                        in_chr = True
                    elif ch == "(":
                        depth += 1
                    elif ch == ")":
                        depth -= 1
                i += 1
            call_text = src[m.start():i]
            for sm in string_re.finditer(call_text):
                literal = sm.group(1)
                bad = sorted({c for c in literal if c not in allowed})
                if bad:
                    line = src[:m.start()].count("\n") + 1
                    error(
                        f"{path}:{line}: Practice_DrawText literal {literal!r} "
                        f"contains unsupported glyphs {bad}; "
                        f"only [A-Z 0-9 space ! : - .] render. See CLAUDE.md "
                        f"(check_practice_text_glyphs)"
                    )


def check_sys_memory_practice_bump_getter():
    """Practice_MemoryGetBumpUsed exposes bump-arena watermark for heap audit."""
    if not os.path.isfile(SYS_MEMORY):
        return
    src = read(SYS_MEMORY)
    i_ifdef = src.find("#ifdef PRACTICE_ROM")
    i_fn = src.find("Practice_MemoryGetBumpUsed")
    if i_fn < 0:
        error(
            f"{SYS_MEMORY}: Practice_MemoryGetBumpUsed missing "
            "(check_sys_memory_practice_bump_getter)"
        )
    if i_ifdef < 0 or i_fn < i_ifdef:
        error(
            f"{SYS_MEMORY}: Practice_MemoryGetBumpUsed must follow "
            "#ifdef PRACTICE_ROM (check_sys_memory_practice_bump_getter)"
        )


def check_osk_declared():
    header = read("lib/ui/osk.h")
    if "osk_open" not in header:
        errors.append("lib/ui/osk.h missing osk_open declaration (check_osk_declared)")


def check_file_browser_declared():
    header = read("lib/ui/file_browser.h")
    if "file_browser_open" not in header:
        errors.append("lib/ui/file_browser.h missing file_browser_open (check_file_browser_declared)")


def check_practice_sd_wired():
    main_src = read("src/practice/practice_main.c")
    if "Practice_Sd_Update" not in main_src:
        errors.append("Practice_Sd_Update not called from practice_main.c (check_practice_sd_wired)")
    if "Practice_Sd_Draw" not in main_src:
        errors.append("Practice_Sd_Draw not called from practice_main.c (check_practice_sd_wired)")


def check_sd_root_namespace():
    src = read("src/practice/practice_sd.c")
    if '"/sageraces"' not in src and '"0:/sageraces"' not in src and '"0:/SAGERACE"' not in src:
        errors.append(
            'SD_ROOT must be "/sageraces", "0:/sageraces", or "0:/SAGERACE" — all SD paths share this namespace '
            "(check_sd_root_namespace)"
        )
    if '"/sf64-practice"' in src or '"/sf64practice"' in src:
        errors.append(
            "Old /sf64-practice path found in practice_sd.c — update to /sageraces/sf64 "
            "(check_sd_root_namespace)"
        )


def check_sd_fatfs_mounted():
    src = read("src/practice/practice_sd.c")
    if "f_mount" not in src:
        errors.append(
            "practice_sd.c does not call f_mount — FatFs needs a work area before "
            "any file operation (check_sd_fatfs_mounted)"
        )
    if "sFatfsWork" not in src:
        errors.append(
            "practice_sd.c missing static FATFS sFatfsWork work area "
            "(check_sd_fatfs_mounted)"
        )


def check_sd_save_implemented():
    src = read("lib/slot_manager.c")
    if "f_open" not in src:
        errors.append(
            "slot_manager_save_sd_named appears to still be a stub (no f_open call) "
            "(check_sd_save_implemented)"
        )
    # FA_CREATE_ALWAYS writes directly to the final path; f_rename was removed because
    # SC64's FatFs implementation returns FR_INVALID_NAME on the rename step.
    if "FA_CREATE_ALWAYS" not in src:
        errors.append(
            "slot_manager_save_sd_named must use FA_CREATE_ALWAYS for direct-write save "
            "(check_sd_save_implemented)"
        )

def check_sd_load_implemented():
    src = read("lib/slot_manager.c")
    if "f_read" not in src:
        errors.append(
            "slot_manager_load_sd_named appears to still be a stub (no f_read call) "
            "(check_sd_load_implemented)"
        )
    if "f_size" not in src:
        errors.append(
            "slot_manager_load_sd_named missing f_size sanity check "
            "(check_sd_load_implemented)"
        )


def check_sd_per_op_release():
    """practice_sd.c must bracket each FatFs operation with sd_op_begin/sd_op_end
    (lazy f_mount / f_unmount). iodev_sd_acquire/release are intentionally absent:
    SD_OP_INIT causes a multi-second hardware stall on SC64 and was confirmed to
    cause OPEN FAIL errors. The card stays acquired from boot-time iodev_sd_init."""
    src = read("src/practice/practice_sd.c")
    if "sd_op_begin" not in src or "sd_op_end" not in src:
        errors.append(
            "practice_sd.c missing sd_op_begin/sd_op_end helpers "
            "(check_sd_per_op_release)"
        )
    if "f_mount" not in src:
        errors.append(
            "practice_sd.c sd_op_begin must call f_mount for lazy FatFs remount "
            "(check_sd_per_op_release)"
        )


def check_deferred_bgm_rescue():
    """Same-spec level launches must queue a deferred BGM rescue play.

    When Practice_LaunchLevel fires Audio_SetAudioSpec with the same spec
    already active, SEQCMD_RESET_AUDIO_HEAP only stops BGM without a heap
    reset. Play_Init's AUDIO_PLAY_BGM may be silently dropped if the audio
    engine's isWaitingForFonts flag is still set from the level-select preview.
    The fix: queue gPracticeBgmPending in Practice_LaunchLevel for same-spec
    launches and count down gPracticeBgmPendingDelay PLAY_UPDATE frames before
    firing in Practice_Save_Tick.
    """
    level_src = read(PRACTICE_LEVEL)
    save_src  = read(PRACTICE_SAVE_C)

    if "Practice_QueueBgmRescue(" not in level_src:
        error(
            f"{PRACTICE_LEVEL}: Practice_LaunchLevel must call Practice_QueueBgmRescue "
            "for same-spec rescue (check_deferred_bgm_rescue)"
        )
    if "void Practice_QueueBgmRescue(" not in save_src:
        error(
            f"{PRACTICE_SAVE_C}: Practice_QueueBgmRescue definition missing "
            "(check_deferred_bgm_rescue)"
        )
    if "gPracticeBgmPendingDelay" not in save_src:
        error(
            f"{PRACTICE_SAVE_C}: gPracticeBgmPendingDelay must be defined "
            "(check_deferred_bgm_rescue)"
        )
    tick_match = re.search(
        r"void\s+Practice_Save_Tick\s*\([^)]*\)\s*\{(.*?)^\}",
        save_src, re.DOTALL | re.MULTILINE,
    )
    if tick_match:
        tick_body = tick_match.group(1)
        if "gPracticeBgmPendingDelay" not in tick_body:
            error(
                f"{PRACTICE_SAVE_C}: Practice_Save_Tick must count down "
                "gPracticeBgmPendingDelay (check_deferred_bgm_rescue)"
            )
        if "PLAY_UPDATE" not in tick_body:
            error(
                f"{PRACTICE_SAVE_C}: Practice_Save_Tick delay countdown must "
                "gate on PLAY_UPDATE (check_deferred_bgm_rescue)"
            )
    else:
        error(f"{PRACTICE_SAVE_C}: could not locate Practice_Save_Tick body")


def check_bgm_rescue_clears_waiting_for_fonts():
    """BGM rescue must clear isWaitingForFonts before calling AUDIO_PLAY_BGM.

    If a same-spec BGM preview is cancelled mid-async-font-load (user presses
    A quickly at level select), Audio_StopSequence cancels the DMA but does NOT
    reset sActiveSequences[BGM].isWaitingForFonts. Audio_ProcessSeqCmd silently
    drops every subsequent AUDIO_PLAY_BGM while that flag is set.  The rescue
    in Practice_Save_Tick must clear the flag before firing AUDIO_PLAY_BGM to
    ensure the call is unconditionally effective.
    """
    save_src = read(PRACTICE_SAVE_C)

    tick_match = re.search(
        r"void\s+Practice_Save_Tick\s*\([^)]*\)\s*\{(.*?)^\}",
        save_src, re.DOTALL | re.MULTILINE,
    )
    if not tick_match:
        error(f"{PRACTICE_SAVE_C}: could not locate Practice_Save_Tick body "
              "(check_bgm_rescue_clears_waiting_for_fonts)")
        return

    tick_body = tick_match.group(1)

    # The extern declaration for sActiveSequences must be visible.
    if "extern ActiveSequence sActiveSequences" not in save_src:
        error(
            f"{PRACTICE_SAVE_C}: extern ActiveSequence sActiveSequences[] declaration missing; "
            "needed to clear isWaitingForFonts in the BGM rescue path "
            "(check_bgm_rescue_clears_waiting_for_fonts)"
        )

    # The rescue must clear isWaitingForFonts before AUDIO_PLAY_BGM.
    clear_pos = tick_body.find("isWaitingForFonts = 0")
    play_pos  = tick_body.find("AUDIO_PLAY_BGM(gPracticeBgmPendingSeqId)")
    if clear_pos < 0:
        error(
            f"{PRACTICE_SAVE_C}: Practice_Save_Tick must clear "
            "sActiveSequences[SEQ_PLAYER_BGM].isWaitingForFonts = 0 before "
            "firing AUDIO_PLAY_BGM (check_bgm_rescue_clears_waiting_for_fonts)"
        )
    if play_pos < 0:
        error(
            f"{PRACTICE_SAVE_C}: Practice_Save_Tick must call "
            "AUDIO_PLAY_BGM(gPracticeBgmPendingSeqId) in the rescue path "
            "(check_bgm_rescue_clears_waiting_for_fonts)"
        )
    if clear_pos >= 0 and play_pos >= 0 and clear_pos > play_pos:
        error(
            f"{PRACTICE_SAVE_C}: isWaitingForFonts clear must come BEFORE "
            "AUDIO_PLAY_BGM in Practice_Save_Tick "
            "(check_bgm_rescue_clears_waiting_for_fonts)"
        )


def check_owl_logo():
    """owl-400 logo texture is wired into the level-select draw path correctly."""
    owl_c = os.path.join(SRC_PRACTICE, "practice_owl_tex.c")
    src = read(owl_c)
    if "sPracticeOwlTex" not in src:
        error(f"{owl_c}: sPracticeOwlTex array missing")
    if "Practice_Owl_Draw" not in src:
        error(f"{owl_c}: Practice_Owl_Draw function missing")
    if "SETUPDL_85" not in src:
        error(f"{owl_c}: CI8 texture draw must use SETUPDL_85 (G_TT_RGBA16), not SETUPDL_76 (G_TT_NONE)")
    if "sPracticeOwlTLUT" not in src:
        error(f"{owl_c}: sPracticeOwlTLUT palette missing")
    if "Lib_TextureRect_CI8" not in src:
        error(f"{owl_c}: missing Lib_TextureRect_CI8 call")

    level_src = read(PRACTICE_LEVEL)
    if "Practice_Owl_Draw" not in level_src:
        error(f"{PRACTICE_LEVEL}: Practice_Owl_Draw not called from LevelSelect_Draw")

    h_src = read(INCLUDE_PRACTICE)
    if "Practice_Owl_Draw" not in h_src:
        error(f"{INCLUDE_PRACTICE}: Practice_Owl_Draw not declared")


def check_hit64_logo():
    """HIT64 logo texture is wired into the level-select draw path correctly."""
    logo_c = os.path.join(SRC_PRACTICE, "practice_logo_tex.c")
    src = read(logo_c)
    if "sPracticeLogoTex" not in src:
        error(f"{logo_c}: sPracticeLogoTex array missing")
    if "Practice_Logo_Draw" not in src:
        error(f"{logo_c}: Practice_Logo_Draw function missing")
    if "SETUPDL_85" not in src:
        error(f"{logo_c}: CI8 texture draw must use SETUPDL_85 (G_TT_RGBA16), not SETUPDL_76 (G_TT_NONE)")
    if "sPracticeLogoTLUT" not in src:
        error(f"{logo_c}: sPracticeLogoTLUT palette missing")
    if "Lib_TextureRect_CI8" not in src:
        error(f"{logo_c}: missing Lib_TextureRect_CI8 call")

    level_src = read(PRACTICE_LEVEL)
    if "Practice_Logo_Draw" not in level_src:
        error(f"{PRACTICE_LEVEL}: Practice_Logo_Draw not called from LevelSelect_Draw")

    h_src = read(INCLUDE_PRACTICE)
    if "Practice_Logo_Draw" not in h_src:
        error(f"{INCLUDE_PRACTICE}: Practice_Logo_Draw not declared")


def check_minimap_boss():
    """Minimap must iterate gBosses and distinguish Great Fox from enemy bosses."""
    minimap_c = os.path.join(SRC_PRACTICE, "practice_minimap.c")
    src = read(minimap_c)
    if "gBosses" not in src:
        error(f"{minimap_c}: gBosses not iterated - boss will never appear on minimap")
    if "OBJ_BOSS_SZ_GREAT_FOX" not in src:
        error(f"{minimap_c}: OBJ_BOSS_SZ_GREAT_FOX not handled - Great Fox needs distinct color from enemy bosses")
    if "boss->obj.rot.y + 180.0f" not in src:
        error(f"{minimap_c}: boss heading missing +180 offset - boss rot.y=0 faces +Z (opposite actor convention)")
    if 'Minimap_FloatValid(&boss->obj.rot.y)' not in src:
        error(f"{minimap_c}: boss rot.y not NaN-guarded - SIN_DEG on a NaN freezes the N64")


def check_xbld_load_zeros_entities():
    """Cross-build loads must zero actor/boss/playerShots arrays when the overlay
    build ID mismatches, to prevent a MIPS float-to-int trap on the next update frame."""
    save_c = os.path.join(SRC_PRACTICE, "practice_save.c")
    src = read(save_c)
    if "sSdLastLoadWasXBld" not in src:
        error(f"{save_c}: sSdLastLoadWasXBld flag missing - cross-build load safety not implemented")
    for arr in ('actors', 'bosses', 'scenery', 'sprites', 'effects', 'items', 'playerShots'):
        if f'bzero(sn->{arr}' not in src:
            error(f"{save_c}: bzero(sn->{arr}) missing - entity array not cleared on overlay mismatch")
    if "SD XBLD OK" not in src:
        error(f"{save_c}: 'SD XBLD OK' status missing - cross-build loads must show distinct status")

    sd_c = os.path.join(SRC_PRACTICE, "practice_sd.c")
    sd_src = read(sd_c)
    if "Practice_Save_LastLoadWasXBuild" not in sd_src:
        error(f"{sd_c}: Practice_Save_LastLoadWasXBuild not called - cross-build status not shown to user")


def check_status_banner_draws_during_pause():
    """Practice_Hud_Draw must render the SD save/load status banner before the
    PLAY_UPDATE early-return, otherwise results triggered from the in-game pause
    menu finish silently. See practice_hud.c bug found 2026-05-02."""
    hud_c = os.path.join(SRC_PRACTICE, "practice_hud.c")
    src = read(hud_c)

    func_match = re.search(
        r"void\s+Practice_Hud_Draw\s*\(\s*void\s*\)\s*\{(.*?)\n\}",
        src, re.DOTALL
    )
    if not func_match:
        error(f"{hud_c}: Practice_Hud_Draw function not found")
        return

    body = func_match.group(1)
    status_idx = body.find("sStatusTimer > 0")
    guard_idx = body.find("gPlayState != PLAY_UPDATE")

    if status_idx < 0:
        error(f"{hud_c}: status banner draw (sStatusTimer > 0) missing from Practice_Hud_Draw")
        return
    if guard_idx < 0:
        error(f"{hud_c}: PLAY_UPDATE guard missing from Practice_Hud_Draw")
        return
    if status_idx > guard_idx:
        error(
            f"{hud_c}: status banner draw must come BEFORE the PLAY_UPDATE guard, "
            f"otherwise SD save/load results from the pause menu render silently"
        )


def check_build_info():
    """Build hash header is generated and included in the level-select draw path."""
    gen_script = os.path.join("tools", "gen_build_info.py")
    if not os.path.exists(gen_script):
        error(f"{gen_script}: gen_build_info.py generator script missing")

    makefile_src = read(MAKEFILE)
    if "gen_build_info.py" not in makefile_src:
        error(f"{MAKEFILE}: gen_build_info.py not invoked from Makefile")
    if "gen-build-info" not in makefile_src:
        error(f"{MAKEFILE}: gen-build-info target missing")

    level_src = read(PRACTICE_LEVEL)
    if "practice_build_info.h" not in level_src:
        error(f"{PRACTICE_LEVEL}: practice_build_info.h not included")
    if "PRACTICE_BUILD_HASH" not in level_src:
        error(f"{PRACTICE_LEVEL}: PRACTICE_BUILD_HASH not drawn in LevelSelect_Draw")

    gitignore = read(".gitignore")
    if "practice_build_info.h" not in gitignore:
        error(".gitignore: practice_build_info.h not gitignored (generated file)")


def check_frame_advance_hook():
    """Practice_FrameAdvance_IsFrozen must guard Play_Main in Game_Update's
    GSTATE_PLAY block in fox_game.c.

    Without this hook, frame advance cannot block Play_Main from running.
    The check locates Game_Update, then finds the GSTATE_PLAY case inside
    it, and verifies Practice_FrameAdvance_IsFrozen appears there.
    """
    src = read(FOX_GAME)

    # Find Game_Update body using brace matching.
    game_update_body = find_c_function(src, "Game_Update")
    if game_update_body is None:
        error("check_frame_advance_hook: could not locate Game_Update in fox_game.c")
        return

    # Find the GSTATE_PLAY case block within Game_Update.
    gstate_play_match = re.search(
        r"case\s+GSTATE_PLAY\s*:(.*?)break\s*;",
        game_update_body, re.DOTALL,
    )
    if not gstate_play_match:
        error("check_frame_advance_hook: could not locate GSTATE_PLAY case in Game_Update (fox_game.c)")
        return

    block = gstate_play_match.group(1)
    if "Practice_FrameAdvance_IsFrozen" not in block:
        error(
            "Practice_FrameAdvance_IsFrozen() must guard Play_Main in the "
            "GSTATE_PLAY block of Game_Update in fox_game.c (check_frame_advance_hook)"
        )

    # IsFrozen must only apply when PMENU_CLOSED — PMENU_OPEN must always
    # let Play_Main tick so that frame advance doesn't freeze menu navigation.
    if "PMENU_OPEN" not in block:
        error(
            "GSTATE_PLAY block in fox_game.c must gate IsFrozen() on PMENU_CLOSED "
            "so that PMENU_OPEN always runs Play_Main (check_frame_advance_hook)"
        )


def check_frame_advance_clears_on_menu():
    """Practice_FrameAdvance_Update must clear sIsPaused when the menu is open.

    If the update just returns early on menu open, sIsPaused stays latched and
    gameplay stays frozen after the menu closes.
    """
    fa_path = os.path.join("src", "practice", "practice_frame_advance.c")
    src = read(fa_path)

    update_body = find_c_function(src, "Practice_FrameAdvance_Update")
    if update_body is None:
        error("check_frame_advance_clears_on_menu: could not locate Practice_FrameAdvance_Update")
        return

    # There must be an assignment to sIsPaused inside the PMENU_CLOSED guard block,
    # meaning state is cleared when the menu is open.
    menu_guard = re.search(
        r"gPracticeMenuState\s*!=\s*PMENU_CLOSED(.*?)return\s*;",
        update_body, re.DOTALL,
    )
    if not menu_guard:
        error(
            "check_frame_advance_clears_on_menu: could not find PMENU_CLOSED guard "
            "in Practice_FrameAdvance_Update"
        )
        return

    guard_block = menu_guard.group(1)
    if "sIsPaused" not in guard_block:
        error(
            "Practice_FrameAdvance_Update must clear sIsPaused when gPracticeMenuState "
            "!= PMENU_CLOSED — otherwise frame advance stays latched across menu opens "
            "(check_frame_advance_clears_on_menu)"
        )


def check_cs_tap_slot_baseline():
    """CS TAP timing must use slot-aware baseline for unlocked shots.

    After firing a charge shot, the code must begin polling gPlayerShots[14]
    until it is fireable again (sShotSlotFreeFrame / sTrackingSlot), and use
    that frame as the baseline when it is later than sChargeReadyFrame.
    This prevents blaming the player for frames they could not have fired.
    """
    cs = os.path.join("src", "practice", "practice_charge_shot.c")
    src = read(cs)

    if "sShotSlotFreeFrame" not in src:
        error(f"{cs}: sShotSlotFreeFrame missing — slot-aware baseline removed")
    if "sTrackingSlot" not in src:
        error(f"{cs}: sTrackingSlot missing — slot tracking state removed")
    if "effectiveBaseline" not in src:
        error(f"{cs}: effectiveBaseline missing — slot-aware formula removed")

    lock_on_begin = find_c_function(src, "Practice_ChargeAssist_LockOnBegin")
    if lock_on_begin is None:
        error(f"{cs}: Practice_ChargeAssist_LockOnBegin missing")
        return
    if "gPlayerShots[14]" not in lock_on_begin:
        error(f"{cs}: gPlayerShots[14] poll missing from Practice_ChargeAssist_LockOnBegin")


def check_enemy_health_hide_models():
    """Ghost mode: enemyHealthHideModels must trigger a fullscreen blackout in practice_enemy_health.c."""
    src_path = os.path.join(SRC_PRACTICE, "practice_enemy_health.c")
    if not os.path.isfile(src_path):
        return
    src = read(src_path)
    if "enemyHealthHideModels" not in src:
        error(f"{src_path}: enemyHealthHideModels not referenced (ghost mode blackout missing)")
    if "320" not in src or "240" not in src:
        error(f"{src_path}: fullscreen blackout rect (320x240) missing for enemyHealthHideModels")


def check_hitbox_shadow_drawn():
    """SHADOW/WHOOSH hitboxes must be drawn (grey), not silently skipped."""
    hitbox_path = os.path.join(SRC_PRACTICE, "practice_hitbox.c")
    if not os.path.isfile(hitbox_path):
        error(f"Hitbox source missing: {hitbox_path}")
        return
    src = read(hitbox_path)
    fn = find_c_function(src, "Hitbox_DrawObjectHitboxes")
    if fn is None:
        error("practice_hitbox.c: Hitbox_DrawObjectHitboxes not found")
        return
    if "HITBOX_SHADOW" not in fn:
        error("Hitbox_DrawObjectHitboxes: HITBOX_SHADOW branch missing")
    # Must call DrawBox inside the SHADOW branch, not just continue
    shadow_idx = fn.find("HITBOX_SHADOW")
    if shadow_idx == -1:
        return
    branch = fn[shadow_idx:shadow_idx + 400]
    if "Hitbox_DrawBox" not in branch:
        error("Hitbox_DrawObjectHitboxes: SHADOW/WHOOSH hitboxes not drawn (grey box missing)")


def check_enemy_health():
    """Enemy health HUD: source exists, wired into Practice_Draw, config fields present."""
    src_path = os.path.join(SRC_PRACTICE, "practice_enemy_health.c")
    if not os.path.isfile(src_path):
        error(f"Enemy health source missing: {src_path}")
        return

    main_src = read(PRACTICE_MAIN_INIT)
    practice_h = read(INCLUDE_PRACTICE)

    if "Practice_EnemyHealth_Draw" not in main_src:
        error(f"{PRACTICE_MAIN_INIT}: Practice_EnemyHealth_Draw not called from Practice_Draw")
    if "showEnemyHealth" not in practice_h:
        error(f"{INCLUDE_PRACTICE}: showEnemyHealth not in PracticeConfig")
    if "enemyHealthSort" not in practice_h:
        error(f"{INCLUDE_PRACTICE}: enemyHealthSort not in PracticeConfig")


def check_boss_test():
    """Boss-test feature: file exists, flag is wired, reset paths are present."""
    boss_test_path = os.path.join(SRC_PRACTICE, "practice_boss_test.c")
    if not os.path.isfile(boss_test_path):
        error(f"Boss-test source missing: {boss_test_path}")
        return

    boss_test_src = read(boss_test_path)
    fox_co_src    = read("src/overlays/ovl_i1/fox_co.c")
    practice_h    = read(INCLUDE_PRACTICE)
    main_src      = read(os.path.join(SRC_PRACTICE, "practice_main.c"))
    level_src     = read(PRACTICE_LEVEL)
    patch_src     = read("tools/patch_linker_script.py")

    if "gPracticeForceCarrier" not in boss_test_src:
        error("practice_boss_test.c missing gPracticeForceCarrier definition")
    if "gPracticeForceCarrier" not in fox_co_src:
        error("fox_co.c missing gPracticeForceCarrier override")
    if "Practice_BossTest_Launch" not in practice_h:
        error("practice.h missing Practice_BossTest_Launch declaration")
    if "Practice_BossTest_Launch" not in boss_test_src:
        error("practice_boss_test.c missing Practice_BossTest_Launch definition")
    if "gPracticeForceCarrier = false" not in main_src:
        error("Practice_Init missing gPracticeForceCarrier = false reset")
    if "gPracticeForceCarrier = false" not in level_src:
        error("Practice_LevelSelect_Update missing gPracticeForceCarrier = false on non-boss A-press")
    if '"practice_boss_test"' not in patch_src:
        error('tools/patch_linker_script.py missing "practice_boss_test" in PRACTICE_OBJS')

    # Negative check: gPracticeForceCarrier must NOT be a PracticeConfig field (runtime only)
    config_match = re.search(
        r"typedef struct PracticeConfig\s*\{(.*?)\}\s*PracticeConfig;",
        practice_h, re.DOTALL
    )
    if config_match and "gPracticeForceCarrier" in config_match.group(1):
        error("gPracticeForceCarrier must not be a PracticeConfig field (runtime-only state)")

    if '"CRUSHER"' not in boss_test_src:
        error('practice_boss_test.c missing "CRUSHER" entry in sBossList')

    crusher_test = os.path.join("tests", "test_boss_test_crusher.lua")
    if not os.path.isfile(crusher_test):
        error(f"Boss Crusher functional test missing: {crusher_test}")

    for name, fname in [
        ('"BACOON"',    "tests/test_boss_test_bacoon.lua"),
        ('"SPYBORG"',   "tests/test_boss_test_spyborg.lua"),
        ('"VULKAIN"',   "tests/test_boss_test_vulkain.lua"),
        ('"SARUMAR"',   "tests/test_boss_test_sarumar.lua"),
        ('"GORAS"',     "tests/test_boss_test_goras.lua"),
        ('"GORGON"',    "tests/test_boss_test_gorgon.lua"),
        ('"GOLEMECH"',  "tests/test_boss_test_golemech.lua"),
        ('"ANDROSS"',   "tests/test_boss_test_andross.lua"),
    ]:
        if name not in boss_test_src:
            error(f'practice_boss_test.c missing {name} entry in sBossList')
        if not os.path.isfile(fname):
            error(f'Boss test missing: {fname}')

    if '"A.BRAIN"' not in boss_test_src:
        error('practice_boss_test.c missing "A.BRAIN" entry in sBossList')
    brain_test = os.path.join("tests", "test_boss_test_andross_brain.lua")
    if not os.path.isfile(brain_test):
        error(f"Boss brain functional test missing: {brain_test}")


def check_macro_hook():
    """Practice_Macro_PrePlay() must be called immediately before Play_Main()
    inside the GSTATE_PLAY block so injected inputs are visible on the same frame.
    """
    src = read(FOX_GAME)
    game_update_body = find_c_function(src, "Game_Update")
    if game_update_body is None:
        error("check_macro_hook: could not locate Game_Update in fox_game.c")
        return

    gstate_play_match = re.search(
        r"case\s+GSTATE_PLAY\s*:(.*?)break\s*;",
        game_update_body, re.DOTALL,
    )
    if not gstate_play_match:
        error("check_macro_hook: could not locate GSTATE_PLAY case in Game_Update")
        return

    block = gstate_play_match.group(1)
    preplay_idx = block.find("Practice_Macro_PrePlay")
    if preplay_idx < 0:
        error(
            "Practice_Macro_PrePlay() must be called in the GSTATE_PLAY block of "
            "Game_Update before Play_Main() (check_macro_hook)"
        )
        return
    # The block may contain multiple Play_Main() calls (e.g. one for PMENU_OPEN
    # that does not need PrePlay, plus one inside the PMENU_CLOSED branch that
    # does). The contract that matters: there must be at least one Play_Main()
    # textually AFTER the PrePlay call so the injected inputs are visible to
    # the gameplay tick. A Play_Main() before PrePlay alone is not sufficient.
    play_main_after = block.find("Play_Main(", preplay_idx)
    if play_main_after < 0:
        error(
            "Practice_Macro_PrePlay() must be called before a Play_Main() in the "
            "GSTATE_PLAY block of Game_Update so injected inputs are visible the "
            "same frame (check_macro_hook)"
        )


def check_macro_buf_section():
    """practice_macro_buf.o(.bss) must be in .practice_macro_pak and
    practice_macro_snap.o(.bss) must be in .practice_macro_snap_pak --
    both kept out of stock RAM.
    """
    ld = read("linker_scripts/us/rev1/starfox64.ld")
    if ".practice_macro_pak" not in ld:
        error(
            ".practice_macro_pak section missing from linker script -- run "
            "tools/patch_linker_script.py (check_macro_buf_section)"
        )
        return
    macro_section_match = re.search(
        r"\.practice_macro_pak\s+0x[0-9a-fA-F]+.*?\{(.*?)\}",
        ld, re.DOTALL,
    )
    if not macro_section_match:
        error(
            ".practice_macro_pak section exists in linker script but the "
            "section body could not be parsed (malformed?) "
            "(check_macro_buf_section)"
        )
        return
    section_body = macro_section_match.group(1)
    if "practice_macro_buf.o(.bss)" not in section_body:
        error(
            ".practice_macro_pak section exists but practice_macro_buf.o(.bss) "
            "is not inside it (check_macro_buf_section)"
        )
    if ".practice_macro_snap_pak" not in ld:
        error(
            ".practice_macro_snap_pak section missing from linker script -- run "
            "tools/patch_linker_script.py (check_macro_buf_section)"
        )
        return
    snap_section_match = re.search(
        r"\.practice_macro_snap_pak\s+0x[0-9a-fA-F]+.*?\{(.*?)\}",
        ld, re.DOTALL,
    )
    if not snap_section_match:
        error(
            ".practice_macro_snap_pak section exists in linker script but the "
            "section body could not be parsed (malformed?) "
            "(check_macro_buf_section)"
        )
        return
    section_body = snap_section_match.group(1)
    if "practice_macro_snap.o(.bss)" not in section_body:
        error(
            ".practice_macro_snap_pak section exists but practice_macro_snap.o(.bss) "
            "is not inside it (check_macro_buf_section)"
        )
    # Verify the two new wrappers exist in practice_save.c.
    save_src = read("src/practice/practice_save.c")
    for fn in ("Practice_Save_MacroSnap", "Practice_Save_MacroApply"):
        if fn not in save_src:
            error(
                f"{fn} not found in practice_save.c (check_macro_buf_section)"
            )


def main():
    check_config_inits()
    check_function_definitions()
    check_source_in_build()
    check_engine_hooks()
    check_score_stats_hooks()
    check_cutscene_skip_hook()
    check_isviewer_sc64()
    check_iodev_sc64()
    check_iodev_ed64()
    check_spawn_zone_typing()
    check_level_select_bgm_ready_gate()
    check_release_patch_workflow()
    check_lib_isolation()
    check_lib_libultra_scope()
    check_fatfs_isolation()
    check_overlay_table_complete()
    check_tag_registry()
    check_serializer_parity()
    check_state_version_defined_once()
    check_max_state_size_budget()
    check_radial_menu_save_allowed()
    check_sys_memory_practice_bump_getter()
    check_snapshot_gplayers_use_cam_count()
    check_overlay_build_id_no_rom_read()
    check_overlay_build_id_eager_init()
    check_audio_spec_for_level_single_source()
    check_deferred_bgm_rescue()
    check_bgm_rescue_clears_waiting_for_fonts()
    check_phase5_state_machine_lifecycle()
    check_phase3_ram_detection()
    check_practice_pool_placement()
    check_practice_pool_no_overlay_overlap()
    check_sd_host()
    check_slot_manager_atomic_write()
    check_boot_main_rom_budget()
    check_late_segment_addresses()
    check_late_segment_ram_caps()
    check_late_segment_rom_budgets()
    check_practice_text_glyphs()
    check_osk_declared()
    check_file_browser_declared()
    check_practice_sd_wired()
    check_sd_root_namespace()
    check_sd_fatfs_mounted()
    check_sd_save_implemented()
    check_sd_load_implemented()
    check_sd_per_op_release()
    check_frame_advance_hook()
    check_frame_advance_clears_on_menu()
    check_macro_hook()
    check_macro_buf_section()
    check_cs_tap_slot_baseline()
    check_hit64_logo()
    check_owl_logo()
    check_minimap_boss()
    check_xbld_load_zeros_entities()
    check_status_banner_draws_during_pause()
    check_build_info()
    check_boss_test()
    check_enemy_health()
    check_hitbox_shadow_drawn()
    check_enemy_health_hide_models()

    if errors:
        print("Practice ROM invariant check FAILED:")
        for e in errors:
            print(f"  - {e}")
        if warnings:
            print("Warnings:")
            for w in warnings:
                print(f"  - {w}")
        return 1

    if warnings:
        print("Practice ROM invariant checks passed (with warnings):")
        for w in warnings:
            print(f"  - {w}")
        return 0

    print("Practice ROM invariant checks passed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
