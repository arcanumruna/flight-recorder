#pragma once

#include <cstdint>

namespace flight_recorder {

// Measure TSC ticks per nanosecond once at startup (idempotent). On the ns
// fallback path tscPerNs stays 1.0 (rdtscp already returns ns).
void calibrate() noexcept;

double tscPerNs() noexcept;

inline double cyclesToNs(uint64_t cycles) noexcept {
    return static_cast<double>(cycles) / tscPerNs();
}

inline uint64_t nsToCycles(double ns) noexcept {
    return static_cast<uint64_t>(ns * tscPerNs());
}

} // namespace flight_recorder
