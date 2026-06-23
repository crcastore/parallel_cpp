cmake_minimum_required(VERSION 3.16)
project(cpp_parallelism LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
