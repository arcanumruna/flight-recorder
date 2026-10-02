#include "rune_fr/Enable.h"

namespace rune_fr {
// Default: disabled, all categories armed (so a single enable() turns on all).
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_categoryMask{0xFFFFFFFFu};
std::atomic<uint64_t> g_slowSpanTsc{0};
std::atomic<uint64_t> g_throttleTsc{0};
} // namespace rune_fr
