// The staged-init rule oracles (#218 plan-phase retirement): the shared
// activation/fill rules live in lib/mode_match.lua alone (match.activation /
// match.fills), consumed by each mode's decide fold at the top of
// on_mode_init — which now runs ONCE, at staging, in a REAL world. These
// suites pin the rules three ways:
//
//  1. The 16-row activation precedence sweep keeps its EXACT coverage
//     through a direct-Lua harness calling match.activation on synthetic
//     inputs tables (the inventory-sanctioned shape — the reborn
//     roster_effective_team_mask sweep, unchanged rows).
//  2. The per-mode domain/fill/limit oracles become STAGED-WORLD
//     assertions: build the fixture shape, run the real mode_stage_init
//     (the exact function MatchStage runs), assert the banked mode vars
//     and the fielded entities exactly.
//  3. The old agreement matrix becomes the apply-executes-decision matrix:
//     the shared Lua rules (via the harness) produce the expected values
//     and the staged world must bank and field exactly those — plus the
//     per-mode staged-vs-adopted BYTE-identity oracles over the real
//     shipped levels (preview == launch as an assertable identity; the
//     soccer arm lives in test_match_stage.cpp since C4/C7).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include "../modes_pack_fixture.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

using namespace og::modes_test;

namespace {

// The modes.core bot provenance tag (mode_caps.lua BOT_MARK_BIT).
constexpr std::int32_t kBotMarkBit = 65536;

// Per-mode banked-slot map (the tests' standing mode-table knowledge):
// {mask slot, count slot or -1, score slot or -1}.
struct ModeSlots
{
    int mask = -1;
    int count = -1;
    int score = -1;
};
constexpr ModeSlots kCtfSlots{11, 10, 8};
constexpr ModeSlots kTdmSlots{8, -1, 9};
constexpr ModeSlots kSoccerSlots{11, 10, 8};
constexpr ModeSlots kBballSlots{11, 10, 8};
constexpr ModeSlots kOnsSlots{9, 10, -1};

bool has_script_error(GameWorld& world, const std::string& needle)
{
    for (const auto& err : world.scripts().host().errors())
    {
        if (err.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

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

int marked_bots_on(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team) ||
            w->myguy != nullptr)
            continue;
        if (w->stats() != nullptr &&
            (w->stats()->bit_flags() & kBotMarkBit) != 0)
            ++count;
    }
    return count;
}

// The staged init, exactly as MatchStage runs it: respawn resolve + anchor
// scan + the real on_mode_init, on the dormant tick-0 fixture world.
void stage_init(ModesCtfWorld& fx)
{
    og::sim::mode_stage_init(fx.world());
    ASSERT_EQ(0u, fx.world().tick_count_) << "staging must not tick";
}

// --- The direct-Lua harness for the shared rules ---------------------------
//
// og.use is load-time-only, so the harness is a registered probe script
// (the TeamCountProbeScript discipline): it binds mode_match at load time
// and registers an on_load hook for probe level 9092 that reads the case
// parameters from mode vars, calls match.activation + match.fills, and
// banks the packed answers back into mode vars. The harness world runs
// STRICTLY sequentially with any staged fixture world (two live fixtures
// resolve script bindings against the last-constructed world — the
// standing harness trap).

// Parameter slots 30..34 (lineup squad knobs + hard-shape cap), 38/39
// (a plan pair for the resolver probe) and 41..59, answers 35/36/37/60/63.
// The resolver probe reads the LV offsets from the WORLD knobs
// (og.match_setting, the same seam the spawn seams use), so the test sets
// ctf_requested_map_units on the probe world.
constexpr const char* kRuleProbeLua =
    "local match = og.use(\"mode_match\")\n"
    "og.register_level_hooks(9092, {\n"
    "  on_load = function(level)\n"
    "    local plans = 0\n"
    "    local pbase = 1\n"
    "    for t = 0, 3 do\n"
    "      local l, u = match.resolve_plan(t, og.mode_get(38), og.mode_get(39))\n"
    "      plans = plans + (l * 10 + u) * pbase\n"
    "      pbase = pbase * 100\n"
    "    end\n"
    "    og.mode_set(35, plans)\n"
    "    local inputs = {\n"
    "      strip_troops = og.mode_get(41),\n"
    "      score_limit = 0,\n"
    "      respawn_ticks = 0,\n"
    "      teams = {},\n"
    "      flags = {},\n"
    "      fill = {},\n"
    "      map_units = {},\n"
    "    }\n"
    "    for t = 0, 3 do\n"
    "      inputs.teams[t + 1] = {\n"
    "        anchors = og.mode_get(42 + t),\n"
    "        roster = og.mode_get(46 + t),\n"
    "        npcs = og.mode_get(50 + t),\n"
    "        generators = og.mode_get(54 + t),\n"
    "      }\n"
    "      inputs.fill[t + 1] = og.mode_get(30 + t)\n"
    "    end\n"
    "    local squad_cap = og.mode_get(34)\n"
    "    if squad_cap == 0 then\n"
    "      squad_cap = nil\n"
    "    end\n"
    "    local mask, starts, matched, matched_size =\n"
    "        match.activation(inputs, og.mode_get(58), og.mode_get(59))\n"
    "    local packed = mask\n"
    "    if starts then\n"
    "      packed = packed + 16\n"
    "    end\n"
    "    if matched then\n"
    "      packed = packed + 32\n"
    "    end\n"
    "    og.mode_set(60, packed + matched_size * 64)\n"
    "    local rows, _, lineup_mask = match.fills(inputs, mask, {\n"
    "      keep_generators = og.mode_get(61) == 1,\n"
    "      no_bots = og.mode_get(62) == 1,\n"
    "      matched = matched,\n"
    "      matched_size = matched_size,\n"
    "      squad_cap = squad_cap,\n"
    "    })\n"
    "    local codes = {\n"
    "      empty = 0,\n"
    "      company = 1,\n"
    "      troops = 2,\n"
    "      bots = 3,\n"
    "      matched = 4,\n"
    "      generators = 5,\n"
    "    }\n"
    "    local fills_packed = 0\n"
    "    local squads_packed = 0\n"
    "    local base = 1\n"
    "    local sbase = 1\n"
    "    for t = 1, 4 do\n"
    "      local row = rows[t]\n"
    "      fills_packed = fills_packed + (codes[row.fill] + row.count * 8) * base\n"
    "      base = base * 100\n"
    "      squads_packed = squads_packed + (row.squad or 0) * sbase\n"
    "      sbase = sbase * 10\n"
    "    end\n"
    "    og.mode_set(63, fills_packed)\n"
    "    og.mode_set(36, squads_packed)\n"
    "    og.mode_set(37, lineup_mask)\n"
    "  end,\n"
    "})\n";

struct RuleProbeScript
{
    RuleProbeScript()
    {
        og::script::register_pack_script(
            {kRulesPackId, "zz_staged_rule_probe.lua", kRuleProbeLua});
    }
    ~RuleProbeScript()
    {
        og::script::register_pack_script(
            {kRulesPackId, "zz_staged_rule_probe.lua", ""});
    }
};

struct RuleAnswer
{
    int mask = -1;
    bool starts = false;
    bool matched = false;
    int matched_size = 0;
    std::int64_t fills_packed = -1;
    int squads_packed = 0;  // row.squad per team, one decimal digit each
    int lineup_mask = -1;   // fills' NONE-narrowed mask
    std::int64_t plans_packed = 0;  // resolve_plan per team, L*10+k base 100
};

// One probe dispatch (its own short-lived world, destroyed before any
// staged fixture world is built). fill carries the four lineup squad
// knobs (0 AUTO / 1 OFF / 2 NONE / 3.. presets — the engine scale),
// squad_cap the caller's hard shape (0 = none), map_units the four LV
// offsets and plan the (L, k) pair the resolver probe folds through them
// — all default off, so every pre-lineup row reads unchanged. The lobby
// TEAMS count is gone from the inputs (lineup A1/A3): nothing here can
// request a team count any more.
RuleAnswer eval_rules(const std::array<std::array<int, 4>, 4>& teams,
                      int strip, unsigned authored, int auto_default,
                      bool keep_generators, bool no_bots,
                      const std::array<int, 4>& fill = {},
                      int squad_cap = 0,
                      const std::array<int, 4>& map_units = {},
                      int plan_level = 1, int plan_up = 0)
{
    RuleProbeScript probe;
    ModesCtfWorld fx(9092);
    GameWorld& w = fx.world();
    w.mode.vars[41] = strip;
    for (std::size_t t = 0; t < 4; ++t)
    {
        w.mode.vars[42 + t] = teams[t][0];
        w.mode.vars[46 + t] = teams[t][1];
        w.mode.vars[50 + t] = teams[t][2];
        w.mode.vars[54 + t] = teams[t][3];
        w.mode.vars[30 + t] = fill[t];
        w.ctf_requested_map_units[t] = static_cast<short>(map_units[t]);
    }
    w.mode.vars[34] = squad_cap;
    w.mode.vars[38] = plan_level;
    w.mode.vars[39] = plan_up;
    w.mode.vars[58] = static_cast<std::int32_t>(authored);
    w.mode.vars[59] = auto_default;
    w.mode.vars[61] = keep_generators ? 1 : 0;
    w.mode.vars[62] = no_bots ? 1 : 0;
    w.mode.vars[60] = -1;
    w.mode.vars[63] = -1;
    w.mode.vars[35] = -1;
    w.mode.vars[36] = -1;
    w.mode.vars[37] = -1;
    w.run_pending_level_on_load();
    RuleAnswer answer;
    EXPECT_TRUE(w.scripts().host().errors().empty())
        << "rule probe raised: "
        << (w.scripts().host().errors().empty()
                ? std::string()
                : w.scripts().host().errors().back().message);
    const std::int32_t packed = w.mode.vars[60];
    EXPECT_NE(-1, packed) << "the rule probe never answered";
    if (packed < 0)
        return answer;
    answer.mask = packed & 0xF;
    answer.starts = (packed & 16) != 0;
    answer.matched = (packed & 32) != 0;
    answer.matched_size = packed >> 6;
    answer.fills_packed = w.mode.vars[63];
    answer.squads_packed = w.mode.vars[36];
    answer.lineup_mask = w.mode.vars[37];
    answer.plans_packed = w.mode.vars[35];
    return answer;
}

// The convenience mask-only read the sweep uses: one anchor per authored
// team, one roster fighter per roster team (the old sweep_inputs shape),
// the four squad knobs (OFF is the only one activation reads besides the
// presets) and the ALL arm's manifest default.
int activation_mask(unsigned authored, unsigned roster, int strip,
                    const std::array<int, 4>& fill = {},
                    int auto_default = 0)
{
    std::array<std::array<int, 4>, 4> teams{};
    for (int t = 0; t < 4; ++t)
    {
        if ((authored & (1u << t)) != 0)
            teams[static_cast<std::size_t>(t)][0] = 1;
        if ((roster & (1u << t)) != 0)
            teams[static_cast<std::size_t>(t)][1] = 1;
    }
    return eval_rules(teams, strip, authored, auto_default, false, false,
                      fill)
        .mask;
}

// The engine's fill scale (lobby_state.h kBotSquad*), spelled once
// for the rows below.
constexpr int kKnobAuto = og::sim::kBotSquadAuto;
constexpr int kKnobOff = og::sim::kBotSquadOff;
constexpr int kKnobNone = og::sim::kBotSquadNone;
constexpr int kKnobBalanc = og::sim::kBotSquadPresetBase;      // 3
constexpr int kKnobCaster = og::sim::kBotSquadPresetBase + 1;  // 4
constexpr int kKnobBrutes = og::sim::kBotSquadPresetBase + 2;  // 5
constexpr int kKnobFair = og::sim::kBotSquadPresetBase + 4;    // 7

}  // namespace

using StagedRules = ModesPackTest;

// ===========================================================================
// 1. Activation precedence — the rows through the direct-Lua harness (the
//    rule's unit-level oracle; auto_default 0 = CTF/TDM's arm, the
//    manifest-default arm is pinned on staged worlds below). The lobby
//    TEAMS count is retired (lineup A1/A3): what it did to a dropped team,
//    the OFF wheel value does per team, and "a team is on when anything
//    is on it" is the whole rule.
// ===========================================================================

TEST_F(StagedRules, activation_precedence_sweep)
{
    // OWN/FAIR, empty roster: the whole authored domain — the retired
    // Auto already meant "as many teams as the map actually has" (issue
    // #218, the 2026-08-18 directive), and no count clamps it any more.
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0, 2));
    EXPECT_EQ(0b1101, activation_mask(0b1101, 0, 2));
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0, 3));

    // With a roster it is still the authored domain (rosters were always
    // inside it)...
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b0001, 2));
    EXPECT_EQ(0b0101, activation_mask(0b0101, 0b0100, 2));
    // ...unless nothing else is authored (the lone bit stands, and the
    // rule reports the not-starting shape).
    EXPECT_EQ(0b0100, activation_mask(0b0100, 0b0100, 2));
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b0101, 2));
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b1110, 2));

    // OFF takes a team out of the authored domain — its troops, flags
    // and generators are not fielded (what TEAMS: n did to a dropped
    // team) — in any position, on either TROOPS arm.
    EXPECT_EQ(0b1101, activation_mask(0b1111, 0, 2, {0, kKnobOff, 0, 0}));
    EXPECT_EQ(0b0101, activation_mask(0b1111, 0, 2,
                                      {0, kKnobOff, 0, kKnobOff}));
    EXPECT_EQ(0b1101, activation_mask(0b1111, 0, 0, {0, kKnobOff, 0, 0}));
    EXPECT_EQ(0b0110, activation_mask(0b1111, 0, 3,
                                      {kKnobOff, 0, 0, kKnobOff}));
    // OFF below two teams reads the not-starting shape, never a clamp.
    EXPECT_EQ(0b0001, activation_mask(0b0011, 0, 2, {0, kKnobOff, 0, 0}));
    // OFF on an occupied team is ignored by the sim: the roster keeps
    // the team on (a seat's team always carries a deployed fighter, M4,
    // so the LINEUP page refuses the value up front and nothing here has
    // to).
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b0010, 2,
                                      {0, kKnobOff, 0, 0}));
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b0010, 0,
                                      {0, kKnobOff, 0, 0}));
    // NONE never touches activation (fills narrows an emptied team).
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0, 2, {0, kKnobNone, 0, 0}));

    // TROOPS: ALL takes the map's own value — the caller's manifest
    // default over the authored domain (auto_default 0 = every authored
    // team, the CTF/TDM arm; 2 = the first two, soccer's row.teams)...
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0, 0));
    EXPECT_EQ(0b0011, activation_mask(0b1111, 0, 0, {}, 2));
    EXPECT_EQ(0b0101, activation_mask(0b1101, 0, 0, {}, 2));
    // ...minus OFF (a dropped team leaves; the default is not a count
    // to be re-filled from the next authored team)...
    EXPECT_EQ(0b0001, activation_mask(0b1111, 0, 0, {0, kKnobOff, 0, 0},
                                      2));
    // ...plus every occupied authored team: a roster on a team the
    // manifest leaves inactive turns it on, and so does a preset squad
    // put there (FAIR included) — "a team is on when anything is on it".
    EXPECT_EQ(0b0111, activation_mask(0b1111, 0b0100, 0, {}, 2));
    EXPECT_EQ(0b1011, activation_mask(0b1111, 0, 0,
                                      {0, 0, 0, kKnobBalanc}, 2));
    EXPECT_EQ(0b1011, activation_mask(0b1111, 0, 0,
                                      {0, 0, 0, kKnobFair}, 2));
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0, 2,
                                      {0, 0, 0, kKnobBalanc}));
    // An ordinal past the registered table degrades to AUTO and turns
    // nothing on.
    EXPECT_EQ(0b0011, activation_mask(0b1111, 0, 0,
                                      {0, 0, 0, og::sim::kMaxBotSquad}, 2));

    // Roster and preset bits outside the authored domain are masked off:
    // nowhere to spawn or score, so nothing activates there.
    EXPECT_EQ(0b0011, activation_mask(0b0011, 0b1100, 2));
    EXPECT_EQ(0b0011, activation_mask(0b0011, 0b0110, 0));
    EXPECT_EQ(0b0011, activation_mask(0b0011, 0, 2,
                                      {0, 0, kKnobBalanc, 0}));
}

// ===========================================================================
// 2. The per-mode staged oracles: real fixture worlds through the real
//    staged init; banked vars and fielded entities pinned exactly.
// ===========================================================================

TEST_F(StagedRules, troops_all_auto_asymmetry_is_per_mode)
{
    // Soccer/basketball/onslaught: TROOPS:ALL at TEAMS: Auto takes the
    // manifest default. 9301 declares teams = 2 — with four anchor teams
    // authored, the staged init fields exactly two.
    {
        ModesCtfWorld fx(kSoccerLevelA);
        for (int team = 0; team < 4; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0011, fx.var(kSoccerSlots.mask))
            << "ALL + Auto = the manifest row.teams default (2), not the "
               "authored count";
        EXPECT_EQ(2, fx.var(kSoccerSlots.count));
    }
    // 9302 declares teams = 4: three authored anchors clamp to all three.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kSoccerSlots.mask));
    }
    // CTF/TDM pass the RAW request on the ALL arm — Auto = every authored
    // team, no manifest default.
    {
        ModesCtfWorld fx(kTdmLevelA);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kTdmSlots.mask))
            << "TDM ALL + Auto = every authored team (raw request)";
    }
    // Onslaught 9401 declares teams = 2: generator domain, Auto -> 2.
    {
        ModesCtfWorld fx(kOnsLevelA);
        fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
        fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
        fx.spawn_generator(FAMILY_TENT, 2, 640, 320);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0011, fx.var(kOnsSlots.mask))
            << "Onslaught ALL + Auto = the manifest row.teams default (2)";
    }
}

TEST_F(StagedRules, ctf_domain_is_the_first_flag_per_team_fold)
{
    // Rows in fx order: team 1 (kept, level 1), team 1 dup (SURPLUS — its
    // level 5 must NOT set the map limit), team 7 (out of range), team 0
    // (kept), team 2 (kept). Anchors on the kept teams so squads field.
    ModesCtfWorld fx(kCtfLevelA);
    fx.spawn_flag(flag_family_, 1, 100, 100, 1);
    fx.spawn_flag(flag_family_, 1, 132, 100, 5);
    fx.spawn_flag(flag_family_, 7, 164, 100, 9);
    fx.spawn_flag(flag_family_, 0, 196, 100, 1);
    fx.spawn_flag(flag_family_, 2, 228, 100, 1);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 200);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0111, fx.var(kCtfSlots.mask))
        << "first flag per in-range team wins the domain";
    EXPECT_EQ(3, fx.var(kCtfSlots.score))
        << "no KEPT flag above level 1 -> the T.capture_limit default; a "
           "surplus flag's level must never leak into the map limit";
    for (int team = 0; team < 3; ++team)
    {
        EXPECT_NE(0, fx.team_var(kSlotFlagEntity, team))
            << "banked FLAG_ENTITY follows the fold, team " << team;
    }

    // The kept-flag capture-limit channel: first kept flag above level 1.
    {
        ModesCtfWorld leveled(kCtfLevelA);
        leveled.spawn_flag(flag_family_, 1, 100, 100, 4);
        leveled.spawn_flag(flag_family_, 0, 196, 100, 1);
        leveled.spawn_anchor(0, 96, 200);
        leveled.spawn_anchor(1, 192, 200);
        leveled.world().ctf_requested_strip_scenario_troops = 0;
        leveled.world().ctf_requested_team_count = 0;
        stage_init(leveled);
        ASSERT_TRUE(leveled.world().mode.active);
        EXPECT_EQ(4, leveled.var(kCtfSlots.score));
    }
    // An explicit request outranks the map.
    {
        ModesCtfWorld requested(kCtfLevelA);
        requested.spawn_flag(flag_family_, 1, 100, 100, 4);
        requested.spawn_flag(flag_family_, 0, 196, 100, 1);
        requested.spawn_anchor(0, 96, 200);
        requested.spawn_anchor(1, 192, 200);
        requested.world().ctf_requested_strip_scenario_troops = 0;
        requested.world().ctf_requested_capture_limit = 9;
        stage_init(requested);
        ASSERT_TRUE(requested.world().mode.active);
        EXPECT_EQ(9, requested.var(kCtfSlots.score));
    }
    // One flag team = the failed-init shape with CTF's exact sentence,
    // raised AFTER the surplus kills (a marker team with no flag never
    // enters the domain).
    {
        ModesCtfWorld lone(kCtfLevelA);
        lone.spawn_flag(flag_family_, 2, 100, 100, 1);
        lone.spawn_anchor(2, 96, 200);
        lone.spawn_anchor(3, 192, 200);
        stage_init(lone);
        EXPECT_FALSE(lone.world().mode.active);
        EXPECT_TRUE(lone.world().mode.init_attempted);
        EXPECT_TRUE(has_script_error(lone.world(),
                                     "ctf: fewer than two flag teams"));
    }
}

TEST_F(StagedRules, tdm_domain_is_anchors_union_livings)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);                       // anchors alone author
    fx.spawn_hero(FAMILY_SOLDIER, 1, 200, 200, 1);    // a roster authors
    fx.spawn_hero(FAMILY_SOLDIER, 1, 232, 200, 2);
    fx.spawn_living(FAMILY_ORC, 2, 300, 300);         // troops author
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0111, fx.var(kTdmSlots.mask));
    EXPECT_EQ(20, fx.var(kTdmSlots.score))
        << "no fixture manifest row for 9101 -> the T.score_limit default";

    // The mode's exact failed-init sentence on a lone-team world.
    ModesCtfWorld lone(kTdmLevelA);
    lone.spawn_living(FAMILY_ORC, 3, 300, 300);
    lone.spawn_living(FAMILY_ORC, 3, 332, 300);
    stage_init(lone);
    EXPECT_FALSE(lone.world().mode.active);
    EXPECT_TRUE(has_script_error(lone.world(), "tdm: fewer than two teams"));
}

// The dormancy carve-out, both halves of it (#218): a delayed-spawn walker
// is excluded from snapshot capture, so the C++ staged report census skips
// it (picker_common.cpp) — and the Lua census must skip it too. Otherwise
// the mode activates a team, banks it a fill, and the preview pane renders
// that same team as absent.
TEST_F(StagedRules, dormant_troops_are_uncensused_like_the_staged_report)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    walker* const delayed = fx.spawn_living(FAMILY_ORC, 2, 400, 300);
    ASSERT_NE(delayed, nullptr);
    delayed->set_spawn_delay(120);
    delayed->set_dormant(true);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0011, fx.var(kTdmSlots.mask))
        << "a delayed-spawn team the preview cannot see must not author the "
           "domain";

    // The identical cohort AWAKE authors team 2 — the only difference is
    // dormancy, so this pins the carve-out rather than an empty world.
    ModesCtfWorld awake(kTdmLevelA);
    awake.spawn_anchor(0, 96, 96);
    awake.spawn_living(FAMILY_ORC, 1, 300, 300);
    awake.spawn_living(FAMILY_ORC, 2, 400, 300);
    awake.world().ctf_requested_strip_scenario_troops = 0;
    awake.world().ctf_requested_team_count = 0;

    stage_init(awake);
    ASSERT_TRUE(awake.world().mode.active);
    EXPECT_EQ(0b0111, awake.var(kTdmSlots.mask))
        << "an awake troop on the same team authors normally";
}

TEST_F(StagedRules, onslaught_domain_and_generator_fills)
{
    ModesCtfWorld fx(kOnsLevelB);
    fx.world().ctf_requested_strip_scenario_troops = 2;  // OWN
    fx.world().ctf_requested_team_count = 0;
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 0, 192, 320);
    fx.spawn_living(FAMILY_ORC, 1, 300, 640);
    fx.spawn_hero(FAMILY_SOLDIER, 2, 400, 640, 1);

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0111, fx.var(kOnsSlots.mask))
        << "livings and generators author alike; OWN Auto = the authored "
           "count";
    EXPECT_EQ(3, fx.var(kOnsSlots.count));
    // The fills, fielded: foundries survive OWN, the stripped npc team is
    // honestly empty (E8), no bots ever (D17), the roster stands.
    EXPECT_EQ(0, live_livings_on(fx.world(), 0));
    EXPECT_EQ(0, live_livings_on(fx.world(), 1))
        << "OWN strips the guy-less npc";
    EXPECT_EQ(1, live_livings_on(fx.world(), 2));
    for (int team = 0; team < 4; ++team)
        EXPECT_EQ(0, marked_bots_on(fx.world(), team))
            << "onslaught never fields a squad (D17), team " << team;
}

TEST_F(StagedRules, basketball_domain_and_reason)
{
    ModesCtfWorld fx(kBballLevelB);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 192, 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0011, fx.var(kBballSlots.mask));
    EXPECT_EQ(6, fx.var(kBballSlots.score)) << "the 9702 fixture row's limit";

    ModesCtfWorld lone(kBballLevelB);
    lone.spawn_anchor(1, 96, 96);
    stage_init(lone);
    EXPECT_FALSE(lone.world().mode.active);
    EXPECT_TRUE(has_script_error(
        lone.world(), "basketball: fewer than two anchor teams"));
}

TEST_F(StagedRules, fair_matched_size_is_the_min_roster_headcount)
{
    // Rosters of 3 and 5 under FAIR: matched, size = min = 3, and the
    // backfilled teams field MATCHED squads truncated to that headcount.
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    int guy_id = 1;
    for (int k = 0; k < 3; ++k)
        fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                      700, guy_id++);
    for (int k = 0; k < 5; ++k)
        fx.spawn_hero(FAMILY_SOLDIER, 2, static_cast<short>(96 + 32 * k),
                      760, guy_id++);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3, fx.var(kSlotMatchedSize))
        << "several roster teams -> the MIN headcount (D34)";
    EXPECT_GT(fx.var(kSlotMatchedTarget), 0)
        << "a live has_guy walker always prices above zero";
    EXPECT_EQ(3, live_livings_on(fx.world(), 0));
    EXPECT_EQ(5, live_livings_on(fx.world(), 2));
    EXPECT_EQ(3, marked_bots_on(fx.world(), 1))
        << "a matched squad truncates to min(headcount, 5)";
    EXPECT_EQ(3, marked_bots_on(fx.world(), 3));

    // A deployed roster OUTSIDE the authored domain still sizes the match
    // (the census prices every has_guy walker) but never activates.
    ModesCtfWorld outside(kSoccerLevelB);
    outside.spawn_anchor(0, 96, 96);
    outside.spawn_anchor(1, 192, 96);
    guy_id = 1;
    for (int k = 0; k < 4; ++k)
        outside.spawn_hero(FAMILY_SOLDIER, 0,
                           static_cast<short>(96 + 32 * k), 700, guy_id++);
    outside.spawn_hero(FAMILY_SOLDIER, 3, 96, 760, guy_id++);
    outside.spawn_hero(FAMILY_SOLDIER, 3, 128, 760, guy_id++);
    outside.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);
    outside.world().ctf_requested_team_count = 0;
    stage_init(outside);
    ASSERT_TRUE(outside.world().mode.active);
    EXPECT_EQ(0b0011, outside.var(kSoccerSlots.mask))
        << "an unauthored roster team never activates";
    EXPECT_EQ(2, outside.var(kSlotMatchedSize))
        << "but its headcount still bounds the matched size";
    EXPECT_EQ(2, marked_bots_on(outside.world(), 1));
}

TEST_F(StagedRules, fair_with_no_roster_degrades_to_legacy_bots)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0, fx.var(kSlotMatchedSize))
        << "FAIR with zero rosters anywhere predicts TARGET 0";
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    for (int team = 0; team < 3; ++team)
    {
        EXPECT_EQ(5, marked_bots_on(fx.world(), team))
            << "the legacy difficulty squad, NOT matched (the degrade), "
               "team " << team;
    }
}

TEST_F(StagedRules, soccer_score_limit_resolution_and_troops_fill)
{
    // Request > row > default, clamped into [1, 255]. 9301's row limit: 3.
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    for (int k = 0; k < 4; ++k)
        fx.spawn_living(FAMILY_ORC, 1, static_cast<short>(300 + 32 * k), 300);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3, fx.var(kSoccerSlots.score)) << "no request -> the row limit";
    EXPECT_EQ(4, live_livings_on(fx.world(), 1))
        << "under ALL the authored troops stand";
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0))
        << "an active team with nothing at all gets the squad";

    ModesCtfWorld requested(kSoccerLevelA);
    requested.spawn_anchor(0, 96, 96);
    requested.spawn_anchor(1, 528, 96);
    requested.world().ctf_requested_capture_limit = 7;
    stage_init(requested);
    ASSERT_TRUE(requested.world().mode.active);
    EXPECT_EQ(7, requested.var(kSoccerSlots.score))
        << "the request outranks the row";

    ModesCtfWorld clamped(kSoccerLevelA);
    clamped.spawn_anchor(0, 96, 96);
    clamped.spawn_anchor(1, 528, 96);
    clamped.world().ctf_requested_capture_limit = 999;
    stage_init(clamped);
    ASSERT_TRUE(clamped.world().mode.active);
    EXPECT_EQ(255, clamped.var(kSoccerSlots.score)) << "clamped to [1, 255]";
}

TEST_F(StagedRules, tdm_matched_fill_truncates_to_the_headcount)
{
    // TDM's fixed squad table makes the matched truncation exactly
    // knowable: min headcount 1 -> the backfilled team fields precisely
    // one marked soldier (the D35 soldier-first prefix).
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(og::sim::kTroopsMatched);
    fx.world().ctf_requested_team_count = 0;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1, fx.var(kSlotMatchedSize));
    EXPECT_EQ(1, marked_bots_on(fx.world(), 1))
        << "the matched squad truncates to the min roster headcount";
    EXPECT_EQ(1, live_livings_on(fx.world(), 1));
}

// ===========================================================================
// 2b. The lineup knob rows (docs/lineup-design.md §3.2/§3.4): NONE removes
//     a fill AUTO makes (and narrows the mask), a preset fills an occupied
//     team, an explicit level lands once and persists as the plan, FAIR on
//     an occupied team solves the allies gap, the hard-shape cap clamps,
//     and all-zero knobs stay byte-identical.
// ===========================================================================

namespace {

// The shared lineup-facts slot (mode_match.lua MATCHED.ANNOUNCED slot 4,
// co-tenanted; the ones digit is the announce latch).
int lineup_fact_code(std::int32_t slot_value, int team)
{
    std::int64_t facts = static_cast<std::int64_t>(slot_value) / 10;
    for (int t = 0; t < team; ++t)
        facts /= 100;
    return static_cast<int>(facts % 100);
}

// The expected code for an applied (knob ordinal, LV offset) pair — the
// mixed-radix packing mode_match.lua lineup_fact documents (squad index
// * 11 + offset code, offset code 0 = AUTO, 1..5 = +1..+5, 6..10 =
// -1..-5), spelled here independently so the test is an oracle of the
// packing, not a mirror of it.
int expected_fact(int knob, int offset)
{
    const int squad =
        knob >= og::sim::kBotSquadPresetBase
            ? knob - og::sim::kBotSquadPresetBase + 1
            : 0;
    const int offset_code = offset < 0 ? 5 - offset : offset;
    return squad * 11 + offset_code;
}

// The refusal reason digit (mode_match.lua REFUSAL_BASE, picker_common.cpp
// kLineupRefusalBase): 10^9, alone in the slot when a band fold refuses.
constexpr std::int32_t kRefusalFighters = 1000000000;

int marked_bots_of_family_on(GameWorld& world, int team, int family)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team) ||
            w->myguy != nullptr)
            continue;
        if (w->family() != family)
            continue;
        if (w->stats() != nullptr &&
            (w->stats()->bit_flags() & kBotMarkBit) != 0)
            ++count;
    }
    return count;
}

std::vector<int> bot_levels_on(GameWorld& world, int team)
{
    std::vector<int> levels;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team) ||
            w->myguy != nullptr)
            continue;
        if (w->stats() != nullptr &&
            (w->stats()->bit_flags() & kBotMarkBit) != 0)
            levels.push_back(w->stats()->level());
    }
    return levels;
}

}  // namespace

// NONE suppresses exactly the squad AUTO fields: the identical OWN/Auto
// world backfills three squads; NONE on team 1 fields two and drops the
// team from the banked mask (lineup §3.2 — "a team is on when anything is
// on it").
TEST_F(StagedRules, lineup_none_removes_a_fill_auto_makes)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.world().ctf_requested_strip_scenario_troops = 2;  // OWN
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[1] = kKnobNone;  // NONE on team 1

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0101, fx.var(kSoccerSlots.mask))
        << "a NONE-emptied backfill team leaves the banked mask";
    EXPECT_EQ(2, fx.var(kSoccerSlots.count));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1))
        << "NONE suppresses the squad AUTO would field";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 2));
    EXPECT_EQ(0, live_livings_on(fx.world(), 1));
    EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1))
        << "nothing spawned, nothing banked";
}

// NONE narrowing below two teams refuses the match with the mode's own
// sentence — an empty team stays inactive, and one team is no match.
TEST_F(StagedRules, lineup_none_below_two_teams_refuses)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.world().ctf_requested_strip_scenario_troops = 2;  // OWN
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[1] = kKnobNone;

    stage_init(fx);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_TRUE(has_script_error(fx.world(),
                                 "soccer: fewer than two anchor teams"));
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced))
        << "a team-mode refusal banks no reason digit: the teams sentence";
}

// A preset fills the team whether or not it is occupied (I3 as amended —
// lineup §3.2, matched-teams-design.md I3): CASTER beside a two-hero
// roster fields the preset's exact families, and the applied ordinal is
// banked in the shared facts slot.
TEST_F(StagedRules, lineup_preset_fills_an_occupied_team)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 232, 200, 2);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[0] = kKnobCaster;  // CASTER

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(7, live_livings_on(fx.world(), 0))
        << "the roster stands and the preset squad joins it";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(2, marked_bots_of_family_on(fx.world(), 0, FAMILY_MAGE))
        << "CASTER's families, not the mode's soldier-first table";
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_CLERIC));
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_ELF));
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_ARCHER));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "the AUTO team keeps its plain squad";
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 1, FAMILY_SOLDIER));
    EXPECT_EQ(expected_fact(kKnobCaster, 0),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
        << "applied ordinal CASTER, no offset";
    EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1));
}

// An LV offset (amendment A6) resolves on top of the AUTO source — here
// the legacy formula, L2 at the fixture's 100 percent — exactly once per
// walker and persists as the team's stored plan (lineup §3.2, the D14
// discipline): +5 lands L7 on every member, the staged bots carry the
// level AND the hp of a single s_set_level + set_difficulty application,
// and the plan banks the RESOLVED level.
TEST_F(StagedRules, lineup_level_offset_lands_once_and_persists)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_map_units[1] = 5;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    const std::vector<int> levels = bot_levels_on(fx.world(), 1);
    ASSERT_EQ(5u, levels.size());
    for (const int level : levels)
        EXPECT_EQ(7, level) << "every member takes formula L2 + 5";
    const std::vector<int> auto_levels = bot_levels_on(fx.world(), 0);
    ASSERT_EQ(5u, auto_levels.size());
    for (const int level : auto_levels)
        EXPECT_EQ(2, level) << "the AUTO team keeps the formula";
    // The stored plan (MATCHED.PLAN slot 3, base-100 per team, code =
    // L * 10 + k): team 1's code is 70 — the RESOLVED level, so a
    // respawn never re-adds the offset — and team 0 has no plan.
    EXPECT_EQ(70, (fx.var(kSlotMatchedPlan) / 100) % 100)
        << "the resolved level is stored so respawns reproduce it";
    EXPECT_EQ(0, fx.var(kSlotMatchedPlan) % 100);
    EXPECT_EQ(expected_fact(0, 5),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1))
        << "the OFFSET is the banked fact (the pane reads LV+5), squad AUTO";
    // Lands ONCE: the team-1 soldier bot's max hp equals a reference
    // walker leveled through the identical single application.
    walker* reference = fx.spawn_living(FAMILY_SOLDIER, 3, 96, 700);
    ASSERT_NE(reference, nullptr);
    reference->stats()->set_level(7);
    reference->set_difficulty(7);
    const walker* bot = nullptr;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() == 1 &&
            w->family() == FAMILY_SOLDIER && w->myguy == nullptr)
        {
            bot = w;
            break;
        }
    }
    ASSERT_NE(bot, nullptr);
    EXPECT_EQ(reference->stats()->max_hitpoints(),
              bot->stats()->max_hitpoints())
        << "a double application would inflate the pool";
}

// FAIR on an occupied team fields allies solved against the gap (the
// 2026-08-25 ruling): the plan is stored for the occupied team, the AUTO
// team stays legacy, and MATCHED.TARGET is never banked on this path.
TEST_F(StagedRules, lineup_fair_preset_allies_on_an_occupied_team)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    int guy_id = 1;
    for (int k = 0; k < 2; ++k)
        fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                      700, guy_id++);
    for (int k = 0; k < 5; ++k)
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 2,
                              static_cast<short>(96 + 32 * k), 760,
                              guy_id++, 3);
    fx.world().ctf_requested_strip_scenario_troops = 0;  // ALL, not FAIR
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[0] = kKnobFair;  // FAIR preset

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(2 + 5, live_livings_on(fx.world(), 0))
        << "FAIR fields allies beside the occupied roster";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_NE(0, fx.var(kSlotMatchedPlan) % 100)
        << "the allies solve stores team 0's plan";
    EXPECT_EQ(0, (fx.var(kSlotMatchedPlan) / 100) % 100)
        << "the AUTO team stays legacy (no plan)";
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget))
        << "the FAIR-preset target is local, never banked (other teams' "
           "AUTO squads must stay legacy)";
    EXPECT_EQ(expected_fact(kKnobFair, 0),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
        << "the applied FAIR ordinal is banked for the pane";
}

// All-zero knobs are byte-identical — and so is an ordinal past the
// preset table (applied AUTO by the degrade rule), which exercises the
// knob-read paths while pinning the identity.
TEST_F(StagedRules, lineup_auto_and_unregistered_ordinal_are_byte_identical)
{
    std::vector<std::uint8_t> auto_bytes;
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        auto_bytes = og::sim::serialize_snapshot(
            og::sim::peek_keyframe_snapshot(fx.world()));
    }
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_fill[0] =
            og::sim::kMaxBotSquad;  // past the registered table
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        // The knob itself rides the snapshot (it is a replicated input),
        // so it is reset before the capture: everything else — entities,
        // mode vars, the lot — must be identical to the AUTO stage.
        fx.world().ctf_requested_fill[0] = 0;
        EXPECT_EQ(auto_bytes,
                  og::sim::serialize_snapshot(
                      og::sim::peek_keyframe_snapshot(fx.world())))
            << "an unregistered ordinal must degrade to AUTO byte for byte";
    }
}

// Basketball's hard shape (5v5) rides its decide fold's squad_cap into
// the staged world: a preset squad on the court fields at most five —
// with the shipped five-family presets, exactly five — and the banked
// count agrees with the census.
TEST_F(StagedRules, lineup_basketball_preset_keeps_the_court_shape)
{
    ModesCtfWorld fx(kBballLevelB);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 192, 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[0] = kKnobBrutes;  // BRUTES

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(3, marked_bots_of_family_on(fx.world(), 0, FAMILY_SOLDIER));
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_BARBARIAN));
    EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_ORC));
    EXPECT_EQ(expected_fact(kKnobBrutes, 0),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0));
}

// The rule-level lineup rows through the direct-Lua harness: NONE narrows
// the mask and empties the row; a preset sizes an empty team's row from
// its own table (capped by the hard shape); a preset on a roster team
// keeps the occupancy fill and carries row.squad; the matched headcount
// truncates a preset like any squad.
TEST_F(StagedRules, lineup_rule_rows_none_preset_cap_and_matched)
{
    std::array<std::array<int, 4>, 4> teams{};
    for (int t = 0; t < 3; ++t)
        teams[static_cast<std::size_t>(t)][0] = 1;  // anchors author 0-2

    // NONE on backfilled team 1: row empties, mask narrows.
    {
        const RuleAnswer a = eval_rules(teams, 2, 0b0111, 0, false, false,
                                        {0, kKnobNone, 0, 0});
        EXPECT_EQ(0b0111, a.mask) << "activation is NONE-blind";
        EXPECT_EQ(0b0101, a.lineup_mask) << "fills narrows the NONE team";
        EXPECT_EQ(0, (a.fills_packed / 100) % 100) << "empty row, count 0";
        EXPECT_EQ(0, a.squads_packed);
    }
    // OFF on team 1: activation drops it, so fills never sees it — the
    // same empty row and narrowed mask, reached one step earlier.
    {
        const RuleAnswer a = eval_rules(teams, 2, 0b0111, 0, false, false,
                                        {0, kKnobOff, 0, 0});
        EXPECT_EQ(0b0101, a.mask) << "activation drops the OFF team";
        EXPECT_EQ(0b0101, a.lineup_mask);
        EXPECT_EQ(0, (a.fills_packed / 100) % 100) << "empty row, count 0";
    }
    // OFF beside a roster: the roster keeps the team on and fields the
    // company alone — OFF still forbids a squad, like NONE.
    {
        std::array<std::array<int, 4>, 4> roster_teams = teams;
        roster_teams[1][1] = 2;
        const RuleAnswer a = eval_rules(roster_teams, 2, 0b0111, 0, false,
                                        false, {0, kKnobOff, 0, 0});
        EXPECT_EQ(0b0111, a.mask) << "a roster keeps an OFF team on";
        EXPECT_EQ(1 + 2 * 8, static_cast<int>((a.fills_packed / 100) % 100))
            << "company fill, the roster alone";
        EXPECT_EQ(0, a.squads_packed);
    }
    // CASTER on empty team 0: fill bots, count = the preset's 5.
    {
        const RuleAnswer a = eval_rules(teams, 2, 0b0111, 0, false, false,
                                        {kKnobCaster, 0, 0, 0});
        EXPECT_EQ(0b0111, a.lineup_mask);
        EXPECT_EQ(3 + 5 * 8, static_cast<int>(a.fills_packed % 100));
        EXPECT_EQ(kKnobCaster, a.squads_packed % 10)
            << "row.squad carries the ordinal";
    }
    // The hard-shape cap clamps the preset row's count (basketball's
    // mechanism, cap 3 so the clamp is visible against 5 families).
    {
        const RuleAnswer a = eval_rules(teams, 2, 0b0111, 0, false, false,
                                        {kKnobCaster, 0, 0, 0}, 3);
        EXPECT_EQ(3 + 3 * 8, static_cast<int>(a.fills_packed % 100))
            << "count clamps to the cap";
    }
    // BRUTES on a two-hero roster team: the occupancy fill stands, the
    // count is what the team fields (roster + the squad beside it — no
    // hard shape, so the full five) and row.squad still calls for the
    // spawn.
    {
        std::array<std::array<int, 4>, 4> roster_teams = teams;
        roster_teams[0][1] = 2;
        const RuleAnswer a = eval_rules(roster_teams, 2, 0b0111, 0, false,
                                        false, {kKnobBrutes, 0, 0, 0});
        EXPECT_EQ(1 + 7 * 8, static_cast<int>(a.fills_packed % 100))
            << "company fill, roster + squad count";
        EXPECT_EQ(kKnobBrutes, a.squads_packed % 10);
    }
    // Under FAIR troops the matched headcount truncates the preset row.
    {
        std::array<std::array<int, 4>, 4> fair_teams = teams;
        fair_teams[0][1] = 2;
        const RuleAnswer a = eval_rules(fair_teams, 3, 0b0111, 0, false,
                                        false, {0, kKnobBalanc, 0, 0});
        ASSERT_TRUE(a.matched);
        EXPECT_EQ(2, a.matched_size);
        EXPECT_EQ(4 + 2 * 8, static_cast<int>((a.fills_packed / 100) % 100))
            << "matched fill, preset truncated to the headcount";
    }
    // NONE on a team whose only content is generators (KEEP arm): the
    // squad is suppressed but the foundries keep the team on — the row
    // reads generators and the mask stands. OFF on the same team takes
    // the foundries with it: the team leaves the mask outright.
    {
        std::array<std::array<int, 4>, 4> gen_teams = teams;
        gen_teams[1][3] = 2;
        const RuleAnswer a = eval_rules(gen_teams, 0, 0b0111, 3, false,
                                        false, {0, kKnobNone, 0, 0});
        EXPECT_EQ(a.mask, a.lineup_mask)
            << "generators keep the NONE team on";
        EXPECT_EQ(5 + 2 * 8, static_cast<int>((a.fills_packed / 100) % 100))
            << "the row degrades to its generators, not to empty";
        const RuleAnswer off = eval_rules(gen_teams, 0, 0b0111, 3, false,
                                          false, {0, kKnobOff, 0, 0});
        EXPECT_EQ(0b0101, off.mask) << "OFF drops a generators-only team";
        EXPECT_EQ(0, (off.fills_packed / 100) % 100);
    }
    // Onslaught's no_bots outranks every knob (D17): a preset row never
    // forms and NONE has nothing to remove.
    {
        const RuleAnswer a = eval_rules(teams, 2, 0b0111, 0, true, true,
                                        {kKnobBalanc, kKnobNone, 0, 0});
        EXPECT_EQ(a.mask, a.lineup_mask) << "no narrowing under no_bots";
        EXPECT_EQ(0, a.squads_packed);
    }
}

// ===========================================================================
// 2c. Review findings on the lineup rows (wp/review-lua): the band modes
//     refuse through the decide fold before any mutation, a preset on an
//     occupied team fills only the room the hard shape leaves, a lone FAIR
//     preset never announces TEAMS MATCHED, and nothing is banked for a
//     squad that spawned nothing.
// ===========================================================================

namespace {

// The shipped FFA row (mode_levels.lua [850], fighters = 8): the fixture
// manifest carries mutant rows but no FFA row, so the band's FFA arm
// stages the real one. Both band modes keep their fighter count in slot 8.
constexpr int kFfaLevelA = 850;
constexpr int kBandSlotFighterCount = 8;

int count_notifications(const og::sim::SimEventLog& log,
                        const std::string& needle)
{
    int count = 0;
    for (const auto& ev : log.events())
    {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            ++count;
    }
    return count;
}

int live_markers_on(GameWorld& world, int team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Special &&
            w->family() == FAMILY_RESERVED_TEAM &&
            w->team_num() == static_cast<unsigned char>(team))
            ++count;
    }
    return count;
}

// The band modes' one-fighter shape under BOTS: NONE (or OFF, which the
// band reads the same way — mode_fighters.lua band_knob), staged the way
// MatchStage stages it: the refusal must be decided BEFORE the world is
// touched — the hero keeps its seat team, the markers survive, the
// authored cast is not stripped — because the kept post-refusal world IS
// the world GO adopts under classic rules (the team modes' discipline; a
// refused init never trips the LobbyServer start gate, which denies
// StageFailed alone). The one write a refusal makes is the reason digit
// in the shared facts slot (REFUSAL_BASE), which the staged report
// renders as FEWER THAN 2 FIGHTERS.
void expect_band_refuses_untouched(int level_id, int knob, const char* reason)
{
    ModesCtfWorld fx(level_id);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    walker* const hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* const troop = fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(troop, nullptr);
    fx.world().ctf_requested_fill[0] = static_cast<short>(knob);

    stage_init(fx);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_TRUE(has_script_error(fx.world(), reason)) << reason;
    EXPECT_EQ(0, hero->team_num())
        << "a refused band init must not reseat the hero on a band byte";
    EXPECT_FALSE(troop->dead()) << "the authored cast is not stripped";
    for (int team = 0; team < 4; ++team)
        EXPECT_EQ(1, live_markers_on(fx.world(), team))
            << "markers are not consumed by a refused init, team " << team;
    EXPECT_EQ(0, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(0, fx.var(kBandSlotFighterCount));
    EXPECT_EQ(kRefusalFighters, fx.var(kSlotMatchedAnnounced))
        << "the reason digit alone is banked, above four zero codes and a "
           "clear latch";
}

// The same knob with two heroes plays: NONE fields nothing and the two
// heroes are the whole band.
void expect_band_none_with_two_heroes_plays(int level_id, int knob)
{
    ModesCtfWorld fx(level_id);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    walker* const a = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* const b = fx.spawn_hero(FAMILY_SOLDIER, 1, 232, 200, 2);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    fx.world().ctf_requested_fill[0] = static_cast<short>(knob);

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(2, fx.var(kBandSlotFighterCount));
    EXPECT_GE(a->team_num(), kFfaTeamBase);
    EXPECT_GE(b->team_num(), kFfaTeamBase);
    EXPECT_EQ(0, marked_bots_on(fx.world(), 0));
    EXPECT_FALSE(has_script_error(fx.world(), "fewer than two"));
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced))
        << "a band that plays banks no reason digit";
}

}  // namespace

// L1: BOTS: NONE with a single deployed hero is a legal knob shape, so it
// must come back as the staged refusal (mode inactive, the mode's own
// reason, world untouched, the reason digit banked), never as an error
// thrown from a half-applied init. OFF is the same shape in a band: it
// cannot take the band out of a mask it does not have.
TEST_F(StagedRules, lineup_band_none_with_one_fighter_refuses_untouched)
{
    for (const int knob : {kKnobNone, kKnobOff})
    {
        SCOPED_TRACE(::testing::Message() << "knob " << knob);
        {
            SCOPED_TRACE("ffa");
            expect_band_refuses_untouched(kFfaLevelA, knob,
                                          "ffa: fewer than two fighters");
        }
        {
            SCOPED_TRACE("mutant");
            expect_band_refuses_untouched(
                kMutantLevelA, knob, "mutant: fewer than two fighters");
        }
    }
}

TEST_F(StagedRules, lineup_band_none_with_two_fighters_plays)
{
    for (const int knob : {kKnobNone, kKnobOff})
    {
        SCOPED_TRACE(::testing::Message() << "knob " << knob);
        {
            SCOPED_TRACE("ffa");
            expect_band_none_with_two_heroes_plays(kFfaLevelA, knob);
        }
        {
            SCOPED_TRACE("mutant");
            expect_band_none_with_two_heroes_plays(kMutantLevelA, knob);
        }
    }
}

// L2: a preset on an OCCUPIED team fills only the room the hard shape
// leaves — basketball's five on five means three deployed humans get two
// BALANC allies, exactly five on court, and the decision row's count is
// the spawned count. Without a hard shape (TDM) the whole squad joins.
TEST_F(StagedRules, lineup_preset_on_an_occupied_team_fills_the_room_left)
{
    {
        ModesCtfWorld fx(kBballLevelB);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 192, 96);
        int guy_id = 1;
        for (int k = 0; k < 3; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, guy_id++);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_fill[0] = kKnobBalanc;  // BALANC

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(5, live_livings_on(fx.world(), 0))
            << "three humans + the room left = five on court";
        EXPECT_EQ(2, marked_bots_on(fx.world(), 0));
        EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_SOLDIER))
            << "the preset's first two families";
        EXPECT_EQ(1, marked_bots_of_family_on(fx.world(), 0, FAMILY_ARCHER));
        EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
            << "the empty team keeps its full squad";
        EXPECT_EQ(expected_fact(kKnobBalanc, 0),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
            << "two allies spawned: the preset is an applied fact";
    }
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 232, 200, 2);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 264, 200, 3);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_fill[0] = kKnobBalanc;  // BALANC

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(8, live_livings_on(fx.world(), 0))
            << "no hard shape: roster + the full squad";
        EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    }
    // The rule rows: the decision's count IS the spawned count (roster +
    // the room left), and row.squad carries the ordinal only while a
    // squad will actually spawn.
    std::array<std::array<int, 4>, 4> teams{};
    teams[0][0] = 1;
    teams[1][0] = 1;
    teams[0][1] = 3;
    {
        const RuleAnswer a = eval_rules(teams, 0, 0b0011, 0, false, false,
                                        {kKnobBalanc, 0, 0, 0}, 5);
        EXPECT_EQ(1 + 5 * 8, static_cast<int>(a.fills_packed % 100))
            << "company fill, count = 3 humans + 2 allies";
        EXPECT_EQ(kKnobBalanc, a.squads_packed % 10);
    }
    {
        const RuleAnswer a = eval_rules(teams, 0, 0b0011, 0, false, false,
                                        {kKnobBalanc, 0, 0, 0});
        EXPECT_EQ(1 + 8 * 8, static_cast<int>(a.fills_packed % 100))
            << "no cap: count = 3 humans + the full squad of 5";
    }
    {
        std::array<std::array<int, 4>, 4> full = teams;
        full[0][1] = 5;
        const RuleAnswer a = eval_rules(full, 0, 0b0011, 0, false, false,
                                        {kKnobBalanc, 0, 0, 0}, 5);
        EXPECT_EQ(1 + 5 * 8, static_cast<int>(a.fills_packed % 100))
            << "a full court leaves no room: count = the roster alone";
        EXPECT_EQ(0, a.squads_packed % 10)
            << "no room, no squad row — nothing will spawn";
    }
}

// L4: a preset that spawned nothing (a full court under basketball's
// hard shape) banks no fact — the pane must never name a squad that is
// not on the floor.
TEST_F(StagedRules, lineup_preset_that_spawns_nothing_banks_no_fact)
{
    ModesCtfWorld fx(kBballLevelB);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 192, 96);
    int guy_id = 1;
    for (int k = 0; k < 5; ++k)
        fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                      700, guy_id++);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[0] = kKnobBalanc;  // BALANC
    fx.world().ctf_requested_map_units[0] = 4;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(5, live_livings_on(fx.world(), 0)) << "the court is full";
    EXPECT_EQ(0, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
        << "0 spawned = no fact, neither the ordinal nor the level";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 1));
}

// L3: the TEAMS MATCHED announce belongs to the match-wide solver
// (TROOPS: FAIR). A FAIR preset on one team solves a LOCAL allies target
// and must neither announce nor latch the shared digit; with the global
// solver running the announce fires exactly once as before.
TEST_F(StagedRules, lineup_fair_preset_alone_never_announces_teams_matched)
{
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        int guy_id = 1;
        for (int k = 0; k < 2; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, guy_id++);
        for (int k = 0; k < 5; ++k)
            fx.spawn_leveled_hero(FAMILY_SOLDIER, 2,
                                  static_cast<short>(96 + 32 * k), 760,
                                  guy_id++, 3);
        fx.world().ctf_requested_strip_scenario_troops = 0;  // ALL, not FAIR
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_fill[0] = kKnobFair;  // FAIR preset

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(5, marked_bots_on(fx.world(), 0)) << "the allies spawn";
        EXPECT_NE(0, fx.var(kSlotMatchedPlan) % 100) << "and are solved";
        EXPECT_EQ(0, count_notifications(fx.events, "TEAMS MATCHED"))
            << "a local allies solve is not the teams being matched";
        EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10)
            << "the announce latch stays clear";
        EXPECT_EQ(expected_fact(kKnobFair, 0),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
            << "the applied FAIR fact still banks above the latch";
    }
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 700, 2);
        fx.world().ctf_requested_strip_scenario_troops =
            static_cast<short>(og::sim::kTroopsMatched);
        fx.world().ctf_requested_team_count = 0;

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"))
            << "the global solver ran for two teams: one announce";
        // Two level-1 soldiers price below B(1) of a two-member squad, so
        // the solve clamps: the LIMIT variant latches 2, the plain one 1 —
        // either way the digit is latched exactly once.
        EXPECT_NE(0, fx.var(kSlotMatchedAnnounced) % 10);
    }
}

// ===========================================================================
// 2d. The 2026-08-26 amendment rows (docs/lineup-design.md A1-A6): OFF in
//     the staged world, the LV offset on every level source with its
//     clamps, and the TROOPS x BOTS matrix (A4).
// ===========================================================================

// A2: OFF on an authored troops team drops the team exactly as TEAMS: n
// did — its troops are not fielded (stripped with the inactive teams),
// the banked mask and count exclude it, and nothing is banked for it.
TEST_F(StagedRules, lineup_off_drops_an_authored_team_and_its_troops)
{
    ModesCtfWorld fx(kSoccerLevelB);  // teams = 4
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    for (int k = 0; k < 4; ++k)
        fx.spawn_living(FAMILY_ORC, 1, static_cast<short>(300 + 32 * k), 300);
    fx.world().ctf_requested_strip_scenario_troops = 0;  // ALL: troops stand
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[1] = kKnobOff;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0101, fx.var(kSoccerSlots.mask))
        << "the OFF team leaves the banked mask";
    EXPECT_EQ(2, fx.var(kSoccerSlots.count));
    EXPECT_EQ(0, live_livings_on(fx.world(), 1))
        << "the authored troops of a dropped team are not fielded";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 2));
    EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1));
}

// A2: OFF on an occupied team is ignored by the sim — the roster keeps
// the team on (a seat's team always carries a deployed fighter, M4, so
// the LINEUP page refuses the value before it gets here; a lobby that
// still sends it changes nothing but the squad, which OFF forbids like
// NONE).
TEST_F(StagedRules, lineup_off_on_an_occupied_team_is_ignored_by_the_sim)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.spawn_hero(FAMILY_SOLDIER, 1, 200, 700, 1);
    fx.spawn_hero(FAMILY_SOLDIER, 1, 232, 700, 2);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[1] = kKnobOff;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0111, fx.var(kSoccerSlots.mask))
        << "a roster keeps an OFF team on";
    EXPECT_EQ(2, live_livings_on(fx.world(), 1));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1)) << "and OFF fields no squad";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 2));
}

// A2: OFF narrowing a two-team map below two refuses with the mode's own
// sentence (reason digit 0 — the teams sentence), the world kept.
TEST_F(StagedRules, lineup_off_below_two_teams_refuses)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.world().ctf_requested_strip_scenario_troops = 0;
    fx.world().ctf_requested_team_count = 0;
    fx.world().ctf_requested_fill[1] = kKnobOff;

    stage_init(fx);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_TRUE(has_script_error(fx.world(),
                                 "soccer: fewer than two anchor teams"));
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced));
}

// A2: a team the map leaves inactive (the manifest's teams = 3 over four
// authored generator teams, under TROOPS: ALL) turns on by putting
// something on it — here a deployed fighter on onslaught 9402 (the
// preset arm is the harness row in the sweep: onslaught fields no
// squads, D17). And the honest limit of the rule: a preset squad on the
// third team of soccer 9301's two-goal pitch turns the team on, and the
// map itself then refuses it — "no goal rect for team 2" — because
// LINEUP can put a team on the pitch but cannot build it a goal.
TEST_F(StagedRules, lineup_something_on_an_inactive_team_turns_it_on)
{
    {
        ModesCtfWorld fx(kOnsLevelB);  // teams = 3
        for (int team = 0; team < 4; ++team)
        {
            fx.spawn_generator(FAMILY_TENT, team,
                               static_cast<short>(96 + 96 * team), 320);
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 512);
        }
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kOnsSlots.mask))
            << "the manifest's own value: three of the four foundries";
    }
    {
        ModesCtfWorld fx(kOnsLevelB);
        for (int team = 0; team < 4; ++team)
        {
            fx.spawn_generator(FAMILY_TENT, team,
                               static_cast<short>(96 + 96 * team), 320);
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 512);
        }
        fx.spawn_hero(FAMILY_SOLDIER, 3, 200, 700, 1);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b1111, fx.var(kOnsSlots.mask))
            << "a deployed fighter turns the fourth team on";
        EXPECT_EQ(4, fx.var(kOnsSlots.count));
        EXPECT_EQ(1, live_livings_on(fx.world(), 3));
    }
    {
        ModesCtfWorld fx(kSoccerLevelA);  // teams = 2, two goal rects
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_fill[2] = kKnobBalanc;
        stage_init(fx);
        EXPECT_FALSE(fx.world().mode.active);
        EXPECT_TRUE(fx.world().mode.init_attempted);
        EXPECT_TRUE(has_script_error(fx.world(),
                                     "soccer: no goal rect for team 2"))
            << "the preset turned the team on; the two-goal pitch refuses";
    }
}

// A6: the offset on every level source. +2 on the legacy formula (L2 at
// the fixture's 100 percent) is L4; -5 clamps at L1; +2 on a FAIR solve
// is the solved level plus two, banked as the plan; the resolver probe
// pins the clamp at L9 and the upgrade rule directly.
TEST_F(StagedRules, lineup_level_offset_arms_and_clamps)
{
    // +2 on the legacy base, -5 clamped at 1, both on one map.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_map_units[1] = 2;
        fx.world().ctf_requested_map_units[2] = -5;

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        for (const int level : bot_levels_on(fx.world(), 0))
            EXPECT_EQ(2, level) << "AUTO: the formula";
        for (const int level : bot_levels_on(fx.world(), 1))
            EXPECT_EQ(4, level) << "+2 on the formula";
        for (const int level : bot_levels_on(fx.world(), 2))
            EXPECT_EQ(1, level) << "-5 clamps at L1";
        EXPECT_EQ(0, fx.var(kSlotMatchedPlan) % 100) << "AUTO stores no plan";
        EXPECT_EQ(40, (fx.var(kSlotMatchedPlan) / 100) % 100);
        EXPECT_EQ(10, (fx.var(kSlotMatchedPlan) / 10000) % 100);
        EXPECT_EQ(expected_fact(0, 2),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1));
        EXPECT_EQ(expected_fact(0, -5),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 2));
    }
    // +2 on a FAIR solve (TROOPS: FAIR, one hero -> a one-soldier matched
    // squad): the plan level is the solved level plus two, so the twin
    // fixture at offset 0 is the oracle.
    int solved = 0;
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
        fx.world().ctf_requested_strip_scenario_troops =
            static_cast<short>(og::sim::kTroopsMatched);
        fx.world().ctf_requested_team_count = 0;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        solved = ((fx.var(kSlotMatchedPlan) / 100) % 100) / 10;
        ASSERT_GT(solved, 0) << "the global solve stores team 1's plan";
        ASSERT_LT(solved, 8) << "the fixture must leave room for +2";
    }
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
        fx.world().ctf_requested_strip_scenario_troops =
            static_cast<short>(og::sim::kTroopsMatched);
        fx.world().ctf_requested_team_count = 0;
        fx.world().ctf_requested_map_units[1] = 2;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(solved + 2, ((fx.var(kSlotMatchedPlan) / 100) % 100) / 10)
            << "the plan banks the RESOLVED level: solve + 2";
        const std::vector<int> levels = bot_levels_on(fx.world(), 1);
        ASSERT_EQ(1u, levels.size());
        EXPECT_EQ(solved + 2, levels[0]);
        EXPECT_EQ(expected_fact(0, 2),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1))
            << "the offset is the banked fact of a matched squad too";
    }
    // The resolver itself, over the harness: (L, k) through each team's
    // offset, packed L * 10 + k per team at base 100.
    {
        std::array<std::array<int, 4>, 4> teams{};
        const RuleAnswer a = eval_rules(teams, 2, 0b0011, 0, false, false,
                                        {}, 0, {0, 3, -3, -5}, 8, 2);
        EXPECT_EQ(82, a.plans_packed % 100) << "offset 0: identity";
        EXPECT_EQ(90, (a.plans_packed / 100) % 100)
            << "+3 clamps at L9, and L9 admits no upgrades";
        EXPECT_EQ(52, (a.plans_packed / 10000) % 100)
            << "-3: L5 with the upgrades intact";
        EXPECT_EQ(32, (a.plans_packed / 1000000) % 100)
            << "-5: L3, upgrades intact (L4 is still a distinct level)";
        const RuleAnswer low = eval_rules(teams, 2, 0b0011, 0, false, false,
                                          {}, 0, {-3, 0, 0, 0}, 1, 2);
        EXPECT_EQ(10, low.plans_packed % 100)
            << "-3 on L1 clamps at L1 and the upgrades clamp down onto it: "
               "k = 0";
    }
}

// A4: TROOPS sets what BOTS: AUTO resolves to on an empty team, and the
// team's own knob overrides it — three staged rows on one three-team
// map with a two-hero roster on team 0.
TEST_F(StagedRules, lineup_troops_sets_what_auto_resolves_to_and_bots_overrides)
{
    auto build = [](ModesCtfWorld& fx) {
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 128, 700, 2);
        fx.world().ctf_requested_team_count = 0;
    };
    // TROOPS: FAIR + BOTS: AUTO everywhere = matched squads, sized to the
    // roster headcount, solved against the banked target.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        build(fx);
        fx.world().ctf_requested_strip_scenario_troops =
            static_cast<short>(og::sim::kTroopsMatched);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_GT(fx.var(kSlotMatchedTarget), 0);
        EXPECT_EQ(2, marked_bots_on(fx.world(), 1)) << "matched: headcount 2";
        EXPECT_EQ(2, marked_bots_on(fx.world(), 2));
        EXPECT_NE(0, (fx.var(kSlotMatchedPlan) / 100) % 100);
        EXPECT_NE(0, (fx.var(kSlotMatchedPlan) / 10000) % 100);
    }
    // TROOPS: FAIR + BOTS: NONE on team 1 = none there; team 2 still
    // matched.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        build(fx);
        fx.world().ctf_requested_strip_scenario_troops =
            static_cast<short>(og::sim::kTroopsMatched);
        fx.world().ctf_requested_fill[1] = kKnobNone;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0101, fx.var(kSoccerSlots.mask));
        EXPECT_EQ(0, live_livings_on(fx.world(), 1)) << "NONE overrides FAIR";
        EXPECT_EQ(2, marked_bots_on(fx.world(), 2));
        EXPECT_NE(0, (fx.var(kSlotMatchedPlan) / 10000) % 100);
    }
    // TROOPS: ALL + BOTS: FAIR on team 1 = matched on that team only:
    // team 1 solves locally (a plan, five allies), team 2 keeps the
    // legacy formula squad (no plan, L2), and no global target is banked.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        build(fx);
        fx.world().ctf_requested_strip_scenario_troops = 0;
        fx.world().ctf_requested_fill[1] = kKnobFair;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0, fx.var(kSlotMatchedTarget)) << "no match-wide solve";
        EXPECT_EQ(5, marked_bots_on(fx.world(), 1));
        EXPECT_NE(0, (fx.var(kSlotMatchedPlan) / 100) % 100)
            << "FAIR on team 1 solves team 1";
        EXPECT_EQ(0, (fx.var(kSlotMatchedPlan) / 10000) % 100)
            << "team 2 is the legacy squad";
        EXPECT_EQ(5, marked_bots_on(fx.world(), 2));
        for (const int level : bot_levels_on(fx.world(), 2))
            EXPECT_EQ(2, level) << "ALL + AUTO = the formula";
        EXPECT_EQ(expected_fact(kKnobFair, 0),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1));
    }
}

// ===========================================================================
// 3. The apply-executes-decision matrix: the shared rules (via the harness)
//    produce the expected values; the staged world must bank and field
//    exactly those. 16 cases per mode, the old agreement matrix's shapes.
// ===========================================================================

namespace {

struct MatrixMode
{
    const char* name;
    int level_id;
    ModeSlots slots;
    int auto_default;   // manifest row.teams (0 = the CTF/TDM raw arm)
    bool keep_generators;
    bool no_bots;
};

// The shared matrix world: the mode's authored domain on teams 0-2, one
// guy-less npc on team 1, and `roster` heroes per team (roster teams are a
// subset of {0, 2}, so the authored domain is 0b0111 for every mode — the
// construction knowledge the old matrix pinned via the plan's fold).
void author_matrix_world(const MatrixMode& mode, ModesCtfWorld& fx,
                         int flag_family, const std::array<int, 4>& roster)
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
        if (ctf || ons)
            fx.spawn_anchor(team, x, 512);  // squads/spawns need anchors
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

// `off_team` is the team whose fill knob reads OFF (-1 = none): the
// control that succeeded the lobby TEAMS count (lineup A1/A2), so the
// matrix's count dimension became an OFF dimension.
void run_staged_case(const MatrixMode& mode, int flag_family, int strip,
                     int off_team, const std::array<int, 4>& roster)
{
    SCOPED_TRACE(::testing::Message()
                 << mode.name << " strip=" << strip << " off="
                 << off_team << " roster=" << roster[0] << roster[1]
                 << roster[2] << roster[3]);
    // The expected decision FIRST, from the ONE shared rule (the harness
    // probe world lives and dies before the staged fixture world exists —
    // the strictly-sequential twin-world discipline): the matrix world
    // authors domain 0b0111 with the census rows below. (activation reads
    // roster/knobs; fills reads roster/npcs/generators — the anchors
    // column is inert to both and kept for the inputs shape.)
    std::array<std::array<int, 4>, 4> teams{};
    for (int t = 0; t < 3; ++t)
    {
        teams[static_cast<std::size_t>(t)][0] = 1;
        if (std::string(mode.name) == "onslaught")
            teams[static_cast<std::size_t>(t)][3] = 1;  // the foundry
    }
    teams[1][2] = 1;  // the guy-less npc on team 1
    for (int t = 0; t < 4; ++t)
        teams[static_cast<std::size_t>(t)][1] =
            roster[static_cast<std::size_t>(t)];
    std::array<int, 4> fill{};
    if (off_team >= 0)
        fill[static_cast<std::size_t>(off_team)] = kKnobOff;

    const RuleAnswer expected =
        eval_rules(teams, strip, 0b0111, mode.auto_default,
                   mode.keep_generators, mode.no_bots, fill);
    ASSERT_GE(expected.mask, 0);
    ASSERT_GE(expected.fills_packed, 0);
    const int expected_mask = expected.mask;
    const bool expected_starts = expected.starts;
    const bool expected_matched = expected.matched;
    const int expected_size = expected.matched_size;
    const std::int64_t fills_packed = expected.fills_packed;

    ModesCtfWorld fx(mode.level_id);
    fx.world().ctf_requested_strip_scenario_troops =
        static_cast<short>(strip);
    fx.world().ctf_requested_team_count = 0;
    if (off_team >= 0)
        fx.world().ctf_requested_fill[static_cast<std::size_t>(
            off_team)] = static_cast<short>(kKnobOff);
    author_matrix_world(mode, fx, flag_family, roster);
    stage_init(fx);
    if (!expected_starts)
    {
        EXPECT_FALSE(fx.world().mode.active)
            << "the apply must refuse when the rule refuses";
        return;
    }
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(expected_mask, fx.var(mode.slots.mask))
        << "the apply must bank the rule's mask";
    if (mode.slots.count >= 0)
    {
        int bits = 0;
        for (int t = 0; t < 4; ++t)
        {
            if ((expected_mask & (1 << t)) != 0)
                bits++;
        }
        EXPECT_EQ(bits, fx.var(mode.slots.count));
    }
    if (expected_matched && expected_size > 0)
    {
        EXPECT_EQ(expected_size, fx.var(kSlotMatchedSize));
        EXPECT_GT(fx.var(kSlotMatchedTarget), 0)
            << "a live has_guy walker always prices above zero";
    }
    else
    {
        EXPECT_EQ(0, fx.var(kSlotMatchedSize));
        EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    }
    // Fielded entities follow the rule's fill rows exactly (fill codes:
    // empty 0, company 1, troops 2, bots 3, matched 4, generators 5).
    std::int64_t remaining = fills_packed;
    for (int team = 0; team < 4; ++team)
    {
        const int digit = static_cast<int>(remaining % 100);
        remaining /= 100;
        const int fill_code = digit % 8;
        const int fill_count = digit / 8;
        int expected_livings = 0;
        switch (fill_code)
        {
        case 1:  // company: the roster stands; under ALL team 1's npc too
            expected_livings = fill_count;
            if (strip == 0 && team == 1)
                expected_livings += 1;
            break;
        case 2:  // troops
        case 3:  // bots
        case 4:  // matched
            expected_livings = fill_count;
            break;
        default:  // empty / generators (no infantry at stage time)
            expected_livings = 0;
            break;
        }
        EXPECT_EQ(expected_livings, live_livings_on(fx.world(), team))
            << "team " << team << " fill code " << fill_code;
        const int expected_marks =
            (fill_code == 3 || fill_code == 4) ? fill_count : 0;
        EXPECT_EQ(expected_marks, marked_bots_on(fx.world(), team))
            << "BOT_MARK provenance, team " << team;
    }
    if (std::string(mode.name) == "ctf")
    {
        // The banked FLAG_ENTITY entries are the authored fold minus the
        // inactive-team teardown — the rule's active mask, per team.
        for (int team = 0; team < 4; ++team)
        {
            const bool active = (expected_mask & (1 << team)) != 0;
            EXPECT_EQ(active, fx.team_var(kSlotFlagEntity, team) != 0)
                << "team " << team
                << ": banked FLAG_ENTITY must follow the rule";
        }
    }
}

// The OFF dimension in every position: none, on the roster team (ignored
// — the roster keeps it on), on the npc team (its troops leave with it)
// and on the empty backfill team; the last row puts OFF on the second
// roster team of the two-roster shape.
void run_staged_matrix(const MatrixMode& mode, int flag_family)
{
    const std::array<int, 4> none{0, 0, 0, 0};
    const std::array<int, 4> solo{2, 0, 0, 0};
    const std::array<int, 4> two{2, 0, 1, 0};
    for (int strip : {0, 2, 3})
    {
        for (int off : {-1, 0, 1, 2})
            run_staged_case(mode, flag_family, strip, off, solo);
    }
    run_staged_case(mode, flag_family, 2, -1, none);
    run_staged_case(mode, flag_family, 3, -1, none);
    run_staged_case(mode, flag_family, 3, -1, two);
    run_staged_case(mode, flag_family, 3, 2, two);
}

}  // namespace

TEST_F(StagedRules, staged_world_matrix_soccer)
{
    run_staged_matrix({"soccer", kSoccerLevelB, kSoccerSlots, 4, false, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_basketball)
{
    run_staged_matrix({"basketball", kBballLevelB, kBballSlots, 4, false,
                       false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_ctf)
{
    run_staged_matrix({"ctf", kCtfLevelA, kCtfSlots, 0, false, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_tdm)
{
    run_staged_matrix({"tdm", kTdmLevelA, kTdmSlots, 0, false, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_onslaught)
{
    run_staged_matrix({"onslaught", kOnsLevelB, kOnsSlots, 3, true, true},
                      flag_family_);
}

// ===========================================================================
// 4. Staged-vs-adopted BYTE identity, per mode, over the real shipped
//    levels: stage through MatchStage, adopt through the SDL shadow's
//    content-transfer seam, and the adopted tick-0 keyframe must equal the
//    staged pair byte for byte (preview == launch as an identity; the
//    soccer arm is MatchStageTest.adopted_world_keyframe_is_byte_identical_
//    to_preview in test_match_stage.cpp).
// ===========================================================================

namespace {

og::sim::LobbyCharacterSlot adoption_slot()
{
    og::sim::LobbyCharacterData character;
    character.guy_id = 100;
    character.name = "Adopter";
    character.family = FAMILY_SOLDIER;
    character.strength = 10;
    character.dexterity = 11;
    character.constitution = 12;
    character.intelligence = 13;
    character.armor = 14;
    character.level = 3;
    character.teamnum = 0;
    return {.slot_index = 0u, .character = character};
}

void run_adoption_identity(short level_id)
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    og::server::MatchStage stage({.networked = true});
    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "modes";
    inputs.equivalent.scen_num = level_id;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.team_list = {adoption_slot()};
    inputs.difficulty = 1;
    inputs.match_seed = 4242u;
    stage.observe_inputs(inputs, /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    const std::vector<std::uint8_t> preview_bytes =
        stage.staged_keyframe_bytes();
    ASSERT_FALSE(preview_bytes.empty());

    LevelRuntimeData dst_level(level_id, /*headless=*/true,
                               &headless_level_data_hooks());
    SaveData dst_save;
    GameWorld& dst = dst_level.world();
    og::sim::SimEventLog dst_events;
    IRandom* dst_rng = &dst.rng_;
    bool dst_active = false;
    GameplayContext dst_ctx;
    dst_ctx.world = &dst;
    dst_ctx.save = &dst_save;
    dst_ctx.sim_events = &dst_events;
    dst_ctx.config = &cfg;
    dst_ctx.session_rng_ref = &dst_rng;
    dst_ctx.gameplay_active_ref = &dst_active;
    GameplayContext* const previous_context = current_game;
    current_game = &dst_ctx;
    dst.tick_count_ = 0;
    dst.reset_level_progress();
    ASSERT_TRUE(og::server::adopt_staged_world(dst_level, dst_save, stage));
    dst_events.append(stage.take_events());
    stage.dispose();
    current_game = previous_context;

    EXPECT_EQ(preview_bytes,
              og::sim::serialize_snapshot(og::sim::peek_keyframe_snapshot(dst)))
        << "adoption must not perturb a single replicated byte (level "
        << level_id << ")";
    EXPECT_FALSE(dst.owes_level_on_load());
}

}  // namespace

using StagedAdoption = ModesPackTest;

TEST_F(StagedAdoption, staged_adoption_identity_ctf)
{
    run_adoption_identity(500);
}

TEST_F(StagedAdoption, staged_adoption_identity_tdm)
{
    run_adoption_identity(300);
}

TEST_F(StagedAdoption, staged_adoption_identity_basketball)
{
    run_adoption_identity(824);
}

TEST_F(StagedAdoption, staged_adoption_identity_onslaught)
{
    run_adoption_identity(800);
}
