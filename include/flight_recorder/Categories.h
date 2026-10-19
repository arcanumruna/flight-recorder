#pragma once
// Categories.h — per-category COMPILE-TIME gating of instrumentation.
//
// Two levels of compile flags (all #ifdef/#ifndef, zero runtime cost):
//
//   1. Master:   FLIGHT_RECORDER (Config.h) — compiles the WHOLE recorder in (1) or out (0).
//                Set via the build (e.g. CMake -DENABLE_FLIGHT_RECORDER=OFF → FLIGHT_RECORDER=0).
//
//   2. Per-path: each category is additionally gated so a build can compile out
//                specific paths without touching the rest:
//                  -DFLIGHT_RECORDER_NO_PRODUCE     -DFLIGHT_RECORDER_NO_REPLICATION
//                  -DFLIGHT_RECORDER_NO_READ        -DFLIGHT_RECORDER_NO_CONSENSUS
//                  -DFLIGHT_RECORDER_NO_STORAGE     -DFLIGHT_RECORDER_NO_EGRESS
//                  -DFLIGHT_RECORDER_NO_RUNTIME
//                Default (none defined): every category is compiled in when FLIGHT_RECORDER is on.
//
// Call sites use the PER-CATEGORY macros (never the bare FLIGHT_RECORDER_SPAN), so each
// span/mark is gated by BOTH the master flag and its category flag:
//   FLIGHT_RECORDER_SPAN_PRODUCE(site, corr);                 scoped span
//   FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(site, corr);            scoped span kept only if >= slowSpanTsc()
//   FLIGHT_RECORDER_MARK_REPLICATION(site, corr);             instant
//   FLIGHT_RECORDER_MARK_VAL_PRODUCE(site, corr, value);      instant + value
//   FLIGHT_RECORDER_MARK_ARG_VAL_PRODUCE(site, corr, arg, v); instant + arg + value
//   FLIGHT_RECORDER_THROTTLED_PRODUCE(thr, site, corr, arg);  coalesced instant (flight_recorder::Throttle thr)
//
// The category suffix MUST match the Site's Category (see Sites.h) — it selects
// which compile flag gates the call.

#include "flight_recorder/Config.h"
#include "flight_recorder/Event.h"

// ---- Produce --------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_PRODUCE)
#  define FLIGHT_RECORDER_SPAN_PRODUCE(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_PRODUCE(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_PRODUCE(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_PRODUCE(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_PRODUCE(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_PRODUCE(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_PRODUCE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_PRODUCE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_PRODUCE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_PRODUCE(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_PRODUCE(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_PRODUCE(t, s, c, a) do {} while (0)
#endif

// ---- Replication ----------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_REPLICATION)
#  define FLIGHT_RECORDER_SPAN_REPLICATION(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_REPLICATION(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_REPLICATION(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_REPLICATION(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_REPLICATION(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_REPLICATION(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_REPLICATION(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_REPLICATION(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_REPLICATION(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_REPLICATION(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_REPLICATION(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_REPLICATION(t, s, c, a) do {} while (0)
#endif

// ---- Read -----------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_READ)
#  define FLIGHT_RECORDER_SPAN_READ(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_READ(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_READ(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_READ(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_READ(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_READ(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_READ(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_READ(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_READ(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_READ(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_READ(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_READ(t, s, c, a) do {} while (0)
#endif

// ---- Consensus ------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_CONSENSUS)
#  define FLIGHT_RECORDER_SPAN_CONSENSUS(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_CONSENSUS(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_CONSENSUS(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_CONSENSUS(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_CONSENSUS(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_CONSENSUS(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_CONSENSUS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_CONSENSUS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_CONSENSUS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_CONSENSUS(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_CONSENSUS(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_CONSENSUS(t, s, c, a) do {} while (0)
#endif

// ---- Storage --------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_STORAGE)
#  define FLIGHT_RECORDER_SPAN_STORAGE(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_STORAGE(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_STORAGE(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_STORAGE(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_STORAGE(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_STORAGE(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_STORAGE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_STORAGE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_STORAGE(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_STORAGE(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_STORAGE(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_STORAGE(t, s, c, a) do {} while (0)
#endif

// ---- Egress ---------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_EGRESS)
#  define FLIGHT_RECORDER_SPAN_EGRESS(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_EGRESS(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_EGRESS(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_EGRESS(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_EGRESS(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_EGRESS(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_EGRESS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_EGRESS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_EGRESS(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_EGRESS(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_EGRESS(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_EGRESS(t, s, c, a) do {} while (0)
#endif

// ---- Runtime --------------------------------------------------------------------
#if FLIGHT_RECORDER && !defined(FLIGHT_RECORDER_NO_RUNTIME)
#  define FLIGHT_RECORDER_SPAN_RUNTIME(s, c) FLIGHT_RECORDER_SPAN((s), (c))
#  define FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(s, c) FLIGHT_RECORDER_SLOW_SPAN((s), (c))
#  define FLIGHT_RECORDER_MARK_RUNTIME(s, c) FLIGHT_RECORDER_MARK((s), (c))
#  define FLIGHT_RECORDER_MARK_VAL_RUNTIME(s, c, v) FLIGHT_RECORDER_MARK_VAL((s), (c), (v))
#  define FLIGHT_RECORDER_MARK_ARG_VAL_RUNTIME(s, c, a, v) FLIGHT_RECORDER_MARK_ARG_VAL((s), (c), (a), (v))
#  define FLIGHT_RECORDER_THROTTLED_RUNTIME(t, s, c, a) FLIGHT_RECORDER_THROTTLED((t), (s), (c), (a))
#else
#  define FLIGHT_RECORDER_SPAN_RUNTIME(s, c) do {} while (0)
#  define FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_RUNTIME(s, c) do {} while (0)
#  define FLIGHT_RECORDER_MARK_VAL_RUNTIME(s, c, v) do {} while (0)
#  define FLIGHT_RECORDER_MARK_ARG_VAL_RUNTIME(s, c, a, v) do {} while (0)
#  define FLIGHT_RECORDER_THROTTLED_RUNTIME(t, s, c, a) do {} while (0)
#endif
