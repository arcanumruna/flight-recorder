#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "rune_fr/Sites.h"

namespace rune_fr {

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

struct Tape;          // defined in Task 11
bool writeTape(const std::string& path);
bool readTape(const std::string& path, Tape& out);

} // namespace rune_fr
