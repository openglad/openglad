#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

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
// individually via --gtest_list_tests. The macro expansion below keeps the
// scenario table as the single source of truth.

#define OG_PARITY_TEST(IDX, NAME)                                              \
    TEST(Parity, NAME)                                                         \
    {                                                                          \
        static_assert(IDX < og::parity::kScenarioCount,                        \
                      "scenario index out of range");                          \
        run_one_scenario(og::parity::kScenarios[IDX]);                         \
    }

OG_PARITY_TEST(0,  ai_idle_wander_scen9301)
OG_PARITY_TEST(1,  combat_attack_scen99)
OG_PARITY_TEST(2,  special_archmage_scen123)
OG_PARITY_TEST(3,  special_cleric_scen124)
OG_PARITY_TEST(4,  special_mage_scen126)
OG_PARITY_TEST(5,  special_thief_scen789)
OG_PARITY_TEST(6,  effect_bomb_lifetime_scen99)
OG_PARITY_TEST(7,  effect_chain_scen9410)
OG_PARITY_TEST(8,  summon_druid_pet_scen950)
OG_PARITY_TEST(9,  scoring_after_combat_scen99)
OG_PARITY_TEST(10, save_roundtrip_scen99)
OG_PARITY_TEST(11, exit_trigger_scen9302)
OG_PARITY_TEST(12, tick_cadence_scen9301)
OG_PARITY_TEST(13, rng_seed_stable_scen99)
OG_PARITY_TEST(14, scripted_input_scen9301)
OG_PARITY_TEST(15, snapshot_dirty_bits_scen9301)
OG_PARITY_TEST(16, smoke_nonempty_scen99)
OG_PARITY_TEST(17, smoke_nonempty_scen99_inputs)

#undef OG_PARITY_TEST

// Phase 02 — verify the two smoke runs produce observably-different walker
// state. If both dumps were identical, the input-injection path is broken.
TEST(Parity, smoke_inputs_diverge_from_no_inputs)
{
    const auto& no_inputs   = og::parity::kScenarios[16];
    const auto& with_inputs = og::parity::kScenarios[17];
    ASSERT_EQ(no_inputs.id,   std::string_view("smoke_nonempty_scen99"));
    ASSERT_EQ(with_inputs.id, std::string_view("smoke_nonempty_scen99_inputs"));

    const auto a = og::parity::run_scenario(no_inputs);
    const auto b = og::parity::run_scenario(with_inputs);
    const std::string sa = og::parity::canonical_serialize(a.dump);
    const std::string sb = og::parity::canonical_serialize(b.dump);
    EXPECT_NE(sa, sb)
        << "smoke scenarios with and without inputs produced identical dumps; "
        << "apply_inputs_at_tick is not reaching the player walker";
}
