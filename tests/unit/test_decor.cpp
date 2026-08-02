/* BASE + DECOR tile-layer mechanics (headless).
 *
 * Direct sim pins for the decor plane (core/decordefs.h + GameWorld storage):
 *   - registry id/passability/concealment contract (persisted bytes: frozen),
 *   - passability composition: for every (base, decor) pair in the migration
 *     mapping, the composed result equals the legacy combined tile for all
 *     probe archetypes (ground/weapon/flyer/temp-flight/forestwalk/special),
 *   - BlocksGround semantics + "decor never grants passage" (base AND decor),
 *   - SHRUB concealment (living forestwalk charge, weapon lineofsight decay)
 *     while BOULDER/BONES do not conceal,
 *   - damage_tile skips decorated cells (decor shields the ground),
 *   - A* routes a grounder around a decor-boulder strip (flyer crosses),
 *   - a valid all-zero decor plane is behavior-identical to no plane at all.
 * og_test_parity stays blind by construction: every new branch is gated on
 * decor plane validity and no stock (pre-migration) level carries a plane.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

// Fill the whole floor-0 grid with one tile byte.
void fill_grid(GameWorld& w, unsigned char tile)
{
    const std::size_t n =
        static_cast<std::size_t>(w.grid.w) * static_cast<std::size_t>(w.grid.h);
    std::fill(w.grid.data.get(), w.grid.data.get() + n, tile);
}

// Allocate (zero-filled) the decor plane for `floor`, sized to that floor's
// grid — the same lazy-allocation shape the editor brush uses.
PixieData& alloc_decor(GameWorld& w, int floor)
{
    const PixieData& fg = w.grid_for_floor(floor);
    PixieData& dp = w.decor_for_floor(floor);
    dp.frames = 1;
    dp.w = fg.w;
    dp.h = fg.h;
    dp.data = std::make_unique<unsigned char[]>(
        static_cast<std::size_t>(fg.w) * static_cast<std::size_t>(fg.h));
    return dp;
}

void fill_decor(PixieData& dp, unsigned char decor_id)
{
    const std::size_t n =
        static_cast<std::size_t>(dp.w) * static_cast<std::size_t>(dp.h);
    std::fill(dp.data.get(), dp.data.get() + n, decor_id);
}

} // namespace

// Decor ids are persisted bytes in shipped "_dN.png" planes: append-only,
// never renumber. The registry rows are the sim contract (§1e of the design):
// TORCH*/BRAZIER/BOULDER_*/COLUMN_* block ground exactly like the legacy
// water/torch arm; PEBBLES/BONES contribute nothing; SHRUB alone conceals.
TEST(Decor, registry_ids_and_contract_are_pinned)
{
    ASSERT_EQ(0, DECOR_NONE);
    ASSERT_EQ(1, DECOR_TORCH1);
    ASSERT_EQ(2, DECOR_TORCH2);
    ASSERT_EQ(3, DECOR_TORCH3);
    ASSERT_EQ(4, DECOR_BRAZIER);
    ASSERT_EQ(5, DECOR_BOULDER_1);
    ASSERT_EQ(6, DECOR_BOULDER_2);
    ASSERT_EQ(7, DECOR_BOULDER_3);
    ASSERT_EQ(8, DECOR_BOULDER_4);
    ASSERT_EQ(9, DECOR_PEBBLES);
    ASSERT_EQ(10, DECOR_COLUMN_BOTTOM);
    ASSERT_EQ(11, DECOR_COLUMN_TOP);
    ASSERT_EQ(12, DECOR_SHRUB);
    ASSERT_EQ(13, DECOR_BONES);
    ASSERT_EQ(14, DECOR_MAX);

    const auto& reg = decor_registry();
    ASSERT_EQ(static_cast<std::size_t>(DECOR_MAX), reg.size());

    EXPECT_EQ(DecorPassability::None, reg[DECOR_NONE].pass);
    for (unsigned char blocking : {DECOR_TORCH1, DECOR_TORCH2, DECOR_TORCH3,
                                   DECOR_BRAZIER, DECOR_BOULDER_1,
                                   DECOR_BOULDER_2, DECOR_BOULDER_3,
                                   DECOR_BOULDER_4, DECOR_COLUMN_BOTTOM,
                                   DECOR_COLUMN_TOP})
    {
        EXPECT_EQ(DecorPassability::BlocksGround, reg[blocking].pass)
            << "decor id " << static_cast<int>(blocking);
        EXPECT_FALSE(reg[blocking].conceals)
            << "no migrated decor may conceal (stock byte-identity)";
    }
    for (unsigned char walkable : {DECOR_PEBBLES, DECOR_BONES})
    {
        EXPECT_EQ(DecorPassability::None, reg[walkable].pass)
            << "decor id " << static_cast<int>(walkable);
        EXPECT_FALSE(reg[walkable].conceals)
            << "decor id " << static_cast<int>(walkable);
    }
    EXPECT_EQ(DecorPassability::None, reg[DECOR_SHRUB].pass)
        << "SHRUB is walkable concealment";
    EXPECT_TRUE(reg[DECOR_SHRUB].conceals) << "SHRUB is THE concealing decor";
}

namespace {

// The five probe archetypes of the §6.4 equivalence suite.
struct PassProbes
{
    walker* ground;
    walker* weapon;
    walker* flyer;
    walker* forest;
    walker* special;
};

PassProbes make_probes(GameWorld& w)
{
    PassProbes p{};
    p.ground = w.add_ob(Order::Living, FAMILY_SOLDIER);
    p.weapon = w.add_ob(Order::Weapon, FAMILY_KNIFE);
    p.flyer = w.add_ob(Order::Living, FAMILY_SOLDIER);
    p.forest = w.add_ob(Order::Living, FAMILY_SOLDIER);
    p.special = w.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    EXPECT_NE(p.ground, nullptr);
    EXPECT_NE(p.weapon, nullptr);
    EXPECT_NE(p.flyer, nullptr);
    EXPECT_NE(p.forest, nullptr);
    EXPECT_NE(p.special, nullptr);
    p.flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    p.forest->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    for (walker* ob : {p.ground, p.weapon, p.flyer, p.forest, p.special})
        ob->setxy(100, 100);
    return p;
}

} // namespace

// The migration mapping (§6): composed (base + decor) passability must equal
// the legacy combined tile for every archetype. This is the property that
// makes the campaign migration parity-safe: identical passability ==>
// identical A* paths ==> identical sim.
TEST(Decor, composition_reproduces_legacy_combined_tiles_for_all_archetypes)
{
    const struct
    {
        unsigned char legacy_combined;
        unsigned char base;
        unsigned char decor;
        bool ground_passes; // sanity pin of the expected shared verdict
    } rows[] = {
        {PIX_TORCH1, PIX_WALLSIDE_C, DECOR_TORCH1, false},
        {PIX_TORCH2, PIX_WALLSIDE_C, DECOR_TORCH2, false},
        {PIX_TORCH3, PIX_WALLSIDE_C, DECOR_TORCH3, false},
        {PIX_BRAZIER1, PIX_FLOOR1, DECOR_BRAZIER, false},
        {PIX_BOULDER_1, PIX_GRASS2, DECOR_BOULDER_1, false},
        {PIX_BOULDER_2, PIX_GRASS3, DECOR_BOULDER_2, false},
        {PIX_BOULDER_3, PIX_GRASS2, DECOR_BOULDER_3, false},
        {PIX_BOULDER_4, PIX_GRASS2, DECOR_BOULDER_4, false},
        {PIX_GRASS_RUBBLE, PIX_GRASS_DARK_1, DECOR_PEBBLES, true},
    };

    for (const auto& row : rows)
    {
        SCOPED_TRACE("legacy pix " + std::to_string(row.legacy_combined) +
                     " -> base " + std::to_string(row.base) + " + decor " +
                     std::to_string(row.decor));

        TestGameWorld legacy_tw(1);
        GameWorld& legacy_w = legacy_tw.world();
        fill_grid(legacy_w, row.legacy_combined);
        PassProbes legacy_probes = make_probes(legacy_w);

        TestGameWorld composed_tw(2);
        GameWorld& composed_w = composed_tw.world();
        fill_grid(composed_w, row.base);
        fill_decor(alloc_decor(composed_w, 0), row.decor);
        PassProbes composed_probes = make_probes(composed_w);

        const std::pair<walker*, walker*> pairs[] = {
            {legacy_probes.ground, composed_probes.ground},
            {legacy_probes.weapon, composed_probes.weapon},
            {legacy_probes.flyer, composed_probes.flyer},
            {legacy_probes.forest, composed_probes.forest},
            {legacy_probes.special, composed_probes.special},
        };
        const char* labels[] = {"ground", "weapon", "flyer", "forestwalk",
                                "special"};
        for (std::size_t i = 0; i < std::size(pairs); ++i)
        {
            const bool legacy_verdict =
                legacy_w.query_grid_passable(100, 100, pairs[i].first);
            const bool composed_verdict =
                composed_w.query_grid_passable(100, 100, pairs[i].second);
            EXPECT_EQ(legacy_verdict, composed_verdict)
                << labels[i]
                << ": composed passability must equal the legacy combined "
                   "tile";
        }

        EXPECT_EQ(row.ground_passes,
                  composed_w.query_grid_passable(
                      100, 100, composed_probes.ground))
            << "expected ground verdict for this mapping row";
    }
}

// BlocksGround reproduces the legacy water/torch arm exactly: weapons and
// flyers (permanent or temporary flight) pass, ground walkers are blocked,
// and the BIT_ETHEREAL early-out still precedes everything.
TEST(Decor, blocks_ground_lets_weapons_and_flyers_pass)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    fill_decor(alloc_decor(w, 0), DECOR_TORCH1); // over default grass

    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* proj = w.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(grounder, nullptr);
    ASSERT_NE(proj, nullptr);
    grounder->setxy(100, 100);
    proj->setxy(100, 100);

    EXPECT_FALSE(w.query_grid_passable(100, 100, grounder))
        << "BlocksGround decor must block a ground unit";
    EXPECT_TRUE(w.query_grid_passable(100, 100, proj))
        << "projectiles fly over BlocksGround decor";

    grounder->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
        << "flyers cross BlocksGround decor";
    grounder->stats()->set_bit_flags(BIT_FLYING, 0);

    grounder->set_flight_left(10);
    EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
        << "temporary flight crosses BlocksGround decor";
    grounder->set_flight_left(0);

    grounder->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
        << "the ethereal early-out precedes the decor consult";
    grounder->stats()->set_bit_flags(BIT_ETHEREAL, 0);
}

// Composition = base AND decor. Decor can only restrict — walkable decor on a
// blocking base never opens the cell (no bridge-decor-on-water in v1), and a
// solid wall still blocks even projectiles regardless of decor.
TEST(Decor, decor_never_grants_passage)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* proj = w.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(grounder, nullptr);
    ASSERT_NE(proj, nullptr);
    grounder->setxy(100, 100);
    proj->setxy(100, 100);

    fill_grid(w, PIX_WATER1);
    fill_decor(alloc_decor(w, 0), DECOR_PEBBLES);
    EXPECT_FALSE(w.query_grid_passable(100, 100, grounder))
        << "pebbles on water: the water arm still blocks ground walkers";
    EXPECT_TRUE(w.query_grid_passable(100, 100, proj))
        << "pebbles on water: weapons still pass per the base water arm";

    fill_grid(w, PIX_H_WALL1);
    EXPECT_FALSE(w.query_grid_passable(100, 100, proj))
        << "walls return false before the decor consult is ever reached";
}

// SHRUB conceals: the forestwalk branch in living::act() (magic charge) and
// the weapon lineofsight decay in weap::act() both fire on a shrub cell and
// stay silent over non-concealing decor (BONES as the decorated control).
TEST(Decor, shrub_conceals_for_living_and_weapon_but_bones_does_not)
{
    // decor_conceals_at contract first (the shared helper all four
    // concealment consumers use).
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        EXPECT_FALSE(w.decor_conceals_at(0, 5, 5)) << "no plane -> false";
        PixieData& dp = alloc_decor(w, 0);
        dp.data[5 + dp.w * 5] = DECOR_SHRUB;
        dp.data[6 + dp.w * 5] = DECOR_BOULDER_1;
        EXPECT_TRUE(w.decor_conceals_at(0, 5, 5)) << "shrub conceals";
        EXPECT_FALSE(w.decor_conceals_at(0, 6, 5)) << "boulder does not";
        EXPECT_FALSE(w.decor_conceals_at(0, 4, 5)) << "empty cell";
        EXPECT_FALSE(w.decor_conceals_at(0, -1, 5)) << "OOB is never hidden";
        EXPECT_FALSE(w.decor_conceals_at(0, 5, 500)) << "OOB is never hidden";
    }

    // Differential living run: identical worlds/walkers, one standing in
    // SHRUB, the control in BONES. Only the shrub walker pays the forestwalk
    // magic charge.
    float magic_after[2] = {0.0f, 0.0f};
    for (int variant = 0; variant < 2; ++variant)
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        fill_decor(alloc_decor(w, 0),
                   variant == 0 ? DECOR_SHRUB : DECOR_BONES);

        walker* elfish = w.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(elfish, nullptr);
        elfish->stats()->set_bit_flags(BIT_FORESTWALK, 1);
        elfish->setxy(100, 100);
        // magic == max keeps the regen tick that runs just before the
        // forestwalk block a no-op (delay stays 0), so the concealment
        // charge is the only thing that can move magicpoints this act.
        elfish->stats()->set_max_magicpoints(10.0f);
        elfish->stats()->set_magicpoints(10.0f);
        elfish->stats()->set_current_magic_delay(0);
        elfish->act();
        magic_after[variant] = elfish->stats()->magicpoints();
    }
    EXPECT_EQ(magic_after[0], magic_after[1] - 1.0f)
        << "the shrub walker pays exactly the one-point forestwalk charge "
           "its BONES twin does not";

    // Differential weapon run: lineofsight decays only over the shrub.
    std::int32_t sight_after[2] = {0, 0};
    for (int variant = 0; variant < 2; ++variant)
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        fill_decor(alloc_decor(w, 0),
                   variant == 0 ? DECOR_SHRUB : DECOR_BONES);

        walker* proj = w.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        ASSERT_NE(proj, nullptr);
        proj->setxy(100, 100);
        proj->set_ani_type(ANI_WALK);
        proj->set_act_type(ACT_SIT);
        proj->set_lineofsight(5);
        proj->act();
        sight_after[variant] = proj->lineofsight();
    }
    EXPECT_EQ(4, sight_after[0])
        << "crossing a concealing decor cell decays weapon lineofsight";
    EXPECT_EQ(5, sight_after[1])
        << "non-concealing decor leaves lineofsight alone";
}

// Decor shields the ground: damage_tile never applies the grass->charred
// transform to a decorated cell (legacy PIX_BOULDER_* cells never
// transformed; migrated boulder-on-grass cells must not start to).
TEST(Decor, damage_tile_skips_decorated_cells)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    fill_grid(w, PIX_GRASS1);
    PixieData& dp = alloc_decor(w, 0);
    dp.data[6 + dp.w * 6] = DECOR_BOULDER_1;

    // Undecorated grass chars and reports the dirty tile.
    const char bare = w.damage_tile(3 * GRID_SIZE, 3 * GRID_SIZE);
    EXPECT_EQ(PIX_GRASS1_DAMAGED, static_cast<unsigned char>(bare));
    EXPECT_EQ(PIX_GRASS1_DAMAGED, w.grid.data[3 + w.grid.w * 3]);
    ASSERT_EQ(1u, w.grid_dirty_tiles().size());

    // The decorated cell is exempt: byte untouched, no dirty tile, 0 return
    // (skipping the caller's no-op dirty fold).
    const char shielded = w.damage_tile(6 * GRID_SIZE, 6 * GRID_SIZE);
    EXPECT_EQ(0, shielded);
    EXPECT_EQ(PIX_GRASS1, w.grid.data[6 + w.grid.w * 6])
        << "decor shields the base tile from the charred transform";
    EXPECT_EQ(1u, w.grid_dirty_tiles().size())
        << "no dirty-tile entry for the shielded cell";
}

// A* routing (mirrors the NewTiles lava-strip test): a decor boulder strip
// down grid column 12 (rows 0..14) blocks the straight line for a grounder —
// the solved path detours below and never touches a boulder cell — while a
// flyer crosses the strip directly. Pathfinding consults decor for free
// because the A* neighbor expansion calls query_grid_passable.
TEST(Decor, grounder_routes_around_decor_boulder_strip_flyer_crosses)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& dp = alloc_decor(w, 0);
    for (int gy = 0; gy <= 14; gy++)
        dp.data[12 + gy * dp.w] = DECOR_BOULDER_1;

    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(grounder, nullptr);
    grounder->setxy(6 * GRID_SIZE, 10 * GRID_SIZE);
    grounder->find_path_to_point(19 * GRID_SIZE, 10 * GRID_SIZE);
    ASSERT_FALSE(grounder->path_to_foe.empty())
        << "a route around the boulder strip exists (below row 14)";
    bool detoured = false;
    for (const PathState state : grounder->path_to_foe)
    {
        const int gx = GET_STATE_X(state) / GRID_SIZE;
        const int gy = GET_STATE_Y(state) / GRID_SIZE;
        EXPECT_FALSE(gx == 12 && gy <= 14)
            << "no grounder path node may sit on a decor boulder (" << gx
            << "," << gy << ")";
        if (gy > 14)
            detoured = true;
    }
    EXPECT_TRUE(detoured) << "the only open route passes below the strip";

    walker* flyer = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(flyer, nullptr);
    flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    flyer->setxy(6 * GRID_SIZE, 10 * GRID_SIZE);
    flyer->find_path_to_point(19 * GRID_SIZE, 10 * GRID_SIZE);
    ASSERT_FALSE(flyer->path_to_foe.empty());
    bool crossed = false;
    for (const PathState state : flyer->path_to_foe)
    {
        const int gx = GET_STATE_X(state) / GRID_SIZE;
        const int gy = GET_STATE_Y(state) / GRID_SIZE;
        if (gx == 12 && gy <= 14)
            crossed = true;
    }
    EXPECT_TRUE(crossed) << "a flyer's straight route crosses the strip";
}

// The core invariant, exercised end-to-end: a level whose decor plane is
// allocated but all-zero runs the exact same sim as one with no plane at all
// (same walker tracks, same RNG stream). Sequential paired runs, N ticks.
TEST(Decor, all_zero_decor_plane_is_behavior_identical_to_no_plane)
{
    std::vector<std::pair<short, short>> tracks[2];
    std::uint32_t rng_state[2] = {0, 0};

    for (int variant = 0; variant < 2; ++variant)
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        if (variant == 1)
            alloc_decor(w, 0); // valid, all DECOR_NONE

        walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
        walker* foe = w.add_ob(Order::Living, FAMILY_SKELETON);
        ASSERT_NE(hero, nullptr);
        ASSERT_NE(foe, nullptr);
        hero->setxy(96, 96);
        hero->set_team_num(0);
        foe->setxy(240, 240);
        foe->set_team_num(1);

        for (int tick = 0; tick < 100; ++tick)
            w.tick();

        for (const auto& uptr : w.oblist)
        {
            const walker* ob = uptr.get();
            if (ob != nullptr)
                tracks[variant].emplace_back(ob->xpos(), ob->ypos());
        }
        rng_state[variant] = w.rng_.state_;
    }

    EXPECT_EQ(tracks[0], tracks[1])
        << "an all-zero plane must not move a single walker";
    EXPECT_EQ(rng_state[0], rng_state[1])
        << "an all-zero plane must not consume or shift the sim RNG stream";
}
