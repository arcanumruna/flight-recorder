# Configuration

Two kinds of switches: **compile-time** flags that remove code, and **run-time** environment variables read by
`configureFromEnv()`.

## Compile time

| Flag | Where | Effect |
|---|---|---|
| `FLIGHT_RECORDER=1` or `0` | Compiler definition on every translation unit; CMake option `FLIGHT_RECORDER` (default `ON`) sets it for consumers of `flight_recorder::flight_recorder` | `0` compiles every `Event`, span, mark and macro to nothing. A consumer that includes the headers without the define gets `1`. Conan: `-o flight-recorder/*:flight_recorder=False`. |
| `FLIGHT_RECORDER_NO_PRODUCE` | Compiler definition | Compiles the `*_PRODUCE` macros out. |
| `FLIGHT_RECORDER_NO_REPLICATION` | same | `*_REPLICATION` macros. |
| `FLIGHT_RECORDER_NO_READ` | same | `*_READ` macros. |
| `FLIGHT_RECORDER_NO_CONSENSUS` | same | `*_CONSENSUS` macros. |
| `FLIGHT_RECORDER_NO_STORAGE` | same | `*_STORAGE` macros. |
| `FLIGHT_RECORDER_NO_EGRESS` | same | `*_EGRESS` macros. |
| `FLIGHT_RECORDER_NO_RUNTIME` | same | `*_RUNTIME` macros. |
| `FLIGHT_RECORDER_AERON_INCLUDE_DIR` | CMake cache variable | Aeron client headers when the `aeron` package is not found (macOS arm64 fallback). |

Call sites must use the **per-category** macros (`FLIGHT_RECORDER_SPAN_PRODUCE`, ...) for the per-category flags to apply.
The category flag and the master switch both gate a call; with neither defined, every category is compiled in when
`FLIGHT_RECORDER` is `1`. In a project that wraps the switch in its own option (Rune's `ENABLE_FLIGHT_RECORDER`), the wrapper
just defines `FLIGHT_RECORDER` for you.

## Run time

A compiled-in recorder is **off** until enabled. `configureFromEnv()` (`Report.h`) reads these variables, sizes the rings,
applies the settings and returns `true` iff recording is on. Call it once from `main`, **before** the threads that record are
started: the ring size applies only to threads that register afterwards.

| Variable | Default | Meaning |
|---|---|---|
| `FLIGHT_RECORDER_ENABLE` | unset (off) | Set to anything other than `0` or empty to enable recording. |
| `FLIGHT_RECORDER_RING_BYTES` | `134217728` (128 MiB) | Payload bytes of each thread's ring. Floored to 256 and rounded up to a power of two. Read even when recording stays off. |
| `FLIGHT_RECORDER_CATEGORIES` | all | Comma-separated subset of `produce`, `replication`, `read`, `consensus`, `storage`, `egress`, `runtime`. Unknown names are ignored; an empty result means all. |
| `FLIGHT_RECORDER_SLOW_SPAN_NS` | `2000` | A `SlowSpan` shorter than this is not recorded. |
| `FLIGHT_RECORDER_THROTTLE_NS` | `50000` | Minimum gap between marks of one `Throttle` call site; hits in between are coalesced into the next mark's `value`. |
| `FLIGHT_RECORDER_DRAIN_MS` | `100` | Period of the background drainer. `0` starts none; reports then drain on demand. |
| `FLIGHT_RECORDER_TAPE_RECORDS_PER_THREAD` | `2097152` | Records kept per thread tape (the newest). Read once, by the registry. |
| `FLIGHT_RECORDER_AERON_MASK` | `0xC7F0F8` | Which C-API (Aeron driver) sites record; see [c-api.md](api/c-api.md#the-aeron-mask). Read once. |

Numbers are parsed as decimal (`strtoull`); a malformed value keeps the default. Programs can do the same without the
environment: `Recorder::instance().configure(bytes)`, `enable()`, `setCategoryMask(mask)`, `setSlowSpanTsc(ticks)`,
`setThrottleTsc(ticks)`, `startBackgroundDrain(ms)` ([C++ API](api/cpp.md)).

## Categories

Seven categories group the 146 sites ([sites.md](sites.md)) for both switches: `produce` (34 sites), `replication` (75),
`read` (3), `consensus` (3), `storage` (8), `egress` (3) and `runtime` (20). The runtime mask is a bit set,
`1u << static_cast<uint32_t>(Category)`: produce is bit 0, replication bit 1, read bit 2, consensus bit 3, storage bit 4, egress
bit 5, runtime bit 6.

## In the Rune broker

`rune_broker` calls `Recorder::configure()` itself and reads `FLIGHT_RECORDER_RING_BYTES`, `FLIGHT_RECORDER_ENABLE` and
`FLIGHT_RECORDER_OUT` (the directory for its reports, default the working directory), then writes a report on `SIGUSR1` and at
shutdown. See Rune's `docs/operations.md`.
