/* lib/ui/file_browser.c - SD file picker using FatFs */
#ifdef PRACTICE_ROM
#include "file_browser.h"
#include "../fatfs/ff.h"

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
        if (res != FR_OK) {
            /* Real I/O / FS error -- distinct from end-of-dir (FR_OK with
             * empty fname). Don't silently truncate the listing and pretend
             * we opened cleanly; surface the FRESULT so the caller can
             * distinguish "empty directory" from "card went away mid-scan". */
            f_closedir(&fatdir);
            fb_zero();
            return -(int)res;
        }
        if (fno.fname[0] == '\0') break; /* end of directory */
        if (fno.fattrib & AM_DIR) continue;
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

    if ((pressed & OSK_BTN_B) || (pressed & OSK_BTN_Z)) {
        gFileBrowser.open = false;
        if (gFileBrowser.cancel) gFileBrowser.cancel(gFileBrowser.ud);
        return;
    }

    if ((pressed & OSK_BTN_A) || (pressed & OSK_BTN_START)) {
        if (gFileBrowser.count == 0) return;
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
