#include "dataset.h"
#include "worker_process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifndef WORKER_SCRIPT_PATH
#define WORKER_SCRIPT_PATH "python/worker.py"
#endif

struct Config {
    std::size_t rows = 100000;
    std::size_t cols = 32;
    std::size_t workers = 4;
    std::size_t chunkRows = 1000;
    std::string pythonExe = "python3";
    std::string workerScript = WORKER_SCRIPT_PATH;
};

Config parse_args(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto readValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--rows") {
            cfg.rows = std::stoull(readValue(arg));
        } else if (arg == "--cols") {
            cfg.cols = std::stoull(readValue(arg));
        } else if (arg == "--workers") {
            cfg.workers = std::stoull(readValue(arg));
        } else if (arg == "--chunk-rows") {
            cfg.chunkRows = std::stoull(readValue(arg));
        } else if (arg == "--python") {
            cfg.pythonExe = readValue(arg);
        } else if (arg == "--worker-script") {
            cfg.workerScript = readValue(arg);
        } else if (arg == "--help") {
            std::cout
                << "Usage: parallel_python [options]\n"
                << "  --rows <n>          Number of dataset rows (default: 100000)\n"
                << "  --cols <n>          Number of columns per row (default: 32)\n"
                << "  --workers <n>       Number of Python processes (default: 4)\n"
                << "  --chunk-rows <n>    Rows per chunk sent to workers (default: 1000)\n"
                << "  --python <path>     Python executable (default: python3)\n"
                << "  --worker-script <path> Python worker script path\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (cfg.rows == 0 || cfg.cols == 0 || cfg.workers == 0 || cfg.chunkRows == 0) {
        throw std::runtime_error("rows, cols, workers and chunk-rows must be > 0.");
    }

    return cfg;
}

int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);

        std::cout << "Creating dataset: " << cfg.rows << "x" << cfg.cols << " (row-major)\n";
        RowMajorDataset dataset(cfg.rows, cfg.cols);

        for (std::size_t r = 0; r < cfg.rows; ++r) {
            for (std::size_t c = 0; c < cfg.cols; ++c) {
                dataset.at(r, c) = static_cast<double>((r % 97) + (c * 0.1));
            }
        }

        const std::size_t numChunks = (cfg.rows + cfg.chunkRows - 1) / cfg.chunkRows;
        std::vector<double> rowSums(cfg.rows, 0.0);
        std::atomic<std::size_t> nextChunk{0};

        const std::size_t threadCount = std::min(cfg.workers, numChunks);
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        std::vector<std::string> errors(threadCount);

        const auto t0 = std::chrono::steady_clock::now();

        for (std::size_t workerId = 0; workerId < threadCount; ++workerId) {
            threads.emplace_back([&, workerId]() {
                try {
                    WorkerProcess worker(cfg.pythonExe, cfg.workerScript);
                    if (!worker.start()) {
                        throw std::runtime_error("Failed to start Python worker process.");
                    }

                    while (true) {
                        const std::size_t chunkIndex = nextChunk.fetch_add(1);
                        if (chunkIndex >= numChunks) {
                            break;
                        }

                        const std::size_t beginRow = chunkIndex * cfg.chunkRows;
                        const std::size_t endRow = std::min(beginRow + cfg.chunkRows, cfg.rows);
                        const std::size_t chunkRowCount = endRow - beginRow;

                        const double* chunkData = dataset.row_ptr(beginRow);
                        std::vector<double> chunkResult = worker.process_chunk(
                            chunkIndex,
                            chunkData,
                            chunkRowCount,
                            cfg.cols
                        );

                        for (std::size_t i = 0; i < chunkRowCount; ++i) {
                            rowSums[beginRow + i] = chunkResult[i];
                        }
                    }

                    worker.stop();
                } catch (const std::exception& ex) {
                    errors[workerId] = ex.what();
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        for (const auto& err : errors) {
            if (!err.empty()) {
                throw std::runtime_error(err);
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(t1 - t0).count();

        std::cout << "Processed " << cfg.rows << " rows via " << threadCount
                  << " Python workers in " << seconds << "s\n";

        std::cout << "First 10 row sums:\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(10, rowSums.size()); ++i) {
            std::cout << "  row " << i << ": " << rowSums[i] << '\n';
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
