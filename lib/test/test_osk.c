/* lib/test/test_osk.c */
#define PRACTICE_ROM 1
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ui/osk.h"

static const char *sLastConfirm = NULL;
static int sCancelCount = 0;

static void on_confirm(const char *text, void *ud) { sLastConfirm = text; (void)ud; }
static void on_cancel(void *ud) { sCancelCount++; (void)ud; }

static void test_type_and_confirm(void) {
    osk_open("NAME?", "", 8, on_confirm, on_cancel, NULL);
    assert(osk_is_open());
    /* Select 'A' (row 0, col 0) */
    osk_update(OSK_BTN_A);
    assert(strcmp(gOsk.text, "A") == 0);
    /* Move right to 'B', select */
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_A);
    assert(strcmp(gOsk.text, "AB") == 0);
    /* Backspace */
    osk_update(OSK_BTN_B);
    assert(strcmp(gOsk.text, "A") == 0);
    /* Confirm via START */
    sLastConfirm = NULL;
    osk_update(OSK_BTN_START);
    assert(!osk_is_open());
    assert(sLastConfirm != NULL && strcmp(sLastConfirm, "A") == 0);
    printf("  test_type_and_confirm: PASS\n");
}

static void test_cancel(void) {
    sCancelCount = 0;
    osk_open("NAME?", "HELLO", 8, on_confirm, on_cancel, NULL);
    assert(osk_is_open());
    osk_update(OSK_BTN_Z);
    assert(!osk_is_open());
    assert(sCancelCount == 1);
    printf("  test_cancel: PASS\n");
}

static void test_max_len(void) {
    int i;
    osk_open("NAME?", "", 3, on_confirm, on_cancel, NULL);
    /* Type 5 chars — only 3 should be accepted */
    for (i = 0; i < 5; i++) osk_update(OSK_BTN_A);  /* 'A' at (0,0) */
    assert(gOsk.text_len == 3);
    osk_close();
    printf("  test_max_len: PASS\n");
}

static void test_default_text(void) {
    osk_open("NAME?", "SAVE1", 8, on_confirm, on_cancel, NULL);
    assert(strcmp(gOsk.text, "SAVE1") == 0);
    assert(gOsk.text_len == 5);
    osk_close();
    printf("  test_default_text: PASS\n");
}

static void test_del_cell(void) {
    /* Navigate to DEL cell (row 5, col 6) and press A */
    osk_open("NAME?", "AB", 8, on_confirm, on_cancel, NULL);
    /* Move to row 5 */
    osk_update(OSK_BTN_DOWN);
    osk_update(OSK_BTN_DOWN);
    osk_update(OSK_BTN_DOWN);
    osk_update(OSK_BTN_DOWN);
    osk_update(OSK_BTN_DOWN);
    /* Move to col 6 */
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_RIGHT);
    osk_update(OSK_BTN_RIGHT);
    /* Press A on DEL */
    osk_update(OSK_BTN_A);
    assert(strcmp(gOsk.text, "A") == 0);
    osk_close();
    printf("  test_del_cell: PASS\n");
}

static void test_empty_confirm_blocked(void) {
    /* Confirm with empty text should be blocked — OSK stays open */
    sLastConfirm = NULL;
    osk_open("NAME?", "", 8, on_confirm, on_cancel, NULL);
    osk_update(OSK_BTN_START);
    assert(osk_is_open());  /* must stay open */
    assert(sLastConfirm == NULL);
    osk_close();
    printf("  test_empty_confirm_blocked: PASS\n");
}

int main(void) {
    printf("test_osk:\n");
    test_type_and_confirm();
    test_cancel();
    test_max_len();
    test_default_text();
    test_del_cell();
    test_empty_confirm_blocked();
    printf("All osk tests PASS.\n");
    return 0;
}
