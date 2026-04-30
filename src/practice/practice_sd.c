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
#include "slot_manager.h"

#define SD_ROOT     "/sageraces"
#define SD_APP      SD_ROOT "/sf64"
#define SD_DIR      SD_APP  "/states"
#define SD_EXT      ".SF64ST"
#define SD_PATH_MAX (FB_PATH_MAX)

static FATFS sFatfsWork;
static bool sSdAvailable = false;
static char sSavePath[SD_PATH_MAX];

/* Acquire the SC64 SD hardware lock before any FatFs operation and force
 * a lazy re-mount so FatFs discards its cached FAT state (host may have
 * modified the card while the lock was released). Pair with sd_op_end(). */
static void sd_op_begin(void) {
    iodev_sd_acquire();
    f_mount(&sFatfsWork, "", 0);
}

static void sd_op_end(void) {
    iodev_sd_release();
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
    int res, i, j;
    (void)ud;
    i = 0;
    for (j = 0; SD_DIR[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_DIR[j]; }
    if (i < SD_PATH_MAX - 1) { sSavePath[i++] = '/'; }
    for (j = 0; name[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = name[j]; }
    for (j = 0; SD_EXT[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_EXT[j]; }
    sSavePath[i] = '\0';

    sd_op_begin();
    res = slot_manager_save_sd_named(sSavePath);
    sd_op_end();
    if (res == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SD SAVE OK", 80, 255, 120);
    } else {
        Practice_Hud_ShowStatus("SD SAVE FAIL", 255, 120, 80);
    }
}

static void on_save_canceled(void *ud) {
    (void)ud;
    Practice_Hud_ShowStatus("SD CANCEL", 160, 160, 160);
}

static void on_load_file_selected(const char *path, void *ud) {
    int res;
    (void)ud;
    sd_op_begin();
    res = slot_manager_load_sd_named(path);
    sd_op_end();
    if (res == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SD LOAD OK", 80, 255, 120);
    } else {
        Practice_Hud_ShowStatus("SD LOAD FAIL", 255, 120, 80);
    }
}

static void on_load_canceled(void *ud) {
    (void)ud;
    Practice_Hud_ShowStatus("SD CANCEL", 160, 160, 160);
}

void Practice_Sd_Init(void) {
    /* Explicitly close OSK and file browser in case BSS zero-init didn't
     * cover the Phase 6 globals (osk.o/file_browser.o added late to BSS). */
    osk_close();
    file_browser_close();

    /* iodev_sd_init() was already called in Practice_Init(); use the cached
     * result so we don't re-issue SC64_CMD_SD_CARD_OP which can stall ~6s. */
    sSdAvailable = iodev_sd_was_ok();
    if (sSdAvailable) {
        f_mount(&sFatfsWork, "", 1);
        f_mkdir(SD_ROOT);
        f_mkdir(SD_APP);
        f_mkdir(SD_DIR);
        /* Release the SD lock so the host (sc64deployer / WebDAV) can access
         * the card while the ROM is idle. Each save/load re-acquires it. */
        iodev_sd_release();
    }
    slot_manager_set_sd_scratch(Practice_Save_ScratchBase(), MAX_STATE_SIZE);
}

bool Practice_Sd_IsActive(void) {
    return osk_is_open() || file_browser_is_open();
}

void Practice_Sd_StartSave(void) {
    if (!sSdAvailable || gPracticeSaveDisabled) {
        Practice_Hud_ShowStatus("NO SD CART", 255, 180, 80);
        return;
    }
    osk_open("SD SAVE NAME:", "", OSK_MAX_TEXT,
              on_save_name_confirmed, on_save_canceled, NULL);
}

void Practice_Sd_StartLoad(void) {
    int r;
    if (!sSdAvailable || gPracticeSaveDisabled) {
        Practice_Hud_ShowStatus("NO SD CART", 255, 180, 80);
        return;
    }
    /* Acquire SD lock for directory listing, then release immediately after.
     * file_browser_open() reads all entries into RAM; no SD needed during
     * user navigation. The selection callback re-acquires for the file read. */
    sd_op_begin();
    r = file_browser_open(FB_LOAD, SD_DIR, SD_EXT,
                          on_load_file_selected, on_load_canceled, NULL);
    sd_op_end();
    if (r != 0) {
        Practice_Hud_ShowStatus("SD OPEN ERR", 255, 120, 80);
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
#define OSK_X0 20
#define OSK_Y0 64   /* grid top; -20 original put prompt in TV overscan */
#define OSK_CW 12
#define OSK_CH 10

static void draw_osk(void) {
    int col, row;
    s32 bx = OSK_X0 - 4;
    s32 by = OSK_Y0 - 34;
    s32 bw = OSK_GRID_COLS * OSK_CW + 8;
    s32 bh = OSK_GRID_ROWS * OSK_CH + 30;

    Practice_DrawBox(bx, by, bw, bh, 0, 0, 0, 210);
    Practice_DrawTextColor(OSK_X0, by + 4, gOsk.prompt, 0, 255, 128);
    Practice_DrawText(OSK_X0, by + 14, gOsk.text);

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
    Practice_DrawTextColor(OSK_X0, by + bh - 8, "A:SELECT B:DEL START:OK Z:CANCEL", 120, 120, 120);
}

/* File browser rendering */
#define FB_X0    20
#define FB_Y0    40   /* row top; -12 original put header in TV overscan */
#define FB_ROW_H 10

static void draw_file_browser(void) {
    int i;
    s32 bx = FB_X0 - 4;
    s32 by = FB_Y0 - 20;
    s32 bw = 280;
    s32 bh = FB_VISIBLE_ROWS * FB_ROW_H + 24;

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
    Practice_DrawTextColor(FB_X0, by + bh - 8, "A:SELECT B:CANCEL", 120, 120, 120);
}

void Practice_Sd_Draw(void) {
    if (osk_is_open()) {
        draw_osk();
    } else if (file_browser_is_open()) {
        draw_file_browser();
    }
}

#endif /* PRACTICE_ROM */
