#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

class WorkerProcess {
public:
    WorkerProcess(std::string pythonExe, std::string scriptPath);
    ~WorkerProcess();

    WorkerProcess(const WorkerProcess&) = delete;
    WorkerProcess& operator=(const WorkerProcess&) = delete;

    bool start();
    bool stop();

    std::vector<double> process_chunk(
        std::size_t taskId,
        const double* data,
        std::size_t rows,
        std::size_t cols
    );

private:
    bool write_exact(const void* buf, std::size_t len);
    bool read_exact(void* buf, std::size_t len);

    std::string pythonExe_;
    std::string scriptPath_;
    pid_t childPid_;
    int childInFd_;
    int childOutFd_;
};
