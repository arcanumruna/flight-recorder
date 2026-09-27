#include "rune_fr/Recorder.h"
#include "rune_fr/Calibration.h"

#include <functional>
#include <thread>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace rune_fr {
namespace {
uint64_t currentOsTid() noexcept {
#if defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
}
} // namespace

Recorder& Recorder::instance() noexcept {
    static Recorder r;
    return r;
}

ThreadRecorder* Recorder::registerThread() {
    // Calibrate lazily on first registration so reports have tscPerNs.
    calibrate();
    std::lock_guard<std::mutex> lk(mutex_);
    auto tr = std::make_unique<ThreadRecorder>(
        defaultCapacity_, defaultMode_, currentOsTid(), std::string{});
    ThreadRecorder* raw = tr.get();
    owned_.push_back(std::move(tr));
    threads_.push_back(raw);
    return raw;
}

void Recorder::reset() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    generation_.fetch_add(1, std::memory_order_release);
    threads_.clear();
    owned_.clear();
}

ThreadRecorder* threadRecorder() noexcept {
    // Reset-aware: if the registry was cleared, the cached pointer is stale.
    // A generation counter keeps the hot path a single load in steady state.
    thread_local ThreadRecorder* cached = nullptr;
    thread_local uint64_t cachedGen = 0;
    uint64_t gen = Recorder::instance().generation();
    if (cached && cachedGen == gen) return cached;
    cached = Recorder::instance().registerThread();
    cachedGen = gen;
    return cached;
}

} // namespace rune_fr
