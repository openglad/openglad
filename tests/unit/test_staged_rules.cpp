// The staged-init rule oracles (#218 plan-phase retirement): the shared
// activation/fill rules live in lib/mode_match.lua alone (match.activation /
// match.fills), consumed by each mode's decide fold at the top of
// on_mode_init — which now runs ONCE, at staging, in a REAL world. These
// suites pin the rules three ways:
//
//  1. The activation precedence sweep keeps its coverage through a
//     direct-Lua harness calling match.activation on synthetic inputs
//     tables (the inventory-sanctioned shape — the reborn
//     roster_effective_team_mask sweep).
//  2. The per-mode domain/fill/limit oracles are STAGED-WORLD assertions:
//     build the fixture shape, run the real mode_stage_init (the exact
//     function MatchStage runs), assert the banked mode vars and the
//     fielded entities exactly.
//  3. The apply-executes-decision matrix: the shared Lua rules (via the
//     harness) produce the expected values and the staged world must bank
//     and field exactly those — plus the per-mode staged-vs-adopted
//     BYTE-identity oracles over the real shipped levels (preview ==
//     launch as an assertable identity; the soccer arm lives in
//     test_match_stage.cpp since C4/C7).
//
// Amendment 2 (docs/lineup-design.md B1-B9) reshaped the knob rows: the
// per-team controls are a FILL wheel (FAIR/NONE/WEAK/STRONG/BRUTAL — the
// matched solver with a multiplier over the weakest human team's f-sum)
// and a MAP UNITS box (the old TROOPS strip, per team). TROOPS and TEAMS
// are inert engine-side and read by nothing here. THE OLD ALL-ZERO
// BYTE-IDENTITY PINS ARE GONE BY DESIGN: the default is FILL: FAIR, which
// SOLVES every backfilled squad against the human reference where one
// exists (the old default left them on the legacy difficulty formula) and
// banks the applied fill fact — the new default rows below are the
// replacement pins.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>

#include "../modes_pack_fixture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
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

// Parameter slots: 25..28 the four MAP UNITS boxes, 30..33 the four FILL
// knobs, 34 the hard-shape cap (0 = none), 38..41 the per-team human
// f-sums (fills decides the allies gap and the weakest reference from the
// inputs alone — the magnitudes only matter through comparisons, so the
// matrix hands in placeholder sums shaped like the fixture rosters),
// 42..57 the four census columns, 58/59 authored mask + auto_default,
// 62 no_bots; answers 60 (mask/starts/matched/matched_size), 63 (fill
// rows), 36 (squad codes), 37 (the narrowed lineup mask).
constexpr const char* kRuleProbeLua =
    "local match = og.use(\"mode_match\")\n"
    "og.register_level_hooks(9092, {\n"
    "  on_load = function(level)\n"
    "    local inputs = {\n"
    "      score_limit = 0,\n"
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
    "        power = og.mode_get(38 + t),\n"
    "      }\n"
    "      inputs.fill[t + 1] = og.mode_get(30 + t)\n"
    "      inputs.map_units[t + 1] = og.mode_get(25 + t)\n"
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
    int lineup_mask = -1;   // fills' narrowed mask
};

// One probe dispatch (its own short-lived world, destroyed before any
// staged fixture world is built). fill carries the four FILL knobs
// (0 FAIR / 1 NONE / 2 WEAK / 3 STRONG / 4 BRUTAL — the engine scale),
// map_units the four boxes (0 on / 1 off), power the per-team human
// f-sums, squad_cap the caller's hard shape (0 = none) — all default
// zero, the all-FAIR-boxes-on default state.
RuleAnswer eval_rules(const std::array<std::array<int, 4>, 4>& teams,
                      unsigned authored, int auto_default, bool no_bots,
                      const std::array<int, 4>& fill = {},
                      int squad_cap = 0,
                      const std::array<int, 4>& map_units = {},
                      const std::array<int, 4>& power = {})
{
    RuleProbeScript probe;
    ModesCtfWorld fx(9092);
    GameWorld& w = fx.world();
    for (std::size_t t = 0; t < 4; ++t)
    {
        w.mode.vars[42 + t] = teams[t][0];
        w.mode.vars[46 + t] = teams[t][1];
        w.mode.vars[50 + t] = teams[t][2];
        w.mode.vars[54 + t] = teams[t][3];
        w.mode.vars[30 + t] = fill[t];
        w.mode.vars[25 + t] = map_units[t];
        w.mode.vars[38 + t] = power[t];
    }
    w.mode.vars[34] = squad_cap;
    w.mode.vars[58] = static_cast<std::int32_t>(authored);
    w.mode.vars[59] = auto_default;
    w.mode.vars[62] = no_bots ? 1 : 0;
    w.mode.vars[60] = -1;
    w.mode.vars[63] = -1;
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
    return answer;
}

// The convenience mask-only read the sweep uses: one anchor per authored
// team, one roster fighter per roster team (the old sweep_inputs shape;
// roster teams carry a placeholder f-sum so the census shape is honest)
// and the caller's manifest default.
int activation_mask(unsigned authored, unsigned roster,
                    int auto_default = 0)
{
    std::array<std::array<int, 4>, 4> teams{};
    std::array<int, 4> power{};
    for (int t = 0; t < 4; ++t)
    {
        if ((authored & (1u << t)) != 0)
            teams[static_cast<std::size_t>(t)][0] = 1;
        if ((roster & (1u << t)) != 0)
        {
            teams[static_cast<std::size_t>(t)][1] = 1;
            power[static_cast<std::size_t>(t)] = 100;
        }
    }
    return eval_rules(teams, authored, auto_default, false, {}, 0, {},
                      power)
        .mask;
}

// The engine's fill scale (lobby_state.h kFill*, D1: 0 is the stored
// DEFAULT and the five explicit codes follow in wheel order), spelled
// once for the rows below.
constexpr int kKnobDefault = og::sim::kFillDefault;  // 0
constexpr int kKnobNone = og::sim::kFillNone;        // 1
constexpr int kKnobWeak = og::sim::kFillWeak;        // 2
constexpr int kKnobFair = og::sim::kFillFair;        // 3
constexpr int kKnobStrong = og::sim::kFillStrong;    // 4
constexpr int kKnobBrutal = og::sim::kFillBrutal;    // 5

// The MAP UNITS box scale (lobby_state.h kMapUnits*).
constexpr int kBoxOn = og::sim::kMapUnitsOn;    // 0
constexpr int kBoxOff = og::sim::kMapUnitsOff;  // 1

}  // namespace

using StagedRules = ModesPackTest;

// ===========================================================================
// 1. Activation precedence — the rows through the direct-Lua harness (the
//    rule's unit-level oracle; auto_default 0 = CTF/TDM's arm, the
//    manifest-default arm is pinned on staged worlds below). "A team is on
//    when anything is on it": the map's own value plus the occupied
//    authored teams; the knobs narrow later, in the fills rows.
// ===========================================================================

TEST_F(StagedRules, activation_precedence_sweep)
{
    // The map's own value: auto_default 0 = every authored team (the
    // CTF/TDM raw arm)...
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0));
    EXPECT_EQ(0b1101, activation_mask(0b1101, 0));
    // ...and a manifest default clamps to the first N authored teams
    // (soccer's row.teams arm).
    EXPECT_EQ(0b0011, activation_mask(0b1111, 0, 2));
    EXPECT_EQ(0b0101, activation_mask(0b1101, 0, 2));

    // A roster keeps its authored team on wherever the default left it...
    EXPECT_EQ(0b1111, activation_mask(0b1111, 0b0001));
    EXPECT_EQ(0b0101, activation_mask(0b0101, 0b0100));
    EXPECT_EQ(0b0111, activation_mask(0b1111, 0b0100, 2))
        << "a deployed roster turns on a team the manifest leaves inactive";
    // ...unless nothing else is authored (the lone bit stands, and the
    // rule reports the not-starting shape).
    EXPECT_EQ(0b0100, activation_mask(0b0100, 0b0100));

    // Roster bits outside the authored domain are masked off: nowhere to
    // spawn or score, so nothing activates there.
    EXPECT_EQ(0b0011, activation_mask(0b0011, 0b1100));
    EXPECT_EQ(0b0011, activation_mask(0b0011, 0b0110));

    // Fielded map units activate their authored team past the manifest
    // default (B4: checked units play); the box off takes that back, and
    // units outside the authored domain activate nothing.
    {
        std::array<std::array<int, 4>, 4> teams{};
        for (int t = 0; t < 3; ++t)
            teams[static_cast<std::size_t>(t)][0] = 1;
        teams[2][2] = 2;  // npcs on team 2, beyond the default of 2
        EXPECT_EQ(0b0111, eval_rules(teams, 0b0111, 2, false).mask);
        EXPECT_EQ(0b0011, eval_rules(teams, 0b0111, 2, false, {}, 0,
                                     {0, 0, kBoxOff, 0})
                              .mask)
            << "the box off takes the units-activation back";
        std::array<std::array<int, 4>, 4> outside{};
        outside[0][0] = 1;
        outside[1][0] = 1;
        outside[3][2] = 2;  // npcs on an unauthored team
        EXPECT_EQ(0b0011, eval_rules(outside, 0b0011, 0, false).mask);
    }

    // The wheel is activation-blind (B8: nothing on the band refuses or
    // deactivates at this step — fills narrows a team the knobs leave
    // empty, one step later).
    {
        std::array<std::array<int, 4>, 4> teams{};
        for (int t = 0; t < 4; ++t)
            teams[static_cast<std::size_t>(t)][0] = 1;
        const RuleAnswer a = eval_rules(teams, 0b1111, 0, false,
                                        {0, kKnobNone, 0, kKnobBrutal});
        EXPECT_EQ(0b1111, a.mask) << "activation ignores every wheel value";
        EXPECT_EQ(0b1101, a.lineup_mask)
            << "the NONE-emptied team drops in the fills rows";
    }
}

// The matched census facts: matched = any deployed roster anywhere (the
// solver's reference will exist), matched_size = the D34 min-roster rule.
TEST_F(StagedRules, activation_matched_size_is_the_min_roster_headcount)
{
    std::array<std::array<int, 4>, 4> teams{};
    teams[0][0] = 1;
    teams[1][0] = 1;
    teams[2][0] = 1;
    {
        const RuleAnswer a = eval_rules(teams, 0b0111, 0, false);
        EXPECT_FALSE(a.matched) << "no roster, no reference";
        EXPECT_EQ(0, a.matched_size);
    }
    {
        std::array<std::array<int, 4>, 4> solo = teams;
        solo[0][1] = 3;
        const RuleAnswer a = eval_rules(solo, 0b0111, 0, false, {}, 0, {},
                                        {300, 0, 0, 0});
        EXPECT_TRUE(a.matched);
        EXPECT_EQ(3, a.matched_size) << "one roster team = its count";
    }
    {
        std::array<std::array<int, 4>, 4> two = teams;
        two[0][1] = 3;
        two[2][1] = 5;
        const RuleAnswer a = eval_rules(two, 0b0111, 0, false, {}, 0, {},
                                        {300, 0, 500, 0});
        EXPECT_TRUE(a.matched);
        EXPECT_EQ(3, a.matched_size) << "several roster teams = the MIN";
    }
}

// ===========================================================================
// 2. The per-mode staged oracles: real fixture worlds through the real
//    staged init; banked vars and fielded entities pinned exactly.
// ===========================================================================

TEST_F(StagedRules, auto_default_asymmetry_is_per_mode)
{
    // Soccer/basketball/onslaught: the manifest row.teams is the map's
    // own value. 9301 declares teams = 2 — with four anchor teams
    // authored, the staged init fields exactly two.
    {
        ModesCtfWorld fx(kSoccerLevelA);
        for (int team = 0; team < 4; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0011, fx.var(kSoccerSlots.mask))
            << "the manifest row.teams default (2), not the authored count";
        EXPECT_EQ(2, fx.var(kSoccerSlots.count));
    }
    // 9302 declares teams = 4: three authored anchors clamp to all three.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kSoccerSlots.mask));
    }
    // CTF/TDM: no manifest default — every authored team.
    {
        ModesCtfWorld fx(kTdmLevelA);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kTdmSlots.mask))
            << "TDM: every authored team (no manifest default)";
    }
    // Onslaught 9401 declares teams = 2, but standing foundries are
    // FIELDED MAP UNITS and fielded units activate their team (B4 —
    // checked units play, whatever the manifest count says).
    {
        ModesCtfWorld fx(kOnsLevelA);
        fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
        fx.spawn_generator(FAMILY_TENT, 1, 480, 320);
        fx.spawn_generator(FAMILY_TENT, 2, 640, 320);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kOnsSlots.mask))
            << "fielded map units activate every foundry team";
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

    stage_init(awake);
    ASSERT_TRUE(awake.world().mode.active);
    EXPECT_EQ(0b0111, awake.var(kTdmSlots.mask))
        << "an awake troop on the same team authors normally";
}

TEST_F(StagedRules, onslaught_domain_and_generator_fills)
{
    ModesCtfWorld fx(kOnsLevelB);
    // The per-team strip: team 1's box is off, so its authored npc is not
    // fielded — and with no generators of its own and no bots ever (D17),
    // the team leaves the match outright.
    fx.world().ctf_requested_map_units[1] = kBoxOff;
    fx.spawn_generator(FAMILY_TENT, 0, 128, 320);
    fx.spawn_generator(FAMILY_TENT, 0, 192, 320);
    fx.spawn_living(FAMILY_ORC, 1, 300, 640);
    fx.spawn_hero(FAMILY_SOLDIER, 2, 400, 640, 1);

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0101, fx.var(kOnsSlots.mask))
        << "a box-off team with nothing else standing leaves the match";
    EXPECT_EQ(2, fx.var(kOnsSlots.count));
    EXPECT_EQ(0, live_livings_on(fx.world(), 0));
    EXPECT_EQ(0, live_livings_on(fx.world(), 1))
        << "the box strips the guy-less npc";
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

// The default (FILL: FAIR everywhere) solves every backfilled squad
// against the weakest human team's f-sum, sized to the min roster
// headcount (B2/B3) — the successor of the old TROOPS: FAIR rows.
TEST_F(StagedRules, default_fill_matches_squads_to_the_min_roster_headcount)
{
    // Rosters of 3 and 5: the backfilled teams field squads truncated to
    // the MIN headcount, and MATCHED.TARGET banks the weakest reference.
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

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3, fx.var(kSlotMatchedSize))
        << "several roster teams -> the MIN headcount (D34)";
    EXPECT_GT(fx.var(kSlotMatchedTarget), 0)
        << "a live has_guy walker always prices above zero";
    EXPECT_EQ(3 + 3, live_livings_on(fx.world(), 0))
        << "the weaker company gets allies too (B3), headcount-sized";
    EXPECT_EQ(3, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(5, live_livings_on(fx.world(), 2));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 2))
        << "the stronger company has no gap";
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
    stage_init(outside);
    ASSERT_TRUE(outside.world().mode.active);
    EXPECT_EQ(0b0011, outside.var(kSoccerSlots.mask))
        << "an unauthored roster team never activates";
    EXPECT_EQ(2, outside.var(kSlotMatchedSize))
        << "but its headcount still bounds the matched size";
    EXPECT_EQ(2, marked_bots_on(outside.world(), 1));
}

TEST_F(StagedRules, no_human_power_degrades_to_the_legacy_squads)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0, fx.var(kSlotMatchedSize))
        << "zero rosters anywhere predicts no reference";
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget));
    for (int team = 0; team < 3; ++team)
    {
        EXPECT_EQ(5, marked_bots_on(fx.world(), team))
            << "the legacy difficulty squad, NOT matched (B3's fallback), "
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
    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(3, fx.var(kSoccerSlots.score)) << "no request -> the row limit";
    EXPECT_EQ(4, live_livings_on(fx.world(), 1))
        << "with the box on the authored troops stand";
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1))
        << "fielded map units are the team's fill: no squad beside them";
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
    // TDM's fixed squad table makes the truncation exactly knowable: min
    // headcount 1 -> the backfilled team fields precisely one marked
    // soldier (the D35 soldier-first prefix) — under the DEFAULT knobs
    // now (FILL: FAIR is the default solver).
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1, fx.var(kSlotMatchedSize));
    EXPECT_EQ(1, marked_bots_on(fx.world(), 1))
        << "the matched squad truncates to the min roster headcount";
    EXPECT_EQ(1, live_livings_on(fx.world(), 1));
}

// ===========================================================================
// 2b. The FILL x MAP UNITS rows (docs/lineup-design.md B1-B9): NONE
//     removes the fill FAIR makes, the wheel scales the solve target
//     monotonically, allies ride an occupied team's gap, the box strips a
//     team's authored units, and the banked facts carry the fill code.
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

// The expected code for an applied fill: since D1 the code IS the
// applied wheel code (the +1 bias is retired on both sides — no
// explicit code is 0 any more, so 0 is unambiguously "nothing banked")
// — spelled here independently so the test is an oracle of the packing,
// not a mirror of it.
int expected_fact(int fill)
{
    return fill;
}

// The refusal reason digit (mode_match.lua REFUSAL_BASE, picker_common.cpp
// kLineupRefusalBase): 10^9, alone in the slot when a band fold refuses.
constexpr std::int32_t kRefusalFighters = 1000000000;

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

// The wheel fixture: one L5 soldier hero on team 0, TDM two-anchor map,
// team 1 backfilled with a single solved soldier (headcount 1). Answers
// the solved plan level for one wheel value.
int solved_level_for_fill(int fill)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 5);
    fx.world().ctf_requested_fill[1] = static_cast<short>(fill);
    og::sim::mode_stage_init(fx.world());
    EXPECT_TRUE(fx.world().mode.active);
    if (!fx.world().mode.active)
        return -1;
    const std::vector<int> levels = bot_levels_on(fx.world(), 1);
    EXPECT_EQ(1u, levels.size());
    const int plan_code = (fx.var(kSlotMatchedPlan) / 100) % 100;
    EXPECT_EQ(plan_code / 10, levels.empty() ? -1 : levels[0])
        << "the spawned level IS the stored plan";
    return plan_code / 10;
}

}  // namespace

// C8 in a mode: the stored default keeps FAIR on every AUTHORED team —
// anchors are presence — and the banked fact says FAIR (never a resolved
// NONE) wherever a default squad walked on. The resolution flips only
// teams the map does not author, which a mode's fills never touch anyway.
TEST_F(StagedRules, default_resolution_keeps_fair_on_authored_mode_teams)
{
    ModesCtfWorld fx(kTdmLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1, 5);
    og::sim::mode_stage_init(fx.world());
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(1u, bot_levels_on(fx.world(), 1).size())
        << "the empty authored team fields its FAIR squad under the default";
    EXPECT_EQ(expected_fact(kKnobFair),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1))
        << "and banks the resolved FAIR — an authored empty team never "
           "resolves NONE, and the banked code is the explicit FAIR it "
           "resolved to (D1), never the stored 0";
}

// The wheel is the solver's multiplier (B2): on a fixed one-hero L5
// roster the solved level of the single backfilled bot tracks the target
// monotonically, and the exact levels are pinned for this roster.
TEST_F(StagedRules, fill_wheel_scales_the_solved_level_monotonically)
{
    const int weak = solved_level_for_fill(kKnobWeak);
    const int fair = solved_level_for_fill(kKnobFair);
    const int strong = solved_level_for_fill(kKnobStrong);
    const int brutal = solved_level_for_fill(kKnobBrutal);
    EXPECT_LE(weak, fair) << "WEAK solves at or below FAIR";
    EXPECT_LE(fair, strong) << "STRONG solves at or above FAIR";
    EXPECT_LE(strong, brutal) << "BRUTAL solves at or above STRONG";
    EXPECT_LT(weak, brutal) << "the multipliers must separate the ends";
    // The exact pins for this fixed roster (one L5 soldier vs one solved
    // soldier; the D22 argmin against reference x {75, 100, 125, 150}%).
    // WEAK and FAIR land the same argmin on this coarse one-bot grid —
    // the separation shows from FAIR to STRONG.
    EXPECT_EQ(3, weak);
    EXPECT_EQ(3, fair);
    EXPECT_EQ(4, strong);
    EXPECT_EQ(4, brutal);
}

// NONE suppresses exactly the squad FAIR fields: the identical world
// backfills squads on every empty team; NONE on team 1 fields none there
// and drops the team from the banked mask (B4/B8).
TEST_F(StagedRules, fill_none_removes_the_squad_fair_fields)
{
    ModesCtfWorld fx(kSoccerLevelB);
    for (int team = 0; team < 3; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.world().ctf_requested_fill[1] = kKnobNone;

    stage_init(fx);
    ASSERT_TRUE(fx.world().mode.active);
    EXPECT_EQ(0b0101, fx.var(kSoccerSlots.mask))
        << "a NONE-emptied backfill team leaves the banked mask";
    EXPECT_EQ(2, fx.var(kSoccerSlots.count));
    EXPECT_EQ(5, marked_bots_on(fx.world(), 0));
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1))
        << "NONE suppresses the squad FAIR would field";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 2));
    EXPECT_EQ(0, live_livings_on(fx.world(), 1));
    EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 1))
        << "nothing spawned, nothing banked";
    EXPECT_EQ(expected_fact(kKnobFair),
              lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
        << "the FAIR squads bank the applied fill code";
}

// NONE narrowing below two teams refuses the match with the mode's own
// sentence — an empty team stays inactive, and one team is no match.
TEST_F(StagedRules, fill_none_below_two_teams_refuses)
{
    ModesCtfWorld fx(kSoccerLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 528, 96);
    fx.world().ctf_requested_fill[1] = kKnobNone;

    stage_init(fx);
    EXPECT_FALSE(fx.world().mode.active);
    EXPECT_TRUE(fx.world().mode.init_attempted);
    EXPECT_TRUE(has_script_error(fx.world(),
                                 "soccer: fewer than two anchor teams"));
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced))
        << "a team-mode refusal banks no reason digit: the teams sentence";
}

// The weakest human team is the reference (B3): adding a STRONGER second
// human team must not move an empty team's solved level — the twin
// fixture with the weak roster alone is the oracle.
TEST_F(StagedRules, reference_is_the_weakest_human_team)
{
    int solo_level = 0;
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        const std::vector<int> levels = bot_levels_on(fx.world(), 1);
        ASSERT_EQ(1u, levels.size());
        solo_level = levels[0];
    }
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        fx.spawn_leveled_hero(FAMILY_SOLDIER, 2, 96, 760, 2, 9);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        const std::vector<int> levels = bot_levels_on(fx.world(), 1);
        ASSERT_EQ(1u, levels.size());
        EXPECT_EQ(solo_level, levels[0])
            << "the reference is the WEAKEST team's f-sum: a stronger "
               "second team must not raise the empty team's squad";
    }
}

// Allies (B3): FILL on an occupied team targets the gap to the strongest
// other team. The weaker company gets solved allies sized by the
// headcount rule; equal companies get none; the applied fill banks only
// where a squad spawned (R4).
TEST_F(StagedRules, allies_ride_the_gap_on_an_occupied_team)
{
    // Two L1 heroes vs five L3 heroes: team 0 is behind, so its default
    // FAIR fill fields allies (headcount min = 2).
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

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(2, marked_bots_on(fx.world(), 0))
            << "the weaker company gets allies, sized by the headcount";
        EXPECT_EQ(2 + 2, live_livings_on(fx.world(), 0));
        EXPECT_EQ(0, marked_bots_on(fx.world(), 2))
            << "the stronger company has no gap to close";
        EXPECT_NE(0, fx.var(kSlotMatchedPlan) % 100)
            << "the allies solve stores team 0's plan";
        EXPECT_EQ(expected_fact(kKnobFair),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
            << "the applied fill code is banked for the pane";
        EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 2))
            << "no squad on the stronger company, nothing banked";
    }
    // Equal companies: no gap anywhere, no allies anywhere.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        fx.spawn_hero(FAMILY_SOLDIER, 2, 96, 760, 2);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0, marked_bots_on(fx.world(), 0));
        EXPECT_EQ(0, marked_bots_on(fx.world(), 2));
        EXPECT_EQ(1, live_livings_on(fx.world(), 0));
        EXPECT_EQ(1, live_livings_on(fx.world(), 2));
    }
}

// The MAP UNITS box (B4): off strips the team's authored troops — the
// team then plays with its FILL squad instead; off with FILL: NONE and
// nothing else leaves the team inactive; on with NONE and no seat keeps
// the team active with its troops alone.
TEST_F(StagedRules, map_units_box_strips_and_deactivates)
{
    // Box off + FILL default: the orcs go, a solved/legacy squad walks on.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        for (int k = 0; k < 4; ++k)
            fx.spawn_living(FAMILY_ORC, 1, static_cast<short>(300 + 32 * k),
                            300);
        fx.world().ctf_requested_map_units[1] = kBoxOff;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kSoccerSlots.mask))
            << "the FILL squad keeps the box-off team in the match";
        EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
            << "the squad replaces the stripped troops";
        EXPECT_EQ(5, live_livings_on(fx.world(), 1))
            << "no orc survives the box";
    }
    // Box off + FILL NONE + nothing else: inactive.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        for (int k = 0; k < 4; ++k)
            fx.spawn_living(FAMILY_ORC, 1, static_cast<short>(300 + 32 * k),
                            300);
        fx.world().ctf_requested_map_units[1] = kBoxOff;
        fx.world().ctf_requested_fill[1] = kKnobNone;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0101, fx.var(kSoccerSlots.mask))
            << "box off + NONE + no seat = nothing on the team";
        EXPECT_EQ(0, live_livings_on(fx.world(), 1));
    }
    // Box on + FILL NONE + no seat: active with the troops alone.
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        for (int k = 0; k < 4; ++k)
            fx.spawn_living(FAMILY_ORC, 1, static_cast<short>(300 + 32 * k),
                            300);
        fx.world().ctf_requested_fill[1] = kKnobNone;
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0b0111, fx.var(kSoccerSlots.mask))
            << "fielded map units keep the NONE team active";
        EXPECT_EQ(4, live_livings_on(fx.world(), 1));
        EXPECT_EQ(0, marked_bots_on(fx.world(), 1));
    }
}

// The default state is FAIR, not the old byte-identical AUTO — the old
// all-zero identity pins are replaced by these default pins (a no-human
// world still fields the legacy squads, now with the FAIR fact banked),
// and a fill code past the table degrades to FAIR byte for byte.
TEST_F(StagedRules, default_fill_pins_and_junk_degrades_to_fair)
{
    std::vector<std::uint8_t> default_bytes;
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        for (int team = 0; team < 2; ++team)
        {
            EXPECT_EQ(5, marked_bots_on(fx.world(), team));
            for (const int level : bot_levels_on(fx.world(), team))
                EXPECT_EQ(2, level)
                    << "no humans: the legacy formula level, team " << team;
            EXPECT_EQ(expected_fact(kKnobFair),
                      lineup_fact_code(fx.var(kSlotMatchedAnnounced), team))
                << "the applied FAIR fact is banked (the pane reads FAIR), "
                   "team " << team;
        }
        EXPECT_EQ(0, fx.var(kSlotMatchedPlan))
            << "the legacy arm stores no plan";
        default_bytes = og::sim::serialize_snapshot(
            og::sim::peek_keyframe_snapshot(fx.world()));
    }
    {
        ModesCtfWorld fx(kTdmLevelA);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 528, 96);
        fx.world().ctf_requested_fill[0] = 9;  // past the wheel
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        // The knob itself rides the snapshot (it is a replicated input),
        // so it is reset before the capture: everything else — entities,
        // mode vars, the lot — must be identical to the default stage.
        fx.world().ctf_requested_fill[0] = 0;
        EXPECT_EQ(default_bytes,
                  og::sim::serialize_snapshot(
                      og::sim::peek_keyframe_snapshot(fx.world())))
            << "a fill code past the table must degrade to FAIR byte for "
               "byte";
    }
}

// Basketball's hard shape (R2): allies fill only the room the court
// leaves, and a full court fields no squad and banks no fact.
TEST_F(StagedRules, basketball_allies_fill_the_room_left)
{
    // Three weak humans vs a stronger company: two allies complete the
    // five-on-court shape.
    {
        ModesCtfWorld fx(kBballLevelB);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 192, 96);
        int guy_id = 1;
        for (int k = 0; k < 3; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, guy_id++);
        for (int k = 0; k < 3; ++k)
            fx.spawn_leveled_hero(FAMILY_SOLDIER, 1,
                                  static_cast<short>(96 + 32 * k), 760,
                                  guy_id++, 5);

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(5, live_livings_on(fx.world(), 0))
            << "three humans + the room left = five on court";
        EXPECT_EQ(2, marked_bots_on(fx.world(), 0));
        EXPECT_EQ(3, live_livings_on(fx.world(), 1))
            << "the stronger company has no gap and no allies";
        EXPECT_EQ(expected_fact(kKnobFair),
                  lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
            << "two allies spawned: the fill is an applied fact";
    }
    // A full court: no room, no squad, no fact (R4).
    {
        ModesCtfWorld fx(kBballLevelB);
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 192, 96);
        int guy_id = 1;
        for (int k = 0; k < 5; ++k)
            fx.spawn_hero(FAMILY_SOLDIER, 0, static_cast<short>(96 + 32 * k),
                          700, guy_id++);
        for (int k = 0; k < 5; ++k)
            fx.spawn_leveled_hero(FAMILY_SOLDIER, 1,
                                  static_cast<short>(96 + 32 * k), 760,
                                  guy_id++, 5);

        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(5, live_livings_on(fx.world(), 0)) << "the court is full";
        EXPECT_EQ(0, marked_bots_on(fx.world(), 0));
        EXPECT_EQ(0, lineup_fact_code(fx.var(kSlotMatchedAnnounced), 0))
            << "0 spawned = no fact banked";
    }
}

// TEAMS MATCHED announces when a squad was solved (the R3 one-shot
// shape): a solved backfill announces exactly once, and a no-human world
// (legacy squads, nothing solved) stays silent.
TEST_F(StagedRules, teams_matched_announces_only_for_a_solved_squad)
{
    auto count_notifications = [](const og::sim::SimEventLog& log,
                                  const std::string& needle) {
        int count = 0;
        for (const auto& ev : log.events())
        {
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find(needle) != std::string::npos)
                ++count;
        }
        return count;
    };
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 700, 1);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(1, count_notifications(fx.events, "TEAMS MATCHED"))
            << "two solved backfills announce once (the latch)";
        EXPECT_NE(0, fx.var(kSlotMatchedAnnounced) % 10);
    }
    {
        ModesCtfWorld fx(kSoccerLevelB);
        for (int team = 0; team < 3; ++team)
            fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
        stage_init(fx);
        ASSERT_TRUE(fx.world().mode.active);
        EXPECT_EQ(0, count_notifications(fx.events, "TEAMS MATCHED"))
            << "legacy squads are not a solve";
        EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced) % 10);
    }
}

// The rule-level rows through the direct-Lua harness: NONE narrows, the
// box redirects a troops team to a squad row, allies ride the power gap,
// and the hard-shape cap clamps.
TEST_F(StagedRules, rule_rows_none_box_allies_and_cap)
{
    std::array<std::array<int, 4>, 4> teams{};
    for (int t = 0; t < 3; ++t)
        teams[static_cast<std::size_t>(t)][0] = 1;  // anchors author 0-2

    // NONE on backfilled team 1: row empties, mask narrows.
    {
        const RuleAnswer a = eval_rules(teams, 0b0111, 0, false,
                                        {0, kKnobNone, 0, 0});
        EXPECT_EQ(0b0111, a.mask) << "activation is NONE-blind";
        EXPECT_EQ(0b0101, a.lineup_mask) << "fills narrows the NONE team";
        EXPECT_EQ(0, (a.fills_packed / 100) % 100) << "empty row, count 0";
        EXPECT_EQ(303, a.squads_packed)
            << "teams 0 and 2 keep their FAIR squad rows (code 3, the "
               "applied FAIR itself since D1); the NONE team has none";
    }
    // The default: every backfilled row is the legacy bots squad (no
    // humans), squad code = the applied FAIR.
    {
        const RuleAnswer a = eval_rules(teams, 0b0111, 0, false);
        EXPECT_EQ(3 + 5 * 8, static_cast<int>(a.fills_packed % 100))
            << "bots fill, count 5";
        EXPECT_EQ(expected_fact(kKnobFair), a.squads_packed % 10)
            << "row.squad carries the banked-fact code";
    }
    // With a human roster the backfill solves: matched fill, headcount 2.
    {
        std::array<std::array<int, 4>, 4> manned = teams;
        manned[0][1] = 2;
        const RuleAnswer a = eval_rules(manned, 0b0111, 0, false, {}, 0, {},
                                        {200, 0, 0, 0});
        ASSERT_TRUE(a.matched);
        EXPECT_EQ(2, a.matched_size);
        EXPECT_EQ(4 + 2 * 8, static_cast<int>((a.fills_packed / 100) % 100))
            << "matched fill, truncated to the headcount";
    }
    // Troops beside the box: on = a troops row and NO squad; off = the
    // troops leave and the squad row takes over.
    {
        std::array<std::array<int, 4>, 4> npc_teams = teams;
        npc_teams[1][2] = 4;
        const RuleAnswer on = eval_rules(npc_teams, 0b0111, 0, false);
        EXPECT_EQ(2 + 4 * 8, static_cast<int>((on.fills_packed / 100) % 100))
            << "fielded map units are the fill";
        EXPECT_EQ(0, (on.squads_packed / 10) % 10) << "no squad beside them";
        const RuleAnswer off = eval_rules(npc_teams, 0b0111, 0, false, {}, 0,
                                          {0, kBoxOff, 0, 0});
        EXPECT_EQ(3 + 5 * 8,
                  static_cast<int>((off.fills_packed / 100) % 100))
            << "the box off trades the troops for the squad";
    }
    // Allies: the weaker company's row counts roster + the solved squad;
    // the stronger one counts the roster alone.
    {
        std::array<std::array<int, 4>, 4> two = teams;
        two[0][1] = 2;
        two[2][1] = 3;
        const RuleAnswer a = eval_rules(two, 0b0111, 0, false, {}, 0, {},
                                        {200, 0, 900, 0});
        EXPECT_EQ(2, a.matched_size);
        EXPECT_EQ(1 + 4 * 8, static_cast<int>(a.fills_packed % 100))
            << "company 2 + 2 allies (headcount rule)";
        EXPECT_EQ(expected_fact(kKnobFair), a.squads_packed % 10);
        EXPECT_EQ(1 + 3 * 8,
                  static_cast<int>((a.fills_packed / 10000) % 100))
            << "the stronger company rides alone";
        EXPECT_EQ(0, (a.squads_packed / 100) % 10);
    }
    // The hard-shape cap clamps the squad rows (basketball's mechanism,
    // cap 3 so the clamp is visible against the squad of 5).
    {
        const RuleAnswer a = eval_rules(teams, 0b0111, 0, false, {}, 3);
        EXPECT_EQ(3 + 3 * 8, static_cast<int>(a.fills_packed % 100))
            << "count clamps to the cap";
    }
    // Onslaught's no_bots outranks every knob (D17): no squad row forms,
    // and a troops team whose box is off leaves the mask outright.
    {
        std::array<std::array<int, 4>, 4> gen_teams = teams;
        for (int t = 0; t < 3; ++t)
            gen_teams[static_cast<std::size_t>(t)][3] = 1;
        gen_teams[1][2] = 1;
        const RuleAnswer a = eval_rules(gen_teams, 0b0111, 3, true, {}, 0,
                                        {0, kBoxOff, 0, 0});
        EXPECT_EQ(0b0101, a.lineup_mask)
            << "box off under no_bots drops the team";
        EXPECT_EQ(0, a.squads_packed);
        EXPECT_EQ(5 + 1 * 8, static_cast<int>(a.fills_packed % 100))
            << "the surviving teams keep their generators rows";
    }
}

// ===========================================================================
// 2c. The band modes (FFA/mutant): the decide fold refuses before any
//     world write (R1), NONE keeps the deployed band, and the fill
//     singles solve against the weakest fighter (B3's band spelling).
// ===========================================================================

namespace {

// The shipped FFA row (mode_levels.lua [850], fighters = 8): the fixture
// manifest carries mutant rows but no FFA row, so the band's FFA arm
// stages the real one. Both band modes keep their fighter count in slot 8.
constexpr int kFfaLevelA = 850;
constexpr int kBandSlotFighterCount = 8;

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

// The band modes' one-fighter shape under FILL: NONE, staged the way
// MatchStage stages it: the refusal must be decided BEFORE the world is
// touched — the hero keeps its seat team, the markers survive, the
// authored cast is not stripped — because the kept post-refusal world IS
// the world GO adopts under classic rules (the team modes' discipline; a
// refused init never trips the LobbyServer start gate, which denies
// StageFailed alone). The one write a refusal makes is the reason digit
// in the shared facts slot (REFUSAL_BASE), which the staged report
// renders as FEWER THAN 2 FIGHTERS.
void expect_band_refuses_untouched(int level_id, const char* reason)
{
    ModesCtfWorld fx(level_id);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    walker* const hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* const troop = fx.spawn_living(FAMILY_ORC, 1, 300, 300);
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(troop, nullptr);
    fx.world().ctf_requested_fill[0] = kKnobNone;

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
void expect_band_none_with_two_heroes_plays(int level_id)
{
    ModesCtfWorld fx(level_id);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    walker* const a = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* const b = fx.spawn_hero(FAMILY_SOLDIER, 1, 232, 200, 2);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    fx.world().ctf_requested_fill[0] = kKnobNone;

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

// The band fill for one wheel value: FFA 850 (target 8) with two deployed
// heroes at the given levels; answers the filled bots' levels (they are
// the livings without a guy record).
std::vector<int> band_bot_levels(int fill, int hero_level_a,
                                 int hero_level_b)
{
    ModesCtfWorld fx(kFfaLevelA);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team, static_cast<short>(96 + 96 * team), 96);
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 0, 200, 200, 1,
                          static_cast<short>(hero_level_a));
    fx.spawn_leveled_hero(FAMILY_SOLDIER, 1, 232, 200, 2,
                          static_cast<short>(hero_level_b));
    fx.world().ctf_requested_fill[0] = static_cast<short>(fill);
    og::sim::mode_stage_init(fx.world());
    EXPECT_TRUE(fx.world().mode.active);
    std::vector<int> levels;
    if (!fx.world().mode.active)
        return levels;
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            continue;
        if (w->stats() != nullptr)
            levels.push_back(w->stats()->level());
    }
    return levels;
}

}  // namespace

// R1: FILL: NONE with a single deployed hero is a legal knob shape, so it
// must come back as the staged refusal (mode inactive, the mode's own
// reason, world untouched, the reason digit banked), never as an error
// thrown from a half-applied init.
TEST_F(StagedRules, band_none_with_one_fighter_refuses_untouched)
{
    {
        SCOPED_TRACE("ffa");
        expect_band_refuses_untouched(kFfaLevelA,
                                      "ffa: fewer than two fighters");
    }
    {
        SCOPED_TRACE("mutant");
        expect_band_refuses_untouched(kMutantLevelA,
                                      "mutant: fewer than two fighters");
    }
}

TEST_F(StagedRules, band_none_with_two_fighters_plays)
{
    {
        SCOPED_TRACE("ffa");
        expect_band_none_with_two_heroes_plays(kFfaLevelA);
    }
    {
        SCOPED_TRACE("mutant");
        expect_band_none_with_two_heroes_plays(kMutantLevelA);
    }
}

// The band fill solves its singles against the weakest fighter times the
// wheel (B3's band spelling): every filled single lands the same solved
// level, the wheel moves it monotonically, and the reference is the
// WEAKER of two unequal fighters.
TEST_F(StagedRules, band_fill_singles_solve_against_the_weakest_fighter)
{
    std::vector<int> weak = band_bot_levels(kKnobWeak, 5, 5);
    std::vector<int> fair = band_bot_levels(kKnobFair, 5, 5);
    std::vector<int> brutal = band_bot_levels(kKnobBrutal, 5, 5);
    ASSERT_EQ(6u, fair.size()) << "target 8 - 2 deployed = 6 singles";
    ASSERT_EQ(6u, weak.size());
    ASSERT_EQ(6u, brutal.size());
    // Every single solves the SAME target on its own measured base, so
    // levels differ per family but never per slot: sorted, the wheel
    // moves each family's solve monotonically.
    std::sort(weak.begin(), weak.end());
    std::sort(fair.begin(), fair.end());
    std::sort(brutal.begin(), brutal.end());
    for (std::size_t i = 0; i < 6u; ++i)
    {
        EXPECT_LE(weak[i], fair[i]) << "slot " << i;
        EXPECT_LE(fair[i], brutal[i]) << "slot " << i;
    }
    EXPECT_LT(weak[0] + weak[5], brutal[0] + brutal[5])
        << "the multipliers must separate the ends";
    // Unequal fighters: the weaker one is the reference — the L5/L9 band
    // solves exactly like the L5/L5 band.
    std::vector<int> uneven = band_bot_levels(kKnobFair, 5, 9);
    ASSERT_EQ(6u, uneven.size());
    std::sort(uneven.begin(), uneven.end());
    EXPECT_EQ(fair, uneven)
        << "the reference is the weakest fighter, not the mean";
}

// ===========================================================================
// 3. The apply-executes-decision matrix: the shared rules (via the harness)
//    produce the expected values; the staged world must bank and field
//    exactly those.
// ===========================================================================

namespace {

struct MatrixMode
{
    const char* name;
    int level_id;
    ModeSlots slots;
    int auto_default;   // manifest row.teams (0 = the CTF/TDM raw arm)
    bool no_bots;
};

// The shared matrix world: the mode's authored domain on teams 0-2, one
// guy-less npc on team 1, and `roster` L1 heroes per team (roster teams
// are a subset of {0, 2}, so the authored domain is 0b0111 for every mode
// — the construction knowledge the old matrix pinned via the plan's
// fold).
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

// The matrix dimensions are the two per-team knobs: `none_team` wears
// FILL: NONE, `box_team` has its MAP UNITS box off (-1 = neither). The
// harness powers are placeholders shaped like the roster (100 per L1
// soldier): the decision reads power only through comparisons, so the
// shape is what matters, and the staged world's real f-sums compare
// identically.
void run_staged_case(const MatrixMode& mode, int flag_family, int none_team,
                     int box_team, const std::array<int, 4>& roster)
{
    SCOPED_TRACE(::testing::Message()
                 << mode.name << " none=" << none_team << " box="
                 << box_team << " roster=" << roster[0] << roster[1]
                 << roster[2] << roster[3]);
    std::array<std::array<int, 4>, 4> teams{};
    std::array<int, 4> power{};
    for (int t = 0; t < 3; ++t)
    {
        teams[static_cast<std::size_t>(t)][0] = 1;
        if (std::string(mode.name) == "onslaught")
            teams[static_cast<std::size_t>(t)][3] = 1;  // the foundry
    }
    teams[1][2] = 1;  // the guy-less npc on team 1
    for (int t = 0; t < 4; ++t)
    {
        teams[static_cast<std::size_t>(t)][1] =
            roster[static_cast<std::size_t>(t)];
        power[static_cast<std::size_t>(t)] =
            roster[static_cast<std::size_t>(t)] * 100;
    }
    std::array<int, 4> fill{};
    if (none_team >= 0)
        fill[static_cast<std::size_t>(none_team)] = kKnobNone;
    std::array<int, 4> map_units{};
    if (box_team >= 0)
        map_units[static_cast<std::size_t>(box_team)] = kBoxOff;

    const RuleAnswer expected =
        eval_rules(teams, 0b0111, mode.auto_default, mode.no_bots, fill, 0,
                   map_units, power);
    ASSERT_GE(expected.mask, 0);
    ASSERT_GE(expected.fills_packed, 0);
    const int expected_mask = expected.lineup_mask;
    const bool expected_starts =
        expected.starts &&
        std::popcount(static_cast<unsigned>(expected.lineup_mask)) >= 2;
    const bool expected_matched = expected.matched;
    const int expected_size = expected.matched_size;
    const std::int64_t fills_packed = expected.fills_packed;

    ModesCtfWorld fx(mode.level_id);
    if (none_team >= 0)
        fx.world().ctf_requested_fill[static_cast<std::size_t>(none_team)] =
            static_cast<short>(kKnobNone);
    if (box_team >= 0)
        fx.world()
            .ctf_requested_map_units[static_cast<std::size_t>(box_team)] =
            static_cast<short>(kBoxOff);
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
        << "the apply must bank the rule's narrowed mask";
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
        case 1:  // company (roster + allies); team 1's npc stands too
                 // while its box is on
            expected_livings = fill_count;
            if (team == 1 && box_team != 1)
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
            (fill_code == 3 || fill_code == 4)
                ? fill_count
                : (fill_code == 1 ? fill_count -
                                        roster[static_cast<std::size_t>(
                                            team)]
                                  : 0);
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

// The knob dimensions in every load-bearing position: no knob, NONE on
// the npc team and on the backfill team, the box off on the npc team and
// the backfill team, and the two-roster shape (unequal companies — the
// allies arm) with and without the knobs.
void run_staged_matrix(const MatrixMode& mode, int flag_family)
{
    const std::array<int, 4> none{0, 0, 0, 0};
    const std::array<int, 4> solo{2, 0, 0, 0};
    const std::array<int, 4> two{2, 0, 1, 0};
    for (int none_team : {-1, 1, 2})
        run_staged_case(mode, flag_family, none_team, -1, solo);
    for (int box_team : {1, 2})
        run_staged_case(mode, flag_family, -1, box_team, solo);
    run_staged_case(mode, flag_family, -1, -1, none);
    run_staged_case(mode, flag_family, -1, -1, two);
    run_staged_case(mode, flag_family, 1, 1, two);
    run_staged_case(mode, flag_family, 2, -1, two);
}

}  // namespace

TEST_F(StagedRules, staged_world_matrix_soccer)
{
    run_staged_matrix({"soccer", kSoccerLevelB, kSoccerSlots, 4, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_basketball)
{
    run_staged_matrix({"basketball", kBballLevelB, kBballSlots, 4, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_ctf)
{
    run_staged_matrix({"ctf", kCtfLevelA, kCtfSlots, 0, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_tdm)
{
    run_staged_matrix({"tdm", kTdmLevelA, kTdmSlots, 0, false},
                      flag_family_);
}

TEST_F(StagedRules, staged_world_matrix_onslaught)
{
    run_staged_matrix({"onslaught", kOnsLevelB, kOnsSlots, 3, true},
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

// ---------------------------------------------------------------------------
// The classic lineup stage (docs/lineup-design.md Amendment 3, C2-C4): the
// packs/core wildcard on_lineup_stage — the REAL shipped hook, not a probe —
// applying the per-team MAP UNITS strip and FILL squads on mode-less levels.
// The C3 pin runs it against a staged REAL gladiator level; the classic
// rules (traded units, ships-empty teams, NONE, the no-humans legacy arm,
// placement determinism) run on synthetic classic worlds through the tick-1
// lazy arm — the exact dispatch solo play uses.
// ---------------------------------------------------------------------------

namespace {

loader& classic_lineup_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_classic_world(GameWorld& w)
{
    loader* game_loader = &classic_lineup_loader();
    w.entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    w.entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                         entity.family());
    };
    w.entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

// A mode-less world: the default TestGameWorld type (no TYPE_SCRIPTED), so
// GameWorld::tick's classic branch runs the lazy lineup-stage arm at tick 1.
struct ClassicWorld : TestGameWorld
{
    explicit ClassicWorld(int level_id = 9800) : TestGameWorld(level_id)
    {
        wire_classic_world(world());
    }

    walker* spawn_npc(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_SIT);
        return w;
    }

    walker* spawn_hero(int family, int team, int x, int y, int guy_id)
    {
        walker* w = spawn_npc(family, team, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->myguy->id = guy_id;
        return w;
    }

    walker* spawn_marker(int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }
};

// Live authored (guy-less) units on a team: livings + generators, the
// population the MAP UNITS box governs.
int authored_units_on(const GameWorld& world, int team)
{
    int count = 0;
    for (const auto& entry : world.oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() || w->myguy != nullptr)
            continue;
        if (w->query_order() != Order::Living &&
            w->query_order() != Order::Generator)
            continue;
        if (w->team_num() == static_cast<unsigned char>(team))
            ++count;
    }
    return count;
}

og::sim::LobbyCharacterSlot classic_slot(std::uint8_t slot_index,
                                         std::int32_t guy_id,
                                         const char* name,
                                         std::int16_t team)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = guy_id;
    character.name = name;
    character.family = FAMILY_SOLDIER;
    character.strength = 10;
    character.dexterity = 11;
    character.constitution = 12;
    character.intelligence = 13;
    character.armor = 14;
    character.level = 3;
    character.teamnum = team;
    return {
        .slot_index = slot_index,
        .character = character,
    };
}

og::server::MatchStageInputs classic_gladiator_inputs(std::uint32_t seed)
{
    og::server::MatchStageInputs inputs;
    inputs.equivalent.current_campaign = "gladiator";
    inputs.equivalent.scen_num = 1;
    inputs.equivalent.numplayers = 1;
    inputs.equivalent.allied_mode = 0;
    inputs.equivalent.team_list = {classic_slot(0u, 100, "Host", 0)};
    inputs.difficulty = 1;
    inputs.match_seed = seed;
    return inputs;
}

class ClassicLineupTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        restore_default_settings();
        // The mount registers every shipped pack chunk — packs/core's
        // scripts/lineup_stage.lua included, which is the hook under test.
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"));
    }
};

}  // namespace

// C3, pinned on a REAL staged gladiator level with the REAL shipped hook:
// all-default (FILL: FAIR, MAP UNITS on, everywhere) the stage step
// dispatches — run_lineup_stage_step answers true, so this is not a
// vacuous pass — and writes nothing: no mode var, no RNG draw, not one
// replicated byte.
TEST_F(ClassicLineupTest, all_default_stage_is_a_byte_noop_on_gladiator)
{
    og::server::MatchStage stage({.networked = false});
    stage.observe_inputs(classic_gladiator_inputs(11u), /*now_ms=*/0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const w = stage.world();
    ASSERT_NE(nullptr, w);
    ASSERT_FALSE(w->mode.active);

    const std::vector<std::uint8_t> before =
        og::sim::serialize_snapshot(og::sim::peek_keyframe_snapshot(*w));
    const auto rng_before = w->rng_.state_;
    ASSERT_TRUE(w->run_lineup_stage_step())
        << "the shipped packs/core wildcard hook must dispatch";
    EXPECT_EQ(rng_before, w->rng_.state_) << "all-default draws no RNG";
    EXPECT_EQ(before, og::sim::serialize_snapshot(
                          og::sim::peek_keyframe_snapshot(*w)))
        << "all-default writes nothing, spawns nothing (C3)";
    for (int slot = 0; slot < og::sim::kModeVarCount; ++slot)
        ASSERT_EQ(0, w->mode.vars[static_cast<std::size_t>(slot)])
            << "mode var " << slot;
    EXPECT_TRUE(w->scripts().host().errors().empty());
}

// The per-team strip on the real gladiator opener: turn one authored
// team's MAP UNITS box off with FILL: NONE beside it and the staged world
// fields none of that team's troops — the census of the staged world shows
// the emptied side, every other team keeps its authored cast, and nothing
// refuses (C4).
TEST_F(ClassicLineupTest, gladiator_map_units_off_strips_one_team)
{
    // First read the authored shape from an all-default stage.
    std::array<int, 4> authored = {};
    int target_team = -1;
    {
        og::server::MatchStage stage({.networked = false});
        stage.observe_inputs(classic_gladiator_inputs(11u), 0);
        ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
        GameWorld* const w = stage.world();
        ASSERT_NE(nullptr, w);
        for (int team = 0; team < 4; ++team)
        {
            authored[static_cast<std::size_t>(team)] =
                authored_units_on(*w, team);
            if (target_team < 0 &&
                authored[static_cast<std::size_t>(team)] > 0)
                target_team = team;
        }
    }
    ASSERT_GE(target_team, 0) << "gladiator/1 must author troops somewhere";

    og::server::MatchStageInputs inputs = classic_gladiator_inputs(11u);
    inputs.equivalent.map_units[static_cast<std::size_t>(target_team)] =
        og::sim::kMapUnitsOff;
    inputs.equivalent.fill[static_cast<std::size_t>(target_team)] =
        og::sim::kFillNone;
    og::server::MatchStage stage({.networked = false});
    stage.observe_inputs(inputs, 0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const w = stage.world();
    ASSERT_NE(nullptr, w);
    EXPECT_EQ(0, authored_units_on(*w, target_team))
        << "the stripped team fields no map units";
    for (int team = 0; team < 4; ++team)
    {
        if (team == target_team)
            continue;
        EXPECT_EQ(authored[static_cast<std::size_t>(team)],
                  authored_units_on(*w, team))
            << "team " << team << " keeps its authored cast";
    }
    EXPECT_TRUE(w->scripts().host().errors().empty())
        << "classic levels never refuse (C4)";
    EXPECT_EQ(0, w->mode.vars[4] / 1000000000)
        << "no refusal digit is ever banked on a classic level";
    // C8 on the real map: this stage is TOUCHED (the box), so every team
    // the level does not author — no units, and gladiator authors no
    // markers off team 0 (W6-C) — banks its resolved NONE, while the
    // authored teams' defaults keep resolving FAIR and bank nothing
    // (nothing spawned on them). The explicit NONE on the stripped team
    // stays unbanked: explicit wheel values are untouched.
    for (int team = 0; team < 4; ++team)
    {
        const int code = lineup_fact_code(w->mode.vars[4], team);
        if (team == target_team ||
            authored[static_cast<std::size_t>(team)] > 0 || team == 0)
        {
            EXPECT_EQ(0, code) << "team " << team << " banks nothing";
        }
        else
        {
            EXPECT_EQ(expected_fact(og::sim::kFillNone), code)
                << "team " << team << " banks its resolved NONE";
        }
    }
}

// Trading authored units for a squad: MAP UNITS off with the wheel left at
// FAIR replaces the team's authored cast with a solved five-bot squad near
// the retired units' centroid, banks the applied FAIR fact and stores the
// plan — through the tick-1 lazy arm, the un-staged worlds' path.
TEST_F(ClassicLineupTest, traded_units_become_a_fair_squad_at_their_centroid)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    fx.spawn_npc(FAMILY_SOLDIER, 1, 384, 576);
    fx.spawn_npc(FAMILY_SOLDIER, 1, 416, 576);
    fx.spawn_npc(FAMILY_SOLDIER, 1, 400, 608);
    fx.world().ctf_requested_map_units[1] = og::sim::kMapUnitsOff;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "a FAIR squad replaces the traded units";
    EXPECT_EQ(5, authored_units_on(fx.world(), 1))
        << "and nothing else stands on the team";
    // Team 1 banks the applied FAIR fact (code 3, digit pair *1000); teams
    // 2 and 3 stand on nothing, so their stored default resolves NONE and
    // banks it (C8, code 1 at *100000 and *10000000).
    EXPECT_EQ(3000 + 100000 + 10000000, fx.world().mode.vars[4])
        << "applied FAIR on team 1, resolved NONE on the unauthored teams";
    EXPECT_NE(0, fx.world().mode.vars[3]) << "the solved plan is stored";
    // Placement: the centroid of the three retired npcs grid-snaps to
    // (400, 576) -> (400, 576); every member sits on the anchor tile or
    // inside the 3-tile ring walk around it.
    int near = 0;
    for (const auto& entry : fx.world().oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() ||
            w->query_order() != Order::Living || w->myguy != nullptr)
            continue;
        if (w->team_num() != 1)
            continue;
        if (std::abs(w->xpos() - 400) <= 48 &&
            std::abs(w->ypos() - 576) <= 48)
            ++near;
    }
    EXPECT_EQ(5, near) << "members place at the centroid + ring discipline";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// FILL: STRONG on a ships-empty authored team (a start marker, no units) of
// a classic multi-team level spawns a solved squad there — ordinary
// walkers: no owner, no guy, counted by the completion scan like any
// authored enemy, so the level stays open while they stand (C4).
TEST_F(ClassicLineupTest, fill_strong_spawns_a_solved_squad_on_a_marker_team)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    fx.spawn_marker(1, 320, 480);
    fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1)) << "the squad walked on";
    // STRONG (code 4) banks on team 1; the unauthored teams 2/3 bank
    // their resolved NONE (C8, code 1). Team 0 holds the hero, so its
    // default resolves FAIR and banks nothing (nothing spawned there).
    EXPECT_EQ(4000 + 100000 + 10000000, fx.world().mode.vars[4])
        << "the applied STRONG fact banks in team 1's digit pair";
    EXPECT_NE(0, fx.world().mode.vars[3]) << "the solve stored a plan";
    for (const auto& entry : fx.world().oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() ||
            w->query_order() != Order::Living || w->myguy != nullptr)
            continue;
        if (w->team_num() != 1)
            continue;
        EXPECT_EQ(nullptr, w->owner()) << "ordinary walkers: nobody owns them";
        EXPECT_LE(std::abs(w->xpos() - 320), 48) << "placed at the marker";
        EXPECT_LE(std::abs(w->ypos() - 480), 48) << "placed at the marker";
    }
    EXPECT_FALSE(fx.world().game_ended)
        << "the squad holds the level open on its own tick";
    fx.world().tick();
    EXPECT_FALSE(fx.world().game_ended)
        << "remaining-foes counting sees the squad (C4)";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// The stored DEFAULT on a ships-empty authored team is the map's own
// value: it resolves FAIR (the marker is presence) but fields nothing and
// banks nothing — since D1 only an EXPLICIT wheel value turns a
// marker-only team on. Team 2 wheels STRONG so the stage genuinely runs
// its per-team pass (an all-default world returns on the fast path before
// the rule is even consulted): team 1's guard is evaluated and holds,
// team 2 fills.
TEST_F(ClassicLineupTest, fair_keeps_a_ships_empty_team_empty)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    fx.spawn_marker(1, 320, 480);
    fx.spawn_marker(2, 480, 160);
    fx.world().ctf_requested_fill[2] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(0, marked_bots_on(fx.world(), 1))
        << "FAIR on a ships-empty team spawns nothing";
    EXPECT_EQ(5, marked_bots_on(fx.world(), 2))
        << "the explicit wheel beside it does";
    EXPECT_EQ(0, fx.world().mode.vars[4] / 1000 % 100)
        << "team 1 banks nothing";
    EXPECT_EQ(4, fx.world().mode.vars[4] / 100000 % 100)
        << "team 2 banks the applied STRONG fact";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// Every anchor cell blocked — the marker tile and the whole 3-tile ring
// ball around it — falls through to the blessed teleport draw, and the
// squad still walks on somewhere legal.
TEST_F(ClassicLineupTest, blocked_anchor_falls_back_to_the_teleport_draw)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    fx.spawn_marker(1, 320, 480);
    for (int dx = -3; dx <= 3; ++dx)
    {
        for (int dy = -3; dy <= 3; ++dy)
        {
            if (std::abs(dx) + std::abs(dy) > 3)
                continue;
            // Wildlife (team 4): blocks the cells without joining any
            // score team's census.
            fx.spawn_npc(FAMILY_SOLDIER, 4, 320 + dx * 16, 480 + dy * 16);
        }
    }
    fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "the teleport fallback still fields the squad";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// NONE plus an emptied side = fewer enemies and the campaign's own win
// logic governs (C4): the kill-all completion fires once the stripped world
// is re-scanned, and no refusal of any shape is raised or banked.
TEST_F(ClassicLineupTest, none_fields_fewer_enemies_and_the_level_completes)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    fx.spawn_npc(FAMILY_SOLDIER, 1, 384, 576);
    fx.spawn_npc(FAMILY_SOLDIER, 1, 416, 576);
    fx.world().ctf_requested_map_units[1] = og::sim::kMapUnitsOff;
    fx.world().ctf_requested_fill[1] = og::sim::kFillNone;
    fx.world().tick();

    EXPECT_EQ(0, authored_units_on(fx.world(), 1)) << "fewer enemies";
    EXPECT_EQ(0, marked_bots_on(fx.world(), 1)) << "and no squad";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty())
        << "no refusal, ever, on a classic level";
    // The EXPLICIT NONE on team 1 banks nothing (explicit wheel values are
    // untouched by C8); only the unauthored teams' RESOLVED default banks.
    EXPECT_EQ(100000 + 10000000, fx.world().mode.vars[4])
        << "explicit NONE unbanked; resolved NONE banked on teams 2/3";
    fx.world().tick();
    fx.world().tick();
    EXPECT_TRUE(fx.world().game_ended)
        << "the campaign's own win logic governs the emptied board";
}

// No humans anywhere: the legacy difficulty formula levels the squad and
// stores no plan (B3's own words) — the discriminator between the solved
// and legacy arms.
TEST_F(ClassicLineupTest, no_humans_takes_the_legacy_formula_and_no_plan)
{
    ClassicWorld fx;
    fx.spawn_marker(1, 320, 480);
    fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1));
    EXPECT_EQ(0, fx.world().mode.vars[3]) << "the legacy arm stores no plan";
    // STRONG (code 4) banks on team 1 (R4); this fixture fields NO hero,
    // so team 0 is as bare as 2/3 and all three bank the resolved NONE
    // (C8, code 1): +10 for team 0's digit pair beside the unauthored
    // sides'.
    EXPECT_EQ(10 + 4000 + 100000 + 10000000, fx.world().mode.vars[4])
        << "the applied fact still banks for what spawned (R4)";
    std::int32_t first_level = -1;
    for (const auto& entry : fx.world().oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() ||
            w->query_order() != Order::Living || w->myguy != nullptr)
            continue;
        if (w->team_num() != 1 || w->stats() == nullptr)
            continue;
        if (first_level < 0)
            first_level = w->stats()->level();
        EXPECT_EQ(first_level, w->stats()->level())
            << "one legacy level for the whole squad";
    }
    EXPECT_GE(first_level, 1);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// C8, the per-team resolution on one touched classic stage: a deployed
// fighter flips an otherwise unauthored team's default back to FAIR (it
// banks nothing — nothing spawned there), an authored ships-empty team's
// explicit wheel banks what it applied, and ONLY the teams with nothing at
// all bank the resolved NONE. The all-default arm never reaches any of
// this (all_default_stage_is_a_byte_noop_on_gladiator pins the fast path).
TEST_F(ClassicLineupTest, default_resolution_follows_presence_per_team)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);   // roster presence
    fx.spawn_marker(1, 320, 480);                    // authored presence
    fx.spawn_hero(FAMILY_SOLDIER, 2, 128, 96, 101);  // fighter on a team
                                                     // the map ships nothing
    // The touch that wakes the stage without touching any default: an
    // explicit wheel value on the authored ships-empty team.
    fx.world().ctf_requested_fill[1] = og::sim::kFillWeak;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "the explicit WEAK squad walks onto the marker team";
    EXPECT_EQ(0, lineup_fact_code(fx.world().mode.vars[4], 0))
        << "team 0's default resolves FAIR (roster) and banks nothing";
    EXPECT_EQ(expected_fact(og::sim::kFillWeak),
              lineup_fact_code(fx.world().mode.vars[4], 1))
        << "the explicit wheel banks what it applied";
    EXPECT_EQ(0, lineup_fact_code(fx.world().mode.vars[4], 2))
        << "a deployed fighter flips the unauthored team back to FAIR";
    EXPECT_EQ(expected_fact(og::sim::kFillNone),
              lineup_fact_code(fx.world().mode.vars[4], 3))
        << "the team with nothing banks its resolved NONE";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// C8, the C++ census half: census_lineup_presence gathers exactly the
// columns the resolver reads — live authored units, live generators, the
// roster, and markers with DEAD ones included (the anchor scan's own
// population, which is why the query's anchors column stays 0).
TEST_F(ClassicLineupTest, presence_census_counts_every_column)
{
    ClassicWorld fx;
    fx.spawn_npc(FAMILY_SOLDIER, 0, 96, 96);
    fx.spawn_hero(FAMILY_SOLDIER, 1, 128, 96, 100);
    walker* const marker = fx.spawn_marker(2, 320, 480);
    ASSERT_NE(nullptr, marker);
    marker->set_dead(1);  // a consumed marker still counts
    walker* const gen = fx.world().add_ob(Order::Generator, 0);
    ASSERT_NE(nullptr, gen);
    gen->set_team_num(3);

    const std::array<og::ui::LineupTeamPresence, 4> counts =
        og::ui::census_lineup_presence(fx.world());
    EXPECT_EQ(1, counts[0].units);
    EXPECT_EQ(0, counts[0].roster);
    EXPECT_EQ(1, counts[1].roster);
    EXPECT_EQ(0, counts[1].units) << "a has_guy living is roster, not unit";
    EXPECT_EQ(1, counts[2].markers) << "dead markers are presence (C8)";
    EXPECT_EQ(0, counts[2].units);
    EXPECT_EQ(1, counts[3].generators);
}

// Same seed, same cells: the classic placement rule (anchor tile, ring
// walk, teleport fallback) is deterministic — two identical worlds field
// their squads on identical coordinates.
TEST_F(ClassicLineupTest, placement_is_deterministic_for_a_seed)
{
    auto run_world = [] {
        ClassicWorld fx;
        fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
        fx.spawn_marker(1, 320, 480);
        fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
        fx.world().rng_.state_ = 12345u;
        fx.world().tick();
        std::vector<std::pair<int, int>> cells;
        for (const auto& entry : fx.world().oblist)
        {
            const walker* const w = entry.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->myguy != nullptr)
                continue;
            if (w->team_num() != 1)
                continue;
            cells.emplace_back(w->xpos(), w->ypos());
        }
        return cells;
    };
    const auto first = run_world();
    const auto second = run_world();
    ASSERT_EQ(5u, first.size());
    EXPECT_EQ(first, second) << "same seed, same cells";
}

// ===========================================================================
// 4b. The D-series outcomes (D2/D3/D4): an explicit wheel value always
//     fields — on unauthored ground and beside standing troops — while
//     the stored default never adds a squad beside anything. Pinned on
//     REAL staged gladiator worlds through the same MatchStage the
//     launch adopts.
// ===========================================================================

namespace
{

// C++ twin of the lib's walker_power under the §4.1 trunc-on-read
// discipline (static_cast truncates the positive float stats exactly
// like og.trunc) — the independent oracle test_modes_tdm.cpp carries,
// re-spelled so the D3 target check does not mirror the Lua it measures.
long long classic_walker_f(const walker* w)
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

long long bot_f_sum_on(GameWorld& world, int team)
{
    long long sum = 0;
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
            sum += classic_walker_f(w);
    }
    return sum;
}

// The weakest-human reference, measured off the staged world itself: the
// f of the deployed hero (gladiator seats one).
long long deployed_hero_f(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            return classic_walker_f(w);
    }
    return 0;
}

std::vector<std::pair<int, int>> bot_cells_on(GameWorld& world, int team)
{
    std::vector<std::pair<int, int>> cells;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->team_num() != static_cast<unsigned char>(team) ||
            w->myguy != nullptr)
            continue;
        cells.emplace_back(static_cast<int>(w->xpos()),
                           static_cast<int>(w->ypos()));
    }
    return cells;
}

// D2's spawn-safety oracle: is this walker's tile the tile of, or one of
// the eight neighbours of, any live hostile (another team's living or
// generator, wildlife included)?
bool adjacent_to_hostile(GameWorld& world, const walker* bot)
{
    const int bx = (static_cast<int>(bot->xpos()) / 16) * 16;
    const int by = (static_cast<int>(bot->ypos()) / 16) * 16;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w == bot)
            continue;
        if (w->query_order() != Order::Living &&
            w->query_order() != Order::Generator)
            continue;
        if (w->team_num() == bot->team_num())
            continue;
        const int hx = (static_cast<int>(w->xpos()) / 16) * 16;
        const int hy = (static_cast<int>(w->ypos()) / 16) * 16;
        if (std::abs(bx - hx) <= 16 && std::abs(by - hy) <= 16)
            return true;
    }
    return false;
}

// gladiator scen 1 with the probe fixture's hero: a level-50 thief slot,
// every trained stat 60, armor 40 — the high-level roster D4's ladder
// demands (a level-3 soldier floors the solver and hides the wheel).
og::server::MatchStageInputs classic_gladiator_hero50_inputs(
    std::uint32_t seed)
{
    og::server::MatchStageInputs inputs = classic_gladiator_inputs(seed);
    og::sim::LobbyCharacterData character;
    character.guy_id = 100;
    character.name = "Host";
    character.family = FAMILY_THIEF;
    character.strength = 60;
    character.dexterity = 60;
    character.constitution = 60;
    character.intelligence = 60;
    character.armor = 40;
    character.level = 50;
    character.teamnum = 0;
    inputs.equivalent.team_list = {og::sim::LobbyCharacterSlot{
        .slot_index = 0u,
        .character = character,
    }};
    return inputs;
}

}  // namespace

// D2: an explicit FAIR on a team gladiator does not author fields a full
// five-bot squad and turns the team on — ordinary hostile walkers on a
// map with no site for them. Placement is the site-less rule: chosen
// deterministically from the match seed (two identical stagings land the
// identical cells) and never adjacent to a hostile at spawn.
TEST_F(ClassicLineupTest, explicit_fair_fields_a_safe_squad_on_unauthored_ground)
{
    auto stage_and_check = [](std::vector<std::pair<int, int>>& cells) {
        og::server::MatchStageInputs inputs =
            classic_gladiator_hero50_inputs(11u);
        inputs.equivalent.fill[2] = og::sim::kFillFair;
        og::server::MatchStage stage({.networked = false});
        stage.observe_inputs(inputs, 0);
        ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
        GameWorld* const w = stage.world();
        ASSERT_NE(nullptr, w);
        EXPECT_EQ(5, marked_bots_on(*w, 2))
            << "explicit FAIR fields on unauthored ground (D2)";
        for (const auto& uptr : w->oblist)
        {
            const walker* bot = uptr.get();
            if (bot == nullptr || bot->dead() ||
                bot->query_order() != Order::Living ||
                bot->team_num() != 2 || bot->myguy != nullptr)
                continue;
            EXPECT_EQ(nullptr, bot->owner())
                << "ordinary walkers: nobody owns them";
            EXPECT_FALSE(adjacent_to_hostile(*w, bot))
                << "no member lands adjacent to a hostile (D2) at ("
                << bot->xpos() << "," << bot->ypos() << ")";
        }
        // The explicit FAIR banks its own code; the still-unauthored
        // team 3 banks its resolved NONE; the authored sides bank
        // nothing (nothing spawned there).
        EXPECT_EQ(expected_fact(og::sim::kFillFair),
                  lineup_fact_code(w->mode.vars[4], 2));
        EXPECT_EQ(expected_fact(og::sim::kFillNone),
                  lineup_fact_code(w->mode.vars[4], 3));
        EXPECT_EQ(0, lineup_fact_code(w->mode.vars[4], 0));
        EXPECT_EQ(0, lineup_fact_code(w->mode.vars[4], 1));
        EXPECT_TRUE(w->scripts().host().errors().empty());
        cells = bot_cells_on(*w, 2);
    };
    std::vector<std::pair<int, int>> first;
    std::vector<std::pair<int, int>> second;
    stage_and_check(first);
    stage_and_check(second);
    ASSERT_EQ(5u, first.size());
    EXPECT_EQ(first, second)
        << "site-less placement is deterministic from the match seed";
}

// D3: BRUTAL on the elves' team fields a solved squad BESIDE the standing
// twelve — the troops are untouched, the squad is sized five, and its
// measured f-sum tracks the D3 target (the weakest human's f × 1.5)
// within solver tolerance, so it actually threatens a level-50 hero.
TEST_F(ClassicLineupTest, brutal_fields_a_threatening_squad_beside_the_elves)
{
    og::server::MatchStageInputs inputs = classic_gladiator_hero50_inputs(11u);
    inputs.equivalent.fill[1] = og::sim::kFillBrutal;
    og::server::MatchStage stage({.networked = false});
    stage.observe_inputs(inputs, 0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const w = stage.world();
    ASSERT_NE(nullptr, w);

    EXPECT_EQ(5, marked_bots_on(*w, 1))
        << "the squad walks on BESIDE the troops (D3)";
    EXPECT_EQ(13 + 5, authored_units_on(*w, 1))
        << "the twelve elves and their generator still stand";
    const long long hero = deployed_hero_f(*w);
    ASSERT_GT(hero, 0) << "the level-50 hero deployed";
    const long long target = hero * 150 / 100;
    const long long fsum = bot_f_sum_on(*w, 1);
    EXPECT_GE(fsum, target * 94 / 100)
        << "hero f " << hero << ", target " << target;
    EXPECT_LE(fsum, target * 106 / 100)
        << "hero f " << hero << ", target " << target;
    EXPECT_NE(0, (w->mode.vars[3] / 100) % 100)
        << "the solve stored team 1's plan";
    EXPECT_EQ(expected_fact(og::sim::kFillBrutal),
              lineup_fact_code(w->mode.vars[4], 1));
    EXPECT_TRUE(w->scripts().host().errors().empty());
}

// D4: the monotone ladder WEAK < FAIR < STRONG < BRUTAL, measured as the
// spawned squad's f-sum against the fixed level-50 roster, on BOTH team
// shapes — the troops-fielded elf team (D3's arm) and the unauthored
// team (D2's arm).
TEST_F(ClassicLineupTest, fill_ladder_is_monotone_on_both_team_shapes)
{
    auto fsum_for = [](int team, std::int16_t fill) {
        og::server::MatchStageInputs inputs =
            classic_gladiator_hero50_inputs(11u);
        inputs.equivalent.fill[static_cast<std::size_t>(team)] = fill;
        og::server::MatchStage stage({.networked = false});
        stage.observe_inputs(inputs, 0);
        EXPECT_EQ(og::server::StageStatus::Staged, stage.status());
        GameWorld* const w = stage.world();
        if (w == nullptr)
            return static_cast<long long>(-1);
        EXPECT_EQ(5, marked_bots_on(*w, team))
            << "team " << team << " fill " << fill;
        return bot_f_sum_on(*w, team);
    };
    for (const int team : {1, 2})
    {
        SCOPED_TRACE(::testing::Message() << "team " << team);
        const long long weak = fsum_for(team, og::sim::kFillWeak);
        const long long fair = fsum_for(team, og::sim::kFillFair);
        const long long strong = fsum_for(team, og::sim::kFillStrong);
        const long long brutal = fsum_for(team, og::sim::kFillBrutal);
        EXPECT_LT(weak, fair) << "WEAK under FAIR";
        EXPECT_LT(fair, strong) << "FAIR under STRONG";
        EXPECT_LT(strong, brutal) << "STRONG under BRUTAL";
    }
}

// D2's spawn-safety on the tick-1 lazy arm (the un-staged worlds'
// path): a wildlife crowd blankets the ground around the only existing
// team, and the site-less squad an explicit STRONG fields on the
// unauthored team walks on OUTSIDE it — five members, none adjacent to
// any hostile, and nothing refuses (C4).
TEST_F(ClassicLineupTest, hostile_crowd_pushes_the_siteless_squad_clear)
{
    ClassicWorld fx;
    fx.spawn_hero(FAMILY_SOLDIER, 0, 96, 96, 100);
    // A marker-only team beside the roster one: the farthest-region
    // rule measures a marker-only team AT its marker (the D2 centroid
    // rule's honest tail).
    fx.spawn_marker(2, 480, 480);
    // Wildlife (team 4) on a 32px lattice around the hero: every cell
    // in the crowd is hostile-adjacent, so the farthest-region rule has
    // to land the squad beyond it.
    for (int ty = 0; ty <= 6; ++ty)
    {
        for (int tx = 0; tx <= 6; ++tx)
            fx.spawn_npc(FAMILY_SOLDIER, 4, tx * 32, ty * 32);
    }
    fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "the explicit wheel fields on unauthored ground (D2)";
    for (const auto& entry : fx.world().oblist)
    {
        const walker* const w = entry.get();
        if (w == nullptr || w->dead() ||
            w->query_order() != Order::Living || w->myguy != nullptr)
            continue;
        if (w->team_num() != 1)
            continue;
        EXPECT_FALSE(adjacent_to_hostile(fx.world(), w))
            << "no member lands adjacent to the crowd, member at ("
            << w->xpos() << "," << w->ypos() << ")";
    }
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// D2 on a map with NO teams at all: nothing exists, so the
// farthest-region rule has no centroid to measure from and the squad an
// explicit STRONG fields takes the bounded safe-teleport scatter — it
// still walks on, because a classic level never refuses (C4).
TEST_F(ClassicLineupTest, explicit_fill_on_a_wholly_empty_map_still_fields)
{
    ClassicWorld fx;
    fx.world().ctf_requested_fill[1] = og::sim::kFillStrong;
    fx.world().tick();

    EXPECT_EQ(5, marked_bots_on(fx.world(), 1))
        << "no centroids: the safe-teleport scatter still fields (C4)";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// The default arms beside the D-series: a stored default NEVER adds a
// squad beside anything — squadless beside standing troops (D3's
// default arm), empty on unauthored ground (D2's default arm) — while
// an explicit WEAK on the other unauthored team fields, proving the
// stage genuinely ran its per-team pass.
TEST_F(ClassicLineupTest, the_default_never_adds_a_squad_beside_anything)
{
    og::server::MatchStageInputs inputs = classic_gladiator_hero50_inputs(11u);
    inputs.equivalent.fill[3] = og::sim::kFillWeak;
    og::server::MatchStage stage({.networked = false});
    stage.observe_inputs(inputs, 0);
    ASSERT_EQ(og::server::StageStatus::Staged, stage.status());
    GameWorld* const w = stage.world();
    ASSERT_NE(nullptr, w);

    EXPECT_EQ(0, marked_bots_on(*w, 1))
        << "the default stays squadless beside the elves";
    EXPECT_EQ(13, authored_units_on(*w, 1)) << "the elves stand untouched";
    EXPECT_EQ(0, live_livings_on(*w, 2))
        << "the default keeps unauthored ground empty (resolved NONE)";
    EXPECT_EQ(5, marked_bots_on(*w, 3))
        << "the explicit WEAK beside it fields (D2)";
    EXPECT_EQ(0, lineup_fact_code(w->mode.vars[4], 1))
        << "no squad on the elves, nothing banked";
    EXPECT_EQ(expected_fact(og::sim::kFillNone),
              lineup_fact_code(w->mode.vars[4], 2))
        << "the unauthored default banks its resolved NONE";
    EXPECT_EQ(expected_fact(og::sim::kFillWeak),
              lineup_fact_code(w->mode.vars[4], 3));
    EXPECT_TRUE(w->scripts().host().errors().empty());
}
