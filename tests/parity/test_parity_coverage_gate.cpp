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
// Implementation note: the existing `Parity` GoogleTest suite is
// populated via TEST(Parity, ...) in test_parity_scenarios.cpp. We
// cannot mix TEST() and TEST_F() with the same suite name, so the
// fixture-style "build the observed union once" pattern is replicated
// here by a function-local static lazily populated on first read.

#include "coverage_targets.h"
#include "fact_predicate.h"
#include "golden_evaluation_helper.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <openglad/core/order.h>

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
                  const std::set<std::int32_t>& observed,
                  Order                        order)
{
    std::vector<std::string> missing;
    for (std::size_t i = 0; i < required_count; ++i)
    {
        const std::int32_t f = required[i];
        if (observed.count(f) == 0)
            missing.push_back(og::parity::family_symbol_by_order(
                static_cast<std::int32_t>(order), f));
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
            os << og::parity::family_symbol_by_order(
                      static_cast<std::int32_t>(Order::Living), pair.first)
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
        u.obs.walker_families, Order::Living);
    EXPECT_TRUE(missing.empty()) << format_missing("walker families", missing);
}

TEST(Parity, coverage_gate_effect_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredEffectFamilies,
        std::size(og::parity::kRequiredEffectFamilies),
        u.obs.effect_families, Order::FX);
    EXPECT_TRUE(missing.empty()) << format_missing("effect families", missing);
}

TEST(Parity, coverage_gate_weapon_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredWeaponFamilies,
        std::size(og::parity::kRequiredWeaponFamilies),
        u.obs.weapon_families, Order::Weapon);
    EXPECT_TRUE(missing.empty()) << format_missing("weapon families", missing);
}

TEST(Parity, coverage_gate_treasure_families)
{
    const auto& u = observed_union();
    const auto missing = missing_families(
        og::parity::kRequiredTreasureFamilies,
        std::size(og::parity::kRequiredTreasureFamilies),
        u.obs.treasure_families, Order::Treasure);
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
                              const std::set<std::int32_t>& obs,
                              Order                         order) {
        for (std::size_t i = 0; i < n; ++i)
            if (obs.count(req[i]) == 0)
            {
                std::ostringstream os;
                os << label << ": "
                   << og::parity::family_symbol_by_order(
                          static_cast<std::int32_t>(order), req[i]);
                missing.push_back(os.str());
            }
    };
    append_family("walker_family",   og::parity::kRequiredWalkerFamilies,
                  std::size(og::parity::kRequiredWalkerFamilies),
                  u.obs.walker_families, Order::Living);
    append_family("effect_family",   og::parity::kRequiredEffectFamilies,
                  std::size(og::parity::kRequiredEffectFamilies),
                  u.obs.effect_families, Order::FX);
    append_family("weapon_family",   og::parity::kRequiredWeaponFamilies,
                  std::size(og::parity::kRequiredWeaponFamilies),
                  u.obs.weapon_families, Order::Weapon);
    append_family("treasure_family", og::parity::kRequiredTreasureFamilies,
                  std::size(og::parity::kRequiredTreasureFamilies),
                  u.obs.treasure_families, Order::Treasure);

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
            os << "special: "
               << og::parity::family_symbol_by_order(
                      static_cast<std::int32_t>(Order::Living), pair.first)
               << " idx " << static_cast<int>(pair.second);
            missing.push_back(os.str());
        }
    }

    EXPECT_TRUE(missing.empty())
        << format_missing("cumulative coverage", missing);
}

// --- Phase 04 — behavioural coverage gates ---------------------------------
//
// Static scans over `kScenarios[].expected_facts` that assert each
// required family / event kind appears as `arg0` of at least one
// predicate of the matching `FactKind`. These are independent of the
// runtime coverage observation: structural-only blob-spawn rows do not
// satisfy these gates because they do not declare expected_facts
// referencing the family in question.
//
// A predicate that is `dumper_deferred(...)` (applies_to_branch=false
// AND applies_to_master=false) still satisfies the static binding —
// the gate looks at predicate kind and arg0, not at evaluation. Phase
// 04b's dumper rework flips those wrappers back to genuine evaluation
// without renumbering the gate.

namespace {

// Return true iff any predicate across kScenarios[].expected_facts has
// `kind == want_kind` and `arg0 == want_arg0`.
bool any_predicate_binds(og::parity::FactKind want_kind,
                          std::int32_t       want_arg0)
{
    for (const auto& spec : og::parity::kScenarios)
    {
        if (spec.expected_facts == nullptr) continue;
        for (std::size_t i = 0; i < spec.fact_count; ++i)
        {
            const auto& p = spec.expected_facts[i];
            if (p.kind == want_kind && p.arg0 == want_arg0)
                return true;
        }
    }
    return false;
}

std::vector<std::string>
missing_family_bindings(const std::int32_t*       required,
                         std::size_t               required_count,
                         og::parity::FactKind      kind,
                         Order                     order)
{
    std::vector<std::string> missing;
    for (std::size_t i = 0; i < required_count; ++i)
    {
        const std::int32_t f = required[i];
        if (!any_predicate_binds(kind, f))
            missing.push_back(og::parity::family_symbol_by_order(
                static_cast<std::int32_t>(order), f));
    }
    return missing;
}

// Phase 03 — true iff at least one predicate in `kScenarios` binds the
// given treasure family. The legacy `TreasureFamilyRemovedFromOblist`
// kind matches arg0 == family_id alone; the Order-aware
// `TreasureFamilyOfOrderRemovedFromOblist` kind additionally requires
// arg1 == kOrderTreasure (so a (family, Living) row, if anyone ever
// authored one, would NOT satisfy this binding for the same family id).
// Both treasure gate sites delegate to this helper so each kind earns
// the right to satisfy the gate without renumbering predicates already
// authored in the legacy form.
bool any_treasure_binding(std::int32_t family_id)
{
    if (any_predicate_binds(og::parity::FactKind::TreasureFamilyRemovedFromOblist,
                            family_id))
        return true;
    for (const auto& spec : og::parity::kScenarios)
    {
        if (spec.expected_facts == nullptr) continue;
        for (std::size_t i = 0; i < spec.fact_count; ++i)
        {
            const auto& p = spec.expected_facts[i];
            if (p.kind == og::parity::FactKind::TreasureFamilyOfOrderRemovedFromOblist
                && p.arg0 == family_id
                && p.arg1 == static_cast<std::int32_t>(Order::Treasure))
                return true;
        }
    }
    return false;
}

} // namespace

TEST(Parity, behavioural_coverage_gate_weapons)
{
    // Each kRequiredWeaponFamilies entry must appear as arg0 of at least
    // one WeaponFamilyEmitted predicate in some scenario's expected_facts.
    const auto missing = missing_family_bindings(
        og::parity::kRequiredWeaponFamilies,
        std::size(og::parity::kRequiredWeaponFamilies),
        og::parity::FactKind::WeaponFamilyEmitted, Order::Weapon);
    EXPECT_TRUE(missing.empty())
        << format_missing("weapon families behaviourally bound "
                          "(WeaponFamilyEmitted arg0)", missing);
}

TEST(Parity, behavioural_coverage_gate_treasures)
{
    // Each kRequiredTreasureFamilies entry must appear as arg0 of at
    // least one TreasureFamilyRemovedFromOblist OR
    // TreasureFamilyOfOrderRemovedFromOblist(arg0, kOrderTreasure)
    // predicate. Phase 03 introduced the Order-aware kind; this gate
    // accepts either form so rows can migrate independently.
    std::vector<std::string> missing;
    for (const std::int32_t f : og::parity::kRequiredTreasureFamilies)
    {
        if (!any_treasure_binding(f))
            missing.push_back(og::parity::family_symbol_by_order(
                static_cast<std::int32_t>(Order::Treasure), f));
    }
    EXPECT_TRUE(missing.empty())
        << format_missing("treasure families behaviourally bound "
                          "(TreasureFamily{,OfOrder}RemovedFromOblist arg0)",
                          missing);
}

TEST(Parity, behavioural_coverage_gate_effects)
{
    // Each kRequiredEffectFamilies entry must appear as arg0 of at least
    // one EffectFamilyCount predicate.
    const auto missing = missing_family_bindings(
        og::parity::kRequiredEffectFamilies,
        std::size(og::parity::kRequiredEffectFamilies),
        og::parity::FactKind::EffectFamilyCount, Order::FX);
    EXPECT_TRUE(missing.empty())
        << format_missing("effect families behaviourally bound "
                          "(EffectFamilyCount arg0)", missing);
}

TEST(Parity, behavioural_coverage_gate_generators)
{
    // Each kRequiredGeneratorFamilies entry must appear as arg0 of at
    // least one WalkerFamilyCount predicate. (Generators sit in oblist
    // and the schema-v1 dumper does not disambiguate Order, so this is
    // the structurally-correct binding kind until Phase 04b adds
    // Order-aware family symbols.)
    const auto missing = missing_family_bindings(
        og::parity::kRequiredGeneratorFamilies,
        std::size(og::parity::kRequiredGeneratorFamilies),
        og::parity::FactKind::WalkerFamilyCount, Order::Generator);
    EXPECT_TRUE(missing.empty())
        << format_missing("generator families behaviourally bound "
                          "(WalkerFamilyCount arg0)", missing);
}

TEST(Parity, behavioural_coverage_gate_event_kinds)
{
    // Each kRequiredEventKinds entry must appear (by ordinal) as arg0
    // of EventKindAtLeast OR EventKindExactly in some scenario's
    // expected_facts. The ordinals match the event_kind_symbol table
    // in state_dump.cpp.
    static constexpr std::pair<std::string_view, std::int32_t> kKindOrdinals[] = {
        {"play_sound",                1},
        {"notification",              2},
        {"set_palette",               3},
        {"request_redraw",            4},
        {"end_game",                  5},
        {"set_end",                   6},
        {"request_exit_confirmation", 7},
        {"withdraw_to_level",         8},
        {"score_change",              9},
    };
    std::vector<std::string> missing;
    for (const auto& [name, ordinal] : kKindOrdinals)
    {
        if (!any_predicate_binds(og::parity::FactKind::EventKindAtLeast, ordinal) &&
            !any_predicate_binds(og::parity::FactKind::EventKindExactly, ordinal))
        {
            missing.emplace_back(name);
        }
    }
    EXPECT_TRUE(missing.empty())
        << format_missing("event kinds behaviourally bound "
                          "(EventKindAtLeast/Exactly arg0)", missing);
}

// Umbrella case — fails iff any of the five behavioural per-category
// gates above would fail. Mirrors the structural `Parity.coverage_gate`
// umbrella so CI dashboards can filter on a single name.
TEST(Parity, behavioural_coverage_gate)
{
    std::vector<std::string> missing;

    auto append_family = [&](std::string_view         label,
                              const std::int32_t*      req,
                              std::size_t              n,
                              og::parity::FactKind     kind,
                              Order                    order) {
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!any_predicate_binds(kind, req[i]))
            {
                std::ostringstream os;
                os << label << ": "
                   << og::parity::family_symbol_by_order(
                          static_cast<std::int32_t>(order), req[i]);
                missing.push_back(os.str());
            }
        }
    };
    append_family("weapon_family",
                  og::parity::kRequiredWeaponFamilies,
                  std::size(og::parity::kRequiredWeaponFamilies),
                  og::parity::FactKind::WeaponFamilyEmitted, Order::Weapon);
    // Treasure family binding accepts either the legacy
    // `TreasureFamilyRemovedFromOblist` or the Phase 03 Order-aware
    // `TreasureFamilyOfOrderRemovedFromOblist(arg0, kOrderTreasure)`
    // kind.
    for (const std::int32_t f : og::parity::kRequiredTreasureFamilies)
    {
        if (!any_treasure_binding(f))
        {
            std::ostringstream os;
            os << "treasure_family: "
               << og::parity::family_symbol_by_order(
                      static_cast<std::int32_t>(Order::Treasure), f);
            missing.push_back(os.str());
        }
    }
    append_family("effect_family",
                  og::parity::kRequiredEffectFamilies,
                  std::size(og::parity::kRequiredEffectFamilies),
                  og::parity::FactKind::EffectFamilyCount, Order::FX);
    append_family("generator_family",
                  og::parity::kRequiredGeneratorFamilies,
                  std::size(og::parity::kRequiredGeneratorFamilies),
                  og::parity::FactKind::WalkerFamilyCount, Order::Generator);

    static constexpr std::pair<std::string_view, std::int32_t> kKindOrdinals[] = {
        {"play_sound",                1},
        {"notification",              2},
        {"set_palette",               3},
        {"request_redraw",            4},
        {"end_game",                  5},
        {"set_end",                   6},
        {"request_exit_confirmation", 7},
        {"withdraw_to_level",         8},
        {"score_change",              9},
    };
    for (const auto& [name, ordinal] : kKindOrdinals)
    {
        if (!any_predicate_binds(og::parity::FactKind::EventKindAtLeast, ordinal) &&
            !any_predicate_binds(og::parity::FactKind::EventKindExactly, ordinal))
        {
            std::ostringstream os;
            os << "event_kind: " << name;
            missing.push_back(os.str());
        }
    }

    EXPECT_TRUE(missing.empty())
        << format_missing("behavioural cumulative coverage", missing);
}

// --- Phase 04-prep — runtime behavioural gate ------------------------------
//
// The static `arg0`-scan gates above only confirm that a predicate of the
// right shape *exists*; they do not enforce that the predicate actually
// flips when evaluated against the committed master golden. Phase 04-prep
// adds the runtime gate the per-family sub-phases depend on:
//
//   Parity.behavioural_coverage_gate_runtime
//     For every master-comparable scenario in `kScenarios` that has at
//     least one non-`TickReached` predicate, load
//     `tests/parity/golden/<id>.json` (skip if missing — that is the
//     per-family sub-phase's job), iterate the row's `expected_facts`
//     and require >= 1 predicate where `applies_to_master == true`,
//     `evaluate_one` returns `ok=true` with `indeterminate=false`, and
//     the predicate kind is NOT `TickReached`. A scenario whose
//     master-side predicates all skip (gated off, indeterminate, or
//     `TickReached`-only) fails the gate.
//
//   Parity.behavioural_coverage_gate_no_dead_predicates
//     Every `FactPredicate` row whose `applies_to_master == false` AND
//     `applies_to_branch == false` is a fatal error — that combination
//     short-circuits both sides of the parity comparison and is vacuous
//     by construction (policy P2).

namespace {

bool scenario_has_non_tick_predicate(const og::parity::ScenarioSpec& spec)
{
    if (spec.expected_facts == nullptr || spec.fact_count == 0)
        return false;
    for (std::size_t i = 0; i < spec.fact_count; ++i)
        if (spec.expected_facts[i].kind != og::parity::FactKind::TickReached)
            return true;
    return false;
}

std::string format_runtime_failure(std::string_view scenario_id,
                                   const og::parity::GoldenEvaluation& ev)
{
    std::ostringstream os;
    os << scenario_id
       << ": no non-TickReached master-side predicate evaluated TRUE on the "
          "committed golden; per-predicate trace:";
    if (ev.predicates.empty())
        os << " <empty>";
    for (const auto& pe : ev.predicates)
    {
        os << "\n      [#" << pe.index
           << "] kind=" << static_cast<unsigned>(pe.kind)
           << " arg0=" << pe.arg0
           << " atm=" << (pe.applies_to_master ? "Y" : "N")
           << " atb=" << (pe.applies_to_branch ? "Y" : "N")
           << " ok=" << (pe.ok ? "Y" : "N")
           << " indet=" << (pe.indeterminate ? "Y" : "N");
        if (!pe.message.empty())
            os << " msg=\"" << pe.message << "\"";
    }
    return os.str();
}

} // namespace

TEST(Parity, behavioural_coverage_gate_runtime)
{
    std::vector<std::string> failures;
    std::vector<std::string> skipped_missing_golden;

    for (const auto& spec : og::parity::kScenarios)
    {
        if (spec.is_branch_internal) continue;
        if (spec.compare_mode == og::parity::CompareMode::Invariant) continue;
        if (!scenario_has_non_tick_predicate(spec)) continue;

        const og::parity::GoldenEvaluation ev =
            og::parity::evaluate_facts_against_golden_for_id(spec.id);

        if (!ev.scenario_present)
        {
            failures.push_back(std::string(spec.id) +
                ": helper reported scenario not present in kScenarios "
                "(internal error)");
            continue;
        }

        if (!ev.golden_present)
        {
            // Per the Phase 04-prep contract: missing goldens are the
            // per-family sub-phase's responsibility. Skip without
            // failing — the parent sub-phase's own gate will catch
            // unpopulated rows.
            skipped_missing_golden.push_back(std::string(spec.id));
            continue;
        }

        if (!ev.parse_ok)
        {
            failures.push_back(std::string(spec.id) +
                ": parse_state_dump rejected the committed golden (" +
                ev.load_error + ")");
            continue;
        }

        if (!ev.any_non_tick_master_pass())
            failures.push_back(format_runtime_failure(spec.id, ev));
    }

    EXPECT_TRUE(failures.empty())
        << format_missing("runtime behavioural gate", failures)
        << "\n  (scenarios skipped — no committed golden yet: "
        << skipped_missing_golden.size() << ")";
}

TEST(Parity, behavioural_coverage_gate_no_dead_predicates)
{
    std::vector<std::string> failures;
    for (const auto& spec : og::parity::kScenarios)
    {
        if (spec.expected_facts == nullptr) continue;
        for (std::size_t i = 0; i < spec.fact_count; ++i)
        {
            const auto& p = spec.expected_facts[i];
            if (!p.applies_to_master && !p.applies_to_branch)
            {
                std::ostringstream os;
                os << spec.id << "[#" << i << "] kind="
                   << static_cast<unsigned>(p.kind)
                   << " arg0=" << p.arg0
                   << " — applies_to_master=false AND applies_to_branch=false "
                      "(dead predicate; both sides short-circuit, evaluation "
                      "is vacuous)";
                failures.push_back(os.str());
            }
        }
    }
    EXPECT_TRUE(failures.empty())
        << format_missing("dead predicates", failures);
}
