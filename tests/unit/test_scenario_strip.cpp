/* TROOPS: NONE on a classic (non-scripted) map.
 *
 * The scripted modes strip from Lua at on_mode_init; classic maps have no
 * mode script, so og::sim::classic_strip_authored_troops does it and
 * GameWorld::tick latches it on the level's first classic tick.
 */

#include <gtest/gtest.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/scenario_strip.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "test_game_world_fixture.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct StripFixture : TestGameWorld
{
    using TestGameWorld::TestGameWorld;

    walker* spawn_living(int family, int team, const char* name = nullptr)
    {
        walker* w = world().add_ob(Order::Living, family);
        w->setxy(160, 160);
        w->set_team_num(static_cast<unsigned char>(team));
        if (name != nullptr && w->stats() != nullptr)
            w->stats()->name = name;
        return w;
    }

    walker* spawn_generator(int family, int team)
    {
        walker* w = world().add_ob(Order::Generator, family);
        w->setxy(256, 256);
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }

    // A roster fighter: a walker with a guy attached, the way a deployed
    // company member arrives.
    walker* spawn_roster_member(int family, int team)
    {
        walker* w = spawn_living(family, team);
        roster_.push_back(std::make_unique<guy>(family));
        w->myguy = roster_.back().get();
        return w;
    }

    int count_order(Order order) const
    {
        int n = 0;
        for (const auto& uptr : world().oblist)
        {
            if (uptr != nullptr && uptr->query_order() == order)
                n++;
        }
        return n;
    }

    bool has_named(const char* name) const
    {
        for (const auto& uptr : world().oblist)
        {
            if (uptr != nullptr && uptr->stats() != nullptr &&
                uptr->stats()->name == name)
            {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::unique_ptr<guy>> roster_;
};

} // namespace

TEST(ScenarioStripClassic, removes_authored_livings_and_generators)
{
    StripFixture fx;
    fx.spawn_living(FAMILY_ORC, 1);
    fx.spawn_living(FAMILY_SKELETON, 2);
    fx.spawn_living(FAMILY_SMALL_SLIME, 5); // wildlife team: "ALL" means all
    fx.spawn_generator(FAMILY_TENT, 1);
    walker* const roster = fx.spawn_roster_member(FAMILY_SOLDIER, 0);

    EXPECT_EQ(4, og::sim::classic_strip_authored_troops(fx.world()));

    EXPECT_EQ(1, fx.count_order(Order::Living)) << "only the roster guy stays";
    EXPECT_EQ(0, fx.count_order(Order::Generator));
    ASSERT_FALSE(fx.world().oblist.empty());
    EXPECT_EQ(roster, fx.world().oblist.front().get());
}

TEST(ScenarioStripClassic, protected_npcs_survive)
{
    StripFixture fx;
    walker* const guard = fx.spawn_living(FAMILY_SOLDIER, 3, "REEVE");
    guard->set_save_all_protected(true);
    fx.spawn_living(FAMILY_ORC, 1, "MOOK");
    walker* const protected_generator = fx.spawn_generator(FAMILY_TENT, 3);
    protected_generator->set_save_all_protected(true);

    EXPECT_EQ(1, og::sim::classic_strip_authored_troops(fx.world()));

    EXPECT_TRUE(fx.has_named("REEVE")) << "the protected bit is the exemption";
    EXPECT_FALSE(fx.has_named("MOOK"));
    EXPECT_EQ(1, fx.count_order(Order::Generator))
        << "a protected generator is protected too";
}

TEST(ScenarioStripClassic, no_troops_removes_nothing)
{
    StripFixture fx;
    fx.spawn_roster_member(FAMILY_SOLDIER, 0);
    EXPECT_EQ(0, og::sim::classic_strip_authored_troops(fx.world()));
    EXPECT_EQ(1, fx.count_order(Order::Living));
}

TEST(ScenarioStripClassic, cross_references_into_removed_walkers_are_cleared)
{
    // The engine's own stale-pointer pass only nulls pointers to walkers
    // that are DEAD but still allocated. A removed walker is FREED, so the
    // strip has to scrub the references itself or the next tick reads freed
    // memory (this is the test that would trip under ASan if it did not).
    StripFixture fx;
    walker* const survivor = fx.spawn_roster_member(FAMILY_SOLDIER, 0);
    walker* const doomed_foe = fx.spawn_living(FAMILY_ORC, 1);
    walker* const doomed_leader = fx.spawn_living(FAMILY_ORC, 1);

    survivor->set_foe(doomed_foe);
    survivor->set_leader(doomed_leader);
    survivor->set_owner(doomed_foe);
    survivor->set_collide_ob(doomed_leader);
    ASSERT_NE(nullptr, survivor->stats());
    survivor->stats()->set_controller(doomed_foe);

    EXPECT_EQ(2, og::sim::classic_strip_authored_troops(fx.world()));

    EXPECT_EQ(nullptr, survivor->foe());
    EXPECT_EQ(nullptr, survivor->leader());
    EXPECT_EQ(nullptr, survivor->owner());
    EXPECT_EQ(nullptr, survivor->collide_ob());
    EXPECT_EQ(nullptr, survivor->stats()->controller());
}

TEST(ScenarioStripClassic, removal_leaves_no_corpse_for_classic_respawn)
{
    // Removal, not set_dead: the classic respawn engine works off corpses,
    // so a stripped fighter cannot be resurrected by "Respawns: Everyone".
    StripFixture fx;
    fx.spawn_roster_member(FAMILY_SOLDIER, 0);
    fx.spawn_living(FAMILY_ORC, 1);
    const std::size_t dead_before = fx.world().dead_list.size();

    EXPECT_EQ(1, og::sim::classic_strip_authored_troops(fx.world()));
    EXPECT_EQ(dead_before, fx.world().dead_list.size())
        << "a stripped fighter must not become a corpse";

    fx.world().respawn_mode = og::sim::kRespawnModeEveryone;
    for (int i = 0; i < 5; ++i)
        fx.world().tick();
    EXPECT_EQ(1, fx.count_order(Order::Living))
        << "classic respawn must not resurrect a stripped fighter";
}

TEST(ScenarioStripClassic, tick_latches_the_sweep_once_on_a_classic_map)
{
    StripFixture fx;
    fx.world().type = 0; // classic: no TYPE_SCRIPTED bit
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_roster_member(FAMILY_SOLDIER, 0);
    fx.spawn_living(FAMILY_ORC, 1);
    fx.spawn_generator(FAMILY_TENT, 1);

    fx.world().tick();
    EXPECT_EQ(1, fx.count_order(Order::Living));
    EXPECT_EQ(0, fx.count_order(Order::Generator));

    // Latched: a fighter arriving later (a script spawn, a joiner's guy) is
    // not swept again on subsequent ticks.
    fx.spawn_living(FAMILY_ORC, 1);
    fx.world().tick();
    fx.world().tick();
    EXPECT_EQ(2, fx.count_order(Order::Living));
}

TEST(ScenarioStripClassic, tick_leaves_the_world_alone_for_states_0_and_1)
{
    for (const short state : {short{0}, short{1}})
    {
        StripFixture fx;
        fx.world().type = 0;
        fx.world().ctf_requested_strip_scenario_troops = state;
        fx.spawn_living(FAMILY_ORC, 1);
        fx.spawn_generator(FAMILY_TENT, 1);

        fx.world().tick();
        EXPECT_EQ(1, fx.count_order(Order::Living)) << "state " << state;
        EXPECT_EQ(1, fx.count_order(Order::Generator)) << "state " << state;
    }
}

TEST(ScenarioStripClassic, scripted_maps_are_left_to_their_mode_script)
{
    // A TYPE_SCRIPTED map runs lib/mode_strip.lua from on_mode_init; the
    // engine arm must not double-strip it.
    StripFixture fx;
    fx.world().type = GameWorld::TYPE_SCRIPTED;
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_living(FAMILY_ORC, 1);
    fx.spawn_generator(FAMILY_TENT, 1);

    fx.world().tick();
    EXPECT_EQ(1, fx.count_order(Order::Living));
    EXPECT_EQ(1, fx.count_order(Order::Generator));
}
