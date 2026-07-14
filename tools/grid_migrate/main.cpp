/* grid_migrate — BASE + DECOR migration for hand-authored campaign packages.
 *
 * Rewrites a stock .glad package so that legacy combined tiles (torches,
 * brazier, boulders, grass rubble — see mapping.h) become BASE ground tiles
 * plus a DECOR plane (.fss v11, core/decordefs.h), leaving every other byte
 * of every level untouched. The tool is equivalence-first, cosmetics second:
 *
 *   1. Mount the input package, snapshot every level IN MEMORY (the OLD
 *      worlds stay alive for the whole run).
 *   2. Transform each level's grids per the mapping (fixed bases first, then
 *      the boulders' deterministic neighbor-majority contextual base against
 *      a frozen snapshot), building the decor planes. Any transformed cell
 *      that fails a cell-local audit (passability across the five mover
 *      archetypes, concealment, damage-transform, door-frame genre) REVERTS
 *      to its legacy byte with no decor.
 *   3. Re-save every level through the production writer (which auto-emits
 *      v11 exactly where decor exists), copy campaign.yaml / icon / all
 *      other zip members through (stale "_dN" members are dropped), rezip.
 *   4. PROOF: mount the OUTPUT, reload every level, and compare against the
 *      in-memory OLD world — per-cell passability matrix, concealment,
 *      damage semantics, door-frame genre, entity-stream identity, footing,
 *      and a byte-identity audit (every cell is an exact copy or a mapping
 *      product). Any mismatch exits nonzero. The weather outdoor-vote
 *      classification is REPORTED (never a failure): boulder cells genuinely
 *      are outdoor, and the weather FX is cfg-gated off by default.
 *
 * og_test_parity is the downstream gate: identical passability ==> identical
 * A* paths ==> identical sim, zero new RNG (probes are owner-less, so even
 * the arrow-wall gamble arm consumes nothing).
 *
 * Usage: grid_migrate <campaign_id> [output.glad]
 *        (default output: builtin/<campaign_id>.glad in the cwd)
 * Driven by scripts/migrate_stock_campaigns.sh for the three hand-authored
 * stock packages (gladiator, tryxian, arenas). Generated packages (ctf,
 * concept, westlands) regenerate from their mapgen tools instead.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "mapping.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

void io_init(int argc, char* argv[]);
void io_exit();
std::string get_user_path();

// --- Headless process globals (same shape as the mapgen tools). -------------
namespace og::runtime {
static SessionState s_migrate_session{};
thread_local SessionState* current_session = &s_migrate_session;
std::atomic<SessionState*> primary_session{&s_migrate_session};
std::atomic<GameplayContext*> primary_game{&s_migrate_session.game_};
} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 20260707u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace gridmig {

namespace {

int g_errors = 0;

void fail(const std::string& message)
{
    std::fprintf(stderr, "grid_migrate: ERROR: %s\n", message.c_str());
    ++g_errors;
}

// ---------------------------------------------------------------------------
// Probe kit: the five mover archetypes of the passability matrix. Owner-less
// 16x16 (one-tile) walkers that are never added to any world list, so probing
// mutates nothing and — crucially — never reaches the arrow-wall RNG gamble
// (that arm needs an owner; owner-less non-livings take the deterministic
// "stays solid" return). Zero RNG consumed per probe, by construction.
// ---------------------------------------------------------------------------
struct ProbeKit
{
    std::unique_ptr<walker> ground;   // ground living (no flight, no forest)
    std::unique_ptr<walker> weapon;   // projectile
    std::unique_ptr<walker> flyer;    // BIT_FLYING living
    std::unique_ptr<walker> forest;   // BIT_FORESTWALK living
    std::unique_ptr<walker> special;  // owner-less Order::Special

    std::array<walker*, 5> all() const
    {
        return {ground.get(), weapon.get(), flyer.get(), forest.get(),
                special.get()};
    }
};

std::unique_ptr<walker> make_probe(GameWorld& world, Order order, int family)
{
    if (!world.entity_factory)
        return nullptr;
    std::unique_ptr<walker> probe = world.entity_factory(order, family);
    if (probe == nullptr)
        return nullptr;
    PixieData square;
    square.frames = 1;
    square.w = 16;
    square.h = 16;
    square.data = std::make_unique<unsigned char[]>(16 * 16);
    probe->set_data(square);
    // Pin the archetype-relevant stat bits explicitly: family data must not
    // leak flight/forestwalk/ethereal into a probe that models a grounder.
    probe->stats()->set_bit_flags(BIT_FLYING, 0);
    probe->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    probe->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    return probe;
}

bool make_probe_kit(GameWorld& world, ProbeKit& kit)
{
    kit.ground = make_probe(world, Order::Living, FAMILY_SOLDIER);
    kit.weapon = make_probe(world, Order::Weapon, FAMILY_KNIFE);
    kit.flyer = make_probe(world, Order::Living, FAMILY_SOLDIER);
    kit.forest = make_probe(world, Order::Living, FAMILY_SOLDIER);
    kit.special = make_probe(world, Order::Special, FAMILY_RESERVED_TEAM);
    if (!kit.ground || !kit.weapon || !kit.flyer || !kit.forest ||
        !kit.special)
    {
        fail("could not create the passability probe kit");
        return false;
    }
    kit.flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    kit.forest->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    return true;
}

// One-tile probe at exact grid alignment: consults exactly cell (tx, ty).
std::array<bool, 5> passability_vector(GameWorld& world, const ProbeKit& kit,
                                       int tx, int ty, int floor)
{
    std::array<bool, 5> out{};
    const std::array<walker*, 5> probes = kit.all();
    for (std::size_t i = 0; i < probes.size(); ++i)
    {
        out[i] = world.query_grid_passable(
            static_cast<float>(tx * GRID_SIZE),
            static_cast<float>(ty * GRID_SIZE), probes[i], floor);
    }
    return out;
}

// Hidden-eligibility of a cell: the four TYPE_TREES concealment consumers
// (forestwalk hide, weapon lineofsight decay, the draw-suppression pair) all
// reduce to "base genre is TREES, or the decor plane conceals here".
bool conceal_eligible(GameWorld& world, int tx, int ty, int floor)
{
    return world.smoother_for_floor(floor).query_genre_x_y(tx, ty) ==
               TYPE_TREES ||
           world.decor_conceals_at(floor, tx, ty);
}

// damage_tile semantics probe (floor 0 only — the legacy single-grid API).
// NOT raw return-byte equality: damage_tile returns the tile byte, which
// changed by construction on migrated cells. We compare "a grid transform
// occurred" plus the resulting cell's passability class, then restore the
// byte and the dirty-tile list so the probe leaves no trace.
struct DamageProbe
{
    bool transformed = false;
    std::array<bool, 5> post_pass{};

    bool operator==(const DamageProbe&) const = default;
};

DamageProbe damage_probe(GameWorld& world, const ProbeKit& kit, int tx, int ty)
{
    DamageProbe result;
    if (!world.grid.valid())
        return result;
    const std::size_t loc =
        static_cast<std::size_t>(ty) * world.grid.w + static_cast<std::size_t>(tx);
    const unsigned char before = world.grid.data[loc];
    (void)world.damage_tile(static_cast<short>(tx * GRID_SIZE),
                            static_cast<short>(ty * GRID_SIZE));
    result.transformed = world.grid.data[loc] != before;
    result.post_pass = passability_vector(world, kit, tx, ty, 0);
    world.grid.data[loc] = before;
    world.clear_grid_dirty_tiles();
    return result;
}

// Cell-local equivalence: the audits every migrated cell must pass, and the
// per-cell body of the post-reload proof sweep.
bool cell_equivalent(GameWorld& old_world, GameWorld& new_world,
                     const ProbeKit& kit, int floor, int tx, int ty,
                     std::string* why)
{
    if (passability_vector(old_world, kit, tx, ty, floor) !=
        passability_vector(new_world, kit, tx, ty, floor))
    {
        if (why != nullptr)
            *why = "passability";
        return false;
    }
    if (conceal_eligible(old_world, tx, ty, floor) !=
        conceal_eligible(new_world, tx, ty, floor))
    {
        if (why != nullptr)
            *why = "concealment";
        return false;
    }
    if (floor == 0 && !(damage_probe(old_world, kit, tx, ty) ==
                        damage_probe(new_world, kit, tx, ty)))
    {
        if (why != nullptr)
            *why = "damage";
        return false;
    }
    return true;
}

// The door-frame orientation predicate, applied at load
// (level_file_io.cpp read_level_body) AND at runtime death
// (weapon_family_door.cpp door_on_death) — one audit covers both.
bool door_frame_wall_above(GameWorld& world, walker* door)
{
    return world.mysmoother.query_genre_x_y(
               door->xpos() / GRID_SIZE,
               (door->ypos() / GRID_SIZE) - 1) == TYPE_WALL;
}

// Mirror of the render-side weather outdoor vote
// (single_floor_reads_outdoor, src/interface/render/effects.cpp): count the
// open-sky genres over floor 0. Reported per level, never a failure — the
// weather FX is cfg-gated OFF by default and migrated boulder cells
// genuinely are outdoor ground.
int outdoor_votes(GameWorld& world)
{
    const PixieData& grid = world.grid_for_floor(0);
    if (!grid.valid())
        return 0;
    smoother& sm = world.smoother_for_floor(0);
    int outdoor = 0;
    for (int y = 0; y < static_cast<int>(grid.h); ++y)
        for (int x = 0; x < static_cast<int>(grid.w); ++x)
            switch (sm.query_genre_x_y(x, y))
            {
                case TYPE_GRASS:
                case TYPE_GRASS_DARK:
                case TYPE_GRASS_LIGHT:
                case TYPE_WATER:
                case TYPE_TREES:
                case TYPE_DIRT:
                case TYPE_DIRT_DARK:
                case TYPE_SNOW:
                case TYPE_LAVA:
                case TYPE_MARSH:
                case TYPE_ASH:
                    outdoor++;
                    break;
                default:
                    break;
            }
    return outdoor;
}

// ---------------------------------------------------------------------------
// Transform: mapping application + cell-local fallback.
// ---------------------------------------------------------------------------

unsigned char* decor_cell(GameWorld& world, int floor, int tx, int ty)
{
    PixieData& dec = world.decor_for_floor(floor);
    if (!dec.valid())
        return nullptr;
    return &dec.data[static_cast<std::size_t>(tx) +
                     static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(ty)];
}

void ensure_decor_plane(GameWorld& world, int floor)
{
    PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid())
        return;
    const PixieData& g = world.grid_for_floor(floor);
    const std::size_t cells =
        static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.h);
    auto* buf = new unsigned char[cells];
    std::fill(buf, buf + cells, static_cast<unsigned char>(DECOR_NONE));
    dec = PixieData(1, static_cast<unsigned char>(g.w),
                    static_cast<unsigned char>(g.h), buf);
}

struct LevelReport
{
    int migrated_cells = 0;
    int reverted_cells = 0;
    int outdoor_old = 0;
    int outdoor_new = 0;
    int total_cells = 0;
    bool outdoor_flip = false;
};

// Deterministic contextual base for a boulder cell: scan the 8 neighbors'
// POST-fixed-pass bytes in a frozen snapshot (so boulder-on-boulder clusters
// cannot order-depend on each other), take the majority walkable plain-ground
// genre, represent it by its canonical tile; ties and ground-less
// surroundings fall back to the row's art-default grass. `snapshot_smoother`
// targets the frozen pass-1 grid so the genre table stays the engine's own.
unsigned char contextual_base(smoother& snapshot_smoother, int w, int h,
                              int tx, int ty, const MappedTile& row)
{
    // genre histogram over the plain-ground genres only
    std::map<std::int32_t, int> votes;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            const int nx = tx + dx;
            const int ny = ty + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            switch (const std::int32_t genre =
                        snapshot_smoother.query_genre_x_y(nx, ny))
            {
                case TYPE_GRASS:
                case TYPE_GRASS_DARK:
                case TYPE_GRASS_LIGHT:
                case TYPE_DIRT:
                case TYPE_DIRT_DARK:
                case TYPE_SNOW:
                case TYPE_ASH:
                    votes[genre] += 1;
                    break;
                default:
                    break;
            }
        }
    }
    std::int32_t best_genre = -1;
    int best_count = 0;
    bool tie = false;
    for (const auto& [genre, count] : votes)
    {
        if (count > best_count)
        {
            best_genre = genre;
            best_count = count;
            tie = false;
        }
        else if (count == best_count)
        {
            tie = true;
        }
    }
    if (best_count == 0 || tie)
        return row.base;
    switch (best_genre)
    {
        case TYPE_GRASS:
            return row.base;  // the row's art-default grass
        case TYPE_GRASS_DARK:
            return kCanonicalGrassDark;
        case TYPE_GRASS_LIGHT:
            return kCanonicalGrassLight;
        case TYPE_DIRT:
            return kCanonicalDirt;
        case TYPE_DIRT_DARK:
            return kCanonicalDirtDark;
        case TYPE_SNOW:
            return kCanonicalSnow;
        case TYPE_ASH:
            return kCanonicalAsh;
        default:
            return row.base;
    }
}

// Transform one level in place (work was freshly loaded from the same input
// as old_world). Applies the mapping per floor, then reverts any cell that
// fails a cell-local audit against the OLD world.
void migrate_world(int id, GameWorld& old_world, GameWorld& work,
                   const ProbeKit& kit, LevelReport& report)
{
    report.outdoor_old = outdoor_votes(old_world);
    report.total_cells = static_cast<int>(old_world.grid.w) *
                         static_cast<int>(old_world.grid.h);

    for (int f = 0; f < work.floor_count(); ++f)
    {
        PixieData& grid = work.grid_for_floor(f);
        if (!grid.valid())
            continue;
        const int w = grid.w;
        const int h = grid.h;

        // Pass 1: fixed-base rows (torches, brazier, rubble).
        std::vector<std::pair<int, int>> migrated;  // audit worklist
        for (int ty = 0; ty < h; ++ty)
        {
            for (int tx = 0; tx < w; ++tx)
            {
                const std::size_t loc = static_cast<std::size_t>(tx) +
                                        static_cast<std::size_t>(w) *
                                            static_cast<std::size_t>(ty);
                const MappedTile* row = map_legacy_tile(grid.data[loc]);
                if (row == nullptr || row->contextual)
                    continue;
                ensure_decor_plane(work, f);
                grid.data[loc] = row->base;
                *decor_cell(work, f, tx, ty) = row->decor;
                migrated.emplace_back(tx, ty);
            }
        }

        // Pass 2: contextual boulders against a frozen snapshot of the
        // pass-1 grid (deterministic regardless of iteration order: a
        // boulder neighbor still reads as its legacy byte, never as another
        // boulder's freshly written ground).
        PixieData snapshot;
        snapshot.frames = 1;
        snapshot.w = static_cast<unsigned char>(w);
        snapshot.h = static_cast<unsigned char>(h);
        const std::size_t cells =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        snapshot.data = std::make_unique<unsigned char[]>(cells);
        std::copy(grid.data.get(), grid.data.get() + cells,
                  snapshot.data.get());
        smoother snapshot_smoother;
        snapshot_smoother.set_target(snapshot);
        for (int ty = 0; ty < h; ++ty)
        {
            for (int tx = 0; tx < w; ++tx)
            {
                const std::size_t loc = static_cast<std::size_t>(tx) +
                                        static_cast<std::size_t>(w) *
                                            static_cast<std::size_t>(ty);
                const MappedTile* row = map_legacy_tile(grid.data[loc]);
                if (row == nullptr || !row->contextual)
                    continue;
                ensure_decor_plane(work, f);
                grid.data[loc] =
                    contextual_base(snapshot_smoother, w, h, tx, ty, *row);
                *decor_cell(work, f, tx, ty) = row->decor;
                migrated.emplace_back(tx, ty);
            }
        }

        report.migrated_cells += static_cast<int>(migrated.size());

        // Cell-local audits with per-cell fallback to the legacy byte.
        const PixieData& old_grid = old_world.grid_for_floor(f);
        auto revert = [&](int tx, int ty) {
            const std::size_t loc = static_cast<std::size_t>(tx) +
                                    static_cast<std::size_t>(w) *
                                        static_cast<std::size_t>(ty);
            grid.data[loc] = old_grid.data[loc];
            if (unsigned char* d = decor_cell(work, f, tx, ty))
                *d = DECOR_NONE;
            ++report.reverted_cells;
            --report.migrated_cells;
        };
        for (const auto& [tx, ty] : migrated)
        {
            std::string why;
            if (!cell_equivalent(old_world, work, kit, f, tx, ty, &why))
            {
                std::fprintf(stderr,
                             "grid_migrate: scen%d floor %d cell (%d, %d): "
                             "%s mismatch — reverting to legacy byte\n",
                             id, f, tx, ty, why.c_str());
                revert(tx, ty);
            }
        }

        // Door-frame genre audit (floor 0 doors; the load-time and runtime
        // checks share the predicate): a migration must never flip the
        // TYPE_WALL-ness of the cell above a door.
        if (f == 0)
        {
            for (auto& uptr : old_world.weaplist)
            {
                walker* door = uptr.get();
                if (door == nullptr || door->family() != FAMILY_DOOR)
                    continue;
                if (door_frame_wall_above(old_world, door) !=
                    door_frame_wall_above(work, door))
                {
                    const int tx = door->xpos() / GRID_SIZE;
                    const int ty = (door->ypos() / GRID_SIZE) - 1;
                    std::fprintf(stderr,
                                 "grid_migrate: scen%d door at (%d, %d): "
                                 "frame genre flip — reverting cell above\n",
                                 id, tx, ty + 1);
                    if (tx >= 0 && ty >= 0 && tx < w && ty < h)
                        revert(tx, ty);
                }
            }
        }
    }

    report.outdoor_new = outdoor_votes(work);
    const bool old_verdict =
        report.total_cells > 0 && report.outdoor_old * 2 >= report.total_cells;
    const bool new_verdict =
        report.total_cells > 0 && report.outdoor_new * 2 >= report.total_cells;
    report.outdoor_flip = old_verdict != new_verdict;
}

// ---------------------------------------------------------------------------
// Post-reload equivalence proof (OLD in-memory world vs the world reloaded
// from the produced package).
// ---------------------------------------------------------------------------

struct EntityRecord
{
    int order;
    int family;
    int team;
    int x;
    int y;
    int floor;
    float worldz;
    int level;
    std::string name;
    int act_type;
    bool specials_disabled;
    int spawn_delay;
    bool dormant;
    int frame;

    bool operator==(const EntityRecord&) const = default;
};

std::vector<EntityRecord>
record_list(const std::list<std::unique_ptr<walker>>& list)
{
    std::vector<EntityRecord> out;
    for (const auto& uptr : list)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        // Names are compared under the writer's own normalization: the
        // serializer snprintf's through a 12-byte buffer (11 chars + NUL),
        // while original DOS-era v6 files carry 12 name bytes with no
        // terminator — several tryxian NPCs have uninitialized junk tails
        // ("Gildmastr\x19\x17\x15") whose 12th byte any re-save has always
        // dropped. Sim-inert: BIT_NAMED keys off size()>1, which holds on
        // both sides.
        std::string written_name = ob->stats()->name.substr(0, 11);
        out.push_back(EntityRecord{
            static_cast<int>(ob->query_order()),
            static_cast<int>(ob->family()),
            static_cast<int>(ob->team_num()),
            static_cast<int>(ob->xpos()),
            static_cast<int>(ob->ypos()),
            static_cast<int>(ob->floor()),
            ob->worldz(),
            static_cast<int>(ob->stats()->level()),
            std::move(written_name),
            static_cast<int>(ob->act_type()),
            ob->specials_disabled(),
            static_cast<int>(ob->spawn_delay()),
            ob->dormant(),
            static_cast<int>(ob->frame()),
        });
    }
    return out;
}

// Tick order follows list order and the parity goldens embed rng_state, so a
// re-serialization that regrouped or reordered objects would silently shift
// the RNG stream even with identical terrain. This pins it.
void check_entity_streams(int id, GameWorld& old_world, GameWorld& new_world)
{
    const struct
    {
        const char* label;
        const std::list<std::unique_ptr<walker>>& old_list;
        const std::list<std::unique_ptr<walker>>& new_list;
    } lists[] = {
        {"oblist", old_world.oblist, new_world.oblist},
        {"fxlist", old_world.fxlist, new_world.fxlist},
        {"weaplist", old_world.weaplist, new_world.weaplist},
    };
    for (const auto& entry : lists)
    {
        const std::vector<EntityRecord> old_rec = record_list(entry.old_list);
        const std::vector<EntityRecord> new_rec = record_list(entry.new_list);
        if (old_rec.size() != new_rec.size())
        {
            fail(std::format("scen{}: {} size {} != {}", id, entry.label,
                             old_rec.size(), new_rec.size()));
            continue;
        }
        for (std::size_t i = 0; i < old_rec.size(); ++i)
        {
            if (!(old_rec[i] == new_rec[i]))
            {
                fail(std::format(
                    "scen{}: {}[{}] diverged (order {} family {} team {} at "
                    "{},{} vs order {} family {} team {} at {},{})",
                    id, entry.label, i, old_rec[i].order, old_rec[i].family,
                    old_rec[i].team, old_rec[i].x, old_rec[i].y,
                    new_rec[i].order, new_rec[i].family, new_rec[i].team,
                    new_rec[i].x, new_rec[i].y));
            }
        }
    }
}

// Byte-identity audit: every cell is either an exact copy of the legacy
// byte (decor 0) or a product of the mapping table. Catches "the tool wrote
// something the table does not sanction" and stale-plane classes outright.
void check_cell_bytes(int id, GameWorld& old_world, GameWorld& new_world,
                      int floor)
{
    const PixieData& og = old_world.grid_for_floor(floor);
    const PixieData& ng = new_world.grid_for_floor(floor);
    const PixieData& nd = new_world.decor_for_floor(floor);
    const bool has_decor = nd.valid();
    if (has_decor && (nd.w != ng.w || nd.h != ng.h))
    {
        fail(std::format("scen{} floor {}: decor plane dims {}x{} != grid "
                         "{}x{}", id, floor, nd.w, nd.h, ng.w, ng.h));
        return;
    }
    for (int ty = 0; ty < ng.h; ++ty)
    {
        for (int tx = 0; tx < ng.w; ++tx)
        {
            const std::size_t loc = static_cast<std::size_t>(tx) +
                                    static_cast<std::size_t>(ng.w) *
                                        static_cast<std::size_t>(ty);
            const unsigned char old_byte = og.data[loc];
            const unsigned char new_byte = ng.data[loc];
            const unsigned char decor_byte = has_decor ? nd.data[loc]
                                                       : DECOR_NONE;
            if (decor_byte >= DECOR_MAX)
            {
                fail(std::format("scen{} floor {} cell ({}, {}): decor byte "
                                 "{} out of range", id, floor, tx, ty,
                                 decor_byte));
                continue;
            }
            if (decor_byte != DECOR_NONE &&
                (new_byte == PIX_AIR || new_byte == PIX_ZSTAIR_UP ||
                 new_byte == PIX_ZSTAIR_DOWN || new_byte == PIX_VOID1))
            {
                fail(std::format("scen{} floor {} cell ({}, {}): decor over "
                                 "air/stair/void base {}", id, floor, tx, ty,
                                 new_byte));
                continue;
            }
            const MappedTile* row = map_legacy_tile(old_byte);
            const bool exact_copy =
                new_byte == old_byte && decor_byte == DECOR_NONE;
            bool table_product = false;
            if (row != nullptr && decor_byte == row->decor)
            {
                table_product = row->contextual
                                    ? is_contextual_base_candidate(new_byte)
                                    : new_byte == row->base;
            }
            if (!exact_copy && !table_product)
            {
                fail(std::format(
                    "scen{} floor {} cell ({}, {}): byte {} -> base {} decor "
                    "{} is neither a copy nor a mapping product", id, floor,
                    tx, ty, old_byte, new_byte, decor_byte));
            }
        }
    }
}

void check_equivalence(int id, GameWorld& old_world, GameWorld& new_world,
                       const ProbeKit& kit)
{
    // World-level fields the loader materializes.
    if (old_world.title != new_world.title)
        fail(std::format("scen{}: title '{}' != '{}'", id, old_world.title,
                         new_world.title));
    if (old_world.type != new_world.type)
        fail(std::format("scen{}: scenario type {} != {}", id,
                         static_cast<int>(old_world.type),
                         static_cast<int>(new_world.type)));
    if (old_world.par_value != new_world.par_value ||
        old_world.time_bonus_limit != new_world.time_bonus_limit)
    {
        fail(std::format("scen{}: par/time {} / {} != {} / {}", id,
                         old_world.par_value, old_world.time_bonus_limit,
                         new_world.par_value, new_world.time_bonus_limit));
    }
    if (old_world.floor_count() != new_world.floor_count())
    {
        fail(std::format("scen{}: floor_count {} != {}", id,
                         old_world.floor_count(), new_world.floor_count()));
        return;
    }
    if (old_world.pixmaxx != new_world.pixmaxx ||
        old_world.pixmaxy != new_world.pixmaxy)
    {
        fail(std::format("scen{}: pixmax {}x{} != {}x{}", id,
                         old_world.pixmaxx, old_world.pixmaxy,
                         new_world.pixmaxx, new_world.pixmaxy));
        return;
    }

    check_entity_streams(id, old_world, new_world);

    for (int f = 0; f < old_world.floor_count(); ++f)
    {
        const PixieData& og = old_world.grid_for_floor(f);
        const PixieData& ng = new_world.grid_for_floor(f);
        if (!og.valid() || !ng.valid() || og.w != ng.w || og.h != ng.h)
        {
            fail(std::format("scen{} floor {}: grid dims mismatch", id, f));
            continue;
        }
        check_cell_bytes(id, old_world, new_world, f);
        int bad_cells = 0;
        for (int ty = 0; ty < og.h && bad_cells < 8; ++ty)
        {
            for (int tx = 0; tx < og.w && bad_cells < 8; ++tx)
            {
                std::string why;
                if (!cell_equivalent(old_world, new_world, kit, f, tx, ty,
                                     &why))
                {
                    fail(std::format(
                        "scen{} floor {} cell ({}, {}): {} mismatch", id, f,
                        tx, ty, why));
                    ++bad_cells;
                }
            }
        }
    }

    // Door-frame genre audit on the reloaded world (also pinned indirectly
    // by the entity streams' frame field).
    for (auto old_it = old_world.weaplist.begin(),
              new_it = new_world.weaplist.begin();
         old_it != old_world.weaplist.end() &&
         new_it != new_world.weaplist.end();
         ++old_it, ++new_it)
    {
        walker* old_door = old_it->get();
        walker* new_door = new_it->get();
        if (old_door == nullptr || new_door == nullptr ||
            old_door->family() != FAMILY_DOOR)
            continue;
        if (door_frame_wall_above(old_world, old_door) !=
            door_frame_wall_above(new_world, new_door))
        {
            fail(std::format("scen{}: door at ({}, {}) frame genre flipped",
                             id, old_door->xpos() / GRID_SIZE,
                             old_door->ypos() / GRID_SIZE));
        }
    }

    // Footing: every authored entity's spawn footprint passable-equal.
    const struct
    {
        const std::list<std::unique_ptr<walker>>& old_list;
        const std::list<std::unique_ptr<walker>>& new_list;
    } lists[] = {
        {old_world.oblist, new_world.oblist},
        {old_world.fxlist, new_world.fxlist},
        {old_world.weaplist, new_world.weaplist},
    };
    for (const auto& entry : lists)
    {
        auto old_it = entry.old_list.begin();
        auto new_it = entry.new_list.begin();
        for (; old_it != entry.old_list.end() &&
               new_it != entry.new_list.end();
             ++old_it, ++new_it)
        {
            walker* old_ob = old_it->get();
            walker* new_ob = new_it->get();
            if (old_ob == nullptr || new_ob == nullptr)
                continue;
            const bool old_footing = old_world.query_grid_passable(
                static_cast<float>(old_ob->xpos()),
                static_cast<float>(old_ob->ypos()), old_ob, old_ob->floor());
            const bool new_footing = new_world.query_grid_passable(
                static_cast<float>(new_ob->xpos()),
                static_cast<float>(new_ob->ypos()), new_ob, new_ob->floor());
            if (old_footing != new_footing)
            {
                fail(std::format(
                    "scen{}: order {} family {} at tile ({}, {}) footing "
                    "flipped {} -> {}", id,
                    static_cast<int>(old_ob->query_order()),
                    static_cast<int>(old_ob->family()),
                    old_ob->xpos() / GRID_SIZE, old_ob->ypos() / GRID_SIZE,
                    old_footing, new_footing));
            }
        }
    }
}

} // namespace
} // namespace gridmig

int main(int argc, char* argv[])
{
    using namespace gridmig;
    namespace fs = std::filesystem;

    if (argc < 2)
    {
        std::fprintf(stderr,
                     "usage: grid_migrate <campaign_id> [output.glad]\n");
        return 2;
    }
    const std::string campaign_id = argv[1];
    const std::string out_glad =
        (argc > 2) ? argv[2] : ("builtin/" + campaign_id + ".glad");
    const fs::path out_abs = fs::absolute(out_glad);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("grid_migrate_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    cfg.load_settings();
    init_all_registries();

    og::runtime::SessionState& session = og::runtime::s_migrate_session;
    static FixedRandom migrate_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    session.ctx_.rng = &migrate_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    int result = 1;
    do
    {
        if (mount_campaign_package_with_error(campaign_id) !=
            CampaignPackageIoError::None)
        {
            fail(std::format("cannot mount campaign {}", campaign_id));
            break;
        }

        const std::vector<int> ids = list_levels_v();
        if (ids.empty())
        {
            fail(std::format("campaign {} lists no levels", campaign_id));
            break;
        }

        // 1. Snapshot every level in memory (the OLD worlds).
        const LevelDataHooks& hooks = headless_level_data_hooks();
        std::vector<std::unique_ptr<LevelRuntimeData>> old_levels;
        std::set<std::string> grid_names;
        for (const int id : ids)
        {
            auto level =
                std::make_unique<LevelRuntimeData>(id, true, &hooks);
            if (!level->load())
            {
                fail(std::format("scen{} failed to load from input", id));
                continue;
            }
            if (!grid_names.insert(level->grid_file).second)
            {
                fail(std::format("scen{}: grid file '{}' is referenced by "
                                 "two levels — refusing to migrate", id,
                                 level->grid_file));
            }
            old_levels.push_back(std::move(level));
        }
        if (g_errors != 0)
            break;

        ProbeKit kit;
        if (!make_probe_kit(old_levels.front()->world(), kit))
            break;

        // 2. Unpack every zip member (campaign.yaml / icon / unreferenced
        // grids ride through), drop stale decor planes, then re-save every
        // level over the unpacked tree.
        cleanup_unpacked_campaign();
        if (!unpack_campaign(campaign_id))
        {
            fail(std::format("cannot unpack campaign {}", campaign_id));
            break;
        }
        const fs::path temp_pix = fs::path(get_user_path()) / "temp" / "pix";
        std::error_code iter_ec;
        for (const auto& entry : fs::directory_iterator(temp_pix, iter_ec))
        {
            const std::string name = entry.path().filename().string();
            const std::size_t d = name.rfind("_d");
            if (d != std::string::npos && d + 2 < name.size() &&
                std::isdigit(static_cast<unsigned char>(name[d + 2])))
            {
                std::error_code ec;
                fs::remove(entry.path(), ec);
                std::fprintf(stderr,
                             "grid_migrate: dropped stale decor member %s\n",
                             name.c_str());
            }
        }

        std::vector<LevelReport> reports(old_levels.size());
        for (std::size_t i = 0; i < old_levels.size(); ++i)
        {
            const int id = ids[static_cast<std::size_t>(i)];
            LevelRuntimeData work(id, true, &hooks);
            if (!work.load())
            {
                fail(std::format("scen{} failed to re-load for transform",
                                 id));
                continue;
            }
            migrate_world(id, old_levels[i]->world(), work.world(), kit,
                          reports[i]);
            if (!work.save())
            {
                fail(std::format("scen{} failed to save", id));
                continue;
            }
        }
        if (g_errors != 0)
            break;

        // 3. Rezip and remount the produced package.
        if (unmount_campaign_package_with_error(campaign_id) !=
            CampaignPackageIoError::None)
        {
            fail(std::format("cannot unmount {} before repack", campaign_id));
            break;
        }
        if (!repack_campaign(campaign_id))
        {
            fail(std::format("cannot repack campaign {}", campaign_id));
            break;
        }
        cleanup_unpacked_campaign();
        if (mount_campaign_package_with_error(campaign_id) !=
            CampaignPackageIoError::None)
        {
            fail(std::format("cannot mount the produced {}", campaign_id));
            break;
        }

        // 4. The equivalence proof, level by level, plus the report.
        const std::vector<int> new_ids = list_levels_v();
        if (new_ids != ids)
        {
            fail("produced package lists a different level set");
            break;
        }
        for (std::size_t i = 0; i < old_levels.size(); ++i)
        {
            const int id = ids[static_cast<std::size_t>(i)];
            LevelRuntimeData reloaded(id, true, &hooks);
            if (!reloaded.load())
            {
                fail(std::format("scen{} failed to reload from output", id));
                continue;
            }
            check_equivalence(id, old_levels[i]->world(), reloaded.world(),
                              kit);
            const LevelReport& r = reports[i];
            std::printf(
                "grid_migrate: %s scen%d: %d cells migrated, %d reverted; "
                "outdoor vote %d/%d -> %d/%d%s\n",
                campaign_id.c_str(), id, r.migrated_cells, r.reverted_cells,
                r.outdoor_old, r.total_cells, r.outdoor_new, r.total_cells,
                r.outdoor_flip ? " [VERDICT FLIPPED]" : "");
        }
        if (g_errors != 0)
            break;

        // 5. Publish.
        std::error_code ec;
        fs::create_directories(out_abs.parent_path(), ec);
        fs::copy_file(get_user_path() + "campaigns/" + campaign_id + ".glad",
                      out_abs, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            fail(std::format("failed to copy output to {}: {}",
                             out_abs.string(), ec.message()));
            break;
        }
        std::printf("grid_migrate: wrote %s\n", out_abs.c_str());
        result = 0;
    } while (false);

    cleanup_unpacked_campaign();
    io_exit();
    if (!scratch.empty())
    {
        std::error_code ec;
        fs::remove_all(scratch, ec);
    }
    if (result != 0)
        std::fprintf(stderr, "grid_migrate: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
