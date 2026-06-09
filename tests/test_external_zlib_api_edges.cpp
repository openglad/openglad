#include <cstring>
#include <string>
#include <vector>

#include "zlib.h"

#include <gtest/gtest.h>

TEST(ExternalZlibApiEdges, external_zlib_deflate_advanced_api_paths)
{
    const std::string payload(4096, 'Z');

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit2 should succeed";

    ASSERT_TRUE(deflateTune(&zs, 4, 4, 16, 16) == Z_OK) << "deflateTune should succeed";
    ASSERT_TRUE(deflateParams(&zs, Z_BEST_SPEED, Z_FILTERED) == Z_OK) << "deflateParams should succeed";
    ASSERT_TRUE(deflateBound(&zs, (uLong)payload.size()) > 0) << "deflateBound should return upper bound";

    std::vector<unsigned char> out(compressBound((uLong)payload.size()));
    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = out.data();
    zs.avail_out = (uInt)out.size();

    rc = deflate(&zs, Z_NO_FLUSH);
    ASSERT_TRUE(rc == Z_OK) << "deflate Z_NO_FLUSH should continue";

    rc = deflate(&zs, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "deflate Z_FINISH should end";
    ASSERT_TRUE(deflateEnd(&zs) == Z_OK) << "deflateEnd should succeed";

    // deflateCopy path on a clean initialized stream.
    z_stream src_copy;
    memset(&src_copy, 0, sizeof(src_copy));
    rc = deflateInit(&src_copy, Z_DEFAULT_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit for copy source";
    z_stream dst_copy;
    memset(&dst_copy, 0, sizeof(dst_copy));
    ASSERT_TRUE(deflateCopy(&dst_copy, &src_copy) == Z_OK) << "deflateCopy should succeed";
    ASSERT_TRUE(deflateEnd(&dst_copy) == Z_OK) << "deflateEnd(copy) should succeed";
    ASSERT_TRUE(deflateEnd(&src_copy) == Z_OK) << "deflateEnd(source copy stream) should succeed";

    // deflatePrime path (raw deflate stream).
    z_stream raw;
    memset(&raw, 0, sizeof(raw));
    rc = deflateInit2(&raw, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    ASSERT_TRUE(rc == Z_OK) << "raw deflateInit2 should succeed";
    ASSERT_TRUE(deflatePrime(&raw, 3, 0x5) == Z_OK) << "deflatePrime should accept small bit prefix";
    (void)deflateEnd(&raw);
}


TEST(ExternalZlibApiEdges, external_zlib_inflate_header_copy_prime_and_invalid_init)
{
    const std::string payload = "gzip header coverage payload\n";

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
    ASSERT_TRUE(rc == Z_OK) << "gzip deflateInit2 should succeed";

    std::vector<unsigned char> comp(512);
    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = comp.data();
    zs.avail_out = (uInt)comp.size();
    rc = deflate(&zs, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "gzip deflate finish";
    const size_t comp_size = comp.size() - zs.avail_out;
    ASSERT_TRUE(deflateEnd(&zs) == Z_OK) << "deflateEnd should succeed";
    comp.resize(comp_size);

    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit2(&is, MAX_WBITS + 16);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit2(gzip) should succeed";

    gz_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    Bytef extra[64] = {};
    Bytef name[64] = {};
    Bytef comment[64] = {};
    hdr.extra = extra;
    hdr.extra_max = sizeof(extra);
    hdr.name = name;
    hdr.name_max = sizeof(name);
    hdr.comment = comment;
    hdr.comm_max = sizeof(comment);
    ASSERT_TRUE(inflateGetHeader(&is, &hdr) == Z_OK) << "inflateGetHeader should succeed";

    z_stream copy;
    memset(&copy, 0, sizeof(copy));
    ASSERT_TRUE(inflateCopy(&copy, &is) == Z_OK) << "inflateCopy should succeed";
    ASSERT_TRUE(inflateEnd(&copy) == Z_OK) << "inflateEnd(copy) should succeed";

    std::string out(payload.size(), '\0');
    is.next_in = comp.data();
    is.avail_in = (uInt)comp.size();
    is.next_out = (Bytef*)out.data();
    is.avail_out = (uInt)out.size();
    rc = inflate(&is, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "inflate should finish gzip stream";
    ASSERT_TRUE(out == payload) << "inflated payload should match";
    const int sync_point = inflateSyncPoint(&is);
    ASSERT_EQ(0, sync_point) << "finished gzip stream should not report an active sync point";
    ASSERT_TRUE(inflateEnd(&is) == Z_OK) << "inflateEnd should succeed";

    z_stream raw;
    memset(&raw, 0, sizeof(raw));
    rc = inflateInit2(&raw, -MAX_WBITS);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit2(raw) should succeed";
    ASSERT_TRUE(inflatePrime(&raw, 2, 0x3) == Z_OK) << "inflatePrime should accept pending bits";
    ASSERT_TRUE(inflateEnd(&raw) == Z_OK) << "inflateEnd(raw) should succeed";

    z_stream bad;
    memset(&bad, 0, sizeof(bad));
    ASSERT_TRUE(deflateInit2(&bad, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 100, 8, Z_DEFAULT_STRATEGY) == Z_STREAM_ERROR) << "invalid deflate windowBits should fail";
    ASSERT_TRUE(inflateInit2(&bad, 100) == Z_STREAM_ERROR) << "invalid inflate windowBits should fail";
}


TEST(ExternalZlibApiEdges, external_zlib_inflate_sync_on_corrupted_stream)
{
    const std::string payload =
        "sync-path-data-1\n"
        "sync-path-data-2\n"
        "sync-path-data-3\n";

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int rc = deflateInit(&zs, Z_DEFAULT_COMPRESSION);
    ASSERT_TRUE(rc == Z_OK) << "deflateInit should succeed";

    std::vector<unsigned char> comp(1024);
    zs.next_in = (Bytef*)payload.data();
    zs.avail_in = (uInt)payload.size();
    zs.next_out = comp.data();
    zs.avail_out = (uInt)comp.size();

    rc = deflate(&zs, Z_FULL_FLUSH);  // create a sync point
    ASSERT_TRUE(rc == Z_OK) << "deflate full flush should succeed";
    rc = deflate(&zs, Z_FINISH);
    ASSERT_TRUE(rc == Z_STREAM_END) << "deflate finish should succeed";
    const size_t used = comp.size() - zs.avail_out;
    ASSERT_TRUE(deflateEnd(&zs) == Z_OK) << "deflateEnd should succeed";
    comp.resize(used);

    // Corrupt prefix to force an error, then try to recover with inflateSync.
    for (size_t i = 0; i < comp.size() && i < 8; ++i)
        comp[i] ^= 0xFF;

    z_stream is;
    memset(&is, 0, sizeof(is));
    rc = inflateInit(&is);
    ASSERT_TRUE(rc == Z_OK) << "inflateInit should succeed";

    std::vector<unsigned char> out(payload.size());
    is.next_in = comp.data();
    is.avail_in = (uInt)comp.size();
    is.next_out = out.data();
    is.avail_out = (uInt)out.size();

    rc = inflate(&is, Z_NO_FLUSH);
    ASSERT_EQ(Z_DATA_ERROR, rc) << "corrupted stream should report a data error before sync";

    int sync_rc = inflateSync(&is);
    ASSERT_EQ(Z_OK, sync_rc) << "inflateSync should recover to the full-flush point";
    const int sp = inflateSyncPoint(&is);
    ASSERT_EQ(0, sp) << "recovered stream should not report another pending sync point";

    (void)inflateEnd(&is);
}
