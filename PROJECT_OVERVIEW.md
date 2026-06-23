# Project Overview: Parallel C++/Python Data Processing

## Purpose

This project demonstrates a **high‑performance parallel processing pipeline** where a C++ master program holds a large row‑contiguous dataset in memory and off‑loads computation to a configurable number of **Python worker processes**.  The workers run in parallel, each receiving a contiguous slice of rows, performing a lightweight quantum‑circuit (QRC) simulation per row, and returning the results back to the C++ side.

---

## Architecture

```
+-------------------+        +-------------------+        +-------------------+
|   C++ executable  |  IPC   |   Python worker   |  IPC   |   C++ executable  |
| (master process) | <----> |   (child process) | <----> | (aggregates results) |
+-------------------+        +-------------------+        +-------------------+
```

* **C++ master**
  * Generates a random dataset (row‑major `std::vector<double>`).
  * Splits the rows **evenly** among the requested number of workers (no `--chunk-rows` flag).
  * Forks/execs the Python script, establishing a pair of pipes for **binary** communication.
  * Sends a binary *task* header followed by the raw double payload.
  * Receives a binary *result* header and a matrix of expectation values.
  * Benchmarks the whole pipeline over a fixed set of worker counts `{1,2,4,8,16}`.

* **Python worker** (`python/worker.py`)
  * Reads the binary header and payload from `stdin.buffer`.
  * Runs a simple **quantum‑circuit simulation** per row (state‑vector of size `2^qubits`).
  * Returns a header and a matrix of Z‑expectation values (one column per qubit).
  * Uses a **fixed RNG seed (42)** for deterministic reservoir parameters.

* **Binary protocol** (little‑endian)
  * Header format: `<IHHQQQ>` → `magic`, `version`, `type`, `taskId`, `rows`, `colsOrAux`.
  * Types: `TASK = 1`, `RESULT = 3`, `QUIT = 2`, `ERROR = 4`.
  * Payloads are raw `double` arrays (`rows × cols` for tasks, `rows × expectation_cols` for results).

---

## Data Flow
1. **Dataset generation** – `std::mt19937_64` seeded to **42**, uniform real distribution in `[-1.0, 1.0]`.
2. **Row partitioning** – For `N` workers, each worker gets `rows/N` contiguous rows (last worker may get the remainder).
3. **Task transmission** – Master writes the binary header + `rows_slice × cols` doubles to the worker pipe.
4. **Quantum simulation** – Worker builds a state vector, applies `RY`, `RZ`, `RX`, `CZ` gates, then computes Z‑expectations for each qubit.
5. **Result transmission** – Worker writes a result header + `rows_slice × expectation_cols` doubles back to the master.
6. **Aggregation** – Master collects all slices, assembles the full expectation matrix, and discards it (the benchmark only measures time).

---

## Benchmarking
The executable always runs a benchmark loop over the worker counts `{1,2,4,8,16}` (or fewer if the dataset has fewer rows).  For each count it reports:

* **Requested workers** – the number the user asked for via `--workers` (now removed, the program internally sweeps the set).
* **Used workers** – `min(requested, total_rows)` – the actual number of processes spawned.
* **Expectation columns** – number of qubits (equal to `--cols`).
* **Elapsed time** – wall‑clock seconds for the whole round‑trip.

Typical output (truncated):
```
Workers Requested | Workers Used | Expectation Columns | Seconds
----------------------------------------------------------------
1                 | 1            | 8                  | 0.42
2                 | 2            | 8                  | 0.23
4                 | 4            | 8                  | 0.14
8                 | 8            | 8                  | 0.09
16                | 16           | 8                  | 0.07
```

---

## Building & Running
### Prerequisites
* C++ compiler with C++17 support (e.g., `clang++` or `g++`).
* CMake ≥ 3.15.
* Python 3 (the worker script uses only the standard library).

### Build (command line)
```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
The resulting executable is `build/parallel_python`.

### VS Code integration
The repository contains a `.vscode` folder with:
* **tasks.json** – CMake configure, build, and a *Run* task that executes the program with the desired `--rows` and `--cols` arguments.
* **launch.json** – Debug configuration that launches the built executable under the debugger.
* **settings.json** – File‑association and CMake provider settings.

You can invoke the *Run* task via **Terminal → Run Task… → Run Parallel Program**.

### Command‑line usage
```bash
./build/parallel_python --rows <NUM_ROWS> --cols <NUM_COLS>
```
* `--rows` – total number of dataset rows (e.g., `20000`).
* `--cols` – number of columns **and** the number of qubits for the QRC simulation (e.g., `8`).

No `--workers` or `--chunk-rows` flags are needed – the program automatically sweeps the predefined worker set.

---

## Configuration Details
| Parameter | Description | Default |
|-----------|-------------|---------|
| `rows` | Number of rows in the dataset. | **required** |
| `cols` | Number of columns (also qubits). | **required** |
| RNG seed | Fixed to `42` for reproducibility. | 42 |
| Data range | Uniform real distribution `[-1.0, 1.0]`. | – |
| Worker count | Internally iterates over `{1,2,4,8,16}` (capped by `rows`). | – |

---

## Extending the Project
* **Add more worker logic** – modify `python/worker.py` to perform different scientific kernels.
* **Change the protocol** – adjust the header struct in `src/worker_process.h` and the corresponding Python parsing.
* **Expose more CLI options** – extend `parse_args` in `src/main.cpp`.
* **Integrate with other languages** – the binary protocol is language‑agnostic; any process that can read/write the defined header and double payload can become a worker.

---

## License
This example code is provided under the MIT License. See the `LICENSE` file for details.

---

*Generated by GitHub Copilot on 2026‑06‑23.*