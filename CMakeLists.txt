cmake_minimum_required(VERSION 3.16)
project(cpp_parallelism LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(parallel_python
    src/main.cpp
    src/worker_process.cpp
)

target_include_directories(parallel_python PRIVATE src)

target_compile_definitions(parallel_python PRIVATE
    WORKER_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/python/worker.py"
)

if(APPLE)
    target_compile_options(parallel_python PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Catch2 unit tests
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
)
FetchContent_MakeAvailable(catch2)

enable_testing()

# --- unit_tests: ASan + UBSan (release) ---
add_executable(unit_tests
    tests/protocol_test.cpp
    tests/asan_options.cpp
)
target_include_directories(unit_tests PRIVATE src)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain)
target_compile_options(unit_tests PRIVATE -O2 -g -fsanitize=address,undefined)
target_link_options(unit_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME protocol_tests COMMAND unit_tests)

# --- worker_integration_test: ASan + UBSan (release) ---
add_executable(worker_integration_test
    tests/worker_process_test.cpp
    tests/asan_options.cpp
    src/worker_process.cpp
)
target_include_directories(worker_integration_test PRIVATE src)
target_link_libraries(worker_integration_test PRIVATE Catch2::Catch2WithMain)
target_compile_definitions(worker_integration_test PRIVATE
    WORKER_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/python/worker.py")
target_compile_options(worker_integration_test PRIVATE -O2 -g -fsanitize=address,undefined)
target_link_options(worker_integration_test PRIVATE -fsanitize=address,undefined)
add_test(NAME worker_process_test COMMAND worker_integration_test)

# --- worker_tsan_test: TSan + UBSan (release) – catches data races in threaded paths ---
add_executable(worker_tsan_test
    tests/worker_process_test.cpp
    src/worker_process.cpp
)
target_include_directories(worker_tsan_test PRIVATE src)
target_link_libraries(worker_tsan_test PRIVATE Catch2::Catch2WithMain)
target_compile_definitions(worker_tsan_test PRIVATE
    WORKER_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/python/worker.py")
target_compile_options(worker_tsan_test PRIVATE -O2 -g -fsanitize=thread,undefined)
target_link_options(worker_tsan_test PRIVATE -fsanitize=thread,undefined)
add_test(NAME worker_tsan_test COMMAND worker_tsan_test)
