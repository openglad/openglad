#include "coverage_targets.h"
#include "fact_predicate.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <openglad/core/constants.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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

    if (spec.is_branch_internal ||
        spec.compare_mode == og::parity::CompareMode::Invariant)
    {
        // Subsystem 12 — snapshot dirty-bit parity. Capture the run a
        // second time and require byte-equal canonical JSON: a deterministic
        // dumper over the same scenario must produce identical output.
        const og::parity::RunOutcome again = og::parity::run_scenario(spec);
        const std::string actual2 = og::parity::canonical_serialize(again.dump);
        EXPECT_EQ(actual, actual2)
            << "branch-internal dumper is non-deterministic for " << spec.id;
        return;
    }

    const std::filesystem::path path = golden_path(spec.id);
    std::string expected;
    const bool have_golden = read_file(path, expected);

    if (spec.compare_mode == og::parity::CompareMode::SemanticParity)
    {
        // Phase 01 semantic-parity contract: evaluate the row's predicates
        // on both the master golden (parsed) and the freshly captured
        // branch dump. The two dumps may differ byte-for-byte (RNG drift,
        // intended branch behavioural diffs) but every predicate must hold
        // on both.
        ASSERT_NE(spec.expected_facts, nullptr)
            << "SemanticParity row " << spec.id
            << " has no expected_facts[]";
        ASSERT_GT(spec.fact_count, 0u)
            << "SemanticParity row " << spec.id
            << " has fact_count == 0";

        const og::parity::FactEvalResult branch =
            og::parity::evaluate_facts(og::parity::FactSide::Branch,
                                       spec.expected_facts, spec.fact_count,
                                       outcome.dump);
        EXPECT_TRUE(branch.ok)
            << "semantic-parity branch dump failed for " << spec.id
            << ": " << branch.message;

        if (have_golden)
        {
            auto parsed = og::parity::parse_state_dump(expected);
            ASSERT_TRUE(parsed.has_value())
                << "could not parse master golden for " << spec.id
                << " at " << path.string();
            const og::parity::FactEvalResult master =
                og::parity::evaluate_facts(og::parity::FactSide::Master,
                                           spec.expected_facts, spec.fact_count,
                                           *parsed);
            EXPECT_TRUE(master.ok)
                << "semantic-parity master golden failed for " << spec.id
                << ": " << master.message;
        }
        else
        {
            GTEST_SKIP() << "master golden missing for " << spec.id
                         << " (expected at " << path.string()
                         << ") — Phase 04+ recapture will populate";
        }
        return;
    }

    // ByteEqual rows: require the dump match the golden byte-for-byte.
    if (!have_golden)
    {
        GTEST_SKIP() << "golden not yet captured for " << spec.id
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
OG_PARITY_TEST(smoke_empty_scen99)
OG_PARITY_TEST(smoke_nonempty_scen99)
OG_PARITY_TEST(smoke_nonempty_scen99_inputs)

// Phase 04 — one byte-equal scenario per walker family.
OG_PARITY_TEST(family_soldier_scen99)
OG_PARITY_TEST(family_elf_scen99)
OG_PARITY_TEST(family_archer_scen99)
OG_PARITY_TEST(family_mage_scen99)
OG_PARITY_TEST(family_skeleton_scen99)
OG_PARITY_TEST(family_cleric_scen99)
OG_PARITY_TEST(family_fireelemental_scen99)
OG_PARITY_TEST(family_faerie_scen99)
OG_PARITY_TEST(family_slime_scen99)
OG_PARITY_TEST(family_small_slime_scen99)
OG_PARITY_TEST(family_medium_slime_scen99)
OG_PARITY_TEST(family_thief_scen99)
OG_PARITY_TEST(family_ghost_scen99)
OG_PARITY_TEST(family_druid_scen99)
OG_PARITY_TEST(family_orc_scen99)
OG_PARITY_TEST(family_big_orc_scen99)
OG_PARITY_TEST(family_barbarian_scen99)
OG_PARITY_TEST(family_archmage_scen99)
OG_PARITY_TEST(family_golem_scen99)
OG_PARITY_TEST(coverage_catchall_scen99)
OG_PARITY_TEST(family_giant_skeleton_scen99)
OG_PARITY_TEST(family_tower1_scen99)


// Phase 04 redo — per-entity behavioural scenarios.
OG_PARITY_TEST(treasure_stain_pickup_scen99)
OG_PARITY_TEST(treasure_drumstick_pickup_scen99)
OG_PARITY_TEST(treasure_gold_bar_pickup_scen99)
OG_PARITY_TEST(treasure_silver_bar_pickup_scen99)
OG_PARITY_TEST(treasure_magic_potion_pickup_scen99)
OG_PARITY_TEST(treasure_invis_potion_pickup_scen99)
OG_PARITY_TEST(treasure_invulnerable_potion_pickup_scen99)
OG_PARITY_TEST(treasure_flight_potion_pickup_scen99)
OG_PARITY_TEST(treasure_teleporter_pickup_scen99)
OG_PARITY_TEST(treasure_life_gem_pickup_scen99)
OG_PARITY_TEST(treasure_key_pickup_scen99)
OG_PARITY_TEST(treasure_speed_potion_pickup_scen99)
OG_PARITY_TEST(weapon_knife_emission_scen99)
OG_PARITY_TEST(weapon_rock_emission_scen99)
OG_PARITY_TEST(weapon_arrow_emission_scen99)
OG_PARITY_TEST(weapon_fireball_emission_scen99)
OG_PARITY_TEST(weapon_tree_emission_scen99)
OG_PARITY_TEST(weapon_meteor_emission_scen99)
OG_PARITY_TEST(weapon_sprinkle_emission_scen99)
OG_PARITY_TEST(weapon_bone_emission_scen99)
OG_PARITY_TEST(weapon_blood_emission_scen99)
OG_PARITY_TEST(weapon_blob_emission_scen99)
OG_PARITY_TEST(weapon_fire_arrow_emission_scen99)
OG_PARITY_TEST(weapon_lightning_emission_scen99)
OG_PARITY_TEST(weapon_glow_emission_scen99)
OG_PARITY_TEST(weapon_wave_emission_scen99)
OG_PARITY_TEST(weapon_wave2_emission_scen99)
OG_PARITY_TEST(weapon_wave3_emission_scen99)
OG_PARITY_TEST(weapon_circle_protection_emission_scen99)
OG_PARITY_TEST(weapon_hammer_emission_scen99)
OG_PARITY_TEST(weapon_door_emission_scen99)
OG_PARITY_TEST(weapon_boulder_emission_scen99)
OG_PARITY_TEST(effect_expand_emission_scen99)
OG_PARITY_TEST(effect_ghost_scare_emission_scen99)
OG_PARITY_TEST(effect_bomb_emission_scen99)
OG_PARITY_TEST(effect_explosion_emission_scen99)
OG_PARITY_TEST(effect_flash_emission_scen99)
OG_PARITY_TEST(effect_magic_shield_emission_scen99)
OG_PARITY_TEST(effect_knife_back_emission_scen99)
OG_PARITY_TEST(effect_boomerang_emission_scen99)
OG_PARITY_TEST(effect_cloud_emission_scen99)
OG_PARITY_TEST(effect_marker_emission_scen99)
OG_PARITY_TEST(effect_chain_emission_scen99)
OG_PARITY_TEST(effect_door_open_emission_scen99)
OG_PARITY_TEST(effect_hit_emission_scen99)
OG_PARITY_TEST(generator_tent_emission_scen99)
OG_PARITY_TEST(generator_tower_emission_scen99)
OG_PARITY_TEST(generator_bones_emission_scen99)
OG_PARITY_TEST(generator_treehouse_emission_scen99)
OG_PARITY_TEST(event_notification_emission_scen99)
OG_PARITY_TEST(event_set_palette_emission_scen99)
OG_PARITY_TEST(event_request_redraw_emission_scen99)
OG_PARITY_TEST(event_end_game_emission_scen99)
OG_PARITY_TEST(event_set_end_emission_scen99)
OG_PARITY_TEST(special_soldier_1_scen99)
OG_PARITY_TEST(special_soldier_2_scen99)
OG_PARITY_TEST(special_soldier_3_scen99)
OG_PARITY_TEST(special_soldier_4_scen99)
OG_PARITY_TEST(special_elf_1_scen99)
OG_PARITY_TEST(special_elf_2_scen99)
OG_PARITY_TEST(special_elf_3_scen99)
OG_PARITY_TEST(special_elf_4_scen99)
OG_PARITY_TEST(special_archer_1_scen99)
OG_PARITY_TEST(special_archer_2_scen99)
OG_PARITY_TEST(special_archer_3_scen99)
OG_PARITY_TEST(special_mage_2_scen99)
OG_PARITY_TEST(special_mage_3_scen99)
OG_PARITY_TEST(special_mage_4_scen99)
OG_PARITY_TEST(special_mage_5_scen99)
OG_PARITY_TEST(special_skeleton_1_scen99)
OG_PARITY_TEST(special_cleric_2_scen99)
OG_PARITY_TEST(special_cleric_3_scen99)
OG_PARITY_TEST(special_cleric_4_scen99)
OG_PARITY_TEST(special_fireelemental_1_scen99)
OG_PARITY_TEST(special_slime_1_scen99)
OG_PARITY_TEST(special_small_slime_1_scen99)
OG_PARITY_TEST(special_medium_slime_1_scen99)
OG_PARITY_TEST(special_thief_2_scen99)
OG_PARITY_TEST(special_thief_3_scen99)
OG_PARITY_TEST(special_thief_4_scen99)
OG_PARITY_TEST(special_ghost_1_scen99)
OG_PARITY_TEST(special_druid_1_scen99)
OG_PARITY_TEST(special_druid_3_scen99)
OG_PARITY_TEST(special_druid_4_scen99)
OG_PARITY_TEST(special_orc_1_scen99)
OG_PARITY_TEST(special_orc_2_scen99)
OG_PARITY_TEST(special_barbarian_1_scen99)
OG_PARITY_TEST(special_barbarian_2_scen99)
OG_PARITY_TEST(special_archmage_2_scen99)
OG_PARITY_TEST(special_archmage_3_scen99)
OG_PARITY_TEST(special_archmage_4_scen99)

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

// Phase 01 new gtests --------------------------------------------------------

TEST(Parity, exercises_bitcount_matches_required_specials)
{
    // The Exercises enum reserves one bit per (family, special_index) pair
    // in kRequiredSpecials. The highest declared bit must equal
    // 1ULL << (count - 1) so coverage masks stay tight against the
    // canonical list.
    constexpr std::size_t kCount = std::size(og::parity::kRequiredSpecials);
    EXPECT_EQ(kCount, std::size_t{42})
        << "kRequiredSpecials length drifted from the 42-bit Exercises "
           "layout in scenario_table.h";

    const std::uint64_t highest =
        static_cast<std::uint64_t>(og::parity::Exercises::Special_Archmage_4);
    EXPECT_EQ(highest, 1ULL << (kCount - 1))
        << "Exercises::Special_Archmage_4 must be bit (kCount - 1)";
}

TEST(Parity, parse_state_dump_tolerates_legacy_v1_shape)
{
    const std::filesystem::path path =
        golden_path("family_soldier_scen99");
    std::string golden;
    ASSERT_TRUE(read_file(path, golden))
        << "missing golden at " << path.string();
    auto parsed = og::parity::parse_state_dump(golden);
    ASSERT_TRUE(parsed.has_value())
        << "parse_state_dump rejected a canonical v1 master golden";
    EXPECT_FALSE(parsed->inventory_keys.has_value())
        << "schema-v1 master goldens must not carry inventory_keys";

    // Evaluate at least one backfilled predicate so the parser's output
    // round-trips through the evaluator on real golden data.
    const og::parity::ScenarioSpec* spec =
        find_scenario("family_soldier_scen99");
    ASSERT_NE(spec, nullptr);
    ASSERT_GT(spec->fact_count, 0u);
    const og::parity::FactEvalResult result =
        og::parity::evaluate_facts(og::parity::FactSide::Master,
                                   spec->expected_facts, spec->fact_count,
                                   *parsed);
    EXPECT_TRUE(result.ok)
        << "backfilled predicate failed on master golden: " << result.message;
}

TEST(Parity, weapon_family_emitted_matches_dump_weapons_only)
{
    // Synthetic dump fixture: a weapon-order FAMILY_DOOR sits in
    // dump.weapons[]; an effect-order FAMILY_DOOR_OPEN sits in
    // dump.effects[]. The predicate must match the weapon and reject
    // the effect.
    og::parity::StateDump dump;
    og::parity::WeaponEntry door_w;
    door_w.family   = og::parity::family_symbol_by_order(
        static_cast<std::int32_t>(Order::Weapon), FAMILY_DOOR);
    door_w.id       = 100;
    door_w.team     = 0;
    door_w.xpos     = 0;
    door_w.ypos     = 0;
    door_w.lifetime = 0;
    dump.weapons.push_back(door_w);

    og::parity::EffectEntry door_open_fx;
    door_open_fx.family   = og::parity::family_symbol_by_order(
        static_cast<std::int32_t>(Order::FX), FAMILY_DOOR_OPEN);
    door_open_fx.id       = 200;
    door_open_fx.lifetime = 0;
    dump.effects.push_back(door_open_fx);

    const auto match_door = og::parity::evaluate_one(
        og::parity::pred::WeaponFamilyEmitted(FAMILY_DOOR), dump);
    EXPECT_TRUE(match_door.ok)
        << "WeaponFamilyEmitted(FAMILY_DOOR) did not match dump.weapons[].family=18";

    const auto match_door_open = og::parity::evaluate_one(
        og::parity::pred::WeaponFamilyEmitted(FAMILY_DOOR_OPEN), dump);
    EXPECT_FALSE(match_door_open.ok)
        << "WeaponFamilyEmitted(FAMILY_DOOR_OPEN) matched dump.effects[].family=11; "
        << "predicate must be pinned to dump.weapons[] only";
}
