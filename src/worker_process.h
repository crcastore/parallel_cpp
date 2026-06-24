#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

#include "dataset.h"
#include "protocol.h"

// Manages a child Python process for quantum reservoir computation.
class WorkerProcess
{
public:
    WorkerProcess(const std::string &pythonExe, const std::string &scriptPath);
    ~WorkerProcess();

    WorkerProcess(const WorkerProcess &) = delete;
    WorkerProcess &operator=(const WorkerProcess &) = delete;

    // Send a quit message and wait for the child to exit.
    bool stop();

    // Send data to the worker and return expectation values.
    // Fills resultCols with the number of output columns.
    std::vector<double> process_chunk(std::size_t taskId, const DataView &input, std::size_t &resultCols);

private:
    bool write_exact(const void *buf, std::size_t len);
    bool read_exact(void *buf, std::size_t len);

    std::string pythonExe_;
    std::string scriptPath_;
    pid_t childPid_{-1};
    int childInFd_{-1};
    int childOutFd_{-1};
};
