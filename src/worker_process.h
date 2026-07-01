#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "dataset.h"

// Lightweight function object that processes one chunk via a Python subprocess.
struct Worker
{
    std::string pythonExe;
    std::string scriptPath;

    // Processes one chunk and writes output column count into resultCols.
    std::vector<double> operator()(std::size_t taskId, const DataView &input, std::size_t &resultCols) const;
};

// Applies worker(taskId, chunk, resultCols) to row chunks in parallel.
// Returns the full output matrix flattened in row-major order.
template <typename WorkerFn>
std::vector<double> parallel_map(
    const WorkerFn &worker,
    const DataView &fullData,
    std::size_t numWorkers)
{
    if (fullData.rows == 0)
    {
        return {};
    }
    if (numWorkers == 0)
    {
        throw std::invalid_argument("parallel_map requires numWorkers > 0.");
    }

    const std::size_t workers = std::min(numWorkers, fullData.rows);

    struct ChunkResult
    {
        std::size_t beginRow{};
        std::size_t rowCount{};
        std::size_t resultCols{};
        std::vector<double> values;
    };

    std::vector<std::future<ChunkResult>> futures;
    futures.reserve(workers);

    for (std::size_t w = 0; w < workers; ++w)
    {
        const std::size_t beginRow = (w * fullData.rows) / workers;
        const std::size_t endRow = ((w + 1) * fullData.rows) / workers;

        futures.emplace_back(std::async(std::launch::async, [&, beginRow, endRow, w]()
                                        {
            const DataView chunk{fullData.data + beginRow * fullData.cols, endRow - beginRow, fullData.cols};
            std::size_t resultCols = 0;
            auto values = worker(w, chunk, resultCols);
            return ChunkResult{beginRow, endRow - beginRow, resultCols, std::move(values)}; }));
    }

    std::vector<double> results;
    std::size_t commonResultCols = 0;

    for (std::future<ChunkResult> &future : futures)
    {
        ChunkResult chunk = future.get();
        if (chunk.rowCount == 0)
        {
            continue;
        }

        if (results.empty())
        {
            commonResultCols = chunk.resultCols;
            if (commonResultCols == 0)
            {
                throw std::runtime_error("Worker returned zero output columns.");
            }
            results.resize(fullData.rows * commonResultCols);
        }

        if (chunk.resultCols != commonResultCols)
        {
            throw std::runtime_error("Workers returned inconsistent output column counts.");
        }

        const std::size_t expectedValues = chunk.rowCount * chunk.resultCols;
        if (chunk.values.size() != expectedValues)
        {
            throw std::runtime_error("Worker returned an unexpected value count.");
        }

        const auto offset = static_cast<std::ptrdiff_t>(chunk.beginRow * commonResultCols);
        std::copy(chunk.values.begin(), chunk.values.end(), results.begin() + offset);
    }

    return results;
}
