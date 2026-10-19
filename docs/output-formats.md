# Output formats and tooling

A report is four files that share a suffix. `dumpAll(dir, suffix, role, node)` writes all four after draining; the three
formats can also be produced one at a time (`dumpHistograms`, `exportChromeTrace`, `writeTape`). `dumpAll` returns `false`
when recording is off, and the directory must already exist. The Rune broker names its files
`fr-hist-n<node>-p<pid>-<tag>.txt` and so on, with the tag `run<N>` for a `SIGUSR1` dump and `shutdown` at exit.

## `fr-hist<sfx>.txt`: histograms

One row per site that recorded, columns `site count p50(ns) p90(ns) p99(ns) p999(ns) max(ns) mean(ns)`:

```text
site                 count     p50(ns)     p90(ns)     p99(ns)    p999(ns)     max(ns)      mean(ns)
WorkerAppend          1001           0          42          42          42     7089703        7103.3
ProduceAckSend        1000           0           0           0           0           0           0.0
```

Durations are `endTsc - startTsc` converted with the calibrated rate and rounded to nanoseconds. Percentiles are the lower
bound of a log-linear bucket (about 0.78% relative error). A mark has duration 0. The table is built from the retained tape,
so a very long run reports its newest `FLIGHT_RECORDER_TAPE_RECORDS_PER_THREAD` records per thread.

## `fr-trace<sfx>.json`: Chrome trace

A JSON array of Chrome trace events. The first is a metadata event carrying the alignment sample:

```json
{"ph":"M","name":"flight_recorder_anchor","pid":1,"tid":0,"args":{"anchorTsc":188212011733584,"tscPerNs":1,"syncTsc":188212027043157,"syncMonoNs":2446731990616000,"syncRealNs":1792443256633438000,"pid":20980,"host":"<hostname>"}}
```

Every recorded span follows as a complete event (`ph` `X`):

| Field | Meaning |
|---|---|
| `name` | The site name. |
| `cat` | The category index as a string (`"0"` produce, `"1"` replication, `"2"` read, `"3"` consensus, `"4"` storage, `"5"` egress, `"6"` runtime). |
| `pid`, `tid` | `1` and the recording thread's OS thread id. |
| `ts`, `dur` | Microseconds: start relative to the earliest record in the run (the anchor), and duration. |
| `args` | `{"corr": <key>, "arg": <n>, "value": <x>}`. |

Open it in `chrome://tracing`, [Perfetto](https://ui.perfetto.dev) or [speedscope](https://www.speedscope.app). The JSON is written
by hand without string escaping, which is safe because the only strings are compile-time site names and the host name.

## `fr-tape<sfx>.bin`: raw tape

Every retained record, versioned, little-endian:

```text
magic "RUNEFR01" (8 bytes) | u32 version (1) | f64 tscPerNs
| u32 siteCount, then each site name as (u16 length + bytes)
| u32 threadCount, then per thread:
      u64 osTid | u16 nameLength + bytes | u64 recordCount | recordCount x FrRecord (32 bytes:
      u64 startTsc, u64 endTsc, u32 corr, u16 site, u16 arg, f64 value)
```

Read it with `flight_recorder::readTape(path, Tape&)`: `Tape{version, tscPerNs, siteNames, threads}`, each thread
`{osTid, threadName, records}`. It returns `false` on a bad magic or version. The site names in the tape let an offline
analyzer map site ids to names without the original headers. Convert ticks to nanoseconds with `1 / tscPerNs`.

## `fr-meta<sfx>.json`: run metadata

One JSON object written **last**, with the anchor of exactly what was dumped:

```json
{"role":"broker","node":"0","pid":20980,"host":"<hostname>","anchorTsc":188212011733584,"tscPerNs":1,"syncTsc":188212028453340,"syncMonoNs":2446731992026000,"syncRealNs":1792443256634848000,"records":2002,"dropped":0,"slowSpanNs":2000,"throttleNs":50000,"suffix":"-demo"}
```

| Field | Meaning |
|---|---|
| `role`, `node` | The strings you passed to `dumpAll`. |
| `pid`, `host` | The process and host. |
| `anchorTsc` | The earliest `startTsc` over every drained record (0 if none). |
| `tscPerNs` | The calibrated tick rate. |
| `syncTsc`, `syncMonoNs`, `syncRealNs` | One simultaneous sample of the tick counter, `CLOCK_MONOTONIC` and `CLOCK_REALTIME`. |
| `records` | Records in the dump. |
| `dropped` | Records lost to full rings. **Non-zero means the trace has holes.** |
| `slowSpanNs`, `throttleNs` | The thresholds in effect. |

## Aligning several processes

Processes on one host share the invariant tick counter, so their traces can be placed on one timeline: convert **differences**
of `anchorTsc` with **one** tick rate (`(anchorTsc_p - anchorTsc_ref) / tscPerNs_ref`). Never use absolute tick values or a
per-process rate: a 1e-6 rate error on a 1e15 absolute tick count is seconds of skew. For processes on different hosts, use the
`syncMonoNs` and `syncRealNs` pair to relate each process's ticks to wall-clock time.

## Reading one request

Give every hop of a request the same `corr` (see `foldCorr` and `partKey` in [the C++ API](api/cpp.md#correlation-keys)), then:

```cpp
flight_recorder::printWaterfall(corr, std::cout);   // every span with that key, by start, with offset and duration
auto spans = flight_recorder::stitchByCorr(corr);   // the same as data: site, startTsc, offsetNs, durNs, osTid
```

`corr == 0` means "none" and is never stitched. Across processes, join the per-process traces on the correlation key after
aligning their anchors.

## Tooling

- **In this repository:** the report functions above, and `readTape`/`Tape` for your own analysis.
- **Viewers:** Chrome trace viewers for the JSON.
- **In Rune:** `broker/bench/kafka-compare/frwaterfall.py 'fr-trace-*.json'` pairs marks by correlation key and prints replication
  commit and ack latency percentiles; it tolerates a trace truncated by `kill -9`.
