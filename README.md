# C++ Parallel Python Workers

This project keeps a dataset in a row-major contiguous array in C++, sends row chunks to Python worker processes in parallel, and collects the resulting expectation matrix.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/parallel_python --rows 200000 --cols 6
```

Command-line options:

- `--rows <n>`: dataset rows, required and must be greater than 0.
- `--cols <n>`: columns per row, required and must be greater than 0.
- `--python <path>`: Python executable used to launch the worker, default `python3`.
- `--worker-script <path>`: Python worker script, default `python/worker.py`.
- `--pin-workers-single-cpu`: Linux-only worker CPU pinning, disabled by default.
- `--cpu-start <n>`: first CPU index used when pinning, default `0`.
- `--cpu-stride <n>`: CPU step between workers when pinning, default `1`.

## Current Behavior

- Dataset storage lives in `src/dataset.h` as a single `std::vector<double>` in row-major order.
- Dataset values are generated in C++ with `std::mt19937_64`, fixed seed `42`, and a uniform distribution over `[-1.0, 1.0]`.
- The benchmark always sweeps worker counts `{1, 2, 4, 8, 16}` and clamps them to the dataset row count.
- Each worker chunk is processed by one Python subprocess launched from a `std::async` task.
- The worker protocol is binary and versioned over stdin/stdout.
- The Python worker uses Qiskit Aer, fixed seeds, and returns one expectation column per input column.

## Binary Protocol

- Header format is little-endian: `<IHHQQQ>`.
- Fields are `magic`, `version`, `type`, `taskId`, `rows`, and `cols`.
- `Task` messages carry raw `float64` row-major payloads with `rows * cols` doubles.
- `Result` messages carry `rows * resultCols` doubles in row-major order.
- `Quit` and `Error` messages use the same header, with `Error` carrying a UTF-8 payload after the header.

## Tests

- `./build/unit_tests`
- `./build/worker_integration_test`
- `./build/worker_tsan_test`
