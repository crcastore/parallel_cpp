#include "worker_process.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#endif

#include "protocol.h"

// Internal: stateful worker process manager
class WorkerProcessImpl
{
public:
    WorkerProcessImpl(const std::string &pythonExe, const std::string &scriptPath, int cpuToPin);
    ~WorkerProcessImpl();

    WorkerProcessImpl(const WorkerProcessImpl &) = delete;
    WorkerProcessImpl &operator=(const WorkerProcessImpl &) = delete;

    std::vector<double> process_chunk(std::size_t taskId, const DataView &input, std::size_t &resultCols);

private:
    bool write_exact(const void *buf, std::size_t len);
    bool read_exact(void *buf, std::size_t len);

    pid_t childPid_{-1};
    int childInFd_{-1};
    int childOutFd_{-1};
};

WorkerProcessImpl::WorkerProcessImpl(const std::string &pythonExe, const std::string &scriptPath, int cpuToPin)
{
    int toChild[2], fromChild[2];
    const auto close_pipe_pair = [](int (&fds)[2])
    {
        close(fds[0]);
        close(fds[1]);
    };

    if (pipe(toChild) != 0 || pipe(fromChild) != 0)
    {
        throw std::runtime_error("Failed to create pipes for worker process.");
    }

    childPid_ = fork();
    if (childPid_ < 0)
    {
        close_pipe_pair(toChild);
        close_pipe_pair(fromChild);
        throw std::runtime_error("Failed to fork worker process.");
    }

    if (childPid_ == 0)
    {
#ifdef __linux__
        if (cpuToPin >= 0)
        {
            const long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
            if (cpuCount <= 0)
            {
                _exit(126);
            }

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(static_cast<int>(cpuToPin % static_cast<int>(cpuCount)), &cpuset);

            if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
            {
                _exit(126);
            }
        }
#else
        (void)cpuToPin;
#endif

        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1]})
            close(fd);
        execlp(pythonExe.c_str(), pythonExe.c_str(), scriptPath.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    close(toChild[0]);
    close(fromChild[1]);
    childInFd_ = toChild[1];
    childOutFd_ = fromChild[0];
}

WorkerProcessImpl::~WorkerProcessImpl()
{
    if (childPid_ <= 0)
        return;

    if (childInFd_ >= 0)
    {
        MessageHeader quit{static_cast<uint32_t>(Protocol::Magic), kVersion,
                           static_cast<uint16_t>(MessageType::Quit), 0, 0, 0};
        write_exact(&quit, sizeof(quit));
        close(childInFd_);
    }
    if (childOutFd_ >= 0)
        close(childOutFd_);

    waitpid(childPid_, nullptr, 0);
}

bool WorkerProcessImpl::write_exact(const void *buf, std::size_t len)
{
    const auto *p = static_cast<const char *>(buf);
    std::size_t written = 0;
    while (written < len)
    {
        const ssize_t n = write(childInFd_, p + written, len - written);
        if (n < 0 && errno != EINTR)
            return false;
        if (n > 0)
            written += static_cast<std::size_t>(n);
    }
    return true;
}

bool WorkerProcessImpl::read_exact(void *buf, std::size_t len)
{
    auto *p = static_cast<char *>(buf);
    std::size_t got = 0;
    while (got < len)
    {
        const ssize_t n = read(childOutFd_, p + got, len - got);
        if (n < 0 && errno != EINTR)
            return false;
        if (n > 0)
            got += static_cast<std::size_t>(n);
    }
    return true;
}

std::vector<double> WorkerProcessImpl::process_chunk(std::size_t taskId, const DataView &input, std::size_t &resultCols)
{
    const MessageHeader req{static_cast<uint32_t>(Protocol::Magic), kVersion,
                            static_cast<uint16_t>(MessageType::Task),
                            static_cast<uint64_t>(taskId),
                            static_cast<uint64_t>(input.rows),
                            static_cast<uint64_t>(input.cols)};
    if (!write_exact(&req, sizeof(req)))
        throw std::runtime_error("Failed to write task header to worker.");

    const std::size_t total = input.rows * input.cols;
    if (total > 0 && !write_exact(input.data, total * sizeof(double)))
        throw std::runtime_error("Failed to write task payload to worker.");

    MessageHeader resp{};
    if (!read_exact(&resp, sizeof(resp)))
        throw std::runtime_error("Worker closed output unexpectedly.");

    if (resp.magic != static_cast<uint32_t>(Protocol::Magic) || resp.version != kVersion || resp.taskId != taskId)
        throw std::runtime_error("Invalid response header from worker.");

    if (resp.type == static_cast<uint16_t>(MessageType::Error))
    {
        std::string errMsg(resp.rows, '\0');
        if (!read_exact(errMsg.data(), resp.rows))
            throw std::runtime_error("Failed to read error message from worker.");
        throw std::runtime_error("Worker error: " + errMsg);
    }

    if (resp.type != static_cast<uint16_t>(MessageType::Result))
        throw std::runtime_error("Unexpected message type from worker.");

    resultCols = resp.cols;
    const std::size_t nValues = resp.rows * resultCols;
    std::vector<double> results(nValues);
    if (nValues > 0 && !read_exact(results.data(), nValues * sizeof(double)))
        throw std::runtime_error("Failed to read results from worker.");

    return results;
}

// Public API: Worker struct with callable operator
std::vector<double> Worker::operator()(std::size_t taskId, const DataView &input, std::size_t &resultCols) const
{
    static thread_local std::unique_ptr<WorkerProcessImpl> impl;
    static thread_local std::string activePythonExe;
    static thread_local std::string activeScriptPath;
    static thread_local bool activePinWorkersToSingleCpu = false;
    static thread_local std::size_t activeCpuStart = 0;
    static thread_local std::size_t activeCpuStride = 1;
    static thread_local int activeCpuToPin = -1;

    int desiredCpuToPin = -1;
    if (pinWorkersToSingleCpu)
    {
        const std::size_t candidate = cpuStart + taskId * cpuStride;
        if (candidate > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw std::runtime_error("Requested CPU index exceeds supported range.");
        }
        desiredCpuToPin = static_cast<int>(candidate);
    }

    const bool configChanged =
        (activePythonExe != pythonExe) ||
        (activeScriptPath != scriptPath) ||
        (activePinWorkersToSingleCpu != pinWorkersToSingleCpu) ||
        (activeCpuStart != cpuStart) ||
        (activeCpuStride != cpuStride) ||
        (activeCpuToPin != desiredCpuToPin);

    if (!impl || configChanged)
    {
        impl = std::make_unique<WorkerProcessImpl>(pythonExe, scriptPath, desiredCpuToPin);
        activePythonExe = pythonExe;
        activeScriptPath = scriptPath;
        activePinWorkersToSingleCpu = pinWorkersToSingleCpu;
        activeCpuStart = cpuStart;
        activeCpuStride = cpuStride;
        activeCpuToPin = desiredCpuToPin;
    }

    return impl->process_chunk(taskId, input, resultCols);
}
