// The Soccer campaign-pack Lua (lib/mode_soccer_impl.lua + families/
// fx-ball.lua) behavior suite: every D7 rule as a sim case — init/
// activation, ball spawn, melee/weapon impulses, fixed-point friction,
// wall reflection, contact damage with the cap, last-toucher scoring with
// the scoring self-goal arms (opponent credit, last-distinct-other credit,
// forfeit) and the untouched-ball no-score arm, kickoff resets and freeze,
// the B1 kickoff wipe-revive backstop and dead-ball reset,
// score-limit and timeout wins, difficulty-submenu respawn honoring,
// director roles, spawn caps, HUD/beacons, determinism and instruction
// budget headroom. Runs on the shared modes-pack harness
// (tests/modes_pack_fixture.h) against the CURRENT repo pack bytes.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

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
    kSocStallSince = 43,
    kSocBallSpin = 44,
    kSocLastTouch2 = 45,
    kSocItemCursor = 46,
    kSocItemLast = 47,
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

// The longest announcement the log carries. D16 caps an announcement row
// at 25 characters, and the own-goal strings are the new worst case
// ("OWN GOAL! YELLOW +1" = 19).
std::size_t longest_notification(const og::sim::SimEventLog& log)
{
    std::size_t longest = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.size() > longest)
        {
            longest = ev.text.size();
        }
    }
    return longest;
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

void expect_no_soccer_script_errors(GameWorld& world)
{
    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << "script error: " << err.where << ": " << err.message;
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// Respawned pickups live in the fx list (lib/mode_items spawns them with
// og.add_fx_ob so the engine's walk-on eat scan finds them).
int live_treasures(GameWorld& world, int family)
{
    int count = 0;
    for (const auto& uptr : world.fxlist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Treasure && w->family() == family)
        {
            count++;
        }
    }
    return count;
}

// A live pickup whose TOP-LEFT is (x, y) — a pad's spawn subtracts 8 from
// its authored pixel center.
walker* item_at(GameWorld& world, int family, int x, int y)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Treasure && w->family() == family &&
            w->xpos() == x && w->ypos() == y)
        {
            return w;
        }
    }
    return nullptr;
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

// The front queue entry's command type, 0 when the queue is empty.
std::int32_t front_type(const walker* w)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return 0;
    return s->commands.front().commandtype;
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

void paint_water(GameWorld& world, int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        for (int x = left; x <= right; ++x)
        {
            world.grid.data[static_cast<std::size_t>(x + world.grid.w * y)] =
                PIX_WATER1;
        }
    }
}

int live_weapons_owned_by(GameWorld& world, const walker* owner)
{
    int count = 0;
    for (const auto& uptr : world.weaplist)
    {
        const walker* shot = uptr.get();
        if (shot != nullptr && !shot->dead() && shot->owner() == owner)
            count++;
    }
    return count;
}

// A live STAIN treasure whose top-left sits at (x, y).
bool stain_alive_at(GameWorld& world, int x, int y)
{
    for (const auto& uptr : world.fxlist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Treasure &&
            w->family() == FAMILY_STAIN && w->xpos() == x && w->ypos() == y)
        {
            return true;
        }
    }
    return false;
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

// The shared mode_anchors squad-seed header slot (#235): the latched
// squad-order code + 1, drawn once per match by the first bot-squad spawn.
inline constexpr int kSlotSquadSeed = 6;

// A team's live Living family bytes in oblist (spawn) order.
std::vector<int> team_families_in_order(GameWorld& world, int team)
{
    std::vector<int> families;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            families.push_back(static_cast<int>(w->family()));
        }
    }
    return families;
}

// Retires a team's dead guy-less bodies, simulating the engine sweep for
// corpses whose entries were genuinely lost (a full all-player queue). With
// corpse persistence (#221) a body whose entry is merely hand-cleared is
// re-adopted by the always-on scan next tick, so the no-revives-in-flight
// kickoff edge needs the drained entries' bodies gone too.
void retire_dead_guyless(GameWorld& world, int team)
{
    std::vector<walker*> corpses;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && w->dead() && w->myguy == nullptr &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team))
        {
            corpses.push_back(w);
        }
    }
    for (walker* w : corpses)
        world.retire_corpse(w);
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

// Ball + match accessors shared by the two pitch fixtures.
struct SoccerPitch : ModesCtfWorld
{
    explicit SoccerPitch(int level_id) : ModesCtfWorld(level_id) {}

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

// Standard two-team pitch on kSoccerLevelA: anchors + one parked
// (ACT_CONTROL, undirected) living per team so no bot squads spawn.
// Kickoff (320, 464); team 0 defends the left strip, team 1 the right.
struct SoccerWorld : SoccerPitch
{
    walker* red = nullptr;
    walker* green = nullptr;

    explicit SoccerWorld(int level_id = kSoccerLevelA) : SoccerPitch(level_id)
    {
        spawn_anchor(0, 96, 448);
        spawn_anchor(0, 96, 480);
        spawn_anchor(1, 528, 448);
        spawn_anchor(1, 528, 480);
        red = spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        green = spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    }
};

// FOURSQUARE pitch on kSoccerLevelB: four anchor teams, one parked living
// each. Kickoff (320, 480); the defended strips are team 0 top
// (256..383, 16..47), 1 right (592..623, 256..383), 2 bottom
// (256..383, 912..943), 3 left (16..47, 256..383).
struct SoccerFourWorld : SoccerPitch
{
    walker* players[4] = {nullptr, nullptr, nullptr, nullptr};

    SoccerFourWorld() : SoccerPitch(kSoccerLevelB)
    {
        for (int team = 0; team < 4; ++team)
        {
            const short x = static_cast<short>(96 + 64 * team);
            spawn_anchor(team, x, 700);
            players[team] = spawn_living(FAMILY_SOLDIER, team, x, 700);
        }
    }
};

// Spots the ball at (cx, cy), walks `w` onto it and runs one tick: the
// walk-in kick stamps `w`'s team as the toucher. A 16x16 living placed
// here has its center 8 px left of the ball center, inside kick_radius.
void touch_ball(SoccerPitch& fx, walker* w, int cx, int cy)
{
    fx.set_ball(cx, cy, 0, 0);
    w->setxy(static_cast<short>(cx - 16), static_cast<short>(cy - 8));
    fx.tick(1);
}

// Parks a living in the top-left corner, clear of every goal rect and of
// the ball, so it takes no further part.
void park_away(walker* w)
{
    w->setxy(96, 96);
}

// Builds the exact short-range fire_check geometry used by the director
// recovery test: an ignored neutral FX ball 40 px east of a team-0 soldier.
// Each denial arm below uses a fresh world so scratch-weapon RNG and dead
// probe cleanup cannot bleed between controls.
walker* prepare_ignored_ball_ray(SoccerWorld& fx, int shooter_x = 272)
{
    walker* shooter =
        fx.spawn_living(FAMILY_SOLDIER, 0, shooter_x, 456, ACT_SIT);
    if (shooter == nullptr)
        return nullptr;
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    shooter->stats()->set_weapon_cost(0);
    shooter->set_current_weapon(FAMILY_KNIFE);
    fx.tick(1);
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);
    shooter->set_foe(fx.ball());
    shooter->face_delta(1, 0);
    return shooter;
}

int g_weapon_customizer_calls = 0;
int g_weapon_on_fire_calls = 0;

void counting_one_pixel_weapon_profile(walker*, walker* weapon)
{
    g_weapon_customizer_calls++;
    weapon->set_stepsize(1.0f);
    weapon->set_lineofsight(1);
}

bool counting_reject_weapon_launch(walker*, walker* weapon)
{
    g_weapon_on_fire_calls++;
    weapon->set_dead(1);
    return false;
}

struct ScopedWeaponCustomizer
{
    FamilyDescriptor* descriptor = nullptr;
    void (*previous)(walker*, walker*) = nullptr;

    ScopedWeaponCustomizer(int family, void (*replacement)(walker*, walker*))
        : descriptor(const_cast<FamilyDescriptor*>(
              get_family_descriptor(family)))
    {
        if (descriptor != nullptr)
        {
            previous = descriptor->customize_weapon;
            descriptor->customize_weapon = replacement;
        }
    }

    ~ScopedWeaponCustomizer()
    {
        if (descriptor != nullptr)
            descriptor->customize_weapon = previous;
    }
};

struct ScopedWeaponFireHook
{
    FamilyDescriptor* descriptor = nullptr;
    bool (*previous)(walker*, walker*) = nullptr;

    ScopedWeaponFireHook(int family, bool (*replacement)(walker*, walker*))
        : descriptor(const_cast<FamilyDescriptor*>(
              get_family_descriptor(family)))
    {
        if (descriptor != nullptr)
        {
            previous = descriptor->on_fire_weapon;
            descriptor->on_fire_weapon = replacement;
        }
    }

    ~ScopedWeaponFireHook()
    {
        if (descriptor != nullptr)
            descriptor->on_fire_weapon = previous;
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
    ASSERT_NE(nullptr, ball->stats());
    EXPECT_TRUE(ball->stats()->query_bit_flags(BIT_SWIMMING))
        << "the real soccer ball carries its descriptor's SWIMMING flag";
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

// BOTS: OFF on two of the four-team pitch's sides (lineup A1/A2, the
// retired TEAMS count's successor) strips them exactly as TEAMS: 2 did.
TEST_F(ModesSoccer, four_team_pitch_strips_the_sides_switched_off)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
    {
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 448);
        fx.spawn_living(FAMILY_SOLDIER, team,
                        static_cast<short>(96 + 64 * team), 96);
    }
    walker* stripped = fx.world().oblist.back().get();
    fx.world().ctf_requested_fill[2] = og::sim::kFillNone;
    fx.world().ctf_requested_map_units[2] = og::sim::kMapUnitsOff;
    fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
    fx.world().ctf_requested_map_units[3] = og::sim::kMapUnitsOff;
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
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: the fill
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
    // Team 1's only anchor is blocked by a parked generator: its bots
    // fall back to the RNG teleport. Init-time draws: the #235 squad-order
    // seed (one og.rand inside the first squad spawn, before placement),
    // then the blocked-anchor teleports (D1).
    fx.spawn_generator(FAMILY_TENT, 1, 528, 448);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
}

TEST_F(ModesSoccer, init_stamps_the_generator_hp_denominator)
{
    // Same engine guarantee the other generator modes rely on: the pitch's
    // ally generators reach the renderer with a denominator, so a damaged one
    // draws a partial bar instead of being skipped at max_hp == 0.
    SoccerWorld fx;
    walker* tower = fx.spawn_generator(FAMILY_TOWER, 0, 200, 700, 2);
    ASSERT_NE(nullptr, tower);
    ASSERT_NE(nullptr, tower->stats());
    const float authored = tower->stats()->hitpoints();
    ASSERT_GT(authored, 0.0f);
    ASSERT_EQ(authored, tower->stats()->max_hitpoints())
        << "the engine stamps the denominator at set_difficulty";

    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_FALSE(tower->dead()) << "an active team's generator is kept";
    EXPECT_EQ(authored, tower->stats()->max_hitpoints())
        << "and mode init leaves it alone";

    tower->stats()->set_hitpoints(authored / 2.0f);
    EXPECT_EQ(authored, tower->stats()->max_hitpoints())
        << "damage moves hp, never the authored denominator";
    EXPECT_LT(tower->stats()->hitpoints(), tower->stats()->max_hitpoints())
        << "so the bar has a fraction to draw";
}

TEST_F(ModesSoccer, map_units_off_takes_the_pitch_generators_too)
{
    // Generators follow the same box as the livings (B4): an ally
    // generator is scenery here, not the board, so a box turned off takes
    // the pitch as well as the troops.
    SoccerWorld fx;
    for (auto& box : fx.world().ctf_requested_map_units)
        box = og::sim::kMapUnitsOff;
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: the trade
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
    walker* tent = fx.spawn_generator(FAMILY_TENT, 0, 200, 700);
    ASSERT_NE(nullptr, tent);
    fx.tick(1);

    ASSERT_TRUE(fx.soccer_active());
    EXPECT_TRUE(tent->dead()) << "generators follow the box (B4)";
    EXPECT_TRUE(fx.red->dead()) << "the authored livings go with it";
    EXPECT_EQ(5, alive_on_team(fx.world(), 0))
        << "and the emptied team is backfilled by the census behind the strip";
}

TEST_F(ModesSoccer, rosters_on_the_foursquare_pitch_field_all_four_sides)
{
    // The scen-841 shape on the FOURSQUARE pitch: rosters on the north
    // and south mouths field all FOUR authored sides — the rosters stay
    // untouched (equal companies, no allies gap) and the east/west teams
    // backfill with squads matched to the roster headcount (B2).
    SoccerPitch fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: backfills
    fx.world().ctf_requested_fill[3] = og::sim::kFillFair;
    walker* soldier = fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
    walker* other = fx.spawn_hero(FAMILY_SOLDIER, 2, 300, 860, 2);
    fx.tick(1);

    ASSERT_TRUE(fx.soccer_active());
    EXPECT_EQ(15, fx.var(kSocTeamMask)) << "all four authored sides";
    EXPECT_EQ(4, fx.var(kSocTeamCount));
    EXPECT_NE(nullptr, fx.ball()) << "the match still gets its ball";
    EXPECT_FALSE(soldier->dead());
    EXPECT_FALSE(other->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the rosters stay as-is";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the empty-team census fields a headcount-matched squad";
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(1, alive_on_team(fx.world(), 3));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, all_bot_anchor_only_pitch_takes_the_manifest_default)
{
    // The map's own value in the ALL-BOT shape (B1-B4): anchors-only
    // teams carry no fielded units, so on a FOUR-anchor pitch whose
    // manifest row declares teams = 2 (kSoccerLevelA, two authored goal
    // mouths) exactly the first two sides play — the retired OWN arm's
    // whole-domain refusal shape is gone with TROOPS.
    ModesCtfWorld fx(kSoccerLevelA);
    for (int team = 0; team < 4; ++team)
    {
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 448);
        fx.world().ctf_requested_fill[static_cast<std::size_t>(team)] =
            og::sim::kFillFair;  // E5: every wheel turned
    }
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active)
        << "the manifest default is a clean two-side match";
    EXPECT_EQ(0b0011, fx.var(kSocTeamMask));
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    EXPECT_EQ(0, alive_on_team(fx.world(), 2))
        << "an anchors-only team past the default fields nothing";
}

TEST_F(ModesSoccer, fair_teams_four_with_a_solo_roster_fields_four_teams)
{
    // Issue #218, the reporter's scenario: TROOPS: FAIR + TEAMS: 4 on the
    // FOURSQUARE pitch with a solo deployed roster. The EXPLICIT lobby
    // count wins the team COUNT — roster team 0 stays, teams 1..3 backfill
    // in index order and each fields a FAIR matched squad through the
    // empty-team census behind the strip. The tenth-budget override also
    // proves init with three matched-squad solves fits the headroom.
    BudgetOverride budget(500000);
    SoccerPitch fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
    arm_matched(fx.world());
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: backfills
    fx.world().ctf_requested_fill[2] = og::sim::kFillFair;
    fx.world().ctf_requested_fill[3] = og::sim::kFillFair;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
    ASSERT_NE(nullptr, hero);
    fx.tick(1);

    ASSERT_TRUE(fx.soccer_active());
    EXPECT_EQ(15, fx.var(kSocTeamMask))
        << "AUTO is every authored side: all four field (the TEAMS count "
           "is retired, lineup A1)";
    EXPECT_EQ(4, fx.var(kSocTeamCount));
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the roster is untouched";
    EXPECT_EQ(1, fx.var(kSlotMatchedSize));
    for (int team = 1; team < 4; ++team)
    {
        EXPECT_EQ(1, alive_on_team(fx.world(), team))
            << "FAIR fields a matched-headcount squad on team " << team;
        EXPECT_NE(0, matched_plan_code(fx.var(kSlotMatchedPlan), team))
            << "team " << team << " was solved";
    }
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_EQ(pos_pack(256, 16), fx.team_var(kSocGoalPos, 0));
    EXPECT_EQ(pos_pack(592, 256), fx.team_var(kSocGoalPos, 1));
    EXPECT_EQ(pos_pack(256, 912), fx.team_var(kSocGoalPos, 2));
    EXPECT_EQ(pos_pack(16, 256), fx.team_var(kSocGoalPos, 3));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, fair_teams_auto_with_a_solo_roster_fields_four_teams)
{
    // The Auto twin of the explicit TEAMS: 4 test above: TEAMS: Auto is
    // the zero sentinel ("as many teams as the map actually has"), so on
    // the FOURSQUARE pitch a solo FAIR roster fields all four authored
    // sides through the SAME backfill arm as the explicit count — and all
    // four goal mouths bank (2026-08-18 maintainer directive, issue #218).
    BudgetOverride budget(500000);
    SoccerPitch fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
    arm_matched(fx.world());
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: backfills
    fx.world().ctf_requested_fill[2] = og::sim::kFillFair;
    fx.world().ctf_requested_fill[3] = og::sim::kFillFair;
    ASSERT_EQ(0, fx.world().ctf_requested_team_count) << "TEAMS: Auto";
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
    ASSERT_NE(nullptr, hero);
    fx.tick(1);

    ASSERT_TRUE(fx.soccer_active());
    EXPECT_EQ(15, fx.var(kSocTeamMask))
        << "Auto resolves to the authored team count: all four sides";
    EXPECT_EQ(4, fx.var(kSocTeamCount));
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the roster is untouched";
    EXPECT_EQ(1, fx.var(kSlotMatchedSize));
    for (int team = 1; team < 4; ++team)
    {
        EXPECT_EQ(1, alive_on_team(fx.world(), team))
            << "FAIR fields a matched-headcount squad on team " << team;
        EXPECT_NE(0, matched_plan_code(fx.var(kSlotMatchedPlan), team))
            << "team " << team << " was solved";
    }
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_EQ(pos_pack(256, 16), fx.team_var(kSocGoalPos, 0));
    EXPECT_EQ(pos_pack(592, 256), fx.team_var(kSocGoalPos, 1));
    EXPECT_EQ(pos_pack(256, 912), fx.team_var(kSocGoalPos, 2));
    EXPECT_EQ(pos_pack(16, 256), fx.team_var(kSocGoalPos, 3));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, wheel_values_never_move_the_mask)
{
    // The D26/D33 mask-invariance restated for the wheel (B2/B8): WEAK
    // and BRUTAL stage the identical mask and fill sites — the multiplier
    // moves only the solved LEVELS. Sequentially scoped worlds — script
    // bindings resolve against the last-constructed live world (the
    // two-live-fixtures trap).
    std::int32_t weak_mask = 0;
    std::vector<int> weak_levels;
    std::int32_t brutal_mask = 0;
    std::vector<int> brutal_levels;
    {
        SoccerPitch weak(kSoccerLevelB);
        for (int team = 0; team < 4; ++team)
            weak.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
        for (auto& knob : weak.world().ctf_requested_fill)
            knob = og::sim::kFillWeak;
        weak.spawn_leveled_hero(FAMILY_SOLDIER, 0, 300, 100, 1, 5);
        weak.tick(1);
        ASSERT_TRUE(weak.soccer_active());
        weak_mask = weak.var(kSocTeamMask);
        EXPECT_EQ(15, weak_mask);
        for (int team = 1; team < 4; ++team)
        {
            EXPECT_EQ(1, alive_on_team(weak.world(), team))
                << "headcount-matched squad on team " << team;
            weak_levels.push_back(
                matched_plan_code(weak.var(kSlotMatchedPlan), team) / 10);
        }
    }
    {
        SoccerPitch brutal(kSoccerLevelB);
        for (int team = 0; team < 4; ++team)
            brutal.spawn_anchor(team, static_cast<short>(96 + 64 * team),
                                700);
        for (auto& knob : brutal.world().ctf_requested_fill)
            knob = og::sim::kFillBrutal;
        brutal.spawn_leveled_hero(FAMILY_SOLDIER, 0, 300, 100, 1, 5);
        brutal.tick(1);
        ASSERT_TRUE(brutal.soccer_active());
        brutal_mask = brutal.var(kSocTeamMask);
        for (int team = 1; team < 4; ++team)
        {
            EXPECT_EQ(1, alive_on_team(brutal.world(), team));
            brutal_levels.push_back(
                matched_plan_code(brutal.var(kSlotMatchedPlan), team) / 10);
        }
    }
    EXPECT_EQ(weak_mask, brutal_mask) << "the wheel never moves the mask";
    for (std::size_t i = 0; i < weak_levels.size(); ++i)
    {
        EXPECT_LT(weak_levels[i], brutal_levels[i])
            << "the multiplier moves the solved level, team " << (i + 1);
    }
}

TEST_F(ModesSoccer, bots_off_never_strips_a_roster_team)
{
    // (a) OFF on the two empty sides of rosters {0, 2} fields exactly
    // {0, 2}: the sides a player chose, never an index-order clamp.
    {
        SoccerPitch fx(kSoccerLevelB);
        for (int team = 0; team < 4; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
        fx.world().ctf_requested_fill[1] = og::sim::kFillNone;
        fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
        fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
        fx.spawn_hero(FAMILY_BARBARIAN, 2, 300, 860, 2);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        EXPECT_EQ(1 + 4, fx.var(kSocTeamMask))
            << "the roster sides stay, the OFF sides leave";
        EXPECT_EQ(2, fx.var(kSocTeamCount));
    }
    // (b) OFF beside a roster is ignored — a deployed side is never
    // stripped to satisfy a knob (lineup A2: the seat keeps it on); OFF
    // on the empty fourth side drops it.
    {
        SoccerPitch fx(kSoccerLevelB);
        for (int team = 0; team < 4; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
        fx.world().ctf_requested_fill[0] = og::sim::kFillNone;
        fx.world().ctf_requested_fill[1] = og::sim::kFillNone;
        fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
        fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
        fx.spawn_hero(FAMILY_BARBARIAN, 1, 300, 700, 2);
        fx.spawn_hero(FAMILY_ELF, 2, 300, 860, 3);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        EXPECT_EQ(1 + 2 + 4, fx.var(kSocTeamMask));
        EXPECT_EQ(3, fx.var(kSocTeamCount));
        EXPECT_EQ(0, alive_on_team(fx.world(), 3));
        EXPECT_EQ(0, fx.team_var(kSocGoalPos, 3))
            << "the left-out fourth mouth stays dead";
    }
}

TEST_F(ModesSoccer, bots_off_on_the_fourth_side_leaves_three)
{
    // Rosters {0, 2} with the fourth side OFF: both roster teams stay,
    // team 1 (authored, nothing on it, not OFF) plays with OWN's legacy
    // squad — AUTO is the map's own value, every authored side — and
    // team 3 stays dead and banks no mouth.
    SoccerPitch fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 700);
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: backfill
    fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 100, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 2, 300, 860, 2);
    fx.tick(1);

    ASSERT_TRUE(fx.soccer_active());
    EXPECT_EQ(1 + 2 + 4, fx.var(kSocTeamMask))
        << "rosters {0, 2} plus the untouched authored side, 1";
    EXPECT_EQ(3, fx.var(kSocTeamCount));
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the backfilled team gets a squad at the roster headcount (B2)";
    EXPECT_EQ(0, alive_on_team(fx.world(), 3));
    EXPECT_EQ(pos_pack(592, 256), fx.team_var(kSocGoalPos, 1))
        << "a backfilled team banks its goal mouth";
    EXPECT_EQ(0, fx.team_var(kSocGoalPos, 3))
        << "the left-out mouth stays dead";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, ball_in_a_closed_authored_mouth_announces_and_resets)
{
    // Issue #219, the reporter's shape via TROOPS: ALL — FOURSQUARE at
    // TEAMS: 2 leaves the south/west mouths authored but dead, and mapgen
    // paints every mouth identically, so a dead one looks live. A ball
    // entering one must announce the closed goal and re-spot at the
    // kickoff instead of sitting there silently. (Since the 2026-08-18
    // Auto-resolves-to-authored-count directive a dead mouth is reachable
    // only by switching a side OFF on the LINEUP band — lineup A1/A2, the
    // retired TEAMS count's successor — which is the shape this test
    // constructs; AUTO activates every mouth.)
    SoccerPitch fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
    {
        fx.spawn_anchor(team, static_cast<short>(96 + 64 * team), 448);
        fx.spawn_living(FAMILY_SOLDIER, team,
                        static_cast<short>(96 + 64 * team), 96);
    }
    fx.world().ctf_requested_fill[2] = og::sim::kFillNone;
    fx.world().ctf_requested_map_units[2] = og::sim::kMapUnitsOff;
    fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
    fx.world().ctf_requested_map_units[3] = og::sim::kMapUnitsOff;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_EQ(3, fx.var(kSocTeamMask))
        << "mouths 2 and 3 are authored but dead";

    fx.thaw_kickoff();
    fx.world().mode.vars[kSocLastTouch1] = 1;  // attribution must not score
    fx.set_ball(320, 928, 0, 0);  // center of team 2's south rect
    fx.tick(1);

    EXPECT_EQ(1, count_notifications(fx.events, "BLUE GOAL CLOSED!"))
        << "team 2 = BLUE announces the dead mouth";
    EXPECT_LE(longest_notification(fx.events), 25u);
    for (int team = 0; team < 4; ++team)
        EXPECT_EQ(0, fx.team_var(kSocGoals, team)) << "no goal scored";
    int score_changes = 0;
    for (const auto& ev : fx.events.events())
    {
        if (ev.kind == og::sim::EventKind::ScoreChange)
            score_changes++;
    }
    EXPECT_EQ(0, score_changes);
    EXPECT_EQ(320, fx.ball_cx()) << "re-spotted at the kickoff";
    EXPECT_EQ(480, fx.ball_cy());
    EXPECT_EQ(0, fx.var(kSocLastTouch1))
        << "the reset wiped the touch history";
    EXPECT_GT(fx.var(kSocKickoffUntil), 0) << "the reset re-armed the freeze";

    // Rate limit: the kickoff freeze gates the scan, so a ball parked back
    // in the dead mouth while frozen announces nothing and stays put...
    fx.set_ball(320, 928, 0, 0);
    fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "GOAL CLOSED!"))
        << "the freeze window is the rate limit";
    EXPECT_EQ(928, fx.ball_cy()) << "no silent re-spot while frozen";

    // ...and announces exactly once more after the freeze expires.
    fx.thaw_kickoff();
    fx.tick(1);
    EXPECT_EQ(2, count_notifications(fx.events, "GOAL CLOSED!"));
    EXPECT_EQ(320, fx.ball_cx());
    EXPECT_EQ(480, fx.ball_cy());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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

TEST_F(ModesSoccer, water_drag_uses_the_footprint_and_swept_contact)
{
    // Dry control: the shared surface damping's keep=256 arm is exactly the
    // old linear friction, so an 8 px/tick roll keeps 2048 - 64 = 1984 fp.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(200, 200, 8 * 256, 0);
        fx.tick(1);
        EXPECT_EQ(208, fx.ball_cx());
        EXPECT_EQ(8 * 256 - kFriction, fx.var(kSocBallVx));
        EXPECT_EQ(0, fx.var(kSocBallVy));
    }

    // The center remains on dry tile 12, but the final 12x12 footprint
    // reaches one pixel into water tile 13. Wet keep=64 then loss=64:
    // div(2048*64,256)-64 = 448 fp. Center-only sampling would miss it.
    {
        SoccerWorld fx;
        paint_water(fx.world(), 13, 12, 13, 12);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(196, 200, 8 * 256, 0);
        fx.tick(1);
        EXPECT_EQ(204, fx.ball_cx());
        EXPECT_EQ(448, fx.var(kSocBallVx));
        EXPECT_EQ(0, fx.var(kSocBallVy));
    }

    // A synthetic 42 px/tick sweep starts dry and ends with its footprint
    // dry, crossing the one-tile water strip only between accepted
    // substeps. Contact must stay accumulated for the tick:
    // div(10752*64,256)-64 = 2624 fp, not dry friction's 10688.
    {
        SoccerWorld fx;
        paint_water(fx.world(), 12, 12, 12, 12);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(173, 200, 42 * 256, 0);
        fx.tick(1);
        EXPECT_EQ(215, fx.ball_cx());
        EXPECT_EQ(2624, fx.var(kSocBallVx));
        EXPECT_EQ(0, fx.var(kSocBallVy));
    }
}

TEST_F(ModesSoccer, projectile_dislodges_a_waterlogged_ball)
{
    SoccerWorld fx;
    paint_water(fx.world(), 18, 27, 22, 31);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);

    walker* shot = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, shot);
    shot->setxy(317, 461);  // 6x6 center exactly on the ball center
    shot->set_owner(fx.green);
    shot->set_lastx(4.0f);
    shot->set_lasty(0.0f);
    shot->set_damage(1.0f);  // clamp(trunc(1)*2, 4, 12) = 4 px/tick
    const std::uint32_t shot_id = shot->entity_id();
    fx.tick(1);

    EXPECT_EQ(324, fx.ball_cx()) << "the projectile moves the bogged ball";
    EXPECT_EQ(192, fx.var(kSocBallVx))
        << "div(1024*64,256)-64: a short wet coast after the hit";
    EXPECT_EQ(0, fx.var(kSocBallVy));
    EXPECT_EQ(static_cast<std::int32_t>(fx.green->entity_id()),
              fx.var(kSocLastKicker));
    EXPECT_EQ(2, fx.var(kSocLastTouch1));
    EXPECT_NE(nullptr, fx.world().find_by_id(shot_id))
        << "ball contact never consumes the projectile";
}

TEST_F(ModesSoccer, fire_check_targets_only_live_hostile_ignored_foes)
{
    // Eligible objective: the ignored neutral FX ball is absent from obmap,
    // but its explicit hostile bounding box satisfies the otherwise ordinary
    // range/facing/grid-checked ray.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        ASSERT_NE(nullptr, fx.ball());
        ASSERT_TRUE(fx.ball()->ignore());
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_TRUE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::None, denial);
    }

    // Friendly ignored objectives remain transparent, exactly like the
    // obmap's friendly-weapon skip.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        fx.ball()->set_team_num(0);
        ASSERT_TRUE(shooter->is_friendly(fx.ball()));
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::RayMiss, denial);
    }

    // Dead ignored entities are no longer valid objectives.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        fx.ball()->set_dead(1);
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::RayMiss, denial);
    }

    // Dormant objectives are gameplay-inactive even though they are live and
    // ignored; naming one as foe cannot make it hittable.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        fx.ball()->set_dormant(true);
        ASSERT_FALSE(fx.ball()->dead());
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::RayMiss, denial);
    }

    // An ignored objective on another floor is outside this projectile ray.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        fx.ball()->set_floor(1);
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::RayMiss, denial);
    }

    // Terrain remains authoritative and is checked before ignored-foe
    // contact. This wall column covers every possible knife waver.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        for (int gy = 27; gy <= 31; ++gy)
        {
            fx.world().grid.data[static_cast<std::size_t>(
                18 + fx.world().grid.w * gy)] = PIX_H_WALL1;
        }
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::WallBlocked, denial);
    }

    // True family reach is still the first targeting gate.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx, 96);
        ASSERT_NE(nullptr, shooter);
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::OutOfRange, denial);
    }

    // A close shooter still has to face the requested attack direction.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        shooter->face_delta(-1, 0);
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_FALSE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::Facing, denial);
    }

    // Ordinary non-ignored enemies still satisfy the generic obmap arm.
    {
        SoccerWorld fx;
        walker* shooter = prepare_ignored_ball_ray(fx);
        ASSERT_NE(nullptr, shooter);
        fx.green->setxy(314, 456);
        shooter->set_foe(fx.green);
        shooter->face_delta(1, 0);
        ASSERT_FALSE(fx.green->ignore());
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        EXPECT_TRUE(shooter->fire_check(1, 0, &denial));
        EXPECT_EQ(walker::FireCheckDenial::None, denial);
    }
}

TEST_F(ModesSoccer, recovery_range_matches_every_ranged_core_class_profile)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    int ranged = 0;
    int exact_profiles = 0;
    int conservative_profiles = 0;
    for (int family = 0; family < NUM_FAMILIES; ++family)
    {
        const FamilyDescriptor* descriptor = get_family_descriptor(family);
        ASSERT_NE(nullptr, descriptor) << family;
        if ((descriptor->init_bit_flags & BIT_NO_RANGED) != 0)
            continue;
        ranged++;

        walker* shooter =
            fx.spawn_living(family, 0, 96, static_cast<short>(96 + family),
                            ACT_SIT);
        ASSERT_NE(nullptr, shooter) << family;
        shooter->face_delta(1, 0);
        const std::uint32_t rng_before = fx.world().rng_.state_;
        const std::size_t weapons_before = fx.world().weaplist.size();

        const std::int32_t prospective =
            shooter->prospective_weapon_reach(1, 0);

        EXPECT_EQ(rng_before, fx.world().rng_.state_) << family;
        EXPECT_EQ(weapons_before, fx.world().weaplist.size()) << family;
        walker* actual = shooter->create_weapon();
        ASSERT_NE(nullptr, actual) << family;
        const std::int32_t actual_reach =
            static_cast<std::int32_t>(actual->stepsize()) *
            actual->lineofsight();
        const og::script::WorldScripts& scripts =
            og::script::active_world_scripts();
        const bool requires_hook =
            descriptor->customize_weapon != nullptr ||
            descriptor->on_fire_weapon != nullptr ||
            scripts.has_hook(Order::Living, family,
                             og::script::FamilyHook::CustomizeWeapon) ||
            scripts.has_hook(Order::Living, family,
                             og::script::FamilyHook::OnFireWeapon);
        if (requires_hook)
        {
            EXPECT_EQ(0, prospective) << family;
            conservative_profiles++;
        }
        else
        {
            EXPECT_EQ(actual_reach, prospective) << family;
            exact_profiles++;
        }
        actual->set_dead(1);
    }
    EXPECT_EQ(NUM_FAMILIES - 3, ranged)
        << "ghost, small slime, and orc are the three NO_RANGED classes";
    EXPECT_EQ(NUM_FAMILIES - 6, exact_profiles)
        << "every ordinary ranged core class uses the exact live profile";
    EXPECT_EQ(3, conservative_profiles)
        << "soldier/archmage on-fire and cleric customization hooks are "
           "never dry-run";
}

TEST_F(ModesSoccer,
       recovery_range_distinguishes_invalid_from_valid_zero_profiles)
{
    SoccerWorld fx;
    walker* shooter =
        fx.spawn_living(FAMILY_ARCHER, 0, 96, 96, ACT_SIT);
    ASSERT_NE(nullptr, shooter);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_NE(nullptr, fx.ball());
    fx.ball()->setxy(shooter->xpos(), shooter->ypos());

    constexpr int kUnpopulatedWeapon = 120;
    ASSERT_EQ(nullptr, get_weapon_family_descriptor(kUnpopulatedWeapon));
    shooter->set_current_weapon(kUnpopulatedWeapon);
    EXPECT_EQ(0, shooter->prospective_weapon_reach(0, 0));
    EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()))
        << "a missing loader profile is ineligible even at exact overlap";

    // A real mod profile may intentionally have zero reach. Returning zero
    // remains numeric API behavior; validity, not reach > 0, admits overlap.
    const auto original_configurator = fx.world().entity_configurator;
    fx.world().entity_configurator =
        [original_configurator](walker& entity, Order order,
                                std::int32_t family) {
            const PixieData* data =
                original_configurator(entity, order, family);
            if (data != nullptr && order == Order::Weapon &&
                family == FAMILY_ARROW)
            {
                entity.set_stepsize(0.0f);
                entity.set_lineofsight(7);
            }
            return data;
        };
    shooter->set_current_weapon(FAMILY_ARROW);
    EXPECT_EQ(0, shooter->prospective_weapon_reach(0, 0));
    EXPECT_TRUE(shooter->can_approach_weapon_range(fx.ball()));
}

TEST_F(ModesSoccer, recovery_range_probe_never_touches_the_world_obmap)
{
    SoccerWorld fx;
    walker* shooter =
        fx.spawn_living(FAMILY_ARCHER, 0, 96, 96, ACT_SIT);
    ASSERT_NE(nullptr, shooter);
    shooter->set_current_weapon(FAMILY_ARROW);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    const std::pair<short, short> probe_cell = {
        obmap::hash(0), obmap::hash(0)};
    auto [cell, inserted] =
        fx.world().myobmap->pos_to_walker.try_emplace(probe_cell);
    ASSERT_TRUE(inserted || cell->second.empty());
    ASSERT_TRUE(cell->second.empty());
    const std::size_t cells_before =
        fx.world().myobmap->pos_to_walker.size();
    const std::size_t walkers_before =
        fx.world().myobmap->walker_to_pos.size();

    EXPECT_EQ(154, shooter->prospective_weapon_reach(1, 0));

    const auto after =
        fx.world().myobmap->pos_to_walker.find(probe_cell);
    ASSERT_NE(fx.world().myobmap->pos_to_walker.end(), after)
        << "destroying a detached probe must not erase an empty world cell";
    EXPECT_TRUE(after->second.empty());
    EXPECT_EQ(cells_before, fx.world().myobmap->pos_to_walker.size());
    EXPECT_EQ(walkers_before, fx.world().myobmap->walker_to_pos.size());
}

TEST_F(ModesSoccer,
       recovery_range_conservatively_rejects_weapon_customization)
{
    SoccerWorld fx;
    walker* shooter =
        fx.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
    ASSERT_NE(nullptr, shooter);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    shooter->set_current_weapon(FAMILY_KNIFE);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    paint_water(fx.world(), 19, 27, 29, 31);
    fx.set_ball(400, 464, 0, 0);

    const std::uint32_t rng_before = fx.world().rng_.state_;
    const std::size_t weapons_before = fx.world().weaplist.size();
    {
        g_weapon_customizer_calls = 0;
        ScopedWeaponCustomizer customizer(FAMILY_ARCHER,
                                           counting_one_pixel_weapon_profile);
        ASSERT_NE(nullptr, customizer.descriptor);
        EXPECT_EQ(0, shooter->prospective_weapon_reach(1, 0));
        EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()))
            << "an arbitrary customizer is ineligible without dispatch";
        EXPECT_EQ(0, g_weapon_customizer_calls);
        EXPECT_EQ(rng_before, fx.world().rng_.state_);
        EXPECT_EQ(weapons_before, fx.world().weaplist.size())
            << "the rejected profile never spawns a scratch projectile";

        walker* actual = shooter->create_weapon();
        ASSERT_NE(nullptr, actual);
        EXPECT_EQ(1, g_weapon_customizer_calls)
            << "a real shot still dispatches its family customizer once";
        EXPECT_EQ(1, static_cast<std::int32_t>(actual->stepsize()) *
                         actual->lineofsight());
        actual->set_dead(1);
    }
    EXPECT_EQ(rng_before, fx.world().rng_.state_);

    EXPECT_EQ(56, shooter->prospective_weapon_reach(1, 0));
    EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()))
        << "the ordinary knife still cannot span this deep water";
    shooter->set_current_weapon(FAMILY_ARROW);
    EXPECT_EQ(154, shooter->prospective_weapon_reach(1, 0));
    EXPECT_TRUE(shooter->can_approach_weapon_range(fx.ball()))
        << "the same class automatically follows its live weapon profile";
}

TEST_F(ModesSoccer, recovery_range_rejects_on_fire_hooks_without_dispatch)
{
    // Native descriptor path: metadata rejects it without invoking the
    // callback, while a real launch still reaches and obeys that callback.
    {
        SoccerWorld fx;
        walker* shooter =
            fx.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, shooter);
        shooter->set_current_weapon(FAMILY_ARROW);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        paint_water(fx.world(), 19, 27, 29, 31);
        fx.set_ball(400, 464, 0, 0);

        g_weapon_on_fire_calls = 0;
        const std::uint32_t rng_before = fx.world().rng_.state_;
        const std::size_t weapons_before = fx.world().weaplist.size();
        {
            ScopedWeaponFireHook hook(FAMILY_ARCHER,
                                      counting_reject_weapon_launch);
            ASSERT_NE(nullptr, hook.descriptor);
            EXPECT_EQ(0, shooter->prospective_weapon_reach(1, 0));
            EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()));
            fx.ball()->setxy(shooter->xpos(), shooter->ypos());
            EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()))
                << "hooked profiles stay ineligible at exact overlap";
            EXPECT_EQ(0, g_weapon_on_fire_calls);
            EXPECT_EQ(rng_before, fx.world().rng_.state_);
            EXPECT_EQ(weapons_before, fx.world().weaplist.size());

            shooter->set_lastx(1.0f);
            shooter->set_lasty(0.0f);
            EXPECT_EQ(nullptr, shooter->fire());
            EXPECT_EQ(1, g_weapon_on_fire_calls)
                << "only the real launch dispatches on_fire_weapon";
        }
    }

    // Active per-world Lua path: soldier's returning-knife hook would spend
    // weapons_left if dispatched. The eligibility query only reads its mask.
    {
        SoccerWorld fx;
        walker* shooter =
            fx.spawn_living(FAMILY_SOLDIER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, shooter);
        shooter->set_current_weapon(FAMILY_KNIFE);
        shooter->set_weapons_left(1);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        paint_water(fx.world(), 19, 27, 29, 31);
        fx.set_ball(400, 464, 0, 0);
        ASSERT_TRUE(og::script::active_world_scripts().has_hook(
            Order::Living, FAMILY_SOLDIER,
            og::script::FamilyHook::OnFireWeapon));
        const std::uint32_t rng_before = fx.world().rng_.state_;
        const std::size_t weapons_before = fx.world().weaplist.size();
        const float magic_before = shooter->stats()->magicpoints();

        EXPECT_EQ(0, shooter->prospective_weapon_reach(1, 0));
        EXPECT_FALSE(shooter->can_approach_weapon_range(fx.ball()));
        EXPECT_EQ(1, shooter->weapons_left());
        EXPECT_EQ(magic_before, shooter->stats()->magicpoints());
        EXPECT_EQ(rng_before, fx.world().rng_.state_);
        EXPECT_EQ(weapons_before, fx.world().weaplist.size());
    }
}

TEST_F(ModesSoccer, director_fires_to_recover_a_waterlogged_ball)
{
    {
        SoccerWorld fx;
        paint_water(fx.world(), 19, 27, 22, 31);
        walker* shooter =
            fx.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, shooter);
        fx.red->set_act_type(ACT_SIT);
        fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        shooter->stats()->set_weapon_cost(0);
        shooter->set_current_weapon(FAMILY_ARROW);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(320, 464, 0, 0);
        fx.red->setxy(40, 456); // keep the melee-only bot in goal

        align_before_cadence(fx.world());
        fx.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(fx.world(), shooter));
        EXPECT_EQ(SCORE_TEAM_COUNT, fx.ball()->team_num());
        EXPECT_EQ(COMMAND_ATTACK, front_type(shooter));
        EXPECT_EQ(fx.ball(), shooter->foe());
        EXPECT_EQ(nullptr, shooter->leader());

        bool fired = false;
        bool cooldown_seen = false;
        for (int tick = 0; tick < 36 && fx.var(kSocLastKicker) == 0; ++tick)
        {
            fx.tick(1);
            fired = fired || live_weapons_owned_by(fx.world(), shooter) > 0;
            cooldown_seen = cooldown_seen || shooter->busy() > 0.0f;
        }
        EXPECT_TRUE(fired)
            << "normal COMMAND_ATTACK must launch a live family projectile";
        EXPECT_TRUE(cooldown_seen)
            << "recovery fire retains the family's ordinary busy cooldown";
        EXPECT_EQ(static_cast<std::int32_t>(shooter->entity_id()),
                  fx.var(kSocLastKicker));
        EXPECT_EQ(486, fx.var(kSocBallVx));
        EXPECT_EQ(88, fx.var(kSocBallVy))
            << "the live arrow's deterministic waver is preserved while "
               "one wet damping step removes most of its speed";
    }

    // Dry control: the same capable bot retains the normal striker GOTO and
    // does not waste a weapon on an ordinary loose ball.
    {
        SoccerWorld dry;
        walker* dry_shooter =
            dry.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, dry_shooter);
        dry.red->set_act_type(ACT_SIT);
        dry.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        dry_shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        dry_shooter->stats()->set_weapon_cost(0);
        dry_shooter->set_current_weapon(FAMILY_ARROW);
        dry.tick(1);
        ASSERT_TRUE(dry.soccer_active());
        dry.thaw_kickoff();
        dry.set_ball(320, 464, 0, 0);
        dry.red->setxy(40, 456);
        align_before_cadence(dry.world());
        dry.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(dry.world(), dry_shooter));
        EXPECT_TRUE(front_command_is(dry_shooter, COMMAND_GOTO, 320, 456));
    }

    // Capability control: a wet ball does not make a BIT_NO_RANGED bot
    // bypass its family restriction.
    {
        SoccerWorld barred;
        paint_water(barred.world(), 19, 27, 22, 31);
        walker* barred_shooter =
            barred.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, barred_shooter);
        barred.red->set_act_type(ACT_SIT);
        barred.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        barred_shooter->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        barred.tick(1);
        ASSERT_TRUE(barred.soccer_active());
        barred.thaw_kickoff();
        barred.set_ball(320, 464, 0, 0);
        barred.red->setxy(40, 456);
        align_before_cadence(barred.world());
        barred.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(barred.world(), barred_shooter));
        EXPECT_TRUE(front_command_is(barred_shooter, COMMAND_GOTO, 320, 456));
    }

    // Mana control: selection cannot appoint a bot that cannot currently pay
    // its ordinary family weapon cost.
    {
        SoccerWorld drained;
        paint_water(drained.world(), 19, 27, 22, 31);
        walker* drained_shooter =
            drained.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, drained_shooter);
        drained.red->set_act_type(ACT_SIT);
        drained.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        drained.green->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        drained_shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        drained_shooter->stats()->set_weapon_cost(10);
        drained_shooter->stats()->set_magicpoints(0.0f);
        drained_shooter->set_current_weapon(FAMILY_ARROW);
        drained.tick(1);
        ASSERT_TRUE(drained.soccer_active());
        drained.thaw_kickoff();
        drained.set_ball(320, 464, 0, 0);
        drained.red->setxy(40, 456);
        align_before_cadence(drained.world());
        drained.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(drained.world(), drained_shooter));
        EXPECT_TRUE(front_command_is(drained_shooter, COMMAND_GOTO, 320, 456));
    }

    // Friendliness control: the ball's current team keeps every ranged bot
    // in its normal pitch role; only a hostile team may appoint a shooter.
    {
        SoccerWorld friendly;
        paint_water(friendly.world(), 19, 27, 22, 31);
        walker* friendly_shooter =
            friendly.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, friendly_shooter);
        friendly.red->set_act_type(ACT_SIT);
        friendly.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        friendly.green->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        friendly_shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        friendly_shooter->stats()->set_weapon_cost(0);
        friendly_shooter->set_current_weapon(FAMILY_ARROW);
        friendly.tick(1);
        ASSERT_TRUE(friendly.soccer_active());
        friendly.thaw_kickoff();
        friendly.set_ball(320, 464, 0, 0);
        friendly.ball()->set_team_num(0);
        ASSERT_TRUE(friendly_shooter->is_friendly(friendly.ball()));
        friendly.red->setxy(40, 456);
        align_before_cadence(friendly.world());
        friendly.tick(1);
        EXPECT_EQ(0, live_weapons_owned_by(friendly.world(), friendly_shooter));
        EXPECT_TRUE(front_command_is(friendly_shooter, COMMAND_GOTO, 320, 456));
    }
}

TEST_F(ModesSoccer, director_skips_a_short_weapon_that_cannot_span_water)
{
    SoccerWorld fx;
    paint_water(fx.world(), 19, 0, 29, fx.world().grid.h - 1);
    walker* knife =
        fx.spawn_living(FAMILY_THIEF, 0, 288, 444, ACT_SIT);
    walker* archer =
        fx.spawn_living(FAMILY_ARCHER, 0, 272, 456, ACT_SIT);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, archer);
    fx.red->set_act_type(ACT_SIT);
    fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    fx.green->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    knife->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    knife->stats()->set_weapon_cost(0);
    knife->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    archer->stats()->set_weapon_cost(0);
    archer->set_current_weapon(FAMILY_ARROW);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(352, 464, 0, 0);
    fx.red->setxy(40, 456);

    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_NE(COMMAND_ATTACK, front_type(knife));
    EXPECT_EQ(nullptr, knife->foe());
    EXPECT_EQ(COMMAND_ATTACK, front_type(archer));
    EXPECT_EQ(fx.ball(), archer->foe());
    EXPECT_EQ(0, live_weapons_owned_by(fx.world(), knife));
    EXPECT_EQ(0, live_weapons_owned_by(fx.world(), archer));

    bool archer_fired = false;
    bool archer_cooldown_seen = false;
    int knife_attack_ticks = 0;
    int knife_weapon_ticks = 0;
    for (int tick = 0; tick < 180; ++tick)
    {
        fx.tick(1);
        if (front_type(knife) == COMMAND_ATTACK)
            ++knife_attack_ticks;
        knife_weapon_ticks += live_weapons_owned_by(fx.world(), knife);
        archer_fired = archer_fired ||
                       live_weapons_owned_by(fx.world(), archer) > 0;
        archer_cooldown_seen =
            archer_cooldown_seen || archer->busy() > 0.0f;
    }

    EXPECT_EQ(0, knife_attack_ticks)
        << "the nearer knife wielder is never assigned an unreachable shot";
    EXPECT_EQ(0, knife_weapon_ticks)
        << "the rejected knife profile never creates a projectile";
    EXPECT_TRUE(archer_fired)
        << "the reachable archer executes ordinary recovery fire";
    EXPECT_TRUE(archer_cooldown_seen)
        << "the archer's ordinary family cooldown remains in force";
    EXPECT_EQ(0, fx.var(kSocLastKicker))
        << "the full-height control isolates selection from arrow waver";
    EXPECT_EQ(0, fx.var(kSocLastTouch1));
    EXPECT_EQ(0, fx.var(kSocBallVx));
    EXPECT_EQ(0, fx.var(kSocBallVy));
}

TEST_F(ModesSoccer, only_ranged_goalie_candidate_takes_recovery_role)
{
    // Wet edge: select the sole capable shooter before reserving the keeper,
    // then deterministically give the remaining melee bot the goal mouth.
    {
        SoccerWorld fx;
        fx.red->transform_to(Order::Living, FAMILY_ARCHER);
        paint_water(fx.world(), 19, 27, 22, 31);
        walker* melee =
            fx.spawn_living(FAMILY_SOLDIER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, melee);
        fx.red->set_act_type(ACT_SIT);
        fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        fx.red->stats()->set_weapon_cost(0);
        fx.red->set_current_weapon(FAMILY_KNIFE);
        melee->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        fx.green->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(320, 464, 0, 0);
        fx.red->setxy(40, 456);

        align_before_cadence(fx.world());
        fx.tick(1);

        EXPECT_EQ(COMMAND_ATTACK, front_type(fx.red));
        EXPECT_EQ(fx.ball(), fx.red->foe());
        EXPECT_EQ(nullptr, fx.red->leader());
        EXPECT_TRUE(front_command_is(melee, COMMAND_GOTO, 64, 464))
            << "the melee striker inherits the vacated goalie role";
        EXPECT_EQ(nullptr, melee->foe());
        EXPECT_EQ(fx.ball(), melee->leader());
    }

    // Dry control: role order is unchanged — the same near-goal ranged bot
    // remains keeper, while the melee member keeps its normal ball drive.
    {
        SoccerWorld fx;
        fx.red->transform_to(Order::Living, FAMILY_ARCHER);
        walker* melee =
            fx.spawn_living(FAMILY_SOLDIER, 0, 272, 456, ACT_SIT);
        ASSERT_NE(nullptr, melee);
        fx.red->set_act_type(ACT_SIT);
        fx.red->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        fx.red->stats()->set_weapon_cost(0);
        fx.red->set_current_weapon(FAMILY_KNIFE);
        melee->stats()->set_bit_flags(BIT_NO_RANGED, 1);
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.set_ball(320, 464, 0, 0);
        fx.red->setxy(40, 456);

        align_before_cadence(fx.world());
        fx.tick(1);

        EXPECT_TRUE(front_command_is(fx.red, COMMAND_GOTO, 64, 464));
        EXPECT_EQ(fx.ball(), fx.red->leader());
        ASSERT_FALSE(melee->stats()->commands.empty());
        const command& dry_role = melee->stats()->commands.front();
        EXPECT_EQ(COMMAND_GOTO, dry_role.commandtype);
        EXPECT_EQ(320, dry_role.com1);
        EXPECT_EQ(456, dry_role.com2);
        EXPECT_EQ(fx.ball(), melee->leader());
    }
}

TEST_F(ModesSoccer, wall_bounce_reflects_the_blocked_axis)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // Wall column at tile x=21 (px 336..351), rows around y=464.
    for (int gy = 27; gy <= 31; ++gy)
        fx.world().grid.data[static_cast<std::size_t>(21 + fx.world().grid.w * gy)] = PIX_H_WALL1;
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
// Rolling spin (sprites/ball.png is an 8-frame turn of the patch lattice)
// ===========================================================================

// Playing the strip forward turns the ball clockwise, so the drawn frame has
// to track the direction of travel. Everything here reads walker::frame(),
// which is what the renderer blits AND what EntitySnapshot replicates.
//
// Each case re-arms the same velocity every tick so friction cannot compound
// and the arithmetic stays checkable by hand: a full kick (8 px/tick = 2048
// fp) survives one friction step at 1984 fp, and run_spin advances the phase
// by 1984 / spin_divisor(8) = 248 units — just under the 256 units a frame —
// so the ball steps exactly one frame per tick around the strip.
inline constexpr int kSpinCycle = 2048;   // T.spin_cycle
inline constexpr int kSpinStep = 256;     // T.spin_step
inline constexpr int kFullKickAdvance = 248;

TEST_F(ModesSoccer, spin_steps_one_frame_per_tick_rolling_right)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    EXPECT_EQ(0, fx.ball()->frame()) << "a spotted ball starts on frame 0";

    // Nine ticks: one full turn (0..7) and the wrap back to 0.
    const int expect_phase[9] = {248, 496, 744, 992, 1240,
                                 1488, 1736, 1984, 184};
    const int expect_frame[9] = {0, 1, 2, 3, 4, 5, 6, 7, 0};
    for (int i = 0; i < 9; ++i)
    {
        fx.set_ball(320, 464, 8 * 256, 0);
        fx.tick(1);
        EXPECT_EQ(expect_phase[i], fx.var(kSocBallSpin)) << "tick " << i;
        EXPECT_EQ(expect_frame[i], static_cast<int>(fx.ball()->frame()))
            << "tick " << i;
    }
}

TEST_F(ModesSoccer, spin_runs_the_strip_backward_rolling_left)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    // Mirror of the rightward case: the phase walks down from 0 and wraps
    // through the top of the strip (og.mod is the C remainder, so run_spin
    // has to fold the negative result back into 0..2047).
    const int expect_phase[9] = {1800, 1552, 1304, 1056, 808,
                                 560, 312, 64, 1864};
    const int expect_frame[9] = {7, 6, 5, 4, 3, 2, 1, 0, 7};
    for (int i = 0; i < 9; ++i)
    {
        fx.set_ball(320, 464, -8 * 256, 0);
        fx.tick(1);
        EXPECT_EQ(expect_phase[i], fx.var(kSocBallSpin)) << "tick " << i;
        EXPECT_EQ(expect_frame[i], static_cast<int>(fx.ball()->frame()))
            << "tick " << i;
    }
}

// A vertical-dominant flight takes its sign from vy, so a ball drifting
// slightly left while flying hard down still rolls FORWARD.
TEST_F(ModesSoccer, vertical_flight_takes_its_spin_direction_from_vy)
{
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        // vx = -256, vy = 2048. After friction: vx = -248, vy = 1991, so
        // |vy| dominates and the advance is (248 + 1991) / 8 = 279.
        int previous = 0;
        for (int i = 0; i < 3; ++i)
        {
            fx.set_ball(320, 400, -256, 8 * 256);
            fx.tick(1);
            EXPECT_EQ(279 * (i + 1), fx.var(kSocBallSpin)) << "tick " << i;
            EXPECT_GT(fx.var(kSocBallSpin), previous)
                << "downward flight rolls the strip forward despite vx < 0";
            previous = fx.var(kSocBallSpin);
        }
        EXPECT_EQ(3, static_cast<int>(fx.ball()->frame())) << "837 / 256";
    }
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        // Same magnitudes upward: the strip runs backward off frame 0.
        fx.set_ball(320, 400, 256, -8 * 256);
        fx.tick(1);
        EXPECT_EQ(kSpinCycle - 279, fx.var(kSocBallSpin));
        EXPECT_EQ(6, static_cast<int>(fx.ball()->frame())) << "1769 / 256";
    }
}

// Spin rate is a function of speed, not a fixed cadence.
TEST_F(ModesSoccer, spin_rate_scales_with_speed)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    fx.set_ball(320, 464, 8 * 256, 0);
    fx.tick(1);
    const int fast = fx.var(kSocBallSpin);
    EXPECT_EQ(kFullKickAdvance, fast);

    fx.world().mode.vars[kSocBallSpin] = 0;
    fx.set_ball(320, 464, 4 * 256, 0);
    fx.tick(1);
    const int slow = fx.var(kSocBallSpin);
    // Half the speed (1024 fp, 960 after friction) is 120 phase units.
    EXPECT_EQ(120, slow);
    EXPECT_GT(fast, slow * 2 - 1) << "a faster ball turns further per tick";
}

TEST_F(ModesSoccer, a_resting_ball_holds_its_frame)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    // Roll it up to a non-zero frame first, so "holds" is not just "never
    // left frame 0".
    for (int i = 0; i < 3; ++i)
    {
        fx.set_ball(320, 464, 8 * 256, 0);
        fx.tick(1);
    }
    const int resting_phase = fx.var(kSocBallSpin);
    const int resting_frame = static_cast<int>(fx.ball()->frame());
    ASSERT_EQ(2, resting_frame) << "744 / 256";

    fx.set_ball(320, 464, 0, 0);
    for (int i = 0; i < 10; ++i)
    {
        fx.tick(1);
        EXPECT_EQ(resting_phase, fx.var(kSocBallSpin)) << "tick " << i;
        EXPECT_EQ(resting_frame, static_cast<int>(fx.ball()->frame()))
            << "tick " << i;
    }
}

TEST_F(ModesSoccer, kickoff_reset_spots_the_ball_on_frame_zero)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    // Spin it up, then roll it into team 0's strip for a team 1 goal.
    for (int i = 0; i < 3; ++i)
    {
        fx.set_ball(320, 464, 8 * 256, 0);
        fx.tick(1);
    }
    ASSERT_NE(0, fx.var(kSocBallSpin));
    fx.world().mode.vars[kSocLastTouch1] = 2;
    fx.set_ball(52, 464, -4 * 256, 0);
    fx.tick(2);

    ASSERT_EQ(1, fx.team_var(kSocGoals, 1)) << "the goal must have landed";
    EXPECT_EQ(0, fx.var(kSocBallSpin)) << "a re-spotted ball rolls from rest";
    EXPECT_EQ(0, static_cast<int>(fx.ball()->frame()));
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

// A self-goal SCORES (this supersedes D7's own-goal-no-score arm). With
// two teams on the pitch the other side takes the point unconditionally:
// there is only one opponent, so no touch history is needed to name it.
TEST_F(ModesSoccer, own_goal_two_team_credits_the_other_side)
{
    // Team 0 puts it through its own left-hand strip.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched last
        fx.set_ball(52, 464, -4 * 256, 0);         // into team 0's own strip
        fx.tick(2);

        EXPECT_EQ(1, fx.team_var(kSocGoals, 1)) << "the opponent banks it";
        EXPECT_EQ(0, fx.team_var(kSocGoals, 0))
            << "the own-goal side scores nothing";
        EXPECT_TRUE(has_score_change(fx.events, 1, 400));
        EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! GREEN +1"));
        EXPECT_EQ(320, fx.ball_cx()) << "ball still resets";
        EXPECT_EQ(0, fx.var(kSocLastTouch1));
    }
    // Mirror direction: team 1 into its own right-hand strip.
    {
        SoccerWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.world().mode.vars[kSocLastTouch1] = 2;  // team 1 touched last
        fx.set_ball(600, 464, 0, 0);               // inside team 1's strip
        fx.tick(1);

        EXPECT_EQ(1, fx.team_var(kSocGoals, 0));
        EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
        EXPECT_TRUE(has_score_change(fx.events, 0, 400));
        EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! RED +1"));
    }
}

// Three or four teams: crediting "the other side" is ambiguous, so the
// point goes to the last DISTINCT other toucher — the side that forced
// the play. The touch history here is built by real walk-in kicks, so
// this also pins stamp_toucher's demotion rule.
TEST_F(ModesSoccer, own_goal_foursquare_credits_the_last_other_toucher)
{
    SoccerFourWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_EQ(4, fx.var(kSocTeamCount));
    ASSERT_EQ(15, fx.var(kSocTeamMask));
    fx.thaw_kickoff();

    touch_ball(fx, fx.players[2], 320, 480);
    EXPECT_EQ(3, fx.var(kSocLastTouch1)) << "team 2 plays it";
    EXPECT_EQ(0, fx.var(kSocLastTouch2)) << "the first touch demotes nobody";
    park_away(fx.players[2]);

    touch_ball(fx, fx.players[0], 320, 480);
    EXPECT_EQ(1, fx.var(kSocLastTouch1)) << "team 0 takes it off them";
    EXPECT_EQ(3, fx.var(kSocLastTouch2)) << "the outgoing toucher is demoted";
    park_away(fx.players[0]);

    // Team 0 carries it into its own top strip.
    fx.set_ball(320, 30, 0, 0);
    fx.tick(1);

    EXPECT_EQ(1, fx.team_var(kSocGoals, 2))
        << "the last distinct other toucher scores";
    EXPECT_EQ(0, fx.team_var(kSocGoals, 0));
    EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
    EXPECT_EQ(0, fx.team_var(kSocGoals, 3));
    EXPECT_TRUE(has_score_change(fx.events, 2, 400));
    EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! BLUE +1"));
    EXPECT_EQ(320, fx.ball_cx()) << "kickoff reset";
    EXPECT_EQ(480, fx.ball_cy());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// Nobody else ever touched it: with three opponents there is no defensible
// beneficiary, so the own-goal team forfeits one of its own. Goals may go
// negative; the HUD prints the integer and the win check keeps working.
TEST_F(ModesSoccer, own_goal_foursquare_without_another_toucher_forfeits)
{
    SoccerFourWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    const std::uint32_t score_before = fx.world().m_score[3];

    touch_ball(fx, fx.players[3], 320, 480);
    ASSERT_EQ(4, fx.var(kSocLastTouch1)) << "team 3 is the only toucher";
    ASSERT_EQ(0, fx.var(kSocLastTouch2));
    park_away(fx.players[3]);

    // Into team 3's own left strip.
    fx.set_ball(30, 320, 0, 0);
    fx.tick(1);

    EXPECT_EQ(-1, fx.team_var(kSocGoals, 3)) << "the forfeit goes negative";
    EXPECT_EQ(0, fx.team_var(kSocGoals, 0)) << "no opponent is credited";
    EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
    EXPECT_EQ(0, fx.team_var(kSocGoals, 2));
    EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! YELLOW -1"));
    EXPECT_GE(25u, longest_notification(fx.events))
        << "announcements stay inside the 25-char row budget";
    // og.award_score takes an UNSIGNED delta, so the forfeit deliberately
    // leaves the match score alone rather than wrapping it to ~4.29e9.
    EXPECT_EQ(score_before, fx.world().m_score[3]);
    EXPECT_FALSE(has_score_change(fx.events, 3, 400));
    EXPECT_STREQ("YELLOW -1/3", fx.world().mode.hud[3].text.data());

    // A negative row does not break the ordered win comparisons.
    fx.world().mode.vars[kSocGoals + 1] = 3;
    fx.tick(1);
    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(1, fx.world().mode.winner_team);
}

// The two rejection arms of the FOURSQUARE credit. A LAST_TOUCH2 naming
// the own-goal team itself is reachable in play: an uncredited impulse
// clears LAST_TOUCH1 between two touches by the same side, so the next
// touch demotes nobody and both slots end up on that side. A LAST_TOUCH2
// outside the active mask is the defensive case. Both forfeit.
TEST_F(ModesSoccer, foursquare_credit_rejects_self_and_inactive_touchers)
{
    // LAST_TOUCH2 == the own-goal team.
    {
        SoccerFourWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.world().mode.vars[kSocLastTouch1] = 1;
        fx.world().mode.vars[kSocLastTouch2] = 1;  // team 0 both times
        fx.set_ball(320, 30, 0, 0);                // team 0's own top strip
        fx.tick(1);

        EXPECT_EQ(-1, fx.team_var(kSocGoals, 0)) << "no self-credit";
        EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! RED -1"));
    }
    // LAST_TOUCH2 naming a team outside the active mask.
    {
        SoccerFourWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.soccer_active());
        fx.thaw_kickoff();
        fx.world().mode.vars[kSocTeamMask] = 7;    // teams 0-2 active
        fx.world().mode.vars[kSocTeamCount] = 3;
        fx.world().mode.vars[kSocLastTouch1] = 1;
        fx.world().mode.vars[kSocLastTouch2] = 4;  // team 3, not playing
        fx.set_ball(320, 30, 0, 0);
        fx.tick(1);

        EXPECT_EQ(-1, fx.team_var(kSocGoals, 0));
        EXPECT_EQ(0, fx.team_var(kSocGoals, 3))
            << "an inactive team is never credited";
        EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! RED -1"));
    }
}

// Defensive arm: a two-team match whose mask somehow holds only the
// scoring side has no opponent to credit, so the forfeit rule applies.
TEST_F(ModesSoccer, own_goal_without_an_opponent_in_the_mask_forfeits)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.world().mode.vars[kSocTeamMask] = 1;  // team 0 alone
    fx.world().mode.vars[kSocLastTouch1] = 1;
    fx.set_ball(30, 464, 0, 0);
    fx.tick(1);

    EXPECT_EQ(-1, fx.team_var(kSocGoals, 0));
    EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! RED -1"));
}

// A ball nobody was ever credited for still scores nothing. The uncredited
// impulse is the shipped path for it: an orphan weapon (weap::act self-owns
// a shot whose parent died, so the owner is no Living) moves the ball while
// clearing LAST_TOUCH1 — and it must NOT erase the older touch history,
// which on its own can never score either.
TEST_F(ModesSoccer, untouched_drift_in_still_scores_nothing)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    touch_ball(fx, fx.green, 320, 464);
    park_away(fx.green);
    touch_ball(fx, fx.red, 320, 464);
    ASSERT_EQ(1, fx.var(kSocLastTouch1));
    ASSERT_EQ(2, fx.var(kSocLastTouch2));
    park_away(fx.red);

    fx.set_ball(70, 464, 0, 0);
    walker* shot = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, shot);
    shot->setxy(67, 458);    // 6x6 knife: center (70, 461), L1 3 from the ball
    shot->set_lastx(-8.0f);  // flying left, no living owner
    shot->set_lasty(0.0f);
    fx.tick(1);

    EXPECT_EQ(0, fx.var(kSocLastTouch1)) << "an unowned shot credits nobody";
    EXPECT_EQ(2, fx.var(kSocLastTouch2))
        << "but it does not erase who forced the play";
    EXPECT_LT(fx.var(kSocBallVx), 0) << "it still imparted an impulse";

    shot->set_dead(1);
    fx.set_ball(30, 464, -256, 0);  // drifting inside team 0's strip
    fx.tick(1);

    EXPECT_EQ(0, fx.team_var(kSocGoals, 0));
    EXPECT_EQ(0, fx.team_var(kSocGoals, 1));
    EXPECT_TRUE(has_notification(fx.events, "NO GOAL!"));
    EXPECT_EQ(0, count_notifications(fx.events, "OWN GOAL!"))
        << "a stale LAST_TOUCH2 alone never scores";
    EXPECT_EQ(320, fx.ball_cx()) << "ball still resets";
}

// Kickoff immunity: the re-spot wipes the WHOLE touch history, so no
// pre-reset toucher can convert into a self-goal credit afterwards.
TEST_F(ModesSoccer, kickoff_reset_clears_the_whole_touch_history)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    touch_ball(fx, fx.green, 320, 464);
    park_away(fx.green);
    touch_ball(fx, fx.red, 320, 464);
    ASSERT_EQ(1, fx.var(kSocLastTouch1)) << "team 0 touched last";
    ASSERT_EQ(2, fx.var(kSocLastTouch2)) << "team 1 before that";
    park_away(fx.red);

    fx.set_ball(600, 464, 0, 0);  // team 0 scores in team 1's strip
    fx.tick(1);

    ASSERT_EQ(1, fx.team_var(kSocGoals, 0));
    EXPECT_EQ(0, fx.var(kSocLastTouch1)) << "slot 19 clears at kickoff";
    EXPECT_EQ(0, fx.var(kSocLastTouch2)) << "slot 45 clears with it";
}

// The self-goal point counts for the match, not just the scoreboard.
TEST_F(ModesSoccer, a_self_goal_can_win_the_match)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.world().mode.vars[kSocGoals + 1] = 2;   // team 1 on match point
    fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched last
    fx.set_ball(30, 464, 0, 0);                // into team 0's own strip
    fx.tick(1);

    EXPECT_EQ(3, fx.team_var(kSocGoals, 1));
    EXPECT_TRUE(has_notification(fx.events, "OWN GOAL! GREEN +1"));
    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(1, fx.world().mode.winner_team);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_TRUE(has_notification(fx.events, "GREEN TEAM WINS!"));
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

// #241: the lobby TIME LIMIT knob beats the manifest row — up, here, from
// the 120-tick probe row to a real match length. (It also closes the hole
// the old `row.time_limit or T.time_limit_ticks` left open: a row carrying
// time_limit = 0 used to bank 0 and end the match on tick one.)
TEST_F(ModesSoccer, time_limit_knob_overrides_the_manifest_row)
{
    ModesCtfWorld fx(kSoccerLevelC);  // row carries time_limit = 120
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.world().ctf_requested_time_limit = 3600;
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3600, fx.var(kSocTimeLimit)) << "the request beats the row";
    fx.tick(125);
    EXPECT_FALSE(fx.world().game_ended) << "the row's 120 no longer decides";
}

// ===========================================================================
// Respawning pickups (#225): the pitch runs lib/mode_items over its row
// ===========================================================================

// A pitch's pads are fed through the make_hooks(row) closure, not
// mode_levels — the fixture rows are synthetic and never enter the
// manifest module — so this also pins that the closure reaches items.run.
// 9301 carries two drumstick pads (tiles (10,10) and (14,10)) on a 30-tick
// interval and authors no food, so both pads start in deficit.
TEST_F(ModesSoccer, item_pad_denies_while_camped_then_fills_the_free_pad)
{
    SoccerWorld fx;
    // Camp pad 1 exactly: pad center (168, 168) means the item's top-left
    // is (160, 160), and a 16x16 living there overlaps it entirely. The
    // camper is ACT_CONTROL like the fixture's own two livings, so the
    // director never commands it off the pad.
    walker* camper = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, camper);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_EQ(1, fx.var(kSocItemLast)) << "init seeds the item clock";
    ASSERT_EQ(0, live_treasures(fx.world(), FAMILY_DRUMSTICK))
        << "the synthetic pitch authors no food — both pads start empty";

    // Tick 60 is the first 30-tick cadence boundary at or past the row's
    // 30-tick interval from the init seed.
    fx.tick(59);
    EXPECT_EQ(1, live_treasures(fx.world(), FAMILY_DRUMSTICK))
        << "exactly one item per firing";
    EXPECT_EQ(nullptr, item_at(fx.world(), FAMILY_DRUMSTICK, 160, 160))
        << "the camped pad is denied (pad_blocked)";
    EXPECT_NE(nullptr, item_at(fx.world(), FAMILY_DRUMSTICK, 224, 160))
        << "the rotation falls through to the free pad";
    EXPECT_EQ(60, fx.var(kSocItemLast));

    // The camper dies; the next firing fills the pad it was sitting on.
    camper->set_dead(1);
    fx.tick(30);
    EXPECT_EQ(2, live_treasures(fx.world(), FAMILY_DRUMSTICK));
    EXPECT_NE(nullptr, item_at(fx.world(), FAMILY_DRUMSTICK, 160, 160))
        << "an uncamped pad fills on the next firing";
    EXPECT_EQ(90, fx.var(kSocItemLast));
    expect_no_soccer_script_errors(fx.world());
}

// ===========================================================================
// B1: the kickoff backstop (design §6.3) + dead-ball reset (§6.2.4)
// ===========================================================================

TEST_F(ModesSoccer, goal_kickoff_reprovisions_a_wiped_bot_team)
{
    SoccerWorld fx;
    walker* red_extra = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 448, ACT_GUARD);
    fx.world().respawn_mode = 0;  // the submenu no longer gates a ball game
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // E5: turn the wheel AFTER init — at init an explicit FAIR would walk
    // a squad on beside the authored green troop (D3); the reprovision
    // backstop reads the knob at spawn time, so the mid-match turn is what
    // lets the wiped side come back.
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
    fx.thaw_kickoff();
    // Team 1 is wiped; team 0 loses a member but keeps red alive. Every
    // corpse schedules the tick it falls — submenu Off included.
    fx.green->set_dead(1);
    red_extra->set_dead(1);
    fx.tick(2);
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 1))
        << "always-on scheduling covers the wiped side";
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0))
        << "and the non-wiped side's corpse too";
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));

    // The B1 reprovision arm still guards the no-revives-in-flight edge
    // (entries genuinely lost, bodies swept — a full all-player queue):
    // drain the queue AND the drained corpses, then score.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);
    retire_dead_guyless(fx.world(), 0);
    fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched last
    fx.set_ball(600, 464, 4 * 256, 0);         // into team 1's strip
    fx.tick(1);
    EXPECT_TRUE(has_notification(fx.events, "RED TEAM GOAL!"));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1))
        << "the kickoff backstop refields the wiped team (B1)";
    EXPECT_EQ(1, alive_on_team(fx.world(), 0))
        << "a team with a live member is left alone";
    // BOT_MARK provenance (#218 staged preview): the revive spawns go
    // through the same add_squad_member choke point as the init squads, so
    // every refielded bot carries the mark (mode_caps BOT_MARK_BIT).
    int marked = 0;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != 1 || w->stats() == nullptr)
            continue;
        if ((w->stats()->bit_flags() & 65536) != 0)
            ++marked;
    }
    EXPECT_EQ(5, marked)
        << "every wiped-team revive spawn carries the bot mark";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// Issue #235 latch agreement: the squad order is drawn ONCE per match and
// latched in the shared header var, so a wiped bot side comes back as the
// SAME squad — the mid-match revive backstop decodes the latched code with
// zero RNG draws instead of re-rolling a new order. (Green before the
// seeded permutation too — the fixed table trivially agreed with itself —
// but post-#235 this pins the init/mid-match latch agreement.)
TEST_F(ModesSoccer, wiped_bot_team_revives_with_the_same_match_squad)
{
    SoccerPitch fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(1, 528, 448);
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: the fill
    walker* red = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_FALSE(red->dead());
    const std::vector<int> at_init = team_families_in_order(fx.world(), 1);
    ASSERT_EQ(5u, at_init.size()) << "the empty side fields a five-bot squad";
    const std::int32_t seed_at_init = fx.var(kSlotSquadSeed);

    fx.thaw_kickoff();
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1)
            w->set_dead(1);
    }
    fx.tick(2);
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));
    // The B1 reprovision arm guards the no-revives-in-flight edge (the
    // goal_kickoff_reprovisions_a_wiped_bot_team shape): drain the queue
    // and the drained corpses, then score to trigger the kickoff.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);
    fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched last
    fx.set_ball(600, 464, 4 * 256, 0);         // into team 1's strip
    fx.tick(1);
    ASSERT_EQ(5, alive_on_team(fx.world(), 1))
        << "the kickoff backstop refields the wiped side (B1)";

    EXPECT_EQ(at_init, team_families_in_order(fx.world(), 1))
        << "a wiped side must come back as the SAME match squad (#235)";
    EXPECT_EQ(seed_at_init, fx.var(kSlotSquadSeed))
        << "the latched squad-order code survives the wipe";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, kickoff_revive_schedules_corpses_and_skips_summons)
{
    SoccerWorld fx;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 560, 448, 7);
    walker* summon = fx.spawn_living(FAMILY_SOLDIER, 1, 560, 480, ACT_GUARD);
    walker* red_extra = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 448, ACT_GUARD);
    summon->set_owner(fx.green);
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    // All of team 1 plus a team-0 extra fall this tick; the corpses are
    // still in the oblist when the goal fires.
    fx.green->set_dead(1);
    hero->set_dead(1);
    summon->set_dead(1);
    red_extra->set_dead(1);
    fx.world().mode.vars[kSocLastTouch1] = 1;
    fx.set_ball(600, 464, 4 * 256, 0);
    fx.tick(1);

    EXPECT_TRUE(player_revive_pending(fx.world(), hero->entity_id()))
        << "roster corpses are scheduled the tick they fall";
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 1))
        << "the unowned AI corpse is scheduled; the summon stays down";
    EXPECT_EQ(0, alive_on_team(fx.world(), 1))
        << "revives in flight: no bot-squad reprovision on top";
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0))
        << "the non-wiped team's corpse is scheduled too — always-on";
    fx.tick(70);  // respawn_ticks 60 + slack
    EXPECT_FALSE(hero->dead()) << "the hero is back for the kickoff";
    EXPECT_EQ(2, alive_on_team(fx.world(), 1))
        << "hero + AI replacement return; the summon does not";
    EXPECT_EQ(2, alive_on_team(fx.world(), 0))
        << "red + the fallen extra's replacement";
}

TEST_F(ModesSoccer, unattended_dead_ball_resets_after_600_ticks)
{
    SoccerWorld fx;
    // Hold the wiped side's revive past the whole stall window so the
    // dead-ball clock (not the respawn delay) is what this test times.
    fx.world().ctf_requested_respawn_ticks = 1200;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // Both parked livings sit L1 > 160 px from the kickoff spot, so the
    // stall clock runs from the first post-init tick (the MUDBOWL
    // parked-ball shape from the sweep). Wipe green first: the reset's
    // kickoff also runs the revive backstop.
    fx.green->set_dead(1);
    fx.tick(598);
    EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET")) << "not yet";
    fx.tick(5);
    EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"))
        << "600 unattended motionless ticks reset the ball (B1)";
    EXPECT_EQ(320, fx.ball_cx());
    EXPECT_EQ(464, fx.ball_cy());
    EXPECT_GT(fx.var(kSocKickoffUntil), 600) << "post-reset freeze armed";
    EXPECT_EQ(0, alive_on_team(fx.world(), 1))
        << "the revive rides its long delay — no squad reprovision on top";
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 1))
        << "the wiped side's revive stays in flight";
}

TEST_F(ModesSoccer, a_nearby_living_clears_the_stall_clock)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // Attendance: red inside the 160 px radius (but outside kick reach)
    // keeps the ball in play indefinitely.
    fx.red->setxy(250, 420);  // center (258, 428): L1 = 98 from the ball
    fx.tick(700);
    EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
        << "an attended ball never dead-balls";
    // Walking away starts the clock fresh.
    fx.red->setxy(96, 96);
    fx.tick(605);
    EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"));
}

TEST_F(ModesSoccer, attended_waterlogged_ball_resets_at_600_ticks)
{
    SoccerWorld fx;
    paint_water(fx.world(), 18, 27, 22, 31);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);
    fx.red->setxy(250, 420);  // center (258,428): attended, outside kick reach
    fx.tick(1);
    const std::int32_t since = fx.var(kSocStallSince);
    ASSERT_GT(since, 0) << "waterlogging starts the recovery clock";

    constexpr int kDeadBallTicks = 600;
    const std::int32_t one_before_reset = since + kDeadBallTicks - 1;
    for (int step = 0;
         step < kDeadBallTicks &&
         static_cast<std::int32_t>(fx.world().tick_count_) < one_before_reset;
         ++step)
    {
        fx.tick(1);
    }
    ASSERT_EQ(one_before_reset,
              static_cast<std::int32_t>(fx.world().tick_count_))
        << "the bounded watchdog probe must reach the exact boundary";
    EXPECT_EQ(0, count_notifications(fx.events, "BALL RESET"))
        << "one tick short: players get the full projectile-rescue window";
    EXPECT_EQ(320, fx.ball_cx());
    EXPECT_EQ(464, fx.ball_cy());

    fx.tick(1);
    EXPECT_EQ(1, count_notifications(fx.events, "BALL RESET"))
        << "water attendance cannot strand the objective forever";
    EXPECT_EQ(320, fx.ball_cx());
    EXPECT_EQ(464, fx.ball_cy());
    EXPECT_GT(fx.var(kSocKickoffUntil),
              static_cast<std::int32_t>(fx.world().tick_count_));
}

// ===========================================================================
// Respawns are always on: a ball game ignores the difficulty submenu's
// respawn choice — every corpse that owns its life comes back.
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

TEST_F(ModesSoccer, submenu_off_still_schedules_everyone)
{
    SoccerRespawnWorld fx;
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    fx.tick(3);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0));
    fx.tick(70);
    EXPECT_FALSE(fx.hero->dead()) << "Off no longer keeps anyone down";
    EXPECT_EQ(3, alive_on_team(fx.world(), 0))
        << "red + hero + the bot's replacement";
}

TEST_F(ModesSoccer, hero_revives_on_anchor_whatever_the_submenu_says)
{
    SoccerRespawnWorld fx;
    fx.world().respawn_mode = 1;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    fx.tick(2);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0))
        << "the AI corpse is scheduled regardless of the Heroes choice";
    fx.tick(70);  // delay 60 + slack
    EXPECT_FALSE(fx.hero->dead());
    const bool at_anchor =
        (fx.hero->xpos() == 96 && (fx.hero->ypos() == 448 ||
                                   fx.hero->ypos() == 480));
    EXPECT_TRUE(at_anchor) << "revive lands on a team-0 anchor, got ("
                           << fx.hero->xpos() << "," << fx.hero->ypos() << ")";
    EXPECT_GT(fx.var(kSocAnchorCursor), 0) << "anchor cursor rotated";
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
        << "the always-on scan matches the old Everyone semantics";
}

TEST_F(ModesSoccer, submenu_team_gate_is_ignored_every_hero_returns)
{
    SoccerRespawnWorld fx;
    walker* green_hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 560, 448, 8);
    fx.world().respawn_mode = 3;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.kill_both();
    green_hero->set_dead(1);
    fx.tick(2);
    EXPECT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    EXPECT_TRUE(player_revive_pending(fx.world(), green_hero->entity_id()))
        << "the Team 1-only choice no longer strands other teams";
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 0));
}

// Issue #221 (the playtest report): a slain soccer bot must leave its
// corpse on the pitch — visible and cleric-raisable — for the whole
// respawn countdown; the fire then retires it with the replacement.
TEST_F(ModesSoccer, slain_bot_leaves_a_raisable_corpse_until_respawn)
{
    SoccerRespawnWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    ASSERT_EQ(3, alive_on_team(fx.world(), 0)) << "red + hero + bot";

    // The reporter's scenario: a fielded team-0 fighter falls in play,
    // through the real death path (bloodspot and all).
    walker* bot = fx.bot;
    const std::uint32_t corpse_id = bot->entity_id();
    bot->set_dead(1);
    bot->death();
    fx.tick(1);  // the always-on scan queues the corpse

    ASSERT_TRUE(og::sim::respawn_pending_for(fx.world(), bot));
    EXPECT_TRUE(stain_alive_at(fx.world(), 160, 480))
        << "the corpse must stay on the pitch through the countdown";

    // The cleric's corpse-discovery seam: nearby_corpse() resolves through
    // find_nearest_blood, so a raise can reach this body mid-countdown.
    fx.hero->setxy(184, 480);
    walker* blood = fx.world().find_nearest_blood(fx.hero);
    ASSERT_NE(nullptr, blood)
        << "a cleric beside the fallen bot must find the corpse";
    EXPECT_EQ(160, blood->xpos());
    EXPECT_EQ(480, blood->ypos());

    fx.tick(70);  // respawn delay 60 + slack
    EXPECT_FALSE(stain_alive_at(fx.world(), 160, 480))
        << "the fire retires the stain along with the corpse";
    EXPECT_EQ(nullptr, fx.world().find_by_id(corpse_id));
    EXPECT_EQ(3, alive_on_team(fx.world(), 0))
        << "the replacement restores the census";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, camped_anchors_cannot_stall_a_respawn)
{
    SoccerRespawnWorld fx;
    // Campers parked EXACTLY on both team-0 anchors: the rotation probe
    // fails everywhere, and before the ring fallback the revive stayed
    // wherever the corpse fell. Wildlife team (outside the score range)
    // so neither the strip nor the AI director ever moves them — a
    // Living blocks a spawn probe whatever its team.
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // Spawn the campers AFTER init (mid-match camping): init-time census
    // and stripping never see them.
    walker* c1 = fx.spawn_living(FAMILY_SOLDIER, 4, 96, 448, ACT_SIT);
    walker* c2 = fx.spawn_living(FAMILY_SOLDIER, 4, 96, 480, ACT_SIT);
    ASSERT_NE(nullptr, c1);
    ASSERT_NE(nullptr, c2);
    fx.hero->setxy(400, 300);  // die far from the spawn line
    fx.hero->set_dead(1);
    fx.tick(2);
    ASSERT_EQ(0, c1->dead()) << "the camper holds its post";
    ASSERT_EQ(0, c2->dead()) << "the camper holds its post";
    ASSERT_TRUE(player_revive_pending(fx.world(), fx.hero->entity_id()));
    fx.tick(70);  // delay 60 + slack
    ASSERT_FALSE(fx.hero->dead());
    ASSERT_EQ(0, c1->dead()) << "the camper still holds the anchor";
    ASSERT_EQ(0, c2->dead()) << "the camper still holds the anchor";
    // The ring walk lands on a clear tile within 3 tiles (48 px L1) of an
    // anchor — never at the corpse spot, and never merely at the hero's
    // own 64-px-away recorded home (the engine's revive-home move).
    const int d1 = std::abs(fx.hero->xpos() - 96) +
                   std::abs(fx.hero->ypos() - 448);
    const int d2 = std::abs(fx.hero->xpos() - 96) +
                   std::abs(fx.hero->ypos() - 480);
    EXPECT_LE(std::min(d1, d2), 48)
        << "revive lands beside the camped spawn line, got ("
        << fx.hero->xpos() << "," << fx.hero->ypos() << ")";
}

// ===========================================================================
// Director: chasers on the own-goal side, goalkeeping
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
    // The goalie holds the intercept line instead: the ball's cross-axis
    // 464 sits inside the mouth span, on the standoff line x = 64.
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 64, 464))
        << "the keeper's objective is the goal-mouth intercept";
    EXPECT_EQ(fx.var(kSocBallEntity),
              static_cast<std::int32_t>(goalie->leader_id()))
        << "everyone leads on the ball (auto-foe suppression)";
}

TEST_F(ModesSoccer, goalie_holds_the_intercept_line_and_blocks_a_shot)
{
    // The staged shot-block: the keeper is sent to the ball-intercept
    // point on its own mouth, and a ball driven at the goal runs into it
    // there — the flight contact reverses the velocity away from the
    // mouth and the follow-up walk-in kick clears, with no goal scored.
    // A second member plays afield (a sole member is all striker under
    // D38, so the mouth geometry needs a two-member squad to pin).
    SoccerWorld fx;
    walker* goalie = fx.spawn_living(FAMILY_SOLDIER, 0, 100, 500, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 0, 400, 800, ACT_SIT);  // afield striker
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();

    // Ball afield at (200, 448): outside the defensive box, so the
    // objective is the intercept line x = 64 (16 + 32 + standoff 16) with
    // the ball's y clamped into the mouth span 400..528.
    fx.set_ball(200, 448, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 64, 448))
        << "the keeper moves onto the ball-intercept line";

    // Park the keeper on that line (center 72, 448) and fire the ball at
    // the mouth at the full 8 px/tick. It must NOT reach the goal rect:
    // the keeper's body takes the contact and the rebound flips vx.
    goalie->setxy(64, 440);
    fx.set_ball(128, 448, -8 * 256, 0);
    fx.tick(8);
    EXPECT_GT(fx.var(kSocBallVx), 0)
        << "the cleared ball moves away from the mouth";
    EXPECT_GT(fx.ball_cx(), 64) << "never past the keeper into the rect";
    for (int team = 0; team < 4; ++team)
        EXPECT_EQ(0, fx.team_var(kSocGoals, team)) << "no goal scored";
    EXPECT_FALSE(has_notification(fx.events, "GOAL"));
}

TEST_F(ModesSoccer, foursquare_keepers_hold_all_four_mouths)
{
    // One directable keeper per team on the FOURSQUARE pitch: every mouth
    // orientation (N/E/S/W) resolves its own standoff line, with the
    // ball's cross-axis clamped into the mouth span. Each team also
    // fields an afield striker (a sole member is all striker under D38,
    // so the keeper pins need two-member squads), placed farther from its
    // own mouth than the keeper designate.
    SoccerFourWorld fx;
    walker* north = fx.spawn_living(FAMILY_SOLDIER, 0, 312, 60, ACT_SIT);
    walker* east = fx.spawn_living(FAMILY_SOLDIER, 1, 560, 312, ACT_SIT);
    walker* south = fx.spawn_living(FAMILY_SOLDIER, 2, 312, 880, ACT_SIT);
    walker* west = fx.spawn_living(FAMILY_SOLDIER, 3, 60, 312, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 400, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 440, 600, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 2, 200, 560, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 3, 440, 360, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    // Ball at the kickoff spot (320, 480), inside nobody's box.
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(north, COMMAND_GOTO, 320, 64))
        << "north wall: line y = 16 + 32 + 16, ball x inside 256..384";
    EXPECT_TRUE(front_command_is(east, COMMAND_GOTO, 576, 384))
        << "east wall: line x = 592 - 16, ball y clamped down to 384";
    EXPECT_TRUE(front_command_is(south, COMMAND_GOTO, 320, 896))
        << "south wall: line y = 912 - 16";
    EXPECT_TRUE(front_command_is(west, COMMAND_GOTO, 64, 384))
        << "west wall: line x = 16 + 32 + 16";

    // Ball into the north box (300, 100): the north keeper charges it
    // directly (contact kicks fire along ball - kicker, so the goal-side
    // charge clears); the south keeper keeps shadowing the cross-axis
    // from its own line.
    fx.set_ball(300, 100, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(north, COMMAND_GOTO, 300, 100))
        << "the north-wall keeper charges a ball inside its box";
    EXPECT_TRUE(front_command_is(south, COMMAND_GOTO, 300, 896));
}

TEST_F(ModesSoccer, goalie_charges_a_box_ball_and_never_outruns_the_leash)
{
    // The afield second member keeps this a two-member squad, so the
    // keeper role exists at all (a sole member is all striker, D38).
    SoccerWorld fx;
    walker* goalie = fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 0, 400, 800, ACT_SIT);  // afield striker
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    // A ball inside the defensive box and inside the leash is charged.
    fx.set_ball(60, 464, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 60, 464))
        << "box ball within the leash: charge it";

    // A box ball BEYOND the leash (Manhattan 192 from the rect center) is
    // played from the intercept line instead — the keeper never abandons
    // the mouth (target distance 96 <= leash 112).
    fx.set_ball(110, 350, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 64, 400))
        << "cross-axis clamped up to the mouth span, not chased";

    // The same on the far post (ball y past the span, distance 128).
    fx.set_ball(64, 560, 0, 0);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 64, 528))
        << "far-post clamp holds the keeper at the mouth";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
}

// The matched-teams D38 pin: a bot that is its team's ONLY live member
// plays STRIKER, never goalkeeper. The pre-amendment director assigned
// the goalie first, so a singleton squad (a matched 1v1, or a whittled
// team's last bot) camped its own mouth forever and could never score.
// The lone bot must be commanded through the ball and actually kick it
// goalward — asserted on the command target and the ball's displacement,
// not role internals. Staged as a TRUE solo (no parked teammate): a live
// human teammate would flip it back to goalie (the mixed-team rule, the
// test below). Accepted residual, stated in the spec: the rule ignores
// score state — a LEADING true-solo survivor still chases.
TEST_F(ModesSoccer, sole_member_plays_striker_and_kicks_never_keeps_goal)
{
    SoccerPitch fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(0, 96, 480);
    fx.spawn_anchor(1, 528, 448);
    fx.spawn_anchor(1, 528, 480);
    // The same spot the two-member striker test uses: center 20 px from
    // the ball, outside kick_radius, inside the drive corridor.
    walker* lone = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 464, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);  // parked enemy player
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);

    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(lone, COMMAND_GOTO, 320, 456))
        << "the singleton takes the striker arm — through the ball, never "
           "the goal-mouth intercept (64, 464)";

    fx.tick(kAiCadence - 1);
    EXPECT_GT(fx.var(kSocBallVx), 0)
        << "the lone bot kicks the ball goalward instead of camping";
    EXPECT_EQ(1, fx.var(kSocLastTouch1)) << "and is the last toucher";
}

// D38 as fixed (adversarial review): a live HUMAN teammate can score, so
// the lone bot keeps the GOALIE assignment — the small-team gate censuses
// the team's live count, not the directable count (is_directable excludes
// player walkers, so the first cut read human + 1 bot as a desperate
// n = 1 and abandoned the mouth).
TEST_F(ModesSoccer, lone_bot_keeps_goal_while_a_human_teammate_plays_on)
{
    SoccerWorld fx;  // red = parked ACT_CONTROL human on team 0
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    // Ball at the kickoff spot (320, 464): the keeper's objective is the
    // intercept line x = 64 with the ball's cross-axis inside the mouth.
    EXPECT_TRUE(front_command_is(bot, COMMAND_GOTO, 64, 464))
        << "the lone bot takes the GOALIE assignment, not the striker "
           "chase, while its human teammate plays on (D38 as fixed)";
    EXPECT_FALSE(fx.red->stats()->has_commands())
        << "the human is never commanded";
}

// ===========================================================================
// Director: the two-phase striker (drive through the ball) + engage gate
// ===========================================================================

TEST_F(ModesSoccer, striker_at_the_approach_point_drives_through_the_ball)
{
    // The milling repro. A chaser standing ON its own approach point has
    // its center 20 px (Manhattan) from the ball — outside the 12 px
    // contact radius — and the pre-fix director re-issued that same point
    // every cadence, so the bot stood next to the ball forever. It must
    // now be sent THROUGH the ball and kick it goalward inside one
    // cadence.
    SoccerWorld fx;
    walker* goalie = fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    walker* striker = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 464, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);
    ASSERT_EQ(20, std::abs(320 - (striker->xpos() + 8)) +
                      std::abs(464 - (striker->ypos() + 8)))
        << "the approach point parks the striker outside kick_radius 12";

    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    // Through-ball: the ball center pushed drive_offset 8 px on toward
    // the enemy goal (608, 464), less the 8 px half-body the GOTO frame
    // needs — so the striker's CENTER, which is what the contact kick
    // measures, is aimed at the ball itself.
    EXPECT_TRUE(front_command_is(striker, COMMAND_GOTO, 320, 456))
        << "the striker is commanded through the ball, not beside it";
    EXPECT_TRUE(front_command_is(goalie, COMMAND_GOTO, 64, 464))
        << "the keeper still keeps goal";

    fx.tick(kAiCadence - 1);
    EXPECT_GT(fx.var(kSocBallVx), 0) << "the kick carries the ball goalward";
    EXPECT_GT(fx.var(kSocBallVx), std::abs(fx.var(kSocBallVy)))
        << "and goalward dominates the lateral component";
    EXPECT_EQ(1, fx.var(kSocLastTouch1)) << "team 0 is the last toucher";
    EXPECT_EQ(static_cast<std::int32_t>(striker->entity_id()),
              fx.var(kSocLastKicker));
}

TEST_F(ModesSoccer, a_wrong_side_chaser_takes_the_approach_point_arm)
{
    // Phase A: a chaser on the goal side of the BALL (nothing between it
    // and the enemy goal to kick toward) is staged behind the ball
    // instead — driving from there would knock the ball back downfield.
    SoccerWorld fx;
    fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    walker* chaser = fx.spawn_living(FAMILY_SOLDIER, 0, 400, 600, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);

    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(chaser, COMMAND_GOTO, 300, 464))
        << "wrong side of the ball: walk the approach point";

    fx.tick(kAiCadence - 1);
    EXPECT_EQ(0, fx.var(kSocBallVx)) << "the staging walk kicks nothing";
    EXPECT_EQ(0, fx.var(kSocBallVy));
    EXPECT_EQ(0, fx.var(kSocLastTouch1));
}

TEST_F(ModesSoccer, the_drive_corridor_holds_once_the_chaser_is_committed)
{
    // Hysteresis. At a 28 px lateral miss a chaser is outside the 24 px
    // corridor a drive may START from but inside the 32 px one a drive is
    // HELD through, and the two arms are told apart by the chaser's own
    // facing — replicated walker state, no script-side memory. So a ball
    // drifting across the line cannot flip a committed chaser back to the
    // approach point every cadence.
    SoccerWorld fx;
    fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    walker* chaser = fx.spawn_living(FAMILY_SOLDIER, 0, 272, 484, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);

    // Center (280, 492): 40 px goal-side of the ball, 28 px off the line.
    // Facing the enemy goal (FACE_RIGHT) = committed, so the drive holds.
    chaser->setxy(272, 484);
    chaser->stats()->clear_command();
    chaser->set_curdir(2);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
    EXPECT_TRUE(front_command_is(chaser, COMMAND_GOTO, 320, 456))
        << "a committed chaser holds the drive out to drive_cross_hold";

    // Same spot, turned back downfield: it has to re-qualify on the tight
    // corridor, and 28 > 24 fails it.
    chaser->setxy(272, 484);
    chaser->stats()->clear_command();
    chaser->set_curdir(6);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(front_command_is(chaser, COMMAND_GOTO, 300, 464))
        << "turned away, the same spot is outside the entry corridor";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesSoccer, at_most_one_chaser_peels_off_to_fight)
{
    // Engage gate. Three chasers all holding a combat command: the one
    // nearest the ball (the striker) is re-commanded onto it whatever it
    // was doing, exactly ONE of the others keeps its fight (the earliest
    // in oblist order), and the rest are pulled back onto the ball.
    SoccerWorld fx;
    fx.spawn_living(FAMILY_SOLDIER, 0, 40, 470, ACT_SIT);
    walker* striker = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 464, ACT_SIT);
    walker* first = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 300, ACT_SIT);
    walker* second = fx.spawn_living(FAMILY_SOLDIER, 0, 250, 600, ACT_SIT);
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    fx.thaw_kickoff();
    fx.set_ball(320, 464, 0, 0);

    // All three are brawling with the parked enemy.
    walker* enemy = fx.green;
    ASSERT_TRUE(enemy != nullptr);
    for (walker* w : {striker, first, second})
    {
        w->set_foe(enemy);
        w->stats()->force_command(COMMAND_ATTACK, 30, 1, 1);
    }
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    EXPECT_TRUE(front_command_is(striker, COMMAND_GOTO, 320, 456))
        << "the striker's fight never outranks the ball";
    EXPECT_EQ(COMMAND_ATTACK, front_type(first))
        << "one defender may keep chasing its foe";
    EXPECT_TRUE(first->foe() != nullptr) << "and keeps the foe to chase";
    EXPECT_TRUE(front_command_is(second, COMMAND_GOTO, 300, 448))
        << "the second brawler is pulled back onto the ball (stagger 16)";
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

// The camera declaration (issue #224, design §8 F): on_mode_init points
// camera slot 0 at the ball in the "auto" style. Nothing seat-derived rides
// the wire — docked-vs-inset is each machine's own resolution.
TEST_F(ModesSoccer, init_points_the_camera_view_at_the_ball)
{
    SoccerWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());

    const walker* const ball = fx.ball();
    ASSERT_NE(nullptr, ball);
    EXPECT_EQ(fx.var(kSocBallEntity), fx.world().mode.cameras[0].entity_id)
        << "camera slot 0 follows the banked ball";
    EXPECT_EQ(static_cast<std::int32_t>(ball->entity_id()),
              fx.world().mode.cameras[0].entity_id);
    EXPECT_EQ(og::sim::kCameraStyleAuto, fx.world().mode.cameras[0].style)
        << "auto: each machine resolves docked-vs-inset locally";

    // Set ONCE, in on_mode_init — no per-tick re-assertion (the
    // instruction-budget rule). Hand-clearing the slot therefore STAYS
    // cleared across a live window including a kickoff reset.
    fx.world().mode.cameras[0] = og::sim::ModeCameraView{};
    fx.tick(30);
    EXPECT_EQ(0, fx.world().mode.cameras[0].entity_id)
        << "on_mode_tick must not re-declare the camera every tick";
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
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: bot sides
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
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
        fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5
        fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
        fx.tick(1);  // init (bot squads + ball) under the budget
        ASSERT_TRUE(fx.world().mode.active);
        fx.tick(45);  // 3 director cadences + kicks + flight + HUD
        EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
            << "a 10x-reduced budget must never trip";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    og::script::g_test_world_instruction_budget = 0;
}


// ===========================================================================
// Client mirror replication
// ===========================================================================

// The shipping regression: modes:ball is the branch's only Lua-SPAWNED pack
// family, so it is the first entity ever to put a family byte above the core
// span (`wire_id: auto` numbers pack families from NUM_FAMILIES up) onto the
// snapshot wire. apply_snapshot clamped that byte to 0, so the mirror's ball
// was a different family from the authority's on every tick: the server's
// hash check struck from the ball's first appearance and every forced
// keyframe re-applied the same clamp. Soccer 820-823 disconnected the local
// transport client at 120 strikes, ~10 s into every match.
TEST_F(ModesSoccer, match_replicates_to_a_client_mirror_without_hash_strikes)
{
    // Anchors only: the init backstop fields bot squads that actually chase
    // and kick, so the window covers init, the kickoff freeze, live ball
    // flight and the director cadences rather than a static pitch.
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 432);
    fx.spawn_anchor(0, 96, 496);
    fx.spawn_anchor(1, 528, 432);
    fx.spawn_anchor(1, 528, 496);
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: bot sides
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;
    ModeMirror mirror(kSoccerLevelA);

    // kMaxConsecutiveSnapshotHashMismatches ticks: the exact run the server
    // needs before it disconnects a desynced client.
    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick
        << "; 120 consecutive strikes disconnect the client";

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);

    walker* const ball = fx.world().find_by_id(
        static_cast<std::uint32_t>(fx.var(kSocBallEntity)));
    ASSERT_NE(nullptr, ball);
    EXPECT_GE(static_cast<int>(ball->family()), NUM_FAMILIES)
        << "modes:ball must still be a pack family for this test to bite";

    walker* const mirrored = mirror.world().find_by_id(ball->entity_id());
    ASSERT_NE(nullptr, mirrored) << "the mirror must materialize the ball";
    EXPECT_EQ(static_cast<int>(ball->family()),
              static_cast<int>(mirrored->family()));
    EXPECT_EQ(ball->query_order(), mirrored->query_order());
    EXPECT_EQ(ball->xpos(), mirrored->xpos());
    EXPECT_EQ(ball->ypos(), mirrored->ypos());
    EXPECT_EQ(ball->sizex(), mirrored->sizex());
    EXPECT_EQ(ball->sizey(), mirrored->sizey());
    EXPECT_EQ(ball->team_num(), mirrored->team_num());
    ASSERT_NE(nullptr, ball->stats());
    ASSERT_NE(nullptr, mirrored->stats());
    EXPECT_TRUE(ball->stats()->query_bit_flags(BIT_SWIMMING));
    EXPECT_TRUE(mirrored->stats()->query_bit_flags(BIT_SWIMMING))
        << "the objective's terrain capability must cross the snapshot";

    // The rolling spin is server-authored: run_spin picks the frame and
    // EntitySnapshot.frame carries it, so the mirror draws the authority's
    // frame without re-deriving it (a mirror never runs animate()). The
    // ball has been kicked around for 120 ticks by now, so this also pins
    // that the frame it landed on is a real strip index.
    EXPECT_EQ(ball->frame(), mirrored->frame())
        << "the drawn ball frame must replicate";
    EXPECT_GE(static_cast<int>(mirrored->frame()), 0);
    EXPECT_LT(static_cast<int>(mirrored->frame()), 8);

    // Everything the client HUD, radar and beacon read comes over the same
    // snapshot; a stale mode block is the other way this desyncs.
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(fx.world().mode.vars[static_cast<std::size_t>(slot)], mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
    EXPECT_EQ(ball->entity_id(),
              static_cast<std::uint32_t>(mirror.world().mode.beacons[0].entity_id))
        << "the ball beacon must point at the replicated ball";
    EXPECT_EQ(ball->entity_id(),
              static_cast<std::uint32_t>(mirror.world().mode.cameras[0].entity_id))
        << "the camera declaration must point at the replicated ball";
}

// The camera channel is replicated state, not a local render decision: a
// mirror never runs mode Lua, so it learns the declaration ONLY through a
// snapshot apply — and learning it must not cost a hash strike.
TEST_F(ModesSoccer, the_camera_declaration_replicates_to_a_client_mirror)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 432);
    fx.spawn_anchor(1, 528, 432);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    ModeMirror mirror(kSoccerLevelA);

    // Ordering: empty BEFORE the first apply — mirrors run no mode Lua.
    EXPECT_EQ(0, mirror.world().mode.cameras[0].entity_id)
        << "a mirror has no camera before its first snapshot apply";
    EXPECT_FALSE(mirror.world().mode.active);

    // One tick: the authority inits and the mirror applies that keyframe.
    const MirrorReplication first = replicate_to_mirror(fx, mirror, 1);
    EXPECT_EQ(0, first.strikes) << "the camera slot must hash identically";
    ASSERT_TRUE(fx.world().mode.active);

    const std::int32_t declared = fx.world().mode.cameras[0].entity_id;
    ASSERT_NE(0, declared) << "the authority declared a camera on init";
    EXPECT_EQ(declared, mirror.world().mode.cameras[0].entity_id)
        << "the mirror follows the same replicated entity id";
    EXPECT_EQ(fx.world().mode.cameras[0].style,
              mirror.world().mode.cameras[0].style);
    EXPECT_NE(nullptr, mirror.world().find_by_id(
                           static_cast<std::uint32_t>(declared)))
        << "the followed id resolves in the mirror world too";

    // ... and stays matched across a live window of ball play.
    const MirrorReplication rest = replicate_to_mirror(fx, mirror, 60);
    EXPECT_EQ(0, rest.strikes)
        << "the mirror first desynced at tick " << rest.first_strike_tick;
    EXPECT_EQ(fx.world().mode.cameras[0].entity_id,
              mirror.world().mode.cameras[0].entity_id);
    EXPECT_EQ(fx.world().mode.cameras[0].style,
              mirror.world().mode.cameras[0].style);
}

// ===========================================================================
// TROOPS: FAIR system tests (matched-teams WP-F, spec §8)
// ===========================================================================

namespace {

// Sorted walker-stats levels of a team's live Livings.
std::vector<int> team_levels_sorted(GameWorld& world, int team)
{
    std::vector<int> levels;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team))
            continue;
        if (w->stats() == nullptr)
            continue;
        levels.push_back(w->stats()->level());
    }
    std::sort(levels.begin(), levels.end());
    return levels;
}

// A pitch with a solo LEVELED roster hero on team 0 and an empty team 1:
// the shape where OWN and FAIR differ only inside the bot spawn seam.
struct SoccerSoloRosterWorld : SoccerPitch
{
    walker* hero = nullptr;

    explicit SoccerSoloRosterWorld(short guy_level)
        : SoccerPitch(kSoccerLevelA)
    {
        spawn_anchor(0, 96, 448);
        spawn_anchor(0, 96, 480);
        spawn_anchor(1, 528, 448);
        spawn_anchor(1, 528, 480);
        // E5: the opponent's fill is a turned wheel now (the stored 0 is
        // NONE and would leave the solo roster unopposed).
        world().ctf_requested_fill[1] = og::sim::kFillFair;
        hero = spawn_leveled_hero(FAMILY_SOLDIER, 0, 96, 96, 1, guy_level);
    }
};

}  // namespace

// Explicit FILL: FAIR (E5 — the stored default is NONE now and fields
// nothing) solves the solo roster's opponent at matched power AND matched
// headcount: one bot, not the old legacy five (D34).
TEST_F(ModesSoccer, explicit_fair_matches_a_solo_roster_opponent)
{
    SoccerSoloRosterWorld matched(4);
    matched.tick(1);
    ASSERT_TRUE(matched.soccer_active());

    EXPECT_EQ(1, alive_on_team(matched.world(), 1))
        << "the generated squad matches the solo headcount (D34/D39)";
    EXPECT_EQ(1, matched.var(kSlotMatchedSize));

    ASSERT_GT(matched.var(kSlotMatchedTarget), 0);
    const int code = matched_plan_code(matched.var(kSlotMatchedPlan), 1);
    ASSERT_NE(0, code) << "the matched twin solved team 1";
    EXPECT_EQ(0, code % 10) << "n = 1 admits no upgrades (D36)";
    const std::vector<int> matched_levels =
        team_levels_sorted(matched.world(), 1);
    ASSERT_EQ(1u, matched_levels.size());
    EXPECT_EQ(code / 10, matched_levels.front())
        << "the lone bot's level follows the stored plan";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// I5/E4 for soccer: a wiped MATCHED team's kickoff reprovision refills at
// the STORED (L*, k*) AND the stored headcount — identical strength and
// size, not the legacy formula (the solo-roster fixture makes this a 1v1,
// D34/D39).
TEST_F(ModesSoccer, kickoff_reprovisions_a_wiped_matched_team_at_strength)
{
    SoccerSoloRosterWorld fx(4);
    arm_matched(fx.world());
    fx.world().respawn_mode = 0;  // permadeath submenu
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    const std::vector<int> at_init = team_levels_sorted(fx.world(), 1);
    ASSERT_EQ(1u, at_init.size())
        << "the solo roster's matched opponent is a single bot (D34)";
    const int code = matched_plan_code(fx.var(kSlotMatchedPlan), 1);
    ASSERT_NE(0, code);
    ASSERT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));

    fx.thaw_kickoff();
    park_away(fx.hero);
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1)
            w->set_dead(1);
    }
    fx.tick(2);  // the corpses persist; their revive entries ride the queue
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));
    EXPECT_EQ(1, ai_entries_for_team(fx.world(), 1))
        << "always-on scheduling covers the matched bot too";
    // The D39 reprovision arm still guards the no-revives-in-flight edge
    // (entries genuinely lost, bodies swept — a full all-player queue):
    // drain the queue AND the drained corpses, then score.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);

    fx.world().mode.vars[kSocLastTouch1] = 1;  // team 0 touched last
    fx.set_ball(600, 464, 4 * 256, 0);         // into team 1's strip
    fx.tick(1);
    EXPECT_TRUE(has_notification(fx.events, "RED TEAM GOAL!"));
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the kickoff backstop refields the wiped matched team at the "
           "stored headcount (D39)";
    EXPECT_EQ(at_init, team_levels_sorted(fx.world(), 1))
        << "the replacement squad reproduces the stored (L*, k*) — never "
           "the legacy formula";
    EXPECT_EQ(code, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the plan is durable across the wipe";
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"))
        << "mid-match reprovision stays silent (§7)";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// E4/D24: a wiped HUMAN team whose corpses are all gone gets a squad
// matched to the STORED target AND the stored min-headcount — measured
// and solved at backstop time, never the legacy formula — and the solve
// stays silent mid-match. Two 1-guy human teams latch H = min(1, 1) = 1,
// so the replacement that faces the SURVIVING solo human is one bot —
// exactly why D34 mins instead of averaging.
TEST_F(ModesSoccer, kickoff_backstop_matches_a_wiped_human_team_to_target)
{
    SoccerPitch fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 448);
    fx.spawn_anchor(0, 96, 480);
    fx.spawn_anchor(1, 528, 448);
    fx.spawn_anchor(1, 528, 480);
    fx.world().ctf_requested_fill[1] = og::sim::kFillFair;  // E5: refield
    walker* red_hero = fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 96, 96, 1, 1);
    walker* green_hero =
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 1, 560, 96, 2, 1);
    arm_matched(fx.world());
    fx.world().respawn_mode = 0;
    fx.tick(1);
    ASSERT_TRUE(fx.soccer_active());
    // Both teams human: no fill, no plan, no announcement — but the
    // census stored T (two fresh soldiers -> mean 2306, derived §4.2)
    // and the min headcount (D34).
    ASSERT_EQ(2306, fx.var(kSlotMatchedTarget));
    ASSERT_EQ(1, fx.var(kSlotMatchedSize));
    ASSERT_EQ(0, fx.var(kSlotMatchedPlan));
    ASSERT_EQ(0, count_notifications(fx.events, "TEAMS MATCHED"));
    ASSERT_EQ(1, alive_on_team(fx.world(), 1));
    ASSERT_EQ(0, fx.var(kSlotSquadSeed))
        << "no bot squad spawned at init, so no squad-order draw yet (#235)";

    fx.thaw_kickoff();
    park_away(red_hero);
    // The green hero falls AND loses its guy record (a destroyed-corpse
    // stand-in): the engine sweeps the now-guyless corpse this tick.
    green_hero->set_dead(1);
    green_hero->clear_myguy();
    fx.tick(2);
    ASSERT_EQ(0, alive_on_team(fx.world(), 1));
    // The guyless corpse scheduled as a plain AI revive under the
    // always-on scan; the D24 measure-and-solve arm guards the
    // drained-queue edge (entries genuinely lost, bodies swept — a full
    // all-player queue) — clear the queue and the body so the backstop
    // refields.
    fx.world().respawn.respawn_queue.clear();
    retire_dead_guyless(fx.world(), 1);

    fx.world().mode.vars[kSocLastTouch1] = 1;
    fx.set_ball(600, 464, 4 * 256, 0);
    fx.tick(1);
    EXPECT_TRUE(has_notification(fx.events, "RED TEAM GOAL!"));
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the formerly-human team is refielded (today's behavior, D24) "
           "at the min headcount — the survivor stays un-outnumbered";
    // This first squad spawn of the match draws the #235 squad-order seed
    // (latched mid-match here — no fill happened at init), and the 1-prefix
    // (D37) is the seeded order's first member: with this world's
    // deterministic stream that is the MAGE, whose far weaker base solves
    // the stored T = 2306 to L3/k0 — visibly NOT the legacy L2 squad the
    // pre-matched backstop fielded. (Re-adjudicated for #235: the
    // pre-permutation prefix was always the soldier, which solved L1/k0.)
    EXPECT_GE(fx.var(kSlotSquadSeed), 1);
    EXPECT_LE(fx.var(kSlotSquadSeed), 120);
    const std::vector<int> squad_families =
        team_families_in_order(fx.world(), 1);
    ASSERT_EQ(1u, squad_families.size());
    EXPECT_EQ(FAMILY_MAGE, squad_families.front())
        << "the refield takes the seeded order's first member";
    EXPECT_EQ(30, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the backstop solved NOW against the stored target and "
           "persisted the plan";
    const std::vector<int> levels = team_levels_sorted(fx.world(), 1);
    ASSERT_EQ(1u, levels.size());
    EXPECT_EQ(3, levels.front());
    EXPECT_EQ(0, count_notifications(fx.events, "TEAMS MATCHED"))
        << "the mid-match D24 solve never announces (§7)";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
