// Phase 03 — runtime coverage gate.
//
// Runs every scenario in `kScenarios` exactly once via `run_scenario`,
// then collects the union of:
//   * walker.family      (CoverageObservation::walker_families)
//   * effect.family      (CoverageObservation::effect_families)
//   * weapon.family      (CoverageObservation::weapon_families)
//   * treasure.family    (CoverageObservation::treasure_families)
//   * generator.family   (CoverageObservation::generator_families)
//   * event.kind         (CoverageObservation::event_kinds)
//   * spec.exercises bits (per-scenario `Exercises` bag for specials)
//
// Each gate case asserts the corresponding required array from
// `coverage_targets.h` is a subset of the observed union. Failures
// print a structured list of every uncovered target so Phases 04-06
// know exactly what scenarios still need to land.
//
// At Phase 03 completion the gate is expected to FAIL — that is what
// verifier `03b` confirms. Phases 04-06 backfill scenarios until the
// gate passes.
//
// The gate FAILS hard whenever any coverage target is uncovered. The
// user's contract for this work is "every single entity type, special
// ability effect, attack type, and occurrence... no exceptions" — the
// honest signal for that contract is a red gate until Phases 03-06 add
// the scenarios that close the remaining `weapon`, `treasure`, `effect`,
// `event_kind`, and `specials` rows in `coverage_targets.h`. Phase 01
// does NOT soften the gate; it lands the predicate framework so each
// future scenario can declare expected_facts + a discriminating mutation.
//
// Build wiring (since the gate is designed to fail before Phases 03-06):
//   - This file is compiled into a STANDALONE binary,
//     `og_test_parity_coverage_gate`, registered by `CMakeLists.txt`
//     without an `add_test()` call so the default `ctest --preset
//     ci-test` does not run it.
//   - Phase 03's verifier 03b invokes the binary directly:
//       ./build/ci-test/og_test_parity_coverage_gate
//     and inspects the structured per-category "uncovered targets" lists
//     to drive Phase 04-06 scenario backfill.
//   - `og_test_parity` (the suite that DOES run on every CI build)
//     contains only the per-scenario byte / semantic-parity assertions
//     and the framework gtests. Those must always pass; the gate's
//     red-until-backfilled signal is segregated into the audit binary
//     above so CI stays green during the framework phase.
//
// Implementation note: the existing `Parity` GoogleTest suite is
// populated via TEST(Parity, ...) in test_parity_scenarios.cpp. We
// cannot mix TEST() and TEST_F() with the same suite name, so the
// fixture-style "build the observed union once" pattern is replicated
// here by a function-local static lazily populated on first read.

#include "coverage_targets.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ObservedUnion
{
    og::parity::CoverageObservation obs;
    std::uint64_t                    exercises = 0;
};

const ObservedUnion& observed_union()
{
    static const ObservedUnion built = []() {
        ObservedUnion u;
        for (const auto& spec : og::parity::kScenarios)
        {
            const og::parity::RunOutcome out = og::parity::run_scenario(spec);
            const auto& c = out.coverage;
            u.obs.walker_families.insert(c.walker_families.begin(),
                                          c.walker_families.end());
            u.obs.weapon_families.insert(c.weapon_families.begin(),
                                          c.weapon_families.end());
            u.obs.treasure_families.insert(c.treasure_families.begin(),
                                            c.treasure_families.end());
            u.obs.generator_families.insert(c.generator_families.begin(),
                                             c.generator_families.end());
            u.obs.effect_families.insert(c.effect_families.begin(),
                                          c.effect_families.end());
            u.obs.event_kinds.insert(c.event_kinds.begin(),
                                      c.event_kinds.end());
            u.exercises |= static_cast<std::uint64_t>(spec.exercises);
        }
        return u;
    }();
    return built;
}

std::string format_missing(std::string_view label,
                            const std::vector<std::string>& missing)
{
    std::ostringstream os;
    os << "Coverage gate FAILED for " << label
       << " (" << missing.size() << " uncovered):\n";
    for (const auto& m : missing)
        os << "  - " << m << "\n";
    os << "  Add a scenario to tests/parity/scenario_table.h that "
       << "exercises each missing target.";
    return os.str();
}

std::vector<std::string>
missing_families(const std::int32_t* required, std::size_t required_count,
                  const std::set<std::int32_t>& observed)
{
    std::vector<std::string> missing;
    for (std::size_t i = 0; i < required_count; ++i)
    {
        const std::int32_t f = required[i];
        if (observed.count(f) == 0)
            missing.push_back(og::parity::family_symbol(f));
    }
    return missing;
}

std::vector<std::string> missing_event_kinds(const std::set<std::string>& observed)
{
    std::vector<std::string> missing;
    for (const auto& kind : og::parity::kRequiredEventKinds)
        if (observed.count(std::string(kind)) == 0)
            missing.emplace_back(kind);
    return missing;
}

std::vector<std::string> missing_specials(std::uint64_t observed_exercises)
{
    std::vector<std::string> missing;
    for (std::size_t i = 0; i < std::size(og::parity::kRequiredSpecials); ++i)
    {
        const std::uint64_t bit = 1ULL << i;
        if ((observed_exercises & bit) == 0)
        {
            const auto& pair = og::parity::kRequiredSpecials[i];
            std::ostringstream os;
            os << og::parity::family_symbol(pair.first)
               << " special " << static_cast<int>(pair.second)
               << " (Exercises bit " << i << ")";
            missing.push_back(os.str());
        }
    }
    return missing;
}

} // namespace

TEST(Parity, coverage_gate_walker_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredWalkerFamilies,
        std::size(og::parity::kRequiredWalkerFamilies),
        u.obs.walker_families);
    EXPECT_TRUE(missing.empty()) << format_missing("walker families", missing);
}

TEST(Parity, coverage_gate_effect_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredEffectFamilies,
        std::size(og::parity::kRequiredEffectFamilies),
        u.obs.effect_families);
    EXPECT_TRUE(missing.empty()) << format_missing("effect families", missing);
}

TEST(Parity, coverage_gate_weapon_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredWeaponFamilies,
        std::size(og::parity::kRequiredWeaponFamilies),
        u.obs.weapon_families);
    EXPECT_TRUE(missing.empty()) << format_missing("weapon families", missing);
}

TEST(Parity, coverage_gate_treasure_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredTreasureFamilies,
        std::size(og::parity::kRequiredTreasureFamilies),
        u.obs.treasure_families);
    EXPECT_TRUE(missing.empty()) << format_missing("treasure families", missing);
}

TEST(Parity, coverage_gate_event_kinds)
{
    const auto& u = observed_union();
    const auto missing = missing_event_kinds(u.obs.event_kinds);
    EXPECT_TRUE(missing.empty()) << format_missing("event kinds", missing);
}

TEST(Parity, coverage_gate_specials)
{
    const auto& u = observed_union();
    const auto missing = missing_specials(u.exercises);
    EXPECT_TRUE(missing.empty()) << format_missing("specials", missing);
}

// Umbrella case — fails iff any of the per-category gates above would
// fail. Lets `--gtest_filter='Parity.coverage_gate'` give a single
// pass/fail signal for CI dashboards.
TEST(Parity, coverage_gate)
{
    const auto& u = observed_union();
    std::vector<std::string> missing;

    auto append_family = [&](std::string_view label,
                              const std::int32_t* req, std::size_t n,
                              const std::set<std::int32_t>& obs) {
        for (std::size_t i = 0; i < n; ++i)
            if (obs.count(req[i]) == 0)
            {
                std::ostringstream os;
                os << label << ": " << og::parity::family_symbol(req[i]);
                missing.push_back(os.str());
            }
    };
    append_family("walker_family",   og::parity::kRequiredWalkerFamilies,
                  std::size(og::parity::kRequiredWalkerFamilies),
                  u.obs.walker_families);
    append_family("effect_family",   og::parity::kRequiredEffectFamilies,
                  std::size(og::parity::kRequiredEffectFamilies),
                  u.obs.effect_families);
    append_family("weapon_family",   og::parity::kRequiredWeaponFamilies,
                  std::size(og::parity::kRequiredWeaponFamilies),
                  u.obs.weapon_families);
    append_family("treasure_family", og::parity::kRequiredTreasureFamilies,
                  std::size(og::parity::kRequiredTreasureFamilies),
                  u.obs.treasure_families);

    for (const auto& kind : og::parity::kRequiredEventKinds)
        if (u.obs.event_kinds.count(std::string(kind)) == 0)
        {
            std::ostringstream os;
            os << "event_kind: " << kind;
            missing.push_back(os.str());
        }

    for (std::size_t i = 0; i < std::size(og::parity::kRequiredSpecials); ++i)
    {
        const std::uint64_t bit = 1ULL << i;
        if ((u.exercises & bit) == 0)
        {
            const auto& pair = og::parity::kRequiredSpecials[i];
            std::ostringstream os;
            os << "special: " << og::parity::family_symbol(pair.first)
               << " idx " << static_cast<int>(pair.second);
            missing.push_back(os.str());
        }
    }

    EXPECT_TRUE(missing.empty())
        << format_missing("cumulative coverage", missing);
}
