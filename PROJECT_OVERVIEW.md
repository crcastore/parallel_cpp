# Project Overview: Parallel C++/Python Data Processing

## Purpose

This project demonstrates a parallel processing pipeline where a C++ executable keeps a row-major dataset in memory, splits it into contiguous row chunks, and off-loads each chunk to a Python worker process. The Python side performs a Qiskit Aer quantum reservoir style simulation per row and returns a row-major matrix of Z expectation values.

---

## Architecture

```
+-------------------+        +-------------------+        +-------------------+
|   C++ executable  |  IPC   |   Python worker   |  IPC   |   C++ executable   |
| (master process)  | <----> |   (child process)  | <----> | (aggregates results) |
+-------------------+        +-------------------+        +-------------------+
```

* **C++ master**
  * Generates a random dataset (row-major `std::vector<double>`).
  * Splits the rows as evenly as possible among the requested number of workers.
  * Uses `std::async` to launch one subprocess-backed worker per chunk.
  * Forks/execs the Python script, establishing a pair of pipes for binary communication.
  * Sends a binary *task* header followed by the raw double payload.
  * Receives a binary *result* header and a matrix of expectation values.
  * Benchmarks the whole pipeline over a fixed set of worker counts `{1,2,4,8,16}`.
  * Can optionally pin each worker to a Linux CPU using `--pin-workers-single-cpu`.

* **Python worker** (`python/worker.py`)
  * Reads the binary header and payload from `stdin.buffer`.
  * Runs a Qiskit Aer simulation per row (state-vector backend, one circuit per input row).
  * Returns a header and a matrix of Z-expectation values (one column per qubit / input column).
  * Uses fixed seeds for deterministic reservoir parameters and simulator output.

* **Binary protocol** (little‑endian)
  * Header format: `<IHHQQQ>` → `magic`, `version`, `type`, `taskId`, `rows`, `cols`.
  * `magic` is the four-byte constant `0x50595043` (`CPYP`), and `version` is currently `1`.
  * `taskId` is echoed by the worker so the C++ side can match requests and responses.
  * For `Task`, `rows` is the number of dataset rows in the chunk and `cols` is the input column count.
  * For `Result`, `rows` is the number of output rows and `cols` is the output column count returned by the worker.
  * For `Error`, `rows` stores the UTF-8 error-message byte length and `cols` is used as an auxiliary length field.
  * Payloads are raw `double` arrays (`rows × cols` for tasks, `rows × expectation_cols` for results).
  * `Quit` carries only the header and no payload.

---

## Data Flow
1. **Dataset generation** – `std::mt19937_64` seeded to `42`, uniform real distribution in `[-1.0, 1.0]`.
2. **Row partitioning** – For `N` workers, rows are split into contiguous chunks as evenly as possible.
3. **Task transmission** – Master writes the binary header + `rows_slice × cols` doubles to the worker pipe.
4. **Quantum simulation** – Worker builds a circuit, applies feature encoding plus reservoir layers with `RY`, `RZ`, `RX`, and `CZ`, then computes Z-expectations for each qubit.
5. **Result transmission** – Worker writes a result header + `rows_slice × expectation_cols` doubles back to the master.
6. **Aggregation** – Master collects all slices, assembles the full expectation matrix, and discards it after timing.

The transport is strictly stream-based: each header is written first, followed immediately by the fixed-size payload for that message type. The worker process exits on `Quit` or EOF, and the C++ side sends `Quit` during destruction after it has finished reading results.

---

## Benchmarking
The executable always runs a benchmark loop over the worker counts `{1,2,4,8,16}` (or fewer if the dataset has fewer rows). For each count it reports:

* **Requested workers** – the sweep value, not a user-supplied `--workers` flag.
* **Used workers** – `min(requested, total_rows)`.
* **Elapsed time** – wall-clock seconds for the whole round-trip.

Typical output (truncated):
```
  workers(requested)  workers(used)  time(s)
  ------------------  -------------  --------
                   1              1  0.420000
                   2              2  0.230000
                   4              4  0.140000
                   8              8  0.090000
                  16             16  0.070000
```

---

## Building & Running
### Prerequisites
* C++ compiler with C++17 support (e.g., `clang++` or `g++`).
* CMake 3.16 or newer.
* Python 3 with `numpy`, `qiskit`, and `qiskit-aer` installed.

### Build (command line)
```bash
cmake -S . -B build
cmake --build build -j
```
The resulting executable is `build/parallel_python`.

### VS Code integration
The workspace defines VS Code tasks for configuring, building, and running the binaries, including the sanitizer-backed test executables.

You can invoke the run tasks via **Terminal → Run Task…**.

### Command‑line usage
```bash
./build/parallel_python --rows <NUM_ROWS> --cols <NUM_COLS>
```
* `--rows` – total number of dataset rows, required.
* `--cols` – number of columns and the number of qubits used by the simulation, required.
* `--pin-workers-single-cpu` – optional Linux-only CPU pinning.
* `--cpu-start` / `--cpu-stride` – CPU selection controls used when pinning.

There is no `--workers` or `--chunk-rows` flag; worker counts are swept internally.

---

## Configuration Details
| Parameter | Description | Default |
|-----------|-------------|---------|
| `rows` | Number of rows in the dataset. | **required** |
| `cols` | Number of columns (also qubits). | **required** |
| RNG seed | Fixed to `42` for reproducibility. | 42 |
| Data range | Uniform real distribution `[-1.0, 1.0]`. | – |
| Worker count | Internally iterates over `{1,2,4,8,16}` (capped by `rows`). | – |
| CPU pinning | Linux-only, disabled unless `--pin-workers-single-cpu` is set. | off |

---

## Extending the Project
* **Add more worker logic** – modify `python/worker.py` to perform different scientific kernels.
* **Change the protocol** – adjust `src/protocol.h`, the C++ worker transport in `src/worker_process.cpp`, and the Python parsing code together.
* **Expose more CLI options** – extend `parse_args` in `src/main.cpp`.
* **Integrate with other languages** – the binary protocol is language‑agnostic; any process that can read/write the defined header and double payload can become a worker.

---

## License
This example code is provided under the MIT License. See the `LICENSE` file for details.