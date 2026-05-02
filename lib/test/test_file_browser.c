/* lib/test/test_file_browser.c
 * Host unit tests for the SD file picker state machine.
 *
 * file_browser_open() is NOT called here — FatFs diskio returns RES_ERROR
 * on the host. Instead, inject_entries() populates gFileBrowser directly
 * to exercise the navigation / selection / cancel state machine. */
#define PRACTICE_ROM 1
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ui/file_browser.h"

static char sLastPath[128];
static int  sLastPathSet = 0;
static int  sCancelCount = 0;

static void on_cb(const char *path, void *ud) {
    strncpy(sLastPath, path, sizeof(sLastPath) - 1);
    sLastPath[sizeof(sLastPath) - 1] = '\0';
    sLastPathSet = 1;
    (void)ud;
}
static void on_cancel(void *ud) { sCancelCount++; (void)ud; }

static void inject_entries(int n) {
    int i;
    gFileBrowser.count = 0;
    for (i = 0; i < n && i < FB_MAX_ENTRIES; i++) {
        snprintf(gFileBrowser.names[i], FB_NAME_MAX, "SAVE%02d.SF64ST", i);
        gFileBrowser.count++;
    }
    gFileBrowser.open     = true;
    gFileBrowser.cursor   = 0;
    gFileBrowser.scroll   = 0;
    gFileBrowser.callback = on_cb;
    gFileBrowser.cancel   = on_cancel;
    gFileBrowser.ud       = NULL;
    strncpy(gFileBrowser.dir, "/sf64-practice/states", FB_PATH_MAX - 1);
    gFileBrowser.dir[FB_PATH_MAX - 1] = '\0';
}

/* Move cursor down once, then press A to select the second entry. */
static void test_navigate_and_select(void) {
    inject_entries(3);
    file_browser_update(OSK_BTN_DOWN);
    assert(gFileBrowser.cursor == 1);
    sLastPathSet = 0;
    sLastPath[0] = '\0';
    file_browser_update(OSK_BTN_A);
    assert(!file_browser_is_open());
    assert(sLastPathSet == 1);
    assert(strstr(sLastPath, "SAVE01.SF64ST") != NULL);
    printf("  test_navigate_and_select: PASS\n");
}

/* Press Z (cancel) — browser closes and cancel callback fires once. */
static void test_cancel(void) {
    sCancelCount = 0;
    inject_entries(2);
    file_browser_update(OSK_BTN_Z);
    assert(!file_browser_is_open());
    assert(sCancelCount == 1);
    printf("  test_cancel: PASS\n");
}

/* Navigate past the visible window — scroll must advance. */
static void test_scroll(void) {
    int i;
    inject_entries(FB_VISIBLE_ROWS + 4);
    for (i = 0; i < FB_VISIBLE_ROWS + 2; i++) file_browser_update(OSK_BTN_DOWN);
    assert(gFileBrowser.scroll > 0);
    assert(gFileBrowser.cursor == FB_VISIBLE_ROWS + 2);
    file_browser_close();
    printf("  test_scroll: PASS\n");
}

/* Pressing A on an empty list must not close or fire the callback. */
static void test_empty_list(void) {
    inject_entries(0);
    sLastPathSet = 0;
    sLastPath[0] = '\0';
    file_browser_update(OSK_BTN_A);
    assert(file_browser_is_open());
    assert(sLastPathSet == 0);
    file_browser_close();
    printf("  test_empty_list: PASS\n");
}

int main(void) {
    printf("test_file_browser:\n");
    test_navigate_and_select();
    test_cancel();
    test_scroll();
    test_empty_list();
    printf("All file_browser tests PASS.\n");
    return 0;
}
