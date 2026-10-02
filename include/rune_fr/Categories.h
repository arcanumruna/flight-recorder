#pragma once
// Categories.h — per-category COMPILE-TIME gating of instrumentation.
//
// Two levels of compile flags (all #ifdef/#ifndef, zero runtime cost):
//
//   1. Master:   RUNE_FR (Config.h) — compiles the WHOLE recorder in (1) or out (0).
//                Set via the build (e.g. CMake -DENABLE_FLIGHT_RECORDER=OFF → RUNE_FR=0).
//
//   2. Per-path: each category is additionally gated so a build can compile out
//                specific paths without touching the rest:
//                  -DRUNE_FR_NO_PRODUCE     -DRUNE_FR_NO_REPLICATION
//                  -DRUNE_FR_NO_READ        -DRUNE_FR_NO_CONSENSUS
//                  -DRUNE_FR_NO_STORAGE     -DRUNE_FR_NO_EGRESS
//                  -DRUNE_FR_NO_RUNTIME
//                Default (none defined): every category is compiled in when RUNE_FR is on.
//
// Call sites use the PER-CATEGORY macros (never the bare RUNE_FR_SPAN), so each
// span/mark is gated by BOTH the master flag and its category flag:
//   RUNE_FR_SPAN_PRODUCE(site, corr);                 scoped span
//   RUNE_FR_SLOW_SPAN_RUNTIME(site, corr);            scoped span kept only if >= slowSpanTsc()
//   RUNE_FR_MARK_REPLICATION(site, corr);             instant
//   RUNE_FR_MARK_VAL_PRODUCE(site, corr, value);      instant + value
//   RUNE_FR_MARK_ARG_VAL_PRODUCE(site, corr, arg, v); instant + arg + value
//   RUNE_FR_THROTTLED_PRODUCE(thr, site, corr, arg);  coalesced instant (rune_fr::Throttle thr)
//
// The category suffix MUST match the Site's Category (see Sites.h) — it selects
// which compile flag gates the call.

#include "rune_fr/Config.h"
#include "rune_fr/Event.h"

// ---- Produce --------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_PRODUCE)
#  define RUNE_FR_SPAN_PRODUCE(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_PRODUCE(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_PRODUCE(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_PRODUCE(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_PRODUCE(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_PRODUCE(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_PRODUCE(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_PRODUCE(s, c) do {} while (0)
#  define RUNE_FR_MARK_PRODUCE(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_PRODUCE(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_PRODUCE(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_PRODUCE(t, s, c, a) do {} while (0)
#endif

// ---- Replication ----------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_REPLICATION)
#  define RUNE_FR_SPAN_REPLICATION(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_REPLICATION(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_REPLICATION(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_REPLICATION(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_REPLICATION(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_REPLICATION(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_REPLICATION(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_REPLICATION(s, c) do {} while (0)
#  define RUNE_FR_MARK_REPLICATION(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_REPLICATION(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_REPLICATION(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_REPLICATION(t, s, c, a) do {} while (0)
#endif

// ---- Read -----------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_READ)
#  define RUNE_FR_SPAN_READ(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_READ(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_READ(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_READ(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_READ(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_READ(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_READ(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_READ(s, c) do {} while (0)
#  define RUNE_FR_MARK_READ(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_READ(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_READ(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_READ(t, s, c, a) do {} while (0)
#endif

// ---- Consensus ------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_CONSENSUS)
#  define RUNE_FR_SPAN_CONSENSUS(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_CONSENSUS(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_CONSENSUS(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_CONSENSUS(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_CONSENSUS(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_CONSENSUS(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_CONSENSUS(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_CONSENSUS(s, c) do {} while (0)
#  define RUNE_FR_MARK_CONSENSUS(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_CONSENSUS(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_CONSENSUS(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_CONSENSUS(t, s, c, a) do {} while (0)
#endif

// ---- Storage --------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_STORAGE)
#  define RUNE_FR_SPAN_STORAGE(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_STORAGE(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_STORAGE(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_STORAGE(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_STORAGE(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_STORAGE(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_STORAGE(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_STORAGE(s, c) do {} while (0)
#  define RUNE_FR_MARK_STORAGE(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_STORAGE(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_STORAGE(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_STORAGE(t, s, c, a) do {} while (0)
#endif

// ---- Egress ---------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_EGRESS)
#  define RUNE_FR_SPAN_EGRESS(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_EGRESS(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_EGRESS(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_EGRESS(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_EGRESS(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_EGRESS(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_EGRESS(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_EGRESS(s, c) do {} while (0)
#  define RUNE_FR_MARK_EGRESS(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_EGRESS(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_EGRESS(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_EGRESS(t, s, c, a) do {} while (0)
#endif

// ---- Runtime --------------------------------------------------------------------
#if RUNE_FR && !defined(RUNE_FR_NO_RUNTIME)
#  define RUNE_FR_SPAN_RUNTIME(s, c) RUNE_FR_SPAN((s), (c))
#  define RUNE_FR_SLOW_SPAN_RUNTIME(s, c) RUNE_FR_SLOW_SPAN((s), (c))
#  define RUNE_FR_MARK_RUNTIME(s, c) RUNE_FR_MARK((s), (c))
#  define RUNE_FR_MARK_VAL_RUNTIME(s, c, v) RUNE_FR_MARK_VAL((s), (c), (v))
#  define RUNE_FR_MARK_ARG_VAL_RUNTIME(s, c, a, v) RUNE_FR_MARK_ARG_VAL((s), (c), (a), (v))
#  define RUNE_FR_THROTTLED_RUNTIME(t, s, c, a) RUNE_FR_THROTTLED((t), (s), (c), (a))
#else
#  define RUNE_FR_SPAN_RUNTIME(s, c) do {} while (0)
#  define RUNE_FR_SLOW_SPAN_RUNTIME(s, c) do {} while (0)
#  define RUNE_FR_MARK_RUNTIME(s, c) do {} while (0)
#  define RUNE_FR_MARK_VAL_RUNTIME(s, c, v) do {} while (0)
#  define RUNE_FR_MARK_ARG_VAL_RUNTIME(s, c, a, v) do {} while (0)
#  define RUNE_FR_THROTTLED_RUNTIME(t, s, c, a) do {} while (0)
#endif
