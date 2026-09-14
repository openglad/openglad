#pragma once

// Shared zlib codec for the snapshot wire tests.
//
// Every caller here is a test that decodes a payload the PRODUCT compressed
// (or compresses one for the product to reject), so the failure these tests
// exist to catch is a truncated or corrupt payload. Four hand-rolled copies
// of this loop used to live in the snapshot test files; three of them drove
// a do/while whose ONLY exit was the end-of-stream code, with a non-fatal
// EXPECT_EQ on the error rc, so a Z_DATA_ERROR/Z_BUF_ERROR stream spun forever
// and the regression was reported as a 600 s ctest timeout, not a message.
// One copy, one shape: throw std::runtime_error on any non-success rc.

#include "zlib.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace og::test_zlib
{

inline std::vector<std::uint8_t> inflate_for_test(const std::uint8_t* data,
                                                  std::size_t size)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("test inflate setup failed");

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 512> chunk{};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END)
        {
            inflateEnd(&stream);
            throw std::runtime_error("test inflate failed");
        }
        output.insert(output.end(), chunk.begin(),
                      chunk.begin() +
                          static_cast<std::ptrdiff_t>(chunk.size() -
                                                      stream.avail_out));
    } while (rc != Z_STREAM_END);

    if (inflateEnd(&stream) != Z_OK)
        throw std::runtime_error("test inflate teardown failed");
    return output;
}

inline std::vector<std::uint8_t> deflate_for_test(
    const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> compressed(
        compressBound(static_cast<uLong>(payload.size())));
    uLongf compressed_size = static_cast<uLongf>(compressed.size());
    const int rc = compress2(compressed.data(),
                             &compressed_size,
                             payload.data(),
                             static_cast<uLong>(payload.size()),
                             Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK)
        throw std::runtime_error("test deflate failed");
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

}  // namespace og::test_zlib
