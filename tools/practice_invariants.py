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
PRACTICE_MAIN_INIT = os.path.join("src", "practice", "practice_main.c")
FOX_GAME = "src/engine/fox_game.c"
FOX_PLAY = "src/engine/fox_play.c"
FOX_DISPLAY = "src/engine/fox_display.c"
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
        (FOX_PLAY, "gPracticeCheckpointProgress", "gPracticeCheckpointProgress checkpoint hook must exist in fox_play.c Player_Setup"),
        (FOX_DISPLAY, "Practice_Hitbox_Draw", "Practice_Hitbox_Draw() must be called from fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_IsActive", "Practice_FreeCam_IsActive() hook must exist in fox_display.c"),
        (FOX_DISPLAY, "Practice_FreeCam_GetView", "Practice_FreeCam_GetView() hook must exist in fox_display.c"),
    ]
    for filepath, symbol, msg in hooks:
        src = read(filepath)
        if symbol not in src:
            error(msg)

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

def check_iodev_ed64():
    """ED64 X iodev backend must preserve protocol invariants.

    Phase 1b Tasks 1-2 only ship cart detection + the verified CRC layer;
    SD init / read / write are stubs. The invariants below cover what is
    actually implemented; once Tasks 3-5 land, additional checks (sd_crc.h
    include, sd_crc7 callsite) should be added here.
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
    # 0xAA55 write to open the FPGA register window. The Phase 1b plan
    # called for a 0xAA55 + 0x55AA pair, but the gz reference firmware
    # (mature working code on real ED64 X hardware) and Krikzz public
    # docs use only 0xAA55. Match the literal 0xAA55 in a #define
    # (not just any mention) so a typo in the constant is caught
    # rather than passing because of an unchanged comment.
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
    "lib/iodev/iodev_ed64.c",  # Phase 1b
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


def check_phase4_engine_hooks():
    """slot-backed save initializes after Wave 3 slot regression test."""
    pm = PRACTICE_MAIN_INIT
    if not os.path.isfile(pm):
        return

    txt = read(pm)
    seq = txt.find("Practice_SlotTest_Run();")
    if seq < 0:
        error(f"{pm}: Practice_SlotTest_Run missing (check_phase4_engine_hooks)")
        return

    ai = txt.find("Practice_Save_Init();")
    if ai < seq:
        error(
            "Practice_Save_Init() must appear after Practice_SlotTest_Run() "
            f"in {pm} (check_phase4_engine_hooks)"
        )

    hb = txt.find("Practice_HeapAudit_Boot();")
    if hb < 0:
        error(f"{pm}: Practice_HeapAudit_Boot() missing (check_phase4_engine_hooks)")
    elif hb < ai:
        error(
            "Practice_HeapAudit_Boot() must appear after Practice_Save_Init() "
            f"in {pm} (check_phase4_engine_hooks)"
        )

    if "Practice_HeapAudit_PerFrame();" not in txt:
        error(
            f"{pm}: Practice_HeapAudit_PerFrame() must be called from Practice_Update "
            "(check_phase4_engine_hooks)"
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


def check_practice_text_glyphs():
    """Practice_DrawText* string literals must use only the glyphs supported
    by Graphics_DisplaySmallText. Per CLAUDE.md the renderer's table is
    `[A-Z 0-9 space ! : - .]`; lowercase, '<', '>', '^', 'v', '/', etc. all
    render as blanks. This catches the input-display class of bug where
    direction arrows were silently invisible."""
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !:-.")
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


def main():
    check_config_inits()
    check_function_definitions()
    check_source_in_build()
    check_engine_hooks()
    check_cutscene_skip_hook()
    check_isviewer_sc64()
    check_iodev_sc64()
    check_iodev_ed64()
    check_spawn_zone_typing()
    check_release_patch_workflow()
    check_lib_isolation()
    check_lib_libultra_scope()
    check_fatfs_isolation()
    check_overlay_table_complete()
    check_tag_registry()
    check_serializer_parity()
    check_state_version_defined_once()
    check_max_state_size_budget()
    check_phase4_engine_hooks()
    check_sys_memory_practice_bump_getter()
    check_phase3_ram_detection()
    check_practice_pool_placement()
    check_practice_pool_no_overlay_overlap()
    check_practice_text_glyphs()

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
