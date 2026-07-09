/* src/practice/practice_sd.c
 * Phase 6: OSK + file browser rendering and practice glue.
 * lib/ui/osk.c and lib/ui/file_browser.c own state machines;
 * this file owns all N64 rendering and N64 input translation.
 * Includes resolve via -Ilib in IINC (Makefile).
 */
#ifdef PRACTICE_ROM
#include "practice.h"
#include "practice_save_config.h"
#include "ui/osk.h"
#include "ui/file_browser.h"
#include "iodev/iodev.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "slot_manager.h"
#include "sf64thread.h"  /* gAudioThread */
#include "PR/os.h"        /* osStopThread / osStartThread */

/* SD operations run during gameplay (the pause menu). osSyncPrintf calls
 * __osPiGetAccess and runs a cart-bus rp/wp drain dance; with no host draining
 * IS-Viewer each call stalls (200k-retry drain) and, on hardware, contends on
 * PI access with the SD command path -- a confirmed wedge source and a
 * violation of the project rule "never osSyncPrintf during gameplay". Every
 * osSyncPrintf call in this file is gated by PRACTICE_SD_TRACE (default 0,
 * same pattern as PRACTICE_SAVE_TRACE in practice_save.c) so a release build
 * compiles the calls out entirely instead of routing them through a no-op --
 * the format strings and call-site marshalling were pure ROM-byte waste with
 * PRACTICE_SD_TRACE=0 (see check_boot_main_rom_budget). Set PRACTICE_SD_TRACE
 * to 1 locally to re-enable tracing under an IS-Viewer session. */
#ifndef PRACTICE_SD_TRACE
#define PRACTICE_SD_TRACE 0
#endif

/* Audio pause is intentionally a no-op: osStopThread(&gAudioThread) combined
 * with sc64_cart_lock() -> __osPiGetAccess() creates a deadlock — if the audio
 * thread is suspended mid-PI-transaction it holds the PI access token and
 * cart_lock blocks forever. Serialization is handled by cart_lock + PI_WAIT
 * in lib/iodev/iodev_sc64.c instead, which is sufficient with the self-heal
 * DEINIT+INIT clearing any poisoned SC64 controller state. */
static void sd_audio_pause(void)  { }
static void sd_audio_resume(void) { }


/* These are shared const storage (not macros) so every f_mkdir()/path-build
 * call site references the same bytes instead of the compiler emitting a
 * fresh string literal per usage -- IDO does not pool identical literals,
 * and SD_ROOT/SD_APP/SD_DIR were each duplicated 3-6x across this file
 * before this change (see check_boot_main_rom_budget). */
static const char sSdRoot[] = "0:/sageraces";
static const char sSdApp[]  = "0:/sageraces/sf64";
static const char sSdDir[]  = "0:/sageraces/sf64/states";
static const char sSdExt[]  = ".SF64ST";
#define SD_ROOT     sSdRoot
#define SD_APP      sSdApp
#define SD_DIR      sSdDir
#define SD_EXT      sSdExt
#define SD_PATH_MAX (FB_PATH_MAX)

static FATFS sFatfsWork;
static bool sSdAvailable = false;
static char sSavePath[SD_PATH_MAX];
static char sSdStatus[48];
static const char *sNoSdMsg = "NO SD CART";

/* Lazy-mount FatFs before any operation so it discards cached FAT state.
 * We do NOT touch the hardware SD lock here: the card stays acquired from
 * the boot-time iodev_sd_init() call.  Releasing and re-acquiring forces a
 * full SD_OP_INIT sequence on the SC64 which can stall the game thread for
 * several seconds. */
static void sd_op_begin(void) {
    f_mount(&sFatfsWork, "0:", 0);
}

static void sd_op_end(void) {
    f_unmount("0:");
}

/* N64 button -> OSK_BTN_* translation */
static u8 osk_buttons_from_n64(void) {
    OSContPad* press = &gControllerPress[gMainController];
    u8 b = 0;
    if (press->button & U_JPAD)       b |= OSK_BTN_UP;
    if (press->button & D_JPAD)       b |= OSK_BTN_DOWN;
    if (press->button & L_JPAD)       b |= OSK_BTN_LEFT;
    if (press->button & R_JPAD)       b |= OSK_BTN_RIGHT;
    if (press->button & A_BUTTON)     b |= OSK_BTN_A;
    if (press->button & B_BUTTON)     b |= OSK_BTN_B;
    if (press->button & START_BUTTON) b |= OSK_BTN_START;
    if (press->button & Z_TRIG)       b |= OSK_BTN_Z;
    return b;
}

static void on_save_name_confirmed(const char *name, void *ud) {
    int res, j;
    int i = 0;
    FRESULT rr, ra, rd;
    int iodev_res;
    (void)ud;

    for (j = 0; SD_DIR[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_DIR[j]; }
    if (i < SD_PATH_MAX - 1) { sSavePath[i++] = '/'; }
    for (j = 0; name[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = name[j]; }
    for (j = 0; SD_EXT[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_EXT[j]; }
    sSavePath[i] = '\0';
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] save path=%s\n", sSavePath);
#endif

    /* SD self-heal: DEINIT+INIT clears any poisoned controller state from prior
     * host sd-upload runs. Audio pause held across the burst for a quiet cart-bus. */
    sd_audio_pause();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] release<\n");
#endif
    (void)iodev_sd_release();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] acquire<\n");
#endif
    iodev_res = iodev_sd_acquire();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] acquire> r=%d\n", iodev_res);
#endif
    sd_op_begin();
    rr = f_mkdir(SD_ROOT);
    ra = f_mkdir(SD_APP);
    rd = f_mkdir(SD_DIR);
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] mkdir root=%d app=%d dir=%d (0=ok 8=exist)\n",
                 (int)rr, (int)ra, (int)rd);
#endif
    res = slot_manager_save_sd_named(sSavePath);
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] slot_save=%d\n", res);
#endif
    sd_op_end();
    sd_audio_resume();

    Practice_Menu_Close();
    if (res == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SD SAVE OK", 80, 255, 120);
    } else {
        const char *msg;
        switch (res) {
            case SLOT_MANAGER_ERR_PARAM:       msg = "SD ERR PARAM";   break;
            case SLOT_MANAGER_ERR_NO_STORAGE:  msg = "SD NO SCRATCH";  break;
            case SLOT_MANAGER_ERR_OVERFLOW:    msg = "SD OVERFLOW";    break;
            case SLOT_MANAGER_ERR_IO_OPEN:
                sprintf(sSdStatus, "SD OPEN FAIL %d", slot_manager_last_fatfs_err());
                msg = sSdStatus;
                break;
            case SLOT_MANAGER_ERR_IO_WRITE:
                /* If a metadata verify failed, show what read back vs what we
                 * wrote: O=first diff offset, W=wrote, R=read, S=read at the
                 * swapped byte. R:0=not persisted, R:255=erased, S==W=byteswap,
                 * else corruption. */
                if (disk_last_verify_off() >= 0) {
                    sprintf(sSdStatus, "WR%d O%d W%d R%d S%d",
                            slot_manager_last_fatfs_err(),
                            disk_last_verify_off(), disk_last_verify_wrote(),
                            disk_last_verify_read(), disk_last_verify_swap());
                } else {
                    sprintf(sSdStatus, "SD WRITE FAIL %d", slot_manager_last_fatfs_err());
                }
                msg = sSdStatus;
                break;
            case SLOT_MANAGER_ERR_IO_RENAME:
                /* Code = unlink_FRESULT*10 + rename_FRESULT (see slot_manager.c).
                 * e.g. 41 = dest absent + rename DISK_ERR; X7 = rename DENIED. */
                sprintf(sSdStatus, "SD RENAME FAIL %d", slot_manager_last_fatfs_err());
                msg = sSdStatus;
                break;
            default:
                sprintf(sSdStatus, "SD ERR %d", res);
                msg = sSdStatus;
                break;
        }
        Practice_Hud_ShowStatus(msg, 255, 120, 80);
    }
}

static void on_save_canceled(void *ud) {
    (void)ud;
    Practice_Hud_ShowStatus("SD CANCEL", 160, 160, 160);
}

static void on_load_file_selected(const char *path, void *ud) {
    int res;
    const char *msg;
    int iodev_res;
    (void)ud;
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] load path=%s\n", path);
#endif
    sd_audio_pause();
    /* SD self-heal: DEINIT+INIT clears any poisoned controller state. */
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] release<\n");
#endif
    (void)iodev_sd_release();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] acquire<\n");
#endif
    iodev_res = iodev_sd_acquire();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] acquire> r=%d\n", iodev_res);
#endif
    sd_op_begin();
    res = slot_manager_load_sd_named(path);
    sd_op_end();
    sd_audio_resume();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] slot_load=%d\n", res);
#endif
    Practice_Menu_Close();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] menu_closed\n");
#endif
    if (res == SLOT_MANAGER_OK) {
        if (Practice_Sd_LoadIsPending()) {
            Practice_Hud_ShowStatus("XSCENE WAIT", 220, 220, 80);
        } else if (Practice_Save_LastLoadWasXBuild()) {
            Practice_Hud_ShowStatus("SD XBLD OK", 220, 220, 80);
        } else {
            Practice_Hud_ShowStatus("SD LOAD OK", 80, 255, 120);
        }
#if PRACTICE_SD_TRACE
        osSyncPrintf("[sd] load_cb done pending=%d\n", (s32)Practice_Sd_LoadIsPending());
#endif
        return;
    }
    switch (res) {
        case SLOT_MANAGER_ERR_PARAM:       msg = "LOAD ERR PARAM";  break;
        case SLOT_MANAGER_ERR_NO_STORAGE:  msg = "LOAD NO SCRATCH"; break;
        case SLOT_MANAGER_ERR_IO_OPEN:     msg = "LOAD OPEN FAIL";  break;
        case SLOT_MANAGER_ERR_IO_READ:     msg = "LOAD READ FAIL";  break;
        case SLOT_MANAGER_ERR_CORRUPT:     msg = "LOAD CORRUPT";    break;
        case SLOT_MANAGER_ERR_MAGIC:       msg = "LOAD BAD MAGIC";  break;
        case SLOT_MANAGER_ERR_VERSION:     msg = "LOAD VERSION";    break;
        case SLOT_MANAGER_ERR_CALLBACK:    msg = "LOAD CB FAIL";    break;
        default:
            sprintf(sSdStatus, "LOAD ERR %d", res);
            msg = sSdStatus;
            break;
    }
    Practice_Hud_ShowStatus(msg, 255, 120, 80);
}

static void on_load_canceled(void *ud) {
    (void)ud;
    Practice_Hud_ShowStatus("SD CANCEL", 160, 160, 160);
}

void Practice_Sd_Init(void) {
    FRESULT r;
    osk_close();
    file_browser_close();
    sSdAvailable = iodev_sd_was_ok();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[si] ok=%d\n", (s32)sSdAvailable);
#endif
    if (!sSdAvailable) {
        iodev_id_t cart = iodev_detect();
        int res = iodev_sd_init_result();
        if (cart == IODEV_ED64) {
            sprintf(sSdStatus, "ED SD ERR %d", res);
            sNoSdMsg = sSdStatus;
        } else if (cart == IODEV_SC64) {
            sprintf(sSdStatus, "SC SD ERR %d", res);
            sNoSdMsg = sSdStatus;
        } else {
            sprintf(sSdStatus, "NO SD ID %04X",
                    (unsigned int)((iodev_ed64_raw_edid() >> 16) & 0xFFFFu));
            sNoSdMsg = sSdStatus;
        }
    }
    if (sSdAvailable) {
        sd_op_begin();
        (void)f_mkdir(SD_ROOT);
        (void)f_mkdir(SD_APP);
        r = f_mkdir(SD_DIR);
        (void)r;
        sd_op_end();
    }
    slot_manager_set_sd_scratch(Practice_Save_ScratchBase(), MAX_STATE_SIZE);
}

bool Practice_Sd_IsActive(void) {
    return osk_is_open() || file_browser_is_open();
}

/* Lazy SD init for first-use. iodev_sd_init() is deferred from boot to here
 * to avoid the cold-boot SC64 firmware wedge — calling it at boot races
 * with the audio thread's first soundbank DMAs and wedges the SC64
 * firmware (red LED stuck on). By the time the user manually triggers a
 * save/load, PI traffic is quiet and the race is gone. Idempotent: returns
 * immediately if iodev_sd_init has been attempted before (cached result
 * via sIodevSdInitResult sentinel). On success creates the SD_ROOT /
 * SD_APP / SD_DIR directory tree (previously done at boot). On failure
 * leaves sNoSdMsg as Practice_Sd_Init's boot-time error message. */
static void Practice_Sd_LazyInit(void) {
    FRESULT r;
    int initRes;
#if PRACTICE_SD_TRACE
    osSyncPrintf("[lz] in av=%d r=%d\n",
                 (s32)sSdAvailable, iodev_sd_init_result());
#endif
    if (sSdAvailable || iodev_sd_init_result() != -99) {
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] skip\n");
#endif
        return;
    }
    sd_audio_pause();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[lz] init<\n");
#endif
    (void)iodev_sd_init();
    initRes = iodev_sd_init_result();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[lz] init> r=%d\n", initRes);
#endif
    sSdAvailable = iodev_sd_was_ok();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[lz] ok=%d\n", (s32)sSdAvailable);
#endif
    if (sSdAvailable) {
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] mnt<\n");
#endif
        f_mount(&sFatfsWork, "0:", 0);
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] mnt>\n");
#endif
        r = f_mkdir(SD_ROOT);
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] mk1=%d\n", (s32)r);
#endif
        r = f_mkdir(SD_APP);
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] mk2=%d\n", (s32)r);
#endif
        r = f_mkdir(SD_DIR);
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] mk3=%d\n", (s32)r);
#endif
        f_unmount("0:");
#if PRACTICE_SD_TRACE
        osSyncPrintf("[lz] umnt\n");
#endif
    } else {
        iodev_id_t cart = iodev_detect();
        if (cart == IODEV_ED64) {
            sprintf(sSdStatus, "ED SD ERR %d", initRes);
            sNoSdMsg = sSdStatus;
        } else if (cart == IODEV_SC64) {
            sprintf(sSdStatus, "SC SD ERR %d", initRes);
            sNoSdMsg = sSdStatus;
        }
    }
    sd_audio_resume();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[lz] out av=%d\n", (s32)sSdAvailable);
#endif
}

void Practice_Sd_StartSave(void) {
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] sv<\n");
#endif
    Practice_Sd_LazyInit();
    if (!sSdAvailable || gPracticeSaveDisabled) {
#if PRACTICE_SD_TRACE
        osSyncPrintf("[sd] sv abort av=%d dis=%d\n",
                     (s32)sSdAvailable, (s32)gPracticeSaveDisabled);
#endif
        Practice_Hud_ShowStatus(sNoSdMsg, 255, 180, 80);
        return;
    }
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] sv osk\n");
#endif
    osk_open("SD SAVE NAME:", "", OSK_MAX_TEXT,
              on_save_name_confirmed, on_save_canceled, NULL);
}

void Practice_Sd_StartLoad(void) {
    int r;
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] ld<\n");
#endif
    Practice_Sd_LazyInit();
    if (!sSdAvailable || gPracticeSaveDisabled) {
#if PRACTICE_SD_TRACE
        osSyncPrintf("[sd] ld abort av=%d dis=%d\n",
                     (s32)sSdAvailable, (s32)gPracticeSaveDisabled);
#endif
        Practice_Hud_ShowStatus(sNoSdMsg, 255, 180, 80);
        return;
    }
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] ld mnt<\n");
#endif
    sd_audio_pause();
    sd_op_begin();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] ld fb<\n");
#endif
    r = file_browser_open(FB_LOAD, SD_DIR, SD_EXT,
                          on_load_file_selected, on_load_canceled, NULL);
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] ld fb> r=%d\n", r);
#endif
    sd_op_end();
    sd_audio_resume();
#if PRACTICE_SD_TRACE
    osSyncPrintf("[sd] load dir=%s r=%d count=%d\n",
                 SD_DIR, r, (int)gFileBrowser.count);
#endif
    if (r != 0) {
        Practice_Hud_ShowStatus("NO SD SAVES", 180, 180, 80);
    }
}

void Practice_Sd_Update(void) {
    u8 pressed = osk_buttons_from_n64();
    if (osk_is_open()) {
        osk_update(pressed);
    } else if (file_browser_is_open()) {
        file_browser_update(pressed);
    }
}

/* OSK rendering */
#define OSK_X0 140
#define OSK_Y0 80   /* grid top; box = Y0-40, bottom = Y0 + rows*CH + ~8 */
#define OSK_CW 16
#define OSK_CH 14

static void draw_osk(void) {
    int col, row;
    s32 bx    = OSK_X0 - 4;
    s32 by    = OSK_Y0 - 40;
    s32 bw    = OSK_GRID_COLS * OSK_CW + 8;
    s32 bh    = OSK_GRID_ROWS * OSK_CH + 44; /* 40 header + grid + 4 pad; grid ends at by+124 */
    s32 pillY = by + bh + 4;

    Practice_DrawBox(bx, by, bw, bh, 0, 0, 0, 210);
    Practice_DrawTextColor(OSK_X0, by + 4, gOsk.prompt, 0, 255, 128);
    Practice_DrawText(OSK_X0, by + 16, gOsk.text);

    for (row = 0; row < OSK_GRID_ROWS; row++) {
        for (col = 0; col < OSK_GRID_COLS; col++) {
            char ch = osk_char_at(col, row);
            s32 cx = OSK_X0 + col * OSK_CW;
            s32 cy = OSK_Y0 + row * OSK_CH;
            bool selected = (col == gOsk.cursor_col && row == gOsk.cursor_row);

            if (selected) {
                Practice_DrawBox(cx - 1, cy - 1, OSK_CW, OSK_CH, 255, 220, 80, 180);
            }
            if (ch == OSK_CHAR_DEL) {
                Practice_DrawTextColor(cx, cy, "DEL", selected ? 0 : 200, 80, 80);
            } else if (ch == OSK_CHAR_OK) {
                Practice_DrawTextColor(cx, cy, "OK", 80, selected ? 0 : 200, 80);
            } else if (ch != OSK_CHAR_NONE) {
                char tmp[2];
                tmp[0] = ch;
                tmp[1] = '\0';
                Practice_DrawText(cx, cy, tmp);
            }
        }
    }

    Practice_DrawButtonPill(20,  pillY, 10, "A",     0, 100, 220);
    Practice_DrawTextColor( 32,  pillY + 1, ":SEL  ",   150, 150, 150);
    Practice_DrawButtonPill(74,  pillY, 10, "B",     0, 160,   0);
    Practice_DrawTextColor( 86,  pillY + 1, ":DEL  ",   150, 150, 150);
    Practice_DrawButtonPill(128, pillY, 10, "S",     200,  30,  30);
    Practice_DrawTextColor( 140, pillY + 1, ":OK  ",    150, 150, 150);
    Practice_DrawButtonPill(209, pillY, 10, "Z",     100, 100, 100);
    Practice_DrawTextColor( 221, pillY + 1, ":CANCEL",  150, 150, 150);
}

/* File browser rendering */
#define FB_X0    20
#define FB_Y0    60   /* row top; box = Y0-20 */
#define FB_ROW_H 10

static void draw_file_browser(void) {
    int i;
    s32 bx    = FB_X0 - 4;
    s32 by    = FB_Y0 - 20;
    s32 bw    = 280;
    s32 bh    = FB_VISIBLE_ROWS * FB_ROW_H + 22; /* rows fit at by+20 to by+162; 2px pad */
    s32 pillY = by + bh + 4;

    Practice_DrawBox(bx, by, bw, bh, 0, 0, 0, 210);
    Practice_DrawTextColor(FB_X0, by + 4, "LOAD FROM SD:", 0, 255, 128);

    if (gFileBrowser.count == 0) {
        Practice_DrawTextColor(FB_X0, FB_Y0 + 4, "NO FILES FOUND", 160, 160, 160);
    } else {
        for (i = 0; i < FB_VISIBLE_ROWS && (gFileBrowser.scroll + i) < gFileBrowser.count; i++) {
            int entry = gFileBrowser.scroll + i;
            s32 y = FB_Y0 + i * FB_ROW_H;
            bool sel = (entry == gFileBrowser.cursor);
            if (sel) {
                Practice_DrawBox(FB_X0 - 2, y - 1, bw - 4, FB_ROW_H, 255, 220, 80, 150);
            }
            Practice_DrawText(FB_X0, y, gFileBrowser.names[entry]);
        }
    }

    Practice_DrawButtonPill(20, pillY, 10, "A",  0, 100, 220);
    Practice_DrawTextColor( 32, pillY + 1, ":SELECT  ", 150, 150, 150);
    Practice_DrawButtonPill(96, pillY, 10, "B",  0, 160,   0);
    Practice_DrawTextColor(108, pillY + 1, ":CANCEL",   150, 150, 150);
}

void Practice_Sd_Draw(void) {
    if (osk_is_open()) {
        draw_osk();
    } else if (file_browser_is_open()) {
        draw_file_browser();
    }
}

#endif /* PRACTICE_ROM */
