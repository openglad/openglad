#include "coverage_targets.h"
#include "fact_predicate.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <openglad/core/constants.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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

            // EXACT weapon-trajectory parity. The WeaponSpeed / WeaponNetTravel
            // band predicates above only bound trajectory approximately
            // (~1px/tick + a coarse path class). This asserts the full per-tick
            // weapon_tracks (every weapon's xpos/ypos at every sampled tick)
            // byte-matches the master golden, locking speed AND path exactly to
            // master rather than to a tolerance band.
            const auto& bt = outcome.dump.weapon_tracks;
            const auto& mt = parsed->weapon_tracks;
            EXPECT_EQ(bt.size(), mt.size())
                << "weapon_tracks sample count diverges from master for "
                << spec.id << " (branch " << bt.size() << " vs master "
                << mt.size() << ")";
            const std::size_t track_n = bt.size() < mt.size() ? bt.size()
                                                              : mt.size();
            for (std::size_t i = 0; i < track_n; ++i)
            {
                if (bt[i].tick != mt[i].tick || bt[i].family != mt[i].family ||
                    bt[i].seq != mt[i].seq || bt[i].xpos != mt[i].xpos ||
                    bt[i].ypos != mt[i].ypos)
                {
                    ADD_FAILURE()
                        << "weapon trajectory diverges from master for "
                        << spec.id << " at weapon_tracks[" << i
                        << "]: branch {tick=" << bt[i].tick << ",family="
                        << bt[i].family << ",seq=" << bt[i].seq << ",x="
                        << bt[i].xpos << ",y=" << bt[i].ypos
                        << "} vs master {tick=" << mt[i].tick << ",family="
                        << mt[i].family << ",seq=" << mt[i].seq << ",x="
                        << mt[i].xpos << ",y=" << mt[i].ypos << "}";
                    break; // first divergence is enough to fail the scenario
                }
            }
        }
        else
        {
            ADD_FAILURE() << "master golden missing for " << spec.id
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
OG_PARITY_TEST(z_stair_up_scen9301)
OG_PARITY_TEST(z_fall_through_air_scen9301)
OG_PARITY_TEST(z_fall_two_story_scen9301)
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
OG_PARITY_TEST(treasure_exit_pickup_scen99)
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
OG_PARITY_TEST(special_druid_2_scen99)
OG_PARITY_TEST(special_druid_3_scen99)
OG_PARITY_TEST(special_druid_4_scen99)
OG_PARITY_TEST(special_orc_1_scen99)
OG_PARITY_TEST(special_orc_2_scen99)
OG_PARITY_TEST(special_barbarian_1_scen99)
OG_PARITY_TEST(special_barbarian_2_scen99)
OG_PARITY_TEST(special_archmage_2_scen99)
OG_PARITY_TEST(special_archmage_3_scen99)
OG_PARITY_TEST(special_archmage_4_scen99)

// Walker status-timer scenarios.
OG_PARITY_TEST(enemy_freeze_mage_scen99)
OG_PARITY_TEST(invisibility_thief_scen99)
OG_PARITY_TEST(speed_potion_movement_scen99)
OG_PARITY_TEST(invulnerable_potion_scen99)

// Summon-lifecycle scenarios.
OG_PARITY_TEST(summon_lifetime_faerie_scen99)
OG_PARITY_TEST(summon_lifetime_decrement_faerie_scen99)

// Generator-saturation scenario.
OG_PARITY_TEST(generator_saturation_scen99)

// Weapon-trajectory scenarios.
OG_PARITY_TEST(weapon_rock_slot2_emit_scen99)
OG_PARITY_TEST(weapon_boomerang_return_scen99)
OG_PARITY_TEST(weapon_exploding_boulder_scen99)

// Effect-emission scenarios.
OG_PARITY_TEST(effect_heartburst_multitarget_scen99)
OG_PARITY_TEST(effect_poison_cloud_emit_scen99)
OG_PARITY_TEST(effect_protection_emit_scen99)

// Effect-timer scenarios.
OG_PARITY_TEST(effect_bomb_timer_scen99)

// Input-pipeline scenarios.
OG_PARITY_TEST(input_diagonal_movement_scen99)
OG_PARITY_TEST(input_hold_fire_search_scen99)
OG_PARITY_TEST(input_switch_char_scen99)
OG_PARITY_TEST(input_special_switch_wrap_scen99)

// Multi-team is_friendly scenario.
OG_PARITY_TEST(multiplayer_two_teams_scen99)

// Level-withdraw scenario.
OG_PARITY_TEST(level_withdraw_scen99)
OG_PARITY_TEST(midcombat_partial_hp_scen99)
OG_PARITY_TEST(consumable_inventory_state_scen99)

// Cleric heal / raise / turn-undead / resurrect scenarios.
OG_PARITY_TEST(special_cleric_heal_ally_scen99)
OG_PARITY_TEST(cleric_raise_skeleton_scen99)
OG_PARITY_TEST(cleric_raise_ghost_scen99)
OG_PARITY_TEST(cleric_turn_undead_scen99)
OG_PARITY_TEST(cleric_resurrect_friendly_scen99)
OG_PARITY_TEST(undead_no_corpse_raise_scen99)

// Treasure guard-arm and consequence scenarios.
OG_PARITY_TEST(treasure_flight_effect_scen99)
OG_PARITY_TEST(treasure_invis_effect_scen99)
OG_PARITY_TEST(treasure_flight_potion_flier_noconsume_scen99)
OG_PARITY_TEST(treasure_drumstick_fullhp_noconsume_scen99)
OG_PARITY_TEST(treasure_gold_bar_team_reject_scen99)
OG_PARITY_TEST(treasure_life_gem_enemy_reject_scen99)
OG_PARITY_TEST(treasure_magic_potion_overfill_scen99)
OG_PARITY_TEST(treasure_key_team1_silent_scen99)
OG_PARITY_TEST(treasure_exit_open_prompt_scen99)
OG_PARITY_TEST(thief_charm_opponent_scen99)
OG_PARITY_TEST(archmage_summon_elemental_scen99)
OG_PARITY_TEST(mage_teleport_marker_scen99)
OG_PARITY_TEST(archmage_teleport_marker_scen99)
OG_PARITY_TEST(archmage_mind_control_team_flip_scen99)
OG_PARITY_TEST(archmage_summon_image_phantom_scen99)
OG_PARITY_TEST(mage_starburst_ring_scen99)
OG_PARITY_TEST(soldier_whirlwind_ring_scen99)
OG_PARITY_TEST(soldier_disarm_matched_levels_scen99)
OG_PARITY_TEST(orc_yell_stun_hold_scen99)
OG_PARITY_TEST(orc_eat_corpse_scen99)
OG_PARITY_TEST(archer_fire_arrows_ring_scen99)
OG_PARITY_TEST(skeleton_tunnel_displacement_scen99)
OG_PARITY_TEST(druid_grow_tree_emission_scen99)
OG_PARITY_TEST(barbarian_boulder_impact_scen99)

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

// Z-axis / multi-floor: run both branch-internal scenarios and assert the
// soldier ends up on the expected floor via the teethed WalkerOnFloor
// predicate. The stair soldier climbs floor 0 -> 1; the air soldier falls
// floor 1 -> 0. The opposite-floor checks demonstrate the predicate's teeth.
TEST(Parity, z_multifloor_walker_floor_transitions)
{
    const og::parity::ScenarioSpec* stair =
        find_scenario("z_stair_up_scen9301");
    const og::parity::ScenarioSpec* fall =
        find_scenario("z_fall_through_air_scen9301");
    if (stair == nullptr || fall == nullptr)
    {
        GTEST_SKIP() << "z multi-floor scenarios not present in kScenarios; "
                        "Parity.coverage_gate* is the responsible gate.";
        return;
    }

    using og::parity::evaluate_one;
    namespace pred = og::parity::pred;

    const auto stair_out = og::parity::run_scenario(*stair);
    const auto fall_out = og::parity::run_scenario(*fall);

    // Stair: the soldier must have climbed to floor 1 and survived.
    const auto stair_on_1 =
        evaluate_one(pred::WalkerOnFloor(FAMILY_SOLDIER, 1, 1), stair_out.dump);
    EXPECT_TRUE(stair_on_1.ok)
        << "z_stair_up: soldier should be alive on floor 1: "
        << stair_on_1.message;

    // Fall: the soldier must have fallen to floor 0 and survived (not pit-died).
    const auto fall_on_0 =
        evaluate_one(pred::WalkerOnFloor(FAMILY_SOLDIER, 0, 0), fall_out.dump);
    EXPECT_TRUE(fall_on_0.ok)
        << "z_fall_through_air: soldier should be alive on floor 0: "
        << fall_on_0.message;

    // Teeth: the predicate distinguishes the two outcomes — the stair soldier is
    // NOT on floor 0, and the fall soldier is NOT on floor 1.
    EXPECT_FALSE(
        evaluate_one(pred::WalkerOnFloor(FAMILY_SOLDIER, 0, 0), stair_out.dump).ok)
        << "z_stair_up: soldier unexpectedly still on floor 0";
    EXPECT_FALSE(
        evaluate_one(pred::WalkerOnFloor(FAMILY_SOLDIER, 1, 1), fall_out.dump).ok)
        << "z_fall_through_air: soldier unexpectedly still on floor 1";

    // Fall DAMAGE teeth (walker::resolve_fall_landing). One story is FREE:
    // the existing 1-story fall soldier must finish at FULL HP (120.00 ->
    // 12000 cents, exact — no damage, no regen in play).
    const auto fall_full_hp = evaluate_one(
        pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
        fall_out.dump);
    EXPECT_TRUE(fall_full_hp.ok)
        << "z_fall_through_air: a 1-story fall must be free (full HP): "
        << fall_full_hp.message;

    // Two stories cost 15% of max HP: 120 - 18 = 102.00. regen_delay (50)
    // exceeds the remaining tick budget, so the value is exact; the band
    // [10199, 10201] absorbs float-formatting jitter only.
    const og::parity::ScenarioSpec* fall2 =
        find_scenario("z_fall_two_story_scen9301");
    ASSERT_NE(fall2, nullptr)
        << "z_fall_two_story_scen9301 missing from kScenarios";
    const auto fall2_out = og::parity::run_scenario(*fall2);
    const auto fall2_on_0 =
        evaluate_one(pred::WalkerOnFloor(FAMILY_SOLDIER, 0, 0), fall2_out.dump);
    EXPECT_TRUE(fall2_on_0.ok)
        << "z_fall_two_story: soldier should be alive on floor 0: "
        << fall2_on_0.message;
    const auto fall2_hp = evaluate_one(
        pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10199, 10201),
        fall2_out.dump);
    EXPECT_TRUE(fall2_hp.ok)
        << "z_fall_two_story: soldier must land at 85% max HP (15% fall "
           "damage for the second story): "
        << fall2_hp.message;
    // Teeth: the band rejects the free-fall outcome.
    EXPECT_FALSE(evaluate_one(
        pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
        fall2_out.dump).ok)
        << "z_fall_two_story: soldier unexpectedly took no fall damage";
}

// Branch-internal Invariant rows skip fact evaluation in run_one_scenario --
// that path only re-runs the dumper and asserts determinism, because there is
// no master golden to evaluate the other side against. Their expected_facts[]
// therefore need a hand-written assertion, exactly as the z-axis rows above
// get one. treasure_exit_open_prompt_scen99 is Invariant because the companion
// records RequestExitConfirmation only inside its withdraw block and its exit
// arm calls endgame() from inside eat_me, so no comparable master dump exists;
// the branch behaviour is still worth pinning, and without this test its
// mutation (treasure_navigation.lua:65 `if can_exit_now then` -> `if false
// then`) has nothing to flip in the suite.
TEST(Parity, treasure_exit_open_prompt_facts)
{
    const og::parity::ScenarioSpec* spec =
        find_scenario("treasure_exit_open_prompt_scen99");
    ASSERT_NE(spec, nullptr)
        << "treasure_exit_open_prompt_scen99 missing from kScenarios";
    ASSERT_NE(spec->expected_facts, nullptr);
    ASSERT_GT(spec->fact_count, 0u);

    const auto outcome = og::parity::run_scenario(*spec);
    const og::parity::FactEvalResult facts =
        og::parity::evaluate_facts(og::parity::FactSide::Branch,
                                   spec->expected_facts, spec->fact_count,
                                   outcome.dump);
    EXPECT_TRUE(facts.ok)
        << "treasure_exit_open_prompt_scen99 facts failed: " << facts.message;
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

namespace {

// Build a synthetic dump carrying one FAMILY_ROCK track (seq 0) from the
// per-tick (tick, xpos, ypos) samples supplied. `seq` is fixed at 0.
og::parity::StateDump make_rock_track_dump(
    std::initializer_list<std::array<std::int32_t, 3>> samples)
{
    og::parity::StateDump dump;
    const std::string sym = og::parity::family_symbol_by_order(
        static_cast<std::int32_t>(Order::Weapon), FAMILY_ROCK);
    for (const auto& s : samples)
    {
        og::parity::WeaponTrackSample t;
        t.family = sym;
        t.seq    = 0;
        t.tick   = static_cast<std::uint32_t>(s[0]);
        t.xpos   = s[1];
        t.ypos   = s[2];
        dump.weapon_tracks.push_back(t);
    }
    return dump;
}

} // namespace

TEST(Parity, weapon_trajectory_predicates_evaluate_over_weapon_tracks)
{
    using og::parity::evaluate_one;
    namespace pred = og::parity::pred;

    // A straight horizontal rock: +10px/tick over 4 consecutive ticks.
    // Per-tick step = 1000 centi-px; net = pathlen = 3000 centi-px.
    const auto straight = make_rock_track_dump(
        {{1, 100, 50}, {2, 110, 50}, {3, 120, 50}, {4, 130, 50}});

    // WeaponSpeed: representative (MAX consecutive) step = 1000 centi-px.
    EXPECT_TRUE(evaluate_one(pred::WeaponSpeed(FAMILY_ROCK, 900, 1100), straight).ok)
        << "straight rock speed should fall in [900,1100] centi-px/tick";
    {
        const auto out_of_window =
            evaluate_one(pred::WeaponSpeed(FAMILY_ROCK, 0, 500), straight);
        EXPECT_FALSE(out_of_window.ok)
            << "1000 centi-px/tick must fall OUT of [0,500] (predicate has teeth)";
        EXPECT_FALSE(out_of_window.indeterminate);
    }

    // STRAIGHT path: net (3000) >= threshold AND net >= 0.7*pathlen.
    EXPECT_TRUE(evaluate_one(
        pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathStraight, 2000),
        straight).ok) << "straight path should classify as STRAIGHT";

    // A returning rock: out to x=140 then back to x=100. pathlen = 8000,
    // net = 0 -> RETURNS, and NOT STRAIGHT.
    const auto returns = make_rock_track_dump(
        {{1, 100, 50}, {2, 120, 50}, {3, 140, 50}, {4, 120, 50}, {5, 100, 50}});
    EXPECT_TRUE(evaluate_one(
        pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathReturns, 4000),
        returns).ok) << "out-and-back path should classify as RETURNS";
    EXPECT_FALSE(evaluate_one(
        pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathStraight, 1000),
        returns).ok) << "out-and-back path must NOT classify as STRAIGHT";

    // A stationary rock: never moves. pathlen = 0 -> STATIONARY.
    const auto stationary = make_rock_track_dump(
        {{1, 100, 50}, {2, 100, 50}, {3, 100, 50}});
    EXPECT_TRUE(evaluate_one(
        pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathStationary, 100),
        stationary).ok) << "non-moving rock should classify as STATIONARY";
    EXPECT_FALSE(evaluate_one(
        pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathStationary, 100),
        straight).ok) << "moving rock must NOT classify as STATIONARY";

    // Legacy-golden tolerance: a dump with NO track for the family is
    // Indeterminate (ok=true) for both trajectory kinds.
    og::parity::StateDump empty;
    {
        const auto s = evaluate_one(pred::WeaponSpeed(FAMILY_ROCK, 0, 100), empty);
        EXPECT_TRUE(s.ok && s.indeterminate)
            << "WeaponSpeed on a track-less dump must be Indeterminate (pass)";
        const auto n = evaluate_one(
            pred::WeaponNetTravel(FAMILY_ROCK, og::parity::kWeaponPathStraight, 100),
            empty);
        EXPECT_TRUE(n.ok && n.indeterminate)
            << "WeaponNetTravel on a track-less dump must be Indeterminate (pass)";
    }
}

TEST(Parity, parse_state_dump_round_trips_weapon_tracks)
{
    // canonical_serialize -> parse_state_dump must preserve weapon_tracks,
    // and the parser must still tolerate a dump that lacks the key.
    og::parity::StateDump dump = make_rock_track_dump(
        {{7, 135, 122}, {8, 142, 121}, {9, 149, 120}});
    const std::string json = og::parity::canonical_serialize(dump);
    auto parsed = og::parity::parse_state_dump(json);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->weapon_tracks.size(), 3u);
    EXPECT_EQ(parsed->weapon_tracks[0].tick, 7u);
    EXPECT_EQ(parsed->weapon_tracks[0].xpos, 135);
    EXPECT_EQ(parsed->weapon_tracks[2].ypos, 120);
    EXPECT_EQ(parsed->weapon_tracks[0].family,
              og::parity::family_symbol_by_order(
                  static_cast<std::int32_t>(Order::Weapon), FAMILY_ROCK));
}
