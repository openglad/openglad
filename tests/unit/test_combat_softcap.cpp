/* Unit pins for the runaway-specials soft caps (docs/runaway-effects-design.md
 * §1/§2/§6.1). Every exact value below is a PIN: the §2 policy table is the
 * contract, and the below-knee identity ranges are what keeps the parity
 * goldens byte-identical. Before-values from the legacy linear formulas are
 * in comments.
 */
#include <gtest/gtest.h>

#include <openglad/core/combat_math.h>
#include <openglad/core/irandom.h>

#include <cstdint>
#include <vector>

using namespace og::combat;

namespace {

// Every (knee, ceiling) pair in use (§1 constants block).
struct KneePair {
    const char* name;
    int knee;
    int ceiling;
};

const std::vector<KneePair>& all_pairs()
{
    static const std::vector<KneePair> pairs = {
        {"scare_duration", kScareDurationKnee, kScareDurationCeiling},   // 325/375
        {"bomb_damage", kBombDamageKnee, kBombDamageCeiling},            // 210/300
        {"sprinkle_roll", kSprinkleRollKnee, kSprinkleRollCeiling},      // 79/110
        {"archmage_charm", kCharmKnee, kCharmCeiling},                   // 264/350
        {"thief_charm", kThiefCharmKnee, kThiefCharmCeiling},            // 375/490
        {"druid_faerie_life", kFaerieLifeKnee, kFaerieLifeCeiling},      // 570/800
        {"elemental_life", kElementalLifeKnee, kElementalLifeCeiling},   // 980/1350
        {"image_life", kImageLifeKnee, kImageLifeCeiling},               // 360/520
        {"skeleton_life", kSkeletonLifeKnee, kSkeletonLifeCeiling},      // 645/900
        {"ghost_raise_life", kGhostRaiseLifeKnee, kGhostRaiseLifeCeiling}, // 670/925
    };
    return pairs;
}

// RNG that records every draw bound and returns a scripted value.
class RecordingRandom : public IRandom {
public:
    explicit RecordingRandom(std::uint32_t value) : value_(value) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        bounds.push_back(max_exclusive);
        return (max_exclusive == 0) ? 0 : (value_ % max_exclusive);
    }
    std::vector<std::uint32_t> bounds;

private:
    std::uint32_t value_;
};

// The tabulated spec levels (§2 columns).
constexpr int kLevels[] = {1, 5, 10, 13, 14, 20, 30, 50};

} // namespace

// ---------------------------------------------------------------------------
// soften() properties (§6.1 bullet 1)

TEST(CombatSoftcap, soften_identity_below_knee_exhaustive)
{
    // Identity for raw <= knee, exhaustive over each curve's below-knee range
    // (this is the parity byte-identity mechanism — every below-knee input
    // maps to itself bit-for-bit).
    for (const auto& p : all_pairs()) {
        for (int raw = 0; raw <= p.knee; ++raw) {
            ASSERT_EQ(soften(raw, p.knee, p.ceiling), raw)
                << p.name << " raw=" << raw;
        }
    }
}

TEST(CombatSoftcap, soften_slope_one_at_knee)
{
    // Slope 1 just above the knee: soften(knee+1) == knee+1 for every pair.
    for (const auto& p : all_pairs()) {
        ASSERT_EQ(soften(p.knee + 1, p.knee, p.ceiling), p.knee + 1) << p.name;
    }
}

TEST(CombatSoftcap, soften_non_decreasing_and_bounded)
{
    for (const auto& p : all_pairs()) {
        int prev = -1;
        for (int raw = 0; raw <= 4 * p.ceiling; ++raw) {
            const int v = soften(raw, p.knee, p.ceiling);
            ASSERT_GE(v, prev) << p.name << " raw=" << raw;
            ASSERT_LE(v, p.ceiling) << p.name << " raw=" << raw;
            prev = v;
        }
    }
}

TEST(CombatSoftcap, soften_below_ceiling_for_all_tabulated_levels_through_60)
{
    // Strictly < ceiling for every level <= 60 on every level-driven curve;
    // the asymptote is reached only at absurd inputs (g >= 2*G^2 - G).
    for (int level = 0; level <= 60; ++level) {
        EXPECT_LT(scare_duration(level), kScareDurationCeiling) << level;
        EXPECT_LT(bomb_damage(level), kBombDamageCeiling) << level;
        EXPECT_LT(druid_faerie_lifetime(level), kFaerieLifeCeiling) << level;
        EXPECT_LT(elemental_lifetime(level), kElementalLifeCeiling) << level;
        EXPECT_LT(image_lifetime(level), kImageLifeCeiling) << level;
        EXPECT_LT(skeleton_lifetime(level), kSkeletonLifeCeiling) << level;
        EXPECT_LT(ghost_raise_lifetime(level), kGhostRaiseLifeCeiling) << level;
    }
}

TEST(CombatSoftcap, soften_round_half_up_edge_cases)
{
    // Scare pair (knee 325, G 50): g=150 → g*G/(g+G) = 7500/200 = 37.5
    // exactly — round-half-up gives 38, not 37.
    ASSERT_EQ(soften(475, 325, 375), 363); // truncation would give 362
    // Just below half rounds down: g=48 → 2400/98 = 24.489...
    ASSERT_EQ(soften(373, 325, 375), 349);
    // Bomb pair (knee 210, G 90): g=30 → 2700/120 = 22.5 exactly → 23.
    ASSERT_EQ(soften(240, 210, 300), 233); // truncation would give 232
}

// ---------------------------------------------------------------------------
// Exact §2 tables per wrapper at L1/L5/L10/L13/L14/L20/L30/L50 (§6.1 bullet 2)

TEST(CombatSoftcap, scare_duration_table)
{
    // Before (25*L): 25/125/250/325/350/500/750/1250
    const int expected[] = {25, 125, 250, 325, 342, 364, 370, 372};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(scare_duration(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, scare_radius_table)
{
    // Before (50 + 10*L): 60/100/150/180/190/250/350/550
    const int expected[] = {60, 100, 150, 180, 190, 250, 250, 250};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(scare_radius(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, bomb_damage_table)
{
    // Before (15*(L+1)): 30/90/165/210/225/315/465/765
    const int expected[] = {30, 90, 165, 210, 223, 258, 277, 287};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(bomb_damage(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, yell_radius_table)
{
    // Before (160 + 20*L): 180/260/360/420/440/560/760/1160
    const int expected[] = {180, 260, 360, 420, 420, 420, 420, 420};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(yell_radius(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, glow_bonus_table)
{
    // Before (110*L): 110/550/1100/1430/1540/2200/3300/5500
    // Flat cap, NOT a knee-13 soften: 110*20 == 2200 == cap exactly (the L20
    // weapon_glow_emission golden serializes weapon lifetime per tick).
    const int expected[] = {110, 550, 1100, 1430, 1540, 2200, 2200, 2200};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(glow_bonus(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, druid_faerie_lifetime_table)
{
    // Before (50 + 40*L): 90/250/450/570/610/850/1250/2050
    const int expected[] = {90, 250, 450, 570, 604, 696, 742, 769};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(druid_faerie_lifetime(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, elemental_lifetime_table)
{
    // Before (200 + 60*L): 260/500/800/980/1040/1400/2000/3200
    const int expected[] = {260, 500, 800, 980, 1032, 1177, 1252, 1297};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(elemental_lifetime(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, image_lifetime_table)
{
    // Before (100 + 20*L): 120/200/300/360/380/500/700/1100
    const int expected[] = {120, 200, 300, 360, 378, 435, 469, 492};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(image_lifetime(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, skeleton_lifetime_table)
{
    // Before (125 + 40*L): 165/325/525/645/685/925/1325/2125
    const int expected[] = {165, 325, 525, 645, 680, 778, 830, 863};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(skeleton_lifetime(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

TEST(CombatSoftcap, ghost_raise_lifetime_table)
{
    // Before (150 + 40*L): 190/350/550/670/710/950/1350/2150
    const int expected[] = {190, 350, 550, 670, 705, 803, 855, 888};
    for (size_t i = 0; i < std::size(kLevels); ++i)
        ASSERT_EQ(ghost_raise_lifetime(kLevels[i]), expected[i]) << "L" << kLevels[i];
}

// ---------------------------------------------------------------------------
// Sprinkle roll mapping (§2.6a) + compute_freeze_duration integration

TEST(CombatSoftcap, sprinkle_roll_mapping)
{
    // Spec pins: 79 → 79 (L20 max roll, identity), 99 → 91 (L30 max roll),
    // 139 → 99 (L50 max roll).
    ASSERT_EQ(soften(79, kSprinkleRollKnee, kSprinkleRollCeiling), 79);
    ASSERT_EQ(soften(99, kSprinkleRollKnee, kSprinkleRollCeiling), 91);
    ASSERT_EQ(soften(139, kSprinkleRollKnee, kSprinkleRollCeiling), 99);
    // Exhaustive identity for every roll the L20 con-0 golden can produce.
    for (int roll = 0; roll <= 79; ++roll)
        ASSERT_EQ(soften(roll, kSprinkleRollKnee, kSprinkleRollCeiling), roll);
}

TEST(CombatSoftcap, compute_freeze_duration_draw_bound_unchanged)
{
    // The rng draw keeps its legacy bound 40 + 2L - con/21 at ALL levels —
    // exactly one draw, with the legacy bound, even above the knee.
    {
        RecordingRandom rng(0);
        (void)compute_freeze_duration(20, 0, rng); // L20 con 0 → bound 80
        ASSERT_EQ(rng.bounds.size(), 1u);
        ASSERT_EQ(rng.bounds[0], 80u);
    }
    {
        RecordingRandom rng(0);
        (void)compute_freeze_duration(50, 42, rng); // 40 + 100 - 2 = 138
        ASSERT_EQ(rng.bounds.size(), 1u);
        ASSERT_EQ(rng.bounds[0], 138u);
    }
}

TEST(CombatSoftcap, compute_freeze_duration_identity_below_knee)
{
    // L20 con 0: bound 80 → rolls 0..79 map to themselves (the
    // weapon_sprinkle_emission golden cannot observe the curve at any roll).
    for (std::uint32_t roll = 0; roll <= 79; ++roll) {
        FixedRandom rng(roll);
        ASSERT_EQ(compute_freeze_duration(20, 0, rng),
                  static_cast<std::int32_t>(roll));
    }
}

TEST(CombatSoftcap, compute_freeze_duration_softens_above_knee)
{
    // L30 con 0: bound 100; max roll 99 → 91. Before: 99.
    FixedRandom max_roll(99);
    ASSERT_EQ(compute_freeze_duration(30, 0, max_roll), 91);
    // L50 con 0: bound 140; roll 139 → 99. Before: 139.
    FixedRandom l50(139);
    ASSERT_EQ(compute_freeze_duration(50, 0, l50), 99);
    // Non-positive max_time still returns 0 with NO draw.
    RecordingRandom rng(7);
    ASSERT_EQ(compute_freeze_duration(-30, 900, rng), 0);
    ASSERT_TRUE(rng.bounds.empty());
}

// ---------------------------------------------------------------------------
// Charm knees (§2.9) + compute_charm_duration integration

TEST(CombatSoftcap, archmage_charm_knee_identity_and_tail)
{
    // Golden is diff 9: max result 25 + 179 = 204 → identity.
    ASSERT_EQ(soften(204, kCharmKnee, kCharmCeiling), 204);
    // Knee 264 = exact max result at diff 12 (caster L13 vs L1) → identity.
    ASSERT_EQ(soften(264, kCharmKnee, kCharmCeiling), 264);
    // Above the knee (before → after): d19 404 → 317, d29 604 → 333,
    // d49 1004 → 341.
    ASSERT_EQ(soften(404, kCharmKnee, kCharmCeiling), 317);
    ASSERT_EQ(soften(604, kCharmKnee, kCharmCeiling), 333);
    ASSERT_EQ(soften(1004, kCharmKnee, kCharmCeiling), 341);
}

TEST(CombatSoftcap, thief_charm_knee_identity_and_tail)
{
    // Draw-free 75 + 25*diff: d12 = 375 identity (knee forced by diff-12
    // identity rule).
    ASSERT_EQ(soften(375, kThiefCharmKnee, kThiefCharmCeiling), 375);
    // d14 425 → 410, d19 550 → 444, d29 800 → 466, d49 1300 → 477.
    ASSERT_EQ(soften(425, kThiefCharmKnee, kThiefCharmCeiling), 410);
    ASSERT_EQ(soften(550, kThiefCharmKnee, kThiefCharmCeiling), 444);
    ASSERT_EQ(soften(800, kThiefCharmKnee, kThiefCharmCeiling), 466);
    ASSERT_EQ(soften(1300, kThiefCharmKnee, kThiefCharmCeiling), 477);
}

TEST(CombatSoftcap, compute_charm_duration_draw_bound_unchanged)
{
    // Exactly one draw with the legacy bound 20*diff even above the knee.
    RecordingRandom rng(0);
    (void)compute_charm_duration(19, rng);
    ASSERT_EQ(rng.bounds.size(), 1u);
    ASSERT_EQ(rng.bounds[0], 380u);
    // Non-positive diff draws with bound 0 and returns the legacy 25.
    RecordingRandom neg(7);
    ASSERT_EQ(compute_charm_duration(-3, neg), 25);
    ASSERT_EQ(neg.bounds.size(), 1u);
    ASSERT_EQ(neg.bounds[0], 0u);
}

TEST(CombatSoftcap, compute_charm_duration_identity_below_knee)
{
    // Golden diff 9: every possible roll (0..179 → 25..204) is identity.
    for (std::uint32_t roll = 0; roll <= 179; ++roll) {
        FixedRandom rng(roll);
        ASSERT_EQ(compute_charm_duration(9, rng),
                  25 + static_cast<std::int32_t>(roll));
    }
    // Diff 12 max roll 239 → 264 == knee, still identity.
    FixedRandom d12(239);
    ASSERT_EQ(compute_charm_duration(12, d12), 264);
}

TEST(CombatSoftcap, compute_charm_duration_softens_above_knee)
{
    // Diff 19 max roll 379 → raw 404 → 317. Before: 404.
    FixedRandom d19(379);
    ASSERT_EQ(compute_charm_duration(19, d19), 317);
    // Diff 49 max roll 979 → raw 1004 → 341. Before: 1004.
    FixedRandom d49(979);
    ASSERT_EQ(compute_charm_duration(49, d49), 341);
}

// ---------------------------------------------------------------------------
// Accumulator semantics (§3.1): monotonic caps, never-reduce, immunity discard

TEST(CombatSoftcap, cloak_total_monotonic_cap)
{
    ASSERT_EQ(cloak_total(0, 96), 96);      // golden thief L4 max cast
    ASSERT_EQ(cloak_total(100, 115), 215);  // below cap: plain sum
    ASSERT_EQ(cloak_total(300, 115), 350);  // clamps at kInvisibilityCloakCap
    ASSERT_EQ(cloak_total(350, 115), 350);  // saturated
    // Never-reduce: a potion-granted 450 stays 450 (monotonic cap idiom).
    ASSERT_EQ(cloak_total(450, 115), 450);
    ASSERT_EQ(cloak_total(450, 0), 450);
    // Max single below-knee cast (L13 max = 267) never clamps from empty.
    ASSERT_EQ(cloak_total(0, 267), 267);
}

TEST(CombatSoftcap, stun_total_monotonic_cap_and_immunity)
{
    ASSERT_EQ(stun_total(0, 15), 15);    // orc L1 mean add, fresh victim
    ASSERT_EQ(stun_total(60, 60), 120);  // stacking below the cap
    ASSERT_EQ(stun_total(120, 110), 150); // clamps at kFrozenStunStackCap
    ASSERT_EQ(stun_total(150, 110), 150); // saturated
    // Never-reduce: an existing above-cap value is not sanitized.
    ASSERT_EQ(stun_total(200, 10), 200);
    // Immunity discard: cur_raw < 0 (thaw-immunity phase) returned unchanged.
    ASSERT_EQ(stun_total(-5, 60), -5);
    ASSERT_EQ(stun_total(-12, 1000), -12);
    // Negative adds are treated as 0 (the legacy floor-at-0 lives upstream).
    ASSERT_EQ(stun_total(40, -7), 40);
}

// ---------------------------------------------------------------------------
// Reward guardrail (§6.1 bullet 3): strict value(L30) > value(L14), plus the
// documented flat-cap exemptions asserted as equalities.

TEST(CombatSoftcap, reward_guardrail_L30_beats_L14)
{
    EXPECT_GT(scare_duration(30), scare_duration(14));           // 370 > 342
    EXPECT_GT(bomb_damage(30), bomb_damage(14));                 // 277 > 223
    // Sprinkle max (con 0): max roll = bound-1 = 39 + 2L.
    EXPECT_GT(soften(39 + 2 * 30, kSprinkleRollKnee, kSprinkleRollCeiling),
              soften(39 + 2 * 14, kSprinkleRollKnee, kSprinkleRollCeiling)); // 91 > 67
    // Both charms (max result per diff).
    EXPECT_GT(soften(24 + 20 * 30, kCharmKnee, kCharmCeiling),
              soften(24 + 20 * 14, kCharmKnee, kCharmCeiling)); // 333 > 291
    EXPECT_GT(soften(75 + 25 * 30, kThiefCharmKnee, kThiefCharmCeiling),
              soften(75 + 25 * 14, kThiefCharmKnee, kThiefCharmCeiling)); // 467 > 410
    // All 5 summon lifetimes.
    EXPECT_GT(druid_faerie_lifetime(30), druid_faerie_lifetime(14)); // 742 > 604
    EXPECT_GT(elemental_lifetime(30), elemental_lifetime(14));       // 1252 > 1032
    EXPECT_GT(image_lifetime(30), image_lifetime(14));               // 469 > 378
    EXPECT_GT(skeleton_lifetime(30), skeleton_lifetime(14));         // 830 > 680
    EXPECT_GT(ghost_raise_lifetime(30), ghost_raise_lifetime(14));   // 855 > 705
    // Freeze single-cast-from-empty: min(20 + 11*L, bank cap).
    const auto freeze_cast = [](int level) {
        const int raw = 20 + 11 * level;
        return raw < kEnemyFreezeBankCap ? raw : kEnemyFreezeBankCap;
    };
    EXPECT_GT(freeze_cast(30), freeze_cast(14)); // 300 > 174
    EXPECT_EQ(freeze_cast(20), 240);             // L20 spectacle preserved
}

TEST(CombatSoftcap, reward_guardrail_flat_cap_exemptions)
{
    // Scare radius flat from L20 (50 + 10*20 = 250 == cap).
    ASSERT_EQ(scare_radius(20), kScareRadiusCap);
    ASSERT_EQ(scare_radius(30), scare_radius(20));
    ASSERT_EQ(scare_radius(50), scare_radius(20));
    // Yell radius flat from L13 (160 + 20*13 = 420 == cap).
    ASSERT_EQ(yell_radius(13), kYellRadiusCap);
    ASSERT_EQ(yell_radius(30), yell_radius(13));
    // Glow flat from L20 (110*20 = 2200 == cap).
    ASSERT_EQ(glow_bonus(20), kGlowBonusCap);
    ASSERT_EQ(glow_bonus(50), glow_bonus(20));
    // Freeze single-cast flat from L26 (20 + 11*26 = 306 → 300).
    const auto freeze_cast = [](int level) {
        const int raw = 20 + 11 * level;
        return raw < kEnemyFreezeBankCap ? raw : kEnemyFreezeBankCap;
    };
    ASSERT_EQ(freeze_cast(26), kEnemyFreezeBankCap);
    ASSERT_EQ(freeze_cast(50), freeze_cast(26));
    ASSERT_LT(freeze_cast(25), kEnemyFreezeBankCap); // identity through L25 (295)
}

// ---------------------------------------------------------------------------
// The remaining named caps are compile-time policy pins (§2 items 4/8/12).

TEST(CombatSoftcap, named_cap_constants_pinned)
{
    static_assert(kEnemyFreezeBankCap == 300);
    static_assert(kFrozenStunStackCap == 150);
    static_assert(kInvisibilityCloakCap == 350);
    static_assert(kFreezeThawImmunityTicks == 12);
    static_assert(kSprinkleRefreshOwnerLevel == 21);
    static_assert(kSprinkleRefreshFloor == 10);
    static_assert(kMpPoolDamageCap == 600);
    static_assert(kStarburstAddCap == 40);
    static_assert(kMaceLifeCap == 468);
    static_assert(kShotDrainCap == 50);
    // MP-pool bind points (§2.12): caps bind only far above golden spawn MP.
    ASSERT_EQ((600 - 80) / 2, 260);   // mage/archmage golden mp 600 → pool 260
    ASSERT_LT((600 - 80) / 2, kMpPoolDamageCap);
    ASSERT_EQ((600 - 60) / 15, 36);   // starburst add at golden mp 600
    ASSERT_LT((600 - 60) / 15, kStarburstAddCap);
    ASSERT_EQ(100 + (80 - 2) / 2, 139); // cleric mace at golden mp 80
    ASSERT_LT(100 + (80 - 2) / 2, kMaceLifeCap);
    ASSERT_EQ(600 / 20, 30);          // archmage drain at golden mp 600
    ASSERT_LT(600 / 20, kShotDrainCap);
}
