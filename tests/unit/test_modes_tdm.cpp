// The Team Deathmatch campaign-pack Lua behavior suite
// (campaigns/modes/packs/modes.core: lib/mode_tdm_impl.lua + lib/mode_match.lua +
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
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
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
        return fx.world().mode.vars[static_cast<std::size_t>(kTdmSlotKills + team)];
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

// #241: the lobby TIME LIMIT knob beats the manifest row. Down, here —
// 302's 7200-tick clock cut to five minutes.
TEST_F(ModesTdm, time_limit_knob_overrides_the_manifest_row)
{
    TdmRig rig(302);
    rig.fx.world().ctf_requested_time_limit = 3600;
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active());
    EXPECT_EQ(3600, rig.fx.var(kTdmSlotTimeLimit))
        << "the request beats the row's 7200";
    EXPECT_EQ(20, rig.fx.var(kTdmSlotScoreLimit))
        << "and moves no other knob";
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
    walker* kept_hero = fx.spawn_hero(FAMILY_SOLDIER, 1, 240, 760, 7);
    walker* wildlife = fx.spawn_living(FAMILY_ORC, 5, 400, 400);
    walker* wild_gen = fx.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, wild_gen);
    wild_gen->setxy(432, 432);
    wild_gen->set_team_num(5);
    // Team 2 leaves through MAP UNITS off + FILL: NONE (amendment B4 —
    // the retired OFF wheel value's successor); the hero sits on an on
    // team, where nothing is ever stripped.
    fx.world().ctf_requested_fill[2] = og::sim::kFillNone;
    fx.world().ctf_requested_map_units[2] = og::sim::kMapUnitsOff;
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_EQ(3, fx.var(kTdmSlotTeamMask)) << "the two teams left on";
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

// --- Shared init helpers (lib/mode_strip) ---------------------------------

TEST_F(ModesTdm, scenario_troops_strip_runs_before_the_bot_census)
{
    // Ordering pin: the shared strip precedes the empty-team census, so a
    // team the strip empties comes back as a bot squad instead of standing
    // the match up with nobody on it.
    ModesCtfWorld fx(kTdmLevelA);
    fx.world().ctf_requested_map_units[1] = og::sim::kMapUnitsOff;
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* troop = fx.spawn_living(FAMILY_ORC, 1, 520, 780);
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_FALSE(hero->dead()) << "roster walkers are never stripped";
    EXPECT_TRUE(troop->dead()) << "the box off takes the authored troop";
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the hero holds team 0 alone";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the census behind the strip backfills the emptied team, sized "
           "to the roster headcount (B2)";
}

TEST_F(ModesTdm, rosters_on_authored_teams_field_all_authored_sides)
{
    // The scen-841 shape on TDM: rosters on teams 0 and 2 of a
    // three-anchor map field all THREE authored sides — the rosters stay
    // untouched (equal companies, so no allies gap) and the unrostered
    // team backfills with a squad matched to the roster headcount (B2).
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    walker* soldier = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* other = fx.spawn_hero(FAMILY_SOLDIER, 2, 216, 760, 2);
    fx.tick(1);

    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_EQ(1 + 2 + 4, fx.var(kTdmSlotTeamMask))
        << "every authored side plays: rosters plus the backfilled team";
    EXPECT_FALSE(soldier->dead());
    EXPECT_FALSE(other->dead());
    EXPECT_EQ(1, alive_on_team(fx.world(), 0)) << "the rosters stay as-is";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the empty-team census fields a squad at the roster headcount";
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesTdm, init_stamps_the_generator_hp_denominator)
{
    // The mini HP bar skips outright at max_hp == 0, which is what the
    // loader ships FAMILY_TOWER with. walker::set_difficulty now stamps the
    // denominator along with the fighting hp, so a damaged post reads as a
    // fraction of its authored hp instead of showing no bar at all.
    TdmRig rig;
    walker* tower = rig.fx.spawn_generator(FAMILY_TOWER, 0, 304, 400, 2);
    ASSERT_NE(nullptr, tower);
    ASSERT_NE(nullptr, tower->stats());
    const float authored = tower->stats()->hitpoints();
    ASSERT_GT(authored, 0.0f);
    ASSERT_EQ(authored, tower->stats()->max_hitpoints())
        << "the engine stamps the denominator at set_difficulty";

    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_FALSE(tower->dead()) << "an active team's generator is kept";
    EXPECT_EQ(authored, tower->stats()->max_hitpoints())
        << "and mode init leaves it alone";

    tower->stats()->set_hitpoints(authored / 2.0f);
    EXPECT_EQ(authored, tower->stats()->max_hitpoints())
        << "damage moves hp, never the authored denominator";
    EXPECT_LT(tower->stats()->hitpoints(), tower->stats()->max_hitpoints())
        << "so the bar has a fraction to draw";
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
        EXPECT_EQ(rig.fx.world().mode.vars[static_cast<std::size_t>(slot)],
                  mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
}

// ===========================================================================
// mode_core team-count helper arms (matched-teams D18)
// ===========================================================================

namespace {

// The probe's own level id (nothing in the fixture manifest binds it) and
// the slots its init writes — no mode impl runs on this level, so the
// whole var file belongs to the probe.
inline constexpr int kHelperProbeLevel = 9091;
inline constexpr int kProbeSlotMatched = 61;
inline constexpr int kProbeSlotConstant = 62;
inline constexpr int kProbeSlotMask = 63;

// RAII registration of the helper probe, under the RULES pack id so
// og.use resolves the mounted mode_core module. Init classifies the FAIR
// sentinel off the strip field (matched-teams D29 — matching never rides
// the team count; core.team_count_request, the last knob-reading rule
// twin, is retired with the plan phase) and feeds the RAW
// og.match_setting count to core.activate_teams over the sparse authored
// mask 13 = {0, 2, 3} (the same domain the 9090 probe pins). The dtor
// swaps in an empty chunk so no later world in this binary re-registers
// level 9091 (the next test's mount clears the registry outright).
struct TeamCountProbeScript
{
    TeamCountProbeScript()
    {
        og::script::register_pack_script(
            {kRulesPackId, "zz_team_count_probe.lua",
             "local core = og.use(\"mode_core\")\n"
             "og.register_level_hooks(9091, {\n"
             "  on_mode_init = function(level)\n"
             "    local matched =\n"
             "        og.match_setting(\"strip_troops\") == core.MATCHED_TROOPS\n"
             "    og.mode_set(61, matched and 1 or 0)\n"
             "    og.mode_set(62, core.MATCHED_TROOPS)\n"
             "    og.mode_set(63, core.activate_teams(13,\n"
             "                    og.match_setting(\"team_count\")))\n"
             "  end,\n"
             "})\n"});
    }

    ~TeamCountProbeScript()
    {
        og::script::register_pack_script(
            {kRulesPackId, "zz_team_count_probe.lua", ""});
    }
};

}  // namespace

TEST_F(ModesTdm, retired_troops_sentinel_and_activate_teams_agree)
{
    // TROOPS is retired (amendment B5): the strip field is inert and no
    // mode Lua reads it, but core.MATCHED_TROOPS stays as the migration
    // prose twin of the C++ kTroopsMatched — this probe is the agreement
    // pin. It also pins core.activate_teams over the sparse authored
    // mask 13 = {0, 2, 3}: count 2 takes the first two authored teams,
    // count 3 all three.
    const struct
    {
        int count;
        int mask;
    } rows[] = {{2, 0b0101}, {3, 0b1101}};
    for (const auto& row : rows)
    {
        TeamCountProbeScript probe;
        ModesCtfWorld fx(kHelperProbeLevel);
        fx.world().ctf_requested_team_count = static_cast<short>(row.count);
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active) << "count " << row.count;
        EXPECT_EQ(row.mask, fx.var(kProbeSlotMask)) << "count " << row.count;
        EXPECT_EQ(0, fx.var(kProbeSlotMatched))
            << "the retired strip field reads 0: never the FAIR sentinel";
        EXPECT_EQ(og::sim::kTroopsMatched, fx.var(kProbeSlotConstant))
            << "core.MATCHED_TROOPS must equal the C++ sentinel";
        EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    }
}

// ===========================================================================
// TROOPS: FAIR power model (matched-teams WP-E, spec §4-§5 + §7)
// ===========================================================================

namespace {

// Log lines the 9095/9096 probes emit, filtered by their first tab field.
std::vector<std::string> matched_log_lines(GameWorld& world,
                                           const std::string& prefix)
{
    std::vector<std::string> out;
    for (const auto& line : world.scripts().host().log())
    {
        if (line.rfind(prefix + "\t", 0) == 0)
            out.push_back(line);
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos)
        {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

// Non-fatal on a missing field (a matched_log miss returns {}), so a lost
// probe line reads as its own clean failure and the rest of the test's
// assertions still run and print their evidence.
long long tab_int(const std::vector<std::string>& fields, std::size_t index)
{
    if (index >= fields.size())
    {
        ADD_FAILURE() << "probe field " << index << " missing (line has "
                      << fields.size() << " fields)";
        return 0;
    }
    return std::stoll(fields[index]);
}

// One line by prefix, split; asserts exactly one match.
std::vector<std::string> matched_log(GameWorld& world,
                                     const std::string& prefix)
{
    const auto lines = matched_log_lines(world, prefix);
    EXPECT_EQ(1u, lines.size()) << "probe line " << prefix;
    if (lines.empty())
        return {};
    return split_tabs(lines.front());
}

// The five squad families in spawn order, keyed to engine family bytes.
struct SquadLevels
{
    int soldier = 0;
    int archer = 0;
    int elf = 0;
    int mage = 0;
    int thief = 0;
};

SquadLevels squad_levels_on_team(GameWorld& world, int team)
{
    SquadLevels levels;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team))
            continue;
        if (w->stats() == nullptr)
            continue;
        const int level = w->stats()->level();
        switch (w->family())
        {
        case FAMILY_SOLDIER: levels.soldier = level; break;
        case FAMILY_ARCHER:  levels.archer = level; break;
        case FAMILY_ELF:     levels.elf = level; break;
        case FAMILY_MAGE:    levels.mage = level; break;
        case FAMILY_THIEF:   levels.thief = level; break;
        default: break;
        }
    }
    return levels;
}

// A fresh (level 1) roster squad of the five bot families on one team —
// the §4.2 stock calibration point.
void author_fresh_squad(ModesCtfWorld& fx, int team, int y)
{
    fx.spawn_leveled_hero(FAMILY_SOLDIER, team, 200, y, 1, 1);
    fx.spawn_leveled_hero(FAMILY_ARCHER, team, 232, y, 2, 1);
    fx.spawn_leveled_hero(FAMILY_ELF, team, 264, y, 3, 1);
    fx.spawn_leveled_hero(FAMILY_MAGE, team, 296, y, 4, 1);
    fx.spawn_leveled_hero(FAMILY_THIEF, team, 328, y, 5, 1);
}

}  // namespace

// Pure-function arms over the 9095 probe: the walker_power zero-offense
// floor, the measured bot base (§4.2: armor 0, mp 50 at bot base — the
// statistics default the loader leaves in place), the predicted_power
// tuple/default/half-armor arms, every solver arm with its tie-break, and
// both map_units_for arms. All pinned values are DERIVED — the probe
// computes them from the shipped tuples over the shipped loader base
// (soldier 120/50/0/20/4/6), and the comments show the arithmetic.
TEST_F(ModesTdm, matched_power_metric_and_solver_arms)
{
    ModesCtfWorld fx(kMatchProbeLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active) << "the probe init must succeed";
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    const auto latch = matched_log(fx.world(), "census_latch");
    EXPECT_EQ(4242, tab_int(latch, 1))
        << "a stored target must survive a repeat census attempt (D24)";
    EXPECT_EQ(0, tab_int(latch, 2))
        << "the TARGET latch guards SIZE too — no repeat bank wrote it";
    EXPECT_EQ(0, fx.var(kSlotMatchedSize))
        << "the TARGET latch guards SIZE too — no repeat census wrote it";

    // Census of a world with no has_guy walker: T = 0, all team sums 0
    // (the headcount half of the old census — H — lives in the shared
    // activation rule alone: match.activation's matched_size, pinned by
    // the staged-rules suite in test_staged_rules.cpp).
    const auto census = matched_log(fx.world(), "census");
    ASSERT_EQ(6u, census.size());
    EXPECT_EQ(0, tab_int(census, 1)) << "no humans -> T = 0 (E1)";
    for (std::size_t i = 2; i < 6; ++i)
        EXPECT_EQ(0, tab_int(census, i));

    // Zero-offense floor: OFF = 0 collapses f to the pool EHP =
    // hp + mp / 2 (armor is 0 at bot base).
    const auto floor_line = matched_log(fx.world(), "wp_floor");
    ASSERT_EQ(4u, floor_line.size());
    EXPECT_EQ(tab_int(floor_line, 2) + tab_int(floor_line, 3) / 2,
              tab_int(floor_line, 1))
        << "f of a zero-offense walker is exactly its pool (the +60 arm)";
    EXPECT_GT(tab_int(floor_line, 1), 0);

    // The measured bot base. The spec's §4.2 aside claimed bot base mp is
    // 0 (the families declare init_max_magicpoints = 0) — MEASURED reality
    // is mp 50, the statistics default the loader leaves in place. This is
    // exactly why D13 measures instead of shipping constants; armor 0 does
    // hold, so the 4*A term vanishes at bot base while M/2 does not.
    const auto base = matched_log(fx.world(), "base");
    ASSERT_EQ(7u, base.size());
    EXPECT_EQ(120, tab_int(base, 1)) << "soldier loader hp";
    EXPECT_EQ(50, tab_int(base, 2)) << "bot base mp (statistics default)";
    EXPECT_EQ(0, tab_int(base, 3)) << "bot base armor is 0";
    EXPECT_EQ(20, tab_int(base, 4)) << "soldier melee damage";
    EXPECT_EQ(4, tab_int(base, 5)) << "soldier stepsize";
    EXPECT_EQ(6, tab_int(base, 6)) << "soldier fire delay";

    // predicted_power over that base (hp 120, mp 50, armor 0, dmg 20,
    // sp 4, ff 6 -> RATE 20):
    //  soldier L2 (13/8/5/2): H 172, M 82, D 30, A 8 -> ED 37, OFF 760,
    //    EHP 245 -> f = (245 * 820) / 60 = 3348
    //  unknown family -> default row (11/11/4/2): EHP 243, OFF 720 ->
    //    f = (243 * 780) / 60 = 3159
    //  mage L2 (7/14/3/0.5): armor trunc(2.0) = 2 -> EHP 209, OFF 660 ->
    //    f = (209 * 720) / 60 = 2508
    //  mage L3: armor trunc(4.5) = 4 (the half-armor floor) -> EHP 287,
    //    OFF 880 -> f = (287 * 940) / 60 = 4496
    const auto pred = matched_log(fx.world(), "pred");
    ASSERT_EQ(5u, pred.size());
    EXPECT_EQ(3348, tab_int(pred, 1)) << "soldier tuple row";
    EXPECT_EQ(3159, tab_int(pred, 2)) << "default tuple row";
    EXPECT_EQ(2508, tab_int(pred, 3)) << "mage row, integral armor term";
    EXPECT_EQ(4496, tab_int(pred, 4)) << "mage row, floored 0.5 * 9 armor";

    // Solver arms over synthetic bases (hp 100, dmg 20, sp 0, ff 6):
    // the tie target is the exact midpoint of P(3,0) and P(3,1) — parity 0
    // proves the tie is real, and tie-break answers the LOWER k (D22).
    const auto parity = matched_log(fx.world(), "solve_tie_parity");
    EXPECT_EQ(0, tab_int(parity, 1)) << "the tie case must be an exact tie";
    const auto tie = matched_log(fx.world(), "solve_tie");
    EXPECT_EQ(3, tab_int(tie, 1));
    EXPECT_EQ(0, tab_int(tie, 2)) << "ties break to lower k";
    EXPECT_EQ(0, tab_int(tie, 3));
    const auto exact = matched_log(fx.world(), "solve_exact");
    EXPECT_EQ(4, tab_int(exact, 1)) << "T = P(4,2) solves exactly";
    EXPECT_EQ(2, tab_int(exact, 2));
    EXPECT_EQ(0, tab_int(exact, 3));
    const auto low = matched_log(fx.world(), "solve_low");
    EXPECT_EQ(1, tab_int(low, 1)) << "T < B(1) clamps to uniform L1";
    EXPECT_EQ(0, tab_int(low, 2));
    EXPECT_EQ(1, tab_int(low, 3)) << "low clamp reports LIMIT";
    const auto high = matched_log(fx.world(), "solve_high");
    EXPECT_EQ(9, tab_int(high, 1)) << "T > B(9) clamps to uniform L9";
    EXPECT_EQ(0, tab_int(high, 2)) << "L9 admits no upgrades";
    EXPECT_EQ(1, tab_int(high, 3)) << "high clamp reports LIMIT";
    const auto n1 = matched_log(fx.world(), "solve_n1");
    EXPECT_EQ(4, tab_int(n1, 1)) << "n = 1 (mutant seat) solves on levels";
    EXPECT_EQ(0, tab_int(n1, 2));
    EXPECT_EQ(0, tab_int(n1, 3));

    // map_units_for: plan code 43 at team 2 -> first 3 members L5, rest
    // L4; team 0 unsolved -> the legacy formula (percent 100 -> L2).
    const auto blf = matched_log(fx.world(), "blf");
    ASSERT_EQ(5u, blf.size());
    EXPECT_EQ(5, tab_int(blf, 1));
    EXPECT_EQ(5, tab_int(blf, 2));
    EXPECT_EQ(4, tab_int(blf, 3));
    EXPECT_EQ(2, tab_int(blf, 4)) << "unsolved team takes the legacy level";
}

// --- Lineup arms (docs/lineup-design.md §3.2) ------------------------------
//
// A runtime-registered probe (the test_staged_rules RuleProbeScript
// discipline — registered pack scripts need no coverage-ledger entry)
// drives the pure lineup functions and the knob-aware spawn seam on probe
// level 9093. The world knobs are set by the C++ test BEFORE the first
// tick; mode vars 12/13/14 are the probe's cursor slots.
constexpr const char* kLineupProbeLua =
    "local match = og.use(\"mode_match\")\n"
    "local squad = { \"core:soldier\", \"core:archer\", \"core:elf\",\n"
    "                \"core:mage\", \"core:thief\" }\n"
    "local function opt(v)\n"
    "  if v == nil then\n"
    "    return -1\n"
    "  end\n"
    "  return v\n"
    "end\n"
    "og.register_level_hooks(9093, {\n"
    "  on_mode_init = function(level)\n"
    "    local sums = { 100, 250, 0, 40 }\n"
    "    og.log(\"target\", opt(match.fill_target(100, sums, 0, 100)),\n"
    "           opt(match.fill_target(100, sums, 1, 100)),\n"
    "           opt(match.fill_target(100, sums, 2, 100)),\n"
    "           opt(match.fill_target(100, sums, 2, 150)),\n"
    "           opt(match.fill_target(100, sums, 0, 150)),\n"
    "           opt(match.fill_target(0, sums, 2, 100)))\n"
    "    og.log(\"room\", opt(match.squad_room(5, 3)),\n"
    "           opt(match.squad_room(5, 9)), opt(match.squad_room(nil, 3)))\n"
    "    og.log(\"pct\", match.fill_percent(3), match.fill_percent(1),\n"
    "           match.fill_percent(2), match.fill_percent(4),\n"
    "           match.fill_percent(5), match.fill_percent(9))\n"
    "    og.log(\"off\", match.squad_off(1) and 1 or 0,\n"
    "           match.squad_off(3) and 1 or 0,\n"
    "           match.squad_off(5) and 1 or 0)\n"
    "    match.spawn_bots(0, squad, 15)\n"
    "    match.spawn_bots(1, squad, 12, nil, 3)\n"
    "    match.spawn_bots(3, squad, 14)\n"
    "  end,\n"
    "})\n";

struct LineupProbeScript
{
    LineupProbeScript()
    {
        og::script::register_pack_script(
            {og::modes_test::kRulesPackId, "zz_lineup_probe.lua",
             kLineupProbeLua});
    }
    ~LineupProbeScript()
    {
        og::script::register_pack_script(
            {og::modes_test::kRulesPackId, "zz_lineup_probe.lua", ""});
    }
};

// The pure lineup arms: fill_target's three shapes (allies gap, no gap,
// empty-team reference — each scaled by the wheel's percent), squad_room,
// the FILL_PERCENT table (junk degrades to FAIR's 100) and squad_off's
// NONE-only vocabulary (B8). Then the knob-aware spawn seam: the default
// squad truncates to the caller's cap, and NONE fields nothing.
TEST_F(ModesTdm, lineup_fill_target_percent_table_and_knobbed_spawns)
{
    LineupProbeScript probe;
    ModesCtfWorld fx(9093);
    for (int i = 0; i < 5; ++i)
    {
        fx.spawn_anchor(0, 96 + 96 * i, 384);
        fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.spawn_anchor(3, 96 + 96 * i, 288);
    }
    fx.world().ctf_requested_fill[3] = og::sim::kFillNone;  // team 3
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    const auto target = matched_log(fx.world(), "target");
    ASSERT_EQ(7u, target.size());
    EXPECT_EQ(150, tab_int(target, 1)) << "allies: best other 250 - own 100";
    EXPECT_EQ(-1, tab_int(target, 2))
        << "the strongest team has no gap: nil = no squad (B3)";
    EXPECT_EQ(100, tab_int(target, 3))
        << "an empty team targets the weakest reference";
    EXPECT_EQ(150, tab_int(target, 4)) << "BRUTAL scales the reference";
    EXPECT_EQ(225, tab_int(target, 5)) << "BRUTAL scales the allies gap";
    EXPECT_EQ(-1, tab_int(target, 6)) << "no reference, no solve";

    const auto room = matched_log(fx.world(), "room");
    EXPECT_EQ(2, tab_int(room, 1)) << "cap 5 - roster 3";
    EXPECT_EQ(0, tab_int(room, 2)) << "never below zero";
    EXPECT_EQ(-1, tab_int(room, 3)) << "no hard shape: no bound";

    const auto pct = matched_log(fx.world(), "pct");
    EXPECT_EQ(100, tab_int(pct, 1)) << "FAIR";
    EXPECT_EQ(0, tab_int(pct, 2)) << "NONE";
    EXPECT_EQ(75, tab_int(pct, 3)) << "WEAK";
    EXPECT_EQ(125, tab_int(pct, 4)) << "STRONG";
    EXPECT_EQ(150, tab_int(pct, 5)) << "BRUTAL";
    EXPECT_EQ(100, tab_int(pct, 6)) << "junk degrades to FAIR";

    const auto off = matched_log(fx.world(), "off");
    EXPECT_EQ(1, tab_int(off, 1)) << "NONE forbids the squad";
    EXPECT_EQ(0, tab_int(off, 2)) << "FAIR does not";
    EXPECT_EQ(0, tab_int(off, 3)) << "BRUTAL does not (B8: no refusals)";

    // The knob-aware spawns (no humans anywhere -> the legacy arm): the
    // full squad on team 0, the cap-truncated squad on team 1, nothing on
    // the NONE team — and the applied FAIR fact banked where a squad
    // walked on (since D1 the banked code IS the applied fill, 3).
    EXPECT_EQ(5, alive_on_team(fx.world(), 0));
    EXPECT_EQ(3, alive_on_team(fx.world(), 1))
        << "the squad truncates to the caller's cap";
    EXPECT_EQ(0, alive_on_team(fx.world(), 3)) << "NONE fields nothing";
    EXPECT_EQ(og::sim::kFillFair, (fx.var(kSlotMatchedAnnounced) / 10) % 100)
        << "team 0 banks the applied FAIR code";
    EXPECT_EQ(og::sim::kFillFair,
              (fx.var(kSlotMatchedAnnounced) / 1000) % 100)
        << "team 1 banks it too";
    EXPECT_EQ(0, (fx.var(kSlotMatchedAnnounced) / 10 / 1000000) % 100)
        << "the NONE team banks nothing";
    EXPECT_EQ(0, fx.var(kSlotMatchedPlan)) << "the legacy arm stores no plan";
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10)
        << "no solve ran, so nothing announced";
}

// The spawn seam's allies floor (B3): driven on an OCCUPIED team through
// the same probe — equal companies leave no gap, so spawn_bots returns
// without fielding or banking anything, while the empty team beside them
// still solves its capped single.
TEST_F(ModesTdm, lineup_spawn_seam_allies_no_gap_spawns_nothing)
{
    LineupProbeScript probe;
    ModesCtfWorld fx(9093);
    for (int i = 0; i < 5; ++i)
    {
        fx.spawn_anchor(0, 96 + 96 * i, 384);
        fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.spawn_anchor(3, 96 + 96 * i, 288);
    }
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 2, 200, 300, 2);
    fx.world().ctf_requested_fill[3] = og::sim::kFillNone;
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    ASSERT_EQ(0u, og::script::hooks::hook_failures().count);

    EXPECT_EQ(1, alive_on_team(fx.world(), 0))
        << "equal companies: no gap, no allies";
    EXPECT_EQ(0, (fx.var(kSlotMatchedAnnounced) / 10) % 100)
        << "nothing spawned on team 0, nothing banked";
    EXPECT_EQ(3, alive_on_team(fx.world(), 1))
        << "the empty team solves its cap-truncated squad (the probe "
           "banks no headcount, so the table rules)";
    EXPECT_NE(0, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the empty-team squad was SOLVED against the reference";
    EXPECT_EQ(1, alive_on_team(fx.world(), 2));
    EXPECT_EQ(0, alive_on_team(fx.world(), 3)) << "NONE fields nothing";
}

// The model-pin tripwire (D13/§8): for EVERY family with a TUPLE row (the
// 5 squad families plus the orc/beast/cleric/druid rows carried for future
// rosters) the probe spawns a real bot, applies s_set_level +
// set_difficulty at L in {1, 3, 5}, and logs measured-with-trunc f beside
// the tuple-table prediction. A core-pack family rebalance that drifts the
// copied tuples, or a misreading of the living::set_difficulty override,
// reds this out on day one. Percent 100 only — other percents would
// compare a scaled measurement to an unscaled model (§4.3).
TEST_F(ModesTdm, matched_model_pin_measured_within_ten_percent_of_predicted)
{
    ModesCtfWorld fx(kMatchProbeLevel);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    const auto lines = matched_log_lines(fx.world(), "modelpin");
    ASSERT_EQ(27u, lines.size())
        << "9 families (every TUPLE row, incl. the orc/beast/cleric/druid "
           "rows carried for future rosters) x 3 levels";
    for (const auto& line : lines)
    {
        const auto fields = split_tabs(line);
        ASSERT_EQ(5u, fields.size()) << line;
        const long long measured = tab_int(fields, 3);
        const long long predicted = tab_int(fields, 4);
        ASSERT_GT(predicted, 0) << line;
        const long long miss = measured > predicted ? measured - predicted
                                                    : predicted - measured;
        EXPECT_LE(miss * 10, predicted)
            << fields[1] << " L" << fields[2] << ": measured " << measured
            << " vs predicted " << predicted << " misses by more than 10%";
    }
}

// Census arms: with a single human team the REFERENCE (B3 — the weakest
// human team's f-sum) is that team's f-sum, and walkers without a guy
// record never enter it (the has_guy guard) — the two worlds differ by
// one big guy-less Living and must census identically.
TEST_F(ModesTdm, matched_census_single_team_sum_ignores_guyless_livings)
{
    long long with_bot = 0;
    long long without_bot = 0;
    long long team_sum = 0;
    {
        ModesCtfWorld fx(kMatchProbeLevel);
        author_fresh_squad(fx, 0, 200);
        fx.spawn_living(FAMILY_ORC, 0, 424, 200);  // no guy record
        fx.tick(1);
        const auto census = matched_log(fx.world(), "census");
        with_bot = tab_int(census, 1);
        team_sum = tab_int(census, 2);
        EXPECT_EQ(0, tab_int(census, 3));
        EXPECT_EQ(0, tab_int(census, 4));
        EXPECT_EQ(0, tab_int(census, 5));
        // The one-human-team headcount rule (H = 5, guy-less Livings
        // excluded, D34) lives in match.activation: matched_size, pinned
        // by StagedRules.fair_matched_size_is_the_min_roster_headcount.
    }
    {
        ModesCtfWorld fx(kMatchProbeLevel);
        author_fresh_squad(fx, 0, 200);
        fx.tick(1);
        const auto census = matched_log(fx.world(), "census");
        without_bot = tab_int(census, 1);
    }
    EXPECT_EQ(without_bot, with_bot)
        << "a guy-less Living must never enter the census";
    EXPECT_EQ(team_sum, with_bot)
        << "one human team -> the reference is its f-sum";
    // Derived calibration (§4.2): the fresh five-family squad's f-sum under
    // the trunc-on-read discipline. Soldier alone is 2306 (the §4.2 worked
    // example): H 166, M 34, D 23, A 9, SP 4, FF trunc(5.87) = 5 -> RATE 24,
    // ED 23, OFF 572, EHP 219 -> (219 * 632) / 60 = 2306.
    EXPECT_EQ(6520, with_bot) << "fresh squad f-sum drifted — recalibrate "
                                 "the derived pins (spec §4.2)";
}

// The weakest-team reference (B3 — D11's mean is retired) and the
// heart-value oracle (§8): three rosters of strictly increasing worth —
// fresh squad, mixed L4 trio, an archmage-anchored roster — must rank
// identically under f-sums and under guy::query_heart_value sums.
// Within-roster inversions are expected; TEAM-level rank agreement is the
// contract. The absolute sums are the derived calibration table
// (recorded from the census itself, §4.2).
TEST_F(ModesTdm, matched_census_weakest_reference_and_heart_value_rank)
{
    ModesCtfWorld fx(kMatchProbeLevel);
    author_fresh_squad(fx, 0, 200);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 1, 200, 400, 11, 4);
    fx.spawn_leveled_hero(FAMILY_ARCHER, 1, 232, 400, 12, 4);
    fx.spawn_leveled_hero(FAMILY_ELF, 1, 264, 400, 13, 4);
    fx.spawn_leveled_hero(FAMILY_MAGE, 2, 200, 600, 21, 9);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 2, 232, 600, 22, 4);
    fx.spawn_leveled_hero(FAMILY_THIEF, 2, 264, 600, 23, 1);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    const auto census = matched_log(fx.world(), "census");
    const long long reference = tab_int(census, 1);
    const long long fresh = tab_int(census, 2);
    const long long mixed = tab_int(census, 3);
    const long long archmage = tab_int(census, 4);
    EXPECT_EQ(fresh, reference)
        << "the reference is the WEAKEST human team's f-sum (B3)";
    // H = MIN of the per-team headcounts (5, 3, 3 -> 3; D34) lives in
    // match.activation: matched_size, pinned by
    // StagedRules.fair_matched_size_is_the_min_roster_headcount.

    // f-sum rank must agree with the engine's own price of the rosters.
    long long hearts[3] = {0, 0, 0};
    for (const auto& uptr : fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->myguy == nullptr)
            continue;
        const int team = w->team_num();
        if (team >= 0 && team < 3)
            hearts[team] += w->myguy->query_heart_value();
    }
    EXPECT_LT(hearts[0], hearts[1]);
    EXPECT_LT(hearts[1], hearts[2]);
    EXPECT_LT(fresh, mixed) << "f must rank the rosters like heart value";
    EXPECT_LT(mixed, archmage) << "f must rank the rosters like heart value";

    // Derived calibration values (recorded from this census — §4.2).
    EXPECT_EQ(6520, fresh);
    EXPECT_EQ(14590, mixed);
    EXPECT_EQ(27947, archmage);
}

// spawn_bots legacy arm: no plan, no target — bots take the session
// difficulty formula (percent 100 -> L2) and no matched state appears.
// The direct-arm twin of the real-mode empty_active_teams cases.
TEST_F(ModesTdm, matched_spawn_bots_legacy_arm_is_unchanged)
{
    ModesCtfWorld fx(kMatchSpawnProbeLevel);
    for (int i = 0; i < 5; ++i)
        fx.spawn_anchor(1, 96 + 96 * i, 96);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier);
    EXPECT_EQ(2, levels.archer);
    EXPECT_EQ(2, levels.elf);
    EXPECT_EQ(2, levels.mage);
    EXPECT_EQ(2, levels.thief);
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    EXPECT_EQ(0, fx.var(kSlotMatchedPlan));
    EXPECT_EQ(og::sim::kFillFair,
              (fx.var(kSlotMatchedAnnounced) / 1000) % 100)
        << "the applied FAIR fact banks even on the legacy arm (B7)";
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10) << "nothing announced";
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED"));
}

// spawn_bots planned arm: a stored plan wins over everything — the first
// k members spawn one level above L, and nothing announces (a stored plan
// only exists after the init solve already ran).
TEST_F(ModesTdm, matched_spawn_bots_planned_arm_applies_stored_plan)
{
    ModesCtfWorld fx(kMatchSpawnProbeLevel);
    for (int i = 0; i < 5; ++i)
        fx.spawn_anchor(1, 96 + 96 * i, 96);
    // Team 1 code 43: L4, first 3 members upgraded.
    fx.world().mode.vars[kMatchProbeInPlan] = 4300;
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(5, levels.soldier) << "member 1 of the k = 3 prefix";
    EXPECT_EQ(5, levels.archer) << "member 2 of the k = 3 prefix";
    EXPECT_EQ(5, levels.elf) << "member 3 of the k = 3 prefix";
    EXPECT_EQ(4, levels.mage) << "past the prefix: L4";
    EXPECT_EQ(4, levels.thief) << "past the prefix: L4";
    EXPECT_EQ(4300, fx.var(kSlotMatchedPlan)) << "the plan is durable";
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10) << "nothing announced";
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED"));
}

// spawn_bots D24 arm at init: with no plan and human power on the board
// the seam censuses, measures the real squad, solves against the
// reference (the fresh squad's derived 6520), persists the plan and
// announces once — the direct-seam twin of the flagship flow below.
TEST_F(ModesTdm, matched_spawn_bots_measures_solves_and_announces)
{
    ModesCtfWorld fx(kMatchSpawnProbeLevel);
    for (int i = 0; i < 5; ++i)
        fx.spawn_anchor(1, 96 + 96 * i, 96);
    author_fresh_squad(fx, 0, 200);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the reference 6520 solves to L1 + one upgrade (derived, as in "
           "the flagship flow)";
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier) << "the k = 1 upgrade prefix";
    EXPECT_EQ(1, levels.archer);
    EXPECT_EQ(1, levels.elf);
    EXPECT_EQ(1, levels.mage);
    EXPECT_EQ(1, levels.thief);
    EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10);
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED (LIMIT)"));
}

// The low clamp announces the LIMIT variant (§7): a lone fresh thief's
// reference prices below B(1) of the five-family squad, so the solve
// floors at uniform L1 and says so. (The high clamp's pure arm stays
// pinned in matched_power_metric_and_solver_arms: solve_high.)
TEST_F(ModesTdm, matched_spawn_bots_low_clamp_announces_the_limit)
{
    ModesCtfWorld fx(kMatchSpawnProbeLevel);
    for (int i = 0; i < 5; ++i)
        fx.spawn_anchor(1, 96 + 96 * i, 96);
    fx.spawn_leveled_hero(FAMILY_THIEF, 0, 200, 200, 1, 1);
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(10, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "a sub-B(1) reference -> uniform L1 (E13)";
    EXPECT_EQ(2, fx.var(kSlotMatchedAnnounced) % 10);
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED (LIMIT)"));
}

// The D24 backstop shape mid-match: once init is over (MODE_ID != 0) a
// measure-and-solve spawn still stores the plan and levels the squad, but
// stays silent — the announcement belongs to on_mode_init alone (§7).
TEST_F(ModesTdm, matched_backstop_solve_stores_plan_but_stays_silent)
{
    ModesCtfWorld fx(kMatchSpawnProbeLevel);
    for (int i = 0; i < 5; ++i)
        fx.spawn_anchor(1, 96 + 96 * i, 96);
    author_fresh_squad(fx, 0, 200);
    fx.world().mode.vars[kMatchProbeInMidMatch] = 1;
    fx.tick(1);
    ASSERT_TRUE(fx.world().mode.active);

    EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier) << "the squad still levels to the solve";
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10);
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED"));
}

// E1 through the real TDM flow: no human power server-side stores
// nothing and fields the legacy squads — no announcement. The MAP UNITS
// box (B4) strips the authored guy-less soldier, so BOTH emptied teams
// backfill at the legacy formula.
TEST_F(ModesTdm, no_humans_with_the_box_off_keeps_legacy_bots)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.world().ctf_requested_map_units[0] = og::sim::kMapUnitsOff;
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    walker* troop = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    EXPECT_TRUE(troop->dead())
        << "the box off strips the authored troop (B4)";
    EXPECT_EQ(5, alive_on_team(fx.world(), 0))
        << "the emptied team backfills with the legacy squad";
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier) << "legacy formula at percent 100";
    EXPECT_EQ(2, levels.thief);
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    EXPECT_EQ(0, fx.var(kSlotMatchedPlan));
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10);
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED"));
}

// The flagship WP-E flow: a fresh solo squad on team 0, the DEFAULT
// knobs (FILL: FAIR is the default solver now, B2), an empty authored
// team 1. Init censuses the roster (reference 6520, the derived §4.2
// value), solves the real squad against it and fields a mixed L1/L2
// rival — an even match, not the old flat L2 wall.
//
// SPEC DEVIATION, recorded: §4.2/§5.3 hoped the fresh-squad target lands
// within one k-step of B(2) = 10991 (the stock squad). The DERIVED numbers
// say otherwise: T = 6520 = 0.59 * B(2), because stock L2 bots genuinely
// outgun a fresh human squad. The solver answers (L1, k1) — P(1,1) = 6711
// misses by 191 where B(1) = 5006 misses by 1514 — the closer match. The
// continuity anchor was aspirational; the derived calibration wins (§4.2:
// no hand-shipped numbers).
TEST_F(ModesTdm, matched_solo_fresh_squad_fields_an_even_rival)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    author_fresh_squad(fx, 0, 200);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    EXPECT_EQ(6520, fx.var(kSlotMatchedTarget))
        << "the census target persists for later spawns (D20)";
    EXPECT_EQ(5, fx.var(kSlotMatchedSize))
        << "H = 5 -> n = 5: a full roster keeps the full squad (D34)";
    EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "T = 6520 between B(1) = 5006 and B(2) = 10991 -> L1, 1 upgrade";
    EXPECT_EQ(0, matched_plan_code(fx.var(kSlotMatchedPlan), 0))
        << "the human team is never solved (I3)";
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier) << "upgrade prefix member 1";
    EXPECT_EQ(1, levels.archer) << "past the k = 1 prefix";
    EXPECT_EQ(1, levels.elf);
    EXPECT_EQ(1, levels.mage);
    EXPECT_EQ(1, levels.thief);
    EXPECT_EQ(5, alive_on_team(fx.world(), 0)) << "the roster is untouched";
    EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10);
    EXPECT_EQ(og::sim::kFillFair,
              (fx.var(kSlotMatchedAnnounced) / 1000) % 100)
        << "the solved squad banks the applied FAIR code";
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED (LIMIT)"));
}

// ===========================================================================
// TROOPS: FAIR system tests (matched-teams WP-F, spec §8)
// ===========================================================================

namespace {

// C++ twin of mode_match.walker_power under the same §4.1 trunc-on-read
// discipline (static_cast truncates the positive float stats exactly like
// og.trunc). Used to cross-check the Lua census against an independent
// implementation and to measure spawned squads.
long long measured_walker_f(const walker* w)
{
    const statistics* s = w->stats();
    if (s == nullptr)
        return 0;
    const long long hp = static_cast<long long>(s->max_hitpoints());
    const long long mp = static_cast<long long>(s->max_magicpoints());
    const long long armor = static_cast<long long>(s->armor());
    const long long dmg = static_cast<long long>(w->damage());
    const long long sp = static_cast<long long>(w->stepsize());
    long long ff = static_cast<long long>(w->fire_frequency());
    if (ff < 1)
        ff = 1;
    const long long level = s->level();
    const long long ed = dmg * (level + 3) / 4;
    const long long rate = 120 / ff;
    const long long off = ed * rate + 5 * sp;
    const long long ehp = hp + 4 * armor + mp / 2;
    return ehp * (off + 60) / 60;
}

long long team_f_sum(GameWorld& world, int team)
{
    long long sum = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team))
            continue;
        sum += measured_walker_f(w);
    }
    return sum;
}

// The matched TDM flow the determinism/mirror/budget probes share: a
// fresh roster squad on team 0 under the default knobs (FILL: FAIR is
// the default solver, B2), empty authored teams 1 and 2.
void author_matched_tdm(ModesCtfWorld& fx)
{
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_anchor(2, 96, 800);
    author_fresh_squad(fx, 0, 200);
}

std::string run_matched_tdm_digest(int ticks)
{
    ModesCtfWorld fx(kTdmLevelA);
    author_matched_tdm(fx);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(ticks);
    EXPECT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_GT(fx.var(kSlotMatchedTarget), 0);
    return digest_world(fx.world());
}

}  // namespace

// The WP-F headline (spec §5.3 accuracy contract): a solo LEVELED roster's
// opponent squad measures within ±15% of the stored target. The target is
// interior (announce is the plain variant, so T sat inside [B(1), B(9)])
// and the C++ f twin is validated against the Lua census on the solo arm
// (T = the roster team's own f-sum, D11).
TEST_F(ModesTdm, matched_solo_l5_roster_squad_lands_within_fifteen_percent)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 5);
    fx.spawn_leveled_hero(FAMILY_ARCHER, 0, 232, 200, 2, 5);
    fx.spawn_leveled_hero(FAMILY_ELF, 0, 264, 200, 3, 5);
    fx.spawn_leveled_hero(FAMILY_MAGE, 0, 296, 200, 4, 5);
    fx.spawn_leveled_hero(FAMILY_THIEF, 0, 328, 200, 5, 5);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    const long long target = fx.var(kSlotMatchedTarget);
    ASSERT_GT(target, 0);
    EXPECT_EQ(team_f_sum(fx.world(), 0), target)
        << "solo roster: the stored target is the roster's own f-sum, and "
           "the C++ twin must agree with the Lua census byte for byte";
    ASSERT_NE(0, matched_plan_code(fx.var(kSlotMatchedPlan), 1));
    EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10)
        << "an interior target announces the plain variant";

    ASSERT_EQ(5, alive_on_team(fx.world(), 1));
    const long long squad = team_f_sum(fx.world(), 1);
    const long long miss = squad > target ? squad - target : target - squad;
    EXPECT_LE(miss * 100, target * 15)
        << "squad f " << squad << " vs target " << target
        << " misses by more than 15%";
}

// E6, THE flagship MATCH test: with both boxes off (B4) the authored
// orcs are stripped, and the matched census (match.bank_match_target,
// banked by every apply before its squad fills) is strip-invariant:
// authored troops carry no guy record, so the has_guy filter keeps them
// out of the reference in either order. Both emptied teams then fill
// with the MATCHED squad — the flagship solo experience.
TEST_F(ModesTdm, boxes_off_solo_roster_gets_a_matched_opponent)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.world().ctf_requested_map_units[1] = og::sim::kMapUnitsOff;
    fx.world().ctf_requested_map_units[2] = og::sim::kMapUnitsOff;
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_anchor(2, 96, 800);
    author_fresh_squad(fx, 0, 200);
    fx.spawn_living(FAMILY_ORC, 1, 544, 700);  // authored troops: no guy
    fx.spawn_living(FAMILY_ORC, 2, 96, 700);   // record, censusless (E6)
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    EXPECT_EQ(7, fx.var(kTdmSlotTeamMask)) << "all three authored sides";
    EXPECT_EQ(6520, fx.var(kSlotMatchedTarget))
        << "stripped authored troops never pollute the census (E6)";
    EXPECT_EQ(5, fx.var(kSlotMatchedSize))
        << "stripped authored troops never pollute the headcount either";
    EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "the boxed-off opponent solves like the empty-team twin";
    EXPECT_EQ(5, alive_on_team(fx.world(), 1));
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(2, levels.soldier) << "the k = 1 upgrade prefix";
    EXPECT_EQ(1, levels.archer);
    EXPECT_EQ(1, levels.thief);
    EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 2))
        << "the backfilled third side solves to the same plan";
    EXPECT_EQ(5, alive_on_team(fx.world(), 2))
        << "its stripped authored orc is replaced by a matched squad";
    const SquadLevels third = squad_levels_on_team(fx.world(), 2);
    EXPECT_EQ(2, third.soldier) << "the same k = 1 upgrade prefix";
    EXPECT_EQ(1, third.archer);
    EXPECT_EQ(1, third.thief);
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// THE playtest ruling (D34/D37, the headcount amendment's flagship): a
// solo fresh soldier's matched opponent is ONE L1 soldier — never the
// five-bot wall the superseded E13 clamp fielded. With n = 1 the target
// T = 2306 is INTERIOR to the single soldier's [B(1), B(9)] = [1643, ...]
// (|1643 - 2306| = 663 beats |3348 - 2306| = 1042), so the solve is
// L1/k0, plan 10, PLAIN announce — the old LIMIT died with the clamp
// that produced it.
TEST_F(ModesTdm, matched_solo_fresh_soldier_faces_a_single_matched_rival)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 800);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 1);
    fx.tick(1);
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));

    EXPECT_EQ(2306, fx.var(kSlotMatchedTarget))
        << "the fresh soldier f (derived pin, §4.2)";
    EXPECT_EQ(1, fx.var(kSlotMatchedSize))
        << "the census latches the human headcount (D34)";
    EXPECT_EQ(1, alive_on_team(fx.world(), 1))
        << "the squad never outnumbers the roster it measured (D34)";
    const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
    EXPECT_EQ(1, levels.soldier)
        << "the 1-prefix of the squad table is the soldier (D35)";
    EXPECT_EQ(0, levels.archer) << "nobody past the prefix spawns";
    EXPECT_EQ(0, levels.elf);
    EXPECT_EQ(0, levels.mage);
    EXPECT_EQ(0, levels.thief);
    EXPECT_EQ(10, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
        << "interior solve at n = 1: L1, no upgrades";
    // The felt result, derived from the built world: one L1 soldier bot
    // at f 1643 — 71% of the human's 2306, a 29% undershoot inside the
    // n = 1 ±35% band (D36), erring player-friendly (D37).
    EXPECT_EQ(1643, team_f_sum(fx.world(), 1))
        << "the fielded rival's measured f (pred soldier L1, derived)";
    EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10)
        << "no clamp fired, so the announce is the plain variant (D37)";
    EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
    EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED (LIMIT)"));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// D37: LIMIT fires only on a GENUINE clamp. The old solo-soldier target
// (2306) is interior at n = 1 — plain announce — while a target under the
// single soldier's B(1) = 1643 still floors at uniform L1 and says LIMIT.
TEST_F(ModesTdm, matched_size_one_announces_limit_only_on_genuine_clamp)
{
    {
        ModesCtfWorld fx(kMatchSpawnProbeLevel);
        for (int i = 0; i < 5; ++i)
            fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 1);
        fx.world().mode.vars[kMatchProbeInSize] = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, alive_on_team(fx.world(), 1));
        EXPECT_EQ(10, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
            << "the fresh soldier's 2306 is interior at n = 1 (closest "
               "single-soldier rung is L1)";
        EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10)
            << "the playtest shape no longer saturates (D37)";
        EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"));
        EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED (LIMIT)"));
    }
    {
        ModesCtfWorld fx(kMatchSpawnProbeLevel);
        for (int i = 0; i < 5; ++i)
            fx.spawn_anchor(1, 96 + 96 * i, 96);
        // A fresh mage prices 1282 < the single soldier's B(1) = 1643.
        fx.spawn_leveled_hero(FAMILY_MAGE, 0, 200, 200, 1, 1);
        fx.world().mode.vars[kMatchProbeInSize] = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, alive_on_team(fx.world(), 1));
        EXPECT_EQ(10, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
            << "a genuinely sub-B(1) reference still floors at uniform L1";
        EXPECT_EQ(2, fx.var(kSlotMatchedAnnounced) % 10);
        EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED (LIMIT)"));
    }
}

// D39/D40 through the 9096 probe: a latched SIZE truncates EVERY
// spawn_bots arm to the n-prefix — planned, measure-and-solve, and the
// mid-match backstop alike — while every caller keeps its signature.
TEST_F(ModesTdm, matched_size_latch_truncates_every_spawn_arm)
{
    // Planned arm at n = 1: plan code 43 -> the 1-prefix soldier is
    // member 1 of the k = 3 upgrade prefix (L5).
    {
        ModesCtfWorld fx(kMatchSpawnProbeLevel);
        for (int i = 0; i < 5; ++i)
            fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.world().mode.vars[kMatchProbeInPlan] = 4300;
        fx.world().mode.vars[kMatchProbeInSize] = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, alive_on_team(fx.world(), 1));
        const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
        EXPECT_EQ(5, levels.soldier) << "member 1 of the k = 3 prefix";
        EXPECT_EQ(0, levels.archer) << "the squad ends at the n = 1 prefix";
    }
    // Measure-and-solve at n = 2: two fresh soldiers census 4612 over
    // the soldier + archer prefix. Derived rungs: P(1,1) = 3348 + 834 =
    // 4182 misses T = 4612 by 430 where P(2,0) = 3348 + 1857 = 5205
    // misses by 593 -> L1/k1, a 2v2 against an L2 soldier and an L1
    // archer.
    {
        ModesCtfWorld fx(kMatchSpawnProbeLevel);
        for (int i = 0; i < 5; ++i)
            fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 1);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 232, 200, 2, 1);
        fx.world().mode.vars[kMatchProbeInSize] = 2;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(2, alive_on_team(fx.world(), 1));
        EXPECT_EQ(11, matched_plan_code(fx.var(kSlotMatchedPlan), 1))
            << "the duo solves L1 with one upgrade (D36 n = 2 rungs)";
        const SquadLevels levels = squad_levels_on_team(fx.world(), 1);
        EXPECT_EQ(2, levels.soldier) << "upgrade prefix member 1";
        EXPECT_EQ(1, levels.archer);
        EXPECT_EQ(0, levels.elf) << "the squad ends at the n = 2 prefix";
        EXPECT_EQ(1, fx.var(kSlotMatchedAnnounced) % 10);
    }
    // The D24 backstop truncates identically and stays silent (§7).
    {
        ModesCtfWorld fx(kMatchSpawnProbeLevel);
        for (int i = 0; i < 5; ++i)
            fx.spawn_anchor(1, 96 + 96 * i, 96);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 1);
        fx.world().mode.vars[kMatchProbeInMidMatch] = 1;
        fx.world().mode.vars[kMatchProbeInSize] = 1;
        fx.tick(1);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, alive_on_team(fx.world(), 1));
        EXPECT_EQ(10, matched_plan_code(fx.var(kSlotMatchedPlan), 1));
        EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10);
        EXPECT_FALSE(has_notification(fx.events, "TEAMS MATCHED"));
    }
}

// Run-twice determinism: the same matched flow (census, solve, spawns,
// respawns, directors) must replay byte-identically — mode vars, RNG,
// positions, command queues.
TEST_F(ModesTdm, matched_census_and_solve_replay_deterministically)
{
    const std::string first = run_matched_tdm_digest(120);
    const std::string second = run_matched_tdm_digest(120);
    ASSERT_EQ(first, second)
        << "the matched census/solve must be a pure function of the world";
}

// Matched bots are ordinary replicated entities: the mirror harness must
// stay strike-free and the matched mode vars must reach the client mirror
// bit for bit (I1: no client twin of the power model exists).
TEST_F(ModesTdm, matched_bots_replicate_to_a_client_mirror_without_strikes)
{
    ModesCtfWorld fx(kTdmLevelA);
    author_matched_tdm(fx);
    ModeMirror mirror(kTdmLevelA);

    const MirrorReplication replication = replicate_to_mirror(fx, mirror, 120);
    EXPECT_EQ(0, replication.strikes)
        << "the mirror first desynced at tick " << replication.first_strike_tick;
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    EXPECT_GT(fx.var(kSlotMatchedTarget), 0);
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
    {
        EXPECT_EQ(fx.world().mode.vars[static_cast<std::size_t>(slot)],
                  mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
}

// Budget headroom (E12): the census pass plus the <= 45 model evaluations
// ride the init dispatch, which must still clear the 10x-reduced budget —
// budgets are measured, never bumped.
TEST_F(ModesTdm, matched_init_fits_a_tenth_of_the_instruction_budget)
{
    const BudgetOverride budget(500000);
    ModesCtfWorld fx(kTdmLevelA);
    author_matched_tdm(fx);
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(1);  // init: census + solve + the paired squad fill
    ASSERT_EQ(kModeIdTdm, fx.var(kTdmSlotModeId));
    ASSERT_GT(fx.var(kSlotMatchedTarget), 0);
    fx.tick(45);  // 3 director cadences + every per-tick phase
    EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
        << "a 10x-reduced budget must never trip with matching on";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
