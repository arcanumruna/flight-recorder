#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rune_fr/FrRecord.h"
#include "rune_fr/detail/FrRing.h"

namespace rune_fr {

// 16 MiB payload per thread (power of two, per spec/user default). Configurable.
inline constexpr size_t kDefaultRingCapacity = 16u * 1024u * 1024u;

struct ThreadRecorder {
    detail::FrRing ring;          // per-thread M2O ring (single producer)
    std::vector<FrRecord> drained; // records pulled out by the one drainer
    uint64_t osTid;
    std::string threadName;

    ThreadRecorder(size_t payloadCapacityPow2, uint64_t tid, std::string name)
        : ring(payloadCapacityPow2), osTid(tid), threadName(std::move(name)) {}
};

class Recorder {
public:
    static Recorder& instance() noexcept;

    // Sets the per-thread ring size for threads registered afterward.
    void configure(size_t payloadCapacityPow2) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        defaultCapacity_ = payloadCapacityPow2;
    }

    ThreadRecorder* registerThread();

    std::vector<ThreadRecorder*> snapshotThreads() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return threads_;
    }

    // The single drainer: pull all currently-committed records out of every
    // thread ring into that thread's `drained` accumulator. Off the hot path.
    // Idempotent when rings are empty; safe to call before each report.
    void drain();

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
    std::atomic<uint64_t> generation_{0};
};

// TLS accessor: first call registers this thread (cold), then pure TLS load.
ThreadRecorder* threadRecorder() noexcept;

} // namespace rune_fr
