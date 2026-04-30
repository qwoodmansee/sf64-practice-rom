/* lib/ui/osk.c */
#ifdef PRACTICE_ROM
#include "osk.h"

osk_state_t gOsk;

/* Char grid: 8 cols x 6 rows. OSK_CHAR_NONE = empty cell. */
static const char sGrid[OSK_GRID_ROWS][OSK_GRID_COLS] = {
    { 'A','B','C','D','E','F','G','H' },
    { 'I','J','K','L','M','N','O','P' },
    { 'Q','R','S','T','U','V','W','X' },
    { 'Y','Z','_','!',':','-','.',OSK_CHAR_NONE },
    { '0','1','2','3','4','5','6','7' },
    { '8','9',OSK_CHAR_NONE,OSK_CHAR_NONE,OSK_CHAR_NONE,OSK_CHAR_NONE,OSK_CHAR_DEL,OSK_CHAR_OK },
};

static void osk_zero_state(void) {
    int i;
    for (i = 0; i <= OSK_MAX_TEXT; i++) gOsk.text[i] = '\0';
    for (i = 0; i < 32; i++) gOsk.prompt[i] = '\0';
    gOsk.text_len = 0;
    gOsk.max_len = OSK_MAX_TEXT;
    gOsk.cursor_col = 0;
    gOsk.cursor_row = 0;
    gOsk.open = false;
    gOsk.confirm = 0;
    gOsk.cancel = 0;
    gOsk.ud = 0;
}

void osk_open(const char *prompt, const char *default_text,
              uint8_t max_len, osk_confirm_fn confirm, osk_cancel_fn cancel, void *ud) {
    int i;
    osk_zero_state();
    if (max_len == 0 || max_len > OSK_MAX_TEXT) max_len = OSK_MAX_TEXT;
    gOsk.max_len = max_len;
    gOsk.confirm = confirm;
    gOsk.cancel  = cancel;
    gOsk.ud      = ud;
    for (i = 0; prompt && prompt[i] && i < 31; i++) gOsk.prompt[i] = prompt[i];
    gOsk.prompt[i] = '\0';
    if (default_text) {
        for (i = 0; default_text[i] && i < (int)max_len; i++) gOsk.text[i] = default_text[i];
        gOsk.text_len = (uint8_t)i;
        gOsk.text[i] = '\0';
    }
    gOsk.open = true;
}

bool osk_is_open(void) { return gOsk.open; }

void osk_close(void) { gOsk.open = false; }

char osk_char_at(int col, int row) {
    if (col < 0 || col >= OSK_GRID_COLS || row < 0 || row >= OSK_GRID_ROWS) return OSK_CHAR_NONE;
    return sGrid[row][col];
}

void osk_update(uint8_t pressed) {
    char ch;
    if (!gOsk.open) return;

    /* Navigation */
    if (pressed & OSK_BTN_UP) {
        if (gOsk.cursor_row > 0) gOsk.cursor_row--;
        else gOsk.cursor_row = OSK_GRID_ROWS - 1;
    }
    if (pressed & OSK_BTN_DOWN) {
        if (gOsk.cursor_row < OSK_GRID_ROWS - 1) gOsk.cursor_row++;
        else gOsk.cursor_row = 0;
    }
    if (pressed & OSK_BTN_LEFT) {
        if (gOsk.cursor_col > 0) gOsk.cursor_col--;
        else gOsk.cursor_col = OSK_GRID_COLS - 1;
    }
    if (pressed & OSK_BTN_RIGHT) {
        if (gOsk.cursor_col < OSK_GRID_COLS - 1) gOsk.cursor_col++;
        else gOsk.cursor_col = 0;
    }

    /* B = backspace */
    if (pressed & OSK_BTN_B) {
        if (gOsk.text_len > 0) {
            gOsk.text_len--;
            gOsk.text[gOsk.text_len] = '\0';
        }
        return;
    }

    /* START = confirm */
    if (pressed & OSK_BTN_START) {
        gOsk.open = false;
        if (gOsk.confirm) gOsk.confirm(gOsk.text, gOsk.ud);
        return;
    }

    /* Z = cancel */
    if (pressed & OSK_BTN_Z) {
        gOsk.open = false;
        if (gOsk.cancel) gOsk.cancel(gOsk.ud);
        return;
    }

    /* A = select current cell */
    if (pressed & OSK_BTN_A) {
        ch = sGrid[gOsk.cursor_row][gOsk.cursor_col];
        if (ch == OSK_CHAR_NONE) return;
        if (ch == OSK_CHAR_DEL) {
            if (gOsk.text_len > 0) { gOsk.text_len--; gOsk.text[gOsk.text_len] = '\0'; }
            return;
        }
        if (ch == OSK_CHAR_OK) {
            gOsk.open = false;
            if (gOsk.confirm) gOsk.confirm(gOsk.text, gOsk.ud);
            return;
        }
        if (gOsk.text_len < gOsk.max_len) {
            gOsk.text[gOsk.text_len++] = ch;
            gOsk.text[gOsk.text_len]   = '\0';
        }
    }
}

#endif /* PRACTICE_ROM */
