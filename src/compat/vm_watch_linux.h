#ifndef DOD3_VM_WATCH_LINUX_H
#define DOD3_VM_WATCH_LINUX_H

#include <stdint.h>

static inline void vm_watch_init(uint8_t* base, uint64_t size)
{
    (void)base;
    (void)size;
}

static inline int vm_watch_fault(uintptr_t addr, int is_write)
{
    (void)addr;
    (void)is_write;
    return 0;
}

#endif
