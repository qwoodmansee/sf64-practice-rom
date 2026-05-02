# Phase 6 — OSK + File Browser Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an on-screen keyboard (`lib/ui/osk`) and SD file browser (`lib/ui/file_browser`) that let the player name and pick save-state files on the flashcart's SD card; wire both into the practice menu and render them from `src/practice/practice_sd.c`.

**Architecture:** `lib/ui/osk.c` and `lib/ui/file_browser.c` are host-portable state machines (no N64 rendering, no libultra). They expose state via a public struct (`gOsk`, `gFileBrowser`) that `practice_sd.c` reads to call `Practice_DrawText`/`Practice_DrawBox`. FatFs calls live in `file_browser.c` because FatFs itself compiles on host (diskio returns errors without real hardware). The OSK freezes gameplay the same way the existing practice menu does.

**Tech Stack:** C89/N64, FatFs R0.15 (already vendored at `lib/fatfs/`), `Graphics_DisplaySmallText` for rendering, existing `Practice_DrawText`/`Practice_DrawBox`/`Practice_DrawTextColor` wrappers.

---

## 0. Status & input artefacts

Phase 5 delivered:
- Cross-scene load state machine (practice_save.c)
- Slot picker UI (practice_menu.c center area)
- All 4 RAM save slots functional

What this phase adds:
- `lib/ui/osk.{c,h}` — on-screen keyboard (state machine only)
- `lib/ui/file_browser.{c,h}` — SD file picker (FatFs traversal + state)
- `src/practice/practice_sd.c` — rendering + practice glue (Update/Draw called from practice_main.c)
- `_` glyph added to `sSmallChars[]` in `src/engine/fox_std_lib.c`
- SD menu items wired into the radial menu (new `RSLICE_SD` slice with SD save/load submenu or inline trigger)
- Linker script integration for new `lib/ui/` objects
- Static invariants
- Host unit tests for OSK and file_browser logic

## 1. File map

| File | Action | Responsibility |
|------|--------|----------------|
| `lib/ui/osk.h` | Create | Types, API, public state struct |
| `lib/ui/osk.c` | Create | Char grid state machine; abstract button input |
| `lib/ui/file_browser.h` | Create | Types, API, public state struct |
| `lib/ui/file_browser.c` | Create | FatFs opendir/readdir, list state, mode dispatch |
| `lib/test/test_osk.c` | Create | Host unit tests for OSK state logic |
| `lib/test/test_file_browser.c` | Create | Host unit tests (fake diskio in-memory) |
| `src/practice/practice_sd.c` | Create | Rendering, N64 input mapping, practice glue |
| `include/practice.h` | Modify | Add `Practice_Sd_Init`, `_Update`, `_Draw` declarations; `gSdAvailable` extern |
| `src/practice/practice_main.c` | Modify | Call `Practice_Sd_Init`, `Practice_Sd_Update`, `Practice_Sd_Draw` |
| `src/practice/practice_menu.c` | Modify | Add SD save/load trigger (Z-button in slot picker center area) |
| `src/engine/fox_std_lib.c` | Modify | Add `_` to `sSmallChars[]` at position after `-.` |
| `tools/patch_linker_script.py` | Modify | Add `LIB_UI_OBJS = ["osk", "file_browser"]` |
| `tools/practice_invariants.py` | Modify | Add 3 new checks |
| `lib/test/Makefile` | Modify | Add test_osk and test_file_browser build targets |
| `docs/superpowers/plans/HW_VERIFY_phase6.md` | Create | Manual HW verification checklist |

## 2. Decisions locked in

| Decision | Choice |
|----------|--------|
| Portability boundary | `lib/ui/osk.c` and `lib/ui/file_browser.c` are host-portable. No N64 registers, no `Graphics_*`, no `gController*`. Rendering lives entirely in `practice_sd.c`. |
| Input abstraction | `osk_update(uint8_t pressed)` takes an `OSK_BTN_*` bitmask. `practice_sd.c` translates N64 `gControllerPress[gMainController].button` bits to `OSK_BTN_*` each frame. |
| File browser FatFs calls | `file_browser_open()` calls `f_opendir` / `f_readdir` immediately to populate a static array. Max 64 entries stored (static, not heap). On host, FatFs diskio returns `RES_ERROR`, so `file_browser_open()` fails gracefully and the list is empty. |
| Filename extension | The user types a name (up to 24 chars, uppercase); `practice_sd.c` appends `.SF64ST` before passing the path to `slot_manager_save_sd_named`. The OSK max_len is 24. |
| SD availability | Cached as `static bool sSdAvailable` set in `Practice_Sd_Init()` by calling `iodev_detect() != IODEV_NONE`. SD menu items are skipped when false. No new global in practice.h required; the flag lives in `practice_sd.c`. |
| Menu integration | Z button in the radial menu (slot picker center, depth 0) opens the SD submenu: one press → shows "SD SAVE / SD LOAD" hint; pressing A on "SD SAVE" opens OSK, pressing A on "SD LOAD" opens file_browser. Keep it simple — add a new `sSdMenuMode` state local to `practice_sd.c`. When SD menu is active, `Practice_Sd_Update` consumes input and returns `true`; `practice_sd.c`'s draw overlays the slot picker center area. |
| Char grid layout | 8 cols × 6 rows = 48 cells. Row 0–2: A–Z (3×8=24, 2 spare), row 3: `_!:-.`, row 4: `0–9` + `DEL` cell, row 5: spacer + `CONFIRM` cell. Exact layout in `osk.c`; `practice_sd.c` renders it using a nested loop with `Practice_DrawText`. |
| OSK game freeze | While `osk_is_open()`, `Practice_Sd_Update()` returns early after processing OSK input. The normal Practice_Menu_Update / gameplay input path in practice_main.c is not reached because `Practice_Sd_Update()` runs first in `Practice_Update()` and the caller checks its return value. |
| `sSmallChars[]` index for `_` | Append `_` at index 40 (after `-.`, before `0`). This shifts all digit character codes by 1. Verify the glyph invariant check is updated to include `_` in the allowed set. |

---

## Chunk 1: lib/ui/osk — state machine

### Task 1: OSK header (`lib/ui/osk.h`)

**Files:**
- Create: `lib/ui/osk.h`

- [ ] **Step 1: Create lib/ui/ directory and write osk.h**

```c
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
#include <stdint.h>

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
```

- [ ] **Step 2: Verify lib/ui/ directory exists (Makefile auto-discovers via find lib -type d)**

```bash
ls lib/ui/
```
Expected: `osk.h` file visible.

---

### Task 2: OSK implementation (`lib/ui/osk.c`)

**Files:**
- Create: `lib/ui/osk.c`

- [ ] **Step 1: Write OSK char grid and state logic**

The char grid layout (8 cols × 6 rows):
- Row 0: `A B C D E F G H`
- Row 1: `I J K L M N O P`
- Row 2: `Q R S T U V W X`
- Row 3: `Y Z _ ! : - . [NONE]`
- Row 4: `0 1 2 3 4 5 6 7`
- Row 5: `8 9 [NONE] [NONE] [NONE] [NONE] [DEL] [OK]`

```c
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
```

- [ ] **Step 2: No build yet — code depends on practice_sd.c hooks. Verify it compiles standalone:**

```bash
gcc -DPRACTICE_ROM -Ilib -c lib/ui/osk.c -o /tmp/osk.o && echo OK
```
Expected: `OK`

---

### Task 3: OSK host unit tests (`lib/test/test_osk.c`)

**Files:**
- Create: `lib/test/test_osk.c`
- Modify: `lib/test/Makefile`

- [ ] **Step 1: Write tests**

```c
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

int main(void) {
    printf("test_osk:\n");
    test_type_and_confirm();
    test_cancel();
    test_max_len();
    test_default_text();
    test_del_cell();
    printf("All osk tests PASS.\n");
    return 0;
}
```

- [ ] **Step 2: Add to lib/test/Makefile**

Open `lib/test/Makefile` and find the existing test targets. Add:
```makefile
test_osk: test_osk.c ../ui/osk.c
	$(CC) $(CFLAGS) -DPRACTICE_ROM -I.. -o $@ $^

run-all: ... test_osk
	./test_osk
```
(Match the exact pattern used by `test_slot_manager` in that Makefile.)

- [ ] **Step 3: Run tests**

```bash
make -C lib/test test_osk && ./lib/test/test_osk
```
Expected:
```
test_osk:
  test_type_and_confirm: PASS
  test_cancel: PASS
  test_max_len: PASS
  test_default_text: PASS
  test_del_cell: PASS
All osk tests PASS.
```

- [ ] **Step 4: Commit**

```bash
git add lib/ui/osk.h lib/ui/osk.c lib/test/test_osk.c lib/test/Makefile
git commit -m "feat(osk): add on-screen keyboard state machine with host unit tests"
```

---

## Chunk 2: Font change + file_browser

### Task 4: Add `_` glyph to `sSmallChars[]`

**Files:**
- Modify: `src/engine/fox_std_lib.c:824`
- Modify: `tools/practice_invariants.py` (glyph allowed set)

The current line is:
```c
char sSmallChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ!:-.0123456789";
```

`_` must be inserted after `-.` so filenames can use it.

- [ ] **Step 1: Edit sSmallChars**

Change line 824 of `src/engine/fox_std_lib.c`:
```c
char sSmallChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ!:-._0123456789";
```

- [ ] **Step 2: Update glyph invariant**

In `tools/practice_invariants.py`, function `check_practice_text_glyphs()`, find:
```python
allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !:-.")
```
Change to:
```python
allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !:-._")
```

- [ ] **Step 3: Verify invariant passes**

```bash
python3 tools/practice_invariants.py
```
Expected: `Practice ROM invariant checks passed.`

- [ ] **Step 4: Build to confirm no compile error**

```bash
make practice -j4 2>&1 | tail -5
```
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/engine/fox_std_lib.c tools/practice_invariants.py
git commit -m "feat: add _ glyph to sSmallChars for SD filenames"
```

---

### Task 5: File browser header (`lib/ui/file_browser.h`)

**Files:**
- Create: `lib/ui/file_browser.h`

- [ ] **Step 1: Write header**

```c
/* lib/ui/file_browser.h — SD file picker (host-portable state machine) */
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
#include <stdint.h>

#define FB_MAX_ENTRIES  64
#define FB_VISIBLE_ROWS 14
#define FB_PATH_MAX     64   /* /sf64-practice/states/XXXXXXXXXXXXXXXXXXXXXXXX.SF64ST + NUL */
#define FB_NAME_MAX     32   /* filename only (without directory) */

typedef enum {
    FB_LOAD,        /* pick a file, callback fires with full path */
    FB_SAVE_PROMPT, /* pick a dir (unused for now: always states/), then OSK fires */
} fb_mode_t;

typedef void (*fb_callback_t)(const char *path, void *ud);
typedef void (*fb_cancel_fn)(void *ud);

typedef struct {
    char        names[FB_MAX_ENTRIES][FB_NAME_MAX]; /* entry filenames */
    uint8_t     count;     /* number of entries loaded */
    uint8_t     cursor;    /* selected row (0-based) */
    uint8_t     scroll;    /* first visible row */
    bool        open;
    fb_mode_t   mode;
    char        dir[FB_PATH_MAX];
    char        suffix[8]; /* e.g. ".SF64ST" */
    fb_callback_t callback;
    fb_cancel_fn  cancel;
    void         *ud;
} fb_state_t;

extern fb_state_t gFileBrowser;

/* OSK_BTN_* reused as abstract button flags */
#ifndef OSK_BTN_UP
#include "osk.h"
#endif

/* Returns SLOT_MANAGER_OK(0) if dir was opened, negative on FatFs error.
 * Populates gFileBrowser.names/count synchronously. */
int  file_browser_open(fb_mode_t mode, const char *dir, const char *suffix,
                       fb_callback_t cb, fb_cancel_fn cancel, void *ud);
void file_browser_update(uint8_t pressed);
bool file_browser_is_open(void);
void file_browser_close(void);

#endif /* LIB_UI_FILE_BROWSER_H */
```

---

### Task 6: File browser implementation (`lib/ui/file_browser.c`)

**Files:**
- Create: `lib/ui/file_browser.c`

- [ ] **Step 1: Write implementation**

```c
/* lib/ui/file_browser.c — SD file picker using FatFs */
#ifdef PRACTICE_ROM
#include "file_browser.h"
#include "../fatfs/ff.h"
#include <string.h>

fb_state_t gFileBrowser;

static void fb_zero(void) {
    int i, j;
    for (i = 0; i < FB_MAX_ENTRIES; i++)
        for (j = 0; j < FB_NAME_MAX; j++)
            gFileBrowser.names[i][j] = '\0';
    gFileBrowser.count  = 0;
    gFileBrowser.cursor = 0;
    gFileBrowser.scroll = 0;
    gFileBrowser.open   = false;
}

/* Copy at most dst_len-1 chars from src to dst, always NUL-terminate. */
static void fb_strncpy(char *dst, const char *src, int dst_len) {
    int i;
    for (i = 0; i < dst_len - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int fb_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Case-insensitive suffix match */
static bool fb_suffix_match(const char *name, const char *suffix) {
    int nlen = fb_strlen(name);
    int slen = fb_strlen(suffix);
    int i;
    if (slen == 0) return true;
    if (nlen < slen) return false;
    for (i = 0; i < slen; i++) {
        char a = name[nlen - slen + i];
        char b = suffix[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return true;
}

int file_browser_open(fb_mode_t mode, const char *dir, const char *suffix,
                      fb_callback_t cb, fb_cancel_fn cancel, void *ud) {
    DIR  fatdir;
    FILINFO fno;
    FRESULT res;

    fb_zero();
    gFileBrowser.mode     = mode;
    gFileBrowser.callback = cb;
    gFileBrowser.cancel   = cancel;
    gFileBrowser.ud       = ud;
    fb_strncpy(gFileBrowser.dir, dir, FB_PATH_MAX);
    fb_strncpy(gFileBrowser.suffix, suffix ? suffix : "", 8);

    res = f_opendir(&fatdir, dir);
    if (res != FR_OK) return -(int)res;

    while (gFileBrowser.count < FB_MAX_ENTRIES) {
        res = f_readdir(&fatdir, &fno);
        if (res != FR_OK || fno.fname[0] == '\0') break;
        if (fno.fattrib & AM_DIR) continue;  /* skip subdirs */
        if (!fb_suffix_match(fno.fname, gFileBrowser.suffix)) continue;
        fb_strncpy(gFileBrowser.names[gFileBrowser.count], fno.fname, FB_NAME_MAX);
        gFileBrowser.count++;
    }
    f_closedir(&fatdir);
    gFileBrowser.open = true;
    return 0;
}

bool file_browser_is_open(void) { return gFileBrowser.open; }

void file_browser_close(void) { gFileBrowser.open = false; }

void file_browser_update(uint8_t pressed) {
    char path[FB_PATH_MAX];
    int dirlen, namelen, i;

    if (!gFileBrowser.open) return;

    if (pressed & OSK_BTN_UP) {
        if (gFileBrowser.cursor > 0) {
            gFileBrowser.cursor--;
            if (gFileBrowser.cursor < gFileBrowser.scroll)
                gFileBrowser.scroll = gFileBrowser.cursor;
        }
    }
    if (pressed & OSK_BTN_DOWN) {
        if (gFileBrowser.count > 0 && gFileBrowser.cursor < gFileBrowser.count - 1) {
            gFileBrowser.cursor++;
            if (gFileBrowser.cursor >= gFileBrowser.scroll + FB_VISIBLE_ROWS)
                gFileBrowser.scroll = gFileBrowser.cursor - FB_VISIBLE_ROWS + 1;
        }
    }

    /* B or Z = cancel */
    if ((pressed & OSK_BTN_B) || (pressed & OSK_BTN_Z)) {
        gFileBrowser.open = false;
        if (gFileBrowser.cancel) gFileBrowser.cancel(gFileBrowser.ud);
        return;
    }

    /* A or START = confirm selection */
    if ((pressed & OSK_BTN_A) || (pressed & OSK_BTN_START)) {
        if (gFileBrowser.count == 0) return;
        /* Build full path: dir + "/" + name */
        dirlen  = fb_strlen(gFileBrowser.dir);
        namelen = fb_strlen(gFileBrowser.names[gFileBrowser.cursor]);
        if (dirlen + 1 + namelen + 1 > FB_PATH_MAX) return;
        for (i = 0; i < dirlen; i++) path[i] = gFileBrowser.dir[i];
        path[dirlen] = '/';
        for (i = 0; i < namelen; i++) path[dirlen + 1 + i] = gFileBrowser.names[gFileBrowser.cursor][i];
        path[dirlen + 1 + namelen] = '\0';
        gFileBrowser.open = false;
        if (gFileBrowser.callback) gFileBrowser.callback(path, gFileBrowser.ud);
    }
}

#endif /* PRACTICE_ROM */
```

- [ ] **Step 2: Compile check**

```bash
gcc -DPRACTICE_ROM -Ilib -Ilib/fatfs -c lib/ui/file_browser.c -o /tmp/fb.o 2>&1
```
Expected: no errors (FatFs types resolve from `lib/fatfs/ff.h`).

---

### Task 7: File browser host unit tests

**Files:**
- Create: `lib/test/test_file_browser.c`

Since FatFs diskio returns RES_ERROR on host, `file_browser_open()` will return non-zero. The test validates the graceful-failure path and the list navigation logic by directly populating `gFileBrowser` state.

- [ ] **Step 1: Write test**

```c
/* lib/test/test_file_browser.c */
#define PRACTICE_ROM 1
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ui/file_browser.h"

static const char *sLastPath = NULL;
static int sCancelCount = 0;

static void on_cb(const char *path, void *ud) { sLastPath = path; (void)ud; }
static void on_cancel(void *ud) { sCancelCount++; (void)ud; }

/* Inject fake entries directly (FatFs not available on host) */
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
}

static void test_navigate_and_select(void) {
    inject_entries(3);
    /* Down to entry 1 */
    file_browser_update(OSK_BTN_DOWN);
    assert(gFileBrowser.cursor == 1);
    /* Select */
    sLastPath = NULL;
    file_browser_update(OSK_BTN_A);
    assert(!file_browser_is_open());
    assert(sLastPath != NULL);
    assert(strstr(sLastPath, "SAVE01.SF64ST") != NULL);
    printf("  test_navigate_and_select: PASS\n");
}

static void test_cancel(void) {
    sCancelCount = 0;
    inject_entries(2);
    file_browser_update(OSK_BTN_Z);
    assert(!file_browser_is_open());
    assert(sCancelCount == 1);
    printf("  test_cancel: PASS\n");
}

static void test_scroll(void) {
    inject_entries(FB_VISIBLE_ROWS + 4);
    /* Scroll down past visible rows */
    int i;
    for (i = 0; i < FB_VISIBLE_ROWS + 2; i++) file_browser_update(OSK_BTN_DOWN);
    assert(gFileBrowser.scroll > 0);
    /* Cursor should be at FB_VISIBLE_ROWS + 2 */
    assert(gFileBrowser.cursor == FB_VISIBLE_ROWS + 2);
    file_browser_close();
    printf("  test_scroll: PASS\n");
}

static void test_empty_list(void) {
    inject_entries(0);
    /* A on empty list should not crash or fire callback */
    sLastPath = NULL;
    file_browser_update(OSK_BTN_A);
    assert(file_browser_is_open());  /* stays open */
    assert(sLastPath == NULL);
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
```

- [ ] **Step 2: Add to lib/test/Makefile**

Add target analogous to test_osk, linking `ui/file_browser.c` and stubbed FatFs. The host `lib/fatfs/diskio.c` already returns `RES_ERROR`; link `lib/fatfs/ff.c` and its dependencies so it compiles:

```makefile
FATFS_SRCS = ../fatfs/ff.c ../fatfs/ffunicode.c ../fatfs/ff_libc.c ../fatfs/diskio.c

test_file_browser: test_file_browser.c ../ui/file_browser.c $(FATFS_SRCS) ../ui/osk.c
	$(CC) $(CFLAGS) -DPRACTICE_ROM -I.. -Ilib -o $@ $^
```

- [ ] **Step 3: Run tests**

```bash
make -C lib/test test_file_browser && ./lib/test/test_file_browser
```
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add lib/ui/file_browser.h lib/ui/file_browser.c lib/test/test_file_browser.c lib/test/Makefile
git commit -m "feat(file_browser): add SD file picker state machine with host unit tests"
```

---

## Chunk 3: Practice glue (practice_sd.c)

### Task 8: practice_sd.c skeleton + OSK rendering

**Files:**
- Create: `src/practice/practice_sd.c`
- Modify: `include/practice.h`
- Modify: `src/practice/practice_main.c`

The file renders the OSK overlay and maps N64 buttons to `OSK_BTN_*`.

- [ ] **Step 1: Add declarations to include/practice.h**

After the existing `Practice_Save_*` declarations (around line 230), add:

```c
/* Phase 6: SD OSK + file browser */
void Practice_Sd_Init(void);
void Practice_Sd_Update(void);
void Practice_Sd_Draw(void);
bool Practice_Sd_IsActive(void);  /* true when OSK or file browser is open */
/* Called from radial menu when user triggers SD save/load */
void Practice_Sd_StartSave(void);
void Practice_Sd_StartLoad(void);
```

- [ ] **Step 2: Wire into practice_main.c**

In `Practice_Init()` (after `Practice_Save_Init()`):
```c
Practice_Sd_Init();
```

In `Practice_Update()`, at the very top of the function body (before the `Practice_HeapAudit_PerFrame()` line):
```c
if (Practice_Sd_IsActive()) {
    Practice_Sd_Update();
    return;
}
```

In `Practice_Draw()`, at the end of the `PSCREEN_GAMEPLAY` case (after `Practice_Menu_Draw()`):
```c
Practice_Sd_Draw();
```

- [ ] **Step 3: Write practice_sd.c — OSK rendering**

```c
/* src/practice/practice_sd.c
 * Phase 6: OSK + file browser rendering and practice glue.
 * lib/ui/osk.c and lib/ui/file_browser.c own the state machines;
 * this file owns all N64 rendering and N64 input translation.
 *
 * Include paths resolve via -Ilib in IINC (see Makefile line 258).
 */
#ifdef PRACTICE_ROM
#include "practice.h"
#include "ui/osk.h"
#include "ui/file_browser.h"
#include "iodev/iodev.h"
#include "fatfs/ff.h"
#include "slot_manager.h"
#include <stddef.h>

#define SD_DIR        "/sf64-practice/states"
#define SD_SUFFIX     ".SF64ST"
#define SD_EXT        ".SF64ST"
#define SD_PATH_MAX   (FB_PATH_MAX)

static bool sSdAvailable = false;
static char sSavePath[SD_PATH_MAX];

/* ---- OSK button translation ------------------------------------------ */
static uint8_t osk_buttons_from_n64(void) {
    OSContPad* press = &gControllerPress[gMainController];
    uint8_t b = 0;
    if (press->button & U_JPAD) b |= OSK_BTN_UP;
    if (press->button & D_JPAD) b |= OSK_BTN_DOWN;
    if (press->button & L_JPAD) b |= OSK_BTN_LEFT;
    if (press->button & R_JPAD) b |= OSK_BTN_RIGHT;
    if (press->button & A_BUTTON) b |= OSK_BTN_A;
    if (press->button & B_BUTTON) b |= OSK_BTN_B;
    if (press->button & START_BUTTON) b |= OSK_BTN_START;
    if (press->button & Z_TRIG) b |= OSK_BTN_Z;
    return b;
}

/* ---- SD save callback (from OSK confirm) ------------------------------ */
static void on_save_name_confirmed(const char *name, void *ud) {
    int res;
    int nlen = 0;
    int i;
    FRESULT fr;
    (void)ud;

    /* Lazy mkdir */
    f_mkdir(SD_DIR);

    /* Build path: dir/NAME.SF64ST */
    for (i = 0; SD_DIR[i]; i++) sSavePath[i] = SD_DIR[i];
    sSavePath[i++] = '/';
    for (; name[nlen] && nlen + i < SD_PATH_MAX - 8; nlen++, i++) sSavePath[i] = name[nlen];
    for (; SD_EXT[nlen - nlen]; ) { /* append extension */ break; } /* see below */
    /* simple append of SD_EXT */
    {
        int j;
        for (j = 0; SD_EXT[j] && i < SD_PATH_MAX - 1; j++, i++) sSavePath[i] = SD_EXT[j];
        sSavePath[i] = '\0';
    }

    res = slot_manager_save_sd_named(sSavePath);
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

/* ---- SD load callback (from file_browser confirm) --------------------- */
static void on_load_file_selected(const char *path, void *ud) {
    int res;
    (void)ud;
    res = slot_manager_load_sd_named(path);
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

/* ---- Public API ------------------------------------------------------- */
void Practice_Sd_Init(void) {
    sSdAvailable = (iodev_detect() != IODEV_NONE);
}

bool Practice_Sd_IsActive(void) {
    return osk_is_open() || file_browser_is_open();
}

void Practice_Sd_StartSave(void) {
    if (!sSdAvailable) {
        Practice_Hud_ShowStatus("NO SD CART", 255, 180, 80);
        return;
    }
    osk_open("SD SAVE NAME:", "", OSK_MAX_TEXT,
             on_save_name_confirmed, on_save_canceled, NULL);
}

void Practice_Sd_StartLoad(void) {
    if (!sSdAvailable) {
        Practice_Hud_ShowStatus("NO SD CART", 255, 180, 80);
        return;
    }
    if (file_browser_open(FB_LOAD, SD_DIR, SD_SUFFIX,
                          on_load_file_selected, on_load_canceled, NULL) != 0) {
        Practice_Hud_ShowStatus("SD OPEN ERR", 255, 120, 80);
    }
}

void Practice_Sd_Update(void) {
    uint8_t pressed = osk_buttons_from_n64();
    if (osk_is_open()) {
        osk_update(pressed);
    } else if (file_browser_is_open()) {
        file_browser_update(pressed);
    }
}

/* ---- OSK rendering ---------------------------------------------------- */
#define OSK_X0 20   /* left edge of OSK overlay */
#define OSK_Y0 20   /* top edge */
#define OSK_CW 12   /* cell width */
#define OSK_CH 10   /* cell height */

static void draw_osk(void) {
    int col, row;
    s32 bx = OSK_X0 - 4;
    s32 by = OSK_Y0 - 20;
    s32 bw = OSK_GRID_COLS * OSK_CW + 8;
    s32 bh = OSK_GRID_ROWS * OSK_CH + 30;

    Practice_DrawBox(bx, by, bw, bh, 0, 0, 0, 210);

    /* Prompt + current text */
    Practice_DrawTextColor(OSK_X0, by + 4, gOsk.prompt, 0, 255, 128);
    Practice_DrawText(OSK_X0, by + 14, gOsk.text);

    /* Char grid */
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
                char tmp[2] = { ch, '\0' };
                Practice_DrawText(cx, cy, tmp);
            }
        }
    }
    Practice_DrawTextColor(OSK_X0, by + bh - 8, "A:SELECT B:DEL START:OK Z:CANCEL", 120, 120, 120);
}

/* ---- File browser rendering ------------------------------------------ */
#define FB_X0 20
#define FB_Y0 15
#define FB_ROW_H 10

static void draw_file_browser(void) {
    int i;
    s32 bx = FB_X0 - 4;
    s32 by = FB_Y0 - 12;
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
    Practice_DrawTextColor(FB_X0, by + bh - 8, "UP:DN  A:SELECT  B:CANCEL", 120, 120, 120);
}

void Practice_Sd_Draw(void) {
    if (osk_is_open()) {
        draw_osk();
    } else if (file_browser_is_open()) {
        draw_file_browser();
    }
}

#endif /* PRACTICE_ROM */
```

Note: the path-building in `on_save_name_confirmed` has a minor bug in the placeholder above — clean it up:

```c
static void on_save_name_confirmed(const char *name, void *ud) {
    int res, i, j;
    (void)ud;
    f_mkdir(SD_DIR);

    i = 0;
    for (j = 0; SD_DIR[j] && i < SD_PATH_MAX - 1; j++) sSavePath[i++] = SD_DIR[j];
    if (i < SD_PATH_MAX - 1) sSavePath[i++] = '/';
    for (j = 0; name[j] && i < SD_PATH_MAX - 1; j++) sSavePath[i++] = name[j];
    for (j = 0; SD_EXT[j] && i < SD_PATH_MAX - 1; j++) sSavePath[i++] = SD_EXT[j];
    sSavePath[i] = '\0';

    res = slot_manager_save_sd_named(sSavePath);
    if (res == SLOT_MANAGER_OK) {
        Practice_Hud_ShowStatus("SD SAVE OK", 80, 255, 120);
    } else {
        Practice_Hud_ShowStatus("SD SAVE FAIL", 255, 120, 80);
    }
}
```

- [ ] **Step 4: Build**

```bash
make practice -j4 2>&1 | tail -10
```
Expected: clean build (slot_manager SD methods still stub, but they compile).

- [ ] **Step 5: Commit skeleton**

```bash
git add src/practice/practice_sd.c include/practice.h src/practice/practice_main.c
git commit -m "feat(practice_sd): add OSK + file_browser rendering glue skeleton"
```

---

### Task 9: Wire SD save/load into radial menu

**Files:**
- Modify: `src/practice/practice_menu.c`

Add Z-button triggers in the slot picker area (depth 0, no hovered radial slice). When SD is available, Z opens SD save; Z+hold (or a second check) opens SD load. Simpler: use Z for save-to-SD and the unused `L_TRIG` isn't free — so use a dedicated press: Z = SD SAVE, Z+A = ... that's awkward.

**Decision:** Add two new radial slices `RSLICE_SD_SAVE` and `RSLICE_SD_LOAD` to the root radial. The current layout:
- Up: RESTART
- Down-right: SAVE / SAVE (y>0) / LOAD (y<0)
- Left-right: CAMERA / LEVELS  
- Down: LOADOUT / DISPLAY

There's no clean open spot. Simplest: make `RSLICE_SAVE` open a 2-item inner radial (RAM save / SD save) and similar for load. But that adds complexity.

**Simpler approach:** While the radial menu is open at depth 0 with no slice hovered, pressing Z triggers SD save. Pressing Z+B (or just holding Z) triggers SD load. Wire just these two hotkeys.

In `Practice_Menu_Update()`, inside the depth-0, no-action-hovered branch, add:

```c
/* Z button = SD save/load shortcuts (when radial is open) */
if ((press->button & Z_TRIG) && !(press->button & B_BUTTON)) {
    Practice_Sd_StartSave();
    return;
}
if ((press->button & Z_TRIG) && (press->button & B_BUTTON)) {
    Practice_Sd_StartLoad();
    return;
}
```

Also update the hint text at the bottom of the menu to include `Z:SD SAVE ZB:SD LOAD`.

- [ ] **Step 1: Find the right place in Practice_Menu_Update**

In `practice_menu.c`, find the block handling `sMenuDepth == 0` A-button presses (around line 187). Just before that block, add the Z-trigger check.

- [ ] **Step 2: Add the Z-button SD triggers**

Open `src/practice/practice_menu.c`. Find:
```c
    if ((press->button & A_BUTTON) && (sHovered[sMenuDepth] != SLICE_NONE)) {
        if (sMenuDepth == 0) {
```

Before that block, add:
```c
    /* Z = SD save (when flashcart available; silently no-ops if not) */
    if (sMenuDepth == 0 && (press->button & Z_TRIG)) {
        if (press->button & B_BUTTON) {
            Practice_Sd_StartLoad();
        } else {
            Practice_Sd_StartSave();
        }
    }
```

- [ ] **Step 3: Update hint text**

Find in `Practice_Menu_Draw()`:
```c
        Practice_DrawTextColor(52, 198, "L:R SLOT STICK:A B:CLOSE", 150, 150, 150);
```
Change to:
```c
        Practice_DrawTextColor(40, 198, "L:R B:CLOSE Z:SD SAVE ZB:SD LOAD", 150, 150, 150);
```
Note: max ~38 chars for 320px screen. No `+` — it's not in sSmallChars. Use `ZB:` notation (Z+B together as a chord). The string above is 33 chars, all in the allowed glyph set (uppercase, space, colon, letters).

- [ ] **Step 4: Build and verify no new glyph violations**

```bash
python3 tools/practice_invariants.py && make practice -j4 2>&1 | tail -5
```
Expected: invariants pass, clean build.

- [ ] **Step 5: Commit**

```bash
git add src/practice/practice_menu.c
git commit -m "feat(menu): add Z-button SD save/load triggers in radial menu"
```

---

## Chunk 4: Linker script + static invariants + HW_VERIFY

### Task 10: Add lib/ui to linker script patcher

**Files:**
- Modify: `tools/patch_linker_script.py`

- [ ] **Step 1: Add LIB_UI_OBJS list**

Open `tools/patch_linker_script.py`. After `LIB_TOP_OBJS`, add:

```python
# lib/ui/* objects. Anchored on the last LIB_TOP entry.
LIB_UI_OBJS = [
    "osk",          # Phase 6: on-screen keyboard state machine
    "file_browser", # Phase 6: SD file picker state machine
]
```

- [ ] **Step 2: Add LIB_UI_OBJS injection loop**

In the section that processes `LIB_TOP_OBJS` and `LIB_FATFS_OBJS`, add an analogous loop for `LIB_UI_OBJS`. The objects live at `build/lib/ui/osk.o` etc. Follow the exact same anchoring pattern as `LIB_FATFS_OBJS`.

Look for the loop handling `LIB_FATFS_OBJS` and copy it for `LIB_UI_OBJS` with path `build/lib/ui/`.

- [ ] **Step 3: If the linker script already exists (it will), manually add the entries**

Open `linker_scripts/us/rev1/starfox64.ld`. Find `build/lib/slot_manager.o` entries (there will be 4 — one in each section). After each set, add analogous entries for `osk.o` and `file_browser.o`. Pattern:

In `.text`:
```ld
        build/lib/ui/osk.o(.text);
        build/lib/ui/file_browser.o(.text);
```
In `.data`, `.rodata`, `.bss`: same pattern.

- [ ] **Step 4: Build to confirm linker finds the new objects**

```bash
rm -rf build/ && make practice -j4 2>&1 | tail -10
```
Expected: clean build with `osk.o` and `file_browser.o` in the map.

- [ ] **Step 5: Commit**

```bash
git add tools/patch_linker_script.py linker_scripts/us/rev1/starfox64.ld
git commit -m "build: add lib/ui osk + file_browser to linker script"
```

---

### Task 11: Static invariants

**Files:**
- Modify: `tools/practice_invariants.py`

Add three new checks at the end of `main()`:

```python
check_osk_declared()
check_file_browser_declared()
check_practice_sd_wired()
```

And implement them:

```python
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
```

- [ ] **Step 1: Add the three functions and register them in `main()`**

- [ ] **Step 2: Run**

```bash
python3 tools/practice_invariants.py
```
Expected: `Practice ROM invariant checks passed.`

- [ ] **Step 3: Commit**

```bash
git add tools/practice_invariants.py
git commit -m "test(invariants): add Phase 6 OSK + file_browser checks"
```

---

### Task 12: Add practice_sd to build system

**Files:**
- Modify: `tools/patch_linker_script.py`
- Modify: `linker_scripts/us/rev1/starfox64.ld`

`practice_sd.c` goes in `src/practice/` so it is automatically discovered by the Makefile. But it must appear in `PRACTICE_OBJS` in `patch_linker_script.py` and in the linker script.

- [ ] **Step 1: Add to PRACTICE_OBJS**

In `tools/patch_linker_script.py`, in the `PRACTICE_OBJS` list, add:
```python
"practice_sd",      # Phase 6: SD OSK + file browser glue
```
Add it after `"practice_test_fatfs"` (keep alphabetical isn't required; add at the end of the list).

- [ ] **Step 2: Add to linker script**

In all four sections of `linker_scripts/us/rev1/starfox64.ld`, add `build/src/practice/practice_sd.o(.text)` etc., following the same pattern as other practice objects.

- [ ] **Step 3: Build**

```bash
make practice -j4 2>&1 | tail -5
```
Expected: clean build.

- [ ] **Step 4: Run full check**

```bash
python3 tools/practice_invariants.py && python3 tools/run_tests.py
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add tools/patch_linker_script.py linker_scripts/us/rev1/starfox64.ld
git commit -m "build: register practice_sd in linker script"
```

---

### Task 13: HW_VERIFY_phase6.md

**Files:**
- Create: `docs/superpowers/plans/HW_VERIFY_phase6.md`

```markdown
# HW_VERIFY Phase 6 — OSK + File Browser

## Prerequisites
- SC64 v2 flashcart with SD card formatted FAT32
- IS-Viewer session running (`sc64deployer debug --isv 0x03FF0000`)
- Latest practice ROM built and uploaded (`./tools/sc64dev`)

## T1 — SD detection
Boot ROM. IS-Viewer should show:
```
[iodev] cart=1 sd_init=0
```
`cart=1` confirms SC64 detected. `sd_init=0` confirms SD init succeeded.

## T2 — OSK opens
1. Launch any level via practice menu.
2. Open practice menu (hold L+R → menu opens).
3. Press Z (without B). OSK overlay should appear.
4. Verify: prompt "SD SAVE NAME:" visible, char grid rendered.

## T3 — OSK typing
1. With OSK open: D-pad to navigate to 'C', press A. Text field shows "C".
2. Navigate to 'O', press A. Text shows "CO".
3. Press B. Text shows "C" (backspace works).
4. Type "CORNERIA" by navigating to each character.
5. Press START. HUD shows "SD SAVE OK" or "SD SAVE FAIL".

## T4 — File browser opens
1. Open practice menu. Press Z+B.
2. File browser overlay appears listing .SF64ST files.
3. D-pad up/down scrolls the list.
4. Press B or Z — browser closes, HUD shows "SD CANCEL".

## T5 — Load from file browser
1. Save a state first (T3 above, note the filename).
2. Open file browser (Z+B in menu).
3. Navigate to the file, press A.
4. HUD shows "SD LOAD OK" (Phase 7 not yet implemented — expect "SD LOAD FAIL" from stub).
   After Phase 7 lands this becomes "SD LOAD OK".

## T6 — No SD cart (emulator)
1. Run ROM in BizHawk (no SD). Open menu, press Z.
2. HUD shows "NO SD CART" — no crash.

## PASS criteria
- T1–T4 all pass without crash
- IS-Viewer shows no unexpected error prints
```

- [ ] **Step 1: Write and commit**

```bash
git add docs/superpowers/plans/HW_VERIFY_phase6.md
git commit -m "docs: Phase 6 hardware verification checklist"
```

---

## Exit criteria

- [ ] `lib/ui/osk.h` and `lib/ui/osk.c` exist; `osk_open`/`osk_update`/`osk_is_open` work
- [ ] `lib/ui/file_browser.h` and `lib/ui/file_browser.c` exist; list navigation + callbacks work
- [ ] `_` is in `sSmallChars[]` (index after `-.`)
- [ ] `src/practice/practice_sd.c` exists; `Practice_Sd_Init/Update/Draw` called from `practice_main.c`
- [ ] Z button in radial menu triggers SD save OSK; Z+B triggers SD load file_browser
- [ ] OSK renders correctly in BizHawk (char grid visible, text field updates)
- [ ] File browser renders correctly (list visible, cursor moves)
- [ ] `make lib-test` passes all lib unit tests
- [ ] `python3 tools/practice_invariants.py` passes
- [ ] `make practice -j4` produces clean build
- [ ] `python3 tools/run_tests.py` passes
- [ ] `HW_VERIFY_phase6.md` exists
