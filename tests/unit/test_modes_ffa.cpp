// The Free For All campaign-pack Lua behavior suite
// (campaigns/modes/packs/modes.core: lib/mode_ffa_impl.lua +
// lib/mode_fighters.lua + scripts/mode_ffa.lua), over the shared modes-pack
// fixture. Rule spec: docs/ffa-design.md §2, §3, §5, §6 (D2/D3/D12/D18/D19).
//
// Levels: all cases bind through the SHIPPED scripts/mode_ffa.lua
// registration over the committed manifest rows 850-855 (the
// mode_match.rows_for adapter), so every case also pins the production
// registration path. The worlds themselves are programmatic (anchors and
// heroes spawned by the rig), exactly like the mutant 840/841 cases.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
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

#include <cstdlib>
#include <format>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace og::modes_test;

namespace {

// The mode-var slot map of lib/mode_ffa_impl.lua (table S).
enum FfaSlot : int {
    kFfaSlotModeId = 0,
    kFfaSlotPhase = 1,
    kFfaSlotFighterCount = 8,
    kFfaSlotScoreLimit = 9,
    kFfaSlotDeadline = 10,
    kFfaSlotItemCursor = 11,
    kFfaSlotItemLast = 12,
    kFfaSlotAnchorCursor = 13,
    kFfaSlotBandBitmap = 14,
    kFfaSlotFlags = 15,
    kFfaSlotFrags = 16,  // +color index
    kFfaSlotIds = 32,    // +color index
};

inline constexpr int kModeIdFfa = 7;  // mode_core.MODE.FFA
inline constexpr int kFfaCadence = 15;
inline constexpr int kBandBase = kFfaTeamBase;    // 16
inline constexpr int kBandCount = kFfaTeamCount;  // 16

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

void align_before_cadence(GameWorld& world)
{
    const std::uint32_t next =
        ((world.tick_count_ / kFfaCadence) + 1) * kFfaCadence;
    world.tick_count_ = next - 1;
}

int alive_band_livings(GameWorld& world)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() >= kBandBase &&
            w->team_num() < kBandBase + kBandCount)
        {
            count++;
        }
    }
    return count;
}

int bitmap_popcount(std::int32_t bitmap)
{
    int count = 0;
    for (int c = 0; c < kBandCount; ++c)
    {
        if ((bitmap >> c) & 1)
            count++;
    }
    return count;
}

bool queue_holds(GameWorld& world, const walker* w)
{
    for (const auto& entry : world.respawn.respawn_queue)
    {
        if (entry.walker_entity_id == w->entity_id())
            return true;
    }
    return false;
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

// An FFA rig on a shipped manifest row: four interleaved anchor clusters
// (position pools) plus hero_count deployed heroes on alternating seat
// teams, initialized on the first tick.
struct FfaRig
{
    ModesCtfWorld fx;
    std::vector<walker*> heroes;

    explicit FfaRig(int level_id = 850, int hero_count = 2,
                    bool anchors = true, int act = ACT_CONTROL)
        : fx(level_id)
    {
        if (anchors)
        {
            fx.spawn_anchor(0, 96, 96);
            fx.spawn_anchor(1, 544, 96);
            fx.spawn_anchor(2, 96, 800);
            fx.spawn_anchor(3, 544, 800);
        }
        for (int i = 0; i < hero_count; ++i)
        {
            walker* h = fx.spawn_hero(FAMILY_SOLDIER, i % 4,
                                      120 + 24 * i, 200, 100 + i, act);
            EXPECT_NE(nullptr, h);
            heroes.push_back(h);
        }
    }

    bool active() const
    {
        return fx.world().mode.vars[kFfaSlotModeId] == kModeIdFfa;
    }

    std::int32_t bitmap() const { return fx.var(kFfaSlotBandBitmap); }
    int frags(int c) const
    {
        return fx.world().mode.vars[static_cast<std::size_t>(kFfaSlotFrags + c)];
    }
    void set_frags(int c, int value)
    {
        fx.world().mode.vars[static_cast<std::size_t>(kFfaSlotFrags + c)] =
            value;
    }
    std::int32_t slot_id(int c) const
    {
        return fx.world().mode.vars[static_cast<std::size_t>(kFfaSlotIds + c)];
    }

    // The color index a walker's entity id occupies, or -1.
    int slot_of(const walker* w) const
    {
        for (int c = 0; c < kBandCount; ++c)
        {
            if (slot_id(c) == static_cast<std::int32_t>(w->entity_id()))
                return c;
        }
        return -1;
    }

    // The lowest occupied color index at or after `from`, or -1.
    int occupied_slot(int from = 0) const
    {
        for (int c = from; c < kBandCount; ++c)
        {
            if (slot_id(c) != 0)
                return c;
        }
        return -1;
    }

    walker* fighter_at(int c)
    {
        return fx.world().find_by_id(static_cast<std::uint32_t>(slot_id(c)));
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

using ModesFfa = ModesPackTest;

// ===========================================================================
// Registration / init
// ===========================================================================

TEST_F(ModesFfa, shipped_manifest_registration_binds_rows_850_to_855)
{
    const std::pair<int, int> rows[] = {{850, 8},  {851, 10}, {852, 12},
                                        {853, 16}, {854, 16}, {855, 16}};
    for (const auto& [level, fighter_count] : rows)
    {
        FfaRig rig(level, 0);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active())
            << "scripts/mode_ffa.lua must bind manifest id " << level;
        EXPECT_EQ(fighter_count, rig.fx.var(kFfaSlotFighterCount))
            << "manifest fighters field fills the arena on " << level;
        EXPECT_EQ(15, rig.fx.var(kFfaSlotScoreLimit)) << "manifest score_limit";
        EXPECT_EQ(7200, rig.fx.var(kFfaSlotDeadline)) << "manifest time_limit";
        EXPECT_EQ(fighter_count, alive_band_livings(rig.fx.world()));
        EXPECT_STREQ("FFA", rig.fx.world().mode.name.data());
        EXPECT_TRUE(has_notification(rig.fx.events, "FREE FOR ALL"));
        EXPECT_TRUE(has_notification(
            rig.fx.events, std::to_string(fighter_count) + " FIGHTERS ENTER"));
    }
}

// #241: the lobby TIME LIMIT knob beats the manifest row (FFA banks the
// resolved value in the slot it calls DEADLINE).
TEST_F(ModesFfa, time_limit_knob_overrides_the_manifest_row)
{
    FfaRig rig(850, 2);
    rig.fx.world().ctf_requested_time_limit = 3600;
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active());
    EXPECT_EQ(3600, rig.fx.var(kFfaSlotDeadline))
        << "the request beats the row's 7200";
    EXPECT_EQ(15, rig.fx.var(kFfaSlotScoreLimit))
        << "and moves no other knob";
}

TEST_F(ModesFfa, init_assigns_exactly_n_distinct_band_bytes)
{
    FfaRig rig(850, 3);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(8, rig.fx.var(kFfaSlotFighterCount));
    EXPECT_EQ(8, bitmap_popcount(rig.bitmap()));

    // Exact set: eight live band livings, eight distinct bytes, every
    // occupied id slot resolving to a live walker wearing 16 + c.
    std::set<int> bytes;
    for (const auto& uptr : rig.fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        const int team = w->team_num();
        ASSERT_GE(team, kBandBase) << "every live living is a band fighter";
        ASSERT_LT(team, kBandBase + kBandCount);
        bytes.insert(team);
    }
    EXPECT_EQ(8u, bytes.size()) << "eight DISTINCT band bytes";
    int occupied = 0;
    for (int c = 0; c < kBandCount; ++c)
    {
        const bool bit = ((rig.bitmap() >> c) & 1) != 0;
        const bool id_set = rig.slot_id(c) != 0;
        EXPECT_EQ(bit, id_set) << "bitmap bit and id slot agree at " << c;
        if (!id_set)
            continue;
        occupied++;
        walker* w = rig.fighter_at(c);
        ASSERT_NE(nullptr, w) << "slot " << c << " resolves to a walker";
        EXPECT_EQ(kBandBase + c, w->team_num())
            << "slot " << c << " wears its own byte";
    }
    EXPECT_EQ(8, occupied);
    // The three heroes are all in the band (bound-first enumeration keeps
    // every deployed character when under the cap).
    for (walker* h : rig.heroes)
    {
        EXPECT_GE(h->team_num(), kBandBase);
        EXPECT_NE(-1, rig.slot_of(h));
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

namespace {

// One seeded init: answers the hero byte sequence and the post-init RNG
// state. The RNG spy records nothing for sim code (inline next()), so the
// determinism pin is rng_.state_ plus the assigned byte sequence.
std::pair<std::vector<int>, std::uint32_t> seeded_init_bytes(
    std::uint32_t seed)
{
    FfaRig rig(850, 4);
    rig.fx.world().rng_.state_ = seed;
    rig.fx.tick(1);
    EXPECT_TRUE(rig.active());
    std::vector<int> bytes;
    for (walker* h : rig.heroes)
        bytes.push_back(h->team_num());
    return {bytes, rig.fx.world().rng_.state_};
}

}  // namespace

TEST_F(ModesFfa, shuffle_is_deterministic_and_seed_sensitive)
{
    const auto [bytes_a, state_a] = seeded_init_bytes(42);
    const auto [bytes_b, state_b] = seeded_init_bytes(42);
    EXPECT_EQ(bytes_a, bytes_b)
        << "same seed must deal the identical byte permutation";
    EXPECT_EQ(state_a, state_b) << "same seed, same post-init RNG state";
    EXPECT_NE(42u, state_a) << "the Fisher-Yates shuffle draws from the "
                               "sim RNG (init advances the stream)";

    const auto [bytes_c, state_c] = seeded_init_bytes(0xBADC0DE);
    EXPECT_NE(bytes_a, bytes_c)
        << "a different seed must deal a different permutation";
    EXPECT_NE(state_a, state_c);
}

TEST_F(ModesFfa, bound_heroes_enumerate_first_and_extras_retire)
{
    FfaRig rig(850, 17);
    // The LAST spawned hero is seat-bound: bound-first enumeration must
    // seat it even though oblist order puts it 17th of 17.
    rig.heroes[16]->set_user(0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(16, rig.fx.var(kFfaSlotFighterCount));

    EXPECT_NE(-1, rig.slot_of(rig.heroes[16]))
        << "the bound hero is guaranteed a slot (D3)";
    EXPECT_GE(rig.heroes[16]->team_num(), kBandBase);
    // Bound-first, then unbound in oblist order: heroes 0..14 keep seats,
    // hero 15 (the 16th unbound candidate) is the one retired.
    for (int i = 0; i < 15; ++i)
    {
        EXPECT_NE(-1, rig.slot_of(rig.heroes[static_cast<std::size_t>(i)]))
            << "hero " << i;
    }
    EXPECT_TRUE(rig.heroes[15]->dead()) << "the 17th candidate retires";
    EXPECT_EQ(SCORE_TEAM_COUNT, rig.heroes[15]->team_num())
        << "mode_strip.retire parks the corpse on byte 4";
    EXPECT_EQ(-1, rig.slot_of(rig.heroes[15]));
    EXPECT_TRUE(has_notification(rig.fx.events, "BAND FULL: 1 RETIRED"));
}

TEST_F(ModesFfa, bot_fill_reaches_the_row_fighter_count)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_EQ(8, rig.fx.var(kFfaSlotFighterCount));
    EXPECT_EQ(8, alive_band_livings(rig.fx.world()));
    int bots = 0;
    for (const auto& uptr : rig.fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            continue;
        bots++;
        EXPECT_EQ(255, w->real_team_num()) << "bots carry the 255 sentinel";
        EXPECT_GE(w->team_num(), kBandBase);
    }
    EXPECT_EQ(6, bots) << "exactly the deficit to the row's fighters count";
}

// The band path honours the lineup knobs through TEAM 1's pair (lineup
// §3.2 — the band is ONE population): NONE suppresses the fill entirely,
// leaving only the deployed fighters — and so does OFF, which has no mask
// to take a team out of in a band (mode_fighters.lua band_knob).
TEST_F(ModesFfa, lineup_none_suppresses_the_band_fill)
{
    for (const short knob : {og::sim::kBotSquadNone, og::sim::kBotSquadOff})
    {
        SCOPED_TRACE(::testing::Message() << "knob " << knob);
        FfaRig rig(850, 2);
        rig.fx.world().ctf_requested_fill[0] = knob;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());

        EXPECT_EQ(2, rig.fx.var(kFfaSlotFighterCount))
            << "the knob fields nothing; the two heroes are the whole band";
        EXPECT_EQ(2, alive_band_livings(rig.fx.world()));
    }
}

namespace {

// Every band bot's level, asserted equal to `expected`; answers the count.
int expect_band_bot_levels(GameWorld& world, int expected)
{
    int bots = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            continue;
        ++bots;
        if (w->stats() != nullptr)
        {
            EXPECT_EQ(expected, w->stats()->level());
        }
    }
    return bots;
}

}  // namespace

// A preset replaces the band's fill roster and the LV offset (amendment
// A6) rides on top of the session-difficulty formula — L2 at the rig's
// 100 percent, so +3 is L5 — singles per free slot staying the band's
// hard shape (the mutant/FFA "one bot per slot" rule, preset.count
// notwithstanding).
TEST_F(ModesFfa, lineup_preset_and_level_shape_the_band_fill)
{
    FfaRig rig(850, 2);
    rig.fx.world().ctf_requested_fill[0] =
        og::sim::kBotSquadPresetBase + 2;  // BRUTES
    rig.fx.world().ctf_requested_map_units[0] = 3;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_EQ(8, rig.fx.var(kFfaSlotFighterCount))
        << "the fill still reaches the row's fighter count — singles per "
           "free slot, whatever the preset says";
    int bots = 0;
    for (const auto& uptr : rig.fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            continue;
        ++bots;
        const int family = w->family();
        EXPECT_TRUE(family == FAMILY_SOLDIER || family == FAMILY_BARBARIAN ||
                    family == FAMILY_ORC)
            << "band bots draw from the preset's families, got " << family;
        ASSERT_NE(nullptr, w->stats());
        EXPECT_EQ(5, w->stats()->level())
            << "the offset rides on top of the formula: L2 + 3";
    }
    EXPECT_EQ(6, bots);
}

// The offset's clamps in the band: -5 on the formula's L2 lands L1, +5
// lands L7 (no clamp needed), and AUTO is the formula itself.
TEST_F(ModesFfa, lineup_level_offset_clamps_in_the_band)
{
    {
        FfaRig rig(850, 2);
        rig.fx.world().ctf_requested_map_units[0] = -5;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        EXPECT_EQ(6, expect_band_bot_levels(rig.fx.world(), 1));
    }
    {
        FfaRig rig(850, 2);
        rig.fx.world().ctf_requested_map_units[0] = 5;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        EXPECT_EQ(6, expect_band_bot_levels(rig.fx.world(), 7));
    }
    {
        FfaRig rig(850, 2);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        EXPECT_EQ(6, expect_band_bot_levels(rig.fx.world(), 2));
    }
}

// FAIR carries no families and no band solver exists (PLAN_BASE indexes
// score teams only): the band reads it as AUTO — the documented carve-out.
TEST_F(ModesFfa, lineup_fair_preset_reads_as_auto_in_the_band)
{
    FfaRig rig(850, 2);
    rig.fx.world().ctf_requested_fill[0] =
        og::sim::kBotSquadPresetBase + 4;  // FAIR
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_EQ(8, rig.fx.var(kFfaSlotFighterCount));
    EXPECT_EQ(8, alive_band_livings(rig.fx.world()));
}

TEST_F(ModesFfa, init_strips_authored_troops_and_generators_keeps_wildlife)
{
    FfaRig rig(850, 2);
    walker* troop = rig.fx.spawn_living(FAMILY_ORC, 1, 520, 200);
    walker* gen = rig.fx.spawn_generator(FAMILY_TENT, 2, 432, 432);
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 700);
    ASSERT_NE(nullptr, troop);
    ASSERT_NE(nullptr, gen);
    ASSERT_NE(nullptr, wildlife);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_TRUE(troop->dead()) << "authored score-range livings retire";
    EXPECT_TRUE(gen->dead()) << "score-range generators retire";
    EXPECT_FALSE(wildlife->dead()) << "wildlife (bytes 4-7) is arena identity";
    EXPECT_FALSE(rig.heroes[0]->dead()) << "roster walkers are never stripped";
    EXPECT_EQ(8, rig.fx.var(kFfaSlotFighterCount))
        << "the stripped troop is not a fighter";

    // A generator death mid-match is a non-Living event for the ledger.
    walker* wild_gen = rig.fx.spawn_generator(FAMILY_TENT, 5, 500, 500);
    ASSERT_NE(nullptr, wild_gen);
    wild_gen->set_dead(1);
    wild_gen->death();
    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.frags(c)) << "slot " << c;
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesFfa, mode_id_writes_last_and_init_spawns_stay_out_of_adoption)
{
    // The activation latch: MODE_ID reads 0 while on_mode_init runs, so
    // the bot spawns' on_entity_spawn dispatches fall out of the adoption
    // arm — every slot is single-booked by init itself.
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_EQ(kModeIdFfa, rig.fx.var(kFfaSlotModeId));

    std::set<std::int32_t> ids;
    int occupied = 0;
    for (int c = 0; c < kBandCount; ++c)
    {
        if (rig.slot_id(c) == 0)
            continue;
        occupied++;
        ids.insert(rig.slot_id(c));
    }
    EXPECT_EQ(8, occupied);
    EXPECT_EQ(8u, ids.size()) << "no double-booked slots";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// Frag ledger (the killer channel, band-shifted TDM semantics)
// ===========================================================================

TEST_F(ModesFfa, attributed_kill_bumps_the_killer_slot_and_schedules)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];

    rig.slay(a, b);
    EXPECT_EQ(1, rig.frags(rig.slot_of(a))) << "killer slot frags";
    EXPECT_EQ(0, rig.frags(rig.slot_of(b)));
    // Synchronous scheduling in on_entity_death: already queued on the
    // death tick, so respawn_retains_player_control's scripted arm holds
    // with no gap tick.
    EXPECT_TRUE(queue_holds(rig.fx.world(), b))
        << "the corpse is queued before the next tick";
}

TEST_F(ModesFfa, suicide_shape_decrements_the_byte_slot)
{
    // The engine's root-team gate refuses literal self-damage, so the
    // suicide arm is reached through attribution: a fresh cross-byte stamp
    // whose victim then wears the killer's byte (charm shapes).
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    const int a_slot = rig.slot_of(a);

    ASSERT_NE(nullptr, b->stats());
    b->stats()->set_hitpoints(100.0f);
    a->attack(b);  // stamps a's band byte
    ASSERT_LT(b->stats()->hitpoints(), 100.0f);
    b->set_team_num(a->team_num());  // the charm flip
    b->set_dead(1);
    b->death();

    EXPECT_EQ(-1, rig.frags(a_slot)) << "suicide decrements, may go negative";
}

TEST_F(ModesFfa, environment_death_scores_nothing)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    rig.heroes[1]->set_dead(1);
    rig.heroes[1]->death();
    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.frags(c)) << "slot " << c;
}

TEST_F(ModesFfa, mutual_kill_scores_via_the_stamped_team)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    const int a_slot = rig.slot_of(a);

    ASSERT_NE(nullptr, b->stats());
    b->stats()->set_hitpoints(100.0f);
    a->attack(b);  // fresh stamp
    ASSERT_LT(b->stats()->hitpoints(), 100.0f);
    a->set_dead(1);  // the killer falls with its victim
    b->set_dead(1);
    b->death();

    EXPECT_EQ(1, rig.frags(a_slot))
        << "the stamped TEAM survives a mutual kill (D3 recency channel)";
}

TEST_F(ModesFfa, summon_kills_credit_the_owner_and_summon_deaths_score_nothing)
{
    FfaRig rig(850, 3);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    walker* c = rig.heroes[2];
    const int a_slot = rig.slot_of(a);
    const int b_slot = rig.slot_of(b);

    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, a->team_num(),
                                      a->xpos() + 32, a->ypos());
    ASSERT_NE(nullptr, pet);
    pet->set_owner(a);

    // The owner-chain-root stamp: the pet's kill credits a's slot.
    rig.slay(pet, c);
    EXPECT_EQ(1, rig.frags(a_slot)) << "summon kills credit the owner root";

    // The pet as a victim: band byte but owned — no frag, no respawn.
    rig.slay(b, pet);
    EXPECT_EQ(0, rig.frags(b_slot)) << "owned victims never award frags";
    EXPECT_FALSE(queue_holds(rig.fx.world(), pet))
        << "an owned spawn never enters the respawn queue";
}

TEST_F(ModesFfa, charmed_killer_credits_the_charmer_byte)
{
    FfaRig rig(850, 3);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    walker* c = rig.heroes[2];
    const int a_slot = rig.slot_of(a);
    const int b_slot = rig.slot_of(b);

    // b charmed by a: wears a's byte, banks its own.
    b->set_real_team_num(static_cast<unsigned char>(kBandBase + b_slot));
    b->set_team_num(a->team_num());

    rig.slay(b, c);
    EXPECT_EQ(1, rig.frags(a_slot))
        << "a charmed fighter's kills credit the charmer's byte";
    EXPECT_EQ(0, rig.frags(b_slot));
}

TEST_F(ModesFfa, wildlife_neither_scores_nor_blocks_fighter_respawn)
{
    FfaRig rig(850, 2);
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 700);
    ASSERT_NE(nullptr, wildlife);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];

    // Killing wildlife earns nothing.
    rig.slay(a, wildlife);
    EXPECT_EQ(0, rig.frags(rig.slot_of(a)));
    EXPECT_FALSE(queue_holds(rig.fx.world(), wildlife))
        << "wildlife never respawns";

    // A wildlife killer earns nothing, but the fighter still respawns.
    walker* wildlife2 = rig.fx.spawn_living(FAMILY_ORC, 5, b->xpos() + 32,
                                            b->ypos());
    ASSERT_NE(nullptr, wildlife2);
    rig.slay(wildlife2, b);
    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.frags(c)) << "slot " << c;
    EXPECT_TRUE(queue_holds(rig.fx.world(), b));
}

TEST_F(ModesFfa, out_of_band_killer_bytes_score_nothing)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];

    // An UNASSIGNED band byte (a free slot's) stamps no ledger entry.
    int free_slot = -1;
    for (int c = 0; c < kBandCount; ++c)
    {
        if (rig.slot_id(c) == 0)
        {
            free_slot = c;
            break;
        }
    }
    ASSERT_NE(-1, free_slot) << "an 8-fighter band leaves free slots";
    walker* stray = rig.fx.spawn_living(FAMILY_ORC, 0, a->xpos() + 32,
                                        a->ypos());
    ASSERT_NE(nullptr, stray);
    stray->set_team_num(static_cast<unsigned char>(kBandBase + free_slot));
    rig.slay(stray, a);

    // A byte past the band scores nothing either.
    walker* stray2 = rig.fx.spawn_living(FAMILY_ORC, 0, b->xpos() + 32,
                                         b->ypos());
    ASSERT_NE(nullptr, stray2);
    stray2->set_team_num(200);
    rig.slay(stray2, b);

    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.frags(c)) << "slot " << c;
    EXPECT_TRUE(queue_holds(rig.fx.world(), a)) << "the victims still respawn";
    EXPECT_TRUE(queue_holds(rig.fx.world(), b));
}

// ===========================================================================
// Win / timeout / winner naming
// ===========================================================================

TEST_F(ModesFfa, score_limit_win_latches_flush_revives_and_reasserts)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    const int a_slot = rig.slot_of(a);

    rig.slay(a, b);
    ASSERT_TRUE(queue_holds(rig.fx.world(), b));
    rig.set_frags(a_slot, 15);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(kBandBase + a_slot, rig.fx.world().mode.winner_team)
        << "og.declare_winner carries the band byte";
    EXPECT_TRUE(rig.fx.world().mode.winner_is_player)
        << "a live myguy on the winning byte is a player win";
    EXPECT_EQ(851, rig.fx.world().next_level) << "player win advances";
    EXPECT_FALSE(b->dead()) << "first arming flush-revives pending respawns";
    EXPECT_TRUE(has_notification(rig.fx.events, "WINNER: "));

    // The latch re-asserts every tick on a decided match.
    rig.fx.tick(2);
    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(kBandBase + a_slot, rig.fx.world().mode.winner_team);
}

TEST_F(ModesFfa, bot_only_win_is_rematch_shape)
{
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int winner_slot = rig.occupied_slot();
    ASSERT_NE(-1, winner_slot);

    rig.set_frags(winner_slot, 15);
    rig.fx.tick(1);
    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(kBandBase + winner_slot, rig.fx.world().mode.winner_team);
    EXPECT_FALSE(rig.fx.world().mode.winner_is_player);
    EXPECT_EQ(850, rig.fx.world().next_level) << "bot winners: rematch shape";
}

TEST_F(ModesFfa, timeout_leader_wins_and_ties_go_to_the_lowest_index)
{
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int c1 = rig.occupied_slot();
    const int c2 = rig.occupied_slot(c1 + 1);
    ASSERT_NE(-1, c1);
    ASSERT_NE(-1, c2);

    rig.set_frags(c1, 4);
    rig.set_frags(c2, 4);
    rig.fx.world().set_level_tick_count(7200 - 2);
    rig.fx.tick(2);

    EXPECT_TRUE(rig.fx.world().game_ended);
    EXPECT_EQ(kBandBase + c1, rig.fx.world().mode.winner_team)
        << "the all-square tie resolves to the LOWEST color index";
}

TEST_F(ModesFfa, winner_toast_carries_the_clipped_character_name)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    ASSERT_NE(nullptr, a->myguy);
    a->myguy->name = "VERYLONGNAMEOVERSEVENTEEN";  // 25 chars

    rig.set_frags(rig.slot_of(a), 15);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.fx.world().game_ended);

    // "WINNER: " (8) + the 17-char clip = the exact 25-char budget.
    EXPECT_TRUE(has_notification(rig.fx.events, "WINNER: VERYLONGNAMEOVERS"));
    EXPECT_FALSE(has_notification(rig.fx.events, "WINNER: VERYLONGNAMEOVERSE"))
        << "the name must clip at 17 chars";
    EXPECT_STREQ("WINNER: VERYLONGNAMEOVERS",
                 rig.fx.world().mode.hud[0].text.data())
        << "HUD slot 0 keeps the winner line";
    EXPECT_EQ(rig.fx.world().mode.winner_team,
              static_cast<int>(rig.fx.world().mode.hud[0].team))
        << "the winner line is tinted with the winner byte";
}

TEST_F(ModesFfa, unnamed_winner_falls_back_to_the_band_color_name)
{
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int winner_slot = rig.occupied_slot();
    ASSERT_NE(-1, winner_slot);

    rig.set_frags(winner_slot, 15);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.fx.world().game_ended);
    const std::string expected =
        std::string("WINNER: ") +
        og::sim::team_color_name(kBandBase + winner_slot);
    EXPECT_TRUE(has_notification(rig.fx.events, expected))
        << "expected \"" << expected << "\" (bots have no character name)";
}

// ===========================================================================
// HUD / beacon
// ===========================================================================

TEST_F(ModesFfa, hud_carries_leader_runnerup_and_goal_rows)
{
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int c1 = rig.occupied_slot();
    const int c2 = rig.occupied_slot(c1 + 1);
    ASSERT_NE(-1, c1);
    ASSERT_NE(-1, c2);

    // All-square standings: leader = lowest occupied index, runner-up next.
    const std::string first = std::string("1ST ") +
                              og::sim::team_color_name(kBandBase + c1) + " 0";
    const std::string second = std::string("2ND ") +
                               og::sim::team_color_name(kBandBase + c2) + " 0";
    EXPECT_STREQ(first.c_str(), rig.fx.world().mode.hud[0].text.data());
    EXPECT_EQ(kBandBase + c1, static_cast<int>(rig.fx.world().mode.hud[0].team));
    EXPECT_STREQ(second.c_str(), rig.fx.world().mode.hud[1].text.data());
    EXPECT_EQ(kBandBase + c2, static_cast<int>(rig.fx.world().mode.hud[1].team));
    EXPECT_STREQ("GOAL 15", rig.fx.world().mode.hud[2].text.data());
    EXPECT_EQ(255, static_cast<int>(rig.fx.world().mode.hud[2].team))
        << "the goal row keeps the default tint";

    // A frag re-sorts the standings.
    rig.set_frags(c2, 3);
    rig.fx.tick(1);
    const std::string flipped =
        std::string("1ST ") + og::sim::team_color_name(kBandBase + c2) + " 3";
    EXPECT_STREQ(flipped.c_str(), rig.fx.world().mode.hud[0].text.data());
}

TEST_F(ModesFfa, beacon_marks_the_leader_inside_the_endgame_margin)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    const int a_slot = rig.slot_of(a);

    rig.set_frags(a_slot, 11);
    rig.fx.tick(1);
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].entity_id)
        << "11/15 is outside the 3-frag margin";

    rig.set_frags(a_slot, 12);
    rig.fx.tick(1);
    EXPECT_EQ(static_cast<std::int32_t>(a->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id)
        << "12/15 arms the endgame beacon";
    EXPECT_EQ(kBandBase + a_slot,
              static_cast<int>(rig.fx.world().mode.beacons[0].team));

    // A dead leader clears the beacon (frags persist, the body is down).
    rig.slay(rig.heroes[1], a);
    rig.fx.tick(1);
    EXPECT_EQ(0, rig.fx.world().mode.beacons[0].entity_id)
        << "no beacon on a corpse";
}

// ===========================================================================
// Respawns (rotation, band-byte retention, D12 re-assert, backstop)
// ===========================================================================

TEST_F(ModesFfa, anchor_rotation_is_deterministic_and_spreads_fighters)
{
    auto positions = []() {
        FfaRig rig(850, 2);
        rig.fx.tick(1);
        EXPECT_TRUE(rig.active());
        std::vector<std::pair<int, int>> out;
        for (const auto& uptr : rig.fx.world().oblist)
        {
            const walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living)
                continue;
            out.emplace_back(w->xpos(), w->ypos());
        }
        return out;
    };
    const auto first = positions();
    const auto second = positions();
    ASSERT_EQ(8u, first.size());
    EXPECT_EQ(first, second) << "same seed, same arena: identical placement";
    const std::set<std::pair<int, int>> distinct(first.begin(), first.end());
    EXPECT_EQ(8u, distinct.size())
        << "the pool rotation + ring fallback never stacks fighters";
}

TEST_F(ModesFfa, band_byte_survives_revive)
{
    FfaRig rig(850, 2);
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* b = rig.heroes[1];
    const int byte = b->team_num();

    rig.slay(rig.heroes[0], b);
    rig.fx.tick(12);
    EXPECT_FALSE(b->dead());
    EXPECT_EQ(byte, b->team_num()) << "the fighter revives on its own byte";
    EXPECT_EQ(255, b->real_team_num());
}

TEST_F(ModesFfa, charmed_death_revive_reasserts_the_slot_byte)
{
    // D12: revive_player_walker clears real_team_num to 255 but restores
    // team only for bytes < 4, so a fighter that dies while charmed would
    // revive wearing the charmer's byte forever — on_respawn re-asserts
    // the slot's assigned byte.
    FfaRig rig(850, 3);
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    walker* c = rig.heroes[2];
    const int b_slot = rig.slot_of(b);

    // b dies while charmed onto c's byte.
    b->set_real_team_num(static_cast<unsigned char>(kBandBase + b_slot));
    b->set_team_num(c->team_num());
    rig.slay(a, b);
    rig.fx.tick(12);

    EXPECT_FALSE(b->dead());
    EXPECT_EQ(kBandBase + b_slot, b->team_num())
        << "on_respawn re-asserts the assigned byte";
    EXPECT_EQ(255, b->real_team_num());
}

TEST_F(ModesFfa, silent_corpse_backstop_schedules_and_revives)
{
    FfaRig rig(850, 2);
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];

    a->set_dead(1);  // bypasses walker::death and the death hook
    rig.fx.tick(1);
    EXPECT_TRUE(queue_holds(rig.fx.world(), a))
        << "the per-tick backstop schedules silent corpses";
    rig.fx.tick(11);
    EXPECT_FALSE(a->dead());
}

TEST_F(ModesFfa, no_anchor_respawn_revives_in_place)
{
    FfaRig rig(850, 2, /*anchors=*/false);
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active()) << "init placement teleports without anchors";
    walker* b = rig.heroes[1];

    rig.slay(rig.heroes[0], b);
    const int corpse_x = b->xpos();
    const int corpse_y = b->ypos();
    rig.fx.tick(12);
    EXPECT_FALSE(b->dead());
    EXPECT_EQ(corpse_x, b->xpos())
        << "no pools: the engine revive-in-place stands";
    EXPECT_EQ(corpse_y, b->ypos());
}

// ===========================================================================
// Charm renormalize (cadence pass)
// ===========================================================================

TEST_F(ModesFfa, renormalize_heals_berserk_residue_and_spares_the_charmed)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* a = rig.heroes[0];
    walker* b = rig.heroes[1];
    const int a_slot = rig.slot_of(a);
    const int b_slot = rig.slot_of(b);

    // Berserk residue: wrong byte, 255 sentinel — heal it. The adoption
    // sweep must NOT re-adopt the low-byte fighter (the id guard).
    a->set_team_num(3);
    // Actively charmed: wrong byte, banked real team, live charm clock
    // (living::act uncharms a walker whose charm_left ran out) — leave
    // alone.
    b->set_real_team_num(static_cast<unsigned char>(kBandBase + b_slot));
    b->set_team_num(5);
    b->set_charm_left(200);

    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(kBandBase + a_slot, a->team_num())
        << "the cadence pass re-asserts the assigned byte";
    EXPECT_EQ(5, b->team_num()) << "charmed fighters are left alone";
    EXPECT_EQ(a_slot, rig.slot_of(a))
        << "the id guard kept the low-byte fighter out of adoption";
}

// ===========================================================================
// Mid-join adoption (D19)
// ===========================================================================

TEST_F(ModesFfa, midjoin_hero_adopts_the_next_free_byte)
{
    FfaRig rig(850, 2);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(8, bitmap_popcount(rig.bitmap()));

    walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 1, 300, 300, 500);
    ASSERT_NE(nullptr, joiner);
    rig.fx.tick(1);

    const int slot = rig.slot_of(joiner);
    ASSERT_NE(-1, slot) << "the joiner is registered";
    EXPECT_EQ(kBandBase + slot, joiner->team_num());
    EXPECT_EQ(0, rig.frags(slot));
    EXPECT_EQ(9, bitmap_popcount(rig.bitmap()));
    EXPECT_EQ(9, rig.fx.var(kFfaSlotFighterCount));
}

TEST_F(ModesFfa, midjoin_into_a_full_band_replaces_the_lowest_frag_bot)
{
    // Explicit low-frag bot.
    {
        FfaRig rig(853, 2);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        ASSERT_EQ(16, bitmap_popcount(rig.bitmap()));
        // Pick a BOT slot that is not the first bot slot and sink its frags.
        int first_bot = -1;
        int low_bot = -1;
        for (int c = 0; c < kBandCount; ++c)
        {
            walker* w = rig.fighter_at(c);
            ASSERT_NE(nullptr, w);
            if (w->myguy != nullptr)
                continue;
            if (first_bot < 0)
            {
                first_bot = c;
                continue;
            }
            if (low_bot < 0)
                low_bot = c;
        }
        ASSERT_NE(-1, first_bot);
        ASSERT_NE(-1, low_bot);
        rig.set_frags(low_bot, -2);
        walker* bot = rig.fighter_at(low_bot);

        walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 300, 600);
        ASSERT_NE(nullptr, joiner);
        rig.fx.tick(1);

        EXPECT_EQ(low_bot, rig.slot_of(joiner))
            << "the lowest-frag bot's slot is the one handed over";
        EXPECT_EQ(kBandBase + low_bot, joiner->team_num());
        EXPECT_EQ(0, rig.frags(low_bot)) << "the slot's frags reset";
        EXPECT_TRUE(bot->dead()) << "the displaced bot retires";
        EXPECT_EQ(SCORE_TEAM_COUNT, bot->team_num());
    }
    // All-square frags: the tiebreak takes the lowest bot index.
    {
        FfaRig rig(853, 2);
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        int first_bot = -1;
        for (int c = 0; c < kBandCount; ++c)
        {
            walker* w = rig.fighter_at(c);
            ASSERT_NE(nullptr, w);
            if (w->myguy == nullptr)
            {
                first_bot = c;
                break;
            }
        }
        ASSERT_NE(-1, first_bot);

        walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 300, 601);
        ASSERT_NE(nullptr, joiner);
        rig.fx.tick(1);
        EXPECT_EQ(first_bot, rig.slot_of(joiner))
            << "ascending color-index tiebreak";
    }
}

TEST_F(ModesFfa, midjoin_with_no_bots_keeps_the_seat_team)
{
    FfaRig rig(853, 16);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(16, bitmap_popcount(rig.bitmap()));

    walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 2, 300, 300, 700);
    ASSERT_NE(nullptr, joiner);
    rig.fx.tick(3);

    EXPECT_EQ(-1, rig.slot_of(joiner)) << "no bots to displace: no slot";
    EXPECT_EQ(2, joiner->team_num())
        << "the joiner keeps its seat team (documented cap) — it still "
           "fights everyone";
    EXPECT_EQ(16, rig.fx.var(kFfaSlotFighterCount));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// AI director (repair-only + endgame focus)
// ===========================================================================

TEST_F(ModesFfa, director_repairs_backstop_wildlife_onto_the_nearest_fighter)
{
    // The engine's pre-act backstop (find_far_foe) refills empty and dead
    // foes before the post-act director runs, and it happily hands out
    // wildlife and other non-band livings — those are the broken foes the
    // repair arm exists for.
    FfaRig rig(850, 0);
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 700);
    ASSERT_NE(nullptr, wildlife);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    // Park the fighters and lay out a known geometry.
    std::vector<walker*> bots;
    for (int c = 0; c < kBandCount; ++c)
    {
        walker* w = rig.fighter_at(c);
        if (w == nullptr)
            continue;
        w->set_act_type(ACT_SIT);
        bots.push_back(w);
    }
    ASSERT_EQ(8u, bots.size());
    wildlife->set_act_type(ACT_SIT);
    walker* m = bots[0];
    walker* near_foe = bots[1];
    m->setxy(200, 200);
    wildlife->setxy(230, 200);  // nearest overall — the backstop's pick
    near_foe->setxy(280, 200);  // nearest FIGHTER — the repair's pick
    for (std::size_t i = 2; i < bots.size(); ++i)
        bots[i]->setxy(static_cast<short>(500 + 20 * i),
                       static_cast<short>(800));

    m->set_foe(nullptr);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(near_foe->entity_id(), m->foe_id())
        << "the wildlife foe the backstop handed out is repaired onto the "
           "nearest FIGHTER (backstop alone would keep the closer orc)";

    // A live out-of-band foe (a byte past the band) also reads as broken.
    walker* stray = rig.fx.spawn_living(FAMILY_ORC, 0, 232, 200);
    ASSERT_NE(nullptr, stray);
    stray->set_team_num(200);
    stray->set_act_type(ACT_SIT);
    m->set_foe(stray);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(near_foe->entity_id(), m->foe_id())
        << "a foe wearing a byte past the band is repaired away";
}

TEST_F(ModesFfa, director_endgame_focus_retargets_at_the_leader)
{
    FfaRig rig(850, 0);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    std::vector<int> slots;
    for (int c = 0; c < kBandCount; ++c)
    {
        if (rig.slot_id(c) != 0)
            slots.push_back(c);
    }
    ASSERT_EQ(8u, slots.size());
    walker* leader = rig.fighter_at(slots[0]);
    walker* hunter = rig.fighter_at(slots[1]);
    walker* other = rig.fighter_at(slots[2]);
    ASSERT_NE(nullptr, leader);
    ASSERT_NE(nullptr, hunter);
    ASSERT_NE(nullptr, other);
    for (int c : slots)
        rig.fighter_at(c)->set_act_type(ACT_SIT);

    // A healthy foe normally sticks…
    hunter->set_foe(other);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(other->entity_id(), hunter->foe_id())
        << "repair-only outside the endgame";

    // …until the leader is within 3 of the limit.
    rig.set_frags(slots[0], 12);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(leader->entity_id(), hunter->foe_id())
        << "endgame focus retargets everyone at the leader";

    // A dead leader ends the focus: the frags persist but nobody is
    // steered at a corpse.
    rig.slay(other, leader);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_NE(leader->entity_id(), hunter->foe_id())
        << "no focus on a corpse leader";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// Determinism + instruction budget
// ===========================================================================

namespace {

std::string run_ffa_bot_match(int ticks, bool* scored)
{
    FfaRig rig(850, 0, /*anchors=*/true);
    // Close-quarters pools so the brawl produces frags inside the window.
    for (auto& uptr : rig.fx.world().oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && w->query_order() == Order::Special)
            w->setxy(static_cast<short>(300 + (w->team_num() % 2) * 60),
                     static_cast<short>(300 + (w->team_num() / 2) * 60));
    }
    rig.fx.world().ctf_requested_respawn_ticks = 30;
    for (int i = 0; i < ticks; ++i)
    {
        rig.fx.tick(1);
        for (int c = 0; c < kBandCount; ++c)
        {
            if (rig.frags(c) != 0)
                *scored = true;
        }
    }
    EXPECT_EQ(kModeIdFfa, rig.fx.var(kFfaSlotModeId));
    return digest_world(rig.fx.world());
}

}  // namespace

TEST_F(ModesFfa, ffa_bot_match_is_deterministic_across_runs)
{
    bool scored_first = false;
    bool scored_second = false;
    const std::string first = run_ffa_bot_match(600, &scored_first);
    const std::string second = run_ffa_bot_match(600, &scored_second);
    ASSERT_TRUE(scored_first)
        << "the close-quarters bot brawl must produce a frag (the run "
           "exercises the ledger and the director, not an idle map)";
    ASSERT_EQ(scored_first, scored_second);
    ASSERT_EQ(first, second)
        << "same seed + same arena must replay identically (shuffle, "
           "placement, ledger, director, respawns included)";
}

TEST_F(ModesFfa, full_mode_tick_fits_a_tenth_of_the_instruction_budget)
{
    BudgetOverride budget(500000);
    FfaRig rig(853, 2, /*anchors=*/true, ACT_RANDOM);
    rig.fx.world().ctf_requested_respawn_ticks = 30;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(16, rig.fx.var(kFfaSlotFighterCount))
        << "the busy world is the full 16-fighter band";
    rig.fx.tick(45);  // 3 director cadences + ledger + win/HUD phases
    EXPECT_FALSE(has_script_error(rig.fx.world(), "instruction budget"))
        << "a 10x-reduced budget must never trip";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}
