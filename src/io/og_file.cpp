/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// SDL-free file I/O implementation.
// Uses PhysFS native API + stdio FILE* — no SDL dependency.

#include <openglad/io/og_file.h>
#include <openglad/core/util.h>
#include <openglad/data/pixie_data.h>

#include "physfs.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

// Path helpers (defined in platform_io.cpp, linked via og_io or text client)
std::string get_user_path();
std::string get_asset_path();

namespace og::io {

// ---------------------------------------------------------------------------
// PhysFS-backed OgFile
// ---------------------------------------------------------------------------
class PhysfsOgFile final : public OgFile {
public:
    explicit PhysfsOgFile(PHYSFS_File* f) : file_(f) {}

    ~PhysfsOgFile() override
    {
        if (file_)
            PHYSFS_close(file_);
    }

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        if (!file_ || size == 0 || count == 0)
            return 0;
        const PHYSFS_sint64 total = static_cast<PHYSFS_sint64>(size * count);
        const PHYSFS_sint64 got = PHYSFS_readBytes(file_, buf, total);
        if (got < 0)
            return 0;
        return static_cast<std::size_t>(got) / size;
    }

    std::size_t write(const void* buf, std::size_t size, std::size_t count) override
    {
        if (!file_ || size == 0 || count == 0)
            return 0;
        const PHYSFS_sint64 total = static_cast<PHYSFS_sint64>(size * count);
        const PHYSFS_sint64 wrote = PHYSFS_writeBytes(file_, buf, total);
        if (wrote < 0)
            return 0;
        return static_cast<std::size_t>(wrote) / size;
    }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        if (!file_)
            return -1;
        PHYSFS_uint64 pos = 0;
        switch (whence) {
            case 0: // SEEK_SET
                pos = static_cast<PHYSFS_uint64>(offset);
                break;
            case 1: // SEEK_CUR
                pos = static_cast<PHYSFS_uint64>(PHYSFS_tell(file_) + offset);
                break;
            case 2: // SEEK_END
                pos = static_cast<PHYSFS_uint64>(PHYSFS_fileLength(file_) + offset);
                break;
            default:
                return -1;
        }
        if (!PHYSFS_seek(file_, pos))
            return -1;
        return static_cast<std::int64_t>(pos);
    }

    std::int64_t tell() override
    {
        return file_ ? PHYSFS_tell(file_) : -1;
    }

private:
    PHYSFS_File* file_;
};

// ---------------------------------------------------------------------------
// stdio FILE*-backed OgFile
// ---------------------------------------------------------------------------
class StdioOgFile final : public OgFile {
public:
    explicit StdioOgFile(FILE* f) : file_(f) {}

    ~StdioOgFile() override
    {
        if (file_)
            std::fclose(file_);
    }

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        if (!file_)
            return 0;
        return std::fread(buf, size, count, file_);
    }

    std::size_t write(const void* buf, std::size_t size, std::size_t count) override
    {
        if (!file_)
            return 0;
        return std::fwrite(buf, size, count, file_);
    }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        if (!file_)
            return -1;
        if (std::fseek(file_, static_cast<long>(offset), whence) != 0)
            return -1;
        return std::ftell(file_);
    }

    std::int64_t tell() override
    {
        return file_ ? std::ftell(file_) : -1;
    }

private:
    FILE* file_;
};

// ---------------------------------------------------------------------------
// Open helpers
// ---------------------------------------------------------------------------

static OgFilePtr try_physfs_read(const char* path)
{
    PHYSFS_File* f = PHYSFS_openRead(path);
    if (f)
        return std::make_unique<PhysfsOgFile>(f);
    return nullptr;
}

static OgFilePtr try_stdio_read(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f)
        return std::make_unique<StdioOgFile>(f);
    return nullptr;
}

OgFilePtr og_open_read(const char* file, bool debug)
{
    if (debug)
        Log("og_open_read: trying PhysFS: {}", file);
    if (auto f = try_physfs_read(file))
        return f;

    if (debug)
        Log("og_open_read: trying cwd: {}", file);
    if (auto f = try_stdio_read(file))
        return f;

    std::string user_file = get_user_path() + "/" + file;
    if (debug)
        Log("og_open_read: trying user path: {}", user_file);
    if (auto f = try_stdio_read(user_file))
        return f;

    std::string asset_file = get_asset_path() + "/" + file;
    if (debug)
        Log("og_open_read: trying asset path: {}", asset_file);
    if (auto f = try_stdio_read(asset_file))
        return f;

    if (debug)
        Log("og_open_read: file not found: {}", file);
    return nullptr;
}

OgFilePtr og_open_read(const char* path, const char* file)
{
    return og_open_read((std::string(path) + file).c_str());
}

OgFilePtr og_open_write(const char* file)
{
    PHYSFS_File* f = PHYSFS_openWrite(file);
    if (f)
        return std::make_unique<PhysfsOgFile>(f);

    FILE* fp = std::fopen(file, "wb");
    if (fp)
        return std::make_unique<StdioOgFile>(fp);

    return nullptr;
}

OgFilePtr og_open_write(const char* path, const char* file)
{
    return og_open_write((std::string(path) + file).c_str());
}

} // namespace og::io

// ---------------------------------------------------------------------------
// read_pixie_file — SDL-free implementation
// ---------------------------------------------------------------------------
// Reads a .pix sprite file: 1-byte frames, 1-byte w, 1-byte h, then frame data.
// This replaces the version formerly in src/render/graphlib.cpp.

PixieData read_pixie_file(const char* filename)
{
    using namespace og::io;
    PixieData result;

    auto infile = og_open_read("pix/", filename);
    if (!infile)
        infile = og_open_read(filename);
    if (!infile) {
        LogError("Cannot open pixie file: pix/{}\n", filename);
        return result;
    }

    if (!og_read_exact(*infile, &result.frames, 1, 1) ||
        !og_read_exact(*infile, &result.w, 1, 1) ||
        !og_read_exact(*infile, &result.h, 1, 1))
    {
        LogError("Failed to read pixie header: pix/{}\n", filename);
        return result;
    }

    std::size_t size = result.w * result.h * result.frames;
    result.data = std::make_unique<unsigned char[]>(size);

    if (!og_read_exact(*infile, result.data.get(), 1, size))
    {
        LogError("Failed to read pixie payload: pix/{} ({} bytes)\n", filename, size);
        result.free();
        return result;
    }

    return result;
}

// ---------------------------------------------------------------------------
// headless_io_init — SDL-free filesystem initialization
// ---------------------------------------------------------------------------
// Mirrors io_init() from platform_io.cpp but uses stdio for file copying
// instead of SDL_RWFromFile.

static void headless_copy_file(const std::string& src, const std::string& dst)
{
    std::error_code ec;
    std::filesystem::copy_file(src, dst,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        LogWarn("headless_copy_file: {} -> {}: {}\n", src, dst, ec.message());
    else
        Log("Copied: {} -> {}\n", src, dst);
}

void headless_io_init(const char* argv0)
{
    // Create user directory tree
    std::string user_path = get_user_path();
    std::error_code mkdir_ec;
    std::filesystem::create_directories(user_path, mkdir_ec);
    std::filesystem::create_directories(user_path + "campaigns/", mkdir_ec);
    std::filesystem::create_directories(user_path + "save/", mkdir_ec);
    std::filesystem::create_directories(user_path + "cfg/", mkdir_ec);

    // Initialize PhysFS
    if (!PHYSFS_init(argv0)) {
        LogError("headless_io_init: PHYSFS_init failed: {}\n",
                 PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return;
    }

    if (!PHYSFS_setWriteDir(user_path.c_str())) {
        LogError("headless_io_init: PHYSFS_setWriteDir failed: {}\n",
                 PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }

    if (!PHYSFS_mount(user_path.c_str(), nullptr, 1)) {
        LogError("headless_io_init: Failed to mount user path: {}\n",
                 PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }

    // Copy default campaign from asset path to user path (stdio, no SDL)
    std::string asset_path = get_asset_path();
    std::string src_campaign = asset_path + "builtin/org.openglad.gladiator.glad";
    std::string dst_campaign = user_path + "campaigns/org.openglad.gladiator.glad";
    headless_copy_file(src_campaign, dst_campaign);

    // Mount default campaign
    if (!PHYSFS_mount(dst_campaign.c_str(), nullptr, 0)) {
        LogError("headless_io_init: Failed to mount campaign: {}\n",
                 PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }

    // Mount asset directories
    auto try_mount = [](const std::string& path, const char* mount) {
        if (!PHYSFS_mount(path.c_str(), mount, 1))
            LogWarn("headless_io_init: Failed to mount {}\n", path);
    };
    try_mount(asset_path + "pix/", "pix/");
    try_mount(asset_path + "sound/", "sound/");
    try_mount(asset_path + "cfg/", "cfg/");

    Log("headless_io_init: done\n");
}
