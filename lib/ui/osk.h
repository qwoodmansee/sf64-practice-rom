/* lib/ui/osk.h — on-screen keyboard (host-portable state machine) */
#ifndef LIB_UI_OSK_H
#define LIB_UI_OSK_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdbool.h>
#else
#ifndef __bool_true_false_are_defined
typedef int bool;
#define false 0
#define true  1
#define __bool_true_false_are_defined 1
#endif
#endif
#include "lib_types.h"

#define OSK_MAX_TEXT  24   /* user-visible chars; caller appends .SF64ST */
#define OSK_GRID_COLS  8
#define OSK_GRID_ROWS  6
#define OSK_CHAR_DEL   '\x01'  /* sentinel: DEL key cell */
#define OSK_CHAR_OK    '\x02'  /* sentinel: CONFIRM key cell */
#define OSK_CHAR_NONE  '\x00'  /* empty/spacer cell */

/* Abstract button mask — maps to N64 buttons in practice_sd.c */
#define OSK_BTN_UP    0x01u
#define OSK_BTN_DOWN  0x02u
#define OSK_BTN_LEFT  0x04u
#define OSK_BTN_RIGHT 0x08u
#define OSK_BTN_A     0x10u
#define OSK_BTN_B     0x20u
#define OSK_BTN_START 0x40u
#define OSK_BTN_Z     0x80u

typedef void (*osk_confirm_fn)(const char *text, void *ud);
typedef void (*osk_cancel_fn)(void *ud);

typedef struct {
    char           text[OSK_MAX_TEXT + 1];
    char           prompt[32];
    uint8_t        text_len;
    uint8_t        max_len;
    uint8_t        cursor_col;
    uint8_t        cursor_row;
    bool           open;
    osk_confirm_fn confirm;
    osk_cancel_fn  cancel;
    void          *ud;
} osk_state_t;

extern osk_state_t gOsk;

void osk_open(const char *prompt, const char *default_text,
              uint8_t max_len, osk_confirm_fn confirm, osk_cancel_fn cancel, void *ud);
void osk_update(uint8_t pressed);
bool osk_is_open(void);
void osk_close(void);
char osk_char_at(int col, int row);  /* for renderer: returns char, OSK_CHAR_DEL, OSK_CHAR_OK, or OSK_CHAR_NONE */

#endif /* LIB_UI_OSK_H */
