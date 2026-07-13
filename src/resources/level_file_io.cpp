/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/resources/level_file_io.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/util.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

bool write_pixie_png(const char* filepath, const PixieData& data);

#include <openglad/resources/filesystem_sync.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using og::data::LevelFileIoError;
using og::data::LevelFileMetadata;

// v10 adds multi-floor support: per-object floor/z packed into the existing
// reserved/filler bytes, a trailing floor_count byte, and derived-name extra
// floor grid PNGs ("{grid}_f{N}.png"). v10 also carries per-placed-NPC extras
// in the same reserved block: npc_flags at [3] (bit 0 = specials disabled,
// bit 1 = guard "hold post" policy, bit 2 = SAVE_ALL "protected") and a
// uint16 delayed-spawn tick count at [4..5]; the rest of the block is
// zero-filled. v2-9 read paths are untouched
// and all-default levels still save as v9 (byte-identical). See
// docs/z-axis-design.md.
//
// v11 adds per-floor DECOR planes (BASE + DECOR tile layering): a uint8
// decor_present[floor_count] array immediately after the floor_count byte,
// plus a derived-name indexed PNG "{grid}_d{N}.png" per flagged floor
// (INCLUDING floor 0; byte = decor id from core/decordefs.h, 0 = none, same
// dims as that floor's grid). The writer downgrades to v10/v9 whenever every
// decor plane is empty, so decor-free levels re-save byte-identical (the
// parity/round-trip pin), and old engines refuse v11 cleanly instead of
// silently dropping blocking decor.
constexpr char kScenarioVersion = 11;
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

        // v10 repurposes the first 3 reserved/filler bytes for the object's
        // floor + sub-floor z. v2-9 leave these as legacy filler, so floor/z
        // default to 0 (legacy flat behavior). The floor MUST be applied
        // BEFORE setxy: the collision obmap is floor-keyed and setxy is what
        // registers the walker in it, so the old set-floor-after-setxy order
        // left every upper-story object bucketed on floor 0 — a never-moving
        // treasure there (a tower top-story EXIT) was uneatable from its own
        // floor, and a hold-post guard was a collision ghost. v<10 objects
        // stay on floor 0 and take the identical code path.
        if (version >= 10)
        {
            new_guy->set_floor(static_cast<short>(
                static_cast<unsigned char>(reserved[0])));
            short obj_z = 0;
            std::memcpy(&obj_z, &reserved[1], sizeof(short));
            new_guy->set_worldz(static_cast<float>(obj_z));
        }
        new_guy->setxy(currentx, currenty);
        new_guy->set_team_num(sanitize_loaded_team_num(tempteam));

        // Honor the authored per-object command byte — for Livings, and only
        // the GUARD command (A11). The byte has been round-tripped by the
        // writer since the original 2002 format but was read-and-dropped by
        // every loader, so authored guards roamed for 24 years. Restoring is
        // deliberately narrow: no stock campaign ships a non-zero Living
        // command (scan: 0 across gladiator/tryxian/arenas/ctf/concept), so
        // legacy levels and parity goldens are byte-identical; non-Living
        // command bytes are serialization noise (e.g. treasures default act
        // 2) and stay ignored; ACT_CONTROL is never applied from files (a
        // hostile level could steal player control).
        if (static_cast<Order>(temporder) == Order::Living &&
            tempcommand == ACT_GUARD)
            new_guy->set_act_type(ACT_GUARD);

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
            // (Floor + worldz were applied above, before setxy.)
            // v10 also carries per-placed-NPC extras: npc_flags at reserved[3]
            // (bit 0 = specials disabled, bit 1 = guard "hold post" policy,
            // bit 2 = SAVE_ALL "protected") and a uint16 delayed-spawn tick
            // count at reserved[4..5]. The v10 writer zero-fills the reserved
            // tail, so files authored before these fields read back as
            // defaults (a zero hold-post bit = the wake-on-sight policy).
            const unsigned char npc_flags =
                static_cast<unsigned char>(reserved[3]);
            new_guy->set_specials_disabled((npc_flags & 0x01u) != 0u);
            new_guy->set_guard_hold_post((npc_flags & 0x02u) != 0u);
            new_guy->set_save_all_protected((npc_flags & 0x04u) != 0u);
            std::uint16_t spawn_delay = 0;
            std::memcpy(&spawn_delay, &reserved[4], sizeof(spawn_delay));
            new_guy->set_spawn_delay(spawn_delay);
            // A delayed walker starts the level dormant. Only oblist-resident
            // orders ever go dormant: GameWorld::tick's oblist act phase is
            // what wakes them, so a weapon (weaplist) or treasure (fxlist)
            // carrying a delay would sleep forever. (The level editor still
            // draws and edits dormant walkers; only gameplay hides them.)
            const Order loaded_order = static_cast<Order>(temporder);
            if (spawn_delay > 0 && loaded_order != Order::Weapon &&
                loaded_order != Order::Treasure)
                new_guy->set_dormant(true);
        }

        // Record the authored placement as the object's level-entry spawn
        // point (the classic respawn feature revives level-authored livings
        // here). Uses the final floor: v10+ files set it above, older files
        // leave the default floor 0.
        new_guy->set_spawn_point(currentx, currenty,
                                 static_cast<std::uint8_t>(new_guy->floor()));
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
            if (!fg.valid())
            {
                // A declared floor plane that fails to load is corruption
                // (the writer always emits every plane the floor_count
                // implies): shipping the level with a void story strands
                // gameplay on an impassable floor. Hard-fail so the caller
                // (or the tower heal path) can regenerate/report instead.
                // The editor bridge (require_valid_grid == false) keeps its
                // fix-it-up leniency, mirroring the base grid above.
                LogError("Failed to load floor {} grid plane: {}\n", f, fpix);
                if (require_valid_grid)
                {
                    err = LevelFileIoError::ParseFailed;
                    return false;
                }
                continue;
            }
            world.grid_for_floor(f) = std::move(fg);
            world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
        }
    }

    // v11: decor planes. The file carries uint8 decor_present[floor_count]
    // immediately after the floor_count byte (nothing else is read from the
    // .fss stream between there and here — grids come from separate PNGs —
    // so consuming the flags after the grid block is stream-equivalent), then
    // each flagged floor loads "{grid}_dN.png". Hostile-file hardening
    // (mirrors the editor's out-of-range grid clamp / ani_count posture):
    // planes whose dims mismatch the floor's grid are dropped with a log, and
    // any byte >= DECOR_MAX clamps to DECOR_NONE. v2..v10 files never reach
    // this arm, so their decor planes stay invalid — byte-identical behavior.
    if (version >= 11)
    {
        std::vector<unsigned char> decor_present(
            static_cast<std::size_t>(loaded_floor_count), 0);
        READ_OR_FAIL(decor_present.data(), 1,
                     static_cast<std::size_t>(loaded_floor_count));
        for (int f = 0; f < loaded_floor_count; ++f)
        {
            if (decor_present[static_cast<std::size_t>(f)] == 0)
                continue;
            const std::string dname =
                std::string(newgrid.data()) + "_d" + std::to_string(f);
            const std::string dpix = ensure_png_extension(dname.c_str());
            PixieData dp = read_pixie_file(dpix.c_str());
            const PixieData& floor_grid = world.grid_for_floor(f);
            if (!dp.valid() || !floor_grid.valid() ||
                dp.w != floor_grid.w || dp.h != floor_grid.h)
            {
                Log("Dropping decor plane {}: missing or dims mismatch\n",
                    dpix);
                continue;
            }
            const std::size_t cells =
                static_cast<std::size_t>(dp.w) * static_cast<std::size_t>(dp.h);
            for (std::size_t c = 0; c < cells; ++c)
            {
                if (dp.data[c] >= DECOR_MAX)
                    dp.data[c] = DECOR_NONE;
            }
            world.decor_for_floor(f) = std::move(dp);
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

// True when `floor`'s decor plane is valid AND carries at least one nonzero
// byte. Shared by the writer's version cascade / presence flags and by
// save_level's "_dN" plane emission so the two always agree. An allocated
// all-zero plane counts as empty: erasing decor in the editor downgrades the
// format again (parity-friendly byte-identity for decor-free levels).
bool floor_decor_nonempty(const GameWorld& world, int floor)
{
    const PixieData& dp = world.decor_for_floor(floor);
    if (!dp.valid())
        return false;
    const std::size_t cells =
        static_cast<std::size_t>(dp.w) * static_cast<std::size_t>(dp.h);
    for (std::size_t c = 0; c < cells; ++c)
    {
        if (dp.data[c] != 0)
            return true;
    }
    return false;
}

bool world_has_decor(const GameWorld& world)
{
    for (int f = 0; f < world.floor_count(); ++f)
    {
        if (floor_decor_nonempty(world, f))
            return true;
    }
    return false;
}

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
    // Levels with all-default data save as v9 (byte-identical to pre-Z); only
    // genuine multi-floor levels — or levels using the per-NPC extras stored in
    // the v10 reserved-block layout (specials-disabled / guard hold-post /
    // delayed spawn / protected) — emit v10. This is the key guard against
    // re-encoding the ~180 parity scenarios.
    auto list_has_npc_extras =
        [](const std::list<std::unique_ptr<walker>>& list) {
            for (const auto& uptr : list)
            {
                const walker* ob = uptr.get();
                if (ob != nullptr &&
                    (ob->specials_disabled() || ob->guard_hold_post() ||
                     ob->save_all_protected() || ob->spawn_delay() > 0))
                    return true;
            }
            return false;
        };
    const bool has_npc_extras = list_has_npc_extras(world.oblist) ||
                                list_has_npc_extras(world.fxlist) ||
                                list_has_npc_extras(world.weaplist);
    // Version cascade: v11 only when some floor carries decor; otherwise the
    // v10/v9 downgrade above keeps decor-free levels emitting today's exact
    // bytes (protects the parity scenarios and every editor round-trip).
    const bool has_decor = world_has_decor(world);
    char temp_version = has_decor
        ? static_cast<char>(11)
        : ((world.is_multifloor() || has_npc_extras)
               ? static_cast<char>(10)
               : static_cast<char>(9));
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

            // v9: legacy "MSTR" filler (byte-identical). v10 owns the whole
            // block: [0] floor, [1..2] sub-floor z, [3] npc_flags (bit 0 =
            // specials disabled, bit 1 = guard "hold post" policy, bit 2 =
            // SAVE_ALL "protected"), [4..5] uint16 delayed-spawn ticks,
            // [6..9] zero padding reserved for future per-object fields.
            std::array<char, 10> obj_reserved{};
            if (temp_version >= 10)
            {
                obj_reserved[0] = static_cast<char>(ob->floor());
                short zval = static_cast<short>(ob->worldz());
                std::memcpy(&obj_reserved[1], &zval, sizeof(short));
                const unsigned char npc_flags = static_cast<unsigned char>(
                    (ob->specials_disabled() ? 0x01u : 0x00u) |
                    (ob->guard_hold_post() ? 0x02u : 0x00u) |
                    (ob->save_all_protected() ? 0x04u : 0x00u));
                obj_reserved[3] = static_cast<char>(npc_flags);
                const std::uint16_t spawn_delay = ob->spawn_delay();
                std::memcpy(&obj_reserved[4], &spawn_delay,
                            sizeof(spawn_delay));
            }
            else
            {
                std::memcpy(obj_reserved.data(), filler.data(),
                            obj_reserved.size());
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

    // v11: uint8 decor_present[floor_count] immediately after the floor_count
    // byte. save_level writes a "{grid}_dN.png" plane for exactly the flagged
    // floors (same floor_decor_nonempty predicate).
    if (temp_version >= 11)
    {
        for (int f = 0; f < world.floor_count(); ++f)
        {
            const std::uint8_t present =
                floor_decor_nonempty(world, f) ? 1 : 0;
            if (!write_field(&present, 1, 1))
                return false;
        }
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

    // v11 decor planes use derived names "{grid}_d{N}" (including floor 0),
    // written for exactly the floors the payload flagged as present. A level
    // whose decor was erased downgrades below v11 and simply stops writing
    // planes; any previously written "_dN" file is inert to v<=10 loaders.
    for (int f = 0; f < world.floor_count(); ++f)
    {
        if (!floor_decor_nonempty(world, f))
            continue;
        const std::string dname = metadata.grid_file + "_d" + std::to_string(f);
        if (!save_grid_file(dname.c_str(), world.decor_for_floor(f)))
        {
            set_error(out_error, LevelFileIoError::OpenWriteFailed);
            return false;
        }
    }

    Log("Scenario saved.\n");
    set_error(out_error, LevelFileIoError::None);
    return true;
}

// --- Tower Climb user-dir level pipeline (docs/tower-triple-design.md D7). ---

namespace {

// The 8-char grid-field convention for generated tower floors ("scen0701").
std::string tower_grid_name(int id)
{
    return std::format("scen{:04d}", id);
}

// Skim a .fss for its trailing floor_count byte without building a world.
// Follows read_level_body's exact stream layout (grid name, versioned
// header fields, object records, description lines, floor_count) but only
// seeks; any short read = a torn/truncated file = nullopt. Kept next to the
// writer/loader so the three stay in lockstep.
std::optional<int> skim_fss_floor_count(const std::filesystem::path& fss_path)
{
    std::ifstream in(fss_path, std::ios::binary);
    if (!in)
        return std::nullopt;

    const auto read_u8 = [&in]() -> std::optional<unsigned char> {
        char byte = 0;
        if (!in.read(&byte, 1))
            return std::nullopt;
        return static_cast<unsigned char>(byte);
    };
    const auto skip = [&in](std::streamoff count) -> bool {
        return static_cast<bool>(in.seekg(count, std::ios::cur)) &&
               in.peek() != std::char_traits<char>::eof();
    };

    std::array<char, 3> header = {};
    if (!in.read(header.data(), 3) || std::string_view(header.data(), 3) != "FSS")
        return std::nullopt;
    const auto version_byte = read_u8();
    if (!version_byte.has_value())
        return std::nullopt;
    const int version = static_cast<int>(*version_byte);
    if (version < 2 || version > kScenarioVersion)
        return std::nullopt;
    if (version < 10)
        return 1; // pre-Z formats have no floor_count byte: always one floor

    // Header: grid name 8; title 30 (v6+); type 1 (v5+); par 2 (v8+);
    // time bonus limit 2 (v9+). v10+ implies all of them.
    if (!skip(8 + 30 + 1 + 2 + 2))
        return std::nullopt;

    // Object list: 2-byte count, then fixed-width records. For v7+ each
    // record is order/family (2) + x/y (4) + team/facing/command (3) +
    // level (2) + name (12) + reserved (10) = 33 bytes.
    std::array<char, 2> listsize_bytes = {};
    if (!in.read(listsize_bytes.data(), 2))
        return std::nullopt;
    std::int16_t listsize = 0;
    std::memcpy(&listsize, listsize_bytes.data(), sizeof(listsize));
    if (listsize < 0 || listsize > kMaxScenarioObjects)
        return std::nullopt;
    if (listsize > 0 &&
        !skip(static_cast<std::streamoff>(listsize) * 33))
        return std::nullopt;

    // Description: 1-byte line count, then (1-byte width + width) per line.
    const auto numlines = read_u8();
    if (!numlines.has_value())
        return std::nullopt;
    for (int line = 0; line < static_cast<int>(*numlines); ++line)
    {
        const auto width = read_u8();
        if (!width.has_value())
            return std::nullopt;
        if (*width > 0 && !skip(static_cast<std::streamoff>(*width)))
            return std::nullopt;
    }

    const auto floor_count = read_u8();
    if (!floor_count.has_value())
        return std::nullopt;
    return std::max(1, static_cast<int>(*floor_count));
}

// A required file counts only when it exists AND is non-empty (a zero-byte
// file is the classic torn-persistence shape — e.g. an interrupted IDBFS
// sync on the web build).
bool file_present_nonempty(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) &&
           std::filesystem::file_size(path, ec) > 0;
}

} // namespace

bool save_level_to_user_dir(GameWorld& world,
                            int id,
                            const LevelFileMetadata& metadata_in,
                            LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    LevelFileMetadata metadata = metadata_in;
    if (metadata.grid_file.empty())
        metadata.grid_file = tower_grid_name(id);

    const std::string user = get_user_path();
    if (!create_dir(user + "scen") || !create_dir(user + "pix"))
    {
        Log("save_level_to_user_dir: cannot create user scen/pix dirs\n");
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    // Same .fss payload writer as save_level, minus the temp/ staging prefix:
    // the file lands directly where the og_file fallback chain loads it from.
    const std::string fss = user + std::format("scen/scen{}.fss", id);
    if (!save_level_scenario_file(world, fss, metadata, out_error))
        return false;

    // Floor 0 grid, then extra floors by derived name "{grid}_f{N}", then
    // decor planes "{grid}_d{N}" for exactly the floors the payload flagged
    // as present — mirrors save_level's emission set byte-for-byte.
    const std::string pix_base = user + "pix/" + metadata.grid_file;
    if (!write_pixie_png((pix_base + ".png").c_str(), world.grid))
    {
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const std::string p = std::format("{}_f{}.png", pix_base, f);
        if (!write_pixie_png(p.c_str(), world.grid_for_floor(f)))
        {
            set_error(out_error, LevelFileIoError::OpenWriteFailed);
            return false;
        }
    }
    for (int f = 0; f < world.floor_count(); ++f)
    {
        if (!floor_decor_nonempty(world, f))
            continue;
        const std::string p = std::format("{}_d{}.png", pix_base, f);
        if (!write_pixie_png(p.c_str(), world.decor_for_floor(f)))
        {
            set_error(out_error, LevelFileIoError::OpenWriteFailed);
            return false;
        }
    }

    // Persist through IDBFS on Emscripten (native no-op) — tower floors must
    // survive a reload the same way save0 does.
    sync_filesystem();

    set_error(out_error, LevelFileIoError::None);
    return true;
}

bool tower_floor_files_exist(int id)
{
    namespace fs = std::filesystem;
    const std::string user = get_user_path();
    const fs::path fss_path =
        fs::path(user) / "scen" / std::format("scen{}.fss", id);
    const fs::path pix_dir = fs::path(user) / "pix";
    const std::string grid = tower_grid_name(id);
    if (!file_present_nonempty(fss_path) ||
        !file_present_nonempty(pix_dir / (grid + ".png")))
        return false;

    // Torn-set detection: the .fss's floor_count byte implies a "{grid}_fN"
    // plane per extra floor; a missing (or empty, or truncated-fss) member
    // must read as ABSENT so ensure_floor_files regenerates the whole floor
    // from (seed, N) instead of the loader shipping an impassable void
    // story. Decor ("_dN") planes are deliberately NOT required: the writer
    // skips empty planes, so absence is ambiguous — the loader treats decor
    // loss as cosmetic and drops it with a log.
    const std::optional<int> floor_count = skim_fss_floor_count(fss_path);
    if (!floor_count.has_value())
        return false;
    for (int f = 1; f < *floor_count; ++f)
    {
        if (!file_present_nonempty(
                pix_dir / std::format("{}_f{}.png", grid, f)))
            return false;
    }
    return true;
}

bool delete_tower_floor_files(int id)
{
    namespace fs = std::filesystem;
    const std::string user = get_user_path();
    std::error_code ec;
    bool removed_any = false;

    if (fs::remove(fs::path(user) / "scen" / std::format("scen{}.fss", id), ec))
        removed_any = true;

    // The base grid PNG plus every derived plane ("{grid}_fN" floor grids,
    // "{grid}_dN" decor). A directory scan handles any floor count without
    // baking a cap in here.
    const std::string grid = tower_grid_name(id);
    const fs::path pix_dir = fs::path(user) / "pix";
    std::error_code iter_ec;
    for (const auto& entry : fs::directory_iterator(pix_dir, iter_ec))
    {
        const std::string name = entry.path().filename().string();
        const bool base_grid = (name == grid + ".png");
        const bool derived_plane =
            (name.rfind(grid + "_f", 0) == 0 || name.rfind(grid + "_d", 0) == 0) &&
            name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0;
        if (!base_grid && !derived_plane)
            continue;
        if (fs::remove(entry.path(), ec))
            removed_any = true;
    }
    return removed_any;
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
    case 10:
    case 11:
        // read_level_body branches on the version it is handed, so the v6
        // bridge covers every later single-pass layout including v10/v11.
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
