// lib/mode_strip.lua — the per-team MAP UNITS strip the six mode impls
// share (docs/lineup-design.md B4, succeeding the global TROOPS: OWN
// strip).
//
// The rule: for each score team whose map_units_N box reads OFF
// (non-zero), every authored living and generator with no roster guy on
// that team is retired — generators follow the same box (Onslaught's
// foundries included). Wildlife (teams at or beyond the score range) has
// no box and always stands — the one deliberate narrowing against the
// retired OWN strip, which took the wildlife too.
//
// The levels are registered by kTestRegistrationLua in modes_pack_fixture.h,
// which zips the CURRENT pack sources — so this suite runs the committed
// mode_strip.lua, not the copy inside builtin/modes.glad.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace og::modes_test;

namespace {

using ModesStrip = ModesPackTest;

// A rig with two score teams (0 and 1), a roster fighter on team 0, canned
// troops on score teams 0/1/2, wildlife on team 5, and one generator per
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

    explicit StripRig(int level_id, const std::array<short, 4>& map_units)
        : fx(level_id)
    {
        fx.world().ctf_requested_map_units = map_units;
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

constexpr short kOn = og::sim::kMapUnitsOn;
constexpr short kOff = og::sim::kMapUnitsOff;

} // namespace

TEST_F(ModesStrip, boxes_on_leave_every_authored_entity_alone)
{
    StripRig rig(kStripLevelDefault, {kOn, kOn, kOn, kOn});
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster));
    EXPECT_TRUE(rig.alive(rig.troop0));
    EXPECT_TRUE(rig.alive(rig.troop1));
    EXPECT_TRUE(rig.alive(rig.troop2));
    EXPECT_TRUE(rig.alive(rig.wildlife));
    EXPECT_TRUE(rig.alive(rig.gen0));
    EXPECT_TRUE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t0")) << "all-on returns early";
}

TEST_F(ModesStrip, one_box_off_strips_exactly_that_team)
{
    StripRig rig(kStripLevelDefault, {kOn, kOff, kOn, kOn});
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster));
    EXPECT_TRUE(rig.alive(rig.troop0)) << "team 0's box is on";
    EXPECT_FALSE(rig.alive(rig.troop1)) << "team 1's troops go";
    EXPECT_TRUE(rig.alive(rig.troop2)) << "team 2's box is on";
    EXPECT_TRUE(rig.alive(rig.wildlife));
    EXPECT_TRUE(rig.alive(rig.gen0));
    EXPECT_FALSE(rig.alive(rig.gen1))
        << "generators follow the same box (B4)";
    EXPECT_TRUE(rig.logged("stripped\t2"));
}

TEST_F(ModesStrip, all_boxes_off_strip_every_score_team_but_never_wildlife)
{
    StripRig rig(kStripLevelDefault, {kOff, kOff, kOff, kOff});
    rig.fx.tick(1);

    EXPECT_TRUE(rig.alive(rig.roster)) << "a roster walker is never stripped";
    EXPECT_FALSE(rig.alive(rig.troop0));
    EXPECT_FALSE(rig.alive(rig.troop1));
    EXPECT_FALSE(rig.alive(rig.troop2))
        << "the active team mask does not gate the strip";
    EXPECT_TRUE(rig.alive(rig.wildlife))
        << "wildlife has no box and always stands";
    EXPECT_FALSE(rig.alive(rig.gen0));
    EXPECT_FALSE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.logged("stripped\t5"));
}

TEST_F(ModesStrip, junk_box_values_read_as_off)
{
    // Junk can only reach the sim from a save or a peer the lobby
    // sanitizer did not clean. Non-zero reads as OFF, so a crafted value
    // can never field units the host's box hid.
    StripRig rig(kStripLevelDefault, {kOn, 9, kOn, kOn});
    rig.fx.tick(1);

    EXPECT_FALSE(rig.alive(rig.troop1));
    EXPECT_FALSE(rig.alive(rig.gen1));
    EXPECT_TRUE(rig.alive(rig.troop0));
    EXPECT_TRUE(rig.logged("stripped\t2"));
}

TEST_F(ModesStrip, stripped_entities_leave_the_score_team_range)
{
    // The corpse-hygiene move: retiring off the score-team range BEFORE
    // set_dead keeps a same-tick death scan from respawning the strip.
    StripRig rig(kStripLevelDefault, {kOff, kOff, kOff, kOff});
    rig.fx.tick(1);

    ASSERT_FALSE(rig.alive(rig.troop0));
    EXPECT_GE(static_cast<int>(rig.troop0->team_num()),
              static_cast<int>(SCORE_TEAM_COUNT));
    EXPECT_GE(static_cast<int>(rig.troop1->team_num()),
              static_cast<int>(SCORE_TEAM_COUNT));
}

TEST_F(ModesStrip, is_troop_classifies_orders_with_no_generator_exception)
{
    StripRig rig(kStripLevelDefault, {kOn, kOn, kOn, kOn});
    rig.fx.tick(1);

    // living=1, generator=1 (the foundry exception died with the global
    // strip — generators follow the box, B4), treasure=0.
    EXPECT_TRUE(rig.logged("is_troop\t1\t1\t0"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
