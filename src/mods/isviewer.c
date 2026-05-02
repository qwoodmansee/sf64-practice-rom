#include "PR/xstdio.h"
#include "PR/rcp.h"
#include "PR/os_internal.h"
#include "libc/stdarg.h"

// SummerCart64 IS-Viewer 64.
//
// Host: `sc64deployer debug --isv 0x03FF0000`
// Cart-bus base = 0x10000000 + 0x03FF0000 = 0x13FF0000
//
// Critical SC64/PI gotcha: back-to-back direct CPU writes to cart space
// drop after the first few. A dummy IO_READ between writes drains the
// PI bus and lets the next write through. We wrap every write in
// PI_WRITE_FLUSH() to enforce this.

#define ISV_TOKEN                0x49533634

#define ISV_BASE                 0x13FF0000
#define ISV_TOKEN_OFF            0x00
#define ISV_READ_PTR_OFF         0x04
#define ISV_WRITE_PTR_OFF        0x14
#define ISV_BUFFER_OFF           0x20

#define ISV_MAX_FLUSH            512

/* If drain-wait times out this many calls in a row, the host has fallen
 * behind in a way we can't recover from cleanly. Kill the channel rather
 * than continue to spew torn rp/wp pairs that the SC64 firmware mistakes
 * for buffer-wraps and dumps as 64KB garbage to USB ("ZZZZZ" storm). */
#define ISV_FAILURE_LIMIT        5

#define PI_WRITE(addr, val)      do {                  \
    IO_WRITE((addr), (val));                           \
    (void) IO_READ(ISV_BASE + ISV_TOKEN_OFF);          \
} while (0)

static u8 sISViewerInitialized = 0;
static u8 sISViewerFailureCount = 0;

static void ISViewer_Write(const u8* data, s32 len);

void ISViewer_Init(void) {
    sISViewerInitialized = 1;
    sISViewerFailureCount = 0;
    PI_WRITE(ISV_BASE + ISV_TOKEN_OFF,     ISV_TOKEN);
    PI_WRITE(ISV_BASE + ISV_READ_PTR_OFF,  0);
    PI_WRITE(ISV_BASE + ISV_WRITE_PTR_OFF, 0);
}

static void ISViewer_Write(const u8* data, s32 len) {
    s32 i;
    s32 retries;
    u32 word;
    u32 rp;
    u32 wp;
    u32 saved_int_mask;

    if ((!sISViewerInitialized) || (len <= 0)) {
        return;
    }
    if (sISViewerFailureCount >= ISV_FAILURE_LIMIT) {
        /* Channel previously fell out of sync and the host is no longer
         * draining cleanly. Stay quiet so we don't keep flooding the
         * deployer with garbage. */
        return;
    }
    if (len > ISV_MAX_FLUSH) {
        len = ISV_MAX_FLUSH;
    }

    /* The rp/wp dance must be atomic from the SC64 firmware's POV. Any
     * preemption (timer/SI/SP/DP interrupt, thread switch) that lands
     * between PI writes can leave the firmware polling a torn (wp < rp)
     * pair, which it interprets as a buffer wrap and dumps ~64KB of
     * garbage to USB. Disable interrupts for the dance. __osDisableInt
     * is a no-op pre-osInitialize and safe to call from any context. */
    saved_int_mask = __osDisableInt();

    /* Wait for prior flush to drain (rp catches up to wp). */
    for (retries = 200000; retries > 0; retries--) {
        rp = IO_READ(ISV_BASE + ISV_READ_PTR_OFF);
        wp = IO_READ(ISV_BASE + ISV_WRITE_PTR_OFF);
        if (rp == wp) {
            break;
        }
    }
    if (retries == 0) {
        /* Host fell behind. Proceeding anyway risks creating a wp<rp state
         * that the SC64 firmware mistakes for a buffer wrap and dumps 64KB
         * of garbage to USB. Drop this message; tally the failure so we
         * eventually disable the channel if it stays wedged. */
        sISViewerFailureCount++;
        __osRestoreInt(saved_int_mask);
        return;
    }
    sISViewerFailureCount = 0;

    /* Park rp outside the valid range so the SC64 firmware bails on
     * (rp >= ISV_BUFFER_SIZE) and ignores us during the rewrite. */
    PI_WRITE(ISV_BASE + ISV_READ_PTR_OFF, 0xFFFFFFFF);
    PI_WRITE(ISV_BASE + ISV_WRITE_PTR_OFF, 0);

    for (i = 0; i + 4 <= len; i += 4) {
        word = ((u32) data[i] << 24) | ((u32) data[i + 1] << 16) |
               ((u32) data[i + 2] << 8) | ((u32) data[i + 3] << 0);
        PI_WRITE(ISV_BASE + ISV_BUFFER_OFF + i, word);
    }
    if (i < len) {
        word = 0;
        if (i < len)         word |= (u32) data[i]     << 24;
        if (i + 1 < len)     word |= (u32) data[i + 1] << 16;
        if (i + 2 < len)     word |= (u32) data[i + 2] << 8;
        PI_WRITE(ISV_BASE + ISV_BUFFER_OFF + i, word);
    }

    /* Re-arm: rp valid (==wp=0), then bump wp to trigger a flush. */
    PI_WRITE(ISV_BASE + ISV_READ_PTR_OFF, 0);
    PI_WRITE(ISV_BASE + ISV_WRITE_PTR_OFF, len);

    __osRestoreInt(saved_int_mask);
}

// Accumulator state passed through _Printf as the prout callback's "arg".
typedef struct {
    char* dst;
    char* end;
} ISViewerAccum;

static char* ISViewer_AccumPrintf(char* arg, const char* str, size_t count) {
    ISViewerAccum* acc = (ISViewerAccum*) arg;
    size_t available;

    if (acc->dst >= acc->end) {
        return arg;
    }
    available = (size_t) (acc->end - acc->dst);
    if (count > available) {
        count = available;
    }
    bcopy((void*) str, acc->dst, (s32) count);
    acc->dst += count;
    return arg;
}

void osSyncPrintf(const char* fmt, ...) {
    va_list args;
    char buffer[ISV_MAX_FLUSH];
    ISViewerAccum acc;
    s32 len;

    if (!sISViewerInitialized) {
        return;
    }

    acc.dst = buffer;
    acc.end = buffer + sizeof(buffer);

    va_start(args, fmt);
    _Printf(ISViewer_AccumPrintf, (char*) &acc, fmt, args);
    va_end(args);

    len = (s32) (acc.dst - buffer);
    if (len > 0) {
        ISViewer_Write((const u8*) buffer, len);
    }
}
