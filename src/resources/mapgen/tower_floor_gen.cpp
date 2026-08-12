/* Tower Climb floor generator — see tower_floor_gen.h and the tower-triple
 * spec §5.6 for the recipe this file implements:
 *
 *   (1) floor_seed + template pick by band weights; (2) init_world, band
 *   base, template carve; (3) stair pairs x2 per z-boundary (opposite
 *   quadrants) + smooth_world (scratch-world rng); (4) place_start x10 in
 *   the entry zone (lead marker FIRST); (5) rooms sorted by distance from
 *   entry, squads room-by-room (room guards ACT_GUARD, 25% roamers);
 *   (6) generators in a rear room, elites/boss in the far room / top story,
 *   turrets where the band allows; (7) treasure economy; (8) scatters;
 *   (9) exit on the top story, far quadrant, destination id+1, type =
 *   SCEN_TYPE_TOWER (| SCEN_TYPE_CAN_EXIT on open floors); (10) title
 *   "Floor {N}", band-voice briefing, par/limit; (11) audits with salted
 *   reroll x3 then the T0 fallback; (12) write to user_path (the caller).
 *
 * Layout disciplines that keep the audits green BY CONSTRUCTION:
 *  - multi-story full-ground templates repeat the SAME skeleton on every
 *    story, so stair pads always land in carved space on both floors;
 *  - stair/entry/exit pads are force-painted ground before smoothing;
 *  - only the spire template (T4) authors PIX_AIR, as shrinking platforms
 *    whose every fall line drops exactly one story onto the platform below;
 *    a post-smooth repair pass re-grounds any landing the dressing broke;
 *  - water/lava pools sit at room centers (never across doors), moats
 *    always carry two bridges, wall clusters and columns are isolated
 *    single obstacles on a spaced lattice.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/mapgen/tower_floor_gen.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/tower_constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_file_io.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace og::tower {

namespace {

using og::mapgen::cell_standable;
using og::mapgen::paint;
using og::mapgen::paint_rect;
using og::mapgen::place;
using og::mapgen::place_exit;
using og::mapgen::place_generator;
using og::mapgen::place_living;
using og::mapgen::place_start;
using og::mapgen::scatter_boulders;
using og::mapgen::scatter_decor;
using og::mapgen::scatter_litter;
using og::mapgen::ScatterGround;
using og::mapgen::stair_pair;

// --- Deterministic streams. --------------------------------------------------

std::uint64_t splitmix64(std::uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// A local splitmix64 counter stream (spec §5.4): every non-positional roll a
// build makes comes from here, in recipe order, so identical (seed) always
// replays identical picks. Never touches world/session/libc RNG.
class SeedStream
{
public:
    explicit SeedStream(std::uint32_t seed, std::uint32_t salt)
        : base_((static_cast<std::uint64_t>(seed) << 32) ^
                (static_cast<std::uint64_t>(salt) * 0x9E3779B97F4A7C15ull))
    {
    }

    std::uint32_t next()
    {
        ++counter_;
        return static_cast<std::uint32_t>(splitmix64(base_ + counter_));
    }

    std::uint32_t next(std::uint32_t bound)
    {
        return (bound == 0) ? 0u : next() % bound;
    }

    // Inclusive integer range.
    int range(int lo, int hi)
    {
        if (hi <= lo)
            return lo;
        return lo + static_cast<int>(next(static_cast<std::uint32_t>(hi - lo + 1)));
    }

    bool chance_percent(int p)
    {
        return next(100u) < static_cast<std::uint32_t>(p);
    }

private:
    std::uint64_t base_;
    std::uint64_t counter_ = 0;
};

// --- Entity-service wiring (mirrors wire_world_loader / the WP-4 tests). -----

loader& tower_gen_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_entity_services(GameWorld& w)
{
    loader* game_loader = &tower_gen_loader();
    w.entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    w.entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                         entity.family());
    };
    w.entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

// --- Band tables (§5.6; data, retunable without structural change). ----------

enum TemplateId
{
    T0_ARENA = 0,
    T1_COURTYARD,
    T2_HALLS,
    T3_WARREN,
    T4_SPIRE,
    T5_MOAT,
    T6_VAULT,
    kTemplateCount
};

struct FoeMix
{
    int family;
    int percent;
    int min_floor; // 0 = always; below it the share folds into entry 0
};

struct GenSpec
{
    int family;    // generator family (FAMILY_TENT/TOWER/BONES/TREEHOUSE)
    int count_min; // stream-rolled [min..max]
    int count_max;
    int min_floor; // 0 = always
};

struct BandSpec
{
    const char* name;
    int tw, th;
    int stories_min, stories_max; // Bailey boss floor promotes 1 -> 2
    unsigned char base_tile;
    std::array<int, kTemplateCount> tmpl_weights;
    std::array<FoeMix, 6> mix; // family -1 terminates
    const char* boss_name;
    int boss_family;
    std::array<GenSpec, 2> generators; // family -1 terminates
    int turrets_max;                   // FAMILY_TOWER1 posts near stairs
    int potion_family;                 // the band-schedule utility potion
    const char* briefing1;
    const char* briefing2; // nullptr = single-line briefing
};

// Briefing line sanity bound. The SCENARIO INFORMATION dialog word-wraps at
// render time (issue #152), so this is no longer the 33-char display budget
// — just a guard against a runaway generated line (the .glad serializer
// caps a line at 255 bytes).
constexpr std::size_t kBriefingLineBudget = 120;
constexpr const char* kOpenStairsLine = "The stairs stand open.";

constexpr std::array<BandSpec, 6> kBands = {{
    // 1-5 The Bailey
    {"The Bailey", 34, 34, 1, 1, PIX_GRASS1,
     {20, 60, 20, 0, 0, 0, 0},
     {{{FAMILY_SOLDIER, 50, 0}, {FAMILY_ELF, 25, 0}, {FAMILY_ARCHER, 25, 0},
       {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}}},
     "Gatewarden", FAMILY_BIG_ORC,
     {{{FAMILY_TREEHOUSE, 1, 1, 3}, {-1, 0, 0, 0}}},
     0, FAMILY_SPEED_POTION,
     "The Bailey tests new blades.", "Climb while breath holds."},
    // 6-10 The Barracks
    {"The Barracks", 36, 30, 1, 2, PIX_PAVEMENT1,
     {20, 0, 60, 0, 0, 20, 0},
     {{{FAMILY_SOLDIER, 40, 0}, {FAMILY_ARCHER, 25, 0}, {FAMILY_THIEF, 15, 0},
       {FAMILY_CLERIC, 10, 0}, {FAMILY_BIG_ORC, 10, 0}, {-1, 0, 0}}},
     "Drillmaster", FAMILY_BARBARIAN,
     {{{-1, 0, 0, 0}, {-1, 0, 0, 0}}},
     0, FAMILY_MAGIC_POTION,
     "Drilled steel bars the way.", nullptr},
    // 11-15 The Undercroft
    {"The Undercroft", 40, 36, 2, 2, PIX_DIRT_DARK_1,
     {0, 0, 30, 50, 0, 0, 20},
     {{{FAMILY_SKELETON, 40, 0}, {FAMILY_GHOST, 20, 0}, {FAMILY_SLIME, 15, 0},
       {FAMILY_CLERIC, 15, 0}, {FAMILY_THIEF, 10, 0}, {-1, 0, 0}}},
     "Cryptlord", FAMILY_GIANT_SKELETON,
     {{{FAMILY_BONES, 1, 1, 0}, {FAMILY_TENT, 1, 1, 0}}},
     0, FAMILY_MAGIC_POTION,
     "The dead keep this cellar.", nullptr},
    // 16-20 The Mage Spires (NO flight potion — §3.8 fall-hazard integrity)
    {"The Mage Spires", 30, 30, 3, 3, PIX_FLOOR1,
     {0, 0, 30, 0, 70, 0, 0},
     {{{FAMILY_MAGE, 35, 0}, {FAMILY_FIREELEMENTAL, 20, 0},
       {FAMILY_FAERIE, 20, 0}, {FAMILY_ARCHER, 15, 0},
       {FAMILY_ARCHMAGE, 10, 18}, {-1, 0, 0}}},
     "Spirelord", FAMILY_ARCHMAGE,
     {{{FAMILY_TOWER, 1, 2, 0}, {-1, 0, 0, 0}}},
     2, FAMILY_SPEED_POTION,
     "Wards hum between the shafts.", "Mind the open air."},
    // 21-25 The Furnace
    {"The Furnace", 38, 34, 1, 2, PIX_ASH1,
     {25, 0, 0, 25, 0, 50, 0},
     {{{FAMILY_ORC, 35, 0}, {FAMILY_BIG_ORC, 25, 0},
       {FAMILY_FIREELEMENTAL, 25, 0}, {FAMILY_GOLEM, 10, 23},
       {FAMILY_DRUID, 5, 0}, {-1, 0, 0}}},
     "Forgeheart", FAMILY_GOLEM,
     {{{FAMILY_TENT, 0, 1, 0}, {-1, 0, 0, 0}}},
     2, FAMILY_INVIS_POTION,
     "The forge floor still burns.", nullptr},
    // 26-30 The Summit (snow fill => Snow weather, deliberate)
    {"The Summit", 40, 40, 2, 2, PIX_SNOW1,
     {0, 40, 30, 0, 30, 0, 0},
     {{{FAMILY_BARBARIAN, 30, 0}, {FAMILY_DRUID, 20, 0}, {FAMILY_ELF, 15, 0},
       {FAMILY_GIANT_SKELETON, 15, 0}, {FAMILY_ARCHMAGE, 10, 0},
       {FAMILY_GHOST, 10, 0}}},
     "Stormcrown", FAMILY_ARCHMAGE,
     {{{FAMILY_TREEHOUSE, 1, 1, 0}, {-1, 0, 0, 0}}},
     0, FAMILY_FLIGHT_POTION,
     "Snow crowns the last rampart.", nullptr},
}};

// Foe team assignments. Foes sit on team 2 (NOT team 1: the builder lib's
// hold-post rule treats teams <= 1 as allied garrisons, and tower guards
// must be wake-on-sight ambush posts — the tower has no allies). The rival
// raider variant uses team 3: NPCs without myguy are friendly only to their
// own team, so raiders fight both sides.
constexpr int kFoeTeam = 2;
constexpr int kRaiderTeam = 3;

// --- Layout bookkeeping. -------------------------------------------------------

struct Rect
{
    int x0, y0, x1, y1; // inclusive tiles
    [[nodiscard]] int cx() const { return (x0 + x1) / 2; }
    [[nodiscard]] int cy() const { return (y0 + y1) / 2; }
    [[nodiscard]] int area() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
};

struct Room
{
    int story;
    Rect r;
};

struct StairCell
{
    int lower_story;
    int tx, ty;
};

struct Layout
{
    std::vector<Room> rooms;
    int vault_room = -1; // index into rooms (T6), else -1
};

struct BuildPlan
{
    int floor = 0;
    std::uint32_t seed = 0;
    const BandSpec* band = nullptr;
    int band_idx = 0;
    int lap = 0;
    int stories = 1;
    int tw = 0, th = 0;
    TemplateId tmpl = T0_ARENA;
    bool open_stairs = false;
    bool ambush_posture = false;
    bool rival_raiders = false;
    Rect entry{};
    Rect exit_pad{};
    std::vector<StairCell> stairs;
};

bool near_any_stair(const BuildPlan& plan, int tx, int ty, int margin)
{
    for (const StairCell& s : plan.stairs)
        if (std::abs(tx - s.tx) <= margin && std::abs(ty - s.ty) <= margin)
            return true;
    return false;
}

// A 2x2-standable spot inside `zone` on `story`, clear of stairs and other
// entities. Hashed tries off the stream; false after 48 misses (the caller
// simply places fewer — audits stay authoritative).
bool find_spot(GameWorld& w, const BuildPlan& plan, SeedStream& s, int story,
               const Rect& zone, int& out_tx, int& out_ty)
{
    const int zw = zone.x1 - zone.x0 + 1;
    const int zh = zone.y1 - zone.y0 + 1;
    if (zw < 2 || zh < 2)
        return false;
    for (int tries = 0; tries < 48; ++tries)
    {
        const int tx = zone.x0 + s.range(0, zw - 2);
        const int ty = zone.y0 + s.range(0, zh - 2);
        if (!cell_standable(w, story, tx, ty) ||
            !cell_standable(w, story, tx + 1, ty) ||
            !cell_standable(w, story, tx, ty + 1) ||
            !cell_standable(w, story, tx + 1, ty + 1))
            continue;
        if (near_any_stair(plan, tx, ty, 2) || near_any_stair(plan, tx + 1, ty + 1, 2))
            continue;
        if (og::mapgen::cell_near_entity(w, story, tx, ty, 0) ||
            og::mapgen::cell_near_entity(w, story, tx + 1, ty + 1, 0))
            continue;
        out_tx = tx;
        out_ty = ty;
        return true;
    }
    return false;
}

// True when the placed entity's REAL footprint, expanded by one tile, covers
// any stair cell — i.e. it could sit on a stair-cross arrival cell the stair
// audit protects (big walkers overhang the 2x2 estimate find_spot uses).
bool overlaps_stair_cross(const BuildPlan& plan, const walker* ob)
{
    const int x0 = ob->xpos() / GRID_SIZE - 1;
    const int y0 = ob->ypos() / GRID_SIZE - 1;
    const int x1 = (ob->xpos() + ob->sizex() - 1) / GRID_SIZE + 1;
    const int y1 = (ob->ypos() + ob->sizey() - 1) / GRID_SIZE + 1;
    for (const StairCell& sc : plan.stairs)
        if (sc.tx >= x0 && sc.tx <= x1 && sc.ty >= y0 && sc.ty <= y1)
            return true;
    return false;
}

// Validated placement: hashed spots until the ENGINE accepts the entity's
// real footprint (query_grid_passable — the exact rule audit_footing
// re-checks) and the footprint stays off every stair cross. nullptr after
// the tries run out: the caller places fewer and the audits stay green.
walker* place_validated(GameWorld& w, const BuildPlan& plan, SeedStream& s,
                        Order order, int family, int team, int story,
                        const Rect& zone, int level, bool guard = false)
{
    for (int tries = 0; tries < 6; ++tries)
    {
        int tx = 0;
        int ty = 0;
        if (!find_spot(w, plan, s, story, zone, tx, ty))
            return nullptr;
        walker* ob = nullptr;
        if (order == Order::Living)
            // Tower posts are ambush guards, never hold-post sentries:
            // explicit false preserves the pre-hold_post-parameter output
            // byte-identically (every tower placement is team 2/3, which
            // the old team<=1 inference never stamped either).
            ob = place_living(w, family, team, story, tx, ty, level, guard,
                              /*hold_post=*/false);
        else if (order == Order::Generator)
            ob = place_generator(w, family, team, story, tx, ty, level);
        else
        {
            ob = place(w, order, family, team, story, tx, ty);
            if (ob != nullptr)
                ob->stats()->set_level(level);
        }
        if (ob == nullptr)
            return nullptr;
        if (w.query_grid_passable(static_cast<float>(ob->xpos()),
                                  static_cast<float>(ob->ypos()), ob,
                                  story) &&
            !overlaps_stair_cross(plan, ob))
            return ob;
        (void)w.remove_ob(ob);
    }
    return nullptr;
}

// --- Carve templates. -----------------------------------------------------------
// Every template starts from stories of solid band ground with a 1-tile wall
// border (T4 upper stories: AIR outside the platform instead) and returns the
// carved rooms.

void border_walls(GameWorld& w, int story)
{
    PixieData& g = w.grid_for_floor(story);
    paint_rect(g, 0, 0, g.w - 1, 0, PIX_H_WALL1);
    paint_rect(g, 0, g.h - 1, g.w - 1, g.h - 1, PIX_H_WALL1);
    paint_rect(g, 0, 0, 0, g.h - 1, PIX_H_WALL1);
    paint_rect(g, g.w - 1, 0, g.w - 1, g.h - 1, PIX_H_WALL1);
}

// Quadrant zones of a story interior (the roomless templates' squad zones).
void quadrant_rooms(Layout& lay, int story, int tw, int th)
{
    const int mx = tw / 2;
    const int my = th / 2;
    lay.rooms.push_back({story, {2, 2, mx - 1, my - 1}});
    lay.rooms.push_back({story, {mx + 1, 2, tw - 3, my - 1}});
    lay.rooms.push_back({story, {2, my + 1, mx - 1, th - 3}});
    lay.rooms.push_back({story, {mx + 1, my + 1, tw - 3, th - 3}});
}

// T0/T1: open halls / courtyards — quadrant zones, plus T1's wall clusters,
// paths and lone trees on a spaced lattice (isolated obstacles can't seal).
void carve_open(GameWorld& w, const BuildPlan& plan, Layout& lay,
                SeedStream& s, bool courtyard)
{
    for (int f = 0; f < plan.stories; ++f)
    {
        border_walls(w, f);
        quadrant_rooms(lay, f, plan.tw, plan.th);
        if (!courtyard)
            continue;
        PixieData& g = w.grid_for_floor(f);
        // Crossing paths (cosmetic, standable).
        paint_rect(g, 2, plan.th / 2, plan.tw - 3, plan.th / 2, PIX_PATH_1);
        paint_rect(g, plan.tw / 2, 2, plan.tw / 2, plan.th - 3, PIX_PATH_1);
        // Wall clusters + lone trees on a 6-tile lattice, never on paths.
        for (int ly = 5; ly < plan.th - 6; ly += 6)
            for (int lx = 5; lx < plan.tw - 6; lx += 6)
            {
                if (std::abs(lx - plan.tw / 2) <= 2 ||
                    std::abs(ly - plan.th / 2) <= 2)
                    continue;
                const std::uint32_t roll = s.next(100u);
                if (roll < 25)
                    paint_rect(g, lx, ly, lx + 1, ly + 1, PIX_H_WALL1);
                else if (roll < 45)
                    paint(g, lx, ly, PIX_TREE_M1);
            }
    }
}

// T2/T6: a wall grid of rooms with 2-tile doors punched in EVERY shared
// edge (connected by construction). Multi-story: the SAME skeleton on every
// story so stair pads always land in carved space on both floors.
void carve_halls(GameWorld& w, const BuildPlan& plan, Layout& lay,
                 SeedStream& s, bool vault)
{
    const int cols = s.range(2, 3);
    const int rows = s.range(2, 3);
    // Door offsets are picked ONCE and reused per story (same skeleton).
    std::vector<int> vdoor((static_cast<std::size_t>(cols) - 1) * static_cast<std::size_t>(rows));
    std::vector<int> hdoor(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows - 1));
    for (int& d : vdoor)
        d = s.range(2, 100); // scaled into the cell span below
    for (int& d : hdoor)
        d = s.range(2, 100);

    const int iw = plan.tw - 2; // interior span
    const int ih = plan.th - 2;
    auto cell_x0 = [&](int c) { return 1 + (iw * c) / cols; };
    auto cell_y0 = [&](int r) { return 1 + (ih * r) / rows; };

    for (int f = 0; f < plan.stories; ++f)
    {
        PixieData& g = w.grid_for_floor(f);
        border_walls(w, f);
        // Internal wall lines.
        for (int c = 1; c < cols; ++c)
            paint_rect(g, cell_x0(c), 1, cell_x0(c), plan.th - 2, PIX_H_WALL1);
        for (int r = 1; r < rows; ++r)
            paint_rect(g, 1, cell_y0(r), plan.tw - 2, cell_y0(r), PIX_H_WALL1);
        // Doors: 2 tiles wide, one per shared edge, off the wall crossings.
        for (int r = 0; r < rows; ++r)
            for (int c = 1; c < cols; ++c)
            {
                const int y0 = cell_y0(r) + 1;
                const int y1 = (r + 1 < rows ? cell_y0(r + 1) : plan.th - 1) - 2;
                const int span = std::max(1, y1 - y0 - 1);
                const int off =
                    vdoor[static_cast<std::size_t>((c - 1) * rows + r)] % span;
                paint_rect(g, cell_x0(c), y0 + off, cell_x0(c), y0 + off + 1,
                           plan.band->base_tile);
            }
        for (int r = 1; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const int x0 = cell_x0(c) + 1;
                const int x1 = (c + 1 < cols ? cell_x0(c + 1) : plan.tw - 1) - 2;
                const int span = std::max(1, x1 - x0 - 1);
                const int off =
                    hdoor[static_cast<std::size_t>((r - 1) * cols + c)] % span;
                paint_rect(g, x0 + off, cell_y0(r), x0 + off + 1, cell_y0(r),
                           plan.band->base_tile);
            }
        // Rooms = the cells, inset from their walls.
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const int x0 = cell_x0(c) + 1;
                const int y0 = cell_y0(r) + 1;
                const int x1 = (c + 1 < cols ? cell_x0(c + 1) : plan.tw - 1) - 1;
                const int y1 = (r + 1 < rows ? cell_y0(r + 1) : plan.th - 1) - 1;
                lay.rooms.push_back({f, {x0, y0, x1, y1}});
            }
    }
    if (vault)
        lay.vault_room = static_cast<int>(lay.rooms.size()) - 1;
}

// T3: warren — the story is solid wall mass; chambers at fixed anchors are
// opened and joined by 2-wide L-corridors whose elbows jitter off the
// stream. Same skeleton on every story.
void carve_warren(GameWorld& w, const BuildPlan& plan, Layout& lay,
                  SeedStream& s)
{
    struct Anchor
    {
        int x, y;
    };
    const std::array<Anchor, 5> anchors = {{
        {plan.tw / 5, plan.th - plan.th / 5}, // entry-side
        {plan.tw / 5, plan.th / 5},
        {plan.tw / 2, plan.th / 2},
        {plan.tw - plan.tw / 5, plan.th - plan.th / 5},
        {plan.tw - plan.tw / 5, plan.th / 5}, // exit-side
    }};
    // Chamber half-sizes + corridor elbows picked once, reused per story.
    std::array<int, 5> half{};
    for (int i = 0; i < 5; ++i)
        half[static_cast<std::size_t>(i)] = s.range(3, 5);
    std::array<int, 4> elbow{};
    for (int i = 0; i < 4; ++i)
        elbow[static_cast<std::size_t>(i)] = s.range(-2, 2);

    for (int f = 0; f < plan.stories; ++f)
    {
        PixieData& g = w.grid_for_floor(f);
        paint_rect(g, 0, 0, plan.tw - 1, plan.th - 1, PIX_H_WALL1);
        auto open_rect = [&](int x0, int y0, int x1, int y1) {
            paint_rect(g, std::max(1, x0), std::max(1, y0),
                       std::min(plan.tw - 2, x1), std::min(plan.th - 2, y1),
                       plan.band->base_tile);
        };
        for (int i = 0; i < 5; ++i)
        {
            const Anchor& a = anchors[static_cast<std::size_t>(i)];
            const int h = half[static_cast<std::size_t>(i)];
            open_rect(a.x - h, a.y - h, a.x + h, a.y + h);
            lay.rooms.push_back(
                {f, {std::max(1, a.x - h), std::max(1, a.y - h),
                     std::min(plan.tw - 2, a.x + h),
                     std::min(plan.th - 2, a.y + h)}});
        }
        for (int i = 0; i + 1 < 5; ++i)
        {
            const Anchor& a = anchors[static_cast<std::size_t>(i)];
            const Anchor& b = anchors[static_cast<std::size_t>(i + 1)];
            const int ex = (a.x + b.x) / 2 + elbow[static_cast<std::size_t>(i)];
            // 2-wide L: a -> (ex, a.y) -> (ex, b.y) -> b.
            open_rect(std::min(a.x, ex), a.y, std::max(a.x, ex) + 1, a.y + 1);
            open_rect(ex, std::min(a.y, b.y), ex + 1, std::max(a.y, b.y) + 1);
            open_rect(std::min(ex, b.x), b.y, std::max(ex, b.x) + 1, b.y + 1);
        }
    }
}

// T4: spire — story 0 a walled hall; each story above is a centered platform
// shrinking by an inset, AIR everywhere beyond it, so every walk-off drops
// exactly one story onto the platform below. Band 4 punches an AIR ring
// (shaft) inside the platform edge with two solid bridges.
void carve_spire(GameWorld& w, const BuildPlan& plan, Layout& lay,
                 SeedStream& s, bool air_rings)
{
    // Shrink per story, capped so the TOP platform keeps >= 16 tiles of
    // span: its core must still seat the exit pad, both stair pads of the
    // last boundary, the boss squad and their mutual clearances.
    int inset_step =
        std::min(plan.tw, plan.th) / (2 * std::max(1, plan.stories));
    if (plan.stories > 1)
        inset_step = std::min(
            inset_step,
            (std::min(plan.tw, plan.th) - 16) / (2 * (plan.stories - 1)));
    inset_step = std::max(3, inset_step);
    border_walls(w, 0);
    quadrant_rooms(lay, 0, plan.tw, plan.th);
    for (int f = 1; f < plan.stories; ++f)
    {
        PixieData& g = w.grid_for_floor(f);
        paint_rect(g, 0, 0, plan.tw - 1, plan.th - 1, PIX_AIR);
        const int in = inset_step * f;
        const Rect plat = {in, in, plan.tw - 1 - in, plan.th - 1 - in};
        if (plat.x1 - plat.x0 < 7 || plat.y1 - plat.y0 < 7)
        {
            // Degenerate platform (tiny grid): keep the story solid instead.
            paint_rect(g, 1, 1, plan.tw - 2, plan.th - 2,
                       plan.band->base_tile);
            lay.rooms.push_back({f, {2, 2, plan.tw - 3, plan.th - 3}});
            continue;
        }
        paint_rect(g, plat.x0, plat.y0, plat.x1, plat.y1,
                   plan.band->base_tile);
        if (air_rings)
        {
            // The shaft ring, 1 tile wide, 2 tiles inside the platform edge,
            // with two 3-tile bridges (N + S) left solid.
            const Rect ring = {plat.x0 + 2, plat.y0 + 2, plat.x1 - 2,
                               plat.y1 - 2};
            paint_rect(g, ring.x0, ring.y0, ring.x1, ring.y0, PIX_AIR);
            paint_rect(g, ring.x0, ring.y1, ring.x1, ring.y1, PIX_AIR);
            paint_rect(g, ring.x0, ring.y0, ring.x0, ring.y1, PIX_AIR);
            paint_rect(g, ring.x1, ring.y0, ring.x1, ring.y1, PIX_AIR);
            const int bx = ring.cx() + s.range(-2, 2);
            paint_rect(g, bx - 1, ring.y0, bx + 1, ring.y0,
                       plan.band->base_tile);
            paint_rect(g, bx - 1, ring.y1, bx + 1, ring.y1,
                       plan.band->base_tile);
        }
        // The platform core (inside any ring) is the story's squad zone.
        lay.rooms.push_back(
            {f, {plat.x0 + 3, plat.y0 + 3, plat.x1 - 3, plat.y1 - 3}});
    }
}

// T5: moat — a lava (or water) ring around the center island with two
// 2-wide bridges (W + E). Upper story, if any: a plain hall.
void carve_moat(GameWorld& w, const BuildPlan& plan, Layout& lay,
                SeedStream& s)
{
    border_walls(w, 0);
    PixieData& g = w.grid_for_floor(0);
    const double cx = plan.tw / 2.0;
    const double cy = plan.th / 2.0;
    const double r0 = std::min(plan.tw, plan.th) / 4.0;
    const unsigned char liquid =
        (plan.band->base_tile == PIX_ASH1) ? PIX_LAVA1 : PIX_WATER1;
    og::mapgen::paint_ring(g, cx, cy, r0, r0 + 2.0, liquid);
    const int by = plan.th / 2 + s.range(-1, 1);
    paint_rect(g, 1, by - 1, plan.tw - 2, by, PIX_PAVEMENT1); // W-E causeway
    const int icx = plan.tw / 2;
    const int icy = plan.th / 2;
    const int ir = static_cast<int>(r0) - 1;
    lay.rooms.push_back(
        {0, {icx - ir, icy - ir, icx + ir, icy + ir}}); // the island
    const int orr = static_cast<int>(r0) + 3;
    lay.rooms.push_back({0, {2, 2, plan.tw - 3, icy - orr}});
    lay.rooms.push_back({0, {2, icy + orr, plan.tw - 3, plan.th - 3}});
    for (int f = 1; f < plan.stories; ++f)
    {
        border_walls(w, f);
        quadrant_rooms(lay, f, plan.tw, plan.th);
    }
}

// Band dressing after the carve: water/lava pools at room centers (never
// across doors), columns in the spires. Pool rooms are hash-picked.
void dress_band(GameWorld& w, const BuildPlan& plan, Layout& lay,
                SeedStream& s)
{
    if (plan.band_idx == 2 || plan.band_idx == 4)
    {
        const unsigned char liquid =
            (plan.band_idx == 4) ? PIX_LAVA1 : PIX_WATER1;
        for (const Room& room : lay.rooms)
        {
            if (room.r.x1 - room.r.x0 < 8 || room.r.y1 - room.r.y0 < 8)
                continue;
            if (!s.chance_percent(40))
                continue;
            const int px = room.r.cx();
            const int py = room.r.cy();
            paint_rect(w.grid_for_floor(room.story), px - 1, py - 1, px + 1,
                       py, liquid);
        }
    }
    if (plan.band_idx == 3)
    {
        // Columns on a spaced lattice inside each squad zone.
        for (const Room& room : lay.rooms)
            for (int ly = room.r.y0 + 2; ly + 2 <= room.r.y1; ly += 4)
                for (int lx = room.r.x0 + 2; lx + 2 <= room.r.x1; lx += 4)
                    if (s.chance_percent(30))
                        paint(w.grid_for_floor(room.story), lx, ly,
                              PIX_COLUMN1);
    }
    if (plan.band_idx == 1)
    {
        // The officer room: carpet the far room's core.
        if (!lay.rooms.empty())
        {
            const Room& room = lay.rooms.back();
            if (room.r.x1 - room.r.x0 >= 6 && room.r.y1 - room.r.y0 >= 6)
                paint_rect(w.grid_for_floor(room.story), room.r.cx() - 2,
                           room.r.cy() - 2, room.r.cx() + 2, room.r.cy() + 2,
                           PIX_CARPET_M);
        }
    }
}

// --- Pads, stairs, fall repair. -------------------------------------------------

void force_ground_pad(GameWorld& w, int story, const Rect& r,
                      unsigned char tile)
{
    paint_rect(w.grid_for_floor(story), r.x0, r.y0, r.x1, r.y1, tile);
}

// Stair pads: pick the room on the UPPER story nearest the ideal quadrant
// point, stamp 3x3 ground on both stories, stair_pair. Multi-story templates
// repeat their skeleton per story, so the stamped pad is in carved space on
// both floors by construction.
void place_stairs(GameWorld& w, BuildPlan& plan, const Layout& lay)
{
    for (int b = 0; b + 1 < plan.stories; ++b)
    {
        const std::array<std::pair<int, int>, 2> ideals = {{
            {plan.tw / 4, plan.th / 4},
            {(3 * plan.tw) / 4, (3 * plan.th) / 4},
        }};
        for (const auto& [ix, iy] : ideals)
        {
            const Room* best = nullptr;
            int best_d = 0;
            for (const Room& room : lay.rooms)
            {
                if (room.story != b + 1)
                    continue;
                const int dx = room.r.cx() - ix;
                const int dy = room.r.cy() - iy;
                const int d = dx * dx + dy * dy;
                if (best == nullptr || d < best_d)
                {
                    best = &room;
                    best_d = d;
                }
            }
            if (best == nullptr)
                continue;
            // Candidate anchors, ALL clamped inside the chosen room (a pad
            // outside it could float in a spire story's open air): the
            // ideal-biased point first, then the room corners, so the two
            // pads of a boundary land apart even in a one-room story.
            const Rect& rr = best->r;
            auto clamp_room_x = [&](int x) {
                return std::clamp(x, std::max(2, rr.x0 + 1),
                                  std::min(plan.tw - 3, rr.x1 - 1));
            };
            auto clamp_room_y = [&](int y) {
                return std::clamp(y, std::max(2, rr.y0 + 1),
                                  std::min(plan.th - 3, rr.y1 - 1));
            };
            const std::array<std::pair<int, int>, 5> candidates = {{
                {clamp_room_x((rr.cx() * 2 + ix) / 3),
                 clamp_room_y((rr.cy() * 2 + iy) / 3)},
                {clamp_room_x(rr.x0 + 1), clamp_room_y(rr.y0 + 1)},
                {clamp_room_x(rr.x1 - 1), clamp_room_y(rr.y1 - 1)},
                {clamp_room_x(rr.x0 + 1), clamp_room_y(rr.y1 - 1)},
                {clamp_room_x(rr.x1 - 1), clamp_room_y(rr.y0 + 1)},
            }};
            int ax = candidates[0].first;
            int ay = candidates[0].second;
            for (const auto& [cx2, cy2] : candidates)
                if (!near_any_stair(plan, cx2, cy2, 2))
                {
                    ax = cx2;
                    ay = cy2;
                    break;
                }
            const Rect pad = {ax - 1, ay - 1, ax + 1, ay + 1};
            force_ground_pad(w, b, pad, plan.band->base_tile);
            force_ground_pad(w, b + 1, pad, plan.band->base_tile);
            stair_pair(w, b, ax, ay);
            plan.stairs.push_back({b, ax, ay});
        }
    }
}

// Post-smooth fall repair: any AIR cell a walker can step into must land on
// standable ground within 4 stories (spec step 11's fall audit, made true by
// construction). Landings the dressing broke are re-grounded; air columns
// past floor 0 are pit deaths and stay legal.
void repair_fall_landings(GameWorld& w, const BuildPlan& plan)
{
    for (int f = 1; f < plan.stories; ++f)
    {
        const PixieData& g = w.grid_for_floor(f);
        for (int ty = 0; ty < g.h; ++ty)
            for (int tx = 0; tx < g.w; ++tx)
            {
                if (g.data[static_cast<std::size_t>(tx + ty * g.w)] != PIX_AIR)
                    continue;
                bool entry = false;
                for (int dy = -1; dy <= 1 && !entry; ++dy)
                    for (int dx = -1; dx <= 1 && !entry; ++dx)
                        if ((dx != 0 || dy != 0) &&
                            cell_standable(w, f, tx + dx, ty + dy))
                            entry = true;
                if (!entry)
                    continue;
                int lf = f - 1;
                while (lf > 0 &&
                       w.grid_for_floor(lf).data[static_cast<std::size_t>(tx + ty * g.w)] == PIX_AIR)
                    --lf;
                if (w.grid_for_floor(lf).data[static_cast<std::size_t>(tx + ty * g.w)] == PIX_AIR)
                    continue; // pit: designed death
                if (!cell_standable(w, lf, tx, ty))
                    paint(w.grid_for_floor(lf), tx, ty, plan.band->base_tile);
            }
    }
}

// --- Composition. ----------------------------------------------------------------

struct FoePick
{
    int family;
    int count;
};

std::vector<FoePick> composition_for(const BandSpec& band, int floor_number,
                                     int total)
{
    std::vector<FoePick> picks;
    int allowed_total_pct = 0;
    for (const FoeMix& m : band.mix)
        if (m.family >= 0 && floor_number >= m.min_floor)
            allowed_total_pct += m.percent;
    if (allowed_total_pct <= 0)
        return picks;
    int placed = 0;
    for (const FoeMix& m : band.mix)
    {
        if (m.family < 0 || floor_number < m.min_floor)
            continue;
        const int n = (total * m.percent) / allowed_total_pct;
        if (n > 0)
            picks.push_back({m.family, n});
        placed += n;
    }
    if (!picks.empty() && placed < total)
        picks.front().count += total - placed; // remainder to the anchor family
    return picks;
}

// The MAXOBS worst-case model (spec step 11): authored livings with slimes
// counted twice (band-3 splitting), plus 8 spawned livings per generator at
// generator_rate=400 (Frenzy). Budget <= 120 leaves 30 slots of headroom for
// the crew and its summons under the engine's MAXOBS=150.
int maxobs_worst_case(const GameWorld& w)
{
    int worst = 0;
    for (const auto& uptr : w.oblist)
    {
        const walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->query_order() == Order::Living)
            worst += (ob->family() == FAMILY_SLIME) ? 2 : 1;
        else if (ob->query_order() == Order::Generator)
            worst += 8;
    }
    return worst;
}

constexpr int kMaxobsBudget = 120;

// --- Audits (spec step 11). -------------------------------------------------------

std::vector<std::string> run_audits(GameWorld& w, const BuildPlan& plan,
                                    const std::list<std::string>& description)
{
    std::vector<std::string> errors = og::mapgen::audit_footing(w);
    {
        auto e = og::mapgen::audit_stairs(w, /*require_every_boundary=*/true);
        errors.insert(errors.end(), e.begin(), e.end());
    }
    {
        auto e = og::mapgen::audit_fall_lines(w, /*max_fall_depth=*/4);
        errors.insert(errors.end(), e.begin(), e.end());
    }
    // Reachability needs an installed GameplayContext. At GO time (D8) none
    // is installed -> guard a scratch context; under the unit harness an
    // ambient context exists and the audit swap-restores its world pointer.
    if (current_game != nullptr)
    {
        auto e = og::mapgen::audit_reachability(w);
        errors.insert(errors.end(), e.begin(), e.end());
    }
    else
    {
        GameplayContext scratch{};
        scratch.world = &w;
        GameplayContextGuard guard(&scratch);
        auto e = og::mapgen::audit_reachability(w);
        errors.insert(errors.end(), e.begin(), e.end());
    }
    const int worst = maxobs_worst_case(w);
    if (worst > kMaxobsBudget)
        errors.push_back(std::format(
            "maxobs: worst-case population {} exceeds the {} budget "
            "(Frenzy generators modeled at 8 spawns, slimes split 2x)",
            worst, kMaxobsBudget));
    if (w.title.size() > 30)
        errors.push_back(std::format("title '{}' overflows the 30-char field",
                                     w.title));
    for (const std::string& line : description)
        if (line.size() > kBriefingLineBudget)
            errors.push_back(std::format(
                "briefing line '{}' overflows the {}-char budget", line,
                kBriefingLineBudget));
    (void)plan;
    return errors;
}

// --- The build itself. -------------------------------------------------------------

TemplateId pick_template(const BandSpec& band, SeedStream& s)
{
    int total = 0;
    for (int wgt : band.tmpl_weights)
        total += wgt;
    int roll = static_cast<int>(s.next(static_cast<std::uint32_t>(total)));
    for (int t = 0; t < kTemplateCount; ++t)
    {
        roll -= band.tmpl_weights[static_cast<std::size_t>(t)];
        if (roll < 0)
            return static_cast<TemplateId>(t);
    }
    return T0_ARENA;
}

void place_squads(GameWorld& w, BuildPlan& plan, const Layout& lay,
                  SeedStream& s)
{
    const int f = plan.floor;
    const int L = foe_level_for_floor(f);
    const bool boss_floor = is_boss_floor(f);
    int total = foe_count_for_floor(f);
    if (boss_floor)
        total = (total * 7) / 10;

    // Rooms sorted by distance from the entry pad, stories counting as
    // farther; the nearest room is the crew's own and gets no squad.
    std::vector<const Room*> order;
    for (const Room& room : lay.rooms)
        order.push_back(&room);
    const int exx = plan.entry.cx();
    const int exy = plan.entry.cy();
    std::sort(order.begin(), order.end(),
              [&](const Room* a, const Room* b) {
                  const int da = (a->r.cx() - exx) * (a->r.cx() - exx) +
                                 (a->r.cy() - exy) * (a->r.cy() - exy) +
                                 a->story * 10000;
                  const int db = (b->r.cx() - exx) * (b->r.cx() - exx) +
                                 (b->r.cy() - exy) * (b->r.cy() - exy) +
                                 b->story * 10000;
                  return da < db;
              });

    // The rival-raider variant carves a quarter of the wave into a team-3
    // squad, hostile to both sides (mid-route room).
    int raiders = plan.rival_raiders ? std::max(3, total / 4) : 0;
    total -= raiders;
    const int roamers = plan.ambush_posture ? 0 : total / 4;
    const int room_troops = total - roamers;

    std::vector<FoePick> comp = composition_for(*plan.band, f, room_troops);
    std::vector<int> roster;
    for (const FoePick& p : comp)
        for (int i = 0; i < p.count; ++i)
            roster.push_back(p.family);

    // Fill room-by-room (skipping the entry room), nearest first, squad
    // size proportional to what remains vs rooms left.
    std::size_t next_foe = 0;
    const std::size_t squad_rooms = order.size() > 1 ? order.size() - 1 : 1;
    for (std::size_t i = 1; i < order.size() && next_foe < roster.size(); ++i)
    {
        const Room& room = *order[i];
        const std::size_t remaining_rooms = squad_rooms - (i - 1);
        std::size_t squad =
            (roster.size() - next_foe + remaining_rooms - 1) / remaining_rooms;
        for (std::size_t k = 0; k < squad && next_foe < roster.size(); ++k)
        {
            if (place_validated(w, plan, s, Order::Living, roster[next_foe],
                                kFoeTeam, room.story, room.r, L,
                                /*guard=*/true) == nullptr)
                break;
            ++next_foe;
        }
    }
    // Corridor roamers: hashed spots anywhere on a hashed story.
    std::vector<FoePick> roam_comp = composition_for(*plan.band, f, roamers);
    const Rect whole = {2, 2, plan.tw - 3, plan.th - 3};
    for (const FoePick& p : roam_comp)
        for (int i = 0; i < p.count; ++i)
        {
            const int story = s.range(0, plan.stories - 1);
            walker* ob = place_validated(w, plan, s, Order::Living, p.family,
                                         kFoeTeam, story, whole, L);
            if (ob != nullptr)
                ob->set_act_type(ACT_RANDOM);
        }
    // Raiders: one mid-route room, team 3.
    if (raiders > 0 && order.size() > 2)
    {
        const Room& room = *order[order.size() / 2];
        std::vector<FoePick> raid_comp =
            composition_for(*plan.band, f, raiders);
        for (const FoePick& p : raid_comp)
            for (int i = 0; i < p.count; ++i)
            {
                walker* ob =
                    place_validated(w, plan, s, Order::Living, p.family,
                                    kRaiderTeam, room.story, room.r, L);
                if (ob != nullptr)
                    ob->set_act_type(ACT_RANDOM);
            }
    }

    // Elites in the far room at L+2; lap k adds elite share (cap 50% of N).
    const int base_total = foe_count_for_floor(f);
    int elites = elite_slots_for_floor(f);
    elites = std::min(elites, base_total / 2);
    const Room& far_room = *order.back();
    for (int i = 0; i < elites; ++i)
    {
        if (place_validated(w, plan, s, Order::Living, plan.band->boss_family,
                            kFoeTeam, far_room.story, far_room.r, L + 2,
                            /*guard=*/true) == nullptr)
            break;
    }

    // Boss floor: the named boss at L+3 near the exit, +1 named elite per
    // lap; the Summit boss brings a golem pair.
    if (boss_floor)
    {
        walker* b = place_validated(w, plan, s, Order::Living,
                                    plan.band->boss_family, kFoeTeam,
                                    far_room.story, far_room.r, L + 3,
                                    /*guard=*/true);
        if (b != nullptr)
            b->stats()->name = plan.band->boss_name;
        for (int k = 0; k < plan.lap; ++k)
        {
            walker* e = place_validated(w, plan, s, Order::Living,
                                        plan.band->boss_family, kFoeTeam,
                                        far_room.story, far_room.r, L + 2,
                                        /*guard=*/true);
            if (e == nullptr)
                break;
            e->stats()->name = "Highwarden";
        }
        if (plan.band_idx == 5)
            for (int i = 0; i < 2; ++i)
                (void)place_validated(w, plan, s, Order::Living, FAMILY_GOLEM,
                                      kFoeTeam, far_room.story, far_room.r,
                                      L + 1, /*guard=*/true);
    }

    // Generators in a rear room (second farthest when there is one).
    const Room& gen_room =
        order.size() > 1 ? *order[order.size() - 2] : far_room;
    for (const GenSpec& gs : plan.band->generators)
    {
        if (gs.family < 0 || f < gs.min_floor)
            continue;
        const int count = s.range(gs.count_min, gs.count_max);
        for (int i = 0; i < count; ++i)
            if (place_validated(w, plan, s, Order::Generator, gs.family,
                                kFoeTeam, gen_room.story, gen_room.r,
                                L) == nullptr)
                break;
    }

    // Turret posts (FAMILY_TOWER1) beside stair pads where the band allows.
    int turrets = plan.band->turrets_max > 0
                      ? s.range(plan.band_idx == 3 ? 2 : 0,
                                plan.band->turrets_max)
                      : 0;
    for (const StairCell& sc : plan.stairs)
    {
        if (turrets <= 0)
            break;
        const Rect around = {sc.tx - 5, sc.ty - 5, sc.tx + 5, sc.ty + 5};
        const Rect clamped = {std::max(2, around.x0), std::max(2, around.y0),
                              std::min(plan.tw - 3, around.x1),
                              std::min(plan.th - 3, around.y1)};
        if (place_validated(w, plan, s, Order::Living, FAMILY_TOWER1,
                            kFoeTeam, sc.lower_story + 1, clamped, L,
                            /*guard=*/true) != nullptr)
            --turrets;
    }
}

void place_treasures(GameWorld& w, BuildPlan& plan, const Layout& lay,
                     SeedStream& s)
{
    const int f = plan.floor;
    const int L = foe_level_for_floor(f);
    auto drop = [&](int family, int story, const Rect& zone, int level) {
        return place_validated(w, plan, s, Order::Treasure, family, 0, story,
                               zone, level);
    };
    // Route = rooms by distance from entry (recomputed cheaply here).
    std::vector<const Room*> route;
    for (const Room& room : lay.rooms)
        route.push_back(&room);
    const int exx = plan.entry.cx();
    const int exy = plan.entry.cy();
    std::sort(route.begin(), route.end(), [&](const Room* a, const Room* b) {
        const int da = (a->r.cx() - exx) * (a->r.cx() - exx) +
                       (a->r.cy() - exy) * (a->r.cy() - exy) +
                       a->story * 10000;
        const int db = (b->r.cx() - exx) * (b->r.cx() - exx) +
                       (b->r.cy() - exy) * (b->r.cy() - exy) +
                       b->story * 10000;
        return da < db;
    });
    auto route_room = [&](std::size_t i) -> const Room& {
        return *route[i % route.size()];
    };

    const int gold = std::min(2 + f / 10, 5);
    const int silver = std::min(2 + f / 8, 6);
    const int sticks = 2 + plan.stories;
    for (int i = 0; i < gold; ++i)
    {
        const Room& room = route_room(static_cast<std::size_t>(1 + i));
        drop(FAMILY_GOLD_BAR, room.story, room.r, L);
    }
    for (int i = 0; i < silver; ++i)
    {
        const Room& room = route_room(static_cast<std::size_t>(2 + i));
        drop(FAMILY_SILVER_BAR, room.story, room.r, L);
    }
    for (int i = 0; i < sticks; ++i)
    {
        const Room& room = route_room(static_cast<std::size_t>(i));
        drop(FAMILY_DRUMSTICK, room.story, room.r, 1);
    }
    // Exactly one band-schedule utility potion, mid-route.
    {
        const Room& room = route_room(route.size() / 2);
        drop(plan.band->potion_family, room.story, room.r, 1);
    }
    // Vault floors (f%5==0): +3 gold at L+1 plus the run's only
    // invulnerable potion, in the vault room (T6) or the far room, behind a
    // pair of extra guard posts.
    if (is_boss_floor(f))
    {
        const Room& vault = (lay.vault_room >= 0)
                                ? lay.rooms[static_cast<std::size_t>(
                                      lay.vault_room)]
                                : *route.back();
        for (int i = 0; i < 3; ++i)
            drop(FAMILY_GOLD_BAR, vault.story, vault.r, L + 1);
        drop(FAMILY_INVULNERABLE_POTION, vault.story, vault.r, 1);
        for (int i = 0; i < 2; ++i)
            (void)place_validated(w, plan, s, Order::Living,
                                  plan.band->boss_family, kFoeTeam,
                                  vault.story, vault.r,
                                  foe_level_for_floor(f) + 1, /*guard=*/true);
    }
}

// The lib scatters keep off the stair CELLS but not their 4-neighborhood
// arrival cells; the stair audit protects the whole cross on both floors of
// a pair. Sweep any blocking decor the dressing left there.
void clear_stair_cross_blocking_decor(GameWorld& w, const BuildPlan& plan)
{
    static constexpr int kCross[5][2] = {
        {0, 0}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (const StairCell& sc : plan.stairs)
        for (const int pf : {sc.lower_story, sc.lower_story + 1})
        {
            PixieData& dec = w.decor_for_floor(pf);
            if (!dec.valid())
                continue;
            for (const auto& off : kCross)
            {
                const int nx = sc.tx + off[0];
                const int ny = sc.ty + off[1];
                if (nx < 0 || ny < 0 || nx >= dec.w || ny >= dec.h)
                    continue;
                const unsigned char d = dec.data[static_cast<std::size_t>(nx + ny * dec.w)];
                if (d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                    dec.data[static_cast<std::size_t>(nx + ny * dec.w)] = DECOR_NONE;
            }
        }
}

void scatter_dressing(GameWorld& w, const BuildPlan& plan)
{
    const std::uint32_t seed = plan.seed;
    for (int f = 0; f < plan.stories; ++f)
    {
        scatter_litter(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 41);
        scatter_boulders(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 37);
        switch (plan.band_idx)
        {
            case 0:
                scatter_decor(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 23,
                              DECOR_PEBBLES, {ScatterGround::Grass});
                scatter_decor(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 29,
                              DECOR_SHRUB, {ScatterGround::Grass});
                break;
            case 2:
                scatter_decor(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 19,
                              DECOR_BONES, {ScatterGround::DarkDirt});
                break;
            case 5:
                scatter_decor(w, seed, f, 1, 1, plan.tw - 2, plan.th - 2, 27,
                              DECOR_PEBBLES, {ScatterGround::Snow});
                break;
            default:
                break;
        }
    }
}

} // namespace

// --- Public knobs. ------------------------------------------------------------------

std::uint32_t floor_seed(std::uint32_t run_seed, int floor_number)
{
    return static_cast<std::uint32_t>(splitmix64(
        static_cast<std::uint64_t>(run_seed) ^
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(floor_number)) *
         0x9E3779B97F4A7C15ull)));
}

int foe_level_for_floor(int floor_number)
{
    return std::min(1 + (floor_number - 1) / 3, 50);
}

int foe_count_for_floor(int floor_number)
{
    return std::min(7 + floor_number, 30);
}

int elite_slots_for_floor(int floor_number)
{
    const int base = floor_number / 5;
    const int lap = lap_for_floor(floor_number);
    const int share =
        (foe_count_for_floor(floor_number) * std::min(10 * lap, 50)) / 100;
    return base + share;
}

bool is_boss_floor(int floor_number)
{
    return floor_number > 0 && floor_number % 5 == 0;
}

int band_index_for_floor(int floor_number)
{
    return ((floor_number - 1) % 30) / 5;
}

int lap_for_floor(int floor_number)
{
    return (floor_number - 1) / 30;
}

// --- The build. -----------------------------------------------------------------------

std::vector<std::string> build_tower_floor(GameWorld& world,
                                           std::list<std::string>& description,
                                           std::uint32_t run_seed,
                                           int floor_number,
                                           int attempt)
{
    BuildPlan plan;
    plan.floor = floor_number;
    // Salted reroll: attempts 1..3 shift the floor seed deterministically.
    plan.seed = floor_seed(run_seed, floor_number) +
                static_cast<std::uint32_t>(attempt);
    plan.band_idx = band_index_for_floor(floor_number);
    plan.band = &kBands[static_cast<std::size_t>(plan.band_idx)];
    plan.lap = lap_for_floor(floor_number);
    plan.tw = plan.band->tw;
    plan.th = plan.band->th;

    // (1) Streams + picks. The scratch world's own SimRandom (smoothing)
    // reruns from the same seed too.
    world.rng_.state_ = plan.seed;
    wire_entity_services(world);
    SeedStream s(plan.seed, /*salt=*/7u);

    plan.stories = s.range(plan.band->stories_min, plan.band->stories_max);
    if (plan.band_idx == 0 && is_boss_floor(floor_number))
        plan.stories = 2; // the Bailey's boss floor grows a second story
    plan.tmpl = (attempt >= 3) ? T0_ARENA : pick_template(*plan.band, s);
    if (attempt >= 3)
        plan.stories = std::min(plan.stories, 2); // fallback keeps it simple

    // ~1 in 4 non-boss floors leave the stairs open (sprint past the foes);
    // every ~3rd floor rolls a posture variant.
    plan.open_stairs = !is_boss_floor(floor_number) && s.chance_percent(25);
    if (floor_number % 3 == 0)
    {
        if (s.chance_percent(50))
            plan.rival_raiders = true;
        else
            plan.ambush_posture = true;
    }

    // (2) World bootstrap + band base + template carve.
    og::mapgen::init_world(world, plan.stories, plan.tw, plan.th);
    for (int f = 0; f < plan.stories; ++f)
        paint_rect(world.grid_for_floor(f), 0, 0, plan.tw - 1, plan.th - 1,
                   plan.band->base_tile);

    Layout lay;
    switch (plan.tmpl)
    {
        case T0_ARENA:
            carve_open(world, plan, lay, s, /*courtyard=*/false);
            break;
        case T1_COURTYARD:
            carve_open(world, plan, lay, s, /*courtyard=*/true);
            break;
        case T2_HALLS:
            carve_halls(world, plan, lay, s, /*vault=*/false);
            break;
        case T3_WARREN:
            carve_warren(world, plan, lay, s);
            break;
        case T4_SPIRE:
            carve_spire(world, plan, lay, s,
                        /*air_rings=*/plan.band_idx == 3);
            break;
        case T5_MOAT:
            carve_moat(world, plan, lay, s);
            break;
        case T6_VAULT:
            carve_halls(world, plan, lay, s, /*vault=*/true);
            break;
        default:
            carve_open(world, plan, lay, s, /*courtyard=*/false);
            break;
    }
    if (attempt < 3) // the fallback arena stays undressed (audit-clean)
        dress_band(world, plan, lay, s);

    // Entry pad: story 0, SW, forced ground. Every template's story 0
    // either is full ground or (the warren) carves its entry chamber
    // overlapping this rect, so the pad always joins the walkable graph.
    plan.entry = {3, plan.th - 9, 14, plan.th - 4};
    force_ground_pad(world, 0, plan.entry, plan.band->base_tile);
    // Exit pad: derived from the top story's room nearest the NE ideal —
    // NOT a fixed rect: on a spire story a fixed NE pad would float in the
    // open air beyond the platform.
    const int top = plan.stories - 1;
    {
        const Room* exit_room = nullptr;
        int best_d = 0;
        const int ix = plan.tw - 5;
        const int iy = 5;
        for (const Room& room : lay.rooms)
        {
            if (room.story != top)
                continue;
            const int dx = room.r.cx() - ix;
            const int dy = room.r.cy() - iy;
            const int d = dx * dx + dy * dy;
            if (exit_room == nullptr || d < best_d)
            {
                exit_room = &room;
                best_d = d;
            }
        }
        if (exit_room != nullptr)
        {
            const Rect& rr = exit_room->r;
            plan.exit_pad = {std::max(rr.x0 + 1, rr.x1 - 4), rr.y0 + 1,
                             rr.x1 - 1, std::min(rr.y1 - 1, rr.y0 + 4)};
        }
        else
        {
            plan.exit_pad = {plan.tw - 9, 3, plan.tw - 4, 8};
        }
    }
    force_ground_pad(world, top, plan.exit_pad, plan.band->base_tile);

    // (3) Stairs, then the genre smoother (scratch-world rng only), then the
    // fall-landing repair over whatever the dressing did.
    place_stairs(world, plan, lay);
    og::mapgen::smooth_world(world);
    repair_fall_landings(world, plan);

    // (4) Start markers: LEAD FIRST (deploy consumes markers in oblist
    // order), 2x2 footprints on a 2-tile grid inside the forced pad.
    for (int i = 0; i < 10; ++i)
        place_start(world, 0, 4 + (i % 5) * 2, plan.th - 8 + (i / 5) * 2);

    // (5)(6) Squads, elites, boss, generators, turrets.
    place_squads(world, plan, lay, s);

    // (7) Treasure economy.
    place_treasures(world, plan, lay, s);

    // (8) Scatter dressing (position-hash streams; entity/stair/fall-safe),
    // then sweep blocking decor off the stair-cross arrival cells.
    scatter_dressing(world, plan);
    clear_stair_cross_blocking_decor(world, plan);

    // (9) The exit: top story, far quadrant, destination id+1.
    {
        int tx = plan.exit_pad.cx();
        int ty = plan.exit_pad.cy();
        int fx = 0;
        int fy = 0;
        if (find_spot(world, plan, s, top, plan.exit_pad, fx, fy))
        {
            tx = fx;
            ty = fy;
        }
        place_exit(world, top, tx, ty,
                   og::kTowerGateLevel + floor_number + 1);
    }
    world.type = static_cast<char>(
        SCEN_TYPE_TOWER | (plan.open_stairs ? SCEN_TYPE_CAN_EXIT : 0));

    // (10) Identity, title, briefing, par/limit.
    world.id = og::kTowerGateLevel + floor_number;
    world.title = std::format("Floor {}", floor_number);
    description.clear();
    description.push_back(plan.band->briefing1);
    if (plan.band->briefing2 != nullptr)
        description.push_back(plan.band->briefing2);
    if (plan.open_stairs)
        description.push_back(kOpenStairsLine);
    world.time_bonus_limit = static_cast<short>(
        2600 + 300 * plan.stories + 12 * foe_count_for_floor(floor_number));
    world.par_value =
        static_cast<short>(4 + 2 * plan.band_idx + 3 * plan.lap);

    // (11) Audits.
    return run_audits(world, plan, description);
}

TowerFloorReport generate_tower_floor_to_user_dir(std::uint32_t run_seed,
                                                  int floor_number)
{
    TowerFloorReport report;
    report.floor_number = floor_number;
    report.run_seed = run_seed;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        GameWorld world(0);
        std::list<std::string> description;
        std::vector<std::string> failures =
            build_tower_floor(world, description, run_seed, floor_number,
                              attempt);
        report.attempts = attempt + 1;
        report.used_fallback = (attempt == 3);
        if (!failures.empty() && attempt < 3)
        {
            for (const std::string& e : failures)
                Log("tower_floor_gen: floor {} attempt {}: {}\n",
                    floor_number, attempt + 1, e);
            continue; // salted reroll
        }
        report.audit_failures = std::move(failures);
        og::data::LevelFileMetadata metadata;
        metadata.description = description;
        og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
        report.written = og::data::save_level_to_user_dir(
            world, og::kTowerGateLevel + floor_number, metadata, &err);
        if (!report.written)
            Log("tower_floor_gen: floor {} write failed (err {})\n",
                floor_number, static_cast<int>(err));
        return report;
    }
    return report; // unreachable: attempt 3 always returns above
}

} // namespace og::tower
