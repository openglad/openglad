/* Multiplayer Game Modes campaign generator — shared authoring helpers.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "modes_mapgen.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/level_file_io.h>

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>

bool write_pixie_png(const char* filepath, const PixieData& data);
std::string get_user_path();

namespace modesgen {

namespace fs = std::filesystem;

int g_errors = 0;

void fail(const std::string& message)
{
    std::fprintf(stderr, "modes_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

const char* mode_name(ModeKind mode)
{
    switch (mode)
    {
        case ModeKind::Tdm: return "tdm";
        case ModeKind::Ctf: return "ctf";
        case ModeKind::Onslaught: return "onslaught";
        case ModeKind::Soccer: return "soccer";
        case ModeKind::Basketball: return "basketball";
        case ModeKind::Mutant: return "mutant";
    }
    return "?";
}

std::unique_ptr<walker> make_probe(GameWorld& world)
{
    if (!world.entity_factory)
        return nullptr;
    std::unique_ptr<walker> probe =
        world.entity_factory(Order::Living, FAMILY_SOLDIER);
    if (probe == nullptr)
        return nullptr;
    PixieData square;
    square.frames = 1;
    square.w = 16;
    square.h = 16;
    square.data = std::make_unique<unsigned char[]>(16 * 16);
    probe->set_data(square);
    return probe;
}

bool tile_passable(GameWorld& world, walker* probe, TilePos t)
{
    return world.query_grid_passable(static_cast<float>(t.tx * GRID_SIZE),
                                     static_cast<float>(t.ty * GRID_SIZE),
                                     probe);
}

walker* place_at(GameWorld& world, Order order, int family, int team,
                 TilePos at, int level)
{
    walker* w = (order == Order::Treasure) ? world.add_fx_ob(order, family)
                                           : world.add_ob(order, family);
    if (w == nullptr)
    {
        fail(std::format("could not place order {} family {} at ({}, {})",
                         static_cast<int>(order), family, at.tx, at.ty));
        return nullptr;
    }
    w->setxy(static_cast<short>(at.tx * GRID_SIZE),
             static_cast<short>(at.ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    if (level > 1 && w->stats() != nullptr)
        w->stats()->set_level(static_cast<short>(level));
    return w;
}

void apply_mode_metadata(GameWorld& world, const char* title, int par_value)
{
    // D9: every mode level authors SCEN_TYPE_SCRIPTED (0x20) ONLY — no
    // CAN_EXIT, no SAVE_ALL, no CTF bit. Mode identity beyond the scripted
    // fork is the pack's per-level Lua registration.
    world.type = SCEN_TYPE_SCRIPTED;
    world.title = title;
    if (world.title.size() > 30)
        fail(std::format("title too long: {}", world.title));
    world.par_value = static_cast<short>(par_value);
    world.time_bonus_limit = 4000;
}

bool save_level(GameWorld& world, const ExpectedLevel& row)
{
    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", row.id);
    metadata.generated = true; // provenance mark: this scen is tool output
    for (const std::string& line : row.briefing)
    {
        if (line.size() > 33)
            fail(std::format("scen{}: briefing line '{}' overflows 33 chars",
                             row.id, line));
        metadata.description.push_back(line);
    }
    if (row.briefing.empty() ||
        row.briefing.back() != "-- THE GAMESMASTER")
        fail(std::format("scen{}: briefing must end '-- THE GAMESMASTER'",
                         row.id));

    const std::string path =
        get_user_path() + std::format("temp/scen/scen{}.fss", row.id);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, path, metadata, &err))
    {
        fail(std::format("failed to write {}", path));
        return false;
    }
    return true;
}

void copy_vendored_pix(const std::string& pkg, const std::string& name)
{
    const fs::path src =
        fs::path("tools/modes_mapgen/data") / pkg / "pix" / name;
    const fs::path dst = fs::path(get_user_path()) / "temp/pix" / name;
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
        fail(std::format("cannot byte-copy {} -> {}: {}", src.string(),
                         dst.string(), ec.message()));
}

void write_decor_plane(GameWorld& world, int out_id)
{
    const PixieData& dec = world.decor;
    if (!dec.valid())
        return;
    const std::size_t cells =
        static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(dec.h);
    bool nonzero = false;
    for (std::size_t c = 0; c < cells && !nonzero; ++c)
        nonzero = dec.data[c] != 0;
    if (!nonzero)
        return;
    const std::string path =
        get_user_path() + std::format("temp/pix/scen{:04d}_d0.png", out_id);
    if (!write_pixie_png(path.c_str(), dec))
        fail(std::format("failed to write {}", path));
}

bool mount_vendored_data(const std::string& pkg)
{
    const std::string abs =
        fs::absolute(fs::path("tools/modes_mapgen/data") / pkg).string();
    if (!og::resources::mount(abs.c_str(), nullptr, 0))
    {
        fail(std::format("cannot mount vendored data dir {}", abs));
        return false;
    }
    return true;
}

void unmount_vendored_data(const std::string& pkg)
{
    const std::string abs =
        fs::absolute(fs::path("tools/modes_mapgen/data") / pkg).string();
    (void)og::resources::unmount(abs.c_str());
}

std::vector<ExpectedLevel> all_expectations()
{
    std::vector<ExpectedLevel> all;
    for (auto rows : {tdm_expectations(), ctf_expectations(),
                      onslaught_expectations(), soccer_expectations(),
                      basketball_expectations(), mutant_expectations()})
        for (ExpectedLevel& row : rows)
            all.push_back(std::move(row));
    return all;
}

} // namespace modesgen
