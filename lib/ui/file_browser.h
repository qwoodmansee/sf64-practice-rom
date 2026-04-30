/* lib/ui/file_browser.h - SD file picker (host-portable state machine) */
#ifndef LIB_UI_FILE_BROWSER_H
#define LIB_UI_FILE_BROWSER_H

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

#define FB_MAX_ENTRIES  64
#define FB_VISIBLE_ROWS 14
#define FB_PATH_MAX     64
#define FB_NAME_MAX     32

typedef enum {
    FB_LOAD,
    FB_SAVE_PROMPT,
} fb_mode_t;

typedef void (*fb_callback_t)(const char *path, void *ud);
typedef void (*fb_cancel_fn)(void *ud);

typedef struct {
    char        names[FB_MAX_ENTRIES][FB_NAME_MAX];
    uint8_t     count;
    uint8_t     cursor;
    uint8_t     scroll;
    bool        open;
    fb_mode_t   mode;
    char        dir[FB_PATH_MAX];
    char        suffix[8];
    fb_callback_t callback;
    fb_cancel_fn  cancel;
    void         *ud;
} fb_state_t;

extern fb_state_t gFileBrowser;

/* OSK_BTN_* reused as abstract button flags */
#ifndef OSK_BTN_UP
#include "osk.h"
#endif

/* Returns 0 if dir was opened, negative on FatFs error. */
int  file_browser_open(fb_mode_t mode, const char *dir, const char *suffix,
                       fb_callback_t cb, fb_cancel_fn cancel, void *ud);
void file_browser_update(uint8_t pressed);
bool file_browser_is_open(void);
void file_browser_close(void);

#endif /* LIB_UI_FILE_BROWSER_H */
