#pragma once
/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Shared fixture for the issue-#162 campaign sprite-reload tests.
//
// Builds temporary .glad packages that ship their OWN entity art —
// pix/footman.png carrying the ELF donor sprite (visibly different bytes,
// still a valid indexed PNG with a matching frame sidecar) — plus an
// embedded class pack whose living family `test:ball` resolves its sprite
// from packs/spritetest/sprites/ball.png. That is exactly the shape the
// multiplayer-modes campaign ships, and exactly what never loaded before
// the reload_graphics_if_stale() fix.

#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/io_common.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

std::string get_user_path();
std::string get_asset_path();

namespace og::test162 {

inline bool write_bytes(const std::filesystem::path& path,
                        const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

inline std::vector<std::uint8_t> read_disk_file(
    const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// Donor art: the shipped elf sprite (10x10, 24 frames, "standard" animation
// layout — same layout family as footman, so it is a drop-in footman
// replacement whose pixels provably differ).
inline std::vector<std::uint8_t> donor_elf_png()
{
    return read_disk_file(get_asset_path() + "pix/elf.png");
}

inline std::vector<std::uint8_t> donor_elf_json()
{
    return read_disk_file(get_asset_path() + "pix/elf.json");
}

// Bytes only the campaign ships (precedence tests: the sprite sheet cannot
// shadow a name it does not carry).
inline std::vector<std::uint8_t> campaign_only_bytes()
{
    return {0x09, 0x08, 0x07, 0x06};
}

inline constexpr const char* kBallFamilyId = "test:ball";

// Minimal-but-complete living family: sprite from the pack's own sprites/
// dir, built-in "standard" animation (the donor elf art IS standard-layout).
inline constexpr const char* kBallFamilyLua =
    "og.pack{ id = 'org.test.spritetest', version = '1' }\n"
    "og.family('living', {\n"
    "  id = 'test:ball',\n"
    "  wire_id = 'auto',\n"
    "  name = 'BALL',\n"
    "  names = { 'Bouncer' },\n"
    "  sprite = 'packs/spritetest/sprites/ball.png',\n"
    "  animation = 'standard',\n"
    "})\n";

// Writes the sprite-fixture files into `root` (a campaign staging dir or the
// unpacked temp/ tree of an existing package).
inline bool write_sprite_fixture_files(const std::filesystem::path& root)
{
    const std::vector<std::uint8_t> art = donor_elf_png();
    const std::vector<std::uint8_t> sidecar = donor_elf_json();
    if (art.empty() || sidecar.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(root / "pix", ec);
    std::filesystem::create_directories(
        root / "packs" / "spritetest" / "families", ec);
    std::filesystem::create_directories(
        root / "packs" / "spritetest" / "sprites", ec);
    if (ec)
        return false;

    if (!write_bytes(root / "pix" / "footman.png", art))
        return false;
    if (!write_bytes(root / "pix" / "footman.json", sidecar))
        return false;
    if (!write_bytes(root / "pix" / "only_in_campaign.png",
                     campaign_only_bytes()))
        return false;
    if (!write_bytes(root / "packs" / "spritetest" / "sprites" / "ball.png",
                     art))
        return false;
    if (!write_bytes(root / "packs" / "spritetest" / "sprites" / "ball.json",
                     sidecar))
        return false;
    {
        std::ofstream lua(root / "packs" / "spritetest" / "families" /
                          "ball.lua");
        lua << kBallFamilyLua;
        if (!lua.good())
            return false;
    }
    return true;
}

// Minimal package (no levels): campaign.yaml + the sprite fixtures. Enough
// for mount/generation/loader assertions.
inline bool install_sprite_carrier_campaign(const std::string& id)
{
    namespace fs = std::filesystem;
    const fs::path staging =
        fs::path(get_user_path()) / "sprite162_staging" / id;
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec)
        return false;
    {
        std::ofstream yaml(staging / "campaign.yaml");
        yaml << "format: 1\ntitle: Sprite Carrier\nfirst_level: 1\n";
        if (!yaml.good())
            return false;
    }
    if (!write_sprite_fixture_files(staging))
        return false;
    const fs::path archive =
        fs::path(get_user_path()) / "campaigns" / (id + ".glad");
    fs::remove(archive, ec);
    return zip_contents_with_error(staging.string(), archive.string()) ==
           ArchiveIoError::None;
}

// Fully playable package: a copy of the shipped gladiator campaign (levels,
// grids, yaml) with the sprite fixtures injected. Requires the default
// campaigns installed (restore_default_campaigns) so CampaignData can copy.
inline bool install_playable_sprite_campaign(const std::string& id)
{
    CampaignData source("gladiator");
    if (!source.load())
        return false;
    source.title = "Sprite Carrier";
    if (!source.save_as(id))
        return false;

    cleanup_unpacked_campaign();
    if (!unpack_campaign(id))
        return false;
    const std::filesystem::path temp =
        std::filesystem::path(get_user_path()) / "temp";
    const bool wrote = write_sprite_fixture_files(temp);
    const bool packed = wrote && repack_campaign(id);
    cleanup_unpacked_campaign();
    return packed;
}

inline void remove_sprite_campaign(const std::string& id)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(fs::path(get_user_path()) / "campaigns" / (id + ".glad"), ec);
    fs::remove_all(fs::path(get_user_path()) / "sprite162_staging", ec);
}

} // namespace og::test162
