// The on_mode_plan engine seam (match_plan.h): the one census producer
// (build_match_plan_inputs), the plain-data marshaling into the plan's
// inputs table, the plan-dispatch fence (every world og.* entry errors
// during a plan), the parsed MatchPlanSummary, and the chained plan->init
// dispatch in hooks::level_mode_init (no-hook / nil-plan / plan-error /
// plan-table interleavings, with the level_damage_gate stack discipline).
// Runs on the shared modes-pack harness (tests/modes_pack_fixture.h) whose
// zz_modes_test.lua registers the 9800-9808 probe hooks.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/match_plan.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <string>
#include <vector>

using namespace og::modes_test;
using og::sim::MatchPlanFill;
using og::sim::MatchPlanInputs;
using og::sim::MatchPlanSummary;

namespace {

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

int count_log_lines(GameWorld& world, const std::string& prefix)
{
    int count = 0;
    for (const auto& line : world.scripts().host().log())
    {
        if (line.rfind(prefix, 0) == 0)
            count++;
    }
    return count;
}

// Synthetic inputs with every field distinct, so a swapped or dropped
// marshal lands on a wrong exact value below.
MatchPlanInputs probe_inputs()
{
    MatchPlanInputs in;
    in.team_count = 5;
    in.strip_troops = 3;
    in.score_limit = 7;
    in.respawn_ticks = 90;
    in.teams[0].anchors = 2;
    in.teams[1].roster = 3;
    in.teams[2].npcs = 4;
    in.teams[3].generators = 6;
    in.flags.push_back({1, 5});
    in.flags.push_back({7, 0});
    return in;
}

}  // namespace

using MatchPlan = ModesPackTest;

// ===========================================================================
// build_match_plan_inputs — the census
// ===========================================================================

TEST_F(MatchPlan, census_projects_the_world)
{
    ASSERT_EQ(og::sim::kMatchPlanFlagFamily, flag_family_)
        << "the modes pack must claim the flag wire byte the census reads";

    ModesCtfWorld fx(9800);
    fx.world().ctf_requested_team_count = 3;
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.world().ctf_requested_capture_limit = 7;
    fx.world().ctf_requested_respawn_ticks = 90;

    // Anchors: two team-0 markers (one already dead — the scan includes
    // dead markers), one team-1 marker.
    fx.spawn_anchor(0, 96, 96);
    walker* dead_marker = fx.spawn_anchor(0, 96, 128);
    dead_marker->set_dead(1);
    fx.spawn_anchor(1, 528, 96);

    // Livings: two team-0 heroes, one team-0 npc, three team-2 npcs, one
    // dead team-1 living (excluded) and one wildlife living (team 5,
    // excluded from every row).
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    fx.spawn_hero(FAMILY_ELF, 0, 216, 200, 2);
    fx.spawn_living(FAMILY_SOLDIER, 0, 232, 200);
    fx.spawn_living(FAMILY_SOLDIER, 2, 200, 300);
    fx.spawn_living(FAMILY_ARCHER, 2, 216, 300);
    fx.spawn_living(FAMILY_MAGE, 2, 232, 300);
    walker* corpse = fx.spawn_living(FAMILY_SOLDIER, 1, 300, 300);
    corpse->set_dead(1);
    fx.spawn_living(FAMILY_SOLDIER, 5, 400, 400);

    // Generators: two on team 1.
    fx.spawn_generator(FAMILY_TENT, 1, 320, 320);
    fx.spawn_generator(FAMILY_TOWER, 1, 352, 320);

    // Flags, in fx order: team 1 level 5 (the capture-limit channel), a
    // second team-1 flag (the surplus rule is the PLAN's — the census
    // carries the row raw), a team-7 out-of-range flag (also raw), a dead
    // flag (excluded) and a waypoint (wrong family, excluded).
    fx.spawn_flag(flag_family_, 1, 100, 100, 5);
    fx.spawn_flag(flag_family_, 1, 132, 100);
    fx.spawn_flag(flag_family_, 7, 164, 100);
    walker* dead_flag = fx.spawn_flag(flag_family_, 0, 196, 100);
    dead_flag->set_dead(1);
    fx.spawn_point(point_family_, 228, 100);

    og::sim::respawn_scan_anchors(fx.world());
    const MatchPlanInputs in = og::sim::build_match_plan_inputs(fx.world());

    EXPECT_EQ(3, in.team_count);
    EXPECT_EQ(2, in.strip_troops);
    EXPECT_EQ(7, in.score_limit);
    EXPECT_EQ(90, in.respawn_ticks);

    EXPECT_EQ(2, in.teams[0].anchors) << "dead markers count (engine scan)";
    EXPECT_EQ(1, in.teams[1].anchors);
    EXPECT_EQ(0, in.teams[2].anchors);
    EXPECT_EQ(0, in.teams[3].anchors);

    EXPECT_EQ(2, in.teams[0].roster);
    EXPECT_EQ(1, in.teams[0].npcs);
    EXPECT_EQ(0, in.teams[0].generators);
    EXPECT_EQ(0, in.teams[1].roster);
    EXPECT_EQ(0, in.teams[1].npcs) << "a corpse is not a live npc";
    EXPECT_EQ(2, in.teams[1].generators);
    EXPECT_EQ(0, in.teams[2].roster);
    EXPECT_EQ(3, in.teams[2].npcs);
    EXPECT_EQ(0, in.teams[3].roster);
    EXPECT_EQ(0, in.teams[3].npcs);
    EXPECT_EQ(0, in.teams[3].generators);

    ASSERT_EQ(3u, in.flags.size())
        << "live flag rows only, surplus and out-of-range included";
    EXPECT_EQ(1, in.flags[0].team);
    EXPECT_EQ(5, in.flags[0].level);
    EXPECT_EQ(1, in.flags[1].team);
    EXPECT_EQ(1, in.flags[1].level) << "an unleveled flag reads its default "
                                       "stats level 1 (below the > 1 "
                                       "capture-limit channel)";
    EXPECT_EQ(7, in.flags[2].team);
    EXPECT_EQ(1, in.flags[2].level);
}

// ===========================================================================
// hooks::level_mode_plan — dispatch, marshaling, parse
// ===========================================================================

TEST_F(MatchPlan, no_hook_answers_nullopt)
{
    ModesCtfWorld fx(9800);
    EXPECT_FALSE(og::script::hooks::level_mode_plan(8999, probe_inputs())
                     .has_value())
        << "an unregistered level id has no plan";
    EXPECT_FALSE(og::script::hooks::level_mode_plan(9806, probe_inputs())
                     .has_value())
        << "an init-only registration has no plan";
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, plan_echoes_marshaled_inputs)
{
    ModesCtfWorld fx(9800);
    const auto plan =
        og::script::hooks::level_mode_plan(9800, probe_inputs());
    ASSERT_TRUE(plan.has_value());
    // The 9800 probe type-checks every field (a userdata or missing number
    // errors the plan) and folds the values into its answer.
    EXPECT_EQ("PROBE", plan->mode_name);
    EXPECT_TRUE(plan->starts);
    EXPECT_EQ("", plan->reason);
    EXPECT_EQ(5, plan->authored_mask) << "team_count echo (masked to 4 bits)";
    EXPECT_EQ(3, plan->active_mask) << "strip_troops echo";
    EXPECT_TRUE(plan->matched);
    EXPECT_EQ(2 * 1000 + 3 * 100 + 4 * 10 + 6, plan->matched_size)
        << "per-team anchors/roster/npcs/generators echo";
    EXPECT_EQ(7 * 1000 + 90, plan->score_limit)
        << "score_limit/respawn_ticks echo";
    EXPECT_TRUE(plan->seeded_squads);

    EXPECT_TRUE(plan->teams[0].active);
    EXPECT_EQ(MatchPlanFill::Company, plan->teams[0].fill);
    EXPECT_EQ("company", plan->teams[0].fill_label);
    EXPECT_EQ(1 * 100 + 5 + 7 * 100, plan->teams[0].count)
        << "flag rows echo ({team, level} in fx order)";
    EXPECT_TRUE(plan->teams[1].active);
    EXPECT_EQ(MatchPlanFill::Troops, plan->teams[1].fill);
    EXPECT_EQ(2, plan->teams[1].count);
    EXPECT_FALSE(plan->teams[2].active);
    EXPECT_EQ(MatchPlanFill::Generators, plan->teams[2].fill);
    EXPECT_EQ(0, plan->teams[2].count);
    EXPECT_TRUE(plan->teams[3].active);
    EXPECT_EQ(MatchPlanFill::Other, plan->teams[3].fill);
    EXPECT_EQ("A DELIBERATELY OVERL", plan->teams[3].fill_label)
        << "unknown fill labels are carried verbatim, clipped to 20";
    EXPECT_EQ(4, plan->teams[3].count);
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, vocab_and_sparse_plan_fields_parse)
{
    ModesCtfWorld fx(9807);
    const auto plan =
        og::script::hooks::level_mode_plan(9807, probe_inputs());
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ("VOCAB", plan->mode_name);
    EXPECT_FALSE(plan->starts);
    EXPECT_EQ("vocab: will not start", plan->reason);
    // Absent fields read as their defaults.
    EXPECT_EQ(0, plan->authored_mask);
    EXPECT_EQ(0, plan->active_mask);
    EXPECT_FALSE(plan->matched);
    EXPECT_EQ(0, plan->matched_size);
    EXPECT_EQ(0, plan->score_limit);
    EXPECT_FALSE(plan->seeded_squads);
    EXPECT_EQ(MatchPlanFill::Bots, plan->teams[0].fill);
    EXPECT_EQ(5, plan->teams[0].count);
    EXPECT_EQ(MatchPlanFill::Matched, plan->teams[1].fill);
    EXPECT_EQ(3, plan->teams[1].count);
    EXPECT_EQ(MatchPlanFill::Empty, plan->teams[2].fill);
    EXPECT_EQ(0, plan->teams[2].count);
    EXPECT_FALSE(plan->teams[3].active);
    EXPECT_EQ(MatchPlanFill::Other, plan->teams[3].fill)
        << "a row with no fill string parses as Other with an empty label";
    EXPECT_EQ("", plan->teams[3].fill_label);
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, non_table_plan_answers_nullopt)
{
    ModesCtfWorld fx(9808);
    EXPECT_FALSE(og::script::hooks::level_mode_plan(9808, probe_inputs())
                     .has_value())
        << "a plan answering a non-table reads as no plan";
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size())
        << "a non-table answer is a fallback, not an error";
}

// ===========================================================================
// The plan-dispatch fence
// ===========================================================================

TEST_F(MatchPlan, fence_blocks_oblist_during_planning)
{
    ModesCtfWorld fx(9801);
    EXPECT_FALSE(og::script::hooks::level_mode_plan(9801, probe_inputs())
                     .has_value());
    EXPECT_TRUE(has_script_error(
        fx.world(),
        "og.oblist: the world API is not available during match planning"));
}

TEST_F(MatchPlan, fence_blocks_rand_during_planning)
{
    ModesCtfWorld fx(9802);
    EXPECT_FALSE(og::script::hooks::level_mode_plan(9802, probe_inputs())
                     .has_value());
    EXPECT_TRUE(has_script_error(
        fx.world(),
        "og.rand: the world API is not available during match planning"));
}

TEST_F(MatchPlan, fence_blocks_mode_set_during_planning)
{
    ModesCtfWorld fx(9803);
    EXPECT_FALSE(og::script::hooks::level_mode_plan(9803, probe_inputs())
                     .has_value());
    EXPECT_TRUE(has_script_error(
        fx.world(),
        "og.mode_set: the world API is not available during match planning"));
    EXPECT_EQ(0, fx.var(0)) << "the fenced write must not have landed";
}

TEST_F(MatchPlan, fence_disarms_after_the_dispatch)
{
    ModesCtfWorld fx(9801);
    ASSERT_FALSE(og::script::hooks::level_mode_plan(9801, probe_inputs())
                     .has_value());
    // The fence is scope-armed: a later init dispatch in the same VM may
    // use the world API again (9806's init calls og.log — sandbox — but a
    // fenced world call inside init would error; prove the flag cleared by
    // running a full init on 9800, whose init reads the chained plan).
    const auto plan =
        og::script::hooks::level_mode_plan(9800, probe_inputs());
    EXPECT_TRUE(plan.has_value())
        << "a failed fenced plan must not poison the next dispatch";
}

// ===========================================================================
// The chained plan -> init dispatch (hooks::level_mode_init)
// ===========================================================================

TEST_F(MatchPlan, init_without_plan_hook_receives_nil)
{
    ModesCtfWorld fx(9806);
    EXPECT_TRUE(og::script::hooks::level_mode_init(&fx.world()));
    EXPECT_EQ(1, count_log_lines(fx.world(), "chain_none\tnil"));
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, init_receives_a_nil_plan_result)
{
    ModesCtfWorld fx(9804);
    EXPECT_TRUE(og::script::hooks::level_mode_init(&fx.world()));
    EXPECT_EQ(1, count_log_lines(fx.world(), "chain_nil\tnil"))
        << "a plan hook answering nil chains nil into init";
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, plan_error_is_the_failed_init_shape)
{
    ModesCtfWorld fx(9805);
    EXPECT_FALSE(og::script::hooks::level_mode_init(&fx.world()))
        << "a plan error at launch reports a failed init";
    EXPECT_EQ(0, count_log_lines(fx.world(), "chain_err"))
        << "on_mode_init must not run after a plan error";
    EXPECT_TRUE(has_script_error(fx.world(), "plan boom"));
    // Stack discipline: the aborted chain left the Lua stack balanced —
    // both a repeat of the failed init and an unrelated plan dispatch in
    // the same VM still work.
    EXPECT_FALSE(og::script::hooks::level_mode_init(&fx.world()));
    EXPECT_TRUE(og::script::hooks::level_mode_plan(9800, probe_inputs())
                    .has_value());
}

TEST_F(MatchPlan, plan_table_chains_into_init_through_the_engine)
{
    // Through the real engine path (mode_run_tick step 0): the plan runs
    // over the world census and its table lands as init's second argument.
    ModesCtfWorld fx(9800);
    fx.spawn_flag(flag_family_, 2, 100, 100, 4);
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.active)
        << "9800's init raises nothing, so the mode activates";
    EXPECT_EQ(1, count_log_lines(fx.world(), "chain\tPROBE\t204"))
        << "init must see plan.mode and the flag-row echo (2*100+4)";
    EXPECT_EQ(0u, fx.world().scripts().host().errors().size());
}

TEST_F(MatchPlan, plan_error_through_the_engine_leaves_classic_rules)
{
    ModesCtfWorld fx(9805);
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    fx.tick(3);
    EXPECT_FALSE(fx.world().mode.active) << "the demotion is latched";
}

TEST_F(MatchPlan, plan_hook_registers_its_kind_bit)
{
    ModesCtfWorld fx(9800);
    EXPECT_EQ((1u << 4) | (1u << 8),
              og::script::hooks::level_hook_kinds_for(9800))
        << "on_mode_plan registers LevelHook::ModePlan = bit 8";
    EXPECT_EQ(1u << 8, og::script::hooks::level_hook_kinds_for(9808));
    EXPECT_EQ(0u, og::script::hooks::level_hook_kinds_for(8999));
}
