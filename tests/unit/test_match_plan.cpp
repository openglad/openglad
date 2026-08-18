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

#include <array>
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

// ===========================================================================
// The shipped plans (commit B): activation precedence, per-mode domains,
// fills, matched sizing and score-limit resolution — driven through the
// REAL registered on_mode_plan hooks over synthetic inputs. These rows are
// the reborn roster_effective_team_mask precedence sweep: the rule now has
// exactly one home (lib/mode_match.lua plan_activation) and this is its
// unit-level oracle.
// ===========================================================================

namespace {

// Inputs shaped like the old pure-mask sweep: one anchor per authored
// team, `roster` heroes per roster team, a request and a strip state.
MatchPlanInputs sweep_inputs(unsigned authored, unsigned roster,
                             int team_count, int strip)
{
    MatchPlanInputs in;
    in.team_count = team_count;
    in.strip_troops = strip;
    for (std::size_t t = 0; t < 4; ++t)
    {
        if ((authored & (1u << t)) != 0)
            in.teams[t].anchors = 1;
        if ((roster & (1u << t)) != 0)
            in.teams[t].roster = 1;
    }
    return in;
}

// The soccer plan on the four-team fixture row (9302, teams = 4) is the
// sweep vehicle: its authored domain is the anchor census, exactly the
// old sweep's `authored` argument.
int soccer_plan_mask(unsigned authored, unsigned roster, int team_count,
                     int strip)
{
    const auto plan = og::script::hooks::level_mode_plan(
        9302, sweep_inputs(authored, roster, team_count, strip));
    if (!plan.has_value())
    {
        ADD_FAILURE() << "soccer plan did not answer";
        return -1;
    }
    return plan->active_mask;
}

}  // namespace

TEST_F(MatchPlan, activation_precedence_sweep)
{
    ModesCtfWorld fx(9302);  // serves the VM; the plan never reads it

    // Empty roster under OWN: identical to the count clamp, Auto and
    // numeric alike (Auto = the AUTHORED team count).
    EXPECT_EQ(0b1111, soccer_plan_mask(0b1111, 0, 0, 2));
    EXPECT_EQ(0b0111, soccer_plan_mask(0b1111, 0, 3, 2));
    EXPECT_EQ(0b0101, soccer_plan_mask(0b1101, 0, 2, 2));

    // Auto resolves to the AUTHORED team count (the zero sentinel means
    // "as many teams as the map actually has" — issue #218, the
    // 2026-08-18 directive), so with a roster it always backfills to the
    // full authored mask...
    EXPECT_EQ(0b1111, soccer_plan_mask(0b1111, 0b0001, 0, 2));
    EXPECT_EQ(0b0101, soccer_plan_mask(0b0101, 0b0100, 0, 2));
    // ...unless nothing else is authored (the lone bit stands, and the
    // plan reports the not-starting shape).
    EXPECT_EQ(0b0100, soccer_plan_mask(0b0100, 0b0100, 0, 2));
    // Auto with two or more rosters still fills to the authored count.
    EXPECT_EQ(0b1111, soccer_plan_mask(0b1111, 0b0101, 0, 2));
    EXPECT_EQ(0b1111, soccer_plan_mask(0b1111, 0b1110, 0, 2));

    // Explicit count: sparse authored {0, 2, 3}, roster {2}, N = 2 —
    // the roster stays and team 0 backfills in index order.
    EXPECT_EQ(0b0101, soccer_plan_mask(0b1101, 0b0100, 2, 2));
    // Rosters {0, 3} already meet N = 2: exactly the rosters.
    EXPECT_EQ(0b1001, soccer_plan_mask(0b1111, 0b1001, 2, 2));
    // Solo roster at N = 3: two backfills in index order.
    EXPECT_EQ(0b0111, soccer_plan_mask(0b1111, 0b0001, 3, 2));
    // Max semantics: three rosters outrank N = 2.
    EXPECT_EQ(0b0111, soccer_plan_mask(0b1111, 0b0111, 2, 2));
    // The clamp: N = 1 acts as 2, N = 5+ acts as 4.
    EXPECT_EQ(0b0011, soccer_plan_mask(0b1111, 0b0001, 1, 2));
    EXPECT_EQ(0b1111, soccer_plan_mask(0b1111, 0b0001, 5, 2));
    // Roster bits outside the authored domain are masked off first.
    EXPECT_EQ(0b0011, soccer_plan_mask(0b0011, 0b1100, 0, 2));
    EXPECT_EQ(0b0011, soccer_plan_mask(0b0011, 0b0110, 4, 2));
}

TEST_F(MatchPlan, troops_all_auto_asymmetry_is_per_mode)
{
    ModesCtfWorld fx(9302);
    // Soccer/basketball/onslaught: TROOPS:ALL at TEAMS: Auto takes the
    // manifest default. 9301 declares teams = 2 — with four anchor teams
    // authored, Auto fields exactly two (the known preview divergence the
    // twin never covered).
    const auto soccer_two = og::script::hooks::level_mode_plan(
        9301, sweep_inputs(0b1111, 0b0001, 0, 0));
    ASSERT_TRUE(soccer_two.has_value());
    EXPECT_EQ(0b0011, soccer_two->active_mask)
        << "ALL + Auto = the manifest row.teams default (2), not the "
           "authored count";
    // 9302 declares teams = 4: three authored anchors clamp to all three.
    EXPECT_EQ(0b0111, soccer_plan_mask(0b0111, 0, 0, 0));
    // CTF/TDM pass the RAW request on the ALL arm — Auto = every
    // authored team, no manifest default.
    MatchPlanInputs tdm_in = sweep_inputs(0b0111, 0, 0, 0);
    const auto tdm_plan = og::script::hooks::level_mode_plan(9101, tdm_in);
    ASSERT_TRUE(tdm_plan.has_value());
    EXPECT_EQ(0b0111, tdm_plan->active_mask)
        << "TDM ALL + Auto = every authored team (raw request)";
    // Onslaught 9401 declares teams = 2: generator domain, Auto -> 2.
    MatchPlanInputs ons_in;
    ons_in.strip_troops = 0;
    ons_in.teams[0].generators = 1;
    ons_in.teams[1].generators = 1;
    ons_in.teams[2].generators = 1;
    const auto ons_plan = og::script::hooks::level_mode_plan(9401, ons_in);
    ASSERT_TRUE(ons_plan.has_value());
    EXPECT_EQ(0b0011, ons_plan->active_mask)
        << "Onslaught ALL + Auto = the manifest row.teams default (2)";
}

TEST_F(MatchPlan, ctf_domain_is_the_first_flag_per_team_fold)
{
    ModesCtfWorld fx(9001);
    // Rows in fx order: team 1 (kept, level 1), team 1 dup (SURPLUS — its
    // level 5 must NOT set the map limit), team 7 (out of range), team 0
    // (kept), team 2 (kept).
    MatchPlanInputs in;
    in.strip_troops = 0;
    in.flags.push_back({1, 1});
    in.flags.push_back({1, 5});
    in.flags.push_back({7, 9});
    in.flags.push_back({0, 1});
    in.flags.push_back({2, 1});
    const auto plan = og::script::hooks::level_mode_plan(9001, in);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ("CTF", plan->mode_name);
    EXPECT_EQ(0b0111, plan->authored_mask)
        << "first flag per in-range team wins";
    EXPECT_TRUE(plan->starts);
    EXPECT_EQ(3, plan->score_limit)
        << "no KEPT flag above level 1 -> the T.capture_limit default; a "
           "surplus flag's level must never leak into the map limit";

    // The kept-flag capture-limit channel: first kept flag above level 1.
    MatchPlanInputs leveled = in;
    leveled.flags[0].level = 4;
    const auto map_plan = og::script::hooks::level_mode_plan(9001, leveled);
    ASSERT_TRUE(map_plan.has_value());
    EXPECT_EQ(4, map_plan->score_limit);
    // An explicit request outranks the map.
    leveled.score_limit = 9;
    const auto req_plan = og::script::hooks::level_mode_plan(9001, leveled);
    ASSERT_TRUE(req_plan.has_value());
    EXPECT_EQ(9, req_plan->score_limit);

    // One flag team = the not-starting shape with CTF's exact sentence.
    MatchPlanInputs lone;
    lone.flags.push_back({2, 1});
    const auto lone_plan = og::script::hooks::level_mode_plan(9001, lone);
    ASSERT_TRUE(lone_plan.has_value());
    EXPECT_FALSE(lone_plan->starts);
    EXPECT_EQ("ctf: fewer than two flag teams", lone_plan->reason);
}

TEST_F(MatchPlan, tdm_domain_is_anchors_union_livings)
{
    ModesCtfWorld fx(9101);
    MatchPlanInputs in;
    in.strip_troops = 0;
    in.teams[0].anchors = 1;   // anchors alone author
    in.teams[1].roster = 2;    // a deployed roster authors
    in.teams[2].npcs = 1;      // authored troops author
    const auto plan = og::script::hooks::level_mode_plan(9101, in);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ("TDM", plan->mode_name);
    EXPECT_EQ(0b0111, plan->authored_mask);
    EXPECT_TRUE(plan->starts);
    EXPECT_EQ(20, plan->score_limit)
        << "no fixture manifest row for 9101 -> the T.score_limit default";
    // The mode's exact not-starting sentence.
    MatchPlanInputs lone;
    lone.teams[3].npcs = 2;
    const auto lone_plan = og::script::hooks::level_mode_plan(9101, lone);
    ASSERT_TRUE(lone_plan.has_value());
    EXPECT_FALSE(lone_plan->starts);
    EXPECT_EQ("tdm: fewer than two teams", lone_plan->reason);
}

TEST_F(MatchPlan, onslaught_domain_and_generator_fills)
{
    ModesCtfWorld fx(9402);
    MatchPlanInputs in;
    in.strip_troops = 2;  // OWN: troops strip, foundries stay
    in.teams[0].generators = 2;
    in.teams[1].npcs = 1;
    in.teams[2].roster = 1;
    const auto plan = og::script::hooks::level_mode_plan(9402, in);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ("ONSLAUGHT", plan->mode_name);
    EXPECT_EQ(0b0111, plan->authored_mask)
        << "livings and generators author alike";
    EXPECT_TRUE(plan->starts);
    EXPECT_EQ(0b0111, plan->active_mask) << "OWN Auto = the authored count";
    EXPECT_FALSE(plan->seeded_squads) << "onslaught never draws squads";
    EXPECT_EQ(MatchPlanFill::Generators, plan->teams[0].fill)
        << "keep_generators: the foundries survive the OWN strip";
    EXPECT_EQ(2, plan->teams[0].count);
    EXPECT_EQ(MatchPlanFill::Empty, plan->teams[1].fill)
        << "stripped npcs + no foundry = active-and-empty (E8, D17 no "
           "bots)";
    EXPECT_EQ(MatchPlanFill::Company, plan->teams[2].fill);
    EXPECT_EQ(1, plan->teams[2].count);

    // Under ALL the authored troops stand.
    in.strip_troops = 0;
    const auto all_plan = og::script::hooks::level_mode_plan(9402, in);
    ASSERT_TRUE(all_plan.has_value());
    EXPECT_EQ(MatchPlanFill::Troops, all_plan->teams[1].fill);
    EXPECT_EQ(1, all_plan->teams[1].count);
    EXPECT_EQ(MatchPlanFill::Generators, all_plan->teams[0].fill);
}

TEST_F(MatchPlan, basketball_domain_and_reason)
{
    ModesCtfWorld fx(9702);
    MatchPlanInputs in = sweep_inputs(0b0011, 0, 0, 0);
    const auto plan = og::script::hooks::level_mode_plan(9702, in);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ("BASKETBALL", plan->mode_name);
    EXPECT_EQ(0b0011, plan->authored_mask);
    EXPECT_TRUE(plan->starts);
    EXPECT_EQ(6, plan->score_limit) << "the 9702 fixture row's limit";
    const auto lone = og::script::hooks::level_mode_plan(
        9702, sweep_inputs(0b0010, 0, 0, 0));
    ASSERT_TRUE(lone.has_value());
    EXPECT_FALSE(lone->starts);
    EXPECT_EQ("basketball: fewer than two anchor teams", lone->reason);
}

TEST_F(MatchPlan, fair_matched_size_is_the_min_roster_headcount)
{
    ModesCtfWorld fx(9302);
    // Rosters of 3 and 5 under FAIR: matched, size = min = 3, and the
    // backfilled teams get MATCHED squads truncated to that headcount.
    MatchPlanInputs in = sweep_inputs(0b1111, 0, 0, 3);
    in.teams[0].roster = 3;
    in.teams[2].roster = 5;
    const auto plan = og::script::hooks::level_mode_plan(9302, in);
    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(plan->matched);
    EXPECT_EQ(3, plan->matched_size)
        << "several roster teams -> the MIN headcount (D34)";
    EXPECT_EQ(MatchPlanFill::Company, plan->teams[0].fill);
    EXPECT_EQ(3, plan->teams[0].count);
    EXPECT_EQ(MatchPlanFill::Matched, plan->teams[1].fill);
    EXPECT_EQ(3, plan->teams[1].count)
        << "a matched squad truncates to min(headcount, 5)";
    EXPECT_EQ(MatchPlanFill::Company, plan->teams[2].fill);
    EXPECT_EQ(5, plan->teams[2].count);
    EXPECT_EQ(MatchPlanFill::Matched, plan->teams[3].fill);
    EXPECT_TRUE(plan->seeded_squads)
        << "soccer draws its squad classes at the first spawn";

    // One roster team of 5: size is its headcount, guy-less npcs never
    // count (they are census inputs.npcs, not roster).
    MatchPlanInputs solo = sweep_inputs(0b0011, 0, 0, 3);
    solo.teams[0].roster = 5;
    solo.teams[1].npcs = 4;
    const auto solo_plan = og::script::hooks::level_mode_plan(9302, solo);
    ASSERT_TRUE(solo_plan.has_value());
    EXPECT_TRUE(solo_plan->matched);
    EXPECT_EQ(5, solo_plan->matched_size)
        << "one roster team -> its own headcount (D34)";
    EXPECT_EQ(MatchPlanFill::Matched, solo_plan->teams[1].fill)
        << "FAIR strips the npcs, so the backfilled team is matched bots";
    EXPECT_EQ(5, solo_plan->teams[1].count);

    // A deployed roster OUTSIDE the authored domain still sizes the
    // match (mirroring the census, which prices every has_guy walker).
    MatchPlanInputs outside = sweep_inputs(0b0011, 0, 0, 3);
    outside.teams[0].roster = 4;
    outside.teams[3].roster = 2;
    const auto out_plan = og::script::hooks::level_mode_plan(9302, outside);
    ASSERT_TRUE(out_plan.has_value());
    EXPECT_EQ(0b0011, out_plan->active_mask)
        << "an unauthored roster team never activates";
    EXPECT_EQ(2, out_plan->matched_size)
        << "but its headcount still bounds the matched size";
}

TEST_F(MatchPlan, fair_with_no_roster_degrades_to_legacy_bots)
{
    ModesCtfWorld fx(9302);
    const auto plan = og::script::hooks::level_mode_plan(
        9302, sweep_inputs(0b0111, 0, 0, 3));
    ASSERT_TRUE(plan.has_value());
    EXPECT_FALSE(plan->matched)
        << "FAIR with zero rosters anywhere predicts TARGET 0";
    EXPECT_EQ(0, plan->matched_size);
    for (int t = 0; t < 3; ++t)
    {
        EXPECT_EQ(MatchPlanFill::Bots, plan->teams[static_cast<std::size_t>(t)].fill)
            << "the legacy difficulty squad, NOT matched (the degrade)";
        EXPECT_EQ(5, plan->teams[static_cast<std::size_t>(t)].count);
    }
    EXPECT_TRUE(plan->seeded_squads);
}

TEST_F(MatchPlan, soccer_score_limit_resolution_and_troops_fill)
{
    ModesCtfWorld fx(9301);
    // Request > row > default, clamped into [1, 255]. 9301's row limit: 3.
    MatchPlanInputs in = sweep_inputs(0b0011, 0, 0, 0);
    in.teams[1].npcs = 4;
    const auto row_plan = og::script::hooks::level_mode_plan(9301, in);
    ASSERT_TRUE(row_plan.has_value());
    EXPECT_EQ(3, row_plan->score_limit) << "no request -> the row's limit";
    EXPECT_EQ(MatchPlanFill::Troops, row_plan->teams[1].fill)
        << "under ALL the authored troops stand";
    EXPECT_EQ(4, row_plan->teams[1].count);
    EXPECT_EQ(MatchPlanFill::Bots, row_plan->teams[0].fill)
        << "an active team with nothing at all gets the squad";
    EXPECT_FALSE(row_plan->matched);
    in.score_limit = 7;
    const auto req_plan = og::script::hooks::level_mode_plan(9301, in);
    ASSERT_TRUE(req_plan.has_value());
    EXPECT_EQ(7, req_plan->score_limit) << "the request outranks the row";
    in.score_limit = 999;
    const auto clamp_plan = og::script::hooks::level_mode_plan(9301, in);
    ASSERT_TRUE(clamp_plan.has_value());
    EXPECT_EQ(255, clamp_plan->score_limit) << "clamped to [1, 255]";
}

// ===========================================================================
// Preview == launch agreement: the SAME registered Lua answers the preview
// dispatch and the launch chain, so this matrix guards exactly what is
// left — input marshaling and apply-executes-plan.
// ===========================================================================

namespace {

struct AgreementMode
{
    const char* name;
    int level_id;
    int mask_slot;
    int count_slot;   // -1 = the mode banks no TEAM_COUNT
    int score_slot;   // -1 = the mode has no score limit
};

int live_livings_on(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            count++;
        }
    }
    return count;
}

// The shared agreement shape: the mode's authored domain on teams 0-2,
// one guy-less npc on team 1, and `roster` heroes per team.
void author_agreement_world(const AgreementMode& mode, ModesCtfWorld& fx,
                            int flag_family,
                            const std::array<int, 4>& roster)
{
    const bool ctf = std::string(mode.name) == "ctf";
    const bool ons = std::string(mode.name) == "onslaught";
    for (int team = 0; team < 3; ++team)
    {
        const short x = static_cast<short>(96 + 96 * team);
        if (ctf)
            fx.spawn_flag(flag_family, team, x, 96);
        else if (ons)
            fx.spawn_generator(FAMILY_TENT, team, x, 320);
        else
            fx.spawn_anchor(team, x, 448);
    }
    fx.spawn_living(FAMILY_ORC, 1, 300, 640);
    int guy_id = 1;
    for (int team = 0; team < 4; ++team)
    {
        for (int k = 0; k < roster[static_cast<std::size_t>(team)]; ++k)
        {
            fx.spawn_hero(FAMILY_SOLDIER, team,
                          static_cast<short>(96 + 32 * k),
                          static_cast<short>(700 + 48 * team), guy_id++);
        }
    }
}

void run_agreement_case(const AgreementMode& mode, int flag_family,
                        int strip, int team_count,
                        const std::array<int, 4>& roster)
{
    SCOPED_TRACE(::testing::Message()
                 << mode.name << " strip=" << strip << " teams="
                 << team_count << " roster=" << roster[0] << roster[1]
                 << roster[2] << roster[3]);
    ModesCtfWorld fx(mode.level_id);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(strip);
    fx.world().ctf_requested_team_count = static_cast<short>(team_count);
    author_agreement_world(mode, fx, flag_family, roster);
    og::sim::respawn_scan_anchors(fx.world());
    const MatchPlanInputs inputs =
        og::sim::build_match_plan_inputs(fx.world());
    const auto plan =
        og::script::hooks::level_mode_plan(mode.level_id, inputs);
    ASSERT_TRUE(plan.has_value());
    fx.tick(1);
    if (!plan->starts)
    {
        EXPECT_FALSE(fx.world().mode.active);
        EXPECT_TRUE(has_script_error(fx.world(), plan->reason));
        return;
    }
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(plan->active_mask, fx.var(mode.mask_slot));
    if (mode.count_slot >= 0)
    {
        int bits = 0;
        for (int t = 0; t < 4; ++t)
        {
            if ((plan->active_mask & (1u << t)) != 0)
                bits++;
        }
        EXPECT_EQ(bits, fx.var(mode.count_slot));
    }
    if (mode.score_slot >= 0)
    {
        EXPECT_EQ(plan->score_limit, fx.var(mode.score_slot));
    }
    if (plan->matched)
    {
        EXPECT_EQ(plan->matched_size, fx.var(kSlotMatchedSize));
        EXPECT_GT(fx.var(kSlotMatchedTarget), 0)
            << "plan.matched must predict a nonzero measured TARGET (a "
               "live has_guy walker always prices above zero)";
    }
    else
    {
        EXPECT_EQ(0, fx.var(kSlotMatchedSize));
        EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    }
    for (int team = 0; team < 4; ++team)
    {
        const auto& row = plan->teams[static_cast<std::size_t>(team)];
        int expected = 0;
        switch (row.fill)
        {
        case MatchPlanFill::Company:
            // Under ALL the team's authored npcs stand beside the roster.
            expected = row.count;
            if (strip == 0 && team == 1)
                expected += 1;
            break;
        case MatchPlanFill::Troops:
        case MatchPlanFill::Bots:
        case MatchPlanFill::Matched:
            expected = row.count;
            break;
        default:
            expected = 0;  // generators (no infantry at tick 1) / empty
            break;
        }
        EXPECT_EQ(expected, live_livings_on(fx.world(), team))
            << "team " << team << " fill '" << row.fill_label << "'";
    }
    if (std::string(mode.name) == "ctf")
    {
        // The banked flag set must equal the plan's active mask (the
        // authored fold minus the inactive-team teardown).
        for (int team = 0; team < 4; ++team)
        {
            const bool active = (plan->active_mask & (1u << team)) != 0;
            EXPECT_EQ(active, fx.team_var(kSlotFlagEntity, team) != 0)
                << "team " << team
                << ": banked FLAG_ENTITY must follow the plan";
        }
    }
}

void run_agreement_matrix(const AgreementMode& mode, int flag_family)
{
    const std::array<int, 4> none{0, 0, 0, 0};
    const std::array<int, 4> solo{2, 0, 0, 0};
    const std::array<int, 4> two{2, 0, 1, 0};
    for (int strip : {0, 2, 3})
    {
        for (int count : {0, 2, 3, 4})
            run_agreement_case(mode, flag_family, strip, count, solo);
    }
    run_agreement_case(mode, flag_family, 2, 0, none);
    run_agreement_case(mode, flag_family, 3, 0, none);
    run_agreement_case(mode, flag_family, 3, 0, two);
    run_agreement_case(mode, flag_family, 3, 4, two);
}

}  // namespace

TEST_F(MatchPlan, agreement_soccer)
{
    run_agreement_matrix({"soccer", 9302, 11, 10, 8}, flag_family_);
}

TEST_F(MatchPlan, agreement_basketball)
{
    run_agreement_matrix({"basketball", 9702, 11, 10, 8}, flag_family_);
}

TEST_F(MatchPlan, agreement_ctf)
{
    run_agreement_matrix({"ctf", 9001, 11, 10, 8}, flag_family_);
}

TEST_F(MatchPlan, agreement_tdm)
{
    run_agreement_matrix({"tdm", 9101, 8, -1, 9}, flag_family_);
}

TEST_F(MatchPlan, agreement_onslaught)
{
    run_agreement_matrix({"onslaught", 9402, 9, 10, -1}, flag_family_);
}

TEST_F(MatchPlan, tdm_matched_fill_kinds_follow_the_plan)
{
    // TDM's fixed squad table makes the matched truncation exactly
    // knowable: min headcount 1 -> the backfilled team fields precisely
    // one soldier (the D35 soldier-first prefix).
    ModesCtfWorld fx(9101);
    fx.arm_matched();
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    og::sim::respawn_scan_anchors(fx.world());
    const auto plan = og::script::hooks::level_mode_plan(
        9101, og::sim::build_match_plan_inputs(fx.world()));
    ASSERT_TRUE(plan.has_value());
    ASSERT_TRUE(plan->matched);
    ASSERT_EQ(1, plan->matched_size);
    ASSERT_EQ(MatchPlanFill::Matched, plan->teams[1].fill);
    ASSERT_EQ(1, plan->teams[1].count);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1, live_livings_on(fx.world(), 1));
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1)
        {
            EXPECT_EQ(FAMILY_SOLDIER, w->family())
                << "the matched prefix is soldier-first";
        }
    }
}

TEST_F(MatchPlan, plan_dispatch_fits_a_tenth_of_the_instruction_budget)
{
    // The first picker-time Lua budget probe: every shipped mode's plan,
    // over a census as large as a shipped level gets, inside 1/10 of the
    // production instruction budget.
    BudgetOverride budget(500000);
    ModesCtfWorld fx(9800);
    MatchPlanInputs in = probe_inputs();
    for (int k = 0; k < 16; ++k)
        in.flags.push_back({k % 6, 1 + k % 5});
    for (int id : {300, 500, 800, 820, 824})
    {
        const auto plan = og::script::hooks::level_mode_plan(id, in);
        EXPECT_TRUE(plan.has_value()) << "shipped level " << id;
    }
    for (const auto& err : fx.world().scripts().host().errors())
        ADD_FAILURE() << "script error: " << err.where << ": "
                      << err.message;
}
