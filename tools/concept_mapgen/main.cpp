/* Concept Playground campaign generator.
 *
 * Produces campaigns/org.openglad.concept/ (the source tree the build
 * composes into builtin/org.openglad.concept.glad): five small multi-floor scenarios
 * that show off the Z-axis feature (levels 600-604 — stacked floors joined by
 * Z-stairs, "air" holes you fall through, see-through "glass" floors, and
 * projectile arcs) plus one scripted boss arena, "The Ninefold Court" (605),
 * that shows off Lua level scripting: its fight logic ships INSIDE the .glad
 * as the embedded pack packs/org.openglad.concept.showcase/scripts/court.lua
 * (hand-authored in the campaign dir's packs/ subtree, composed into the
 * archive by the build). The six epic multifloor war stories that once shipped
 * here as levels 605-610 moved to the "War of the Westlands" story campaign
 * (tools/westlands_mapgen, builtin/org.openglad.westlands.glad). SDL-free;
 * reuses the headless platform glue, mirrors tools/ctf_mapgen. Builds the v10
 * multi-floor scenario format (docs/z-axis-design.md), zips a campaign
 * package, mounts it, and self-checks every level by reloading it (the court
 * additionally runs one sim tick to prove the embedded script dispatches).
 *
 * Usage: concept_mapgen [output-dir]   (default: campaigns/org.openglad.concept)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include "../campaign_export.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
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

// --- Headless process globals (same shape as the dedicated server binary). ---
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
    static std::uint32_t state = 20260628u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace conceptgen {
namespace {

namespace fs = std::filesystem;

// The embedded pack's id; its hand-authored source lives at
// campaigns/org.openglad.concept/packs/org.openglad.concept.showcase/
// (composed into the archive by the build, staged here for the
// self-checks). Loads after "core" — pack replay order is pack-id
// lexicographic.
constexpr const char* kShowcasePackId = "org.openglad.concept.showcase";

int g_errors = 0;

void fail(const std::string& message)
{
    std::fprintf(stderr, "concept_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

// A grass field of (tw x th) tiles; PixieData owns the heap buffer.
PixieData make_grid(int tw, int th, unsigned char fill)
{
    auto* buf = new unsigned char[static_cast<std::size_t>(tw) * th];
    std::fill(buf, buf + static_cast<std::size_t>(tw) * th, fill);
    return PixieData(1, static_cast<unsigned char>(tw),
                     static_cast<unsigned char>(th), buf);
}

void paint(PixieData& g, int tx, int ty, unsigned char tile)
{
    if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h)
        g.data[tx + ty * g.w] = tile;
}

// Fill a rectangle [tx0,tx1] x [ty0,ty1] (inclusive) with a tile.
void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1, unsigned char tile)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, tile);
}

} // namespace — the decor hook below gets external linkage on purpose

// DECOR plane authoring hook (BASE + DECOR tile layering, .fss v11; mirrors
// westlands_mapgen::paint_decor). No concept level places decor today — the
// playground predates the decor plane and regenerates byte-identically — but
// the hook keeps the three mapgens' authoring vocabulary uniform for future
// levels. External linkage on purpose: an anonymous-namespace helper with no
// caller would trip -Wunused-function.
void paint_decor(GameWorld& w, int floor, int tx, int ty,
                 unsigned char decor_id)
{
    PixieData& g = w.grid_for_floor(floor);
    if (tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
    {
        fail(std::format("paint_decor: tile ({}, {}) outside floor {} grid",
                         tx, ty, floor));
        return;
    }
    const unsigned char base = g.data[tx + ty * g.w];
    if (decor_id >= DECOR_MAX || base == PIX_AIR || base == PIX_ZSTAIR_UP ||
        base == PIX_ZSTAIR_DOWN || base == PIX_VOID1)
    {
        fail(std::format("paint_decor: decor {} rejected over base {} at "
                         "({}, {}) floor {}", decor_id, base, tx, ty, floor));
        return;
    }
    PixieData& dec = w.decor_for_floor(floor);
    if (!dec.valid())
    {
        auto* buf = new unsigned char[static_cast<std::size_t>(g.w) * g.h];
        std::fill(buf, buf + static_cast<std::size_t>(g.w) * g.h,
                  static_cast<unsigned char>(DECOR_NONE));
        dec = PixieData(1, static_cast<unsigned char>(g.w),
                        static_cast<unsigned char>(g.h), buf);
    }
    dec.data[tx + ty * dec.w] = decor_id;
}

namespace {

walker* place(GameWorld& world, Order order, int family, int team, int floor,
              int tx, int ty)
{
    walker* w = (order == Order::Treasure) ? world.add_fx_ob(order, family)
                                           : world.add_ob(order, family);
    if (w == nullptr)
    {
        fail(std::format("could not place order {} family {}",
                         static_cast<int>(order), family));
        return nullptr;
    }
    w->set_floor(static_cast<short>(floor));
    w->setxy(static_cast<short>(tx * GRID_SIZE), static_cast<short>(ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    return w;
}

void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description)
{
    world.title = title;
    world.par_value = 3;
    world.time_bonus_limit = 4000;

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", id);
    for (const std::string& line : description)
        metadata.description.push_back(line);

    const std::string user = get_user_path();
    const std::string fss = user + std::format("temp/scen/scen{}.fss", id);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, fss, metadata, &err))
    {
        fail(std::format("failed to write {}", fss));
        return;
    }
    // Floor 0 grid, then extra floors by derived name "{grid}_f{N}".
    const std::string base = user + "temp/pix/" + metadata.grid_file;
    if (!write_pixie_png((base + ".png").c_str(), world.grid))
        fail(std::format("failed to write {}.png", base));
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const std::string p = std::format("{}_f{}.png", base, f);
        if (!write_pixie_png(p.c_str(), world.grid_for_floor(f)))
            fail(std::format("failed to write {}", p));
    }
    // Decor planes by derived name "{grid}_d{N}" (including floor 0), for
    // exactly the floors the .fss v11 payload flags as present (valid plane
    // with a nonzero byte). No concept level places decor today, so this
    // loop is a no-op that keeps the package byte-identical.
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const std::size_t cells =
            static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(dec.h);
        bool nonzero = false;
        for (std::size_t c = 0; c < cells && !nonzero; ++c)
            nonzero = dec.data[c] != 0;
        if (!nonzero)
            continue;
        const std::string p = std::format("{}_d{}.png", base, f);
        if (!write_pixie_png(p.c_str(), dec))
            fail(std::format("failed to write {}", p));
    }
    std::printf("concept_mapgen: built %d '%s' (%d floors)\n", id, title,
                world.floor_count());
}

// Common world bootstrap: a LevelRuntimeData with N floors all sized tw x th,
// floor 0 filled grass; extra floors filled grass too (callers paint specials).
void init_world(LevelRuntimeData& level, int floors, int tw, int th)
{
    GameWorld& world = level.world();
    world.grid = make_grid(tw, th, PIX_GRASS1);
    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;
    world.mysmoother.set_target(world.grid);
    if (floors > 1)
    {
        world.set_floor_count(floors);
        for (int f = 1; f < floors; ++f)
        {
            world.grid_for_floor(f) = make_grid(tw, th, PIX_GRASS1);
            world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
        }
    }
}

// --- The five demo levels. --------------------------------------------------

// 600 STAIRS: floor 0 and floor 1 joined by a vertically-aligned Z-stair.
void build_stairs()
{
    LevelRuntimeData level(600, true, &headless_level_data_hooks());
    init_world(level, 2, 24, 18);
    GameWorld& w = level.world();
    const int sx = 12, sy = 9;
    paint(w.grid, sx, sy, PIX_ZSTAIR_UP);            // floor 0 -> up
    paint(w.grid_for_floor(1), sx, sy, PIX_ZSTAIR_DOWN); // floor 1 -> down
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 0, 3, 3);   // player start, floor 0
    place(w, Order::Living, FAMILY_ORC, 1, 1, 20, 14);     // foe waiting upstairs
    place(w, Order::Treasure, FAMILY_EXIT, 0, 1, 20, 3);   // exit on floor 1
    save_level_files(w, 600, "Stairs",
                     {"Climb the stairs to the upper", "floor and reach the exit."});
}

// 601 MIND THE GAP: an "air" chasm on the upper floor that ground units must
// path around (or fall through to floor 0); a flyer can cross straight over.
void build_mind_the_gap()
{
    LevelRuntimeData level(601, true, &headless_level_data_hooks());
    init_world(level, 2, 28, 18);
    GameWorld& w = level.world();
    // A horizontal air band across the upper floor with a walkable lip on the
    // right edge so ground pathing can route around it.
    paint_rect(w.grid_for_floor(1), 4, 9, 22, 10, PIX_AIR);
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 1, 4, 3);   // start above the gap
    place(w, Order::Living, FAMILY_ELF, 1, 1, 4, 15);      // foe below the gap
    place(w, Order::Treasure, FAMILY_EXIT, 0, 1, 22, 15);  // exit past the gap
    place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 1, 6, 3); // fly over!
    save_level_files(w, 601, "Mind the Gap",
                     {"Path around the chasm, or grab",
                      "flight to soar across it."});
}

// 602 GLASSHOUSE: a glass upper floor you walk on while seeing and dropping in
// on foes on the floor below.
void build_glasshouse()
{
    LevelRuntimeData level(602, true, &headless_level_data_hooks());
    init_world(level, 2, 22, 16);
    GameWorld& w = level.world();
    paint_rect(w.grid_for_floor(1), 6, 4, 15, 11, PIX_GLASS); // see-through floor
    paint(w.grid_for_floor(1), 10, 8, PIX_ZSTAIR_DOWN);       // drop in on them
    paint(w.grid, 10, 8, PIX_ZSTAIR_UP);
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 1, 3, 3);   // start on glass floor
    place(w, Order::Living, FAMILY_SKELETON, 1, 0, 10, 8); // foe below the glass
    place(w, Order::Treasure, FAMILY_EXIT, 0, 0, 18, 12);  // exit on lower floor
    save_level_files(w, 602, "Glasshouse",
                     {"Spot the foe through the glass,", "then drop in to finish it."});
}

// 603 DROP ZONE: an open pit of air on the upper floor — walk in and fall.
void build_drop_zone()
{
    LevelRuntimeData level(603, true, &headless_level_data_hooks());
    init_world(level, 2, 22, 16);
    GameWorld& w = level.world();
    paint_rect(w.grid_for_floor(1), 8, 5, 13, 10, PIX_AIR); // the pit
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 1, 3, 3);    // start up top
    place(w, Order::Living, FAMILY_BARBARIAN, 1, 0, 10, 8); // foe at the bottom
    place(w, Order::Treasure, FAMILY_EXIT, 0, 0, 18, 12);   // exit below
    save_level_files(w, 603, "Drop Zone",
                     {"Step into the pit and drop to", "the arena floor below."});
}

// 604 ARC RANGE: an open arena to show projectile arcs (knife/rock/fireball)
// and a pit so projectiles can drop a floor.
void build_arc_range()
{
    LevelRuntimeData level(604, true, &headless_level_data_hooks());
    init_world(level, 2, 30, 16);
    GameWorld& w = level.world();
    paint_rect(w.grid_for_floor(1), 14, 6, 16, 9, PIX_AIR); // a small pit mid-arena
    paint(w.grid, 15, 12, PIX_ZSTAIR_UP);               // floor 0 -> climb back out of the pit
    paint(w.grid_for_floor(1), 15, 12, PIX_ZSTAIR_DOWN); // floor 1 -> a second, deliberate way down
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 1, 4, 8);        // player start (floor 1)
    place(w, Order::Living, FAMILY_ARCHER, 1, 1, 26, 8);    // foe across the arena
    place(w, Order::Living, FAMILY_SKELETON, 1, 0, 15, 8);  // foe under the pit
    // The demo tour ends at the scripted showcase: Arc Range chains into
    // The Ninefold Court (605), which loops home to 600.
    walker* arc_exit = place(w, Order::Treasure, FAMILY_EXIT, 0, 1, 26, 3);
    if (arc_exit != nullptr)
        arc_exit->stats()->set_level(605);
    save_level_files(w, 604, "Arc Range",
                     {"Watch thrown weapons arc — and", "drop through the pit below."});
}

// 605 THE NINEFOLD COURT: the Lua level-scripting showcase. A walled cobble
// court on a single floor; four corner pillar generators (the four colleges:
// tent, tower, bones, treehouse) ward the Magistrate — a named, hold-post
// archmage on the north bench. The fight logic ships in the campaign's
// EMBEDDED pack (packs/org.openglad.concept.showcase/scripts/court.lua,
// hand-authored in the campaign dir's packs/ subtree and staged for the
// self-checks exactly as the build composes it): the boss is
// invincible while any pillar stands, the wards fail when the last one
// falls, ninefold judgment rings pulse after that, and every 3rd generator
// spawn is promoted to an Adjutant. Impossible as pure level data.
void build_ninefold_court()
{
    LevelRuntimeData level(605, true, &headless_level_data_hooks());
    init_world(level, 1, 30, 22);
    GameWorld& w = level.world();

    // The court: a one-tile wall ring around a cobbled floor, a two-tile
    // south gate, and a carpeted bench (dais) at the north end. A grass
    // apron surrounds the walls; the whole grid is autotiled below.
    paint_rect(w.grid, 2, 2, 27, 19, PIX_WALL2);      // the wall ring
    paint_rect(w.grid, 3, 3, 26, 18, PIX_COBBLE_1);   // the court floor
    paint_rect(w.grid, 14, 19, 15, 19, PIX_COBBLE_1); // the south gate
    paint_rect(w.grid, 12, 3, 17, 5, PIX_CARPET_M);   // the bench dais

    // The Magistrate: ACT_GUARD + hold-post (npc_flags bit 1) keeps him ON
    // the bench raining spells instead of hunting at first sight (guard
    // wake policy); the ward itself is stamped by the pack script at load.
    walker* boss = place(w, Order::Living, FAMILY_ARCHMAGE, 1, 0, 14, 4);
    if (boss != nullptr)
    {
        boss->stats()->set_level(8);
        boss->stats()->name = "Magistrate"; // 10 chars: fits the 11-char field
        boss->set_act_type(ACT_GUARD);
        boss->set_guard_hold_post(true);
    }

    // The four pillars, one college per corner. Level 2 keeps the trickle
    // of level 1-2 spawns steady but self-throttling (act_generate scales
    // its threshold with the live population).
    const struct { int family; int tx; int ty; } pillars[] = {
        {FAMILY_TENT, 5, 5},        // NW: the college of bone-raisers
        {FAMILY_TOWER, 23, 5},      // NE: the college of mages
        {FAMILY_BONES, 5, 15},      // SW: the college of haunts
        {FAMILY_TREEHOUSE, 23, 15}, // SE: the college of wardens
    };
    for (const auto& p : pillars)
    {
        walker* gen = place(w, Order::Generator, p.family, 1, 0, p.tx, p.ty);
        if (gen != nullptr)
            gen->stats()->set_level(2);
    }

    // The petitioners' floor: eight start markers (lead first) south of
    // center, inside the walls, clear of every pillar's spawn apron.
    const struct { int tx; int ty; } starts[] = {
        {14, 15},                                    // the lead, front-center
        {12, 15}, {16, 15}, {10, 15}, {18, 15},
        {12, 17}, {14, 17}, {16, 17},
    };
    for (const auto& s : starts)
        place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, 0, s.tx, s.ty);

    // The exit waits OUTSIDE the south gate on the apron: the tour's loop
    // home to 600. An exit-bearing level never auto-ends, so the standard
    // finish is judge the court, then walk out the way you came in.
    walker* court_exit = place(w, Order::Treasure, FAMILY_EXIT, 0, 0, 14, 20);
    if (court_exit != nullptr)
        court_exit->stats()->set_level(600);

    // Set dressing: braziers flank the bench's back row, torches line the
    // north and south walls (skipping the gate), and old verdicts molder
    // by the necromantic colleges. Braziers/torches block ground movement,
    // so every one sits on a wall tile or the dais back row, off all fight
    // lanes; the bones are non-blocking ambience.
    paint_decor(w, 0, 12, 3, DECOR_BRAZIER);
    paint_decor(w, 0, 17, 3, DECOR_BRAZIER);
    for (int tx : {6, 10, 14, 18, 22})
        paint_decor(w, 0, tx, 2, DECOR_TORCH1);
    for (int tx : {6, 10, 18, 22}) // the gate columns stay bare
        paint_decor(w, 0, tx, 19, DECOR_TORCH1);
    paint_decor(w, 0, 7, 6, DECOR_BONES);
    paint_decor(w, 0, 7, 16, DECOR_BONES);

    // Bake the autotiling (wall edges, cobble variants, carpet border) the
    // way westlands_mapgen does. Only the court smooths: the five demos are
    // plain fields, so their bytes cannot shift.
    w.mysmoother.smooth();

    save_level_files(w, 605, "The Ninefold Court",
                     {"The Court's wards hold while its",
                      "pillars stand. Fell all four,",
                      "then judge the Magistrate."});
}

void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           Concept Playground\n"
        << "version:         1\n"
        << "first_level:     600\n"
        << "suggested_power: 0\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    A sampler of the Z-axis: stacked\n"
        << "    floors and stairs, air holes you\n"
        << "    fall through, see-through glass\n"
        << "    floors, arcing projectiles — and\n"
        << "    a Lua-scripted boss court whose\n"
        << "    warded Magistrate ends the tour.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: two stacked floor bands with a stairway, in the engine palette.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = make_grid(kSize, kSize, 0);
    auto put = [&](int x, int y, unsigned char c) { paint(icon, x, y, c); };
    for (int x = 3; x < 29; ++x)            // lower floor band
        for (int y = 20; y < 27; ++y) put(x, y, 5);
    for (int x = 3; x < 29; ++x)            // upper floor band
        for (int y = 6; y < 13; ++y) put(x, y, 10);
    for (int i = 0; i < 6; ++i)             // stairway
        for (int x = 14 + i; x < 18; ++x)
            put(x, 19 - i, 88);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

struct ExpectedLevel
{
    int id;
    int floors;
    const char* title;
    // Team-0 start markers (the player crew's authored formation) plus the
    // placed team-0 allies (named heroes, defender generators).
    int start_markers;
    int team0_livings;
    int team0_generators;
    int team1_livings;
    int team1_generators;
    int team2_livings;
    int team2_generators;
    // Per-NPC v10 extras that must round-trip through the package.
    int delayed_spawns;
    int specials_disabled;
    // When set, every floor boundary must have >= 1 aligned UP/DOWN stair pair
    // (601/603 traverse floors by falling, so they opt out).
    bool stairs_every_boundary;
};

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line).
constexpr std::size_t kBriefingLineBudget = 33;

void self_check_level(const ExpectedLevel& ex)
{
    LevelRuntimeData level(ex.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load", ex.id));
        return;
    }
    GameWorld& world = level.world();
    if (world.floor_count() != ex.floors)
        fail(std::format("self-check scen{}: floor_count {} != expected {}",
                         ex.id, world.floor_count(), ex.floors));
    if (world.title != ex.title)
        fail(std::format("self-check scen{}: title '{}' != '{}'", ex.id,
                         world.title, ex.title));
    // Every floor grid must have loaded (extra-floor PNGs round-tripped).
    for (int f = 0; f < world.floor_count(); ++f)
        if (!world.grid_for_floor(f).valid())
            fail(std::format("self-check scen{}: floor {} grid invalid", ex.id, f));

    for (const std::string& line : level.description)
        if (line.size() > kBriefingLineBudget)
            fail(std::format("self-check scen{}: briefing line '{}' overflows "
                             "the {}-char budget", ex.id, line,
                             kBriefingLineBudget));

    // Army audit: exact per-team living/generator/marker counts, the v10
    // per-NPC extras round-trip, and the seeded armies must leave headroom
    // under the engine's living cap.
    int livings[MAX_TEAM + 1] = {};
    int generators[MAX_TEAM + 1] = {};
    int total_livings = 0;
    int starts = 0;
    int delayed = 0;
    int no_specials = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->spawn_delay() > 0)
            ++delayed;
        if (ob->specials_disabled())
            ++no_specials;
        const int team = ob->team_num();
        if (team < 0 || team > MAX_TEAM)
            continue;
        if (ob->query_order() == Order::Living)
        {
            ++livings[team];
            ++total_livings;
        }
        else if (ob->query_order() == Order::Generator)
            ++generators[team];
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && team == 0)
            ++starts;
    }
    if (starts != ex.start_markers)
        fail(std::format("self-check scen{}: {} start markers, expected {}",
                         ex.id, starts, ex.start_markers));
    if (livings[0] != ex.team0_livings || generators[0] != ex.team0_generators)
        fail(std::format("self-check scen{}: team 0 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[0],
                         generators[0], ex.team0_livings, ex.team0_generators));
    if (livings[1] != ex.team1_livings || generators[1] != ex.team1_generators)
        fail(std::format("self-check scen{}: team 1 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[1],
                         generators[1], ex.team1_livings, ex.team1_generators));
    if (livings[2] != ex.team2_livings || generators[2] != ex.team2_generators)
        fail(std::format("self-check scen{}: team 2 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[2],
                         generators[2], ex.team2_livings, ex.team2_generators));
    if (delayed != ex.delayed_spawns || no_specials != ex.specials_disabled)
        fail(std::format("self-check scen{}: {} delayed spawns / {} "
                         "specials-disabled NPCs, expected {} / {}", ex.id,
                         delayed, no_specials, ex.delayed_spawns,
                         ex.specials_disabled));
    if (total_livings > MAXOBS)
        fail(std::format("self-check scen{}: {} seeded livings exceed the "
                         "MAXOBS={} living cap", ex.id, total_livings, MAXOBS));

    // Decor plane audit (BASE + DECOR layering): a plane that round-tripped
    // must match its floor grid's dims, carry only known decor ids, and
    // never sit over air / Z-stair / void bases. (Concept levels ship no
    // decor today; the audit guards future authoring.)
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid() || dec.w != g.w || dec.h != g.h)
        {
            fail(std::format("self-check scen{}: floor {} decor plane dims "
                             "mismatch", ex.id, f));
            continue;
        }
        for (int ty = 0; ty < dec.h; ++ty)
            for (int tx = 0; tx < dec.w; ++tx)
            {
                const unsigned char d = dec.data[tx + ty * dec.w];
                if (d >= DECOR_MAX)
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor id {} out of range",
                                     ex.id, f, tx, ty, d));
                const unsigned char base_tile = g.data[tx + ty * g.w];
                if (d != DECOR_NONE &&
                    (base_tile == PIX_AIR || base_tile == PIX_ZSTAIR_UP ||
                     base_tile == PIX_ZSTAIR_DOWN || base_tile == PIX_VOID1))
                {
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor {} over air/stair/void",
                                     ex.id, f, tx, ty, d));
                }
            }
    }

    // Stair audit: each floor boundary reachable through at least one
    // vertically-aligned UP/DOWN pair.
    if (ex.stairs_every_boundary)
    {
        for (int f = 0; f + 1 < world.floor_count(); ++f)
        {
            const PixieData& lo = world.grid_for_floor(f);
            const PixieData& hi = world.grid_for_floor(f + 1);
            int pairs = 0;
            const int cells = lo.w * lo.h;
            for (int i = 0; i < cells; ++i)
                if (lo.data[i] == PIX_ZSTAIR_UP && hi.data[i] == PIX_ZSTAIR_DOWN)
                    ++pairs;
            if (pairs < 1)
                fail(std::format("self-check scen{}: no aligned stair pair on "
                                 "floor boundary {}<->{}", ex.id, f, f + 1));
        }
    }

    // Footing audit: every authored entity stands on a tile of its own floor
    // that its own footprint can occupy, and no ground troop spawns over air.
    auto check_footing = [&](walker* ob)
    {
        if (ob == nullptr)
            return;
        if (!world.query_grid_passable(static_cast<float>(ob->xpos()),
                                       static_cast<float>(ob->ypos()), ob,
                                       ob->floor()))
        {
            fail(std::format(
                "self-check scen{}: order {} family {} at tile ({}, {}) floor "
                "{} stands on impassable ground", ex.id,
                static_cast<int>(ob->query_order()),
                static_cast<int>(ob->family()),
                ob->xpos() / GRID_SIZE, ob->ypos() / GRID_SIZE, ob->floor()));
        }
        if (ob->query_order() == Order::Living &&
            !ob->stats()->query_bit_flags(BIT_FLYING))
        {
            const PixieData& g = world.grid_for_floor(ob->floor());
            const int tx = (ob->xpos() + ob->sizex() / 2) / GRID_SIZE;
            const int ty = (ob->ypos() + ob->sizey() / 2) / GRID_SIZE;
            if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h &&
                g.data[tx + ty * g.w] == PIX_AIR)
            {
                fail(std::format(
                    "self-check scen{}: ground unit family {} at tile ({}, {}) "
                    "floor {} spawns over air", ex.id,
                    static_cast<int>(ob->family()), tx, ty, ob->floor()));
            }
        }
    };
    for (const auto& uptr : world.oblist)
        check_footing(uptr.get());
    for (const auto& uptr : world.fxlist)
        check_footing(uptr.get());
}

// The Ninefold Court's embedded pack must actually dispatch, not merely
// ride along in the zip. With the produced campaign mounted, the pack
// script registry must carry the showcase pack (campaign packs follow
// mounts), and one sim tick of the court must run court.lua's on_load:
// gimmick notification emitted, ward stamped on the Magistrate, and zero
// recorded script errors. This catches Lua syntax slips, hook-name typos,
// and dispatch wiring at generation time.
void self_check_court_script()
{
    bool registered = false;
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        if (ps.pack_id == kShowcasePackId)
            registered = true;
    if (!registered)
    {
        fail("self-check: showcase pack script not registered on mount");
        return;
    }

    LevelRuntimeData level(605, true, &headless_level_data_hooks());
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
        fail("self-check: scen605 failed to load for the script check");
        current_game = prev;
        return;
    }
    level.world().tick();

    bool wards_announced = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find("wards hold") != std::string::npos)
            wards_announced = true;
    if (!wards_announced)
        fail("self-check: court on_load did not announce the ward gimmick");

    walker* boss = nullptr;
    for (const auto& uptr : level.world().oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->family() == FAMILY_ARCHMAGE)
            boss = ob;
    }
    if (boss == nullptr || !boss->stats()->query_bit_flags(BIT_INVINCIBLE))
        fail("self-check: the Magistrate is not warded after on_load");

    for (const og::script::ScriptError& err :
         level.world().scripts().host().errors())
        fail(std::format("self-check: script error at {}: {}", err.where,
                         err.message));

    current_game = prev;
}

} // namespace
} // namespace conceptgen

int main(int argc, char* argv[])
{
    using namespace conceptgen;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/org.openglad.concept";
    const fs::path out_abs = fs::absolute(out_tree);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("concept_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "org.openglad.gladiator")
    {
        std::fprintf(stderr, "concept_mapgen: ERROR: stock campaign not mounted; "
                             "run next to staged assets (build dir)\n");
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

    const std::string user = get_user_path();
    cleanup_unpacked_campaign();
    create_dir(user + "temp/");
    create_dir(user + "temp/scen/");
    create_dir(user + "temp/pix/");
    write_campaign_yaml(user + "temp/campaign.yaml");
    write_icon(user + "temp/icon.png");
    if (!og::toolexport::stage_pack_tree(
            "campaigns/org.openglad.concept/packs/"
            "org.openglad.concept.showcase",
            user + "temp/", kShowcasePackId))
        fail("failed to stage the embedded showcase pack");

    build_stairs();
    build_mind_the_gap();
    build_glasshouse();
    build_drop_zone();
    build_arc_range();
    build_ninefold_court();

    const std::string glad_path = user + "campaigns/org.openglad.concept.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) != ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("org.openglad.concept") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
            //  delayed spawns, specials-disabled, stairs-every-boundary}
            const ExpectedLevel expectations[] = {
                {600, 2, "Stairs", 1, 0, 0, 1, 0, 0, 0, 0, 0, true},
                {601, 2, "Mind the Gap", 1, 0, 0, 1, 0, 0, 0, 0, 0, false},
                {602, 2, "Glasshouse", 1, 0, 0, 1, 0, 0, 0, 0, 0, true},
                {603, 2, "Drop Zone", 1, 0, 0, 1, 0, 0, 0, 0, 0, false},
                {604, 2, "Arc Range", 1, 0, 0, 2, 0, 0, 0, 0, 0, true},
                {605, 1, "The Ninefold Court", 8, 0, 0, 1, 4, 0, 0, 0, 0,
                 false},
            };
            for (const ExpectedLevel& e : expectations)
                self_check_level(e);
            self_check_court_script();
            (void)unmount_campaign_package_with_error("org.openglad.concept");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        // The hand-authored packs/ subtree (and README.md) live in the
        // campaign dir itself; export_campaign_tree preserves them and
        // rewrites only the generated level data.
        if (!og::toolexport::export_campaign_tree(user + "temp/", out_abs))
            fail(std::format("failed to export the campaign tree to {}",
                             out_abs.string()));
        else
        {
            std::printf("concept_mapgen: wrote %s\n", out_abs.c_str());
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
        std::fprintf(stderr, "concept_mapgen: FAILED with %d error(s)\n", g_errors);
    return result;
}
