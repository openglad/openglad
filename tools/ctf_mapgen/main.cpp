/* CTF campaign generator.
 *
 * Produces builtin/org.openglad.ctf.glad (levels 500-509): six classics
 * adapted into capture-the-flag arenas plus four originals. The tool is
 * SDL-free: it mounts the stock gladiator campaign through the headless
 * platform layer, loads each source level with the production loader,
 * transforms it (strip exits/markers/stains/generators, re-team or strip
 * livings, author flags + per-team respawn anchors + control points),
 * paints the original grids, assembles a campaign directory, zips it, and
 * finally remounts the produced package for a full self-check pass.
 *
 * Every authored flag/anchor/control-point tile is validated with a
 * 16x16 query_grid_passable probe; the tool exits nonzero on any failure.
 *
 * Usage: ctf_mapgen [output.glad]   (default: builtin/org.openglad.ctf.glad)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "ctf_mapgen.h"

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/save_data.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

bool write_pixie_png(const char* filepath, const PixieData& data);
void io_init(int argc, char* argv[]);
void io_exit();
std::string get_user_path();

// ---------------------------------------------------------------------------
// Headless process globals (same shape as the dedicated server binary).
// ---------------------------------------------------------------------------
namespace og::runtime {

static SessionState s_mapgen_session{};
thread_local SessionState* current_session = &s_mapgen_session;
std::atomic<SessionState*> primary_session{&s_mapgen_session};
std::atomic<GameplayContext*> primary_game{&s_mapgen_session.game_};

} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 20260609u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace ctfgen {
namespace {

namespace fs = std::filesystem;

int g_errors = 0;

void fail(const std::string& message)
{
    std::fprintf(stderr, "ctf_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

// ---------------------------------------------------------------------------
// Passability probe: a living-sized (16x16) ground walker that is never
// added to the world, so probing mutates nothing.
// ---------------------------------------------------------------------------
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

// Deterministic ring search around a center tile: nearest passable tile not
// already taken. Returns {-1,-1} when nothing within max_r passes.
TilePos find_spot(GameWorld& world, walker* probe, TilePos center, int max_r,
                  const std::set<std::pair<short, short>>& taken)
{
    for (int r = 0; r <= max_r; ++r)
    {
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                const TilePos t{static_cast<short>(center.tx + dx),
                                static_cast<short>(center.ty + dy)};
                if (t.tx < 0 || t.ty < 0)
                    continue;
                if (taken.count({t.tx, t.ty}))
                    continue;
                if (tile_passable(world, probe, t))
                    return t;
            }
        }
    }
    return TilePos{};
}

walker* spawn_fx(GameWorld& world, int family, int team, TilePos at, int level)
{
    walker* fx = world.add_fx_ob(Order::Treasure, family);
    if (fx == nullptr)
    {
        fail(std::format("could not spawn treasure family {}", family));
        return nullptr;
    }
    fx->setxy(static_cast<short>(at.tx * GRID_SIZE),
              static_cast<short>(at.ty * GRID_SIZE));
    fx->set_team_num(static_cast<unsigned char>(team));
    if (level > 1 && fx->stats() != nullptr)
        fx->stats()->set_level(level);
    return fx;
}

// Authors flags, per-team anchor clusters, control points, and spice on a
// transformed (or freshly painted) world.
void dress_world(GameWorld& world, const Dressing& dress)
{
    std::unique_ptr<walker> probe = make_probe(world);
    if (probe == nullptr)
    {
        fail("could not create passability probe");
        return;
    }

    std::set<std::pair<short, short>> taken;

    // Flags: one per team at the base-zone center (nudged to passable).
    std::array<TilePos, 4> homes{};
    for (int team = 0; team < dress.team_count; ++team)
    {
        const TilePos home =
            find_spot(world, probe.get(), dress.base[team], 6, taken);
        if (home.tx < 0)
        {
            fail(std::format("no passable flag home near ({}, {}) for team {}",
                             dress.base[team].tx, dress.base[team].ty, team));
            continue;
        }
        taken.insert({home.tx, home.ty});
        homes[team] = home;
        spawn_fx(world, og::FAMILY_FLAG, team, home, dress.flag_level);
    }

    // Respawn anchors: a sheltered cluster of distinct passable tiles a few
    // steps behind the flag, on the side away from the contested middle.
    for (int team = 0; team < dress.team_count; ++team)
    {
        TilePos cluster = homes[team].tx >= 0 ? homes[team] : dress.base[team];
        if (!dress.cps.empty())
        {
            const TilePos& front = dress.cps.front();
            auto away = [](short from, short toward) -> short {
                if (from > toward)
                    return 3;
                if (from < toward)
                    return -3;
                return 0;
            };
            cluster.tx = static_cast<short>(cluster.tx +
                                            away(cluster.tx, front.tx));
            cluster.ty = static_cast<short>(cluster.ty +
                                            away(cluster.ty, front.ty));
        }
        int placed = 0;
        for (int i = 0; i < dress.anchors_per_team * 6 &&
                        placed < dress.anchors_per_team; ++i)
        {
            const TilePos t = find_spot(world, probe.get(), cluster, 8, taken);
            if (t.tx < 0)
                break;
            taken.insert({t.tx, t.ty});
            walker* anchor = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
            if (anchor == nullptr)
            {
                fail("could not spawn respawn anchor");
                break;
            }
            anchor->setxy(static_cast<short>(t.tx * GRID_SIZE),
                          static_cast<short>(t.ty * GRID_SIZE));
            anchor->set_team_num(static_cast<unsigned char>(team));
            ++placed;
        }
        if (placed < 8)
            fail(std::format("only {} anchors fit near ({}, {}) for team {}",
                             placed, dress.base[team].tx, dress.base[team].ty,
                             team));
    }

    // Control points at the contested middles (neutral team 7).
    for (const TilePos& cp_center : dress.cps)
    {
        const TilePos t = find_spot(world, probe.get(), cp_center, 6, taken);
        if (t.tx < 0)
        {
            fail(std::format("no passable control point near ({}, {})",
                             cp_center.tx, cp_center.ty));
            continue;
        }
        taken.insert({t.tx, t.ty});
        spawn_fx(world, og::FAMILY_CTF_POINT, 7, t, 1);
    }

    // Spice: consumables as tools (speed on flanks, invisibility at the
    // riskiest middle, drumsticks behind the lines).
    for (const Spice& sp : dress.spice)
    {
        const TilePos t = find_spot(world, probe.get(), sp.at, 4, taken);
        if (t.tx < 0)
        {
            fail(std::format("no passable spice tile near ({}, {})", sp.at.tx,
                             sp.at.ty));
            continue;
        }
        spawn_fx(world, sp.family, sp.team, t, sp.level);
    }
}

void apply_metadata(GameWorld& world, const char* title, short par_value)
{
    world.type = static_cast<char>(og::SCEN_TYPE_CTF); // clears CAN_EXIT bits
    world.title = title;
    if (world.title.size() > 30)
        fail(std::format("title too long: {}", world.title));
    world.par_value = par_value;
    world.time_bonus_limit = 4000;
}

og::data::LevelFileMetadata
make_metadata(int out_id, const std::vector<std::string>& description)
{
    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", out_id);
    for (const std::string& line : description)
    {
        if (line.size() > 78)
            fail(std::format("description line too long: {}", line));
        metadata.description.push_back(line);
    }
    return metadata;
}

bool save_fss(GameWorld& world, const og::data::LevelFileMetadata& metadata,
              int out_id)
{
    const std::string path =
        get_user_path() + std::format("temp/scen/scen{}.fss", out_id);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, path, metadata, &err))
    {
        fail(std::format("failed to write {}", path));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Adapted classics: load from the mounted gladiator campaign, transform,
// dress, and serialize. The grid is copied byte-for-byte from the source
// package (no recompression).
// ---------------------------------------------------------------------------
bool copy_grid_bytes(const std::string& src_grid_file, int out_id)
{
    std::string name = src_grid_file;
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".png") != 0)
        name += ".png";
    auto in = og::io::og_open_read("pix/", name.c_str());
    if (!in)
    {
        fail(std::format("cannot open source grid pix/{}", name));
        return false;
    }
    in->seek(0, 2);
    const auto size = in->tell();
    in->seek(0, 0);
    std::vector<char> bytes(static_cast<size_t>(size));
    if (in->read(bytes.data(), 1, bytes.size()) != bytes.size())
    {
        fail(std::format("cannot read source grid pix/{}", name));
        return false;
    }
    const std::string out_path =
        get_user_path() + std::format("temp/pix/scen{:04d}.png", out_id);
    std::ofstream out(out_path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out)
    {
        fail(std::format("cannot write {}", out_path));
        return false;
    }
    return true;
}

std::vector<AdaptedSpec> adapted_specs();

void build_adapted(const AdaptedSpec& spec)
{
    LevelRuntimeData level(spec.src_id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("failed to load source scen{}", spec.src_id));
        return;
    }
    GameWorld& world = level.world();

    // Pass 1: collect transforms over every entity list.
    std::vector<walker*> doomed;
    int kept_per_team[4] = {};
    auto scan_list = [&](auto& list) {
        for (auto& uptr : list)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            if (ob->stats() != nullptr)
                ob->stats()->name.clear(); // strip named NPCs

            const Order order = ob->query_order();
            const int family = ob->family();
            if (order == Order::Treasure &&
                (family == FAMILY_EXIT || family == FAMILY_STAIN))
            {
                doomed.push_back(ob);
            }
            else if (order == Order::Special && family == FAMILY_RESERVED_TEAM)
            {
                doomed.push_back(ob); // replaced by per-team clusters
            }
            else if (order == Order::Generator)
            {
                doomed.push_back(ob); // AI-only authored generators are re-added
            }
            else if (order == Order::Living)
            {
                const unsigned char team =
                    std::min<unsigned char>(ob->team_num(), 7);
                const int mapped = spec.team_map[team];
                if (mapped < 0 || spec.keep_livings_per_team == 0 ||
                    kept_per_team[mapped] >= spec.keep_livings_per_team)
                {
                    doomed.push_back(ob);
                }
                else
                {
                    ob->set_team_num(static_cast<unsigned char>(mapped));
                    ++kept_per_team[mapped];
                }
            }
        }
    };
    scan_list(world.oblist);
    scan_list(world.fxlist);
    scan_list(world.weaplist);
    for (walker* ob : doomed)
        world.remove_ob(ob);

    // Optional besieger generator on the AI-side team (team 1).
    if (spec.add_siege_tent)
    {
        walker* tent = world.add_ob(Order::Generator, FAMILY_TENT);
        if (tent == nullptr)
        {
            fail("could not spawn siege tent");
        }
        else
        {
            tent->setxy(static_cast<short>(spec.tent_at.tx * GRID_SIZE),
                        static_cast<short>(spec.tent_at.ty * GRID_SIZE));
            tent->set_team_num(1);
            if (tent->stats() != nullptr)
                tent->stats()->set_level(spec.tent_level);
        }
    }

    dress_world(world, spec.dress);
    apply_metadata(world, spec.title, spec.par_value);

    const std::string src_grid = level.grid_file;
    og::data::LevelFileMetadata metadata =
        make_metadata(spec.out_id, spec.description);
    if (!save_fss(world, metadata, spec.out_id))
        return;
    copy_grid_bytes(src_grid, spec.out_id);
    std::printf("ctf_mapgen: built %d '%s' from scen%d\n", spec.out_id,
                spec.title, spec.src_id);
}

void build_original(const OriginalSpec& spec)
{
    LevelRuntimeData level(spec.out_id, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    world.grid = spec.paint();
    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;

    dress_world(world, spec.dress);
    apply_metadata(world, spec.title, spec.par_value);

    og::data::LevelFileMetadata metadata =
        make_metadata(spec.out_id, spec.description);
    if (!save_fss(world, metadata, spec.out_id))
        return;
    const std::string grid_path =
        get_user_path() + std::format("temp/pix/scen{:04d}.png", spec.out_id);
    if (!write_pixie_png(grid_path.c_str(), world.grid))
        fail(std::format("failed to write {}", grid_path));
    std::printf("ctf_mapgen: built %d '%s' (original %dx%d)\n", spec.out_id,
                spec.title, world.grid.w, world.grid.h);
}

// ---------------------------------------------------------------------------
// Adapted-map table. Base zones were chosen from the source layouts; the
// dresser nudges every spot to the nearest passable tile.
// ---------------------------------------------------------------------------
std::vector<AdaptedSpec> adapted_specs()
{
    constexpr std::array<int, 8> kStripAll{-1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<AdaptedSpec> out;

    {
        // 500: scen25 THE LAGAREN BATTLEGROUND, 90x90, two undead armies.
        AdaptedSpec s;
        s.out_id = 500;
        s.src_id = 25;
        s.title = "CTF: LAGAREN BATTLEGROUND";
        s.team_map = {-1, 0, 1, -1, -1, -1, -1, -1};
        s.keep_livings_per_team = 12;
        s.par_value = 8;
        s.dress.team_count = 2;
        s.dress.flag_level = 5; // the epic map plays to five captures
        s.dress.base = {TilePos{68, 12}, TilePos{20, 82}, TilePos{}, TilePos{}};
        s.dress.cps = {TilePos{47, 46}};
        s.description = {
            "CAPTURE THE FLAG ON THE OLD KILLING",
            "FIELDS. TWO ARMIES OF THE RESTLESS DEAD",
            "STILL HOLD THEIR LINES; SEIZE THE ENEMY",
            "STANDARD AND DRAG IT HOME ACROSS THE",
            "BATTLEFIELD. THE CENTRAL SHRINE WAYPOINT",
            "HASTENS YOUR REINFORCEMENTS.",
            "FIRST TO FIVE CAPTURES TAKES THE FIELD.",
        };
        out.push_back(std::move(s));
    }

    {
        // 501: scen23 DUNGEON OF STARS, 70x70, four teleporter quadrants.
        AdaptedSpec s;
        s.out_id = 501;
        s.src_id = 23;
        s.title = "CTF: DUNGEON OF STARS";
        s.team_map = kStripAll;
        s.par_value = 6;
        s.dress.team_count = 4;
        s.dress.base = {TilePos{15, 11}, TilePos{54, 11}, TilePos{16, 57},
                        TilePos{53, 57}};
        s.dress.cps = {TilePos{34, 30}};
        s.description = {
            "FOUR-TEAM CAPTURE THE FLAG IN THE DEEP.",
            "EACH CREW CAMPS BESIDE AN ANCIENT",
            "TELEPORTER - RIDE THEM FOR WILD ESCAPES",
            "WITH A STOLEN FLAG. THE STAR CHAMBER",
            "WAYPOINT COMMANDS THE CROSSROADS.",
        };
        out.push_back(std::move(s));
    }

    {
        // 502: scen11 TIC AND TAC, 100x100, fort versus the open field.
        AdaptedSpec s;
        s.out_id = 502;
        s.src_id = 11;
        s.title = "CTF: TIC AND TAC";
        s.team_map = kStripAll;
        s.par_value = 6;
        s.dress.team_count = 2;
        s.dress.base = {TilePos{32, 42}, TilePos{39, 10}, TilePos{},
                        TilePos{}};
        s.dress.cps = {TilePos{36, 26}};
        s.description = {
            "CAPTURE THE FLAG BENEATH THE TWIN",
            "FORTRESS. ONE BAND HOLDS THE FIELD",
            "CAMP, THE OTHER THE CASTLE APPROACH.",
            "THE MEADOW WAYPOINT BETWEEN THEM PAYS",
            "FOR ITSELF IN FASTER RESPAWNS.",
        };
        out.push_back(std::move(s));
    }

    {
        // 503: scen42 A BORDER FORT, 30x30, garrison versus besiegers.
        AdaptedSpec s;
        s.out_id = 503;
        s.src_id = 42;
        s.title = "CTF: A BORDER FORT";
        s.team_map = {-1, -1, -1, 0, -1, -1, -1, -1};
        s.keep_livings_per_team = 5;
        s.add_siege_tent = true;
        s.tent_at = {24, 27};
        s.tent_level = 2;
        s.par_value = 4;
        s.dress.team_count = 2;
        s.dress.base = {TilePos{18, 15}, TilePos{14, 26}, TilePos{}, TilePos{}};
        s.dress.cps = {TilePos{16, 23}};
        s.description = {
            "SIEGE-STYLE CAPTURE THE FLAG.",
            "THE GARRISON KEEPS ITS BANNER IN THE",
            "COURTYARD; THE BESIEGERS RAISE THEIRS ON",
            "THE PLAIN, WHERE A WAR TENT MUSTERS",
            "FRESH RAIDERS. THE GATEHOUSE WAYPOINT",
            "DECIDES WHO DICTATES THE SIEGE.",
        };
        out.push_back(std::move(s));
    }

    {
        // 504: scen45 LAKE TACONA, 80x60, lake with teleporter island.
        AdaptedSpec s;
        s.out_id = 504;
        s.src_id = 45;
        s.title = "CTF: LAKE TACONA";
        s.team_map = kStripAll;
        s.par_value = 6;
        s.dress.team_count = 2;
        s.dress.base = {TilePos{20, 6}, TilePos{36, 48}, TilePos{}, TilePos{}};
        s.dress.cps = {TilePos{37, 30}, TilePos{3, 26}};
        s.description = {
            "CAPTURE THE FLAG AROUND LAKE TACONA.",
            "MARCH THE LONG SHORES, OR CHANCE THE",
            "TELEPORTERS THAT TOUCH THE ISLAND",
            "WAYPOINT IN THE LAKE'S HEART. THE WEST",
            "FORD WAYPOINT GUARDS THE SHORT ROAD.",
            "FLAGS LOST OVER WATER RETURN HOME.",
        };
        out.push_back(std::move(s));
    }

    {
        // 505: scen17 THE CITY OF NUTHRAM, 100x100, three district crews.
        AdaptedSpec s;
        s.out_id = 505;
        s.src_id = 17;
        s.title = "CTF: CITY OF NUTHRAM";
        s.team_map = kStripAll;
        s.par_value = 8;
        s.dress.team_count = 3;
        s.dress.base = {TilePos{12, 6}, TilePos{90, 9}, TilePos{16, 91},
                        TilePos{}};
        s.dress.cps = {TilePos{40, 50}, TilePos{70, 44}};
        s.description = {
            "THREE CREWS RUN THE STREETS OF NUTHRAM",
            "IN A CITY-WIDE GAME OF CAPTURE THE",
            "FLAG. TELEPORTERS CROSS TOWN IN A",
            "BLINK - SO DO STOLEN FLAGS. HOLD THE",
            "MARKET WAYPOINTS TO RULE THE AVENUES.",
        };
        out.push_back(std::move(s));
    }

    return out;
}

// ---------------------------------------------------------------------------
// Campaign descriptor + icon.
// ---------------------------------------------------------------------------
void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           Capture the Flag\n"
        << "version:         1\n"
        << "first_level:     500\n"
        << "suggested_power: 60\n"
        << "authors:         OpenGlad CTF\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    Steal the enemy banner and carry it\n"
        << "    home while your own still stands.\n"
        << "    Ten arenas - six reforged from the\n"
        << "    classic campaign and four built for\n"
        << "    the mode - with respawning squads,\n"
        << "    contested waypoints, and up to four\n"
        << "    teams. First to the capture limit\n"
        << "    wins the field.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 banner icon painted in the engine palette (red banner, gray
// pole, gold finial) and encoded by the engine's own PNG writer.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon;
    icon.frames = 1;
    icon.w = kSize;
    icon.h = kSize;
    icon.data = std::make_unique<unsigned char[]>(kSize * kSize);
    std::memset(icon.data.get(), 0, kSize * kSize);
    auto put = [&icon](int x, int y, unsigned char c) {
        if (x >= 0 && x < kSize && y >= 0 && y < kSize)
            icon.data[y * kSize + x] = c;
    };

    for (int y = 2; y < 30; ++y) // pole
    {
        put(7, y, 19);
        put(8, y, 21);
    }
    put(7, 2, 56); // gold finial
    put(8, 2, 57);
    for (int y = 28; y < 30; ++y) // base
        for (int x = 5; x <= 10; ++x)
            put(x, y, 17);
    for (int x = 9; x < 28; ++x) // banner with a ripple and swallowtail
    {
        const int wave = ((x / 5) % 2 == 0) ? 0 : 1;
        const int top = 3 + wave;
        const int bottom = 16 + wave;
        for (int y = top; y <= bottom; ++y)
        {
            if (x > 23 && y > top + 3 && y < bottom - 3)
                continue; // swallowtail notch
            unsigned char c = 41; // bright red ramp 40..47
            if (y < top + 3)
                c = 40;
            else if (y > bottom - 3)
                c = 43;
            if (x > 21)
                c = static_cast<unsigned char>(c + 2);
            put(x, y, c);
        }
    }
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// ---------------------------------------------------------------------------
// Self-check: remount the produced package and re-validate every level.
// ---------------------------------------------------------------------------
struct ExpectedLevel
{
    int id;
    int team_count;
};

void self_check_level(const ExpectedLevel& expected)
{
    LevelRuntimeData level(expected.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load", expected.id));
        return;
    }
    GameWorld& world = level.world();
    const auto where = std::format("self-check scen{}", expected.id);

    if (!(world.type & GameWorld::TYPE_CTF))
        fail(std::format("{}: TYPE_CTF not set (type={})", where,
                         static_cast<int>(world.type)));
    if (world.title.rfind("CTF: ", 0) != 0)
        fail(std::format("{}: title '{}' lacks CTF prefix", where, world.title));
    if (level.description.empty())
        fail(std::format("{}: empty intro description", where));

    std::unique_ptr<walker> probe = make_probe(world);
    if (probe == nullptr)
    {
        fail(std::format("{}: no probe", where));
        return;
    }

    int flags_per_team[8] = {};
    int cp_count = 0;
    int exit_count = 0;
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->query_order() != Order::Treasure)
            continue;
        const TilePos t{static_cast<short>(fx->xpos() / GRID_SIZE),
                        static_cast<short>(fx->ypos() / GRID_SIZE)};
        if (fx->family() == og::FAMILY_FLAG)
        {
            flags_per_team[std::min<unsigned char>(fx->team_num(), 7)]++;
            if (!tile_passable(world, probe.get(), t))
                fail(std::format("{}: flag tile ({}, {}) impassable", where,
                                 t.tx, t.ty));
        }
        else if (fx->family() == og::FAMILY_CTF_POINT)
        {
            ++cp_count;
            if (!tile_passable(world, probe.get(), t))
                fail(std::format("{}: control point ({}, {}) impassable",
                                 where, t.tx, t.ty));
        }
        else if (fx->family() == FAMILY_EXIT)
        {
            ++exit_count;
        }
    }

    int anchors_per_team[8] = {};
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Special ||
            ob->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = std::min<unsigned char>(ob->team_num(), 7);
        anchors_per_team[team]++;
        const TilePos t{static_cast<short>(ob->xpos() / GRID_SIZE),
                        static_cast<short>(ob->ypos() / GRID_SIZE)};
        if (!tile_passable(world, probe.get(), t))
            fail(std::format("{}: anchor ({}, {}) impassable", where, t.tx,
                             t.ty));
    }

    int flag_teams = 0;
    for (int t = 0; t < 4; ++t)
    {
        if (flags_per_team[t] == 1)
            ++flag_teams;
        else if (flags_per_team[t] != 0)
            fail(std::format("{}: team {} has {} flags", where, t,
                             flags_per_team[t]));
    }
    if (flag_teams != expected.team_count)
        fail(std::format("{}: expected {} flag teams, found {}", where,
                         expected.team_count, flag_teams));
    for (int t = 0; t < 4; ++t)
    {
        if (flags_per_team[t] == 1 && anchors_per_team[t] < 8)
            fail(std::format("{}: team {} has only {} anchors", where, t,
                             anchors_per_team[t]));
    }
    if (cp_count < 1)
        fail(std::format("{}: no control point", where));
    if (exit_count != 0)
        fail(std::format("{}: {} exit treasures survived", where, exit_count));
}

} // namespace
} // namespace ctfgen

int main(int argc, char* argv[])
{
    using namespace ctfgen;
    namespace fs = std::filesystem;

    const std::string out_glad =
        (argc > 1) ? argv[1] : "builtin/org.openglad.ctf.glad";
    const fs::path out_abs = fs::absolute(out_glad);

    // Never touch ~/.openglad: run inside a disposable config dir unless the
    // caller already redirected it.
    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("ctf_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "org.openglad.gladiator")
    {
        std::fprintf(stderr,
                     "ctf_mapgen: ERROR: stock campaign not mounted; run "
                     "next to staged assets (build dir)\n");
        io_exit();
        return 1;
    }

    cfg.load_settings();
    init_all_registries();

    og::runtime::SessionState& session = og::runtime::s_mapgen_session;
    static FixedRandom mapgen_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    session.ctx_.rng = &mapgen_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    // Assemble the campaign under <user>/temp/ (the repack layout).
    const std::string user = get_user_path();
    cleanup_unpacked_campaign();
    create_dir(user + "temp/");
    create_dir(user + "temp/scen/");
    create_dir(user + "temp/pix/");
    write_campaign_yaml(user + "temp/campaign.yaml");
    write_icon(user + "temp/icon.png");

    for (const AdaptedSpec& spec : adapted_specs())
        build_adapted(spec);
    for (const OriginalSpec& spec : original_specs())
        build_original(spec);

    // Zip and install into <user>/campaigns/ so the self-check can use the
    // production mount path.
    const std::string glad_path = user + "campaigns/org.openglad.ctf.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) !=
        ArchiveIoError::None)
    {
        fail(std::format("failed to zip campaign into {}", glad_path));
    }

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("org.openglad.ctf") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            const ExpectedLevel expectations[] = {
                {500, 2}, {501, 4}, {502, 2}, {503, 2}, {504, 2},
                {505, 3}, {506, 2}, {507, 2}, {508, 3}, {509, 4},
            };
            for (const ExpectedLevel& expected : expectations)
                self_check_level(expected);
            (void)unmount_campaign_package_with_error("org.openglad.ctf");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        std::error_code ec;
        fs::create_directories(out_abs.parent_path(), ec);
        fs::copy_file(glad_path, out_abs, fs::copy_options::overwrite_existing,
                      ec);
        if (ec)
        {
            fail(std::format("failed to copy {} -> {}: {}", glad_path,
                             out_abs.string(), ec.message()));
        }
        else
        {
            std::printf("ctf_mapgen: wrote %s (%ju bytes)\n",
                        out_abs.c_str(),
                        static_cast<uintmax_t>(fs::file_size(out_abs, ec)));
            result = 0;
        }
    }

    cleanup_unpacked_campaign();
    io_exit();
    if (!scratch.empty())
    {
        std::error_code ec;
        fs::remove_all(scratch, ec);
    }
    if (result != 0)
        std::fprintf(stderr, "ctf_mapgen: FAILED with %d error(s)\n",
                     ctfgen::g_errors);
    return result;
}
