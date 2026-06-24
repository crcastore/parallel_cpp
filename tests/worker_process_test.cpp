#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <stdexcept>
#include <vector>
#include <thread>
#include <mutex>
#include "worker_process.h"
#include "dataset.h"

static WorkerProcess make_worker()
{
    return WorkerProcess("python3", WORKER_SCRIPT_PATH);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("WorkerProcess starts in constructor and stops cleanly", "[worker][lifecycle]")
{
    auto wp = make_worker();
    REQUIRE(wp.stop());
}

TEST_CASE("WorkerProcess stop is idempotent", "[worker][lifecycle]")
{
    auto wp = make_worker();
    REQUIRE(wp.stop());
    REQUIRE(wp.stop());
}

TEST_CASE("WorkerProcess destructor stops automatically", "[worker][lifecycle]")
{
    {
        auto wp = make_worker();
    }
    SUCCEED();
}

// ---------------------------------------------------------------------------
// process_chunk – result shape
// ---------------------------------------------------------------------------
TEST_CASE("process_chunk returns one expectation value per qubit (col)", "[worker][chunk]")
{
    auto wp = make_worker();

    constexpr std::size_t rows = 1, cols = 4;
    const double data[rows * cols] = {0.1, -0.2, 0.3, -0.4};

    std::size_t rc = 0;
    auto result = wp.process_chunk(0, DataView{data, rows, cols}, rc);

    REQUIRE(rc == cols);
    REQUIRE(result.size() == rows * rc);
    wp.stop();
}

TEST_CASE("process_chunk returns correct number of rows", "[worker][chunk]")
{
    auto wp = make_worker();

    constexpr std::size_t rows = 5, cols = 3;
    std::vector<double> data(rows * cols, 0.0);

    std::size_t rc = 0;
    auto result = wp.process_chunk(2, DataView{data.data(), rows, cols}, rc);

    REQUIRE(rc == cols);
    REQUIRE(result.size() == rows * rc);
    wp.stop();
}

TEST_CASE("process_chunk works with a single row", "[worker][chunk]")
{
    auto wp = make_worker();

    constexpr std::size_t rows = 1, cols = 6;
    std::vector<double> data(rows * cols, 0.0);

    std::size_t rc = 0;
    auto result = wp.process_chunk(0, DataView{data.data(), rows, cols}, rc);

    REQUIRE(rc == cols);
    REQUIRE(result.size() == rows * rc);
    wp.stop();
}

// ---------------------------------------------------------------------------
// process_chunk – value correctness
// ---------------------------------------------------------------------------
TEST_CASE("process_chunk expectation values are in [-1, 1]", "[worker][chunk]")
{
    auto wp = make_worker();

    constexpr std::size_t rows = 10, cols = 4;
    std::vector<double> data(rows * cols, 0.5);

    std::size_t rc = 0;
    auto result = wp.process_chunk(1, DataView{data.data(), rows, cols}, rc);

    REQUIRE(result.size() == rows * rc);
    for (double v : result)
    {
        REQUIRE(v >= -1.0);
        REQUIRE(v <= 1.0);
    }
    wp.stop();
}

TEST_CASE("process_chunk is deterministic for identical inputs", "[worker][chunk]")
{
    constexpr std::size_t rows = 3, cols = 4;
    std::vector<double> data(rows * cols);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<double>(i) * 0.1 - 0.5;

    auto run = [&]() -> std::vector<double>
    {
        auto wp = make_worker();
        std::size_t rc = 0;
        auto res = wp.process_chunk(0, DataView{data.data(), rows, cols}, rc);
        wp.stop();
        return res;
    };

    auto r1 = run();
    auto r2 = run();
    REQUIRE(r1.size() == r2.size());
    // With seeded simulator and high shots, results should be nearly identical
    for (std::size_t i = 0; i < r1.size(); ++i)
        REQUIRE(r1[i] == Catch::Approx(r2[i]).epsilon(1e-6));
}

TEST_CASE("process_chunk handles multiple sequential tasks with different IDs", "[worker][chunk]")
{
    auto wp = make_worker();

    constexpr std::size_t rows = 2, cols = 2;
    const double data[rows * cols] = {0.1, 0.2, 0.3, 0.4};

    for (std::size_t id = 10; id <= 14; ++id)
    {
        std::size_t rc = 0;
        auto res = wp.process_chunk(id, DataView{data, rows, cols}, rc);
        REQUIRE(res.size() == rows * rc);
        REQUIRE(rc == cols);
    }
    wp.stop();
}

// ---------------------------------------------------------------------------
// process_chunk – error handling
// ---------------------------------------------------------------------------
TEST_CASE("process_chunk throws after worker is stopped", "[worker][chunk]")
{
    auto wp = make_worker();
    REQUIRE(wp.stop());
    const double data[4] = {0.1, 0.2, 0.3, 0.4};
    std::size_t rc = 0;
    REQUIRE_THROWS_AS(wp.process_chunk(0, DataView{data, 1, 4}, rc), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Dataset + WorkerProcess integration
// ---------------------------------------------------------------------------
TEST_CASE("process_chunk works with Dataset row_ptr", "[worker][dataset]")
{
    Dataset ds(4, 3);
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = 0; c < 3; ++c)
            ds.at(r, c) = static_cast<double>(r) * 0.1 + static_cast<double>(c) * 0.01;

    auto wp = make_worker();

    std::size_t rc = 0;
    auto result = wp.process_chunk(0, DataView{ds.row_ptr(0), ds.rows(), ds.cols()}, rc);
    REQUIRE(result.size() == ds.rows() * rc);
    REQUIRE(rc == ds.cols());
    wp.stop();
}

TEST_CASE("process_chunk result dimensions match dataset slice", "[worker][dataset]")
{
    Dataset ds(8, 5);
    for (std::size_t r = 0; r < 8; ++r)
        for (std::size_t c = 0; c < 5; ++c)
            ds.at(r, c) = static_cast<double>(r + c) * 0.05;

    auto wp = make_worker();

    // Process only the middle 4 rows
    std::size_t rc = 0;
    auto result = wp.process_chunk(0, DataView{ds.row_ptr(2), 4, ds.cols()}, rc);
    REQUIRE(rc == ds.cols());
    REQUIRE(result.size() == 4 * rc);
    wp.stop();
}

// ---------------------------------------------------------------------------
// Parallel multi-worker
// ---------------------------------------------------------------------------
TEST_CASE("two workers processing disjoint slices cover all rows", "[worker][parallel]")
{
    constexpr std::size_t totalRows = 10, cols = 4;
    Dataset ds(totalRows, cols);
    for (std::size_t r = 0; r < totalRows; ++r)
        for (std::size_t c = 0; c < cols; ++c)
            ds.at(r, c) = static_cast<double>(r * cols + c) * 0.01;

    std::vector<double> combined(totalRows * cols, 0.0);
    std::size_t sharedResultCols = 0;
    std::mutex mtx;

    auto run_slice = [&](std::size_t begin, std::size_t end, std::size_t taskId)
    {
        auto wp = make_worker();
        std::size_t rc = 0;
        auto res = wp.process_chunk(taskId, DataView{ds.row_ptr(begin), end - begin, cols}, rc);
        wp.stop();
        std::lock_guard<std::mutex> lk(mtx);
        sharedResultCols = rc;
        for (std::size_t i = 0; i < (end - begin); ++i)
            for (std::size_t j = 0; j < rc; ++j)
                combined[(begin + i) * rc + j] = res[i * rc + j];
    };

    std::thread t1(run_slice, 0, 5, 0);
    std::thread t2(run_slice, 5, 10, 1);
    t1.join();
    t2.join();

    REQUIRE(sharedResultCols == cols);
    REQUIRE(combined.size() == totalRows * sharedResultCols);
}

TEST_CASE("four parallel workers cover all rows without loss", "[worker][parallel]")
{
    constexpr std::size_t totalRows = 16, cols = 3, nWorkers = 4;
    Dataset ds(totalRows, cols);
    for (std::size_t r = 0; r < totalRows; ++r)
        for (std::size_t c = 0; c < cols; ++c)
            ds.at(r, c) = static_cast<double>(r + c) * 0.1;

    std::vector<std::vector<double>> sliceResults(nWorkers);
    std::vector<std::size_t> sliceCols(nWorkers, 0);

    std::vector<std::thread> threads;
    for (std::size_t w = 0; w < nWorkers; ++w)
    {
        threads.emplace_back([&, w]()
                             {
            const std::size_t begin = (w * totalRows) / nWorkers;
            const std::size_t end   = ((w + 1) * totalRows) / nWorkers;
            auto wp = make_worker();
            sliceResults[w] = wp.process_chunk(w, DataView{ds.row_ptr(begin), end - begin, cols}, sliceCols[w]);
            wp.stop(); });
    }
    for (auto &t : threads)
        t.join();

    for (std::size_t w = 0; w < nWorkers; ++w)
    {
        REQUIRE(sliceCols[w] == cols);
        const std::size_t begin = (w * totalRows) / nWorkers;
        const std::size_t end = ((w + 1) * totalRows) / nWorkers;
        REQUIRE(sliceResults[w].size() == (end - begin) * cols);
    }
}

TEST_CASE("parallel workers produce same results as sequential for same slices", "[worker][parallel]")
{
    constexpr std::size_t rows = 6, cols = 3;
    Dataset ds(rows, cols);
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < cols; ++c)
            ds.at(r, c) = static_cast<double>(r * cols + c) * 0.07;

    // Sequential reference
    std::vector<double> seqResult;
    {
        auto wp = make_worker();
        std::size_t rc = 0;
        seqResult = wp.process_chunk(0, DataView{ds.row_ptr(0), rows, cols}, rc);
        wp.stop();
    }

    // Two parallel workers each covering half – assemble result
    std::vector<double> parResult(rows * cols, 0.0);
    std::mutex mtx;

    auto run_half = [&](std::size_t begin, std::size_t end, std::size_t id)
    {
        auto wp = make_worker();
        std::size_t rc = 0;
        auto res = wp.process_chunk(id, DataView{ds.row_ptr(begin), end - begin, cols}, rc);
        wp.stop();
        std::lock_guard<std::mutex> lk(mtx);
        for (std::size_t i = 0; i < (end - begin); ++i)
            for (std::size_t j = 0; j < rc; ++j)
                parResult[(begin + i) * rc + j] = res[i * rc + j];
    };

    std::thread t1(run_half, 0, 3, 0);
    std::thread t2(run_half, 3, 6, 1);
    t1.join();
    t2.join();

    REQUIRE(seqResult.size() == parResult.size());
    // With seeded simulator and high shots, sequential and parallel results should match
    for (std::size_t i = 0; i < seqResult.size(); ++i)
        REQUIRE(seqResult[i] == Catch::Approx(parResult[i]).epsilon(1e-6));
}
