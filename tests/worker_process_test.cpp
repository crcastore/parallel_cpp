#include <catch2/catch_test_macros.hpp>
#include "worker_process.h"

TEST_CASE("WorkerProcess start and stop", "[worker]")
{
    // The script path is provided via a compile‑time definition WORKER_SCRIPT_PATH
    WorkerProcess wp("python3", WORKER_SCRIPT_PATH);
    REQUIRE(wp.start());
    // Stop the worker; should return true and clean up resources.
    REQUIRE(wp.stop());
}
