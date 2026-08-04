// The Onslaught campaign-pack Lua (lib/mode_onslaught_impl.lua) behavior
// suite: every D5 rule as a sim case — init/activation, the damage-gate
// generator flip (lethal boundary, non-living/own-team/neutral arms, the
// 1 hp floor, the B6 post-flip guard window with its per-generator
// records and overflow degradation, the C2 classic-toast suppression
// arms), zero-generator grace elimination with
// recapture reset, waypoint holds (spawn-level + schedule-time respawn
// bonuses), fire_frequency spawn caps over the marked-spawn census,
// corpse-stain scrubbing, HUD, director roles, timeout tiebreaks,
// determinism and instruction budget headroom. Runs on the shared
// modes-pack harness (tests/modes_pack_fixture.h).

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

#include <cmath>
#include <cstdlib>
#include <format>
#include <string>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

// The mode-var slot map of lib/mode_onslaught_impl.lua (table S). A silent
// re-map in the Lua breaks these tests loudly.
enum OnsSlot : int {
    kOnsModeId = 0,
    kOnsRespawnTicks = 8,
    kOnsTeamMask = 9,
    kOnsTeamCount = 10,
    kOnsTimeLimit = 11,
    kOnsAnchorCursor = 12,
    kOnsGenCount = 13,    // +team
    kOnsZeroSince = 17,   // +team
    kOnsEliminated = 21,  // +team
    kOnsWpCount = 25,
    kOnsWpEntity = 26,    // +wp
    kOnsWpPos = 30,       // +wp, packed
    kOnsWpOwner1 = 34,    // +wp, holder + 1
    kOnsWpProgTeam1 = 38, // +wp
    kOnsWpProgress = 42,  // +wp
    kOnsSpawnCap = 46,    // +team byte
};

inline constexpr int kModeIdOnslaught = 3;  // mode_core.MODE.ONSLAUGHT
inline constexpr int kAiCadence = 15;
inline constexpr int kGraceTicks = 600;         // T.no_gen_grace_ticks
inline constexpr int kWpHoldTicks = 36;         // T.wp_hold_ticks
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

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool queue_front_type_is_goto(const walker* w)
{
    const statistics* s = w->stats();
    if (s == nullptr || s->commands.empty())
        return false;
    return s->commands.front().commandtype == COMMAND_GOTO;
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

// The first live marked generator-spawn on a team, if any.
walker* find_marked_spawn(GameWorld& world, int team)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->stats() != nullptr &&
            w->stats()->query_bit_flags(kSpawnMarkBit))
        {
            return w;
        }
    }
    return nullptr;
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
        digest += std::format("[id={} x={} y={} dead={} team={} act={}",
                              w->entity_id(), w->xpos(), w->ypos(),
                              w->dead() ? 1 : 0,
                              static_cast<int>(w->team_num()),
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

using ModesOnslaught = ModesPackTest;

namespace {

// Two-team foundry on kOnsLevelA: two tents per team, anchors, one parked
// (ACT_CONTROL, undirected) living per team. Caps: 3/3 (+2 on neutral 7).
struct OnsWorld : ModesCtfWorld
{
    walker* red_gen_a = nullptr;
    walker* red_gen_b = nullptr;
    walker* green_gen = nullptr;
    walker* red = nullptr;
    walker* green = nullptr;

    explicit OnsWorld(int level_id = kOnsLevelA) : ModesCtfWorld(level_id)
    {
        spawn_anchor(0, 96, 128);
        spawn_anchor(0, 96, 160);
        spawn_anchor(1, 528, 128);
        spawn_anchor(1, 528, 160);
        red_gen_a = spawn_generator(FAMILY_TENT, 0, 128, 320);
        red_gen_b = spawn_generator(FAMILY_TENT, 0, 128, 640);
        green_gen = spawn_generator(FAMILY_TENT, 1, 480, 480);
        red = spawn_living(FAMILY_SOLDIER, 0, 96, 96);
        green = spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    }

    bool ons_active() const { return var(kOnsModeId) == kModeIdOnslaught; }

    // A lethal C++ melee hit through the real attack path (the damage gate
    // dispatches the Lua on_damage).
    void smash(walker* attacker, walker* target, float staged = 5000.0f)
    {
        attacker->set_damage(staged);
        attacker->attack(target);
    }
};

}  // namespace

// ===========================================================================
// Activation / init
// ===========================================================================

TEST_F(ModesOnslaught, init_activates_generator_teams_and_banks_caps)
{
    OnsWorld fx;
    walker* wp = fx.spawn_point(point_family_, 320, 320);
    ASSERT_NE(nullptr, wp);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_TRUE(fx.ons_active());
    EXPECT_EQ(3, fx.var(kOnsTeamMask));
    EXPECT_EQ(2, fx.var(kOnsTeamCount));
    EXPECT_EQ(120, fx.var(kOnsRespawnTicks));
    EXPECT_EQ(14400, fx.var(kOnsTimeLimit));
    EXPECT_EQ(2, fx.team_var(kOnsGenCount, 0));
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 1));
    EXPECT_EQ(0, fx.team_var(kOnsZeroSince, 0));
    EXPECT_EQ(3, fx.team_var(kOnsSpawnCap, 0));
    EXPECT_EQ(3, fx.team_var(kOnsSpawnCap, 1));
    EXPECT_EQ(2, fx.team_var(kOnsSpawnCap, 7));
    EXPECT_EQ(-1, fx.team_var(kOnsSpawnCap, 2)) << "holes stay uncapped";
    EXPECT_EQ(1, fx.var(kOnsWpCount));
    EXPECT_EQ(pos_pack(320, 320), fx.var(kOnsWpPos));
    EXPECT_EQ(0, fx.var(kOnsWpOwner1)) << "waypoints start neutral";
    EXPECT_STREQ("ONSLAUGHT", fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(fx.events, "ONSLAUGHT! HOLD THE LINE"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, init_below_two_teams_demotes)
{
    ModesCtfWorld fx(kOnsLevelA);
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "fewer than two teams"));
    fx.tick(5);
    EXPECT_FALSE(fx.world().mode.active) << "the demotion is latched";
}

TEST_F(ModesOnslaught, registration_without_a_manifest_row_demotes)
{
    ModesCtfWorld fx(9409);  // bound via make_hooks(nil) in the fixture
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "no manifest row"));
}

TEST_F(ModesOnslaught, genless_active_team_starts_its_grace_clock)
{
    ModesCtfWorld fx(kOnsLevelA);
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);  // livings, no generator
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0, fx.team_var(kOnsZeroSince, 0));
    EXPECT_GT(fx.team_var(kOnsZeroSince, 1), 0)
        << "a genless team is on the clock from init";
}

// ===========================================================================
// The D5 flip (on_damage)
// ===========================================================================

TEST_F(ModesOnslaught, lethal_hit_flips_the_generator_intact)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(140, 340);  // adjacent to red_gen_a
    const float max_hp = fx.red_gen_a->stats()->max_hitpoints();
    fx.smash(fx.green, fx.red_gen_a);

    EXPECT_FALSE(fx.red_gen_a->dead()) << "generators never die (D5)";
    EXPECT_EQ(1, fx.red_gen_a->team_num());
    EXPECT_EQ(1, fx.red_gen_a->real_team_num());
    EXPECT_EQ(max_hp, fx.red_gen_a->stats()->hitpoints())
        << "the flip restores full HP";
    EXPECT_TRUE(has_notification(fx.events, "GREEN TAKES A TENT!"));
    fx.tick(1);
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 0));
    EXPECT_EQ(2, fx.team_var(kOnsGenCount, 1));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, lethal_boundary_is_exact)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(140, 340);
    // A staged 3.0 makes the attack spread deterministic (base damage
    // d - sqrt(d)/2 + rng(floor(sqrt(d))), and next(1) is always 0), so
    // the gate amount is exactly computable from the generator's armor.
    const float base = 3.0f - std::sqrt(3.0f) / 2.0f;
    const float reduction =
        std::min(fx.red_gen_a->stats()->armor() / 2.0f, base - 1.0f);
    const auto amount = static_cast<short>(base - reduction);
    ASSERT_GE(amount, 1);

    // One point over the amount: damage lands (down to 1 hp), no flip.
    fx.red_gen_a->stats()->set_hitpoints(static_cast<float>(amount + 1));
    fx.smash(fx.green, fx.red_gen_a, 3.0f);
    EXPECT_EQ(0, fx.red_gen_a->team_num());
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints());

    // hp exactly equal to the amount: lethal, the flip fires.
    fx.red_gen_a->stats()->set_hitpoints(static_cast<float>(amount));
    fx.smash(fx.green, fx.red_gen_a, 3.0f);
    EXPECT_EQ(1, fx.red_gen_a->team_num());
    EXPECT_EQ(fx.red_gen_a->stats()->max_hitpoints(),
              fx.red_gen_a->stats()->hitpoints());
}

TEST_F(ModesOnslaught, unowned_weapon_lethal_hit_floors_at_one_hp)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* shot = fx.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, shot);
    shot->setxy(140, 340);
    shot->set_team_num(1);  // attack() refuses friendly fire before the gate
    shot->set_damage(5000.0f);
    shot->attack(fx.red_gen_a);

    EXPECT_FALSE(fx.red_gen_a->dead());
    EXPECT_EQ(0, fx.red_gen_a->team_num()) << "no living attacker, no flip";
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints())
        << "the 1 hp floor holds";
    // A second overkill hit cannot push it below the floor.
    shot->set_damage(5000.0f);
    shot->attack(fx.red_gen_a);
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints());
    EXPECT_FALSE(fx.red_gen_a->dead());
}

TEST_F(ModesOnslaught, non_active_team_attacker_floors_instead_of_flipping)
{
    // A living outside the active mask (team 3 on a {0, 1} match) is not
    // a score attacker: the hit floors at 1 hp instead of flipping. (The
    // same-team arm shares this branch; attack() itself refuses friendly
    // fire before the gate, so it can only matter for exotic dispatches.)
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* outsider = fx.spawn_living(FAMILY_SOLDIER, 3, 140, 340);
    fx.smash(outsider, fx.red_gen_a);

    EXPECT_EQ(0, fx.red_gen_a->team_num()) << "no flip for a non-score team";
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints())
        << "floored, not healed";
    EXPECT_FALSE(fx.red_gen_a->dead());
}

TEST_F(ModesOnslaught, guard_window_clamps_the_same_tick_flip_back)
{
    // The B6 ping-pong killer: a generator that just flipped cannot flip
    // again for flip_guard_ticks — the lethal hit lands on the 1 hp floor
    // arm instead, silently (C1).
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(140, 340);
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    fx.smash(red_hitter, fx.red_gen_a);

    EXPECT_EQ(1, fx.red_gen_a->team_num())
        << "a fresh flip cannot flip back inside the guard window (B6)";
    EXPECT_FALSE(fx.red_gen_a->dead());
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints())
        << "the guarded lethal hit clamps to the 1 hp floor";
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN TAKES A TENT!"));
    EXPECT_EQ(0, count_notifications(fx.events, "RED TAKES A TENT!"))
        << "no notification for a guard-clamped hit (C1)";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, flip_guard_expires_after_96_ticks)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(140, 340);
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 340);
    fx.smash(fx.green, fx.red_gen_a);  // flip recorded at world tick 1
    ASSERT_EQ(1, fx.red_gen_a->team_num());

    // World tick 96: elapsed 95 < 96, the last window tick still guards.
    fx.tick(95);
    fx.smash(red_hitter, fx.red_gen_a);
    EXPECT_EQ(1, fx.red_gen_a->team_num()) << "still guarded at elapsed 95";
    EXPECT_EQ(1.0f, fx.red_gen_a->stats()->hitpoints());

    // World tick 97: elapsed 96 >= 96, the guard expires on time.
    fx.tick(1);
    fx.smash(red_hitter, fx.red_gen_a);
    EXPECT_EQ(0, fx.red_gen_a->team_num()) << "guard expired: contested again";
    EXPECT_EQ(fx.red_gen_a->stats()->max_hitpoints(),
              fx.red_gen_a->stats()->hitpoints());
    EXPECT_EQ(1, count_notifications(fx.events, "RED TAKES A TENT!"));
}

TEST_F(ModesOnslaught, flip_guard_is_per_generator)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // Entity-id ranks: red_gen_a = 0 (low-packed), red_gen_b = 1
    // (high-packed in the same guard slot).
    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    // The sibling generator carries no record: it flips at once.
    fx.green->setxy(140, 660);
    fx.smash(fx.green, fx.red_gen_b);
    EXPECT_EQ(1, fx.red_gen_b->team_num())
        << "the guard keys on the flipped generator only";
    EXPECT_EQ(2, count_notifications(fx.events, "GREEN TAKES A TENT!"));
    // And the high-packed record now guards its own generator.
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 660);
    fx.smash(red_hitter, fx.red_gen_b);
    EXPECT_EQ(1, fx.red_gen_b->team_num()) << "rank-1 record guards rank 1";
    EXPECT_EQ(1.0f, fx.red_gen_b->stats()->hitpoints());
}

TEST_F(ModesOnslaught, guard_table_overflow_degrades_to_unguarded)
{
    // 18 extra tents raise the census to 21 generators; the highest
    // entity id ranks 20, past the 20-entry guard table, and degrades
    // gracefully to the pre-B6 unguarded behavior (no shipped map fields
    // more than 12 generators).
    OnsWorld fx;
    walker* last = nullptr;
    for (int i = 0; i < 18; ++i)
    {
        last = fx.spawn_generator(FAMILY_TENT, 0,
                                  static_cast<short>(128 + 16 * i), 480);
    }
    ASSERT_NE(nullptr, last);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(static_cast<short>(last->xpos() + 12),
                    static_cast<short>(last->ypos() + 20));
    fx.smash(fx.green, last);
    ASSERT_EQ(1, last->team_num()) << "past the table a flip still fires";
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 480);
    fx.smash(red_hitter, last);
    EXPECT_EQ(0, last->team_num())
        << "unguarded past the table: the accepted degradation arm";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, neutral_generator_is_capturable)
{
    OnsWorld fx;
    walker* bones = fx.spawn_generator(FAMILY_BONES, 7, 320, 480);
    ASSERT_NE(nullptr, bones);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    EXPECT_FALSE(bones->dead()) << "neutral generators survive the strip";
    fx.red->setxy(332, 500);
    fx.smash(fx.red, bones);

    EXPECT_EQ(0, bones->team_num()) << "neutral post joins the attacker";
    EXPECT_TRUE(has_notification(fx.events, "RED TAKES A BONES!"));
    fx.tick(1);
    EXPECT_EQ(3, fx.team_var(kOnsGenCount, 0));
}

TEST_F(ModesOnslaught, flip_keeps_existing_spawns_on_their_old_team)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* veteran = fx.spawn_living(FAMILY_SKELETON, 0, 250, 320, ACT_GUARD);
    veteran->stats()->set_bit_flags(kSpawnMarkBit, 1);
    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);

    ASSERT_EQ(1, fx.red_gen_a->team_num());
    EXPECT_EQ(0, veteran->team_num())
        << "a captured post's old spawns fight on as a remnant";
}

// ===========================================================================
// Grace elimination + recapture
// ===========================================================================

TEST_F(ModesOnslaught, zero_generator_grace_eliminates_after_600_ticks)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // Capture green's only generator (simulated flip: live team change).
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    const std::int32_t since = fx.team_var(kOnsZeroSince, 1);
    ASSERT_GT(since, 0) << "the grace clock starts";

    // One tick before the deadline: still alive.
    while (static_cast<std::int32_t>(fx.world().tick_count_) <
           since + kGraceTicks - 1)
        fx.tick(1);
    EXPECT_EQ(0, fx.team_var(kOnsEliminated, 1));
    EXPECT_FALSE(fx.world().game_ended);

    fx.tick(2);
    EXPECT_EQ(1, fx.team_var(kOnsEliminated, 1));
    EXPECT_TRUE(has_notification(fx.events, "GREEN FALLS!"));
    EXPECT_TRUE(fx.green->dead()) << "elimination kills the team's livings";
    EXPECT_TRUE(fx.world().game_ended) << "last team standing wins";
    EXPECT_EQ(0, fx.world().mode.winner_team);
    EXPECT_TRUE(has_notification(fx.events, "RED TEAM WINS!"));
    fx.tick(3);
    EXPECT_TRUE(fx.world().game_ended) << "win shape re-asserts every tick";
}

TEST_F(ModesOnslaught, recapturing_a_generator_resets_the_grace_clock)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    ASSERT_GT(fx.team_var(kOnsZeroSince, 1), 0);
    fx.tick(300);

    // Green takes a post back before the deadline.
    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    fx.tick(1);
    EXPECT_EQ(0, fx.team_var(kOnsZeroSince, 1)) << "grace clock cleared";
    fx.tick(kGraceTicks);
    EXPECT_EQ(0, fx.team_var(kOnsEliminated, 1));
    EXPECT_FALSE(fx.world().game_ended);
}

TEST_F(ModesOnslaught, three_team_match_survives_one_elimination)
{
    ModesCtfWorld fx(kOnsLevelB);
    walker* gen0 = fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_generator(FAMILY_TENT, 2, 320, 800);
    (void)gen0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    walker* green = fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.spawn_living(FAMILY_SOLDIER, 2, 320, 860);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(7, fx.var(kOnsTeamMask));

    // Green loses its only post; the deadline passes.
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && w->query_order() == Order::Generator &&
            w->team_num() == 1)
        {
            w->set_team_num(2);
            w->set_real_team_num(2);
        }
    }
    fx.tick(kGraceTicks + 5);

    EXPECT_EQ(1, fx.team_var(kOnsEliminated, 1));
    EXPECT_TRUE(green->dead());
    EXPECT_FALSE(fx.world().game_ended)
        << "two teams remain: the match continues";
    EXPECT_STREQ("GREEN OUT", fx.world().mode.hud[1].text.data());
}

// ===========================================================================
// Waypoint holds
// ===========================================================================

TEST_F(ModesOnslaught, majority_hold_takes_the_waypoint_at_36_ticks)
{
    OnsWorld fx;
    walker* wp = fx.spawn_point(point_family_, 320, 320);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // 2 red vs 1 green inside the 48 px disc: red holds the strict
    // majority (2 of 3).
    fx.spawn_living(FAMILY_SOLDIER, 0, 300, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 340, 320);
    walker* contester = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 300);
    (void)contester;
    fx.tick(kWpHoldTicks - 1);
    EXPECT_EQ(0, fx.var(kOnsWpOwner1)) << "not yet";
    fx.tick(1);

    EXPECT_EQ(1, fx.var(kOnsWpOwner1)) << "red holds the waypoint";
    EXPECT_EQ(0, wp->team_num()) << "the marker entity wears the color";
    EXPECT_TRUE(has_notification(fx.events, "RED HOLDS WAYPOINT!"));
}

TEST_F(ModesOnslaught, losing_the_majority_clears_the_meter)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* holder = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 320);
    fx.tick(10);
    ASSERT_GT(fx.var(kOnsWpProgress), 0);
    holder->setxy(96, 700);  // walks away mid-capture
    fx.tick(1);
    EXPECT_EQ(0, fx.var(kOnsWpProgress)) << "no majority, no meter";
    EXPECT_EQ(0, fx.var(kOnsWpProgTeam1));
}

TEST_F(ModesOnslaught, holding_team_spawns_one_level_higher)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    fx.world().generator_rate = 2000;  // 20x spawn cadence for the wait loop
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.world().mode.vars[kOnsWpOwner1] = 1;  // red holds

    walker* spawn = nullptr;
    for (int i = 0; i < 4000 && spawn == nullptr; ++i)
    {
        fx.tick(1);
        spawn = find_marked_spawn(fx.world(), 0);
    }
    ASSERT_NE(nullptr, spawn) << "a level-1 tent must fire within the window";
    EXPECT_TRUE(spawn->stats()->query_bit_flags(kSpawnMarkBit))
        << "customize_spawn marks every generator spawn";
    EXPECT_EQ(2, spawn->stats()->level())
        << "level-1 tent spawns level 1 + the held-waypoint bonus";
}

TEST_F(ModesOnslaught, unheld_team_spawns_at_the_authored_level)
{
    OnsWorld fx;
    fx.world().generator_rate = 2000;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    walker* spawn = nullptr;
    for (int i = 0; i < 4000 && spawn == nullptr; ++i)
    {
        fx.tick(1);
        spawn = find_marked_spawn(fx.world(), 0);
    }
    ASSERT_NE(nullptr, spawn);
    EXPECT_EQ(1, spawn->stats()->level()) << "no waypoint, no bonus";
}

TEST_F(ModesOnslaught, holding_halves_the_hero_respawn_delay_at_schedule)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 160, 128, 7);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    // Unheld: the full 120-tick delay is booked.
    hero->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1u, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(120, fx.world().respawn.respawn_queue.front().ticks_left);
    fx.tick(125);
    ASSERT_FALSE(hero->dead());

    // Held: the delay halves at schedule time.
    fx.world().mode.vars[kOnsWpOwner1] = 1;
    hero->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1u, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(60, fx.world().respawn.respawn_queue.front().ticks_left)
        << "the held-waypoint bonus applies when the entry is booked";
}

// ===========================================================================
// Spawn caps (fire_frequency toggling)
// ===========================================================================

TEST_F(ModesOnslaught, census_at_the_cap_pauses_and_resumes_generators)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    const float authored = fx.red_gen_a->fire_frequency();

    // Three marked red spawns == the cap for team 0.
    walker* s1 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 320, ACT_GUARD);
    walker* s2 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 350, ACT_GUARD);
    walker* s3 = fx.spawn_living(FAMILY_SKELETON, 0, 250, 380, ACT_GUARD);
    s1->stats()->set_bit_flags(kSpawnMarkBit, 1);
    s2->stats()->set_bit_flags(kSpawnMarkBit, 1);
    s3->stats()->set_bit_flags(kSpawnMarkBit, 1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_GE(fx.red_gen_a->fire_frequency(),
              static_cast<float>(kPauseFireFrequency));
    EXPECT_GE(fx.red_gen_b->fire_frequency(),
              static_cast<float>(kPauseFireFrequency));
    EXPECT_LT(fx.green_gen->fire_frequency(),
              static_cast<float>(kPauseFireFrequency))
        << "green is under its own cap";

    // Killing spawns re-opens the tap; a paused-fire busy residue clamps.
    s1->set_dead(1);
    s2->set_dead(1);
    fx.red_gen_a->set_busy(20000.0f);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(authored, fx.red_gen_a->fire_frequency());
    EXPECT_LE(fx.red_gen_a->busy(), authored)
        << "resume clamps the pause-sized busy residue";
}

// ===========================================================================
// Corpse-stain scrub
// ===========================================================================

TEST_F(ModesOnslaught, bot_corpses_are_scrubbed_heroes_are_not)
{
    OnsWorld fx;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 480, 96, 9);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    // A real kill through the attack path drops the stain, then the
    // on_entity_death hook scrubs it (bots never respawn here).
    const int bx = fx.green->xpos();
    const int by = fx.green->ypos();
    fx.red->setxy(static_cast<short>(bx - 20), static_cast<short>(by));
    fx.smash(fx.red, fx.green);
    ASSERT_TRUE(fx.green->dead());
    fx.tick(1);
    EXPECT_FALSE(stain_alive_at(fx.world(), bx, by))
        << "the bot's fresh stain is scrubbed";

    // The hero corpse is NOT hook-scrubbed: it enters the respawn queue
    // instead (og.respawn_schedule scrubs scheduled corpses engine-side,
    // so the distinguishing observable is the booked revive).
    const int hx = hero->xpos();
    const int hy = hero->ypos();
    fx.red->setxy(static_cast<short>(hx - 20), static_cast<short>(hy));
    fx.smash(fx.red, hero);
    ASSERT_TRUE(hero->dead());
    fx.tick(1);
    bool hero_booked = false;
    for (const auto& entry : fx.world().respawn.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == hero->entity_id())
            hero_booked = true;
    }
    EXPECT_TRUE(hero_booked) << "hero corpses respawn instead of scrubbing";
}

// ===========================================================================
// C2: the classic "All foes defeated!" toast stays off mode-owned levels
// ===========================================================================

TEST_F(ModesOnslaught, momentary_wipe_emits_no_classic_foes_toast)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // green is the last cross-team living: killing it used to fire the
    // classic completion toast mid-match while the mode played on (C2).
    fx.red->setxy(static_cast<short>(fx.green->xpos() - 20),
                  fx.green->ypos());
    fx.smash(fx.red, fx.green);
    ASSERT_TRUE(fx.green->dead());
    EXPECT_EQ(0, count_notifications(fx.events, "All foes defeated!"))
        << "mode-owned levels suppress the classic completion toast";
    fx.tick(1);
    EXPECT_FALSE(fx.world().game_ended)
        << "the match itself continues past the momentary wipe";
}

TEST_F(ModesOnslaught, classic_worlds_keep_the_foes_toast)
{
    ModesCtfWorld fx(kOnsLevelA);
    fx.world().type = 0;  // classic completion rules own this world
    walker* red = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    walker* green = fx.spawn_living(FAMILY_SOLDIER, 1, 128, 96);
    red->set_damage(5000.0f);
    red->attack(green);
    ASSERT_TRUE(green->dead());
    EXPECT_EQ(1, count_notifications(fx.events, "All foes defeated!"))
        << "classic worlds keep the classic toast";
}

TEST_F(ModesOnslaught, demoted_scripted_worlds_keep_the_foes_toast)
{
    // A scripted map whose init demoted (one score team) falls back to
    // classic rules — including the completion toast.
    ModesCtfWorld fx(kOnsLevelA);
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    walker* red = fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    walker* wild = fx.spawn_living(FAMILY_SOLDIER, 4, 128, 96);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.init_attempted);
    ASSERT_FALSE(fx.world().mode.active);
    red->set_damage(5000.0f);
    red->attack(wild);
    ASSERT_TRUE(wild->dead());
    EXPECT_EQ(1, count_notifications(fx.events, "All foes defeated!"))
        << "a demoted scripted map keeps classic completion semantics";
}

// ===========================================================================
// Eliminated teams stay down
// ===========================================================================

TEST_F(ModesOnslaught, eliminated_team_late_revives_are_rekilled)
{
    ModesCtfWorld fx(kOnsLevelB);
    fx.spawn_anchor(1, 528, 128);
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 2, 320, 800);
    walker* green_gen = fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 528, 96, 7);
    fx.spawn_living(FAMILY_SOLDIER, 2, 320, 860);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    // The hero dies and books its 120-tick revive; then green is
    // eliminated (clock forced past the deadline) before it fires.
    hero->set_dead(1);
    fx.tick(1);
    ASSERT_EQ(1u, fx.world().respawn.respawn_queue.size());
    green_gen->set_team_num(2);
    green_gen->set_real_team_num(2);
    fx.tick(1);
    fx.world().mode.vars[kOnsZeroSince + 1] =
        static_cast<std::int32_t>(fx.world().tick_count_) - kGraceTicks;
    fx.tick(1);
    ASSERT_EQ(1, fx.team_var(kOnsEliminated, 1));
    ASSERT_FALSE(fx.world().game_ended) << "three-team match continues";

    fx.tick(130);  // past the revive window
    EXPECT_TRUE(hero->dead())
        << "an eliminated team's late revive is re-killed";
    EXPECT_EQ(0, alive_on_team(fx.world(), 1));
}

// ===========================================================================
// HUD
// ===========================================================================

TEST_F(ModesOnslaught, hud_rows_carry_counts_and_the_waypoint_tag)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    EXPECT_STREQ("RED 2 GEN", fx.world().mode.hud[0].text.data());
    EXPECT_STREQ("GREEN 1 GEN", fx.world().mode.hud[1].text.data());
    EXPECT_EQ(0, fx.world().mode.hud[0].team);

    fx.world().mode.vars[kOnsWpOwner1] = 1;
    fx.tick(1);
    EXPECT_STREQ("RED 2 GEN +WP", fx.world().mode.hud[0].text.data());
}

// ===========================================================================
// Director
// ===========================================================================

TEST_F(ModesOnslaught, attackers_walk_the_nearest_enemy_generator)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // Two directable members: oblist order makes the first the defender,
    // the second the attacker.
    walker* defender = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 640, ACT_SIT);
    walker* attacker = fx.spawn_living(FAMILY_SOLDIER, 0, 300, 480, ACT_SIT);
    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_TRUE(front_command_is(attacker, COMMAND_GOTO,
                                 fx.green_gen->xpos(), fx.green_gen->ypos()))
        << "attacker heads for the nearest enemy post";
    EXPECT_TRUE(front_command_is(defender, COMMAND_GOTO,
                                 fx.red_gen_a->xpos(), fx.red_gen_a->ypos()))
        << "defender leashes back to its own post";
    EXPECT_EQ(fx.red_gen_a->entity_id(), defender->leader_id())
        << "defender leads on its post (auto-foe suppression)";

    // Adjacency turns the walk into an attack.
    attacker->setxy(static_cast<short>(fx.green_gen->xpos() - 20),
                    static_cast<short>(fx.green_gen->ypos()));
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(fx.green_gen->entity_id(), attacker->foe_id())
        << "adjacent attacker sets the generator as its foe";

    // Every red post flips away: the ex-defender re-partitions as an
    // attacker and its stale generator leader is cleared.
    fx.red_gen_a->set_team_num(1);
    fx.red_gen_b->set_team_num(1);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(0u, defender->leader_id())
        << "role change clears the director-set generator leader";
    EXPECT_TRUE(queue_front_type_is_goto(defender));
}

TEST_F(ModesOnslaught, an_unheld_waypoint_gets_a_holder)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* near_wp = fx.spawn_living(FAMILY_SOLDIER, 0, 340, 360, ACT_SIT);
    fx.spawn_living(FAMILY_SOLDIER, 0, 300, 640, ACT_SIT);
    align_before_cadence(fx.world());
    fx.tick(1);

    EXPECT_TRUE(front_command_is(near_wp, COMMAND_GOTO, 320, 320))
        << "the nearest member walks the unheld waypoint";
}

TEST_F(ModesOnslaught, director_never_touches_player_walkers)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // red/green are ACT_CONTROL (players): the director must leave them.
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_TRUE(fx.red->stats()->commands.empty());
    EXPECT_TRUE(fx.green->stats()->commands.empty());
}

// ===========================================================================
// Timeout
// ===========================================================================

TEST_F(ModesOnslaught, timeout_picks_leader_with_tiebreakers)
{
    // Generator lead wins.
    {
        OnsWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.ons_active());
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team) << "2 posts beat 1";
    }
    // Counts tied: larger m_score wins.
    {
        OnsWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.ons_active());
        // Move the spare red post out of the mask: counts tie at 1-1.
        fx.red_gen_b->set_team_num(2);
        fx.red_gen_b->set_real_team_num(2);
        fx.world().m_score[1] = 900;
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(1, fx.world().mode.winner_team);
    }
    // Full tie: the lower team byte wins.
    {
        OnsWorld fx;
        fx.tick(1);
        ASSERT_TRUE(fx.ons_active());
        fx.red_gen_b->set_team_num(2);
        fx.red_gen_b->set_real_team_num(2);
        fx.world().set_level_tick_count(14400 - 2);
        fx.tick(2);
        EXPECT_TRUE(fx.world().game_ended);
        EXPECT_EQ(0, fx.world().mode.winner_team);
    }
}

TEST_F(ModesOnslaught, short_manifest_time_limit_is_honored)
{
    ModesCtfWorld fx(kOnsLevelC);  // row carries time_limit = 120
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(120, fx.var(kOnsTimeLimit));
    fx.tick(125);
    EXPECT_TRUE(fx.world().game_ended) << "manifest time limit decides";
}

// ===========================================================================
// Registration through the shipped manifest
// ===========================================================================

TEST_F(ModesOnslaught, shipped_manifest_registers_the_onslaught_levels)
{
    // scripts/mode_onslaught.lua scans the committed manifest: scen800
    // binds with FOUNDRY LINE's caps.
    ModesCtfWorld fx(800);
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_living(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(kModeIdOnslaught, fx.var(kOnsModeId));
    EXPECT_EQ(24, fx.team_var(kOnsSpawnCap, 0));
    EXPECT_EQ(24, fx.team_var(kOnsSpawnCap, 1));
    EXPECT_EQ(14400, fx.var(kOnsTimeLimit));
}

// ===========================================================================
// Determinism + instruction budget
// ===========================================================================

namespace {

std::string run_ons_match_digest(int point_family, int ticks)
{
    OnsWorld fx;
    fx.spawn_point(point_family, 320, 320);
    fx.world().generator_rate = 1000;
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 300, ACT_GUARD);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 340, ACT_GUARD);
    fx.spawn_living(FAMILY_SOLDIER, 1, 440, 300, ACT_GUARD);
    fx.spawn_living(FAMILY_SOLDIER, 1, 440, 340, ACT_GUARD);
    fx.tick(ticks);
    EXPECT_TRUE(fx.world().mode.active);
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesOnslaught, directed_generator_war_is_deterministic)
{
    const std::string first = run_ons_match_digest(point_family_, 400);
    const std::string second = run_ons_match_digest(point_family_, 400);
    ASSERT_EQ(first, second)
        << "same seed + same map must reproduce spawns, roles and flips";
}

TEST_F(ModesOnslaught, full_mode_tick_fits_a_tenth_of_the_instruction_budget)
{
    og::script::g_test_world_instruction_budget = 500000;
    {
        OnsWorld fx;
        fx.spawn_point(point_family_, 320, 320);
        fx.spawn_point(point_family_, 320, 640);
        fx.world().generator_rate = 1000;
        fx.spawn_living(FAMILY_SOLDIER, 0, 200, 300, ACT_GUARD);
        fx.spawn_living(FAMILY_SOLDIER, 1, 440, 300, ACT_GUARD);
        fx.tick(1);  // init under the budget
        ASSERT_TRUE(fx.world().mode.active);
        fx.tick(45);  // 3 director cadences + census + waypoints + HUD
        EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
            << "a 10x-reduced budget must never trip";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
    og::script::g_test_world_instruction_budget = 0;
}

// ===========================================================================
// Client mirror replication
// ===========================================================================

// Companion to the soccer case. Soccer desynced because its Lua-spawned ball
// carries a class-pack family byte (>= NUM_FAMILIES) that apply_snapshot used
// to clamp to 0. This mode spawns no pack-family entity, so it was never hit
// -- pin that, so a future mode entity that DOES reach for one fails here
// instead of in a player's match.

TEST_F(ModesOnslaught, match_replicates_to_a_client_mirror_without_hash_strikes)
{
    OnsWorld fx;
    fx.spawn_point(point_family_, 320, 320);
    ModeMirror mirror(kOnsLevelA);

    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick;
    ASSERT_TRUE(fx.ons_active());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(fx.world().mode.vars[slot], mirror.world().mode.vars[slot])
            << "mode var slot " << slot;
    }
}
