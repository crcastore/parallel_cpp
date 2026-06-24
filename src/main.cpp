#include "dataset.h"
#include "worker_process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef WORKER_SCRIPT_PATH
#define WORKER_SCRIPT_PATH "python/worker.py"
#endif

// Runtime options for the benchmark utility.
struct Args
{
    std::size_t rows = 100000;
    std::size_t cols = 6;
    std::string pythonExe = "python3";
    std::string script = WORKER_SCRIPT_PATH;
};

// Prints command-line usage.
void print_usage()
{
    std::cout << "Usage: parallel_python [--rows N] [--cols N] [--python PATH] [--worker-script PATH]\n";
}

// Parses command-line arguments into Args.
Args parse_args(int argc, char **argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        if (a == "--help")
        {
            print_usage();
            std::exit(0);
        }
        if (i + 1 >= argc)
            throw std::runtime_error("Missing value for " + std::string(a));

        if (a == "--rows")
            args.rows = std::stoull(argv[++i]);
        else if (a == "--cols")
            args.cols = std::stoull(argv[++i]);
        else if (a == "--python")
            args.pythonExe = argv[++i];
        else if (a == "--worker-script")
            args.script = argv[++i];
        else
            throw std::runtime_error("Unknown argument: " + std::string(a));
    }
    if (args.rows == 0 || args.cols == 0)
        throw std::runtime_error("rows and cols must be > 0.");
    return args;
}

// Fills a dataset with deterministic pseudo-random values in [-1, 1].
void fill_random(Dataset &ds)
{
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (std::size_t r = 0; r < ds.rows(); ++r)
        for (std::size_t c = 0; c < ds.cols(); ++c)
            ds.at(r, c) = dist(rng);
}

// Runs the worker benchmark across multiple process counts.
void run_benchmark(const Args &args, const Dataset &ds)
{
    const Worker worker{args.pythonExe, args.script};
    const DataView view{ds.data(), ds.rows(), ds.cols()};
    const std::array<std::size_t, 5> workerCounts{1, 2, 4, 8, 16};

    std::cout << "\nRuntime by parallel process count:\n"
              << "  workers(requested)  workers(used)  time(s)\n";

    for (const std::size_t requestedWorkers : workerCounts)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto output = parallel_map(worker, view, requestedWorkers);
        const auto t1 = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(t1 - t0).count();
        (void)output;

        const std::size_t usedWorkers = std::min(requestedWorkers, ds.rows());
        std::cout << "  " << std::setw(18) << requestedWorkers
                  << std::setw(15) << usedWorkers
                  << std::setw(10) << std::fixed << std::setprecision(6) << seconds << "\n";
    }
}

int main(int argc, char **argv)
{
    try
    {
        const Args args = parse_args(argc, argv);

        std::cout << "Creating dataset: " << args.rows << "x" << args.cols << "\n";
        Dataset ds(args.rows, args.cols);
        fill_random(ds);

        run_benchmark(args, ds);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
