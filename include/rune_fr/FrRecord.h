#pragma once

#include <cstdint>
#include <bit>

namespace rune_fr {

// v1 record: 24 bytes, 8-byte aligned. Absolute start+end stamps (not just
// duration) so the exporter can place spans on a shared timeline. The 16-byte
// {start,dur32,...} variant was rejected for v1 (spec §2.2): clarity + timeline
// win over halving memory; revisit only under measured memory pressure.
struct FrRecord {
    uint64_t startTsc; // TSC at begin()
    uint64_t endTsc;   // TSC at commit(); == startTsc for an instant/mark
    uint32_t corr;     // correlation id (0 = none)
    uint16_t site;     // Site
    uint16_t arg;      // small per-site payload; 0 if unused

    // Guard against end-before-start (clock skew / equal stamps): never underflow.
    [[nodiscard]] uint64_t durationTsc() const noexcept {
        return endTsc >= startTsc ? endTsc - startTsc : 0;
    }
};

static_assert(sizeof(FrRecord) == 24, "FrRecord must be 24 bytes");
static_assert(alignof(FrRecord) == 8, "FrRecord must be 8-byte aligned");
static_assert(std::endian::native == std::endian::little,
              "rune-fr assumes a little-endian target (matches Rune)");

} // namespace rune_fr
