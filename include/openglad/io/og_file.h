/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// SDL-free file I/O abstraction.
// Backed by PhysFS (for mounted archives) or stdio FILE* (for filesystem).

namespace og::io {

class OgFile {
public:
    virtual ~OgFile() = default;

    // Read up to size*count bytes into buf.
    // Returns the number of size-byte objects successfully read.
    virtual std::size_t read(void* buf, std::size_t size, std::size_t count) = 0;

    // Write size*count bytes from buf.
    // Returns the number of size-byte objects successfully written.
    virtual std::size_t write(const void* buf, std::size_t size, std::size_t count) = 0;

    // Seek to offset from whence (0=SET, 1=CUR, 2=END).
    // Returns new position or -1 on error.
    virtual std::int64_t seek(std::int64_t offset, int whence) = 0;

    // Current file position.
    virtual std::int64_t tell() = 0;

    // Not copyable/movable.
    OgFile(const OgFile&) = delete;
    OgFile& operator=(const OgFile&) = delete;

protected:
    OgFile() = default;
};

using OgFilePtr = std::unique_ptr<OgFile>;

// Open a file for reading (SDL-free).
// Tries PhysFS first, then cwd, user path, asset path.
OgFilePtr og_open_read(const char* file, bool debug = false);
OgFilePtr og_open_read(const char* path, const char* file);

// Open a file for writing (SDL-free).
// Tries PhysFS first, then direct filesystem.
OgFilePtr og_open_write(const char* file);
OgFilePtr og_open_write(const char* path, const char* file);

// Helper: read exactly size*count bytes, return false on short read.
inline bool og_read_exact(OgFile& f, void* dst, std::size_t size, std::size_t count)
{
    return f.read(dst, size, count) == count;
}

// Helper: write exactly size*count bytes, return false on short write.
inline bool og_write_exact(OgFile& f, const void* src, std::size_t size, std::size_t count)
{
    return f.write(src, size, count) == count;
}

} // namespace og::io

class PixieData;

// Read a pixie (.pix) sprite file. SDL-free — implemented in og_file.cpp.
PixieData read_pixie_file(const char* filename);
