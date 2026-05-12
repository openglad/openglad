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
        // Branch-internal scenarios have no master golden. Phase 04 leaves
        // the invariant body unimplemented; Phase 06 will fill it in.
        GTEST_FAIL() << "branch-internal invariant body not yet implemented for "
                     << spec.id;
        return;
    }

    const std::filesystem::path path = golden_path(spec.id);
    std::string expected;
    if (!read_file(path, expected))
    {
        GTEST_FAIL() << "golden not yet captured for " << spec.id
                     << " (expected at " << path.string() << ")";
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

#undef OG_PARITY_TEST
