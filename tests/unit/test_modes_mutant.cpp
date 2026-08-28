// The Mutant campaign-pack Lua behavior suite
// (campaigns/modes/packs/modes.core: lib/mode_mutant_impl.lua +
// lib/mode_fighters.lua + scripts/mode_mutant.lua), over the shared
// modes-pack fixture. Rule spec: docs/ffa-design.md §8 (D13) amending
// modes.md §7 — competitors are individual deployed characters on the
// fighter band (team bytes 16-31), cap 16, bot fill to the manifest row's
// fighters count; two humans on one lobby team become separate mutually
// hostile competitors (issue #187).
//
// Mode-private slot map mirror (lib/mode_mutant_impl.lua table S):
//   8  FIGHTER_COUNT      9  SCORE_LIMIT       10 TIME_LIMIT
//   11 RESPAWN_TICKS      12 ANCHOR_CURSOR     13 MUTANT_ENTITY
//   14 MUTANT_TEAM        15 MUTANT_BASE_DAMAGE
//   16..31 SCORE (+c)     32..47 IDS (+c)      48 BAND_BITMAP
//   49 ITEM_CURSOR        50 ITEM_LAST
//
// Levels: 9201/9202 bind through the test manifest rows; 840 binds through
// the SHIPPED scripts/mode_mutant.lua registration (mode_match.rows_for
// over the committed manifest, fighters = 4), which also arms the
// manifest-gated teleport clamp and the on_entity_spawn adoption arm.

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
#include <vector>

using namespace og::modes_test;

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace {

// The mode-var slot map of lib/mode_mutant_impl.lua (table S).
enum MutantSlot : int {
    kMutSlotModeId = 0,
    kMutSlotPhase = 1,
    kMutSlotFighterCount = 8,
    kMutSlotScoreLimit = 9,
    kMutSlotTimeLimit = 10,
    kMutSlotRespawnTicks = 11,
    kMutSlotAnchorCursor = 12,
    kMutSlotMutantEntity = 13,
    kMutSlotMutantTeam = 14,        // the mutant's ASSIGNED band byte; 0 = none
    kMutSlotMutantBaseDamage = 15,  // x256
    kMutSlotScore = 16,             // +color index
    kMutSlotIds = 32,               // +color index
    kMutSlotBandBitmap = 48,
};

inline constexpr int kModeIdMutant = 5;  // mode_core.MODE.MUTANT
inline constexpr int kPhaseFfa = 1;
inline constexpr int kPhaseMutant = 2;
inline constexpr int kMutantCadence = 15;
inline constexpr int kMutantBit = 16384;
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

// A 4-competitor rig on the fighter band (four anchor pools, four deployed
// heroes on seat teams 0-3 reseated onto band bytes at init), initialized
// on the first tick.
struct MutantRig
{
    ModesCtfWorld fx;
    walker* a = nullptr;  // deployed on seat team 0
    walker* b = nullptr;  // deployed on seat team 1
    walker* c = nullptr;  // deployed on seat team 2
    walker* d = nullptr;  // deployed on seat team 3

    explicit MutantRig(int level_id = kMutantLevelA, int act = ACT_CONTROL)
        : fx(level_id)
    {
        fx.spawn_anchor(0, 96, 96);
        fx.spawn_anchor(1, 544, 96);
        fx.spawn_anchor(2, 96, 800);
        fx.spawn_anchor(3, 544, 800);
        a = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1, act);
        b = fx.spawn_hero(FAMILY_ORC, 1, 216, 200, 2, act);
        c = fx.spawn_hero(FAMILY_SOLDIER, 2, 200, 232, 3, act);
        d = fx.spawn_hero(FAMILY_ORC, 3, 216, 232, 4, act);
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
    std::int32_t mutant_team() const
    {
        return fx.world().mode.vars[kMutSlotMutantTeam];
    }
    std::int32_t bitmap() const { return fx.var(kMutSlotBandBitmap); }
    int score(int slot) const
    {
        return fx.world()
            .mode.vars[static_cast<std::size_t>(kMutSlotScore + slot)];
    }
    void set_score(int slot, int value)
    {
        fx.world().mode.vars[static_cast<std::size_t>(kMutSlotScore + slot)] =
            value;
    }
    std::int32_t slot_id(int slot) const
    {
        return fx.world()
            .mode.vars[static_cast<std::size_t>(kMutSlotIds + slot)];
    }

    // The color index a walker's entity id occupies, or -1.
    int slot_of(const walker* w) const
    {
        for (int slot = 0; slot < kBandCount; ++slot)
        {
            if (slot_id(slot) == static_cast<std::int32_t>(w->entity_id()))
                return slot;
        }
        return -1;
    }

    // The lowest occupied color index at or after `from`, or -1.
    int occupied_slot(int from = 0) const
    {
        for (int slot = from; slot < kBandCount; ++slot)
        {
            if (slot_id(slot) != 0)
                return slot;
        }
        return -1;
    }

    walker* fighter_at(int slot)
    {
        return fx.world().find_by_id(
            static_cast<std::uint32_t>(slot_id(slot)));
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
// Activation / init (the band competitor model)
// ===========================================================================

TEST_F(ModesMutant, lazy_init_activates_ffa_with_defaults)
{
    MutantRig rig;
    ASSERT_FALSE(rig.fx.world().mode.active);
    rig.fx.tick(1);

    EXPECT_TRUE(rig.fx.world().mode.active);
    EXPECT_TRUE(rig.active());
    EXPECT_EQ(kPhaseFfa, rig.phase());
    EXPECT_EQ(4, rig.fx.var(kMutSlotFighterCount))
        << "four deployed characters, no bot fill needed";
    EXPECT_EQ(4, bitmap_popcount(rig.bitmap()));
    EXPECT_EQ(10, rig.fx.var(kMutSlotScoreLimit));
    EXPECT_EQ(7200, rig.fx.var(kMutSlotTimeLimit));
    EXPECT_EQ(60, rig.fx.var(kMutSlotRespawnTicks))
        << "the Mutant default delay is 60, not the engine's 120";
    EXPECT_EQ(0, rig.mutant_id());
    EXPECT_EQ(0, rig.mutant_team());
    EXPECT_STREQ("MUTANT", rig.fx.world().mode.name.data());
    EXPECT_TRUE(has_notification(rig.fx.events, "MUTANT! FIRST TO 10"));

    // Every deployed character is a registered band competitor on its own
    // byte (exact set: four distinct bytes, id slots agree with the bitmap).
    std::set<int> bytes;
    for (walker* w : {rig.a, rig.b, rig.c, rig.d})
    {
        const int slot = rig.slot_of(w);
        ASSERT_NE(-1, slot) << "every deployed character takes a slot";
        EXPECT_EQ(kBandBase + slot, w->team_num())
            << "slot " << slot << " wears its own byte";
        bytes.insert(w->team_num());
    }
    EXPECT_EQ(4u, bytes.size()) << "four DISTINCT band bytes";
    for (int c = 0; c < kBandCount; ++c)
    {
        const bool bit = ((rig.bitmap() >> c) & 1) != 0;
        EXPECT_EQ(bit, rig.slot_id(c) != 0)
            << "bitmap bit and id slot agree at " << c;
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, shipped_manifest_registration_binds_level_840)
{
    MutantRig rig(840);
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active())
        << "scripts/mode_mutant.lua must bind manifest id 840";
    EXPECT_EQ(10, rig.fx.var(kMutSlotScoreLimit)) << "manifest score_limit";
    EXPECT_EQ(7200, rig.fx.var(kMutSlotTimeLimit)) << "manifest time_limit";
    EXPECT_EQ(4, rig.fx.var(kMutSlotFighterCount))
        << "manifest rows 840-843 carry fighters = 4 (pre-conversion feel)";
}

// #241: the lobby TIME LIMIT knob beats the manifest row.
TEST_F(ModesMutant, time_limit_knob_overrides_the_manifest_row)
{
    MutantRig rig(840);
    rig.fx.world().ctf_requested_time_limit = 3600;
    rig.fx.tick(1);

    ASSERT_TRUE(rig.active());
    EXPECT_EQ(3600, rig.fx.var(kMutSlotTimeLimit))
        << "the request beats the row's 7200";
    EXPECT_EQ(10, rig.fx.var(kMutSlotScoreLimit))
        << "and moves no other knob";
}

// The #187 acceptance test: two humans seated on one lobby team become
// separate band competitors and the sim's team-equality hostility rule
// makes them fight each other — first blood between them crowns.
TEST_F(ModesMutant, issue_187_two_humans_on_one_lobby_team_fight_each_other)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    walker* h1 = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    walker* h2 = fx.spawn_hero(FAMILY_SOLDIER, 0, 216, 200, 2);
    ASSERT_NE(nullptr, h1);
    ASSERT_NE(nullptr, h2);
    h1->set_user(0);
    h2->set_user(1);
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));

    ASSERT_GE(h1->team_num(), kBandBase) << "human 1 reseats onto the band";
    ASSERT_GE(h2->team_num(), kBandBase) << "human 2 reseats onto the band";
    EXPECT_NE(h1->team_num(), h2->team_num())
        << "one lobby team, two mutually hostile competitors (#187)";

    // The hit LANDS (is_friendly is team-byte equality, so the reseat is
    // the whole mechanism) and first blood between the two seatmates
    // crowns the killer.
    ASSERT_NE(nullptr, h2->stats());
    h2->stats()->set_hitpoints(100.0f);
    h1->attack(h2);
    EXPECT_LT(h2->stats()->hitpoints(), 100.0f)
        << "seatmates must be able to damage each other";
    h2->stats()->set_hitpoints(1.0f);
    h1->attack(h2);
    ASSERT_TRUE(h2->dead());
    EXPECT_EQ(kPhaseMutant, fx.var(kMutSlotPhase))
        << "first blood between seatmates crowns";
    EXPECT_EQ(static_cast<std::int32_t>(h1->entity_id()),
              fx.var(kMutSlotMutantEntity));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, sixteen_deployed_competitors_fill_the_band)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    std::vector<walker*> heroes;
    for (int i = 0; i < 16; ++i)
    {
        walker* h = fx.spawn_hero(FAMILY_SOLDIER, i % 4, 120 + 24 * i, 200,
                                  100 + i);
        ASSERT_NE(nullptr, h);
        heroes.push_back(h);
    }
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));

    EXPECT_EQ(16, fx.var(kMutSlotFighterCount)) << "the cap-16 band fills";
    EXPECT_EQ(16, bitmap_popcount(fx.var(kMutSlotBandBitmap)));
    EXPECT_EQ(16, alive_band_livings(fx.world()));
    std::set<int> bytes;
    for (walker* h : heroes)
    {
        ASSERT_GE(h->team_num(), kBandBase);
        ASSERT_LT(h->team_num(), kBandBase + kBandCount);
        bytes.insert(h->team_num());
    }
    EXPECT_EQ(16u, bytes.size()) << "sixteen DISTINCT band bytes";

    // The phase machine spans the whole band: first blood anywhere crowns.
    ASSERT_NE(nullptr, heroes[1]->stats());
    heroes[1]->stats()->set_hitpoints(1.0f);
    heroes[0]->attack(heroes[1]);
    ASSERT_TRUE(heroes[1]->dead());
    EXPECT_EQ(kPhaseMutant, fx.var(kMutSlotPhase));
    EXPECT_EQ(heroes[0]->team_num(), fx.var(kMutSlotMutantTeam));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, bot_fill_reaches_the_default_fighter_count)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    // E5: the band fill needs a turned wheel now — the stored 0 is NONE
    // (E1) and an all-NONE empty band refuses instead of filling.
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;
    fx.tick(1);

    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    EXPECT_EQ(4, fx.var(kMutSlotFighterCount))
        << "no deployed characters: bots fill to the default 4";
    EXPECT_EQ(4, alive_band_livings(fx.world()));
    int bots = 0;
    for (const auto& uptr : fx.world().oblist)
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
    EXPECT_EQ(4, bots);
}

TEST_F(ModesMutant, authored_troops_always_strip_and_wildlife_stays)
{
    // The band conversion strips the whole authored score-range cast
    // regardless of the lobby strip request (deployed rosters ARE the
    // match); wildlife bytes 4-7 stay as arena identity.
    MutantRig rig;
    walker* troop = rig.fx.spawn_living(FAMILY_ORC, 1, 520, 200);
    walker* gen = rig.fx.spawn_generator(FAMILY_TENT, 2, 432, 432);
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 700);
    ASSERT_NE(nullptr, troop);
    ASSERT_NE(nullptr, gen);
    ASSERT_NE(nullptr, wildlife);
    // A death BEFORE the lazy init: the hook is inert while MODE_ID is 0.
    walker* early = rig.fx.spawn_living(FAMILY_ORC, 1, 500, 300);
    ASSERT_NE(nullptr, early);
    early->set_dead(1);
    early->death();
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_TRUE(troop->dead()) << "authored score-range livings retire";
    EXPECT_TRUE(gen->dead()) << "score-range generators retire";
    EXPECT_FALSE(wildlife->dead()) << "wildlife (bytes 4-7) is arena identity";
    EXPECT_FALSE(rig.a->dead()) << "roster walkers are never stripped";
    EXPECT_EQ(1, alive_on_team(rig.fx.world(), 5));
    EXPECT_EQ(4, rig.fx.var(kMutSlotFighterCount))
        << "the stripped troop is not a competitor";

    // A generator death mid-match is a non-Living event for the ledger.
    walker* wild_gen = rig.fx.spawn_generator(FAMILY_TENT, 5, 500, 500);
    ASSERT_NE(nullptr, wild_gen);
    wild_gen->set_dead(1);
    wild_gen->death();
    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.score(c)) << "slot " << c;
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, band_fill_solves_without_touching_the_matched_seam)
{
    // The team modes' plan packing (PLAN_BASE) indexes score teams, which
    // band bytes overflow — so the conversion's bot singles solve against
    // the weakest deployed fighter (B3's band spelling) WITHOUT banking
    // anything in the shared MATCHED slots: no target, no plan, no
    // announce, no facts.
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: turned wheel
    walker* hero = fx.spawn_hero(FAMILY_SOLDIER, 0, 200, 200, 1);
    ASSERT_NE(nullptr, hero);
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));

    EXPECT_EQ(4, fx.var(kMutSlotFighterCount)) << "hero + 3 fill bots";
    EXPECT_FALSE(hero->dead());
    EXPECT_GE(hero->team_num(), kBandBase);
    EXPECT_EQ(0, fx.var(kSlotMatchedTarget))
        << "the band fill never banks a matched target";
    EXPECT_EQ(0, fx.var(kSlotMatchedPlan)) << "no plan is ever packed";
    EXPECT_EQ(0, fx.var(kSlotMatchedAnnounced))
        << "no announce latch, no banked facts";
    EXPECT_EQ(0, count_notifications(fx.events, "TEAMS MATCHED"));
    for (const auto& uptr : fx.world().oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr || w->stats() == nullptr)
            continue;
        EXPECT_EQ(1, w->stats()->level())
            << "each single solves against the fresh hero's f (a fresh "
               "soldier prices closest to the L1 rung of its own family)";
    }
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, slot_map_migration_pins)
{
    // The var namespace after the band conversion: SCORE moved from
    // 16+team (4 slots) to 16+color_index (16 slots), IDS at 32..47,
    // the bitmap at 48, MUTANT_TEAM (14) stores the band byte itself.
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    EXPECT_EQ(4, rig.fx.world().mode.vars[8]) << "FIGHTER_COUNT at slot 8";
    EXPECT_EQ(4, bitmap_popcount(rig.fx.world().mode.vars[48]))
        << "BAND_BITMAP at slot 48";
    int occupied = 0;
    for (int c = 0; c < kBandCount; ++c)
    {
        const std::int32_t id =
            rig.fx.world().mode.vars[static_cast<std::size_t>(32 + c)];
        if (id == 0)
            continue;
        occupied++;
        walker* w = rig.fx.world().find_by_id(static_cast<std::uint32_t>(id));
        ASSERT_NE(nullptr, w) << "IDS slot " << c << " resolves";
        EXPECT_EQ(kBandBase + c, w->team_num());
    }
    EXPECT_EQ(4, occupied) << "IDS range 32..47 holds the competitors";

    // A mutant kill writes 16 + color index, and MUTANT_TEAM carries the
    // crowned competitor's band byte (int32 fits the full byte).
    rig.crown_a();
    const int a_slot = rig.slot_of(rig.a);
    EXPECT_EQ(kBandBase + a_slot, rig.fx.world().mode.vars[14])
        << "MUTANT_TEAM at slot 14 stores the band byte";
    rig.slay(rig.a, rig.c);
    EXPECT_EQ(1, rig.fx.world().mode.vars[static_cast<std::size_t>(16 + a_slot)])
        << "SCORE range 16..31 is keyed by color index";
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
    const int a_slot = rig.slot_of(rig.a);
    EXPECT_EQ(kBandBase + a_slot, rig.mutant_team())
        << "MUTANT_TEAM stores the assigned band byte";
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
    const std::string crowned =
        std::string(og::sim::team_color_name(kBandBase + a_slot)) +
        " IS THE MUTANT!";
    EXPECT_TRUE(has_notification(rig.fx.events, crowned))
        << "expected \"" << crowned << "\" (band color name)";
    EXPECT_EQ(0, rig.score(a_slot)) << "first blood itself scores nothing";
}

TEST_F(ModesMutant, crown_beacon_carries_a_band_byte)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    const int a_slot = rig.slot_of(rig.a);

    EXPECT_EQ(static_cast<std::int32_t>(rig.a->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id);
    EXPECT_EQ(kBandBase + a_slot,
              static_cast<int>(rig.fx.world().mode.beacons[0].team))
        << "the crown beacon rides the widened og.set_beacon with the "
           "band byte";

    // The per-tick re-assert tints from MUTANT_TEAM, never the worn byte:
    // berserk-charm residue parks the mutant on a low byte the widened
    // guard would refuse, and the beacon must not flicker off the band.
    rig.a->set_team_num(3);
    rig.fx.tick(1);
    EXPECT_EQ(kBandBase + a_slot,
              static_cast<int>(rig.fx.world().mode.beacons[0].team))
        << "upkeep keeps the ASSIGNED byte while the walker wears residue";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, ffa_environment_summon_and_unregistered_deaths_crown_nobody)
{
    // Suicide is the environment shape by construction: the engine's
    // root-team gate means self-damage never lands, so a self-inflicted
    // death always arrives with a nil killer.
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    // Environment: no attacker stamp.
    rig.b->set_dead(1);
    rig.b->death();
    EXPECT_EQ(kPhaseFfa, rig.phase());

    // A summon victim decides nothing even with a live killer.
    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, rig.c->team_num(),
                                      232, 232);
    ASSERT_NE(nullptr, pet);
    pet->set_owner(rig.c);
    rig.slay(rig.a, pet);
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "owned victims never crown";

    // A wildlife killer (a stamp outside the band) decides nothing.
    walker* wolf = rig.fx.spawn_living(FAMILY_ORC, 5, rig.c->xpos() + 32,
                                       rig.c->ypos());
    ASSERT_NE(nullptr, wolf);
    rig.slay(wolf, rig.c);
    EXPECT_EQ(kPhaseFfa, rig.phase()) << "out-of-band stamps crown nobody";

    // An UNREGISTERED killer wearing a registered byte (the charmed-
    // wildlife shape) cannot take the crown — only competitors mutate.
    walker* stray = rig.fx.spawn_living(FAMILY_ORC, 0, rig.d->xpos() + 32,
                                        rig.d->ypos());
    ASSERT_NE(nullptr, stray);
    stray->set_team_num(rig.a->team_num());
    rig.slay(stray, rig.d);
    EXPECT_EQ(kPhaseFfa, rig.phase())
        << "a killer with no IDS slot crowns nobody";

    // A mutual kill: the killer fell with its victim, so nobody can wear
    // the crown.
    ASSERT_NE(nullptr, rig.a->stats());
    rig.a->stats()->set_hitpoints(100.0f);
    walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 0, 300, 300, 77);
    ASSERT_NE(nullptr, joiner);
    rig.fx.tick(1);
    ASSERT_NE(-1, rig.slot_of(joiner));
    rig.a->stats()->set_hitpoints(100.0f);
    joiner->attack(rig.a);  // stamps the joiner's band byte
    ASSERT_LT(rig.a->stats()->hitpoints(), 100.0f);
    joiner->set_dead(1);  // the killer falls with its victim
    rig.a->set_dead(1);
    rig.a->death();
    EXPECT_EQ(kPhaseFfa, rig.phase())
        << "a dead killer cannot wear the crown";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

TEST_F(ModesMutant, killing_the_mutant_transfers_crown_scores_and_heals)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    rig.crown_a();
    ASSERT_NE(nullptr, rig.c->stats());
    const int c_slot = rig.slot_of(rig.c);
    const float c_base_damage = rig.c->damage();
    rig.c->stats()->set_hitpoints(10.0f);
    const float a_base =
        static_cast<float>(rig.fx.var(kMutSlotMutantBaseDamage)) / 256.0f;

    rig.slay(rig.c, rig.a);

    EXPECT_EQ(kPhaseMutant, rig.phase());
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()), rig.mutant_id())
        << "mutancy transfers to the killer";
    EXPECT_EQ(kBandBase + c_slot, rig.mutant_team());
    EXPECT_EQ(1, rig.score(c_slot)) << "killing the Mutant scores 1";
    EXPECT_EQ(a_base, rig.a->damage())
        << "the old mutant's corpse gets its base damage back";
    EXPECT_FALSE(rig.a->stats()->query_bit_flags(kMutantBit));
    EXPECT_TRUE(rig.c->stats()->query_bit_flags(kMutantBit));
    EXPECT_EQ(c_base_damage * 2.0f, rig.c->damage());
    EXPECT_EQ(25.0f, rig.c->stats()->hitpoints())
        << "the heir heals kill_heal (15) clamped";
    EXPECT_EQ(static_cast<std::int32_t>(rig.c->entity_id()),
              rig.fx.world().mode.beacons[0].entity_id);
    EXPECT_EQ(kBandBase + c_slot,
              static_cast<int>(rig.fx.world().mode.beacons[0].team));
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
    EXPECT_EQ(0, rig.mutant_team());
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
    const int a_slot = rig.slot_of(rig.a);
    rig.a->stats()->set_hitpoints(20.0f);

    rig.slay(rig.a, rig.c);
    EXPECT_EQ(1, rig.score(a_slot)) << "kills AS the Mutant score 1";
    EXPECT_EQ(35.0f, rig.a->stats()->hitpoints())
        << "each kill heals kill_heal (15)";

    // A summon victim scores nothing.
    walker* pet = rig.fx.spawn_living(FAMILY_SKELETON, rig.d->team_num(),
                                      232, 232);
    ASSERT_NE(nullptr, pet);
    pet->set_owner(rig.d);
    rig.slay(rig.a, pet);
    EXPECT_EQ(1, rig.score(a_slot)) << "summon deaths never score";
}

TEST_F(ModesMutant, mutant_phase_deaths_without_the_mutant_score_nothing)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    // A cross-phase stamp: c chips d during FFA, the crown lands, d dies
    // seconds later — the ACCEPTED 48-tick attribution resolves killer c,
    // and under mutant-phase rules a non-mutant killer scores nothing.
    ASSERT_NE(nullptr, rig.d->stats());
    rig.d->stats()->set_hitpoints(100.0f);
    rig.c->attack(rig.d);
    ASSERT_LT(rig.d->stats()->hitpoints(), 100.0f);
    rig.crown_a();
    const int c_slot = rig.slot_of(rig.c);

    rig.d->set_dead(1);
    rig.d->death();
    EXPECT_EQ(0, rig.score(c_slot))
        << "only the Mutant's kills score in the mutant phase";
    EXPECT_EQ(kPhaseMutant, rig.phase()) << "no transfer, no revert";

    // An environment death of a competitor scores nobody either.
    rig.c->set_dead(1);
    rig.c->death();
    for (int c = 0; c < kBandCount; ++c)
        EXPECT_EQ(0, rig.score(c)) << "slot " << c;
    EXPECT_EQ(kPhaseMutant, rig.phase());
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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
    walker* mutant_pet = rig.fx.spawn_living(FAMILY_SKELETON,
                                             rig.a->team_num(), 260, 200);
    ASSERT_NE(nullptr, mutant_pet);
    mutant_pet->set_owner(rig.a);
    rig.c->stats()->set_hitpoints(100.0f);
    mutant_pet->attack(rig.c);
    EXPECT_LT(rig.c->stats()->hitpoints(), 100.0f)
        << "the Mutant's summon inherits its matrix";

    walker* c_pet = rig.fx.spawn_living(FAMILY_SKELETON, rig.c->team_num(),
                                        260, 232);
    ASSERT_NE(nullptr, c_pet);
    c_pet->set_owner(rig.c);
    rig.d->stats()->set_hitpoints(100.0f);
    c_pet->attack(rig.d);
    EXPECT_EQ(100.0f, rig.d->stats()->hitpoints())
        << "a competitor's summon is void against competitors";

    // Arm 5: non-living targets stay damageable — a generator is not a
    // living-vs-living pair, so the gate keeps out of the way.
    walker* wild_gen = rig.fx.spawn_generator(FAMILY_TENT, 5, 500, 500);
    ASSERT_NE(nullptr, wild_gen);
    ASSERT_NE(nullptr, wild_gen->stats());
    const float gen_hp = wild_gen->stats()->hitpoints();
    rig.c->attack(wild_gen);
    EXPECT_LT(wild_gen->stats()->hitpoints(), gen_hp)
        << "structures stay damageable in the mutant phase";
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

    // A cleric bystander and an undead COMPETITOR (deployed hero, adopted
    // onto a free band byte by the mid-join sweep); the crown stays on a.
    walker* cleric = rig.fx.spawn_living(FAMILY_CLERIC, 2, 200, 264);
    walker* undead = rig.fx.spawn_hero(FAMILY_SKELETON, 3, 216, 264, 9);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, undead);
    rig.fx.tick(1);
    ASSERT_NE(-1, rig.slot_of(undead)) << "the undead joiner is adopted";
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

    // The Mutant itself is still a legal turn-undead target: crown the
    // undead competitor and the same cleric may kill it.
    rig.fx.world().mode.vars[kMutSlotMutantEntity] =
        static_cast<std::int32_t>(undead->entity_id());
    rig.fx.world().mode.vars[kMutSlotMutantTeam] = undead->team_num();
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
    const int c_slot = rig.slot_of(rig.c);

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
    EXPECT_EQ(1, rig.score(c_slot));
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
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    // Spawned after init so the strip does not retire the bystander (the
    // spawn also exercises the shipped on_entity_spawn adoption arm — no
    // guy, no adoption).
    walker* mage = rig.fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    ASSERT_NE(nullptr, mage);

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
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* mage = rig.fx.spawn_living(FAMILY_MAGE, 0, 400, 400);
    ASSERT_NE(nullptr, mage);

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
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    walker* skel = rig.fx.spawn_living(FAMILY_SKELETON, 0, 400, 400);
    ASSERT_NE(nullptr, skel);
    rig.fx.tick(10);  // ANI_SKEL_GROW completes
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

TEST_F(ModesMutant, ffa_director_repairs_backstop_wildlife_onto_a_competitor)
{
    // The engine's pre-act backstop (find_far_foe) refills empty foes
    // before the post-act director runs, and it happily hands out
    // wildlife — those broken foes are what the repair arm exists for.
    MutantRig rig(kMutantLevelA, ACT_RANDOM);
    walker* wildlife = rig.fx.spawn_living(FAMILY_ORC, 5, 400, 700);
    ASSERT_NE(nullptr, wildlife);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    for (walker* w : {rig.a, rig.b, rig.c, rig.d})
        w->set_act_type(ACT_SIT);
    wildlife->set_act_type(ACT_SIT);
    rig.a->setxy(200, 200);
    wildlife->setxy(230, 200);  // nearest overall — the backstop's pick
    rig.b->setxy(280, 200);     // nearest COMPETITOR — the repair's pick
    rig.c->setxy(700, 800);
    rig.d->setxy(720, 800);

    rig.a->set_foe(nullptr);
    align_before_cadence(rig.fx.world());
    rig.fx.tick(1);
    EXPECT_EQ(rig.b->entity_id(), rig.a->foe_id())
        << "the wildlife foe the backstop handed out is repaired onto the "
           "nearest COMPETITOR (backstop alone would keep the closer orc)";
}

TEST_F(ModesMutant, hunters_converge_on_the_mutant_and_players_stay_free)
{
    MutantRig rig(kMutantLevelA, ACT_RANDOM);
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    // A mid-join player seat: adopted by the per-tick sweep, never steered
    // (the director skips ACT_CONTROL).
    walker* player = rig.fx.spawn_hero(FAMILY_SOLDIER, 2, 260, 260, 5,
                                       ACT_CONTROL);
    ASSERT_NE(nullptr, player);
    rig.fx.tick(1);
    ASSERT_NE(-1, rig.slot_of(player)) << "the joiner is adopted";
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
// Respawns (rotated pools, band-byte retention, D12 re-assert)
// ===========================================================================

TEST_F(ModesMutant, dead_competitor_revives_on_a_rotated_pool)
{
    MutantRig rig;
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(10, rig.fx.var(kMutSlotRespawnTicks));
    const int byte = rig.b->team_num();

    rig.slay(rig.a, rig.b);
    // Synchronous scheduling in on_entity_death: already queued on the
    // death tick, so respawn_retains_player_control's scripted arm holds.
    EXPECT_TRUE(queue_holds(rig.fx.world(), rig.b))
        << "the corpse is queued before the next tick";
    rig.b->setxy(320, 480);  // park the corpse away from every pool
    rig.fx.tick(12);

    EXPECT_FALSE(rig.b->dead());
    EXPECT_EQ(byte, rig.b->team_num()) << "the competitor revives on its byte";
    EXPECT_EQ(255, rig.b->real_team_num());
    // The revive repositions onto one of the four anchor POOLS (exact
    // anchor when clear, else the deterministic ring fallback within 3
    // tiles = 48 px).
    const int anchors[4][2] = {{96, 96}, {544, 96}, {96, 800}, {544, 800}};
    bool on_a_pool = false;
    for (const auto& anchor : anchors)
    {
        if (std::abs(rig.b->xpos() - anchor[0]) <= 48 &&
            std::abs(rig.b->ypos() - anchor[1]) <= 48)
        {
            on_a_pool = true;
        }
    }
    EXPECT_TRUE(on_a_pool) << "revive landed at (" << rig.b->xpos() << ","
                           << rig.b->ypos() << "), not on a pool";
}

TEST_F(ModesMutant, charmed_death_revive_reasserts_the_slot_byte)
{
    // D12: revive_player_walker clears real_team_num to 255 but restores
    // team only for bytes < 4, so a competitor that dies while charmed
    // would revive wearing the charmer's byte forever — on_respawn
    // re-asserts the slot's assigned byte.
    MutantRig rig;
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int b_slot = rig.slot_of(rig.b);

    // b dies while charmed onto c's byte.
    rig.b->set_real_team_num(static_cast<unsigned char>(kBandBase + b_slot));
    rig.b->set_team_num(rig.c->team_num());
    rig.slay(rig.a, rig.b);
    rig.fx.tick(12);

    EXPECT_FALSE(rig.b->dead());
    EXPECT_EQ(kBandBase + b_slot, rig.b->team_num())
        << "on_respawn re-asserts the assigned byte";
    EXPECT_EQ(255, rig.b->real_team_num());
}

TEST_F(ModesMutant, silent_corpse_backstop_schedules_and_revives)
{
    MutantRig rig;
    rig.fx.world().ctf_requested_respawn_ticks = 10;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());

    rig.b->set_dead(1);  // bypasses walker::death and the death hook
    rig.fx.tick(1);
    EXPECT_TRUE(queue_holds(rig.fx.world(), rig.b))
        << "the per-tick backstop schedules silent corpses";
    rig.fx.tick(11);
    EXPECT_FALSE(rig.b->dead());
}

// ===========================================================================
// Mid-join adoption
// ===========================================================================

TEST_F(ModesMutant, midjoin_hero_adopts_a_free_band_byte)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    ASSERT_EQ(4, bitmap_popcount(rig.bitmap()));

    walker* joiner = rig.fx.spawn_hero(FAMILY_SOLDIER, 1, 300, 300, 500);
    ASSERT_NE(nullptr, joiner);
    rig.fx.tick(1);

    const int slot = rig.slot_of(joiner);
    ASSERT_NE(-1, slot) << "the joiner is registered";
    EXPECT_EQ(kBandBase + slot, joiner->team_num());
    EXPECT_EQ(0, rig.score(slot));
    EXPECT_EQ(5, bitmap_popcount(rig.bitmap()));
    EXPECT_EQ(5, rig.fx.var(kMutSlotFighterCount));
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
}

// ===========================================================================
// Win / timeout / HUD
// ===========================================================================

TEST_F(ModesMutant, score_limit_win_and_timeout_lowest_index_tiebreak)
{
    // Score limit first: the winner byte is the competitor's band byte
    // through the widened og.declare_winner, and a live myguy on that
    // byte is a player win (next_level advances).
    {
        MutantRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        const int c_slot = rig.slot_of(rig.c);
        rig.set_score(c_slot, 10);
        rig.fx.tick(1);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(kBandBase + c_slot, rig.fx.world().mode.winner_team)
            << "og.declare_winner carries the band byte";
        EXPECT_TRUE(rig.fx.world().mode.winner_is_player);
        EXPECT_EQ(kMutantLevelA + 1, rig.fx.world().next_level)
            << "player win advances";
        const std::string wins =
            std::string(og::sim::team_color_name(kBandBase + c_slot)) +
            " TEAM WINS!";
        EXPECT_TRUE(has_notification(rig.fx.events, wins))
            << "expected \"" << wins << "\"";
    }
    // Timeout leader; an all-square tie resolves to the lowest color
    // index. (The old model's m_score middle rung is gone: band
    // competitors never touch the 4-wide m_score.)
    {
        MutantRig rig;
        rig.fx.tick(1);
        ASSERT_TRUE(rig.active());
        const int c1 = rig.occupied_slot();
        const int c2 = rig.occupied_slot(c1 + 1);
        ASSERT_NE(-1, c1);
        ASSERT_NE(-1, c2);
        rig.set_score(c1, 4);
        rig.set_score(c2, 4);
        rig.fx.world().set_level_tick_count(7200 - 2);
        rig.fx.tick(2);
        EXPECT_TRUE(rig.fx.world().game_ended);
        EXPECT_EQ(kBandBase + c1, rig.fx.world().mode.winner_team)
            << "leading score wins; the tie resolves to the lowest index";
    }
}

TEST_F(ModesMutant, bot_only_win_is_rematch_shape)
{
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: bot band
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    int winner_slot = -1;
    for (int c = 0; c < kBandCount; ++c)
    {
        if (fx.world().mode.vars[static_cast<std::size_t>(kMutSlotIds + c)] !=
            0)
        {
            winner_slot = c;
            break;
        }
    }
    ASSERT_NE(-1, winner_slot);

    fx.world().mode.vars[static_cast<std::size_t>(kMutSlotScore +
                                                  winner_slot)] = 10;
    fx.tick(1);
    EXPECT_TRUE(fx.world().game_ended);
    EXPECT_EQ(kBandBase + winner_slot, fx.world().mode.winner_team);
    EXPECT_FALSE(fx.world().mode.winner_is_player);
    EXPECT_EQ(kMutantLevelA, fx.world().next_level)
        << "bot winners: rematch shape";
}

TEST_F(ModesMutant, hud_carries_leader_and_runnerup_and_stars_the_mutant)
{
    MutantRig rig;
    rig.fx.tick(1);
    ASSERT_TRUE(rig.active());
    const int c1 = rig.occupied_slot();
    const int c2 = rig.occupied_slot(c1 + 1);
    ASSERT_NE(-1, c1);
    ASSERT_NE(-1, c2);

    // All-square standings: leader = lowest occupied index, runner-up next.
    const std::string first =
        std::string(og::sim::team_color_name(kBandBase + c1)) + " 0/10";
    const std::string second =
        std::string(og::sim::team_color_name(kBandBase + c2)) + " 0/10";
    EXPECT_STREQ(first.c_str(), rig.fx.world().mode.hud[0].text.data());
    EXPECT_EQ(kBandBase + c1,
              static_cast<int>(rig.fx.world().mode.hud[0].team))
        << "the leader row is tinted with the band byte";
    EXPECT_STREQ(second.c_str(), rig.fx.world().mode.hud[1].text.data());
    EXPECT_EQ(kBandBase + c2,
              static_cast<int>(rig.fx.world().mode.hud[1].team));

    // The crown wearer's row is starred once it leads.
    rig.crown_a();
    const int a_slot = rig.slot_of(rig.a);
    rig.set_score(a_slot, 1);
    rig.fx.tick(1);
    const std::string starred =
        std::string(og::sim::team_color_name(kBandBase + a_slot)) +
        " 1/10 *";
    EXPECT_STREQ(starred.c_str(), rig.fx.world().mode.hud[0].text.data())
        << "the crown wearer's line is starred";
    EXPECT_EQ(kBandBase + a_slot,
              static_cast<int>(rig.fx.world().mode.hud[0].team));

    // A later color index overtaking the runner-up (but not the leader)
    // displaces second place in the standings.
    const int s3 = rig.occupied_slot(c2 + 1);
    ASSERT_NE(-1, s3);
    rig.set_score(c1, 5);
    rig.set_score(c2, 0);
    rig.set_score(s3, 2);
    rig.set_score(a_slot, a_slot == c1 ? 5 : (a_slot == s3 ? 2 : 0));
    rig.fx.tick(1);
    EXPECT_EQ(kBandBase + c1,
              static_cast<int>(rig.fx.world().mode.hud[0].team));
    EXPECT_EQ(kBandBase + s3,
              static_cast<int>(rig.fx.world().mode.hud[1].team))
        << "the later index displaces the runner-up";
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
    fx.world().ctf_requested_fill[0] = og::sim::kFillFair;  // E5: bot band
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
    BudgetOverride budget(500000);
    // The busy world: a full 16-competitor band brawling under the
    // reduced budget (the director iterates every color slot).
    ModesCtfWorld fx(kMutantLevelA);
    fx.spawn_anchor(0, 96, 96);
    fx.spawn_anchor(1, 544, 96);
    fx.spawn_anchor(2, 96, 800);
    fx.spawn_anchor(3, 544, 800);
    std::vector<walker*> heroes;
    for (int i = 0; i < 16; ++i)
    {
        walker* h = fx.spawn_hero(FAMILY_SOLDIER, i % 4, 120 + 24 * i, 200,
                                  100 + i, ACT_RANDOM);
        ASSERT_NE(nullptr, h);
        heroes.push_back(h);
    }
    fx.world().ctf_requested_respawn_ticks = 30;
    fx.tick(1);
    ASSERT_EQ(kModeIdMutant, fx.var(kMutSlotModeId));
    ASSERT_EQ(16, fx.var(kMutSlotFighterCount));
    ASSERT_NE(nullptr, heroes[1]->stats());
    heroes[1]->stats()->set_hitpoints(1.0f);
    heroes[0]->attack(heroes[1]);
    ASSERT_EQ(kPhaseMutant, fx.var(kMutSlotPhase));
    fx.tick(45);  // 3 director cadences + upkeep + win/HUD phases
    EXPECT_FALSE(has_script_error(fx.world(), "instruction budget"))
        << "a 10x-reduced budget must never trip";
    EXPECT_EQ(0u, og::script::hooks::hook_failures().count);
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
        EXPECT_EQ(rig.fx.world().mode.vars[static_cast<std::size_t>(slot)],
                  mirror.world().mode.vars[static_cast<std::size_t>(slot)])
            << "mode var slot " << slot;
    }
}
