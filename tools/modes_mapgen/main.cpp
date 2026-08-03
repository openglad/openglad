/* Multiplayer Game Modes campaign generator.
 *
 * Produces builtin/org.openglad.modes.glad: the 28-scenario five-mode
 * campaign (TDM 300-305 absorbing the arenas grids, CTF 500-509 keeping
 * the shipped CTF maps, Onslaught 800-803, Soccer 820-823, Mutant
 * 840-843), every level typed SCEN_TYPE_SCRIPTED — the mode rules live in
 * the campaign's embedded Lua pack. This tool assembles the package
 * (yaml + icon + pack tree + 28 built levels), regenerates the committed
 * level manifest (pack/lib/mode_levels.lua) from the same tables that
 * build the maps, zips, remounts, and hard-fails on any self-check
 * violation before copying into builtin/.
 *
 * Usage: modes_mapgen [output.glad]  (default: builtin/org.openglad.modes.glad)
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
#include <openglad/core/ctf_constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/family_registries.h>
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

constexpr const char* kCampaignId = "org.openglad.modes";
constexpr const char* kPackId = "org.openglad.modes.rules";

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
        << "    One arena, five games, one book.\n"
        << "    The Gamesmaster calls team\n"
        << "    deathmatch, capture the flag,\n"
        << "    onslaught, mutant, and soccer\n"
        << "    across twenty-eight fields old\n"
        << "    and new. The bots know the rules.\n"
        << "    Respawns honor your difficulty.\n"
        << "    First to the posted score takes\n"
        << "    the purse.\n";
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
            icon.data[y * kSize + x] = c;
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
// Pack tree: byte-copy tools/modes_mapgen/pack/** (Lua, sprites, sidecars)
// into the staging dir. The mode scripts/libs land with the Lua-mode
// waves; whatever exists is shipped.
// ---------------------------------------------------------------------------
void copy_pack_tree(const std::string& staging_root)
{
    const fs::path src = "tools/modes_mapgen/pack";
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
// ball). Must stay <= 190 unless the row carries the documented waiver.
int obmap_ledger(const ExpectedLevel& row)
{
    int gens = 0;
    for (const int g : row.generators_per_team)
        gens += g;
    int caps = 0;
    for (const SpawnCap& cap : row.spawn_caps)
        caps += cap.cap;
    const int ball = (row.mode == ModeKind::Soccer) ? 1 : 0;
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
                const unsigned char d = dec.data[tx + ty * dec.w];
                if (d == DECOR_NONE)
                    continue;
                ++decor_cells;
                if (d >= DECOR_MAX)
                    fail(std::format("{}: decor id {} out of range at "
                                     "({}, {})", where, d, tx, ty));
                const unsigned char base =
                    world.grid.data[tx + ty * world.grid.w];
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
                if (family == og::FAMILY_FLAG)
                {
                    ++flags_per_team[team];
                    if (!tile_passable(world, probe.get(), t))
                        fail(std::format("{}: flag ({}, {}) impassable",
                                         where, t.tx, t.ty));
                }
                else if (family == og::FAMILY_CTF_POINT)
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
            !lead_clearance(world, probe.get(), lead[team]))
            fail(std::format("{}: team {} lead marker ({}, {}) lacks 2x2 "
                             "clearance", where, team, lead[team].tx,
                             lead[team].ty));
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
        if (gens_per_team[team] != row.generators_per_team[team])
            fail(std::format("{}: team {} has {} generators, expected {}",
                             where, team, gens_per_team[team],
                             row.generators_per_team[team]));
    if (named != 0)
        fail(std::format("{}: {} named NPCs survived", where, named));
    if (save_protected != 0)
        fail(std::format("{}: {} protected (SAVE_ALL) walkers", where,
                         save_protected));
    if (guard_violations != 0)
        fail(std::format("{}: {} allied ACT_GUARD livings without "
                         "hold-post", where, guard_violations));

    // Obmap ledger.
    const int ledger = obmap_ledger(row);
    if (!row.a_star_waived && ledger > 190)
        fail(std::format("{}: obmap ledger {} exceeds 190", where, ledger));
    if (row.a_star_waived && ledger <= 190)
        fail(std::format("{}: ledger {} no longer needs its A* waiver",
                         where, ledger));

    // Soccer: closed perimeter, carpet goals, passable kickoff.
    if (row.mode == ModeKind::Soccer)
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
        for (const GoalRect& g : row.goal_rects)
            for (int ty = g.y0; ty <= g.y1; ++ty)
                for (int tx = g.x0; tx <= g.x1; ++tx)
                    if (world.grid.data[tx + ty * world.grid.w] !=
                        PIX_CARPET_M)
                        fail(std::format("{}: goal tile ({}, {}) is not the "
                                         "goal carpet", where, tx, ty));
        if (!tile_passable(world, probe.get(), row.kickoff))
            fail(std::format("{}: kickoff ({}, {}) impassable", where,
                             row.kickoff.tx, row.kickoff.ty));
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
        if (fx->family() == og::FAMILY_FLAG ||
            fx->family() == og::FAMILY_CTF_POINT)
            add_target({static_cast<short>(fx->xpos() / GRID_SIZE),
                        static_cast<short>(fx->ypos() / GRID_SIZE)});
    }
    for (const GoalRect& g : row.goal_rects)
        add_target({static_cast<short>((g.x0 + g.x1) / 2),
                    static_cast<short>((g.y0 + g.y1) / 2)});
    if (row.kickoff.tx >= 0)
        add_target(row.kickoff);
    for (int team = 1; team < row.team_count; ++team)
        add_target(lead[team]);
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
    std::ifstream in("tools/modes_mapgen/pack/lib/mode_levels.lua",
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
    check_sprite((base + "ball.png").c_str(), 12, 12, 1);
    check_sprite((base + "aura.png").c_str(), 16, 16, 4);
}

// One real tick per mode whose script exists: proves the pack registers,
// the level loads under a full sim context, and the script dispatches
// without errors. With no script landed yet, the tick still proves the
// scripted-type level loads and runs clean.
void self_check_mode_dispatch(ModeKind mode, int level_id)
{
    const std::string script =
        std::format("tools/modes_mapgen/pack/scripts/mode_{}.lua",
                    mode_name(mode));
    const bool script_present = fs::exists(script);

    LevelRuntimeData level(level_id, true, &headless_level_data_hooks());
    SaveData save;
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

    const std::string out_glad =
        (argc > 1) ? argv[1] : "builtin/org.openglad.modes.glad";
    const fs::path out_abs = fs::absolute(out_glad);

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
    if (get_mounted_campaign() != "org.openglad.gladiator")
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
    if (rows.size() != 28)
        fail(std::format("expected 28 levels, tables carry {}", rows.size()));
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

    build_tdm();
    build_ctf();
    build_onslaught();
    build_soccer();
    build_mutant();

    const std::string glad_path =
        user + std::format("campaigns/{}.glad", kCampaignId);
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
            self_check_mode_dispatch(ModeKind::Mutant, 840);
            (void)unmount_campaign_package_with_error(kCampaignId);
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
            std::printf("modes_mapgen: wrote %s (%ju bytes)\n",
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
        std::fprintf(stderr, "modes_mapgen: FAILED with %d error(s)\n",
                     modesgen::g_errors);
    return result;
}
