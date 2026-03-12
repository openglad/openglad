#include <cstring>
#include <string>
#include <vector>

#include "zlib.h"

#include "test_framework.h"

TEST(ExternalZlib, compress2_uncompress_roundtrip)
{
    const std::string payload =
        "The quick brown fox jumps over the lazy dog.\n"
        "The quick brown fox jumps over the lazy dog.\n"
        "The quick brown fox jumps over the lazy dog.\n";

    uLong src_len = static_cast<uLong>(payload.size());
    uLongf bound = compressBound(src_len);
    std::vector<unsigned char> compressed(bound);

    uLongf comp_len = bound;
    int rc = compress2(compressed.data(), &comp_len,
                       reinterpret_cast<const unsigned char*>(payload.data()),
                       src_len, Z_BEST_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "compress2 should succeed";
    compressed.resize(static_cast<size_t>(comp_len));

    // The bundled zlib subset does not compile uncompr.c (uncompress()),
    // so decode via streaming inflate instead.
    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit(&is);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit";

    std::vector<unsigned char> out(src_len);
    is.next_in = compressed.data();
    is.avail_in = (uInt)compressed.size();
    is.next_out = out.data();
    is.avail_out = (uInt)out.size();

    rc = inflate(&is, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "inflate Z_FINISH should end stream";
    ASSERT_TRUE(is.avail_out == 0) << "inflate should fill expected output size";
    inflateEnd(&is);

    ASSERT_TRUE(memcmp(out.data(), payload.data(), src_len) == 0) << "roundtrip payload should match";
}


TEST(ExternalZlib, deflate_inflate_streaming)
{
    // Exercise streaming deflate/inflate code paths.
    const std::string payload(8192, 'A');
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    int rc = deflateInit(&zs, Z_DEFAULT_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit";

    std::vector<unsigned char> compressed;
    compressed.resize(16384);

    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = compressed.data();
    zs.avail_out = (uInt)compressed.size();

    rc = deflate(&zs, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "deflate Z_FINISH should end stream";
    size_t comp_size = compressed.size() - zs.avail_out;
    deflateEnd(&zs);
    compressed.resize(comp_size);

    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit(&is);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit";

    std::vector<unsigned char> out;
    out.resize(payload.size());
    is.next_in = compressed.data();
    is.avail_in = (uInt)compressed.size();
    is.next_out = out.data();
    is.avail_out = (uInt)out.size();

    rc = inflate(&is, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "inflate Z_FINISH should end stream";
    inflateEnd(&is);

    ASSERT_TRUE(memcmp(out.data(), payload.data(), payload.size()) == 0) << "inflate output should match";
}

