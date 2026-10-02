#pragma once

#include <atomic>
#include <cstdint>

#include "rune_fr/Sites.h"

namespace rune_fr {

// Definitions live in Enable.cpp so the atomics have one home.
extern std::atomic<bool> g_enabled;
extern std::atomic<uint32_t> g_categoryMask;
// Read ONLY on the enabled path (never by a disabled site).
extern std::atomic<uint64_t> g_slowSpanTsc;   // SlowSpan commit threshold (0 = commit all)
extern std::atomic<uint64_t> g_throttleTsc;   // Throttle min gap between marks (0 = no coalescing)

constexpr uint32_t categoryBit(Category c) noexcept {
    return 1u << static_cast<uint32_t>(c);
}

inline void enable() noexcept { g_enabled.store(true, std::memory_order_relaxed); }
inline void disable() noexcept { g_enabled.store(false, std::memory_order_relaxed); }
inline bool isEnabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

inline void setCategoryMask(uint32_t mask) noexcept {
    g_categoryMask.store(mask, std::memory_order_relaxed);
}
inline uint32_t categoryMask() noexcept {
    return g_categoryMask.load(std::memory_order_relaxed);
}

// SlowSpan: a span shorter than this is not recorded (keeps per-pass spans of a
// busy-spinning loop from flooding the ring; outliers are what matter).
inline void setSlowSpanTsc(uint64_t tsc) noexcept { g_slowSpanTsc.store(tsc, std::memory_order_relaxed); }
inline uint64_t slowSpanTsc() noexcept { return g_slowSpanTsc.load(std::memory_order_relaxed); }
// Throttle: repeated marks from one call site closer than this are coalesced
// into the next emitted mark (its value = the number of events it stands for).
inline void setThrottleTsc(uint64_t tsc) noexcept { g_throttleTsc.store(tsc, std::memory_order_relaxed); }
inline uint64_t throttleTsc() noexcept { return g_throttleTsc.load(std::memory_order_relaxed); }

inline bool enabled(Site s) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    return (g_categoryMask.load(std::memory_order_relaxed) &
            categoryBit(categoryOf(s))) != 0;
}

} // namespace rune_fr
