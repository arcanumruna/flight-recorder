#pragma once
// Keys.h — the 32-bit correlation keys every Rune producer / broker site uses, so
// one batch is chained end to end across processes (client -> broker leader ->
// followers -> leader -> client) by an offline analyzer.  Pure, header-only,
// constexpr: available (and free) whether the recorder is compiled in or out.

#include <cstdint>

namespace flight_recorder {

// The client batch correlation id folded to 32 bits, attempt-free.  The Rune
// client's batch id is tag | ordinal<<50 | slot<<32 | attempt<<24 | seq(24);
// the fold keeps the partition slot's low byte and the 24-bit sequence and drops
// the attempt tag, so every attempt (retransmit) of one batch shares the key and
// batches of different partitions do not collide.  A small plain id (< 2^24,
// e.g. a legacy client) folds to itself.  Broker and client compute the SAME
// fold from the same 64-bit wire value.
constexpr uint32_t foldCorr(int64_t correlationId) noexcept {
    const auto c = static_cast<uint64_t>(correlationId);
    return (static_cast<uint32_t>((c >> 32) & 0xFFu) << 24) | static_cast<uint32_t>(c & 0xFFFFFFu);
}

// (topic, partition, offset) -> ((topic+8)&0xF)<<28 | (partition&0xF)<<24 | offset&0xFFFFFF.
// Internal topics (negative ids down to -8) keep distinct nibbles.  The offset is
// 24 bits: an analyzer unwraps it per (topic, partition) series (monotonic).
constexpr uint32_t partKey(int32_t topic, int32_t partition, int64_t offset) noexcept {
    return (static_cast<uint32_t>((topic + 8) & 0xF) << 28) |
           (static_cast<uint32_t>(partition & 0xF) << 24) |
           (static_cast<uint32_t>(static_cast<uint64_t>(offset)) & 0xFFFFFFu);
}

} // namespace flight_recorder
