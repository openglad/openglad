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
#include <openglad/core/test_trace.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/resources/our_palette.h>

#include "physfs.h"
#include "lodepng.h"

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
// Pixie<->PNG conversion lineage: based on pixedit by Zardus (9/03/2002) and
// the standalone pixconvert tool by Jonathan Dearborn (5/07/2013), now folded
// into the engine's I/O layer.

static void build_indexed_png_state(lodepng::State& state)
{
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    // Force lodepng to write the configured indexed colortype rather than
    // auto-downgrading to grayscale when the image only references low-index
    // entries.
    state.encoder.auto_convert = 0;

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
}

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
    build_indexed_png_state(state);

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
// Aseprite sprite-sheet sidecar — per-PNG <basename>.json describes frame
// layout in Aseprite's "Hash" export format. We need three numbers (frames,
// frame_w, frame_h). The parser below is a small recursive-descent JSON reader
// scoped to the subset Aseprite emits: top-level object, nested objects and
// arrays, integer numbers, string keys without escapes, and the keywords
// true/false/null. It cross-validates meta.size against frame_count*frame_h
// before accepting; the caller falls back to single-frame on std::nullopt.
// ---------------------------------------------------------------------------
struct FrameInfo { int frames; int frame_w; int frame_h; };

constexpr std::int64_t kMaxSpritePngBytes = 16ll * 1024ll * 1024ll;
constexpr std::int64_t kMaxAsepriteSidecarBytes = 1ll * 1024ll * 1024ll;
constexpr unsigned kMaxPixieDimension = 255u;
constexpr unsigned kMaxPixieStackedHeight = kMaxPixieDimension * kMaxPixieDimension;
constexpr std::uint64_t kMaxPixiePixels =
    static_cast<std::uint64_t>(kMaxPixieDimension) *
    static_cast<std::uint64_t>(kMaxPixieDimension) *
    static_cast<std::uint64_t>(kMaxPixieDimension);

static std::string sidecar_path_for(const char* filename)
{
    std::string s = filename ? filename : "";
    auto dot = s.find_last_of('.');
    if (dot != std::string::npos)
        s.resize(dot);
    s += ".json";
    return s;
}

namespace {

struct AsepriteMeta {
    int meta_w = 0;
    int meta_h = 0;
    int frame_count = 0;
    int frame_w = 0;
    int frame_h = 0;
    bool seen_meta_size = false;
    bool seen_first_frame = false;
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    bool parse(AsepriteMeta& m)
    {
        skip_ws();
        if (!eat('{')) return fail("expected top-level object");
        skip_ws();
        if (eat('}')) return fail("empty top-level object");
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':'");
            skip_ws();
            if (key == "frames") {
                if (!parse_frames(m)) return false;
            } else if (key == "meta") {
                if (!parse_meta(m)) return false;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) break;
            return fail("expected ',' or '}' at top level");
        }
        return true;
    }

    const char* why() const { return why_ ? why_ : "parse error"; }

private:
    bool parse_frames(AsepriteMeta& m)
    {
        if (!eat('{')) return fail("\"frames\" not an object");
        skip_ws();
        if (eat('}')) return true; // empty frames object
        bool first = true;
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':' in frames");
            skip_ws();
            if (first) {
                if (!parse_first_frame_entry(m)) return false;
                first = false;
            } else {
                if (!skip_value()) return false;
            }
            ++m.frame_count;
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return fail("expected ',' or '}' in frames");
        }
    }

    bool parse_first_frame_entry(AsepriteMeta& m)
    {
        if (!eat('{')) return fail("frame entry not an object");
        skip_ws();
        if (eat('}')) return fail("frame entry empty");
        bool found_frame = false;
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':' in frame entry");
            skip_ws();
            if (key == "frame" && !found_frame) {
                if (!parse_frame_rect(m)) return false;
                found_frame = true;
                m.seen_first_frame = true;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) {
                if (!found_frame) return fail("missing \"frame\" rectangle");
                return true;
            }
            return fail("expected ',' or '}' in frame entry");
        }
    }

    bool parse_frame_rect(AsepriteMeta& m)
    {
        if (!eat('{')) return fail("\"frame\" rect not an object");
        skip_ws();
        if (eat('}')) return fail("empty frame rect");
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':' in frame rect");
            skip_ws();
            if (key == "w") {
                if (!read_uint(m.frame_w)) return false;
            } else if (key == "h") {
                if (!read_uint(m.frame_h)) return false;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return fail("expected ',' or '}' in frame rect");
        }
    }

    bool parse_meta(AsepriteMeta& m)
    {
        if (!eat('{')) return fail("\"meta\" not an object");
        skip_ws();
        if (eat('}')) return true;
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':' in meta");
            skip_ws();
            if (key == "size") {
                if (!parse_meta_size(m)) return false;
                m.seen_meta_size = true;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return fail("expected ',' or '}' in meta");
        }
    }

    bool parse_meta_size(AsepriteMeta& m)
    {
        if (!eat('{')) return fail("\"meta.size\" not an object");
        skip_ws();
        if (eat('}')) return fail("empty meta.size");
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':' in meta.size");
            skip_ws();
            if (key == "w") {
                if (!read_uint(m.meta_w)) return false;
            } else if (key == "h") {
                if (!read_uint(m.meta_h)) return false;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return fail("expected ',' or '}' in meta.size");
        }
    }

    bool skip_value()
    {
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        char c = text_[pos_];
        if (c == '{') return skip_object();
        if (c == '[') return skip_array();
        if (c == '"') { std::string tmp; return read_string(tmp); }
        if (c == 't' || c == 'f' || c == 'n') return skip_keyword();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            int dummy = 0;
            return read_uint(dummy);
        }
        return fail("unexpected character in value");
    }

    bool skip_object()
    {
        if (!eat('{')) return fail("expected '{'");
        skip_ws();
        if (eat('}')) return true;
        while (true) {
            skip_ws();
            std::string key;
            if (!read_string(key)) return false;
            skip_ws();
            if (!eat(':')) return fail("expected ':'");
            skip_ws();
            if (!skip_value()) return false;
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            return fail("expected ',' or '}'");
        }
    }

    bool skip_array()
    {
        if (!eat('[')) return fail("expected '['");
        skip_ws();
        if (eat(']')) return true;
        while (true) {
            skip_ws();
            if (!skip_value()) return false;
            skip_ws();
            if (eat(',')) continue;
            if (eat(']')) return true;
            return fail("expected ',' or ']'");
        }
    }

    bool skip_keyword()
    {
        static constexpr const char* kws[] = {"true", "false", "null"};
        for (auto* kw : kws) {
            const std::size_t n = std::strlen(kw);
            if (pos_ + n <= text_.size() && text_.compare(pos_, n, kw) == 0) {
                pos_ += n;
                return true;
            }
        }
        return fail("unknown keyword");
    }

    bool read_string(std::string& out)
    {
        if (!eat('"')) return fail("expected '\"'");
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') return fail("backslash escape unsupported");
            out.push_back(c);
        }
        return fail("unterminated string");
    }

    bool read_uint(int& out)
    {
        if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_])))
            return fail("expected non-negative integer");
        // Use a 64-bit accumulator: on ILP32 targets (wasm32/Emscripten) `long`
        // is 32-bit, so `v * 10` overflowed (signed UB) before the guard below
        // could reject it. `long long` is >=64-bit everywhere; values that pass
        // the <=1e9 guard still fit losslessly in the int result.
        long long v = 0;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            v = v * 10 + (text_[pos_] - '0');
            if (v > 1'000'000'000L) return fail("integer overflow");
            ++pos_;
        }
        if (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == '.' || c == 'e' || c == 'E')
                return fail("non-integer numeric not supported");
        }
        out = static_cast<int>(v);
        return true;
    }

    void skip_ws()
    {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool eat(char c)
    {
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool fail(const char* msg) { if (!why_) why_ = msg; return false; }

    std::string_view text_;
    std::size_t pos_ = 0;
    const char* why_ = nullptr;
};

} // namespace

static std::optional<FrameInfo> load_aseprite_sidecar(const char* filename)
{
    using namespace og::io;
    const std::string rel = sidecar_path_for(filename);
    auto infile = og_open_read("pix/", rel.c_str());
    if (!infile) infile = og_open_read(rel.c_str());
    if (!infile) return std::nullopt; // absent — silent

    auto warn = [&](const char* why) {
        LogWarn("malformed Aseprite sidecar pix/{}: {}\n", rel, why);
        TRACE("io", "malformed Aseprite sidecar pix/%s: %s", rel.c_str(), why);
    };

    infile->seek(0, 2);
    auto file_size = infile->tell();
    infile->seek(0, 0);
    if (file_size <= 0) {
        warn("empty file");
        return std::nullopt;
    }
    if (file_size > kMaxAsepriteSidecarBytes) {
        warn("file too large");
        return std::nullopt;
    }

    std::string text(static_cast<std::size_t>(file_size), '\0');
    if (infile->read(text.data(), 1, static_cast<std::size_t>(file_size))
        != static_cast<std::size_t>(file_size)) {
        warn("read failed");
        return std::nullopt;
    }

    AsepriteMeta meta;
    JsonReader reader(text);
    if (!reader.parse(meta)) {
        warn(reader.why());
        return std::nullopt;
    }
    if (!meta.seen_meta_size) {
        warn("missing \"meta.size\"");
        return std::nullopt;
    }
    if (!meta.seen_first_frame || meta.frame_count <= 0) {
        warn("no frames");
        return std::nullopt;
    }
    if (meta.frame_w <= 0 || meta.frame_h <= 0) {
        warn("invalid frame dimensions");
        return std::nullopt;
    }
    if (static_cast<unsigned>(meta.frame_w) > kMaxPixieDimension
        || static_cast<unsigned>(meta.frame_h) > kMaxPixieDimension
        || static_cast<unsigned>(meta.frame_count) > kMaxPixieDimension) {
        warn("frame metadata out of range");
        return std::nullopt;
    }
    if (meta.meta_w != meta.frame_w
        || static_cast<long long>(meta.meta_h)
               != static_cast<long long>(meta.frame_h) * meta.frame_count) {
        warn("size mismatch between meta.size and frames*frame");
        return std::nullopt;
    }
    return FrameInfo{meta.frame_count, meta.frame_w, meta.frame_h};
}

// ---------------------------------------------------------------------------
// read_pixie_file — SDL-free indexed PNG implementation
// ---------------------------------------------------------------------------
// Reads a PNG sprite file via lodepng. Each pixel value is a palette index.
// Frame metadata comes from a per-PNG Aseprite "Hash"-format JSON sidecar
// (pix/<basename>.json); single-frame sprites have no sidecar.

PixieData read_pixie_file(const char* filename)
{
    using namespace og::io;
    PixieData result;

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
    if (file_size > kMaxSpritePngBytes) {
        LogError("Sprite PNG is too large: pix/{} ({} bytes)\n",
                 filename, static_cast<long long>(file_size));
        return result;
    }

    const auto png_size = static_cast<std::size_t>(file_size);
    auto file_data = std::make_unique<unsigned char[]>(png_size);
    if (infile->read(file_data.get(), 1, png_size) != png_size)
    {
        LogError("Failed to read sprite file: pix/{}\n", filename);
        return result;
    }

    lodepng::State state;
    state.decoder.color_convert = 0;
    unsigned png_w = 0;
    unsigned png_h = 0;
    const unsigned inspect_err =
        lodepng_inspect(&png_w, &png_h, &state, file_data.get(), png_size);
    if (inspect_err != 0) {
        LogError("Failed to inspect PNG: pix/{}: {}\n", filename,
                 lodepng_error_text(inspect_err));
        return result;
    }
    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(png_w) * static_cast<std::uint64_t>(png_h);
    if (png_w > kMaxPixieDimension || png_h > kMaxPixieStackedHeight ||
        pixel_count > kMaxPixiePixels) {
        LogError("Sprite PNG dimensions too large: pix/{} ({}x{})\n",
                 filename, png_w, png_h);
        return result;
    }

    std::vector<unsigned char> pixels;
    const unsigned decode_err = lodepng::decode(
        pixels, png_w, png_h, state, file_data.get(), png_size);
    if (decode_err != 0) {
        LogError("Failed to decode PNG: pix/{}: {}\n", filename, lodepng_error_text(decode_err));
        return result;
    }

    const auto colortype = state.info_png.color.colortype;
    const auto bitdepth = state.info_png.color.bitdepth;
    if (colortype != LCT_PALETTE || bitdepth != 8) {
        LogError("Sprite PNG must be indexed 8-bit: pix/{}\n", filename);
        return result;
    }

    if (state.info_png.color.palettesize != 256) {
        LogError("Sprite PNG palette must have 256 entries: pix/{} (got {})\n",
                 filename, static_cast<unsigned>(state.info_png.color.palettesize));
        return result;
    }
    const unsigned char* pal = state.info_png.color.palette;
    for (unsigned i = 0; i < 256; ++i) {
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned expected6 = our_pal_lookup(static_cast<int>(i * 3 + c));
            const unsigned expected8 = (expected6 * 255u) / 63u;
            const unsigned stored = pal[i * 4 + c];
            const int diff = static_cast<int>(stored) - static_cast<int>(expected8);
            if (diff < -1 || diff > 1) {
                LogError("Sprite PNG palette mismatch at entry {} channel {}: pix/{} "
                         "(stored {}, expected {})\n",
                         i, c, filename, stored, expected8);
                return result;
            }
        }
    }

    if (pixels.size() != static_cast<std::size_t>(png_w) * static_cast<std::size_t>(png_h)) {
        LogError("Unexpected indexed PNG size for pix/{}\n", filename);
        return result;
    }

    // Look up frame metadata from per-PNG Aseprite JSON sidecar; absent
    // sidecar means single-frame sprite.
    int frames = 1;
    int frame_h = static_cast<int>(png_h);
    if (auto info = load_aseprite_sidecar(filename)) {
        frames = info->frames;
        frame_h = info->frame_h;
    }

    if (png_w > 255 || frame_h > 255 || frames > 255) {
        LogError("Sprite dimensions too large for PixieData: pix/{}\n", filename);
        return result;
    }

    const auto expected_h = static_cast<unsigned>(frame_h) * static_cast<unsigned>(frames);
    if (expected_h != png_h) {
        LogError("Sprite sidecar mismatch for pix/{}: expected total height {}, got {}\n",
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
