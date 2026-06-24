#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include "worker_process.h"
#include "dataset.h"

static Worker make_worker()
{
    return Worker{"python3", WORKER_SCRIPT_PATH};
}

// ---------------------------------------------------------------------------
// Worker Functionality Tests
// ---------------------------------------------------------------------------
TEST_CASE("Worker can process a chunk", "[worker]")
{
    constexpr std::size_t rows = 3, cols = 4;
    Dataset ds(rows, cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        ds.data()[i] = static_cast<double>(i) * 0.1 - 0.5;

    auto worker = make_worker();
    std::size_t resultCols = 0;
    auto results = worker(0, DataView{ds.data(), rows, cols}, resultCols);

    REQUIRE(resultCols > 0);
    REQUIRE(results.size() == rows * resultCols);
    for (double v : results)
    {
        REQUIRE(v >= -1.0);
        REQUIRE(v <= 1.0);
    }
}

TEST_CASE("Worker returns correct result columns", "[worker]")
{
    constexpr std::size_t rows = 5, cols = 6;
    Dataset ds(rows, cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        ds.data()[i] = 0.1 * (i % cols);

    auto worker = make_worker();
    std::size_t resultCols = 0;
    worker(0, DataView{ds.data(), rows, cols}, resultCols);

    REQUIRE(resultCols == cols);
}

TEST_CASE("Worker handles single row", "[worker]")
{
    constexpr std::size_t cols = 4;
    Dataset ds(1, cols);
    for (std::size_t i = 0; i < cols; ++i)
        ds.data()[i] = 0.2 * i;

    auto worker = make_worker();
    std::size_t resultCols = 0;
    auto results = worker(0, DataView{ds.data(), 1, cols}, resultCols);

    REQUIRE(results.size() == resultCols);
}

TEST_CASE("Worker is deterministic", "[worker]")
{
    constexpr std::size_t rows = 3, cols = 4;
    std::vector<double> data(rows * cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        data[i] = static_cast<double>(i) * 0.1 - 0.5;

    auto worker = make_worker();
    std::size_t rc1 = 0, rc2 = 0;
    auto r1 = worker(0, DataView{data.data(), rows, cols}, rc1);
    auto r2 = worker(1, DataView{data.data(), rows, cols}, rc2);

    REQUIRE(rc1 == rc2);
    REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < r1.size(); ++i)
        REQUIRE(r1[i] == Catch::Approx(r2[i]).epsilon(1e-6));
}

// ---------------------------------------------------------------------------
// parallel_map utility tests
// ---------------------------------------------------------------------------
TEST_CASE("parallel_map processes data", "[parallel_map]")
{
    constexpr std::size_t rows = 6, cols = 4;
    Dataset ds(rows, cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        ds.data()[i] = static_cast<double>(i) * 0.1;

    auto worker = make_worker();
    DataView view{ds.data(), rows, cols};
    auto results = parallel_map(worker, view, 2);

    REQUIRE(results.size() == rows * cols);
}

TEST_CASE("parallel_map with 1 worker is consistent", "[parallel_map]")
{
    constexpr std::size_t rows = 3, cols = 4;
    Dataset ds(rows, cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        ds.data()[i] = 0.1 * i;

    auto worker = make_worker();
    DataView view{ds.data(), rows, cols};

    auto r1 = parallel_map(worker, view, 1);
    auto r2 = parallel_map(worker, view, 1);

    REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < r1.size(); ++i)
        REQUIRE(r1[i] == Catch::Approx(r2[i]).epsilon(1e-6));
}

TEST_CASE("parallel_map with multiple workers", "[parallel_map]")
{
    constexpr std::size_t rows = 8, cols = 4;
    Dataset ds(rows, cols);
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < cols; ++c)
            ds.at(r, c) = 0.1 * (r * cols + c);

    auto worker = make_worker();
    DataView view{ds.data(), rows, cols};

    auto r1 = parallel_map(worker, view, 1);
    auto r2 = parallel_map(worker, view, 4);

    REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < r1.size(); ++i)
        REQUIRE(r1[i] == Catch::Approx(r2[i]).epsilon(1e-6));
}

TEST_CASE("parallel_map scales to many workers", "[parallel_map]")
{
    constexpr std::size_t rows = 20, cols = 4;
    Dataset ds(rows, cols);
    for (std::size_t i = 0; i < rows * cols; ++i)
        ds.data()[i] = 0.05 * i;

    auto worker = make_worker();
    DataView view{ds.data(), rows, cols};

    for (auto nw : {1, 2, 4, 8})
    {
        auto results = parallel_map(worker, view, nw);
        REQUIRE(results.size() == rows * cols);
    }
}
