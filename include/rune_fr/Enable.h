#pragma once

#include <atomic>
#include <cstdint>

#include "rune_fr/Sites.h"

namespace rune_fr {

// Definitions live in Enable.cpp so the atomics have one home.
extern std::atomic<bool> g_enabled;
extern std::atomic<uint32_t> g_categoryMask;

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

inline bool enabled(Site s) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    return (g_categoryMask.load(std::memory_order_relaxed) &
            categoryBit(categoryOf(s))) != 0;
}

} // namespace rune_fr
