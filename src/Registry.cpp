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
        defaultCapacity_, currentOsTid(), std::string{});
    ThreadRecorder* raw = tr.get();
    owned_.push_back(std::move(tr));
    threads_.push_back(raw);
    return raw;
}

void Recorder::drain() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& tr : owned_) {
        tr->ring.drainInto(tr->drained);
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


// ---- C API (for the Aeron C driver hooks) — defined HERE, in an object that is
// always linked, so the weak references from Aeron always resolve and no
// consumer's build has to list an extra source file. ----
#include "rune_fr/capi.h"
#include "rune_fr/Event.h"

#if RUNE_FR
namespace {
constexpr rune_fr::Site kMap[RUNE_FR_C_SITE_COUNT] = {
    rune_fr::Site::AeronConductorDuty,
    rune_fr::Site::AeronSenderDuty,
    rune_fr::Site::AeronReceiverDuty,
    rune_fr::Site::AeronSendData,
    rune_fr::Site::AeronRecvInsert,
    rune_fr::Site::AeronSmSent,
    rune_fr::Site::AeronSmRecv,
    rune_fr::Site::AeronSendSyscall,
    rune_fr::Site::AeronRecvSyscall,
    rune_fr::Site::AeronSenderDoSend,
    rune_fr::Site::AeronSenderControlPoll,
    rune_fr::Site::AeronPubSend,
    rune_fr::Site::AeronSendBlocked,
    rune_fr::Site::AeronHeartbeatSent,
    rune_fr::Site::AeronSetupSent,
    rune_fr::Site::AeronRetransmit,
    rune_fr::Site::AeronNakRecv,
    rune_fr::Site::AeronNakSent,
    rune_fr::Site::AeronLossGap,
    rune_fr::Site::AeronReceiverPoll,
    rune_fr::Site::AeronImageClean,
    rune_fr::Site::AeronPubClean,
    rune_fr::Site::AeronConductorCmd,
    rune_fr::Site::AeronRecvDispatch};
inline bool mapSite(uint16_t c, rune_fr::Site& out) noexcept {
    if (c >= RUNE_FR_C_SITE_COUNT) return false;
    out = kMap[c];
    return rune_fr::enabled(out);
}
} // namespace

extern "C" uint64_t rune_fr_c_now(void) { return rune_fr::rdtscp(); }

extern "C" void rune_fr_c_span(uint16_t c, uint32_t corr, uint64_t start, double value) {
    rune_fr::Site s;
    if (!mapSite(c, s) || start == 0) return;
    rune_fr::threadRecorder()->ring.push(
        rune_fr::FrRecord{start, rune_fr::rdtscp(), corr, static_cast<uint16_t>(s), 0, value});
}

extern "C" void rune_fr_c_mark(uint16_t c, uint32_t corr, double value) {
    rune_fr::Site s;
    if (!mapSite(c, s)) return;
    const uint64_t t = rune_fr::rdtscp();
    rune_fr::threadRecorder()->ring.push(rune_fr::FrRecord{t, t, corr, static_cast<uint16_t>(s), 0, value});
}
#else
extern "C" uint64_t rune_fr_c_now(void) { return 0; }
extern "C" void rune_fr_c_span(uint16_t, uint32_t, uint64_t, double) {}
extern "C" void rune_fr_c_mark(uint16_t, uint32_t, double) {}
#endif
