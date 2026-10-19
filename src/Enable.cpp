#include "flight_recorder/Enable.h"

namespace flight_recorder {
// Default: disabled, all categories armed (so a single enable() turns on all).
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_categoryMask{0xFFFFFFFFu};
std::atomic<uint64_t> g_slowSpanTsc{0};
std::atomic<uint64_t> g_throttleTsc{0};
} // namespace flight_recorder
