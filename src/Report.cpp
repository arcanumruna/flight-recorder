#include "rune_fr/Report.h"
#include "rune_fr/Recorder.h"
#include "rune_fr/Histogram.h"
#include "rune_fr/Calibration.h"
#include "rune_fr/FrRecord.h"

#include "rune_fr/Enable.h"
#include "rune_fr/Tsc.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include <unistd.h>

namespace rune_fr {

std::vector<SiteStats> collectSiteStats() {
    std::array<Histogram, static_cast<size_t>(Site::Count)> hists;
    Recorder::instance().drain();
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        for (const auto& r : tr->drained) {
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
    Recorder::instance().drain();
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        for (const auto& r : tr->drained) {
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

namespace {
uint64_t clockNs(clockid_t id) noexcept {
    struct timespec ts;
    clock_gettime(id, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<uint64_t>(ts.tv_nsec);
}
// Earliest startTsc over the drained records of `threads` (0 when empty).
uint64_t earliestTsc(const std::vector<ThreadRecorder*>& threads) noexcept {
    uint64_t anchor = std::numeric_limits<uint64_t>::max();
    for (auto* tr : threads)
        for (const auto& r : tr->drained) anchor = std::min(anchor, r.startTsc);
    return anchor == std::numeric_limits<uint64_t>::max() ? 0 : anchor;
}
RunAnchor sampleAnchor(uint64_t anchorTsc) noexcept {
    RunAnchor a;
    a.anchorTsc = anchorTsc;
    a.tscPerNs  = tscPerNs();
    // Bracket the clock reads with two TSC reads; keep the midpoint.
    const uint64_t t0 = rdtscp();
    a.syncMonoNs = clockNs(CLOCK_MONOTONIC);
    a.syncRealNs = clockNs(CLOCK_REALTIME);
    const uint64_t t1 = rdtscp();
    a.syncTsc = t0 + (t1 - t0) / 2;
    return a;
}
std::string hostName() {
    char buf[256] = {};
    if (::gethostname(buf, sizeof(buf) - 1) != 0) return "unknown";
    return buf;
}
} // namespace

RunAnchor runAnchor() {
    Recorder::instance().drain();
    return sampleAnchor(earliestTsc(Recorder::instance().snapshotThreads()));
}

bool exportChromeTrace(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    // Anchor the timeline at the earliest startTsc across all rings.
    Recorder::instance().drain();
    auto threads = Recorder::instance().snapshotThreads();
    const RunAnchor a = sampleAnchor(earliestTsc(threads));
    const uint64_t anchor = a.anchorTsc;

    // NOTE: JSON is hand-written without string escaping. This is safe ONLY
    // because the string fields emitted come from the compile-time kSiteMeta
    // table (controlled C identifiers) and the host name (no quotes/backslashes
    // on any sane host). If a runtime/user-supplied string is ever emitted
    // here, add proper JSON escaping first.
    out << "[";
    // Anchor metadata so traces from several processes can be aligned (see
    // RunAnchor): ts_us is relative to anchorTsc; align processes on one host by
    // (anchorTsc_p - anchorTsc_ref) / tscPerNs_ref, never by absolute TSC.
    out << "{\"ph\":\"M\",\"name\":\"rune_fr_anchor\",\"pid\":1,\"tid\":0,\"args\":{\"anchorTsc\":" << anchor
        << ",\"tscPerNs\":" << std::setprecision(12) << a.tscPerNs
        << ",\"syncTsc\":" << a.syncTsc << ",\"syncMonoNs\":" << a.syncMonoNs
        << ",\"syncRealNs\":" << a.syncRealNs << ",\"pid\":" << static_cast<long>(::getpid())
        << ",\"host\":\"" << hostName() << "\"}}";
    for (auto* tr : threads) {
        for (const auto& r : tr->drained) {
            if (r.site >= static_cast<uint16_t>(Site::Count)) continue;
            double tsUs = cyclesToNs(r.startTsc - anchor) / 1000.0;
            double durUs = cyclesToNs(r.durationTsc()) / 1000.0;
            out << ",{\"ph\":\"X\",\"name\":\"" << nameOf(static_cast<Site>(r.site))
                << "\",\"cat\":\""
                << static_cast<int>(categoryOf(static_cast<Site>(r.site)))
                << "\",\"pid\":1,\"tid\":" << tr->osTid
                << ",\"ts\":" << std::fixed << std::setprecision(3) << tsUs
                << ",\"dur\":" << durUs
                << ",\"args\":{\"corr\":" << r.corr << ",\"arg\":" << r.arg
                << ",\"value\":" << std::fixed << std::setprecision(3) << r.value << "}}";
        }
    }
    out << "]";
    return static_cast<bool>(out);
}

// Tape binary format (little-endian, documented assumption):
//   magic "RUNEFR01" (8 bytes) | u32 version | f64 tscPerNs
//   | u32 siteCount, then each site name as (u16 len + bytes)
//   | u32 threadCount, then per thread:
//       u64 osTid | u16 nameLen + bytes | u64 recordCount | recordCount * sizeof(FrRecord)
//       (32 bytes: u64 startTsc, u64 endTsc, u32 corr, u16 site, u16 arg, f64 value)
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

    Recorder::instance().drain();
    auto threads = Recorder::instance().snapshotThreads();
    uint32_t threadCount = static_cast<uint32_t>(threads.size());
    putPod(out, threadCount);
    for (auto* tr : threads) {
        putPod(out, tr->osTid);
        putStr(out, tr->threadName);
        const auto& recs = tr->drained;
        uint64_t recCount = recs.size();
        putPod(out, recCount);
        if (recCount)
            out.write(reinterpret_cast<const char*>(recs.data()),
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

// ---------------------------------------------------------------------------
// Process lifecycle helpers
// ---------------------------------------------------------------------------
namespace {
uint64_t envU64(const char* name, uint64_t dflt) noexcept {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return dflt;
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v, &end, 10);
    return (end != nullptr && *end == '\0') ? static_cast<uint64_t>(n) : dflt;
}
uint32_t categoryMaskFromEnv() noexcept {
    const char* v = std::getenv("RUNE_FR_CATEGORIES");
    if (v == nullptr || *v == '\0') return 0xFFFFFFFFu;
    static constexpr std::pair<const char*, Category> kNames[] = {
        {"produce", Category::Produce}, {"replication", Category::Replication}, {"read", Category::Read},
        {"consensus", Category::Consensus}, {"storage", Category::Storage}, {"egress", Category::Egress},
        {"runtime", Category::Runtime}};
    uint32_t mask = 0;
    std::string list(v);
    std::size_t pos = 0;
    while (pos <= list.size()) {
        const std::size_t comma = list.find(',', pos);
        const std::string tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        for (const auto& [n, c] : kNames)
            if (tok == n) mask |= categoryBit(c);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return mask == 0 ? 0xFFFFFFFFu : mask;
}
} // namespace

bool configureFromEnv() {
    Recorder::instance().configure(static_cast<size_t>(envU64("RUNE_FR_RING_BYTES", 128ull * 1024 * 1024)));
    const char* en = std::getenv("RUNE_FR_ENABLE");
    if (en == nullptr || *en == '\0' || std::strcmp(en, "0") == 0) return false;
    calibrate();
    setCategoryMask(categoryMaskFromEnv());
    setSlowSpanTsc(nsToCycles(static_cast<double>(envU64("RUNE_FR_SLOW_SPAN_NS", 2000))));
    setThrottleTsc(nsToCycles(static_cast<double>(envU64("RUNE_FR_THROTTLE_NS", 50000))));
    enable();
    const uint64_t drainMs = envU64("RUNE_FR_DRAIN_MS", 100);
    if (drainMs > 0) startBackgroundDrain(static_cast<unsigned>(drainMs));
    return true;
}

bool dumpAll(const std::string& dir, const std::string& suffix, const std::string& role,
             const std::string& node) {
    if (!isEnabled()) return false;
    Recorder::instance().drain();
    bool ok = true;
    {
        std::ofstream f(dir + "/fr-hist" + suffix + ".txt");
        if (f) dumpHistograms(f); else ok = false;
    }
    ok = exportChromeTrace(dir + "/fr-trace" + suffix + ".json") && ok;
    ok = writeTape(dir + "/fr-tape" + suffix + ".bin") && ok;
    // The meta is written LAST, with the anchor of exactly what was dumped (the
    // tape and the trace above share the earliest TSC: drained data only grows).
    const RunAnchor a = runAnchor();
    uint64_t records = 0;
    for (auto* tr : Recorder::instance().snapshotThreads()) records += tr->drained.size();
    std::ofstream m(dir + "/fr-meta" + suffix + ".json", std::ios::trunc);
    if (!m) return false;
    m << "{\"role\":\"" << role << "\",\"node\":\"" << node << "\",\"pid\":" << static_cast<long>(::getpid())
      << ",\"host\":\"" << hostName() << "\",\"anchorTsc\":" << a.anchorTsc << ",\"tscPerNs\":"
      << std::setprecision(12) << a.tscPerNs << ",\"syncTsc\":" << a.syncTsc << ",\"syncMonoNs\":" << a.syncMonoNs
      << ",\"syncRealNs\":" << a.syncRealNs << ",\"records\":" << records << ",\"dropped\":" << droppedRecords()
      << ",\"slowSpanNs\":" << static_cast<uint64_t>(cyclesToNs(slowSpanTsc()) + 0.5)
      << ",\"throttleNs\":" << static_cast<uint64_t>(cyclesToNs(throttleTsc()) + 0.5)
      << ",\"suffix\":\"" << suffix << "\"}\n";
    return ok && static_cast<bool>(m);
}

} // namespace rune_fr
