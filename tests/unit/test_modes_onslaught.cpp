// The Onslaught campaign-pack Lua (lib/mode_onslaught_impl.lua) behavior
// suite: every D5 rule as a sim case — init/activation, the damage-gate
// generator flip (lethal boundary, non-living/own-team/neutral arms, the
// 1 hp floor, the init max_hp normalization + authored-hp flip restore
// with the <= 1 hp floor cancel (INVESTIGATION-3), the B6 post-flip
// guard window with its per-generator
// records and overflow degradation, the C2 classic-toast suppression
// arms), zero-generator grace elimination with
// recapture reset and its HUD countdown + one-shot warning, waypoint holds (spawn-level + schedule-time respawn
// bonuses), fire_frequency spawn caps over the marked-spawn census,
// corpse-stain scrubbing, HUD, director roles, timeout tiebreaks,
// determinism and instruction budget headroom. Runs on the shared
// modes-pack harness (tests/modes_pack_fixture.h).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include "../modes_pack_fixture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>
#include <tuple>
#include <vector>

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

TEST_F(ModesOnslaught, scenario_troops_strip_spares_the_foundries)
{
    // Onslaught is the one mode that passes keep_generators to the shared
    // strip: its generators ARE the board, so "strip ALL" clears the
    // authored infantry and leaves every post standing.
    OnsWorld fx;
    fx.world().ctf_requested_strip_scenario_troops = 2;
    walker* wildlife = fx.spawn_living(FAMILY_ORC, 5, 320, 700);
    ASSERT_NE(nullptr, wildlife);
    fx.tick(1);

    ASSERT_TRUE(fx.ons_active());
    EXPECT_TRUE(fx.red->dead()) << "STRIP_ALL takes the authored livings";
    EXPECT_TRUE(fx.green->dead());
    EXPECT_TRUE(wildlife->dead()) << "\"ALL\" reaches wildlife too";
    EXPECT_FALSE(fx.red_gen_a->dead()) << "keep_generators spares the posts";
    EXPECT_FALSE(fx.red_gen_b->dead());
    EXPECT_FALSE(fx.green_gen->dead());
    EXPECT_EQ(2, fx.team_var(kOnsGenCount, 0)) << "the census still counts them";
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 1));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, troops_own_at_auto_activates_the_authored_team_count)
{
    // The scen-841 shape on Onslaught at TEAMS: Auto: Auto is the zero
    // sentinel ("as many teams as the map actually has"), so three
    // authored generator teams with rosters on 0 and 2 under OWN all
    // activate — the unrostered team keeps its foundry (keep_generators)
    // and self-populates from it mid-match (D17: no init bot squads), the
    // rosters stay untouched (issue #218; the 2026-08-18 directive
    // superseding D26's Auto scope).
    ModesCtfWorld fx(kOnsLevelB);
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_anchor(0, 96, 128);
    fx.spawn_anchor(1, 528, 128);
    fx.spawn_anchor(2, 96, 800);
    walker* gen0 = fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    walker* gen1 = fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    walker* gen2 = fx.spawn_generator(FAMILY_TENT, 2, 128, 640);
    walker* soldier = fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 1);
    walker* barbarian = fx.spawn_hero(FAMILY_BARBARIAN, 2, 96, 760, 2);
    fx.tick(1);

    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1 + 2 + 4, fx.var(kOnsTeamMask))
        << "Auto resolves to the authored team count: all three sides";
    EXPECT_FALSE(gen0->dead()) << "active teams keep their foundries";
    EXPECT_FALSE(gen2->dead());
    EXPECT_FALSE(gen1->dead())
        << "the backfilled team keeps its foundry too (keep_generators)";
    EXPECT_FALSE(soldier->dead());
    EXPECT_FALSE(barbarian->dead());
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 0));
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 1));
    EXPECT_EQ(1, fx.team_var(kOnsGenCount, 2));
    EXPECT_EQ(1, alive_on_team(fx.world(), 0));
    EXPECT_EQ(0, alive_on_team(fx.world(), 1))
        << "no init infantry under OWN (D17) — the foundry fields it later";
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// Matched teams D17/E8 (amendment re-ruling): the matched request now
// arrives via the strip field (TROOPS: FAIR), so Onslaught runs its strip
// with keep_generators — the foundries stay — and ignores matched POWER
// entirely: no plan is solved, nothing announces, and the generators field
// the same armies as the OWN twin (D33(a): FAIR's twin is OWN, the strip
// states being identical apart from power). Only the shared census (now
// match.bank_match_target, run by every mode's apply) latches a target.
TEST_F(ModesOnslaught, matched_request_masks_like_own_and_ignores_power)
{
    auto author = [](ModesCtfWorld& fx) {
        fx.spawn_anchor(0, 96, 128);
        fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
        fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
        fx.spawn_generator(FAMILY_TENT, 2, 128, 640);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 96, 96, 1, 2);
    };
    // (team, family, level) of every live Living, sorted — the twin worlds
    // are deterministic, so equal multisets mean the generators kept their
    // legacy spawn levels under Matched.
    auto fielded = [](GameWorld& world) {
        std::vector<std::tuple<int, int, int>> out;
        for (const auto& uptr : world.oblist)
        {
            const walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->stats() == nullptr)
                continue;
            out.emplace_back(static_cast<int>(w->team_num()),
                             static_cast<int>(w->family()),
                             w->stats()->level());
        }
        std::sort(out.begin(), out.end());
        return out;
    };

    // The twin worlds run strictly one after the other (the E3 twin idiom:
    // construct-and-tick the first fully before constructing the second —
    // the script bindings resolve the CURRENT world).
    ModesCtfWorld matched(kOnsLevelA);
    arm_matched(matched.world());
    author(matched);
    matched.tick(90);
    ModesCtfWorld own(kOnsLevelA);
    own.world().ctf_requested_strip_scenario_troops = 2;  // the OWN twin
    author(own);
    own.tick(90);
    ASSERT_EQ(kModeIdOnslaught, matched.var(kOnsModeId));
    ASSERT_EQ(kModeIdOnslaught, own.var(kOnsModeId));

    // TEAMS: Auto resolves to the authored team count (issue #218, the
    // 2026-08-18 directive): the solo roster plus BOTH authored non-roster
    // generator teams — identically for strip 2 and strip 3, the D26/D33
    // twin invariant applied where no squad seam exists.
    EXPECT_EQ(7, matched.var(kOnsTeamMask))
        << "Auto fields every authored generator team";
    EXPECT_EQ(own.var(kOnsTeamMask), matched.var(kOnsTeamMask))
        << "FAIR-mask == OWN-mask (D26/D33/E8)";
    EXPECT_EQ(own.var(kOnsTeamCount), matched.var(kOnsTeamCount));

    EXPECT_EQ(fielded(own.world()), fielded(matched.world()))
        << "matched power must not touch onslaught's generator armies (D17)";
    auto foundries_alive = [](GameWorld& world) {
        int alive = 0;
        for (const auto& uptr : world.oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Generator)
                ++alive;
        }
        return alive;
    };
    EXPECT_EQ(foundries_alive(own.world()), foundries_alive(matched.world()))
        << "FAIR and OWN keep the same foundries";
    EXPECT_EQ(3, foundries_alive(matched.world()))
        << "the FAIR strip keeps the ACTIVE teams' generators "
           "(keep_generators, E8) — and at TEAMS: Auto every authored "
           "generator team is active, so all three tents stand";

    EXPECT_GT(matched.var(kSlotMatchedTarget), 0)
        << "the shared census still runs (matched request + a human)";
    EXPECT_EQ(0, own.var(kSlotMatchedTarget));
    EXPECT_EQ(0, matched.var(kSlotMatchedPlan)) << "no seat ever solves";
    EXPECT_EQ(0, matched.var(kSlotMatchedAnnounced));
    EXPECT_FALSE(has_notification(matched.events, "TEAMS MATCHED"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// The 2026-08-18 Auto directive in the ALL-BOT shape (#218 review): with
// TROOPS: OWN or FAIR armed but no roster deployed anywhere, TEAMS: Auto
// still means "as many teams as the map actually has" — the authored
// generator teams, NOT the manifest row.teams default (kOnsLevelA declares
// teams = 2). The rule's one home is lib/mode_match.lua match.activation,
// consumed by the decide fold at the top of this init (the one-time C++
// preview twin is deleted). Only TROOPS: ALL keeps the manifest default
// at Auto.
TEST_F(ModesOnslaught, all_bot_own_and_fair_auto_field_every_authored_team)
{
    auto author = [](ModesCtfWorld& fx) {
        fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
        fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
        fx.spawn_generator(FAMILY_TENT, 2, 128, 640);
    };
    auto foundries_alive = [](GameWorld& world) {
        int alive = 0;
        for (const auto& uptr : world.oblist)
        {
            const walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Generator)
                ++alive;
        }
        return alive;
    };

    // Twin worlds, strictly sequential (the E3 twin idiom: construct and
    // tick the first fully before constructing the second).
    ModesCtfWorld own(kOnsLevelA);
    own.world().ctf_requested_strip_scenario_troops = 2;  // TROOPS: OWN
    author(own);
    own.tick(1);
    ModesCtfWorld fair(kOnsLevelA);
    arm_matched(fair.world());  // TROOPS: FAIR
    author(fair);
    fair.tick(1);

    ASSERT_TRUE(own.world().mode.active);
    ASSERT_TRUE(fair.world().mode.active);
    EXPECT_EQ(1 + 2 + 4, own.var(kOnsTeamMask))
        << "Auto = the authored teams, not the manifest default (2)";
    EXPECT_EQ(3, own.var(kOnsTeamCount));
    EXPECT_EQ(3, foundries_alive(own.world()))
        << "no tent is stripped as an inactive team's";
    EXPECT_EQ(1, own.team_var(kOnsGenCount, 2))
        << "the third team's foundry is censused";
    EXPECT_EQ(own.var(kOnsTeamMask), fair.var(kOnsTeamMask))
        << "FAIR-mask == OWN-mask in the all-bot shape too (D33(a))";
    EXPECT_EQ(own.var(kOnsTeamCount), fair.var(kOnsTeamCount));
    EXPECT_EQ(3, foundries_alive(fair.world()));
    EXPECT_EQ(0, fair.var(kSlotMatchedTarget))
        << "no humans -> the census latches nothing (E1)";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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
// Authored-hp flip restore (INVESTIGATION-3)
//
// The loader ships TOWER/BONES/TREEHOUSE with max_hp 0 (TENT with 100)
// while walker::set_difficulty stamps fighting hp into hitpoints only, so
// the flip's hp = max_hp restore used to leave a fresh capture at 0 hp —
// and the B6 guard's floor arm then returned the NUMBER 0 (a replacement
// amount, not a cancel), letting walker::attack's hp <= 0 death check
// explode the "immortal" generator. on_mode_init now normalizes every
// generator's max_hp to its authored hp, and the floor arm cancels
// outright at trunc(hp) <= 1.
// ===========================================================================

TEST_F(ModesOnslaught, flip_restores_authored_hp_for_zero_max_families)
{
    OnsWorld fx;
    walker* bones = fx.spawn_generator(FAMILY_BONES, 1, 320, 480, 2);
    walker* tower = fx.spawn_generator(FAMILY_TOWER, 1, 320, 560, 2);
    walker* tree = fx.spawn_generator(FAMILY_TREEHOUSE, 1, 320, 640, 2);
    ASSERT_NE(nullptr, bones);
    ASSERT_NE(nullptr, tower);
    ASSERT_NE(nullptr, tree);
    walker* const gens[] = {bones, tower, tree};
    float authored[3];
    for (int i = 0; i < 3; ++i)
    {
        authored[i] = gens[i]->stats()->hitpoints();
        ASSERT_GT(authored[i], 0.0f);
        ASSERT_EQ(authored[i], gens[i]->stats()->max_hitpoints())
            << "the engine stamps the denominator at set_difficulty, even for "
               "the families the loader ships with base hp 0";
    }
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(authored[i], gens[i]->stats()->max_hitpoints())
            << "and mode init leaves it alone";
    }

    for (int i = 0; i < 3; ++i)
    {
        fx.red->setxy(static_cast<short>(gens[i]->xpos() + 12),
                      static_cast<short>(gens[i]->ypos() + 20));
        fx.smash(fx.red, gens[i]);
        EXPECT_FALSE(gens[i]->dead()) << "generators never die (D5)";
        EXPECT_EQ(0, gens[i]->team_num()) << "the flip lands";
        EXPECT_EQ(authored[i], gens[i]->stats()->hitpoints())
            << "the flip restores the AUTHORED hp, not the loader's 0";
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, tent_flip_restores_full_authored_hp_not_half)
{
    OnsWorld fx;
    walker* tent = fx.spawn_generator(FAMILY_TENT, 1, 320, 480, 2);
    ASSERT_NE(nullptr, tent);
    const float authored = tent->stats()->hitpoints();
    ASSERT_EQ(authored, tent->stats()->max_hitpoints())
        << "a difficulty-stamped tent used to run above its loader max_hp "
           "of 100; set_difficulty now moves both";
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    EXPECT_EQ(authored, tent->stats()->max_hitpoints());

    fx.red->setxy(332, 500);
    fx.smash(fx.red, tent);
    EXPECT_EQ(0, tent->team_num());
    EXPECT_EQ(authored, tent->stats()->hitpoints())
        << "full authored hp, not the loader-max half";
}

TEST_F(ModesOnslaught, guarded_lethal_hits_on_a_fresh_flip_never_kill)
{
    // The exact reported kill chain: flip a zero-max family, then land
    // enemy lethal hits inside the 96-tick guard window. Pre-fix the
    // flipped shell sat at 0 hp and the second hit exploded it.
    OnsWorld fx;
    walker* bones = fx.spawn_generator(FAMILY_BONES, 1, 320, 480, 2);
    ASSERT_NE(nullptr, bones);
    const float authored = bones->stats()->hitpoints();
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    fx.red->setxy(332, 500);
    fx.smash(fx.red, bones);  // flip to red at world tick 1
    ASSERT_EQ(0, bones->team_num());
    ASSERT_EQ(authored, bones->stats()->hitpoints());

    walker* green_hitter = fx.spawn_living(FAMILY_SOLDIER, 1, 344, 500);
    fx.smash(green_hitter, bones);  // guarded lethal: the floor arm
    EXPECT_FALSE(bones->dead()) << "the reported explode-instead-of-flip";
    EXPECT_EQ(0, bones->team_num()) << "no re-flip inside the guard (B6)";
    EXPECT_EQ(1.0f, bones->stats()->hitpoints()) << "floored, alive";

    fx.smash(green_hitter, bones);  // guarded lethal AT the floor
    EXPECT_FALSE(bones->dead())
        << "trunc(hp) <= 1 cancels outright: the 0-damage death path is "
           "unreachable";
    EXPECT_EQ(1.0f, bones->stats()->hitpoints());

    // Guard expiry: the ping-pong prevention stays intact, and the next
    // lethal hit flips again at full authored hp.
    fx.tick(96);
    fx.smash(green_hitter, bones);
    EXPECT_EQ(1, bones->team_num()) << "post-guard lethal hit flips again";
    EXPECT_EQ(authored, bones->stats()->hitpoints());
    EXPECT_FALSE(bones->dead());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, zero_hp_shell_cannot_die_through_the_floor_arm)
{
    // Belt and braces: even if some future path leaves a live generator at
    // 0 hp, the hardened floor arm cancels the hit before walker::attack's
    // hp <= 0 death check can fire.
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);  // flip: the guard record is written
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    fx.red_gen_a->stats()->set_hitpoints(0.0f);  // the surprise state

    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 340);
    fx.smash(red_hitter, fx.red_gen_a);
    EXPECT_FALSE(fx.red_gen_a->dead())
        << "pre-fix: the applied-0-damage path killed the 0-hp shell";
    EXPECT_EQ(0.0f, fx.red_gen_a->stats()->hitpoints())
        << "cancelled outright — nothing applied";
    EXPECT_EQ(1, fx.red_gen_a->team_num());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, adversarial_lethal_trades_never_kill_a_generator)
{
    // The investigator's regression net: two enemy teams trade lethal
    // blows on a zero-max-family generator every tick across guard
    // expiries. Pre-fix this died on the first flipped 0-max generator.
    OnsWorld fx;
    walker* bones = fx.spawn_generator(FAMILY_BONES, 7, 320, 480, 2);
    ASSERT_NE(nullptr, bones);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 308, 500);
    walker* green_hitter = fx.spawn_living(FAMILY_SOLDIER, 1, 344, 500);
    for (int i = 0; i < 200; ++i)
    {
        fx.smash(red_hitter, bones);
        ASSERT_FALSE(bones->dead()) << "iteration " << i;
        fx.smash(green_hitter, bones);
        ASSERT_FALSE(bones->dead()) << "iteration " << i;
        ASSERT_GT(bones->stats()->hitpoints(), 0.0f) << "iteration " << i;
        fx.tick(1);
        ASSERT_FALSE(bones->dead()) << "iteration " << i;
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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

TEST_F(ModesOnslaught, the_grace_warning_fires_once_per_clock_start)
{
    // The warning rides the ZERO_SINCE 0 -> now transition, so it fires on
    // exactly one tick of a window — and on one tick of the NEXT window,
    // after a recapture cleared the var.
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    EXPECT_EQ(0, count_notifications(fx.events, "HAS NO ENGINES"))
        << "a team with a post is never warned";

    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    ASSERT_GT(fx.team_var(kOnsZeroSince, 1), 0);
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN HAS NO ENGINES!"));
    fx.tick(200);
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN HAS NO ENGINES!"))
        << "the running clock re-warns on no later tick";

    // Green takes a post back: the clock clears, and the warning stays put.
    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    fx.tick(1);
    ASSERT_EQ(0, fx.team_var(kOnsZeroSince, 1));
    EXPECT_EQ(1, count_notifications(fx.events, "GREEN HAS NO ENGINES!"));

    // Red takes it straight back once the B6 guard expires: a new window,
    // so a new warning.
    walker* red_hitter = fx.spawn_living(FAMILY_SOLDIER, 0, 116, 340);
    fx.tick(100);
    fx.smash(red_hitter, fx.red_gen_a);
    ASSERT_EQ(0, fx.red_gen_a->team_num());
    fx.tick(1);
    EXPECT_GT(fx.team_var(kOnsZeroSince, 1), 0);
    EXPECT_EQ(2, count_notifications(fx.events, "GREEN HAS NO ENGINES!"))
        << "a fresh grace window warns again";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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

    // The hero corpse is NOT hook-scrubbed: it enters the respawn queue and
    // keeps its stain through the countdown for cleric raises.
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
    EXPECT_TRUE(stain_alive_at(fx.world(), hx, hy))
        << "a queued hero leaves a raisable corpse during the countdown";
}

// ===========================================================================
// Scoring (modes.md §5.5): generator 300, waypoint 50, kills 10 / 50
// ===========================================================================

TEST_F(ModesOnslaught, taking_a_generator_pays_three_hundred)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    ASSERT_EQ(0u, fx.world().m_score[1]);

    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num()) << "the flip must land";
    EXPECT_EQ(300u, fx.world().m_score[1]) << "the capture pays the team";
    EXPECT_TRUE(has_score_change(fx.events, 1, 300));
    EXPECT_EQ(0u, fx.world().m_score[0]) << "the loser pays nothing";
}

TEST_F(ModesOnslaught, a_floored_hit_pays_nothing)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    // Same-team smash: the hit floors at 1 hp, there is no flip and no pay.
    fx.red->setxy(140, 340);
    fx.smash(fx.red, fx.red_gen_a);
    EXPECT_EQ(0, fx.red_gen_a->team_num());
    EXPECT_EQ(0u, fx.world().m_score[0]);
}

TEST_F(ModesOnslaught, holding_a_waypoint_pays_fifty)
{
    OnsWorld fx;
    walker* wp = fx.spawn_point(point_family_, 320, 480);
    ASSERT_NE(nullptr, wp);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    ASSERT_EQ(1, fx.var(kOnsWpCount));

    fx.red->setxy(320, 480);
    fx.tick(kWpHoldTicks + 1);
    ASSERT_EQ(1, fx.team_var(kOnsWpOwner1, 0)) << "red must take the point";
    EXPECT_EQ(50u, fx.world().m_score[0]);
    EXPECT_TRUE(has_score_change(fx.events, 0, 50));
}

TEST_F(ModesOnslaught, kills_pay_ten_for_a_spawn_and_fifty_for_a_hero)
{
    OnsWorld fx;
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 480, 96, 9);
    ASSERT_NE(nullptr, hero);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    // m_score also carries the engine's own per-damage credit, so the award
    // is read off the ScoreChange events (the TDM suite's idiom).
    fx.red->setxy(static_cast<short>(fx.green->xpos() - 20),
                  static_cast<short>(fx.green->ypos()));
    fx.smash(fx.red, fx.green);
    ASSERT_TRUE(fx.green->dead());
    EXPECT_TRUE(has_score_change(fx.events, 0, 10))
        << "a non-hero body is small change";
    EXPECT_FALSE(has_score_change(fx.events, 0, 50))
        << "and it is not paid at the hero rate";

    fx.red->setxy(static_cast<short>(hero->xpos() - 20),
                  static_cast<short>(hero->ypos()));
    fx.smash(fx.red, hero);
    ASSERT_TRUE(hero->dead());
    EXPECT_TRUE(has_score_change(fx.events, 0, 50))
        << "a hero is worth five bodies";
}

TEST_F(ModesOnslaught, teamkills_and_environment_deaths_pay_nothing)
{
    OnsWorld fx;
    walker* ally = fx.spawn_living(FAMILY_SOLDIER, 0, 120, 96);
    ASSERT_NE(nullptr, ally);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    // Teamkill through the attribution channel (same-team attacks are
    // refused at the engine gate, so stamp then flip the team).
    ASSERT_NE(nullptr, ally->stats());
    ally->stats()->set_hitpoints(100.0f);
    fx.green->setxy(116, 96);
    fx.green->attack(ally);   // stamps killer green, team 1
    ally->set_team_num(1);    // the charm flip
    ally->set_dead(1);
    ally->death();
    EXPECT_EQ(0u, fx.world().m_score[1]) << "no pay for friendly fire";

    // Environment death: no stamp at all.
    walker* stray = fx.spawn_living(FAMILY_SOLDIER, 1, 200, 700);
    ASSERT_NE(nullptr, stray);
    stray->set_dead(1);
    stray->death();
    EXPECT_EQ(0u, fx.world().m_score[0]);
    EXPECT_EQ(0u, fx.world().m_score[1]);
}

// ===========================================================================
// Elimination hygiene
// ===========================================================================

TEST_F(ModesOnslaught, elimination_scrubs_the_bodies_it_leaves)
{
    // Three teams, so the elimination does not end the match: a 2-team
    // elimination latches the win, which flush-revives and short-circuits
    // the sweep that this test is about.
    OnsWorld fx(kOnsLevelB);
    walker* blue_gen = fx.spawn_generator(FAMILY_TENT, 2, 320, 200);
    ASSERT_NE(nullptr, blue_gen);
    fx.spawn_anchor(2, 320, 160);
    ASSERT_NE(nullptr, fx.spawn_living(FAMILY_SOLDIER, 2, 320, 160));
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());

    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    ASSERT_GT(fx.team_var(kOnsZeroSince, 1), 0);

    // Age the grace clock rather than idling it out: a stain left sitting
    // for 600 ticks expires on its own and proves nothing.
    fx.world().mode.vars[kOnsZeroSince + 1] =
        static_cast<std::int32_t>(fx.world().tick_count_) - kGraceTicks;
    fx.tick(1);
    ASSERT_EQ(1, fx.team_var(kOnsEliminated, 1)) << "green is marked";
    ASSERT_FALSE(fx.green->dead()) << "the marking tick does not kill";

    // A fresh stain under green's living, the shape a raise or a farm needs.
    const int gx = fx.green->xpos();
    const int gy = fx.green->ypos();
    walker* stain = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    stain->setxy(static_cast<short>(gx), static_cast<short>(gy));
    stain->set_team_num(1);
    ASSERT_TRUE(stain_alive_at(fx.world(), gx, gy));

    fx.tick(1);
    ASSERT_TRUE(fx.green->dead()) << "the sweep kills the eliminated team";
    ASSERT_FALSE(fx.world().game_ended) << "two teams remain";
    EXPECT_FALSE(stain_alive_at(fx.world(), gx, gy))
        << "run_elimination writes the dead flag directly, so walker::death "
           "never scrubs — the sweep must scrub for it";
}

TEST_F(ModesOnslaught, an_eliminated_teams_dead_do_not_contest_waypoints)
{
    // Three teams so the elimination does not end the match, and a waypoint
    // the doomed team is standing on when it falls.
    OnsWorld fx(kOnsLevelB);
    walker* blue_gen = fx.spawn_generator(FAMILY_TENT, 2, 320, 200);
    ASSERT_NE(nullptr, blue_gen);
    fx.spawn_anchor(2, 320, 160);
    walker* blue = fx.spawn_living(FAMILY_SOLDIER, 2, 320, 160);
    ASSERT_NE(nullptr, blue);
    walker* wp = fx.spawn_point(point_family_, 320, 480);
    ASSERT_NE(nullptr, wp);
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    ASSERT_EQ(1, fx.var(kOnsWpCount));

    // Green loses its only generator, so its grace clock starts.
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    ASSERT_GT(fx.team_var(kOnsZeroSince, 1), 0);

    // Age the clock instead of idling out the full grace: 600 ticks of
    // generator spawns would fight this arrangement apart long before the
    // elimination lands.
    fx.world().mode.vars[kOnsZeroSince + 1] =
        static_cast<std::int32_t>(fx.world().tick_count_) - kGraceTicks;

    // Green outnumbers red on the disc two to one, and holds the meter.
    walker* green_two = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 484);
    ASSERT_NE(nullptr, green_two);
    fx.green->setxy(320, 480);
    fx.red->setxy(324, 480);
    fx.tick(1);
    ASSERT_EQ(1, fx.team_var(kOnsEliminated, 1)) << "green is marked";
    ASSERT_FALSE(fx.green->dead()) << "the marking tick does not kill";
    ASSERT_EQ(2, fx.team_var(kOnsWpProgTeam1, 0))
        << "green owns the meter going into its last tick";

    // The next tick is the one that matters: run_elimination kills green's
    // members, then run_waypoints runs — on the SAME census.
    fx.green->setxy(320, 480);
    green_two->setxy(320, 484);
    fx.red->setxy(324, 480);
    fx.tick(1);
    ASSERT_TRUE(fx.green->dead());
    ASSERT_TRUE(green_two->dead());
    ASSERT_FALSE(fx.world().game_ended) << "two teams remain";

    // A stale census still sees green's two bodies outnumbering red's one
    // and keeps accruing for the team that just died. The re-derived one
    // leaves red alone on the disc.
    EXPECT_EQ(1, fx.team_var(kOnsWpProgTeam1, 0))
        << "the dead must not contest the disc they died on";
    EXPECT_EQ(1, fx.team_var(kOnsWpProgress, 0))
        << "red's meter restarts on the elimination tick itself";
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

namespace {

// Advance to the world tick sitting exactly `elapsed` ticks past a grace
// start (the tick the census recorded the zero).
void tick_to_elapsed(OnsWorld& fx, std::int32_t since, int elapsed)
{
    while (static_cast<std::int32_t>(fx.world().tick_count_) < since + elapsed)
        fx.tick(1);
}

// One whole run of the countdown shape: green's only post flips away on
// the first scripted tick, and the row is read `hold` ticks into the
// window.
std::string run_grace_row(int hold)
{
    OnsWorld fx;
    fx.tick(1);
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1 + hold);
    EXPECT_TRUE(fx.ons_active());
    return std::string(fx.world().mode.hud[1].text.data());
}

}  // namespace

TEST_F(ModesOnslaught, hud_counts_the_recapture_window_down_in_seconds)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    // Green's only post flips away: the row becomes the countdown on the
    // tick the census records the zero.
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    const std::int32_t since = fx.team_var(kOnsZeroSince, 1);
    ASSERT_GT(since, 0);
    EXPECT_STREQ("GREEN OUT IN 50s", fx.world().mode.hud[1].text.data())
        << "the full 600-tick window reads 50 s at 12 ticks/s";
    EXPECT_EQ(1, fx.world().mode.hud[1].team) << "the row keeps its tint";
    EXPECT_STREQ("RED 3 GEN", fx.world().mode.hud[0].text.data())
        << "the captor's row is untouched";

    tick_to_elapsed(fx, since, 12);
    EXPECT_STREQ("GREEN OUT IN 49s", fx.world().mode.hud[1].text.data())
        << "one second of ticks, one second off the row";
    tick_to_elapsed(fx, since, 300);
    EXPECT_STREQ("GREEN OUT IN 25s", fx.world().mode.hud[1].text.data());

    // Rounding is up, so the row names the second the team is still inside.
    tick_to_elapsed(fx, since, 587);
    EXPECT_STREQ("GREEN OUT IN 2s", fx.world().mode.hud[1].text.data())
        << "13 ticks left is still into the second second";
    tick_to_elapsed(fx, since, 588);
    EXPECT_STREQ("GREEN OUT IN 1s", fx.world().mode.hud[1].text.data());
    tick_to_elapsed(fx, since, 599);
    EXPECT_STREQ("GREEN OUT IN 1s", fx.world().mode.hud[1].text.data())
        << "1s holds through the last tick of the window";
    EXPECT_EQ(0, fx.team_var(kOnsEliminated, 1));

    fx.tick(1);
    EXPECT_EQ(1, fx.team_var(kOnsEliminated, 1));
    EXPECT_STREQ("GREEN OUT", fx.world().mode.hud[1].text.data())
        << "the deadline still flips the row to OUT";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, recapture_swaps_the_countdown_back_to_the_gen_row)
{
    OnsWorld fx;
    fx.tick(1);
    ASSERT_TRUE(fx.ons_active());
    fx.green_gen->set_team_num(0);
    fx.green_gen->set_real_team_num(0);
    fx.tick(1);
    const std::int32_t since = fx.team_var(kOnsZeroSince, 1);
    ASSERT_GT(since, 0);
    tick_to_elapsed(fx, since, 120);
    ASSERT_STREQ("GREEN OUT IN 40s", fx.world().mode.hud[1].text.data());

    fx.green->setxy(140, 340);
    fx.smash(fx.green, fx.red_gen_a);
    ASSERT_EQ(1, fx.red_gen_a->team_num());
    fx.tick(1);
    EXPECT_EQ(0, fx.team_var(kOnsZeroSince, 1)) << "the clock cleared";
    EXPECT_STREQ("GREEN 1 GEN", fx.world().mode.hud[1].text.data())
        << "a recapture puts the count back on the row";
    EXPECT_STREQ("RED 2 GEN", fx.world().mode.hud[0].text.data());
    fx.tick(1);
    EXPECT_STREQ("GREEN 1 GEN", fx.world().mode.hud[1].text.data())
        << "and it stays there";
}

TEST_F(ModesOnslaught, the_longest_countdown_row_fits_the_hud_budget)
{
    // YELLOW is the worst case: "YELLOW OUT IN 50s" is 17 of the row's 25
    // bytes. Teams 1-3 are the authored ones, so the level's three-team
    // clamp activates GREEN/BLUE/YELLOW.
    ModesCtfWorld fx(kOnsLevelB);
    fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
    fx.spawn_generator(FAMILY_TENT, 2, 320, 800);
    walker* yellow_gen = fx.spawn_generator(FAMILY_TENT, 3, 128, 320);
    fx.spawn_living(FAMILY_SOLDIER, 1, 528, 96);
    fx.spawn_living(FAMILY_SOLDIER, 2, 320, 860);
    fx.spawn_living(FAMILY_SOLDIER, 3, 96, 96);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(14, fx.var(kOnsTeamMask)) << "GREEN | BLUE | YELLOW";

    yellow_gen->set_team_num(1);
    yellow_gen->set_real_team_num(1);
    fx.tick(1);
    const std::string row(fx.world().mode.hud[3].text.data());
    EXPECT_EQ("YELLOW OUT IN 50s", row);
    EXPECT_EQ(17u, row.size());
    EXPECT_LT(row.size(), static_cast<std::size_t>(og::sim::kModeHudTextBytes));
    EXPECT_EQ(1, count_notifications(fx.events, "YELLOW HAS NO ENGINES!"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, the_countdown_row_replays_identically)
{
    // The row is a pure function of replicated state (ZERO_SINCE and the
    // world clock), so the same match draws the same row twice — and every
    // peer draws the row its own census produced.
    const std::string first = run_grace_row(200);
    const std::string second = run_grace_row(200);
    ASSERT_EQ(first, second);
    EXPECT_EQ("GREEN OUT IN 34s", first) << "400 ticks left rounds up to 34 s";
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
        EXPECT_EQ(fx.world().mode.vars[static_cast<std::size_t>(slot)], mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
}

// ===========================================================================
// Scripted turn-undead honors the gate's replacement amount
// (INVESTIGATION-3 finding B)
//
// walker::turn_undead's scripted branch used to treat ANY non-negative gate
// result as "kill approved" — a floor-arm 1-hp (or 0) verdict on an undead
// living became an instant kill. The contract now mirrors walker::attack's
// gate site: gated < 0 spares the victim; gated below the offered lethal
// amount applies as ordinary damage (positive-amount attribution stamp,
// hp write, retaliation, NO death, NO kill credit); gated at or above the
// offered lethal amount destroys exactly as before.
//
// The verdict channel is a test-only level (9490) whose on_damage answers
// from mode var slot 60: -1 = cancel (return false), else return the
// number. Registered per test through the pack-script registry and exactly
// unregistered afterwards.
// ===========================================================================

namespace {

inline constexpr int kGateLevel = 9490;
inline constexpr int kGateVerdictSlot = 60;

constexpr const char* kUndeadGateScript =
    "og.register_level_hooks(9490, {\n"
    "  on_mode_init = function(level)\n"
    "    og.log(\"gate_init\", level)\n"
    "  end,\n"
    "  on_damage = function(target, attacker, amount)\n"
    "    if target:order() ~= og.C.ORDER_LIVING then\n"
    "      return nil\n"
    "    end\n"
    "    local verdict = og.mode_get(60)\n"
    "    if verdict < 0 then\n"
    "      return false\n"
    "    end\n"
    "    return verdict\n"
    "  end,\n"
    "})\n";

// RAII registration of the verdict level: constructed BEFORE the world so
// its VM evaluates the chunk, unregistered on scope exit so no other test
// in the binary ever sees level 9490.
struct GateScript
{
    GateScript()
    {
        og::script::register_pack_script(
            {"zz.test.undeadgate", "gate.lua", kUndeadGateScript});
    }
    ~GateScript()
    {
        og::script::unregister_pack_scripts("zz.test.undeadgate");
    }
};

// walker::turn_undead rolls rng(range*40) > rng(level*10) per target; with
// a level-1 victim and a 200 px range the roll lands overwhelmingly. The
// bounded retry stops on any observable landing (death or an hp change),
// so blind-verdict shapes (cancel, zero) simply spend all eight attempts.
int cast_turn_until_it_lands(walker* caster, walker* victim, float hp_before)
{
    int killed = 0;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        if (victim->dead())
            break;
        if (victim->stats()->hitpoints() != hp_before)
            break;
        killed += static_cast<int>(caster->turn_undead(200, 1));
    }
    return killed;
}

}  // namespace

TEST_F(ModesOnslaught, turn_undead_partial_replacement_damages_not_kills)
{
    GateScript gate;
    ModesCtfWorld fx(kGateLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active) << "the gate level must activate";
    walker* cleric = fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    undead->stats()->set_level(1);
    const float hp_before = undead->stats()->hitpoints();
    ASSERT_GT(hp_before, 7.0f);
    fx.world().mode.vars[kGateVerdictSlot] = 7;

    const int killed = cast_turn_until_it_lands(cleric, undead, hp_before);
    EXPECT_EQ(0, killed) << "a partial replacement is never a kill";
    EXPECT_FALSE(undead->dead());
    EXPECT_EQ(0, undead->death_called());
    EXPECT_EQ(hp_before - 7.0f, undead->stats()->hitpoints())
        << "the replaced amount lands as ordinary damage";
    EXPECT_EQ(cleric->entity_id(), undead->last_attacker_id())
        << "a positive amount stamps attribution, like walker::attack";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, turn_undead_zero_replacement_leaves_victim_untouched)
{
    // The floor-arm trap itself: a gate verdict of 0 used to read as "kill
    // approved". Now it is an applied 0-damage hit — no death, no stamp.
    GateScript gate;
    ModesCtfWorld fx(kGateLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    walker* cleric = fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    undead->stats()->set_level(1);
    const float hp_before = undead->stats()->hitpoints();
    fx.world().mode.vars[kGateVerdictSlot] = 0;

    const int killed = cast_turn_until_it_lands(cleric, undead, hp_before);
    EXPECT_EQ(0, killed);
    EXPECT_FALSE(undead->dead()) << "pre-fix: gated == 0 destroyed the victim";
    EXPECT_EQ(0, undead->death_called());
    EXPECT_EQ(hp_before, undead->stats()->hitpoints());
    EXPECT_EQ(0u, undead->last_attacker_id()) << "no stamp for a 0 amount";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, turn_undead_replacement_at_lethal_still_destroys)
{
    // The kill threshold is the OFFERED amount (hitpoints clamped to
    // [1, 32000] and truncated): a replacement equal to it destroys even
    // when float hp carries a fraction — the keep/no-hook path stays
    // byte-identical to the pre-contract behavior.
    GateScript gate;
    ModesCtfWorld fx(kGateLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    walker* cleric = fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    undead->stats()->set_level(1);
    undead->stats()->set_hitpoints(60.7f);
    fx.world().mode.vars[kGateVerdictSlot] = 60;  // == the offered lethal

    const int killed = cast_turn_until_it_lands(cleric, undead, 60.7f);
    EXPECT_EQ(1, killed);
    EXPECT_TRUE(undead->dead());
    EXPECT_EQ(1, undead->death_called());
    EXPECT_EQ(0.0f, undead->stats()->hitpoints());
    EXPECT_EQ(cleric->entity_id(), undead->last_attacker_id());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, turn_undead_replacement_above_lethal_destroys_as_before)
{
    GateScript gate;
    ModesCtfWorld fx(kGateLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    walker* cleric = fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    undead->stats()->set_level(1);
    const float hp_before = undead->stats()->hitpoints();
    fx.world().mode.vars[kGateVerdictSlot] = 32000;

    const int killed = cast_turn_until_it_lands(cleric, undead, hp_before);
    EXPECT_EQ(1, killed);
    EXPECT_TRUE(undead->dead());
    EXPECT_EQ(1, undead->death_called());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesOnslaught, turn_undead_gate_cancel_spares_the_victim)
{
    GateScript gate;
    ModesCtfWorld fx(kGateLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    walker* cleric = fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    undead->stats()->set_level(1);
    const float hp_before = undead->stats()->hitpoints();
    fx.world().mode.vars[kGateVerdictSlot] = -1;

    const int killed = cast_turn_until_it_lands(cleric, undead, hp_before);
    EXPECT_EQ(0, killed);
    EXPECT_FALSE(undead->dead());
    EXPECT_EQ(hp_before, undead->stats()->hitpoints());
    EXPECT_EQ(0u, undead->last_attacker_id()) << "a cancel never stamps";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
