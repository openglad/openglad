#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <cstdint>

TEST(StatsHitResponseMore, wounded_world_member_recruits_allies_and_keeps_its_escape)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();
    walker* victim = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* attacker = world.add_ob(Order::Living, FAMILY_ORC);
    walker* ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, attacker);
    ASSERT_NE(nullptr, ally);
    victim->set_specials_disabled(true);
    victim->set_team_num(0);
    ally->set_team_num(0);
    attacker->set_team_num(1);
    ASSERT_TRUE(victim->setxy(160, 160));
    ASSERT_TRUE(attacker->setxy(180, 180));
    ASSERT_TRUE(ally->setxy(120, 160));
    victim->stats()->set_hitpoints(1.0f);
    victim->set_yo_delay(0);
    victim->set_foe(nullptr);
    victim->set_leader(nullptr);
    attacker->set_foe(nullptr);
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);
    victim->stats()->set_last_distance(7);
    victim->stats()->set_current_distance(7);
    ally->stats()->set_last_distance(9);
    ally->stats()->set_current_distance(9);
    victim->stats()->force_command(COMMAND_WALK, 3, 1, 1);

    victim->stats()->hit_response(attacker);

    EXPECT_EQ(nullptr, victim->leader()) << "the yeller must not recruit itself";
    EXPECT_EQ(attacker, victim->foe());
    EXPECT_EQ(victim, attacker->foe()) << "a new attacker gets the reciprocal target";
    EXPECT_EQ(victim, ally->leader());
    EXPECT_EQ(attacker, ally->foe());
    EXPECT_EQ(32000u, victim->stats()->last_distance());
    EXPECT_EQ(32000, victim->stats()->current_distance());
    EXPECT_EQ(32000u, ally->stats()->last_distance());
    EXPECT_EQ(32000, ally->stats()->current_distance());
    EXPECT_EQ(80, victim->yo_delay());
    ASSERT_EQ(1u, victim->stats()->commands.size());
    const command& escape = victim->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, escape.commandtype);
    EXPECT_EQ(16, escape.commandcount);
    EXPECT_EQ(-1, escape.com1);
    EXPECT_EQ(-1, escape.com2);
    EXPECT_TRUE(escape.forced);

    // The same attacker during the yell cooldown must neither recruit again
    // nor replace the walk the caller has already queued.
    victim->stats()->clear_command();
    victim->stats()->force_command(COMMAND_WALK, 7, 1, 0);
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);
    victim->stats()->hit_response(attacker);
    EXPECT_EQ(80, victim->yo_delay());
    EXPECT_EQ(nullptr, ally->foe());
    EXPECT_EQ(nullptr, ally->leader());
    ASSERT_EQ(1u, victim->stats()->commands.size());
    const command& retained = victim->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, retained.commandtype);
    EXPECT_EQ(7, retained.commandcount);
    EXPECT_EQ(1, retained.com1);
    EXPECT_EQ(0, retained.com2);
    EXPECT_TRUE(retained.forced);
    world.delete_objects();
}

// myscreen is now a macro defined in base.h (via game_session.h)

// The archer's hit_response (packs/core/families/living-02-archer.lua:64-79)
// makes no og.* draw and returns before check_special(), so this file pins the
// SIM stream by its ABSENCE: the world LCG must stand still across the call.
// The deleted GameContext/FixedRandom guard could not have pinned anything
// here -- og.rand reaches current_game->world->rng_, never GameContext::rng.

TEST(StatsHitResponseMore, statistics_hit_response_archer_runs_away_and_queues_walk)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* archer = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(archer != nullptr && foe != nullptr) << "actors created";
    if (!(archer && foe))
        return;

    archer->setxy(100, 100);
    foe->setxy(120, 100); // within < 64 distance
    archer->stats()->clear_command();
    archer->set_foe(nullptr);

    // A no-draw pin is vacuous while any override is installed: SimRandom::next
    // never touches state_ then (include/openglad/gameplay/game_world.h:63-66).
    ASSERT_EQ(nullptr, og::sim::sim_random_override())
        << "no scripted RNG may be in scope, or the no-draw pin below is vacuous";
    const std::uint32_t lcg_before = og::runtime::current_session->myscreen_->world().rng_.state_;

    archer->stats()->hit_response(foe);

    EXPECT_EQ(lcg_before, og::runtime::current_session->myscreen_->world().rng_.state_)
        << "the archer hook (living-02-archer.lua hit_response) makes no og.* draw "
           "and returns before check_special(): the sim LCG must not step";

    // packs/core/families/living-02-archer.lua:65-79: a new attacker is taken
    // as the foe with both distance caches reset to 15000, and a foe closer
    // than melee_backpedal_range (64) forces a WALK of 8 in the sign of
    // (self - foe), i.e. due west from (100,100) with the orc at (120,100).
    EXPECT_EQ(foe, archer->foe()) << "the hit archer takes its attacker as foe";
    EXPECT_EQ(15000u, archer->stats()->last_distance())
        << "the Lua response resets the distance caches to 15000";
    EXPECT_EQ(15000, archer->stats()->current_distance())
        << "the Lua response resets the distance caches to 15000";

    ASSERT_EQ(1u, archer->stats()->commands.size())
        << "archer hit_response should queue exactly one walk-away command";
    const command& away = archer->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, away.commandtype) << "the back-pedal is a walk";
    EXPECT_EQ(8, away.commandcount) << "the archer back-pedals for 8 rounds";
    EXPECT_EQ(-1, away.com1) << "it walks AWAY from the foe: sign(100-120) == -1";
    EXPECT_EQ(0, away.com2) << "no north/south component: sign(100-100) == 0";
    EXPECT_TRUE(away.forced) << "s_force_command marks the entry forced";

    // Control: the same hit from beyond melee_backpedal_range retargets but
    // queues no back-pedal at all.
    archer->set_foe(nullptr);
    archer->stats()->clear_command();
    foe->setxy(100 + 200, 100);
    archer->stats()->hit_response(foe);
    EXPECT_EQ(foe, archer->foe()) << "a distant attacker is still taken as foe";
    EXPECT_FALSE(archer->stats()->has_commands())
        << "no back-pedal is queued from outside melee_backpedal_range";

    og::runtime::current_session->myscreen_->world().delete_objects();
}
