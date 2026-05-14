/*
 * Minimal mupen64plus test harness.
 *
 * Boots a ROM, advances frames, reads/writes memory on demand.
 * Communicates with the Python test runner via line-based stdin/stdout.
 *
 * Build:
 *   cc -O2 -o tools/m64p_harness tools/m64p_harness.c \
 *      -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 \
 *      -L/opt/homebrew/lib -lmupen64plus -lSDL2
 *
 * Usage:
 *   m64p_harness <rom_path>            # headless (no video window)
 *   m64p_harness --headed <rom_path>   # with video window
 *
 * Protocol (stdin -> harness, harness -> stdout):
 *   ADVANCE <n>                    -> OK
 *   READ32 <rdram_addr>            -> VAL <hex>
 *   WRITE32 <rdram_addr> <hex_val> -> OK
 *   SLEEP <ms>                     -> OK
 *   QUIT                           -> BYE
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#define M64P_CORE_PROTOTYPES 1
#include "mupen64plus/m64p_common.h"
#include "mupen64plus/m64p_frontend.h"
#include "mupen64plus/m64p_types.h"

extern void *       DebugMemGetPointer(int);

#define M64P_DBG_PTR_RDRAM 1

static unsigned char *g_rdram = NULL;

#define PLUGIN_DIR  "/opt/homebrew/lib/mupen64plus"
#define DATA_DIR    "/opt/homebrew/share/mupen64plus"
#define CONFIG_DIR  "/tmp/m64p_test_config"

#define FRONTEND_API_VERSION 0x020001

static volatile int g_emu_running = 0;
static volatile int g_frame_ready = 0;
static pthread_mutex_t g_frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_frame_cond  = PTHREAD_COND_INITIALIZER;
static int g_headed = 0;
static const char *g_rom_path = NULL;

/* --- Stub video extension for headless mode --- */

static m64p_error stub_vid_init(void) { return M64ERR_SUCCESS; }
static m64p_error stub_vid_quit(void) { return M64ERR_SUCCESS; }
static m64p_error stub_vid_list_modes(m64p_2d_size *s, int *n) { if (n) *n = 0; return M64ERR_SUCCESS; }
static m64p_error stub_vid_list_rates(m64p_2d_size s, int *r, int *n) { (void)s; if (n) *n = 0; return M64ERR_SUCCESS; }
static m64p_error stub_vid_set_mode(int a, int b, int c, int d, int e) { (void)a;(void)b;(void)c;(void)d;(void)e; return M64ERR_SUCCESS; }
static m64p_error stub_vid_set_mode_rate(int a, int b, int c, int d, int e, int f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return M64ERR_SUCCESS; }
static m64p_function stub_vid_gl_getproc(const char *n) { (void)n; return NULL; }
static m64p_error stub_vid_gl_setattr(m64p_GLattr a, int v) { (void)a;(void)v; return M64ERR_SUCCESS; }
static m64p_error stub_vid_gl_getattr(m64p_GLattr a, int *v) { (void)a; if (v) *v = 0; return M64ERR_SUCCESS; }
static m64p_error stub_vid_gl_swapbuf(void) { return M64ERR_SUCCESS; }
static m64p_error stub_vid_set_caption(const char *c) { (void)c; return M64ERR_SUCCESS; }
static m64p_error stub_vid_toggle_fs(void) { return M64ERR_SUCCESS; }
static m64p_error stub_vid_resize(int w, int h) { (void)w;(void)h; return M64ERR_SUCCESS; }
static uint32_t   stub_vid_gl_getfbo(void) { return 0; }
static m64p_error stub_vid_init_render(m64p_render_mode m) { (void)m; return M64ERR_SUCCESS; }
static m64p_error stub_vid_vk_getsurface(void **a, void *b) { (void)a;(void)b; return M64ERR_UNSUPPORTED; }
static m64p_error stub_vid_vk_getexts(const char **e[], uint32_t *n) { (void)e; if (n) *n = 0; return M64ERR_UNSUPPORTED; }

static m64p_video_extension_functions g_vidext = {
    17, stub_vid_init, stub_vid_quit, stub_vid_list_modes, stub_vid_list_rates,
    stub_vid_set_mode, stub_vid_set_mode_rate, stub_vid_gl_getproc,
    stub_vid_gl_setattr, stub_vid_gl_getattr, stub_vid_gl_swapbuf,
    stub_vid_set_caption, stub_vid_toggle_fs, stub_vid_resize,
    stub_vid_gl_getfbo, stub_vid_init_render, stub_vid_vk_getsurface, stub_vid_vk_getexts,
};

/* --- Callbacks --- */

static void debug_callback(void *ctx, int level, const char *msg) {
    (void)ctx;
    if (level <= 1 && msg)
        fprintf(stderr, "[m64p] %s\n", msg);
}

static void state_callback(void *ctx, m64p_core_param param, int value) {
    (void)ctx;
    if (param == M64CORE_EMU_STATE && value == M64EMU_STOPPED) {
        g_emu_running = 0;
        pthread_mutex_lock(&g_frame_mutex);
        g_frame_ready = 1;
        pthread_cond_signal(&g_frame_cond);
        pthread_mutex_unlock(&g_frame_mutex);
    }
}

static void frame_callback(unsigned int frame) {
    (void)frame;
    pthread_mutex_lock(&g_frame_mutex);
    g_frame_ready = 1;
    pthread_cond_signal(&g_frame_cond);
    pthread_mutex_unlock(&g_frame_mutex);
}

static void wait_frame(void) {
    pthread_mutex_lock(&g_frame_mutex);
    while (!g_frame_ready)
        pthread_cond_wait(&g_frame_cond, &g_frame_mutex);
    g_frame_ready = 0;
    pthread_mutex_unlock(&g_frame_mutex);
}

static unsigned int rdram_read32(unsigned int addr) {
    addr &= 0x1FFFFFFF;
    /* mupen64plus stores RDRAM in host byte order */
    return *(unsigned int *)(g_rdram + addr);
}

static void rdram_write32(unsigned int addr, unsigned int val) {
    addr &= 0x1FFFFFFF;
    *(unsigned int *)(g_rdram + addr) = val;
}

/* --- Plugin loading --- */

static m64p_dynlib_handle load_plugin(m64p_plugin_type type, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PLUGIN_DIR, filename);

    void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "dlopen(%s): %s\n", filename, dlerror());
        return NULL;
    }

    typedef m64p_error (*fn_startup)(m64p_dynlib_handle, void*, void(*)(void*,int,const char*));
    fn_startup pStartup = (fn_startup)dlsym(handle, "PluginStartup");
    if (!pStartup) {
        dlclose(handle);
        return NULL;
    }

    void *core_handle = dlopen("/opt/homebrew/lib/libmupen64plus.dylib", RTLD_NOW | RTLD_NOLOAD);
    m64p_error err = pStartup(core_handle, NULL, (void(*)(void*,int,const char*))debug_callback);
    if (err != M64ERR_SUCCESS) {
        fprintf(stderr, "PluginStartup(%s) failed: %d\n", filename, err);
        dlclose(handle);
        return NULL;
    }

    err = CoreAttachPlugin(type, handle);
    if (err != M64ERR_SUCCESS) {
        fprintf(stderr, "CoreAttachPlugin(%d, %s) failed: %d\n", type, filename, err);
        typedef m64p_error (*fn_shutdown)(void);
        fn_shutdown pShutdown = (fn_shutdown)dlsym(handle, "PluginShutdown");
        if (pShutdown) pShutdown();
        dlclose(handle);
        return NULL;
    }

    return handle;
}

/* --- Command loop (runs on worker thread) --- */

static void *cmd_thread_fn(void *arg) {
    (void)arg;

    /* Wait for first frame before accepting commands */
    wait_frame();

    g_rdram = (unsigned char *)DebugMemGetPointer(M64P_DBG_PTR_RDRAM);
    if (!g_rdram) {
        fprintf(stderr, "DebugMemGetPointer(RDRAM) returned NULL\n");
        printf("ERR no RDRAM\n");
        fflush(stdout);
        CoreDoCommand(M64CMD_STOP, 0, NULL);
        return NULL;
    }
    fprintf(stderr, "RDRAM ptr=%p first8=[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
            g_rdram,
            g_rdram[0], g_rdram[1], g_rdram[2], g_rdram[3],
            g_rdram[4], g_rdram[5], g_rdram[6], g_rdram[7]);

    printf("READY\n");

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;

        if (strncmp(line, "ADVANCE ", 8) == 0) {
            int n = atoi(line + 8);
            int state = 0;
            CoreDoCommand(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &state);
            if (state == M64EMU_PAUSED)
                CoreDoCommand(M64CMD_RESUME, 0, NULL);
            for (int i = 0; i < n; i++) {
                if (!g_emu_running) break;
                wait_frame();
            }
            CoreDoCommand(M64CMD_PAUSE, 0, NULL);
            printf("OK\n");
        }
        else if (strncmp(line, "READ32 ", 7) == 0) {
            unsigned int addr = (unsigned int)strtoul(line + 7, NULL, 0);
            printf("VAL 0x%08X\n", rdram_read32(addr));
        }
        else if (strncmp(line, "SCAN ", 5) == 0) {
            unsigned int start, count;
            sscanf(line + 5, "%x %u", &start, &count);
            start &= 0x1FFFFFFF;
            int found = 0;
            for (unsigned int i = 0; i < count; i++) {
                unsigned int val = rdram_read32(start + i * 4);
                if (val != 0) {
                    printf("  0x%06X = %d (0x%08X)\n", start + i * 4, (int)val, val);
                    found++;
                    if (found > 50) { printf("  ...\n"); break; }
                }
            }
            printf("OK %d non-zero\n", found);
        }
        else if (strncmp(line, "WRITE32 ", 8) == 0) {
            unsigned int addr, val;
            sscanf(line + 8, "%x %x", &addr, &val);
            rdram_write32(addr, val);
            printf("OK\n");
        }
        else if (strncmp(line, "SLEEP ", 6) == 0) {
            int ms = atoi(line + 6);
            int state = 0;
            CoreDoCommand(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &state);
            if (state == M64EMU_PAUSED)
                CoreDoCommand(M64CMD_RESUME, 0, NULL);
            usleep(ms * 1000);
            CoreDoCommand(M64CMD_PAUSE, 0, NULL);
            pthread_mutex_lock(&g_frame_mutex);
            g_frame_ready = 0;
            pthread_mutex_unlock(&g_frame_mutex);
            printf("OK\n");
        }
        else if (strncmp(line, "DUMP ", 5) == 0) {
            unsigned int addr = (unsigned int)strtoul(line + 5, NULL, 0);
            addr &= 0x1FFFFFFF;
            printf("BYTES");
            for (int i = 0; i < 16; i++)
                printf(" %02X", g_rdram[addr + i]);
            printf(" LE32=0x%08X BE32=0x%08X\n",
                   *(unsigned int*)(g_rdram + addr),
                   ((unsigned int)g_rdram[addr] << 24) |
                   ((unsigned int)g_rdram[addr+1] << 16) |
                   ((unsigned int)g_rdram[addr+2] << 8) |
                    (unsigned int)g_rdram[addr+3]);
        }
        else if (strncmp(line, "WAIT ", 5) == 0) {
            /* WAIT <addr> <expected_val> <timeout_ms>
             * Polls memory until value matches, checking every ~100ms */
            unsigned int addr, expected;
            int timeout_ms;
            sscanf(line + 5, "%x %x %d", &addr, &expected, &timeout_ms);
            int state = 0;
            CoreDoCommand(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &state);
            if (state == M64EMU_PAUSED)
                CoreDoCommand(M64CMD_RESUME, 0, NULL);
            int elapsed = 0;
            int found = 0;
            while (elapsed < timeout_ms && g_emu_running) {
                unsigned int val = rdram_read32(addr);
                if (val == expected) { found = 1; break; }
                usleep(100000);
                elapsed += 100;
            }
            CoreDoCommand(M64CMD_PAUSE, 0, NULL);
            pthread_mutex_lock(&g_frame_mutex);
            g_frame_ready = 0;
            pthread_mutex_unlock(&g_frame_mutex);
            if (found)
                printf("OK\n");
            else
                printf("TIMEOUT 0x%08X\n", rdram_read32(addr));
        }
        else if (strcmp(line, "QUIT") == 0) {
            break;
        }
        else {
            printf("ERR unknown command\n");
        }
    }

    printf("BYE\n");
    fflush(stdout);

    if (g_emu_running)
        CoreDoCommand(M64CMD_STOP, 0, NULL);

    return NULL;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headed") == 0)
            g_headed = 1;
        else
            g_rom_path = argv[i];
    }

    if (!g_rom_path) {
        fprintf(stderr, "Usage: m64p_harness [--headed] <rom_path>\n");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    mkdir(CONFIG_DIR, 0755);

    SDL_Init(g_headed ? SDL_INIT_VIDEO : 0);

    m64p_error err = CoreStartup(FRONTEND_API_VERSION,
                                  CONFIG_DIR, DATA_DIR,
                                  NULL, debug_callback,
                                  NULL, state_callback);
    if (err != M64ERR_SUCCESS) {
        fprintf(stderr, "CoreStartup failed: %d\n", err);
        return 1;
    }

    if (!g_headed)
        CoreOverrideVidExt(&g_vidext);

    FILE *f = fopen(g_rom_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM: %s\n", g_rom_path);
        CoreShutdown();
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long rom_size = ftell(f);
    rewind(f);
    unsigned char *rom_data = malloc(rom_size);
    fread(rom_data, 1, rom_size, f);
    fclose(f);

    err = CoreDoCommand(M64CMD_ROM_OPEN, (int)rom_size, rom_data);
    free(rom_data);
    if (err != M64ERR_SUCCESS) {
        fprintf(stderr, "ROM_OPEN failed: %d\n", err);
        CoreShutdown();
        return 1;
    }

    /* Attach plugins (must be after ROM_OPEN).
     * Skip audio in headless mode — load it for headed so music is audible. */
    m64p_dynlib_handle h_gfx = NULL;
    if (g_headed) {
        h_gfx = load_plugin(M64PLUGIN_GFX, "mupen64plus-video-rice.dylib");
        load_plugin(M64PLUGIN_AUDIO, "mupen64plus-audio-sdl.dylib");
    }
    load_plugin(M64PLUGIN_INPUT, "mupen64plus-input-sdl.dylib");
    m64p_dynlib_handle h_rsp = load_plugin(M64PLUGIN_RSP, "mupen64plus-rsp-hle.dylib");

    if (!h_rsp) {
        fprintf(stderr, "RSP plugin required but failed to load\n");
        CoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
        CoreShutdown();
        return 1;
    }

    CoreDoCommand(M64CMD_SET_FRAME_CALLBACK, 0, (void*)frame_callback);

    /* Start command loop on worker thread */
    g_emu_running = 1;
    pthread_t cmd_tid;
    pthread_create(&cmd_tid, NULL, cmd_thread_fn, NULL);

    /* Run emulator on main thread (required for macOS Cocoa/SDL video) */
    if (!g_headed) {
        int zero = 0;
        CoreDoCommand(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &zero);
    }
    CoreDoCommand(M64CMD_EXECUTE, 0, NULL);

    /* Emulator stopped — wait for command thread to finish */
    pthread_join(cmd_tid, NULL);

    _exit(0);
}
