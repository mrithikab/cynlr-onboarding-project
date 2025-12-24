# cynlr-onboarding-project
Onboarding project of Cynlr Software Engineering internship 2026. 
The project replicates the functionality of a line scan camera, implementing the Data generation block, which generates two elements per time step, and the Filter threshold block which applies a filter and outputs 0/1 based on the threshold. 
The blocks internally will process the elements in parallel, but are connected to each other sequentially.

The project will be built using C++ 17 in Visual Studio 2022.

## Project documentation

### Overview
This repository implements a small high-throughput, low-latency producer-consumer pipeline in C++14. It produces pairs of 8-bit pixel samples, applies a 9-point linear filter (non-causal) in a consumer (`FilterBlock`), and records optional metrics. The design focuses on predictable steady-state memory use, low per-output compute time, and configurable diagnostics for short profiling runs.

Key goals:
- Per-output compute latency well below configured budget T (ns).
- Two outputs per pair emitted back-to-back (inter-output delta target: 100 ns).
- Enforce steady-state memory bound: in-flight queued pixels ? m/2.
- Safe shutdown with no deadlocks and deterministic CSV streaming for tests.

### Major components
- `ThreadSafeQueue<T>` (include/ThreadSafeQueue.h)
  - Bounded single-producer single-consumer circular queue.
  - Spin-then-condition-variable fallback for long waits.
  - `shutdown()` API to unblock waiting producers/consumers safely.
  - `try_push`, `push`, `pop(out)` interfaces.

- `DataGenerator` (include/DataGenerator.h, src/DataGenerator.cpp)
  - Produces `DataPair` (two uint8_t pixels + gen timestamp + seq).
  - Modes: RANDOM (uniform [0,255]) and CSV (streamed reader).
  - Enforces inter-pair spacing `T_ns` using `hybrid_sleep_ns`.
  - CSV streaming reads tokens on demand (no full-file buffering), clamps values to 0..255, drops a final odd token.
  - On CSV EOF the generator calls `queue->shutdown()` and stops (no Ctrl+C required).
  - Uses `try_push` with a short yield/sleep throttle to avoid indefinite blocking but falls back to `push()` for progress.

- `FilterBlock` (include/FilterBlock.h, src/FilterBlock.cpp)
  - Consumer thread that reads `DataPair`s and applies a fixed 9-coefficient filter to a 9-sample circular buffer.
  - Zero pre-fill border policy by default and post-pad with eight zeros on shutdown to flush final outputs.
  - Records statistics (queue latency, per-output compute times) and can emit per-pair metrics through `MetricsCollector`.
  - Includes consumer-ready handshake (`isReady()`) so main() can start producer after consumer is ready.

- `CsvStreamer` (include/stream/CsvStreamer.h, src/stream/CsvStreamer.cpp)
  - Small helper focused on CSV tokenization and clamping. Used in tests or can replace DataGenerator streaming logic.

- `MetricsCollector` abstraction (include/metrics/MetricsCollector.h)
  - Pluggable collectors: `FileMetricsCollector` (writes `pair_metrics.csv`) and `NoopMetricsCollector`.
  - Inject `MetricsCollector*` into `FilterBlock` constructor to enable/disable logging without changing hot path.

- Tests: `root/tests/TestRunner.cpp`
  - Lightweight test harness validating `CsvStreamer` parsing and `DataGenerator` CSV streaming + shutdown semantics.

### Queue capacity and memory policy

#### Default queue capacity
- For now, the pipeline uses a conservative, fixed queue capacity of 1024 usable pairs. This default is chosen to balance burst absorption and low steady memory usage while you iterate on a byte-accurate memory budgeting implementation.

#### Why 1024
- It's large enough to absorb short producer bursts and scheduling jitter yet small in absolute bytes on modern systems. It keeps the queue footprint modest while preventing immediate producer blocking in common test scenarios.

#### Memory budgeting and the `m` parameter
- The `m` value in the program represents the number of columns detected for CSV mode (or supplied by the user). It is not currently used to enforce a strict byte-accurate memory cap for the whole process.
- A future improvement will convert `m` into an explicit byte budget, subtract fixed reserved buffers (FilterBlock circular buffer, metric buffers if enabled, safety margin) and compute queue capacity from `sizeof(DataPair)` to guarantee steady-state memory ? m. That implementation is planned but not applied in the current commit.

#### Operational guidance
- Diagnostic runs: enable file metrics for short runs only — file I/O distorts timing. Use Release build with metrics enabled for brief traces (1–5s).
- Measurement runs: disable metrics, set `verbose = false`, build Release and run the executable outside the debugger for accurate timings.
- If you need to reduce memory usage now, lower the queue capacity in `main.cpp` (pairsCapacity constant). If you need to tolerate more bursts, increase it.

#### Next steps (recommended)
1. Implement byte-accurate enforcement: treat `m` as bytes (or convert pixels?bytes), subtract reserved bytes, compute `desired_pairs = floor(bytes_for_queue / sizeof(DataPair))`, request `desired_pairs + 1` for the queue and assert estimated total memory ? m.
2. Add runtime monitoring that samples `queue.size()` and process RSS and emits warnings when usage approaches the budget.
3. Expose queue capacity and memory-budget mode via CLI flags for easier experiments.

This section documents the current conservative policy (1024 pairs) and the plan to implement strict memory enforcement later.

### Build & run
1. Configuration: use Release for measurements. In Visual Studio set __Configuration__ = __Release__ and __Platform__ = __x64__ if applicable.
2. Build: __Build > Rebuild Solution__.
3. Diagnostic run (short): enable metrics by creating a `FileMetricsCollector` and passing it to `FilterBlock` (or set `verbose = true` briefly). Run without debugger: __Debug > Start Without Debugging__ or run exe directly.
4. Measurement run (accurate): disable metrics and verbose, run the Release exe without debugger for minimal perturbation.

Example (direct exec):
cd <solution_output_folder>
# Diagnostic (short)
CynlrOnboarding.exe --mode csv --metrics pair_metrics.csv
# Measurement (no metrics)
CynlrOnboarding.exe --mode random

(If CLI flags are not implemented, use the main.cpp prompts to select mode and CSV path.)

### Where to place `test.csv`
The repository includes a sample `test.csv` used by the CSV input mode. Ensure your run can find this file:

- Running the EXE directly (Explorer or command line): place `test.csv` in the same folder as the executable.
- Running from Visual Studio: either set Project Properties -> __Debugging > Working Directory__ to `$(SolutionDir)` (so the repo root is used), or copy `test.csv` into the build output directory for your configuration/platform (e.g., `x64\Release`).
- To make the build copy the file automatically, add the following snippet to your `.vcxproj` inside an `<ItemGroup>` (adjust the `Include` path if your `test.csv` is not in the project root):

<None Include="test.csv">
  <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
</None>

- Quick manual copy (PowerShell):

Copy-Item -Path "$(ProjectDir)\test.csv" -Destination "$(ProjectDir)\x64\Release\" -Force

### Tests
- `root/tests/TestRunner.cpp` provides a tiny, dependency-free test runner. Build and run it as a normal executable in Debug or Release. Tests create temporary CSV files and exercise parsing and streaming.

### Configuration knobs and tuning
- `m` (columns) — used to compute queue capacity for steady-state memory: pairsCapacity = max(1, floor(m/4)). This enforces in-flight pixels ? m/2.
- `T_ns` — inter-pair spacing enforced by `DataGenerator`.
- `verbose` — per-translation-unit compile-time flag that enables diagnostic logging (kept false for measurements).
- Queue tuning: spin/yield thresholds and sleep fallback in `ThreadSafeQueue` and throttle constants in `DataGenerator` (`TRY_YIELD_ATTEMPTS`, `TRY_SLEEP_ATTEMPTS`). Tune for target hardware.
- Thread priority/affinity — guarded by `#ifdef _WIN32`; enable only for controlled experiments.

### Shutdown sequence (safe, recommended)
1. Stop producer: `gen.stop()` (ensures producer thread exits and will not push more items).
2. Shutdown queue: `queue.shutdown()` (unblocks consumer if waiting).
3. Stop consumer: `filter.stop()` (joins consumer thread and prints stats).

For CSV mode DataGenerator already calls `queue->shutdown()` after EOF so `main()` waits for generator completion and then stops the filter.

### Memory, capacity and steady-state guarantee
- Each `DataPair` holds two pixels (2 bytes). To ensure steady-state queued pixels ? m/2, set queue usable capacity (pairs) ? floor(m/4). The code computes `pairsCapacity = max(1, m/4)` in `main.cpp` when constructing the queue.

### Instrumentation and metrics
- Use `MetricsCollector` (file-based) for short diagnostic traces only. File I/O distorts timing — keep diagnostic runs brief (few seconds).
- For production measurement disable metrics, run Release without debugger, and optionally pin threads / raise priority for controlled experiments.

### File map (important files)
- include/ThreadSafeQueue.h — queue implementation
- include/DataGenerator.h, src/DataGenerator.cpp — generator
- include/FilterBlock.h, src/FilterBlock.cpp — consumer filter
- include/stream/CsvStreamer.h, src/stream/CsvStreamer.cpp — CSV helper
- include/metrics/MetricsCollector.h, src/metrics/* — metrics implementations
- root/tests/TestRunner.cpp — small unit tests
- root/src/main.cpp — example wiring and CLI prompts






