#include "flight_recorder/Recorder.h"
#include "flight_recorder/Calibration.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace flight_recorder {
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
        defaultCapacity_, currentOsTid(), std::string{});
    ThreadRecorder* raw = tr.get();
    owned_.push_back(std::move(tr));
    threads_.push_back(raw);
    return raw;
}

// Bounded tape: keep only the most recent N drained records per thread (a FLIGHT
// recorder, not an archive).  Unbounded, a busy-spin thread's per-pass spans
// (millions/s) grew the tape ~60 MB/s until the process was OOM-killed.
// Histograms are accumulated at record time and stay complete regardless.
// FLIGHT_RECORDER_TAPE_RECORDS_PER_THREAD (default 2M records = 48 MiB per thread).
static std::size_t tapeCapRecords() {
    static const std::size_t cap = [] {
        if (const char* e = std::getenv("FLIGHT_RECORDER_TAPE_RECORDS_PER_THREAD")) {
            const unsigned long long v = std::strtoull(e, nullptr, 10);
            if (v > 0) return static_cast<std::size_t>(v);
        }
        return static_cast<std::size_t>(2u * 1024u * 1024u);
    }();
    return cap;
}

void Recorder::drain() {
    std::lock_guard<std::mutex> lk(mutex_);
    drainLocked_();
}

Recorder::TapeView Recorder::drainAndView() {
    std::unique_lock<std::mutex> lk(mutex_);
    drainLocked_();
    return TapeView(std::move(lk), threads_);
}

void Recorder::drainLocked_() {
    const std::size_t cap = tapeCapRecords();
    for (auto& tr : owned_) {
        tr->ring.drainInto(tr->drained);
        // Trim the OLDEST records once the tape exceeds cap by 25% (amortised:
        // one bulk erase per ~cap/4 records, cold drainer thread only).
        if (tr->drained.size() > cap + cap / 4) {
            const std::size_t excess = tr->drained.size() - cap;
            tr->drained.erase(tr->drained.begin(),
                              tr->drained.begin() + static_cast<std::ptrdiff_t>(excess));
            tr->tapeTrimmed += excess;
        }
    }
}

void Recorder::reset() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    generation_.fetch_add(1, std::memory_order_release);
    threads_.clear();
    // Retire (do not free) the rings: a producer may still hold a raw pointer
    // into one and be mid-push. Moving them to retired_ keeps that memory valid
    // until process exit; the retired ring is just never drained again.
    for (auto& tr : owned_) retired_.push_back(std::move(tr));
    owned_.clear();
}

uint64_t droppedRecords() {
    uint64_t n = 0;
    for (auto* tr : Recorder::instance().snapshotThreads()) n += tr->dropped.load(std::memory_order_relaxed);
    return n;
}

// ---- background drainer (cold; owned by flight-recorder) ----
namespace {
struct BackgroundDrain {
    std::thread       thread;
    std::atomic<bool> stop{false};
};
BackgroundDrain& backgroundDrain() {
    static BackgroundDrain d;
    return d;
}
} // namespace

void startBackgroundDrain(unsigned periodMs) {
    BackgroundDrain& d = backgroundDrain();
    if (d.thread.joinable() || periodMs == 0) return;
    d.stop.store(false, std::memory_order_relaxed);
    d.thread = std::thread([periodMs, &d] {
        while (!d.stop.load(std::memory_order_acquire)) {
            Recorder::instance().drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(periodMs));
        }
    });
    // Registered after the static above was constructed → runs before its
    // destructor: the thread is never destroyed joinable.
    static const bool registered = (std::atexit([] { stopBackgroundDrain(); }), true);
    (void)registered;
}

void stopBackgroundDrain() {
    BackgroundDrain& d = backgroundDrain();
    if (!d.thread.joinable()) return;
    d.stop.store(true, std::memory_order_release);
    d.thread.join();
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

} // namespace flight_recorder


// ---- C API (for the Aeron C driver hooks) — defined HERE, in an object that is
// always linked, so the weak references from Aeron always resolve and no
// consumer's build has to list an extra source file. ----
#include "flight_recorder/capi.h"
#include "flight_recorder/Event.h"

#if FLIGHT_RECORDER
namespace {
constexpr flight_recorder::Site kMap[FLIGHT_RECORDER_C_SITE_COUNT] = {
    flight_recorder::Site::AeronConductorDuty,
    flight_recorder::Site::AeronSenderDuty,
    flight_recorder::Site::AeronReceiverDuty,
    flight_recorder::Site::AeronSendData,
    flight_recorder::Site::AeronRecvInsert,
    flight_recorder::Site::AeronSmSent,
    flight_recorder::Site::AeronSmRecv,
    flight_recorder::Site::AeronSendSyscall,
    flight_recorder::Site::AeronRecvSyscall,
    flight_recorder::Site::AeronSenderDoSend,
    flight_recorder::Site::AeronSenderControlPoll,
    flight_recorder::Site::AeronPubSend,
    flight_recorder::Site::AeronSendBlocked,
    flight_recorder::Site::AeronHeartbeatSent,
    flight_recorder::Site::AeronSetupSent,
    flight_recorder::Site::AeronRetransmit,
    flight_recorder::Site::AeronNakRecv,
    flight_recorder::Site::AeronNakSent,
    flight_recorder::Site::AeronLossGap,
    flight_recorder::Site::AeronReceiverPoll,
    flight_recorder::Site::AeronImageClean,
    flight_recorder::Site::AeronPubClean,
    flight_recorder::Site::AeronConductorCmd,
    flight_recorder::Site::AeronRecvDispatch};
// Per-C-site runtime mask (bit i = C site i).  The per-duty-cycle Aeron spans
// (duty, recv syscall, do-send, control poll, pub send, receiver poll, image/pub
// clean) fire on EVERY idle cycle and overflowed the per-thread rings (36% of
// records dropped), hiding the hop events.  Default keeps the hop-explaining
// sites; FLIGHT_RECORDER_AERON_MASK=<hex> overrides (0xFFFFFF = everything).
constexpr uint32_t kDefaultAeronMask =
    (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 12) | (1u << 13) |
    (1u << 14) | (1u << 15) | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 22) | (1u << 23);
inline uint32_t aeronMask() noexcept {
    static const uint32_t m = [] {
        if (const char* e = std::getenv("FLIGHT_RECORDER_AERON_MASK")) return static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
        return kDefaultAeronMask;
    }();
    return m;
}
inline bool mapSite(uint16_t c, flight_recorder::Site& out) noexcept {
    if (c >= FLIGHT_RECORDER_C_SITE_COUNT || ((aeronMask() >> c) & 1u) == 0) return false;
    out = kMap[c];
    return flight_recorder::enabled(out);
}
} // namespace

// A8: 0 while the recorder is disabled — no rdtscp per Aeron duty-cycle hook (a span
// started at 0 records nothing, see flight_recorder_c_span).
extern "C" uint64_t flight_recorder_c_now(void) { return flight_recorder::isEnabled() ? flight_recorder::rdtscp() : 0; }

extern "C" void flight_recorder_c_span(uint16_t c, uint32_t corr, uint64_t start, double value) {
    flight_recorder::Site s;
    if (start == 0 || !mapSite(c, s)) return;   // FR-L: the free check first (a disabled-time start)
    flight_recorder::pushRecord(flight_recorder::FrRecord{start, flight_recorder::rdtscp(), corr, static_cast<uint16_t>(s), 0, value});
}

extern "C" void flight_recorder_c_mark(uint16_t c, uint32_t corr, double value) {
    flight_recorder::Site s;
    if (!mapSite(c, s)) return;
    const uint64_t t = flight_recorder::rdtscp();
    flight_recorder::pushRecord(flight_recorder::FrRecord{t, t, corr, static_cast<uint16_t>(s), 0, value});
}
#else
extern "C" uint64_t flight_recorder_c_now(void) { return 0; }
extern "C" void flight_recorder_c_span(uint16_t, uint32_t, uint64_t, double) {}
extern "C" void flight_recorder_c_mark(uint16_t, uint32_t, double) {}
#endif
