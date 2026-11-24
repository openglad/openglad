/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// What a bot can get at (reach.h): the maps of where each kind of fighter can stand, with the doors
// that bar it shut or with every door open, and the questions read from them. Nothing here draws,
// moves, eats, opens or files anything: every function is a question of the world as it stands.
//
// A kind is what terrain and doors answer of an asker: its box, its order, four flags (ethereal,
// forest-walk, walker::flies, swimming) and the set of live doors that bar it. The terrain half of a
// map (a Ground) is per box, order and flags; the door half (a Map) per kind. Both are built on
// first need and dropped when a writer bumps a count they were built from (Maps, reach.h). Only
// equality of region numbers is ever read, so the numbering order is free.
//
// An asker is resolved to its kind once (Asker): its barring set (by its colour, keys and life),
// its terrain half and its door half looked up in the indexes, its start cell worked out; every
// question of that Asker reads them again only after what they were made from changed. The
// lookups copy nothing.

#include <openglad/gameplay/reach.h>

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace og::reach {

namespace detail {

// A live door filed in the body map, as a door question reads it.
struct DoorRec
{
    const walker* door = nullptr;
    short x = 0;
    short y = 0;
    short sx = 0;
    short sy = 0;
    int floor = 0;           // the floor the body map files it under (obmap::filed_floor)
    unsigned char team = 0;  // the colour of its alliance head (walker::sides_friendly reads it)
    std::int32_t level = 0;  // the key it wants
    bool operator==(const DoorRec&) const = default;
};

// One mode's standing cells and regions for one kind.
struct Layer
{
    int w = 0;
    int h = 0;
    int floors = 1;
    std::vector<std::uint8_t> bits;      // per cell: kStep, kFlank
    std::vector<std::int32_t> region;    // per cell: its region, -1 when it is not a standing cell
    int regions = 0;
    std::vector<std::vector<std::int32_t>> links;  // per region: the stair landings of its cells
    bool any_link = false;
    // Per region, filled on first need: the regions it gets to and the cells that are no standing
    // cell but are reached (a stair's landing on air), sorted.
    std::vector<std::vector<std::uint8_t>> gets;
    std::vector<std::vector<std::int32_t>> gets_cells;

    [[nodiscard]] int index(int x, int y, int f) const { return (f * h + y) * w + x; }
    [[nodiscard]] bool inside(int x, int y, int f) const
    {
        return x >= 0 && y >= 0 && f >= 0 && x < w && y < h && f < floors;
    }
};

// A layer read as for_each_step's Cells: the one reading of a stored layer (stored_passes).
struct LayerCells
{
    const Layer* layer = nullptr;
    bool operator()(int x, int y, int f, StepTest test) const
    {
        return stored_passes(layer->bits.data(), layer->w, layer->h, layer->floors, x, y, f, test);
    }
};

struct GroundKey
{
    short sx = 0;
    short sy = 0;
    std::uint8_t order = 0;
    std::uint8_t flags = 0;  // kEthereal, kForestwalk, kFlies, kSwimming
    auto tie() const { return std::tie(sx, sy, order, flags); }
    bool operator<(const GroundKey& o) const { return tie() < o.tie(); }
};

constexpr std::uint8_t kEthereal = 1;
constexpr std::uint8_t kForestwalk = 2;
constexpr std::uint8_t kFlies = 4;
constexpr std::uint8_t kSwimming = 8;

struct Ground
{
    GroundKey key;
    Layer open;
};

// The listed doors (indices into MapsData::doors) that bar an asker, kept once for every asker
// whose doors are the same: they share its id, and so one door half.
struct BarringSet
{
    int id = 0;
    std::vector<std::uint16_t> doors;
};

struct Map
{
    Ground* ground = nullptr;
    const BarringSet* barring = nullptr;
    Layer shut;                             // empty when no door bars the kind: the open layer
    std::vector<std::vector<Cell>> closed;  // per barring door (its place in barring): the cells it closes
    // The door walk's facts, per barring door: the shut regions it touches and the barring doors it
    // touches (their places in barring), sorted; per shut region: the barring doors beside it.
    std::vector<std::vector<std::int32_t>> door_regions;
    std::vector<std::vector<std::uint16_t>> door_doors;
    std::vector<std::vector<std::uint16_t>> region_doors;
};

// What an asker's barring set depends on: its alliance head's colour, the keys it holds (its own
// or the keys a question names) and its life (walker::is_friendly answers no to a dead walker).
struct BarringKey
{
    unsigned char team = 0;
    std::uint32_t keys = 0;
    bool dead = false;
    auto tie() const { return std::tie(team, keys, dead); }
    bool operator<(const BarringKey& o) const { return tie() < o.tie(); }
};

// A walk answer of the tick's memo: the layer, the start region and the body's box and floor are
// all it reads.
struct WalkKey
{
    const Layer* layer = nullptr;
    std::int32_t start = 0;
    short x = 0;
    short y = 0;
    short sx = 0;
    short sy = 0;
    int floor = 0;
    bool operator==(const WalkKey&) const = default;
};

struct WalkKeyHash
{
    std::size_t operator()(const WalkKey& k) const noexcept
    {
        std::size_t h = std::hash<const void*>()(k.layer);
        const auto mix = [&h](std::uint64_t v) { h ^= static_cast<std::size_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(static_cast<std::uint32_t>(k.start));
        mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(k.x)) << 16) | static_cast<std::uint16_t>(k.y));
        mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(k.sx)) << 16) | static_cast<std::uint16_t>(k.sy));
        mix(static_cast<std::uint32_t>(k.floor));
        return h;
    }
};

}  // namespace detail

struct MapsData
{
    // What the maps were built from.
    std::uint64_t terrain_seen = ~std::uint64_t{0};
    std::uint64_t doors_seen = ~std::uint64_t{0};
    const obmap* obmap_seen = nullptr;
    std::uint64_t obmap_writes_seen = ~std::uint64_t{0};
    // Moved on whenever maps or barring sets are dropped: an Asker resolved before resolves again.
    std::uint64_t generation = 0;

    std::vector<detail::DoorRec> doors;
    std::vector<std::unique_ptr<detail::Ground>> grounds;
    std::vector<std::unique_ptr<detail::Map>> maps;
    std::vector<std::unique_ptr<detail::BarringSet>> barring_sets;
    std::map<detail::GroundKey, detail::Ground*> ground_index;
    std::map<std::pair<const detail::Ground*, int>, detail::Map*> map_index;  // (terrain half, barring set id)
    std::map<std::vector<std::uint16_t>, detail::BarringSet*> barring_ids;
    std::map<detail::BarringKey, const detail::BarringSet*> barring_memo;

    // The tick's memo of walk answers.
    bool memo_valid = false;
    std::uint32_t memo_tick = 0;
    std::unordered_map<detail::WalkKey, bool, detail::WalkKeyHash> walk_memo;

    std::uint64_t builds = 0;
    std::uint64_t memo_hits = 0;
    std::uint64_t resolutions = 0;

    // The test build checks the doors the maps were built from against a fresh list of the doors
    // filed in the body map, at the first question of every tick.
    bool checked = false;
    std::uint32_t checked_tick = 0;
};

Maps::Maps() : data_(std::make_unique<MapsData>()) {}
Maps::~Maps() = default;

std::uint64_t Maps::builds() const noexcept
{
    return data_->builds;
}

std::uint64_t Maps::memo_hits() const noexcept
{
    return data_->memo_hits;
}

std::uint64_t Maps::resolutions() const noexcept
{
    return data_->resolutions;
}

namespace detail {

// The floor index a layer stores `floor` under, or -1: the floor the body map files a body of that
// floor under (obmap::filed_floor), which a single-floor level holds only as floor 0.
int layer_floor(const Layer& layer, int floor)
{
    const int f = obmap::filed_floor(floor);
    // plumbing: a single-floor level stores floor 0 alone; a band past the floors is not stored
    if (layer.floors == 1)
        return f == 0 ? 0 : -1;
    return f < layer.floors ? f : -1;
}

// The live doors filed in the body map, in list order (the weapons first), as a door question
// reads them.
std::vector<DoorRec> filed_doors(GameWorld& world)
{
    std::vector<DoorRec> out;
    obmap* map = world.myobmap.get();
    // plumbing: no body map files no door
    if (map == nullptr)
        return out;
    const auto take = [&](const GameWorld::EntityList& list) {
        // plumbing: walk one list; the door test is below
        for (const auto& up : list)
        {
            const walker* d = up.get();
            // rule: og::reach: a door question lists the doors filed in the body map: a door filed
            //   nowhere (dormant, hidden, placed while ignored) closes no cell. A dead door is left
            //   off the list too (door_bars bars no dead door).
            // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
            if (d == nullptr || !d->is_door() || d->dead() ||
                map->walker_to_pos.find(const_cast<walker*>(d)) == map->walker_to_pos.end())
                continue;
            DoorRec r;
            r.door = d;
            r.x = d->xpos();
            r.y = d->ypos();
            r.sx = d->sizex();
            r.sy = d->sizey();
            r.floor = obmap::filed_floor(d->floor());
            r.team = d->alliance_head()->team_num();
            r.level = d->stats()->level();
            out.push_back(r);
        }
    };
    take(world.weaplist);
    take(world.oblist);
    take(world.fxlist);
    return out;
}

// The maps, current: the counts they were built from compared with the writers' counts, the
// stale halves dropped, the tick's memo started over on a new tick.
MapsData& current(GameWorld& world)
{
    Maps& maps = world.reach_maps;
    MapsData& d = maps.data();
    obmap* map = world.myobmap.get();
    // plumbing: a world with no body map files no door (the count reads 0)
    const std::uint64_t writes = map != nullptr ? map->door_writes() : 0;
    // rule: og::reach: a terrain writer (Maps::note_terrain) makes every map stale: the terrain
    //   halves, the door halves and the memos are dropped and built again on need. The blast's
    //   charred grass (GameWorld::damage_tile) is no writer: plain grass and charred grass are both
    //   ground every kind stands on, so the maps answer it alike.
    // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
    if (maps.terrain_count() != d.terrain_seen)
    {
        d.grounds.clear();
        d.ground_index.clear();
        d.maps.clear();
        d.map_index.clear();
        d.walk_memo.clear();
        d.terrain_seen = maps.terrain_count();
        ++d.generation;
    }
    bool remade = false;
    // rule: og::reach: a door writer (the body map's door count, a door's dead, team and level
    //   setters) makes the door halves stale: the doors are listed again, the barring sets, the
    //   door halves and the memos dropped; the terrain halves stay.
    // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
    if (maps.door_count() != d.doors_seen || map != d.obmap_seen || writes != d.obmap_writes_seen)
    {
        remade = true;
        d.doors = filed_doors(world);
        d.maps.clear();
        d.map_index.clear();
        d.barring_sets.clear();
        d.barring_ids.clear();
        d.barring_memo.clear();
        d.walk_memo.clear();
        d.doors_seen = maps.door_count();
        d.obmap_seen = map;
        d.obmap_writes_seen = writes;
        d.checked = true;
        d.checked_tick = world.tick_count_;
        ++d.generation;
    }
#if defined(TESTING) || defined(OPENGLAD_TEST_SUPPORT)
    // rule: og::reach, the test build: at the first question of every tick the doors the maps were
    //   built from are checked against a fresh list of the doors filed in the body map; a door that
    //   changed while no writer bumped a count is a writer this list forgot, and stops the test.
    // tests: SemReach.a_door_changed_behind_the_writers_back_stops_the_test_build
    if (!remade && (!d.checked || d.checked_tick != world.tick_count_))
    {
        d.checked = true;
        d.checked_tick = world.tick_count_;
        // rule: og::reach, the test build: the forgotten writer stops the test (see above).
        // tests: SemReach.a_door_changed_behind_the_writers_back_stops_the_test_build
        if (filed_doors(world) != d.doors)
        {
            LogError("og::reach: a door changed and no writer bumped a door count "
                     "(a writer the maps were not told about)\n");
            std::abort();
        }
    }
#else
    (void)remade;
#endif
    // rule: og::reach: the walk answers of the memo are kept for one tick (and only while every
    //   map they were read from stands).
    // tests: SemReach.twelve_askers_of_one_kind_in_one_room_are_answered_once_a_tick
    if (!d.memo_valid || d.memo_tick != world.tick_count_)
    {
        d.walk_memo.clear();
        d.memo_valid = true;
        d.memo_tick = world.tick_count_;
    }
    return d;
}

GroundKey ground_key_of(const Resolution& r)
{
    GroundKey k;
    k.sx = r.sx;
    k.sy = r.sy;
    k.order = r.order;
    k.flags = r.flags;
    return k;
}

// Was `a` resolved from the same world, maps and walker state as `b` describes?
bool same_source(const Resolution& a, const Resolution& b)
{
    return a.world == b.world && a.generation == b.generation && a.x == b.x && a.y == b.y &&
           a.floor == b.floor && a.sx == b.sx && a.sy == b.sy && a.order == b.order &&
           a.flags == b.flags && a.team == b.team && a.dead == b.dead && a.keys == b.keys;
}

// The doors that bar w holding `key.keys` (indices into d.doors), by door_bars; the same for every
// asker of the same colour, keys and life, so it is kept until the doors change.
const BarringSet& barring_of(MapsData& d, walker& w, const BarringKey& key)
{
    auto found = d.barring_memo.find(key);
    // plumbing: the barring set of this colour, keys and life is known
    if (found != d.barring_memo.end())
        return *found->second;
    std::vector<std::uint16_t> doors;
    // plumbing: ask door_bars of every listed door
    for (std::size_t i = 0; i < d.doors.size(); ++i)
    {
        // rule: og::reach: a kind's barring set is the listed doors door_bars says bar its asker,
        //   holding the keys the question names (the asker's own, or the keys given: none for the
        //   keyless kind).
        // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
        // tests: SemReach.a_walker_holding_no_key_is_asked_as_a_kind_of_its_own
        if (door_bars(&w, d.doors[i].door, key.keys))
            doors.push_back(static_cast<std::uint16_t>(i));
    }
    const BarringSet* set = nullptr;
    auto interned = d.barring_ids.find(doors);
    // plumbing: askers barred by the same doors share one set (and so one door half)
    if (interned != d.barring_ids.end())
    {
        set = interned->second;
    }
    else
    {
        auto made = std::make_unique<BarringSet>();
        made->id = static_cast<int>(d.barring_sets.size());
        made->doors = doors;
        set = made.get();
        d.barring_ids.emplace(std::move(doors), made.get());
        d.barring_sets.push_back(std::move(made));
    }
    d.barring_memo.emplace(key, set);
    return *set;
}

// The regions of a layer whose bits are set: one pass of the route finder's own step loop
// (for_each_step) from every standing cell not yet numbered; the stair hops are kept as links.
void number_regions(GameWorld& world, walker& w, Layer& layer)
{
    const std::size_t n = layer.bits.size();
    layer.region.assign(n, -1);
    layer.regions = 0;
    layer.links.clear();
    layer.any_link = false;
    layer.gets.clear();
    layer.gets_cells.clear();
    const bool multifloor = world.floor_count() > 1;
    const bool nonflyer = !w.flies();
    const LayerCells cells{&layer};
    std::vector<std::int32_t> queue;
    // plumbing: number every standing cell not yet numbered
    for (std::size_t i = 0; i < n; ++i)
    {
        // plumbing: a cell that does not stand, or is numbered already, starts no region
        if ((layer.bits[i] & kStep) == 0 || layer.region[i] >= 0)
            continue;
        const std::int32_t r = layer.regions++;
        layer.links.emplace_back();
        layer.region[i] = r;
        queue.assign(1, static_cast<std::int32_t>(i));
        // plumbing: a breadth-first fill over the route finder's steps
        while (!queue.empty())
        {
            const std::int32_t u = queue.back();
            queue.pop_back();
            const int uf = u / (layer.w * layer.h);
            const int uy = (u / layer.w) % layer.h;
            const int ux = u % layer.w;
            const auto visit = [&](int ax, int ay, int af, StepKind kind) {
                const int cx = ax / GRID_SIZE;
                const int cy = ay / GRID_SIZE;
                // plumbing: for_each_step answers only cells the layer holds
                if (!layer.inside(cx, cy, af))
                    return;
                const std::int32_t ni = layer.index(cx, cy, af);
                // rule: og::reach: a region is the standing cells joined by the route finder's
                //   steps on one floor; a stair's hop is a one-way link to its landing.
                // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
                if (kind == StepKind::Stair)
                {
                    layer.links[static_cast<std::size_t>(r)].push_back(ni);
                    layer.any_link = true;
                    return;
                }
                // plumbing: numbered already
                if (layer.region[static_cast<std::size_t>(ni)] >= 0)
                    return;
                layer.region[static_cast<std::size_t>(ni)] = r;
                queue.push_back(ni);
            };
            for_each_step(world, ux * GRID_SIZE, uy * GRID_SIZE, uf, multifloor, nonflyer, cells, visit);
        }
    }
}

void size_layer(GameWorld& world, Layer& layer)
{
    // plumbing: a single-floor level is one floor whatever its walkers' floor fields say
    layer.floors = world.floor_count() > 1 ? world.floor_count() : 1;
    layer.w = 0;
    layer.h = 0;
    // plumbing: every floor shares the level's footprint; the widest grid is taken
    for (int f = 0; f < layer.floors; ++f)
    {
        layer.w = std::max(layer.w, static_cast<int>(world.grid_for_floor(f).w));
        layer.h = std::max(layer.h, static_cast<int>(world.grid_for_floor(f).h));
    }
}

Ground& ground_of(MapsData& d, GameWorld& world, walker& w, const GroundKey& key)
{
    auto found = d.ground_index.find(key);
    // plumbing: this box, order and flags have their terrain half
    if (found != d.ground_index.end())
        return *found->second;
    auto g = std::make_unique<Ground>();
    g->key = key;
    Layer& layer = g->open;
    size_layer(world, layer);
    layer.bits.assign(static_cast<std::size_t>(layer.w) * static_cast<std::size_t>(layer.h) *
                          static_cast<std::size_t>(layer.floors),
                      0);
    const bool multifloor = world.floor_count() > 1;
    const bool nonflyer = !w.flies();
    // plumbing: every cell's two terrain answers, by the route finder's own test (terrain_passes)
    for (int f = 0; f < layer.floors; ++f)
    {
        // plumbing: (the same walk)
        for (int y = 0; y < layer.h; ++y)
        {
            // plumbing: (the same walk)
            for (int x = 0; x < layer.w; ++x)
            {
                std::uint8_t b = 0;
                // rule: og::reach: a standing cell of a kind is one whose step test the route finder
                //   passes (terrain_passes, Step); its flank bit is the diagonal's flank test.
                // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
                if (terrain_passes(world, w, x * GRID_SIZE, y * GRID_SIZE, f, StepTest::Step, multifloor, nonflyer))
                    b = static_cast<std::uint8_t>(b | kStep);
                // rule: og::reach: (the flank bit, the same rule)
                // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
                if (terrain_passes(world, w, x * GRID_SIZE, y * GRID_SIZE, f, StepTest::Flank, multifloor, nonflyer))
                    b = static_cast<std::uint8_t>(b | kFlank);
                // rule: og::reach: (the landing bit, the same rule: a stair's hop may land on the
                //   cell)
                // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
                // tests: SemBotReach.a_stair_whose_landing_a_door_closes_is_no_way_up
                if (terrain_passes(world, w, x * GRID_SIZE, y * GRID_SIZE, f, StepTest::Landing, multifloor, nonflyer))
                    b = static_cast<std::uint8_t>(b | kLanding);
                layer.bits[static_cast<std::size_t>(layer.index(x, y, f))] = b;
            }
        }
    }
    number_regions(world, w, layer);
    ++d.builds;
    Ground* raw = g.get();
    d.grounds.push_back(std::move(g));
    d.ground_index.emplace(key, raw);
    return *raw;
}

// The door walk's facts of a door half (reach.h, Asker::regions_touched, doors_touched,
// doors_beside), from the cells each barring door closes and the shut layer's regions.
void note_touches(Map& m)
{
    const Layer& shut = m.shut;
    const std::size_t n = m.barring->doors.size();
    m.door_regions.assign(n, {});
    m.door_doors.assign(n, {});
    m.region_doors.assign(static_cast<std::size_t>(shut.regions), {});
    std::vector<std::pair<std::int32_t, std::uint16_t>> closers;  // (cell index, barring door), sorted
    // plumbing: every closed cell of every barring door
    for (std::size_t k = 0; k < n; ++k)
    {
        // plumbing: (the same walk)
        for (const Cell& c : m.closed[k])
            closers.emplace_back(shut.index(c.x, c.y, c.floor), static_cast<std::uint16_t>(k));
    }
    std::sort(closers.begin(), closers.end());
    constexpr int kAround[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};  // the cell, its 4-neighbours
    // plumbing: each barring door's closed cells and their 4-neighbours
    for (std::size_t k = 0; k < n; ++k)
    {
        // plumbing: (the same walk)
        for (const Cell& c : m.closed[k])
        {
            // plumbing: (the same walk)
            for (const auto& off : kAround)
            {
                const int nx = c.x + off[0];
                const int ny = c.y + off[1];
                // plumbing: a neighbour off the layer touches nothing
                if (!shut.inside(nx, ny, c.floor))
                    continue;
                const std::int32_t ni = shut.index(nx, ny, c.floor);
                const std::int32_t r = shut.region[static_cast<std::size_t>(ni)];
                // rule: og::reach: a barring door touches a region of the shut layer when one of the
                //   cells it closes has a 4-neighbour standing cell (shut) in that region (the
                //   closed cell itself stands in none).
                // tests: SemReach.the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches
                if (r >= 0)
                    m.door_regions[k].push_back(r);
                auto at = std::lower_bound(closers.begin(), closers.end(), std::make_pair(ni, std::uint16_t{0}));
                // rule: og::reach: a barring door touches another when one of its closed cells is, or
                //   is a 4-neighbour of, one of the other's closed cells (two doors in a row with no
                //   floor between them); a door does not touch itself.
                // tests: SemReach.the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches
                for (; at != closers.end() && at->first == ni; ++at)
                {
                    // rule: og::reach: (the same rule: not itself)
                    // tests: SemReach.the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches
                    if (at->second != k)
                        m.door_doors[k].push_back(at->second);
                }
            }
        }
        std::sort(m.door_regions[k].begin(), m.door_regions[k].end());
        m.door_regions[k].erase(std::unique(m.door_regions[k].begin(), m.door_regions[k].end()), m.door_regions[k].end());
        std::sort(m.door_doors[k].begin(), m.door_doors[k].end());
        m.door_doors[k].erase(std::unique(m.door_doors[k].begin(), m.door_doors[k].end()), m.door_doors[k].end());
        // rule: og::reach: per region of the shut layer, the barring doors beside it are the doors
        //   that touch it, in the barring order (each door is visited once, in that order).
        // tests: SemReach.the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches
        for (const std::int32_t r : m.door_regions[k])
            m.region_doors[static_cast<std::size_t>(r)].push_back(static_cast<std::uint16_t>(k));
    }
}

// A kind's map: the terrain half and, when doors bar the kind, the door half (the terrain's
// standing cells minus the cells a barring door's box meets), looked up by the terrain half and the
// barring set's id.
Map& map_of(MapsData& d, GameWorld& world, walker& w, Ground& ground, const BarringSet& barring)
{
    const std::pair<const Ground*, int> key{&ground, barring.id};
    auto found = d.map_index.find(key);
    // plumbing: this kind has its map
    if (found != d.map_index.end())
        return *found->second;
    auto m = std::make_unique<Map>();
    m->ground = &ground;
    m->barring = &barring;
    m->closed.resize(barring.doors.size());
    // rule: og::reach: a kind no door bars reads the terrain half alone (no door half is built).
    // tests: SemReach.with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route
    if (!barring.doors.empty())
    {
        const Layer& open = ground.open;
        Layer& shut = m->shut;
        shut.w = open.w;
        shut.h = open.h;
        shut.floors = open.floors;
        shut.bits = open.bits;
        const int sx = ground.key.sx;
        const int sy = ground.key.sy;
        // plumbing: each barring door closes the cells its box meets
        for (std::size_t k = 0; k < barring.doors.size(); ++k)
        {
            const DoorRec& door = d.doors[barring.doors[k]];
            const int f = layer_floor(open, door.floor);
            // plumbing: a door on a floor the level does not hold closes nothing
            if (f < 0)
                continue;
            const int x0 = std::max(0, (door.x - sx) / GRID_SIZE - 1);
            const int x1 = std::min(open.w - 1, (door.x + door.sx) / GRID_SIZE + 1);
            const int y0 = std::max(0, (door.y - sy) / GRID_SIZE - 1);
            const int y1 = std::min(open.h - 1, (door.y + door.sy) / GRID_SIZE + 1);
            // plumbing: the cells the door's box could meet, row by row
            for (int y = y0; y <= y1; ++y)
            {
                // plumbing: (the same walk)
                for (int x = x0; x <= x1; ++x)
                {
                    const std::size_t i = static_cast<std::size_t>(open.index(x, y, f));
                    // rule: og::reach: a barring door closes the cells that stand with the doors
                    //   open and on which the kind's box, its corner on the cell's corner, meets the
                    //   door's box by the body map's own test (collide; the height test never spares
                    //   a living, whose height is 0); closed, a cell answers none of its tests (a
                    //   stair's hop does not land on it either).
                    // tests: SemReach.a_door_off_the_grid_closes_exactly_the_cells_its_box_meets
                    // tests: SemBotReach.a_stair_whose_landing_a_door_closes_is_no_way_up
                    if ((open.bits[i] & kStep) == 0 ||
                        !collide(static_cast<short>(x * GRID_SIZE), static_cast<short>(y * GRID_SIZE),
                                 static_cast<short>(sx), static_cast<short>(sy),
                                 door.x, door.y, door.sx, door.sy))
                        continue;
                    m->closed[k].push_back(Cell{x, y, f});
                    shut.bits[i] = 0;
                }
            }
        }
        number_regions(world, w, shut);
        note_touches(*m);
        ++d.builds;
    }
    Map* raw = m.get();
    d.maps.push_back(std::move(m));
    d.map_index.emplace(key, raw);
    return *raw;
}

Layer& layer_of(Map& map, Mode mode)
{
    // plumbing: the open layer is the terrain half's; the shut one the door half's, or the terrain
    //   half's when no door bars the kind
    if (mode == Mode::Open || map.barring->doors.empty())
        return map.ground->open;
    return map.shut;
}

int region_at(const Layer& layer, Cell c)
{
    const int f = layer_floor(layer, c.floor);
    // plumbing: a cell off the layer has no region
    if (f < 0 || !layer.inside(c.x, c.y, f))
        return -1;
    return layer.region[static_cast<std::size_t>(layer.index(c.x, c.y, f))];
}

// What the start state reaches, into (regions, cells): breadth first over the regions (their stair
// links) and over the cells that are no standing cell (their own steps, as the route finder expands
// any state it reaches: a start on air or inside terrain, a stair's landing on air).
void reach_from(GameWorld& world, walker& w, const Layer& layer, std::int32_t start,
                std::vector<std::uint8_t>& regions, std::vector<std::int32_t>& cells)
{
    regions.assign(static_cast<std::size_t>(layer.regions), 0);
    cells.clear();
    std::vector<std::int32_t> region_queue;
    std::vector<std::int32_t> cell_queue;
    std::unordered_set<std::int32_t> seen;
    const auto visit = [&](std::int32_t i) {
        const std::int32_t r = layer.region[static_cast<std::size_t>(i)];
        // rule: og::reach: a reached standing cell brings in its whole region; a reached cell that
        //   is no standing cell is kept and expanded by its own steps.
        // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
        if (r >= 0)
        {
            // plumbing: a region is brought in once
            if (regions[static_cast<std::size_t>(r)] == 0)
            {
                regions[static_cast<std::size_t>(r)] = 1;
                region_queue.push_back(r);
            }
        }
        else if (seen.insert(i).second)
        {
            cells.push_back(i);
            cell_queue.push_back(i);
        }
    };
    visit(start);
    const bool multifloor = world.floor_count() > 1;
    const bool nonflyer = !w.flies();
    const LayerCells bits{&layer};
    const auto step = [&](int ax, int ay, int af, StepKind) {
        const int cx = ax / GRID_SIZE;
        const int cy = ay / GRID_SIZE;
        // plumbing: for_each_step answers only cells the layer holds
        if (layer.inside(cx, cy, af))
            visit(layer.index(cx, cy, af));
    };
    // plumbing: breadth first over the two queues
    while (!region_queue.empty() || !cell_queue.empty())
    {
        // plumbing: a region's links first, then a cell's own steps
        if (!region_queue.empty())
        {
            const std::int32_t r = region_queue.back();
            region_queue.pop_back();
            // plumbing: every landing of the region's stairs is reached
            for (const std::int32_t landing : layer.links[static_cast<std::size_t>(r)])
                visit(landing);
            continue;
        }
        const std::int32_t c = cell_queue.back();
        cell_queue.pop_back();
        const int cf = c / (layer.w * layer.h);
        const int cy = (c / layer.w) % layer.h;
        const int cx = c % layer.w;
        for_each_step(world, cx * GRID_SIZE, cy * GRID_SIZE, cf, multifloor, nonflyer, bits, step);
    }
    std::sort(cells.begin(), cells.end());
}

// Does the start cell reach the cell `goal` (an index into the layer)?
bool reaches_index(GameWorld& world, walker& w, Layer& layer, std::int32_t start, std::int32_t goal)
{
    // plumbing: the same cell needs no walk (reach_from would answer the same: it keeps its start)
    if (start == goal)
        return true;
    const std::int32_t rs = layer.region[static_cast<std::size_t>(start)];
    const std::int32_t rg = layer.region[static_cast<std::size_t>(goal)];
    // rule: og::reach: on a layer with no stair, a standing start gets to its own region alone.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    // tests: SemReach.twelve_askers_of_one_kind_in_one_room_are_answered_once_a_tick
    if (rs >= 0 && !layer.any_link)
        return rg == rs;
    // plumbing: a standing start's reach is kept per region, filled on first need (a cost, not a
    //   rule: a start that is no standing cell is reached afresh, with the same answer)
    if (rs >= 0)
    {
        // plumbing: size the per-region slots once
        if (layer.gets.size() != static_cast<std::size_t>(layer.regions))
        {
            layer.gets.assign(static_cast<std::size_t>(layer.regions), {});
            layer.gets_cells.assign(static_cast<std::size_t>(layer.regions), {});
        }
        std::vector<std::uint8_t>& regions = layer.gets[static_cast<std::size_t>(rs)];
        std::vector<std::int32_t>& cells = layer.gets_cells[static_cast<std::size_t>(rs)];
        // plumbing: fill this region's reach on first need
        if (regions.empty())
            reach_from(world, w, layer, start, regions, cells);
        // plumbing: a standing goal is asked by region, any other by cell
        if (rg >= 0)
            return regions[static_cast<std::size_t>(rg)] != 0;
        return std::binary_search(cells.begin(), cells.end(), goal);
    }
    std::vector<std::uint8_t> regions;
    std::vector<std::int32_t> cells;
    reach_from(world, w, layer, start, regions, cells);
    // plumbing: a standing goal is asked by region, any other by cell
    if (rg >= 0)
        return regions[static_cast<std::size_t>(rg)] != 0;
    return std::binary_search(cells.begin(), cells.end(), goal);
}

// Each approach cell of (w, b) in `layer`, row by row, left to right, handed to `take(cell, index)`;
// the walk stops when `take` answers true. Nothing is collected.
template <class Take>
void for_each_approach(const Layer& layer, const walker& w, const walker& b, Take&& take)
{
    const int f = layer_floor(layer, b.floor());
    // plumbing: a body on a floor the level does not hold has no approach cell
    if (f < 0)
        return;
    const int sx = w.sizex();
    const int sy = w.sizey();
    const int x0 = std::max(0, (b.xpos() - sx - 2 * GRID_SIZE) / GRID_SIZE - 1);
    const int x1 = std::min(layer.w - 1, (b.xpos() + b.sizex() + 2 * GRID_SIZE) / GRID_SIZE + 1);
    const int y0 = std::max(0, (b.ypos() - sy - 2 * GRID_SIZE) / GRID_SIZE - 1);
    const int y1 = std::min(layer.h - 1, (b.ypos() + b.sizey() + 2 * GRID_SIZE) / GRID_SIZE + 1);
    // plumbing: the cells whose grown box could meet the body, row by row
    for (int y = y0; y <= y1; ++y)
    {
        // plumbing: (the same walk)
        for (int x = x0; x <= x1; ++x)
        {
            const std::int32_t i = layer.index(x, y, f);
            // rule: og::reach: an approach cell of (w, b) is a standing cell of the mode on which
            //   w's box, grown by one cell on every side, meets b's box (collide).
            // tests: SemReach.approach_cells_ring_a_tent_a_big_asker_and_a_two_door_doorway
            if ((layer.bits[static_cast<std::size_t>(i)] & kStep) == 0 ||
                !collide(static_cast<short>(x * GRID_SIZE - GRID_SIZE), static_cast<short>(y * GRID_SIZE - GRID_SIZE),
                         static_cast<short>(sx + 2 * GRID_SIZE), static_cast<short>(sy + 2 * GRID_SIZE),
                         b.xpos(), b.ypos(), b.sizex(), b.sizey()))
                continue;
            // plumbing: the caller may stop at this cell
            if (take(Cell{x, y, f}, i))
                return;
        }
    }
}

// Does w, starting at `start` (an index into `layer`, or -1) in region `rs`, walk to b in that
// layer: some approach cell lies in a region the start gets to? Kept in the tick's memo.
bool walks_in(MapsData& d, GameWorld& world, walker& w, Layer& layer, std::int32_t start, std::int32_t rs,
              const walker& b)
{
    // plumbing: a start off the layer reaches nothing
    if (start < 0)
        return false;
    detail::WalkKey key;
    key.layer = &layer;
    key.start = rs;
    key.x = b.xpos();
    key.y = b.ypos();
    key.sx = b.sizex();
    key.sy = b.sizey();
    key.floor = b.floor();
    // rule: og::reach: a walk answer of the tick's memo is given again for the same layer, start
    //   region and body box (only a standing start is kept).
    // tests: SemReach.twelve_askers_of_one_kind_in_one_room_are_answered_once_a_tick
    if (rs >= 0)
    {
        const auto hit = d.walk_memo.find(key);
        // plumbing: the memo holds it
        if (hit != d.walk_memo.end())
        {
            ++d.memo_hits;
            return hit->second;
        }
    }
    bool answer = false;
    for_each_approach(layer, w, b, [&](Cell, std::int32_t i) {
        answer = reaches_index(world, w, layer, start, i);
        return answer;  // the first one reached ends the walk
    });
    // plumbing: only a standing start's answer is kept
    if (rs >= 0)
        d.walk_memo.emplace(key, answer);
    return answer;
}

bool judged(walker& w)
{
    // rule: og::reach: nothing is judged (and no map built) with the setting off, with no world or
    //   no body map, or for an asker that is not a living.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    return og::sim::session_gameplay_fixes() != 0 && current_game != nullptr &&
           current_game->world != nullptr && current_game->world->myobmap != nullptr &&
           w.query_order() == Order::Living;
}

// The place of `door` among the map's barring doors, or -1.
int barring_place(const MapsData& d, const Map& map, const walker& door)
{
    // plumbing: find the door among the kind's barring doors
    for (std::size_t k = 0; k < map.barring->doors.size(); ++k)
    {
        // plumbing: the listed door it is
        if (d.doors[map.barring->doors[k]].door == &door)
            return static_cast<int>(k);
    }
    return -1;
}

}  // namespace detail

using namespace detail;

bool asks(const walker* w)
{
    // rule: og::reach: a walker is asked at the sites of the rules for an enemy behind a locked
    //   door, a key holder at its door and a door broken down as a last resort only with the
    //   setting on, and only a bot (walker::is_bot) that is not stationary (a tower) and is not a
    //   guard at its post.
    // tests: SemReach.a_bot_is_a_living_no_seat_drives_and_only_a_moving_bot_is_asked
    return w != nullptr && og::sim::session_gameplay_fixes() != 0 && w->is_bot() && !w->stationary() &&
           w->act_type() != ACT_GUARD;
}

// --------------------------------------------------------------------------------------------
// Asker
// --------------------------------------------------------------------------------------------

bool Asker::fresh()
{
    walker& w = *w_;
    // plumbing: nothing judged (judged's rule) leaves the resolution as it is
    if (!judged(w))
        return false;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    Resolution now;
    now.made = true;
    now.world = &world;
    now.generation = d.generation;
    now.x = w.xpos();
    now.y = w.ypos();
    now.floor = w.floor();
    now.sx = w.sizex();
    now.sy = w.sizey();
    now.order = static_cast<std::uint8_t>(w.query_order());
    now.team = w.alliance_head()->team_num();
    now.dead = w.dead() != 0;
    // plumbing: the four flags of a kind, one bit each; the keys given, else the walker's own
    now.flags = static_cast<std::uint8_t>(
        (w.stats()->query_bit_flags(BIT_ETHEREAL) != 0 ? kEthereal : 0) |
        (w.stats()->query_bit_flags(BIT_FORESTWALK) != 0 ? kForestwalk : 0) |
        (w.flies() ? kFlies : 0) |
        (w.stats()->query_bit_flags(BIT_SWIMMING) != 0 ? kSwimming : 0));
    now.keys = keys_given_ ? keys_ : w.keys();
    // rule: og::reach: an asker is resolved to its kind's barring doors and map once, and every
    //   question of it reads them while what they were made from stands (the maps, the world, the
    //   walker's box, order, flags, side, life, keys and corner); after any change it resolves
    //   again, so it never reads a dropped map.
    // tests: SemReach.an_asker_is_resolved_once_and_resolved_again_when_what_it_read_changes
    if (r_.made && same_source(r_, now))
        return true;
    r_ = now;
    ++d.resolutions;
    BarringKey key;
    key.team = now.team;
    key.keys = now.keys;
    key.dead = now.dead;
    r_.barring = &barring_of(d, w, key);
    return true;
}

Map& Asker::map()
{
    // plumbing: looked up (or built) once a resolution
    if (r_.map == nullptr)
    {
        GameWorld& world = *current_game->world;
        MapsData& d = world.reach_maps.data();
        Ground& ground = ground_of(d, world, *w_, ground_key_of(r_));
        r_.map = &map_of(d, world, *w_, ground, *r_.barring);
    }
    return *r_.map;
}

void Asker::start()
{
    // plumbing: worked out once a resolution
    if (r_.started)
        return;
    r_.started = true;
    walker& w = *w_;
    const Cell corner{w.xpos() / GRID_SIZE, w.ypos() / GRID_SIZE, w.floor()};
    r_.start = corner;
    // rule: og::reach: with no door barring its kind a walker starts from its corner's cell, and no
    //   map is built for the question.
    // tests: SemReach.with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route
    if (r_.barring->doors.empty())
        return;
    Map& m = map();
    const Layer& shut = layer_of(m, Mode::Shut);
    const Layer& open = layer_of(m, Mode::Open);
    // rule: og::reach: the corner's cell is the start cell unless it stands with the doors open and
    //   not with them shut (only a barring door closes it); a corner's cell that does not stand even
    //   with the doors open stays the start cell, with no region.
    // tests: SemReach.the_start_cell_leaves_a_doors_cell_for_the_cell_the_box_overlaps_most
    if (region_at(shut, corner) >= 0 || region_at(open, corner) < 0)
        return;
    const int f = layer_floor(shut, w.floor());
    const int x0 = w.xpos();
    const int y0 = w.ypos();
    const int x1 = x0 + w.sizex();
    const int y1 = y0 + w.sizey();
    int best = 0;
    // plumbing: the cells meeting the box, row by row, left to right
    for (int cy = y0 / GRID_SIZE; cy * GRID_SIZE < y1; ++cy)
    {
        // plumbing: (the same walk)
        for (int cx = x0 / GRID_SIZE; cx * GRID_SIZE < x1; ++cx)
        {
            const Cell c{cx, cy, w.floor()};
            const int ox = std::min(x1, (cx + 1) * GRID_SIZE) - std::max(x0, cx * GRID_SIZE);
            const int oy = std::min(y1, (cy + 1) * GRID_SIZE) - std::max(y0, cy * GRID_SIZE);
            // rule: og::reach: the standing cell (shut) the box overlaps most is the start cell; a
            //   tie keeps the first in the scan; with none, the corner's cell (no region).
            // tests: SemReach.the_start_cell_leaves_a_doors_cell_for_the_cell_the_box_overlaps_most
            if (f >= 0 && region_at(shut, c) >= 0 && ox * oy > best)
            {
                best = ox * oy;
                r_.start = c;
            }
        }
    }
}

void Asker::index_start()
{
    // plumbing: worked out once a resolution
    if (r_.indexed)
        return;
    start();
    Map& m = map();
    r_.indexed = true;
    // plumbing: the start cell's index and region in each layer
    for (const Mode mode : {Mode::Shut, Mode::Open})
    {
        const Layer& layer = layer_of(m, mode);
        const int f = layer_floor(layer, r_.start.floor);
        const std::size_t k = static_cast<std::size_t>(mode);
        // plumbing: a start off the layer has no index and no region (it reaches nothing)
        if (f < 0 || !layer.inside(r_.start.x, r_.start.y, f))
            continue;
        r_.start_index[k] = layer.index(r_.start.x, r_.start.y, f);
        r_.start_region[k] = layer.region[static_cast<std::size_t>(r_.start_index[k])];
    }
}

Cell Asker::start_cell()
{
    // rule: og::reach: not judged, a walker starts from its corner's cell.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return Cell{w_->xpos() / GRID_SIZE, w_->ypos() / GRID_SIZE, w_->floor()};
    start();
    return r_.start;
}

int Asker::region_of(Cell c, Mode mode)
{
    // rule: og::reach: not judged, a cell has no region.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return -1;
    return region_at(layer_of(map(), mode), c);
}

CellAnswer Asker::cell_answer(Cell c, Mode mode)
{
    // rule: og::reach: not judged, a cell answers the terrain's two tests.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
    {
        CellAnswer a;
        // plumbing: no world, no terrain to ask
        if (current_game == nullptr || current_game->world == nullptr)
            return a;
        GameWorld& world = *current_game->world;
        const bool multifloor = world.floor_count() > 1;
        a.step = terrain_passes(world, *w_, c.x * GRID_SIZE, c.y * GRID_SIZE, c.floor, StepTest::Step, multifloor, !w_->flies());
        a.flank = terrain_passes(world, *w_, c.x * GRID_SIZE, c.y * GRID_SIZE, c.floor, StepTest::Flank, multifloor, !w_->flies());
        return a;
    }
    const Layer& layer = layer_of(map(), mode);
    const int f = layer_floor(layer, c.floor);
    CellAnswer a;
    // plumbing: a cell off the layer answers neither test
    if (f < 0 || !layer.inside(c.x, c.y, f))
        return a;
    const std::uint8_t bits = layer.bits[static_cast<std::size_t>(layer.index(c.x, c.y, f))];
    a.step = (bits & kStep) != 0;
    a.flank = (bits & kFlank) != 0;
    return a;
}

bool Asker::reaches(Cell from, Cell to, Mode mode)
{
    // rule: og::reach: not judged, every route is open.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return true;
    GameWorld& world = *current_game->world;
    Layer& layer = layer_of(map(), mode);
    const int ff = layer_floor(layer, from.floor);
    const int tf = layer_floor(layer, to.floor);
    // plumbing: a cell off the layer reaches nothing and is reached by nothing
    if (ff < 0 || tf < 0 || !layer.inside(from.x, from.y, ff) || !layer.inside(to.x, to.y, tf))
        return false;
    return reaches_index(world, *w_, layer, layer.index(from.x, from.y, ff), layer.index(to.x, to.y, tf));
}

std::vector<Cell> Asker::approach_cells(const walker& b, Mode mode)
{
    std::vector<Cell> out;
    // rule: og::reach: not judged, there are no approach cells to list.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return out;
    for_each_approach(layer_of(map(), mode), *w_, b, [&out](Cell c, std::int32_t) {
        out.push_back(c);
        return false;
    });
    return out;
}

bool Asker::nearest_approach(const walker& b, Mode mode, Cell& out)
{
    // rule: og::reach: not judged, there is no approach cell to pick.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return false;
    index_start();
    GameWorld& world = *current_game->world;
    Layer& layer = layer_of(map(), mode);
    const std::int32_t start = r_.start_index[static_cast<std::size_t>(mode)];
    // plumbing: a start off the layer reaches nothing
    if (start < 0)
        return false;
    bool found = false;
    int best = 0;
    const auto consider = [&](Cell c, std::int32_t i) {
        // plumbing: a cell w does not get to is no candidate
        if (!reaches_index(world, *w_, layer, start, i))
            return false;
        const int dist = std::abs(c.x - r_.start.x) + std::abs(c.y - r_.start.y);
        // rule: og::reach: the nearest approach cell is the reached one fewest cells from the start
        //   cell (Manhattan); approach cells come row by row, left to right, so the first of equal
        //   distance is the topmost, then the leftmost.
        // tests: SemReach.approach_cells_ring_a_tent_a_big_asker_and_a_two_door_doorway
        if (!found || dist < best)
        {
            found = true;
            best = dist;
            out = c;
        }
        return false;
    };
    for_each_approach(layer, *w_, b, consider);
    return found;
}

bool Asker::walks_to(const walker& b, Mode mode)
{
    // rule: og::reach: not judged, every body is walked to (no door keeps anything).
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return true;
    index_start();
    GameWorld& world = *current_game->world;
    const std::size_t k = static_cast<std::size_t>(mode);
    return walks_in(world.reach_maps.data(), world, *w_, layer_of(map(), mode), r_.start_index[k],
                    r_.start_region[k], b);
}

std::vector<Cell> Asker::closed_cells(const walker& door)
{
    // rule: og::reach: not judged, a door closes nothing.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return {};
    Map& m = map();
    const int k = barring_place(current_game->world->reach_maps.data(), m, door);
    // plumbing: a door that does not bar the kind closes nothing
    if (k < 0)
        return {};
    return m.closed[static_cast<std::size_t>(k)];
}

Exit Asker::door_shut_exit(const walker& b)
{
    // rule: og::reach: only a walker asks() names is judged; any other gets today's answer.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (!asks(w_) || !fresh())
        return Exit::NotAsked;
    // rule: og::reach: with no live door barring its kind nothing is door-shut for it, and no map is
    //   built for the question.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    // tests: SemReach.with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route
    if (r_.barring->doors.empty())
        return Exit::NoBarringDoor;
    // rule: og::reach: only a live living or generator is judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (b.dead() || (b.query_order() != Order::Living && b.query_order() != Order::Generator))
        return Exit::NotAFighter;
    // rule: og::reach: a body on another floor is not judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (b.floor() != w_->floor())
        return Exit::OtherFloor;
    index_start();
    // rule: og::reach: a walker whose start cell has no region (it stands inside terrain or on air)
    //   is not judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (r_.start_region[static_cast<std::size_t>(Mode::Shut)] < 0)
        return Exit::NoRegion;
    // rule: og::reach: a body the walker walks to with the doors that bar it shut is no door's.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (walks_to(b, Mode::Shut))
        return Exit::WalksShut;
    // rule: og::reach: a body the walker could not walk to even with every door open (a wall,
    //   water) is no door's either: today's play.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (!walks_to(b, Mode::Open))
        return Exit::NoWayOpen;
    // rule: og::reach: a body the walker's shot lands on from where it stands is kept by the shot.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (shoots_at(*w_, b))
        return Exit::Shot;
    return Exit::DoorShut;
}

bool Asker::door_shut(const walker& b)
{
    return door_shut_exit(b) == Exit::DoorShut;
}

bool Asker::shut_out()
{
    // plumbing: a walker asks() does not name is never shut out (door_shut_exit asks the same of
    //   every enemy below); answered at once, before the list is walked
    if (!asks(w_) || !fresh())
        return false;
    // plumbing: with no live door barring its kind nothing is door-shut for it (door_shut_exit's own
    //   exit); answered at once, before the list is walked
    if (r_.barring->doors.empty())
        return false;
    walker& w = *w_;
    // plumbing: every body of the object list, judged by the rule below
    for (const auto& up : current_game->world->oblist)
    {
        const walker* b = up.get();
        // rule: og::reach: the walker is shut out when some live, awake, visible hostile living or
        //   generator on its floor is door-shut for it (cloaked or not, at any distance).
        // tests: SemReach.a_bot_is_shut_out_by_an_enemy_only_a_door_keeps_from_it
        if (b == nullptr || b == &w || b->dead() || b->dormant() || b->hidden() ||
            b->floor() != w.floor() ||
            (b->query_order() != Order::Living && b->query_order() != Order::Generator) ||
            w.is_friendly(b))
            continue;
        // plumbing: the first door-shut enemy answers
        if (door_shut_exit(*b) == Exit::DoorShut)
            return true;
    }
    return false;
}

CellsView Asker::cells()
{
    CellsView v;
    // rule: og::reach: not judged, the view is empty: no cell stands in it.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return v;
    Map& m = map();
    const Layer& shut = layer_of(m, Mode::Shut);
    const Layer& open = layer_of(m, Mode::Open);
    v.shut = shut.bits.data();
    v.open = open.bits.data();
    v.w = open.w;
    v.h = open.h;
    v.floors = open.floors;
    return v;
}

std::vector<const walker*> Asker::barring_doors(int floor)
{
    std::vector<const walker*> out;
    // rule: og::reach: not judged, no door is listed.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return out;
    const MapsData& d = current_game->world->reach_maps.data();
    const int f = obmap::filed_floor(floor);
    // plumbing: the kind's barring doors, in the world's order
    for (const std::uint16_t i : r_.barring->doors)
    {
        // rule: og::reach: a kind's barring doors on a floor are those the body map files under that
        //   floor (obmap::filed_floor).
        // tests: SemReach.the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches
        if (d.doors[i].floor == f)
            out.push_back(d.doors[i].door);
    }
    return out;
}

std::vector<int> Asker::regions_touched(const walker& door)
{
    // rule: og::reach: not judged, a door touches no region.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return {};
    Map& m = map();
    const int k = barring_place(current_game->world->reach_maps.data(), m, door);
    // plumbing: a door that does not bar the kind touches nothing in its map
    if (k < 0)
        return {};
    const std::vector<std::int32_t>& r = m.door_regions[static_cast<std::size_t>(k)];
    return std::vector<int>(r.begin(), r.end());
}

std::vector<const walker*> Asker::doors_touched(const walker& door)
{
    std::vector<const walker*> out;
    // rule: og::reach: not judged, a door touches no door.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return out;
    Map& m = map();
    const MapsData& d = current_game->world->reach_maps.data();
    const int k = barring_place(d, m, door);
    // plumbing: a door that does not bar the kind touches nothing in its map
    if (k < 0)
        return out;
    // plumbing: the touched doors, in the barring order
    for (const std::uint16_t e : m.door_doors[static_cast<std::size_t>(k)])
        out.push_back(d.doors[m.barring->doors[e]].door);
    return out;
}

std::vector<const walker*> Asker::doors_beside(int region)
{
    std::vector<const walker*> out;
    // rule: og::reach: not judged, no door is beside a region.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!fresh())
        return out;
    Map& m = map();
    const MapsData& d = current_game->world->reach_maps.data();
    // plumbing: a region the shut layer does not have has no door beside it
    if (region < 0 || static_cast<std::size_t>(region) >= m.region_doors.size())
        return out;
    // plumbing: the doors beside it, in the barring order
    for (const std::uint16_t e : m.region_doors[static_cast<std::size_t>(region)])
        out.push_back(d.doors[m.barring->doors[e]].door);
    return out;
}

// --------------------------------------------------------------------------------------------
// The free questions: each is one question of a fresh Asker.
// --------------------------------------------------------------------------------------------

Cell start_cell(walker& w)
{
    return Asker(w).start_cell();
}

int region_of(walker& w, Cell c, Mode mode)
{
    return Asker(w).region_of(c, mode);
}

CellAnswer cell_answer(walker& w, Cell c, Mode mode)
{
    return Asker(w).cell_answer(c, mode);
}

bool reaches(walker& w, Cell from, Cell to, Mode mode)
{
    return Asker(w).reaches(from, to, mode);
}

std::vector<Cell> approach_cells(walker& w, const walker& b, Mode mode)
{
    return Asker(w).approach_cells(b, mode);
}

bool nearest_approach(walker& w, const walker& b, Mode mode, Cell& out)
{
    return Asker(w).nearest_approach(b, mode, out);
}

bool walks_to(walker& w, const walker& b, Mode mode)
{
    return Asker(w).walks_to(b, mode);
}

std::vector<Cell> closed_cells(walker& w, const walker& door)
{
    return Asker(w).closed_cells(door);
}

Exit door_shut_exit(walker& w, const walker& b)
{
    return Asker(w).door_shut_exit(b);
}

bool door_shut(walker& w, const walker& b)
{
    return Asker(w).door_shut(b);
}

bool shut_out(walker& w)
{
    return Asker(w).shut_out();
}

bool gets_at(walker& w, const walker& b)
{
    return Asker(w).walks_to(b, Mode::Shut) || shoots_at(w, b);
}

bool shoots_at(walker& w, const walker& b)
{
    // rule: og::reach: a melee-only bot (BIT_NO_RANGED) is never credited with a shot.
    // tests: SemReach.the_reach_read_without_the_weapon_hook_is_the_line_checks_for_three_families
    if (w.stats()->query_bit_flags(BIT_NO_RANGED))
        return false;
    // rule: og::reach: not judged, the shot term is no question (it answers yes).
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return true;
    const short xdist = static_cast<short>(b.xpos() - w.xpos());
    const short ydist = static_cast<short>(b.ypos() - w.ypos());
    // rule: og::reach: a body outside w's firing box is not shot at (walker::in_firing_box).
    // tests: SemReach.the_shot_term_judges_only_inside_the_firing_box_and_the_reach
    if (!w.in_firing_box(xdist, ydist))
        return false;
    // The facing toward b is read from the raw pixel delta. statistics::direct_walk snaps its delta
    // 3:1 before its line check (a delta more than three times longer on one axis is taken as
    // straight along it); that snap never changes a facing, because such a delta has a slope under
    // 0.334 or over 3, inside walker::facing's sector borders at 0.414 and 2.414, and the facing is
    // all the profile (its circular range) and the heading read of the delta. So the raw delta
    // gives the facing the snapped one gives, and the snap is not written here.
    const float xd = static_cast<float>(xdist);
    const float yd = static_cast<float>(ydist);
    weap test_weapon(walker::WeaponProfileProbeTag{});
    // rule: og::reach: a family whose weapon the loader cannot make is not judged (yes).
    // tests: SemReach.the_reach_read_without_the_weapon_hook_is_the_line_checks_for_three_families
    if (!w.unhooked_weapon(test_weapon, xd, yd))
        return true;
    // rule: og::reach: a body beyond the reach of w's weapon read without its hook (Manhattan
    //   distance, walker::weapon_reach) is not shot at.
    // tests: SemReach.the_shot_term_judges_only_inside_the_firing_box_and_the_reach
    if (w.distance_to_ob(&b) > walker::weapon_reach(test_weapon))
        return false;
    w.set_weapon_heading(&test_weapon, w.facing(xd, yd), 0);  // fire()'s pad for that facing, no waver
    const std::optional<bool> flown =
        w.line_check_flight(&test_weapon, walker::ignored_target(b, test_weapon), nullptr,
                            /*every_door_stops=*/true);
    return flown.value_or(true);
}

// --------------------------------------------------------------------------------------------
// Gameplay Fixes BI, a door a bot cannot open is a wall to it: its route, its straight walk and
// the push an ally gives it.
// --------------------------------------------------------------------------------------------

bool door_is_a_wall_to(const walker* w)
{
    // rule: Gameplay Fixes BI, a door a bot cannot open is a wall to it: only with the setting on
    //   and only for a bot (walker::is_bot: a living no seat drives), a tower and a guard at its
    //   post included (unlike og::reach::asks): a tower's route and straight walk only turn it, and
    //   a guard or a tower at such a door is not pushed into it; a player's own fighter, and every
    //   walker with the setting off, routes, walks and is pushed as before.
    // tests: SemBotReach.the_route_goes_round_a_door_fence_and_never_through_it
    // tests: SemBotReach.the_straight_walk_takes_no_step_into_a_door_it_cannot_open
    // tests: SemBotReach.a_shoved_bot_at_a_door_keeps_its_orders_and_a_seat_is_shoved_as_before
    // tests: SemBotReach.a_tower_and_a_guard_at_its_post_are_bots_to_this_rule
    return w != nullptr && og::sim::session_gameplay_fixes() != 0 && w->is_bot();
}

bool RouteCells::only_the_foe_closes(int x, int y, int f) const
{
    // plumbing: no bot named, or no world: no cell is opened for the route
    if (foe_door_of == nullptr || current_game == nullptr || current_game->world == nullptr)
        return false;
    return !current_game->world->door_bars_at(static_cast<short>(x), static_cast<short>(y), foe_door_of, f);
}

RoutePlan plan_route(walker& w, Cell goal, const walker* foe)
{
    RoutePlan plan;
    // rule: Gameplay Fixes BI: the route of a walker the rule does not name is the classic solve.
    // tests: SemBotReach.the_route_goes_round_a_door_fence_and_never_through_it
    if (!door_is_a_wall_to(&w))
        return plan;
    Asker a(w);
    plan.cells.view = a.cells();
    // plumbing: nothing judged (no body map): the classic solve
    if (plan.cells.view.shut == nullptr)
        return plan;
    plan.judged = true;
    plan.start = a.start_cell();
    plan.goal = goal;
    const bool standing = a.region_of(plan.start, Mode::Shut) >= 0;
    // rule: Gameplay Fixes BI: a bot whose foe is a door that bars it is routed into that door: the
    //   cells only that door closes stand for the solve (RouteCells), the goal stays the door's
    //   corner cell, and the search is not run when the start cell has a region and the bot does
    //   not walk to the door with the doors that bar it shut.
    // tests: SemBotReach.a_bot_whose_foe_is_a_door_is_routed_into_that_door_and_no_other
    if (foe != nullptr && door_bars(&w, foe))
    {
        plan.cells.foe_door_of = &w;
        plan.run = !standing || a.walks_to(*foe, Mode::Shut);
        return plan;
    }
    // rule: Gameplay Fixes BI: the goal of a route to a foe is the foe's corner cell, but when that
    //   cell stands with the doors open and not with them shut (only a barring door closes it), it
    //   is the nearest approach cell the bot gets to; with none, the goal stays and the search below
    //   is not run. Asked only from a start cell with a region.
    // tests: SemBotReach.a_foe_whose_cell_a_door_closes_is_routed_to_from_beside_it
    if (foe != nullptr && standing && a.region_of(goal, Mode::Shut) < 0 && a.region_of(goal, Mode::Open) >= 0)
    {
        Cell near{};
        // plumbing: no approach cell reached: the goal stays, and reaches() below answers no
        if (a.nearest_approach(*foe, Mode::Shut, near))
            plan.goal = near;
    }
    // rule: Gameplay Fixes BI: a search that must fail is not run: from a start cell with a region,
    //   a goal in no region the bot gets to leaves the route empty, as a failed search leaves it;
    //   from a start cell with no region the search runs.
    // tests: SemBotReach.a_search_that_must_fail_is_not_run
    plan.run = !standing || a.reaches(plan.start, plan.goal, Mode::Shut);
    return plan;
}

}  // namespace og::reach

bool GameWorld::step_refused(float x, float y, walker* w)
{
    // rule: Gameplay Fixes BI, a door a bot cannot open is a wall to it: the straight walk's and the
    //   shove's step test refuses a box the terrain refuses (the classic test) and, for a bot with
    //   the setting on, a box a door that bars it meets (door_bars_at: not its own foe, nothing
    //   drawn); no air test is added.
    // tests: SemBotReach.the_straight_walk_takes_no_step_into_a_door_it_cannot_open
    // tests: SemBotReach.a_shoved_bot_at_a_door_keeps_its_orders_and_a_seat_is_shoved_as_before
    // tests: SemBotReach.on_a_level_with_floors_and_no_door_the_straight_walk_steps_onto_air_as_before
    if (!query_grid_passable(x, y, w))
        return true;
    return og::reach::door_is_a_wall_to(w) &&
           door_bars_at(static_cast<short>(x), static_cast<short>(y), w, w->floor());
}

bool GameWorld::door_bars_at(short x, short y, walker* w, int floor)
{
    // plumbing: no walker or no body map: no door is met
    if (w == nullptr || myobmap == nullptr)
        return false;
    // plumbing: a body map that files no door meets none: answered before the sweep of its piles,
    //   which would answer the same (only a door answers "blocked" in the doors-only question, and
    //   every door the map files is counted by files_a_door)
    if (!myobmap->files_a_door())
        return false;
    const obmap::DoorsOnly only{floor};
    return myobmap->query_list(w, x, y, /*quiet=*/true, /*weapons_block=*/true, &only) == 0;
}
