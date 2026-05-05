/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/level_file_io.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/util.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/og_file.h>

bool write_pixie_png(const char* filepath, const PixieData& data);

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace {

using og::data::LevelFileIoError;
using og::data::LevelFileMetadata;

constexpr char kScenarioVersion = 9;
constexpr short kMaxScenarioObjects = 4096;

bool rw_read_exact_or_log(og::io::OgFile& file, void* dst, size_t size,
                          size_t count)
{
    const size_t got = file.read(dst, size, count);
    if (got != count)
    {
        Log("Read error: expected {} items, got {}\n", count, got);
        return false;
    }
    return true;
}

unsigned char sanitize_loaded_team_num(unsigned char team_num)
{
    if (team_num <= MAX_TEAM)
        return team_num;
    LogWarn("Scenario object uses invalid team id {}. Clamping to team 0.\n",
            static_cast<int>(team_num));
    return 0;
}

void fill_fixed_field(char* dst, size_t fixed_len, std::string_view src,
                      const char* field_name)
{
    if (dst == nullptr || fixed_len == 0)
        return;

    memset(dst, 0, fixed_len);
    const size_t to_copy = std::min(src.size(), fixed_len);
    memcpy(dst, src.data(), to_copy);
    if (src.size() > fixed_len)
    {
        LogWarn("Truncating {} to {} bytes for scenario serialization.\n",
                field_name, fixed_len);
    }
}

std::string ensure_png_extension(std::string_view name)
{
    std::string s(name);
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".png") == 0)
        return s;
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".pix") == 0)
        s.replace(s.size() - 4, 4, ".png");
    else
        s += ".png";
    return s;
}

void set_error(LevelFileIoError* out_error, LevelFileIoError err)
{
    if (out_error != nullptr)
        *out_error = err;
}

bool read_level_body(og::io::OgFile& infile, short version, GameWorld& world,
                     LevelFileMetadata& metadata, LevelFileIoError& err,
                     bool require_valid_grid)
{
    short currentx = 0;
    short currenty = 0;
    unsigned char temporder = 0;
    unsigned char tempfamily = 0;
    unsigned char tempteam = 0;
    char tempfacing = 0;
    char tempcommand = 0;
    char templevel = 0;
    short shortlevel = 0;
    short listsize = 0;
    char newgrid[9] = "grid";
    char new_scen_type = 0;
    std::array<char, 80> oneline{};
    char numlines = 0;
    char tempwidth = 0;
    std::array<char, 13> tempname{};
    std::array<char, 31> scentitle{};
    short temp_par = static_cast<short>(world.id);
    short temp_time_limit = 4000;

#define READ_OR_FAIL(ptr, size, n)                                             \
    do                                                                          \
    {                                                                           \
        if (!rw_read_exact_or_log(infile, (ptr), (size), (n)))                 \
        {                                                                       \
            err = LevelFileIoError::ParseFailed;                               \
            return false;                                                       \
        }                                                                       \
    } while (0)

    READ_OR_FAIL(newgrid, 8, 1);
    newgrid[8] = '\0';
    // Zardus: FIX: make sure they're lowercased
    //buffers: PORT: make sure grid name is lowercase
    lowercase(newgrid);
    metadata.grid_file = newgrid;

    if (version >= 6)
    {
        READ_OR_FAIL(scentitle.data(), 30, 1);
        world.title = std::string(scentitle.data(),
                                  strnlen(scentitle.data(), 30));
    }
    else
    {
        world.title = "New Level";
    }

    if (version >= 5)
    {
        READ_OR_FAIL(&new_scen_type, 1, 1);
        world.type = new_scen_type;
    }
    else
    {
        world.type = 0;
    }

    if (version >= 8)
        READ_OR_FAIL(&temp_par, 2, 1);
    world.par_value = temp_par;

    if (version >= 9)
        READ_OR_FAIL(&temp_time_limit, 2, 1);
    world.time_bonus_limit = temp_time_limit;

    READ_OR_FAIL(&listsize, 2, 1);
    if (listsize < 0 || listsize > kMaxScenarioObjects)
    {
        Log("Invalid scenario object count: {}\n", listsize);
        err = LevelFileIoError::ParseFailed;
        return false;
    }

    for (short i = 0; i < listsize; ++i)
    {
        READ_OR_FAIL(&temporder, 1, 1);
        READ_OR_FAIL(&tempfamily, 1, 1);
        READ_OR_FAIL(&currentx, 2, 1);
        READ_OR_FAIL(&currenty, 2, 1);
        READ_OR_FAIL(&tempteam, 1, 1);
        READ_OR_FAIL(&tempfacing, 1, 1);
        READ_OR_FAIL(&tempcommand, 1, 1);

        if (version >= 7)
            READ_OR_FAIL(&shortlevel, 2, 1);
        else if (version >= 3)
            READ_OR_FAIL(&templevel, 1, 1);

        std::string obj_name;
        if (version >= 4)
        {
            READ_OR_FAIL(tempname.data(), 12, 1);
            tempname[12] = '\0';
            obj_name = std::string(tempname.data(),
                                   strnlen(tempname.data(), 12));
        }

        const int reserved_width = (version == 2) ? 11 : 10;
        std::array<char, 20> reserved{};
        READ_OR_FAIL(reserved.data(), reserved_width, 1);

        walker* new_guy = nullptr;
        if (static_cast<Order>(temporder) == Order::Treasure)
        {
            if (version == 3)
                new_guy = world.add_ob(static_cast<Order>(temporder),
                                       tempfamily, true);
            else
                new_guy = world.add_fx_ob(static_cast<Order>(temporder),
                                          tempfamily);
        }
        else
        {
            new_guy = world.add_ob(static_cast<Order>(temporder), tempfamily);
        }

        if (new_guy == nullptr)
        {
            Log("Error creating object when loading.\n");
            err = LevelFileIoError::ParseFailed;
            return false;
        }

        new_guy->setxy(currentx, currenty);
        new_guy->team_num = sanitize_loaded_team_num(tempteam);

        if (version >= 7)
            new_guy->stats()->level = shortlevel;
        else if (version >= 3)
            new_guy->stats()->level = templevel;

        if (version >= 4)
        {
            new_guy->stats()->name = obj_name;
            if (new_guy->stats()->name.size() > 1)
                new_guy->stats()->set_bit_flags(BIT_NAMED, 1);
        }
    }

    metadata.description.clear();
    if (version >= 3)
    {
        READ_OR_FAIL(&numlines, 1, 1);
        for (short i = 0; i < numlines; ++i)
        {
            READ_OR_FAIL(&tempwidth, 1, 1);
            const int original_width = static_cast<unsigned char>(tempwidth);
            int width = original_width;
            if (width >= static_cast<int>(oneline.size()))
                width = static_cast<int>(oneline.size()) - 1;

            if (width > 0)
            {
                READ_OR_FAIL(oneline.data(), width, 1);
                oneline[static_cast<size_t>(width)] = 0;

                if (original_width > width)
                {
                    std::array<char, 256> discard{};
                    int remaining = original_width - width;
                    while (remaining > 0)
                    {
                        const int chunk = std::min(
                            remaining, static_cast<int>(discard.size()));
                        READ_OR_FAIL(discard.data(), chunk, 1);
                        remaining -= chunk;
                    }
                }
            }
            else
            {
                oneline[0] = 0;
            }

            metadata.description.emplace_back(oneline.data());
        }
    }

    const std::string gridpix = ensure_png_extension(newgrid);
    world.grid = read_pixie_file(gridpix.c_str());
    if (!world.grid.valid())
    {
        LogError("Failed to load scenario grid file: {}\n", gridpix);
        if (require_valid_grid)
        {
            err = LevelFileIoError::ParseFailed;
            return false;
        }
        world.pixmaxx = 0;
        world.pixmaxy = 0;
        return true;
    }

    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;

    if (version >= 5)
    {
        world.mysmoother.set_target(world.grid);

        for (auto& uptr : world.weaplist)
        {
            walker* w = uptr.get();
            if (w != nullptr && w->family == FAMILY_DOOR)
            {
                if (world.mysmoother.query_genre_x_y(
                        w->xpos / GRID_SIZE,
                        (w->ypos / GRID_SIZE) - 1) == TYPE_WALL)
                {
                    w->set_frame(1);
                }
            }
        }
    }

#undef READ_OR_FAIL
    return true;
}

short load_version_bridge(og::io::OgFile& infile, GameWorld* world,
                          LevelFileMetadata* metadata, short version)
{
    if (world == nullptr || metadata == nullptr)
        return 0;

    world->delete_objects();
    world->delete_grid();

    LevelFileIoError err = LevelFileIoError::None;
    if (!read_level_body(infile, version, *world, *metadata, err, false))
        return 0;

    return 1;
}

} // namespace

namespace og::data {

bool load_level(const std::string& path,
                GameWorld& world,
                LevelFileMetadata& metadata,
                LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    auto infile = og::io::og_open_read("scen/", path.c_str());
    if (!infile)
    {
        LogError("Cannot open level file for reading: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenReadFailed);
        return false;
    }

    char header[4] = {};
    char versionnumber = 0;
    if (!rw_read_exact_or_log(*infile, header, 1, 3))
    {
        set_error(out_error, LevelFileIoError::ParseFailed);
        return false;
    }

    if (std::string(header, 3) != "FSS")
    {
        LogError("File {} is not a valid scenario!\n", path);
        set_error(out_error, LevelFileIoError::InvalidHeader);
        return false;
    }

    if (!rw_read_exact_or_log(*infile, &versionnumber, 1, 1))
    {
        set_error(out_error, LevelFileIoError::ParseFailed);
        return false;
    }

    if (versionnumber < 2 || versionnumber > kScenarioVersion)
    {
        Log("Scenario {} is version-level {}, and cannot be read.\n", world.id,
            static_cast<int>(versionnumber));
        set_error(out_error, LevelFileIoError::UnsupportedVersion);
        return false;
    }

    world.clear();
    metadata.description.clear();

    LevelFileIoError io_err = LevelFileIoError::None;
    if (!read_level_body(*infile, versionnumber, world, metadata, io_err, true))
    {
        set_error(out_error, io_err);
        return false;
    }

    set_error(out_error, LevelFileIoError::None);
    return true;
}

bool load_level(const std::string& path,
                GameWorld& world,
                std::string& grid_file,
                std::list<std::string>& description,
                const std::function<void()>& prepare_for_load,
                LevelFileIoError* out_error)
{
    LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    metadata.description = description;

    if (prepare_for_load)
        prepare_for_load();

    if (!load_level(path, world, metadata, out_error))
        return false;

    grid_file = std::move(metadata.grid_file);
    description = std::move(metadata.description);
    return true;
}

namespace {

bool write_scenario_payload(og::io::OgFile& outfile,
                            std::string_view path_for_log,
                            GameWorld& world,
                            const LevelFileMetadata& metadata,
                            LevelFileIoError* out_error)
{
    auto write_field = [&](const void* src, std::size_t size,
                           std::size_t count) -> bool {
        if (outfile.write(src, size, count) != count)
        {
            Log("Failed to write scenario file: {}\n", path_for_log);
            set_error(out_error, LevelFileIoError::SerializeFailed);
            return false;
        }
        return true;
    };

    const char header[3] = {'F', 'S', 'S'};
    char temp_version = kScenarioVersion;
    char temp_grid[20] = {};
    char scentitle[30] = {};
    char filler[20] = "MSTRMSTRMSTRMSTR";

    if (!write_field(header, 3, 1) || !write_field(&temp_version, 1, 1))
        return false;

    fill_fixed_field(temp_grid, 8, metadata.grid_file, "grid_file");
    if (!write_field(temp_grid, 8, 1))
        return false;

    fill_fixed_field(scentitle, 30, world.title, "title");
    if (!write_field(scentitle, 30, 1))
        return false;

    char temp_scen_type = world.type;
    if (!write_field(&temp_scen_type, 1, 1))
        return false;

    short temp_par = world.par_value;
    if (!write_field(&temp_par, 2, 1))
        return false;

    short temp_time_limit = world.time_bonus_limit;
    if (!write_field(&temp_time_limit, 2, 1))
        return false;

    const size_t total_objects =
        world.oblist.size() + world.fxlist.size() + world.weaplist.size();
    const size_t serialized_objects =
        std::min(total_objects, static_cast<size_t>(kMaxScenarioObjects));
    if (serialized_objects != total_objects)
    {
        Log("Scenario object count {} exceeds {}, truncating on save.\n",
            total_objects, kMaxScenarioObjects);
    }

    short listsize = static_cast<short>(serialized_objects);
    if (!write_field(&listsize, 2, 1))
        return false;

    size_t remaining_objects = serialized_objects;
    auto write_object_list = [&](std::list<std::unique_ptr<walker>>& list,
                                 const char* null_label) -> bool {
        for (auto& uptr : list)
        {
            if (remaining_objects == 0)
                break;

            walker* ob = uptr.get();
            if (ob == nullptr)
            {
                Log("Unexpected nullptr {} object.\n", null_label);
                set_error(out_error, LevelFileIoError::SerializeFailed);
                return false;
            }

            unsigned char temporder =
                static_cast<unsigned char>(ob->query_order());
            char tempfamily = ob->family;
            std::int32_t currentx = ob->xpos;
            std::int32_t currenty = ob->ypos;
            char tempteam = static_cast<char>(ob->team_num);
            char tempfacing = ob->curdir;
            char tempcommand = static_cast<char>(ob->act_type);
            short shortlevel = static_cast<short>(ob->stats()->level);
            char tempname[12] = {};
            snprintf(tempname, sizeof(tempname), "%s", ob->stats()->name.c_str());

            if (!write_field(&temporder, 1, 1) ||
                !write_field(&tempfamily, 1, 1) ||
                !write_field(&currentx, 2, 1) ||
                !write_field(&currenty, 2, 1) ||
                !write_field(&tempteam, 1, 1) ||
                !write_field(&tempfacing, 1, 1) ||
                !write_field(&tempcommand, 1, 1) ||
                !write_field(&shortlevel, 2, 1) ||
                !write_field(tempname, 12, 1) ||
                !write_field(filler, 10, 1))
            {
                return false;
            }

            --remaining_objects;
        }
        return true;
    };

    if (!write_object_list(world.oblist, "regular") ||
        !write_object_list(world.fxlist, "fx") ||
        !write_object_list(world.weaplist, "weap"))
    {
        return false;
    }

    const std::uint8_t numlines =
        static_cast<std::uint8_t>(metadata.description.size());
    if (!write_field(&numlines, 1, 1))
        return false;
    for (const auto& line : metadata.description)
    {
        const size_t serialized_width = std::min<size_t>(line.size(), 0xffu);
        const std::uint8_t tempwidth =
            static_cast<std::uint8_t>(serialized_width);
        if (!write_field(&tempwidth, 1, 1))
            return false;
        if (serialized_width > 0 &&
            !write_field(line.data(), serialized_width, 1))
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool save_grid_file(const char* gridname, const PixieData& grid)
{
    std::string fullpath(gridname);
    fullpath += ".png";
    lowercase(fullpath);

    const std::string full_with_dir = "temp/pix/" + fullpath;
    if (!write_pixie_png(full_with_dir.c_str(), grid))
    {
        Log("Failed to save map file: {}\n", full_with_dir);
        return false;
    }
    return true;
}

bool save_level_scenario_file(GameWorld& world,
                              const std::string& path,
                              const LevelFileMetadata& metadata,
                              LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    auto outfile = og::io::og_open_write(path.c_str());
    if (!outfile)
    {
        Log("Could not open file for writing: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    if (!write_scenario_payload(*outfile, path, world, metadata, out_error))
        return false;

    set_error(out_error, LevelFileIoError::None);
    return true;
}

bool save_level(GameWorld& world,
                const std::string& path,
                const LevelFileMetadata& metadata,
                LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    const std::string scenario_path = std::string("temp/scen/") + path;
    if (!save_level_scenario_file(world, scenario_path, metadata, out_error))
        return false;

    if (!save_grid_file(metadata.grid_file.c_str(), world.grid))
    {
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    Log("Scenario saved.\n");
    set_error(out_error, LevelFileIoError::None);
    return true;
}

short load_version_2(og::io::OgFile& infile, GameWorld* world,
                     LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 2);
}

short load_version_3(og::io::OgFile& infile, GameWorld* world,
                     LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 3);
}

short load_version_4(og::io::OgFile& infile, GameWorld* world,
                     LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 4);
}

short load_version_5(og::io::OgFile& infile, GameWorld* world,
                     LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 5);
}

short load_version_6(og::io::OgFile& infile, GameWorld* world,
                     LevelFileMetadata* metadata, short version)
{
    return load_version_bridge(infile, world, metadata, version);
}

short load_scenario_version(og::io::OgFile& infile, GameWorld* world,
                            LevelFileMetadata* metadata, short version)
{
    if (world == nullptr)
        return 0;

    switch (version)
    {
    case 2:
        return load_version_2(infile, world, metadata);
    case 3:
        return load_version_3(infile, world, metadata);
    case 4:
        return load_version_4(infile, world, metadata);
    case 5:
        return load_version_5(infile, world, metadata);
    case 6:
    case 7:
    case 8:
    case 9:
        return load_version_6(infile, world, metadata, version);
    default:
        Log("Scenario {} is version-level {}, and cannot be read.\n", world->id,
            version);
        return 0;
    }
}

LevelFileIoError load_scenario_title_with_error(const char* filename,
                                                std::string& out_title)
{
    out_title = "none";
    if (filename == nullptr || filename[0] == '\0')
        return LevelFileIoError::OpenReadFailed;

    std::string tempfile = std::string(filename) + ".fss";
    auto infile = og::io::og_open_read("scen/", tempfile.c_str());
    if (!infile)
        return LevelFileIoError::OpenReadFailed;

    char header[4] = {};
    char versionnumber = 0;
    char gridname[8] = {};
    char buffer[31] = {};

    if (!rw_read_exact_or_log(*infile, header, 1, 3))
        return LevelFileIoError::ParseFailed;
    if (std::string(header, 3) != "FSS")
        return LevelFileIoError::InvalidHeader;
    if (!rw_read_exact_or_log(*infile, &versionnumber, 1, 1))
        return LevelFileIoError::ParseFailed;
    if (versionnumber < 6)
        return LevelFileIoError::UnsupportedVersion;
    if (!rw_read_exact_or_log(*infile, gridname, 1, 8))
        return LevelFileIoError::ParseFailed;
    if (!rw_read_exact_or_log(*infile, buffer, 1, 30))
        return LevelFileIoError::ParseFailed;
    out_title = std::string(buffer);
    return LevelFileIoError::None;
}

std::string load_scenario_title(const char* filename)
{
    std::string out_title;
    if (load_scenario_title_with_error(filename, out_title) !=
        LevelFileIoError::None)
        return "none";
    return out_title;
}

} // namespace og::data
