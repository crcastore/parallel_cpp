#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <cstring>
#include "protocol.h"
#include "dataset.h"

// ---------------------------------------------------------------------------
// Protocol struct layout
// ---------------------------------------------------------------------------
TEST_CASE("MessageHeader is exactly 32 bytes", "[protocol]")
{
    static_assert(sizeof(MessageHeader) == 32, "MessageHeader must be 32 bytes");
    REQUIRE(sizeof(MessageHeader) == 32);
}

TEST_CASE("MessageHeader field offsets are correct", "[protocol]")
{
    REQUIRE(offsetof(MessageHeader, magic) == 0);
    REQUIRE(offsetof(MessageHeader, version) == 4);
    REQUIRE(offsetof(MessageHeader, type) == 6);
    REQUIRE(offsetof(MessageHeader, taskId) == 8);
    REQUIRE(offsetof(MessageHeader, rows) == 16);
    REQUIRE(offsetof(MessageHeader, colsOrAux) == 24);
}

TEST_CASE("MessageHeader round-trips through raw bytes", "[protocol]")
{
    MessageHeader h{};
    h.magic = static_cast<std::uint32_t>(Protocol::Magic);
    h.version = kVersion;
    h.type = static_cast<std::uint16_t>(MessageType::Task);
    h.taskId = 0xDEADBEEFCAFEBABEull;
    h.rows = 1234;
    h.colsOrAux = 8;

    unsigned char buf[32];
    std::memcpy(buf, &h, 32);

    MessageHeader h2{};
    std::memcpy(&h2, buf, 32);

    REQUIRE(h2.magic == h.magic);
    REQUIRE(h2.version == h.version);
    REQUIRE(h2.type == h.type);
    REQUIRE(h2.taskId == h.taskId);
    REQUIRE(h2.rows == h.rows);
    REQUIRE(h2.colsOrAux == h.colsOrAux);
}

// ---------------------------------------------------------------------------
// Protocol magic / version constants
// ---------------------------------------------------------------------------
TEST_CASE("Protocol::Magic encodes bytes 'C','P','Y','P'", "[protocol]")
{
    constexpr std::uint32_t magic = static_cast<std::uint32_t>(Protocol::Magic);
    REQUIRE(magic == 0x50595043u);
    const unsigned char *b = reinterpret_cast<const unsigned char *>(&magic);
    REQUIRE(b[0] == 'C');
    REQUIRE(b[1] == 'P');
    REQUIRE(b[2] == 'Y');
    REQUIRE(b[3] == 'P');
}

TEST_CASE("kVersion is 1", "[protocol]")
{
    REQUIRE(kVersion == 1);
}

// ---------------------------------------------------------------------------
// MessageType enum
// ---------------------------------------------------------------------------
TEST_CASE("MessageType values are correct", "[protocol]")
{
    REQUIRE(static_cast<std::uint16_t>(MessageType::Task) == 1);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Quit) == 2);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Result) == 3);
    REQUIRE(static_cast<std::uint16_t>(MessageType::Error) == 4);
}

TEST_CASE("MessageType values are all distinct", "[protocol]")
{
    const auto task = static_cast<std::uint16_t>(MessageType::Task);
    const auto quit = static_cast<std::uint16_t>(MessageType::Quit);
    const auto result = static_cast<std::uint16_t>(MessageType::Result);
    const auto error = static_cast<std::uint16_t>(MessageType::Error);
    REQUIRE(task != quit);
    REQUIRE(task != result);
    REQUIRE(task != error);
    REQUIRE(quit != result);
    REQUIRE(quit != error);
    REQUIRE(result != error);
}

TEST_CASE("MessageType has uint16_t underlying type", "[protocol]")
{
    static_assert(sizeof(MessageType) == sizeof(std::uint16_t),
                  "MessageType underlying type must be uint16_t");
    REQUIRE(sizeof(MessageType) == sizeof(std::uint16_t));
}

// ---------------------------------------------------------------------------
// RowMajorDataset
// ---------------------------------------------------------------------------
TEST_CASE("RowMajorDataset default-initialises to zero", "[dataset]")
{
    RowMajorDataset ds(4, 3);
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = 0; c < 3; ++c)
            REQUIRE(ds.at(r, c) == 0.0);
}

TEST_CASE("RowMajorDataset rows() and cols() are correct", "[dataset]")
{
    RowMajorDataset ds(7, 5);
    REQUIRE(ds.rows() == 7);
    REQUIRE(ds.cols() == 5);
}

TEST_CASE("RowMajorDataset at() read/write roundtrip", "[dataset]")
{
    RowMajorDataset ds(3, 4);
    ds.at(1, 2) = 3.14;
    REQUIRE(ds.at(1, 2) == Catch::Approx(3.14));
    REQUIRE(ds.at(1, 1) == 0.0);
    REQUIRE(ds.at(1, 3) == 0.0);
    REQUIRE(ds.at(0, 2) == 0.0);
    REQUIRE(ds.at(2, 2) == 0.0);
}

TEST_CASE("RowMajorDataset at() const overload works", "[dataset]")
{
    RowMajorDataset ds(2, 2);
    ds.at(0, 1) = 9.9;
    const RowMajorDataset &cds = ds;
    REQUIRE(cds.at(0, 1) == Catch::Approx(9.9));
}

TEST_CASE("RowMajorDataset row_ptr points to contiguous row data", "[dataset]")
{
    RowMajorDataset ds(3, 4);
    ds.at(1, 0) = 1.0;
    ds.at(1, 1) = 2.0;
    ds.at(1, 2) = 3.0;
    ds.at(1, 3) = 4.0;

    const double *p = ds.row_ptr(1);
    REQUIRE(p[0] == Catch::Approx(1.0));
    REQUIRE(p[1] == Catch::Approx(2.0));
    REQUIRE(p[2] == Catch::Approx(3.0));
    REQUIRE(p[3] == Catch::Approx(4.0));
}

TEST_CASE("RowMajorDataset storage is row-major (adjacent rows differ by cols)", "[dataset]")
{
    RowMajorDataset ds(3, 5);
    REQUIRE(ds.row_ptr(1) - ds.row_ptr(0) == static_cast<std::ptrdiff_t>(ds.cols()));
    REQUIRE(ds.row_ptr(2) - ds.row_ptr(1) == static_cast<std::ptrdiff_t>(ds.cols()));
}

TEST_CASE("RowMajorDataset 1x1 edge case", "[dataset]")
{
    RowMajorDataset ds(1, 1);
    REQUIRE(ds.rows() == 1);
    REQUIRE(ds.cols() == 1);
    ds.at(0, 0) = -7.5;
    REQUIRE(ds.at(0, 0) == Catch::Approx(-7.5));
}

TEST_CASE("RowMajorDataset row_ptr(0) equals &at(0,0)", "[dataset]")
{
    RowMajorDataset ds(2, 3);
    ds.at(0, 0) = 42.0;
    REQUIRE(ds.row_ptr(0) == &ds.at(0, 0));
}
