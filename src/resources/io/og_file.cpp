/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// SDL-free file I/O implementation.
// Uses PhysFS native API + stdio FILE* — no SDL dependency.

#include <openglad/resources/og_file.h>
#include <openglad/core/util.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/resources/our_palette.h>

#include "physfs.h"
#include "lodepng.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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
    if (fp) {
        // Disable stdio buffering so write errors are reported on write().
        std::setvbuf(fp, nullptr, _IONBF, 0);
        return std::make_unique<StdioOgFile>(fp);
    }

    return nullptr;
}

OgFilePtr og_open_write(const char* path, const char* file)
{
    return og_open_write((std::string(path) + file).c_str());
}

} // namespace og::io

// ---------------------------------------------------------------------------
// write_pixie_png — SDL-free indexed PNG writing via lodepng + OgFile
// ---------------------------------------------------------------------------

bool write_pixie_png(const char* filepath, const PixieData& data)
{
    if (!data.valid()) return false;

    auto outfile = og::io::og_open_write(filepath);
    if (!outfile) {
        LogError("Failed to open for writing: {}\n", filepath);
        return false;
    }

    const unsigned w = static_cast<unsigned>(data.w);
    const unsigned h = static_cast<unsigned>(data.h) * static_cast<unsigned>(data.frames);

    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;

    for (unsigned i = 0; i < 256; ++i) {
        const unsigned r6 = our_pal_lookup(static_cast<int>(i * 3));
        const unsigned g6 = our_pal_lookup(static_cast<int>(i * 3 + 1));
        const unsigned b6 = our_pal_lookup(static_cast<int>(i * 3 + 2));
        const auto r8 = static_cast<unsigned char>((r6 * 255u) / 63u);
        const auto g8 = static_cast<unsigned char>((g6 * 255u) / 63u);
        const auto b8 = static_cast<unsigned char>((b6 * 255u) / 63u);
        const auto a8 = static_cast<unsigned char>((i == 0) ? 0u : 255u);
        lodepng_palette_add(&state.info_raw, r8, g8, b8, a8);
        lodepng_palette_add(&state.info_png.color, r8, g8, b8, a8);
    }

    std::vector<unsigned char> png_bytes;
    const unsigned err = lodepng::encode(png_bytes, data.data.get(), w, h, state);
    if (err != 0) {
        LogError("Failed to encode PNG: {}: {}\n", filepath, lodepng_error_text(err));
        return false;
    }

    const std::size_t wrote = outfile->write(png_bytes.data(), 1, png_bytes.size());
    if (wrote != png_bytes.size()) {
        LogError("Failed to write PNG: {}\n", filepath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Sprite manifest — maps PNG filename to (frames, width, height)
// ---------------------------------------------------------------------------
struct SpriteInfo { int frames; int w; int h; };
static std::unordered_map<std::string, SpriteInfo> s_sprite_manifest;
static bool s_manifest_loaded = false;

static void load_sprite_manifest()
{
    using namespace og::io;
    if (s_manifest_loaded) return;
    s_manifest_loaded = true;

    auto infile = og_open_read("pix/", "sprite_manifest.txt");
    if (!infile)
        infile = og_open_read("sprite_manifest.txt");
    if (!infile) {
        LogWarn("No sprite_manifest.txt found; assuming 1 frame per PNG\n");
        return;
    }

    // Read entire file
    infile->seek(0, 2); // SEEK_END
    auto file_size = infile->tell();
    infile->seek(0, 0); // SEEK_SET
    if (file_size <= 0) return;

    std::string content(static_cast<std::size_t>(file_size), '\0');
    infile->read(content.data(), 1, static_cast<std::size_t>(file_size));

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream lss(line);
        std::string fname;
        int frames, w, h;
        if (lss >> fname >> frames >> w >> h) {
            s_sprite_manifest[fname] = {frames, w, h};
        }
    }
}

// ---------------------------------------------------------------------------
// read_pixie_file — SDL-free indexed PNG implementation
// ---------------------------------------------------------------------------
// Reads a PNG sprite file via lodepng. Each pixel value is a palette index.
// Frame metadata comes from pix/sprite_manifest.txt.

PixieData read_pixie_file(const char* filename)
{
    using namespace og::io;
    PixieData result;

    load_sprite_manifest();

    auto infile = og_open_read("pix/", filename);
    if (!infile)
        infile = og_open_read(filename);
    if (!infile) {
        LogError("Cannot open sprite file: pix/{}\n", filename);
        return result;
    }

    // Read entire file into memory for lodepng
    infile->seek(0, 2); // SEEK_END
    auto file_size = infile->tell();
    infile->seek(0, 0); // SEEK_SET
    if (file_size <= 0) {
        LogError("Empty sprite file: pix/{}\n", filename);
        return result;
    }

    auto file_data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(file_size));
    if (infile->read(file_data.get(), 1, static_cast<std::size_t>(file_size))
        != static_cast<std::size_t>(file_size))
    {
        LogError("Failed to read sprite file: pix/{}\n", filename);
        return result;
    }

    lodepng::State state;
    state.decoder.color_convert = 0;

    std::vector<unsigned char> pixels;
    unsigned png_w = 0;
    unsigned png_h = 0;
    const unsigned decode_err = lodepng::decode(
        pixels, png_w, png_h, state, file_data.get(), static_cast<std::size_t>(file_size));
    if (decode_err != 0) {
        LogError("Failed to decode PNG: pix/{}: {}\n", filename, lodepng_error_text(decode_err));
        return result;
    }

    const auto colortype = state.info_png.color.colortype;
    const auto bitdepth = state.info_png.color.bitdepth;
    const bool indexed_8bit = (colortype == LCT_PALETTE && bitdepth == 8);
    const bool legacy_grey_8bit = (colortype == LCT_GREY && bitdepth == 8);
    if (!indexed_8bit && !legacy_grey_8bit) {
        LogError("Sprite PNG must be indexed or legacy grayscale (8-bit): pix/{}\n", filename);
        return result;
    }

    if (pixels.size() != static_cast<std::size_t>(png_w) * static_cast<std::size_t>(png_h)) {
        LogError("Unexpected indexed PNG size for pix/{}\n", filename);
        return result;
    }

    // Look up frame metadata from manifest
    auto it = s_sprite_manifest.find(filename);
    int frames = 1;
    int frame_h = static_cast<int>(png_h);
    if (it != s_sprite_manifest.end()) {
        frames = it->second.frames;
        frame_h = it->second.h;
    }

    if (png_w > 255 || frame_h > 255 || frames > 255) {
        LogError("Sprite dimensions too large for PixieData: pix/{}\n", filename);
        return result;
    }

    const auto expected_h = static_cast<unsigned>(frame_h) * static_cast<unsigned>(frames);
    if (expected_h != png_h) {
        LogError("Sprite manifest mismatch for pix/{}: expected total height {}, got {}\n",
                 filename, expected_h, png_h);
        return result;
    }

    result.frames = static_cast<unsigned char>(frames);
    result.w = static_cast<unsigned char>(png_w);
    result.h = static_cast<unsigned char>(frame_h);

    std::size_t total_size = static_cast<std::size_t>(png_w) * static_cast<std::size_t>(frame_h)
        * static_cast<std::size_t>(frames);
    result.data = std::make_unique<unsigned char[]>(total_size);
    std::memcpy(result.data.get(), pixels.data(), total_size);
    return result;
}
