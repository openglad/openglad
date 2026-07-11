/* Concept Playground campaign generator.
 *
 * Produces builtin/org.openglad.concept.glad: five small multi-floor scenarios
 * that show off the Z-axis feature (levels 600-604 — stacked floors joined by
 * Z-stairs, "air" holes you fall through, see-through "glass" floors, and
 * projectile arcs) plus six epic multifloor war stories (levels 605-610). In
 * each war story the player's crew IS the defense: team-0 start markers
 * arranged in a tactical formation deploy the whole team, a few placed team-0
 * allies (generators, the Grey Wizards) fight alongside, and the attacker
 * horde is team 2. SDL-free; reuses the headless platform glue,
 * mirrors tools/ctf_mapgen. Builds the v10 multi-floor scenario format
 * (docs/z-axis-design.md), zips a campaign package, mounts it, and self-checks
 * every level by reloading it.
 *
 * Usage: concept_mapgen [output.glad]   (default: builtin/org.openglad.concept.glad)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
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

// --- Epic-level helpers (605-610). -------------------------------------------

// Deterministic pavement pattern (PAVEMENT1-3 are inert to the autotiler, so
// the variety must be painted in rather than smoothed in).
void paint_pavement(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[3] = {PIX_PAVEMENT1, PIX_PAVEMENT2,
                                                  PIX_PAVEMENT3};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 3]);
}

// Deterministic worn-path pattern (PATH_1-4 are likewise autotiler-inert and
// walkable: the dirt track an army's boots leave behind).
void paint_path(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[4] = {PIX_PATH_1, PIX_PATH_2,
                                                  PIX_PATH_3, PIX_PATH_4};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 5 + y * 3) % 4]);
}

// Fill every tile whose center distance from (cx, cy) lies in [r0, r1).
void paint_ring(PixieData& g, double cx, double cy, double r0, double r1,
                unsigned char tile)
{
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d >= r0 && d < r1)
                paint(g, x, y, tile);
        }
}

// A vertically-aligned Z-stair pair: UP on floor f, DOWN on floor f+1 at the
// SAME cell (Z-stairs are positional; see docs/z-axis-design.md).
void stair_pair(GameWorld& w, int f, int tx, int ty)
{
    paint(w.grid_for_floor(f), tx, ty, PIX_ZSTAIR_UP);
    paint(w.grid_for_floor(f + 1), tx, ty, PIX_ZSTAIR_DOWN);
}

// Run the genre autotiler over every floor (grass/water/tree/wall/cobble/
// carpet shaping). The Z tiles and pavement/boulder decor are genre-inert.
void smooth_world(GameWorld& w)
{
    for (int f = 0; f < w.floor_count(); ++f)
        w.smoother_for_floor(f).smooth();
}

// Per-placed-NPC scenario extras (level file v10, reserved[3..5]): disable the
// family special and/or delay the walker's entry into the world by
// `spawn_delay` sim ticks. Safe on any AI NPC, team 0 included: players deploy
// onto reserved-team START MARKERS (game.cpp), never onto placed livings, so a
// delayed team-0 ally simply arrives late. Defaults leave the level
// byte-identical to a v9-era save.
void set_npc_extras(walker* ob, bool specials_disabled, int spawn_delay)
{
    if (ob == nullptr)
        return;
    ob->set_specials_disabled(specials_disabled);
    ob->set_spawn_delay(static_cast<std::uint16_t>(spawn_delay));
}

walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard = false,
                     bool specials_disabled = false, int spawn_delay = 0)
{
    walker* ob = place(w, Order::Living, family, team, floor, tx, ty);
    if (ob == nullptr)
        return nullptr;
    ob->stats()->set_level(level);
    if (guard)
        ob->set_act_type(ACT_GUARD);
    set_npc_extras(ob, specials_disabled, spawn_delay);
    return ob;
}

walker* place_generator(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level)
{
    walker* ob = place(w, Order::Generator, family, team, floor, tx, ty);
    if (ob != nullptr)
        ob->stats()->set_level(level);
    return ob;
}

// A team-0 player START MARKER (Order::Special, FAMILY_RESERVED_TEAM). At
// level load the game deploys one team character per marker, consuming
// markers in oblist order (game.cpp) — so place the formation's lead position
// first and let the rest of the crew fill the defensive posture. Markers
// beyond the team size are simply destroyed at load.
void place_start(GameWorld& w, int floor, int tx, int ty)
{
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, floor, tx, ty);
}

// A named team-0 hero NPC. The level-file name field serializes through a
// 12-byte buffer via snprintf, so names must fit in 11 characters
// ("Grey Wizard" exactly fills it).
walker* place_hero(GameWorld& w, int family, int floor, int tx, int ty,
                   int level, const char* name, bool guard = false,
                   bool specials_disabled = false, int spawn_delay = 0)
{
    walker* ob = place_living(w, family, 0, floor, tx, ty, level, guard,
                              specials_disabled, spawn_delay);
    if (ob != nullptr)
        ob->stats()->name = name;
    return ob;
}

// An exit that names its destination level (campaign chaining).
void place_exit(GameWorld& w, int floor, int tx, int ty, int destination)
{
    walker* e = place(w, Order::Treasure, FAMILY_EXIT, 0, floor, tx, ty);
    if (e != nullptr)
        e->stats()->set_level(destination);
}

// True when the tile (tx, ty) on 'floor' is covered by (or within 'margin'
// tiles of) any already-placed entity's footprint.
bool cell_near_entity(GameWorld& w, int floor, int tx, int ty, int margin)
{
    auto overlaps = [&](walker* ob) {
        if (ob == nullptr || ob->floor() != floor)
            return false;
        const int x0 = ob->xpos() / GRID_SIZE - margin;
        const int y0 = ob->ypos() / GRID_SIZE - margin;
        const int x1 = (ob->xpos() + ob->sizex() - 1) / GRID_SIZE + margin;
        const int y1 = (ob->ypos() + ob->sizey() - 1) / GRID_SIZE + margin;
        return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1;
    };
    for (const auto& uptr : w.oblist)
        if (overlaps(uptr.get()))
            return true;
    for (const auto& uptr : w.fxlist)
        if (overlaps(uptr.get()))
            return true;
    return false;
}

// Scatter a 4-variant decor tile set over a rectangle. Runs AFTER army
// placement and keeps one tile of clearance around every entity (and never
// covers a Z-stair) so no one spawns wedged in the scenery.
void scatter_decor(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                   int ty1, int modulus, const unsigned char (&variants)[4])
{
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 7 + y * 11) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[x + y * g.w];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            paint(g, x, y, variants[(x + y) % 4]);
        }
}

void scatter_boulders(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, int modulus)
{
    static constexpr unsigned char boulders[4] = {PIX_BOULDER_1, PIX_BOULDER_2,
                                                  PIX_BOULDER_3, PIX_BOULDER_4};
    scatter_decor(w, floor, tx0, ty0, tx1, ty1, modulus, boulders);
}

// Jagged-ground debris: reads as bone litter / battlefield wrack. NOTE:
// jagged tiles block ALL movement (even projectiles), so keep the modulus
// high and rely on the entity-clearance rule to stay out of the lanes.
void scatter_litter(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                    int ty1, int modulus)
{
    static constexpr unsigned char litter[4] = {
        PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2, PIX_JAGGED_GROUND_3,
        PIX_JAGGED_GROUND_4};
    scatter_decor(w, floor, tx0, ty0, tx1, ty1, modulus, litter);
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
    place(w, Order::Treasure, FAMILY_EXIT, 0, 1, 26, 3);
    save_level_files(w, 604, "Arc Range",
                     {"Watch thrown weapons arc — and", "drop through the pit below."});
}

// --- The six epic war stories (605-610). The defense of every battlefield is
// the PLAYER'S: team-0 start markers arranged in the old garrison's tactical
// formation deploy the crew into position, joined by a few placed team-0
// allies (defender generators, the two Grey Wizards). The attacker horde
// stays team 2, so each level plays as a battle the moment the crew lands.

// 605 THE DEEPING WALL: a fortress wall crosses the map east-west. Floor 0 is
// the wall itself with a gate gap; floor 1 is the walkable rampart parapet
// bounded by air. The player's crew deploys along the wall under a stone
// keep; the horde (team 2) masses in the southern field — and at first light
// the Grey Wizard arrives behind its eastern flank.
void build_deeping_wall()
{
    LevelRuntimeData level(605, true, &headless_level_data_hooks());
    init_world(level, 2, 80, 50);
    GameWorld& w = level.world();

    // Floor 0: keep courtyard (cobble) north, the wall, the field south.
    paint_rect(w.grid, 0, 0, 79, 17, PIX_COBBLE_1);
    paint_rect(w.grid, 0, 18, 79, 21, PIX_WALL2);
    paint_rect(w.grid, 30, 0, 49, 6, PIX_WALL2);        // the keep
    paint_rect(w.grid, 34, 2, 45, 4, PIX_CARPET_M);     // its long hall
    // Field texture: flanking woods, a pond, ground trampled by the horde.
    paint_rect(w.grid, 2, 25, 10, 33, PIX_TREE_M1);
    paint_rect(w.grid, 68, 43, 77, 48, PIX_TREE_M1);
    paint_rect(w.grid, 4, 42, 14, 48, PIX_WATER1);
    paint_rect(w.grid, 16, 24, 24, 28, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 56, 30, 66, 36, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 24, 45, 33, 48, PIX_GRASS_DARK_1);
    // Floor 1: a rampart strip directly over the wall, parapet edged by air.
    paint_rect(w.grid_for_floor(1), 0, 0, 79, 49, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 31, 1, 48, 1);                // keep interior ring
    paint_pavement(w.grid, 31, 5, 48, 5);                // around the hall
    paint_pavement(w.grid, 31, 2, 33, 4);
    paint_pavement(w.grid, 46, 2, 48, 4);
    paint_pavement(w.grid, 38, 6, 41, 6);                // keep door
    paint_pavement(w.grid, 39, 7, 40, 17);               // processional way
    paint(w.grid, 37, 7, PIX_BRAZIER1);                  // keep-door braziers
    paint(w.grid, 42, 7, PIX_BRAZIER1);
    paint_pavement(w.grid, 38, 18, 41, 21);              // the gate gap
    paint_path(w.grid, 38, 22, 41, 25);                  // the war-road, worn
    paint_path(w.grid, 39, 26, 40, 49);                  // to the gate
    paint_pavement(w.grid_for_floor(1), 0, 18, 79, 21);  // the rampart
    paint(w.grid_for_floor(1), 10, 18, PIX_BRAZIER1);    // rampart fire posts
    paint(w.grid_for_floor(1), 30, 18, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 50, 18, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 70, 18, PIX_BRAZIER1);
    stair_pair(w, 0, 20, 17); // up onto the rampart, north (courtyard) side
    stair_pair(w, 0, 60, 17);

    // The defense is yours (team-0 start markers in the garrison's posture):
    // the lead pair over the gate arch, a double rank plugging the gate,
    // archer posts along the parapet, reserves in the courtyard. Markers are
    // 32x32 (2x2 tiles), so anchors keep a tile of shoulder room from walls.
    place_start(w, 1, 38, 19);
    place_start(w, 1, 40, 19);
    place_start(w, 0, 38, 19); // the gate gap, plugged rank on rank
    place_start(w, 0, 40, 19);
    place_start(w, 0, 38, 21);
    place_start(w, 0, 40, 21);
    static constexpr int parapet_posts[8] = {8, 16, 24, 32, 46, 54, 62, 70};
    for (int i = 0; i < 8; ++i)
        place_start(w, 1, parapet_posts[i], 19);
    static constexpr int courtyard_posts[4] = {36, 38, 41, 43};
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, courtyard_posts[i], 16);
    static constexpr int courtyard_line2[4] = {37, 39, 41, 43};
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, courtyard_line2[i], 13);
    place_start(w, 0, 34, 16);
    place_start(w, 0, 37, 15);
    place_start(w, 0, 42, 15);
    place_start(w, 0, 45, 16);
    // Allies: the courtyard treehouse musters elves for the wall, and at
    // dawn (delayed spawn 500 ticks) the Grey Wizard arrives at the east
    // edge of the field, behind the horde's flank.
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 35, 9, 4);
    place_hero(w, FAMILY_ARCHMAGE, 0, 76, 33, 10, "Grey Wizard", false, false,
               500);

    // The horde (team 2): orc ranks, big-orc flank, champions, siege camp.
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 7; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 20 + col * 5, 30 + row * 2,
                         2 + ((row + col) % 3));
    for (int i = 0; i < 5; ++i)
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 22 + i * 6, 41, 3 + (i % 2));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 25 + i * 6, 43, 3 + (i % 2));
    }
    place_living(w, FAMILY_BIG_ORC, 2, 0, 38, 28, 8); // the champions
    place_living(w, FAMILY_BIG_ORC, 2, 0, 42, 28, 8);
    place_generator(w, FAMILY_TENT, 2, 0, 30, 46, 5); // the siege camp
    place_generator(w, FAMILY_TENT, 2, 0, 48, 46, 5);

    place_exit(w, 0, 40, 3, 606); // in the heart of the keep
    scatter_boulders(w, 0, 0, 22, 18, 49, 17);  // rocky field margins
    scatter_boulders(w, 0, 52, 22, 79, 49, 13);
    save_level_files(w, 605, "The Deeping Wall",
                     {"Hold the wall. Nothing gets",
                      "through the gate. At dawn,",
                      "look to the east."});
}

// 606 THE WIZARD'S VALE: a ringed vale — forest, garden ring, moat, cobble
// ring — around a four-story tower. Floors 1-2 are stone (floor 2 carpeted:
// the library), floor 3 is the glass observatory. The player's crew deploys
// in a besieging ring at the forest's edge; the tower (team 2) holds.
void build_wizards_vale()
{
    LevelRuntimeData level(606, true, &headless_level_data_hooks());
    init_world(level, 4, 60, 60);
    GameWorld& w = level.world();
    constexpr double cx = 29.5, cy = 29.5;

    // Floor 0 rings: forest, garden ring, moat, cobble ring, tower footprint.
    // (The garden ring is 4 tiles wide so the 2x2 crew markers on the 20.5
    // ring never clip the moat or the tree line.)
    paint_ring(w.grid, cx, cy, 22.5, 100.0, PIX_TREE_M1);
    paint_ring(w.grid, cx, cy, 18.5, 22.5, PIX_GRASS_LIGHT_1); // ring gardens
    paint_ring(w.grid, cx, cy, 15.5, 18.5, PIX_WATER1);
    paint_ring(w.grid, cx, cy, 0.0, 15.5, PIX_COBBLE_1);
    // Waystone glades among the trees (each gets a standing stone below).
    static constexpr int glades[3][2] = {{51, 42}, {8, 42}, {29, 4}};
    for (const auto& c : glades)
        paint_rect(w.grid, c[0] - 1, c[1] - 1, c[0] + 2, c[1] + 2, PIX_GRASS1);
    // The tower ground floor: walls with north and south doors.
    paint_rect(w.grid, 23, 23, 36, 36, PIX_WALL2);
    // Upper floors: air everywhere except the tower.
    for (int f = 1; f <= 3; ++f)
    {
        paint_rect(w.grid_for_floor(f), 0, 0, 59, 59, PIX_AIR);
        paint_rect(w.grid_for_floor(f), 23, 23, 36, 36, PIX_WALL2);
    }
    paint_rect(w.grid_for_floor(2), 26, 26, 33, 33, PIX_CARPET_M); // library
    smooth_world(w);
    // Post-smoothing decor: interiors, doors, causeways, glass observatory.
    paint_pavement(w.grid, 24, 24, 35, 35);
    paint_pavement(w.grid, 29, 23, 30, 23); // north door
    paint_pavement(w.grid, 29, 36, 30, 36); // south door
    paint_pavement(w.grid, 29, 9, 30, 15);  // causeways across the moat
    paint_pavement(w.grid, 29, 44, 30, 50);
    paint_pavement(w.grid, 9, 29, 15, 30);
    paint_pavement(w.grid, 44, 29, 50, 30);
    // The fountain court on the south approach: a paved court around a
    // water basin, cornered by columns (clear of the door wards' footprints).
    paint_pavement(w.grid, 27, 41, 32, 44);
    paint_rect(w.grid, 29, 42, 30, 43, PIX_WATER1);
    paint(w.grid, 26, 41, PIX_COLUMN1);
    paint(w.grid, 33, 41, PIX_COLUMN2);
    paint(w.grid, 26, 44, PIX_COLUMN2);
    paint(w.grid, 33, 44, PIX_COLUMN1);
    for (const auto& c : glades) // the standing stones
        paint(w.grid, c[0], c[1], PIX_BOULDER_1);
    // Tower interiors: pavement on floor 1, a pavement gallery around the
    // library carpet on floor 2, glass under the sky on floor 3.
    paint_pavement(w.grid_for_floor(1), 24, 24, 35, 35);
    paint(w.grid_for_floor(1), 27, 27, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 32, 32, PIX_BRAZIER1);
    paint_pavement(w.grid_for_floor(2), 24, 24, 35, 25);
    paint_pavement(w.grid_for_floor(2), 24, 34, 35, 35);
    paint_pavement(w.grid_for_floor(2), 24, 26, 25, 33);
    paint_pavement(w.grid_for_floor(2), 34, 26, 35, 33);
    paint_rect(w.grid_for_floor(3), 24, 24, 35, 35, PIX_GLASS);
    paint(w.grid_for_floor(3), 24, 24, PIX_COLUMN1); // observatory corners
    paint(w.grid_for_floor(3), 35, 24, PIX_COLUMN2);
    paint(w.grid_for_floor(3), 24, 35, PIX_COLUMN2);
    stair_pair(w, 0, 24, 24); // the interior stair chain spirals corner
    stair_pair(w, 1, 35, 24); // to corner: NW, then NE, then SE
    stair_pair(w, 2, 35, 35);

    // The tower (team 2): mages on every floor, golems at the doors, the
    // wizard alone on the glass top.
    static constexpr int mage_f0[6][2] = {{26, 26}, {33, 26}, {26, 33},
                                          {33, 33}, {29, 30}, {30, 29}};
    static constexpr int mage_f1[6][2] = {{25, 25}, {34, 25}, {25, 34},
                                          {34, 34}, {25, 30}, {34, 30}};
    static constexpr int mage_f2[6][2] = {{25, 25}, {34, 25}, {25, 34},
                                          {34, 34}, {25, 29}, {34, 29}};
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_MAGE, 2, 0, mage_f0[i][0], mage_f0[i][1],
                     3 + (i % 3), true);
        place_living(w, FAMILY_MAGE, 2, 1, mage_f1[i][0], mage_f1[i][1],
                     3 + (i % 3), true);
        place_living(w, FAMILY_MAGE, 2, 2, mage_f2[i][0], mage_f2[i][1],
                     4 + (i % 3), true);
    }
    place_living(w, FAMILY_MAGE, 2, 3, 29, 29, 10, true); // the wizard
    place_living(w, FAMILY_GOLEM, 2, 0, 26, 20, 5, true); // door wards
    place_living(w, FAMILY_GOLEM, 2, 0, 31, 20, 5, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 26, 37, 5, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 31, 37, 5, true);
    place_generator(w, FAMILY_TOWER, 2, 1, 28, 26, 5);
    place_generator(w, FAMILY_TOWER, 2, 2, 28, 30, 5);

    // The player's crew: the lead pair on the south causeway (stacked — the
    // causeway is exactly one 2x2 marker wide), then a besieging ring through
    // the gardens — every other post of the old elf ring plus the druid
    // stations between them.
    place_start(w, 0, 29, 46);
    place_start(w, 0, 29, 48);
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 30; i += 2)
    {
        const double a = (2.0 * kPi * i) / 30.0;
        const int tx = static_cast<int>(std::lround(cx + 20.5 * std::cos(a)));
        const int ty = static_cast<int>(std::lround(cy + 20.5 * std::sin(a)));
        place_start(w, 0, tx, ty);
    }
    for (int i = 0; i < 6; ++i)
    {
        const double a = (2.0 * kPi * (i + 0.5)) / 6.0;
        const int tx = static_cast<int>(std::lround(cx + 20.5 * std::cos(a)));
        const int ty = static_cast<int>(std::lround(cy + 20.5 * std::sin(a)));
        place_start(w, 0, tx, ty);
    }

    place_exit(w, 3, 31, 31, 607); // the observatory is the prize
    save_level_files(w, 606, "The Wizard's Vale",
                     {"The wizard watches from his",
                      "glass crown. Climb and end it."});
}

// 607 THE BRIDGE OF SHADOW: a narrow stone causeway spans a sea of air on
// floor 1; whatever falls lands in the bone-strewn cavern on floor 0. The
// player's crew holds the west; the dead (team 2) pour from the east, and
// the Grey Wizard stands alone at mid-span.
void build_bridge_of_shadow()
{
    LevelRuntimeData level(607, true, &headless_level_data_hooks());
    init_world(level, 2, 70, 40);
    GameWorld& w = level.world();

    // Floor 0: the dark cavern — moss beds glowing in the black, and two
    // chasms that even the light abandons. Floor 1: air, two landings, the
    // causeway, and the crumbling side-spans of the older, greater bridge.
    paint_rect(w.grid, 0, 0, 69, 39, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 10, 22, 16, 27, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 30, 12, 36, 16, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 45, 25, 52, 30, PIX_GRASS_DARK_1);
    paint_rect(w.grid_for_floor(1), 0, 0, 69, 39, PIX_AIR);
    smooth_world(w);
    paint_rect(w.grid, 24, 2, 29, 5, PIX_VOID1);   // the chasms
    paint_rect(w.grid, 55, 34, 61, 38, PIX_VOID1);
    paint_pavement(w.grid_for_floor(1), 2, 15, 8, 24);   // west landing
    paint_pavement(w.grid_for_floor(1), 61, 15, 67, 24); // east landing
    paint_pavement(w.grid_for_floor(1), 9, 19, 60, 21);  // the causeway
    // Crumbling side-spans: dead-end spurs bitten by air, their broken lips
    // shored with directional drop-blocks.
    paint_pavement(w.grid_for_floor(1), 20, 12, 22, 18); // north spur
    paint(w.grid_for_floor(1), 20, 12, PIX_AIR);
    paint(w.grid_for_floor(1), 22, 14, PIX_AIR);
    paint(w.grid_for_floor(1), 21, 12, PIX_DROPBLOCK_UP);
    paint_pavement(w.grid_for_floor(1), 47, 22, 49, 30); // south spur
    paint(w.grid_for_floor(1), 49, 30, PIX_AIR);
    paint(w.grid_for_floor(1), 47, 27, PIX_AIR);
    paint(w.grid_for_floor(1), 48, 30, PIX_DROPBLOCK_DOWN);
    stair_pair(w, 0, 4, 20);  // stairs down from each landing
    stair_pair(w, 0, 65, 20);

    // The player's crew holds the west: the shield-line lead on the span,
    // the column behind it, pickets on the west landing.
    place_start(w, 1, 33, 19);
    place_start(w, 1, 33, 21);
    place_start(w, 1, 31, 19);
    place_start(w, 1, 31, 21);
    for (int i = 0; i < 5; ++i)
    {
        place_start(w, 1, 12 + i * 4, 19);
        place_start(w, 1, 12 + i * 4, 21);
    }
    place_start(w, 1, 3, 17);
    place_start(w, 1, 3, 23);
    place_start(w, 1, 5, 20);
    // He shall not pass: the Grey Wizard guards mid-span on his own two
    // feet — specials disabled, he cannot teleport away.
    place_hero(w, FAMILY_MAGE, 1, 35, 20, 9, "Grey Wizard", true, true, 0);

    // The pursuers (team 2): skeletons and ghosts from the east, the shadow.
    for (int i = 0; i < 8; ++i)
    {
        place_living(w, FAMILY_SKELETON, 2, 1, 45 + i * 2, 19, 2 + (i % 3));
        place_living(w, FAMILY_SKELETON, 2, 1, 45 + i * 2, 21, 2 + (i % 3));
    }
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_SKELETON, 2, 1, 52 + i * 2, 20, 3);
    static constexpr int ghost_xs[4] = {48, 52, 56, 60};
    for (int i = 0; i < 4; ++i)
    {
        place_living(w, FAMILY_GHOST, 2, 1, ghost_xs[i], 17, 3); // flying
        place_living(w, FAMILY_GHOST, 2, 1, ghost_xs[i], 23, 3); // beside it
    }
    place_generator(w, FAMILY_BONES, 2, 1, 62, 16, 6);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 1, 38, 20, 10); // THE SHADOW

    // Floor 0: slimes wandering among the fallen.
    place_living(w, FAMILY_SLIME, 2, 0, 15, 10, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 40, 8, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 25, 30, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 51, 32, 2);

    place_exit(w, 1, 66, 23, 608);
    scatter_boulders(w, 0, 0, 0, 69, 39, 19); // the fallen, among boulders
    scatter_litter(w, 0, 0, 0, 69, 39, 26);   // and the bones of the rest
    save_level_files(w, 607, "The Bridge of Shadow",
                     {"The bridge is narrow.", "He shall not pass."});
}

// 608 UNDER THE MOUNTAIN: a descent — columned entry hall (floor 2), the
// great gallery with its carpeted nave and twin colonnades (floor 1), and the
// torchlit treasure vault (floor 0) where the wardens (team 2) and their
// sleeping keeper await the player's crew.
void build_under_the_mountain()
{
    LevelRuntimeData level(608, true, &headless_level_data_hooks());
    init_world(level, 3, 60, 60);
    GameWorld& w = level.world();

    // Floor 0: dark cavern (moss in the corners) with the walled vault;
    // carpet marks the hoard.
    paint_rect(w.grid, 0, 0, 59, 59, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 4, 2, 14, 8, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 38, 4, 50, 10, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 2, 20, 57, 57, PIX_WALL2);
    paint_pavement(w.grid, 3, 21, 56, 56);               // vault interior
    paint_rect(w.grid, 18, 30, 41, 47, PIX_CARPET_M);    // the hoard carpet
    // Floor 1: the great gallery, its nave carpeted end to end. Floor 2: the
    // entry hall.
    paint_rect(w.grid_for_floor(1), 0, 0, 59, 59, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 4, 4, 55, 44, PIX_WALL2);
    paint_pavement(w.grid_for_floor(1), 5, 5, 54, 43);   // gallery interior
    paint_rect(w.grid_for_floor(1), 8, 21, 51, 27, PIX_CARPET_M); // the nave
    paint_rect(w.grid_for_floor(2), 0, 0, 59, 59, PIX_AIR);
    paint_rect(w.grid_for_floor(2), 4, 4, 55, 18, PIX_WALL2);
    paint_pavement(w.grid_for_floor(2), 5, 5, 54, 17);   // hall interior
    paint_rect(w.grid_for_floor(2), 6, 10, 50, 12, PIX_CARPET_M); // runner
    smooth_world(w);
    paint(w.grid, 17, 29, PIX_BRAZIER1); // braziers frame the hoard
    paint(w.grid, 42, 29, PIX_BRAZIER1);
    paint(w.grid, 17, 48, PIX_BRAZIER1);
    paint(w.grid, 42, 48, PIX_BRAZIER1);
    for (int px = 6; px <= 54; px += 8)  // torches down the vault's north wall
        paint(w.grid, px, 21, PIX_TORCH1);
    // The twin colonnades flanking the gallery nave, and corner braziers.
    for (int px = 12; px <= 48; px += 6)
    {
        paint(w.grid_for_floor(1), px, 19,
              (px % 12 == 0) ? PIX_COLUMN1 : PIX_COLUMN2);
        paint(w.grid_for_floor(1), px, 29,
              (px % 12 == 0) ? PIX_COLUMN2 : PIX_COLUMN1);
    }
    paint(w.grid_for_floor(1), 6, 6, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 53, 6, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 6, 42, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 53, 42, PIX_BRAZIER1);
    for (int px = 20; px <= 44; px += 6) // hall columns march to the stair
    {
        paint(w.grid_for_floor(2), px, 9, PIX_COLUMN1);
        paint(w.grid_for_floor(2), px, 13, PIX_COLUMN2);
    }
    stair_pair(w, 1, 52, 11); // hall's far end, down into the gallery
    stair_pair(w, 0, 7, 41);  // across the gallery, down into the vault

    // The wardens (team 2): guards ringing the hoard, its keeper at center.
    static constexpr int golems[6][2] = {{19, 31}, {28, 31}, {37, 31},
                                         {19, 43}, {28, 44}, {37, 43}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_GOLEM, 2, 0, golems[i][0], golems[i][1],
                     5 + (i % 3), true);
    static constexpr int giants[4][2] = {{24, 34}, {32, 34}, {24, 40}, {32, 40}};
    for (const auto& g : giants)
        place_living(w, FAMILY_GIANT_SKELETON, 2, 0, g[0], g[1], 8, true);
    static constexpr int gallery_skeletons[10][2] = {
        {9, 9},   {17, 13}, {25, 9},  {33, 13}, {41, 9},
        {49, 13}, {13, 25}, {29, 25}, {45, 25}, {29, 35}};
    for (int i = 0; i < 10; ++i)
        place_living(w, FAMILY_SKELETON, 2, 1, gallery_skeletons[i][0],
                     gallery_skeletons[i][1], 2 + (i % 2));
    place_generator(w, FAMILY_BONES, 2, 0, 27, 52, 5);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 29, 38, 10, true); // the keeper

    // The player's crew assembles in the entry hall: cleric lead at the
    // front line, the barbarian file, elf pairs, thieves in the rear.
    place_start(w, 2, 16, 9);
    place_start(w, 2, 16, 13);
    for (int i = 0; i < 6; ++i)
        place_start(w, 2, 14, 6 + i * 2);
    for (int i = 0; i < 4; ++i)
    {
        place_start(w, 2, 10, 7 + i * 2);
        place_start(w, 2, 12, 7 + i * 2);
    }
    for (int i = 0; i < 6; ++i)
    {
        place_start(w, 2, 6, 6 + i * 2);
        place_start(w, 2, 8, 6 + i * 2);
    }

    // The hoard itself, denser than ever: gold, silver, provisions, and four
    // old potions at the corners. Cells under a warden's feet stay bare.
    for (int gy = 32; gy <= 44; gy += 2)
        for (int gx = 20; gx <= 38; gx += 2)
        {
            if (cell_near_entity(w, 0, gx, gy, 0))
                continue;
            const int fam = (((gx + gy) / 2) % 3 == 2) ? FAMILY_SILVER_BAR
                                                       : FAMILY_GOLD_BAR;
            place(w, Order::Treasure, fam, 0, 0, gx, gy);
        }
    for (int i = 0; i < 6; ++i)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 21 + i * 3, 46);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 33);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 33);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 45);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 45);

    place_exit(w, 0, 30, 39, 609); // the heart of the hoard
    save_level_files(w, 608, "Under the Mountain",
                     {"The hoard sleeps below.", "So does its keeper."});
}

// 609 THE BLACK GATE: a north-south wall with a six-tile gate splits the map;
// gatehouse ramparts flank the gap on floor 1. The player's host deploys on
// the western meadows; the legion (team 2) pours through the gate from the
// torch-lined, bone-littered eastern waste.
void build_black_gate()
{
    LevelRuntimeData level(609, true, &headless_level_data_hooks());
    init_world(level, 2, 90, 50);
    GameWorld& w = level.world();

    // Floor 0: grass west (woods at the corners, soft meadow bands), the
    // wall, blasted dirt waste east with one foul pool.
    paint_rect(w.grid, 50, 0, 89, 49, PIX_DIRT_1);
    paint_rect(w.grid, 2, 2, 10, 8, PIX_TREE_M1);
    paint_rect(w.grid, 2, 42, 12, 48, PIX_TREE_M1);
    paint_rect(w.grid, 2, 12, 9, 20, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 3, 28, 10, 36, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 74, 38, 84, 46, PIX_GRASS1); // fringe of the pool
    paint_rect(w.grid, 76, 40, 82, 44, PIX_WATER1);
    paint_rect(w.grid, 44, 0, 49, 49, PIX_WALL2);
    // Floor 1: the two gatehouse ramparts flanking the gate, edged by air.
    paint_rect(w.grid_for_floor(1), 0, 0, 89, 49, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 44, 22, 49, 27);              // the gate gap
    paint_path(w.grid, 2, 24, 43, 25);                   // the war-road west
    paint_path(w.grid, 50, 24, 86, 25);                  // and into the waste
    // Torch standards line the legion's approach like banners.
    for (int px = 54; px <= 75; px += 7)
    {
        paint(w.grid, px, 21, PIX_TORCH1);
        paint(w.grid, px, 28, PIX_TORCH1);
    }
    paint(w.grid, 51, 21, PIX_TORCH1); // flanking the gate mouth
    paint(w.grid, 51, 28, PIX_TORCH1);
    paint_pavement(w.grid_for_floor(1), 43, 15, 50, 20); // north gatehouse
    paint_pavement(w.grid_for_floor(1), 43, 29, 50, 34); // south gatehouse
    paint(w.grid_for_floor(1), 44, 15, PIX_BRAZIER1);    // watch fires
    paint(w.grid_for_floor(1), 49, 15, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 44, 34, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 49, 34, PIX_BRAZIER1);
    stair_pair(w, 0, 50, 17); // into the gatehouses from the east side
    stair_pair(w, 0, 50, 32);

    // The player's host on the western meadows, in the old parade's posture:
    // the banner pair leads, the barbarian vanguard, then thinned ranks —
    // soldiers, elf line, mage line, clerics — 30 posts for a full crew.
    place_start(w, 0, 41, 23);
    place_start(w, 0, 41, 26);
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, 39, 18 + i * 4);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
            if ((row + col) % 2 == 0)
                place_start(w, 0, 24 + col * 3, 14 + row * 5);
    for (int i = 0; i < 7; i += 2)
        place_start(w, 0, 20, 12 + i * 4);
    for (int i = 0; i < 3; i += 2)
        place_start(w, 0, 17, 18 + i * 6);
    for (int i = 0; i < 6; i += 2)
        place_start(w, 0, 14, 14 + i * 4);
    for (int i = 0; i < 4; i += 2)
        place_start(w, 0, 11, 18 + i * 4);

    // The legion (team 2): orcs in and around the gate, big-orc wings,
    // skeletons and ghosts behind, champions on the ramparts, the east camp.
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_ORC, 2, 0, 46, 22 + i, 2 + (i % 3)); // in the
        place_living(w, FAMILY_ORC, 2, 0, 48, 22 + i, 2 + (i % 3)); // gate
    }
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 6; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 52 + col * 3, 20 + row * 5,
                         2 + ((row + col) % 3));
    for (int i = 0; i < 3; ++i)
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 12, 3 + (i % 3));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 16, 3 + (i % 3));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 36, 4 + (i % 2));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 40, 4 + (i % 2));
    }
    for (int i = 0; i < 5; ++i)
    {
        place_living(w, FAMILY_SKELETON, 2, 0, 70, 15 + i * 5, 2 + (i % 3));
        place_living(w, FAMILY_SKELETON, 2, 0, 74, 15 + i * 5, 2 + (i % 3));
    }
    place_living(w, FAMILY_GHOST, 2, 0, 56, 8, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 60, 42, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 68, 10, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 68, 40, 3);
    place_living(w, FAMILY_ORC, 2, 1, 46, 17, 8, true); // rampart champions
    place_living(w, FAMILY_ORC, 2, 1, 46, 32, 8, true);
    place_generator(w, FAMILY_TENT, 2, 0, 83, 13, 5);
    place_generator(w, FAMILY_BONES, 2, 0, 83, 23, 5);
    place_generator(w, FAMILY_TENT, 2, 0, 83, 34, 5);

    place_exit(w, 0, 88, 24, 610); // through the gate, beyond the waste
    scatter_boulders(w, 0, 51, 0, 89, 49, 23); // the blasted waste
    scatter_litter(w, 0, 52, 0, 89, 49, 29);   // strewn with old bones
    save_level_files(w, 609, "The Black Gate",
                     {"Two armies. One gate.", "No quarter."});
}

// 610 THE FROZEN WALL: the wall IS the arena — floor 0 at its base (wild
// north, farm villages south), floor 1 the brazier-lit interior galleries,
// floor 2 the open-sky top ribbon. The player's crew mans the Watch posts;
// the Wild (team 2) climbs from the north via the end stairs.
void build_frozen_wall()
{
    LevelRuntimeData level(610, true, &headless_level_data_hooks());
    init_world(level, 3, 80, 45);
    GameWorld& w = level.world();

    // Floor 0: the wall body with end chambers and a center gate; the wild
    // north (trees, tundra, the frozen mere), farms and hearths south.
    paint_rect(w.grid, 0, 18, 79, 23, PIX_WALL2);
    paint_rect(w.grid, 8, 2, 20, 7, PIX_TREE_M1); // beyond-the-wall woods
    paint_rect(w.grid, 24, 5, 32, 10, PIX_TREE_M1);
    paint_rect(w.grid, 52, 2, 62, 7, PIX_TREE_M1);
    paint_rect(w.grid, 64, 2, 74, 6, PIX_TREE_M1);
    paint_rect(w.grid, 36, 3, 48, 9, PIX_WATER1); // the frozen mere
    paint_rect(w.grid, 2, 2, 7, 5, PIX_GRASS_DARK_1); // tundra scrub
    paint_rect(w.grid, 44, 11, 50, 14, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 66, 8, 76, 12, PIX_GRASS_DARK_1);
    // South of the wall: two farmsteads and their strip fields.
    paint_rect(w.grid, 18, 30, 22, 33, PIX_WALL2);       // west hut
    paint_rect(w.grid, 56, 29, 60, 32, PIX_WALL2);       // east hut
    paint_rect(w.grid, 8, 36, 15, 41, PIX_GRASS_LIGHT_1); // strip fields
    paint_rect(w.grid, 25, 35, 31, 40, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 63, 34, 71, 40, PIX_GRASS_LIGHT_1);
    // Floor 1: interior galleries inside the wall footprint.
    paint_rect(w.grid_for_floor(1), 0, 0, 79, 44, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 0, 18, 79, 23, PIX_WALL2);
    // Floor 2: the top ribbon, open sky on every side.
    paint_rect(w.grid_for_floor(2), 0, 0, 79, 44, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 0, 18, 3, 22);   // west end chamber (opens north;
    paint_pavement(w.grid, 76, 18, 79, 22); // east end chamber — climbers go UP)
    paint_pavement(w.grid, 38, 18, 41, 23); // the gate
    paint_path(w.grid, 39, 10, 40, 17);     // trails worn to the gate,
    paint_path(w.grid, 39, 24, 40, 31);     // north and south
    paint_pavement(w.grid, 19, 31, 21, 32); // hut hearth floors + doors
    paint_pavement(w.grid, 20, 33, 20, 33);
    paint_pavement(w.grid, 57, 30, 59, 31);
    paint_pavement(w.grid, 58, 32, 58, 32);
    paint_path(w.grid, 20, 34, 38, 34);     // village lanes, door to road
    paint_path(w.grid, 41, 33, 58, 33);
    paint_pavement(w.grid_for_floor(1), 1, 19, 78, 22); // gallery corridors
    for (int px = 10; px <= 70; px += 10) // partitions with door gaps
    {
        paint(w.grid_for_floor(1), px, 19, PIX_WALL2);
        paint(w.grid_for_floor(1), px, 22, PIX_WALL2);
    }
    static constexpr int gallery_braziers[6][2] = {
        {8, 19}, {18, 22}, {28, 19}, {38, 22}, {58, 19}, {68, 22}};
    for (const auto& b : gallery_braziers) // hearth-light room to room
        paint(w.grid_for_floor(1), b[0], b[1], PIX_BRAZIER1);
    paint_rect(w.grid_for_floor(1), 33, 20, 37, 21, PIX_CARPET_M); // barracks rug
    paint_pavement(w.grid_for_floor(2), 0, 18, 79, 23);
    for (int px = 0; px <= 79; ++px) // stone/glass ribbon under open sky
        if (px % 8 == 3 || px % 8 == 4)
            for (int py = 18; py <= 23; ++py)
                paint(w.grid_for_floor(2), px, py, PIX_GLASS);
    static constexpr int top_braziers[5] = {13, 23, 33, 53, 63};
    for (const int px : top_braziers) // fire posts between the watch posts
        paint(w.grid_for_floor(2), px, 18, PIX_BRAZIER1);
    stair_pair(w, 0, 1, 20);  // climber stairs at both wall ends: 0 -> 1
    stair_pair(w, 0, 78, 20);
    stair_pair(w, 1, 4, 20);  // and up again: 1 -> 2
    stair_pair(w, 1, 75, 20);

    // The Watch is yours: the champion's door lead in the gallery, the
    // patrol rooms, then every post along the wind-scoured top.
    place_start(w, 1, 48, 20);
    place_start(w, 1, 15, 20); // gallery patrol posts
    place_start(w, 1, 35, 21);
    place_start(w, 1, 55, 20);
    place_start(w, 1, 65, 21);
    static constexpr int watch_posts[14] = {6,  11, 16, 21, 26, 31, 36,
                                            43, 48, 53, 58, 63, 68, 73};
    for (int i = 0; i < 14; ++i)
        place_start(w, 2, watch_posts[i], 20 + (i % 2));
    static constexpr int elf_posts[6] = {9, 19, 29, 45, 55, 65};
    for (int i = 0; i < 6; ++i)
        place_start(w, 2, elf_posts[i], 19);
    // The gallery tower musters mages for the Watch.
    place_generator(w, FAMILY_TOWER, 0, 1, 43, 19, 4);

    // The Wild (team 2): war bands massing at both end stairs, ghosts over
    // the mere, the giant at the gate, camps in the northern woods.
    for (int i = 0; i < 9; ++i)
    {
        place_living(w, FAMILY_BARBARIAN, 2, 0, 2 + (i % 3) * 3, 8 + (i / 3) * 3,
                     2 + (i % 3));
        place_living(w, FAMILY_BARBARIAN, 2, 0, 71 + (i % 3) * 3, 8 + (i / 3) * 3,
                     2 + (i % 3));
    }
    static constexpr int ghosts[10][2] = {{26, 15}, {34, 15}, {46, 15},
                                          {54, 15}, {30, 12}, {50, 12},
                                          {38, 14}, {42, 14}, {22, 12},
                                          {58, 12}};
    for (const auto& g : ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, g[0], g[1], 3);
    static constexpr int skels[8][2] = {{12, 15}, {18, 15}, {62, 15}, {68, 15},
                                        {20, 13}, {65, 12}, {12, 9},  {64, 9}};
    for (const auto& s : skels)
        place_living(w, FAMILY_SKELETON, 2, 0, s[0], s[1], 2);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 38, 15, 9); // the giant at
    place_generator(w, FAMILY_BONES, 2, 0, 14, 10, 5);       // the gate
    place_generator(w, FAMILY_TENT, 2, 0, 57, 10, 5);

    place_exit(w, 2, 40, 21, 600); // the top of the Wall; then home again
    scatter_boulders(w, 0, 0, 0, 79, 16, 19); // the stony wild north
    save_level_files(w, 610, "The Frozen Wall",
                     {"The Wall has stood for an age.", "Tonight it is tested."});
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
        << "    floors, and arcing projectiles —\n"
        << "    then six epic multifloor war\n"
        << "    stories: a besieged wall, a\n"
        << "    wizard's tower, a bridge over\n"
        << "    shadow, a dragon's vault, the\n"
        << "    Black Gate, and the frozen Wall.\n";
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

} // namespace
} // namespace conceptgen

int main(int argc, char* argv[])
{
    using namespace conceptgen;
    namespace fs = std::filesystem;

    const std::string out_glad =
        (argc > 1) ? argv[1] : "builtin/org.openglad.concept.glad";
    const fs::path out_abs = fs::absolute(out_glad);

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

    build_stairs();
    build_mind_the_gap();
    build_glasshouse();
    build_drop_zone();
    build_arc_range();
    build_deeping_wall();
    build_wizards_vale();
    build_bridge_of_shadow();
    build_under_the_mountain();
    build_black_gate();
    build_frozen_wall();

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
                {605, 2, "The Deeping Wall", 26, 1, 1, 0, 0, 47, 2, 1, 0, true},
                {606, 4, "The Wizard's Vale", 23, 0, 0, 0, 0, 23, 2, 0, 0, true},
                {607, 2, "The Bridge of Shadow", 17, 1, 0, 0, 0, 33, 1, 0, 1, true},
                {608, 3, "Under the Mountain", 28, 0, 0, 0, 0, 21, 1, 0, 0, true},
                {609, 2, "The Black Gate", 30, 0, 0, 0, 0, 58, 3, 0, 0, true},
                {610, 3, "The Frozen Wall", 25, 0, 1, 0, 0, 37, 2, 0, 0, true},
            };
            for (const ExpectedLevel& e : expectations)
                self_check_level(e);
            (void)unmount_campaign_package_with_error("org.openglad.concept");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        std::error_code ec;
        fs::create_directories(out_abs.parent_path(), ec);
        fs::copy_file(glad_path, out_abs, fs::copy_options::overwrite_existing, ec);
        if (ec)
            fail(std::format("failed to copy {} -> {}: {}", glad_path,
                             out_abs.string(), ec.message()));
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
