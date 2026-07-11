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
#include <openglad/resources/io_common.h>
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

// v10 adds multi-floor support: per-object floor/z packed into the existing
// reserved/filler bytes, a trailing floor_count byte, and derived-name extra
// floor grid PNGs ("{grid}_f{N}.png"). v2-9 read paths are untouched and
// single-floor levels still save as v9 (byte-identical). See docs/z-axis-design.md.
constexpr char kScenarioVersion = 10;
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
    const bool ends_with_legacy_pix_ext =
        s.size() >= 4 &&
        s[s.size() - 4] == '.' &&
        s[s.size() - 3] == 'p' &&
        s[s.size() - 2] == 'i' &&
        s[s.size() - 1] == 'x';
    if (ends_with_legacy_pix_ext)
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
    std::array<char, 9> newgrid = {'g', 'r', 'i', 'd'};
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

    READ_OR_FAIL(newgrid.data(), 8, 1);
    newgrid[8] = '\0';
    // Zardus: FIX: make sure they're lowercased
    //buffers: PORT: make sure grid name is lowercase
    lowercase(newgrid.data());
    metadata.grid_file = newgrid.data();
    if (!is_safe_virtual_basename(metadata.grid_file, 32))
    {
        LogError("Rejected unsafe scenario grid file name: {}\n",
                 metadata.grid_file);
        err = LevelFileIoError::ParseFailed;
        return false;
    }

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
    world.ctf = {};

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
        new_guy->set_team_num(sanitize_loaded_team_num(tempteam));

        if (version >= 7)
            new_guy->stats()->set_level(shortlevel);
        else if (version >= 3)
            new_guy->stats()->set_level(templevel);

        if (version >= 4)
        {
            new_guy->stats()->name = obj_name;
            if (new_guy->stats()->name.size() > 1)
                new_guy->stats()->set_bit_flags(BIT_NAMED, 1);
        }

        if (version >= 10)
        {
            // v10 repurposes the first 3 reserved/filler bytes for the object's
            // floor + sub-floor z. v2-9 leave these as legacy filler, so
            // floor/z default to 0 (legacy flat behavior).
            new_guy->set_floor(static_cast<short>(
                static_cast<unsigned char>(reserved[0])));
            short obj_z = 0;
            std::memcpy(&obj_z, &reserved[1], sizeof(short));
            new_guy->set_worldz(static_cast<float>(obj_z));
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

    short loaded_floor_count = 1;
    if (version >= 10)
    {
        unsigned char fc = 1;
        READ_OR_FAIL(&fc, 1, 1);
        loaded_floor_count = (fc < 1) ? 1 : static_cast<short>(fc);
    }

    const std::string gridpix = ensure_png_extension(newgrid.data());
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
            if (w != nullptr && w->family() == FAMILY_DOOR)
            {
                if (world.mysmoother.query_genre_x_y(
                        w->xpos() / GRID_SIZE,
                        (w->ypos() / GRID_SIZE) - 1) == TYPE_WALL)
                {
                    w->set_frame(1);
                }
            }
        }
    }

    if (version >= 10 && loaded_floor_count > 1)
    {
        world.set_floor_count(loaded_floor_count);
        for (int f = 1; f < loaded_floor_count; ++f)
        {
            const std::string fname =
                std::string(newgrid.data()) + "_f" + std::to_string(f);
            const std::string fpix = ensure_png_extension(fname.c_str());
            PixieData fg = read_pixie_file(fpix.c_str());
            if (fg.valid())
            {
                world.grid_for_floor(f) = std::move(fg);
                world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
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
    if (!is_safe_virtual_basename(path, 64))
    {
        LogError("Rejected unsafe level file name for load: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenReadFailed);
        return false;
    }

    auto infile = og::io::og_open_read("scen/", path.c_str());
    if (!infile)
    {
        LogError("Cannot open level file for reading: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenReadFailed);
        return false;
    }

    std::array<char, 4> header = {};
    char versionnumber = 0;
    if (!rw_read_exact_or_log(*infile, header.data(), 1, 3))
    {
        set_error(out_error, LevelFileIoError::ParseFailed);
        return false;
    }

    if (std::string(header.data(), 3) != "FSS")
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

    const std::array<char, 3> header = {'F', 'S', 'S'};
    // Single-floor levels save as v9 (byte-identical to pre-Z); only genuine
    // multi-floor levels emit v10. This is the key guard against re-encoding the
    // ~180 parity scenarios.
    char temp_version = world.is_multifloor() ? static_cast<char>(10)
                                              : static_cast<char>(9);
    std::array<char, 20> temp_grid = {};
    std::array<char, 30> scentitle = {};
    std::array<char, 20> filler = {'M', 'S', 'T', 'R', 'M', 'S', 'T', 'R',
                                   'M', 'S', 'T', 'R', 'M', 'S', 'T', 'R'};

    if (!write_field(header.data(), 3, 1) || !write_field(&temp_version, 1, 1))
        return false;

    fill_fixed_field(temp_grid.data(), 8, metadata.grid_file, "grid_file");
    if (!write_field(temp_grid.data(), 8, 1))
        return false;

    fill_fixed_field(scentitle.data(), 30, world.title, "title");
    if (!write_field(scentitle.data(), 30, 1))
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
    auto write_object_list = [&](const std::list<std::unique_ptr<walker>>& list,
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
            char tempfamily = ob->family();
            std::int32_t currentx = ob->xpos();
            std::int32_t currenty = ob->ypos();
            char tempteam = static_cast<char>(ob->team_num());
            char tempfacing = ob->curdir();
            char tempcommand = static_cast<char>(ob->act_type());
            short shortlevel = static_cast<short>(ob->stats()->level());
            std::array<char, 12> tempname = {};
            snprintf(tempname.data(), tempname.size(), "%s", ob->stats()->name.c_str());

            // v9: legacy "MSTR" filler (byte-identical). v10: first 3 bytes hold
            // the object's floor + sub-floor z.
            std::array<char, 10> obj_reserved{};
            std::memcpy(obj_reserved.data(), filler.data(), obj_reserved.size());
            if (temp_version >= 10)
            {
                obj_reserved[0] = static_cast<char>(ob->floor());
                short zval = static_cast<short>(ob->worldz());
                std::memcpy(&obj_reserved[1], &zval, sizeof(short));
            }

            if (!write_field(&temporder, 1, 1) ||
                !write_field(&tempfamily, 1, 1) ||
                !write_field(&currentx, 2, 1) ||
                !write_field(&currenty, 2, 1) ||
                !write_field(&tempteam, 1, 1) ||
                !write_field(&tempfacing, 1, 1) ||
                !write_field(&tempcommand, 1, 1) ||
                !write_field(&shortlevel, 2, 1) ||
                !write_field(tempname.data(), 12, 1) ||
                !write_field(obj_reserved.data(), 10, 1))
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

    if (temp_version >= 10)
    {
        std::uint8_t floor_count_byte =
            static_cast<std::uint8_t>(world.floor_count());
        if (!write_field(&floor_count_byte, 1, 1))
            return false;
    }

    return true;
}

} // namespace

bool save_grid_file(const char* gridname, const PixieData& grid)
{
    if (gridname == nullptr || !is_safe_virtual_basename(gridname, 32))
    {
        Log("Rejected unsafe grid file name for save: {}\n",
            gridname == nullptr ? "" : gridname);
        return false;
    }
    std::string fullpath(gridname);
    fullpath += ".png";
    //buffers: PORT: make sure grid name is lowercase
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
    if (!is_safe_virtual_basename(path, 64))
    {
        Log("Rejected unsafe scenario file name for save: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    const std::string scenario_path = std::string("temp/scen/") + path;
    if (!save_level_scenario_file(world, scenario_path, metadata, out_error))
        return false;

    if (!save_grid_file(metadata.grid_file.c_str(), world.grid))
    {
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    // Extra floor grids use derived names "{grid}_f{N}".
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const std::string fname = metadata.grid_file + "_f" + std::to_string(f);
        if (!save_grid_file(fname.c_str(), world.grid_for_floor(f)))
        {
            set_error(out_error, LevelFileIoError::OpenWriteFailed);
            return false;
        }
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
    if (!is_safe_virtual_basename(filename, 64))
        return LevelFileIoError::OpenReadFailed;

    std::string tempfile = std::string(filename) + ".fss";
    auto infile = og::io::og_open_read("scen/", tempfile.c_str());
    if (!infile)
        return LevelFileIoError::OpenReadFailed;

    std::array<char, 4> header = {};
    char versionnumber = 0;
    std::array<char, 8> gridname = {};
    std::array<char, 31> buffer = {};

    if (!rw_read_exact_or_log(*infile, header.data(), 1, 3))
        return LevelFileIoError::ParseFailed;
    if (std::string(header.data(), 3) != "FSS")
        return LevelFileIoError::InvalidHeader;
    if (!rw_read_exact_or_log(*infile, &versionnumber, 1, 1))
        return LevelFileIoError::ParseFailed;
    if (versionnumber < 6)
        return LevelFileIoError::UnsupportedVersion;
    if (!rw_read_exact_or_log(*infile, gridname.data(), 1, 8))
        return LevelFileIoError::ParseFailed;
    if (!rw_read_exact_or_log(*infile, buffer.data(), 1, 30))
        return LevelFileIoError::ParseFailed;
    out_title = std::string(buffer.data());
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
