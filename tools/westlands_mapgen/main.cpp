/* War of the Westlands campaign generator.
 *
 * Produces builtin/org.openglad.westlands.glad: a 26-level story campaign
 * (docs at scratch design + docs/z-axis-design.md) built act by act — the
 * flight east, the dark road, the war in the west, the burden's road, and
 * the convergence at the Mountain of Fire. In every battle the player's
 * crew IS the defense: team-0 start markers arranged in a tactical
 * formation deploy the whole team, a few placed team-0 allies (named
 * heroes, defender generators) fight alongside, and the enemy host is
 * team 2. SDL-free; reuses the headless platform glue, mirrors
 * tools/concept_mapgen (where the six epic war stories now numbered
 * 6, 7, 8, 14, 15 and 17 were first authored). Builds the v10 multi-floor
 * scenario format, zips a campaign package, mounts it, and self-checks
 * every registered level by reloading it — including exit-destination
 * validation against the registered id set.
 *
 * Usage: westlands_mapgen [output.glad]
 *        (default: builtin/org.openglad.westlands.glad)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "builders.h"

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
#include <set>
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

namespace westlands {

namespace {
int g_errors = 0;
} // namespace

void fail(const std::string& message)
{
    std::fprintf(stderr, "westlands_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

void warn(const std::string& message)
{
    std::fprintf(stderr, "westlands_mapgen: WARNING: %s\n", message.c_str());
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

void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1, unsigned char tile)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, tile);
}

void paint_pavement(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[3] = {PIX_PAVEMENT1, PIX_PAVEMENT2,
                                                  PIX_PAVEMENT3};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 3]);
}

void paint_path(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[4] = {PIX_PATH_1, PIX_PATH_2,
                                                  PIX_PATH_3, PIX_PATH_4};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 5 + y * 3) % 4]);
}

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

void stair_pair(GameWorld& w, int f, int tx, int ty)
{
    paint(w.grid_for_floor(f), tx, ty, PIX_ZSTAIR_UP);
    paint(w.grid_for_floor(f + 1), tx, ty, PIX_ZSTAIR_DOWN);
}

void smooth_world(GameWorld& w)
{
    for (int f = 0; f < w.floor_count(); ++f)
        w.smoother_for_floor(f).smooth();
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

void set_npc_extras(walker* ob, bool specials_disabled, int spawn_delay)
{
    if (ob == nullptr)
        return;
    ob->set_specials_disabled(specials_disabled);
    ob->set_spawn_delay(static_cast<std::uint16_t>(spawn_delay));
}

walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard, bool specials_disabled,
                     int spawn_delay)
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

void place_start(GameWorld& w, int floor, int tx, int ty)
{
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, floor, tx, ty);
}

walker* place_hero(GameWorld& w, int family, int floor, int tx, int ty,
                   int level, const char* name, bool guard,
                   bool specials_disabled, int spawn_delay)
{
    walker* ob = place_living(w, family, 0, floor, tx, ty, level, guard,
                              specials_disabled, spawn_delay);
    if (ob != nullptr)
        ob->stats()->name = name;
    return ob;
}

void place_exit(GameWorld& w, int floor, int tx, int ty, int destination)
{
    walker* e = place(w, Order::Treasure, FAMILY_EXIT, 0, floor, tx, ty);
    if (e != nullptr)
        e->stats()->set_level(destination);
}

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

namespace {

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

} // namespace

void scatter_boulders(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, int modulus)
{
    static constexpr unsigned char boulders[4] = {PIX_BOULDER_1, PIX_BOULDER_2,
                                                  PIX_BOULDER_3, PIX_BOULDER_4};
    scatter_decor(w, floor, tx0, ty0, tx1, ty1, modulus, boulders);
}

void scatter_litter(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                    int ty1, int modulus)
{
    static constexpr unsigned char litter[4] = {
        PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2, PIX_JAGGED_GROUND_3,
        PIX_JAGGED_GROUND_4};
    scatter_decor(w, floor, tx0, ty0, tx1, ty1, modulus, litter);
}

void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description,
                      int par_value, int time_bonus_limit)
{
    world.title = title;
    world.par_value = static_cast<short>(par_value);
    world.time_bonus_limit = static_cast<short>(time_bonus_limit);

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
    std::printf("westlands_mapgen: built %d '%s' (%d floors)\n", id, title,
                world.floor_count());
}

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

namespace {

void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           War of the Westlands\n"
        << "version:         1\n"
        << "first_level:     1\n"
        << "suggested_power: 0\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    The Westlands burn. A small\n"
        << "    burden must cross a great war:\n"
        << "    the flight through the forest,\n"
        << "    the dark road under the mountain,\n"
        << "    sieges of wall and gate and city,\n"
        << "    marsh and spider-pass and ash —\n"
        << "    until two roads meet again at\n"
        << "    the Mountain of Fire, and all\n"
        << "    is weighed in the crack of the\n"
        << "    world.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: the White Tree — a bare white tree with three tiers of
// upswept branches on a pure black field. Painted in the engine palette's
// grey ramp (16..31 runs dark -> bright; 31 is the brightest white). No
// cycled palette bands anywhere: the sigil must not shimmer.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = make_grid(kSize, kSize, 0); // the black field
    auto put = [&](int x, int y, unsigned char c) { paint(icon, x, y, c); };
    for (int x = 11; x <= 20; ++x)              // the root mound
        for (int y = 28; y <= 29; ++y)
            put(x, y, 24);
    for (int y = 10; y <= 27; ++y)              // the trunk, two shaded columns
    {
        put(15, y, 28);
        put(16, y, 31);
    }
    for (int i = 0; i <= 6; ++i)                // lower branch pair
    {
        put(14 - i, 21 - i, 30);
        put(17 + i, 21 - i, 30);
    }
    for (int i = 0; i <= 5; ++i)                // middle branch pair
    {
        put(14 - i, 17 - i, 30);
        put(17 + i, 17 - i, 30);
    }
    for (int i = 0; i <= 4; ++i)                // upper branch pair
    {
        put(14 - i, 13 - i, 30);
        put(17 + i, 13 - i, 30);
    }
    static constexpr int tips[6][2] = {{8, 15}, {23, 15}, {9, 12},
                                       {22, 12}, {10, 9}, {21, 9}};
    for (const auto& t : tips)                  // brightest buds at the tips
        put(t[0], t[1], 31);
    put(15, 7, 31);                             // the crown
    put(16, 7, 31);
    put(14, 8, 31);
    put(17, 8, 31);
    put(15, 6, 26);                             // a faint taper above it
    put(16, 6, 26);
    put(13, 27, 26);                            // root flare
    put(18, 27, 26);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// The full designed level graph: ids 1-26, contiguous except 18 (act gap).
// Every level is now registered, so exit-destination validation is a hard
// failure: any exit naming a level absent from the package fails the build.
constexpr bool kRequireAllDestinationsBuilt = true;

bool is_planned_level(int id)
{
    return (id >= 1 && id <= 17) || (id >= 19 && id <= 26);
}

std::string join_ids(const std::vector<int>& ids)
{
    std::string out;
    for (const int id : ids)
    {
        if (!out.empty())
            out += ", ";
        out += std::to_string(id);
    }
    return out;
}

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line).
constexpr std::size_t kBriefingLineBudget = 33;

void self_check_level(const ExpectedLevel& ex, const std::set<int>& registered)
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

    // Exit audit: the authored destination set must match the design graph
    // exactly, and every destination must exist in the package (warn-only
    // for planned-but-not-yet-built levels until the final integrate phase).
    std::vector<int> destinations;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            destinations.push_back(static_cast<int>(ob->stats()->level()));
        }
    }
    std::vector<int> expected_dests = ex.exit_destinations;
    std::sort(destinations.begin(), destinations.end());
    std::sort(expected_dests.begin(), expected_dests.end());
    if (destinations != expected_dests)
        fail(std::format("self-check scen{}: exit destinations [{}] != "
                         "expected [{}]", ex.id, join_ids(destinations),
                         join_ids(expected_dests)));
    for (const int dest : destinations)
    {
        if (registered.count(dest) != 0)
            continue;
        if (!kRequireAllDestinationsBuilt && is_planned_level(dest))
        {
            warn(std::format("scen{}: exit destination {} is planned but not "
                             "yet built", ex.id, dest));
        }
        else
        {
            fail(std::format("self-check scen{}: exit destination {} does not "
                             "exist in the package", ex.id, dest));
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

} // namespace
} // namespace westlands

int main(int argc, char* argv[])
{
    using namespace westlands;
    namespace fs = std::filesystem;

    const std::string out_glad =
        (argc > 1) ? argv[1] : "builtin/org.openglad.westlands.glad";
    const fs::path out_abs = fs::absolute(out_glad);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("westlands_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "org.openglad.gladiator")
    {
        std::fprintf(stderr, "westlands_mapgen: ERROR: stock campaign not "
                             "mounted; run next to staged assets (build dir)\n");
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

    const LevelDataHooks& hooks = headless_level_data_hooks();
    build_act1(hooks);
    build_act2(hooks);
    build_act3a(hooks);
    build_act3b(hooks);
    build_finale(hooks);

    std::vector<ExpectedLevel> expectations;
    for (auto rows : {act1_expectations(), act2_expectations(),
                      act3a_expectations(), act3b_expectations(),
                      finale_expectations()})
    {
        for (ExpectedLevel& row : rows)
            expectations.push_back(std::move(row));
    }
    std::set<int> registered;
    for (const ExpectedLevel& e : expectations)
    {
        if (!is_planned_level(e.id))
            fail(std::format("scen{} is not in the planned level graph", e.id));
        if (!registered.insert(e.id).second)
            fail(std::format("scen{} registered twice", e.id));
    }

    const std::string glad_path = user + "campaigns/org.openglad.westlands.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) != ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("org.openglad.westlands") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
            (void)unmount_campaign_package_with_error("org.openglad.westlands");
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
            std::printf("westlands_mapgen: wrote %s\n", out_abs.c_str());
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
        std::fprintf(stderr, "westlands_mapgen: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
