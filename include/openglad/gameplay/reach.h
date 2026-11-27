/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// What a bot can get at: for a bot and a body, "can it walk to it with the doors it cannot open
// shut", "could it with every door open", "does its shot land on it from where it stands", and so
// "does only a locked door keep it from this enemy" and "is this bot shut out". The answers are read
// from maps of where each kind of fighter can stand, kept by the world (GameWorld::reach_maps):
// derived state only, never in a snapshot, a save, a replay or on the wire, a pure function of the
// grids, the live doors and the kind, the same headless and drawn. With the Gameplay Fixes setting
// off nothing is asked and no map is built. Defined in src/gameplay/reach.cpp; the door predicate
// door_bars beside the door arm of ob_pass_check (obmap.cpp).
//
// The route finder's neighbour rule, its passability half (for_each_step below), is written here
// once: the route finder (PathingMap::adjacent_cost, gameplay_context.cpp) prices the steps it
// answers, and the maps number their regions with the same steps. It is a template over the world
// and the walker so that the route finder keeps its inlined cost; this header is light enough for
// game_world.h to hold the maps by value.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>

class GameWorld;
class walker;

namespace og::reach {

// A door bars `mover` when the door is alive, is a door (walker::is_door), is not of the mover's
// side (mover->is_friendly(door) is false) and the mover does not hold its key. No exception for a
// door that is the mover's foe. The door arm of ob_pass_check asks it for its two key tests.
[[nodiscard]] bool door_bars(const walker* mover, const walker* door);
// The same rule for a walker of the mover's box, flags and side holding `keys` instead of its own
// (0: no key at all). door_bars(mover, door) is door_bars(mover, door, mover->keys()).
[[nodiscard]] bool door_bars(const walker* mover, const walker* door, std::uint32_t keys);

// Who is asked at the sites of the rules for an enemy behind a locked door, a key holder at its
// door and a door broken down as a last resort: the setting is on, `w` is a bot (walker::is_bot),
// not stationary (a tower) and not a guard at its post (ACT_GUARD).
[[nodiscard]] bool asks(const walker* w);

// The two modes of a map: the doors that bar the kind shut, or every door open.
enum class Mode : std::uint8_t { Shut, Open };

// A cell of the 16 px grid (x, y in cells) on a floor.
struct Cell
{
    int x = 0;
    int y = 0;
    int floor = 0;
    bool operator==(const Cell&) const = default;
};

// What a cell answers a step in a mode (the route finder's two tests): a step may end on it; a
// diagonal may pass beside it.
struct CellAnswer
{
    bool step = false;
    bool flank = false;
    bool operator==(const CellAnswer&) const = default;
};

// The tests a step asks of a cell: a step may end on it, a diagonal may pass beside it, a stair's
// hop may land on it.
enum class StepTest : std::uint8_t { Step, Flank, Landing };
// What a step is: to one of the eight neighbours on its floor, or a stair's hop to another floor.
enum class StepKind : std::uint8_t { Straight, Diagonal, Stair };

// Which exit door_shut answered from, in its order; only DoorShut is "a door keeps it".
enum class Exit : std::uint8_t
{
    NotAsked,       // not asks(w)
    NoBarringDoor,  // no live door bars w's kind
    NotAFighter,    // b is not a live living or generator
    OtherFloor,     // b is on another floor
    NoRegion,       // start_cell(w) has no region
    WalksShut,      // walks_to(w, b, Shut)
    NoWayOpen,      // not walks_to(w, b, Open)
    Shot,           // shoots_at(w, b)
    DoorShut,       // only a locked door keeps w from b
};

namespace detail {

constexpr std::uint8_t kStep = 1;
constexpr std::uint8_t kFlank = 2;
constexpr std::uint8_t kLanding = 4;

// A stored layer's answer to a step test of the cell whose corner is at pixels (x, y) on floor f:
// the one reading of a layer, for the maps' own walks and for a route finder's view (CellsView).
inline bool stored_passes(const std::uint8_t* bits, int w, int h, int floors, int x, int y, int f,
                          StepTest test)
{
    // plumbing: no layer, or a cell off it, stands nowhere (a step's corner is a multiple of 16,
    //   so a negative one is off the layer)
    if (bits == nullptr || x < 0 || y < 0 || f < 0 || x / GRID_SIZE >= w || y / GRID_SIZE >= h ||
        f >= floors)
        return false;
    // rule: og::reach: a stored cell answers a step's test by its step bit, a diagonal's flank
    //   test by its flank bit and a stair hop's landing test by its landing bit.
    // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
    // tests: SemBotReach.a_stair_whose_landing_a_door_closes_is_no_way_up
    const std::uint8_t want = test == StepTest::Step ? kStep : test == StepTest::Flank ? kFlank : kLanding;
    return (bits[static_cast<std::size_t>((f * h + y / GRID_SIZE) * w + x / GRID_SIZE)] & want) != 0;
}

struct BarringSet;
struct Layer;
struct Map;

// What an Asker was resolved from (the world, the maps as they stood, the walker's box, order,
// flags, side, life, keys and corner) and what it was resolved to. reach.cpp fills it.
struct Resolution
{
    bool made = false;
    const GameWorld* world = nullptr;
    std::uint64_t generation = 0;
    short x = 0;
    short y = 0;
    short floor = 0;
    short sx = 0;
    short sy = 0;
    std::uint8_t order = 0;
    std::uint8_t flags = 0;
    unsigned char team = 0;
    bool dead = false;
    std::uint32_t keys = 0;
    const BarringSet* barring = nullptr;
    Map* map = nullptr;  // built on first need
    bool started = false;  // start worked out
    Cell start{};
    bool indexed = false;  // start_index and start_region worked out
    std::int32_t start_index[2] = {-1, -1};   // per Mode: the start cell in that layer, or -1
    std::int32_t start_region[2] = {-1, -1};  // per Mode: its region, or -1
};

}  // namespace detail

// A kind's two layers read cell by cell with no lookup: what a route finder's solve reads for each
// neighbour (Asker::cells). The view itself is a Cells of for_each_step over the shut layer (the
// doors that bar the kind shut); passes() reads either layer, so a cell's open and shut answers
// can be read side by side. Valid while no writer bumps a count (Maps) and the walker keeps its
// kind: fetch it again for each solve. Empty when nothing is judged (no cell stands).
struct CellsView
{
    const std::uint8_t* shut = nullptr;
    const std::uint8_t* open = nullptr;
    int w = 0;
    int h = 0;
    int floors = 0;

    [[nodiscard]] bool operator()(int x, int y, int f, StepTest test) const
    {
        return detail::stored_passes(shut, w, h, floors, x, y, f, test);
    }
    [[nodiscard]] bool passes(Mode mode, int x, int y, int f, StepTest test) const
    {
        // plumbing: the layer of the mode asked
        return detail::stored_passes(mode == Mode::Shut ? shut : open, w, h, floors, x, y, f, test);
    }
};

// What a bot's route reads for each neighbour (Gameplay Fixes BI, a door a bot cannot open is a
// wall to it): the shut layer of its kind, where a door that bars it closes the cells its box would
// meet as a wall closes them. For a bot whose foe is a door that bars it (a door a level script or a
// mod named its foe), a cell that stands with the doors open and that no barring door but that foe
// meets stands too (GameWorld::door_bars_at leaves the walker's own foe out), so its route can end
// in the door. A Cells of for_each_step. Valid for one solve, as its view is.
struct RouteCells
{
    CellsView view;
    walker* foe_door_of = nullptr;  // the bot, when its foe is a door that bars it; else none

    [[nodiscard]] bool operator()(int x, int y, int f, StepTest test) const
    {
        // rule: Gameplay Fixes BI, a door a bot cannot open is a wall to it: a bot's route reads
        //   every step, flank and landing test from its kind's shut layer.
        // tests: SemBotReach.the_route_goes_round_a_door_fence_and_never_through_it
        if (view(x, y, f, test))
            return true;
        // rule: Gameplay Fixes BI: for a bot whose foe is a door that bars it, a cell closed by no
        //   barring door but that foe stands for its route, so the route ends in the door.
        // tests: SemBotReach.a_bot_whose_foe_is_a_door_is_routed_into_that_door_and_no_other
        if (foe_door_of == nullptr || !view.passes(Mode::Open, x, y, f, test))
            return false;
        return only_the_foe_closes(x, y, f);
    }
    // No barring door but the walker's own foe meets its box at (x, y) on floor f (reach.cpp).
    [[nodiscard]] bool only_the_foe_closes(int x, int y, int f) const;
};

// The route of a bot (Gameplay Fixes BI), worked out before its solve: where the solve starts and
// ends, what it reads for each neighbour, and whether it is run at all.
struct RoutePlan
{
    bool judged = false;  // false: the classic solve runs (the setting off, not a bot, no world)
    bool run = false;     // false: the search could only fail; the route is left empty
    Cell start{};
    Cell goal{};
    RouteCells cells;
};

// Who plans its route, its straight walk and the push an ally gives it with a door it cannot open
// as a wall (Gameplay Fixes BI): the setting is on and w is a bot (walker::is_bot), a tower and a
// guard at its post included (asks() leaves them out; this rule does not).
[[nodiscard]] bool door_is_a_wall_to(const walker* w);

// The plan of a bot's route to the cell `goal`; `foe` is the walker's foe when the route is to it
// (goal its corner's cell), null for a route to a point. The solve starts from start_cell(w). Its
// goal is `goal`, but when `foe` is given and its corner's cell stands with the doors open and not
// with them shut, the nearest approach cell w gets to. When the start cell has a region and the
// goal lies in no region w gets to, the search is not run. When `foe` is a door that bars w, the
// cells only that door closes stand for the solve, the goal stays the door's corner cell, and the
// search is not run when the start has a region and w does not walk to the door. With a start cell
// of no region the search runs to `goal`. Not judged (door_is_a_wall_to says no, or nothing is
// judged): judged = false.
[[nodiscard]] RoutePlan plan_route(walker& w, Cell goal, const walker* foe);

// A walker resolved once for many questions: its kind (its box, order, flags and side, holding its
// own keys or the keys given), the kind's map and its start cell. A search's candidates, the cast
// rules' list and shut_out's enemies are asked one after another of one Asker. Each question first
// checks that what it was resolved from still stands (the maps' writers, the world, the walker's
// box, flags, side, life, keys and corner) and resolves again when not, so a held Asker never reads
// a dropped map and always answers the world as it stands. The free functions below are each one
// question of a fresh Asker.
class Asker
{
public:
    // w as it stands, holding its own keys.
    explicit Asker(walker& w) : w_(&w) {}
    // w's box, order, flags and side holding `keys` instead of its own (0: no key at all, the
    // keyless kind).
    Asker(walker& w, std::uint32_t keys) : w_(&w), keys_(keys), keys_given_(true) {}

    [[nodiscard]] Cell start_cell();
    [[nodiscard]] int region_of(Cell c, Mode mode);
    [[nodiscard]] CellAnswer cell_answer(Cell c, Mode mode);
    [[nodiscard]] bool reaches(Cell from, Cell to, Mode mode);
    [[nodiscard]] std::vector<Cell> approach_cells(const walker& b, Mode mode);
    bool nearest_approach(const walker& b, Mode mode, Cell& out);
    [[nodiscard]] bool walks_to(const walker& b, Mode mode);
    [[nodiscard]] std::vector<Cell> closed_cells(const walker& door);
    [[nodiscard]] Exit door_shut_exit(const walker& b);
    [[nodiscard]] bool door_shut(const walker& b);
    [[nodiscard]] bool shut_out();
    // The view of the kind's two layers for one solve (see CellsView).
    [[nodiscard]] CellsView cells();
    // The barring doors of the kind filed on `floor` (obmap::filed_floor), in the order the world
    // lists them (its weapons first).
    [[nodiscard]] std::vector<const walker*> barring_doors(int floor);
    // The regions of the shut layer that a barring door touches: one of the cells it closes has a
    // 4-neighbour standing cell (shut) in the region. Sorted; empty for a door that does not bar.
    [[nodiscard]] std::vector<int> regions_touched(const walker& door);
    // The barring doors a barring door touches: one of its closed cells is, or is a 4-neighbour
    // of, one of theirs (two doors in a row with no floor between them). In the world's order.
    [[nodiscard]] std::vector<const walker*> doors_touched(const walker& door);
    // The barring doors beside a region of the shut layer (each touches it), in the world's order.
    [[nodiscard]] std::vector<const walker*> doors_beside(int region);
    // Gameplay Fixes BL, a bot with no enemy it can get at breaks down the locked door in its way:
    // the door breaks() answers for this walker (none when its conditions fail or no edge door
    // leads to a hostile), whether `held` is an edge door that leads to a hostile (the keep's
    // "no flip between doors"), and whether the walker is shut out (shut_out's answer, read from
    // the same walk over its hostiles, so the idle decision asks one question). See breaks() and
    // keeps_door() below.
    struct LastResort
    {
        const walker* door = nullptr;
        bool held_leads = false;
        bool shut = false;
    };
    [[nodiscard]] LastResort last_resort(const walker* held);

private:
    // The hostiles this walker is judged against (shut_out and last_resort), each with
    // door_shut_exit's answer, in the object list's order, until `visit` answers true (reach.cpp).
    template <class Visit>
    void judge_hostiles(Visit&& visit);
    // The maps current and the resolution made from them; false when nothing is judged (reach.cpp).
    bool fresh();
    detail::Map& map();
    void start();
    void index_start();

    walker* w_;
    std::uint32_t keys_ = 0;
    bool keys_given_ = false;
    detail::Resolution r_;
};

// The cell w starts its walks from: the cell of its corner, but when only a barring door closes
// that cell, the standing cell (shut) its box overlaps most (row by row, left to right, a tie
// keeps the first); when none stands, the corner's cell, which then has no region. With the
// setting off, or no door barring its kind, the corner's cell.
[[nodiscard]] Cell start_cell(walker& w);
// The region of `c` for w's kind in `mode`, or -1 (not a standing cell, or not judged).
[[nodiscard]] int region_of(walker& w, Cell c, Mode mode);
// What `c` answers a step of w's kind in `mode` (with the setting off: the terrain's answer).
[[nodiscard]] CellAnswer cell_answer(walker& w, Cell c, Mode mode);
// Does a route of w's kind lead from `from` to `to` in `mode` (the route finder's question: true
// for the same cell, else `to` lies in a region `from` gets to, or is a cell that is not a
// standing cell but is reached, as a stair's landing on air)? Not judged: true.
[[nodiscard]] bool reaches(walker& w, Cell from, Cell to, Mode mode);
// The approach cells of (w, b) in `mode`: every standing cell c of w's kind such that w's box set
// on c, grown by one cell on every side, meets b's box (collide). Row by row, left to right.
[[nodiscard]] std::vector<Cell> approach_cells(walker& w, const walker& b, Mode mode);
// The nearest approach cell w gets to from start_cell(w) (Manhattan in cells, ties by row then
// column), into `out`; false when there is none or it is not judged.
bool nearest_approach(walker& w, const walker& b, Mode mode, Cell& out);
// Some approach cell of (w, b) that stands in `mode` lies in a region w gets to from
// start_cell(w). Not judged (the setting off, no world, a non-living asker): true.
[[nodiscard]] bool walks_to(walker& w, const walker& b, Mode mode);
// The shot term: false for a melee-only bot (BIT_NO_RANGED); else b is inside w's firing box
// (walker::in_firing_box), within the reach of w's weapon read without the family's weapon hook
// (walker::unhooked_weapon, walker::weapon_reach), and the line check's flight
// (walker::line_check_flight) passes for a detached test weapon on fire()'s pad for the facing
// toward b, headed that way with no waver, stopped by every live enemy door, b included. A family
// with no weapon descriptor, the setting off: true (not judged).
[[nodiscard]] bool shoots_at(walker& w, const walker& b);
// The cells the barring door `door` closes for w's kind (standing with the doors open, the box
// meeting the door), row by row; empty when it does not bar w or it is not judged.
[[nodiscard]] std::vector<Cell> closed_cells(walker& w, const walker& door);
[[nodiscard]] Exit door_shut_exit(walker& w, const walker& b);
// Only a door keeps w from b: door_shut_exit(w, b) == Exit::DoorShut.
[[nodiscard]] bool door_shut(walker& w, const walker& b);
// asks(w), and some live, awake, visible hostile living or generator on w's floor is door_shut
// for it (cloaked or not, at any distance, no roll, no draw).
[[nodiscard]] bool shut_out(walker& w);
// Gameplay Fixes BK, a bot that holds the key to a door on its way walks its route into the door:
// only a key w holds opens its way to b. asks(w), b on w's floor, and w holds the key of a live door
// on its floor (the cheap tests, answered before any map is read or any Asker resolved); then
// start_cell(w) has a region, w walks to b with the doors that bar it shut, and a walker of w's box,
// flags and side holding no key (Asker(w, 0), the keyless kind) would not walk to b.
[[nodiscard]] bool opens_way(walker& w, const walker& b);
// Gameplay Fixes BL, a bot with no enemy it can get at breaks down the locked door in its way: the
// door w is handed as its foe, or none. None unless asks(w), shut_out(w), no live, awake, visible
// hostile living or generator on w's floor is one w gets at (walks_to Shut, or shoots_at: the
// words of "w gets at b", which only this question asks, from door_shut_exit's answer), and
// start_cell(w) has a region; a key w holds changes nothing here (a door it can open bars it not,
// so it is open in its map). The door walk: for w's kind a barring door closes the cells that stand
// with the doors open and on which the kind's box meets it; it touches a region of the shut layer
// when a closed cell has a 4-neighbour standing cell in that region, and touches another barring
// door when a closed cell of one is, or is a 4-neighbour of, a closed cell of the other. The walk
// goes region to door, door to door and door to region along the touches; every door entered costs
// one; a region's stair links cost nothing and lead one way. The door: among the barring doors on
// w's floor that touch a region w gets to (the edge doors), the one from which a region holding an
// approach cell of such a hostile is reached at the least cost, that door counted; ties by
// Manhattan cells from start_cell(w) to the door's corner cell, then row, then column. An edge door
// that leads to no such region is never answered. Draws nothing.
[[nodiscard]] walker* breaks(walker& w);
// The keep of Gameplay Fixes BL, asked of a bot (asks) whose foe is the live door `held`, whether
// that door bars it or not: `held` while breaks' conditions hold and `held` is an edge door that
// leads to a hostile (it need not be the best: no flip between doors); while the conditions hold
// and only another door qualifies, the door breaks(w) answers; otherwise none (the bot lets it go).
// A door that does not bar w is no edge door. Draws nothing.
[[nodiscard]] walker* keeps_door(walker& w, const walker& held);

struct MapsData;

// The maps a world keeps (GameWorld::reach_maps). Built on first need, stale by the writers and
// compared at every question: a door's body added to, moved in or removed from the body map (the
// body map's own door count, obmap::door_writes), a door's dead, team and level setters
// (note_doors), and a grid replaced, resized, moved in, written by a snapshot or a floor added or
// dropped (note_terrain).
class Maps
{
public:
    Maps();
    ~Maps();
    Maps(const Maps&) = delete;
    Maps& operator=(const Maps&) = delete;

    // A writer changed what a door question reads: the door halves are stale.
    void note_doors() noexcept { ++door_count_; }
    // A writer changed the terrain: every map is stale.
    void note_terrain() noexcept { ++terrain_count_; }
    [[nodiscard]] std::uint64_t door_count() const noexcept { return door_count_; }
    [[nodiscard]] std::uint64_t terrain_count() const noexcept { return terrain_count_; }
    // How many maps (terrain halves and door halves) were built: with the setting off it stays 0.
    [[nodiscard]] std::uint64_t builds() const noexcept;
    // How many walk answers the tick's memo gave instead of the arrays.
    [[nodiscard]] std::uint64_t memo_hits() const noexcept;
    // How many times an asker was resolved to its kind's barring doors and map (Asker): once for
    // all the questions of one Asker while what it was resolved from stands.
    [[nodiscard]] std::uint64_t resolutions() const noexcept;

    MapsData& data() noexcept { return *data_; }

private:
    std::uint64_t door_count_ = 0;
    std::uint64_t terrain_count_ = 0;
    std::unique_ptr<MapsData> data_;
};

// --------------------------------------------------------------------------------------------
// The route finder's neighbour rule, its passability half.
// --------------------------------------------------------------------------------------------

// The terrain's answer to a step test of the cell whose corner is at pixels (x, y) on floor f, for
// walker w; `multifloor` and `nonflyer` are the level's and the walker's, read once by the caller.
// The route finder asks it; the terrain half of a map stores its answers.
template <class World, class Walker>
bool terrain_passes(World& world, Walker& w, int x, int y, int f, StepTest test, bool multifloor,
                    bool nonflyer)
{
    // Any terrain in the way?  This checks boundaries too.
    // rule: The AI never plans a step into terrain the walker cannot enter (walls, trees for
    //   non-forestwalkers, water for non-swimmers, off-map), nor past a flank such terrain holds.
    // tests: Decor.grounder_routes_around_decor_boulder_strip_flyer_crosses
    // tests: NewTiles.grounder_routes_around_lava_strip_flyer_crosses
    // tests: SingleFloorPathing.astar_never_emits_diagonals_past_blocked_flanks
    if (!world.query_grid_passable(static_cast<float>(x), static_cast<float>(y), &w, f))
        return false;
    // rule: A stair's hop asks its landing cell the terrain's test alone (a landing on air is a
    //   link, as it always was).
    // tests: SemPathing.a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor
    // tests: SemReach.the_maps_steps_and_regions_are_the_route_finders_for_six_kinds
    if (test == StepTest::Landing)
        return true;
    // Non-flyers avoid "air" holes in pathing (they would fall), so
    // AI routes around pits and only changes floors via Z-stairs.
    // Gated on multifloor so single-floor expansion is unchanged.
    // rule: On stacked levels a walker that cannot fly never plans a step onto air (it would fall);
    //   flyers (permanent or potion) may.
    // tests: SemPathing.a_non_flyer_never_plans_a_step_onto_air_on_stacked_levels
    if (test == StepTest::Step)
        return !(multifloor && nonflyer &&
                 world.smoother_for_floor(f).query_genre_x_y(x / GRID_SIZE, y / GRID_SIZE) == TYPE_AIR);
    // rule: A walker that cannot fly never plans a diagonal past an air flank, on every level (a
    //   body swept over an air flank can clip its center into the hole and fall).
    // tests: SemPathing.a_non_flyer_never_plans_a_diagonal_past_an_air_flank
    return !(nonflyer &&
             world.smoother_for_floor(f).query_genre_x_y(x / GRID_SIZE, y / GRID_SIZE) == TYPE_AIR);
}

// The steps out of the cell whose corner is at pixels (x1, y1) on floor f: each of
// the eight neighbours (in the route finder's order) whose cell passes `cells(x, y, f, Step)`, a
// diagonal only when both flanking cells pass `cells(x, y, f, Flank)`; then a stair's hop, when
// its landing passes `cells(x1, y1, tf, Landing)`.
// `emit(x, y, f, kind)` is called for each, in that order. `cells` is the terrain's answer for
// the route finder (terrain_passes) and a map's stored answer for a map.
template <class World, class Cells, class Emit>
void for_each_step(World& world, int x1, int y1, int f, bool multifloor, bool nonflyer, Cells&& cells,
                   Emit&& emit)
{
    // plumbing: 8-neighbour expansion (skip self); rules below,
    //   below
    for (int i = -1; i <= 1; i++)
    {
        // plumbing: 8-neighbour expansion (skip self); rules below,
        //   below
        for (int j = -1; j <= 1; j++)
        {
            // plumbing: 8-neighbour expansion (skip self); rules below,
            //   below
            if (i == 0 && j == 0)
                continue;

            const int adj_x = x1 + i * GRID_SIZE;
            const int adj_y = y1 + j * GRID_SIZE;

            // rule: The AI never plans a step into a cell the cell's step test refuses (terrain the
            //   walker cannot enter; air for a walker that cannot fly on stacked levels).
            // tests: Decor.grounder_routes_around_decor_boulder_strip_flyer_crosses
            // tests: NewTiles.grounder_routes_around_lava_strip_flyer_crosses
            // tests: SemPathing.a_non_flyer_never_plans_a_step_onto_air_on_stacked_levels
            if (!cells(adj_x, adj_y, f, StepTest::Step))
                continue;
            // No corner cutting (2026-07, deliberate fix; see
            // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). A full-cell body can
            // only make a diagonal cell transition when BOTH flanking
            // orthogonal cells are open: its swept box necessarily
            // overlaps them, so a diagonal past a blocked flank is
            // pixel-impassable at EVERY offset. The classic graph emitted
            // such hops anyway; when the map pinched (lava + scree
            // boulder in an X around the hop) the follower seized on an
            // edge it could never take — permanently (the L24 terrace
            // wedge). Flanks impose the same rules as targets: grid
            // passability plus the non-flyer air skip (a body swept over
            // an air flank can clip its center into the hole and fall).
            // 2026-07-10: the multifloor gate is REMOVED — single-floor
            // corridor mazes (Westlands L2 "The Forest Road") exhibit the
            // same permanent corner seizures the gate was preserving, so
            // corner-cut edges are now pruned on every level. This is an
            // audited single-floor parity rebaseline (see the doc row);
            // the rule consumes no RNG, so drift is position/timing only.
            // rule: The AI never plans a diagonal step past a blocked corner: a diagonal needs
            //   both orthogonal neighbours open (and, for non-flyers, not air). Deliberate fix
            //   on every level, ported to the old game.
            // tests: SingleFloorPathing.astar_never_emits_diagonals_past_blocked_flanks
            // tests: ZAxis.multifloor_astar_never_emits_diagonals_past_blocked_flanks
            // tests: GameContext.pathfinding_state_supports_move_construction_and_assignment
            // tests: WalkerPathing.direct_solver_returns_expected_route_and_cost
            // tests: SemPathing.a_non_flyer_never_plans_a_diagonal_past_an_air_flank
            if (i != 0 && j != 0)
            {
                // rule: The AI never plans a diagonal step past a blocked corner: a diagonal
                //   needs both orthogonal neighbours open (and, for non-flyers, not air); the
                //   flank across x is asked first, as the classic loop asked it.
                // tests: SingleFloorPathing.astar_never_emits_diagonals_past_blocked_flanks
                // tests: ZAxis.multifloor_astar_never_emits_diagonals_past_blocked_flanks
                // tests: SemPathing.a_non_flyer_never_plans_a_diagonal_past_an_air_flank
                if (!cells(adj_x, y1, f, StepTest::Flank) || !cells(x1, adj_y, f, StepTest::Flank))
                    continue;
                emit(adj_x, adj_y, f, StepKind::Diagonal);
                continue;
            }
            emit(adj_x, adj_y, f, StepKind::Straight);
        }
    }

    // Cross-floor Z-stair edge: if the current cell is a stair tile, add a
    // vertical neighbor on the connected floor (positional: UP→f+1,
    // DOWN→f-1). Non-flyers only; gated on multifloor so single-floor A*
    // expansion is byte-identical.
    // rule: On stacked levels a walker that cannot fly plans floor changes only through stairs:
    //   an up-stair links to the floor above, a down-stair to the floor below, if that floor
    //   exists and its cell passes the landing test (the terrain's; in a shut layer a landing a
    //   barring door closes is no link); flyers never change floors.
    // tests: SemPathing.a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor
    // tests: SemBotReach.a_stair_whose_landing_a_door_closes_is_no_way_up
    if (multifloor && nonflyer)
    {
        const int tile = world.smoother_for_floor(f).query_x_y(x1 / GRID_SIZE, y1 / GRID_SIZE);
        int tf = -1;
        // rule: On stacked levels a walker that cannot fly plans floor changes only through
        //   stairs.
        // tests: SemPathing.a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor
        if (tile == PIX_ZSTAIR_UP && (f + 1) < world.floor_count())
            tf = f + 1;
        else if (tile == PIX_ZSTAIR_DOWN && f > 0)
            tf = f - 1;
        // rule: On stacked levels a walker that cannot fly plans floor changes only through
        //   stairs, onto a landing that passes the landing test.
        // tests: SemPathing.a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor
        // tests: SemBotReach.a_stair_whose_landing_a_door_closes_is_no_way_up
        if (tf >= 0 && cells(x1, y1, tf, StepTest::Landing))
            emit(x1, y1, tf, StepKind::Stair);
    }
}

}  // namespace og::reach
