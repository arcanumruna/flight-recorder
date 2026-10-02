#pragma once

#include <cstdint>

#include "rune_fr/Config.h"
#include "rune_fr/Sites.h"

#if RUNE_FR

#include "rune_fr/FrRecord.h"
#include "rune_fr/Tsc.h"
#include "rune_fr/Enable.h"
#include "rune_fr/Recorder.h"

namespace rune_fr {

class Event {
public:
    explicit Event(Site s) noexcept
        : rec_{0, 0, 0, static_cast<uint16_t>(s), 0, 0.0}, on_(false) {}

    // Gate ONCE here so the disabled path is two relaxed loads + a branch.
    Event& begin() noexcept {
        on_ = enabled(static_cast<Site>(rec_.site));
        if (on_) rec_.startTsc = rdtscp();
        return *this;
    }
    Event& corr(uint32_t c) noexcept { rec_.corr = c; return *this; }
    Event& arg(uint16_t a) noexcept { rec_.arg = a; return *this; }
    Event& value(double v) noexcept { rec_.value = v; return *this; }

    void commit() noexcept {
        if (!on_) return;
        rec_.endTsc = rdtscp();
        pushRecord(rec_);
    }

    // Commit only if the span lasted at least `minTsc` ticks (SlowSpan).
    void commitIfAtLeast(uint64_t minTsc) noexcept {
        if (!on_) return;
        rec_.endTsc = rdtscp();
        if (rec_.endTsc - rec_.startTsc < minTsc) return;
        pushRecord(rec_);
    }

private:
    FrRecord rec_;
    bool on_;
};

inline void mark(Site s, uint32_t corr, uint16_t arg = 0) noexcept {
    if (!enabled(s)) return;
    uint64_t t = rdtscp();
    pushRecord(FrRecord{t, t, corr, static_cast<uint16_t>(s), arg, 0.0});
}

// Mark carrying a free-form custom double value (e.g. measured latency µs,
// inFlight depth, batch bytes).  corr still correlates across hops.
inline void markVal(Site s, uint32_t corr, double value) noexcept {
    if (!enabled(s)) return;
    uint64_t t = rdtscp();
    pushRecord(FrRecord{t, t, corr, static_cast<uint16_t>(s), 0, value});
}

// Mark carrying both the small per-site arg (reason / worker / partition) and a value.
inline void markArgVal(Site s, uint32_t corr, uint16_t arg, double value) noexcept {
    if (!enabled(s)) return;
    uint64_t t = rdtscp();
    pushRecord(FrRecord{t, t, corr, static_cast<uint16_t>(s), arg, value});
}

// A span that is recorded only when it lasted >= slowSpanTsc() (per-pass /
// per-stage spans of a busy-spinning loop: only the outliers are kept, so the
// ring holds a whole run).  Disabled cost: identical to ScopedSpan.
class SlowSpan {
public:
    SlowSpan(Site s, uint32_t corr) noexcept : ev_(s) { ev_.begin().corr(corr); }
    ~SlowSpan() { ev_.commitIfAtLeast(slowSpanTsc()); }
    SlowSpan(const SlowSpan&) = delete;
    SlowSpan& operator=(const SlowSpan&) = delete;

private:
    Event ev_;
};

// Per-call-site coalescer for marks that can repeat every pass while a
// condition lasts (a refused offer retried each pass, a skipped poll): at most
// one record per throttleTsc() ticks; the emitted record's value is the number
// of events it stands for (this one + the coalesced ones since the last).  A
// plain member of the (single-threaded) owner; disabled cost: one enabled()
// check, the members are untouched.
struct Throttle {
    uint64_t lastTsc = 0;
    uint32_t pending = 0;

    void hit(Site s, uint32_t corr, uint16_t arg) noexcept {
        if (!enabled(s)) return;
        const uint64_t t = rdtscp();
        ++pending;
        if (t - lastTsc < throttleTsc()) return;
        lastTsc = t;
        pushRecord(FrRecord{t, t, corr, static_cast<uint16_t>(s), arg, static_cast<double>(pending)});
        pending = 0;
    }
};

class ScopedSpan {
public:
    ScopedSpan(Site s, uint32_t corr) noexcept : ev_(s) { ev_.begin().corr(corr); }
    ~ScopedSpan() { ev_.commit(); }
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

private:
    Event ev_;
};

} // namespace rune_fr

#define RUNE_FR_SPAN(site, corr) \
    ::rune_fr::ScopedSpan RUNE_FR_CONCAT(_fr_span_, __LINE__){(site), (corr)}
#define RUNE_FR_MARK(site, corr) ::rune_fr::mark((site), (corr))
#define RUNE_FR_MARK_VAL(site, corr, value) ::rune_fr::markVal((site), (corr), (value))
#define RUNE_FR_MARK_ARG_VAL(site, corr, arg, value) ::rune_fr::markArgVal((site), (corr), (arg), (value))
#define RUNE_FR_SLOW_SPAN(site, corr) \
    ::rune_fr::SlowSpan RUNE_FR_CONCAT(_fr_slow_, __LINE__){(site), (corr)}
#define RUNE_FR_THROTTLED(throttle, site, corr, arg) (throttle).hit((site), (corr), (arg))

#else // !RUNE_FR — compiled out: zero code.

namespace rune_fr {
class Event {
public:
    explicit Event(Site) noexcept {}
    Event& begin() noexcept { return *this; }
    Event& corr(uint32_t) noexcept { return *this; }
    Event& arg(uint16_t) noexcept { return *this; }
    Event& value(double) noexcept { return *this; }
    void commit() noexcept {}
};
inline void mark(Site, uint32_t, uint16_t = 0) noexcept {}
inline void markVal(Site, uint32_t, double) noexcept {}
inline void markArgVal(Site, uint32_t, uint16_t, double) noexcept {}
class ScopedSpan {
public:
    ScopedSpan(Site, uint32_t) noexcept {}
};
class SlowSpan {
public:
    SlowSpan(Site, uint32_t) noexcept {}
};
struct Throttle {
    void hit(Site, uint32_t, uint16_t) noexcept {}
};
} // namespace rune_fr

#define RUNE_FR_SPAN(site, corr) do {} while (0)
#define RUNE_FR_MARK(site, corr) do {} while (0)
#define RUNE_FR_MARK_VAL(site, corr, value) do {} while (0)
#define RUNE_FR_MARK_ARG_VAL(site, corr, arg, value) do {} while (0)
#define RUNE_FR_SLOW_SPAN(site, corr) do {} while (0)
#define RUNE_FR_THROTTLED(throttle, site, corr, arg) do {} while (0)

#endif // RUNE_FR
