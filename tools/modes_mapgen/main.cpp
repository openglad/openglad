/* Multiplayer Game Modes campaign generator.
 *
 * Produces campaigns/modes/ (the source tree the build
 * composes into builtin/modes.glad): the 39-scenario seven-mode
 * campaign (TDM 300-305 absorbing the arenas grids, CTF 500-509 keeping
 * the shipped CTF maps, Onslaught 800-803, Soccer 820-823, Basketball
 * 824-828, Mutant 840-843, Free For All 850-855), every level typed
 * SCEN_TYPE_SCRIPTED — the mode rules live in the campaign's embedded
 * Lua pack. This tool assembles the package (yaml + icon + pack tree +
 * 39 built levels), regenerates the committed
 * level manifest (pack/lib/mode_levels.lua) from the same tables that
 * build the maps, zips, remounts, and hard-fails on any self-check
 * violation before exporting the campaign tree.
 *
 * Usage: modes_mapgen [output-dir]  (default: campaigns/modes)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "../campaign_export.h"
#include "modes_mapgen.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/lobby_state.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
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
    static std::uint32_t state = 20260803u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace modesgen {
namespace {

namespace fs = std::filesystem;

constexpr const char* kCampaignId = "modes";
constexpr const char* kPackId = "modes.core";

// ---------------------------------------------------------------------------
// Campaign descriptor + icon.
// ---------------------------------------------------------------------------
void write_campaign_yaml(const std::string& path)
{
    // `matchup: versus` is the campaign-level versus marker the picker/lobby
    // accessors key on (unknown keys are ignored by older readers).
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           Multiplayer Game Modes\n"
        << "version:         1\n"
        << "first_level:     300\n"
        << "suggested_power: 60\n"
        << "matchup:         versus\n"
        << "authors:         OpenGlad\n"
        << "contributors:    Forgotten Sages (arena grids)\n"
        << "\n"
        << "description:     |\n"
        << "    One arena, seven games, one\n"
        << "    book. The Gamesmaster calls\n"
        << "    team deathmatch, capture the\n"
        << "    flag, onslaught, mutant,\n"
        << "    soccer, basketball, and free\n"
        << "    for all across thirty-nine\n"
        << "    fields old and new. The bots\n"
        << "    know the rules. Respawns honor\n"
        << "    your difficulty. First to the\n"
        << "    target score takes the purse.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// The quartered games shield: four team-color quadrants under a gray
// shield border, the Gamesmaster's gold coin as the boss.
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
            icon.data[static_cast<std::size_t>(y * kSize + x)] = c;
    };

    // Shield border: 2px, dark outer ring, mid inner ring.
    for (int y = 2; y < 30; ++y)
        for (int x = 3; x < 29; ++x)
        {
            const bool outer = (y == 2 || y == 29 || x == 3 || x == 28);
            const bool inner = (y == 3 || y == 28 || x == 4 || x == 27);
            if (outer)
                put(x, y, 19); // GRAY_DARK
            else if (inner)
                put(x, y, 21); // GRAY_MID
        }

    // Quadrants: red / green / blue / yellow ramps, shaded at the bottom.
    const struct { int x0, y0, x1, y1; unsigned char base; } quads[] = {
        {5, 4, 15, 15, 40},   // red
        {16, 4, 26, 15, 56},  // green
        {5, 16, 15, 27, 72},  // blue
        {16, 16, 26, 27, 88}, // yellow
    };
    for (const auto& q : quads)
        for (int y = q.y0; y <= q.y1; ++y)
            for (int x = q.x0; x <= q.x1; ++x)
            {
                unsigned char c = static_cast<unsigned char>(q.base + 1);
                if (y >= q.y1 - 2)
                    c = static_cast<unsigned char>(q.base + 3);
                put(x, y, c);
            }

    // Center boss: the coin, gold with a shadow ring.
    for (int y = 12; y <= 19; ++y)
        for (int x = 12; x <= 19; ++x)
        {
            const int dx = 2 * x - 31;
            const int dy = 2 * y - 31;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= 25)
                put(x, y, (x + y) % 2 ? 88 : 89);
            else if (d2 <= 49)
                put(x, y, 17); // GRAY_SHADOW ring
        }

    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// ---------------------------------------------------------------------------
// Pack tree: byte-copy kPackSourceDir/** (Lua, sprites, sidecars) into the
// staging dir. The mode scripts/libs land with the Lua-mode waves;
// whatever exists is shipped.
// ---------------------------------------------------------------------------
void copy_pack_tree(const std::string& staging_root)
{
    const fs::path src = kPackSourceDir;
    const fs::path dst = fs::path(staging_root) / "packs" / kPackId;
    std::error_code ec;
    for (const char* sub : {"lib", "families", "scripts", "sprites"})
    {
        fs::create_directories(dst / sub, ec);
        if (ec)
            fail(std::format("cannot create pack staging dir {}: {}",
                             (dst / sub).string(), ec.message()));
    }
    for (const auto& entry : fs::recursive_directory_iterator(src))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".lua" && ext != ".png" && ext != ".json")
            continue; // skip .gitkeep and friends
        const fs::path rel = fs::relative(entry.path(), src);
        fs::create_directories((dst / rel).parent_path(), ec);
        fs::copy_file(entry.path(), dst / rel,
                      fs::copy_options::overwrite_existing, ec);
        if (ec)
            fail(std::format("cannot stage pack file {}: {}", rel.string(),
                             ec.message()));
    }
}

// ---------------------------------------------------------------------------
// Self-check: remount the produced package and re-validate every level
// against its ExpectedLevel row.
// ---------------------------------------------------------------------------

// Obmap peak ledger (§2.3 model): authored ground load + capped spawns +
// 16 heroes + 20 corpse/stain transients + 25 projectiles (+ the soccer
// ball; + basketball's ball AND its shadow fx, D11, AND one hoop sprite
// per authored hoop, D29/D32 — the peak activation, even when a 2/3-team
// game on a 4-hoop court spawns fewer). Must stay <= 190 unless the row
// carries the documented waiver.
int obmap_ledger(const ExpectedLevel& row)
{
    int gens = 0;
    for (const int g : row.generators_per_team)
        gens += g;
    int caps = 0;
    for (const SpawnCap& cap : row.spawn_caps)
        caps += cap.cap;
    int ball = 0;
    if (row.mode == ModeKind::Soccer)
        ball = 1;
    else if (row.mode == ModeKind::Basketball)
        ball = 2 + static_cast<int>(row.hoops.size()); // ball + shadow + rims
    return gens + row.treasures + row.flags + row.control_points + row.doors +
           row.authored_livings + caps + 16 + 20 + 25 + ball;
}

// A 2x2-tile clearance probe: some 2x2 tile block containing the tile is
// fully passable (the deploy formation needs elbow room at the lead).
bool lead_clearance(GameWorld& world, walker* probe, TilePos t)
{
    for (int oy = -1; oy <= 0; ++oy)
        for (int ox = -1; ox <= 0; ++ox)
        {
            bool ok = true;
            for (int dy = 0; dy <= 1 && ok; ++dy)
                for (int dx = 0; dx <= 1 && ok; ++dx)
                    ok = tile_passable(
                        world, probe,
                        {static_cast<short>(t.tx + ox + dx),
                         static_cast<short>(t.ty + oy + dy)});
            if (ok)
                return true;
        }
    return false;
}

void self_check_level(const ExpectedLevel& row)
{
    LevelRuntimeData level(row.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load", row.id));
        return;
    }
    GameWorld& world = level.world();
    const auto where = std::format("self-check scen{}", row.id);

    // 1. Structure: the scripted type byte ONLY (no CAN_EXIT/SAVE_ALL/CTF/
    // tower bits), exact title/par/dims, single floor.
    if (world.type != SCEN_TYPE_SCRIPTED)
        fail(std::format("{}: type {} != SCEN_TYPE_SCRIPTED", where,
                         static_cast<int>(world.type)));
    if (world.title != row.title)
        fail(std::format("{}: title '{}' != '{}'", where, world.title,
                         row.title));
    if (world.title.size() > 30)
        fail(std::format("{}: title exceeds 30 bytes", where));
    if (world.par_value != row.par)
        fail(std::format("{}: par {} != {}", where, world.par_value,
                         row.par));
    if (world.floor_count() != 1)
        fail(std::format("{}: floor_count {} != 1", where,
                         world.floor_count()));
    if (world.grid.w != row.grid_w || world.grid.h != row.grid_h)
        fail(std::format("{}: grid {}x{} != {}x{}", where, world.grid.w,
                         world.grid.h, row.grid_w, row.grid_h));

    // Briefing: exact text, budget, sign-off.
    const std::vector<std::string> description(level.description.begin(),
                                               level.description.end());
    if (description != row.briefing)
        fail(std::format("{}: briefing does not match the authored text",
                         where));
    for (const std::string& line : description)
        if (line.size() > 33)
            fail(std::format("{}: briefing line '{}' overflows 33", where,
                             line));
    if (description.empty() || description.back() != "-- THE GAMESMASTER")
        fail(std::format("{}: briefing must end '-- THE GAMESMASTER'",
                         where));

    // Decor plane: well-formed ids/dims, never over air/stair/void, and
    // the exact nonzero-cell pin.
    int decor_cells = 0;
    const PixieData& dec = world.decor;
    if (dec.valid())
    {
        if (dec.w != world.grid.w || dec.h != world.grid.h)
            fail(std::format("{}: decor plane dims mismatch", where));
        for (int ty = 0; ty < dec.h; ++ty)
            for (int tx = 0; tx < dec.w; ++tx)
            {
                const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
                if (d == DECOR_NONE)
                    continue;
                ++decor_cells;
                if (d >= DECOR_MAX)
                    fail(std::format("{}: decor id {} out of range at "
                                     "({}, {})", where, d, tx, ty));
                const unsigned char base =
                    world.grid.data[static_cast<std::size_t>(tx + ty * world.grid.w)];
                if (base == PIX_AIR || base == PIX_ZSTAIR_UP ||
                    base == PIX_ZSTAIR_DOWN || base == PIX_VOID1)
                    fail(std::format("{}: decor {} over air/stair/void at "
                                     "({}, {})", where, d, tx, ty));
            }
    }
    if (decor_cells != row.decor_cells)
        fail(std::format("{}: {} decor cells, expected {}", where,
                         decor_cells, row.decor_cells));

    std::unique_ptr<walker> probe = make_probe(world);
    if (probe == nullptr)
    {
        fail(std::format("{}: no passability probe", where));
        return;
    }

    // 2-5. Inventory sweep.
    int markers_per_team[8] = {};
    std::array<TilePos, 8> lead{};
    int flags_per_team[8] = {};
    int cps = 0;
    int exits = 0;
    int stains = 0;
    int teleporters = 0;
    int treasures = 0;
    int doors = 0;
    int other_weapons = 0;
    int livings = 0;
    int gens_per_team[8] = {};
    int named = 0;
    int save_protected = 0;
    int guard_violations = 0;
    std::vector<ItemPad> actual_pads;

    auto team_of = [](walker* ob) {
        return std::min<int>(ob->team_num(), 7);
    };
    auto sweep = [&](auto& list) {
        for (const auto& uptr : list)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const Order order = ob->query_order();
            const int family = ob->family();
            const int team = team_of(ob);
            const TilePos t{static_cast<short>(ob->xpos() / GRID_SIZE),
                            static_cast<short>(ob->ypos() / GRID_SIZE)};
            if (ob->stats() != nullptr && !ob->stats()->name.empty())
                ++named;
            if (ob->save_all_protected())
                ++save_protected;
            if (order == Order::Special && family == FAMILY_RESERVED_TEAM)
            {
                if (markers_per_team[team] == 0)
                    lead[static_cast<std::size_t>(team)] = t;
                ++markers_per_team[team];
                if (!tile_passable(world, probe.get(), t))
                    fail(std::format("{}: marker ({}, {}) impassable", where,
                                     t.tx, t.ty));
            }
            else if (order == Order::Treasure)
            {
                if (family == modesgen::kFlagFamily)
                {
                    ++flags_per_team[team];
                    if (!tile_passable(world, probe.get(), t))
                        fail(std::format("{}: flag ({}, {}) impassable",
                                         where, t.tx, t.ty));
                }
                else if (family == modesgen::kWaypointFamily)
                {
                    ++cps;
                    if (!tile_passable(world, probe.get(), t))
                        fail(std::format("{}: waypoint ({}, {}) impassable",
                                         where, t.tx, t.ty));
                }
                else if (family == FAMILY_EXIT)
                    ++exits;
                else if (family == FAMILY_STAIN)
                    ++stains;
                else
                {
                    ++treasures;
                    if (family == FAMILY_TELEPORTER)
                        ++teleporters;
                    if (family == FAMILY_DRUMSTICK ||
                        family == FAMILY_MAGIC_POTION ||
                        family == FAMILY_INVIS_POTION ||
                        family == FAMILY_SPEED_POTION)
                    {
                        actual_pads.push_back({family, t});
                    }
                }
            }
            else if (order == Order::Weapon)
            {
                if (family == FAMILY_DOOR)
                    ++doors;
                else
                    ++other_weapons;
            }
            else if (order == Order::Living)
            {
                ++livings;
                if (team <= 1 && ob->act_type() == ACT_GUARD &&
                    !ob->guard_hold_post())
                    ++guard_violations;
            }
            else if (order == Order::Generator)
                ++gens_per_team[team];
        }
    };
    sweep(world.oblist);
    sweep(world.fxlist);
    sweep(world.weaplist);

    for (int team = 0; team < 8; ++team)
    {
        const int expect =
            (team < row.team_count) ? row.markers_per_team : 0;
        if (markers_per_team[team] != expect)
            fail(std::format("{}: team {} has {} markers, expected {}",
                             where, team, markers_per_team[team], expect));
        if (team < row.team_count &&
            !lead_clearance(world, probe.get(), lead[static_cast<std::size_t>(team)]))
            fail(std::format("{}: team {} lead marker ({}, {}) lacks 2x2 "
                             "clearance", where, team, lead[static_cast<std::size_t>(team)].tx,
                             lead[static_cast<std::size_t>(team)].ty));
    }
    int total_flags = 0;
    for (int team = 0; team < 8; ++team)
        total_flags += flags_per_team[team];
    if (total_flags != row.flags)
        fail(std::format("{}: {} flags, expected {}", where, total_flags,
                         row.flags));
    if (row.mode == ModeKind::Ctf)
        for (int team = 0; team < row.team_count; ++team)
            if (flags_per_team[team] != 1)
                fail(std::format("{}: team {} has {} flags, expected 1",
                                 where, team, flags_per_team[team]));
    if (cps != row.control_points)
        fail(std::format("{}: {} waypoints, expected {}", where, cps,
                         row.control_points));
    if (row.mode == ModeKind::Ctf && cps < 1)
        fail(std::format("{}: CTF level without a control point", where));
    if (exits != 0)
        fail(std::format("{}: {} exit treasures (MP levels ship none)",
                         where, exits));
    if (stains != 0)
        fail(std::format("{}: {} stains survived", where, stains));
    if (row.mode == ModeKind::Mutant && teleporters != 0)
        fail(std::format("{}: {} teleporter pads on a Mutant map", where,
                         teleporters));
    if (treasures != row.treasures)
        fail(std::format("{}: {} treasures, expected {}", where, treasures,
                         row.treasures));
    if (doors != row.doors)
        fail(std::format("{}: {} doors, expected {}", where, doors,
                         row.doors));
    if (other_weapons != row.other_weapons)
        fail(std::format("{}: {} non-door weapons, expected {}", where,
                         other_weapons, row.other_weapons));
    if (livings != row.authored_livings)
        fail(std::format("{}: {} authored livings, expected {}", where,
                         livings, row.authored_livings));
    if (livings > 120)
        fail(std::format("{}: {} livings exceed the 120 authoring cap",
                         where, livings));
    for (int team = 0; team < 8; ++team)
        if (gens_per_team[team] != row.generators_per_team[static_cast<std::size_t>(team)])
            fail(std::format("{}: team {} has {} generators, expected {}",
                             where, team, gens_per_team[team],
                             row.generators_per_team[static_cast<std::size_t>(team)]));
    if (named != 0)
        fail(std::format("{}: {} named NPCs survived", where, named));
    if (save_protected != 0)
        fail(std::format("{}: {} protected (SAVE_ALL) walkers", where,
                         save_protected));
    if (guard_violations != 0)
        fail(std::format("{}: {} allied ACT_GUARD livings without "
                         "hold-post", where, guard_violations));

    // Respawnable-item pads (D8 single source): on the item-respawning
    // modes the world's live treasures of the four respawnable families
    // must be EXACTLY the row's item_pads (same multiset of (family,
    // tile)). This is what keeps the vendored CTF/arenas transcriptions
    // honest and prevents canvas-builder drift — on mismatch the actual
    // multiset is printed as a paste-ready initializer.
    //
    // Soccer and Basketball used to be OFF here alongside Onslaught, on
    // the ruling that "Soccer's short respawn already regulates
    // attrition" (Basketball extended it as D2). The #225 playtest
    // reversed that: both ball games turned into a constant hunt for the
    // chicken, because the ball keeps everyone moving and fighting while
    // the authored food is eaten once and never returns. Both bands now
    // carry drumstick pads and join the exact-multiset pin, so every
    // authored drumstick on 820-828 must appear in its row.
    //
    // Onslaught stays OFF with its original rationale intact: its spawn
    // attrition IS the mode, and its mode-var budget is full.
    if (row.mode == ModeKind::Onslaught)
    {
        if (!row.item_pads.empty() || row.item_interval != 0)
            fail(std::format("{}: {} mode ships no item respawns; drop the "
                             "item_pads/item_interval row fields", where,
                             mode_name(row.mode)));
    }
    else
    {
        auto pad_key = [](const ItemPad& p) {
            return (p.family << 20) | (p.at.ty << 10) | p.at.tx;
        };
        auto by_key = [&pad_key](const ItemPad& a, const ItemPad& b) {
            return pad_key(a) < pad_key(b);
        };
        std::vector<ItemPad> expected = row.item_pads;
        std::sort(actual_pads.begin(), actual_pads.end(), by_key);
        std::sort(expected.begin(), expected.end(), by_key);
        bool match = actual_pads.size() == expected.size();
        for (std::size_t i = 0; match && i < expected.size(); ++i)
            match = pad_key(actual_pads[i]) == pad_key(expected[i]);
        if (!match)
        {
            fail(std::format("{}: item_pads do not match the world's "
                             "respawnable treasures ({} authored, {} in the "
                             "row); transcribe the initializer below",
                             where, actual_pads.size(), expected.size()));
            auto family_token = [](int family) {
                switch (family)
                {
                    case FAMILY_DRUMSTICK: return "FAMILY_DRUMSTICK";
                    case FAMILY_MAGIC_POTION: return "FAMILY_MAGIC_POTION";
                    case FAMILY_INVIS_POTION: return "FAMILY_INVIS_POTION";
                    case FAMILY_SPEED_POTION: return "FAMILY_SPEED_POTION";
                    default: return "?";
                }
            };
            std::fprintf(stderr, "    row.item_pads = {\n");
            for (const ItemPad& p : actual_pads)
                std::fprintf(stderr, "        {%s, {%d, %d}},\n",
                             family_token(p.family), p.at.tx, p.at.ty);
            std::fprintf(stderr, "    };\n");
        }
        for (const ItemPad& p : row.item_pads)
            if (!tile_passable(world, probe.get(), p.at))
                fail(std::format("{}: item pad ({}, {}) impassable", where,
                                 p.at.tx, p.at.ty));
    }

    // Obmap ledger.
    const int ledger = obmap_ledger(row);
    if (!row.a_star_waived && ledger > 190)
        fail(std::format("{}: obmap ledger {} exceeds 190", where, ledger));
    if (row.a_star_waived && ledger <= 190)
        fail(std::format("{}: ledger {} no longer needs its A* waiver",
                         where, ledger));

    // Soccer + Basketball: closed perimeter on all four edges (the ball
    // must never leave the court).
    if (row.mode == ModeKind::Soccer || row.mode == ModeKind::Basketball)
    {
        for (int tx = 0; tx < world.grid.w; ++tx)
        {
            if (tile_passable(world, probe.get(), {static_cast<short>(tx), 0}))
                fail(std::format("{}: open perimeter at ({}, 0)", where, tx));
            if (tile_passable(world, probe.get(),
                              {static_cast<short>(tx),
                               static_cast<short>(world.grid.h - 1)}))
                fail(std::format("{}: open perimeter at ({}, {})", where, tx,
                                 world.grid.h - 1));
        }
        for (int ty = 0; ty < world.grid.h; ++ty)
        {
            if (tile_passable(world, probe.get(), {0, static_cast<short>(ty)}))
                fail(std::format("{}: open perimeter at (0, {})", where, ty));
            if (tile_passable(world, probe.get(),
                              {static_cast<short>(world.grid.w - 1),
                               static_cast<short>(ty)}))
                fail(std::format("{}: open perimeter at ({}, {})", where,
                                 world.grid.w - 1, ty));
        }
    }

    // Soccer: carpet goals, passable kickoff.
    if (row.mode == ModeKind::Soccer)
    {
        for (const GoalRect& g : row.goal_rects)
            for (int ty = g.y0; ty <= g.y1; ++ty)
                for (int tx = g.x0; tx <= g.x1; ++tx)
                    if (world.grid.data[static_cast<std::size_t>(tx + ty * world.grid.w)] !=
                        PIX_CARPET_M)
                        fail(std::format("{}: goal tile ({}, {}) is not the "
                                         "goal carpet", where, tx, ty));
        if (!tile_passable(world, probe.get(), row.kickoff))
            fail(std::format("{}: kickoff ({}, {}) impassable", where,
                             row.kickoff.tx, row.kickoff.ty));
    }

    // Basketball structural arm (design doc §5.5): dunk carpets, centered
    // jump tile, arc sanity + per-quadrant runner presence, hoop
    // separation.
    if (row.mode == ModeKind::Basketball)
    {
        auto base_at = [&world](int tx, int ty) {
            return world.grid.data[static_cast<std::size_t>(
                tx + ty * world.grid.w)];
        };

        // Manifest completeness: exactly one hoop per manifest team. A
        // short (or empty) hoops list would sail through every per-hoop
        // check below, yet on_mode_init refuses such a row and the level
        // silently falls back to classic play.
        if (static_cast<int>(row.hoops.size()) != row.team_count)
            fail(std::format("{}: {} hoops authored for {} teams", where,
                             row.hoops.size(), row.team_count));

        // Per authored hoop: the 3x3 dunk carpet — 8 outer PIX_CARPET_M
        // tiles around the PIX_CARPET_M2 rim tile (the hoop tile IS the
        // carpet center, D3), all nine passable (the painted carpet is
        // exactly the Chebyshev dunk box).
        for (const TilePos& hoop : row.hoops)
        {
            if (base_at(hoop.tx, hoop.ty) != PIX_CARPET_M2)
                fail(std::format("{}: hoop tile ({}, {}) is not the "
                                 "PIX_CARPET_M2 rim tile", where, hoop.tx,
                                 hoop.ty));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const TilePos t{static_cast<short>(hoop.tx + dx),
                                    static_cast<short>(hoop.ty + dy)};
                    if ((dx != 0 || dy != 0) &&
                        base_at(t.tx, t.ty) != PIX_CARPET_M)
                        fail(std::format("{}: dunk-carpet tile ({}, {}) is "
                                         "not PIX_CARPET_M", where, t.tx,
                                         t.ty));
                    if (!tile_passable(world, probe.get(), t))
                        fail(std::format("{}: dunk-carpet tile ({}, {}) "
                                         "impassable", where, t.tx, t.ty));
                }
        }

        // Jump tile: passable and the exact court center (all grids are
        // odd-dimensioned by the court grammar, so a true center exists).
        if (!tile_passable(world, probe.get(), row.jump_ball))
            fail(std::format("{}: jump-ball tile ({}, {}) impassable", where,
                             row.jump_ball.tx, row.jump_ball.ty));
        if (row.jump_ball.tx != (row.grid_w - 1) / 2 ||
            row.jump_ball.ty != (row.grid_h - 1) / 2)
            fail(std::format("{}: jump-ball tile ({}, {}) is not the court "
                             "center ({}, {})", where, row.jump_ball.tx,
                             row.jump_ball.ty, (row.grid_w - 1) / 2,
                             (row.grid_h - 1) / 2));

        // Arc sanity bounds (D18: the manifest number is the sim truth,
        // the paint is advisory).
        if (row.arc_radius < 32 ||
            row.arc_radius >=
                std::min(row.grid_w, row.grid_h) * GRID_SIZE / 2)
            fail(std::format("{}: arc_radius {} outside sanity bounds",
                             where, row.arc_radius));

        // Hoop separation: pairwise L1 > 2 * (scatter_cap_total 24 +
        // rim_r 12 + rim_lip 6) = 84 px — cross-rim landings impossible,
        // so shot resolution may test only its target hoop.
        for (std::size_t a = 0; a < row.hoops.size(); ++a)
            for (std::size_t b = a + 1; b < row.hoops.size(); ++b)
            {
                const int l1 =
                    std::abs(row.hoops[a].tx - row.hoops[b].tx) * GRID_SIZE +
                    std::abs(row.hoops[a].ty - row.hoops[b].ty) * GRID_SIZE;
                if (l1 <= 84)
                    fail(std::format("{}: hoops {} and {} only {} px apart "
                                     "(need > 84)", where, a, b, l1));
            }

        // Per-quadrant arc-runner presence (the D18/D26 amendment): for
        // every hoop, each axis-aligned quadrant (N/E/S/W half-planes
        // split at the diagonals; ties go east/west like the painter's
        // VER pick) that holds at least one PASSABLE tile whose center
        // distance^2 lies in the painted band [(r-8)^2, (r+8)^2) must
        // keep >= 2 carpet runners in that band, and every hoop keeps
        // >= 8 band runners total — a player must never lose a point of
        // shot value on an invisible boundary.
        for (std::size_t i = 0; i < row.hoops.size(); ++i)
        {
            const TilePos& hoop = row.hoops[i];
            const int hx = hoop.tx * GRID_SIZE + GRID_SIZE / 2;
            const int hy = hoop.ty * GRID_SIZE + GRID_SIZE / 2;
            const int r = row.arc_radius;
            int band_passable[4] = {};
            int band_runners[4] = {};
            int total_runners = 0;
            for (int ty = 1; ty < world.grid.h - 1; ++ty)
                for (int tx = 1; tx < world.grid.w - 1; ++tx)
                {
                    const int dx = tx * GRID_SIZE + GRID_SIZE / 2 - hx;
                    const int dy = ty * GRID_SIZE + GRID_SIZE / 2 - hy;
                    const int d2 = dx * dx + dy * dy;
                    if (d2 < (r - 8) * (r - 8) || d2 >= (r + 8) * (r + 8))
                        continue;
                    const int quadrant = (dx * dx >= dy * dy)
                                             ? ((dx >= 0) ? 1 : 3)
                                             : ((dy >= 0) ? 2 : 0);
                    if (tile_passable(world, probe.get(),
                                      {static_cast<short>(tx),
                                       static_cast<short>(ty)}))
                        ++band_passable[quadrant];
                    const unsigned char base = base_at(tx, ty);
                    if (base == PIX_CARPET_SMALL_HOR ||
                        base == PIX_CARPET_SMALL_VER)
                    {
                        ++band_runners[quadrant];
                        ++total_runners;
                    }
                }
            static constexpr const char* kQuadrantName[4] = {"N", "E", "S",
                                                             "W"};
            for (int q = 0; q < 4; ++q)
                if (band_passable[q] > 0 && band_runners[q] < 2)
                    fail(std::format("{}: hoop {} arc quadrant {} has {} "
                                     "painted runners (need >= 2)", where, i,
                                     kQuadrantName[q], band_runners[q]));
            if (total_runners < 8)
                fail(std::format("{}: hoop {} arc has {} painted runners "
                                 "total (need >= 8)", where, i,
                                 total_runners));
        }
    }

    // 6. Footing for every authored entity. RESERVED_TEAM markers are
    // exempt from the library audit: it probes with the marker's own
    // oversized editor sprite (~48px), while deploy/respawn only ever
    // stand 16x16 livings there — the per-marker probe above is the
    // correct footing check for them.
    for (const std::string& err : og::mapgen::audit_footing(world))
    {
        if (err.find("order 5 family 0") != std::string::npos)
            continue;
        fail(std::format("{}: {}", where, err));
    }

    // Generator spawn egress: every live generator must own at least one of
    // walker::fire()'s eight spawn positions that a spawn can both occupy
    // and walk out of, and none that strands one in a closed cell. Runs
    // before the reachability probes below so the temporary target livings
    // never enter the picture. NOTE: nothing to do with row.a_star_waived,
    // which waives only the obmap LEDGER cap above (a performance budget) —
    // this audit and audit_reachability both run un-waived on every level,
    // TDM 303/305 included.
    std::vector<bool> pocket_waiver_used(row.spawn_pocket_ok.size(), false);
    for (const std::string& err :
         og::mapgen::audit_generator_spawn_exits(world))
    {
        bool waived = false;
        if (err.find("spawn pocket") != std::string::npos)
        {
            for (std::size_t i = 0; i < row.spawn_pocket_ok.size(); ++i)
            {
                const std::string anchor =
                    std::format("at tile ({}, {})", row.spawn_pocket_ok[i].tx,
                                row.spawn_pocket_ok[i].ty);
                if (err.find(anchor) != std::string::npos)
                {
                    waived = true;
                    pocket_waiver_used[i] = true;
                }
            }
        }
        else if (err.find("cut off from the lead start marker") !=
                 std::string::npos)
        {
            // Same question, same declaration: a generator this level
            // already documents as unreachable on foot from the lead
            // (scen304's teleporter-served court) cannot also be required to
            // walk its spawns back to that lead. Reusing the reachability
            // exception keeps ONE list instead of two that can disagree —
            // and audit_reachability keeps proving the exception is real.
            for (const std::string& exception : row.reachability_exceptions)
                if (err.find(exception) != std::string::npos)
                    waived = true;
        }
        if (!waived)
            fail(std::format("{}: {}", where, err));
    }
    for (std::size_t i = 0; i < row.spawn_pocket_ok.size(); ++i)
        if (!pocket_waiver_used[i])
            fail(std::format("{}: spawn_pocket_ok ({}, {}) no longer produces "
                             "a pocket - drop the waiver", where,
                             row.spawn_pocket_ok[i].tx,
                             row.spawn_pocket_ok[i].ty));

    // Reachability from the team-0 lead: the library audits every living,
    // generator and exit; temporary probes extend it to flags, waypoints,
    // goal centers, the kickoff and every team's lead marker.
    std::vector<walker*> targets;
    auto add_target = [&](TilePos t) {
        walker* w = place_at(world, Order::Living, FAMILY_SOLDIER, 0, t);
        if (w != nullptr)
            targets.push_back(w);
    };
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->query_order() != Order::Treasure)
            continue;
        if (fx->family() == modesgen::kFlagFamily ||
            fx->family() == modesgen::kWaypointFamily)
            add_target({static_cast<short>(fx->xpos() / GRID_SIZE),
                        static_cast<short>(fx->ypos() / GRID_SIZE)});
    }
    for (const GoalRect& g : row.goal_rects)
        add_target({static_cast<short>((g.x0 + g.x1) / 2),
                    static_cast<short>((g.y0 + g.y1) / 2)});
    if (row.kickoff.tx >= 0)
        add_target(row.kickoff);
    // Basketball: every rebound scrum spot is provably walkable — probe
    // each hoop tile and the jump tile (§5.5).
    for (const TilePos& hoop : row.hoops)
        add_target(hoop);
    if (row.jump_ball.tx >= 0)
        add_target(row.jump_ball);
    for (int team = 1; team < row.team_count; ++team)
        add_target(lead[static_cast<std::size_t>(team)]);
    // Every item pad must be A*-reachable from the team-0 lead: an
    // unreachable pad would bank a permanent census deficit that
    // lib/mode_items can never fill (deduplicated — vendored scatter may
    // stack two pads on one tile).
    {
        std::vector<int> pad_tiles;
        for (const ItemPad& p : row.item_pads)
        {
            const int key = p.at.ty * 4096 + p.at.tx;
            bool seen = false;
            for (const int k : pad_tiles)
                seen = seen || k == key;
            if (!seen)
            {
                pad_tiles.push_back(key);
                add_target(p.at);
            }
        }
    }
    for (const std::string& err : og::mapgen::audit_reachability(world))
    {
        bool allowed = false;
        for (const std::string& exception : row.reachability_exceptions)
            if (err.find(exception) != std::string::npos)
                allowed = true;
        if (!allowed)
            fail(std::format("{}: {}", where, err));
    }
    for (walker* w : targets)
        world.remove_ob(w);
}

// ---------------------------------------------------------------------------
// Pack self-checks: the manifest module must execute clean in the sandbox,
// the sprites must load with their pinned shapes, and each mode whose
// script has landed must prove one-real-tick dispatch (absent scripts are
// SKIPPED — they arrive with the Lua-mode waves — and assert-fire once
// present).
// ---------------------------------------------------------------------------
void self_check_manifest_chunk()
{
    std::ifstream in(std::string(kPackSourceDir) + "/lib/mode_levels.lua",
                     std::ios::binary);
    if (!in)
    {
        fail("self-check: cannot read the committed manifest");
        return;
    }
    std::string src((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    og::script::ScriptHost host;
    if (!host.run_chunk("mode_levels.lua", src, kPackId))
        fail("self-check: the manifest chunk failed to execute");
    for (const og::script::ScriptError& err : host.errors())
        fail(std::format("self-check: manifest error at {}: {}", err.where,
                         err.message));
}

void check_sprite(const char* virtual_path, int w, int h, int frames)
{
    const PixieData pix = read_pixie_file(virtual_path);
    if (!pix.valid() || pix.w != w || pix.h != h || pix.frames != frames)
        fail(std::format("self-check: sprite {} did not load as {}x{}x{}",
                         virtual_path, w, h, frames));
}

void self_check_pack_art()
{
    check_sprite("icon.png", 32, 32, 1);
    const std::string base = std::format("packs/{}/sprites/", kPackId);
    check_sprite((base + "flag.png").c_str(), 10, 14, 4);
    check_sprite((base + "ctfpoint.png").c_str(), 16, 16, 1);
    check_sprite((base + "ball.png").c_str(), 12, 12, 8);
    check_sprite((base + "bball.png").c_str(), 12, 12, 8);
    check_sprite((base + "bshadow.png").c_str(), 12, 12, 4);
    check_sprite((base + "hoop.png").c_str(), 24, 26, 6);
    check_sprite((base + "aura.png").c_str(), 16, 16, 4);
}

// One real tick per mode whose script exists: proves the pack registers,
// the level loads under a full sim context, and the script dispatches
// without errors. With no script landed yet, the tick still proves the
// scripted-type level loads and runs clean.
void self_check_mode_dispatch(ModeKind mode, int level_id)
{
    const std::string script =
        std::format("{}/scripts/mode_{}.lua", kPackSourceDir,
                    mode_name(mode));
    const bool script_present = fs::exists(script);

    LevelRuntimeData level(level_id, true, &headless_level_data_hooks());
    SaveData save;
    // Amendment 4 (E3): FILL defaults to NONE on every map, so a bare save
    // stages an honestly refusing match ("fewer than two teams"). The
    // dispatch proof turns every wheel to explicit FAIR — the one-knob
    // shape a real host uses — so the check still proves the level loads
    // and its mode initializes, without blessing a squad nobody asked for.
    save.fill.fill(og::sim::kFillFair);
    og::sim::SimEventLog events;
    FixedRandom script_rng{0};
    level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                          &script_rng, &cfg);
    GameplayContext script_ctx;
    script_ctx.world = &level.world();
    script_ctx.save = &save;
    script_ctx.sim_events = &events;
    script_ctx.config = &cfg;
    GameplayContext* prev = current_game;
    current_game = &script_ctx;

    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load for the {} "
                         "dispatch check", level_id, mode_name(mode)));
        current_game = prev;
        return;
    }
    for (int i = 0; i < 30; ++i)
        level.world().tick();

    for (const og::script::ScriptError& err :
         level.world().scripts().host().errors())
        fail(std::format("self-check: scen{} script error at {}: {}",
                         level_id, err.where, err.message));

    if (!script_present)
    {
        std::printf("modes_mapgen: %s dispatch proof SKIPPED (pack/scripts/"
                    "mode_%s.lua not landed yet); scen%d ticked clean\n",
                    mode_name(mode), mode_name(mode), level_id);
        current_game = prev;
        return;
    }

    // The mode script is present: it must have registered and announced.
    bool pack_registered = false;
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        if (ps.pack_id == kPackId)
            pack_registered = true;
    if (!pack_registered)
        fail("self-check: the modes pack script is not registered on mount");
    bool announced = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification)
            announced = true;
    if (!announced)
        fail(std::format("self-check: scen{} {} script never announced "
                         "itself", level_id, mode_name(mode)));
    current_game = prev;
}

} // namespace
} // namespace modesgen

int main(int argc, char* argv[])
{
    using namespace modesgen;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/modes";
    const fs::path out_abs = fs::absolute(out_tree);

    // Never touch ~/.openglad: run inside a disposable config dir unless
    // the caller already redirected it.
    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("modes_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "gladiator")
    {
        std::fprintf(stderr,
                     "modes_mapgen: ERROR: stock campaign not mounted; run "
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

    // The committed manifest must match the build tables (D8).
    const std::vector<ExpectedLevel> rows = all_expectations();
    if (rows.size() != 39)
        fail(std::format("expected 39 levels, tables carry {}", rows.size()));
    (void)check_and_refresh_manifest(rows);

    // Assemble the campaign under <user>/temp/ (the repack layout).
    const std::string user = get_user_path();
    cleanup_unpacked_campaign();
    create_dir(user + "temp/");
    create_dir(user + "temp/scen/");
    create_dir(user + "temp/pix/");
    write_campaign_yaml(user + "temp/campaign.yaml");
    write_icon(user + "temp/icon.png");
    copy_pack_tree(user + "temp/");

    // Mount a scen-less preliminary package BEFORE building the levels: the
    // flag/waypoint/ball families live in THIS pack (wire ids 13/14 left
    // core with the CTF engine retirement), so their descriptors and
    // sprites must be installed for the level builders to place them.
    const std::string glad_path =
        user + std::format("campaigns/{}.glad", kCampaignId);
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) !=
        ArchiveIoError::None)
    {
        fail(std::format("failed to zip preliminary pack into {}", glad_path));
    }
    else if (mount_campaign_package_with_error(kCampaignId) !=
             CampaignPackageIoError::None)
    {
        fail("failed to mount the preliminary pack");
    }

    build_tdm();
    build_ctf();
    build_onslaught();
    build_soccer();
    build_basketball();
    build_mutant();
    build_ffa();

    (void)unmount_campaign_package_with_error(kCampaignId);
    std::remove(glad_path.c_str());
    if (g_errors == 0 &&
        zip_contents_with_error(user + "temp/", glad_path) !=
            ArchiveIoError::None)
    {
        fail(std::format("failed to zip campaign into {}", glad_path));
    }

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error(kCampaignId) !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            for (const ExpectedLevel& row : rows)
                self_check_level(row);
            self_check_manifest_chunk();
            self_check_pack_art();
            self_check_mode_dispatch(ModeKind::Tdm, 300);
            self_check_mode_dispatch(ModeKind::Ctf, 500);
            self_check_mode_dispatch(ModeKind::Onslaught, 800);
            self_check_mode_dispatch(ModeKind::Soccer, 820);
            self_check_mode_dispatch(ModeKind::Basketball, 824);
            self_check_mode_dispatch(ModeKind::Mutant, 840);
            self_check_mode_dispatch(ModeKind::Ffa, 850);
            (void)unmount_campaign_package_with_error(kCampaignId);
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        // The hand-authored packs/ subtree (and README.md) live in the
        // campaign dir itself; export_campaign_tree preserves them and
        // rewrites only the generated level data.
        if (!og::toolexport::export_campaign_tree(user + "temp/", out_abs))
        {
            fail(std::format("failed to export the campaign tree to {}",
                             out_abs.string()));
        }
        else
        {
            std::printf("modes_mapgen: wrote %s\n", out_abs.c_str());
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
        std::fprintf(stderr, "modes_mapgen: FAILED with %d error(s)\n",
                     modesgen::g_errors);
    return result;
}
