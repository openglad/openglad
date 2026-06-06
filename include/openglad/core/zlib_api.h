#pragma once

#include <cstddef>

extern "C" {

using Bytef = unsigned char;
using uInt = unsigned int;
using uLong = unsigned long;
using uLongf = unsigned long;
using voidpf = void*;

using alloc_func = voidpf (*)(voidpf opaque, uInt items, uInt size);
using free_func = void (*)(voidpf opaque, voidpf address);

struct internal_state;

struct z_stream_s {
    const Bytef* next_in;
    uInt avail_in;
    uLong total_in;

    Bytef* next_out;
    uInt avail_out;
    uLong total_out;

    const char* msg;
    internal_state* state;

    alloc_func zalloc;
    free_func zfree;
    voidpf opaque;

    int data_type;
    uLong adler;
    uLong reserved;
};

using z_stream = z_stream_s;
using z_streamp = z_stream*;

uLong compressBound(uLong source_len);
int compress2(Bytef* dest,
              uLongf* dest_len,
              const Bytef* source,
              uLong source_len,
              int level);
uLong crc32(uLong crc, const Bytef* buf, uInt len);
int inflateInit_(z_streamp strm, const char* version, int stream_size);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);

} // extern "C"

inline constexpr const char* kZlibVersion = "1.3.1";

inline constexpr int Z_NO_FLUSH = 0;
inline constexpr int Z_OK = 0;
inline constexpr int Z_STREAM_END = 1;
inline constexpr int Z_DEFAULT_COMPRESSION = -1;

inline int inflateInit(z_streamp strm)
{
    return inflateInit_(strm, kZlibVersion, static_cast<int>(sizeof(z_stream)));
}
