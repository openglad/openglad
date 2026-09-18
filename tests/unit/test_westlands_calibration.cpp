/* F4 fresh-team calibration pins for War of the Westlands
 * (scratchpad westlands/levels/campaign_meta.md, "F4 calibration table").
 *
 * The campaign contract: a fresh team at the campaign_meta difficulty-curve
 * crew power can reasonably attempt every level. The full contract was
 * swept with the playtest harness (scripts/westlands_playtest.sh, crew
 * brackets {curve-1, curve, curve+1} x 3 seeds x 2 rosters, 6000-tick
 * runs); CI cannot afford that, so this test pins the CHEAP invariant per
 * level: the 8-unit mixed fresh team (4 soldiers, elf, archer, cleric,
 * barbarian — the harness's representative full-lobby roster; the 4-soldier
 * stand-in is a pessimistic floor that bottoms out at zero on the war
 * levels and would pin nothing) at the curve level, deployed exactly the
 * way the harness deploys it, still has at least `floor` members alive at
 * tick 600 on the pinned seed. Since the text client's context-before-load
 * fix, this fixture and openglad_text --protocol produce bit-identical
 * simulations, so these floors hold for the harness verbatim.
 *
 * Floors are the MINIMUM measured across seeds {42, 1337, 2025} at the
 * time of calibration (the test runs seed 42 only; the min-across-seeds
 * floor absorbs future engine-side drift without going stale on the first
 * cosmetic change). A failure here means a builder or engine change made
 * some level MEANINGFULLY hotter for a fresh team — re-run the F4 bracket
 * sweeps and re-calibrate deliberately instead of bumping the number.
 *
 * Measurement mode: set WESTLANDS_CALIBRATION_MEASURE=1 to print the
 * per-seed survivor counts for all three seeds instead of asserting, plus
 * the seed-42 battle line (alive / host / damage taken / damage dealt) the
 * two-sided pins below are cut from.
 *
 * Two-sidedness (2026-09-13): a survival FLOOR alone cannot notice a level
 * that stopped biting, so every row also carries a hot-side `ceiling` (the
 * seed-42 measure plus two, capped at 7, on the war/ambush rows whose floor
 * is 0) and every row pins that the fight actually happened — the level
 * fields an army, the crew took damage, and the crew dealt damage. A damage
 * sink that stops landing on the crew, or foes that never engage, reds every
 * row instead of quietly turning the campaign into a walk.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "westlands_sim_fixture.h"

#include <cstdio>
#include <cstdlib>

namespace {

using westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::WestlandsCampaignTest;
using westlands_fixture::deploy_crew;

struct CurvePin
{
    int level_id;
    int crew_level; // campaign_meta curve, upper value of ranges
    int floor;      // min 8-mixed-crew survivors at tick 600 across 3 seeds
    int ceiling;    // max seed-42 survivors (8 = no ceiling); see kCurve
};

// What the 600 ticks actually DID, beside the survivor count: a floor alone
// is one-sided, so an engine change that stopped the levels from biting
// (damage clamped, foes never engaging) would leave every floor green while
// the campaign turned trivial. These are the two-sided halves.
struct BattleResult
{
    int alive = -1;          // crew members still standing at tick 600
    float crew_taken = 0.0f; // hit points the crew lost to the level
    float crew_dealt = 0.0f; // hit points the crew took off the level
    int host_alive = 0;      // non-crew livings the level fields at deploy
};

// The difficulty-curve contract (campaign_meta.md) with the F4-measured
// 600-tick survival floors and (2026-09-13) their hot-side ceilings.
// 18 is the unused act gap. Ceilings: 8 means "no ceiling" (the row's own
// floor is high enough to be two-sided already); every 0-floor war row
// carries min(7, seed-42 measure + 2), measured on the same package as the
// floors (seed-42 survivors: 7:0 8:0 9:1 10:0 12:0 13:1 14:0 15:3 20:0
// 22:1).
// (Measured 2026-07-08 on the F4-calibrated package. Floors of 0 mark the
// war/ambush levels where placed allies and delayed waves carry the fight
// and the stand-in crew's own 600-tick survival is not the contract —
// their gates live in the harness sweeps, not here.)
// (Recalibrated 2026-07-10 for the B1/B2 stair fixes — the re-trigger
// latch + blocked-arrival nudge, docs/z-axis-design.md: enemies that used
// to bounce back down 6 ticks after climbing now COMMIT to floor
// crossings, so the multi-floor levels 6, 7, 15 and 17 got genuinely
// hotter for a fresh crew. New floors are the re-measured minima across
// seeds {42, 1337, 2025}; every single-floor level re-measured identical,
// as the floor_count>1 gate guarantees. The F4 win/hold contract on the
// hotter levels should be re-swept with scripts/westlands_playtest.sh
// before the next balance pass.)
// (Re-pinned 2026-07 for the content batch that scaled the Ford (scen3,
// 19 -> 39 foes) and Refuge (scen4, 23 -> 38 foes) waves up to the
// briefings' flood, with the matching defense levers — the Ford's ally
// camp 6 -> 7, the Refuge's porch ward + lvl-3 garden muster: the mixed
// curve crew re-measured 8/8/8 and 7/8/8 — the act-1 ally rosters absorb
// the first two beats, which is exactly why the waves had to grow.
// Minima across the same three seeds; every other level re-measured
// within its existing floor, including the war levels 13/15/16/17 whose
// new NAMED captains rename existing posts and are sim-inert by
// construction.)
// (Recalibrated 2026-07-10 for the forest-pathing wave — the single-floor
// un-gating of the A* no-corner-cut + follow-path alignment assist, the
// shove command-theft probe, and the guard facing gate (see
// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): enemy companies that used to wedge
// on convex corners, X-pinches and allied columns now actually arrive and
// fight, so the army-heavy levels got genuinely hotter for the pessimistic
// brawler stand-in crew (which cannot kite, heal, or exit). Re-measured
// minima across seeds {42, 1337, 2025}: 3: 8->7, 7: 1->0, 8: 4->0,
// 15: 1->0, 17: 4->2, 19: 6->5, 22: 8->5, 23: 7->5, 24: 4->2, 25: 8->7;
// levels 7/8/15 join the 0-floor war class where placed allies and waves
// carry the fight. The Forest Road (scen2) re-measured 8/8/8 with its
// RCA-4.5 content fix (flush bend-1 SE junction, first shadow wolf at
// tick 400). The F4 win/hold contract on the hotter levels should be
// re-swept with scripts/westlands_playtest.sh before the next balance
// pass.)
// (Recalibrated 2026-07-11 for the guard wake rule (wake-on-sight +
// hold-post policy, docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): allied garrisons
// now provably hold their posts (hold-post bit), which made the escorted
// act-1 beats slightly SAFER (4: 7->8, 6: 6->7), while enemy ambush posts
// spring and hunt once sighted, which made warden-dense levels hotter
// (11: 5->4, 16: 3->2, 25: 7->5) and dropped the two mage-tower levels
// into the 0-floor war class (14: 7->0, 22: 5->0 — a dozen-plus waking
// mage wards converge on the pessimistic brawler crew; playable levels for
// a real crew that kites and heals, but the stand-in cannot). The F4
// win/hold contract on 14 and 22 should be re-swept with
// scripts/westlands_playtest.sh before the next balance pass.)
// (Recalibrated 2026-07-13 for the runaway-effect safeguards
// (docs/runaway-effects-design.md §5): the all-level mechanism fixes
// — the ghost-scare fright MERGE (stacked scares collapse into one
// bounded fright) and the orc-yell stun stack cap (150) — bind in-battle
// on every ghost/orc level, so their chaotic 600-tick outcomes drift by a
// survivor or two in EITHER direction (merged scares keep crews engaged:
// they flee less, so they fight more — and sometimes die more). Deliberate
// re-measure across seeds {42, 1337, 2025}: 3: 7->6 (Last Ford, ghosts +
// orcs) and 19: 5->4 (Dead Marshes, L6-8 ghosts) dipped below their pinned
// minima and are re-pinned to the new minima; 11/17/22/24 churned within
// their existing floors; 13/16/23 moved UP (slack kept, floors not
// raised). Every no-ghost/no-orc level re-measured identical, as the
// below-knee identity construction guarantees. Tower and Long Season
// floors did not move (Long Season scen11's seed-1337 measure healed
// 3->5, back at its pin).)
// (Recalibrated 2026-09-08 for the armor-roll expectation fix,
// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md: damage reduction is the exact
// expectation of the 2002 random(armor) roll instead of the 2013 armor/2
// clamp, and hits round to the nearest point instead of truncating, so
// every hit on BOTH sides lands ~0.5 harder and armored foes stop being
// one-point sponges. Deliberate re-measure across seeds {42, 1337, 2025}:
// 1: 8->7 (7 8 8), 4: 8->7 (7 8 8), 5: 7->6 (7 6 7) and 6: 7->3 (7 7 3 —
// The Hay War's seed-2025 run now loses five of the fresh crew, the
// largest single dip of this pass and a balance signal for the next
// Westlands pass) are re-pinned to the new minima; 3/11/16/17/19/21/23/
// 24/25/26 held or moved up (slack kept, floors not raised); the 0-floor
// levels stayed 0. Tower f5 moved 3->0 in the same pass, see
// test_tower_calibration.cpp.)
constexpr CurvePin kCurve[] = {
    {1, 1, 7, 8},  {2, 2, 8, 8},  {3, 2, 6, 8},  {4, 3, 7, 8},
    {5, 3, 6, 8},  {6, 4, 3, 8},  {7, 4, 0, 2},  {8, 5, 0, 2},
    {9, 6, 0, 3},  {10, 5, 0, 2}, {11, 6, 4, 8}, {12, 6, 0, 2},
    {13, 6, 0, 3}, {14, 7, 0, 2}, {15, 7, 0, 5}, {16, 8, 2, 8},
    {17, 8, 2, 8}, {19, 6, 4, 8}, {20, 7, 0, 2}, {21, 7, 7, 8},
    {22, 8, 0, 3}, {23, 8, 5, 8}, {24, 8, 2, 8}, {25, 9, 5, 8},
    {26, 9, 8, 8},
};

constexpr int kCalibrationTicks = 600;

// Every living that is not on the crew's team: the garrisons and ambush
// posts the level fields against it (asleep wave spawns included).
int host_count(GameWorld& world)
{
    int n = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* const w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() != 0)
            ++n;
    }
    return n;
}

BattleResult battle_at_600(int level_id, int crew_level, std::uint32_t seed)
{
    BattleResult out;
    LoadedWestlandsLevel fx(level_id, seed);
    if (!fx.loaded)
        return out;
    GameWorld& world = fx.world();
    std::vector<walker*> crew = deploy_crew(
        fx.level, world, {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
                          FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER,
                          FAMILY_CLERIC, FAMILY_BARBARIAN},
        crew_level);
    if (crew.size() != 8u)
        return out;
    out.host_alive = host_count(world);
    for (int t = 0; t < kCalibrationTicks; ++t)
        world.tick();
    out.alive = 0;
    for (walker* w : crew)
    {
        if (w == nullptr)
            continue;
        if (!w->dead())
            ++out.alive;
        if (w->myguy != nullptr)
        {
            out.crew_taken += w->myguy->scen_damage_taken;
            out.crew_dealt += w->myguy->scen_damage;
        }
    }
    return out;
}

int survivors_at_600(int level_id, int crew_level, std::uint32_t seed)
{
    return battle_at_600(level_id, crew_level, seed).alive;
}

} // namespace

TEST_F(WestlandsCampaignTest, curve_crew_survival_floors_at_600_ticks)
{
    // Balance floors are measured on the GCC ci-test lane. Under
    // ThreadSanitizer (the one clang lane) the sim's floating-point codegen
    // diverges just enough that these chaotic 600-tick battles drift off the
    // pinned floors — a compiler-determinism property, not a thread-safety
    // one. The suite finds no races (single-threaded sim), so it is skipped
    // there; the ci-test and coverage (GCC) lanes enforce the pins.
    //
    // Also skipped under AddressSanitizer, for BUDGET not correctness: the
    // guard wake rule (2026-07-11) made these battles converge instead of
    // holding posts, and at ASan's ~20x sim cost the two calibration suites
    // pushed the ASan+UBSan job past its 30-minute cap (the lane sat at
    // 28m40s before the wake rule; og_unit_sim was still mid-suite when the
    // runner killed it). The lane's memory-error coverage of the battle sim
    // comes from the rest of og_unit_sim and the integration suites; the
    // pins themselves are GCC contracts already enforced twice elsewhere.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#endif
#endif

    const bool measure =
        std::getenv("WESTLANDS_CALIBRATION_MEASURE") != nullptr;

    for (const CurvePin& pin : kCurve)
    {
        SCOPED_TRACE("scen" + std::to_string(pin.level_id) + " at crew level " +
                     std::to_string(pin.crew_level));
        if (measure)
        {
            std::printf("CALIB level=%d cl=%d survivors@600:", pin.level_id,
                        pin.crew_level);
            for (std::uint32_t seed : {42u, 1337u, 2025u})
                std::printf(" %d", survivors_at_600(pin.level_id,
                                                    pin.crew_level, seed));
            // The two-sided halves, on the asserted seed: the ceiling comes
            // from `alive`, and taken/dealt are what prove the fight happened.
            const BattleResult m =
                battle_at_600(pin.level_id, pin.crew_level, 42u);
            std::printf("  [seed42 alive=%d host=%d taken=%.0f dealt=%.0f]\n",
                        m.alive, m.host_alive, m.crew_taken, m.crew_dealt);
            std::fflush(stdout);
            continue;
        }
        const BattleResult r = battle_at_600(pin.level_id, pin.crew_level, 42u);
        ASSERT_GE(r.alive, 0) << "level failed to load or deploy";
        EXPECT_GE(r.alive, pin.floor)
            << "a curve-level fresh crew fell below its F4 survival floor — "
               "the level got meaningfully hotter; re-run the F4 bracket "
               "sweeps before touching this pin";
        EXPECT_LE(r.alive, pin.ceiling)
            << "a curve-level fresh crew walked a hot level with more of the "
               "eight standing than the band has ever left — the level got "
               "meaningfully COLDER; re-measure with "
               "WESTLANDS_CALIBRATION_MEASURE=1 before touching this pin";
        EXPECT_GT(r.host_alive, 0) << "the level must field an army";
        EXPECT_GT(r.crew_taken, 0.0f)
            << "the crew came home untouched: the level stopped biting (a "
               "damage sink that never lands, foes that never engage) — no "
               "survival floor can notice that";
        EXPECT_GT(r.crew_dealt, 0.0f)
            << "the crew never landed a hit in 600 ticks: it is not fighting "
               "the level at all";
    }
}
