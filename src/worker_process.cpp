#include "worker_process.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "protocol.h"

// Helper: close a file descriptor if open, then set to -1.
static void close_fd(int &fd)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

WorkerProcess::WorkerProcess(const std::string &pythonExe, const std::string &scriptPath)
    : pythonExe_(pythonExe), scriptPath_(scriptPath)
{
    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0 || pipe(fromChild) != 0)
    {
        throw std::runtime_error("Failed to create pipes for worker process.");
    }

    childPid_ = fork();
    if (childPid_ < 0)
    {
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        throw std::runtime_error("Failed to fork worker process.");
    }

    if (childPid_ == 0)
    {
        // Child: redirect stdin/stdout and exec Python.
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1]})
            close(fd);
        execlp(pythonExe_.c_str(), pythonExe_.c_str(), scriptPath_.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    // Parent: close child-side pipe ends.
    close(toChild[0]);
    close(fromChild[1]);
    childInFd_ = toChild[1];
    childOutFd_ = fromChild[0];
}

WorkerProcess::~WorkerProcess() { stop(); }

bool WorkerProcess::stop()
{
    if (childPid_ <= 0)
        return true;

    // Send quit message before closing pipes.
    if (childInFd_ >= 0)
    {
        MessageHeader quit{static_cast<uint32_t>(Protocol::Magic), kVersion,
                           static_cast<uint16_t>(MessageType::Quit), 0, 0, 0};
        write_exact(&quit, sizeof(quit));
    }
    close_fd(childInFd_);
    close_fd(childOutFd_);

    int status = 0;
    pid_t result = waitpid(childPid_, &status, 0);
    childPid_ = -1;
    return result > 0;
}

bool WorkerProcess::write_exact(const void *buf, std::size_t len)
{
    auto p = static_cast<const char *>(buf);
    std::size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(childInFd_, p + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool WorkerProcess::read_exact(void *buf, std::size_t len)
{
    auto p = static_cast<char *>(buf);
    std::size_t got = 0;
    while (got < len)
    {
        ssize_t n = read(childOutFd_, p + got, len - got);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

std::vector<double> WorkerProcess::process_chunk(std::size_t taskId, const DataView &input, std::size_t &resultCols)
{
    if (childPid_ <= 0 || childInFd_ < 0 || childOutFd_ < 0)
        throw std::runtime_error("Worker is not running.");

    // Send task header + payload.
    MessageHeader req{static_cast<uint32_t>(Protocol::Magic), kVersion,
                      static_cast<uint16_t>(MessageType::Task),
                      static_cast<uint64_t>(taskId),
                      static_cast<uint64_t>(input.rows),
                      static_cast<uint64_t>(input.cols)};
    if (!write_exact(&req, sizeof(req)))
        throw std::runtime_error("Failed to write task header to worker.");

    auto total = input.rows * input.cols;
    if (total > 0 && !write_exact(input.data, total * sizeof(double)))
        throw std::runtime_error("Failed to write task payload to worker.");

    // Read response header.
    MessageHeader resp{};
    if (!read_exact(&resp, sizeof(resp)))
        throw std::runtime_error("Worker closed output unexpectedly.");

    if (resp.magic != static_cast<uint32_t>(Protocol::Magic) || resp.version != kVersion)
        throw std::runtime_error("Invalid worker response header.");
    if (resp.taskId != static_cast<uint64_t>(taskId))
        throw std::runtime_error("Worker response task ID mismatch.");

    // Handle response body.
    auto msgType = static_cast<MessageType>(resp.type);
    if (msgType == MessageType::Error)
    {
        auto msgLen = static_cast<std::size_t>(resp.cols);
        std::string msg(msgLen, '\0');
        if (msgLen > 0 && !read_exact(&msg[0], msgLen))
            throw std::runtime_error("Failed to read worker error message.");
        throw std::runtime_error("Worker error: " + msg);
    }

    if (msgType != MessageType::Result)
        throw std::runtime_error("Unexpected worker response type.");

    if (resp.rows != static_cast<uint64_t>(input.rows))
        throw std::runtime_error("Worker response row count mismatch.");

    resultCols = static_cast<std::size_t>(resp.cols);
    if (resultCols == 0)
        throw std::runtime_error("Worker returned zero result columns.");

    auto totalResults = input.rows * resultCols;
    std::vector<double> values(totalResults);
    if (totalResults > 0 && !read_exact(values.data(), totalResults * sizeof(double)))
        throw std::runtime_error("Failed to read worker result payload.");

    return values;
}
