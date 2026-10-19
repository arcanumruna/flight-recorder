#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flight_recorder {

// Log-linear histogram (HdrHistogram-style). Keeps P=8 mantissa bits => max
// relative error ~1/128 (~0.78%), below the spec's <1% requirement. Values are
// nanoseconds (or any nonneg integer). Reconstruction returns the bucket lower
// bound. Bucketing is exact for values < 2^P.
class Histogram {
public:
    static constexpr unsigned kP = 8;             // mantissa bits kept
    static constexpr unsigned kSub = 1u << kP;    // 256

    Histogram() : counts_(kNumBuckets, 0) {}

    void record(uint64_t v) noexcept {
        ++count_;
        sum_ += static_cast<long double>(v);
        if (v < min_) min_ = v;
        if (v > max_) max_ = v;
        ++counts_[index(v)];
    }

    [[nodiscard]] uint64_t count() const noexcept { return count_; }
    [[nodiscard]] uint64_t min() const noexcept { return count_ ? min_ : 0; }
    [[nodiscard]] uint64_t max() const noexcept { return max_; }
    [[nodiscard]] double mean() const noexcept {
        return count_ ? static_cast<double>(sum_ / count_) : 0.0;
    }

    // p in [0,100]. Returns the lower bound of the bucket holding the p-th pct.
    [[nodiscard]] uint64_t valueAtPercentile(double p) const noexcept {
        if (count_ == 0) return 0;
        if (p < 0.0) p = 0.0;
        if (p > 100.0) p = 100.0;
        uint64_t target =
            static_cast<uint64_t>((p / 100.0) * static_cast<double>(count_) + 0.5);
        if (target == 0) target = 1;
        uint64_t running = 0;
        for (size_t i = 0; i < counts_.size(); ++i) {
            running += counts_[i];
            if (running >= target) return lowerBound(static_cast<unsigned>(i));
        }
        return max_;
    }

    static unsigned index(uint64_t v) noexcept {
        if (v < kSub) return static_cast<unsigned>(v); // exact linear region
        unsigned msb = 63u - static_cast<unsigned>(__builtin_clzll(v)); // >= kP
        unsigned exp = msb - (kP - 1);                                  // >= 1
        unsigned mantissa = static_cast<unsigned>((v >> exp) & (kSub - 1));
        return exp * kSub + mantissa;
    }

    static uint64_t lowerBound(unsigned idx) noexcept {
        if (idx < kSub) return idx;
        unsigned exp = idx / kSub;
        unsigned mantissa = idx % kSub;
        return static_cast<uint64_t>(mantissa) << exp;
    }

private:
    // exponents 0..56 (for 64-bit values with kP=8) => 57*256 buckets.
    static constexpr size_t kNumBuckets = (64 - kP + 1) * kSub;

    std::vector<uint64_t> counts_;
    uint64_t count_ = 0;
    uint64_t min_ = UINT64_MAX;
    uint64_t max_ = 0;
    long double sum_ = 0.0L;
};

} // namespace flight_recorder
