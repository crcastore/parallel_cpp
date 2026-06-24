#include "dataset.h"
#include "worker_process.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef WORKER_SCRIPT_PATH
#define WORKER_SCRIPT_PATH "python/worker.py"
#endif

constexpr std::uint64_t kDatasetSeed = 42;
constexpr double kRandomMin = -1.0;
constexpr double kRandomMax = 1.0;

struct Config
{
    std::size_t rows{100000};
    std::size_t cols{6};
    std::string pythonExe{"python3"};
    std::string workerScript{WORKER_SCRIPT_PATH};
};

struct RunResult
{
    std::size_t requestedWorkers{};
    std::size_t usedWorkers{};
    std::size_t expectationCols{};
    double seconds{};
};

struct WorkerChunkResult
{
    std::size_t beginRow{};
    std::size_t rowCount{};
    std::size_t resultCols{};
    std::vector<double> values{};
};

struct DatasetResult
{
    std::size_t rows{};
    std::size_t cols{};
    std::vector<double> values{};
};

Config parse_args(int argc, char **argv)
{
    Config cfg;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        auto readValue = [&](const std::string &name) -> std::string
        {
            if (i + 1 >= argc)
            {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--rows")
        {
            cfg.rows = std::stoull(readValue(arg));
        }
        else if (arg == "--cols")
        {
            cfg.cols = std::stoull(readValue(arg));
        }
        else if (arg == "--python")
        {
            cfg.pythonExe = readValue(arg);
        }
        else if (arg == "--worker-script")
        {
            cfg.workerScript = readValue(arg);
        }
        else if (arg == "--help")
        {
            std::cout
                << "Usage: parallel_python [options]\n"
                << "  --rows <n>          Number of dataset rows (default: 100000)\n"
                << "  --cols <n>          Number of columns per row (default: 6)\n"
                << "  --python <path>     Python executable (default: python3)\n"
                << "  --worker-script <path> Python worker script path\n";
            std::exit(0);
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (cfg.rows == 0 || cfg.cols == 0)
    {
        throw std::runtime_error("rows and cols must be > 0.");
    }

    return cfg;
}

WorkerChunkResult process_worker_chunk(
    const std::string &pythonExe,
    const std::string &workerCommand,
    const RowMajorDataset &dataset,
    std::size_t workerId,
    std::size_t workerCount)
{
    WorkerProcess worker(pythonExe, workerCommand);

    const std::size_t beginRow = (workerId * dataset.rows()) / workerCount;
    const std::size_t endRow = ((workerId + 1) * dataset.rows()) / workerCount;
    const std::size_t rowCount = endRow - beginRow;

    std::size_t resultCols{};
    std::vector<double> values = worker.process_chunk(
        workerId,
        DataView{dataset.row_ptr(beginRow), rowCount, dataset.cols()},
        resultCols);

    return WorkerChunkResult{beginRow, rowCount, resultCols, std::move(values)};
}

DatasetResult run_python_command_over_dataset(
    const std::string &pythonExe,
    const std::string &workerCommand,
    const RowMajorDataset &dataset,
    std::size_t requestedWorkers)
{
    if (requestedWorkers == 0)
    {
        throw std::runtime_error("worker count must be > 0.");
    }
    if (dataset.cols() == 0)
    {
        throw std::runtime_error("dataset columns must be > 0.");
    }
    if (dataset.rows() == 0)
    {
        return DatasetResult{};
    }

    const std::size_t workerCount = std::min(requestedWorkers, dataset.rows());

    std::vector<std::future<WorkerChunkResult>> futures;
    futures.reserve(workerCount);

    for (std::size_t workerId = 0; workerId < workerCount; ++workerId)
    {
        futures.emplace_back(std::async(
            std::launch::async,
            [&, workerId]()
            {
                return process_worker_chunk(pythonExe, workerCommand, dataset, workerId, workerCount);
            }));
    }

    std::size_t resultCols{};
    std::vector<double> results;

    for (auto &future : futures)
    {
        WorkerChunkResult chunk = future.get();

        if (resultCols == 0)
        {
            resultCols = chunk.resultCols;
            results.assign(dataset.rows() * resultCols, 0.0);
        }
        else if (resultCols != chunk.resultCols)
        {
            throw std::runtime_error("Inconsistent number of result columns from worker.");
        }

        for (std::size_t row = 0; row < chunk.rowCount; ++row)
        {
            const std::size_t srcBase = row * resultCols;
            const std::size_t dstBase = (chunk.beginRow + row) * resultCols;
            std::copy_n(
                chunk.values.begin() + srcBase,
                resultCols,
                results.begin() + dstBase);
        }
    }

    return DatasetResult{dataset.rows(), resultCols, std::move(results)};
}

RunResult run_once(
    const Config &cfg,
    const RowMajorDataset &dataset,
    std::size_t requestedWorkers)
{
    const std::size_t workerCount = std::min(requestedWorkers, dataset.rows());

    const auto t0 = std::chrono::steady_clock::now();
    const DatasetResult result = run_python_command_over_dataset(
        cfg.pythonExe,
        cfg.workerScript,
        dataset,
        requestedWorkers);

    const auto t1 = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(t1 - t0).count();

    return RunResult{requestedWorkers, workerCount, result.cols, seconds};
}

int main(int argc, char **argv)
{
    try
    {
        const Config cfg = parse_args(argc, argv);

        std::cout << "Creating dataset: " << cfg.rows << "x" << cfg.cols << " (row-major)\n";
        RowMajorDataset dataset(cfg.rows, cfg.cols);

        std::mt19937_64 rng(kDatasetSeed);
        std::uniform_real_distribution<double> dist(kRandomMin, kRandomMax);
        for (std::size_t r = 0; r < cfg.rows; ++r)
        {
            for (std::size_t c = 0; c < cfg.cols; ++c)
            {
                dataset.at(r, c) = dist(rng);
            }
        }

        const std::vector<std::size_t> benchmarkWorkers{1, 2, 4, 8, 16};
        std::vector<RunResult> timings;
        timings.reserve(benchmarkWorkers.size());

        for (const std::size_t requestedWorkers : benchmarkWorkers)
        {
            timings.push_back(run_once(cfg, dataset, requestedWorkers));
        }

        std::cout << "\nRuntime by parallel process count:\n";
        std::cout << "  workers(requested)  workers(used)  time(s)\n";
        for (const auto &result : timings)
        {
            std::cout << "  "
                      << std::setw(18) << result.requestedWorkers
                      << std::setw(15) << result.usedWorkers
                      << std::setw(10) << std::fixed << std::setprecision(6) << result.seconds
                      << '\n';
        }

        if (!timings.empty())
        {
            std::cout << "Each row returned " << timings.front().expectationCols
                      << " expectation values (QRC-style features).\n";
        }

        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
