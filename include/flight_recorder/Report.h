#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "flight_recorder/Sites.h"
#include "flight_recorder/FrRecord.h"

namespace flight_recorder {

struct SiteStats {
    Site site;
    uint64_t count;
    uint64_t p50, p90, p99, p999, max; // ns
    double mean;                       // ns
};

std::vector<SiteStats> collectSiteStats();
void dumpHistograms(std::ostream& out);

struct WaterfallEntry {
    Site site;
    uint64_t startTsc;
    uint64_t durTsc;
    uint64_t offsetNs; // ns from the earliest event in this correlation
    double durNs;
    uint64_t osTid;
};

std::vector<WaterfallEntry> stitchByCorr(uint32_t corr);
void printWaterfall(uint32_t corr, std::ostream& out);

// --- Chrome trace + tape declared here; implemented in Tasks 10 & 11. ---
bool exportChromeTrace(const std::string& path);

struct TapeThread {
    uint64_t osTid;
    std::string threadName;
    std::vector<FrRecord> records;
};

struct Tape {
    uint32_t version = 1;
    double tscPerNs = 1.0;
    std::vector<std::string> siteNames;
    std::vector<TapeThread> threads;
};

bool writeTape(const std::string& path);
bool readTape(const std::string& path, Tape& out);

// ---- Process lifecycle helpers (cold; one call each from a binary's main) ----
//
// configureFromEnv(): size the per-thread rings and arm recording from the env,
// BEFORE the threads that record are started.  Returns true iff recording is ON.
//   FLIGHT_RECORDER_ENABLE       set (and not "0")      → enable recording (default OFF)
//   FLIGHT_RECORDER_RING_BYTES   per-thread ring bytes  (default 128 MiB)
//   FLIGHT_RECORDER_CATEGORIES   comma list of produce,replication,read,consensus,storage,
//                        egress,runtime (default: all)
//   FLIGHT_RECORDER_SLOW_SPAN_NS SlowSpan threshold     (default 2000 ns)
//   FLIGHT_RECORDER_THROTTLE_NS  Throttle min gap       (default 50000 ns)
//   FLIGHT_RECORDER_DRAIN_MS     background drain period (default 100 ms, 0 = off)
bool configureFromEnv();

// Run metadata for cross-process alignment: the trace anchor (earliest recorded
// TSC), the calibrated rate and ONE simultaneous (TSC, CLOCK_MONOTONIC,
// CLOCK_REALTIME) sample.  Processes on one host share the invariant TSC: align
// them by anchor DIFFERENCES converted with one rate, never by absolute TSC / a
// per-process rate (a 1e-6 rate error x a 1e15 absolute TSC = seconds of skew).
struct RunAnchor {
    uint64_t anchorTsc = 0;   // earliest startTsc over all drained records (0 = none)
    double   tscPerNs  = 1.0;
    uint64_t syncTsc   = 0;
    uint64_t syncMonoNs = 0;
    uint64_t syncRealNs = 0;
};
RunAnchor runAnchor();

// Drain, then write <dir>/fr-hist<sfx>.txt, fr-trace<sfx>.json, fr-tape<sfx>.bin
// and fr-meta<sfx>.json (role, node, pid, host, anchor, drops, config).  No-op
// returning false when recording is off.  Off the hot path (exit / signal).
bool dumpAll(const std::string& dir, const std::string& suffix, const std::string& role,
             const std::string& node);

} // namespace flight_recorder
