#include "rune_fr/Report.h"
#include "rune_fr/Recorder.h"
#include "rune_fr/Histogram.h"
#include "rune_fr/Calibration.h"
#include "rune_fr/FrRecord.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>

namespace rune_fr {

std::vector<SiteStats> collectSiteStats() {
    std::array<Histogram, static_cast<size_t>(Site::Count)> hists;
    std::vector<FrRecord> buf;
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        tr->ring.snapshot(buf);
        for (const auto& r : buf) {
            if (r.site >= static_cast<uint16_t>(Site::Count)) continue;
            hists[r.site].record(
                static_cast<uint64_t>(cyclesToNs(r.durationTsc()) + 0.5));
        }
    }

    std::vector<SiteStats> stats;
    for (size_t i = 0; i < hists.size(); ++i) {
        if (hists[i].count() == 0) continue;
        Histogram& h = hists[i];
        stats.push_back(SiteStats{
            static_cast<Site>(i), h.count(),
            h.valueAtPercentile(50.0), h.valueAtPercentile(90.0),
            h.valueAtPercentile(99.0), h.valueAtPercentile(99.9),
            h.max(), h.mean()});
    }
    return stats;
}

void dumpHistograms(std::ostream& out) {
    auto stats = collectSiteStats();
    out << std::left << std::setw(16) << "site" << std::right
        << std::setw(10) << "count" << std::setw(12) << "p50(ns)"
        << std::setw(12) << "p90(ns)" << std::setw(12) << "p99(ns)"
        << std::setw(12) << "p999(ns)" << std::setw(12) << "max(ns)"
        << std::setw(14) << "mean(ns)" << "\n";
    for (const auto& s : stats) {
        out << std::left << std::setw(16) << nameOf(s.site) << std::right
            << std::setw(10) << s.count << std::setw(12) << s.p50
            << std::setw(12) << s.p90 << std::setw(12) << s.p99
            << std::setw(12) << s.p999 << std::setw(12) << s.max
            << std::setw(14) << std::fixed << std::setprecision(1) << s.mean
            << "\n";
    }
}

std::vector<WaterfallEntry> stitchByCorr(uint32_t corr) {
    std::vector<WaterfallEntry> entries;
    if (corr == 0) return entries; // 0 = "none"; never stitch the sentinel
    std::vector<FrRecord> buf;
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        tr->ring.snapshot(buf);
        for (const auto& r : buf) {
            if (r.corr != corr) continue;
            entries.push_back(WaterfallEntry{
                static_cast<Site>(r.site), r.startTsc, r.durationTsc(),
                0, 0.0, tr->osTid});
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const WaterfallEntry& a, const WaterfallEntry& b) {
                  return a.startTsc < b.startTsc;
              });
    if (!entries.empty()) {
        uint64_t anchor = entries.front().startTsc;
        for (auto& e : entries) {
            e.offsetNs = static_cast<uint64_t>(cyclesToNs(e.startTsc - anchor) + 0.5);
            e.durNs = cyclesToNs(e.durTsc);
        }
    }
    return entries;
}

void printWaterfall(uint32_t corr, std::ostream& out) {
    auto wf = stitchByCorr(corr);
    out << "waterfall corr=" << corr << " (" << wf.size() << " spans)\n";
    for (const auto& e : wf) {
        out << std::left << std::setw(16) << nameOf(e.site) << std::right
            << " +" << std::setw(10) << e.offsetNs << "ns  dur="
            << std::fixed << std::setprecision(1) << e.durNs << "ns  tid="
            << e.osTid << "\n";
    }
}

bool exportChromeTrace(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    // Anchor the timeline at the earliest startTsc across all rings.
    std::vector<FrRecord> buf;
    auto threads = Recorder::instance().snapshotThreads();
    uint64_t anchor = std::numeric_limits<uint64_t>::max();
    for (auto* tr : threads) {
        tr->ring.snapshot(buf);
        for (const auto& r : buf) anchor = std::min(anchor, r.startTsc);
    }
    if (anchor == std::numeric_limits<uint64_t>::max()) anchor = 0;

    out << "[";
    bool first = true;
    for (auto* tr : threads) {
        tr->ring.snapshot(buf);
        for (const auto& r : buf) {
            if (r.site >= static_cast<uint16_t>(Site::Count)) continue;
            double tsUs = cyclesToNs(r.startTsc - anchor) / 1000.0;
            double durUs = cyclesToNs(r.durationTsc()) / 1000.0;
            if (!first) out << ",";
            first = false;
            out << "{\"ph\":\"X\",\"name\":\"" << nameOf(static_cast<Site>(r.site))
                << "\",\"cat\":\""
                << static_cast<int>(categoryOf(static_cast<Site>(r.site)))
                << "\",\"pid\":1,\"tid\":" << tr->osTid
                << ",\"ts\":" << std::fixed << std::setprecision(3) << tsUs
                << ",\"dur\":" << durUs
                << ",\"args\":{\"corr\":" << r.corr << ",\"arg\":" << r.arg << "}}";
        }
    }
    out << "]";
    return static_cast<bool>(out);
}

// Tape binary format (little-endian, documented assumption):
//   magic "RUNEFR01" (8 bytes) | u32 version | f64 tscPerNs
//   | u32 siteCount, then each site name as (u16 len + bytes)
//   | u32 threadCount, then per thread:
//       u64 osTid | u16 nameLen + bytes | u64 recordCount | recordCount * 24-byte FrRecord
namespace {
template <class T> void putPod(std::ostream& o, const T& v) {
    o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T> bool getPod(std::istream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}
void putStr(std::ostream& o, const std::string& s) {
    uint16_t n = static_cast<uint16_t>(s.size());
    putPod(o, n);
    o.write(s.data(), n);
}
bool getStr(std::istream& i, std::string& s) {
    uint16_t n = 0;
    if (!getPod(i, n)) return false;
    s.resize(n);
    return n == 0 || static_cast<bool>(i.read(s.data(), n));
}
constexpr char kMagic[8] = {'R', 'U', 'N', 'E', 'F', 'R', '0', '1'};
} // namespace

bool writeTape(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(kMagic, 8);
    uint32_t version = 1;
    putPod(out, version);
    double perNs = tscPerNs();
    putPod(out, perNs);

    uint32_t siteCount = static_cast<uint32_t>(Site::Count);
    putPod(out, siteCount);
    for (uint32_t i = 0; i < siteCount; ++i)
        putStr(out, nameOf(static_cast<Site>(i)));

    auto threads = Recorder::instance().snapshotThreads();
    uint32_t threadCount = static_cast<uint32_t>(threads.size());
    putPod(out, threadCount);
    std::vector<FrRecord> buf;
    for (auto* tr : threads) {
        putPod(out, tr->osTid);
        putStr(out, tr->threadName);
        tr->ring.snapshot(buf);
        uint64_t recCount = buf.size();
        putPod(out, recCount);
        if (recCount)
            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(recCount * sizeof(FrRecord)));
    }
    return static_cast<bool>(out);
}

bool readTape(const std::string& path, Tape& t) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[8];
    if (!in.read(magic, 8) || std::memcmp(magic, kMagic, 8) != 0) return false;
    if (!getPod(in, t.version) || t.version != 1) return false;
    if (!getPod(in, t.tscPerNs)) return false;

    uint32_t siteCount = 0;
    if (!getPod(in, siteCount)) return false;
    t.siteNames.resize(siteCount);
    for (uint32_t i = 0; i < siteCount; ++i)
        if (!getStr(in, t.siteNames[i])) return false;

    uint32_t threadCount = 0;
    if (!getPod(in, threadCount)) return false;
    t.threads.resize(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i) {
        TapeThread& th = t.threads[i];
        if (!getPod(in, th.osTid)) return false;
        if (!getStr(in, th.threadName)) return false;
        uint64_t recCount = 0;
        if (!getPod(in, recCount)) return false;
        th.records.resize(recCount);
        if (recCount &&
            !in.read(reinterpret_cast<char*>(th.records.data()),
                     static_cast<std::streamsize>(recCount * sizeof(FrRecord))))
            return false;
    }
    return true;
}

} // namespace rune_fr
