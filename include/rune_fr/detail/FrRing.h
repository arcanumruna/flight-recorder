#pragma once
// FrRing — a per-thread flight-recorder ring: a fixed byte buffer owned here,
// framed by the isolated M2O ring. One producer (the owning thread) pushes
// fixed-size FrRecords via claim/commit; one drainer reads them back.
// Drop-on-full: push() returns false when the ring is full (never blocks).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "rune_fr/FrRecord.h"
#include "rune_fr/detail/M2ORingBuffer.h"

namespace rune_fr::detail {

class FrRing {
public:
    static constexpr std::int32_t kMsgTypeId = 1; // FrRecord payload

    // Smallest payload capacity for which the ring's maxMsgLength (= capacity/8)
    // can still hold one FrRecord. Below this, tryClaim(1, sizeof(FrRecord))
    // would throw — and since push() is noexcept that would std::terminate.
    // Power of two so the M2O capacity check passes: 8 * sizeof(FrRecord) = 192
    // rounds up to 256.
    static constexpr std::size_t kMinPayloadCapacity = 256;

    // payloadCapacityPow2: usable payload bytes (must be a power of two). Values
    // below kMinPayloadCapacity are clamped up so push() can never throw.
    explicit FrRing(std::size_t payloadCapacityPow2)
        : buf_(effectiveCapacity(payloadCapacityPow2) +
                   static_cast<std::size_t>(M2ORingBuffer::trailerLength()),
               0),
          ring_(buf_.data(), buf_.size()) {}

    FrRing(const FrRing&) = delete;
    FrRing& operator=(const FrRing&) = delete;

    // Producer (owning thread). Returns false if the record was dropped (full).
    // No allocation, no lock: claim -> memcpy(24B) -> commit.
    bool push(const FrRecord& r) noexcept {
        const aeron::util::index_t off = ring_.tryClaim(kMsgTypeId, sizeof(FrRecord));
        if (off < 0) return false; // INSUFFICIENT_CAPACITY -> drop
        std::memcpy(ring_.base() + off, &r, sizeof(FrRecord));
        ring_.commit(off);
        return true;
    }

    // Consumer (single drainer). Appends all currently-committed records to out
    // (out.push_back(const FrRecord&)), consuming them from the ring. Returns count.
    template <typename Out>
    std::size_t drainInto(Out& out) {
        std::size_t drained = 0;
        int read = 0;
        do {
            read = ring_.read([&](std::int32_t, aeron::concurrent::AtomicBuffer& b,
                                  aeron::util::index_t offset, aeron::util::index_t) {
                FrRecord r;
                std::memcpy(&r, b.buffer() + offset, sizeof(FrRecord));
                out.push_back(r);
                ++drained;
            });
        } while (read > 0);
        return drained;
    }

    [[nodiscard]] std::size_t payloadCapacity() const {
        return static_cast<std::size_t>(ring_.capacity());
    }

private:
    static std::size_t effectiveCapacity(std::size_t requested) noexcept {
        return requested < kMinPayloadCapacity ? kMinPayloadCapacity : requested;
    }

    std::vector<std::uint8_t> buf_;
    M2ORingBuffer ring_;
};

} // namespace rune_fr::detail
