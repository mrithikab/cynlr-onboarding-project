**Cynlr Onboarding — Design Overview**

- **Project Root:** root/
- **Primary Goal:** a small SPSC pipeline that simulates a line-scan data source (pairs of uint8 pixels) and applies a 9-tap non-causal filter + threshold, while meeting tight timing and memory constraints and providing optional metrics.

**Architecture Summary**

- Pattern: modular, producer–consumer pipeline (single-producer single-consumer, SPSC).
- Components (files referenced):
  - **Producer / Data generation:** `DataGenerator` (`root/src/DataGenerator.cpp`) — produces `DataPair` items from RNG or CSV via `CsvStreamer`.
  - **Transport:** `ThreadSafeQueue<DataPair>` (`root/include/ThreadSafeQueue.h`) — bounded, power-of-two circular buffer optimized for SPSC with spin-then-block behaviour and explicit `shutdown()`.
  - **Consumer / Processing:** `FilterBlock` (`root/src/FilterBlock.cpp`, `root/include/FilterBlock.h`) — maintains a contiguous 9-sample circular buffer, applies the 9-tap `KERNEL`, thresholds against `TV`, and publishes metrics.
  - **CSV reader:** `CsvStreamer` (`root/include/stream/CsvStreamer.h`, `root/src/stream/CsvStreamer.cpp`) — simple token-based parser returning pairs of clamped uint8 values.
  - **Metrics:** `MetricsCollector` interface (`root/include/metrics/MetricsCollector.h`) with implementations: `FileMetricsCollector` (writes CSV) and `NoopMetricsCollector`.
  - **Entrypoint:** `main.cpp` (`root/src/main.cpp`) — configuration, startup/shutdown ordering, and mode selection.

**Data model**

- `DataPair` (defined in project headers) carries `a`, `b` (two uint8 samples), a `seq` identifier and `gen_ts_ns` timestamp (set by generator when available).
- Sentinel semantics: the project uses both explicit queue shutdown and sometimes a pushed sentinel `seq == UINT64_MAX` to indicate end-of-stream; `ThreadSafeQueue::shutdown()` is the canonical shutdown mechanism.

**Data flow & timing**

- Producer emits two samples every logical interval `T` (ns). Generator sets `gen_ts_ns` when samples are produced (0 indicates no timestamp).
- Consumer (`FilterBlock`) pops pairs, processes each sample sequentially through `process_sample`, and when 9 samples are available computes filtered value and timestamps the output with `now_ns()`.
- Throughput expectations: time delta between successive output pixels should be < 100 ns for the evaluation runs; `T` is an external constraint (>=500 ns) that producers and consumer code must respect.

**Threading & concurrency**

- Exactly one producer thread (DataGenerator) and one consumer thread (`FilterBlock`). `ThreadSafeQueue` is SPSC and not safe for multiple producers/consumers.
- Startup ordering in `main.cpp`: start consumer first, wait for `filter.isReady()`, then start producer. This avoids lost wakeups and ensures consumer readiness.
- Backpressure: `ThreadSafeQueue` capacity computed from `m` (e.g., `pairsCapacity = max(1, m/4)`) keeps steady-state memory O(m). Producer blocks when queue is full (spin then wait) which exerts backpressure and prevents memory growth.

**Memory & steady-state bound**

- Memory usage is bounded by queue capacity ∝ `m` plus small per-component buffers (9-sample circular buffer in `FilterBlock`). The design enforces steady-state memory bound by sizing the queue relative to `m`.

**Filtering & windowing**

- Non-causal 9-tap filter with given `KERNEL[]` (hard-coded constants in `FilterBlock.cpp`). For each sample we need the 4 previous and 4 next samples; the implementation achieves this by buffering samples and producing outputs once the buffer contains 9 entries (zero pre-fill and post-pad on shutdown to flush outputs).

**CSV parsing**

- `CsvStreamer` reads tokens separated by commas and returns contiguous pairs.
- Parsing behavior: trims whitespace, treats empty tokens as 0, clamps numeric values into [0,255] ("clamped" meaning out-of-range values are forced into the range), and on parse error logs and closes the stream.
- `CsvStreamer::probeColumns(path)` is provided to detect number of columns in CSV non-destructively; it is recommended to keep column-probing logic inside `CsvStreamer` rather than `DataGenerator`.

**Metrics collection**

- `FilterBlock` accepts an optional `MetricsCollector*` (can be `nullptr`). When present it calls `recordPair(...)` per dequeued pair with generation timestamp, pop timestamp, processing start, per-output timestamps, queue-latency, per-output processing times, and inter-output delta.
- Two implementations shipped:
  - `NoopMetricsCollector`: does nothing.
  - `FileMetricsCollector`: thread-safe (mutex-protected) CSV writer that flushes on `stop()`.
- Note: enabling file-based metrics distorts timing; use for short diagnostic runs only.

**Shutdown and sentinel handling**

- Canonical shutdown uses `queue->shutdown()` to unblock `pop()` and cause the consumer to finish post-padding zeros and exit.
- The code also sometimes uses a sentinel `DataPair` with `seq == UINT64_MAX`; since `FilterBlock` currently does not special-case the sentinel, pushing such a sentinel will be processed as data — prefer using `shutdown()` or add explicit sentinel handling inside `FilterBlock::run()` if sentinel semantics are desired.

**Build & Run**

- Build in Visual Studio 2022 (project and solution files present).
- Runtime inputs prompted in `main.cpp`:
  - `TV` (threshold, double)
  - `T` (process time, uint64 ns, >= 500)
  - Mode: Random (0) or CSV (1). For CSV, user may enter path (default `test.csv`).
  - `m` (number of columns) either probed via CSV or entered manually for Random mode.
- To enable metrics: create a `FileMetricsCollector` and pass it to `FilterBlock`'s constructor (or leave `metrics` as `nullptr` to disable).

**Testing & profiling guidance**

- Unit tests: verify `CsvStreamer` parsing (including empty tokens, whitespace, clamping, odd token counts), `ThreadSafeQueue` edge behaviors (full/empty, shutdown), filter correctness for known inputs (compare to reference convolution), and sentinel/shutdown transitions.
- Performance profiling: measure queue latency and per-output processing times (the system already collects these). Use Visual Studio CPU profiler and high-resolution timers. Run short diagnostic traces when `FileMetricsCollector` is enabled.
- Memory profiling: confirm memory usage scales with `m` and queue capacity; verify steady-state bound.

**Extension points & recommendations**

- Make `CsvStreamer::parseClampedUint8` use `std::from_chars` for exception-free parsing if performance is critical.
- If multi-consumer or multi-producer is required in future, replace `ThreadSafeQueue` or add a small adapter ensuring correct synchronization (current queue is SPSC-only).
- Add explicit sentinel handling in `FilterBlock::run()` if desired, or standardize on `shutdown()` only and remove main's sentinel push.
- Consider building a small configuration utility or command-line flags to choose metrics mode (`--metrics=file` vs `--metrics=none`) rather than interactive prompts.

**Mapping to submission requirements**

- Optimal architecture: SPSC producer-consumer with bounded queue — matches low-latency and tight memory constraint goals.
- Communication mechanism: bounded circular buffer with spin-then-block for fast short waits and condition variable fallback for longer waits (backpressure built-in).
- Scalability & modularity: components are injected and decoupled (metrics collector is pluggable, CSV handling encapsulated). Future blocks can be added as additional pipeline stages with minimal changes.
- Requirement translation: `T`, `m`, and `TV` are exposed to runtime; generator supports both CSV and random modes; filtering semantics implemented with 9-tap kernel and zero padding.

**Files of interest**

- `root/src/main.cpp`
- `root/include/ThreadSafeQueue.h`
- `root/include/FilterBlock.h`, `root/src/FilterBlock.cpp`
- `root/src/DataGenerator.cpp`
- `root/include/stream/CsvStreamer.h`, `root/src/stream/CsvStreamer.cpp`
- `root/include/metrics/MetricsCollector.h`, `root/src/metrics/*`

---

*Document created from repository code and specification in `TextFile1.txt`.*
