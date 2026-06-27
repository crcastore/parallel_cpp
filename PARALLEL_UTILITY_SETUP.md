# Parallel Python Worker Utility Setup

## Purpose

This project is a small C++ utility for running Python workers in parallel on row-chunked data.

The current implementation focuses on:

- Simple process-based parallelism.
- A stable binary IPC protocol between C++ and Python.
- Deterministic test behavior (seeded inputs and seeded simulator in the Python worker).

## High-Level Architecture

1. C++ builds a row-major dataset.
2. C++ splits rows into chunks.
3. Each chunk is sent to a dedicated Python worker subprocess.
4. Python processes a chunk and returns row-major results.
5. C++ reassembles all chunk outputs into one contiguous result buffer.

Core files:

- `src/main.cpp`: CLI parsing, dataset generation, benchmark driver.
- `src/worker_process.h`: `Worker` function object and generic `parallel_map` utility.
- `src/worker_process.cpp`: subprocess + pipe IPC implementation.
- `src/protocol.h`: message header and message type enums.
- `src/dataset.h`: row-major `Dataset` + lightweight `DataView`.
- `python/worker.py`: Python worker entrypoint.

## IPC Contract

Messages use `MessageHeader` (`src/protocol.h`) followed by optional payload data.

Message types:

- `Task`: C++ -> Python, includes rows, cols, and input doubles.
- `Quit`: C++ -> Python, tells worker to exit.
- `Result`: Python -> C++, includes output shape and output doubles.
- `Error`: Python -> C++, includes UTF-8 error text.

The current wire format is little-endian and matches `<IHHQQQ>` in Python.

## Parallel API Surface

`Worker` is a callable object:

```cpp
std::vector<double> operator()(std::size_t taskId,
                               const DataView& input,
                               std::size_t& resultCols) const;
```

`parallel_map` is the reusable utility:

```cpp
template <typename WorkerFn>
std::vector<double> parallel_map(const WorkerFn& worker,
                                 const DataView& fullData,
                                 std::size_t numWorkers);
```

This keeps call sites functional in style: pass a callable + data view + worker count, receive one combined result vector.

## Build and Test

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

Activate Python environment:

```bash
source ve/bin/activate
```

Run tests:

```bash
./build/unit_tests
./build/worker_integration_test
./build/worker_tsan_test
```

Run benchmark:

```bash
./build/parallel_python --rows 3000 --cols 6
```

Run benchmark with Linux worker CPU pinning enabled:

```bash
./build/parallel_python --rows 3000 --cols 6 \
    --pin-workers-single-cpu --cpu-start 0 --cpu-stride 1
```

Relevant CLI options in the current build:

- `--rows` and `--cols` are required.
- `--python` overrides the Python executable.
- `--worker-script` overrides the worker script path.
- `--pin-workers-single-cpu`, `--cpu-start`, and `--cpu-stride` control Linux-only affinity behavior.

## Notes on Determinism and Performance

- Input generation in C++ is seeded.
- Python worker is configured to reduce thread oversubscription.
- Quantum simulation uses explicit seeding and fixed shots for reproducible tests.

CPU pinning and NUMA notes:

- Pinning each worker to one CPU can reduce scheduler migrations and improve cache locality.
- This can help on NUMA systems when each worker is mostly CPU-bound and memory reuse is local.
- It is not universally faster: strict pinning can hurt if load is imbalanced or if worker count exceeds available physical cores.
- Current implementation applies Linux-only affinity (`sched_setaffinity`) in the child before launching Python.
- On non-Linux systems, pinning flags are accepted but affinity is a no-op.

## Direction for Reusable Utility Evolution

To evolve this into a general utility for "kick off N Python processes on chunks":

1. Introduce a pluggable worker command (script/module + args).
2. Make chunking strategy configurable (fixed rows, dynamic queue, strided).
3. Add retry policy around worker failures.
4. Add result serializer abstraction beyond `double` arrays.
5. Add optional worker pool warmup and lifecycle metrics.
