#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// Look up a scenario by id at runtime. Returns nullptr if no entry in
// `kScenarios` matches — callers GTEST_SKIP in that case so removing a
// scenario from the table does not break compilation of this file
// (Phase 03 verifier 03b stashes one entry and expects og_test_parity
// to still build; the coverage gate alone signals the omission).
const og::parity::ScenarioSpec* find_scenario(std::string_view id)
{
    for (const auto& spec : og::parity::kScenarios)
        if (spec.id == id) return &spec;
    return nullptr;
}

std::filesystem::path golden_path(std::string_view id)
{
    return std::filesystem::path("tests/parity/golden") /
           (std::string(id) + ".json");
}

bool read_file(const std::filesystem::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void run_one_scenario(const og::parity::ScenarioSpec& spec)
{
    const og::parity::RunOutcome outcome = og::parity::run_scenario(spec);
    const std::string actual = og::parity::canonical_serialize(outcome.dump);

    if (spec.is_branch_internal)
    {
        // Subsystem 12 — snapshot dirty-bit parity. Capture the run a second
        // time and require byte-equal canonical JSON: a deterministic dumper
        // over the same scenario must produce identical output. Phase 06
        // skeleton: while the runner exercises an empty world, this still
        // catches non-determinism in canonical_serialize and the StateDump
        // capture path. Phase 07 expands the predicate once world-loading
        // lands (compare dirty-tracked mirror vs direct iteration).
        const og::parity::RunOutcome again = og::parity::run_scenario(spec);
        const std::string actual2 = og::parity::canonical_serialize(again.dump);
        EXPECT_EQ(actual, actual2)
            << "branch-internal dumper is non-deterministic for " << spec.id;
        return;
    }

    // Phase 02 smoke scenarios verify the runner actually loaded a scenario
    // and exercised a non-empty world. They have no master golden yet
    // (Phase 07 captures), so just check the dump itself is sensible.
    if (spec.id.rfind("smoke_", 0) == 0)
    {
        EXPECT_GE(outcome.dump.tick, 50u)
            << "smoke scenario " << spec.id << " ran fewer than 50 ticks";
        EXPECT_FALSE(outcome.dump.walkers.empty())
            << "smoke scenario " << spec.id << " produced an empty walker list";
        return;
    }

    const std::filesystem::path path = golden_path(spec.id);
    std::string expected;
    if (!read_file(path, expected))
    {
        // Phase 01 teardown deleted the fraudulent goldens; Phase 07
        // captures honest replacements from the rewritten master companion.
        // Until then, mark the comparison as pending rather than failing.
        GTEST_SKIP() << "golden not yet captured for " << spec.id
                     << " (expected at " << path.string()
                     << ") — Phase 07 will capture from master companion";
        return;
    }

    EXPECT_EQ(expected, actual)
        << "parity mismatch for scenario " << spec.id
        << "; see scripts/parity/diff_dumps.py for a structured diff";
}

} // namespace

// One TEST per scenario. The id forms the GoogleTest case name so
// `ctest --preset ci-test -R '^og_test_parity'` enumerates each scenario
// individually via --gtest_list_tests. The macro looks the scenario up
// in `kScenarios` by id at runtime, NOT by hardcoded index — removing
// an entry from the table must remain a compile-clean operation so the
// coverage gate (Phase 03 verifier 03b) can fail at runtime rather than
// breaking the build of unrelated tests.

#define OG_PARITY_TEST(NAME)                                                   \
    TEST(Parity, NAME)                                                         \
    {                                                                          \
        const og::parity::ScenarioSpec* spec = find_scenario(#NAME);           \
        if (spec == nullptr)                                                   \
        {                                                                      \
            GTEST_SKIP() << "scenario \"" #NAME                                \
                            "\" is not present in kScenarios; "                \
                            "Parity.coverage_gate* is the responsible gate.";  \
            return;                                                            \
        }                                                                      \
        run_one_scenario(*spec);                                               \
    }

OG_PARITY_TEST(ai_idle_wander_scen9301)
OG_PARITY_TEST(combat_attack_scen99)
OG_PARITY_TEST(special_archmage_scen123)
OG_PARITY_TEST(special_cleric_scen124)
OG_PARITY_TEST(special_mage_scen126)
OG_PARITY_TEST(special_thief_scen789)
OG_PARITY_TEST(effect_bomb_lifetime_scen99)
OG_PARITY_TEST(effect_chain_scen9410)
OG_PARITY_TEST(summon_druid_pet_scen950)
OG_PARITY_TEST(scoring_after_combat_scen99)
OG_PARITY_TEST(save_roundtrip_scen99)
OG_PARITY_TEST(exit_trigger_scen9302)
OG_PARITY_TEST(tick_cadence_scen9301)
OG_PARITY_TEST(rng_seed_stable_scen99)
OG_PARITY_TEST(scripted_input_scen9301)
OG_PARITY_TEST(snapshot_dirty_bits_scen9301)
OG_PARITY_TEST(smoke_nonempty_scen99)
OG_PARITY_TEST(smoke_nonempty_scen99_inputs)

#undef OG_PARITY_TEST

// Phase 02 — verify the two smoke runs produce observably-different walker
// state. If both dumps were identical, the input-injection path is broken.
// Looks scenarios up by id so removing them from the table degrades to
// SKIP rather than failing compilation.
TEST(Parity, smoke_inputs_diverge_from_no_inputs)
{
    const og::parity::ScenarioSpec* no_inputs =
        find_scenario("smoke_nonempty_scen99");
    const og::parity::ScenarioSpec* with_inputs =
        find_scenario("smoke_nonempty_scen99_inputs");
    if (no_inputs == nullptr || with_inputs == nullptr)
    {
        GTEST_SKIP() << "smoke scenarios not present in kScenarios; "
                        "Parity.coverage_gate* is the responsible gate.";
        return;
    }

    const auto a = og::parity::run_scenario(*no_inputs);
    const auto b = og::parity::run_scenario(*with_inputs);
    const std::string sa = og::parity::canonical_serialize(a.dump);
    const std::string sb = og::parity::canonical_serialize(b.dump);
    EXPECT_NE(sa, sb)
        << "smoke scenarios with and without inputs produced identical dumps; "
        << "apply_inputs_at_tick is not reaching the player walker";
}
