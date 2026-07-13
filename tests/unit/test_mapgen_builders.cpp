/* Headless unit tests for the og::mapgen builder library (WP-4).
 *
 * Covers: grid-byte pins (paint helpers and a fixed-seed scatter produce
 * exact tile bytes), placement clearance (scatters keep entity/stair
 * clearance), audit pass/fail fixtures (footing, stair alignment +
 * clearance, fall-line + fall-depth, A*-reachability), seed determinism
 * (same seed twice -> identical grids and entity lists; different seed ->
 * different grids), probe hygiene (the reachability audit leaves the
 * entity lists exactly as found), and the GameplayContextGuard audit
 * smoke (the GO-time no-context-installed pattern the tower generator
 * uses). All tick/byte-compare driven — no wall-clock timing.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace og::mapgen;

// Snapshot a floor grid's bytes.
std::vector<unsigned char> grid_bytes(const GameWorld& w, int floor)
{
    const PixieData& g = w.grid_for_floor(floor);
    if (!g.valid())
        return {};
    return std::vector<unsigned char>(g.data.get(),
                                      g.data.get() + g.w * g.h);
}

// Snapshot the entity lists: (order, family, team, floor, x, y, level) in
// list order, oblist then fxlist.
using EntityRow = std::tuple<int, int, int, int, int, int, int>;
std::vector<EntityRow> entity_rows(const GameWorld& w)
{
    std::vector<EntityRow> rows;
    auto add = [&](walker* ob) {
        if (ob == nullptr)
            return;
        rows.emplace_back(static_cast<int>(ob->query_order()),
                          static_cast<int>(ob->family()),
                          static_cast<int>(ob->team_num()),
                          static_cast<int>(ob->floor()),
                          static_cast<int>(ob->xpos()),
                          static_cast<int>(ob->ypos()),
                          static_cast<int>(ob->stats()->level()));
    };
    for (const auto& uptr : w.oblist)
        add(uptr.get());
    for (const auto& uptr : w.fxlist)
        add(uptr.get());
    return rows;
}

bool any_error_contains(const std::vector<std::string>& errors,
                        const std::string& needle)
{
    for (const std::string& e : errors)
        if (e.find(needle) != std::string::npos)
            return true;
    return false;
}

std::string join_errors(const std::vector<std::string>& errors)
{
    std::string out;
    for (const std::string& e : errors)
    {
        out += e;
        out += '\n';
    }
    return out;
}

// GameWorld::add_ob refuses to create entities without the world's
// entity-service callbacks (production wires them via LevelRuntimeData /
// the headless hooks; the westlands tool reaches the builders through a
// LevelRuntimeData too). These tests build bare scratch GameWorlds, so wire
// the classic loader onto them the same way wire_world_loader
// (level_runtime_data.cpp) does — this is also what the tower generator's
// scratch worlds get through their own runtime data.
loader& mapgen_test_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_entity_services(GameWorld& w)
{
    loader* game_loader = &mapgen_test_loader();
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

// audit_reachability needs an installed GameplayContext (builders.h header
// note). The unit harness installs an ambient session context
// (unit_main.cpp), and the audit itself swaps/restores the context's world
// pointer — so the bare call is correct here; the guard_smoke test below
// exercises the GO-time no-ambient-context pattern explicitly.

// --- Grid-byte pins. ---------------------------------------------------------

TEST(MapgenBuilders, paint_and_paint_rect_exact_bytes)
{
    PixieData g = make_grid(6, 4, PIX_GRASS1);
    paint(g, 1, 1, PIX_H_WALL1);
    paint(g, -1, 2, PIX_WATER1);  // out of grid: ignored
    paint(g, 6, 0, PIX_WATER1);   // out of grid: ignored
    paint_rect(g, 3, 1, 5, 2, PIX_WATER1);

    static constexpr unsigned char kExpected[24] = {
        1, 1, 1, 1, 1, 1,  // row 0: untouched grass
        1, 0, 1, 2, 2, 2,  // row 1: wall at x=1, water x=3..5
        1, 1, 1, 2, 2, 2,  // row 2: water x=3..5
        1, 1, 1, 1, 1, 1,  // row 3: untouched grass
    };
    ASSERT_EQ(g.w, 6);
    ASSERT_EQ(g.h, 4);
    for (int i = 0; i < 24; ++i)
        ASSERT_EQ(g.data[i], kExpected[i]) << "byte " << i;
}

TEST(MapgenBuilders, paint_ring_exact_bytes)
{
    PixieData g = make_grid(7, 7, PIX_GRASS1);
    paint_ring(g, 3.0, 3.0, 1.5, 2.5, PIX_WATER1);

    // Cells whose center distance from (3,3) lies in [1.5, 2.5):
    // d=2 at (+-2,0)/(0,+-2), d=sqrt(5)~2.236 at (+-1,+-2)/(+-2,+-1).
    static constexpr unsigned char G = PIX_GRASS1;
    static constexpr unsigned char W = PIX_WATER1;
    static constexpr unsigned char kExpected[49] = {
        G, G, G, G, G, G, G,
        G, G, W, W, W, G, G,
        G, W, G, G, G, W, G,
        G, W, G, G, G, W, G,
        G, W, G, G, G, W, G,
        G, G, W, W, W, G, G,
        G, G, G, G, G, G, G,
    };
    for (int i = 0; i < 49; ++i)
        ASSERT_EQ(g.data[i], kExpected[i]) << "byte " << i;
}

TEST(MapgenBuilders, stair_pair_paints_aligned_up_down)
{
    GameWorld w(7u);
    init_world(w, 2, 10, 8);
    ASSERT_EQ(w.floor_count(), 2);
    ASSERT_TRUE(w.grid_for_floor(1).valid());
    EXPECT_EQ(w.pixmaxx, 10 * GRID_SIZE);
    EXPECT_EQ(w.pixmaxy, 8 * GRID_SIZE);

    stair_pair(w, 0, 4, 5);
    EXPECT_EQ(w.grid_for_floor(0).data[4 + 5 * 10], PIX_ZSTAIR_UP);
    EXPECT_EQ(w.grid_for_floor(1).data[4 + 5 * 10], PIX_ZSTAIR_DOWN);
}

// Fixed-seed scatter pin: exact bytes of a 12x8 grass field after
// scatter_litter(seed=0xC0FFEE, modulus=3). Pinned from the first
// implementation run; any change to position_hash or the scatter rules
// shows up here as a byte diff.
TEST(MapgenBuilders, scatter_litter_fixed_seed_byte_pin)
{
    GameWorld w(0u);
    init_world(w, 1, 12, 8);
    scatter_litter(w, 0xC0FFEEu, 0, 0, 0, 11, 7, 3);

    // OG_MAPGEN_SCATTER_PIN: 1 = grass, 123..126 = the four jagged-litter
    // variants (position-hash cell select + variant pick).
    static constexpr unsigned char kExpected[96] = {
        1, 124, 1,   1,   1,   1,   1,   126, 1,   126, 1,   1,
        125, 1,  123, 1,   1,   1,   1,   1,   1,   1,   1,   124,
        125, 126, 123, 1,  125, 1,   1,   1,   1,   126, 1,   1,
        126, 125, 1,  1,   123, 126, 1,   1,   123, 125, 1,   1,
        125, 1,   1,  1,   1,   1,   1,   125, 1,   1,   123, 1,
        126, 1,   1,  124, 1,   1,   1,   1,   1,   1,   1,   1,
        124, 125, 1,  1,   126, 124, 1,   125, 1,   1,   1,   1,
        126, 126, 1,  1,   126, 1,   125, 123, 1,   123, 1,   125,
    };
    const std::vector<unsigned char> got = grid_bytes(w, 0);
    ASSERT_EQ(got.size(), 96u);
    for (int i = 0; i < 96; ++i)
        ASSERT_EQ(got[static_cast<std::size_t>(i)], kExpected[i])
            << "byte " << i;
}

// --- Seed determinism. ---------------------------------------------------------

struct BuildSnapshot
{
    std::vector<std::vector<unsigned char>> grids;
    std::vector<EntityRow> entities;
};

// A small two-floor level exercising every builder family: paints, stairs,
// smoothing (world-RNG), placements and all three seeded scatters.
BuildSnapshot build_fixture(std::uint32_t seed)
{
    GameWorld w(seed);
    init_world(w, 2, 20, 14);
    wire_entity_services(w);
    paint_rect(w.grid_for_floor(0), 0, 0, 19, 0, PIX_H_WALL1);
    paint_rect(w.grid_for_floor(0), 14, 6, 17, 9, PIX_WATER1);
    paint_rect(w.grid_for_floor(1), 0, 0, 19, 13, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 2, 2, 8, 6, PIX_GRASS1);
    stair_pair(w, 0, 3, 3);
    smooth_world(w);

    place_start(w, 0, 2, 10);
    place_start(w, 0, 4, 10);
    walker* foe = place_living(w, FAMILY_SOLDIER, 2, 0, 10, 10, 3);
    EXPECT_NE(foe, nullptr);
    place_living(w, FAMILY_ARCHER, 2, 1, 5, 4, 2, true);
    place_generator(w, FAMILY_TENT, 2, 0, 12, 3, 1);
    place_exit(w, 1, 7, 5, 701);

    scatter_litter(w, seed, 0, 0, 0, 19, 13, 5);
    scatter_boulders(w, seed, 0, 0, 0, 19, 13, 7);
    scatter_decor(w, seed, 0, 0, 0, 19, 13, 4, DECOR_PEBBLES,
                  {ScatterGround::Grass});

    BuildSnapshot snap;
    for (int f = 0; f < w.floor_count(); ++f)
        snap.grids.push_back(grid_bytes(w, f));
    snap.entities = entity_rows(w);
    return snap;
}

TEST(MapgenDeterminism, same_seed_identical_grids_and_entities)
{
    const BuildSnapshot a = build_fixture(20260712u);
    const BuildSnapshot b = build_fixture(20260712u);
    ASSERT_EQ(a.grids.size(), b.grids.size());
    for (std::size_t f = 0; f < a.grids.size(); ++f)
        EXPECT_EQ(a.grids[f], b.grids[f]) << "floor " << f;
    EXPECT_EQ(a.entities, b.entities);
}

TEST(MapgenDeterminism, different_seed_differs)
{
    const BuildSnapshot a = build_fixture(1u);
    const BuildSnapshot b = build_fixture(2u);
    ASSERT_EQ(a.grids.size(), b.grids.size());
    bool any_diff = false;
    for (std::size_t f = 0; f < a.grids.size() && !any_diff; ++f)
        any_diff = (a.grids[f] != b.grids[f]);
    EXPECT_TRUE(any_diff)
        << "different seeds produced byte-identical grids on every floor";
    // Entity placement is authored (not hashed), so the lists match — that
    // is by design: the seed drives terrain streams only.
    EXPECT_EQ(a.entities, b.entities);
}

TEST(MapgenDeterminism, position_hash_pure_and_salted)
{
    EXPECT_EQ(position_hash(42u, 7, 9, 1), position_hash(42u, 7, 9, 1));
    EXPECT_NE(position_hash(42u, 7, 9, 1), position_hash(43u, 7, 9, 1));
    EXPECT_NE(position_hash(42u, 7, 9, 1), position_hash(42u, 8, 9, 1));
    EXPECT_NE(position_hash(42u, 7, 9, 1), position_hash(42u, 7, 9, 2));
}

// --- Placement clearance. -------------------------------------------------------

TEST(MapgenPlacement, scatters_keep_entity_and_stair_clearance)
{
    GameWorld w(3u);
    init_world(w, 2, 20, 14);
    wire_entity_services(w);
    stair_pair(w, 0, 5, 5);
    walker* soldier = place_living(w, FAMILY_SOLDIER, 2, 0, 10, 7, 1);
    ASSERT_NE(soldier, nullptr);

    // cell_near_entity: footprint cell, margin ring, and beyond.
    EXPECT_TRUE(cell_near_entity(w, 0, 10, 7, 0));
    EXPECT_TRUE(cell_near_entity(w, 0, 9, 6, 1));
    EXPECT_TRUE(cell_near_entity(w, 0, 11, 8, 1));
    EXPECT_FALSE(cell_near_entity(w, 0, 8, 7, 1));
    EXPECT_FALSE(cell_near_entity(w, 1, 10, 7, 1)) << "other floor is clear";

    // modulus 1 = every eligible cell: the scatter must still skip the
    // stair cell and the one-tile clearance ring around the soldier.
    scatter_litter(w, 99u, 0, 0, 0, 19, 13, 1);
    const PixieData& g = w.grid_for_floor(0);
    EXPECT_EQ(g.data[5 + 5 * 20], PIX_ZSTAIR_UP) << "stair cell overwritten";
    int littered = 0;
    for (int ty = 0; ty < 14; ++ty)
        for (int tx = 0; tx < 20; ++tx)
        {
            const unsigned char t = g.data[tx + ty * 20];
            const bool jagged =
                t >= PIX_JAGGED_GROUND_1 && t <= PIX_JAGGED_GROUND_4;
            if (jagged)
                ++littered;
            if (tx >= 9 && tx <= 11 && ty >= 6 && ty <= 8)
            {
                EXPECT_FALSE(jagged)
                    << "litter inside the soldier's clearance ring at ("
                    << tx << ", " << ty << ")";
            }
        }
    EXPECT_GT(littered, 100) << "modulus-1 scatter barely painted";
}

// --- paint_decor / scatter_decor rules. ----------------------------------------

TEST(MapgenDecor, paint_decor_refusals_and_lazy_plane)
{
    GameWorld w(4u);
    init_world(w, 2, 10, 8);
    paint(w.grid_for_floor(0), 2, 2, PIX_AIR);
    stair_pair(w, 0, 4, 4);

    EXPECT_FALSE(w.decor_for_floor(0).valid()) << "plane must start absent";
    EXPECT_FALSE(paint_decor(w, 0, 2, 2, DECOR_BOULDER_1)) << "over air";
    EXPECT_FALSE(paint_decor(w, 0, 4, 4, DECOR_BOULDER_1)) << "over stair";
    EXPECT_FALSE(paint_decor(w, 0, -1, 0, DECOR_BOULDER_1)) << "off grid";
    EXPECT_FALSE(paint_decor(w, 0, 5, 5, DECOR_MAX)) << "id out of range";
    EXPECT_FALSE(w.decor_for_floor(0).valid())
        << "refusals must not allocate the plane";

    EXPECT_TRUE(paint_decor(w, 0, 6, 6, DECOR_BOULDER_2));
    ASSERT_TRUE(w.decor_for_floor(0).valid());
    EXPECT_EQ(w.decor_for_floor(0).data[6 + 6 * 10], DECOR_BOULDER_2);
    EXPECT_NE(w.grid_for_floor(0).data[6 + 6 * 10], PIX_AIR)
        << "base byte must stay untouched";
}

TEST(MapgenDecor, scatter_decor_ambience_rules)
{
    GameWorld w(5u);
    init_world(w, 1, 12, 10);
    wire_entity_services(w);
    paint(w.grid, 3, 3, PIX_DIRT_1);
    walker* soldier = place_living(w, FAMILY_SOLDIER, 2, 0, 6, 6, 1);
    ASSERT_NE(soldier, nullptr);
    ASSERT_TRUE(paint_decor(w, 0, 2, 2, DECOR_BONES)); // hand-placed decor

    EXPECT_FALSE(scatter_decor(w, 11u, 0, 0, 0, 11, 9, 1, DECOR_TORCH1,
                               {ScatterGround::Grass}))
        << "blocking decor id must be refused";

    ASSERT_TRUE(scatter_decor(w, 11u, 0, 0, 0, 11, 9, 1, DECOR_PEBBLES,
                              {ScatterGround::Grass}));
    const PixieData& dec = w.decor_for_floor(0);
    ASSERT_TRUE(dec.valid());
    EXPECT_EQ(dec.data[2 + 2 * 12], DECOR_BONES)
        << "hand-placed decor keeps its cell";
    EXPECT_EQ(dec.data[3 + 3 * 12], DECOR_NONE) << "dirt is not Grass class";
    EXPECT_EQ(dec.data[6 + 6 * 12], DECOR_NONE) << "entity cell skipped";
    EXPECT_EQ(dec.data[0 + 0 * 12], DECOR_PEBBLES)
        << "grass corner cell dressed at modulus 1";
}

// --- Audits: footing. -----------------------------------------------------------

TEST(MapgenAudits, footing_pass_and_fail)
{
    GameWorld w(6u);
    init_world(w, 2, 12, 10);
    wire_entity_services(w);
    place_start(w, 0, 2, 2);
    place_living(w, FAMILY_SOLDIER, 2, 0, 6, 6, 1);
    EXPECT_TRUE(audit_footing(w).empty())
        << join_errors(audit_footing(w));

    // A ground troop on water: impassable footing.
    walker* wet = place_living(w, FAMILY_SOLDIER, 2, 0, 8, 8, 1);
    ASSERT_NE(wet, nullptr);
    paint(w.grid_for_floor(0), 8, 8, PIX_WATER1);
    {
        const auto errors = audit_footing(w);
        EXPECT_TRUE(any_error_contains(errors, "impassable ground"))
            << join_errors(errors);
    }
    paint(w.grid_for_floor(0), 8, 8, PIX_GRASS1);
    EXPECT_TRUE(audit_footing(w).empty());

    // A ground troop over an upper-floor air hole: air-spawn violation.
    paint_rect(w.grid_for_floor(1), 0, 0, 11, 9, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 3, 3, 5, 5, PIX_GRASS1);
    walker* faller = place_living(w, FAMILY_SOLDIER, 2, 1, 7, 4, 1);
    ASSERT_NE(faller, nullptr);
    {
        const auto errors = audit_footing(w);
        EXPECT_TRUE(any_error_contains(errors, "spawns over air"))
            << join_errors(errors);
    }
}

// --- Audits: stairs. -------------------------------------------------------------

TEST(MapgenAudits, stair_alignment_and_clearance)
{
    GameWorld w(8u);
    init_world(w, 2, 12, 10);
    wire_entity_services(w);

    // No stairs at all: boundary check fails, opt-out passes.
    EXPECT_TRUE(any_error_contains(audit_stairs(w, true),
                                   "no aligned stair pair"));
    EXPECT_TRUE(audit_stairs(w, false).empty());

    stair_pair(w, 0, 5, 5);
    EXPECT_TRUE(audit_stairs(w, true).empty())
        << join_errors(audit_stairs(w, true));

    // An UP with no aligned DOWN does not satisfy the boundary.
    paint(w.grid_for_floor(1), 5, 5, PIX_GRASS1);
    EXPECT_TRUE(any_error_contains(audit_stairs(w, true),
                                   "no aligned stair pair"));
    paint(w.grid_for_floor(1), 5, 5, PIX_ZSTAIR_DOWN);

    // An ACT_GUARD post on an arrival cell seals the staircase.
    walker* guard_post =
        place_living(w, FAMILY_SOLDIER, 2, 0, 5, 4, 1, /*guard=*/true);
    ASSERT_NE(guard_post, nullptr);
    {
        const auto errors = audit_stairs(w, true);
        EXPECT_TRUE(any_error_contains(errors, "ACT_GUARD post"))
            << join_errors(errors);
        EXPECT_TRUE(any_error_contains(errors, "seal the staircase"));
    }
    guard_post->set_act_type(ACT_RANDOM); // roamers are fine
    EXPECT_TRUE(audit_stairs(w, true).empty())
        << join_errors(audit_stairs(w, true));

    // Blocking decor on an arrival cell (either floor of the pair).
    ASSERT_TRUE(paint_decor(w, 1, 4, 5, DECOR_BOULDER_1));
    EXPECT_TRUE(any_error_contains(audit_stairs(w, true), "blocking decor"));
}

// --- Audits: fall lines + depth. ----------------------------------------------

TEST(MapgenAudits, fall_line_landing_and_pit)
{
    GameWorld w(9u);
    init_world(w, 2, 10, 8);
    // Floor 1: a grass patch in an air field; every border air cell is a
    // legal fall entry landing on floor-0 grass.
    paint_rect(w.grid_for_floor(1), 0, 0, 9, 7, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 2, 2, 4, 4, PIX_GRASS1);
    EXPECT_TRUE(audit_fall_lines(w, 4).empty())
        << join_errors(audit_fall_lines(w, 4));

    // Water under one fall entry: blocked landing.
    paint(w.grid_for_floor(0), 5, 3, PIX_WATER1);
    {
        const auto errors = audit_fall_lines(w, 4);
        ASSERT_EQ(errors.size(), 1u) << join_errors(errors);
        EXPECT_TRUE(any_error_contains(errors, "lands on impassable ground"));
    }

    // A pit column (air all the way past floor 0) is a designed death, not
    // an audit failure.
    paint(w.grid_for_floor(0), 5, 3, PIX_AIR);
    EXPECT_TRUE(audit_fall_lines(w, 4).empty())
        << join_errors(audit_fall_lines(w, 4));
}

TEST(MapgenAudits, fall_depth_budget)
{
    GameWorld w(10u);
    init_world(w, 6, 6, 6);
    // Floors 1..4 pure air (no standable adjacency: not entries), floor 5
    // grass with one hole: a 5-story drop to floor 0.
    for (int f = 1; f <= 4; ++f)
        paint_rect(w.grid_for_floor(f), 0, 0, 5, 5, PIX_AIR);
    paint(w.grid_for_floor(5), 3, 3, PIX_AIR);

    {
        const auto errors = audit_fall_lines(w, 4);
        ASSERT_EQ(errors.size(), 1u) << join_errors(errors);
        EXPECT_TRUE(any_error_contains(errors, "drops 5 stories"))
            << join_errors(errors);
    }
    EXPECT_TRUE(audit_fall_lines(w, 5).empty())
        << join_errors(audit_fall_lines(w, 5));
}

// --- Audits: reachability. -------------------------------------------------------

TEST(MapgenAudits, reachability_pass_fail_and_probe_hygiene)
{
    GameWorld w(12u);
    init_world(w, 1, 20, 14);
    wire_entity_services(w);
    // A wall column splits the map; the lead marker is west, foe and exit
    // east.
    paint_rect(w.grid, 10, 0, 10, 13, PIX_H_WALL1);
    place_start(w, 0, 2, 2);
    place_living(w, FAMILY_SOLDIER, 2, 0, 15, 5, 1);
    place_exit(w, 0, 16, 8, 701);

    const std::size_t obs_before = w.oblist.size();
    const std::size_t fx_before = w.fxlist.size();
    {
        const auto errors = audit_reachability(w);
        EXPECT_TRUE(any_error_contains(errors, "unreachable"))
            << join_errors(errors);
        EXPECT_GE(errors.size(), 2u)
            << "both the foe and the exit are sealed away:\n"
            << join_errors(errors);
    }
    EXPECT_EQ(w.oblist.size(), obs_before)
        << "the audit probe leaked into the oblist";
    EXPECT_EQ(w.fxlist.size(), fx_before);

    // Carve a gap: everything reachable.
    paint(w.grid, 10, 7, PIX_GRASS1);
    EXPECT_TRUE(audit_reachability(w).empty())
        << join_errors(audit_reachability(w));
    EXPECT_EQ(w.oblist.size(), obs_before);

    // Flyers are exempt: a ghost re-sealed behind the wall is fine.
    paint(w.grid, 10, 7, PIX_H_WALL1);
    walker* ghost = place_living(w, FAMILY_GHOST, 2, 0, 17, 11, 1);
    ASSERT_NE(ghost, nullptr);
    ASSERT_TRUE(ghost->stats()->query_bit_flags(BIT_FLYING));
    paint(w.grid, 10, 7, PIX_GRASS1);
    EXPECT_TRUE(audit_reachability(w).empty())
        << join_errors(audit_reachability(w));
}

TEST(MapgenAudits, reachability_requires_lead_marker)
{
    GameWorld w(13u);
    init_world(w, 1, 10, 8);
    wire_entity_services(w);
    place_living(w, FAMILY_SOLDIER, 2, 0, 5, 5, 1);
    const auto errors = audit_reachability(w);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(any_error_contains(errors, "lead start marker"));
}

// The GO-time execution pattern (tower spec D8/§5.6): no gameplay context
// is installed while generating, so the generator wraps the audits in a
// GameplayContextGuard over a scratch context. The guard's non-reentrancy
// assert is satisfied because generation runs outside any installed
// context; here we simulate that window by clearing the ambient test
// context around the guarded audit.
TEST(MapgenAudits, gameplay_context_guard_smoke)
{
    GameWorld w(14u);
    init_world(w, 2, 16, 12);
    wire_entity_services(w);
    stair_pair(w, 0, 8, 6);
    place_start(w, 0, 2, 2);
    place_living(w, FAMILY_SOLDIER, 2, 0, 12, 9, 1);
    place_living(w, FAMILY_SOLDIER, 2, 1, 8, 5, 1);
    place_exit(w, 1, 3, 3, 701);

    GameplayContext* const ambient = current_game;
    current_game = nullptr; // the GO-time window: nothing installed
    {
        GameplayContext scratch{};
        scratch.world = &w;
        GameplayContextGuard guard(&scratch);
        ASSERT_EQ(current_game, &scratch);
        const auto errors = audit_reachability(w);
        EXPECT_TRUE(errors.empty()) << join_errors(errors);
        EXPECT_EQ(current_game->world, &w)
            << "the audit must restore the context's world pointer";
    }
    EXPECT_EQ(current_game, nullptr) << "guard must restore the prior state";
    current_game = ambient;
}

} // namespace
