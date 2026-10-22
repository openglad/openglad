/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// og::io::read_rgba_png, the loader behind the CHANGELOG! keyboard picture
// (docs/ability-select-design.md F.4): the shipped pictures decode as RGBA,
// and every kind of bad file comes back empty instead of crashing or asking
// for an unbounded allocation.

#include <gtest/gtest.h>

#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{

namespace fs = std::filesystem;

constexpr const char* kDesktopPicture = "pix/changelog/keyboard_belt.png";
constexpr const char* kWebPicture = "pix/changelog/keyboard_belt_web.png";

// Scratch under the user dir's temp/, spelled absolute and removed at the
// end (the test_io_coverage.cpp pattern).
struct Scratch
{
    fs::path dir = fs::path(get_user_path()) / "temp" / "rgba_png";
    Scratch()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~Scratch()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    std::string write(const char* name, const std::vector<char>& bytes) const
    {
        const fs::path path = dir / name;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path.string();
    }
};

std::vector<char> read_bytes(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>());
}

// The PNG chunk CRC (ISO 3309), so a hand-edited header still passes the
// decoder's checksum and reaches the size check.
std::uint32_t png_crc(const char* data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint8_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

void put_be32(std::vector<char>& bytes, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes[at + static_cast<std::size_t>(i)] =
            static_cast<char>((value >> (24 - 8 * i)) & 0xFFu);
}

void append_be32(std::vector<char>& bytes, std::uint32_t value)
{
    bytes.resize(bytes.size() + 4u);
    put_be32(bytes, bytes.size() - 4u, value);
}

void append_chunk(std::vector<char>& png, const char* type,
                  const std::vector<char>& data)
{
    append_be32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    append_be32(png, png_crc(png.data() + start, png.size() - start));
}

// A whole, valid w x 1 RGBA PNG (every pixel transparent black), its one
// row in a zlib STORED block, so a picture just past the side cap is a
// small, honest file the decoder WOULD read: only the cap refuses it.
std::vector<char> one_row_png(std::uint32_t w)
{
    std::vector<char> raw(1u + static_cast<std::size_t>(w) * 4u, 0);  // filter 0
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (char c : raw) {
        a = (a + static_cast<std::uint8_t>(c)) % 65521u;
        b = (b + a) % 65521u;
    }
    std::vector<char> zlib = {0x78, 0x01, 0x01};  // header, final stored block
    const auto len = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<char>(len & 0xFFu));
    zlib.push_back(static_cast<char>(len >> 8));
    const auto nlen = static_cast<std::uint16_t>(0xFFFFu - len);
    zlib.push_back(static_cast<char>(nlen & 0xFFu));
    zlib.push_back(static_cast<char>(nlen >> 8));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    append_be32(zlib, (b << 16) | a);

    std::vector<char> png = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    std::vector<char> ihdr;
    append_be32(ihdr, w);
    append_be32(ihdr, 1u);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

} // namespace

TEST(RgbaPng, decodes_both_shipped_keyboard_pictures)
{
    for (const char* path : {kDesktopPicture, kWebPicture}) {
        const std::optional<og::io::RgbaImage> image =
            og::io::read_rgba_png(path);
        ASSERT_TRUE(image.has_value()) << path;
        EXPECT_EQ(1560, image->w) << path;
        EXPECT_EQ(550, image->h) << path;
        ASSERT_EQ(static_cast<std::size_t>(1560 * 550 * 4), image->bytes.size())
            << path;
        // The picture is opaque (the native plane composites it without
        // blending) and not blank.
        std::size_t opaque = 0;
        std::size_t bright = 0;
        for (std::size_t i = 0; i < image->bytes.size(); i += 4) {
            if (image->bytes[i + 3] == 255)
                ++opaque;
            if (image->bytes[i] > 200)
                ++bright;
        }
        EXPECT_EQ(static_cast<std::size_t>(1560 * 550), opaque) << path;
        EXPECT_GT(bright, 10000u) << path << " has no coloured key caps";
    }
    const std::optional<og::io::RgbaImage> desktop =
        og::io::read_rgba_png(kDesktopPicture);
    const std::optional<og::io::RgbaImage> web =
        og::io::read_rgba_png(kWebPicture);
    ASSERT_TRUE(desktop && web);
    EXPECT_NE(desktop->bytes, web->bytes)
        << "the web picture shows player 1's FIRE on Z";
}

TEST(RgbaPng, a_missing_file_is_empty)
{
    EXPECT_FALSE(og::io::read_rgba_png("pix/changelog/no_such_picture.png"));
}

TEST(RgbaPng, an_empty_file_is_empty)
{
    const Scratch scratch;
    EXPECT_FALSE(og::io::read_rgba_png(scratch.write("empty.png", {}).c_str()));
}

TEST(RgbaPng, a_truncated_file_is_rejected)
{
    const Scratch scratch;
    std::vector<char> bytes = read_bytes(kDesktopPicture);
    ASSERT_GT(bytes.size(), 400u);
    // The signature and half the header: the decoder cannot even read the
    // size.
    std::vector<char> header_cut(bytes.begin(), bytes.begin() + 20);
    EXPECT_FALSE(og::io::read_rgba_png(
        scratch.write("header_cut.png", header_cut).c_str()));
    // A whole header and a cut-off image body: the size reads, the pixels
    // do not decode.
    std::vector<char> body_cut(bytes.begin(), bytes.begin() + 400);
    EXPECT_FALSE(og::io::read_rgba_png(
        scratch.write("body_cut.png", body_cut).c_str()));
}

TEST(RgbaPng, an_oversize_file_or_picture_is_rejected)
{
    const Scratch scratch;
    // Over the 16 MiB file cap: refused before anything is read.
    const std::string big = scratch.write("big.png", read_bytes(kDesktopPicture));
    std::error_code ec;
    fs::resize_file(big, 16u * 1024u * 1024u + 1u, ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_FALSE(og::io::read_rgba_png(big.c_str()));

    // A small file whose header claims 5000 x 550: refused at the header,
    // before the decoder allocates for it. Width sits at byte 16, the IHDR
    // CRC (over the chunk type and data) at byte 29.
    std::vector<char> bytes = read_bytes(kDesktopPicture);
    ASSERT_GT(bytes.size(), 33u);
    ASSERT_EQ(std::string("IHDR"), std::string(bytes.data() + 12, 4));
    put_be32(bytes, 16, 5000u);
    put_be32(bytes, 29, png_crc(bytes.data() + 12, 17));
    EXPECT_FALSE(og::io::read_rgba_png(
        scratch.write("wide.png", bytes).c_str()));
}

TEST(RgbaPng, the_side_cap_is_4096_pixels)
{
    // The same honest one-row picture either side of the cap: 4096 wide
    // decodes, 4097 wide is refused (the file is 16 KiB either way, far under
    // the file cap, so only the side cap can refuse it).
    const Scratch scratch;
    const auto at_cap = og::io::read_rgba_png(
        scratch.write("at_cap.png", one_row_png(4096u)).c_str());
    ASSERT_TRUE(at_cap) << "the hand-built PNG does not decode";
    EXPECT_EQ(4096, at_cap->w);
    EXPECT_EQ(1, at_cap->h);
    EXPECT_NE(0u, at_cap->serial);
    EXPECT_FALSE(og::io::read_rgba_png(
        scratch.write("past_cap.png", one_row_png(4097u)).c_str()));
}
