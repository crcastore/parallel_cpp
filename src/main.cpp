#include "dataset.h"
#include "worker_process.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef WORKER_SCRIPT_PATH
#define WORKER_SCRIPT_PATH "python/worker.py"
#endif

// --- Config ---
struct Config
{
    std::size_t rows = 100000;
    std::size_t cols = 6;
    std::string pythonExe = "python3";
    std::string workerScript = WORKER_SCRIPT_PATH;
};

Config parse_args(int argc, char **argv)
{
    Config cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (i + 1 >= argc)
            throw std::runtime_error("Missing value for " + arg);

        if (arg == "--rows")
            cfg.rows = std::stoull(argv[++i]);
        else if (arg == "--cols")
            cfg.cols = std::stoull(argv[++i]);
        else if (arg == "--python")
            cfg.pythonExe = argv[++i];
        else if (arg == "--worker-script")
            cfg.workerScript = argv[++i];
        else if (arg == "--help")
        {
            std::cout << "Usage: parallel_python [options]\n"
                      << "  --rows <n>              Dataset rows (default: 100000)\n"
                      << "  --cols <n>              Columns per row (default: 6)\n"
                      << "  --python <path>         Python executable (default: python3)\n"
                      << "  --worker-script <path>  Worker script path\n";
            std::exit(0);
        }
        else
            throw std::runtime_error("Unknown argument: " + arg);
    }
    if (cfg.rows == 0 || cfg.cols == 0)
        throw std::runtime_error("rows and cols must be > 0.");
    return cfg;
}

// --- Run one worker on a slice of the dataset ---
std::vector<double> run_worker(const Config &cfg, const Dataset &ds,
                               std::size_t beginRow, std::size_t rowCount,
                               std::size_t taskId, std::size_t &resultCols)
{
    WorkerProcess worker(cfg.pythonExe, cfg.workerScript);
    auto values = worker.process_chunk(taskId,
                                       DataView{ds.row_ptr(beginRow), rowCount, ds.cols()},
                                       resultCols);
    worker.stop();
    return values;
}

// --- Run all workers in parallel and assemble results ---
std::vector<double> run_parallel(const Config &cfg, const Dataset &ds,
                                 std::size_t numWorkers, std::size_t &resultCols)
{
    numWorkers = std::min(numWorkers, ds.rows());

    // Each worker returns its values and the number of result columns.
    struct ChunkResult
    {
        std::size_t beginRow;
        std::size_t rowCount;
        std::size_t cols;
        std::vector<double> values;
    };

    // Launch workers.
    std::vector<std::future<ChunkResult>> futures;
    futures.reserve(numWorkers);
    for (std::size_t w = 0; w < numWorkers; ++w)
    {
        auto beginRow = (w * ds.rows()) / numWorkers;
        auto endRow = ((w + 1) * ds.rows()) / numWorkers;
        futures.push_back(std::async(std::launch::async, [&, beginRow, endRow, w]() -> ChunkResult
                                     {
            std::size_t rc = 0;
            auto vals = run_worker(cfg, ds, beginRow, endRow - beginRow, w, rc);
            return {beginRow, endRow - beginRow, rc, std::move(vals)}; }));
    }

    // Collect results.
    std::vector<double> results;
    for (std::size_t w = 0; w < numWorkers; ++w)
    {
        auto chunk = futures[w].get();
        if (results.empty())
        {
            resultCols = chunk.cols;
            results.resize(ds.rows() * resultCols);
        }
        std::copy(chunk.values.begin(), chunk.values.end(),
                  results.begin() + static_cast<std::ptrdiff_t>(chunk.beginRow * resultCols));
    }
    return results;
}

int main(int argc, char **argv)
{
    try
    {
        auto cfg = parse_args(argc, argv);

        std::cout << "Creating dataset: " << cfg.rows << "x" << cfg.cols << "\n";
        Dataset ds(cfg.rows, cfg.cols);

        std::mt19937_64 rng(42);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        for (std::size_t r = 0; r < ds.rows(); ++r)
            for (std::size_t c = 0; c < ds.cols(); ++c)
                ds.at(r, c) = dist(rng);

        const std::size_t workerCounts[] = {1, 2, 4, 8, 16};
        std::cout << "\nRuntime by parallel process count:\n"
                  << "  workers(requested)  workers(used)  time(s)\n";

        for (auto nw : workerCounts)
        {
            auto t0 = std::chrono::steady_clock::now();
            std::size_t resultCols = 0;
            run_parallel(cfg, ds, nw, resultCols);
            auto t1 = std::chrono::steady_clock::now();
            double seconds = std::chrono::duration<double>(t1 - t0).count();

            std::cout << "  " << std::setw(18) << nw
                      << std::setw(15) << std::min(nw, ds.rows())
                      << std::setw(10) << std::fixed << std::setprecision(6) << seconds << "\n";
        }

        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
