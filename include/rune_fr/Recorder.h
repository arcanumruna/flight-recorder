#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rune_fr/Ring.h"

namespace rune_fr {

inline constexpr size_t kDefaultRingCapacity = 65536; // 1.5 MiB/thread at 24 B

struct ThreadRecorder {
    Ring ring;
    uint64_t osTid;
    std::string threadName;

    ThreadRecorder(size_t cap, RingMode mode, uint64_t tid, std::string name)
        : ring(cap, mode), osTid(tid), threadName(std::move(name)) {}
};

class Recorder {
public:
    static Recorder& instance() noexcept;

    void configure(size_t capacityPow2, RingMode mode) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        defaultCapacity_ = capacityPow2;
        defaultMode_ = mode;
    }

    ThreadRecorder* registerThread();

    std::vector<ThreadRecorder*> snapshotThreads() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return threads_;
    }

    // Bumped by reset() so stale thread-local pointers are invalidated.
    [[nodiscard]] uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    void reset() noexcept;

private:
    Recorder() = default;

    mutable std::mutex mutex_;
    std::vector<ThreadRecorder*> threads_;
    std::vector<std::unique_ptr<ThreadRecorder>> owned_;
    size_t defaultCapacity_ = kDefaultRingCapacity;
    RingMode defaultMode_ = RingMode::OverwriteOldest;
    std::atomic<uint64_t> generation_{0};
};

// TLS accessor: first call registers this thread (cold), then pure TLS load.
ThreadRecorder* threadRecorder() noexcept;

} // namespace rune_fr
