# C++ Parallel Python Workers (CMake)

This project keeps a dataset in a **row-major contiguous** in-memory array in C++, starts a configurable number of Python worker processes, sends chunks to workers in parallel, and collects results.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/parallel_python --rows 200000 --cols 6
```

Options:

- `--rows <n>`: dataset rows
- `--cols <n>`: columns per row
- `--python <path>`: Python executable (default `python3`)
- `--worker-script <path>`: worker script path (default points to `python/worker.py`)

## Notes

- Dataset storage is implemented in `src/dataset.h` as a single `std::vector<double>` in row-major order.
- Dataset values are generated randomly in C++ using STL (`std::mt19937_64` + `std::uniform_real_distribution<double>`) with fixed seed 42 and range [-1.0, 1.0].
- Rows are divided evenly into contiguous partitions and each C++ thread owns one Python process for its partition.
- Python processes run a small QRC-like quantum circuit simulation per row and return per-qubit expectation values.
- IPC now uses a versioned binary protocol over stdin/stdout:
	- Header format (little-endian): `<IHHQQQ>`
	- Fields: `magic`, `version`, `message_type`, `task_id`, `rows`, `cols_or_aux`
	- `TASK` messages carry raw `float64` row-major payload (`rows * cols` doubles)
	- `RESULT` messages carry `rows * expectation_cols` `float64` values (row-major expectation matrix)
