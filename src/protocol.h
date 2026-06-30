#pragma once
#include <cstdint>

enum class Protocol : std::uint32_t
{
    Magic = 0x50595043 // "CPYP"
};

constexpr std::uint16_t kVersion{1};

enum class MessageType : std::uint16_t
{
    Task = 1,
    Quit = 2,
    Result = 3,
    Error = 4
};

#pragma pack(push, 1)
struct MessageHeader
{
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t type{};
    std::uint64_t taskId{};
    std::uint64_t rows{};
    std::uint64_t cols{};
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 32);
