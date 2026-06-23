#include <catch2/catch_test_macros.hpp>
#include "protocol.h"

TEST_CASE("MessageHeader size", "[protocol]")
{
    static_assert(sizeof(MessageHeader) == 32, "MessageHeader must be 32 bytes");
    REQUIRE(sizeof(MessageHeader) == 32);
}

TEST_CASE("Protocol magic value", "[protocol]")
{
    REQUIRE(static_cast<std::uint32_t>(Protocol::Magic) == 0x50595043u);
}

TEST_CASE("MessageType values", "[protocol]")
{
    REQUIRE(static_cast<std::uint16_t>(MessageType::Task) == 1);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Quit) == 2);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Result) == 3);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Error) == 4);
}
