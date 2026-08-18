/* The Long Season campaign generator.
 *
 * Produces campaigns/longseason/ (the source tree the build
 * composes into builtin/longseason.glad): a 19-level story campaign
 * built season by season — flood dikes in spring, two armies in summer,
 * mines in autumn, snow passes in winter, and the Reckoning at the foundry
 * gate. Every briefing is an entry in the Brass Kettle Company's ledger.
 * In every battle the player's crew IS the company: team-0 start markers
 * arranged in a tactical formation deploy the whole team, a few placed
 * team-0 allies (named company hands, defender generators) fight alongside,
 * and the employer's problem is team 2 (team 1 appears only on the finale).
 * SDL-free; a structural clone of tools/westlands_mapgen. Builds the
 * multi-floor scenario format, zips a campaign package, mounts it, and
 * self-checks every registered level by reloading it — including
 * exit-destination validation against the registered id set.
 *
 * Usage: longseason_mapgen [output.glad]
 *        (default: campaigns/longseason)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "../campaign_export.h"
#include "builders.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/campaign_state_providers.h>
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
    static std::uint32_t state = 20260708u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace longseason {

namespace {
int g_errors = 0;

// The level-file unit-name field serializes through a 12-byte buffer via
// snprintf: 11 characters exactly ("The Founder" fills it). Names are
// story-bible pinned; an oversize name is an authoring bug, never a silent
// truncation.
constexpr std::size_t kUnitNameBudget = 11;

// The story bible's SAVE_ALL scoping: the campaign's npc_flags-bit-2
// protectees are EXACTLY the Assessor on level 4 and the Reeve on level 15,
// and nobody else — Kettle is protect-OPTIONAL everywhere he is placed
// (4/9/18), and no other level sets bit 2 or the SAVE_ALL type (9's
// strongroom hold and 13's wagon run are design gates, not type bits).
// save_level_files and the self-check both key off this one table so the
// package and the audit cannot drift apart.
const char* save_all_protectee_name(int id)
{
    switch (id)
    {
        case 4:
            return "Assessor"; // spring: the crown's assessor
        case 15:
            return "The Reeve"; // winter: Thornby's reeve
        default:
            return nullptr;
    }
}

} // namespace

void fail(const std::string& message)
{
    std::fprintf(stderr, "longseason_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

void warn(const std::string& message)
{
    std::fprintf(stderr, "longseason_mapgen: WARNING: %s\n", message.c_str());
}

// A grass field of (tw x th) tiles; PixieData owns the heap buffer.
PixieData make_grid(int tw, int th, unsigned char fill)
{
    auto* buf = new unsigned char[static_cast<std::size_t>(tw) * static_cast<std::size_t>(th)];
    std::fill(buf, buf + static_cast<std::size_t>(tw) * static_cast<std::size_t>(th), fill);
    return PixieData(1, static_cast<unsigned char>(tw),
                     static_cast<unsigned char>(th), buf);
}

void paint(PixieData& g, int tx, int ty, unsigned char tile)
{
    if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h)
        g.data[static_cast<std::size_t>(tx + ty * g.w)] = tile;
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
    if (decor_id >= DECOR_MAX)
    {
        fail(std::format("paint_decor: decor id {} out of range", decor_id));
        return;
    }
    const unsigned char base = g.data[static_cast<std::size_t>(tx + ty * g.w)];
    if (base == PIX_AIR || base == PIX_ZSTAIR_UP || base == PIX_ZSTAIR_DOWN ||
        base == PIX_VOID1)
    {
        fail(std::format("paint_decor: decor {} over air/stair/void base {} "
                         "at ({}, {}) floor {}", decor_id, base, tx, ty,
                         floor));
        return;
    }
    PixieData& dec = w.decor_for_floor(floor);
    if (!dec.valid())
    {
        // Lazy plane allocation, zero-filled (same pattern as add_floor's
        // grid allocation): an untouched floor keeps an invalid plane and
        // the level saves without it.
        auto* buf = new unsigned char[static_cast<std::size_t>(g.w) * g.h];
        std::fill(buf, buf + static_cast<std::size_t>(g.w) * g.h,
                  static_cast<unsigned char>(DECOR_NONE));
        dec = PixieData(1, static_cast<unsigned char>(g.w),
                        static_cast<unsigned char>(g.h), buf);
    }
    dec.data[static_cast<std::size_t>(tx + ty * dec.w)] = decor_id;
}

void paint_decor_rect(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, unsigned char decor_id)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint_decor(w, floor, x, y, decor_id);
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
    {
        ob->set_act_type(ACT_GUARD);
        // Guard wake policy (npc_flags bit 1): enemy guards are ambush
        // posts — they hold until a foe walks into genuine sight (range +
        // clear ray, walker::act_guard) and then hunt. Allied guards
        // (teams 0/1) exist to hold a spot: the Assessor and Reeve, Kettle,
        // ferrymen, fort garrisons, strongroom wardens — waking would march
        // them off their posts. Audited placement-by-placement (2026-07-11):
        // every allied guard in this campaign is a posted NPC/garrison,
        // every enemy guard (incl. Long Tom, the Miller, the Count, the
        // Founder) an ambusher whose chamber the crew must enter to be
        // seen. A future exception can override on the returned walker;
        // self_check_level enforces the allied rule on the reloaded package.
        if (team <= 1)
            ob->set_guard_hold_post(true);
    }
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

namespace {

// Shared naming arm of place_hero / place_named_foe: hard-fail an oversize
// name (the .fss field truncates silently at 11; the ledger's names are
// budgeted to fit — "The Founder" exactly).
void set_unit_name(walker* ob, const char* name)
{
    if (ob == nullptr)
        return;
    if (std::strlen(name) > kUnitNameBudget)
        fail(std::format("unit name '{}' overflows the {}-char budget", name,
                         kUnitNameBudget));
    ob->stats()->name = name;
}

} // namespace

walker* place_hero(GameWorld& w, int family, int floor, int tx, int ty,
                   int level, const char* name, bool guard,
                   bool specials_disabled, int spawn_delay)
{
    walker* ob = place_living(w, family, 0, floor, tx, ty, level, guard,
                              specials_disabled, spawn_delay);
    set_unit_name(ob, name);
    return ob;
}

walker* place_named_foe(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level, const char* name, bool guard)
{
    if (team == 0)
        fail(std::format("place_named_foe: '{}' placed on team 0 — use "
                         "place_hero for company hands", name));
    walker* ob = place_living(w, family, team, floor, tx, ty, level, guard);
    set_unit_name(ob, name);
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

// --- Fall-line support. ------------------------------------------------------
// Single-cell ground passability: the Living arm of
// GameWorld::query_grid_passable with none of the flyer / forestwalk /
// ethereal escapes — the tiles a plain ground walker can STAND on. A fall
// landing must be immediately standable; water, lava, boulder and torch
// bases all bounce the faller into the engine's landing nudge, and levels
// must not rely on the nudge.
bool ground_cell_standable(unsigned char tile)
{
    switch (tile)
    {
        case PIX_GRASS1:
        case PIX_GRASS2:
        case PIX_GRASS3:
        case PIX_GRASS4:
        case PIX_GRASS_DARK_1:
        case PIX_GRASS_DARK_2:
        case PIX_GRASS_DARK_3:
        case PIX_GRASS_DARK_4:
        case PIX_GRASS_DARK_LL:
        case PIX_GRASS_DARK_UR:
        case PIX_GRASS_DARK_B1:
        case PIX_GRASS_DARK_B2:
        case PIX_GRASS_DARK_BR:
        case PIX_GRASS_DARK_R1:
        case PIX_GRASS_DARK_R2:
        case PIX_GRASS_RUBBLE:
        case PIX_GRASS1_DAMAGED:
        case PIX_GRASS_LIGHT_1:
        case PIX_GRASS_LIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT:
        case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT:
        case PIX_GRASS_LIGHT_LEFT_TOP:
        case PIX_GRASSWATER_LL:
        case PIX_GRASSWATER_LR:
        case PIX_GRASSWATER_UL:
        case PIX_GRASSWATER_UR:
        case PIX_PAVEMENT1:
        case PIX_PAVEMENT2:
        case PIX_PAVEMENT3:
        case PIX_COBBLE_1:
        case PIX_COBBLE_2:
        case PIX_COBBLE_3:
        case PIX_COBBLE_4:
        case PIX_FLOOR_PAVEL:
        case PIX_FLOOR_PAVER:
        case PIX_FLOOR_PAVEU:
        case PIX_FLOOR_PAVED:
        case PIX_PAVESTEPS1:
        case PIX_PAVESTEPS2:
        case PIX_PAVESTEPS2L:
        case PIX_PAVESTEPS2R:
        case PIX_FLOOR1:
        case PIX_CARPET_LL:
        case PIX_CARPET_B:
        case PIX_CARPET_LR:
        case PIX_CARPET_UR:
        case PIX_CARPET_U:
        case PIX_CARPET_UL:
        case PIX_CARPET_L:
        case PIX_CARPET_M:
        case PIX_CARPET_M2:
        case PIX_CARPET_R:
        case PIX_CARPET_SMALL_HOR:
        case PIX_CARPET_SMALL_VER:
        case PIX_CARPET_SMALL_CUP:
        case PIX_CARPET_SMALL_CAP:
        case PIX_CARPET_SMALL_LEFT:
        case PIX_CARPET_SMALL_RIGHT:
        case PIX_CARPET_SMALL_TINY:
        case PIX_DIRT_1:
        case PIX_DIRTGRASS_UL1:
        case PIX_DIRTGRASS_UR1:
        case PIX_DIRTGRASS_LL1:
        case PIX_DIRTGRASS_LR1:
        case PIX_DIRT_DARK_1:
        case PIX_DIRTGRASS_DARK_UL1:
        case PIX_DIRTGRASS_DARK_UR1:
        case PIX_DIRTGRASS_DARK_LL1:
        case PIX_DIRTGRASS_DARK_LR1:
        case PIX_PATH_1:
        case PIX_PATH_2:
        case PIX_PATH_3:
        case PIX_PATH_4:
        case PIX_SNOW1:
        case PIX_SNOW2:
        case PIX_MARSH1:
        case PIX_MARSH2:
        case PIX_ASH1:
        case PIX_ASH2:
        // Z tiles a ground walker occupies at the grid layer (stairs carry
        // you off; glass and drop blocks resolve in movement) — all legal
        // landings. PIX_AIR is deliberately NOT here: the fall audit chases
        // air columns itself.
        case PIX_ZSTAIR_UP:
        case PIX_ZSTAIR_DOWN:
        case PIX_GLASS:
        case PIX_DROPBLOCK_UP:
        case PIX_DROPBLOCK_RIGHT:
        case PIX_DROPBLOCK_DOWN:
        case PIX_DROPBLOCK_LEFT:
            return true;
        default:
            return false; // walls, trees, water, lava, boulders, torches,
                          // braziers, columns, jagged litter, void, air
    }
}

// Z-furniture cells a ground walker stands on but an ash pass must never
// repaint: stairs would lose their link, glass and drop blocks their trick.
bool z_furniture_cell(unsigned char tile)
{
    switch (tile)
    {
        case PIX_ZSTAIR_UP:
        case PIX_ZSTAIR_DOWN:
        case PIX_GLASS:
        case PIX_DROPBLOCK_UP:
        case PIX_DROPBLOCK_RIGHT:
        case PIX_DROPBLOCK_DOWN:
        case PIX_DROPBLOCK_LEFT:
            return true;
        default:
            return false;
    }
}

// Base tile AND decor plane both standable for a ground walker.
bool cell_standable(GameWorld& world, int floor, int tx, int ty)
{
    const PixieData& g = world.grid_for_floor(floor);
    if (!g.valid() || tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (!ground_cell_standable(g.data[static_cast<std::size_t>(tx + ty * g.w)]))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
        if (d < DECOR_MAX &&
            kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            return false;
    }
    return true;
}

// True when (tx, ty) on 'floor' is the LANDING cell of some fall line
// above: scan up through stacked AIR cells; any air floor a ground walker
// can step into (an 8-adjacent standable cell of that same floor) drops
// its faller here. The scatters must never mine such a landing with
// blocking litter or boulders (fall-line rule) — the same way they never
// cover a Z-stair. On levels whose upper floors are untouched grass fills
// this is always false, so single-floor scatters are unaffected.
bool cell_is_fall_landing(GameWorld& w, int floor, int tx, int ty)
{
    for (int g = floor + 1; g < w.floor_count(); ++g)
    {
        const PixieData& gg = w.grid_for_floor(g);
        if (!gg.valid() || tx < 0 || ty < 0 || tx >= gg.w || ty >= gg.h ||
            gg.data[static_cast<std::size_t>(tx + ty * gg.w)] != PIX_AIR)
            return false; // solid above: no fall reaches this cell
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx != 0 || dy != 0) &&
                    cell_standable(w, g, tx + dx, ty + dy))
                    return true;
    }
    return false;
}

// Scatter a 4-variant BASE tile set over a rectangle (jagged litter — a
// full-tile terrain, not decor). Runs AFTER army placement and keeps one
// tile of clearance around every entity (and never covers a Z-stair) so no
// one spawns wedged in the scenery. A full-tile base replaces the whole
// cell, decor included — legacy overwrite semantics: when litter lands on
// a cell an earlier scatter gave a boulder, the boulder goes away exactly
// as it did when both were base tiles.
void scatter_base_tiles(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                        int ty1, int modulus,
                        const unsigned char (&variants)[4])
{
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 7 + y * 11) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            paint(g, x, y, variants[(x + y) % 4]);
            PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && x < dec.w && y < dec.h)
                dec.data[static_cast<std::size_t>(x + y * dec.w)] = DECOR_NONE;
        }
}

} // namespace

void paint_ash_open(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    // The same (x * 7 + y * 13) % 2 checker recipe as the westlands
    // paint_ash, restricted to open ground: authored structures (smoothed
    // walls, tree eaves), water/lava, void and the Z furniture all keep
    // their cells, so one call ashes a whole plain around the set pieces.
    static constexpr unsigned char ash[2] = {PIX_ASH1, PIX_ASH2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            if (!ground_cell_standable(t) || z_furniture_cell(t))
                continue;
            paint(g, x, y, ash[(x * 7 + y * 13) % 2]);
        }
}

void scatter_boulders(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, int modulus)
{
    // Same cell hash and variant pick as the legacy base-tile scatter, but
    // the rock is now DECOR over the existing biome ground (snow stays snow
    // under it). An AIR cell keeps the legacy base-tile boulder instead: a
    // decor rock cannot plug a fall-through hole the way a base tile could
    // (paint_decor hard-fails over air by design). Bases that block weapons
    // or flyers (walls, mid-canopy trees) are skipped outright: eroding such
    // cells into weapon-permeable rock would silently breach authored walls.
    static constexpr unsigned char decor_boulders[4] = {
        DECOR_BOULDER_1, DECOR_BOULDER_2, DECOR_BOULDER_3, DECOR_BOULDER_4};
    static constexpr unsigned char base_boulders[4] = {
        PIX_BOULDER_1, PIX_BOULDER_2, PIX_BOULDER_3, PIX_BOULDER_4};
    auto blocks_weapons_or_flyers = [](unsigned char t) {
        switch (t)
        {
            case PIX_H_WALL1:
            case PIX_WALL2:
            case PIX_WALL3:
            case PIX_WALL4:
            case PIX_WALL5:
            case PIX_WALL_LL:
            case PIX_WALLTOP_H:
            case PIX_WALL_ARROW_GRASS:
            case PIX_WALL_ARROW_FLOOR:
            case PIX_WALL_ARROW_GRASS_DARK:
            case PIX_TREE_M1:
            case PIX_TREE_ML:
            case PIX_TREE_MR:
            case PIX_TREE_MT:
            case PIX_TREE_T1:
                return true;
            default:
                return false;
        }
    };
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 7 + y * 11) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (blocks_weapons_or_flyers(t))
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing — and never PLUG an
                          // air hole into one (a base-boulder plug under a
                          // fall entry would be a blocked landing)
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            if (t == PIX_AIR)
                paint(g, x, y, base_boulders[(x + y) % 4]);
            else
                paint_decor(w, floor, x, y, decor_boulders[(x + y) % 4]);
        }
}

void scatter_litter(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                    int ty1, int modulus)
{
    static constexpr unsigned char litter[4] = {
        PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2, PIX_JAGGED_GROUND_3,
        PIX_JAGGED_GROUND_4};
    scatter_base_tiles(w, floor, tx0, ty0, tx1, ty1, modulus, litter);
}

namespace {

// The exact base tiles each ScatterGround class dresses. Interior tiles
// only: the smoothed edge/corner variants (shorelines, tree eaves, dirt
// aprons) are deliberately excluded so the dressing hugs the middle of a
// zone instead of crowding its seams.
bool scatter_ground_matches(unsigned char t, ScatterGround g)
{
    switch (g)
    {
        case ScatterGround::Grass:
            return t == PIX_GRASS1 || t == PIX_GRASS2 || t == PIX_GRASS3 ||
                   t == PIX_GRASS4;
        case ScatterGround::LightGrass:
            return t == PIX_GRASS_LIGHT_1;
        case ScatterGround::DarkGrass:
            return t == PIX_GRASS_DARK_1 || t == PIX_GRASS_DARK_2 ||
                   t == PIX_GRASS_DARK_3 || t == PIX_GRASS_DARK_4;
        case ScatterGround::Dirt:
            return t == PIX_DIRT_1;
        case ScatterGround::DarkDirt:
            return t == PIX_DIRT_DARK_1;
        case ScatterGround::Snow:
            return t == PIX_SNOW1 || t == PIX_SNOW2;
        case ScatterGround::Marsh:
            return t == PIX_MARSH1 || t == PIX_MARSH2;
        case ScatterGround::Ash:
            return t == PIX_ASH1 || t == PIX_ASH2;
        case ScatterGround::Path:
            return t == PIX_PATH_1 || t == PIX_PATH_2 || t == PIX_PATH_3 ||
                   t == PIX_PATH_4;
        case ScatterGround::Pavement:
            return t == PIX_PAVEMENT1 || t == PIX_PAVEMENT2 ||
                   t == PIX_PAVEMENT3;
        case ScatterGround::Cobble:
            return t == PIX_COBBLE_1 || t == PIX_COBBLE_2 ||
                   t == PIX_COBBLE_3 || t == PIX_COBBLE_4;
    }
    return false;
}

} // namespace

void scatter_decor(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                   int ty1, int modulus, unsigned char decor_id,
                   std::initializer_list<ScatterGround> grounds)
{
    // Ambience only: a blocking id here could mine a route or a fall
    // landing the audits would then have to re-prove — refuse it outright.
    if (decor_id >= DECOR_MAX ||
        kDecorRegistry[decor_id].pass != DecorPassability::None)
    {
        fail(std::format("scatter_decor: id {} is not non-blocking ambience "
                         "decor", decor_id));
        return;
    }
    const PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            // A different cell hash than the boulder/litter scatters so the
            // dressings interleave instead of stacking on the same cells.
            if ((x * 5 + y * 7) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            bool allowed = false;
            for (const ScatterGround ground : grounds)
                if (scatter_ground_matches(t, ground))
                {
                    allowed = true;
                    break;
                }
            if (!allowed)
                continue;
            const PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && x < dec.w && y < dec.h &&
                dec.data[static_cast<std::size_t>(x + y * dec.w)] != DECOR_NONE)
                continue; // hand-placed decor keeps its cell
            if (cell_near_entity(w, floor, x, y, 0))
                continue; // no one spawns standing in the set dressing
            paint_decor(w, floor, x, y, decor_id);
        }
}

void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description,
                      int par_value, int time_bonus_limit)
{
    world.title = title;
    world.par_value = static_cast<short>(par_value);
    world.time_bonus_limit = static_cast<short>(time_bonus_limit);

    // SAVE_ALL scoping: on every SAVE_ALL level the designed protectee —
    // and ONLY that walker — carries npc_flags bit 2 ("protected"). With
    // any flagged NPC placed, the engine watches ONLY flagged walkers, so
    // the other named hands (Kettle above all — protect-OPTIONAL by story
    // bible) are never mission-fail conditions. Centralized here, keyed by
    // the save_all_protectee_name table, so a level builder cannot forget
    // the flag, hand it to anyone else, or set the type on a level the
    // story bible does not allow to be SAVE_ALL.
    const char* protectee = save_all_protectee_name(id);
    if ((world.type & SCEN_TYPE_SAVE_ALL) != 0)
    {
        if (protectee == nullptr)
        {
            fail(std::format("scen{}: SAVE_ALL type on a level with no "
                             "designed protectee (only 4 and 15 may be "
                             "SAVE_ALL)", id));
        }
        else
        {
            int flagged = 0;
            for (auto& uptr : world.oblist)
            {
                walker* ob = uptr.get();
                if (ob == nullptr || ob->query_order() != Order::Living)
                    continue;
                if (ob->stats()->name == protectee)
                {
                    ob->set_save_all_protected(true);
                    ++flagged;
                }
            }
            if (flagged != 1)
                fail(std::format("scen{}: a SAVE_ALL level must carry exactly "
                                 "one '{}' to protect, found {}", id,
                                 protectee, flagged));
        }
    }
    else if (protectee != nullptr)
    {
        fail(std::format("scen{}: designed SAVE_ALL level (protectee '{}') "
                         "does not set SCEN_TYPE_SAVE_ALL", id, protectee));
    }

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", id);
    metadata.generated = true; // provenance mark: this scen is tool output
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
    // exactly the floors the .fss payload flags as present: the writer's
    // predicate is "valid plane with at least one nonzero byte", so an
    // all-zero plane is treated as absent on both sides.
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
    std::printf("longseason_mapgen: built %d '%s' (%d floors)\n", id, title,
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
        << "title:           The Long Season\n"
        << "version:         1\n"
        << "first_level:     1\n"
        << "suggested_power: 0\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    The Brass Kettle Company takes\n"
        << "    what work the year brings: flood\n"
        << "    dikes in spring, two armies in\n"
        << "    summer, mines in autumn, snow\n"
        << "    passes in winter. Every employer\n"
        << "    pays in the same strange warm\n"
        << "    coin, and every road bends toward\n"
        << "    the foundry that struck it.\n"
        << "    A year of wages. Keep the book;\n"
        << "    collect at the gate.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: the brass kettle sigil — a round-bellied kettle with spout,
// iron handle and three steam wisps, one warm coin glowing at its foot, on
// a pure black field. Painted in the warm orange-brown ramp (129 body, 128
// highlight, 130 rim/mouth/coin rim, 133-134 shadow and dark iron), the
// grey ramp (24 ground line, 26-28 steam), and the yellow end of the STATIC
// fire-ramp copy (238-239: brass glint, coin face). No cycled palette bands
// anywhere — nothing in 208-223 (water) or 224-231 (fire); the sigil must
// not shimmer. The static copy 232-239 is safe.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = make_grid(kSize, kSize, 0); // the black field
    auto put = [&](int x, int y, unsigned char c) { paint(icon, x, y, c); };
    auto put_span = [&](int y, int x0, int x1, unsigned char c) {
        for (int x = x0; x <= x1; ++x)
            put(x, y, c);
    };
    // Kettle body (129, warm brass), row spans of the round belly.
    put_span(12, 12, 19, 129);
    put_span(13, 10, 21, 129);
    for (int y = 14; y <= 21; ++y)
        put_span(y, 9, 22, 129);
    put_span(22, 10, 21, 129);
    put_span(23, 11, 20, 129);
    put_span(24, 13, 18, 129);
    // Belly highlight (128) with one specular dot.
    for (int y = 14; y <= 16; ++y)
        put_span(y, 11, 13, 128);
    put(12, 15, 238);
    // Base shadow (133): the bottom rows' outer two pixels each side.
    put_span(23, 11, 12, 133);
    put_span(23, 19, 20, 133);
    put_span(24, 13, 14, 133);
    put_span(24, 17, 18, 133);
    // Rim/collar and the lid knob.
    put_span(11, 12, 19, 130);
    for (int y = 9; y <= 10; ++y)
        put_span(y, 15, 16, 129);
    put(15, 9, 238);
    // Spout, out to the left; the mouth ends bright.
    put(8, 13, 129);
    put(7, 12, 129);
    put(6, 12, 129);
    put(5, 11, 130);
    // Handle arc (134, dark iron), anchored on the shoulders.
    put(11, 8, 134);
    put(20, 8, 134);
    static constexpr int arc[8][2] = {{12, 7}, {13, 6}, {14, 5}, {15, 5},
                                      {16, 5}, {17, 5}, {18, 6}, {19, 7}};
    for (const auto& p : arc)
        put(p[0], p[1], 134);
    // Steam wisps (greys), rising off-center.
    put(14, 3, 26);
    put(17, 2, 27);
    put(15, 1, 28);
    // THE WARM COIN, lower right — the campaign's mystery on the sigil: a
    // 4x4 disc rim (130) around a glowing 2x2 core (238), one glint (239).
    for (int y = 25; y <= 28; ++y)
        for (int x = 24; x <= 27; ++x)
            if (x == 24 || x == 27 || y == 25 || y == 28)
                put(x, y, 130);
    for (int y = 26; y <= 27; ++y)
        put_span(y, 25, 26, 238);
    put(25, 26, 239);
    // Ground line under the kettle (dark grey).
    put_span(25, 10, 21, 24);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// The full designed level graph: ids 1-19 (campaign_meta.md's season table
// and Reckoning tail). All 19 levels are built, so every exit destination
// must exist in the package — an exit naming an absent level fails the
// build. (While the seasons were being authored this was false: exits into
// planned-but-not-yet-built levels only warned.)
constexpr bool kRequireAllDestinationsBuilt = true;

bool is_planned_level(int id)
{
    return id >= 1 && id <= 19;
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

// --- Reachability audit. -----------------------------------------------------
// Multi-floor A* path-state encoding (walker_pathing.cpp's MAKE_STATE).
PathState make_path_state(int x, int y, int floor)
{
    return reinterpret_cast<PathState>(
        static_cast<intptr_t>(floor) * FLOOR_STRIDE +
        static_cast<intptr_t>(y / GRID_SIZE) * MAP_WIDTH + (x / GRID_SIZE));
}

// DELIBERATELY ground-unreachable placements, matched by (level, floor,
// tile). Flyers are already exempt (ghosts hover over water, meres and air
// pits by design); everything ELSE a ground probe cannot reach from the
// lead start marker fails the build unless it is listed here with a reason.
struct ReachabilityException
{
    int id;
    int floor;
    int tx;
    int ty;
};
const std::vector<ReachabilityException>& reachability_exceptions()
{
    static const std::vector<ReachabilityException> exceptions = {
        // (none: every ground living and generator in the shipped package
        //  must be reachable — the kill-all contracts demand it, and the
        //  CAN_EXIT levels promise the player no foe is sealed away.)
    };
    return exceptions;
}

bool reachability_exception_allowed(int id, int floor, int tx, int ty)
{
    for (const ReachabilityException& e : reachability_exceptions())
        if (e.id == id && e.floor == floor && e.tx == tx && e.ty == ty)
            return true;
    return false;
}

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

    // Decor plane audit (BASE + DECOR layering, .fss v11): planes that
    // round-tripped must match their floor grid's dims, carry only known
    // decor ids, and never sit over air / Z-stair / void bases.
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
                const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
                if (d >= DECOR_MAX)
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor id {} out of range",
                                     ex.id, f, tx, ty, d));
                const unsigned char base = g.data[static_cast<std::size_t>(tx + ty * g.w)];
                if (d != DECOR_NONE &&
                    (base == PIX_AIR || base == PIX_ZSTAIR_UP ||
                     base == PIX_ZSTAIR_DOWN || base == PIX_VOID1))
                {
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor {} over air/stair/void",
                                     ex.id, f, tx, ty, d));
                }
            }
    }

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
    int protected_walkers = 0;
    bool protected_is_designed = true;
    const char* designed_protectee = save_all_protectee_name(ex.id);
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->spawn_delay() > 0)
            ++delayed;
        if (ob->specials_disabled())
            ++no_specials;
        // Guard wake-policy audit: an allied (team 0/1) guard exists to hold
        // a post — protectees, garrisons, wardens. If the hold-post bit
        // failed to round-trip, the wake rule would march the garrison off
        // its post at first sight of the enemy. Enemy guards deliberately
        // wake (ambush posts); an authored enemy statue is legal but must be
        // set explicitly.
        if (ob->query_order() == Order::Living &&
            ob->act_type() == ACT_GUARD && ob->team_num() <= 1 &&
            !ob->guard_hold_post())
            fail(std::format("self-check scen{}: allied guard (family {}, "
                             "team {}) at ({}, {}) is missing hold-post",
                             ex.id, static_cast<int>(ob->family()),
                             static_cast<int>(ob->team_num()),
                             ob->xpos() / GRID_SIZE, ob->ypos() / GRID_SIZE));
        if (ob->save_all_protected())
        {
            ++protected_walkers;
            if (designed_protectee == nullptr ||
                ob->query_order() != Order::Living ||
                ob->stats()->name != designed_protectee)
                protected_is_designed = false;
        }
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

    // SAVE_ALL scoping audit: npc_flags bit 2 must round-trip through the
    // package on exactly the designed protectee — one protected Living
    // named by the save_all_protectee_name table on every SAVE_ALL level,
    // zero protected walkers anywhere else. (The engine narrows the
    // SAVE_ALL watch to flagged walkers the moment any is present, so a
    // stray flag would silently rewrite a level's loss condition.)
    const int expected_protected =
        ((world.type & SCEN_TYPE_SAVE_ALL) != 0) ? 1 : 0;
    if (protected_walkers != expected_protected || !protected_is_designed)
        fail(std::format("self-check scen{}: {} protected walkers "
                         "(designed-protectee-only {}), expected {} on a {} "
                         "level", ex.id, protected_walkers,
                         protected_is_designed, expected_protected,
                         expected_protected == 1 ? "SAVE_ALL" : "non-SAVE_ALL"));

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
                if (lo.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_UP && hi.data[static_cast<std::size_t>(i)] == PIX_ZSTAIR_DOWN)
                    ++pairs;
            if (pairs < 1)
                fail(std::format("self-check scen{}: no aligned stair pair on "
                                 "floor boundary {}<->{}", ex.id, f, f + 1));
        }
    }

    // Fall-line audit: any AIR cell a ground walker can actually step into
    // — 8-adjacent to a standable cell of the SAME floor — must land its
    // faller cleanly. Chase the column down through stacked AIR; the
    // landing cell must be standable (base AND decor): never a wall top,
    // water, lava or blocking decor. Falling past floor 0 is a pit death,
    // a designed mechanic, and stays legal. The engine's landing nudge can
    // rescue a blocked landing, but no level may RELY on the nudge.
    //
    // Fall-DEPTH audit (fall damage, docs/z-axis-design.md): the same walk
    // now also measures each designed fall line's depth in stories (entry
    // floor minus landing floor). Report-only for the per-level max and the
    // >= 2-story count (those lines now cost HP — each gets a design
    // ruling: optional-shortcut keeps, mandatory-path reroutes); a line
    // deeper than 4 stories (the 50%-cap knee) is a self-check failure.
    // Generation is untouched: committed .glads stay byte-identical.
    int max_fall_depth = 0;
    int damaging_fall_lines = 0;
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid())
            continue;
        for (int ty = 0; ty < g.h; ++ty)
        {
            for (int tx = 0; tx < g.w; ++tx)
            {
                if (g.data[static_cast<std::size_t>(tx + ty * g.w)] != PIX_AIR)
                    continue;
                bool fall_entry = false;
                for (int dy = -1; dy <= 1 && !fall_entry; ++dy)
                    for (int dx = -1; dx <= 1 && !fall_entry; ++dx)
                        if ((dx != 0 || dy != 0) &&
                            cell_standable(world, f, tx + dx, ty + dy))
                            fall_entry = true;
                if (!fall_entry)
                    continue; // open sky no walker can step into
                int lf = f - 1;
                while (lf > 0 &&
                       world.grid_for_floor(lf).data[static_cast<std::size_t>(tx + ty * world.grid_for_floor(lf).w)] == PIX_AIR)
                    --lf;
                if (world.grid_for_floor(lf).data[static_cast<std::size_t>(tx + ty * world.grid_for_floor(lf).w)] == PIX_AIR)
                    continue; // fell past floor 0: pit death by design
                if (!cell_standable(world, lf, tx, ty))
                {
                    fail(std::format(
                        "self-check scen{}: fall line at tile ({}, {}) floor "
                        "{} lands on impassable ground of floor {} (the level "
                        "must not rely on the engine's landing nudge)",
                        ex.id, tx, ty, f, lf));
                }
                const int depth = f - lf;
                max_fall_depth = std::max(max_fall_depth, depth);
                if (depth >= 2)
                    ++damaging_fall_lines;
                if (depth > 4)
                {
                    fail(std::format(
                        "self-check scen{}: fall line at tile ({}, {}) floor "
                        "{} drops {} stories to floor {} — deeper than the "
                        "4-story fall-damage cap knee (reroute or shallow "
                        "the shaft)", ex.id, tx, ty, f, depth, lf));
                }
            }
        }
    }
    if (world.floor_count() > 1)
    {
        std::printf("longseason_mapgen: scen%d fall-depth: max %d stories, "
                    "%d damaging (>=2-story) fall line(s)\n",
                    ex.id, max_fall_depth, damaging_fall_lines);
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
                g.data[static_cast<std::size_t>(tx + ty * g.w)] == PIX_AIR)
            {
                fail(std::format(
                    "self-check scen{}: ground unit family {} at tile ({}, {}) "
                    "floor {} spawns over air", ex.id,
                    static_cast<int>(ob->family()), tx, ty, ob->floor()));
            }
            // No ground footprint on BlocksGround decor: the scatter keeps
            // entity clearance and hand-placed decor must not pin troops.
            const PixieData& dec = world.decor_for_floor(ob->floor());
            if (dec.valid() && tx >= 0 && ty >= 0 && tx < dec.w &&
                ty < dec.h)
            {
                const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
                if (d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                {
                    fail(std::format(
                        "self-check scen{}: ground unit family {} at tile "
                        "({}, {}) floor {} spawns on blocking decor {}",
                        ex.id, static_cast<int>(ob->family()), tx, ty,
                        ob->floor(), d));
                }
            }
        }
    };
    for (const auto& uptr : world.oblist)
        check_footing(uptr.get());
    for (const auto& uptr : world.fxlist)
        check_footing(uptr.get());

    // Reachability audit: every living and every generator must be
    // A*-reachable from the crew's lead start marker by a ground probe,
    // respecting passability, air holes, lava and Z-stairs. Kill-all
    // contracts demand the player can close with every foe; the allowlist
    // above is the only escape hatch for deliberate exceptions.
    walker* lead = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            lead = ob;
            break;
        }
    }
    if (lead == nullptr || current_game == nullptr ||
        world.myobmap == nullptr)
    {
        fail(std::format("self-check scen{}: reachability audit needs a lead "
                         "start marker, a gameplay context and an obmap",
                         ex.id));
        return;
    }
    GameWorld* const prev_world = current_game->world;
    current_game->world = &world;
    // A ground probe on the lead marker; removed from the obmap so it
    // never self-blocks a solve.
    walker* probe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (probe == nullptr)
    {
        fail(std::format("self-check scen{}: could not seed the reachability "
                         "probe", ex.id));
        current_game->world = prev_world;
        return;
    }
    probe->set_team_num(0);
    probe->set_real_team_num(0);
    probe->set_floor(lead->floor());
    probe->setxy(lead->xpos(), lead->ypos());
    (void)world.myobmap->remove(probe);
    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    const PathState start =
        make_path_state(probe->xpos(), probe->ypos(), probe->floor());
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob == probe)
            continue;
        const Order order = ob->query_order();
        if (order != Order::Living && order != Order::Generator)
            continue;
        if (order == Order::Living &&
            ob->stats()->query_bit_flags(BIT_FLYING))
        {
            continue; // flyers cross lava/water/pits by design
        }
        const int tx = ob->xpos() / GRID_SIZE;
        const int ty = ob->ypos() / GRID_SIZE;
        if (reachability_exception_allowed(ex.id, ob->floor(), tx, ty))
            continue;
        const PathState goal =
            make_path_state(ob->xpos(), ob->ypos(), ob->floor());
        if (goal == start)
            continue;
        std::vector<PathState> path;
        float total_cost = 0.0f;
        pathing->solve_for_point(probe, static_cast<short>(ob->xpos()),
                                 static_cast<short>(ob->ypos()), start, goal,
                                 path, total_cost);
        if (path.empty())
        {
            fail(std::format(
                "self-check scen{}: order {} family {} at tile ({}, {}) "
                "floor {} is unreachable from the lead start marker "
                "(fix the map or add a reachability-allowlist entry)",
                ex.id, static_cast<int>(order),
                static_cast<int>(ob->family()), tx, ty, ob->floor()));
        }
    }
    current_game->world = prev_world;
}

// Kettle's Book (packs/longseason.ledger) must actually register and
// dispatch, not merely ride along in the zip. With the produced campaign
// mounted: the pack script registry carries the ledger pack, the scripted
// picker AND the Base Camp zone registered with the four sim-visible vars,
// every camp composition and the one surviving room fit the content
// budgets (<= 6 lines of <= 38 chars, <= 24 entries, labels <= 24, notes
// <= 20, and the zone's own per-kind widget caps), and one var-injected
// tick of The Long Toll spawns the two Collectors. This catches Lua syntax
// slips, hook-name typos, budget overruns and dispatch wiring at
// generation time.
void self_check_book_script()
{
    constexpr const char* kLedgerPackId = "longseason.ledger";
    bool pack_registered = false;
    for (const og::script::PackScript& ps : og::script::pack_scripts())
        if (ps.pack_id == kLedgerPackId)
            pack_registered = true;
    if (!pack_registered)
    {
        fail("self-check: ledger pack script not registered on mount");
        return;
    }

    if (!og::script::hooks::campaign_picker_registered())
    {
        fail("self-check: Kettle's Book registered no campaign picker");
        return;
    }
    if (!og::script::hooks::campaign_zone_registered())
    {
        fail("self-check: Kettle's Book composed no Base Camp");
        return;
    }
    const std::vector<std::string> expected_vars = {
        "coin_kept", "advance_debt", "provisions", "fair_round"};
    if (og::script::hooks::campaign_registered_vars() != expected_vars)
        fail("self-check: the book's registered vars drifted");

    // Composition budgets across the camp faces the book morphs through.
    struct BookState
    {
        const char* name;
        int cursor;
        int completed_to;
        bool debt;
        bool settled;
        bool coin_waiting;
    };
    const BookState states[] = {
        {"ordinary week", 6, 5, false, false, false},
        {"coin waiting", 6, 5, false, false, true},
        {"debt outstanding", 14, 13, true, false, false},
        {"settlement", 19, 18, true, false, false},
        {"new-season", 19, 19, false, true, false},
    };
    const auto check_entry = [](const og::script::hooks::CampaignPageEntry& e,
                                const char* where) {
        if (e.label.size() > 24)
            fail(std::format("self-check: label over 24 chars in {}: '{}'",
                             where, e.label));
        if (e.note.size() > 20)
            fail(std::format("self-check: note over 20 chars in {}: '{}'",
                             where, e.note));
    };
    for (const BookState& s : states)
    {
        SaveData book;
        book.current_campaign = "longseason";
        book.my_team = 0;
        book.scen_num = static_cast<short>(s.cursor);
        book.m_totalcash[0] = 5000;
        for (int lvl = 1; lvl <= s.completed_to; ++lvl)
            book.add_level_completed("longseason", lvl);
        if (!s.coin_waiting)
        {
            std::int32_t kept = 0;
            for (int lvl = 2; lvl <= std::min(s.completed_to, 18); ++lvl)
                kept += 1 << lvl;
            (void)book.campaign_state_set("longseason", "coin_kept", kept);
        }
        if (s.debt)
            (void)book.campaign_state_set("longseason", "advance_debt", 900);
        if (s.settled)
            (void)book.campaign_state_set("longseason", "settled", 1);
        (void)book.campaign_state_set("longseason", "kettle_asked", 2);
        (void)book.campaign_state_set("longseason", "provisions",
                                      3 + 8 * s.cursor);
        og::script::hooks::install_campaign_providers(
            og::data::make_campaign_providers(book));

        // The camp itself: it must compose, stay inside the widget caps,
        // and keep every row and line inside the content budgets.
        og::script::hooks::CampaignZone zone;
        if (!og::script::hooks::campaign_zone(zone))
        {
            fail(std::format("self-check: camp state '{}' did not compose",
                             s.name));
        }
        else
        {
            if (zone.widgets.size() > 5)
                fail(std::format("self-check: camp state '{}' composes {} "
                                 "widgets (5 is the ceiling)", s.name,
                                 zone.widgets.size()));
            int rows = 0;
            for (const og::script::hooks::CampaignZoneWidget& widget :
                 zone.widgets)
            {
                rows += static_cast<int>(widget.entries.size());
                for (const std::string& line : widget.lines)
                    if (line.size() > 38)
                        fail(std::format("self-check: camp line over 38 "
                                         "chars: '{}'", line));
                for (const og::script::hooks::CampaignPageEntry& entry :
                     widget.entries)
                    check_entry(entry, s.name);
            }
            if (rows > 16)
                fail(std::format("self-check: camp state '{}' composes {} "
                                 "action rows (16 is the cap)", s.name, rows));
        }

        for (const char* page_id : {"stores"})
        {
            og::script::hooks::CampaignPage page;
            if (!og::script::hooks::campaign_picker_page(page_id, page))
            {
                fail(std::format("self-check: book state '{}' page '{}' did "
                                 "not parse", s.name, page_id));
                continue;
            }
            if (page.title.empty())
                fail(std::format("self-check: page '{}' has no title",
                                 page_id));
            if (page.lines.size() > 6)
                fail(std::format("self-check: page '{}' overruns 6 lines "
                                 "({} in state '{}')", page_id,
                                 page.lines.size(), s.name));
            for (const std::string& line : page.lines)
                if (line.size() > 38)
                    fail(std::format("self-check: page '{}' line over 38 "
                                     "chars: '{}'", page_id, line));
            if (page.entries.size() > 24)
                fail(std::format("self-check: page '{}' overruns 24 entries",
                                 page_id));
            for (const og::script::hooks::CampaignPageEntry& entry :
                 page.entries)
            {
                if (entry.label.size() > 24)
                    fail(std::format("self-check: label over 24 chars: '{}'",
                                     entry.label));
                if (entry.note.size() > 20)
                    fail(std::format("self-check: note over 20 chars: '{}'",
                                     entry.note));
            }
        }
        og::script::hooks::clear_campaign_providers();
    }

    // One var-injected tick: the unpaid advance sends the Collectors up
    // The Long Toll's west road.
    LevelRuntimeData level(14, true, &headless_level_data_hooks());
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
        fail("self-check: scen14 failed to load for the book script check");
        current_game = prev;
        return;
    }
    const auto count_t2_livings = [&level] {
        int count = 0;
        for (const auto& uptr : level.world().oblist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Living &&
                ob->team_num() == 2)
                ++count;
        }
        return count;
    };
    const int t2_before = count_t2_livings();
    level.world().campaign_vars.emplace_back("advance_debt", 900);
    level.world().tick();
    if (count_t2_livings() != t2_before + 2)
        fail(std::format("self-check: advance_debt did not field the two "
                         "Collectors ({} -> {})", t2_before,
                         count_t2_livings()));

    for (const og::script::ScriptError& err :
         level.world().scripts().host().errors())
        fail(std::format("self-check: script error at {}: {}", err.where,
                         err.message));

    current_game = prev;
}

} // namespace
} // namespace longseason

int main(int argc, char* argv[])
{
    using namespace longseason;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/longseason";
    const fs::path out_abs = fs::absolute(out_tree);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("longseason_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "gladiator")
    {
        std::fprintf(stderr, "longseason_mapgen: ERROR: stock campaign not "
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
    // Kettle's Book rides inside the archive; staging it here lets the
    // self-check audit the package players actually get (the committed
    // pack source itself is hand-authored and preserved by the export).
    if (!og::toolexport::stage_pack_tree(
            "campaigns/longseason/packs/"
            "longseason.ledger",
            user + "temp/", "longseason.ledger"))
        fail("failed to stage the embedded ledger pack");

    const LevelDataHooks& hooks = headless_level_data_hooks();
    build_spring(hooks);
    build_summer(hooks);
    build_autumn(hooks);
    build_winter(hooks);
    build_reckoning(hooks);

    std::vector<ExpectedLevel> expectations;
    for (auto rows : {spring_expectations(), summer_expectations(),
                      autumn_expectations(), winter_expectations(),
                      reckoning_expectations()})
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

    const std::string glad_path = user + "campaigns/longseason.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) != ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("longseason") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
            self_check_book_script();
            (void)unmount_campaign_package_with_error("longseason");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        if (!og::toolexport::export_campaign_tree(user + "temp/", out_abs))
            fail(std::format("failed to export the campaign tree to {}",
                             out_abs.string()));
        else
        {
            std::printf("longseason_mapgen: wrote %s\n", out_abs.c_str());
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
        std::fprintf(stderr, "longseason_mapgen: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
