#include "dataset.h"
#include "worker_process.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifndef WORKER_SCRIPT_PATH
#define WORKER_SCRIPT_PATH "python/worker.py"
#endif

// Parse command-line arguments, simplified
struct Args
{
    std::size_t rows = 100000;
    std::size_t cols = 6;
    std::string pythonExe = "python3";
    std::string script = WORKER_SCRIPT_PATH;
};

Args parse(int argc, char **argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--help")
        {
            std::cout << "Usage: parallel_python [--rows N] [--cols N] [--python PATH] [--worker-script PATH]\n";
            std::exit(0);
        }
        if (i + 1 >= argc)
            throw std::runtime_error("Missing value for " + a);

        if (a == "--rows")
            args.rows = std::stoull(argv[++i]);
        else if (a == "--cols")
            args.cols = std::stoull(argv[++i]);
        else if (a == "--python")
            args.pythonExe = argv[++i];
        else if (a == "--worker-script")
            args.script = argv[++i];
        else
            throw std::runtime_error("Unknown: " + a);
    }
    if (args.rows == 0 || args.cols == 0)
        throw std::runtime_error("rows and cols must be > 0");
    return args;
}

// Populate dataset with random values
void fill_random(Dataset &ds)
{
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (std::size_t r = 0; r < ds.rows(); ++r)
        for (std::size_t c = 0; c < ds.cols(); ++c)
            ds.at(r, c) = dist(rng);
}

// Run benchmark: apply worker to data at different parallelism levels
void benchmark(const Args &args, const Dataset &ds)
{
    const auto worker = Worker{args.pythonExe, args.script};
    const DataView view{ds.data(), ds.rows(), ds.cols()};

    std::cout << "\nRuntime by parallel process count:\n"
              << "  workers(requested)  workers(used)  time(s)\n";

    for (auto nw : {1, 2, 4, 8, 16})
    {
        auto t0 = std::chrono::steady_clock::now();
        parallel_map(worker, view, nw);
        auto t1 = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(t1 - t0).count();

        auto used = std::min(static_cast<std::size_t>(nw), ds.rows());
        std::cout << "  " << std::setw(18) << nw
                  << std::setw(15) << used
                  << std::setw(10) << std::fixed << std::setprecision(6) << seconds << "\n";
    }
}

int main(int argc, char **argv)
{
    try
    {
        auto args = parse(argc, argv);

        std::cout << "Creating dataset: " << args.rows << "x" << args.cols << "\n";
        Dataset ds(args.rows, args.cols);
        fill_random(ds);

        benchmark(args, ds);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
