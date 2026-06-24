#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <sys/types.h>
#include <vector>

#include "dataset.h"

// Lightweight worker that processes data chunks via a Python subprocess.
// Can be used with parallel_map utility for easy parallelization.
struct Worker
{
    std::string pythonExe;
    std::string scriptPath;

    // Process one chunk of data and return results.
    std::vector<double> operator()(std::size_t taskId, const DataView &input, std::size_t &resultCols) const;
};

// Generic parallel map: apply a worker function to data chunks.
// WorkerFn: callable that takes (taskId, DataView, resultCols) and returns std::vector<double>
template <typename WorkerFn>
std::vector<double> parallel_map(
    const WorkerFn &worker,
    const DataView &fullData,
    std::size_t numWorkers)
{
    numWorkers = std::min(numWorkers, fullData.rows);

    std::vector<std::future<std::pair<std::size_t, std::vector<double>>>> futures;
    futures.reserve(numWorkers);

    // Launch all workers
    for (std::size_t w = 0; w < numWorkers; ++w)
    {
        auto begin = (w * fullData.rows) / numWorkers;
        auto end = ((w + 1) * fullData.rows) / numWorkers;

        futures.push_back(std::async(std::launch::async, [&, begin, end, w]()
                                     {
            std::size_t resultCols = 0;
            DataView chunk{fullData.data + begin * fullData.cols, end - begin, fullData.cols};
            auto values = worker(w, chunk, resultCols);
            return std::make_pair(begin * resultCols, std::move(values)); }));
    }

    // Collect results
    std::size_t totalResultCols = 0;
    std::vector<double> results;

    for (std::size_t w = 0; w < numWorkers; ++w)
    {
        auto [offset, values] = futures[w].get();
        if (results.empty())
        {
            totalResultCols = values.size() / (fullData.rows / numWorkers);
            results.resize(fullData.rows * totalResultCols);
        }
        std::copy(values.begin(), values.end(), results.begin() + offset);
    }

    return results;
}
