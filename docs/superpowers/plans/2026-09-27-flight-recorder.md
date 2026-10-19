# flight-recorder — Flight Recorder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `flight-recorder`, a standalone C++23 header-first flight-recorder library that captures JFR-style begin/commit measured events into per-thread lock-free ring buffers at < 10 ns/event, and drains them off-hot-path into per-site latency histograms, a Chrome-trace timeline, correlation-id waterfalls, and a versioned binary tape.

**Architecture:** Hot path is header-only and inline: an `Event` stamps the TSC (`rdtscp`) on `begin()`/`commit()` and pushes a 24-byte POD `FrRecord` into the calling thread's own single-producer ring (shared-nothing; the only cross-thread atomic is a release-store of the published write index). Cold path lives in a small `.cpp`: TSC↔ns calibration, thread-registry (mutex only at register time), and a `Recorder` singleton that snapshots all rings to produce reports. A master compile switch `FLIGHT_RECORDER` makes every macro and `Event` expand to nothing when off; a runtime `enabled(site)` check (two relaxed atomic loads) gates recording when compiled in.

**Tech Stack:** C++23, CMake 3.31, Conan 2.x, GoogleTest 1.16.0 (test-only), Google Benchmark 1.8.2 (bench-only), nlohmann_json 3.12.0 (test-only, for JSON schema assertions). x86_64 uses `rdtscp`; aarch64 (macOS dev) uses `cntvct_el0`; other targets fall back to `clock_gettime(CLOCK_MONOTONIC_RAW)`.

**Spec:** `docs/specs/2026-09-27-flight-recorder-design.md`

## Global Constraints

Every task's requirements implicitly include this section. Values are copied verbatim from the spec and sibling-repo conventions.

- **Language/std:** C++23. `set(CMAKE_CXX_STANDARD 23)` + `set(CMAKE_CXX_STANDARD_REQUIRED ON)`.
- **CMake:** `cmake_minimum_required(VERSION 3.31)`, `project(flight_recorder)`. Must **configure on both macOS (CLion, armv8) and Linux (x86_64)** — never require x86 at configure time. The aarch64/fallback timestamp path keeps macOS builds green.
- **Compile switch:** CMake option `FLIGHT_RECORDER` **default ON**. Off → all macros and `Event` emit zero code. Also expose it as the preprocessor define `FLIGHT_RECORDER` (`1`/`0`) via `target_compile_definitions`.
- **Header-first:** all hot-path code inline in `include/flight_recorder/*.h`; a single small static lib `flight_recorder_core` (`.cpp`) holds calibration, registry, and reporting. Public umbrella header: `include/flight_recorder/FlightRecorder.h`.
- **Layout:** headers in `include/flight_recorder/`, sources in `src/`, tests in `tests/` (mirrors `include/` structure), benches in `bench/`.
- **Little-endian** assumed and documented (matches Rune). Add `static_assert(std::endian::native == std::endian::little)` in `FrRecord.h`.
- **Dependencies:** standard library only for the core library. GoogleTest/Benchmark/nlohmann_json are `test_requires`/bench-only — never linked into `flight_recorder` / `flight_recorder_core`.
- **Shared-nothing write path:** one ring per thread; a thread writes only its own ring. The only cross-thread synchronization on the write path is the ring's `std::atomic<uint64_t> writeIndex_` published with `release` and read by the drainer with `acquire`. No locks, no syscalls, no allocation on the write path after first-touch thread registration.
- **Packaging:** write `conanfile.py` + `conandata.yml` + `VERSION.txt`. **Do NOT run `conan create`/`export`/`upload`** — that is the user's CI. `git init` is allowed (repo is not yet a git repo).
- **Overhead gate (mandatory):** enabled `commit()` ≤ 10 ns, runtime-disabled ≤ 1 ns, compiled-out = 0, on the reference VM. Do not declare done without published microbench numbers in the README.
- **Record size:** 24-byte `FrRecord` for v1 (per spec §2.2 recommendation). Document the choice in `FrRecord.h`.
- **Ring capacity default:** 65536 records/thread (= 1.5 MiB/thread at 24 B/record); configurable.
- **Test naming:** `{Component}Test.cpp`, registered via `gtest_discover_tests`, linked against `GTest::gtest_main`. Bench naming: `{component}_bench.cpp` linked against `benchmark::benchmark`.

## Review Focus

Input classes / failure modes the spec implies but that need explicit test coverage. Each is pinned to the task that owns the code.

- **No invariant TSC / non-x86 target** — calibration and `now()` must not crash and must transparently use the fallback clock; `tscPerNs` must stay positive. → Task 2.
- **Ring capacity not a power of two, or zero** — the ring constructor must reject it (assert/throw) rather than silently corrupting the mask. → Task 4.
- **`corr == 0` ("none") events** — `stitchByCorr` must never match the sentinel 0, and passing `corr=0` must return an empty waterfall, not every unattributed event. → Task 9.
- **`endTsc == startTsc` (mark / instant event) and end-before-start guard** — duration must compute as 0, never underflow a `uint64_t`; histograms must accept a 0-duration sample. → Tasks 7 & 8.
- **Empty rings at report time** — `dumpHistograms`, `exportChromeTrace`, and `writeTape` must produce valid (empty-but-parseable) output and not divide by zero or emit malformed JSON. → Tasks 9 & 10.

---

## Amendment (2026-09-27, post code-review — storage layer)

The hand-rolled POD ring in **Task 4** was **superseded** after review: its overwrite-oldest
`snapshot()` tore records when a drainer read a wrapped ring while the owning thread wrote
(a real data race, confirmed by a concurrent stress test). Per the owner's direction the
storage layer is now a **per-thread `flight_recorder::detail::M2ORingBuffer`** — a namespace-isolated
copy of Rune's agrona M2O ring built on aeron's header-only `AtomicBuffer`/`RecordDescriptor`
(depends on **aeron**, not `common`), wrapped by `detail::FrRing` (owns a **16 MiB** payload
buffer per thread). Semantics changed from overwrite-oldest to **drop-on-full** (`commit()`
never blocks; full ⇒ event dropped). A single **drainer** (`Recorder::drain()`) reads every
ring into per-thread accumulators; the reporting functions read the accumulators. M2O's
length-header framing makes a live drain tear-free by construction. Build: `conanfile.py`
requires aeron (skipped on macOS/armv8 + CMake stub `aeron::aeron`; set
`FLIGHT_RECORDER_AERON_INCLUDE_DIR` to build against cached aeron headers on macOS). Overhead
re-measured: enabled span **8.56 ns** (still ≤ 10 ns), disabled 0.66 ns. Tasks 5/7/9/10/11
were adjusted to the drain/accumulator model; `Ring.h`, `RingTest`, `RingRaceTest` were
removed in favor of `detail/M2ORingBuffer.h`, `detail/FrRing.h`, and `M2ORingTest`.

## File Structure

Created files and their single responsibilities:

**Headers (`include/flight_recorder/`)**
- `Config.h` — `FLIGHT_RECORDER` define normalization, platform detection macros (`FLIGHT_RECORDER_X86`/`FLIGHT_RECORDER_ARM`/`FLIGHT_RECORDER_FALLBACK`), `FLIGHT_RECORDER_CONCAT` helper.
- `Sites.h` — `Category` enum, `Site` enum (`None=0 … Count`), `SiteMeta`, `kSiteMeta[]`, `categoryOf(Site)`, `nameOf(Site)`.
- `Tsc.h` — `rdtscp()`/timestamp read per platform, `hasInvariantTsc()`, raw counter frequency helper for ARM.
- `Calibration.h` / `src/Calibration.cpp` — `calibrate()`, `tscPerNs()`, `cyclesToNs()`, `nsToCycles()`.
- `FrRecord.h` — the 24-byte POD + `static_assert`s + `durationTsc()`.
- `Ring.h` — `Ring` (single-producer POD ring, two modes, published index, snapshot access).
- `Enable.h` / `src/Enable.cpp` — `g_enabled`, `g_categoryMask`, `enable()/disable()`, `setCategoryMask()`, `enabled(Site)`.
- `Recorder.h` / `src/Registry.cpp` — `ThreadRecorder`, `threadRecorder()`, `Recorder` singleton (registry + config).
- `Event.h` — `Event`, `ScopedSpan`, `mark()`, `FLIGHT_RECORDER_SPAN`/`FLIGHT_RECORDER_MARK` macros (with compile-out).
- `Histogram.h` — log-linear `Histogram` (percentiles, <1% relative error).
- `Report.h` / `src/Report.cpp` — `dumpHistograms`, `stitchByCorr`, `exportChromeTrace`, `writeTape`/`readTape`, `Tape` struct.
- `Version.h` — `kVersion`.
- `FlightRecorder.h` — umbrella include of the public surface.

**Build/packaging**
- `CMakeLists.txt`, `conanfile.py`, `conandata.yml`, `VERSION.txt`, `.gitignore`, `README.md`.

**Sample consumer (Task 13)**
- `sample/CMakeLists.txt`, `sample/main.cpp`.

---

## Task 1: Repo scaffold + build/test wiring (P0)

**Files:**
- Create: `VERSION.txt`, `.gitignore`, `CMakeLists.txt`, `conanfile.py`, `conandata.yml`, `README.md`
- Create: `include/flight_recorder/Version.h`, `include/flight_recorder/Config.h`, `include/flight_recorder/FlightRecorder.h`
- Test: `tests/VersionTest.cpp`

**Interfaces:**
- Produces: static lib target `flight_recorder_core` and INTERFACE target `flight_recorder` (both alias-exported as `flight_recorder::flight_recorder`); preprocessor define `FLIGHT_RECORDER` (1 when option ON); `namespace flight_recorder { inline constexpr const char* kVersion; }`; macro `FLIGHT_RECORDER_CONCAT(a,b)`.

- [ ] **Step 1: `git init` and create version + ignore files**

```bash
cd ~/Projects/flight-recorder && git init
```

`VERSION.txt`:
```
0.1.0-snapshot
```

`.gitignore`:
```
# --- Conan and build artifacts ---
/build/
/build_release/
/cmake-build-*/
.conan/
CMakeFiles/
CMakeCache.txt
*.ninja
*.ninja_deps
*.ninja_log
compile_commands.json
conan-graph.json
CMakeUserPresets.json
conan_toolchain.cmake
conandeps_legacy.cmake
*Config.cmake
*Targets*.cmake
*-config.cmake

# --- OS / editor ---
.DS_Store
*.swp
.idea/
.vscode/

# --- logs / temp ---
*.log
*.tmp

# --- superpowers scratch ---
.superpowers/
```

- [ ] **Step 2: Write `include/flight_recorder/Config.h`**

```cpp
#pragma once

// FLIGHT_RECORDER master switch. The build defines it to 1 (ON) or 0 (OFF).
// If a consumer includes headers without the define, default to enabled.
#ifndef FLIGHT_RECORDER
#define FLIGHT_RECORDER 1
#endif

// Platform detection for the timestamp source.
#if defined(__x86_64__) || defined(_M_X64)
#define FLIGHT_RECORDER_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define FLIGHT_RECORDER_ARM 1
#else
#define FLIGHT_RECORDER_FALLBACK 1
#endif

#define FLIGHT_RECORDER_CONCAT_INNER(a, b) a##b
#define FLIGHT_RECORDER_CONCAT(a, b) FLIGHT_RECORDER_CONCAT_INNER(a, b)
```

- [ ] **Step 3: Write `include/flight_recorder/Version.h` and the failing test**

`include/flight_recorder/Version.h`:
```cpp
#pragma once

namespace flight_recorder {
inline constexpr const char* kVersion = "0.1.0-snapshot";
}
```

`include/flight_recorder/FlightRecorder.h` (umbrella — grows as tasks land; start minimal):
```cpp
#pragma once

#include "flight_recorder/Config.h"
#include "flight_recorder/Version.h"
```

`tests/VersionTest.cpp`:
```cpp
#include <gtest/gtest.h>
#include <string>
#include "flight_recorder/Version.h"

TEST(Version, IsSet) {
    EXPECT_EQ(std::string(flight_recorder::kVersion), "0.1.0-snapshot");
}
```

- [ ] **Step 4: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.31)
project(flight_recorder CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" FLIGHT_RECORDER_VERSION)

option(FLIGHT_RECORDER "Enable flight-recorder flight recording (compile-in)" ON)
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

# Compiled cold-path library (calibration, registry, reporting).
add_library(flight_recorder_core STATIC
    src/Calibration.cpp
    src/Enable.cpp
    src/Registry.cpp
    src/Report.cpp
)
target_include_directories(flight_recorder_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(flight_recorder_core PUBLIC FLIGHT_RECORDER=$<BOOL:${FLIGHT_RECORDER}>)

# Public consumable target: headers + the core lib.
add_library(flight_recorder INTERFACE)
target_include_directories(flight_recorder INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(flight_recorder INTERFACE flight_recorder_core)
target_compile_definitions(flight_recorder INTERFACE FLIGHT_RECORDER=$<BOOL:${FLIGHT_RECORDER}>)
add_library(flight_recorder::flight_recorder ALIAS flight_recorder)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_executable(flight_recorder_tests
        tests/VersionTest.cpp
    )
    target_link_libraries(flight_recorder_tests PRIVATE flight_recorder GTest::gtest_main)
    gtest_discover_tests(flight_recorder_tests)
endif()

if(BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_executable(flight_recorder_bench
        bench/fr_overhead_bench.cpp
    )
    target_link_libraries(flight_recorder_bench PRIVATE flight_recorder benchmark::benchmark)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS flight_recorder flight_recorder_core EXPORT flight_recorderTargets)
install(EXPORT flight_recorderTargets NAMESPACE flight_recorder:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flight_recorder)
```

> Note: `src/Calibration.cpp`, `src/Enable.cpp`, `src/Registry.cpp`, `src/Report.cpp` are created in later tasks. For Task 1 to build, create each as a stub containing only `#include "flight_recorder/Config.h"` so the static lib links. Replace stubs in their owning tasks.

- [ ] **Step 5: Create the four `.cpp` stubs so P0 links**

Create each of `src/Calibration.cpp`, `src/Enable.cpp`, `src/Registry.cpp`, `src/Report.cpp` with exactly:
```cpp
#include "flight_recorder/Config.h"
```

- [ ] **Step 6: Write `conandata.yml`, `conanfile.py`, `README.md`**

`conandata.yml`:
```yaml
test_requirements:
  - "gtest/1.16.0"
  - "benchmark/1.8.2"
  - "nlohmann_json/3.12.0"
```

`conanfile.py`:
```python
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
import os


class FlightRecorderConan(ConanFile):
    name = "flight-recorder"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"flight_recorder": [True, False]}
    default_options = {"flight_recorder": True}
    exports_sources = "src/*", "include/*", "CMakeLists.txt", "VERSION.txt"

    def set_version(self):
        version_file = os.path.join(os.path.dirname(__file__), "VERSION.txt")
        self.version = open(version_file).read().strip()

    def build_requirements(self):
        for requirement in self.conan_data.get("test_requirements", []):
            self.test_requires(requirement)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["FLIGHT_RECORDER"] = self.options.flight_recorder
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "flight_recorder::flight_recorder")
        self.cpp_info.libs = ["flight_recorder_core"]
        self.cpp_info.includedirs = ["include"]
        if self.options.flight_recorder:
            self.cpp_info.defines = ["FLIGHT_RECORDER=1"]
        else:
            self.cpp_info.defines = ["FLIGHT_RECORDER=0"]
```

`README.md` (skeleton — Task 12 fills the overhead numbers):
```markdown
# flight-recorder

Ultra-low-overhead C++23 flight recorder (JFR-style begin/commit spans) for Rune's hot paths.

## Build

```bash
conan install . -of build -s build_type=Release -o flight-recorder/*:flight_recorder=True --build=missing
cmake --preset conan-release -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON
cmake --build build/Release -j
ctest --test-dir build/Release --output-on-failure
```

## Usage

```cpp
#include <flight_recorder/FlightRecorder.h>

flight_recorder::Event ev(flight_recorder::Site::WorkerAppend);
ev.begin().corr(correlationId);
// ... work ...
ev.commit();

// or RAII:
FLIGHT_RECORDER_SPAN(flight_recorder::Site::WorkerAppend, correlationId);
```

## Reports

Off the hot path, drain via `flight_recorder::Recorder`:
- `dumpHistograms(out)` — per-site p50/p90/p99/p999/max/count/mean (ns).
- `exportChromeTrace(path)` — open in `chrome://tracing`, Perfetto, or speedscope.
- `stitchByCorr(corr, out)` — per-request waterfall across threads.
- `writeTape(path)` / `readTape(path)` — versioned raw dump.

## Overhead

<!-- filled by Task 12 from bench numbers on the reference VM -->
```

- [ ] **Step 7: Configure, build, and run the test — verify green**

Run:
```bash
conan install . -of build -s build_type=Debug -s compiler.cppstd=23 --build=missing
cmake --preset conan-debug -DBUILD_TESTING=ON
cmake --build build/Debug -j
ctest --test-dir build/Debug --output-on-failure
```
Expected: `flight_recorder_tests` builds; `Version.IsSet` PASSES.

- [ ] **Step 8: Verify macOS/arm configure guard**

Confirm `CMakeLists.txt` has no x86-only `find_library`/flags at configure time. (The platform split lives entirely in `Config.h`/`Tsc.h` at compile time.) This satisfies the "green on macOS armv8" constraint.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: P0 repo scaffold, CMake/Conan wiring, gtest green"
```

---

## Task 2: TSC read + calibration (P1)

**Files:**
- Create: `include/flight_recorder/Tsc.h`, `include/flight_recorder/Calibration.h`
- Modify: `src/Calibration.cpp` (replace stub)
- Test: `tests/CalibrationTest.cpp`
- Modify: `CMakeLists.txt` (add test file), `include/flight_recorder/FlightRecorder.h` (add includes)

**Interfaces:**
- Produces:
  - `uint64_t flight_recorder::rdtscp() noexcept` — monotonic per-thread timestamp counter read.
  - `bool flight_recorder::hasInvariantTsc() noexcept`.
  - `void flight_recorder::calibrate() noexcept` — measures/sets `tscPerNs`; idempotent.
  - `double flight_recorder::tscPerNs() noexcept` — always > 0 after calibrate; defaults to 1.0.
  - `double flight_recorder::cyclesToNs(uint64_t cycles) noexcept`, `uint64_t flight_recorder::nsToCycles(double ns) noexcept`.
- Consumes: nothing prior.

- [ ] **Step 1: Write `include/flight_recorder/Tsc.h`**

```cpp
#pragma once

#include <cstdint>
#include "flight_recorder/Config.h"

#if defined(FLIGHT_RECORDER_X86)
#include <x86intrin.h>
#include <cpuid.h>
#endif
#if !defined(FLIGHT_RECORDER_X86)
#include <ctime>
#endif

namespace flight_recorder {

// Read the timestamp counter. Ordering is guaranteed within a thread, which is
// all the write path needs (same-thread start/end). Cross-core absolute ordering
// assumes invariant + synchronized TSC (documented; true on modern single-socket).
inline uint64_t rdtscp() noexcept {
#if defined(FLIGHT_RECORDER_X86)
    unsigned aux;
    return __builtin_ia32_rdtscp(&aux);
#elif defined(FLIGHT_RECORDER_ARM)
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
#endif
}

// Nominal counter frequency in Hz for the non-x86 counters, when known.
// On ARM, cntfrq_el0 gives the cntvct_el0 tick rate directly.
inline uint64_t counterFrequencyHz() noexcept {
#if defined(FLIGHT_RECORDER_ARM)
    uint64_t f;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
#else
    return 0; // unknown / measured by calibration
#endif
}

inline bool hasInvariantTsc() noexcept {
#if defined(FLIGHT_RECORDER_X86)
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(0x80000007u, &eax, &ebx, &ecx, &edx)) return false;
    return (edx & (1u << 8)) != 0; // invariant TSC bit
#else
    return true; // ARM cntvct and the ns fallback are inherently stable
#endif
}

} // namespace flight_recorder
```

- [ ] **Step 2: Write `include/flight_recorder/Calibration.h`**

```cpp
#pragma once

#include <cstdint>

namespace flight_recorder {

// Measure TSC ticks per nanosecond once at startup (idempotent). On the ns
// fallback path tscPerNs stays 1.0 (rdtscp already returns ns).
void calibrate() noexcept;

double tscPerNs() noexcept;

inline double cyclesToNs(uint64_t cycles) noexcept {
    return static_cast<double>(cycles) / tscPerNs();
}

inline uint64_t nsToCycles(double ns) noexcept {
    return static_cast<uint64_t>(ns * tscPerNs());
}

} // namespace flight_recorder
```

- [ ] **Step 3: Write the failing test `tests/CalibrationTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "flight_recorder/Tsc.h"
#include "flight_recorder/Calibration.h"

TEST(Calibration, TscPerNsIsPositive) {
    flight_recorder::calibrate();
    EXPECT_GT(flight_recorder::tscPerNs(), 0.0);
}

TEST(Calibration, RdtscpMonotonicWithinThread) {
    uint64_t a = flight_recorder::rdtscp();
    uint64_t b = flight_recorder::rdtscp();
    EXPECT_GE(b, a);
}

TEST(Calibration, KnownSleepMeasuresWithinTolerance) {
    flight_recorder::calibrate();
    uint64_t start = flight_recorder::rdtscp();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint64_t end = flight_recorder::rdtscp();
    double measuredNs = flight_recorder::cyclesToNs(end - start);
    // 50 ms = 50e6 ns; allow generous scheduler slop (30%..300%).
    EXPECT_GT(measuredNs, 35'000'000.0);
    EXPECT_LT(measuredNs, 150'000'000.0);
}

TEST(Calibration, FallbackNeverCrashesAndStaysPositive) {
    // Whatever platform this runs on, calibrate + read must be safe and > 0.
    flight_recorder::calibrate();
    EXPECT_TRUE(flight_recorder::tscPerNs() > 0.0);
    (void)flight_recorder::hasInvariantTsc(); // must not crash on any target
}
```

- [ ] **Step 4: Run to verify it fails**

Add `tests/CalibrationTest.cpp` to `flight_recorder_tests` in `CMakeLists.txt`, then:
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Calibration --output-on-failure
```
Expected: FAIL to link — `flight_recorder::calibrate`/`tscPerNs` undefined (stub `Calibration.cpp`).

- [ ] **Step 5: Implement `src/Calibration.cpp`**

```cpp
#include "flight_recorder/Calibration.h"
#include "flight_recorder/Tsc.h"

#include <atomic>
#include <ctime>

namespace flight_recorder {
namespace {
std::atomic<double> g_tscPerNs{1.0};
std::atomic<bool>   g_calibrated{false};

uint64_t monotonicRawNs() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}
} // namespace

void calibrate() noexcept {
    bool expected = false;
    if (!g_calibrated.compare_exchange_strong(expected, true)) return;

#if defined(FLIGHT_RECORDER_FALLBACK)
    g_tscPerNs.store(1.0, std::memory_order_relaxed); // rdtscp() already returns ns
    return;
#elif defined(FLIGHT_RECORDER_ARM)
    uint64_t f = counterFrequencyHz();
    g_tscPerNs.store(f > 0 ? static_cast<double>(f) / 1e9 : 1.0,
                     std::memory_order_relaxed);
    return;
#else
    // x86: measure TSC ticks over a fixed CLOCK_MONOTONIC_RAW window.
    constexpr uint64_t kWindowNs = 10'000'000ull; // 10 ms
    uint64_t nsStart = monotonicRawNs();
    uint64_t tscStart = rdtscp();
    uint64_t nsNow;
    do {
        nsNow = monotonicRawNs();
    } while (nsNow - nsStart < kWindowNs);
    uint64_t tscEnd = rdtscp();

    double elapsedNs = static_cast<double>(nsNow - nsStart);
    double elapsedTsc = static_cast<double>(tscEnd - tscStart);
    double perNs = elapsedNs > 0.0 ? elapsedTsc / elapsedNs : 1.0;
    g_tscPerNs.store(perNs > 0.0 ? perNs : 1.0, std::memory_order_relaxed);
#endif
}

double tscPerNs() noexcept {
    return g_tscPerNs.load(std::memory_order_relaxed);
}

} // namespace flight_recorder
```

- [ ] **Step 6: Run tests — verify pass**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Calibration --output-on-failure
```
Expected: all 4 PASS.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat: P1 TSC read + calibration with fallback path"
```

---

## Task 3: FrRecord POD + Site registry (P2a)

**Files:**
- Create: `include/flight_recorder/FrRecord.h`, `include/flight_recorder/Sites.h`
- Test: `tests/SitesTest.cpp`
- Modify: `CMakeLists.txt`, `include/flight_recorder/FlightRecorder.h`

**Interfaces:**
- Produces:
  - `struct flight_recorder::FrRecord { uint64_t startTsc; uint64_t endTsc; uint32_t corr; uint16_t site; uint16_t arg; }` (24 bytes) + `uint64_t durationTsc() const`.
  - `enum class flight_recorder::Category : uint8_t { Produce, Replication, Read, Consensus, Storage, Egress, Count }`.
  - `enum class flight_recorder::Site : uint16_t { None=0, IngressRecv, GatewayHandle, WorkerEnqueue, WorkerAppend, StorageWrite, EgressClaim, EgressSend, ClientAckRecv, Count }`.
  - `struct flight_recorder::SiteMeta { const char* name; Category category; }`, `inline constexpr SiteMeta kSiteMeta[]`.
  - `Category flight_recorder::categoryOf(Site) noexcept`, `const char* flight_recorder::nameOf(Site) noexcept`.

- [ ] **Step 1: Write `include/flight_recorder/FrRecord.h`**

```cpp
#pragma once

#include <cstdint>
#include <bit>

namespace flight_recorder {

// v1 record: 24 bytes, 8-byte aligned. Absolute start+end stamps (not just
// duration) so the exporter can place spans on a shared timeline. The 16-byte
// {start,dur32,...} variant was rejected for v1 (spec §2.2): clarity + timeline
// win over halving memory; revisit only under measured memory pressure.
struct FrRecord {
    uint64_t startTsc; // TSC at begin()
    uint64_t endTsc;   // TSC at commit(); == startTsc for an instant/mark
    uint32_t corr;     // correlation id (0 = none)
    uint16_t site;     // Site
    uint16_t arg;      // small per-site payload; 0 if unused

    // Guard against end-before-start (clock skew / equal stamps): never underflow.
    [[nodiscard]] uint64_t durationTsc() const noexcept {
        return endTsc >= startTsc ? endTsc - startTsc : 0;
    }
};

static_assert(sizeof(FrRecord) == 24, "FrRecord must be 24 bytes");
static_assert(alignof(FrRecord) == 8, "FrRecord must be 8-byte aligned");
static_assert(std::endian::native == std::endian::little,
              "flight-recorder assumes a little-endian target (matches Rune)");

} // namespace flight_recorder
```

- [ ] **Step 2: Write `include/flight_recorder/Sites.h`**

```cpp
#pragma once

#include <cstdint>

namespace flight_recorder {

enum class Category : uint8_t {
    Produce,
    Replication,
    Read,
    Consensus,
    Storage,
    Egress,
    Count
};

// Static site registry — ONE source of truth. Hot path carries only the id.
enum class Site : uint16_t {
    None = 0,
    // --- produce path (Category::Produce) ---
    IngressRecv,
    GatewayHandle,
    WorkerEnqueue,
    WorkerAppend,
    StorageWrite,
    EgressClaim,
    EgressSend,
    ClientAckRecv,
    // ... grows as more paths are instrumented ...
    Count
};

struct SiteMeta {
    const char* name;
    Category category;
};

// Indexed by Site value. MUST stay in lockstep with the Site enum order.
inline constexpr SiteMeta kSiteMeta[] = {
    {"None", Category::Produce},
    {"IngressRecv", Category::Produce},
    {"GatewayHandle", Category::Produce},
    {"WorkerEnqueue", Category::Produce},
    {"WorkerAppend", Category::Produce},
    {"StorageWrite", Category::Storage},
    {"EgressClaim", Category::Egress},
    {"EgressSend", Category::Egress},
    {"ClientAckRecv", Category::Produce},
};

static_assert(sizeof(kSiteMeta) / sizeof(kSiteMeta[0]) ==
                  static_cast<size_t>(Site::Count),
              "kSiteMeta must have exactly one entry per Site (excluding Count)");

inline Category categoryOf(Site s) noexcept {
    return kSiteMeta[static_cast<uint16_t>(s)].category;
}

inline const char* nameOf(Site s) noexcept {
    return kSiteMeta[static_cast<uint16_t>(s)].name;
}

} // namespace flight_recorder
```

- [ ] **Step 3: Write the failing test `tests/SitesTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <string>
#include "flight_recorder/FrRecord.h"
#include "flight_recorder/Sites.h"

TEST(FrRecord, IsTwentyFourBytes) {
    EXPECT_EQ(sizeof(flight_recorder::FrRecord), 24u);
}

TEST(FrRecord, DurationNeverUnderflows) {
    flight_recorder::FrRecord r{100, 90, 0, 0, 0}; // end before start
    EXPECT_EQ(r.durationTsc(), 0u);
    flight_recorder::FrRecord eq{100, 100, 0, 0, 0}; // instant
    EXPECT_EQ(eq.durationTsc(), 0u);
    flight_recorder::FrRecord span{100, 175, 0, 0, 0};
    EXPECT_EQ(span.durationTsc(), 75u);
}

TEST(Sites, CategoryAndNameLookup) {
    EXPECT_EQ(flight_recorder::categoryOf(flight_recorder::Site::WorkerAppend), flight_recorder::Category::Produce);
    EXPECT_EQ(flight_recorder::categoryOf(flight_recorder::Site::StorageWrite), flight_recorder::Category::Storage);
    EXPECT_EQ(std::string(flight_recorder::nameOf(flight_recorder::Site::EgressSend)), "EgressSend");
}

TEST(Sites, MetaTableCoversEverySite) {
    EXPECT_EQ(sizeof(flight_recorder::kSiteMeta) / sizeof(flight_recorder::kSiteMeta[0]),
              static_cast<size_t>(flight_recorder::Site::Count));
}
```

- [ ] **Step 4: Run to verify it fails, then passes**

Add `tests/SitesTest.cpp` to `flight_recorder_tests`. These are header-only; the test compiles against them directly.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R "FrRecord|Sites" --output-on-failure
```
Expected: builds and PASSES once headers are added (the `static_assert`s enforce table/enum lockstep at compile time). If it fails to compile, the meta table is out of sync — fix `kSiteMeta`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: P2a FrRecord POD + static Site registry"
```

---

## Task 4: Per-thread ring buffer (P2b)

**Files:**
- Create: `include/flight_recorder/Ring.h`
- Test: `tests/RingTest.cpp`
- Modify: `CMakeLists.txt`, `include/flight_recorder/FlightRecorder.h`

**Interfaces:**
- Produces:
  - `enum class flight_recorder::RingMode { OverwriteOldest, StopWhenFull }`.
  - `class flight_recorder::Ring` with:
    - `Ring(size_t capacityPow2, RingMode mode)` — asserts capacity is a nonzero power of two; allocates `capacityPow2` records once.
    - `void push(const FrRecord& r) noexcept` — single-producer write path (no alloc, no lock).
    - `uint64_t writeCount() const noexcept` — acquire-load of total pushes (monotonic; may exceed capacity in overwrite mode).
    - `size_t capacity() const noexcept`, `RingMode mode() const noexcept`.
    - `size_t snapshot(std::vector<FrRecord>& out) const` — copies the currently-live records in chronological order; returns count.
- Consumes: `FrRecord` (Task 3).

- [ ] **Step 1: Write `include/flight_recorder/Ring.h`**

```cpp
#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "flight_recorder/FrRecord.h"

namespace flight_recorder {

enum class RingMode { OverwriteOldest, StopWhenFull };

// Single-producer (owning thread) / single-consumer (drainer) POD ring.
// The only cross-thread sync is writeIndex_ published with release and read
// with acquire. No locks, no allocation, no syscalls on push().
class Ring {
public:
    Ring(size_t capacityPow2, RingMode mode)
        : capacity_(capacityPow2),
          mask_(capacityPow2 - 1),
          mode_(mode),
          buffer_(std::make_unique<FrRecord[]>(capacityPow2)) {
        assert(capacityPow2 != 0 && (capacityPow2 & (capacityPow2 - 1)) == 0 &&
               "Ring capacity must be a nonzero power of two");
    }

    void push(const FrRecord& r) noexcept {
        uint64_t idx = writeIndex_.load(std::memory_order_relaxed);
        if (mode_ == RingMode::StopWhenFull && idx >= capacity_) return;
        buffer_[idx & mask_] = r;
        writeIndex_.store(idx + 1, std::memory_order_release);
    }

    [[nodiscard]] uint64_t writeCount() const noexcept {
        return writeIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] RingMode mode() const noexcept { return mode_; }

    // Copy currently-live records in chronological (oldest-first) order.
    size_t snapshot(std::vector<FrRecord>& out) const {
        uint64_t total = writeIndex_.load(std::memory_order_acquire);
        uint64_t live = total < capacity_ ? total : capacity_;
        uint64_t start = total - live; // absolute index of oldest live record
        out.clear();
        out.reserve(live);
        for (uint64_t i = 0; i < live; ++i) {
            out.push_back(buffer_[(start + i) & mask_]);
        }
        return static_cast<size_t>(live);
    }

private:
    size_t capacity_;
    size_t mask_;
    RingMode mode_;
    std::unique_ptr<FrRecord[]> buffer_;
    std::atomic<uint64_t> writeIndex_{0};
};

} // namespace flight_recorder
```

- [ ] **Step 2: Write the failing test `tests/RingTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <vector>
#include "flight_recorder/Ring.h"

using flight_recorder::FrRecord;
using flight_recorder::Ring;
using flight_recorder::RingMode;

static FrRecord rec(uint64_t start) {
    return FrRecord{start, start + 1, 0, 1, 0};
}

TEST(Ring, FillBelowCapacityKeepsAll) {
    Ring ring(8, RingMode::OverwriteOldest);
    for (uint64_t i = 0; i < 5; ++i) ring.push(rec(i));
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 5u);
    EXPECT_EQ(out.front().startTsc, 0u);
    EXPECT_EQ(out.back().startTsc, 4u);
}

TEST(Ring, OverwriteOldestWrapsAndKeepsNewest) {
    Ring ring(4, RingMode::OverwriteOldest);
    for (uint64_t i = 0; i < 10; ++i) ring.push(rec(i)); // 6..9 survive
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 4u);
    EXPECT_EQ(out.front().startTsc, 6u);
    EXPECT_EQ(out.back().startTsc, 9u);
    EXPECT_EQ(ring.writeCount(), 10u);
}

TEST(Ring, StopWhenFullDropsNewest) {
    Ring ring(4, RingMode::StopWhenFull);
    for (uint64_t i = 0; i < 10; ++i) ring.push(rec(i)); // only 0..3 kept
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 4u);
    EXPECT_EQ(out.front().startTsc, 0u);
    EXPECT_EQ(out.back().startTsc, 3u);
}

TEST(Ring, RejectsNonPowerOfTwoCapacity) {
    // Review Focus: capacity must be a nonzero power of two.
    EXPECT_DEATH({ Ring bad(6, RingMode::OverwriteOldest); }, "power of two");
    EXPECT_DEATH({ Ring zero(0, RingMode::OverwriteOldest); }, "power of two");
}
```

- [ ] **Step 3: Run — verify fail then pass**

Add `tests/RingTest.cpp` to `flight_recorder_tests`.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Ring --output-on-failure
```
Expected: PASS. (`EXPECT_DEATH` needs the assert active — build type `Debug` keeps `assert` on. If the project ever runs these under `NDEBUG`, guard those two lines with `#ifndef NDEBUG`.)

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: P2b single-producer ring buffer, both modes"
```

---

## Task 5: Thread registry + Recorder singleton (P2c)

**Files:**
- Create: `include/flight_recorder/Recorder.h`
- Modify: `src/Registry.cpp` (replace stub)
- Test: `tests/RegistryTest.cpp`
- Modify: `CMakeLists.txt`, `include/flight_recorder/FlightRecorder.h`

**Interfaces:**
- Produces:
  - `struct flight_recorder::ThreadRecorder { Ring ring; uint64_t osTid; std::string threadName; }`.
  - `ThreadRecorder* flight_recorder::threadRecorder() noexcept` — TLS accessor; lazily registers on first call (cold; mutex + alloc once per thread), then a pure TLS load.
  - `class flight_recorder::Recorder` (singleton) with:
    - `static Recorder& instance() noexcept`.
    - `void configure(size_t capacityPow2, RingMode mode) noexcept` — sets defaults for threads registered afterward (default 65536 / OverwriteOldest).
    - `ThreadRecorder* registerThread()` — used by `threadRecorder()`; allocates + links under mutex.
    - `std::vector<ThreadRecorder*> snapshotThreads() const` — copy of the registry pointer list (drainer entry point).
    - `void reset() noexcept` — clears the registry (test-support; also resets TLS pointers lazily).
- Consumes: `Ring`, `RingMode` (Task 4).

- [ ] **Step 1: Write `include/flight_recorder/Recorder.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "flight_recorder/Ring.h"

namespace flight_recorder {

inline constexpr size_t kDefaultRingCapacity = 65536; // 1.5 MiB/thread at 24 B

struct ThreadRecorder {
    Ring ring;
    uint64_t osTid;
    std::string threadName;

    ThreadRecorder(size_t cap, RingMode mode, uint64_t tid, std::string name)
        : ring(cap, mode), osTid(tid), threadName(std::move(name)) {}
};

class Recorder {
public:
    static Recorder& instance() noexcept;

    void configure(size_t capacityPow2, RingMode mode) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        defaultCapacity_ = capacityPow2;
        defaultMode_ = mode;
    }

    ThreadRecorder* registerThread();

    std::vector<ThreadRecorder*> snapshotThreads() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return threads_;
    }

    void reset() noexcept;

private:
    Recorder() = default;

    mutable std::mutex mutex_;
    std::vector<ThreadRecorder*> threads_;
    std::vector<std::unique_ptr<ThreadRecorder>> owned_;
    size_t defaultCapacity_ = kDefaultRingCapacity;
    RingMode defaultMode_ = RingMode::OverwriteOldest;
};

// TLS accessor: first call registers this thread (cold), then pure TLS load.
ThreadRecorder* threadRecorder() noexcept;

} // namespace flight_recorder
```

- [ ] **Step 2: Write the failing test `tests/RegistryTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include "flight_recorder/Recorder.h"

using namespace flight_recorder;

TEST(Registry, SameThreadReturnsSameRecorder) {
    Recorder::instance().reset();
    ThreadRecorder* a = threadRecorder();
    ThreadRecorder* b = threadRecorder();
    EXPECT_EQ(a, b);
    EXPECT_NE(a, nullptr);
}

TEST(Registry, EachThreadGetsItsOwnRing) {
    Recorder::instance().reset();
    constexpr int kThreads = 4;
    std::vector<ThreadRecorder*> seen(kThreads, nullptr);
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&, i] { seen[i] = threadRecorder(); });
    }
    for (auto& t : ts) t.join();

    std::set<ThreadRecorder*> unique(seen.begin(), seen.end());
    EXPECT_EQ(unique.size(), static_cast<size_t>(kThreads)); // all distinct rings
    EXPECT_EQ(Recorder::instance().snapshotThreads().size(),
              static_cast<size_t>(kThreads));
}
```

- [ ] **Step 3: Run — verify fail (undefined symbols)**

```bash
cmake --build build/Debug -j
```
Expected: link error — `Recorder::instance`, `registerThread`, `threadRecorder`, `reset` undefined.

- [ ] **Step 4: Implement `src/Registry.cpp`**

```cpp
#include "flight_recorder/Recorder.h"
#include "flight_recorder/Calibration.h"

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
        defaultCapacity_, defaultMode_, currentOsTid(), std::string{});
    ThreadRecorder* raw = tr.get();
    owned_.push_back(std::move(tr));
    threads_.push_back(raw);
    return raw;
}

void Recorder::reset() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    threads_.clear();
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

} // namespace flight_recorder
```

> This introduces a `generation()` accessor + counter on `Recorder` so `reset()` invalidates stale TLS pointers (needed only because tests reset; production never resets). Add to `Recorder.h`: a `std::atomic<uint64_t> generation_{0};` member, `uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }`, and bump it (`generation_.fetch_add(1, std::memory_order_release);`) inside `reset()` before clearing.

- [ ] **Step 5: Add `generation()` to `Recorder.h` and bump in `reset()`**

In `Recorder.h`, add the public method and private member described above. Change `reset()`'s declaration is unchanged; its definition (in `Registry.cpp`) becomes:
```cpp
void Recorder::reset() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    generation_.fetch_add(1, std::memory_order_release);
    threads_.clear();
    owned_.clear();
}
```

- [ ] **Step 6: Run tests — verify pass**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Registry --output-on-failure
```
Expected: both PASS.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat: P2c thread registry + Recorder singleton"
```

---

## Task 6: Enable / disable / category mask (P3a)

**Files:**
- Create: `include/flight_recorder/Enable.h`
- Modify: `src/Enable.cpp` (replace stub)
- Test: `tests/EnableTest.cpp`
- Modify: `CMakeLists.txt`, `include/flight_recorder/FlightRecorder.h`

**Interfaces:**
- Produces:
  - `void flight_recorder::enable() noexcept`, `void flight_recorder::disable() noexcept`.
  - `void flight_recorder::setCategoryMask(uint32_t mask) noexcept`, `uint32_t flight_recorder::categoryMask() noexcept`.
  - `bool flight_recorder::isEnabled() noexcept`.
  - `bool flight_recorder::enabled(Site s) noexcept` — `g_enabled && (mask & bit(categoryOf(s)))`; two relaxed loads + a branch, predicted not-taken when off.
  - `constexpr uint32_t flight_recorder::categoryBit(Category c) noexcept`.
- Consumes: `Site`, `Category`, `categoryOf` (Task 3).

- [ ] **Step 1: Write `include/flight_recorder/Enable.h`**

```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include "flight_recorder/Sites.h"

namespace flight_recorder {

// Definitions live in Enable.cpp so the atomics have one home.
extern std::atomic<bool> g_enabled;
extern std::atomic<uint32_t> g_categoryMask;

constexpr uint32_t categoryBit(Category c) noexcept {
    return 1u << static_cast<uint32_t>(c);
}

inline void enable() noexcept { g_enabled.store(true, std::memory_order_relaxed); }
inline void disable() noexcept { g_enabled.store(false, std::memory_order_relaxed); }
inline bool isEnabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

inline void setCategoryMask(uint32_t mask) noexcept {
    g_categoryMask.store(mask, std::memory_order_relaxed);
}
inline uint32_t categoryMask() noexcept {
    return g_categoryMask.load(std::memory_order_relaxed);
}

inline bool enabled(Site s) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    return (g_categoryMask.load(std::memory_order_relaxed) &
            categoryBit(categoryOf(s))) != 0;
}

} // namespace flight_recorder
```

- [ ] **Step 2: Write the failing test `tests/EnableTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include "flight_recorder/Enable.h"

using namespace flight_recorder;

TEST(Enable, DisabledSuppressesAllSites) {
    disable();
    setCategoryMask(0xFFFFFFFFu);
    EXPECT_FALSE(enabled(Site::WorkerAppend));
}

TEST(Enable, CategoryMaskGatesBySite) {
    enable();
    setCategoryMask(categoryBit(Category::Produce)); // only Produce
    EXPECT_TRUE(enabled(Site::WorkerAppend));       // Produce
    EXPECT_FALSE(enabled(Site::StorageWrite));      // Storage
    setCategoryMask(categoryBit(Category::Storage));
    EXPECT_TRUE(enabled(Site::StorageWrite));
    EXPECT_FALSE(enabled(Site::WorkerAppend));
    disable(); // leave global state clean for other tests
}
```

- [ ] **Step 3: Run — verify fail (undefined atomics)**

```bash
cmake --build build/Debug -j
```
Expected: link error — `g_enabled`/`g_categoryMask` undefined.

- [ ] **Step 4: Implement `src/Enable.cpp`**

```cpp
#include "flight_recorder/Enable.h"

namespace flight_recorder {
// Default: disabled, all categories armed (so a single enable() turns on all).
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_categoryMask{0xFFFFFFFFu};
} // namespace flight_recorder
```

- [ ] **Step 5: Run — verify pass**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Enable --output-on-failure
```
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: P3a runtime enable + category mask"
```

---

## Task 7: Event API + macros + compile-out (P3b)

**Files:**
- Create: `include/flight_recorder/Event.h`
- Test: `tests/EventTest.cpp`, `tests/CompileOutTest.cpp`
- Modify: `include/flight_recorder/FlightRecorder.h` (export full public surface), `CMakeLists.txt`

**Interfaces:**
- Produces (when `FLIGHT_RECORDER`):
  - `class flight_recorder::Event` — `explicit Event(Site) noexcept`, `Event& begin() noexcept`, `Event& corr(uint32_t) noexcept`, `Event& arg(uint16_t) noexcept`, `void commit() noexcept`.
  - `void flight_recorder::mark(Site, uint32_t corr, uint16_t arg = 0) noexcept`.
  - `class flight_recorder::ScopedSpan` — RAII begin/commit.
  - macros `FLIGHT_RECORDER_SPAN(site, corr)`, `FLIGHT_RECORDER_MARK(site, corr)`.
- When `!FLIGHT_RECORDER`: `Event`/`ScopedSpan`/`mark` are empty no-ops; macros expand to `do {} while (0)`.
- Consumes: `rdtscp` (Task 2), `FrRecord`/`Site` (Task 3), `threadRecorder` (Task 5), `enabled` (Task 6).

- [ ] **Step 1: Write `include/flight_recorder/Event.h`**

```cpp
#pragma once

#include <cstdint>

#include "flight_recorder/Config.h"
#include "flight_recorder/Sites.h"

#if FLIGHT_RECORDER

#include "flight_recorder/FrRecord.h"
#include "flight_recorder/Tsc.h"
#include "flight_recorder/Enable.h"
#include "flight_recorder/Recorder.h"

namespace flight_recorder {

class Event {
public:
    explicit Event(Site s) noexcept
        : rec_{0, 0, 0, static_cast<uint16_t>(s), 0}, on_(false) {}

    // Gate ONCE here so the disabled path is two relaxed loads + a branch.
    Event& begin() noexcept {
        on_ = enabled(static_cast<Site>(rec_.site));
        if (on_) rec_.startTsc = rdtscp();
        return *this;
    }
    Event& corr(uint32_t c) noexcept { rec_.corr = c; return *this; }
    Event& arg(uint16_t a) noexcept { rec_.arg = a; return *this; }

    void commit() noexcept {
        if (!on_) return;
        rec_.endTsc = rdtscp();
        threadRecorder()->ring.push(rec_);
    }

private:
    FrRecord rec_;
    bool on_;
};

inline void mark(Site s, uint32_t corr, uint16_t arg = 0) noexcept {
    if (!enabled(s)) return;
    uint64_t t = rdtscp();
    threadRecorder()->ring.push(
        FrRecord{t, t, corr, static_cast<uint16_t>(s), arg});
}

class ScopedSpan {
public:
    ScopedSpan(Site s, uint32_t corr) noexcept : ev_(s) { ev_.begin().corr(corr); }
    ~ScopedSpan() { ev_.commit(); }
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

private:
    Event ev_;
};

} // namespace flight_recorder

#define FLIGHT_RECORDER_SPAN(site, corr) \
    ::flight_recorder::ScopedSpan FLIGHT_RECORDER_CONCAT(_fr_span_, __LINE__){(site), (corr)}
#define FLIGHT_RECORDER_MARK(site, corr) ::flight_recorder::mark((site), (corr))

#else // !FLIGHT_RECORDER — compiled out: zero code.

namespace flight_recorder {
class Event {
public:
    explicit Event(Site) noexcept {}
    Event& begin() noexcept { return *this; }
    Event& corr(uint32_t) noexcept { return *this; }
    Event& arg(uint16_t) noexcept { return *this; }
    void commit() noexcept {}
};
inline void mark(Site, uint32_t, uint16_t = 0) noexcept {}
class ScopedSpan {
public:
    ScopedSpan(Site, uint32_t) noexcept {}
};
} // namespace flight_recorder

#define FLIGHT_RECORDER_SPAN(site, corr) do {} while (0)
#define FLIGHT_RECORDER_MARK(site, corr) do {} while (0)

#endif // FLIGHT_RECORDER
```

- [ ] **Step 2: Update `include/flight_recorder/FlightRecorder.h` to the full public surface**

```cpp
#pragma once

#include "flight_recorder/Config.h"
#include "flight_recorder/Version.h"
#include "flight_recorder/Sites.h"
#include "flight_recorder/FrRecord.h"
#include "flight_recorder/Tsc.h"
#include "flight_recorder/Calibration.h"
#include "flight_recorder/Ring.h"
#include "flight_recorder/Enable.h"
#include "flight_recorder/Recorder.h"
#include "flight_recorder/Event.h"
#include "flight_recorder/Histogram.h"
#include "flight_recorder/Report.h"
```

> `Histogram.h` and `Report.h` are created in Tasks 8–11. If implementing strictly in order, add those two includes when those files exist; until then include only through `Event.h`. (Executors doing tasks out of order: create empty-guarded stubs.)

- [ ] **Step 3: Write the failing test `tests/EventTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <vector>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

namespace {
size_t liveRecords() {
    size_t total = 0;
    std::vector<FrRecord> tmp;
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        total += tr->ring.snapshot(tmp);
    }
    return total;
}
} // namespace

TEST(Event, BeginCommitProducesOneCorrectRecord) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(categoryBit(Category::Produce));

    Event ev(Site::WorkerAppend);
    ev.begin().corr(4242).arg(7);
    ev.commit();

    auto threads = Recorder::instance().snapshotThreads();
    ASSERT_EQ(threads.size(), 1u);
    std::vector<FrRecord> out;
    ASSERT_EQ(threads[0]->ring.snapshot(out), 1u);
    EXPECT_EQ(out[0].site, static_cast<uint16_t>(Site::WorkerAppend));
    EXPECT_EQ(out[0].corr, 4242u);
    EXPECT_EQ(out[0].arg, 7u);
    EXPECT_GE(out[0].endTsc, out[0].startTsc); // end >= start
    disable();
}

TEST(Event, DisabledProducesNoRecord) {
    Recorder::instance().reset();
    disable();
    Event ev(Site::WorkerAppend);
    ev.begin().corr(1);
    ev.commit();
    EXPECT_EQ(liveRecords(), 0u);
}

TEST(Event, CategoryMaskedOutProducesNoRecord) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(categoryBit(Category::Storage)); // WorkerAppend is Produce
    Event ev(Site::WorkerAppend);
    ev.begin();
    ev.commit();
    EXPECT_EQ(liveRecords(), 0u);
    disable();
}

TEST(Event, ScopedSpanRecordsOnDestruction) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    {
        FLIGHT_RECORDER_SPAN(Site::EgressSend, 99);
    }
    EXPECT_EQ(liveRecords(), 1u);
    disable();
}

TEST(Event, MarkRecordsInstantWithEqualStartEnd) {
    // Review Focus: mark => startTsc == endTsc, duration 0, accepted.
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    FLIGHT_RECORDER_MARK(Site::IngressRecv, 5);
    auto threads = Recorder::instance().snapshotThreads();
    ASSERT_EQ(threads.size(), 1u);
    std::vector<FrRecord> out;
    ASSERT_EQ(threads[0]->ring.snapshot(out), 1u);
    EXPECT_EQ(out[0].startTsc, out[0].endTsc);
    EXPECT_EQ(out[0].durationTsc(), 0u);
    disable();
}
```

- [ ] **Step 4: Run — verify pass**

Add `tests/EventTest.cpp` to `flight_recorder_tests` (skip `Histogram.h`/`Report.h` includes for now if not yet created — include `Event.h` directly instead of the umbrella).
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Event --output-on-failure
```
Expected: all PASS.

- [ ] **Step 5: Write the compile-out test `tests/CompileOutTest.cpp` + a dedicated target**

`tests/CompileOutTest.cpp`:
```cpp
// Compiled with FLIGHT_RECORDER=0. Proves Event/macros are no-ops and emit no recorder
// symbols. If any hot-path symbol were referenced, this TU would fail to link
// against a build that omits flight_recorder_core's recording — but here we simply assert
// the API compiles to nothing observable.
#include <gtest/gtest.h>
#include "flight_recorder/Event.h"

TEST(CompileOut, MacrosAndEventAreNoOps) {
    flight_recorder::Event ev(flight_recorder::Site::WorkerAppend);
    ev.begin().corr(1).arg(2);
    ev.commit();
    FLIGHT_RECORDER_SPAN(flight_recorder::Site::EgressSend, 3);
    FLIGHT_RECORDER_MARK(flight_recorder::Site::IngressRecv, 4);
    SUCCEED(); // compiling + linking with FLIGHT_RECORDER=0 is the assertion
}
```

In `CMakeLists.txt`, under `if(BUILD_TESTING)`, add a separate executable built with `FLIGHT_RECORDER=0`:
```cmake
    add_executable(flight_recorder_compileout_test tests/CompileOutTest.cpp)
    target_include_directories(flight_recorder_compileout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(flight_recorder_compileout_test PRIVATE FLIGHT_RECORDER=0)
    target_link_libraries(flight_recorder_compileout_test PRIVATE GTest::gtest_main)
    gtest_discover_tests(flight_recorder_compileout_test)
```
Note: it links **only** `GTest::gtest_main` — deliberately NOT `flight_recorder` — so if the no-op `Event.h` accidentally referenced `threadRecorder`/`rdtscp`, the link would fail and catch the regression.

- [ ] **Step 6: Run the compile-out test — verify pass**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R CompileOut --output-on-failure
```
Expected: builds (proving no recorder symbols referenced) and PASSES.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat: P3b Event/ScopedSpan/mark + macros + compile-out"
```

---

## Task 8: Log-linear Histogram (P4a)

**Files:**
- Create: `include/flight_recorder/Histogram.h`
- Test: `tests/HistogramTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class flight_recorder::Histogram` with `void record(uint64_t value) noexcept`, `uint64_t count() const`, `uint64_t min() const`, `uint64_t max() const`, `double mean() const`, `uint64_t valueAtPercentile(double p) const` (p in [0,100]).
  - Guarantees ≤ ~0.8% relative error (8 mantissa bits) for `valueAtPercentile`.
- Consumes: nothing prior.

- [ ] **Step 1: Write `include/flight_recorder/Histogram.h`**

```cpp
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
        uint64_t target = static_cast<uint64_t>((p / 100.0) * static_cast<double>(count_) + 0.5);
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
```

- [ ] **Step 2: Write the failing test `tests/HistogramTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <cmath>
#include "flight_recorder/Histogram.h"

using flight_recorder::Histogram;

TEST(Histogram, EmptyIsSafe) {
    Histogram h;
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h.valueAtPercentile(50.0), 0u);
    EXPECT_EQ(h.min(), 0u);
    EXPECT_EQ(h.mean(), 0.0);
}

TEST(Histogram, AcceptsZeroDurationSample) {
    // Review Focus: mark/instant events record duration 0.
    Histogram h;
    h.record(0);
    EXPECT_EQ(h.count(), 1u);
    EXPECT_EQ(h.min(), 0u);
    EXPECT_EQ(h.valueAtPercentile(50.0), 0u);
}

TEST(Histogram, UniformDistributionPercentilesWithinOnePercent) {
    Histogram h;
    for (uint64_t v = 1; v <= 100000; ++v) h.record(v);
    EXPECT_EQ(h.count(), 100000u);
    auto near = [](uint64_t got, double expected) {
        return std::fabs(static_cast<double>(got) - expected) / expected < 0.01;
    };
    EXPECT_TRUE(near(h.valueAtPercentile(50.0), 50000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(90.0), 90000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(99.0), 99000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(99.9), 99900.0));
    EXPECT_GE(h.max(), 100000u * 99 / 100); // max bucket lower-bound near 100000
}

TEST(Histogram, IndexLowerBoundRoundTripBoundsError) {
    for (uint64_t v : {1ull, 255ull, 256ull, 300ull, 1000ull, 1u<<20, 1ull<<40}) {
        uint64_t lb = Histogram::lowerBound(Histogram::index(v));
        EXPECT_LE(lb, v);
        if (v >= Histogram::kSub) {
            EXPECT_LT(static_cast<double>(v - lb) / v, 0.008); // < 0.8%
        }
    }
}
```

- [ ] **Step 3: Run — verify pass**

Add `tests/HistogramTest.cpp` to `flight_recorder_tests`.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Histogram --output-on-failure
```
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: P4a log-linear histogram (<1% error)"
```

---

## Task 9: dumpHistograms + stitchByCorr (P4b)

**Files:**
- Create: `include/flight_recorder/Report.h`
- Modify: `src/Report.cpp` (replace stub)
- Test: `tests/ReportHistogramTest.cpp`
- Modify: `CMakeLists.txt`, `include/flight_recorder/FlightRecorder.h`

**Interfaces:**
- Produces (in `namespace flight_recorder`):
  - `struct SiteStats { Site site; uint64_t count; uint64_t p50, p90, p99, p999, max; double mean; }` (all latencies in ns).
  - `std::vector<SiteStats> collectSiteStats()` — snapshots all rings, aggregates per-site duration histograms, converts cycles→ns.
  - `void dumpHistograms(std::ostream& out)` — human-readable table built from `collectSiteStats()`.
  - `struct WaterfallEntry { Site site; uint64_t startTsc; uint64_t durTsc; uint64_t offsetNs; double durNs; uint64_t osTid; }`.
  - `std::vector<WaterfallEntry> stitchByCorr(uint32_t corr)` — all events with that corr across threads, sorted by `startTsc`; **empty if `corr == 0`**.
  - `void printWaterfall(uint32_t corr, std::ostream& out)`.
- Consumes: `Recorder`/`ThreadRecorder` (Task 5), `Histogram` (Task 8), `cyclesToNs` (Task 2), `Site`/`nameOf` (Task 3).

- [ ] **Step 1: Write `include/flight_recorder/Report.h`**

```cpp
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "flight_recorder/Sites.h"

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

struct Tape;          // defined in Task 11
bool writeTape(const std::string& path);
bool readTape(const std::string& path, Tape& out);

} // namespace flight_recorder
```

- [ ] **Step 2: Write the failing test `tests/ReportHistogramTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <sstream>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

namespace {
// Push a record directly into this thread's ring, bypassing the TSC clock so
// durations are deterministic (in "cycles" == ns here since we control values).
void pushSynthetic(Site s, uint32_t corr, uint64_t start, uint64_t end, uint16_t arg = 0) {
    threadRecorder()->ring.push(
        FrRecord{start, end, corr, static_cast<uint16_t>(s), arg});
}
} // namespace

TEST(ReportHistogram, EmptyRingsProduceNoStatsAndDontCrash) {
    Recorder::instance().reset();
    std::ostringstream os;
    dumpHistograms(os); // must not throw / divide by zero
    EXPECT_TRUE(collectSiteStats().empty());
}

TEST(ReportHistogram, AggregatesDurationsPerSite) {
    Recorder::instance().reset();
    // WorkerAppend durations: 100..1000 ns (calibration forced to 1 tick/ns).
    calibrate(); // on fallback/x86 this sets tscPerNs; test tolerates either.
    for (uint64_t d = 100; d <= 1000; d += 100)
        pushSynthetic(Site::WorkerAppend, 1, 0, d);

    auto stats = collectSiteStats();
    ASSERT_FALSE(stats.empty());
    bool found = false;
    for (auto& s : stats) {
        if (s.site == Site::WorkerAppend) {
            found = true;
            EXPECT_EQ(s.count, 10u);
            EXPECT_GT(s.max, 0u);
            EXPECT_GT(s.p50, 0u);
        }
    }
    EXPECT_TRUE(found);
}

TEST(Stitch, CorrZeroReturnsEmpty) {
    // Review Focus: corr==0 is the "none" sentinel; never stitch it.
    Recorder::instance().reset();
    pushSynthetic(Site::IngressRecv, 0, 10, 20);
    pushSynthetic(Site::WorkerAppend, 0, 30, 40);
    EXPECT_TRUE(stitchByCorr(0).empty());
}

TEST(Stitch, OrdersByStartTsc) {
    Recorder::instance().reset();
    pushSynthetic(Site::EgressSend, 77, 300, 350);
    pushSynthetic(Site::IngressRecv, 77, 100, 150);
    pushSynthetic(Site::WorkerAppend, 77, 200, 260);

    auto wf = stitchByCorr(77);
    ASSERT_EQ(wf.size(), 3u);
    EXPECT_EQ(wf[0].site, Site::IngressRecv);
    EXPECT_EQ(wf[1].site, Site::WorkerAppend);
    EXPECT_EQ(wf[2].site, Site::EgressSend);
    EXPECT_EQ(wf[0].offsetNs, 0u); // earliest is the anchor
}
```

- [ ] **Step 3: Run — verify fail (undefined symbols)**

```bash
cmake --build build/Debug -j
```
Expected: link error for `collectSiteStats`/`dumpHistograms`/`stitchByCorr`.

- [ ] **Step 4: Implement histogram + stitch parts of `src/Report.cpp`**

```cpp
#include "flight_recorder/Report.h"
#include "flight_recorder/Recorder.h"
#include "flight_recorder/Histogram.h"
#include "flight_recorder/Calibration.h"
#include "flight_recorder/FrRecord.h"

#include <algorithm>
#include <array>
#include <iomanip>

namespace flight_recorder {

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

} // namespace flight_recorder
```

> `exportChromeTrace`, `writeTape`, `readTape` are still undefined at this point. To keep the test target linking, add temporary stubs at the bottom of `Report.cpp` returning `false` — Tasks 10 & 11 replace them. (Or implement Tasks 9–11 together before first link.)

- [ ] **Step 5: Run — verify pass**

Add `tests/ReportHistogramTest.cpp` to `flight_recorder_tests`.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R "ReportHistogram|Stitch" --output-on-failure
```
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: P4b dumpHistograms + stitchByCorr"
```

---

## Task 10: exportChromeTrace (P4c)

**Files:**
- Modify: `src/Report.cpp` (replace the `exportChromeTrace` stub)
- Test: `tests/ChromeTraceTest.cpp`
- Modify: `CMakeLists.txt` (link nlohmann_json into the test target only)

**Interfaces:**
- Produces: `bool flight_recorder::exportChromeTrace(const std::string& path)` — writes a Chrome Trace Event JSON array of complete events (`ph:"X"`), `ts`/`dur` in µs, `pid`=1, `tid`=osTid, `name`=site name, `args:{corr,arg}`. Returns false on file-open failure. Empty rings → valid empty `[]` array.
- Consumes: `Recorder` (Task 5), `cyclesToNs` (Task 2).

- [ ] **Step 1: Write the failing test `tests/ChromeTraceTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

namespace {
void pushSynthetic(Site s, uint32_t corr, uint64_t start, uint64_t end) {
    threadRecorder()->ring.push(
        FrRecord{start, end, corr, static_cast<uint16_t>(s), 0});
}
nlohmann::json loadJson(const std::string& path) {
    std::ifstream in(path);
    nlohmann::json j;
    in >> j;
    return j;
}
} // namespace

TEST(ChromeTrace, EmptyProducesValidEmptyArray) {
    Recorder::instance().reset();
    const std::string path = "/tmp/flight_recorder_empty_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));
    auto j = loadJson(path);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 0u);
    std::remove(path.c_str());
}

TEST(ChromeTrace, EmitsCompleteEventsWithRequiredFields) {
    Recorder::instance().reset();
    calibrate();
    pushSynthetic(Site::WorkerAppend, 42, 1000, 5000);
    const std::string path = "/tmp/flight_recorder_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));

    auto j = loadJson(path);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 1u);
    const auto& e = j[0];
    EXPECT_EQ(e.at("ph"), "X");
    EXPECT_EQ(e.at("name"), "WorkerAppend");
    EXPECT_TRUE(e.contains("ts"));
    EXPECT_TRUE(e.contains("dur"));
    EXPECT_EQ(e.at("pid"), 1);
    EXPECT_TRUE(e.contains("tid"));
    EXPECT_EQ(e.at("args").at("corr"), 42);
    std::remove(path.c_str());
}
```

- [ ] **Step 2: Link nlohmann_json into the test target**

In `CMakeLists.txt` under `if(BUILD_TESTING)`:
```cmake
    find_package(nlohmann_json REQUIRED)
    target_link_libraries(flight_recorder_tests PRIVATE nlohmann_json::nlohmann_json)
```
Add `nlohmann/json.hpp` include only in test files — **never** in `flight_recorder_core`. Add `tests/ChromeTraceTest.cpp` to `flight_recorder_tests`.

- [ ] **Step 3: Run — verify fail**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R ChromeTrace --output-on-failure
```
Expected: FAIL (stub returns false / no fields).

- [ ] **Step 4: Implement `exportChromeTrace` in `src/Report.cpp`**

Write JSON by hand (no JSON dep in core). Replace the stub:
```cpp
#include <fstream>

// (add near the other includes in Report.cpp)

namespace flight_recorder {

bool exportChromeTrace(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    // Anchor timeline at the earliest startTsc across all rings.
    std::vector<FrRecord> buf;
    auto threads = Recorder::instance().snapshotThreads();
    uint64_t anchor = UINT64_MAX;
    for (auto* tr : threads) {
        tr->ring.snapshot(buf);
        for (const auto& r : buf) anchor = std::min(anchor, r.startTsc);
    }
    if (anchor == UINT64_MAX) anchor = 0;

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
                << "\",\"cat\":\"" << /*category*/ static_cast<int>(categoryOf(static_cast<Site>(r.site)))
                << "\",\"pid\":1,\"tid\":" << tr->osTid
                << ",\"ts\":" << std::fixed << std::setprecision(3) << tsUs
                << ",\"dur\":" << durUs
                << ",\"args\":{\"corr\":" << r.corr << ",\"arg\":" << r.arg << "}}";
        }
    }
    out << "]";
    return static_cast<bool>(out);
}

} // namespace flight_recorder
```
Remove the earlier temporary `exportChromeTrace` stub.

- [ ] **Step 5: Run — verify pass**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R ChromeTrace --output-on-failure
```
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: P4c Chrome-trace JSON export"
```

---

## Task 11: writeTape / readTape (P4d)

**Files:**
- Modify: `include/flight_recorder/Report.h` (define `Tape`), `src/Report.cpp` (replace tape stubs)
- Test: `tests/TapeTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct flight_recorder::TapeThread { uint64_t osTid; std::string threadName; std::vector<FrRecord> records; }`.
  - `struct flight_recorder::Tape { uint32_t version; double tscPerNs; std::vector<std::string> siteNames; std::vector<TapeThread> threads; }`.
  - `bool flight_recorder::writeTape(const std::string& path)` — dumps all rings + header (magic `"RUNEFR01"`, version, tscPerNs, site table, thread names). Returns false on open failure.
  - `bool flight_recorder::readTape(const std::string& path, Tape& out)` — parses; returns false on bad magic/version/truncation.
- Consumes: `Recorder` (Task 5), `tscPerNs` (Task 2), `Site`/`nameOf` (Task 3).

- [ ] **Step 1: Define `Tape` in `include/flight_recorder/Report.h`**

Replace the forward declaration `struct Tape;` with:
```cpp
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
```
Add `#include "flight_recorder/FrRecord.h"` to `Report.h`.

- [ ] **Step 2: Write the failing test `tests/TapeTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <cstdio>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

TEST(Tape, RoundTripPreservesRecords) {
    Recorder::instance().reset();
    calibrate();
    threadRecorder()->ring.push(FrRecord{100, 250, 7, static_cast<uint16_t>(Site::WorkerAppend), 3});
    threadRecorder()->ring.push(FrRecord{300, 400, 8, static_cast<uint16_t>(Site::EgressSend), 0});

    const std::string path = "/tmp/flight_recorder_test.tape";
    ASSERT_TRUE(writeTape(path));

    Tape t;
    ASSERT_TRUE(readTape(path, t));
    EXPECT_EQ(t.version, 1u);
    EXPECT_GT(t.tscPerNs, 0.0);
    EXPECT_EQ(t.siteNames.size(), static_cast<size_t>(Site::Count));
    ASSERT_EQ(t.threads.size(), 1u);
    ASSERT_EQ(t.threads[0].records.size(), 2u);
    EXPECT_EQ(t.threads[0].records[0].corr, 7u);
    EXPECT_EQ(t.threads[0].records[0].site, static_cast<uint16_t>(Site::WorkerAppend));
    EXPECT_EQ(t.threads[0].records[0].arg, 3u);
    EXPECT_EQ(t.threads[0].records[1].corr, 8u);
    std::remove(path.c_str());
}

TEST(Tape, RejectsBadMagic) {
    const std::string path = "/tmp/flight_recorder_bad.tape";
    { std::ofstream out(path); out << "NOTATAPE"; }
    Tape t;
    EXPECT_FALSE(readTape(path, t));
    std::remove(path.c_str());
}

TEST(Tape, EmptyRingsWriteReadableTape) {
    Recorder::instance().reset();
    const std::string path = "/tmp/flight_recorder_empty.tape";
    ASSERT_TRUE(writeTape(path));
    Tape t;
    ASSERT_TRUE(readTape(path, t));
    EXPECT_EQ(t.threads.size(), 0u);
    std::remove(path.c_str());
}
```
(Add `#include <fstream>` to the test.)

- [ ] **Step 3: Run — verify fail**

```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Tape --output-on-failure
```
Expected: FAIL (stubs return false).

- [ ] **Step 4: Implement `writeTape`/`readTape` in `src/Report.cpp`**

Binary format: magic `RUNEFR01` (8 bytes) | u32 version | f64 tscPerNs | u32 siteCount, then each site name as (u16 len + bytes) | u32 threadCount, then per thread (u64 osTid | u16 nameLen + bytes | u64 recordCount | recordCount × 24-byte FrRecord). Little-endian (documented assumption).

```cpp
#include <cstring>

namespace flight_recorder {
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
constexpr char kMagic[8] = {'R','U','N','E','F','R','0','1'};
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

} // namespace flight_recorder
```
Remove the earlier temporary tape stubs. Add `#include <fstream>` / `#include <cstring>` to `Report.cpp` if not already present.

- [ ] **Step 5: Run — verify pass**

Add `tests/TapeTest.cpp` to `flight_recorder_tests`.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Tape --output-on-failure
```
Expected: all 3 PASS.

- [ ] **Step 6: Full suite green + commit**

```bash
ctest --test-dir build/Debug --output-on-failure
git add -A && git commit -m "feat: P4d versioned tape write/read round-trip"
```

---

## Task 12: Overhead microbench + throughput test (P5 — GATE)

**Files:**
- Create: `bench/fr_overhead_bench.cpp`
- Test: `tests/ThroughputTest.cpp`
- Modify: `CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: full public API (`Event`, `mark`, `FLIGHT_RECORDER_SPAN`, `enable`, `setCategoryMask`).
- Produces: published ns/op numbers in `README.md`.

- [ ] **Step 1: Write `bench/fr_overhead_bench.cpp`**

```cpp
#include <benchmark/benchmark.h>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

// Enabled SPAN: begin+commit of a real measured event.
static void BM_SpanEnabled(benchmark::State& state) {
    Recorder::instance().reset();
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    for (auto _ : state) {
        Event ev(Site::WorkerAppend);
        ev.begin().corr(1);
        ev.commit();
    }
    disable();
}
BENCHMARK(BM_SpanEnabled)->Unit(benchmark::kNanosecond);

// Runtime-disabled (compiled-in): should be ~1 ns (one predicted branch path).
static void BM_SpanDisabled(benchmark::State& state) {
    disable();
    for (auto _ : state) {
        Event ev(Site::WorkerAppend);
        ev.begin().corr(1);
        ev.commit();
        benchmark::DoNotOptimize(ev);
    }
}
BENCHMARK(BM_SpanDisabled)->Unit(benchmark::kNanosecond);

// Enabled MARK: single-timestamp instant.
static void BM_MarkEnabled(benchmark::State& state) {
    Recorder::instance().reset();
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    for (auto _ : state) {
        FLIGHT_RECORDER_MARK(Site::IngressRecv, 1);
    }
    disable();
}
BENCHMARK(BM_MarkEnabled)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
```

- [ ] **Step 2: Build and run the bench**

```bash
conan install . -of build -s build_type=Release -s compiler.cppstd=23 --build=missing
cmake --preset conan-release -DBUILD_BENCHMARKS=ON
cmake --build build/Release -j --target flight_recorder_bench
./build/Release/flight_recorder_bench --benchmark_min_time=1s
```
Expected: prints ns/op for the three cases.

- [ ] **Step 3: Evaluate the gate**

- `BM_SpanEnabled` ≤ **10 ns** on the reference VM.
- `BM_SpanDisabled` ≤ **1 ns**.
If `BM_SpanEnabled` exceeds 10 ns, iterate before proceeding: verify Release `-O2/-O3`, confirm `rdtscp` (not fallback) on the x86 VM, check the ring `push` isn't cache-missing (record fits in one line), and consider the 16-byte record variant (spec open question §9) only if warranted. Re-run until within target; document any tradeoff taken in `FrRecord.h`.

- [ ] **Step 4: Write the throughput / isolation test `tests/ThroughputTest.cpp`**

```cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

TEST(Throughput, MultiThreadNoInterferenceEachRingHoldsItsOwn) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);

    constexpr int kThreads = 4;
    constexpr uint64_t kPerThread = 100000; // < default capacity 65536? no -> overwrite
    // Use a small deterministic count under capacity to assert exact retention.
    constexpr uint64_t kUnderCap = 1000;

    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            for (uint64_t n = 0; n < kUnderCap; ++n) {
                Event ev(Site::WorkerAppend);
                ev.begin().corr(static_cast<uint32_t>(n));
                ev.commit();
            }
        });
    }
    for (auto& t : ts) t.join();

    auto threads = Recorder::instance().snapshotThreads();
    EXPECT_EQ(threads.size(), static_cast<size_t>(kThreads));
    std::vector<FrRecord> buf;
    for (auto* tr : threads) {
        EXPECT_EQ(tr->ring.snapshot(buf), kUnderCap); // each thread kept exactly its own
    }
    disable();
    (void)kPerThread;
}
```

- [ ] **Step 5: Run throughput test — verify pass**

Add `tests/ThroughputTest.cpp` to `flight_recorder_tests`.
```bash
cmake --build build/Debug -j && ctest --test-dir build/Debug -R Throughput --output-on-failure
```
Expected: PASS (each of 4 rings holds exactly 1000 records — proves isolation, no cross-thread interference).

- [ ] **Step 6: Publish numbers in `README.md`**

Fill the `## Overhead` section with the measured ns/op from Step 2 and the VM description, e.g.:
```markdown
## Overhead

Measured with Google Benchmark on the reference VM (x86_64, invariant TSC), Release `-O3`:

| case | ns/op |
|------|-------|
| enabled SPAN (begin+commit) | <measured> |
| enabled MARK | <measured> |
| runtime-disabled SPAN | <measured> |
| compiled-out (`FLIGHT_RECORDER=0`) | 0 |

Reproduce: `cmake --preset conan-release -DBUILD_BENCHMARKS=ON && ./build/Release/flight_recorder_bench`.
```

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat: P5 overhead microbench + throughput test; publish numbers"
```

---

## Task 13: Conan package_info finalize + sample consumer (P6)

**Files:**
- Create: `sample/CMakeLists.txt`, `sample/main.cpp`
- Modify: `README.md` (consumer instructions)
- Verify: `conanfile.py` / `conandata.yml` (from Task 1)

**Interfaces:**
- Consumes: the installed `flight_recorder::flight_recorder` CMake target + `FLIGHT_RECORDER` define.

- [ ] **Step 1: Write `sample/main.cpp`**

```cpp
#include <iostream>
#include "flight_recorder/FlightRecorder.h"

int main() {
    using namespace flight_recorder;
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);

    for (int i = 0; i < 5; ++i) {
        FLIGHT_RECORDER_SPAN(Site::WorkerAppend, static_cast<uint32_t>(i + 1));
    }
    FLIGHT_RECORDER_MARK(Site::IngressRecv, 42);

    dumpHistograms(std::cout);
    if (!exportChromeTrace("/tmp/flight_recorder_sample_trace.json")) {
        std::cerr << "trace export failed\n";
        return 1;
    }
    std::cout << "wrote /tmp/flight_recorder_sample_trace.json\n";
    return 0;
}
```

- [ ] **Step 2: Write `sample/CMakeLists.txt` (standalone consumer)**

```cmake
cmake_minimum_required(VERSION 3.31)
project(flight_recorder_sample CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(flight_recorder CONFIG REQUIRED)

add_executable(flight_recorder_sample main.cpp)
target_link_libraries(flight_recorder_sample PRIVATE flight_recorder::flight_recorder)
```

- [ ] **Step 3: Smoke-build the sample against the local build tree**

Since the agent must not `conan create`, verify consumption via CMake against the local install (or `add_subdirectory`). Simplest local smoke: configure the sample pointing at the source tree's install.

```bash
cmake --build build/Release -j
cmake --install build/Release --prefix /tmp/flight_recorder_install
cmake -S sample -B build/sample -DCMAKE_PREFIX_PATH=/tmp/flight_recorder_install
cmake --build build/sample -j
./build/sample/flight_recorder_sample
```
Expected: `flight_recorder_sample` builds, prints a histogram table, and writes the trace JSON.

- [ ] **Step 4: Add consumer instructions to `README.md`**

```markdown
## Consuming flight-recorder

Add to your `conandata.yml`:
```yaml
requirements:
  - "flight-recorder/0.1.0-snapshot"
```
Then in CMake:
```cmake
find_package(flight_recorder CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE flight_recorder::flight_recorder)
```
Toggle recording off at build time with `-o flight-recorder/*:flight_recorder=False` (compiles to nothing).
```

- [ ] **Step 5: Final full-suite verification + commit**

```bash
ctest --test-dir build/Debug --output-on-failure
git add -A && git commit -m "feat: P6 Conan package_info + sample consumer smoke"
```

---

## Self-Review Notes

**Spec coverage:** §2.1 Sites → Task 3. §2.2 FrRecord → Task 3. §2.3 Event/begin/commit + macros + compile-out → Task 7. §2.4 TSC + calibration + fallback → Task 2. §2.5 per-thread ring, both modes, published index → Task 4; registry → Task 5. §2.6 enable/mask → Task 6. §3.1 dumpHistograms → Task 9; §3.2 exportChromeTrace → Task 10; §3.3 stitchByCorr → Task 9; §3.4 writeTape/readTape → Task 11; §3.5 trigger capture → explicitly v2+ (out of scope). §4 overhead bench + throughput + gate → Task 12. §5 build/Conan → Tasks 1 & 13. §6 integration surface → documented in README (actual Rune wiring is a follow-up session, per spec). §8 acceptance criteria 1–7 → covered across Tasks 1,7,12,9–11,13.

**Review Focus coverage:** no-invariant-TSC/fallback → Task 2 Step 3 `FallbackNeverCrashes`. Non-power-of-two capacity → Task 4 `RejectsNonPowerOfTwoCapacity`. `corr==0` → Task 9 `CorrZeroReturnsEmpty`. `endTsc==startTsc`/no-underflow → Task 3 `DurationNeverUnderflows` + Task 7 `MarkRecords...` + Task 8 `AcceptsZeroDurationSample`. Empty rings → Task 9 `EmptyRingsProduceNoStats` + Task 10 `EmptyProducesValidEmptyArray` + Task 11 `EmptyRingsWriteReadableTape`.

**Type consistency:** `threadRecorder()` returns `ThreadRecorder*`; `Ring::snapshot(std::vector<FrRecord>&)` returns count; `enabled(Site)`, `categoryOf(Site)`, `nameOf(Site)` consistent across tasks; `SiteStats`/`WaterfallEntry`/`Tape` defined once in `Report.h` and used unchanged in tests.

**Open questions (spec §9) resolutions baked into the plan:** record size = 24 B (Task 3, revisit only if Task 12 gate fails); histogram = log-linear 8-mantissa-bit (Task 8, <0.8% error); ring default = 65536 records (Task 5); ARM path = `cntvct_el0` with `clock_gettime` fallback (Task 2).
