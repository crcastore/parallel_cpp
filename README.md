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
- `magic` (`uint32_t`): `0x50595043`, the ASCII bytes `CPYP`.
- `version` (`uint16_t`): protocol version, currently `1`.
- `type` (`uint16_t`): `1 = Task`, `2 = Quit`, `3 = Result`, `4 = Error`.
- `taskId` (`uint64_t`): opaque request identifier echoed back by the worker.
- `rows` (`uint64_t`): row count for the payload, or error-message byte count for `Error`.
- `cols` (`uint64_t`): input column count for `Task`, output column count for `Result`, and auxiliary length for `Error`.
- `Task` messages are followed by `rows * cols` raw `float64` values in row-major order.
- `Result` messages are followed by `rows * resultCols` raw `float64` values in row-major order.
- `Quit` carries no payload and is used during worker shutdown.
- `Error` carries `rows` bytes of UTF-8 text immediately after the header.
- The C++ side validates `magic`, `version`, and `taskId` before consuming a reply payload.
- The Python worker reads and writes the same layout using `struct.pack` / `struct.unpack` with `HEADER_FMT = "<IHHQQQ"`.

Example: when C++ sends a `Task`, it writes the 32-byte header first, then streams the input chunk as raw doubles. The Python worker reads that header, checks that the message is valid, uses `rows` and `cols` to know exactly how many doubles to consume, runs the simulation for each row, and sends back a `Result` header with the same `taskId` plus the output matrix. That lets the C++ side match the reply to the original request and place the returned values in the right slice of the final result buffer.

## Tests

- `./build/unit_tests`
- `./build/worker_integration_test`
- `./build/worker_tsan_test`
