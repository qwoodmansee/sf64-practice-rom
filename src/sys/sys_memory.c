#include "sys.h"

s32 sMemoryBuffer[0x2200];
s32* sMemoryPtr;

void Memory_FreeAll(void) {
    sMemoryPtr = sMemoryBuffer;
}

void* Memory_Allocate(s32 size) {
    void* addr = sMemoryPtr;

    sMemoryPtr = (void*) (((size + 0xF) & ~0xF) + (uintptr_t) sMemoryPtr);
    return addr;
}

#ifdef PRACTICE_ROM
u32 Practice_MemoryGetBumpUsed(void) {
    return (u32)((uintptr_t)sMemoryPtr - (uintptr_t)sMemoryBuffer);
}
#endif
