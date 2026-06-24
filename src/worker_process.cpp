#include "worker_process.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

// Protocol definitions are now centralized in protocol.h
#include "protocol.h"

WorkerProcess::WorkerProcess(std::string pythonExe, std::string scriptPath)
    : pythonExe_(std::move(pythonExe)),
      scriptPath_(std::move(scriptPath))
{
    if (!start())
    {
        throw std::runtime_error("Failed to start Python worker process.");
    }
}

WorkerProcess::~WorkerProcess()
{
    stop();
}

bool WorkerProcess::start()
{
    if (childPid_ > 0)
    {
        return true;
    }

    int toChild[2];
    int fromChild[2];

    if (pipe(toChild) != 0)
    {
        return false;
    }
    if (pipe(fromChild) != 0)
    {
        close(toChild[0]);
        close(toChild[1]);
        return false;
    }

    childPid_ = fork();
    if (childPid_ < 0)
    {
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        childPid_ = -1;
        return false;
    }

    if (childPid_ == 0)
    {
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);

        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);

        execlp(
            pythonExe_.c_str(),
            pythonExe_.c_str(),
            scriptPath_.c_str(),
            static_cast<char *>(nullptr));
        _exit(127);
    }

    close(toChild[0]);
    close(fromChild[1]);

    childInFd_ = toChild[1];
    childOutFd_ = fromChild[0];
    return true;
}

bool WorkerProcess::stop()
{
    if (childPid_ <= 0)
    {
        return true;
    }

    if (childInFd_ >= 0)
    {
        const MessageHeader quit{static_cast<std::uint32_t>(Protocol::Magic), kVersion,
                                 static_cast<std::uint16_t>(MessageType::Quit), 0, 0, 0};
        (void)write_exact(&quit, sizeof(quit));
        close(childInFd_);
        childInFd_ = -1;
    }

    if (childOutFd_ >= 0)
    {
        close(childOutFd_);
        childOutFd_ = -1;
    }

    int status = 0;
    int waitResult = waitpid(childPid_, &status, 0);
    childPid_ = -1;

    return waitResult > 0;
}

bool WorkerProcess::write_exact(const void *buf, std::size_t len)
{
    const char *p = static_cast<const char *>(buf);
    std::size_t total = 0;
    while (total < len)
    {
        const ssize_t wrote = write(childInFd_, p + total, len - total);
        if (wrote < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (wrote == 0)
        {
            return false;
        }
        total += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool WorkerProcess::read_exact(void *buf, std::size_t len)
{
    char *p = static_cast<char *>(buf);
    std::size_t total = 0;
    while (total < len)
    {
        const ssize_t got = read(childOutFd_, p + total, len - total);
        if (got < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (got == 0)
        {
            return false;
        }
        total += static_cast<std::size_t>(got);
    }
    return true;
}

std::vector<double> WorkerProcess::process_chunk(
    std::size_t taskId,
    const DataView &input,
    std::size_t &resultCols)
{
    if (childPid_ <= 0 || childInFd_ < 0 || childOutFd_ < 0)
    {
        throw std::runtime_error("Worker is not started.");
    }

    const MessageHeader request{
        static_cast<std::uint32_t>(Protocol::Magic),
        kVersion,
        static_cast<std::uint16_t>(MessageType::Task),
        static_cast<std::uint64_t>(taskId),
        static_cast<std::uint64_t>(input.rows),
        static_cast<std::uint64_t>(input.cols)};
    if (!write_exact(&request, sizeof(request)))
    {
        throw std::runtime_error("Failed to write task header to worker.");
    }

    const std::size_t total = input.rows * input.cols;
    if (total > 0 && !write_exact(input.data, total * sizeof(double)))
    {
        throw std::runtime_error("Failed to write task payload to worker.");
    }

    MessageHeader response{};
    if (!read_exact(&response, sizeof(response)))
    {
        throw std::runtime_error("Worker closed output unexpectedly.");
    }

    if (response.magic != static_cast<std::uint32_t>(Protocol::Magic) || response.version != kVersion)
    {
        throw std::runtime_error("Invalid worker response protocol header.");
    }

    if (response.taskId != static_cast<std::uint64_t>(taskId))
    {
        throw std::runtime_error("Worker response task id mismatch.");
    }

    return handle_response(response, input, resultCols);
}

std::vector<double> WorkerProcess::handle_response(
    const MessageHeader &response,
    const DataView &input,
    std::size_t &resultCols)
{
    const auto message_type = static_cast<MessageType>(response.type);
    switch (message_type)
    {
    case MessageType::Error:
    {
        const std::size_t msgLen = static_cast<std::size_t>(response.colsOrAux);
        std::string msg(msgLen, '\0');
        if (msgLen > 0 && !read_exact(&msg[0], msgLen))
        {
            throw std::runtime_error("Failed to read worker error message.");
        }
        throw std::runtime_error("Worker error: " + msg);
    }

    case MessageType::Result:
    {
        if (response.rows != static_cast<std::uint64_t>(input.rows))
        {
            throw std::runtime_error("Worker response row count mismatch.");
        }

        resultCols = static_cast<std::size_t>(response.colsOrAux);
        if (resultCols == 0)
        {
            throw std::runtime_error("Worker response has zero result columns.");
        }

        const std::size_t totalResults = input.rows * resultCols;
        std::vector<double> values(totalResults, 0.0);
        if (totalResults > 0 && !read_exact(values.data(), totalResults * sizeof(double)))
        {
            throw std::runtime_error("Failed to read worker result payload.");
        }
        return values;
    }

    default:
        throw std::runtime_error("Unexpected worker response type.");
    }
}
