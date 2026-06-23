# C++ Parallel Python Workers (CMake)

This project keeps a dataset in a **row-major contiguous** in-memory array in C++, starts a configurable number of Python worker processes, sends chunks to workers in parallel, and collects results.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/parallel_python --rows 200000 --cols 64 --workers 8 --chunk-rows 2000
```

Options:

- `--rows <n>`: dataset rows
- `--cols <n>`: columns per row
- `--workers <n>`: number of Python processes
- `--chunk-rows <n>`: rows per chunk sent per request
- `--python <path>`: Python executable (default `python3`)
- `--worker-script <path>`: worker script path (default points to `python/worker.py`)

## Notes

- Dataset storage is implemented in `src/dataset.h` as a single `std::vector<double>` in row-major order.
- Each C++ thread owns one Python process and repeatedly sends chunk tasks until all chunks are processed.
- IPC now uses a versioned binary protocol over stdin/stdout:
	- Header format (little-endian): `<IHHQQQ>`
	- Fields: `magic`, `version`, `message_type`, `task_id`, `rows`, `cols_or_aux`
	- `TASK` messages carry raw `float64` row-major payload (`rows * cols` doubles)
	- `RESULT` messages carry `rows` `float64` values
