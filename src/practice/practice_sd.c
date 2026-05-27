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

#define SD_ROOT     "0:/sageraces"
#define SD_APP      SD_ROOT "/sf64"
#define SD_DIR      SD_APP  "/states"
#define SD_EXT      ".SF64ST"
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
    FRESULT st1, st2, st3;
    FILINFO finfo;
    (void)ud;

    for (j = 0; SD_DIR[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_DIR[j]; }
    if (i < SD_PATH_MAX - 1) { sSavePath[i++] = '/'; }
    for (j = 0; name[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = name[j]; }
    for (j = 0; SD_EXT[j] && i < SD_PATH_MAX - 1; j++) { sSavePath[i++] = SD_EXT[j]; }
    sSavePath[i] = '\0';
    osSyncPrintf("[sd] save path=%s\n", sSavePath);

    /* Single acquire+lazy-mount like the load path; no intermediate remounts. */
    sd_op_begin();
    rr = f_mkdir(SD_ROOT);
    ra = f_mkdir(SD_APP);
    rd = f_mkdir(SD_DIR);
    osSyncPrintf("[sd] mkdir root=%d app=%d dir=%d (0=ok 8=exist)\n",
                 (int)rr, (int)ra, (int)rd);
    st1 = f_stat("0:/sageraces", &finfo);
    st2 = f_stat("0:/sageraces/sf64", &finfo);
    st3 = f_stat("0:/sageraces/sf64/states", &finfo);
    osSyncPrintf("[sd] stat root=%d app=%d dir=%d\n", (int)st1, (int)st2, (int)st3);

    res = slot_manager_save_sd_named(sSavePath);
    osSyncPrintf("[sd] slot_save=%d\n", res);
    sd_op_end();

    Practice_Menu_Close();
    if (res == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SD SAVE OK", 80, 255, 120);
    } else {
        const char *msg;
        switch (res) {
            case SLOT_MANAGER_ERR_PARAM:       msg = "SD ERR PARAM";   break;
            case SLOT_MANAGER_ERR_NO_STORAGE:  msg = "SD NO SCRATCH";  break;
            case SLOT_MANAGER_ERR_OVERFLOW:    msg = "SD OVERFLOW";    break;
            case SLOT_MANAGER_ERR_IO_OPEN:     msg = "SD OPEN FAIL";   break;
            case SLOT_MANAGER_ERR_IO_WRITE:    msg = "SD WRITE FAIL";  break;
            case SLOT_MANAGER_ERR_IO_RENAME:   msg = "SD RENAME FAIL"; break;
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
    (void)ud;
    osSyncPrintf("[sd] load path=%s\n", path);
    sd_op_begin();
    res = slot_manager_load_sd_named(path);
    sd_op_end();
    osSyncPrintf("[sd] slot_load=%d\n", res);
    Practice_Menu_Close();
    osSyncPrintf("[sd] menu_closed\n");
    if (res == SLOT_MANAGER_OK) {
        if (Practice_Sd_LoadIsPending()) {
            Practice_Hud_ShowStatus("XSCENE WAIT", 220, 220, 80);
        } else if (Practice_Save_LastLoadWasXBuild()) {
            Practice_Hud_ShowStatus("SD XBLD OK", 220, 220, 80);
        } else {
            Practice_Hud_ShowStatus("SD LOAD OK", 80, 255, 120);
        }
        osSyncPrintf("[sd] load_cb done pending=%d\n", (s32)Practice_Sd_LoadIsPending());
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
#ifdef PRACTICE_BOOT_BREADCRUMBS
    /* SD1 PINK: Practice_Sd_Init entered. */
    Lib_DebugFillScreen(0xFB1F);
#endif
    osSyncPrintf("[sd_init] osk_close enter\n");
    osk_close();
    osSyncPrintf("[sd_init] osk_close exit; file_browser_close enter\n");
    file_browser_close();
    osSyncPrintf("[sd_init] file_browser_close exit; iodev_sd_was_ok enter\n");
    sSdAvailable = iodev_sd_was_ok();
    osSyncPrintf("[sd_init] iodev_sd_was_ok=%d\n", (s32) sSdAvailable);
#ifdef PRACTICE_BOOT_BREADCRUMBS
    /* SD2 WHITE: early closes + sSdAvailable check done. */
    Lib_DebugFillScreen(0xFFFF);
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
            /* IODEV_NONE: show raw EDID upper 16 bits for hardware diagnosis.
             * 0xED64 = magic present but wrong BITLEN/detection bug.
             * 0x0000 = registers locked or PI write dropped.
             * 0xFFFF = open bus (cart not present / address wrong). */
            sprintf(sSdStatus, "NO SD ID %04X",
                    (unsigned int)((iodev_ed64_raw_edid() >> 16) & 0xFFFFu));
            sNoSdMsg = sSdStatus;
        }
    }
    if (sSdAvailable) {
        /* Create the save directory tree at ROM boot so the first save never
         * stalls on fresh directory allocation mid-game. */
        osSyncPrintf("[sd_init] sd_op_begin (f_mount) enter\n");
        sd_op_begin();
        osSyncPrintf("[sd_init] sd_op_begin exit; f_mkdir SD_ROOT enter\n");
#ifdef PRACTICE_BOOT_BREADCRUMBS
        /* SD3 BROWN: sd_op_begin (f_mount) returned. */
        Lib_DebugFillScreen(0x8001);
#endif
        r = f_mkdir(SD_ROOT);
        osSyncPrintf("[sd_init] f_mkdir SD_ROOT r=%d; f_mkdir SD_APP enter\n", (s32) r);
#ifdef PRACTICE_BOOT_BREADCRUMBS
        /* SD4 OLIVE: first f_mkdir(SD_ROOT) returned. */
        Lib_DebugFillScreen(0x8401);
#endif
        r = f_mkdir(SD_APP);
        osSyncPrintf("[sd_init] f_mkdir SD_APP r=%d; f_mkdir SD_DIR enter\n", (s32) r);
        r = f_mkdir(SD_DIR);
        osSyncPrintf("[sd_init] f_mkdir SD_DIR r=%d; sd_op_end enter\n", (s32) r);
        sd_op_end();
        osSyncPrintf("[sd_init] sd_op_end exit\n");
    }
    osSyncPrintf("[sd_init] slot_manager_set_sd_scratch enter\n");
    slot_manager_set_sd_scratch(Practice_Save_ScratchBase(), MAX_STATE_SIZE);
    osSyncPrintf("[sd_init] slot_manager_set_sd_scratch exit (Practice_Sd_Init done)\n");
}

bool Practice_Sd_IsActive(void) {
    return osk_is_open() || file_browser_is_open();
}

void Practice_Sd_StartSave(void) {
    if (!sSdAvailable || gPracticeSaveDisabled) {
        Practice_Hud_ShowStatus(sNoSdMsg, 255, 180, 80);
        return;
    }
    osk_open("SD SAVE NAME:", "", OSK_MAX_TEXT,
              on_save_name_confirmed, on_save_canceled, NULL);
}

void Practice_Sd_StartLoad(void) {
    int r;
    if (!sSdAvailable || gPracticeSaveDisabled) {
        Practice_Hud_ShowStatus(sNoSdMsg, 255, 180, 80);
        return;
    }
    sd_op_begin();
    r = file_browser_open(FB_LOAD, SD_DIR, SD_EXT,
                          on_load_file_selected, on_load_canceled, NULL);
    sd_op_end();
    osSyncPrintf("[sd] load dir=%s r=%d count=%d\n",
                 SD_DIR, r, (int)gFileBrowser.count);
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
