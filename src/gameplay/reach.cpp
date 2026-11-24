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

constexpr std::uint8_t kStep = 1;
constexpr std::uint8_t kFlank = 2;

// A live door filed in the body map, as a door question reads it.
struct DoorRec
{
    const walker* door = nullptr;
    short x = 0;
    short y = 0;
    short sx = 0;
    short sy = 0;
    int floor = 0;           // the pile band's floor (clamped as obmap clamps it)
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

struct GroundKey
{
    short sx = 0;
    short sy = 0;
    Order order = Order::Living;
    bool ethereal = false;
    bool forestwalk = false;
    bool flies = false;
    bool swimming = false;
    auto tie() const { return std::tie(sx, sy, order, ethereal, forestwalk, flies, swimming); }
    bool operator<(const GroundKey& o) const { return tie() < o.tie(); }
};

struct Ground
{
    GroundKey key;
    Layer open;
};

struct Map
{
    Ground* ground = nullptr;
    std::vector<std::uint16_t> barring;     // indices into MapsData::doors
    Layer shut;                             // empty when no door bars the kind: the open layer
    std::vector<std::vector<Cell>> closed;  // per barring door: the cells it closes
};

// What an asker's barring set depends on: its alliance head's colour, its keys and its life
// (walker::is_friendly answers no to a dead walker).
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

    std::vector<detail::DoorRec> doors;
    std::vector<std::unique_ptr<detail::Ground>> grounds;
    std::vector<std::unique_ptr<detail::Map>> maps;
    std::map<detail::GroundKey, detail::Ground*> ground_index;
    std::map<std::pair<detail::GroundKey, std::vector<std::uint16_t>>, detail::Map*> map_index;
    std::map<detail::BarringKey, std::vector<std::uint16_t>> barring_memo;

    // The tick's memo of walk answers.
    bool memo_valid = false;
    std::uint32_t memo_tick = 0;
    std::unordered_map<detail::WalkKey, bool, detail::WalkKeyHash> walk_memo;

    std::uint64_t builds = 0;
    std::uint64_t memo_hits = 0;

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

namespace detail {

// The floor index a layer stores `floor` under, or -1: a pile band's floor (clamped to 0..120 as
// the body map clamps it), which a single-floor level holds only as floor 0.
int layer_floor(const Layer& layer, int floor)
{
    const int f = std::clamp(floor, 0, 120);
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
            r.floor = std::clamp(static_cast<int>(d->floor()), 0, 120);
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
    }
    bool remade = false;
    // rule: og::reach: a door writer (the body map's door count, a door's dead, team and level
    //   setters) makes the door halves stale: the doors are listed again, the door halves and the
    //   memos dropped; the terrain halves stay.
    // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
    if (maps.door_count() != d.doors_seen || map != d.obmap_seen || writes != d.obmap_writes_seen)
    {
        remade = true;
        d.doors = filed_doors(world);
        d.maps.clear();
        d.map_index.clear();
        d.barring_memo.clear();
        d.walk_memo.clear();
        d.doors_seen = maps.door_count();
        d.obmap_seen = map;
        d.obmap_writes_seen = writes;
        d.checked = true;
        d.checked_tick = world.tick_count_;
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

GroundKey ground_key(walker& w)
{
    GroundKey k;
    k.sx = w.sizex();
    k.sy = w.sizey();
    k.order = w.query_order();
    k.ethereal = w.stats()->query_bit_flags(BIT_ETHEREAL) != 0;
    k.forestwalk = w.stats()->query_bit_flags(BIT_FORESTWALK) != 0;
    k.flies = w.flies();
    k.swimming = w.stats()->query_bit_flags(BIT_SWIMMING) != 0;
    return k;
}

// The doors that bar w (indices into d.doors), by door_bars; the same for every asker of the same
// colour, keys and life, so it is kept until the doors change.
const std::vector<std::uint16_t>& barring_of(MapsData& d, walker& w)
{
    BarringKey key;
    key.team = w.alliance_head()->team_num();
    key.keys = w.keys();
    key.dead = w.dead() != 0;
    auto found = d.barring_memo.find(key);
    // plumbing: the barring set of this colour, keys and life is known
    if (found != d.barring_memo.end())
        return found->second;
    std::vector<std::uint16_t> barring;
    // plumbing: ask door_bars of every listed door
    for (std::size_t i = 0; i < d.doors.size(); ++i)
    {
        // rule: og::reach: a kind's barring set is the listed doors door_bars says bar its asker.
        // tests: SemReach.a_door_bars_a_walker_as_the_loud_step_meets_it_row_by_row
        if (door_bars(&w, d.doors[i].door))
            barring.push_back(static_cast<std::uint16_t>(i));
    }
    return d.barring_memo.emplace(key, std::move(barring)).first->second;
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
    const auto cells = [&layer](int x, int y, int f, StepTest test) {
        const int cx = x / GRID_SIZE;
        const int cy = y / GRID_SIZE;
        // plumbing: a cell off the layer stands nowhere (x < 0 divides to 0 only when x > -16,
        //   and a step's x is a cell corner, so it is a multiple of 16)
        if (x < 0 || y < 0 || !layer.inside(cx, cy, f))
            return false;
        const std::uint8_t want = test == StepTest::Step ? kStep : kFlank;
        return (layer.bits[static_cast<std::size_t>(layer.index(cx, cy, f))] & want) != 0;
    };
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
            for_each_step(world, w, ux * GRID_SIZE, uy * GRID_SIZE, uf, multifloor, nonflyer, cells, visit);
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

Ground& ground_of(MapsData& d, GameWorld& world, walker& w)
{
    const GroundKey key = ground_key(w);
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

// w's kind's map: the terrain half and, when doors bar the kind, the door half (the terrain's
// standing cells minus the cells a barring door's box meets).
Map& map_of(MapsData& d, GameWorld& world, walker& w)
{
    Ground& ground = ground_of(d, world, w);
    const std::vector<std::uint16_t>& barring = barring_of(d, w);
    auto key = std::make_pair(ground.key, barring);
    auto found = d.map_index.find(key);
    // plumbing: this kind has its map
    if (found != d.map_index.end())
        return *found->second;
    auto m = std::make_unique<Map>();
    m->ground = &ground;
    m->barring = barring;
    m->closed.resize(barring.size());
    // rule: og::reach: a kind no door bars reads the terrain half alone (no door half is built).
    // tests: SemReach.with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route
    if (!barring.empty())
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
        for (std::size_t k = 0; k < barring.size(); ++k)
        {
            const DoorRec& door = d.doors[barring[k]];
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
                    //   a living, whose height is 0); closed, a cell answers neither test.
                    // tests: SemReach.a_door_off_the_grid_closes_exactly_the_cells_its_box_meets
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
        ++d.builds;
    }
    Map* raw = m.get();
    d.maps.push_back(std::move(m));
    d.map_index.emplace(std::move(key), raw);
    return *raw;
}

Layer& layer_of(Map& map, Mode mode)
{
    // plumbing: the open layer is the terrain half's; the shut one the door half's, or the terrain
    //   half's when no door bars the kind
    if (mode == Mode::Open || map.barring.empty())
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
    const auto bits = [&layer](int x, int y, int f, StepTest test) {
        const int cx = x / GRID_SIZE;
        const int cy = y / GRID_SIZE;
        // plumbing: a cell off the layer stands nowhere
        if (x < 0 || y < 0 || !layer.inside(cx, cy, f))
            return false;
        const std::uint8_t want = test == StepTest::Step ? kStep : kFlank;
        return (layer.bits[static_cast<std::size_t>(layer.index(cx, cy, f))] & want) != 0;
    };
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
        for_each_step(world, w, cx * GRID_SIZE, cy * GRID_SIZE, cf, multifloor, nonflyer, bits, step);
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

// The start cell, from w's map (see reach.h).
Cell start_cell_in(MapsData& d, GameWorld& world, walker& w)
{
    const Cell corner{w.xpos() / GRID_SIZE, w.ypos() / GRID_SIZE, w.floor()};
    // rule: og::reach: with no door barring its kind a walker starts from its corner's cell, and no
    //   map is built for the question.
    // tests: SemReach.the_start_cell_leaves_a_doors_cell_for_the_cell_the_box_overlaps_most
    if (barring_of(d, w).empty())
        return corner;
    Map& map = map_of(d, world, w);
    const Layer& shut = layer_of(map, Mode::Shut);
    const Layer& open = layer_of(map, Mode::Open);
    // rule: og::reach: the corner's cell is the start cell unless it stands with the doors open and
    //   not with them shut (only a barring door closes it); a corner's cell that does not stand even
    //   with the doors open stays the start cell, with no region.
    // tests: SemReach.the_start_cell_leaves_a_doors_cell_for_the_cell_the_box_overlaps_most
    if (region_at(shut, corner) >= 0 || region_at(open, corner) < 0)
        return corner;
    const int f = layer_floor(shut, w.floor());
    const int x0 = w.xpos();
    const int y0 = w.ypos();
    const int x1 = x0 + w.sizex();
    const int y1 = y0 + w.sizey();
    int best = 0;
    Cell pick = corner;
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
                pick = c;
            }
        }
    }
    return pick;
}

std::vector<Cell> approach_in(const Layer& layer, walker& w, const walker& b)
{
    std::vector<Cell> out;
    const int f = layer_floor(layer, b.floor());
    // plumbing: a body on a floor the level does not hold has no approach cell
    if (f < 0)
        return out;
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
            // rule: og::reach: an approach cell of (w, b) is a standing cell of the mode on which
            //   w's box, grown by one cell on every side, meets b's box (collide).
            // tests: SemReach.approach_cells_ring_a_tent_a_big_asker_and_a_two_door_doorway
            if ((layer.bits[static_cast<std::size_t>(layer.index(x, y, f))] & kStep) != 0 &&
                collide(static_cast<short>(x * GRID_SIZE - GRID_SIZE), static_cast<short>(y * GRID_SIZE - GRID_SIZE),
                        static_cast<short>(sx + 2 * GRID_SIZE), static_cast<short>(sy + 2 * GRID_SIZE),
                        b.xpos(), b.ypos(), b.sizex(), b.sizey()))
                out.push_back(Cell{x, y, f});
        }
    }
    return out;
}

bool walks_to_in(MapsData& d, GameWorld& world, walker& w, const walker& b, Mode mode)
{
    Map& map = map_of(d, world, w);
    Layer& layer = layer_of(map, mode);
    const Cell s = start_cell_in(d, world, w);
    const int sf = layer_floor(layer, s.floor);
    // plumbing: a start off the layer reaches nothing
    if (sf < 0 || !layer.inside(s.x, s.y, sf))
        return false;
    const std::int32_t start = layer.index(s.x, s.y, sf);
    const std::int32_t rs = layer.region[static_cast<std::size_t>(start)];
    WalkKey key;
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
    // plumbing: any approach cell w gets to answers yes
    for (const Cell& c : approach_in(layer, w, b))
    {
        // plumbing: the first one reached ends the walk
        if (reaches_index(world, w, layer, start, layer.index(c.x, c.y, c.floor)))
        {
            answer = true;
            break;
        }
    }
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

}  // namespace detail

using namespace detail;

bool asks(const walker* w)
{
    // rule: og::reach: a walker is asked at the sites of Gameplay Fixes BJ, BK and BL only with the
    //   setting on, and only a bot (walker::is_bot) that is not stationary (a tower) and is not a
    //   guard at its post.
    // tests: SemReach.a_bot_is_a_living_no_seat_drives_and_only_a_moving_bot_is_asked
    return w != nullptr && og::sim::session_gameplay_fixes() != 0 && w->is_bot() && !w->stationary() &&
           w->act_type() != ACT_GUARD;
}

Cell start_cell(walker& w)
{
    // rule: og::reach: not judged, a walker starts from its corner's cell.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return Cell{w.xpos() / GRID_SIZE, w.ypos() / GRID_SIZE, w.floor()};
    GameWorld& world = *current_game->world;
    return start_cell_in(current(world), world, w);
}

int region_of(walker& w, Cell c, Mode mode)
{
    // rule: og::reach: not judged, a cell has no region.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return -1;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    return region_at(layer_of(map_of(d, world, w), mode), c);
}

CellAnswer cell_answer(walker& w, Cell c, Mode mode)
{
    // rule: og::reach: not judged, a cell answers the terrain's two tests.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
    {
        CellAnswer a;
        // plumbing: no world, no terrain to ask
        if (current_game == nullptr || current_game->world == nullptr)
            return a;
        GameWorld& world = *current_game->world;
        const bool multifloor = world.floor_count() > 1;
        a.step = terrain_passes(world, w, c.x * GRID_SIZE, c.y * GRID_SIZE, c.floor, StepTest::Step, multifloor, !w.flies());
        a.flank = terrain_passes(world, w, c.x * GRID_SIZE, c.y * GRID_SIZE, c.floor, StepTest::Flank, multifloor, !w.flies());
        return a;
    }
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    const Layer& layer = layer_of(map_of(d, world, w), mode);
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

bool reaches(walker& w, Cell from, Cell to, Mode mode)
{
    // rule: og::reach: not judged, every route is open.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return true;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    Layer& layer = layer_of(map_of(d, world, w), mode);
    const int ff = layer_floor(layer, from.floor);
    const int tf = layer_floor(layer, to.floor);
    // plumbing: a cell off the layer reaches nothing and is reached by nothing
    if (ff < 0 || tf < 0 || !layer.inside(from.x, from.y, ff) || !layer.inside(to.x, to.y, tf))
        return false;
    return reaches_index(world, w, layer, layer.index(from.x, from.y, ff), layer.index(to.x, to.y, tf));
}

std::vector<Cell> approach_cells(walker& w, const walker& b, Mode mode)
{
    // rule: og::reach: not judged, there are no approach cells to list.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return {};
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    return approach_in(layer_of(map_of(d, world, w), mode), w, b);
}

bool nearest_approach(walker& w, const walker& b, Mode mode, Cell& out)
{
    // rule: og::reach: not judged, there is no approach cell to pick.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return false;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    Layer& layer = layer_of(map_of(d, world, w), mode);
    const Cell s = start_cell_in(d, world, w);
    const int sf = layer_floor(layer, s.floor);
    // plumbing: a start off the layer reaches nothing
    if (sf < 0 || !layer.inside(s.x, s.y, sf))
        return false;
    const std::int32_t start = layer.index(s.x, s.y, sf);
    bool found = false;
    int best = 0;
    // plumbing: the reached approach cells, nearest kept
    for (const Cell& c : approach_in(layer, w, b))
    {
        // plumbing: a cell w does not get to is no candidate
        if (!reaches_index(world, w, layer, start, layer.index(c.x, c.y, c.floor)))
            continue;
        const int dist = std::abs(c.x - s.x) + std::abs(c.y - s.y);
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
    }
    return found;
}

bool walks_to(walker& w, const walker& b, Mode mode)
{
    // rule: og::reach: not judged, every body is walked to (no door keeps anything).
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return true;
    GameWorld& world = *current_game->world;
    return walks_to_in(current(world), world, w, b, mode);
}

std::vector<Cell> closed_cells(walker& w, const walker& door)
{
    // rule: og::reach: not judged, a door closes nothing.
    // tests: SemReach.nothing_is_asked_or_built_with_the_setting_off
    if (!judged(w))
        return {};
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    Map& map = map_of(d, world, w);
    // plumbing: find the door among the kind's barring doors
    for (std::size_t k = 0; k < map.barring.size(); ++k)
    {
        // plumbing: the listed door it is
        if (d.doors[map.barring[k]].door == &door)
            return map.closed[k];
    }
    return {};
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
    // The raw pixel delta, snapped 3:1 as statistics::direct_walk snaps its own before its line
    // check (stats.cpp, direct_walk): a delta more than three times longer on one axis than on the
    // other is taken as straight along that axis.
    float xd = static_cast<float>(xdist);
    float yd = static_cast<float>(ydist);
    // plumbing: the snap decides no facing (3:1 is 18.4 degrees, inside walker::facing's 22.5 degree
    //   sectors): the facing and the heading read the same with it or without it; kept so the
    //   judgment starts from the delta direct_walk starts from
    if (std::fabs(xd) > std::fabs(3 * yd))
        yd = 0;
    if (std::fabs(yd) > std::fabs(3 * xd))
        xd = 0;
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

Exit door_shut_exit(walker& w, const walker& b)
{
    // rule: og::reach: only a walker asks() names is judged; any other gets today's answer.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (!asks(&w) || !judged(w))
        return Exit::NotAsked;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    // rule: og::reach: with no live door barring its kind nothing is door-shut for it, and no map is
    //   built for the question.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    // tests: SemReach.with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route
    if (barring_of(d, w).empty())
        return Exit::NoBarringDoor;
    // rule: og::reach: only a live living or generator is judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (b.dead() || (b.query_order() != Order::Living && b.query_order() != Order::Generator))
        return Exit::NotAFighter;
    // rule: og::reach: a body on another floor is not judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (b.floor() != w.floor())
        return Exit::OtherFloor;
    Map& map = map_of(d, world, w);
    // rule: og::reach: a walker whose start cell has no region (it stands inside terrain or on air)
    //   is not judged.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (region_at(layer_of(map, Mode::Shut), start_cell_in(d, world, w)) < 0)
        return Exit::NoRegion;
    // rule: og::reach: a body the walker walks to with the doors that bar it shut is no door's.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (walks_to_in(d, world, w, b, Mode::Shut))
        return Exit::WalksShut;
    // rule: og::reach: a body the walker could not walk to even with every door open (a wall,
    //   water) is no door's either: today's play.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (!walks_to_in(d, world, w, b, Mode::Open))
        return Exit::NoWayOpen;
    // rule: og::reach: a body the walker's shot lands on from where it stands is kept by the shot.
    // tests: SemReach.each_exit_of_door_shut_gives_todays_answer
    if (shoots_at(w, b))
        return Exit::Shot;
    return Exit::DoorShut;
}

bool door_shut(walker& w, const walker& b)
{
    return door_shut_exit(w, b) == Exit::DoorShut;
}

bool shut_out(walker& w)
{
    // plumbing: a walker asks() does not name is never shut out (door_shut_exit asks the same of
    //   every enemy below); answered at once, before the list is walked
    if (!asks(&w) || !judged(w))
        return false;
    GameWorld& world = *current_game->world;
    MapsData& d = current(world);
    // plumbing: with no live door barring its kind nothing is door-shut for it (door_shut_exit's own
    //   exit); answered at once, before the list is walked
    if (barring_of(d, w).empty())
        return false;
    // plumbing: every body of the object list, judged by the rule below
    for (const auto& up : world.oblist)
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
        if (door_shut_exit(w, *b) == Exit::DoorShut)
            return true;
    }
    return false;
}

bool gets_at(walker& w, const walker& b)
{
    return walks_to(w, b, Mode::Shut) || shoots_at(w, b);
}

}  // namespace og::reach

bool GameWorld::door_bars_at(short x, short y, walker* w, int floor)
{
    // plumbing: no walker or no body map: no door is met
    if (w == nullptr || myobmap == nullptr)
        return false;
    const obmap::DoorsOnly only{floor};
    return myobmap->query_list(w, x, y, /*quiet=*/true, /*weapons_block=*/true, &only) == 0;
}
