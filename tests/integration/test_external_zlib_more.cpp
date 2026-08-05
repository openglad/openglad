#include <cstring>
#include <string>
#include <vector>

#include "zlib.h"

#include <gtest/gtest.h>

static bool deflate_all(const std::string& payload,
                        int level,
                        int windowBits,
                        int memLevel,
                        int strategy,
                        int flush_mode,
                        std::vector<unsigned char>* out_buf)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit2(&zs, level, Z_DEFLATED, windowBits, memLevel, strategy);
    if (rc != Z_OK)
        return false;

    std::vector<unsigned char> out;
    out.resize(compressBound((uLong)payload.size()));

    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = out.data();
    zs.avail_out = (uInt)out.size();

    rc = deflate(&zs, flush_mode);
    if (rc != Z_STREAM_END)
    {
        deflateEnd(&zs);
        return false;
    }

    const size_t used = out.size() - zs.avail_out;
    deflateEnd(&zs);
    out.resize(used);
    if (out_buf)
        *out_buf = std::move(out);
    return true;
}

static bool inflate_all(const std::vector<unsigned char>& compressed,
                        size_t out_size,
                        int windowBits,
                        std::string* out_str)
{
    z_stream is;
    memset(&is, 0, sizeof(is));
    int rc = inflateInit2(&is, windowBits);
    if (rc != Z_OK)
        return false;

    std::string out;
    out.resize(out_size);
    is.next_in = (Bytef*)compressed.data();
    is.avail_in = (uInt)compressed.size();
    is.next_out = (Bytef*)out.data();
    is.avail_out = (uInt)out.size();

    rc = inflate(&is, Z_FINISH);
    if (rc != Z_STREAM_END)
    {
        inflateEnd(&is);
        return false;
    }
    inflateEnd(&is);
    if (out_str)
        *out_str = std::move(out);
    return true;
}

TEST(ExternalZlibMore, external_zlib_raw_deflate_inflate_roundtrip)
{
    const std::string payload(4096, 'B');
    // raw deflate stream (negative windowBits).
    std::vector<unsigned char> comp;
    ASSERT_TRUE(deflate_all(payload, Z_BEST_SPEED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY, Z_FINISH, &comp)) << "raw deflate should succeed";
    std::string out;
    ASSERT_TRUE(inflate_all(comp, payload.size(), -MAX_WBITS, &out)) << "raw inflate should succeed";
    ASSERT_TRUE(out == payload) << "raw deflate/inflate roundtrip";
}


TEST(ExternalZlibMore, external_zlib_gzip_wrapper_deflate_inflate_roundtrip)
{
    const std::string payload =
        "gzip wrapper test payload\n"
        "gzip wrapper test payload\n"
        "gzip wrapper test payload\n";
    // gzip stream (MAX_WBITS + 16).
    std::vector<unsigned char> comp;
    ASSERT_TRUE(deflate_all(payload, Z_DEFAULT_COMPRESSION, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY, Z_FINISH, &comp)) << "gzip deflate should succeed";
    std::string out;
    ASSERT_TRUE(inflate_all(comp, payload.size(), MAX_WBITS + 16, &out)) << "gzip inflate should succeed";
    ASSERT_TRUE(out == payload) << "gzip wrapper deflate/inflate roundtrip";
}


TEST(ExternalZlibMore, external_zlib_streaming_small_buffers_and_resets)
{
    const std::string payload(16384, 'C');

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit(&zs, Z_DEFAULT_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit";

    std::vector<unsigned char> compressed;
    compressed.resize(4096);

    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    size_t produced_total = 0;

    while (true)
    {
        if (produced_total == compressed.size())
            compressed.resize(compressed.size() * 2);
        zs.next_out = compressed.data() + produced_total;
        zs.avail_out = (uInt)(compressed.size() - produced_total);

        rc = deflate(&zs, zs.avail_in ? Z_SYNC_FLUSH : Z_FINISH);
        produced_total = compressed.size() - zs.avail_out;
        if (rc == Z_STREAM_END)
            break;
        ASSERT_TRUE(rc == Z_OK) << "deflate should continue with small buffers";
    }

    compressed.resize(produced_total);
    ASSERT_TRUE(deflateReset(&zs) == Z_OK) << "deflateReset";
    deflateEnd(&zs);

    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit(&is);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit";

    std::string out;
    out.resize(payload.size());
    is.next_in = (Bytef*)compressed.data();
    is.avail_in = (uInt)compressed.size();

    size_t consumed_in = 0;
    size_t produced_out = 0;

    while (true)
    {
        is.next_in = (Bytef*)compressed.data() + consumed_in;
        is.avail_in = (uInt)(compressed.size() - consumed_in);
        is.next_out = (Bytef*)out.data() + produced_out;
        is.avail_out = (uInt)(out.size() - produced_out);

        rc = inflate(&is, Z_NO_FLUSH);
        consumed_in = compressed.size() - is.avail_in;
        produced_out = out.size() - is.avail_out;

        if (rc == Z_STREAM_END)
            break;
        ASSERT_TRUE(rc == Z_OK) << "inflate should continue with small buffers";
    }

    ASSERT_TRUE(out == payload) << "inflate streaming output should match";
    ASSERT_TRUE(inflateReset(&is) == Z_OK) << "inflateReset";
    inflateEnd(&is);
}


TEST(ExternalZlibMore, external_zlib_dictionary_roundtrip)
{
    const std::string dict = "common-prefix-dict";
    const std::string payload = dict + std::string(2048, 'D');

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit(&zs, Z_BEST_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit";
    ASSERT_TRUE(deflateSetDictionary(&zs, (const Bytef*)dict.data(), (uInt)dict.size()) == Z_OK) << "deflateSetDictionary";

    std::vector<unsigned char> compressed;
    compressed.resize(compressBound((uLong)payload.size()));
    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = compressed.data();
    zs.avail_out = (uInt)compressed.size();
    rc = deflate(&zs, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "deflate should end";
    const size_t used = compressed.size() - zs.avail_out;
    deflateEnd(&zs);
    compressed.resize(used);

    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit(&is);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit";

    std::string out;
    out.resize(payload.size());
    is.next_in = (Bytef*)compressed.data();
    is.avail_in = (uInt)compressed.size();
    is.next_out = (Bytef*)out.data();
    is.avail_out = (uInt)out.size();

    rc = inflate(&is, Z_FINISH);
    if (rc == Z_NEED_DICT)
    {
        ASSERT_TRUE(inflateSetDictionary(&is, (const Bytef*)dict.data(), (uInt)dict.size()) == Z_OK) << "inflateSetDictionary";
        rc = inflate(&is, Z_FINISH);
    }
    ASSERT_TRUE(rc == Z_STREAM_END) << "inflate should end with dictionary";
    inflateEnd(&is);

    ASSERT_TRUE(out == payload) << "dictionary roundtrip payload should match";
}

