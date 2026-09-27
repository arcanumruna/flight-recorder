#pragma once

#include <cstdint>
#include "rune_fr/Config.h"

#if defined(RUNE_FR_X86)
#include <x86intrin.h>
#include <cpuid.h>
#endif
#if !defined(RUNE_FR_X86)
#include <ctime>
#endif

namespace rune_fr {

// Read the timestamp counter. Ordering is guaranteed within a thread, which is
// all the write path needs (same-thread start/end). Cross-core absolute ordering
// assumes invariant + synchronized TSC (documented; true on modern single-socket).
inline uint64_t rdtscp() noexcept {
#if defined(RUNE_FR_X86)
    unsigned aux;
    return __builtin_ia32_rdtscp(&aux);
#elif defined(RUNE_FR_ARM)
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
#endif
}

// Nominal counter frequency in Hz for the non-x86 counters, when known.
// On ARM, cntfrq_el0 gives the cntvct_el0 tick rate directly.
inline uint64_t counterFrequencyHz() noexcept {
#if defined(RUNE_FR_ARM)
    uint64_t f;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
#else
    return 0; // unknown / measured by calibration
#endif
}

inline bool hasInvariantTsc() noexcept {
#if defined(RUNE_FR_X86)
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(0x80000007u, &eax, &ebx, &ecx, &edx)) return false;
    return (edx & (1u << 8)) != 0; // invariant TSC bit
#else
    return true; // ARM cntvct and the ns fallback are inherently stable
#endif
}

} // namespace rune_fr
