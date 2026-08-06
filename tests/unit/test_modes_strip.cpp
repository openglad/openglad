// lib/mode_strip.lua — the scenario-troops strip the six mode impls share.
//
// The two states of og.match_setting("strip_troops"):
//   0 keep the level as authored ("TROOPS: ALL"),
//   2 strip every authored living/generator on ANY team (wildlife too) —
//     "TROOPS: OWN" — minus the generators when the caller passes
//     keep_generators (Onslaught's foundries are the board, not troops).
// Anything above 0 means OWN, so a stored 1 from the retired middle state
// strips everything too.
//
// The levels are registered by kTestRegistrationLua in modes_pack_fixture.h,
// which zips the CURRENT pack sources — so this suite runs the committed
// mode_strip.lua, not the copy inside builtin/modes.glad.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace og::modes_test;

namespace {

using ModesStrip = ModesPackTest;

// A rig with two score teams (0 and 1), a roster fighter on team 0, canned
// troops on both score teams, wildlife on team 5, and one generator per
// score team.
struct StripRig
{
    ModesCtfWorld fx;
    walker* roster = nullptr;
    walker* troop0 = nullptr;
    walker* troop1 = nullptr;
    walker* troop2 = nullptr;   // score team with no anchor: outside the mask
    walker* wildlife = nullptr;
    walker* gen0 = nullptr;
    walker* gen1 = nullptr;

    explicit StripRig(int level_id, short strip_setting)
        : fx(level_id)
    {
        fx.world().ctf_requested_strip_scenario_troops = strip_setting;
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 480, 96);
        roster = fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 128, 1);
        troop0 = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
        troop1 = fx.spawn_living(FAMILY_ORC, 1, 448, 160);
        troop2 = fx.spawn_living(FAMILY_ORC, 2, 448, 320);
        wildlife = fx.spawn_living(FAMILY_SMALL_SLIME, 5, 300, 300);
        gen0 = fx.spawn_generator(FAMILY_TENT, 0, 200, 200);
        gen1 = fx.spawn_generator(FAMILY_TENT, 1, 400, 200);
    }

    bool alive(const walker* w) const { return w != nullptr && !w->dead(); }

    // og.log lines the probe hook emitted, tab-joined by the binding.
    bool logged(const std::string& line)
    {
        const std::vector<std::string>& lines =
            fx.world().scripts().host().log();
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    }
};

} // namespace

TEST_F(ModesStrip, keep_leaves_every_authored_entity_alone)
{
    StripRig rig(kStripLevelDefault, 0);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster));
    EXPECT_TRUE(rig.alive(rig.troop0));
    EXPECT_TRUE(rig.alive(rig.troop1));
    EXPECT_TRUE(rig.alive(rig.wildlife));
    EXPECT_TRUE(rig.alive(rig.gen0));
    EXPECT_TRUE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t0")) << "state 0 returns early";
}

TEST_F(ModesStrip, legacy_middle_state_strips_everything_too)
{
    // State 1 was "strip only the roster teams' canned troops" before the
    // toggle went two-state. A save written by that build still loads, and
    // the value now means the same thing as the 2 the menus write.
    StripRig rig(kStripLevelDefault, 1);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster)) << "a roster walker is never stripped";
    EXPECT_FALSE(rig.alive(rig.troop0));
    EXPECT_FALSE(rig.alive(rig.troop1)) << "no longer a roster-team rule";
    EXPECT_FALSE(rig.alive(rig.troop2));
    EXPECT_FALSE(rig.alive(rig.wildlife));
    EXPECT_FALSE(rig.alive(rig.gen0));
    EXPECT_FALSE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t6"));
}

TEST_F(ModesStrip, strip_all_takes_wildlife_and_generators_too)
{
    StripRig rig(kStripLevelDefault, 2);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster));
    EXPECT_FALSE(rig.alive(rig.troop0));
    EXPECT_FALSE(rig.alive(rig.troop1));
    EXPECT_FALSE(rig.alive(rig.troop2))
        << "the active team mask does not gate the strip";
    EXPECT_FALSE(rig.alive(rig.wildlife)) << "\"ALL\" means all";
    EXPECT_FALSE(rig.alive(rig.gen0));
    EXPECT_FALSE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t6"));
}

TEST_F(ModesStrip, strip_all_keeps_generators_when_the_caller_asks)
{
    // Onslaught's arm: the foundries ARE the board.
    StripRig rig(kStripLevelKeepGens, 2);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster));
    EXPECT_FALSE(rig.alive(rig.troop0));
    EXPECT_FALSE(rig.alive(rig.troop1));
    EXPECT_FALSE(rig.alive(rig.wildlife));
    EXPECT_TRUE(rig.alive(rig.gen0)) << "keep_generators must spare foundries";
    EXPECT_TRUE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t4"));
}

TEST_F(ModesStrip, explicit_keep_generators_false_strips_them)
{
    StripRig rig(kStripLevelDropGens, 2);
    rig.fx.tick(1);

    EXPECT_FALSE(rig.alive(rig.gen0));
    EXPECT_FALSE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t6"));
}

TEST_F(ModesStrip, out_of_range_settings_read_as_keep_or_own_by_sign)
{
    // Junk can only reach the sim from a save or a peer the lobby sanitizer
    // did not clean. The rule is a threshold, not a value list: anything
    // above keep strips, anything at or below it keeps.
    StripRig high(kStripLevelDefault, 9);
    high.fx.tick(1);
    EXPECT_FALSE(high.alive(high.troop0));
    EXPECT_FALSE(high.alive(high.wildlife));
    EXPECT_TRUE(high.logged("stripped\t6"));

    StripRig low(kStripLevelDefault, -3);
    low.fx.tick(1);
    EXPECT_TRUE(low.alive(low.troop0));
    EXPECT_TRUE(low.alive(low.wildlife));
    EXPECT_TRUE(low.logged("stripped\t0"));
}

TEST_F(ModesStrip, stripped_entities_leave_the_score_team_range)
{
    // The corpse-hygiene move: retiring off the score-team range BEFORE
    // set_dead keeps a same-tick death scan from respawning the strip.
    StripRig rig(kStripLevelDefault, 2);
    rig.fx.tick(1);

    ASSERT_FALSE(rig.alive(rig.troop0));
    EXPECT_GE(static_cast<int>(rig.troop0->team_num()),
              static_cast<int>(SCORE_TEAM_COUNT));
    EXPECT_GE(static_cast<int>(rig.troop1->team_num()),
              static_cast<int>(SCORE_TEAM_COUNT));
}

TEST_F(ModesStrip, is_troop_classifies_orders_and_the_generator_exception)
{
    StripRig rig(kStripLevelDefault, 0);
    rig.fx.tick(1);

    // living(keep_gens=true)=1, generator(keep)=0, generator(drop)=1,
    // treasure=0.
    EXPECT_TRUE(rig.logged("is_troop\t1\t0\t1\t0"));
    EXPECT_TRUE(rig.logged("states\t0\t2"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
