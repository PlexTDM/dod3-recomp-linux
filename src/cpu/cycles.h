/*
 * A cheap cycle counter for the timing hooks (src/patches/gc_native.cpp,
 * src/cpu/hot.cpp): the TSC on x86-64, the steady clock in ns elsewhere --
 * the reports call both "cycles"; compare within a platform only.
 */
#pragma once
#include <stdint.h>
#if defined(__x86_64__)
#include <x86intrin.h>
static inline uint64_t dod3_cycles(void) { return __rdtsc(); }
#else
#include <chrono>
static inline uint64_t dod3_cycles(void)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
#endif
