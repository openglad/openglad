/* Delayed-spawn wake vs occupied spawn spots.
 *
 * Bug (observed while staging delayed-spawn proof media): when several
 * dormant walkers with EQUAL spawn delays share overlapping footprints, the
 * wake tick must bring ALL of them into the world. No spawn/wake path may
 * ever consume (delete) a dormant walker, and no wake may permanently stack
 * two live walkers on one spot. Same rule for a live generator whose pad
 * holds a dormant walker: the spawn must defer, never delete or stack.
 *
 * Wake-relocation contract (deterministic, RNG-free):
 *   - oblist order decides who wakes first; dormant walkers never block, so
 *     the first member of a mutual-overlap group always wakes in place;
 *   - a waker whose spot is blocked by a LIVE blocking entity relocates to
 *     the nearest clear cell (ring-by-ring, row-major — the
 *     land_on_nearest_clear_cell order) before entering the obmap;
 *   - if no cell within the nudge radius is clear, the wake defers to the
 *     next tick (the walker stays dormant, keeps counting as alive, and
 *     retries; it is never deleted).
 * og_test_parity stays blind to all of this: every branch is gated on a
 * dormant walker being involved, and no golden scenario authors spawn_delay.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace {

// True when the snapshot's oblist carries the entity id.
bool snapshot_has_entity(const og::sim::WorldSnapshot& snap, std::uint32_t id)
{
    return std::any_of(snap.oblist.begin(), snap.oblist.end(),
                       [id](const og::sim::EntitySnapshot& e) {
                           return e.entity_id == id;
                       });
}

// True when the two live walkers' bounding boxes overlap.
bool boxes_overlap(const walker* a, const walker* b)
{
    return a->xpos() + a->sizex() > b->xpos() &&
           a->xpos() < b->xpos() + b->sizex() &&
           a->ypos() + a->sizey() > b->ypos() &&
           a->ypos() < b->ypos() + b->sizey();
}

walker* make_dormant_orc(GameWorld& w, short x, short y, unsigned delay)
{
    walker* ob = w.add_ob(Order::Living, FAMILY_ORC);
    if (ob == nullptr)
        return nullptr;
    ob->setxy(x, y);
    ob->set_team_num(1);
    ob->set_real_team_num(1);
    ob->set_act_type(ACT_GUARD); // hold still so post-wake spots stay pinned
    ob->set_spawn_delay(static_cast<std::uint16_t>(delay));
    ob->set_dormant(true);
    return ob;
}

walker* make_hero(GameWorld& w)
{
    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    if (hero == nullptr)
        return nullptr;
    hero->setxy(2 * GRID_SIZE, 2 * GRID_SIZE);
    hero->set_team_num(0);
    hero->set_real_team_num(0);
    hero->set_act_type(ACT_GUARD);
    return hero;
}

} // namespace

// A packed formation with EQUAL delays and identical footprints: every member
// must survive the wake tick, none may stay dormant, and no two of the woken
// bodies may overlap. Positions are pinned: the first (oblist order) keeps
// the authored spot, later ones take the nearest clear cells in the fixed
// ring/row-major order.
TEST(DelayedSpawnWake, packed_formation_equal_delays_all_wake)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 1u;
    w.my_team = 0;

    walker* hero = make_hero(w);
    ASSERT_NE(nullptr, hero);

    const short spot_x = 10 * GRID_SIZE;
    const short spot_y = 8 * GRID_SIZE;
    std::vector<walker*> orcs;
    std::vector<std::uint32_t> ids;
    for (int i = 0; i < 4; ++i)
    {
        walker* orc = make_dormant_orc(w, spot_x, spot_y, 3);
        ASSERT_NE(nullptr, orc);
        orcs.push_back(orc);
        ids.push_back(orc->entity_id());
    }

    for (int t = 0; t < 3; ++t)
        w.tick(); // level ticks 1..3: all within the delay
    for (walker* orc : orcs)
        ASSERT_TRUE(orc->dormant());

    w.tick(); // level tick 4 > delay 3: the wake tick

    // Every member survived and woke: still oblist-resident, alive, awake,
    // and obmap-registered.
    int found = 0;
    for (const auto& uptr : w.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (std::find(ids.begin(), ids.end(), ob->entity_id()) != ids.end())
            ++found;
    }
    ASSERT_EQ(4, found) << "no formation member may be deleted by the wake";
    for (walker* orc : orcs)
    {
        EXPECT_FALSE(orc->dead()) << "id " << orc->entity_id();
        EXPECT_FALSE(orc->dormant())
            << "equal-delay member must wake on the shared tick, id "
            << orc->entity_id();
        EXPECT_NE(w.myobmap->walker_to_pos.end(),
                  w.myobmap->walker_to_pos.find(orc))
            << "woken member must be obmap-registered, id " << orc->entity_id();
    }

    // No two woken bodies overlap.
    for (size_t i = 0; i < orcs.size(); ++i)
        for (size_t j = i + 1; j < orcs.size(); ++j)
            EXPECT_FALSE(boxes_overlap(orcs[i], orcs[j]))
                << "members " << i << " and " << j << " overlap at ("
                << orcs[i]->xpos() << "," << orcs[i]->ypos() << ") / ("
                << orcs[j]->xpos() << "," << orcs[j]->ypos() << ")";

    // Deterministic placement pins (ring search, row-major): the first member
    // keeps the authored spot; the rest fill the ring-1 perimeter cells in
    // the fixed row-major order — for one-cell (16x16) walkers that is the
    // row above, left to right.
    const std::vector<std::pair<short, short>> expected = {
        {spot_x, spot_y},
        {static_cast<short>(spot_x - GRID_SIZE),
         static_cast<short>(spot_y - GRID_SIZE)},
        {spot_x, static_cast<short>(spot_y - GRID_SIZE)},
        {static_cast<short>(spot_x + GRID_SIZE),
         static_cast<short>(spot_y - GRID_SIZE)},
    };
    for (size_t i = 0; i < orcs.size(); ++i)
    {
        EXPECT_EQ(expected[i].first, orcs[i]->xpos()) << "member " << i;
        EXPECT_EQ(expected[i].second, orcs[i]->ypos()) << "member " << i;
    }

    // All woken members appear in the keyframe snapshot: mirrors reconcile
    // every copy awake, so the HUD's pending "(+n)" count drains to zero.
    const og::sim::WorldSnapshot snap = og::sim::capture_keyframe_snapshot(w);
    for (std::uint32_t id : ids)
        EXPECT_TRUE(snapshot_has_entity(snap, id))
            << "woken member missing from snapshots, id " << id;
}

// The two-walker mutual-block case: EXACT overlap, equal delays. Both must
// be alive and awake after the wake tick (the first in place, the second on
// the nearest clear cell) — deterministic across runs.
TEST(DelayedSpawnWake, exact_overlap_pair_both_survive_wake)
{
    auto run = [](std::vector<std::pair<short, short>>* out_positions) {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.rng_.state_ = 7u;
        w.my_team = 0;

        walker* hero = make_hero(w);
        ASSERT_NE(nullptr, hero);

        walker* a = make_dormant_orc(w, 12 * GRID_SIZE, 10 * GRID_SIZE, 2);
        walker* b = make_dormant_orc(w, 12 * GRID_SIZE, 10 * GRID_SIZE, 2);
        ASSERT_NE(nullptr, a);
        ASSERT_NE(nullptr, b);

        w.tick();
        w.tick();
        w.tick(); // level tick 3 > delay 2: wake

        EXPECT_FALSE(a->dead());
        EXPECT_FALSE(b->dead());
        EXPECT_FALSE(a->dormant());
        EXPECT_FALSE(b->dormant());
        EXPECT_FALSE(boxes_overlap(a, b));
        // The first keeps the authored spot.
        EXPECT_EQ(12 * GRID_SIZE, a->xpos());
        EXPECT_EQ(10 * GRID_SIZE, a->ypos());
        if (out_positions != nullptr)
        {
            out_positions->push_back({a->xpos(), a->ypos()});
            out_positions->push_back({b->xpos(), b->ypos()});
        }
    };

    std::vector<std::pair<short, short>> first;
    std::vector<std::pair<short, short>> second;
    run(&first);
    run(&second);
    EXPECT_EQ(first, second)
        << "wake relocation must be deterministic across identical runs";
}

// A live generator whose pad holds a dormant walker: the generator must not
// delete the dormant walker, and must not stack its spawn on top of it — the
// spawn defers exactly like a blocked pad. Once the dormant walker wakes and
// leaves, the generator resumes spawning as before.
TEST(DelayedSpawnWake, generator_pad_dormant_walker_survives)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 3u;
    w.my_team = 0;

    walker* hero = make_hero(w);
    ASSERT_NE(nullptr, hero);

    walker* gen = w.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen);
    gen->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    gen->set_team_num(1);
    ASSERT_NE(nullptr, gen->stats());
    gen->stats()->set_level(5);

    // Park a dormant walker with a LONG delay right on the generator's pads
    // (generators fire one step in a random direction, so cover the pad ring
    // by making the lurker big enough to matter: place it adjacent).
    walker* lurker = make_dormant_orc(
        w, static_cast<short>(gen->xpos() + gen->sizex() + 1), gen->ypos(),
        60000);
    ASSERT_NE(nullptr, lurker);
    const std::uint32_t lurker_id = lurker->entity_id();

    // Run long enough for many spawn attempts. A roamer may legally WALK
    // over the dormant cell (dormancy = intangible), so the stacking check
    // applies to each spawn exactly once: at the tick it is first seen (its
    // birth placement). Checking only the end state would miss a stacked
    // spawn that wanders off before the finish line.
    int spawned_livings = 0;
    std::set<std::uint32_t> seen;
    for (int t = 0; t < 400; ++t)
    {
        w.tick();
        for (const auto& uptr : w.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob == lurker || ob == hero || ob->dead() ||
                ob->query_order() != Order::Living || ob->dormant())
                continue;
            if (!seen.insert(ob->entity_id()).second)
                continue; // birth placement already checked
            ++spawned_livings; // a TENT spawn (skeleton) placed alive
            ASSERT_FALSE(boxes_overlap(ob, lurker))
                << "tick " << t << ": spawn (family "
                << static_cast<int>(ob->family()) << ", id "
                << ob->entity_id()
                << ") was PLACED onto the dormant walker's footprint";
        }
    }

    // The dormant walker survived, untouched: still dormant, still parked.
    walker* still = w.find_by_id(lurker_id);
    ASSERT_NE(nullptr, still)
        << "a generator spawn probe must never delete a dormant walker";
    EXPECT_EQ(lurker, still);
    EXPECT_TRUE(still->dormant());
    EXPECT_FALSE(still->dead());
    // The generator itself stayed productive: pads away from the dormant
    // walker still spawn (the gate only defers spawns that overlap it).
    EXPECT_GT(spawned_livings, 0)
        << "generator must keep spawning on its clear pads";
}

// Deferred wake has no deadlock: a waker whose whole nudge radius is blocked
// stays dormant (alive, retrying) and wakes as soon as a cell frees up.
TEST(DelayedSpawnWake, blocked_wake_defers_then_completes)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 9u;
    w.my_team = 0;

    walker* hero = make_hero(w);
    ASSERT_NE(nullptr, hero);

    // Wall the waker in: a solid block of live hold-post guards covering
    // every cell of the nudge radius around the spot (one guard per cell —
    // the fixture's walkers are one cell wide).
    const short cx = 12;
    const short cy = 12;
    std::vector<walker*> wall;
    for (int gy = cy - 5; gy <= cy + 5; ++gy)
    {
        for (int gx = cx - 5; gx <= cx + 5; ++gx)
        {
            walker* g = w.add_ob(Order::Living, FAMILY_SKELETON);
            ASSERT_NE(nullptr, g);
            g->setxy(static_cast<short>(gx * GRID_SIZE),
                     static_cast<short>(gy * GRID_SIZE));
            g->set_team_num(1);
            g->set_real_team_num(1);
            g->set_act_type(ACT_GUARD);
            g->set_guard_hold_post(true);
            wall.push_back(g);
        }
    }

    walker* waker = make_dormant_orc(w, static_cast<short>(cx * GRID_SIZE),
                                     static_cast<short>(cy * GRID_SIZE), 1);
    ASSERT_NE(nullptr, waker);

    w.tick();
    w.tick(); // wake tick: spot + all nudge cells blocked -> defer
    EXPECT_FALSE(waker->dead());
    ASSERT_TRUE(waker->dormant())
        << "with the whole nudge radius blocked, the wake must defer (stay "
           "dormant), not stack";

    // Deferral is not deletion: open the pad by removing the wall, and the
    // per-tick retry completes the wake.
    for (walker* g : wall)
    {
        g->set_dead(1);
        g->myguy = nullptr;
    }
    w.tick(); // sweeps the wall; retry may still see stale blockers this tick
    w.tick(); // retry tick: pad clear now
    EXPECT_FALSE(waker->dormant())
        << "a deferred wake must complete once the pad clears";
    EXPECT_FALSE(waker->dead());
    EXPECT_NE(w.myobmap->walker_to_pos.end(),
              w.myobmap->walker_to_pos.find(waker));
}

// The network-mirror phantom: dormant walkers are excluded from snapshots, so
// a mirror keeps its own level-load copies dormant until the authoritative
// side's snapshot shows them awake. After the shared wake tick, applying the
// server's snapshot to an identically-constructed mirror must leave ZERO
// dormant copies — the HUD's pending "(+n)" count drains completely instead
// of lingering as a phantom.
TEST(DelayedSpawnWake, mirror_dormant_pending_drains_to_zero_after_wake)
{
    auto build = [](TestGameWorld& tw) {
        GameWorld& w = tw.world();
        w.rng_.state_ = 5u;
        w.my_team = 0;
        ASSERT_NE(nullptr, make_hero(w));
        for (int i = 0; i < 3; ++i)
            ASSERT_NE(nullptr,
                      make_dormant_orc(w, 9 * GRID_SIZE, 9 * GRID_SIZE, 2));
    };

    TestGameWorld server_tw;
    build(server_tw);
    GameWorld& server = server_tw.world();

    TestGameWorld mirror_tw(1);
    build(mirror_tw);
    GameWorld& mirror = mirror_tw.world();

    auto dormant_count = [](const GameWorld& w) {
        int n = 0;
        for (const auto& uptr : w.oblist)
            if (uptr != nullptr && !uptr->dead() && uptr->dormant())
                ++n;
        return n;
    };
    ASSERT_EQ(3, dormant_count(mirror));

    server.tick();
    server.tick();
    server.tick(); // level tick 3 > delay 2: the shared wake tick

    ASSERT_EQ(0, dormant_count(server))
        << "every equal-delay member must wake on the shared tick";

    og::sim::apply_snapshot(mirror,
                            og::sim::capture_keyframe_snapshot(server));
    EXPECT_EQ(0, dormant_count(mirror))
        << "the mirror's pending (+n) copies must all wake with the server";
}
