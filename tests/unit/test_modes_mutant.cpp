// The Mutant campaign-pack Lua behavior suite
// (tools/modes_mapgen/pack: lib/mode_mutant_impl.lua + lib/mode_match.lua +
// scripts/mode_mutant.lua), over the shared modes-pack fixture. Rule spec:
// modes.md §7 as amended by DECISIONS D4 — FFA teams 0-3 stay, the
// damage gate replaces team juggling, herd/Bottom-Feeder machinery is cut.
//
// Levels: 9201/9202 bind through the test manifest rows; 840 binds through
// the SHIPPED scripts/mode_mutant.lua registration (mode_match.rows_for
// over the committed manifest), which also arms the manifest-gated
// teleport clamp.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
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

// The mode-var slot map of lib/mode_mutant_impl.lua (table S).
enum MutantSlot : int {
    kMutSlotModeId = 0,
    kMutSlotPhase = 1,
    kMutSlotTeamMask = 8,
    kMutSlotScoreLimit = 9,
    kMutSlotTimeLimit = 10,
    kMutSlotRespawnTicks = 11,
    kMutSlotAnchorCursor = 12,
    kMutSlotMutantEntity = 13,
    kMutSlotMutantTeam1 = 14,
    kMutSlotMutantBaseDamage = 15,  // x256
    kMutSlotScore = 16,             // +team
};

inline constexpr int kModeIdMutant = 5;  // mode_core.MODE.MUTANT
inline constexpr int kPhaseFfa = 1;
inline constexpr int kPhaseMutant = 2;
inline constexpr int kMutantCadence = 15;
inline constexpr int kMutantBit = 16384;

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

void align_before_cadence(GameWorld& world)
{
    const std::uint32_t next =
        ((world.tick_count_ / kMutantCadence) + 1) * kMutantCadence;
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

// A 4-competitor FFA rig (one living per team, anchors everywhere),
// initialized on the first tick.
struct MutantRig
{
    ModesCtfWorld fx;
    walker* a = nullptr;  // team 0
    walker* b = nullptr;  // team 1
    walker* c = nullptr;  // team 2
    walker* d = nullptr;  // team 3

    explicit MutantRig(int level_id = kMutantLevelA, int act = ACT_CONTROL)
        : fx(level_id)
    {
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 544, 96);
        fx.spawn_anchor(2, 96, 800);
        fx.spawn_anchor(3, 544, 800);
        a = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, act);
        b = fx.spawn_living(FAMILY_ORC, 1, 216, 200, act);
        c = fx.spawn_living(FAMILY_SOLDIER, 2, 200, 232, act);
        d = fx.spawn_living(FAMILY_ORC, 3, 216, 232, act);
        EXPECT_NE(nullptr, a);
        EXPECT_NE(nullptr, b);
        EXPECT_NE(nullptr, c);
        EXPECT_NE(nullptr, d);
    }

    bool active() const
    {
        return fx.world().mode.vars[kMutSlotModeId] == kModeIdMutant;
    }

    int phase() const { return fx.world().mode.vars[kMutSlotPhase]; }
    std::int32_t mutant_id() const
    {
        return fx.world().mode.vars[kMutSlotMutantEntity];
    }
    int score(int team) const
    {
        return fx.world().mode.vars[kMutSlotScore + team];
    }

    void slay(walker* attacker, walker* victim)
    {
        ASSERT_NE(nullptr, victim->stats());
        victim->stats()->set_hitpoints(1.0f);
        attacker->attack(victim);
        ASSERT_TRUE(victim->dead()) << "the rigged blow must be lethal";
    }

    // First blood: a kills b, crowning a. Positions untouched.
    void crown_a()
    {
        slay(a, b);
        ASSERT_EQ(kPhaseMutant, phase()) << "first blood must crown";
        ASSERT_EQ(static_cast<std::int32_t>(a->entity_id()), mutant_id());
    }
};

}  // namespace

using ModesMutant = ModesPackTest;

// ===========================================================================
// Activation / init
// ===========================================================================

TEST_F(ModesMutant, lazy_init_activates_ffa_with_defaults)
{
    MutantRig rig;
    ASSERT_FALSE(rig.fx.world().mode.active);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.fx.world().mode.active);
    EXPECT_TRUE(rig.active());
    EXPECT_EQ(15, rig.fx.var(kMutSlotTeamMask));
    EXPECT_EQ(kPhaseFfa, rig.phase());
    EXPECT_EQ(10, rig.fx.var(kMutSlotScoreLimit));
    EXPECT_EQ(7200, rig.fx.var(kMutSlotTimeLimit));
    EXPECT_EQ(60, rig.fx.var(kMutSlotRespawnTicks))
        << "the Mutant default delay is 60, not the engine's 120";
    EXPECT_EQ(0, rig.mutant_id());
    EXPECT_STREQ("MUTANT", rig.fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(rig.fx.events, "MUTANT! FIRST TO 10"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, init_demotes_below_two_competitors)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "fewer than two competitors"));
}

TEST_F(ModesMutant, shipped_manifest_registration_binds_level_840)
{
    MutantRig rig(840);
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active())
        << "scripts/mode_mutant.lua must bind manifest id 840";
    EXPECT_EQ(10, rig.fx.var(kMutSlotScoreLimit)) << "manifest score_limit";
    EXPECT_EQ(7200, rig.fx.var(kMutSlotTimeLimit)) << "manifest time_limit";
}

TEST_F(ModesMutant, empty_competitor_teams_field_one_bot_each)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    fx.tick(1);

    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "FFA seats, not squads";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1));
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(1, alive_on_team(fx.world(), 3));
}

TEST_F(ModesMutant, scenario_troops_strip_runs_before_the_seat_census)
{
    // The shared strip (lib/mode_strip) precedes the one-bot-per-empty-seat
    // census, so a seat the strip empties is filled rather than left vacant.
    ModesCtfWorld fx(kMutantLevelA);
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* troop = fx.spawn_living(FAMILY_ORC, 1, 520, 200);
    fx.tick(1);

    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    EXPECT_FALSE(hero->dead()) << "roster walkers are never stripped";
    EXPECT_TRUE(troop->dead()) << "STRIP_ALL takes the authored troop";
    EXPECT_EQ(1, alive_on_team(fx.world(), 0));
    EXPECT_EQ(1, alive_on_team(fx.world(), 1)) << "the emptied seat is refilled";
}

// ===========================================================================
// Crown / transfer / revert (the phase machine)
// ===========================================================================

TEST_F(ModesMutant, first_blood_crowns_the_killer_with_full_buffs)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_NE(nullptr, rig.a->stats());
    const float base_damage = rig.a->damage();

    rig.crown_a();
    EXPECT_EQ(0 + 1, rig.fx.var(kMutSlotMutantTeam1));
    EXPECT_EQ(static_cast<std::int32_t>(base_damage * 256.0f),
              rig.fx.var(kMutSlotMutantBaseDamage))
        << "pre-buff damage banks x256 for exact restore";
    EXPECT_EQ(base_damage * 2.0f, rig.a->damage())
        << "berserk doubles damage";
    EXPECT_EQ(100, rig.a->invisibility_left());
    EXPECT_EQ(30, rig.a->speed_bonus_left());
    EXPECT_TRUE(rig.a->stats()->query_bit_flags(kMutantBit))
        << "the stats-bit mark rides the mutant";
    EXPECT_EQ(static_cast<std::int32_t>(rig.a->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id)
        << "beacon slot 0 marks the mutant";
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].team);
    EXPECT_TRUE(has_notification(rig.fx.events, "RED IS THE MUTANT!"));
    EXPECT_EQ(0, rig.score(0)) << "first blood itself scores nothing";
}

TEST_F(ModesMutant, ffa_environment_and_summon_deaths_crown_nobody)
{
    // Suicide is the environment shape by construction: the engine's
    // root-team gate means self-damage never lands, so a self-inflicted
    // death always arrives with a nil killer.
    MutantRig rig;
    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, 2, 232, 232);
    ASSERT_NE(nullptr, pet);
    pet->set_owner(rig.c);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // Environment: no attacker stamp.
    rig.b->set_dead(1);
    rig.b->death();
    EXPECT_EQ(kPhaseFfa, rig.phase());

    // A summon victim decides nothing even with a live killer.
    rig.slay(rig.a, pet);
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "owned victims never crown";
}

TEST_F(ModesMutant, killing_the_mutant_transfers_crown_scores_and_heals)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    ASSERT_NE(nullptr, rig.c->stats());
    const float c_base_damage = rig.c->damage();
    rig.c->stats()->set_hitpoints(10.0f);
    const float a_base =
        static_cast<float>(rig.fx.var(kMutSlotMutantBaseDamage)) / 256.0f;

    rig.slay(rig.c, rig.a);

    EXPECT_EQ(kPhaseMutant, rig.phase());
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()), rig.mutant_id())
        << "mutancy transfers to the killer";
    EXPECT_EQ(2 + 1, rig.fx.var(kMutSlotMutantTeam1));
    EXPECT_EQ(1, rig.score(2)) << "killing the Mutant scores 1";
    EXPECT_TRUE(has_score_change(rig.fx.events, 2, 1))
        << "og.award_score(team, 1) rides beside the engine damage credit";
    EXPECT_EQ(a_base, rig.a->damage())
        << "the old mutant's corpse gets its base damage back";
    EXPECT_FALSE(rig.a->stats()->query_bit_flags(kMutantBit));
    EXPECT_TRUE(rig.c->stats()->query_bit_flags(kMutantBit));
    EXPECT_EQ(c_base_damage * 2.0f, rig.c->damage());
    EXPECT_EQ(25.0f, rig.c->stats()->hitpoints())
        << "the heir heals kill_heal (15) clamped";
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id);
    EXPECT_EQ(2, rig.fx.world().mode.beacons[0].team);
}

TEST_F(ModesMutant, mutant_environment_death_reverts_the_pool)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();

    rig.a->set_dead(1);
    rig.a->death();
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "environment death reverts to FFA";
    EXPECT_EQ(0, rig.mutant_id());
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].entity_id)
        << "the beacon clears on revert";
    EXPECT_FALSE(rig.a->stats()->query_bit_flags(kMutantBit));
    EXPECT_TRUE(has_notification(rig.fx.events, "THE MUTANT IS NO MORE!"));

    // The next first blood re-mutates.
    rig.slay(rig.c, rig.d);
    EXPECT_EQ(kPhaseMutant, rig.phase());
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()), rig.mutant_id());
}

TEST_F(ModesMutant, silent_dead_flag_kill_reverts_on_the_next_tick)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();

    rig.a->set_dead(1);  // bypasses walker::death and the hook
    rig.fx.tick(1);
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "upkeep reverts a stale crown";
    EXPECT_EQ(0, rig.mutant_id());
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].entity_id);
}

TEST_F(ModesMutant, mutant_kills_score_and_heal_the_mutant)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    rig.a->stats()->set_hitpoints(20.0f);

    rig.slay(rig.a, rig.c);
    EXPECT_EQ(1, rig.score(0)) << "kills AS the Mutant score 1";
    EXPECT_TRUE(has_score_change(rig.fx.events, 0, 1));
    EXPECT_EQ(35.0f, rig.a->stats()->hitpoints())
        << "each kill heals kill_heal (15)";

    // A summon victim scores nothing.
    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, 3, 232, 232);
    ASSERT_NE(nullptr, pet);
    pet->set_owner(rig.d);
    rig.slay(rig.a, pet);
    EXPECT_EQ(1, rig.score(0)) << "summon deaths never score";
}

// ===========================================================================
// The damage matrix (D4 gate, all four arms + summon inheritance)
// ===========================================================================

TEST_F(ModesMutant, damage_matrix_blocks_only_nonmutant_on_nonmutant)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_NE(nullptr, rig.c->stats());
    ASSERT_NE(nullptr, rig.d->stats());

    // FFA phase: everyone hostile, damage lands.
    rig.c->stats()->set_hitpoints(100.0f);
    rig.d->attack(rig.c);
    EXPECT_LT(rig.c->stats()->hitpoints(), 100.0f)
        << "FFA phase gates nothing";

    rig.crown_a();

    // Arm 1: non-mutant vs non-mutant is void.
    rig.c->stats()->set_hitpoints(100.0f);
    rig.d->attack(rig.c);
    EXPECT_EQ(100.0f, rig.c->stats()->hitpoints())
        << "competitors cannot hurt each other in the mutant phase";

    // Arm 2: anyone can hurt the Mutant.
    ASSERT_NE(nullptr, rig.a->stats());
    rig.a->stats()->set_hitpoints(100.0f);
    rig.c->attack(rig.a);
    EXPECT_LT(rig.a->stats()->hitpoints(), 100.0f);

    // Arm 3: the Mutant can hurt anyone.
    rig.c->stats()->set_hitpoints(100.0f);
    rig.a->attack(rig.c);
    EXPECT_LT(rig.c->stats()->hitpoints(), 100.0f);

    // Arm 4 (owner-chain inheritance): the Mutant's summon hurts a
    // competitor; a competitor's summon cannot hurt another competitor.
    walker* mutant_pet = rig.fx.spawn_living(FAMILY_SKELETON, 0, 260, 200);
    ASSERT_NE(nullptr, mutant_pet);
    mutant_pet->set_owner(rig.a);
    rig.c->stats()->set_hitpoints(100.0f);
    mutant_pet->attack(rig.c);
    EXPECT_LT(rig.c->stats()->hitpoints(), 100.0f)
        << "the Mutant's summon inherits its matrix";

    walker* c_pet = rig.fx.spawn_living(FAMILY_SKELETON, 2, 260, 232);
    ASSERT_NE(nullptr, c_pet);
    c_pet->set_owner(rig.c);
    rig.d->stats()->set_hitpoints(100.0f);
    c_pet->attack(rig.d);
    EXPECT_EQ(100.0f, rig.d->stats()->hitpoints())
        << "a competitor's summon is void against competitors";
}

// The turn-undead instant kill used to skip walker::attack's hit path
// entirely (the target was already dead when attack ran), so the one-way
// damage matrix — the ONLY thing keeping competitors off each other in the
// mutant phase — never saw it: a non-mutant cleric could erase an undead
// competitor outright. The gate now cancels it.
TEST_F(ModesMutant, turn_undead_on_a_competitor_is_cancelled_by_the_matrix)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    ASSERT_EQ(kPhaseMutant, rig.phase());

    // Two NON-mutant competitors: the crown sits on team 0.
    walker* cleric = rig.fx.spawn_living(FAMILY_CLERIC, 2, 200, 264);
    walker* undead = rig.fx.spawn_living(FAMILY_SKELETON, 3, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    ASSERT_NE(nullptr, undead->stats());
    undead->stats()->set_level(1);
    const float hp_before = undead->stats()->hitpoints();

    for (int attempt = 0; attempt < 8; ++attempt)
        (void)cleric->turn_undead(200, 1);

    EXPECT_FALSE(undead->dead())
        << "the matrix must spare a non-mutant competitor";
    EXPECT_EQ(0, undead->death_called());
    EXPECT_EQ(hp_before, undead->stats()->hitpoints());
    EXPECT_EQ(kPhaseMutant, rig.phase()) << "no transfer, no revert";
    EXPECT_EQ(0, rig.score(2)) << "a cancelled kill scores nothing";

    // The Mutant itself is still a legal turn-undead target: crown the
    // undead competitor and the same cleric may kill it.
    rig.fx.world().mode.vars[kMutSlotMutantEntity] =
        static_cast<std::int32_t>(undead->entity_id());
    rig.fx.world().mode.vars[kMutSlotMutantTeam1] = 3 + 1;
    ASSERT_GT(static_cast<int>(cleric->turn_undead(200, 1)), 0)
        << "an undead Mutant is a legal target for turn undead";
    EXPECT_TRUE(undead->dead());
    EXPECT_TRUE(undead->death_called());
}

// ===========================================================================
// Buff top-ups, HP decay, kill-heal, beacon lifecycle
// ===========================================================================

TEST_F(ModesMutant, decay_drops_two_hp_over_24_ticks_and_topups_hold)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    rig.a->stats()->set_heal_per_round(0.0f);  // isolate the decay
    rig.a->stats()->set_current_heal_delay(0);  // park the +1 REGEN rollover
    rig.a->stats()->set_hitpoints(50.0f);

    rig.fx.tick(24);
    EXPECT_EQ(48.0f, rig.a->stats()->hitpoints())
        << "exactly two decay steps of 1.0 hp per 12 ticks";
    EXPECT_EQ(100, rig.a->invisibility_left()) << "topped up every tick";
    EXPECT_EQ(30, rig.a->speed_bonus_left());
    EXPECT_EQ(static_cast<std::int32_t>(rig.a->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id)
        << "the beacon re-asserts every tick";
}

// The per-tick buff refresh is a FLOOR, not an assignment: a potion drunk
// mid-reign used to be erased one tick later by the crown's flat re-stamp.
TEST_F(ModesMutant, mid_reign_pickups_survive_the_buff_refresh)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    rig.fx.tick(1);
    ASSERT_EQ(1.0f, rig.a->speed_bonus()) << "the crown's own floor";
    ASSERT_EQ(30, rig.a->speed_bonus_left());
    ASSERT_EQ(100, rig.a->invisibility_left());

    // What a speed potion does: add duration, set the bonus to its level.
    rig.a->set_speed_bonus_left(rig.a->speed_bonus_left() + 50);
    rig.a->set_speed_bonus(3.0f);
    rig.a->set_invisibility_left(rig.a->invisibility_left() + 200);

    rig.fx.tick(1);
    EXPECT_EQ(3.0f, rig.a->speed_bonus())
        << "a stronger pickup must not be stomped back to the crown value";
    EXPECT_GT(rig.a->speed_bonus_left(), 30)
        << "the pickup's longer duration must survive";
    EXPECT_GT(rig.a->invisibility_left(), 100);

    // And the crown's floor is restored once the pickup decays past it.
    rig.a->set_speed_bonus_left(2);
    rig.a->set_invisibility_left(2);
    rig.fx.tick(1);
    EXPECT_EQ(30, rig.a->speed_bonus_left()) << "the floor comes back";
    EXPECT_EQ(100, rig.a->invisibility_left());
}

TEST_F(ModesMutant, decay_death_with_no_recent_attacker_reverts)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    rig.a->stats()->set_heal_per_round(0.0f);
    rig.a->stats()->set_current_heal_delay(0);
    rig.a->stats()->set_hitpoints(1.0f);

    rig.fx.tick(12);
    EXPECT_TRUE(rig.a->dead()) << "decay can kill";
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "environment shape: pool revert";
    EXPECT_EQ(0, rig.mutant_id());
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].entity_id);
}

TEST_F(ModesMutant, decay_death_after_a_recent_chip_transfers_to_the_chipper)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    rig.a->stats()->set_heal_per_round(0.0f);

    // C chips the Mutant (a landed, gate-approved hit stamps the D3
    // channel), then decay finishes it within the 48-tick window.
    rig.a->stats()->set_hitpoints(100.0f);
    rig.c->attack(rig.a);
    ASSERT_LT(rig.a->stats()->hitpoints(), 100.0f);
    rig.a->stats()->set_hitpoints(1.0f);
    rig.fx.tick(12);

    EXPECT_TRUE(rig.a->dead());
    EXPECT_EQ(kPhaseMutant, rig.phase())
        << "D3 recency: the chipper inherits the crown from a decay death";
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()), rig.mutant_id());
    EXPECT_EQ(1, rig.score(2));
}

// ===========================================================================
// Teleport-range clamp (manifest-gated pack overrides)
// ===========================================================================

namespace {

// Arms and runs one mage blink; answers the landing displacement.
void blink_mage(ModesCtfWorld& fx, walker* mage, int* dx, int* dy)
{
    const short from_x = mage->xpos();
    const short from_y = mage->ypos();
    ASSERT_NE(nullptr, mage->stats());
    mage->stats()->set_max_magicpoints(500);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    ASSERT_TRUE(mage->special()) << "mage teleport special must arm TELE_OUT";
    fx.tick(20);
    *dx = std::abs(mage->xpos() - from_x);
    *dy = std::abs(mage->ypos() - from_y);
}

}  // namespace

TEST_F(ModesMutant, mutant_level_blink_is_clamped_to_160px_and_ignores_markers)
{
    MutantRig rig(840);
    walker* mage = rig.fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    ASSERT_NE(nullptr, mage);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // A full-map recall marker: the clamp must ignore it (a cross-map
    // recall is exactly the escape the clamp forbids).
    walker* marker = rig.fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(96, 800);
    marker->set_lifetime(5000);
    marker->set_ani_type(ANI_SPIN);

    for (int blink = 0; blink < 4; ++blink)
    {
        int dx = 0;
        int dy = 0;
        blink_mage(rig.fx, mage, &dx, &dy);
        EXPECT_LE(dx, 160) << "blink " << blink;
        EXPECT_LE(dy, 160) << "blink " << blink;
    }
}

TEST_F(ModesMutant, off_manifest_level_blink_keeps_core_marker_recall)
{
    // 9201 is a live Mutant TEST level but NOT a manifest row: the clamp
    // is manifest-gated, so the core walker::teleport marker recall must
    // still work here (and on the campaign's other modes).
    MutantRig rig;
    walker* mage = rig.fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    ASSERT_NE(nullptr, mage);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    walker* marker = rig.fx.world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->set_owner(mage);
    marker->setxy(96, 800);
    marker->set_lifetime(5000);
    marker->set_ani_type(ANI_SPIN);

    int dx = 0;
    int dy = 0;
    blink_mage(rig.fx, mage, &dx, &dy);
    EXPECT_GT(dx + dy, 320)
        << "off the manifest the blink recalls to the far marker";
}

TEST_F(ModesMutant, skeleton_tunnel_clamps_to_160_only_past_its_own_range)
{
    MutantRig rig(840);
    walker* skel = rig.fx.spawn_living(FAMILY_SKELETON, 0, 400, 400);
    ASSERT_NE(nullptr, skel);
    rig.fx.tick(10);  // ANI_SKEL_GROW completes
    ASSERT_TRUE(rig.active());
    ASSERT_NE(nullptr, skel->stats());
    skel->stats()->set_level(20);  // family range 360 -> clamped 160

    bool past_family_floor = false;
    for (int blink = 0; blink < 6; ++blink)
    {
        const short from_x = skel->xpos();
        const short from_y = skel->ypos();
        skel->stats()->set_max_magicpoints(500);
        skel->stats()->set_magicpoints(500);
        skel->set_current_special(1);
        ASSERT_TRUE(skel->special()) << "tunnel must arm TELE_OUT";
        rig.fx.tick(20);
        const int dx = std::abs(skel->xpos() - from_x);
        const int dy = std::abs(skel->ypos() - from_y);
        EXPECT_LE(dx, 160) << "blink " << blink;
        EXPECT_LE(dy, 160) << "blink " << blink;
        if (dx > 18 || dy > 18)
            past_family_floor = true;
    }
    EXPECT_TRUE(past_family_floor)
        << "the clamp is min(family range, 160), not a collapse to the "
           "level-1 range";
}

// ===========================================================================
// AI director (both phases)
// ===========================================================================

TEST_F(ModesMutant, ffa_director_repairs_idle_bots_onto_nearest_competitor)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, ACT_RANDOM);
    walker* near_foe = fx.spawn_living(FAMILY_ORC, 1, 300, 200, ACT_RANDOM);
    walker* far_foe = fx.spawn_living(FAMILY_ORC, 1, 700, 800, ACT_RANDOM);
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, near_foe);
    ASSERT_NE(nullptr, far_foe);
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));

    bot->set_foe(nullptr);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(near_foe->entity_id(), bot->foe_id())
        << "FFA repair: nearest competitor by Manhattan";
}

TEST_F(ModesMutant, hunters_converge_on_the_mutant_and_players_stay_free)
{
    MutantRig rig(kMutantLevelA, ACT_RANDOM);
    walker* player = rig.fx.spawn_living(FAMILY_SOLDIER, 2, 260, 260,
                                         ACT_CONTROL);
    ASSERT_NE(nullptr, player);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();

    rig.c->set_foe(nullptr);
    rig.d->set_foe(nullptr);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);

    EXPECT_EQ(rig.a->entity_id(), rig.c->foe_id())
        << "hunters take the mutant as explicit foe (defeats invisibility)";
    EXPECT_EQ(rig.a->entity_id(), rig.d->foe_id());
    // The engine's own backstop may hand players a foe (pre-existing);
    // the director must never issue them commands.
    ASSERT_NE(nullptr, player->stats());
    EXPECT_TRUE(player->stats()->commands.empty())
        << "players are never steered";
}

TEST_F(ModesMutant, mutant_bot_culls_the_weakest_competitor_in_radius)
{
    MutantRig rig(kMutantLevelA, ACT_RANDOM);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();

    // Two competitors inside the 120px cull radius; the farther one is
    // weaker — the Mutant must take the weakest, not the nearest.
    rig.c->setxy(static_cast<short>(rig.a->xpos() + 40), rig.a->ypos());
    rig.d->setxy(static_cast<short>(rig.a->xpos() + 90), rig.a->ypos());
    ASSERT_NE(nullptr, rig.c->stats());
    ASSERT_NE(nullptr, rig.d->stats());
    rig.c->stats()->set_hitpoints(50.0f);
    rig.d->stats()->set_hitpoints(5.0f);
    rig.a->set_foe(nullptr);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(rig.d->entity_id(), rig.a->foe_id())
        << "cull rule: lowest hp within the radius";

    // With one competitor in radius the nearest rule applies.
    rig.d->setxy(700, 800);
    rig.a->set_foe(nullptr);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(rig.c->entity_id(), rig.a->foe_id())
        << "single-neighbor shape falls back to nearest";
}

// ===========================================================================
// Respawns
// ===========================================================================

TEST_F(ModesMutant, dead_competitor_revives_at_anchor_with_cursor)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 256, 256);
    fx.spawn_anchor(1, 544, 800);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 7);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    ASSERT_EQ(10, fx.var(kMutSlotRespawnTicks));

    hero->set_dead(1);
    fx.tick(1);
    EXPECT_EQ(1u, fx.world().respawn.respawn_queue.size())
        << "everyone schedules (Lua eligibility)";
    fx.tick(10);
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(256, hero->xpos()) << "anchor-cursor placement";
    EXPECT_EQ(256, hero->ypos());
}

// ===========================================================================
// Win / timeout / HUD
// ===========================================================================

TEST_F(ModesMutant, score_limit_win_and_timeout_lowest_byte_tiebreak)
{
    // Score limit first.
    {
        MutantRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kMutSlotScore + 2] = 10;
        rig.fx.tick(1);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(2, rig.fx.world().mode.winner_team);
        EXPECT_EQ(kMutantLevelA, rig.fx.world().next_level)
            << "bot competitors: rematch shape";
        EXPECT_TRUE(has_notification(rig.fx.events, "BLUE TEAM WINS!"));
    }
    // Timeout leader; an all-square tie resolves to the lowest team byte.
    {
        MutantRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kMutSlotScore + 1] = 4;
        rig.fx.world().mode.vars[kMutSlotScore + 3] = 4;
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(1, rig.fx.world().mode.winner_team)
            << "leading score wins; the tie resolves to the lowest byte";
    }
    // m_score is the middle rung between the mode metric and the team byte
    // (modes.md §8.2, the same ladder Soccer/Onslaught/CTF spell inline).
    {
        MutantRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kMutSlotScore + 1] = 4;
        rig.fx.world().mode.vars[kMutSlotScore + 3] = 4;
        rig.fx.world().m_score[3] = 700;
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(3, rig.fx.world().mode.winner_team)
            << "equal mutant score breaks on m_score before the team byte";
    }
}

TEST_F(ModesMutant, hud_lines_star_the_mutant)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    EXPECT_STREQ("RED 0/10", rig.fx.world().mode.hud[0].text.data());
    EXPECT_EQ(0, rig.fx.world().mode.hud[0].team);
    EXPECT_STREQ("BLUE 0/10", rig.fx.world().mode.hud[2].text.data());

    rig.crown_a();
    rig.fx.tick(1);
    EXPECT_STREQ("RED 0/10 *", rig.fx.world().mode.hud[0].text.data())
        << "the crown wearer's line is starred";
    EXPECT_STREQ("GREEN 0/10", rig.fx.world().mode.hud[1].text.data());
}

// ===========================================================================
// Determinism + instruction budget
// ===========================================================================

namespace {

std::string run_mutant_bot_match(int ticks, bool* crowned)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 300, 300);
    fx.spawn_anchor(1, 400, 300);
    fx.spawn_anchor(2, 300, 400);
    fx.spawn_anchor(3, 400, 400);
    fx.world().ctf_requested_respawn_ticks = 30;
    for (int i = 0; i < ticks; ++i)
    {
        fx.tick(1);
        if (fx.world().mode.vars[kMutSlotPhase] == kPhaseMutant)
            *crowned = true;
    }
    EXPECT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesMutant, mutant_bot_match_is_deterministic_across_runs)
{
    bool crowned_first = false;
    bool crowned_second = false;
    const std::string first = run_mutant_bot_match(600, &crowned_first);
    const std::string second = run_mutant_bot_match(600, &crowned_second);
    ASSERT_TRUE(crowned_first)
        << "the close-quarters bot brawl must produce a crown (the run "
           "exercises the phase machine, not an idle map)";
    ASSERT_EQ(crowned_first, crowned_second);
    ASSERT_EQ(first, second)
        << "same seed + same arena must replay identically (gate, buffs, "
           "decay, director included)";
}

TEST_F(ModesMutant, full_mutant_tick_fits_a_tenth_of_the_instruction_budget)
{
    og::script::g_test_world_instruction_budget = 500000;
    {
        MutantRig rig(kMutantLevelA, ACT_RANDOM);
        rig.fx.world().ctf_requested_respawn_ticks = 30;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.crown_a();
        rig.fx.tick(45);  // 3 director cadences + upkeep + win/HUD phases
        EXPECT_FALSE(has_script_error(rig.fx.world(), "instruction budget"))
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

TEST_F(ModesMutant, match_replicates_to_a_client_mirror_without_hash_strikes)
{
    MutantRig rig(kMutantLevelA, ACT_RANDOM);
    ModeMirror mirror(kMutantLevelA);

    const MirrorReplication replication =
        replicate_to_mirror(rig.fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick;
    ASSERT_TRUE(rig.active());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(rig.fx.world().mode.vars[slot],
                  mirror.world().mode.vars[slot])
            << "mode var slot " << slot;
    }
}
