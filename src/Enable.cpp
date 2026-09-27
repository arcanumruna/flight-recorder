#include "rune_fr/Enable.h"

namespace rune_fr {
// Default: disabled, all categories armed (so a single enable() turns on all).
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_categoryMask{0xFFFFFFFFu};
} // namespace rune_fr
