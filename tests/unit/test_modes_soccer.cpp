// The Soccer campaign-pack Lua (lib/mode_soccer_impl.lua + families/
// fx-ball.lua) behavior suite: every D7 rule as a sim case — init/
// activation, ball spawn, melee/weapon impulses, fixed-point friction,
// wall reflection, contact damage with the cap, last-toucher scoring with
// the own-goal/untouched no-score arm, kickoff resets and the freeze,
// score-limit and timeout wins, difficulty-submenu respawn honoring,
// director roles, spawn caps, HUD/beacons, determinism and instruction
// budget headroom. Runs on the shared modes-pack harness
// (tests/modes_pack_fixture.h) against the CURRENT repo pack bytes.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <cstdlib>
#include <format>
#include <string>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

// The mode-var slot map of lib/mode_soccer_impl.lua (table S). A silent
// re-map in the Lua breaks these tests loudly.
enum SoccerSlot : int {
    kSocModeId = 0,
    kSocScoreLimit = 8,
    kSocRespawnTicks = 9,
    kSocTeamCount = 10,
    kSocTeamMask = 11,
    kSocTimeLimit = 12,
    kSocAnchorCursor = 13,
    kSocBallEntity = 14,
    kSocBallPx = 15,
    kSocBallPy = 16,
    kSocBallVx = 17,
    kSocBallVy = 18,
    kSocLastTouch1 = 19,
    kSocLastKicker = 20,
    kSocKickoffUntil = 21,
    kSocKickoffPos = 22,
    kSocGoals = 23,     // +team
    kSocGoalPos = 27,   // +team, packed
    kSocGoalSize = 31,  // +team, packed
    kSocSpawnCap = 35,  // +team byte
};

inline constexpr int kModeIdSoccer = 4;  // mode_core.MODE.SOCCER
inline constexpr int kAiCadence = 15;
inline constexpr int kFriction = 64;           // fp/tick (T.friction)
inline constexpr int kKickoffFreeze = 36;      // T.kickoff_freeze
inline constexpr int kPauseFireFrequency = 16384;
inline constexpr int kSpawnMarkBit = 32768;

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

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
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

// A kind-0 (player revive) queue entry for this corpse id.
bool player_revive_pending(GameWorld& world, std::uint32_t id)
{
    for (const auto& entry : world.respawn.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == id)
            return true;
    }
    return false;
}

// Kind-1 (AI replacement) queue entries for a team.
int ai_entries_for_team(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& entry : world.respawn.respawn_queue)
    {
        if (entry.kind == 1 && entry.team == team)
            count++;
    }
    return count;
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
        digest += std::format("[id={} x={} y={} dead={} act={}",
                              w->entity_id(), w->xpos(), w->ypos(),
                              w->dead() ? 1 : 0,
                              static_cast<int>(w->act_type()));
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

using ModesSoccer = ModesPackTest;

namespace {

// Standard two-team pitch on kSoccerLevelA: anchors + one parked
// (ACT_CONTROL, undirected) living per team so no bot squads spawn.
// Kickoff (320, 464); team 0 defends the left strip, team 1 the right.
struct SoccerWorld : ModesCtfWorld
{
    walker* red = nullptr;
    walker* green = nullptr;

    explicit SoccerWorld(int level_id = kSoccerLevelA) : ModesCtfWorld(level_id)
    {
        spawn_anchor(0, 96, 448);
        spawn_anchor(0, 96, 480);
        spawn_anchor(1, 528, 448);
        spawn_anchor(1, 528, 480);
        red = spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        green = spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    }

    bool soccer_active() const { return var(kSocModeId) == kModeIdSoccer; }
    walker* ball() { return world().find_by_id(
        static_cast<std::uint32_t>(var(kSocBallEntity))); }
    int ball_cx() const { return var(kSocBallPx) / 256; }
    int ball_cy() const { return var(kSocBallPy) / 256; }
    void thaw_kickoff() { world().mode.vars[kSocKickoffUntil] = 0; }
    void set_ball(int cx, int cy, int vx_fp, int vy_fp)
    {
        world().mode.vars[kSocBallPx] = cx * 256;
        world().mode.vars[kSocBallPy] = cy * 256;
        world().mode.vars[kSocBallVx] = vx_fp;
        world().mode.vars[kSocBallVy] = vy_fp;
        walker* b = ball();
        if (b != nullptr)
            b->setxy(static_cast<short>(cx - 6), static_cast<short>(cy - 6));
    }
};

}  // namespace

// ===========================================================================
// Activation / init
// ===========================================================================

TEST_F(ModesSoccer, init_activates_anchor_teams_and_spawns_ball)
{
    SoccerWorld fx;
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_TRUE(fx.soccer_active());
    EXPECT_EQ(3, fx.var(kSocTeamMask));
    EXPECT_EQ(2, fx.var(kSocTeamCount));
    EXPECT_EQ(3, fx.var(kSocScoreLimit));
    EXPECT_EQ(60, fx.var(kSocRespawnTicks));
    EXPECT_EQ(10800, fx.var(kSocTimeLimit));
    EXPECT_EQ(pos_pack(320, 464), fx.var(kSocKickoffPos));
    EXPECT_EQ(pos_pack(16, 400), fx.team_var(kSocGoalPos, 0));
    EXPECT_EQ(pos_pack(32, 128), fx.team_var(kSocGoalSize, 0));
    EXPECT_EQ(pos_pack(592, 400), fx.team_var(kSocGoalPos, 1));

    walker* ball = fx.ball();
    ASSERT_NE(nullptr, ball) << "init must spawn the ball entity";
    EXPECT_EQ(Order::FX, ball->query_order());
    EXPECT_EQ(SCORE_TEAM_COUNT, ball->team_num()) << "ball starts neutral";
    EXPECT_EQ(320, fx.ball_cx());
    EXPECT_EQ(464, fx.ball_cy());
    EXPECT_EQ(314, ball->xpos()) << "12x12 ball is centered on the kickoff";
    EXPECT_EQ(0, fx.var(kSocBallVx));
    EXPECT_EQ(0, fx.var(kSocBallVy));
    EXPECT_GT(fx.var(kSocKickoffUntil), 0) << "match opens frozen";

    EXPECT_STREQ("SOCCER", fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(fx.events, "SOCCER! FIRST TO 3"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, init_below_two_anchor_teams_demotes)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 448);
    walker* survivor = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "fewer than two anchor teams"));
    EXPECT_FALSE(survivor->dead());
    fx.tick(5);
    EXPECT_FALSE(fx.world().mode.active) << "the demotion is latched";
}

TEST_F(ModesSoccer, four_team_pitch_strips_beyond_requested_count)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
    {
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 448);
        fx.spawn_living(FAMILY_SOLDIER, team,
                        static_cast<short>(96 + 64 * team), 96);
    }
    walker* stripped = fx.world().oblist.back().get();
    fx.world().ctf_requested_team_count = 2;
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3, fx.var(kSocTeamMask)) << "active mask is {0, 1}";
    EXPECT_EQ(2, fx.var(kSocTeamCount));
    EXPECT_TRUE(stripped->dead()) << "team 3 living is stripped";
    EXPECT_EQ(0, fx.team_var(kSocGoalPos, 2))
        << "inactive teams bank no goal rect";
    EXPECT_EQ(pos_pack(256, 16), fx.team_var(kSocGoalPos, 0));
}

TEST_F(ModesSoccer, empty_active_teams_get_bot_squads)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    // Team 1's only anchor is blocked by a parked generator: its bots
    // fall back to the RNG teleport (the one init-time draw, D1).
    fx.spawn_generator(FAMILY_TENT, 1, 528, 448);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
}

TEST_F(ModesSoccer, bad_manifest_rows_demote_with_recorded_errors)
{
    // A row missing an active team's goal rect.
    {
        ModesCtfWorld fx(9308);
        fx.spawn_anchor(0, 96, 448);
        fx.spawn_anchor(1, 528, 448);
        fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
        fx.tick(1);
        EXPECT_FALSE(fx.world().mode.active);
        EXPECT_TRUE(has_script_error(fx.world(), "no goal rect for team 1"));
    }
    // A registration with no manifest row at all.
    {
        ModesCtfWorld fx(9309);
        fx.spawn_anchor(0, 96, 448);
        fx.spawn_anchor(1, 528, 448);
        fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
        fx.tick(1);
        EXPECT_FALSE(fx.world().mode.active);
        EXPECT_TRUE(has_script_error(fx.world(), "no manifest row"));
    }
}

TEST_F(ModesSoccer, score_limit_and_respawn_requests_override_defaults)
{
    SoccerWorld fx;
    fx.world().ctf_requested_capture_limit = 5;
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(5, fx.var(kSocScoreLimit));
    EXPECT_EQ(30, fx.var(kSocRespawnTicks));
    EXPECT_TRUE(has_notification(fx.events, "SOCCER! FIRST TO 5"));
}

// ===========================================================================
// Ball physics: kicks, shots, friction, walls, contact damage
// ===========================================================================

TEST_F(ModesSoccer, walk_in_kick_scales_with_melee_damage_and_stamps_toucher)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Kicker center (312, 464): 8 px left of the ball center.
    fx.red->setxy(304, 456);
    const int speed_px = std::clamp(static_cast<int>(fx.red->damage()), 2, 8);
    fx.tick(1);

    EXPECT_EQ(static_cast<std::int32_t>(fx.red->entity_id()),
              fx.var(kSocLastKicker));
    EXPECT_EQ(1, fx.var(kSocLastTouch1)) << "last toucher is team 0 (+1)";
    EXPECT_EQ(0u, fx.ball()->team_num()) << "ball wears the kicker's color";
    // The kick fired rightward at speed_px, then this tick's flight
    // applied one friction step: |v| == speed_px*256 - 64, all on +x.
    EXPECT_EQ(speed_px * 256 - kFriction, fx.var(kSocBallVx));
    EXPECT_EQ(0, fx.var(kSocBallVy));
    EXPECT_GT(fx.ball_cx(), 320) << "ball moved away from the kicker";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, dead_center_kick_uses_the_kicker_facing)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Soldier center exactly on the ball center; facing right.
    fx.red->setxy(312, 456);
    // The act phase re-faces a living toward enddir, so stage both.
    fx.red->set_curdir(FACE_RIGHT);
    fx.red->set_enddir(FACE_RIGHT);
    fx.tick(1);

    EXPECT_GT(fx.var(kSocBallVx), 0) << "facing decides the direction";
    EXPECT_EQ(0, fx.var(kSocBallVy));
}

TEST_F(ModesSoccer, kickoff_freeze_blocks_kicks_until_expiry)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.red->setxy(304, 456);
    fx.tick(1);
    EXPECT_EQ(0, fx.var(kSocBallVx)) << "frozen ball ignores the touch";
    EXPECT_EQ(0, fx.var(kSocLastKicker));

    fx.tick(kKickoffFreeze);
    EXPECT_NE(0, fx.var(kSocBallVx)) << "freeze expired: the kick fires";
}

TEST_F(ModesSoccer, friction_stops_a_full_kick_in_32_ticks)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // A full kick is 8 px/tick = 2048 fp; T.friction = 64 drains it in
    // exactly 2048/64 = 32 ticks (~2.67 s at 12 ticks/s).
    fx.set_ball(200, 200, 8 * 256, 0);
    fx.tick(31);
    EXPECT_GT(fx.var(kSocBallVx), 0) << "still rolling at tick 31";
    fx.tick(1);
    EXPECT_EQ(0, fx.var(kSocBallVx)) << "stopped at tick 32";
    EXPECT_EQ(0, fx.var(kSocBallVy));
}

TEST_F(ModesSoccer, wall_bounce_reflects_the_blocked_axis)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Wall column at tile x=21 (px 336..351), rows around y=464.
    for (int gy = 27; gy <= 31; ++gy)
        fx.world().grid.data[21 + fx.world().grid.w * gy] = PIX_H_WALL1;
    // Ball center 330 moving +4 px/tick: the moved footprint (328..339)
    // overlaps the wall, so vx reflects, then one friction step:
    // vx = -(1024 * 960 / 1024) = -960.
    fx.set_ball(330, 464, 4 * 256, 0);
    fx.tick(1);

    EXPECT_EQ(-960, fx.var(kSocBallVx));
    EXPECT_EQ(326, fx.ball_cx()) << "bounced step lands 4 px back";
}

TEST_F(ModesSoccer, fast_contact_damages_capped_and_rebounds)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Victim parked in the path; ball at 20 px/tick stages the 12 cap.
    fx.green->setxy(352, 456);  // center (360, 464)
    const float hp_before = fx.green->stats()->hitpoints();
    fx.set_ball(330, 464, 20 * 256, 0);
    fx.tick(1);

    const float dealt = hp_before - fx.green->stats()->hitpoints();
    EXPECT_GT(dealt, 0.0f) << "contact at speed deals damage";
    EXPECT_LE(dealt, 12.0f) << "1 per px/tick, capped at 12 (D7)";
    EXPECT_LT(fx.var(kSocBallVx), 0) << "ball rebounds off the victim";
    // Restitution halves the speed (x256/2), then friction.
    EXPECT_LT(std::abs(fx.var(kSocBallVx)), 20 * 128);
}

TEST_F(ModesSoccer, slow_contact_deals_no_damage)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.green->setxy(352, 456);
    const float hp_before = fx.green->stats()->hitpoints();
    // 2 px/tick == the fast threshold: at it, no damage fires.
    fx.set_ball(356, 476, 2 * 256, 0);
    fx.tick(1);
    EXPECT_EQ(hp_before, fx.green->stats()->hitpoints());
}

TEST_F(ModesSoccer, last_kicker_is_immune_during_flight)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.green->setxy(352, 456);
    const float hp_before = fx.green->stats()->hitpoints();
    fx.set_ball(330, 464, 20 * 256, 0);
    fx.world().mode.vars[kSocLastKicker] =
        static_cast<std::int32_t>(fx.green->entity_id());
    fx.tick(1);

    EXPECT_EQ(hp_before, fx.green->stats()->hitpoints())
        << "the last kicker is not a contact victim";
    EXPECT_GT(fx.var(kSocBallVx), 0) << "no rebound either";
}

TEST_F(ModesSoccer, weapon_hit_impulses_along_the_shot_motion)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    walker* shot = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, shot);
    shot->setxy(312, 452);  // near the ball center
    shot->set_owner(fx.green);
    shot->set_lastx(0.0f);
    shot->set_lasty(8.0f);  // flying straight down
    const int speed_px =
        std::clamp(static_cast<int>(shot->damage()) * 2, 4, 12);
    fx.tick(1);

    EXPECT_EQ(0, fx.var(kSocBallVx)) << "impulse follows the shot motion";
    EXPECT_EQ(speed_px * 256 - kFriction, fx.var(kSocBallVy));
    EXPECT_EQ(2, fx.var(kSocLastTouch1)) << "the shooter is the toucher";
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.var(kSocLastKicker));
}

// ===========================================================================
// Goals, kickoff resets, wins
// ===========================================================================

TEST_F(ModesSoccer, goal_scores_for_the_last_toucher_and_resets)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Team 1 touched last; ball rolls into team 0's defended strip.
    fx.world().mode.vars[kSocLastTouch1] = 2;
    fx.set_ball(52, 464, -4 * 256, 0);
    fx.tick(2);

    EXPECT_EQ(1, fx.team_var(kSocGoals, 1));
    EXPECT_EQ(0, fx.team_var(kSocGoals, 0));
    EXPECT_TRUE(has_score_change(fx.events, 1, 400));
    EXPECT_TRUE(has_notification(fx.events, "GREEN TEAM GOAL!"));
    EXPECT_EQ(320, fx.ball_cx()) << "kickoff reset";
    EXPECT_EQ(464, fx.ball_cy());
    EXPECT_EQ(0, fx.var(kSocBallVx));
    EXPECT_EQ(0, fx.var(kSocLastTouch1)) << "touch history clears";
    EXPECT_EQ(SCORE_TEAM_COUNT, fx.ball()->team_num());
    EXPECT_GT(fx.var(kSocKickoffUntil),
              static_cast<std::int32_t>(fx.world().tick_count_))
        << "post-goal freeze armed";
}

TEST_F(ModesSoccer, own_goal_and_untouched_ball_score_nothing)
{
    // Own goal: the defender was the last toucher.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched
        fx.set_ball(52, 464, -4 * 256, 0);          // into team 0's own strip
        fx.tick(2);
        EXPECT_EQ(0, fx.team_var(kSocGoals, 0));
        EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
        EXPECT_TRUE(has_notification(fx.events, "NO GOAL!"));
        EXPECT_EQ(320, fx.ball_cx()) << "ball still resets";
    }
    // Untouched ball: nobody is credited.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(52, 464, -4 * 256, 0);
        fx.tick(2);
        EXPECT_EQ(0, fx.team_var(kSocGoals, 0));
        EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
        EXPECT_TRUE(has_notification(fx.events, "NO GOAL!"));
    }
}

TEST_F(ModesSoccer, score_limit_win_latches_and_reasserts)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.world().mode.vars[kSocGoals + 1] = 3;
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(1, fx.world().mode.winner_team);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_TRUE(has_notification(fx.events, "GREEN TEAM WINS!"));
    fx.tick(3);
    EXPECT_TRUE(fx.world().game_ended) << "win shape re-asserts every tick";
}

TEST_F(ModesSoccer, timeout_picks_leader_with_tiebreakers)
{
    // Goal lead wins.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.world().mode.vars[kSocGoals + 1] = 2;
        fx.world().mode.vars[kSocGoals + 0] = 1;
        fx.world().set_level_tick_count(10800 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Goals tied: larger m_score wins.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.world().m_score[1] = 700;
        fx.world().set_level_tick_count(10800 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Full tie: the lower team byte wins.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.world().set_level_tick_count(10800 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team);
    }
}

TEST_F(ModesSoccer, short_manifest_time_limit_is_honored)
{
    ModesCtfWorld fx(kSoccerLevelC);  // row carries time_limit = 120
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(120, fx.var(kSocTimeLimit));
    fx.tick(125);
    EXPECT_TRUE(fx.world().game_ended) << "manifest time limit decides";
}

// ===========================================================================
// Respawns honor the difficulty submenu (og.match_setting("respawn_mode"))
// ===========================================================================

namespace {

// A dead team-0 hero + a dead team-0 AI on an initialized pitch, plus a
// live keeper per team so the match shape stays sane.
struct SoccerRespawnWorld : SoccerWorld
{
    walker* hero = nullptr;
    walker* bot = nullptr;

    SoccerRespawnWorld()
    {
        hero = spawn_hero(FAMILY_SOLDIER, 0, 160, 448, 7);
        bot = spawn_living(FAMILY_SOLDIER, 0, 160, 480, ACT_GUARD);
    }

    void kill_both()
    {
        hero->set_dead(1);
        bot->set_dead(1);
    }
};

}  // namespace

TEST_F(ModesSoccer, respawn_mode_off_schedules_nothing)
{
    SoccerRespawnWorld fx;
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    fx.tick(3);
    EXPECT_FALSE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_EQ(0, ai_entries_for_team(fx.world(), 0));
    fx.tick(70);
    EXPECT_TRUE(fx.hero->dead()) << "Off means nobody comes back";
}

TEST_F(ModesSoccer, respawn_mode_heroes_revives_heroes_at_anchors)
{
    SoccerRespawnWorld fx;
    fx.world().respawn_mode = 1;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    fx.tick(2);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_EQ(0, ai_entries_for_team(fx.world(), 0))
        << "Heroes mode never schedules AI corpses";
    fx.tick(70);  // delay 60 + slack
    EXPECT_FALSE(fx.hero->dead());
    const bool at_anchor =
        (fx.hero->xpos() == 96 && (fx.hero->ypos() == 448 ||
                                   fx.hero->ypos() == 480));
    EXPECT_TRUE(at_anchor) << "revive lands on a team-0 anchor, got ("
                           << fx.hero->xpos() << "," << fx.hero->ypos() << ")";
    EXPECT_GT(fx.var(kSocAnchorCursor), 0) << "anchor cursor rotated";
    EXPECT_TRUE(fx.bot->dead());
}

TEST_F(ModesSoccer, respawn_mode_everyone_includes_unowned_ai)
{
    SoccerRespawnWorld fx;
    fx.world().respawn_mode = 2;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    fx.tick(2);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0))
        << "Everyone mode schedules the unowned AI corpse too";
}

TEST_F(ModesSoccer, respawn_mode_team_one_heroes_gates_by_team)
{
    SoccerRespawnWorld fx;
    walker* green_hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 560, 448, 8);
    fx.world().respawn_mode = 3;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    green_hero->set_dead(1);
    fx.tick(2);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()))
        << "Team 1 (internal team 0) heroes respawn";
    EXPECT_FALSE(player_revive_pending(fx.world(), green_hero->entity_id()))
        << "other teams' heroes stay down";
    EXPECT_EQ(0, ai_entries_for_team(fx.world(), 0));
}

// ===========================================================================
// Director: chasers on the own-goal side, goalie leash + engage
// ===========================================================================

TEST_F(ModesSoccer, chasers_walk_the_own_goal_side_approach_point)
{
    SoccerWorld fx;
    // Team-0 goalie designate near the defended strip + a chaser afield.
    walker* goalie = fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    walker* chaser = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 400, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    align_before_cadence(fx.world());
    fx.tick(1);

    // Ball (320, 464); enemy goal center (608, 464): the approach point is
    // 20 px past the ball on the own-goal side, so walking in kicks it
    // goalward. Chaser index 0 carries no stagger.
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(chaser, COMMAND_GOTO, 300, 464))
        << "chaser walks (300, 464)";
    // Goalie is inside the leash (dist 14 <= 48) and the ball is far from
    // the goal, so it holds position with no director GOTO.
    EXPECT_FALSE(front_command_is(goalie, COMMAND_GOTO, 300, 464));
    EXPECT_EQ(fx.var(kSocBallEntity),
              static_cast<std::int32_t>(goalie->leader_id()))
        << "everyone leads on the ball (auto-foe suppression)";
}

TEST_F(ModesSoccer, goalie_leashes_back_and_engages_a_threatening_ball)
{
    SoccerWorld fx;
    walker* goalie = fx.spawn_living(FAMILY_SOLDIER, 0, 150, 464, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // Beyond the 48 px leash from the goal center (32, 464): sent home.
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 32, 464))
        << "leash pulls the goalie back to the defended rect";

    // Inside the leash with the ball threatening the goal: the goalie
    // walks the clearing approach point (ball 60,464 -> approach 40,464).
    goalie->setxy(40, 470);
    fx.set_ball(60, 464, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 40, 464))
        << "goalie clears the ball via the approach geometry";
}

// ===========================================================================
// Spawn caps (manifest ally generators, kSoccerLevelD)
// ===========================================================================

TEST_F(ModesSoccer, manifest_spawn_caps_pause_and_resume_generators)
{
    ModesCtfWorld fx(kSoccerLevelD);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    walker* red = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    (void)red;
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    walker* tent = fx.spawn_generator(FAMILY_TENT, 0, 200, 200);
    ASSERT_NE(nullptr, tent);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(2, fx.team_var(kSocSpawnCap, 0)) << "row caps banked";
    EXPECT_EQ(-1, fx.team_var(kSocSpawnCap, 2)) << "holes stay uncapped";
    const float authored = tent->fire_frequency();

    // Two marked team-0 spawns == the cap: the tent pauses at cadence.
    walker* s1 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 200, ACT_GUARD);
    walker* s2 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 230, ACT_GUARD);
    s1->stats()->set_bit_flags(kSpawnMarkBit, 1);
    s2->stats()->set_bit_flags(kSpawnMarkBit, 1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_GE(tent->fire_frequency(),
              static_cast<float>(kPauseFireFrequency))
        << "at the cap the generator pauses";

    // Below the cap it resumes on the authored value.
    s1->set_dead(1);
    s2->set_dead(1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(authored, tent->fire_frequency()) << "resume restores authored";
}

// ===========================================================================
// HUD / beacon / registration
// ===========================================================================

TEST_F(ModesSoccer, hud_rows_and_ball_beacon_track_the_match)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    EXPECT_STREQ("RED 0/3", fx.world().mode.hud[0].text.data());
    EXPECT_STREQ("GREEN 0/3", fx.world().mode.hud[1].text.data());
    EXPECT_EQ(0, fx.world().mode.hud[0].team);
    EXPECT_EQ(fx.var(kSocBallEntity), fx.world().mode.beacons[0].entity_id)
        << "beacon slot 0 tracks the ball";

    fx.world().mode.vars[kSocGoals + 0] = 2;
    fx.tick(1);
    EXPECT_STREQ("RED 2/3", fx.world().mode.hud[0].text.data());
}

TEST_F(ModesSoccer, shipped_manifest_registers_the_soccer_levels)
{
    // scripts/mode_soccer.lua scans the committed manifest: scen820 binds
    // with THE PITCH's goal rects and kickoff.
    ModesCtfWorld fx(820);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(kModeIdSoccer, fx.var(kSocModeId));
    EXPECT_EQ(pos_pack(344, 216), fx.var(kSocKickoffPos));
    EXPECT_EQ(pos_pack(16, 160), fx.team_var(kSocGoalPos, 0));
    EXPECT_EQ(pos_pack(656, 160), fx.team_var(kSocGoalPos, 1));
    EXPECT_EQ(3, fx.var(kSocScoreLimit));
}

// ===========================================================================
// Determinism + instruction budget
// ===========================================================================

namespace {

std::string run_soccer_bot_match_digest(int ticks)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 432);
    fx.spawn_anchor(0, 96, 496);
    fx.spawn_anchor(1, 528, 432);
    fx.spawn_anchor(1, 528, 496);
    fx.tick(ticks);  // bot squads for both teams play the ball
    EXPECT_TRUE(fx.world().mode.active);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesSoccer, directed_bot_match_is_deterministic)
{
    const std::string first = run_soccer_bot_match_digest(400);
    const std::string second = run_soccer_bot_match_digest(400);
    ASSERT_EQ(first, second)
        << "same seed + same pitch must reproduce kicks, rolls and roles";
}

TEST_F(ModesSoccer, full_mode_tick_fits_a_tenth_of_the_instruction_budget)
{
    og::script::g_test_world_instruction_budget = 500000;
    {
        ModesCtfWorld fx(kSoccerLevelA);
        fx.spawn_anchor(0, 96, 432);
        fx.spawn_anchor(0, 96, 496);
        fx.spawn_anchor(1, 528, 432);
        fx.spawn_anchor(1, 528, 496);
        fx.tick(1);  // init (bot squads + ball) under the budget
        ASSERT_TRUE(fx.world().mode.active);
        fx.tick(45);  // 3 director cadences + kicks + flight + HUD
        EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
            << "a 10x-reduced budget must never trip";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    og::script::g_test_world_instruction_budget = 0;
}
