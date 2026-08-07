// The CTF Lua port (campaigns/modes/packs/modes.core) behavior suite: every RULE
// case from test_ctf_core.cpp (48) and every director case from
// test_ctf_ai.cpp (15) re-expressed against the campaign-pack Lua running
// on scripted (0x20) levels, plus the Lua-specific additions (instruction
// budget headroom, HUD/beacon writes, anchor-cursor respawn placement,
// schedule-time CP respawn speedup, mode_core probes, and the keyframe
// continuation across a blink boundary).
//
// This suite is now the ONLY CTF coverage. d44ccf88 ("The engine cut")
// deleted src/gameplay/ctf/ along with test_ctf_core.cpp and
// test_ctf_ai.cpp; what survived of og_unit_ctf was the respawn engine,
// which 9f358819 renamed og_unit_respawn. There is no C++ CTF twin left to
// pin equivalence against, so a case dropped here is coverage lost.
//
// Ported-vs-retired ledger. Every section header names the C++ cases it
// re-expresses; the retirements and where each retired behavior lives now:
//  - flag_touch_runs_entirely_through_the_pack_hook: RETIRED — the
//    boundary it pinned (core:flag delegating to og.ctf_on_flag_touch)
//    does not exist here; every flag case below dispatches through the
//    pack hook by construction (the descriptor-slot-empty assert survives
//    in pickup_fires_through_obmap_collision).
//  - control_point_touch_is_the_no_op_the_lua_hook_assumes: RETIRED —
//    replaced by waypoint_touch_is_a_no_op (the Lua no-op is now the only
//    implementation; there is no C++ twin to pin equivalence against).
//  - authored_flag_teams_scans_live_flags_without_rng /
//    authored_flag_teams_empty_on_classic_world: RETIRED WITH THE ENGINE.
//    og::sim::ctf_authored_flag_teams no longer exists in production — the
//    pack's own census_mask does that job and is pinned by the init cases
//    here. No twin is needed, and none exists.
//  - walk_to_point_crosses_open_map_and_completes /
//    walk_to_point_routes_around_wall / find_path_to_point_solves_around_
//    wall: RETIRED HERE ONLY. The C++ COMMAND_GOTO/pathfinder machinery
//    the director hands off to is untouched by this port and still lives
//    in walker_pathing.cpp / stats.cpp; its coverage moved with it, to
//    test_astar.cpp, test_forest_pathing.cpp and test_new_tiles.cpp (which
//    calls find_path_to_point directly). It was never og_unit_ctf's to
//    keep.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/campaign_ids.h>

// The modes pack's flag/waypoint treasure wire bytes (the pack families
// claim the retired core CTF slots; the shipped maps author these bytes).
inline constexpr int kModesFlagFamily = 13;
inline constexpr int kModesWaypointFamily = 14;
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

inline constexpr int kAiCadence = 15;
inline constexpr int kCpCaptureTicks = 36;

int count_notifications(const og::sim::SimEventLog& log,
                        const std::string& needle)
{
    int count = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
        {
            count++;
        }
    }
    return count;
}

bool has_notification(const og::sim::SimEventLog& log,
                      const std::string& needle)
{
    return count_notifications(log, needle) > 0;
}

bool has_score_change(const og::sim::SimEventLog& log, std::uint32_t team,
                      std::uint32_t points)
{
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::ScoreChange && ev.a == team &&
            ev.b == points)
        {
            return true;
        }
    }
    return false;
}

bool vm_logged(GameWorld& world, const std::string& needle)
{
    for (const auto& line : world.scripts().host().log())
    {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

int manhattan(const walker* w, int x, int y)
{
    return std::abs(static_cast<int>(w->xpos()) - x) +
           std::abs(static_cast<int>(w->ypos()) - y);
}

bool front_command_is(const walker* w, std::int32_t type, std::int32_t com1,
                      std::int32_t com2)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return false;
    const command& front = s->commands.front();
    return front.commandtype == type && front.com1 == com1 &&
           front.com2 == com2;
}

bool queue_contains(const walker* w, std::int32_t type)
{
    if (w->stats() == nullptr)
        return false;
    for (const command& c : w->stats()->commands)
    {
        if (c.commandtype == type)
            return true;
    }
    return false;
}

// Advances the world to one tick short of the next director cadence
// boundary, so the NEXT fx.tick() runs the director exactly once.
void align_before_cadence(GameWorld& world)
{
    const std::uint32_t next =
        ((world.tick_count_ / kAiCadence) + 1) * kAiCadence;
    world.tick_count_ = next - 1;
}

int alive_on_team(GameWorld& world, int team)
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

// Full behavior digest: mode vars, RNG, positions, command queues.
std::string digest_world(GameWorld& world)
{
    std::string digest = std::format("rng={} tick={} act={}|",
                                     world.rng_.state_, world.tick_count_,
                                     world.mode.active ? 1 : 0);
    for (int i = 0; i < og::sim::kModeVarCount; ++i)
        digest += std::format("{},", world.mode.vars[static_cast<std::size_t>(i)]);
    digest += std::format("|q={}|", world.respawn.respawn_queue.size());
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr)
            continue;
        digest += std::format("[id={} x={} y={} dead={} act={} lead={} foe={}",
                              w->entity_id(), w->xpos(), w->ypos(),
                              w->dead() ? 1 : 0,
                              static_cast<int>(w->act_type()), w->leader_id(),
                              w->foe_id());
        if (w->stats() != nullptr)
        {
            digest += std::format(" q={}", w->stats()->commands.size());
            for (const command& c : w->stats()->commands)
            {
                digest += std::format(" ({},{},{},{})", c.commandtype,
                                      c.commandcount, c.com1, c.com2);
            }
        }
        digest += "]";
    }
    return digest;
}

}  // namespace

using ModesCtf = ModesPackTest;

// ===========================================================================
// Activation / init
// PORTS: lazy_init_activates_two_team_map,
// init_demotes_to_inactive_below_two_flag_teams,
// init_strips_teams_beyond_requested_count,
// map_capture_limit_comes_from_flag_level_and_request_wins,
// strip_scenario_troops_* (4 cases).
// ===========================================================================

TEST_F(ModesCtf, lazy_init_activates_two_team_map)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    ASSERT_NE(nullptr, flag0);
    ASSERT_NE(nullptr, flag1);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);

    ASSERT_FALSE(fx.world().mode.active);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_TRUE(fx.ctf_active());
    EXPECT_EQ(3, fx.var(kSlotTeamMask));
    EXPECT_EQ(2, fx.var(kSlotTeamCount));
    EXPECT_EQ(3, fx.var(kSlotCaptureLimit));
    EXPECT_EQ(120, fx.var(kSlotRespawnTicks));
    EXPECT_EQ(static_cast<std::int32_t>(flag0->entity_id()),
              fx.team_var(kSlotFlagEntity, 0));
    EXPECT_EQ(static_cast<std::int32_t>(flag1->entity_id()),
              fx.team_var(kSlotFlagEntity, 1));
    EXPECT_EQ(pos_pack(96, 96), fx.team_var(kSlotFlagHome, 0));
    EXPECT_EQ(pos_pack(544, 800), fx.team_var(kSlotFlagHome, 1));
    EXPECT_TRUE(fx.flag_at_home(0));
    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(1, fx.world().respawn.anchor_count[0]);
    EXPECT_EQ(1, fx.world().respawn.anchor_count[1]);
    EXPECT_TRUE(has_notification(fx.events, "CAPTURE THE FLAG! TO 3"));
    EXPECT_STREQ("CTF", fx.world().mode.name.data());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesCtf, init_demotes_to_inactive_below_two_flag_teams)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* survivor = fx.spawn_living(FAMILY_ORC, 1, 400, 700);

    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.ctf_active());
    EXPECT_FALSE(survivor->dead())
        << "demoted init must leave the map untouched";
    EXPECT_FALSE(has_notification(fx.events, "CAPTURE THE FLAG"));
    EXPECT_TRUE(has_script_error(fx.world(), "fewer than two flag teams"))
        << "the failed-init shape is a raised, recorded error";
    fx.tick(5);
    EXPECT_FALSE(fx.world().mode.active) << "the demotion is latched";
}

TEST_F(ModesCtf, init_strips_teams_beyond_requested_count)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 96);
    walker* flag2 = fx.spawn_flag(flag_family_, 2, 96, 800);
    walker* flag3 = fx.spawn_flag(flag_family_, 3, 544, 800);
    // Stripped flags are dead fxlist treasures, DESTROYED by the
    // end-of-tick sweep — capture ids now.
    const std::uint32_t flag2_id = flag2->entity_id();
    const std::uint32_t flag3_id = flag3->entity_id();
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 128);
    fx.spawn_anchor(2, 128, 832);
    fx.spawn_anchor(3, 512, 832);
    walker* stripped_living = fx.spawn_living(FAMILY_ORC, 2, 200, 760);
    fx.world().ctf_requested_team_count = 2;

    fx.tick(1);

    ASSERT_TRUE(fx.ctf_active());
    EXPECT_EQ(3, fx.var(kSlotTeamMask));
    EXPECT_EQ(2, fx.var(kSlotTeamCount));
    EXPECT_EQ(0, fx.team_var(kSlotFlagEntity, 2));
    EXPECT_EQ(0, fx.team_var(kSlotFlagEntity, 3));
    EXPECT_EQ(nullptr, fx.world().find_by_id(flag2_id))
        << "stripped flag entity must be removed from the world";
    EXPECT_EQ(nullptr, fx.world().find_by_id(flag3_id));
    EXPECT_TRUE(stripped_living->dead());  // dead livings persist (dead_list)

    // Active teams had no livings: each gets a five-bot squad.
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    EXPECT_EQ(0, alive_on_team(fx.world(), 2));
}

// Matched-teams D16 acceptance: the init squad fill goes through the one
// shared spawner (match.spawn_bots) but keeps CTF's OWN placer — when every
// team anchor probe fails, the first squad member lands on the team's
// flag-home square, NOT on a teleport draw. A naive re-point at
// mode_match's placer would lose the flag-home fallback and teleport all
// five; this pins the fallback chain anchor -> flag home -> teleport.
TEST_F(ModesCtf, blocked_anchor_bot_fill_falls_back_to_flag_home)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_anchor(1, 512, 128);
    // Team-0 livings stand exactly on both team-1 anchors, so every anchor
    // probe fails for the empty team's squad. They also keep team 0
    // populated (no squad there).
    fx.spawn_living(FAMILY_SOLDIER, 0, 512, 832);
    fx.spawn_living(FAMILY_SOLDIER, 0, 512, 128);

    fx.tick(1);

    ASSERT_TRUE(fx.ctf_active());
    EXPECT_EQ(3, fx.var(kSlotTeamMask));
    EXPECT_EQ(2, alive_on_team(fx.world(), 0)) << "the blockers, no squad";
    EXPECT_EQ(5, alive_on_team(fx.world(), 1)) << "the empty team's squad";

    // The first squad member (soldier, squad index 0) took the flag-home
    // fallback; the four behind it found the square occupied and fell to
    // the blessed init-time teleport. Exactly one bot at flag home proves
    // the placer probed the flag home BEFORE any teleport draw.
    int at_flag_home = 0;
    const walker* home_bot = nullptr;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1)
        {
            EXPECT_FALSE(w->xpos() == 512 && w->ypos() == 832)
                << "no bot may land on a blocked anchor";
            EXPECT_FALSE(w->xpos() == 512 && w->ypos() == 128)
                << "no bot may land on a blocked anchor";
            if (w->xpos() == 544 && w->ypos() == 800)
            {
                at_flag_home++;
                home_bot = w;
            }
        }
    }
    ASSERT_EQ(1, at_flag_home);
    ASSERT_NE(nullptr, home_bot);
    EXPECT_EQ(FAMILY_SOLDIER, home_bot->family())
        << "the FIRST squad member is the one the flag-home fallback placed";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// The modes.md §4.10 sparse-activation shape: flags on {0, 2, 3} with a
// requested count of 2 activate {0, 2} in index order; roster walkers on
// the stripped team survive.
TEST_F(ModesCtf, sparse_flag_teams_activate_in_index_order)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 2, 480, 800);
    fx.spawn_flag(flag_family_, 3, 480, 160);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 2, 400, 700);
    walker* stripped = fx.spawn_living(FAMILY_ORC, 3, 500, 200);
    walker* kept_hero = fx.spawn_hero(FAMILY_SOLDIER, 3, 520, 200, 7);
    fx.world().ctf_requested_team_count = 2;
    fx.tick(1);

    ASSERT_TRUE(fx.ctf_active());
    EXPECT_EQ(5, fx.var(kSlotTeamMask)) << "active mask is {0, 2}";
    EXPECT_EQ(2, fx.var(kSlotTeamCount));
    EXPECT_TRUE(stripped->dead());
    EXPECT_FALSE(kept_hero->dead()) << "roster walkers are never stripped";
}

TEST_F(ModesCtf, map_capture_limit_comes_from_flag_level_and_request_wins)
{
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 96, 96, 5);
        fx.spawn_flag(flag_family_, 1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick(1);
        ASSERT_TRUE(fx.ctf_active());
        EXPECT_EQ(5, fx.var(kSlotCaptureLimit)) << "flag level sets the limit";
    }
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 96, 96, 5);
        fx.spawn_flag(flag_family_, 1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.world().ctf_requested_capture_limit = 9;
        fx.tick(1);
        ASSERT_TRUE(fx.ctf_active());
        EXPECT_EQ(9, fx.var(kSlotCaptureLimit)) << "explicit request wins";
    }
}

// --- Scenario-troops strip -------------------------------------------------

namespace {

struct StripScenarioActors
{
    walker* hero = nullptr;
    walker* authored_friend = nullptr;
    walker* friendly_gen = nullptr;
    walker* authored_enemy = nullptr;
    walker* enemy_gen = nullptr;
};

StripScenarioActors build_strip_scenario(ModesCtfWorld& fx, int flag_family)
{
    StripScenarioActors actors;
    fx.spawn_flag(flag_family, 0, 96, 96);
    fx.spawn_flag(flag_family, 1, 544, 800);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);

    actors.hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    actors.authored_friend = fx.spawn_living(FAMILY_ARCHER, 0, 200, 160);
    actors.authored_enemy = fx.spawn_living(FAMILY_ORC, 1, 480, 760);

    actors.friendly_gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
    actors.friendly_gen->setxy(256, 128);
    actors.friendly_gen->set_team_num(0);

    actors.enemy_gen = fx.world().add_ob(Order::Generator, FAMILY_TOWER);
    actors.enemy_gen->setxy(448, 832);
    actors.enemy_gen->set_team_num(1);
    return actors;
}

}  // namespace

TEST_F(ModesCtf, strip_scenario_troops_removes_every_authored_entity)
{
    ModesCtfWorld fx;
    StripScenarioActors actors = build_strip_scenario(fx, flag_family_);
    fx.world().ctf_requested_strip_scenario_troops = 2;

    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    EXPECT_FALSE(actors.hero->dead());
    EXPECT_TRUE(actors.authored_friend->dead());
    EXPECT_TRUE(actors.friendly_gen->dead());
    EXPECT_TRUE(actors.authored_enemy->dead())
        << "TROOPS: OWN takes the opposing team's authored cast too";
    EXPECT_TRUE(actors.enemy_gen->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0))
        << "only the hero should remain on team 0";
    EXPECT_GT(alive_on_team(fx.world(), 1), 0)
        << "the empty-team census runs after the sweep, so team 1 is "
           "backfilled with bots rather than left unopposed";

    for (const auto& entry : fx.world().respawn.respawn_queue)
    {
        EXPECT_FALSE(entry.kind == 1 && entry.team == 0)
            << "init-stripped troop entered the bot respawn queue";
    }

    // Past the respawn window the stripped troops must stay gone. Counted,
    // not asserted on the hero: team 1 now fields a bot squad and the lone
    // roster fighter losing that fight is not what this case pins.
    fx.tick(150);
    EXPECT_LE(alive_on_team(fx.world(), 0), 1)
        << "stripped troops must stay gone past the respawn window";
    for (const auto& entry : fx.world().respawn.respawn_queue)
    {
        EXPECT_FALSE(entry.kind == 1 && entry.team == 0)
            << "bot respawn queued for the roster team after the window";
    }
}

TEST_F(ModesCtf, troops_own_activates_only_the_roster_flag_teams)
{
    // The scen-841 shape on CTF: a four-flag map under OWN with rosters on
    // teams 0 and 2 fields exactly those two sides. The unrostered flag
    // teams strip with their flags, and no bot squads appear.
    ModesCtfWorld fx;
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 96);
    fx.spawn_flag(flag_family_, 2, 96, 800);
    fx.spawn_flag(flag_family_, 3, 544, 800);
    walker* soldier = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 1);
    walker* barbarian = fx.spawn_hero(FAMILY_BARBARIAN, 2, 160, 760, 2);
    fx.tick(1);

    ASSERT_TRUE(fx.ctf_active());
    EXPECT_EQ(1 + 4, fx.var(kSlotTeamMask))
        << "exactly the two roster flag teams activate";
    EXPECT_EQ(2, fx.var(kSlotTeamCount));
    EXPECT_EQ(0, fx.team_var(kSlotFlagEntity, 1))
        << "the unrostered teams' flags strip with them";
    EXPECT_EQ(0, fx.team_var(kSlotFlagEntity, 3));
    EXPECT_FALSE(soldier->dead());
    EXPECT_FALSE(barbarian->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0));
    EXPECT_EQ(0, alive_on_team(fx.world(), 1)) << "no CTF bot squads under OWN";
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(0, alive_on_team(fx.world(), 3));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

namespace {

std::string run_strip_scenario_match(int flag_family, bool set_field,
                                     short strip_flag, int ticks)
{
    ModesCtfWorld fx(kCtfLevelB);
    build_strip_scenario(fx, flag_family);
    if (set_field)
        fx.world().ctf_requested_strip_scenario_troops = strip_flag;
    fx.tick(ticks);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesCtf, strip_scenario_troops_off_matches_control_run)
{
    const std::string off = run_strip_scenario_match(flag_family_, true, 0, 50);
    const std::string control =
        run_strip_scenario_match(flag_family_, false, 0, 50);
    ASSERT_EQ(off, control);
}

TEST_F(ModesCtf, strip_scenario_troops_run_is_deterministic)
{
    const std::string first =
        run_strip_scenario_match(flag_family_, true, 2, 150);
    const std::string second =
        run_strip_scenario_match(flag_family_, true, 2, 150);
    ASSERT_NE(first.find("act=1"), std::string::npos);
    ASSERT_EQ(first, second);
}

TEST_F(ModesCtf, strip_scenario_troops_inert_when_ctf_does_not_activate)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 160, 7);
    fx.spawn_living(FAMILY_ARCHER, 0, 200, 160);
    fx.world().ctf_requested_strip_scenario_troops = 2;

    fx.tick(1);

    ASSERT_FALSE(fx.world().mode.active);
    // The mode script never reached on_mode_init, and the engine's own sweep
    // is latched to the first tick — which this map spent inside the mode
    // branch. The authored archer therefore survives. Counted rather than
    // dereferenced: the engine sweep REMOVES walkers, so a regression here
    // would be a dangling pointer, not a dead flag.
    EXPECT_EQ(2, alive_on_team(fx.world(), 0))
        << "non-activating CTF maps keep classic rules";
}
TEST_F(ModesCtf, enemy_touch_picks_up_and_carry_visual_follows)
{
    ModesCtfWorld fx;
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    ASSERT_TRUE(flag1->eat_me(runner));
    EXPECT_TRUE(fx.flag_carried(1));
    EXPECT_EQ(runner->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(1, flag1->ignore());
    EXPECT_TRUE(has_notification(fx.events, "GREEN FLAG TAKEN!"));

    // A scripted warp with no self-teleport marker must NOT drop — position
    // warps from snapshots and tests are not teleports.
    runner->setxy(260, 240);
    fx.tick(1);
    EXPECT_TRUE(fx.flag_carried(1))
        << "an unmarked position warp must never drop the flag";
    EXPECT_EQ(260, flag1->xpos());
    EXPECT_EQ(232, flag1->ypos()) << "carry visual rides 8px above";
    EXPECT_EQ(260, fx.flag_x(1));
    EXPECT_EQ(240, fx.flag_y(1));
}

TEST_F(ModesCtf, pickup_fires_through_obmap_collision)
{
    // modes:flag must have NO C++ on_eat callback: the whole touch rule is
    // the pack hook (the surviving half of the retired boundary pin).
    const TreasureFamilyDescriptor* tfd =
        get_treasure_family_descriptor(flag_family_);
    ASSERT_NE(nullptr, tfd);
    ASSERT_EQ(nullptr, tfd->on_eat);

    ModesCtfWorld fx;
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);
    og::script::hooks::reset_hook_failures();

    // Probing the flag's tile through the obmap runs the production
    // collision -> eat_me -> pack on_eat dispatch.
    runner->setxy(544, 780);
    (void)fx.world().query_passable(544.0f, 796.0f, runner);
    EXPECT_TRUE(fx.flag_carried(1));
    EXPECT_EQ(runner->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(1, flag1->ignore());

    // Capture through the same path: run home onto the own flag.
    runner->setxy(96, 80);
    (void)fx.world().query_passable(96.0f, 96.0f, runner);
    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(1, fx.captures(0));
    EXPECT_TRUE(has_score_change(fx.events, 0, 400));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count)
        << og::script::hooks::hook_failures().message;
}

TEST_F(ModesCtf, own_team_touch_returns_dropped_flag)
{
    ModesCtfWorld fx;
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_dead(1);
    fx.tick(1);
    ASSERT_TRUE(fx.flag_dropped(1));
    EXPECT_TRUE(has_notification(fx.events, "GREEN FLAG DROPPED!"));

    ASSERT_TRUE(flag1->eat_me(enemy));
    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(544, flag1->xpos());
    EXPECT_EQ(800, flag1->ypos());
    EXPECT_EQ(0, flag1->ignore());
    EXPECT_TRUE(has_notification(fx.events, "GREEN FLAG RETURNED!"));
}

TEST_F(ModesCtf, regrab_of_dropped_flag_emits_no_second_taken)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* backup = fx.spawn_living(FAMILY_SOLDIER, 0, 232, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    ASSERT_TRUE(flag1->eat_me(runner));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));

    runner->set_dead(1);
    fx.tick(1);
    ASSERT_TRUE(fx.flag_dropped(1));
    ASSERT_TRUE(flag1->eat_me(backup));
    EXPECT_TRUE(fx.flag_carried(1));
    EXPECT_EQ(backup->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"))
        << "a regrab of a dropped flag must not re-announce TAKEN";
}

TEST_F(ModesCtf, notifications_fit_25_char_budget)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 96);
    fx.spawn_flag(flag_family_, 2, 96, 800);
    walker* flag3 = fx.spawn_flag(flag_family_, 3, 544, 800);
    fx.spawn_point(point_family_, 320, 320);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 60);
    fx.spawn_living(FAMILY_SOLDIER, 2, 60, 800);
    fx.spawn_living(FAMILY_SOLDIER, 3, 352, 320);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(has_notification(fx.events, "CAPTURE THE FLAG! TO 3"));

    // Team 3 holds the waypoint alone until it flips.
    fx.tick(kCpCaptureTicks + 1);
    ASSERT_TRUE(has_notification(fx.events, "YELLOW TAKES WAYPOINT!"));

    // Take, drop, and auto-return team 3's flag.
    ASSERT_TRUE(flag3->eat_me(runner));
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG TAKEN!"));
    runner->set_dead(1);
    fx.tick(1);
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG DROPPED!"));
    fx.world().mode.vars[kSlotFlagReturn + 3] = 4;
    fx.tick(5);
    ASSERT_TRUE(has_notification(fx.events, "YELLOW FLAG RETURNED!"));

    for (const auto& ev : fx.events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification)
        {
            EXPECT_LE(ev.text.size(), 25u) << "over budget: " << ev.text;
        }
    }
}

TEST_F(ModesCtf, capture_requires_own_flag_home_and_awards_score)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag0->eat_me(enemy));
    ASSERT_TRUE(fx.flag_carried(0));

    // Own flag away: touching it (it rides the enemy) cannot capture.
    ASSERT_TRUE(flag0->eat_me(runner));
    EXPECT_EQ(0, fx.captures(0));

    // Kill the enemy: the runner's flag drops; a friendly touch returns it.
    enemy->set_dead(1);
    fx.tick(1);
    ASSERT_TRUE(fx.flag_dropped(0));
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_TRUE(fx.flag_at_home(0));

    // Own flag home: the touch banks the carried enemy flag.
    const std::uint32_t score_before = fx.world().m_score[0];
    ASSERT_TRUE(flag0->eat_me(runner));
    EXPECT_EQ(1, fx.captures(0));
    EXPECT_EQ(score_before + 400, fx.world().m_score[0]);
    EXPECT_TRUE(has_score_change(fx.events, 0, 400));
    EXPECT_TRUE(has_notification(fx.events, "TEAM 1 SCORES! 1/3"));
    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(544, flag1->xpos());
    EXPECT_EQ(800, flag1->ypos());
    EXPECT_EQ(0, flag1->ignore());
}

TEST_F(ModesCtf, multi_carry_capture_awards_all_flags)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 96);
    walker* flag2 = fx.spawn_flag(flag_family_, 2, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 500, 150);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 760);
    fx.world().ctf_requested_team_count = 3;
    fx.tick(1);
    ASSERT_EQ(3, fx.var(kSlotTeamCount));

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag2->eat_me(runner));
    ASSERT_TRUE(fx.flag_carried(1));
    ASSERT_TRUE(fx.flag_carried(2));

    ASSERT_TRUE(flag0->eat_me(runner));
    EXPECT_EQ(2, fx.captures(0));
    EXPECT_EQ(2u * 400u, fx.world().m_score[0]);
    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_TRUE(fx.flag_at_home(2));
}

TEST_F(ModesCtf, charm_flipped_carrier_drops_flag)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_team_num(1);  // charm flip onto the flag's own team
    fx.tick(1);
    EXPECT_TRUE(fx.flag_dropped(1));
    EXPECT_EQ(0u, fx.carrier_id(1));
}

TEST_F(ModesCtf, dropped_flag_auto_returns_after_countdown)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);

    ASSERT_TRUE(flag1->eat_me(runner));
    runner->set_dead(1);
    fx.tick(1);  // drop (carried phase) + first countdown step (dropped phase)
    ASSERT_TRUE(fx.flag_dropped(1));
    EXPECT_EQ(359, fx.team_var(kSlotFlagReturn, 1))
        << "the drop arms the 360-tick countdown, decremented same tick";

    fx.world().mode.vars[kSlotFlagReturn + 1] = 5;
    fx.tick(4);
    ASSERT_TRUE(fx.flag_dropped(1));
    fx.tick(1);
    ASSERT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(544, flag1->xpos());
    EXPECT_EQ(800, flag1->ypos());
}

TEST_F(ModesCtf, drop_on_impassable_tile_returns_home_instantly)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);

    ASSERT_TRUE(flag1->eat_me(runner));

    // Wall in the carrier's tile neighborhood, then kill it: the corpse
    // tile fails the terrain probe and the flag goes home, not down.
    PixieData& grid = fx.world().grid;
    for (int gy = 20; gy <= 21; ++gy)
        for (int gx = 20; gx <= 21; ++gx)
            grid.data[static_cast<std::size_t>(gx + grid.w * gy)] = PIX_H_WALL1;
    runner->set_dead(1);
    fx.tick(1);

    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(544, flag1->xpos());
    EXPECT_EQ(800, flag1->ypos());
    EXPECT_EQ(0, flag1->ignore());
}

// ===========================================================================
// Self-teleport flag drop (the UT rule)
// PORTS: staged_blink_drops_flag_at_departure_point,
// real_mage_marker_teleport_special_drops_flag,
// real_level1_skeleton_blink_drops_flag,
// fighter_rush_charge_at_max_stepsize_keeps_flag,
// real_teleporter_pad_ride_keeps_flag_carried,
// blink_from_beside_pad_to_beside_partner_drops,
// capture_during_blink_is_refused,
// pickup_during_blink_is_refused_and_flag_stays_home,
// teleport_drop_on_impassable_departure_returns_home,
// one_blink_drops_every_carried_flag, regrab_after_teleport_drop_stays_silent.
// ===========================================================================

namespace {

// Snapshots deliberately carry no queued commands or computed paths;
// apply_snapshot clears them on the restored side. Mirror that reset on the
// uninterrupted world at the capture point so both runs continue from the
// same transient state and any later divergence is a real replication gap.
void normalize_transient_state(GameWorld& world)
{
    auto normalize_list = [](const auto& entities) {
        for (const auto& uptr : entities)
        {
            walker* w = uptr.get();
            if (w == nullptr)
                continue;
            if (w->stats() != nullptr)
                w->stats()->commands.clear();
            w->path_to_foe.clear();
        }
    };
    normalize_list(world.oblist);
    normalize_list(world.fxlist);
    normalize_list(world.weaplist);
}

struct TeleportWorld : ModesCtfWorld
{
    walker* flag1 = nullptr;
    walker* runner = nullptr;

    explicit TeleportWorld(int flag_family)
    {
        spawn_flag(flag_family, 0, 96, 96);
        flag1 = spawn_flag(flag_family, 1, 544, 800);
        runner = spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        world().ctf_requested_respawn_ticks = 5000;
    }
};

}  // namespace

TEST_F(ModesCtf, staged_blink_drops_flag_at_departure_point)
{
    TeleportWorld fx(flag_family_);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(1);
    ASSERT_TRUE(fx.flag_carried(1));

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(440, 488);  // the blink
    fx.tick(1);

    EXPECT_TRUE(fx.flag_dropped(1));
    EXPECT_EQ(0u, fx.carrier_id(1));
    EXPECT_EQ(200, fx.flag_x(1)) << "the flag stays at the departure point";
    EXPECT_EQ(200, fx.flag_y(1));
    EXPECT_EQ(200, fx.flag1->xpos());
    EXPECT_EQ(200, fx.flag1->ypos());
    EXPECT_EQ(0, fx.flag1->ignore());
    EXPECT_GT(fx.team_var(kSlotFlagReturn, 1), 0);
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

// The v10 twin of master's retired
// CtfSnapshot.keyframe_at_blink_tick_boundary_continues_byte_equal.
//
// last_self_teleport_tick is NOT on the wire, and the drop rule reads it —
// but only as a SAME-TICK marker: the act phase stamps the current tick and
// on_mode_tick of that same tick consumes it. Snapshots are captured and
// applied at tick boundaries, never between the stamp and its read, so by
// the time any restored world reads the stamp again it is stale on both
// sides (`stamp == world_tick` is false for the original's T and for the
// restored default 0 alike). This pins that: a keyframe taken at the blink
// boundary continues byte-identically even though the stamp is gone.
TEST_F(ModesCtf, keyframe_at_the_blink_boundary_continues_byte_equal)
{
    std::vector<std::uint8_t> boundary_bytes;
    std::vector<std::uint8_t> uninterrupted_end;
    std::uint32_t runner_id = 0;
    {
        TeleportWorld fx(flag_family_);
        fx.tick(1);
        ASSERT_TRUE(fx.ctf_active());
        ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
        fx.tick(1);
        ASSERT_TRUE(fx.flag_carried(1));
        runner_id = fx.runner->entity_id();

        // The blink itself, exactly as staged_blink_drops_flag_at_departure
        // _point performs it.
        fx.stage_self_teleport(fx.runner);
        fx.runner->setxy(440, 488);
        fx.tick(1);
        ASSERT_TRUE(fx.flag_dropped(1));
        ASSERT_NE(0u, fx.runner->last_self_teleport_tick())
            << "the stamp must be live at this boundary";

        normalize_transient_state(fx.world());
        boundary_bytes = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(fx.world()));
        fx.tick(20);
        uninterrupted_end = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(fx.world()));
    }

    ModesCtfWorld restored;
    og::sim::apply_snapshot(restored.world(),
                            og::sim::deserialize_snapshot(boundary_bytes));
    walker* restored_runner = restored.world().find_by_id(runner_id);
    ASSERT_NE(nullptr, restored_runner);
    EXPECT_EQ(0u, restored_runner->last_self_teleport_tick())
        << "the stamp does not cross the wire — that is the whole point";

    restored.tick(20);
    const std::vector<std::uint8_t> restored_end = og::sim::serialize_snapshot(
        og::sim::capture_keyframe_snapshot(restored.world()));
    EXPECT_EQ(uninterrupted_end, restored_end)
        << "a keyframe restored at the blink boundary must continue "
           "identically: the drop already happened, and the stamp it used "
           "is stale on the next read either way";
    EXPECT_NE(boundary_bytes, uninterrupted_end)
        << "the 20-tick continuation must actually move the world, or the "
           "equality above proves nothing";
}

TEST_F(ModesCtf, real_mage_marker_teleport_special_drops_flag)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(416, 416);
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN);

    ASSERT_TRUE(flag1->eat_me(mage));
    fx.tick(1);
    const short depart_x = mage->xpos();
    const short depart_y = mage->ypos();

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special()) << "mage teleport special must arm TELE_OUT";
    fx.tick(20);

    ASSERT_GT(manhattan(mage, depart_x, depart_y), 64)
        << "the mage must have blinked to the marker";
    EXPECT_TRUE(fx.flag_dropped(1));
    EXPECT_EQ(depart_x, fx.flag_x(1));
    EXPECT_EQ(depart_y, fx.flag_y(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

TEST_F(ModesCtf, real_level1_skeleton_blink_drops_flag)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* skel = fx.spawn_living(FAMILY_SKELETON, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(10);  // activation; ANI_SKEL_GROW completes
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_NE(nullptr, skel->stats());
    ASSERT_EQ(1u, skel->stats()->level());

    ASSERT_TRUE(flag1->eat_me(skel));
    fx.tick(1);
    const short depart_x = skel->xpos();
    const short depart_y = skel->ypos();

    skel->stats()->set_max_magicpoints(100);
    skel->stats()->set_magicpoints(100);
    skel->set_current_special(1);  // TUNNEL
    ASSERT_TRUE(skel->special()) << "skeleton tunnel must arm TELE_OUT";
    fx.tick(20);

    ASSERT_LE(std::abs(skel->xpos() - depart_x), 18);
    ASSERT_LE(std::abs(skel->ypos() - depart_y), 18);
    EXPECT_TRUE(fx.flag_dropped(1)) << "even the shortest blink drops";
    EXPECT_EQ(depart_x, fx.flag_x(1));
    EXPECT_EQ(depart_y, fx.flag_y(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

TEST_F(ModesCtf, fighter_rush_charge_at_max_stepsize_keeps_flag)
{
    TeleportWorld fx(flag_family_);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(1);
    ASSERT_TRUE(fx.flag_carried(1));

    fx.runner->set_normal_stepsize(12.0f);
    fx.runner->set_stepsize(12.0f);
    fx.runner->set_curdir(static_cast<signed char>(FACE_RIGHT));
    fx.runner->set_enddir(static_cast<char>(FACE_RIGHT));
    ASSERT_NE(nullptr, fx.runner->stats());
    fx.runner->stats()->add_command(COMMAND_RUSH, 3, 1, 0);

    int max_tick_dx = 0;
    for (int step = 0; step < 3; ++step)
    {
        const short before_x = fx.runner->xpos();
        fx.tick(1);
        const int dx = static_cast<int>(fx.runner->xpos()) - before_x;
        if (dx > max_tick_dx)
            max_tick_dx = dx;
        ASSERT_TRUE(fx.flag_carried(1))
            << "step " << step << ": a rush charge must never drop the flag";
        ASSERT_EQ(fx.runner->xpos(), fx.flag_x(1));
        ASSERT_EQ(fx.runner->ypos(), fx.flag_y(1));
    }
    ASSERT_EQ(36, max_tick_dx)
        << "the charge must actually cover 3x stepsize in a single tick";
    ASSERT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

TEST_F(ModesCtf, real_teleporter_pad_ride_keeps_flag_carried)
{
    TeleportWorld fx(flag_family_);
    walker* padA = fx.spawn_teleporter(320, 320);
    walker* padB = fx.spawn_teleporter(480, 640);
    ASSERT_NE(nullptr, padA);
    ASSERT_NE(nullptr, padB);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    fx.runner->center_on(padA);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(1);  // FLAG_POS syncs on the departure pad

    (void)fx.world().query_passable(static_cast<float>(padA->xpos()),
                                    static_cast<float>(padA->ypos()),
                                    fx.runner);
    ASSERT_LE(std::abs((fx.runner->xpos() + fx.runner->sizex() / 2) -
                       (padB->xpos() + padB->sizex() / 2)),
              1)
        << "the eat must have center_on'd the rider onto the partner pad";

    fx.tick(1);
    EXPECT_TRUE(fx.flag_carried(1))
        << "a map teleporter ride carries the flag through";
    EXPECT_EQ(fx.runner->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(fx.runner->xpos(), fx.flag_x(1));
    EXPECT_EQ(fx.runner->ypos(), fx.flag_y(1));
    EXPECT_EQ(fx.runner->xpos(), fx.flag1->xpos());
    EXPECT_EQ(static_cast<short>(fx.runner->ypos() - 8), fx.flag1->ypos());
    EXPECT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

TEST_F(ModesCtf, blink_from_beside_pad_to_beside_partner_drops)
{
    TeleportWorld fx(flag_family_);
    walker* padA = fx.spawn_teleporter(320, 320);
    walker* padB = fx.spawn_teleporter(480, 640);
    fx.tick(1);

    fx.runner->setxy(static_cast<short>(padA->xpos() + 20),
                     static_cast<short>(padA->ypos() + 20));
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(1);
    const short depart_x = fx.runner->xpos();
    const short depart_y = fx.runner->ypos();

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(static_cast<short>(padB->xpos() + 20),
                     static_cast<short>(padB->ypos() + 20));
    fx.tick(1);

    EXPECT_TRUE(fx.flag_dropped(1));
    EXPECT_EQ(depart_x, fx.flag_x(1));
    EXPECT_EQ(depart_y, fx.flag_y(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

TEST_F(ModesCtf, capture_during_blink_is_refused)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(flag0->xpos(), flag0->ypos());
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN);

    ASSERT_TRUE(flag1->eat_me(mage));
    fx.tick(1);
    const short depart_x = mage->xpos();
    const short depart_y = mage->ypos();

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special());
    fx.tick(20);  // the blink lands the mage on its own stand

    ASSERT_LE(std::abs(mage->xpos() - flag0->xpos()), 8);
    EXPECT_EQ(0, fx.captures(0))
        << "a capture banked by the blink's destination probe is an exploit";
    EXPECT_FALSE(has_notification(fx.events, "SCORES"));
    EXPECT_TRUE(fx.flag_dropped(1))
        << "the carried flag drops at the departure point instead";
    EXPECT_EQ(depart_x, fx.flag_x(1));
    EXPECT_EQ(depart_y, fx.flag_y(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
}

TEST_F(ModesCtf, pickup_during_blink_is_refused_and_flag_stays_home)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* mage = fx.spawn_living(FAMILY_MAGE, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    walker* marker = fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(flag1->xpos(), flag1->ypos());
    marker->set_lifetime(5);
    marker->set_ani_type(ANI_SPIN);

    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special());
    fx.tick(20);  // the blink lands the mage on the enemy flag

    ASSERT_LE(std::abs(mage->xpos() - 544), 8);
    ASSERT_LE(std::abs(mage->ypos() - 800), 8);
    EXPECT_TRUE(fx.flag_at_home(1))
        << "the blink's destination probe must not pick the flag up";
    EXPECT_EQ(0u, fx.carrier_id(1));
    EXPECT_EQ(544, flag1->xpos()) << "and the flag must not relocate";
    EXPECT_EQ(800, flag1->ypos());
    EXPECT_EQ(0, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
    EXPECT_EQ(0, count_notifications(fx.events, "DROPPED"));

    // The marker is stale on later ticks: a real touch picks it up.
    (void)fx.world().query_passable(static_cast<float>(flag1->xpos()),
                                    static_cast<float>(flag1->ypos()), mage);
    EXPECT_TRUE(fx.flag_carried(1));
    EXPECT_EQ(mage->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
}

TEST_F(ModesCtf, teleport_drop_on_impassable_departure_returns_home)
{
    TeleportWorld fx(flag_family_);
    fx.tick(1);
    fx.runner->setxy(320, 320);
    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    fx.tick(1);

    PixieData& grid = fx.world().grid;
    for (int gy = 20; gy <= 21; ++gy)
        for (int gx = 20; gx <= 21; ++gx)
            grid.data[static_cast<std::size_t>(gx + grid.w * gy)] = PIX_H_WALL1;
    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(96, 700);
    fx.tick(1);

    EXPECT_TRUE(fx.flag_at_home(1));
    EXPECT_EQ(544, fx.flag1->xpos());
    EXPECT_EQ(800, fx.flag1->ypos());
    EXPECT_EQ(0, fx.flag1->ignore());
    EXPECT_TRUE(has_notification(fx.events, "GREEN FLAG RETURNED!"));
    EXPECT_EQ(0, count_notifications(fx.events, "DROPPED"));
}

TEST_F(ModesCtf, one_blink_drops_every_carried_flag)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 96);
    walker* flag2 = fx.spawn_flag(flag_family_, 2, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 500, 150);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 760);
    fx.world().ctf_requested_team_count = 3;
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_EQ(3, fx.var(kSlotTeamCount));

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag2->eat_me(runner));
    fx.tick(1);

    fx.stage_self_teleport(runner);
    runner->setxy(200, 500);
    fx.tick(1);
    EXPECT_TRUE(fx.flag_dropped(1));
    EXPECT_TRUE(fx.flag_dropped(2));
    EXPECT_EQ(200, fx.flag_x(1));
    EXPECT_EQ(200, fx.flag_y(1));
    EXPECT_EQ(200, fx.flag_x(2));
    EXPECT_EQ(200, fx.flag_y(2));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG DROPPED!"));
    EXPECT_EQ(1, count_notifications(fx.events, "BLUE FLAG DROPPED!"));
}

TEST_F(ModesCtf, regrab_after_teleport_drop_stays_silent)
{
    TeleportWorld fx(flag_family_);
    walker* backup = fx.spawn_living(FAMILY_SOLDIER, 0, 232, 200);
    fx.tick(1);

    ASSERT_TRUE(fx.flag1->eat_me(fx.runner));
    ASSERT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"));
    fx.tick(1);

    fx.stage_self_teleport(fx.runner);
    fx.runner->setxy(440, 488);
    fx.tick(1);
    ASSERT_TRUE(fx.flag_dropped(1));

    ASSERT_TRUE(fx.flag1->eat_me(backup));
    EXPECT_TRUE(fx.flag_carried(1));
    EXPECT_EQ(backup->entity_id(), fx.carrier_id(1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN FLAG TAKEN!"))
        << "a regrab after a teleport drop must not re-announce TAKEN";
}

// ===========================================================================
// Control points
// PORTS: control_point_capture_and_contender_reset,
// retake_of_enemy_owned_point_flips_at_36_sole_ticks,
// corpses_inside_disc_do_not_contest_retake,
// majority_retake_accrues_while_outnumbered_owner_contests,
// equal_presence_holds_meter_without_reset,
// owner_dominance_decays_progress_stepwise,
// radius_edge_geometry_contests_at_exactly_48px,
// control_point_pulse_is_localized_to_owner_team,
// waypoint_touch_is_a_no_op (the reshaped boundary retirement).
// ===========================================================================

TEST_F(ModesCtf, control_point_capture_and_contender_reset)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* point = fx.spawn_point(point_family_, 320, 320);
    walker* holder = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320);
    walker* rival = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700);
    fx.tick(1);
    ASSERT_EQ(1, fx.var(kSlotCpCount));
    ASSERT_EQ(pos_pack(320, 320), fx.var(kSlotCpPos));

    // Single-team presence accumulates.
    fx.tick(10);
    EXPECT_EQ(0 + 1, fx.var(kSlotCpProgTeam1));
    EXPECT_GT(fx.var(kSlotCpProgress), 0);
    EXPECT_EQ(0, fx.var(kSlotCpOwner1)) << "still neutral";

    // Contender change resets the meter.
    const std::int32_t before_swap = fx.var(kSlotCpProgress);
    holder->setxy(96, 700);
    rival->setxy(352, 320);
    fx.tick(1);
    EXPECT_EQ(1 + 1, fx.var(kSlotCpProgTeam1));
    EXPECT_LE(fx.var(kSlotCpProgress), before_swap);

    // Sole presence to the capture threshold flips owner + entity team.
    fx.tick(kCpCaptureTicks);
    EXPECT_EQ(1 + 1, fx.var(kSlotCpOwner1));
    EXPECT_EQ(1, point->team_num());
    EXPECT_EQ(50u, fx.world().m_score[1]);
    EXPECT_TRUE(has_score_change(fx.events, 1, 50));
    EXPECT_TRUE(has_notification(fx.events, "GREEN TAKES WAYPOINT!"));
}

TEST_F(ModesCtf, waypoint_touch_is_a_no_op)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* point = fx.spawn_point(point_family_, 320, 320);
    walker* toucher = fx.spawn_living(FAMILY_SOLDIER, 1, 352, 320);
    fx.tick(1);
    ASSERT_EQ(1, fx.var(kSlotCpCount));

    const std::int32_t owner_before = fx.var(kSlotCpOwner1);
    const std::int32_t progress_before = fx.var(kSlotCpProgress);
    const std::uint32_t score_before = fx.world().m_score[1];
    const std::size_t events_before = fx.events.events().size();
    const int team_before = point->team_num();
    og::script::hooks::reset_hook_failures();

    EXPECT_EQ(1, point->eat_me(toucher)) << "the touch answers true";

    EXPECT_EQ(owner_before, fx.var(kSlotCpOwner1));
    EXPECT_EQ(progress_before, fx.var(kSlotCpProgress));
    EXPECT_EQ(team_before, point->team_num());
    EXPECT_EQ(score_before, fx.world().m_score[1]);
    EXPECT_EQ(events_before, fx.events.events().size())
        << "a touch must not emit anything the occupancy pass did not";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

namespace {

// Enemy-owned waypoint scaffold: one CP at (320,320) pre-owned by team 1
// (owner var + pad entity team stamped after the lazy init).
struct RetakeWorld : ModesCtfWorld
{
    walker* point = nullptr;
    walker* attacker = nullptr;
    walker* owner_bot = nullptr;

    RetakeWorld(int flag_family, int point_family)
    {
        spawn_flag(flag_family, 0, 96, 96);
        spawn_flag(flag_family, 1, 544, 800);
        point = spawn_point(point_family, 320, 320);
        attacker = spawn_living(FAMILY_SOLDIER, 0, 96, 700);
        owner_bot = spawn_living(FAMILY_SOLDIER, 1, 544, 700);
        world().ctf_requested_respawn_ticks = 5000;
        tick(1);
        world().mode.vars[kSlotCpOwner1] = 1 + 1;
        if (point != nullptr)
            point->set_team_num(1);
    }

    std::int32_t owner1() const { return var(kSlotCpOwner1); }
    std::int32_t progress() const { return var(kSlotCpProgress); }
    std::int32_t prog_team1() const { return var(kSlotCpProgTeam1); }
};

}  // namespace

TEST_F(ModesCtf, retake_of_enemy_owned_point_flips_at_36_sole_ticks)
{
    RetakeWorld fx(flag_family_, point_family_);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(1, fx.var(kSlotCpCount));

    fx.attacker->setxy(336, 320);  // 16px from the pad: inside the disc
    fx.tick(kCpCaptureTicks - 1);
    EXPECT_EQ(1 + 1, fx.owner1()) << "35 sole-occupancy ticks must not flip";
    EXPECT_EQ(0 + 1, fx.prog_team1());
    EXPECT_EQ(kCpCaptureTicks - 1, fx.progress());

    fx.tick(1);
    EXPECT_EQ(0 + 1, fx.owner1())
        << "the 36th sole-occupancy tick retakes the waypoint";
    EXPECT_EQ(0, fx.point->team_num()) << "pad entity recolors to the taker";
    EXPECT_EQ(0, fx.progress());
    EXPECT_EQ(0, fx.prog_team1());
    EXPECT_TRUE(has_notification(fx.events, "RED TAKES WAYPOINT!"));
}

TEST_F(ModesCtf, corpses_inside_disc_do_not_contest_retake)
{
    RetakeWorld fx(flag_family_, point_family_);
    walker* enemy_corpse = fx.spawn_hero(FAMILY_SOLDIER, 1, 320, 352, 21);
    walker* friendly_corpse = fx.spawn_hero(FAMILY_SOLDIER, 0, 320, 288, 22);
    ASSERT_NE(nullptr, enemy_corpse);
    ASSERT_NE(nullptr, friendly_corpse);
    enemy_corpse->set_dead(1);
    friendly_corpse->set_dead(1);

    fx.attacker->setxy(336, 320);
    fx.tick(kCpCaptureTicks);
    ASSERT_TRUE(enemy_corpse->dead());
    ASSERT_TRUE(friendly_corpse->dead());
    EXPECT_EQ(0 + 1, fx.owner1())
        << "corpses inside the disc must not contest";
}

TEST_F(ModesCtf, majority_retake_accrues_while_outnumbered_owner_contests)
{
    RetakeWorld fx(flag_family_, point_family_);
    walker* attacker_b = fx.spawn_living(FAMILY_SOLDIER, 0, 304, 320);
    ASSERT_NE(nullptr, attacker_b);

    fx.attacker->setxy(336, 320);
    fx.owner_bot->setxy(320, 352);  // owner contests inside the disc, 2v1
    fx.tick(kCpCaptureTicks - 1);
    EXPECT_EQ(1 + 1, fx.owner1());
    EXPECT_EQ(0 + 1, fx.prog_team1())
        << "a 2v1 strict majority accrues for the attackers";
    EXPECT_EQ(kCpCaptureTicks - 1, fx.progress());

    fx.tick(1);
    EXPECT_EQ(0 + 1, fx.owner1())
        << "a 2v1 majority retakes through the contesting owner";
}

TEST_F(ModesCtf, equal_presence_holds_meter_without_reset)
{
    RetakeWorld fx(flag_family_, point_family_);
    fx.world().mode.vars[kSlotCpProgress] = 10;
    fx.world().mode.vars[kSlotCpProgTeam1] = 0 + 1;

    fx.attacker->setxy(336, 320);
    fx.owner_bot->setxy(320, 352);  // 1v1: no strict majority
    fx.tick(20);
    EXPECT_EQ(1 + 1, fx.owner1());
    EXPECT_EQ(10, fx.progress()) << "an even contest freezes the meter";
    EXPECT_EQ(0 + 1, fx.prog_team1())
        << "an even contest must not reset the contender";
}

TEST_F(ModesCtf, owner_dominance_decays_progress_stepwise)
{
    RetakeWorld fx(flag_family_, point_family_);
    fx.world().mode.vars[kSlotCpProgress] = 30;
    fx.world().mode.vars[kSlotCpProgTeam1] = 0 + 1;

    fx.owner_bot->setxy(336, 320);  // owner alone in the disc
    fx.tick(5);
    EXPECT_EQ(25, fx.progress())
        << "owner-alone ticks decay progress one step per tick";
    EXPECT_EQ(0 + 1, fx.prog_team1())
        << "decay keeps the contending team until the meter empties";

    fx.tick(25);
    EXPECT_EQ(0, fx.progress());
    EXPECT_EQ(0, fx.prog_team1()) << "draining to zero clears the contender";

    fx.tick(5);
    EXPECT_EQ(0, fx.progress()) << "an empty meter stays empty";
    EXPECT_EQ(1 + 1, fx.owner1());
}

TEST_F(ModesCtf, radius_edge_geometry_contests_at_exactly_48px)
{
    RetakeWorld fx(flag_family_, point_family_);
    fx.attacker->setxy(336, 320);
    fx.owner_bot->setxy(320 + 48, 320);
    fx.tick(10);
    EXPECT_EQ(0, fx.progress())
        << "an enemy at exactly 48px is inside the disc and contests (1v1)";

    fx.owner_bot->setxy(320 + 49, 320);
    fx.tick(10);
    EXPECT_EQ(10, fx.progress()) << "one px past the radius stops contesting";
    EXPECT_EQ(0 + 1, fx.prog_team1());
    EXPECT_EQ(1 + 1, fx.owner1());
}

TEST_F(ModesCtf, control_point_pulse_is_localized_to_owner_team)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_point(point_family_, 320, 320);
    walker* near_member = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320);
    walker* far_member = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 800);
    walker* enemy_near = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 250);
    fx.tick(1);

    // Enemy walks out of the radius so team 0 holds the point alone.
    enemy_near->setxy(544, 700);
    fx.tick(kCpCaptureTicks + 2);
    ASSERT_EQ(0 + 1, fx.var(kSlotCpOwner1));

    // The pulse fires on the capture tick: near owner-team livings only.
    EXPECT_EQ(1.0f, near_member->speed_bonus());
    EXPECT_GT(near_member->speed_bonus_left(), 0);
    EXPECT_EQ(0.0f, far_member->speed_bonus());
    EXPECT_EQ(0, enemy_near->speed_bonus_left());
}

// ===========================================================================
// Win conditions + the engine latch
// PORTS: capture_limit_win_sets_match_end_shape, bot_win_keeps_same_map_cursor,
// time_limit_win_picks_leader_with_tiebreakers; NEW: decided match
// re-asserts the full end shape for 100 further ticks with no more mode Lua.
// ===========================================================================

TEST_F(ModesCtf, capture_limit_win_sets_match_end_shape)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 11);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_capture_limit = 1;
    fx.tick(1);
    ASSERT_EQ(1, fx.var(kSlotCaptureLimit));

    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag0->eat_me(runner));
    ASSERT_EQ(1, fx.captures(0));

    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(0, fx.world().ending);
    EXPECT_EQ(0, fx.world().mode.winner_team);
    EXPECT_TRUE(fx.world().mode.winner_is_player);
    EXPECT_EQ(kCtfLevelA + 1, fx.world().next_level)
        << "human win advances the campaign";
    EXPECT_TRUE(has_notification(fx.events, "RED TEAM WINS!"))
        << "match-end notify uses the team color name, not a bare number";
}

TEST_F(ModesCtf, bot_win_keeps_same_map_cursor)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);

    fx.world().mode.vars[kSlotCaptures + 1] = fx.var(kSlotCaptureLimit);
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(1, fx.world().mode.winner_team);
    EXPECT_FALSE(fx.world().mode.winner_is_player);
    EXPECT_EQ(kCtfLevelA, fx.world().next_level)
        << "bot win replays the same map";
}

TEST_F(ModesCtf, time_limit_win_picks_leader_with_tiebreakers)
{
    // Capture lead wins.
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 96, 96);
        fx.spawn_flag(flag_family_, 1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick(1);
        fx.world().mode.vars[kSlotCaptures + 1] = 2;
        fx.world().mode.vars[kSlotCaptures + 0] = 1;
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Captures tied: larger m_score wins.
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 96, 96);
        fx.spawn_flag(flag_family_, 1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick(1);
        fx.world().m_score[1] = 700;
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Full tie: smaller team index wins.
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 96, 96);
        fx.spawn_flag(flag_family_, 1, 544, 800);
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
        fx.tick(1);
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team);
    }
}

TEST_F(ModesCtf, decided_match_reasserts_win_shape_every_tick)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 11);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_capture_limit = 1;
    fx.tick(1);
    ASSERT_TRUE(flag1->eat_me(runner));
    ASSERT_TRUE(flag0->eat_me(runner));
    fx.tick(1);
    ASSERT_TRUE(fx.world().game_ended);
    const int hud_events = static_cast<int>(fx.events.events().size());

    for (int i = 0; i < 100; ++i)
    {
        fx.tick(1);
        ASSERT_TRUE(fx.world().game_ended) << "tick " << i;
        ASSERT_EQ(0, fx.world().ending);
        ASSERT_EQ(kCtfLevelA + 1, fx.world().next_level);
    }
    EXPECT_EQ(hud_events, static_cast<int>(fx.events.events().size()))
        << "a decided match runs no further mode Lua (no new events)";
}

// ===========================================================================
// Respawn interplay (Lua eligibility + engine queue + on_respawn placement)
// Re-expresses the death-scan/queue behaviors the C++ suite exercised
// through run_death_scan, with the D1/D10 placement amendments.
// ===========================================================================

TEST_F(ModesCtf, dead_hero_revives_at_team_anchor_with_cursor_rotation)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(0, 256, 256);
    fx.spawn_anchor(0, 320, 256);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 7);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(10, fx.var(kSlotRespawnTicks));
    ASSERT_EQ(2, fx.world().respawn.anchor_count[0]);

    hero->set_dead(1);
    fx.tick(1);
    EXPECT_EQ(1, fx.world().respawn.respawn_queue.size())
        << "the Lua death scan schedules the corpse";
    const std::int32_t cursor_before = fx.var(kSlotAnchorCursor);

    fx.tick(10);
    EXPECT_FALSE(hero->dead()) << "the engine timer revived the hero";
    EXPECT_EQ(256, hero->xpos()) << "on_respawn placed it at anchor 0";
    EXPECT_EQ(256, hero->ypos());
    EXPECT_GT(fx.var(kSlotAnchorCursor), cursor_before)
        << "the rotation cursor advanced";

    // Second death rotates to the next anchor.
    hero->set_dead(1);
    fx.tick(11);
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(320, hero->xpos()) << "the cursor rotated to anchor 1";
    EXPECT_EQ(256, hero->ypos());
}

TEST_F(ModesCtf, cp_ownership_halves_the_respawn_delay_at_schedule_time)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_point(point_family_, 320, 320);
    fx.spawn_anchor(0, 256, 256);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 7);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 100;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    // Team 0 owns the CP: the schedule-time delay halves (D10 — no
    // mid-countdown rate change).
    fx.world().mode.vars[kSlotCpOwner1] = 0 + 1;
    hero->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(50, fx.world().respawn.respawn_queue.front().ticks_left)
        << "CP ownership halves the delay when the corpse is scheduled";

    // Losing the CP mid-countdown does NOT restore the full delay.
    fx.world().mode.vars[kSlotCpOwner1] = 0;
    fx.tick(49);
    ASSERT_TRUE(hero->dead());
    fx.tick(1);
    EXPECT_FALSE(hero->dead())
        << "the halved delay stands (schedule-time simplification)";
}

TEST_F(ModesCtf, dead_bot_respawns_as_replacement_at_anchor)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(0, 256, 256);
    walker* bot = fx.spawn_living(FAMILY_ARCHER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(1, alive_on_team(fx.world(), 0));

    bot->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(1, fx.world().respawn.respawn_queue.front().kind)
        << "a non-roster corpse queues as an AI replacement";

    fx.tick(10);
    ASSERT_EQ(1, alive_on_team(fx.world(), 0));
    const walker* replacement = nullptr;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 0)
            replacement = w;
    }
    ASSERT_NE(nullptr, replacement);
    EXPECT_EQ(FAMILY_ARCHER, replacement->family())
        << "the replacement keeps the corpse's family";
    EXPECT_EQ(256, replacement->xpos()) << "placed at the team anchor";
    EXPECT_EQ(256, replacement->ypos());
}

TEST_F(ModesCtf, generator_owned_spawns_stay_out_of_the_respawn_queue)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* keeper = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    walker* owned = fx.spawn_living(FAMILY_ORC, 0, 260, 200);
    owned->set_owner(keeper);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    owned->set_dead(1);
    fx.tick(1);
    for (const auto& entry : fx.world().respawn.respawn_queue)
    {
        EXPECT_NE(entry.walker_entity_id, owned->entity_id())
            << "an owned spawn is the generator's business, not the scan's";
    }
}

// ===========================================================================
// HUD / beacons (the Lua observability writes)
// ===========================================================================

TEST_F(ModesCtf, hud_rows_and_carrier_beacons_track_match_state)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);

    EXPECT_STREQ("RED 0/3", fx.world().mode.hud[0].text.data());
    EXPECT_EQ(0, fx.world().mode.hud[0].team);
    EXPECT_STREQ("GREEN 0/3", fx.world().mode.hud[1].text.data());
    EXPECT_EQ(1, fx.world().mode.hud[1].team);
    EXPECT_EQ(0, fx.world().mode.beacons[1].entity_id) << "no carrier yet";

    ASSERT_TRUE(flag1->eat_me(runner));
    fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(runner->entity_id()),
              fx.world().mode.beacons[1].entity_id)
        << "the stolen flag's beacon marks its carrier";
    EXPECT_EQ(1, fx.world().mode.beacons[1].team);

    runner->set_dead(1);
    fx.tick(2);
    EXPECT_EQ(0, fx.world().mode.beacons[1].entity_id)
        << "the beacon clears when the flag is no longer carried";
}

// ===========================================================================
// AI director
// PORTS: director_partitions_roles_exactly,
// director_targets_enemy_carried_flag_at_its_live_position,
// director_sends_nearest_retriever_to_dropped_own_flag,
// director_sends_two_nearest_interceptors_after_enemy_carrier,
// director_never_touches_player_walkers,
// director_skips_frozen_and_dead_members,
// carrier_runs_home_distance_shrinks_each_cadence_then_captures,
// attacker_reaches_and_picks_up_enemy_flag_through_collision,
// defender_stays_leashed_while_attackers_leave.
// The director runs at the world-tick cadence (og.mod(world_tick, 15)),
// so these align the tick counter and cross one boundary per assertion
// window instead of invoking a C++ entry point.
// ===========================================================================

TEST_F(ModesCtf, director_partitions_roles_exactly)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* flag2 = fx.spawn_flag(flag_family_, 2, 544, 96);
    fx.world().ctf_requested_team_count = 3;
    // Oblist order fixes the partition: m0 carrier, m1/m2 defenders
    // (ceil(5/3) = 2 of the 5 remaining), attacker m3, escort m4,
    // attacker m5.
    walker* m0 = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* m1 = fx.spawn_living(FAMILY_SOLDIER, 0, 256, 256, ACT_SIT);
    walker* m2 = fx.spawn_living(FAMILY_SOLDIER, 0, 128, 128, ACT_SIT);
    walker* m3 = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 320, ACT_SIT);
    walker* m4 = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 320, ACT_SIT);
    walker* m5 = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 320, ACT_SIT);
    walker* enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    fx.spawn_living(FAMILY_SOLDIER, 2, 500, 150, ACT_CONTROL);
    ASSERT_NE(nullptr, enemy);

    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(3, fx.var(kSlotTeamCount));
    ASSERT_TRUE(flag2->eat_me(m0)) << "m0 picks up team 2's flag";
    ASSERT_TRUE(fx.flag_carried(2));

    align_before_cadence(fx.world());
    fx.tick(1);  // the cadence tick runs the director once

    // CARRIER: leader is the OWN flag entity, ordered home.
    EXPECT_EQ(flag0, m0->leader());
    EXPECT_TRUE(front_command_is(m0, COMMAND_GOTO, 96, 96));
    EXPECT_EQ(60, m0->stats()->commands.front().commandcount);

    // DEFENDERS: m1 is beyond the 96px leash and is sent home; m2 sits
    // inside it and is left alone.
    EXPECT_GT(manhattan(m1, 96, 96), 96);
    EXPECT_TRUE(front_command_is(m1, COMMAND_GOTO, 96, 96));
    EXPECT_EQ(45, m1->stats()->commands.front().commandcount);
    EXPECT_LE(manhattan(m2, 96, 96), 96);
    EXPECT_FALSE(m2->stats()->has_commands());

    // ATTACKERS: team 2's flag is secured (carried by the own team), so
    // both head for team 1's flag at its home spot.
    EXPECT_TRUE(front_command_is(m3, COMMAND_GOTO, 544, 800));
    EXPECT_EQ(60, m3->stats()->commands.front().commandcount);
    EXPECT_TRUE(front_command_is(m5, COMMAND_GOTO, 544, 800));

    // ESCORT: the 2nd would-be attacker shadows the carrier.
    EXPECT_EQ(m0, m4->leader());
    EXPECT_EQ(nullptr, m4->foe());
    EXPECT_TRUE(front_command_is(m4, COMMAND_FOLLOW, 0, 0));
    EXPECT_EQ(100, m4->stats()->commands.front().commandcount);

    // The enemy player walker is never touched.
    EXPECT_FALSE(enemy->stats()->has_commands());

    // The next cadence refreshes the same front commands in place instead
    // of growing the queues (s_refresh_front, not a forced push).
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(1u, m0->stats()->commands.size());
    EXPECT_EQ(1u, m1->stats()->commands.size());
    EXPECT_EQ(1u, m3->stats()->commands.size());
    EXPECT_EQ(1u, m4->stats()->commands.size());
}

TEST_F(ModesCtf, director_targets_enemy_carried_flag_at_its_live_position)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_flag(flag_family_, 2, 544, 96);
    fx.world().ctf_requested_team_count = 3;
    fx.spawn_living(FAMILY_SOLDIER, 0, 128, 128, ACT_SIT);  // defender
    walker* attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    walker* carrier = fx.spawn_living(FAMILY_SOLDIER, 2, 480, 300, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(3, fx.var(kSlotTeamCount));

    // Team 2 steals team 1's flag: from the attacker the carried flag is
    // nearer than team 2's home flag, so the goto converges on the rival
    // carrier — the emergent interception.
    ASSERT_TRUE(flag1->eat_me(carrier));
    fx.tick(1);  // the carried phase pins the flag to the carrier
    ASSERT_TRUE(fx.flag_carried(1));
    ASSERT_EQ(carrier->xpos(), fx.flag_x(1));

    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(attacker, COMMAND_GOTO, fx.flag_x(1),
                                 fx.flag_y(1)));
}

TEST_F(ModesCtf, director_sends_nearest_retriever_to_dropped_own_flag)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* thief = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* far_member = fx.spawn_living(FAMILY_SOLDIER, 1, 544, 96, ACT_SIT);
    walker* near_member = fx.spawn_living(FAMILY_SOLDIER, 1, 416, 480, ACT_SIT);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    // Steal team 1's flag, then kill the thief mid-map: the flag drops.
    ASSERT_TRUE(flag1->eat_me(thief));
    thief->set_dead(1);
    fx.tick(1);
    ASSERT_TRUE(fx.flag_dropped(1));
    const int drop_x = fx.flag_x(1);
    const int drop_y = fx.flag_y(1);

    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_TRUE(front_command_is(near_member, COMMAND_GOTO, drop_x, drop_y));
    EXPECT_EQ(45, near_member->stats()->commands.front().commandcount);
    EXPECT_TRUE(front_command_is(far_member, COMMAND_GOTO, 544, 800))
        << "the remaining member defends the (empty) flag home";
}

TEST_F(ModesCtf, director_sends_two_nearest_interceptors_after_enemy_carrier)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* thief = fx.spawn_living(FAMILY_SOLDIER, 0, 480, 736, ACT_SIT);
    walker* near1 = fx.spawn_living(FAMILY_SOLDIER, 1, 480, 800, ACT_SIT);
    walker* near2 = fx.spawn_living(FAMILY_SOLDIER, 1, 416, 800, ACT_SIT);
    walker* far1 = fx.spawn_living(FAMILY_SOLDIER, 1, 96, 800, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(flag1->eat_me(thief));

    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_EQ(thief, near1->foe());
    EXPECT_TRUE(front_command_is(near1, COMMAND_SEARCH, 0, 0));
    EXPECT_EQ(120, near1->stats()->commands.front().commandcount);
    EXPECT_EQ(thief, near2->foe());
    EXPECT_TRUE(front_command_is(near2, COMMAND_SEARCH, 0, 0));

    EXPECT_TRUE(front_command_is(far1, COMMAND_GOTO, 544, 800))
        << "the remaining member defends rather than joining the chase";
    EXPECT_FALSE(queue_contains(far1, COMMAND_SEARCH));
}

TEST_F(ModesCtf, director_never_touches_player_walkers)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* control = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_CONTROL);
    walker* bound = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 480, ACT_SIT);
    bound->set_user(0);
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_FALSE(control->stats()->has_commands());
    EXPECT_EQ(nullptr, control->leader());
    EXPECT_FALSE(bound->stats()->has_commands());
    EXPECT_EQ(nullptr, bound->leader());
    // The lone directable walker was assigned (sole member => defender,
    // beyond the leash => ordered home): the gate is per-walker.
    EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 96, 96));
}

TEST_F(ModesCtf, director_skips_frozen_and_dead_members)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    walker* frozen = fx.spawn_living(FAMILY_SOLDIER, 0, 320, 480, ACT_SIT);
    walker* dead = fx.spawn_living(FAMILY_SOLDIER, 0, 352, 480, ACT_SIT);
    walker* live = fx.spawn_living(FAMILY_SOLDIER, 0, 384, 480, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 700, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    frozen->stats()->set_frozen_delay(20);
    dead->set_dead(1);
    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_FALSE(frozen->stats()->has_commands());
    EXPECT_FALSE(dead->stats()->has_commands());
    EXPECT_TRUE(front_command_is(live, COMMAND_GOTO, 96, 96))
        << "the one eligible member is the whole roster (sole => defender)";
}

TEST_F(ModesCtf, carrier_runs_home_distance_shrinks_each_cadence_then_captures)
{
    ModesCtfWorld fx;
    walker* flag0 = fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 480, 800);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 480, 760, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 1, 96, 832, ACT_CONTROL);
    fx.world().ctf_requested_capture_limit = 5;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(flag1->eat_me(runner));

    int last_distance = manhattan(runner, 96, 96);
    bool captured = false;
    for (int window = 0; window < 90 && !captured; ++window)
    {
        fx.tick(kAiCadence);
        if (fx.captures(0) > 0)
        {
            captured = true;
            break;
        }
        const int now = manhattan(runner, 96, 96);
        ASSERT_LT(now, last_distance)
            << "carrier must make progress toward home every cadence window";
        last_distance = now;
        ASSERT_EQ(flag0, runner->leader())
            << "carrier leader is the own flag entity (auto-foe suppressed)";
        ASSERT_TRUE(queue_contains(runner, COMMAND_GOTO));
        ASSERT_TRUE(fx.flag_carried(1));
        ASSERT_EQ(runner->entity_id(), fx.carrier_id(1));
    }
    ASSERT_TRUE(captured)
        << "the carrier touching its own home flag banks the capture";
    ASSERT_TRUE(fx.flag_at_home(1));
}

TEST_F(ModesCtf, attacker_reaches_and_picks_up_enemy_flag_through_collision)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 480, 160);
    fx.spawn_anchor(0, 128, 96);
    fx.spawn_anchor(1, 512, 96);
    walker* defender = fx.spawn_living(FAMILY_SOLDIER, 0, 128, 160, ACT_RANDOM);
    walker* attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 1, 480, 192, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    bool picked_up = false;
    std::uint32_t carrier = 0;
    for (int i = 0; i < 800 && !picked_up; ++i)
    {
        fx.tick(1);
        if (fx.flag_carried(1))
        {
            picked_up = true;
            carrier = fx.carrier_id(1);
        }
    }
    ASSERT_TRUE(picked_up)
        << "a team-0 bot must reach the enemy flag and take it via the "
           "obmap collision -> eat_me path";
    ASSERT_TRUE(carrier == attacker->entity_id() ||
                carrier == defender->entity_id());
    ASSERT_EQ(attacker->entity_id(), carrier)
        << "the attacker role (2nd member) is the one sent across the map";
}

TEST_F(ModesCtf, defender_stays_leashed_while_attackers_leave)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(0, 128, 96);
    fx.spawn_anchor(1, 512, 832);
    walker* defender = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    walker* atk1 = fx.spawn_living(FAMILY_SOLDIER, 0, 192, 160, ACT_RANDOM);
    walker* atk2 = fx.spawn_living(FAMILY_SOLDIER, 0, 224, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 1, 544, 768, ACT_CONTROL);
    fx.world().ctf_requested_respawn_ticks = 5000;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());

    int defender_max = 0;
    int attacker_max = 0;
    for (int window = 0; window < 24; ++window)
    {
        fx.tick(kAiCadence);
        if (window < 4)
            continue;
        if (!defender->dead())
            defender_max = std::max(defender_max, manhattan(defender, 96, 96));
        if (!atk1->dead())
            attacker_max = std::max(attacker_max, manhattan(atk1, 96, 96));
        if (!atk2->dead())
            attacker_max = std::max(attacker_max, manhattan(atk2, 96, 96));
    }
    ASSERT_LE(defender_max, 250)
        << "the defender must stay leashed near the flag home";
    ASSERT_GT(attacker_max, 400)
        << "attackers must leave the base toward the enemy flag";
}

// ===========================================================================
// Classic no-op + determinism
// PORTS: classic_world_is_untouched_and_deterministic,
// classic_world_emits_no_ctf_events,
// classic_world_never_sees_director_or_goto_and_rng_is_stable,
// ctf_bot_match_is_deterministic_across_runs,
// teleport_rule_is_deterministic_across_runs,
// directed_bot_match_is_deterministic, bot_match_produces_pickups_and_captures.
// ===========================================================================

namespace {

std::string run_classic_skirmish_checked(int ticks, bool* saw_goto)
{
    ModesCtfWorld fx(1);
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_SOLDIER, 0, 192, 160, ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 160, 320, ACT_RANDOM);
    fx.spawn_living(FAMILY_ORC, 1, 192, 320, ACT_RANDOM);
    for (int i = 0; i < ticks; ++i)
    {
        fx.tick(1);
        for (const auto& uptr : fx.world().oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && queue_contains(w, COMMAND_GOTO))
                *saw_goto = true;
        }
    }
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_FALSE(fx.world().mode.init_attempted);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesCtf, classic_world_never_sees_director_or_goto_and_rng_is_stable)
{
    bool saw_goto = false;
    const std::string first = run_classic_skirmish_checked(300, &saw_goto);
    const std::string second = run_classic_skirmish_checked(300, &saw_goto);
    EXPECT_FALSE(saw_goto) << "COMMAND_GOTO issued in a classic world";
    ASSERT_EQ(first, second)
        << "twin classic runs must match byte for byte (rng stream included)";
}

TEST_F(ModesCtf, classic_world_emits_no_ctf_events)
{
    ModesCtfWorld fx(1);
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    fx.spawn_living(FAMILY_ORC, 1, 480, 800);
    fx.tick(50);
    EXPECT_FALSE(has_notification(fx.events, "CAPTURE THE FLAG"));
    EXPECT_FALSE(has_notification(fx.events, "FLAG"));
}

namespace {

// Open 2-team arena with auto-spawned bot squads (no authored livings).
void build_bot_match(ModesCtfWorld& fx, int flag_family)
{
    fx.spawn_flag(flag_family, 0, 160, 128);
    fx.spawn_flag(flag_family, 1, 160, 800);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(0, 224, 96);
    fx.spawn_anchor(1, 96, 832);
    fx.spawn_anchor(1, 224, 832);
    fx.world().ctf_requested_respawn_ticks = 30;
}

std::string run_bot_match_digest(int flag_family, int ticks)
{
    ModesCtfWorld fx;
    build_bot_match(fx, flag_family);
    fx.tick(ticks);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesCtf, directed_bot_match_is_deterministic)
{
    const std::string first = run_bot_match_digest(flag_family_, 300);
    const std::string second = run_bot_match_digest(flag_family_, 300);
    ASSERT_EQ(first, second)
        << "same seed + same arena must reproduce identical roles, commands, "
           "and movement";
}

TEST_F(ModesCtf, bot_match_produces_pickups_and_captures)
{
    ModesCtfWorld fx;
    build_bot_match(fx, flag_family_);

    bool saw_pickup = false;
    for (int i = 0; i < 1000 && !fx.world().game_ended; ++i)
    {
        fx.tick(1);
        if (fx.flag_carried(0) || fx.flag_carried(1))
            saw_pickup = true;
    }

    ASSERT_TRUE(fx.ctf_active());
    ASSERT_TRUE(saw_pickup) << "flags must get picked up in a directed match";
    ASSERT_GE(fx.captures(0) + fx.captures(1), 1)
        << "a directed 5v5 bot match must produce at least one capture in "
           "1000 ticks";
}

namespace {

struct TeleportRunResult
{
    std::string digest;
    int dropped_notifications = 0;

    bool operator==(const TeleportRunResult& o) const = default;
};

TeleportRunResult run_teleport_script(int flag_family, int ticks)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family, 0, 160, 160);
    walker* flag1 = fx.spawn_flag(flag_family, 1, 480, 800);
    walker* padA = fx.spawn_teleporter(320, 320);
    fx.spawn_teleporter(480, 640);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(1);  // team 1 fields a five-bot squad (mage + skeleton included)

    runner->center_on(padA);
    flag1->eat_me(runner);
    fx.tick(1);
    (void)fx.world().query_passable(static_cast<float>(padA->xpos()),
                                    static_cast<float>(padA->ypos()), runner);
    fx.tick(5);  // pad ride carried the flag through; settle
    fx.stage_self_teleport(runner);
    runner->setxy(112, 200);  // staged blink: drop
    fx.tick(ticks);

    TeleportRunResult result;
    result.digest = digest_world(fx.world());
    result.dropped_notifications = count_notifications(fx.events, "DROPPED");
    return result;
}

}  // namespace

TEST_F(ModesCtf, teleport_rule_is_deterministic_across_runs)
{
    const TeleportRunResult first = run_teleport_script(flag_family_, 100);
    const TeleportRunResult second = run_teleport_script(flag_family_, 100);
    ASSERT_GE(first.dropped_notifications, 1)
        << "the scripted blink must have dropped the flag";
    ASSERT_EQ(first, second)
        << "the teleport rule reads only deterministic position/entity state";
}

TEST_F(ModesCtf, ctf_bot_match_is_deterministic_across_runs)
{
    // The C++ twin used a mid-map control point; keep it for parity of
    // scenario shape (CP pulses + respawn speedup in the digest).
    auto run = [&](int ticks) {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 160, 160);
        fx.spawn_flag(flag_family_, 1, 480, 800);
        fx.spawn_anchor(0, 128, 128);
        fx.spawn_anchor(0, 192, 128);
        fx.spawn_anchor(1, 448, 832);
        fx.spawn_anchor(1, 512, 832);
        fx.spawn_point(point_family_, 320, 480);
        fx.world().ctf_requested_respawn_ticks = 30;
        fx.tick(ticks);
        EXPECT_TRUE(fx.ctf_active());
        return digest_world(fx.world());
    };
    const std::string first = run(300);
    const std::string second = run(300);
    ASSERT_EQ(first, second)
        << "same seed + same scripted CTF scenario must replay identically";
}

// ===========================================================================
// Instruction budget headroom
// ===========================================================================

// A full mode tick (death scan + flags + CPs + director cadence + win check
// + HUD) on a busy world stays comfortably inside a 10x-REDUCED instruction
// budget (500k vs the 5M production budget).
TEST_F(ModesCtf, full_mode_tick_fits_a_tenth_of_the_instruction_budget)
{
    og::script::g_test_world_instruction_budget = 500000;
    {
        ModesCtfWorld fx;
        fx.spawn_flag(flag_family_, 0, 160, 128);
        fx.spawn_flag(flag_family_, 1, 160, 800);
        fx.spawn_point(point_family_, 320, 320);
        fx.spawn_point(point_family_, 320, 480);
        fx.spawn_point(point_family_, 160, 480);
        fx.spawn_point(point_family_, 480, 480);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(0, 224, 96);
        fx.spawn_anchor(1, 96, 832);
        fx.spawn_anchor(1, 224, 832);
        fx.world().ctf_requested_respawn_ticks = 30;
        fx.tick(1);  // init (the priciest single dispatch) under the budget
        ASSERT_TRUE(fx.ctf_active());
        fx.tick(45);  // 3 director cadences + all per-tick phases
        EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
            << "a 10x-reduced budget must never trip";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    og::script::g_test_world_instruction_budget = 0;
}

// ===========================================================================
// mode_core probes (helpers the CTF flow does not enter)
// ===========================================================================

TEST_F(ModesCtf, mode_core_probe_helpers_answer_exactly)
{
    ModesCtfWorld fx(kProbeLevel);
    fx.spawn_living(FAMILY_ORC, 1, 480, 480);
    walker* corpse = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    walker* stain = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    stain->setxy(200, 200);
    stain->set_team_num(0);
    // A second stain, far from the corpse: the scrub is position-keyed, so
    // this one must outlive the tick. Both are read back by id afterwards --
    // the tick reaps what it kills, so neither pointer survives it.
    walker* far_stain = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, far_stain);
    far_stain->setxy(480, 200);
    far_stain->set_team_num(0);
    const std::uint32_t stain_id = stain->entity_id();
    const std::uint32_t far_stain_id = far_stain->entity_id();
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active) << "the probe init must succeed";
    EXPECT_TRUE(vm_logged(fx.world(), "difficulty\t100"))
        << "og.match_setting('difficulty') reads the session percent";
    EXPECT_TRUE(vm_logged(fx.world(), "refresh_empty\t0"))
        << "s_refresh_front on an empty queue is a loud error";
    EXPECT_TRUE(vm_logged(fx.world(), "pp\t16715775"));
    EXPECT_TRUE(vm_logged(fx.world(), "pxy\t123\t456"));
    EXPECT_TRUE(vm_logged(fx.world(), "mask\t3\t0\t1"));
    EXPECT_TRUE(vm_logged(fx.world(), "madd\t1\t9"));
    // activate_teams(0b1101): all -> 13; first 2 -> {0,2} = 5; clamp(9) -> 13.
    EXPECT_TRUE(vm_logged(fx.world(), "act\t13\t5\t13"));
    EXPECT_EQ(nullptr, fx.world().find_by_id(stain_id))
        << "scrub_corpse kills the fresh stain, and the tick reaps it";
    EXPECT_NE(nullptr, fx.world().find_by_id(far_stain_id))
        << "a stain away from the corpse is left alone by the scrub";
    EXPECT_STREQ("YELLOW 7/9", fx.world().mode.hud[0].text.data());
    EXPECT_EQ(3, fx.world().mode.hud[0].team);
    // match.resolve_limit, the per-field manifest ladder TDM and Mutant read
    // their limits through: request beats row beats default, and a row that
    // omits (or zeroes) a field falls back instead of erroring on nil.
    EXPECT_TRUE(vm_logged(fx.world(), "lim_full\t5\t7"))
        << "the row's value, then the explicit request over it";
    EXPECT_TRUE(vm_logged(fx.world(), "lim_sparse\t20\t7200"))
        << "a sparse row falls back to the mode defaults, per field";
    EXPECT_TRUE(vm_logged(fx.world(), "lim_nil_zero\t20\t7200"))
        << "no row at all, and a zeroed field, both fall back";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// The registration path itself: manifest rows for OTHER modes register
// nothing (kOtherModeLevel carries a "tdm" row in the test manifest), so a
// scripted world on that id falls to classic rules like an unbound level.
TEST_F(ModesCtf, other_mode_manifest_rows_do_not_bind_ctf)
{
    ModesCtfWorld fx(kOtherModeLevel);
    walker* flag1 = fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_flag(flag_family_, 0, 96, 96);
    walker* runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active)
        << "a row for another mode must not register the CTF hooks";
    EXPECT_FALSE(has_notification(fx.events, "CAPTURE THE FLAG"));

    // A flag touch with the CTF mode inactive is refused outright (the
    // MODE_ID guard — the ctf.active analogue).
    EXPECT_EQ(1, flag1->eat_me(runner));
    EXPECT_EQ(0u, fx.carrier_id(1));
    EXPECT_EQ(0, flag1->ignore());
    EXPECT_FALSE(has_notification(fx.events, "FLAG TAKEN"));

    // The authored core family falls through to the shipped C++ delegation
    // when no scripted CTF match is live — inert on a non-TYPE_CTF world,
    // exactly what core's own hook did.
    walker* core_flag = fx.spawn_flag(kModesFlagFamily, 1, 500, 700);
    ASSERT_NE(nullptr, core_flag);
    EXPECT_EQ(1, core_flag->eat_me(runner));
    EXPECT_EQ(0, core_flag->ignore());
    EXPECT_FALSE(has_notification(fx.events, "FLAG TAKEN"));
}

// ===========================================================================
// The REAL shipped package: scen500 from builtin/modes.glad
// must activate through the manifest registration and run the Lua touch
// rules on its AUTHORED core-family flags — end to end, the case the
// synthetic fixture masked.
// ===========================================================================

namespace {

// A real campaign level under full sim context (the test_modes_levels
// LoadedModesLevel shape, over the shared modes_test loader hooks).
struct LoadedRealLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedRealLevel(int id)
        : level(id, true, &modes_test_level_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedRealLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

}  // namespace

TEST(ModesRealCampaign, shipped_scen500_runs_the_lua_ctf_rules)
{
    restore_default_campaigns();
    const std::string previous = get_mounted_campaign();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "builtin/modes.glad should restore and mount";
    {
        LoadedRealLevel fx(500);
        ASSERT_TRUE(fx.loaded) << "scen500 must load from the package";
        ASSERT_NE(0, fx.world().type & GameWorld::TYPE_SCRIPTED);

        fx.world().tick();
        ASSERT_TRUE(fx.world().mode.active)
            << "the manifest registration must activate CTF on scen500";
        EXPECT_EQ(kModeIdCtf, fx.world().mode.vars[kSlotModeId]);
        EXPECT_NE(0, fx.world().mode.vars[kSlotTeamMask]);
        EXPECT_STREQ("CTF", fx.world().mode.name.data());
        bool announced = false;
        for (const auto& ev : fx.events.events())
        {
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find("CAPTURE THE FLAG") != std::string::npos)
                announced = true;
        }
        EXPECT_TRUE(announced);

        // The authored flags are the CORE family: pick team 1's, touch it
        // with a fresh team-0 living, and the LUA rules must bank the
        // pickup (carrier var + TAKEN + the carried visual).
        const std::int32_t flag1_id =
            fx.world().mode.vars[kSlotFlagEntity + 1];
        ASSERT_NE(0, flag1_id) << "team 1 must author a flag on scen500";
        walker* flag1 =
            fx.world().find_by_id(static_cast<std::uint32_t>(flag1_id));
        ASSERT_NE(nullptr, flag1);
        EXPECT_EQ(kModesFlagFamily, flag1->family())
            << "the shipped maps author the core flag family";

        walker* runner = fx.world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, runner);
        runner->setxy(static_cast<short>(flag1->xpos() + 24),
                      static_cast<short>(flag1->ypos()));
        runner->set_team_num(0);
        runner->set_real_team_num(255);
        runner->set_act_type(ACT_CONTROL);

        ASSERT_TRUE(flag1->eat_me(runner));
        EXPECT_EQ(static_cast<std::int32_t>(runner->entity_id()),
                  fx.world().mode.vars[kSlotFlagCarrier + 1]);
        EXPECT_EQ(1, flag1->ignore());
        bool taken = false;
        for (const auto& ev : fx.events.events())
        {
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find("FLAG TAKEN!") != std::string::npos)
                taken = true;
        }
        EXPECT_TRUE(taken) << "the Lua pickup rule must announce TAKEN";

        fx.world().tick();
        EXPECT_EQ(static_cast<std::int32_t>(runner->entity_id()),
                  fx.world().mode.vars[kSlotFlagCarrier + 1])
            << "the carried flag survives a full tick of the Lua phases";
        for (const auto& err : fx.world().scripts().host().errors())
            ADD_FAILURE() << "script error: " << err.where << ": "
                          << err.message;
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    const std::string now = get_mounted_campaign();
    if (now == "modes")
        (void)unmount_campaign_package_with_error(now);
    if (previous.empty())
    {
        const std::string still = get_mounted_campaign();
        if (!still.empty())
            (void)unmount_campaign_package_with_error(still);
    }
    else if (get_mounted_campaign() != previous)
    {
        (void)mount_campaign_package_with_error(previous);
    }
}

// ===========================================================================
// Client mirror replication
// ===========================================================================

// Companion to the soccer case. Soccer desynced because its Lua-spawned ball
// carries a class-pack family byte (>= NUM_FAMILIES) that apply_snapshot used
// to clamp to 0. This mode spawns no pack-family entity, so it was never hit
// -- pin that, so a future mode entity that DOES reach for one fails here
// instead of in a player's match.

TEST_F(ModesCtf, match_replicates_to_a_client_mirror_without_hash_strikes)
{
    ModesCtfWorld fx;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    ModeMirror mirror(kCtfLevelA);

    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick;
    ASSERT_TRUE(fx.ctf_active());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(fx.world().mode.vars[static_cast<std::size_t>(slot)], mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
}

// ===========================================================================
// Teams: Match — the engine-respawn survival path (matched-teams WP-F, I5)
// ===========================================================================

// I5(b): a dead MATCHED bot's replacement is a FRESH walker whose level
// the engine re-derives from the corpse's stamped s_level snapshot — no
// Lua runs on that path. The test asserts the replacement's s_level
// equals the matched level, never walker-object identity.
TEST_F(ModesCtf, dead_matched_bot_respawns_at_its_matched_level)
{
    ModesCtfWorld fx;
    fx.world().ctf_requested_team_count = og::sim::kTeamCountMatched;
    fx.spawn_flag(flag_family_, 0, 96, 96);
    fx.spawn_flag(flag_family_, 1, 544, 800);
    fx.spawn_anchor(1, 256, 256);
    fx.spawn_anchor(1, 256, 320);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 5);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_TRUE(fx.ctf_active());
    ASSERT_EQ(5, alive_on_team(fx.world(), 1)) << "the matched init fill";
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"))
        << "the CTF fill runs before the MODE_ID latch, so the one-shot "
           "init announcement still fires (§7)";

    const int code = matched_plan_code(fx.var(kSlotMatchedPlan), 1);
    ASSERT_NE(0, code) << "team 1 solved against the L5 hero's target";
    // Squad member 1 is the soldier: L*+1 when k* >= 1, else L*.
    const int expected_level = code / 10 + (code % 10 >= 1 ? 1 : 0);

    walker* bot = nullptr;
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1 &&
            w->family() == FAMILY_SOLDIER)
            bot = w;
    }
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, bot->stats());
    ASSERT_EQ(expected_level, bot->stats()->level())
        << "the spawned soldier carries the plan's level stamp";
    const std::uint32_t corpse_id = bot->entity_id();

    bot->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1u, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(1, fx.world().respawn.respawn_queue.front().kind)
        << "a matched bot corpse queues as an AI replacement";
    EXPECT_EQ(expected_level, fx.world().respawn.respawn_queue.front().level)
        << "the queue snapshots the corpse's matched level";

    fx.tick(10);
    ASSERT_EQ(5, alive_on_team(fx.world(), 1));
    const walker* replacement = nullptr;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1 &&
            w->family() == FAMILY_SOLDIER)
            replacement = w;
    }
    ASSERT_NE(nullptr, replacement);
    ASSERT_NE(nullptr, replacement->stats());
    EXPECT_NE(corpse_id, replacement->entity_id())
        << "the replacement is a FRESH walker, not the revived corpse";
    EXPECT_EQ(expected_level, replacement->stats()->level())
        << "matched strength survives the engine respawn queue (I5)";
    EXPECT_EQ(code, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the stored plan is untouched by the engine path";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
