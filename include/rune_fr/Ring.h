#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "rune_fr/FrRecord.h"

namespace rune_fr {

enum class RingMode { OverwriteOldest, StopWhenFull };

// Single-producer (owning thread) / single-consumer (drainer) POD ring.
// The only cross-thread sync is writeIndex_ published with release and read
// with acquire. No locks, no allocation, no syscalls on push().
class Ring {
public:
    Ring(size_t capacityPow2, RingMode mode)
        : capacity_(capacityPow2),
          mask_(capacityPow2 - 1),
          mode_(mode),
          buffer_(std::make_unique<FrRecord[]>(capacityPow2)) {
        assert(capacityPow2 != 0 && (capacityPow2 & (capacityPow2 - 1)) == 0 &&
               "Ring capacity must be a nonzero power of two");
    }

    void push(const FrRecord& r) noexcept {
        uint64_t idx = writeIndex_.load(std::memory_order_relaxed);
        if (mode_ == RingMode::StopWhenFull && idx >= capacity_) return;
        buffer_[idx & mask_] = r;
        writeIndex_.store(idx + 1, std::memory_order_release);
    }

    [[nodiscard]] uint64_t writeCount() const noexcept {
        return writeIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] RingMode mode() const noexcept { return mode_; }

    // Copy currently-live records in chronological (oldest-first) order.
    size_t snapshot(std::vector<FrRecord>& out) const {
        uint64_t total = writeIndex_.load(std::memory_order_acquire);
        uint64_t live = total < capacity_ ? total : capacity_;
        uint64_t start = total - live; // absolute index of oldest live record
        out.clear();
        out.reserve(live);
        for (uint64_t i = 0; i < live; ++i) {
            out.push_back(buffer_[(start + i) & mask_]);
        }
        return static_cast<size_t>(live);
    }

private:
    size_t capacity_;
    size_t mask_;
    RingMode mode_;
    std::unique_ptr<FrRecord[]> buffer_;
    std::atomic<uint64_t> writeIndex_{0};
};

} // namespace rune_fr
