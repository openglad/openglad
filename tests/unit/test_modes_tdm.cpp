// The Team Deathmatch campaign-pack Lua behavior suite
// (tools/modes_mapgen/pack: lib/mode_tdm_impl.lua + lib/mode_match.lua +
// scripts/mode_tdm.lua), over the shared modes-pack fixture. Rule spec:
// modes.md §3 as amended (manifest limits 20/7200, 1 m_score per frag,
// cadence 15, lowest-team-byte timeout tiebreak).
//
// Levels: 9101/9102 bind through the test manifest rows; 302 and 305 bind
// through the SHIPPED scripts/mode_tdm.lua registration over the committed
// manifest (the mode_match.rows_for adapter), so those cases also pin the
// production registration path.

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

// The mode-var slot map of lib/mode_tdm_impl.lua (table S).
enum TdmSlot : int {
    kTdmSlotModeId = 0,
    kTdmSlotPhase = 1,
    kTdmSlotTeamMask = 8,
    kTdmSlotScoreLimit = 9,
    kTdmSlotTimeLimit = 10,
    kTdmSlotRespawnTicks = 11,
    kTdmSlotAnchorCursor = 12,
    kTdmSlotKills = 13,  // +team
};

inline constexpr int kModeIdTdm = 1;  // mode_core.MODE.TDM
inline constexpr int kTdmCadence = 15;
inline constexpr float kGenPauseOffset = 30000.0f;

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
        ((world.tick_count_ / kTdmCadence) + 1) * kTdmCadence;
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

// A 2-team rig: anchors + one living per team, initialized on first tick.
struct TdmRig
{
    ModesCtfWorld fx;
    walker* red = nullptr;    // team 0
    walker* green = nullptr;  // team 1

    explicit TdmRig(int level_id = kTdmLevelA, int red_act = ACT_CONTROL,
                    int green_act = ACT_CONTROL)
        : fx(level_id)
    {
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 544, 800);
        red = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, red_act);
        green = fx.spawn_living(FAMILY_ORC, 1, 216, 200, green_act);
        EXPECT_NE(nullptr, red);
        EXPECT_NE(nullptr, green);
    }

    bool active() const
    {
        return fx.world().mode.vars[kTdmSlotModeId] == kModeIdTdm;
    }

    int kills(int team) const
    {
        return fx.world().mode.vars[kTdmSlotKills + team];
    }

    // A real attributed kill through walker::attack (stamps the D3
    // channel and runs walker::death -> on_entity_death).
    void slay(walker* attacker, walker* victim)
    {
        ASSERT_NE(nullptr, victim->stats());
        victim->stats()->set_hitpoints(1.0f);
        attacker->attack(victim);
        ASSERT_TRUE(victim->dead()) << "the rigged blow must be lethal";
    }
};

}  // namespace

using ModesTdm = ModesPackTest;

// ===========================================================================
// Activation / init / config resolution
// ===========================================================================

TEST_F(ModesTdm, lazy_init_activates_and_resolves_defaults)
{
    TdmRig rig;
    ASSERT_FALSE(rig.fx.world().mode.active);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.fx.world().mode.active);
    EXPECT_TRUE(rig.active());
    EXPECT_EQ(3, rig.fx.var(kTdmSlotTeamMask));
    EXPECT_EQ(20, rig.fx.var(kTdmSlotScoreLimit)) << "T default score limit";
    EXPECT_EQ(7200, rig.fx.var(kTdmSlotTimeLimit)) << "T default time limit";
    EXPECT_EQ(120, rig.fx.var(kTdmSlotRespawnTicks));
    EXPECT_EQ(1, rig.fx.var(kTdmSlotPhase));
    EXPECT_STREQ("TDM", rig.fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(rig.fx.events, "DEATHMATCH! FIRST TO 20"));
    EXPECT_FALSE(has_script_error(rig.fx.world(), "tdm:"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesTdm, init_demotes_below_two_teams)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    walker* only = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.tick(1);

    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(has_script_error(fx.world(), "fewer than two teams"));
    EXPECT_FALSE(only->dead()) << "demoted init leaves the map untouched";
    fx.tick(5);
    EXPECT_FALSE(fx.world().mode.active) << "the demotion is latched";
}

// The SHIPPED registration path: scripts/mode_tdm.lua binds the committed
// manifest through mode_match.rows_for, so a world on a real TDM level id
// activates with the manifest row's limits without any test-side rows.
TEST_F(ModesTdm, shipped_manifest_registration_binds_level_302)
{
    TdmRig rig(302);
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active())
        << "scripts/mode_tdm.lua must bind manifest id 302";
    EXPECT_EQ(20, rig.fx.var(kTdmSlotScoreLimit)) << "manifest score_limit";
    EXPECT_EQ(7200, rig.fx.var(kTdmSlotTimeLimit)) << "manifest time_limit";
}

TEST_F(ModesTdm, playtest_pace_cuts_bind_303_and_304)
{
    // The B3 sweep verdicts: GATEKEEPERS (5/6 timeouts) and THE CASTLE
    // (6/6 timeouts, ~1 frag/min) end by score, not clock, at these caps.
    {
        TdmRig rig(303);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        EXPECT_EQ(12, rig.fx.var(kTdmSlotScoreLimit))
            << "GATEKEEPERS score_limit 20 -> 12 (B3)";
    }
    {
        TdmRig rig(304);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        EXPECT_EQ(10, rig.fx.var(kTdmSlotScoreLimit))
            << "THE CASTLE score_limit 20 -> 10 (B3)";
    }
}

TEST_F(ModesTdm, explicit_requests_override_manifest_and_defaults)
{
    TdmRig rig(302);
    rig.fx.world().ctf_requested_capture_limit = 5;
    rig.fx.world().ctf_requested_respawn_ticks = 60;
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active());
    EXPECT_EQ(5, rig.fx.var(kTdmSlotScoreLimit)) << "request beats manifest";
    EXPECT_EQ(60, rig.fx.var(kTdmSlotRespawnTicks))
        << "submenu delay is honored";
}

TEST_F(ModesTdm, init_strips_score_range_only_and_keeps_wildlife)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    walker* stripped = fx.spawn_living(FAMILY_ORC, 2, 200, 760);
    walker* kept_hero = fx.spawn_hero(FAMILY_SOLDIER, 2, 240, 760, 7);
    walker* wildlife = fx.spawn_living(FAMILY_ORC, 5, 400, 400);
    walker* wild_gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, wild_gen);
    wild_gen->setxy(432, 432);
    wild_gen->set_team_num(5);
    fx.world().ctf_requested_team_count = 2;
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_EQ(3, fx.var(kTdmSlotTeamMask)) << "first two authored teams";
    EXPECT_TRUE(stripped->dead()) << "inactive score team is stripped";
    EXPECT_FALSE(kept_hero->dead()) << "roster walkers are never stripped";
    EXPECT_FALSE(wildlife->dead()) << "wildlife is arena identity (D16)";
    EXPECT_FALSE(wild_gen->dead()) << "wildlife generators stay";
}

TEST_F(ModesTdm, empty_active_teams_field_five_bot_squads)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(0, 160, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_anchor(1, 480, 800);
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
}

// --- Shared init helpers (lib/mode_strip, core.normalize_generator_hp) -----

TEST_F(ModesTdm, scenario_troops_strip_runs_before_the_bot_census)
{
    // Ordering pin: the shared strip precedes the empty-team census, so a
    // team the strip empties comes back as a bot squad instead of standing
    // the match up with nobody on it.
    ModesCtfWorld fx(kTdmLevelA);
    fx.world().ctf_requested_strip_scenario_troops = 2;
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* troop = fx.spawn_living(FAMILY_ORC, 1, 520, 780);
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_FALSE(hero->dead()) << "roster walkers are never stripped";
    EXPECT_TRUE(troop->dead()) << "STRIP_ALL takes the authored troop";
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the hero holds team 0 alone";
    EXPECT_EQ(5, alive_on_team(fx.world(), 1))
        << "the census behind the strip backfills the emptied team";
}

// ===========================================================================
// Frag scoring (the killer channel) — every arm of the ledger
// ===========================================================================

TEST_F(ModesTdm, frag_bumps_kills_and_awards_one_point)
{
    TdmRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    rig.slay(rig.red, rig.green);
    EXPECT_EQ(1, rig.kills(0)) << "killer team frags";
    EXPECT_EQ(0, rig.kills(1));
    EXPECT_TRUE(has_score_change(rig.fx.events, 0, 1))
        << "the frag awards exactly 1 point through og.award_score (the "
           "engine's own per-damage credit is separate and untouched)";
}

TEST_F(ModesTdm, teamkill_decrements_and_awards_nothing)
{
    // Same-team attacks are refused at the engine's root-team gate, so a
    // real teamkill happens through attribution: the fatal-stamp channel
    // recorded a cross-team hit, then the victim changed team (charm
    // shapes) before dying.
    TdmRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    ASSERT_NE(nullptr, rig.green->stats());
    rig.green->stats()->set_hitpoints(100.0f);
    rig.red->attack(rig.green);  // stamps killer red, team 0
    ASSERT_LT(rig.green->stats()->hitpoints(), 100.0f);
    rig.green->set_team_num(0);  // the charm flip
    rig.green->set_dead(1);
    rig.green->death();

    EXPECT_EQ(-1, rig.kills(0)) << "teamkill decrements, may go negative";
    EXPECT_FALSE(has_score_change(rig.fx.events, 0, 1))
        << "no frag point for a teamkill";
}

TEST_F(ModesTdm, environment_death_scores_nothing)
{
    // Also the suicide shape: the engine's root-team gate means
    // self-damage never lands, so a self-inflicted death always arrives
    // here with a nil killer.
    TdmRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    rig.green->set_dead(1);
    rig.green->death();
    EXPECT_EQ(0, rig.kills(0));
    EXPECT_EQ(0, rig.kills(1));
    EXPECT_EQ(0, rig.fx.world().m_score[0]);
}

TEST_F(ModesTdm, wildlife_victims_and_wildlife_killers_score_nothing)
{
    TdmRig rig;
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 400);
    ASSERT_NE(nullptr, wildlife);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // Killing wildlife earns nothing.
    rig.slay(rig.red, wildlife);
    EXPECT_EQ(0, rig.kills(0));
    EXPECT_FALSE(has_score_change(rig.fx.events, 0, 1))
        << "no frag point for a wildlife kill";

    // A wildlife killer earns nothing for byte 5.
    walker* wildlife2 = rig.fx.spawn_living(FAMILY_ORC, 5, 232, 200);
    ASSERT_NE(nullptr, wildlife2);
    rig.slay(wildlife2, rig.green);
    EXPECT_EQ(0, rig.kills(0));
    EXPECT_EQ(0, rig.kills(1));
}

TEST_F(ModesTdm, owned_spawn_victims_score_nothing_and_get_scrubbed)
{
    TdmRig rig;
    walker* gen = rig.fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen);
    gen->setxy(432, 432);
    gen->set_team_num(1);
    walker* spawn = rig.fx.spawn_living(FAMILY_SKELETON, 1, 260, 200);
    ASSERT_NE(nullptr, spawn);
    spawn->set_owner(gen);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // A fresh stain under the doomed spawn: the scrub must kill it.
    walker* stain = rig.fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    stain->setxy(260, 200);
    stain->set_team_num(1);

    rig.slay(rig.red, spawn);
    EXPECT_EQ(0, rig.kills(0)) << "gen-spawn victims never award frags";
    EXPECT_FALSE(has_score_change(rig.fx.events, 0, 1));
    EXPECT_TRUE(stain->dead()) << "non-respawning body scrubs its stain";
    for (const auto& entry : rig.fx.world().respawn.respawn_queue)
    {
        EXPECT_NE(entry.walker_entity_id, spawn->entity_id())
            << "an owned spawn is the generator's business, not the queue's";
    }
}

// The orphan fountain: clear_stale_cross_refs nulls owner() the tick the
// generator dies, so an owner-null eligibility test cannot tell a dead
// tent's leftover spawns from the init-time bots. On 305 — the one shipped
// TDM map with score-team generators — that adopted them as respawning
// competitors, and every re-kill paid another frag toward the 20-frag win.
TEST_F(ModesTdm, orphaned_generator_spawns_stay_out_of_the_frag_ledger)
{
    TdmRig rig;
    walker* gen = rig.fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen);
    gen->setxy(432, 432);
    gen->set_team_num(1);
    walker* spawn = rig.fx.spawn_living(FAMILY_SKELETON, 1, 260, 200);
    ASSERT_NE(nullptr, spawn);
    spawn->set_owner(gen);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // The shipped shape, reproduced as its end state. living::act kills a
    // living whose owner is dead, so a spawn only becomes an orphan when the
    // generator dies AFTER it has acted for the tick — which is the ordinary
    // case, since weaplist acts after oblist and arrows are how generators
    // die. Those spawns finish the tick untouched and end-of-tick
    // clear_stale_cross_refs then nulls their owner.
    gen->set_dead(1);
    spawn->set_owner(nullptr);
    ASSERT_EQ(nullptr, spawn->owner()) << "the orphan link is severed";
    ASSERT_FALSE(spawn->dead());

    walker* stain = rig.fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    stain->setxy(260, 200);
    stain->set_team_num(1);

    rig.slay(rig.red, spawn);
    EXPECT_EQ(0, rig.kills(0)) << "an orphan is still the generator's spawn";
    EXPECT_FALSE(has_score_change(rig.fx.events, 0, 1));
    EXPECT_TRUE(stain->dead()) << "a body that never respawns scrubs";

    // Neither the death hook nor the per-tick scan may adopt it.
    rig.fx.tick(4);
    for (const auto& entry : rig.fx.world().respawn.respawn_queue)
    {
        EXPECT_NE(entry.walker_entity_id, spawn->entity_id())
            << "an orphaned spawn must never enter the respawn queue";
    }

    // The guard must not over-block: an ordinary unowned competitor still
    // respawns and still scores.
    rig.slay(rig.red, rig.green);
    EXPECT_EQ(1, rig.kills(0)) << "a real competitor still pays a frag";
    bool green_queued = false;
    for (const auto& entry : rig.fx.world().respawn.respawn_queue)
    {
        if (entry.walker_entity_id == rig.green->entity_id())
            green_queued = true;
    }
    EXPECT_TRUE(green_queued) << "a real competitor still respawns";
}

// The same severed-owner shape without a generator: a summon outlives its
// summoner, so the durable mark has to come from the mode's own tick scan.
TEST_F(ModesTdm, orphaned_summons_stay_out_of_the_frag_ledger)
{
    TdmRig rig;
    walker* summoner = rig.fx.spawn_living(FAMILY_CLERIC, 1, 300, 300);
    ASSERT_NE(nullptr, summoner);
    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, 1, 316, 300);
    ASSERT_NE(nullptr, pet);
    pet->set_owner(summoner);
    rig.fx.tick(2);
    ASSERT_TRUE(rig.active());

    // Same end state as above: the summoner falls after the pet has acted,
    // and the end-of-tick sweep nulls the link.
    summoner->set_dead(1);
    pet->set_owner(nullptr);
    ASSERT_EQ(nullptr, pet->owner()) << "the summon is orphaned";
    ASSERT_FALSE(pet->dead());

    rig.slay(rig.red, pet);
    EXPECT_EQ(0, rig.kills(0)) << "an orphaned summon is not a competitor";
    rig.fx.tick(4);
    for (const auto& entry : rig.fx.world().respawn.respawn_queue)
    {
        EXPECT_NE(entry.walker_entity_id, pet->entity_id())
            << "an orphaned summon must never enter the respawn queue";
    }
}

TEST_F(ModesTdm, killer_outside_the_active_mask_scores_nothing)
{
    TdmRig rig;
    rig.fx.world().ctf_requested_team_count = 2;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(3, rig.fx.var(kTdmSlotTeamMask));

    // A post-init team-2 interloper (not in the mask) kills green.
    walker* outsider = rig.fx.spawn_living(FAMILY_ORC, 2, 232, 200);
    ASSERT_NE(nullptr, outsider);
    rig.slay(outsider, rig.green);
    EXPECT_EQ(0, rig.kills(0));
    EXPECT_EQ(0, rig.kills(1));
    EXPECT_EQ(0, rig.kills(2));
}

// ===========================================================================
// Win / timeout / latch
// ===========================================================================

TEST_F(ModesTdm, score_limit_win_latches_and_revives_pending_corpses)
{
    TdmRig rig(kTdmLevelA, ACT_CONTROL, ACT_CONTROL);
    walker* hero = rig.fx.spawn_hero(FAMILY_SOLDIER, 0, 260, 260, 11);
    ASSERT_NE(nullptr, hero);
    rig.fx.world().ctf_requested_respawn_ticks = 5000;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    hero->set_dead(1);
    rig.fx.tick(1);
    ASSERT_EQ(1u, rig.fx.world().respawn.respawn_queue.size());

    rig.fx.world().mode.vars[kTdmSlotKills + 0] = 20;
    rig.fx.tick(1);
    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(0, rig.fx.world().ending);
    EXPECT_EQ(0, rig.fx.world().mode.winner_team);
    EXPECT_TRUE(rig.fx.world().mode.winner_is_player);
    EXPECT_EQ(kTdmLevelA + 1, rig.fx.world().next_level);
    EXPECT_FALSE(hero->dead()) << "the win latch flush-revives (D2)";
    EXPECT_TRUE(has_notification(rig.fx.events, "RED TEAM WINS!"));

    for (int i = 0; i < 20; ++i)
    {
        rig.fx.tick(1);
        ASSERT_TRUE(rig.fx.world().game_ended) << "tick " << i;
        ASSERT_EQ(kTdmLevelA + 1, rig.fx.world().next_level);
    }
}

TEST_F(ModesTdm, bot_only_win_is_rematch_shape)
{
    TdmRig rig(kTdmLevelA, ACT_RANDOM, ACT_RANDOM);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    rig.fx.world().mode.vars[kTdmSlotKills + 1] = 20;
    rig.fx.tick(1);
    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(1, rig.fx.world().mode.winner_team);
    EXPECT_FALSE(rig.fx.world().mode.winner_is_player);
    EXPECT_EQ(kTdmLevelA, rig.fx.world().next_level)
        << "bot win replays the same map";
}

TEST_F(ModesTdm, timeout_picks_leader_and_ties_go_to_lowest_byte)
{
    // Leading KILLS wins.
    {
        TdmRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kTdmSlotKills + 0] = 2;
        rig.fx.world().mode.vars[kTdmSlotKills + 1] = 3;
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(1, rig.fx.world().mode.winner_team);
    }
    // Tied KILLS: m_score is the middle rung of the shared ladder
    // (modes.md §8.2 — mode metric, then m_score, then lowest byte).
    {
        TdmRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kTdmSlotKills + 0] = 3;
        rig.fx.world().mode.vars[kTdmSlotKills + 1] = 3;
        rig.fx.world().m_score[1] = 900;
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(1, rig.fx.world().mode.winner_team)
            << "equal frags break on m_score before the team byte";
    }
    // Tied KILLS and tied m_score: the LOWEST team byte, the last rung.
    {
        TdmRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        rig.fx.world().mode.vars[kTdmSlotKills + 0] = 3;
        rig.fx.world().mode.vars[kTdmSlotKills + 1] = 3;
        rig.fx.world().m_score[0] = 900;
        rig.fx.world().m_score[1] = 900;
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(0, rig.fx.world().mode.winner_team)
            << "an all-square tie resolves to the lowest team byte";
    }
}

// ===========================================================================
// Respawns (Lua eligibility + engine queue + anchor-cursor placement)
// ===========================================================================

TEST_F(ModesTdm, dead_hero_revives_at_team_anchor_with_cursor_rotation)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 256, 256);
    fx.spawn_anchor(0, 320, 256);
    fx.spawn_anchor(1, 544, 800);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 7);
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    fx.world().ctf_requested_respawn_ticks = 10;
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    ASSERT_EQ(10, fx.var(kTdmSlotRespawnTicks));

    hero->set_dead(1);
    fx.tick(1);
    EXPECT_EQ(1u, fx.world().respawn.respawn_queue.size())
        << "the death scan schedules the silent corpse";
    const std::int32_t cursor_before = fx.var(kTdmSlotAnchorCursor);

    fx.tick(10);
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(256, hero->xpos()) << "on_respawn placed it at anchor 0";
    EXPECT_EQ(256, hero->ypos());
    EXPECT_GT(fx.var(kTdmSlotAnchorCursor), cursor_before);

    hero->set_dead(1);
    fx.tick(11);
    EXPECT_FALSE(hero->dead());
    EXPECT_EQ(320, hero->xpos()) << "the cursor rotated to anchor 1";
    EXPECT_EQ(256, hero->ypos());
}

TEST_F(ModesTdm, combat_kill_schedules_through_the_death_hook)
{
    TdmRig rig;
    rig.fx.world().ctf_requested_respawn_ticks = 30;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(0u, rig.fx.world().respawn.respawn_queue.size());

    // The kill happens OUTSIDE any world tick: only on_entity_death can
    // have queued the corpse here (the per-tick scan has not run).
    rig.slay(rig.red, rig.green);
    EXPECT_EQ(1u, rig.fx.world().respawn.respawn_queue.size())
        << "on_entity_death schedules the corpse immediately";
}

// ===========================================================================
// Turn undead through the damage gate (walker_specials.cpp)
// ===========================================================================

namespace {

// walker::turn_undead rolls rng(range*40) > rng(level*10) per target. With a
// level-1 victim and a 200px range the roll lands overwhelmingly; a bounded
// retry keeps the test off the RNG's exact stream without ever spinning.
int turn_undead_until_it_lands(walker* cleric, walker* victim)
{
    int killed = 0;
    for (int attempt = 0; attempt < 8 && killed <= 0; ++attempt)
    {
        if (victim->dead())
            break;
        killed = static_cast<int>(cleric->turn_undead(200, 1));
    }
    return killed;
}

}  // namespace

// The classic instant kill sets dead BEFORE walker::attack, whose dead-target
// early-out then skips the whole hit path — so this kill class never reached
// the death hook and the corpse was never respawn-scheduled for the rest of
// the match. Scripted matches now route it through the gate, the attribution
// stamp and walker::death.
TEST_F(ModesTdm, turn_undead_kill_scores_the_frag_and_schedules_the_corpse)
{
    TdmRig rig;
    rig.fx.world().ctf_requested_respawn_ticks = 30;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(0u, rig.fx.world().respawn.respawn_queue.size());

    walker* cleric = rig.fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = rig.fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    ASSERT_NE(nullptr, undead->stats());
    undead->stats()->set_level(1);

    ASSERT_GT(turn_undead_until_it_lands(cleric, undead), 0)
        << "the turn-undead roll must land within eight attempts";

    EXPECT_TRUE(undead->dead());
    EXPECT_TRUE(undead->death_called())
        << "scripted matches run the real death so the mode hooks see it";
    EXPECT_EQ(cleric->entity_id(), undead->last_attacker_id())
        << "the kill is attributed to the cleric";
    EXPECT_EQ(1, rig.kills(0)) << "the frag lands on the cleric's team";
    EXPECT_EQ(0, rig.kills(1));
    EXPECT_TRUE(has_score_change(rig.fx.events, 0, 1))
        << "a turn-undead frag awards its point like any other";

    bool queued = false;
    for (const og::sim::RespawnEntry& entry :
         rig.fx.world().respawn.respawn_queue)
    {
        if (entry.walker_entity_id == undead->entity_id())
            queued = true;
    }
    EXPECT_TRUE(queued) << "on_entity_death schedules the corpse immediately";
}

// The classic branch is untouched: no gate dispatch, no attribution stamp,
// no walker::death — the engine's own scan owns the corpse (parity 256/256).
TEST_F(ModesTdm, classic_worlds_keep_the_legacy_turn_undead_prekill)
{
    TdmRig rig;
    // Never scripted: the mode cannot init, so this is a classic world on a
    // level that DOES carry mode hooks.
    rig.fx.world().type = 0;
    rig.fx.tick(1);
    ASSERT_FALSE(rig.active()) << "no TYPE_SCRIPTED, no mode";

    walker* cleric = rig.fx.spawn_living(FAMILY_CLERIC, 0, 200, 264);
    walker* undead = rig.fx.spawn_living(FAMILY_SKELETON, 1, 216, 264);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    ASSERT_NE(nullptr, undead->stats());
    undead->stats()->set_level(1);

    ASSERT_GT(turn_undead_until_it_lands(cleric, undead), 0)
        << "the classic roll still kills";

    EXPECT_TRUE(undead->dead());
    EXPECT_EQ(0, undead->death_called())
        << "the classic pre-kill leaves walker::death to the engine";
    EXPECT_EQ(0u, undead->last_attacker_id())
        << "the classic branch never stamps the attribution channel";
    EXPECT_EQ(0, rig.kills(0));
    EXPECT_EQ(0u, rig.fx.world().respawn.respawn_queue.size())
        << "no mode hook ran, so nothing was scheduled";
}

// ===========================================================================
// Generator spawn caps (D5 mechanism)
// ===========================================================================

TEST_F(ModesTdm, wildlife_cap_pauses_generator_and_resumes_below_cap)
{
    // Level 302's manifest caps: team byte 4 -> 8 live spawns.
    TdmRig rig(302);
    walker* gen = rig.fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen);
    gen->setxy(432, 432);
    gen->set_team_num(4);
    ASSERT_NE(nullptr, gen->stats());
    gen->stats()->set_level(20);
    walker* uncapped = rig.fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, uncapped);
    uncapped->setxy(700, 700);
    uncapped->set_team_num(6);  // no caps row for byte 6 on 302
    const float authored = gen->fire_frequency();
    walker* herd[8] = {};
    for (int i = 0; i < 8; ++i)
    {
        herd[i] = rig.fx.spawn_living(FAMILY_ORC, 4, 500 + 20 * i, 500);
        ASSERT_NE(nullptr, herd[i]);
    }
    // Crank the Bernoulli roll so an unpaused generator fires briskly —
    // the paused window below is then load-bearing, not idle luck.
    rig.fx.world().generator_rate = 10000;
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_GE(gen->fire_frequency(), kGenPauseOffset)
        << "at cap: the pause marker rides fire_frequency";
    EXPECT_GT(gen->busy(), 0.0f) << "busy top-up is the actual fire gate";
    EXPECT_LT(uncapped->fire_frequency(), kGenPauseOffset)
        << "no caps row for byte 6: untouched";

    // Cap enforcement starts at the first cadence pass; a fire during
    // the init tick itself may precede it. Pin STABILITY from the pause
    // on: the census must not grow while paused.
    const int at_pause = alive_on_team(rig.fx.world(), 4);
    ASSERT_GE(at_pause, 8);
    rig.fx.tick(90);
    EXPECT_EQ(at_pause, alive_on_team(rig.fx.world(), 4))
        << "a paused generator must not spawn";
    EXPECT_GE(gen->fire_frequency(), kGenPauseOffset);

    for (int i = 0; i < 5; ++i)
        herd[i]->set_dead(1);
    const int after_cull = alive_on_team(rig.fx.world(), 4);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(authored, gen->fire_frequency())
        << "below cap: the authored value is restored exactly";

    rig.fx.tick(300);
    EXPECT_GT(alive_on_team(rig.fx.world(), 4), after_cull)
        << "a resumed generator spawns again";
}

TEST_F(ModesTdm, score_team_cap_counts_only_generator_owned_livings)
{
    // Level 305's manifest caps: team bytes 0-3 -> 4 live spawns each.
    ModesCtfWorld fx(305);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen);
    gen->setxy(432, 432);
    gen->set_team_num(0);
    // Five free team-0 livings: they must NOT starve their own generator.
    for (int i = 0; i < 5; ++i)
        ASSERT_NE(nullptr, fx.spawn_living(FAMILY_SOLDIER, 0, 100 + 24 * i, 100));
    fx.spawn_living(FAMILY_SOLDIER, 1, 400, 700);
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_LT(gen->fire_frequency(), kGenPauseOffset)
        << "players do not count toward the spawn cap";

    // Four generator-owned spawns reach the cap.
    for (int i = 0; i < 4; ++i)
    {
        walker* spawn = fx.spawn_living(FAMILY_SKELETON, 0, 500 + 20 * i, 500);
        ASSERT_NE(nullptr, spawn);
        spawn->set_owner(gen);
    }
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_GE(gen->fire_frequency(), kGenPauseOffset)
        << "generator-owned livings do count";
}

// ===========================================================================
// AI director (hunt-nearest, endgame focus, player hands-off)
// ===========================================================================

TEST_F(ModesTdm, director_repairs_idle_bots_onto_nearest_enemy)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, ACT_RANDOM);
    walker* near_foe = fx.spawn_living(FAMILY_ORC, 1, 300, 200, ACT_RANDOM);
    walker* far_foe = fx.spawn_living(FAMILY_ORC, 1, 700, 800, ACT_RANDOM);
    walker* wildlife = fx.spawn_living(FAMILY_ORC, 5, 210, 210, ACT_RANDOM);
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, near_foe);
    ASSERT_NE(nullptr, far_foe);
    ASSERT_NE(nullptr, wildlife);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    bot->set_foe(nullptr);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(near_foe->entity_id(), bot->foe_id())
        << "idle bot re-acquires the nearest ACTIVE-team enemy (wildlife "
           "at 20px is not a target)";
}

TEST_F(ModesTdm, director_endgame_focus_retargets_preemptable_members)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_anchor(2, 96, 800);
    walker* bot = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, ACT_RANDOM);
    walker* leader_member = fx.spawn_living(FAMILY_ORC, 1, 700, 800, ACT_RANDOM);
    walker* decoy = fx.spawn_living(FAMILY_ORC, 2, 240, 200, ACT_RANDOM);
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, leader_member);
    ASSERT_NE(nullptr, decoy);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    ASSERT_EQ(7, fx.var(kTdmSlotTeamMask));

    // Team 1 is five frags from the limit: every preemptable member of
    // the other teams must deny the leader, nearest-member first.
    fx.world().mode.vars[kTdmSlotKills + 1] = 15;
    bot->set_foe(decoy);
    align_before_cadence(fx.world());
    fx.tick(1);
    EXPECT_EQ(leader_member->entity_id(), bot->foe_id())
        << "endgame focus retargets the leading team";
}

TEST_F(ModesTdm, director_never_touches_players)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* player = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200, ACT_CONTROL);
    fx.spawn_living(FAMILY_ORC, 1, 300, 200, ACT_RANDOM);
    ASSERT_NE(nullptr, player);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    // The engine's own backstop may hand players a foe (pre-existing);
    // the director must never issue them commands.
    align_before_cadence(fx.world());
    fx.tick(1);
    ASSERT_NE(nullptr, player->stats());
    EXPECT_TRUE(player->stats()->commands.empty())
        << "ACT_CONTROL walkers are never steered by the director";
}

// ===========================================================================
// HUD
// ===========================================================================

TEST_F(ModesTdm, hud_rows_carry_per_team_frag_lines)
{
    TdmRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_STREQ("RED 0/20", rig.fx.world().mode.hud[0].text.data());
    EXPECT_EQ(0, rig.fx.world().mode.hud[0].team);
    EXPECT_STREQ("GREEN 0/20", rig.fx.world().mode.hud[1].text.data());
    EXPECT_EQ(1, rig.fx.world().mode.hud[1].team);

    rig.slay(rig.red, rig.green);
    rig.fx.tick(1);
    EXPECT_STREQ("RED 1/20", rig.fx.world().mode.hud[0].text.data())
        << "the frag line tracks the ledger";
}

// ===========================================================================
// Determinism + instruction budget
// ===========================================================================

namespace {

std::string run_tdm_bot_match_digest(int ticks)
{
    ModesCtfWorld fx(302);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(0, 224, 96);
    fx.spawn_anchor(1, 96, 300);
    fx.spawn_anchor(1, 224, 300);
    walker* gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
    EXPECT_NE(nullptr, gen);
    gen->setxy(432, 432);
    gen->set_team_num(4);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(ticks);
    EXPECT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    return digest_world(fx.world());
}

}  // namespace

TEST_F(ModesTdm, tdm_bot_match_is_deterministic_across_runs)
{
    const std::string first = run_tdm_bot_match_digest(300);
    const std::string second = run_tdm_bot_match_digest(300);
    ASSERT_EQ(first, second)
        << "same seed + same arena must replay identically (director, "
           "caps, respawns included)";
}

TEST_F(ModesTdm, full_tdm_tick_fits_a_tenth_of_the_instruction_budget)
{
    og::script::g_test_world_instruction_budget = 500000;
    {
        ModesCtfWorld fx(302);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(0, 224, 96);
        fx.spawn_anchor(1, 96, 832);
        fx.spawn_anchor(1, 224, 832);
        walker* gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
        ASSERT_NE(nullptr, gen);
        gen->setxy(432, 432);
        gen->set_team_num(4);
        for (int i = 0; i < 6; ++i)
            fx.spawn_living(FAMILY_SKELETON, 4, 500 + 20 * i, 500, ACT_RANDOM);
        fx.world().ctf_requested_respawn_ticks = 30;
        fx.tick(1);  // init (bot squads, the priciest dispatch)
        ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
        fx.tick(45);  // 3 director cadences + every per-tick phase
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

TEST_F(ModesTdm, match_replicates_to_a_client_mirror_without_hash_strikes)
{
    TdmRig rig(kTdmLevelA, ACT_RANDOM, ACT_RANDOM);
    ModeMirror mirror(kTdmLevelA);

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
