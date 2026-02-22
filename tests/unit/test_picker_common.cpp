#include "unit.h"
#include <openglad/ui/picker_common.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <cstdlib>
#include <cstring>

// --- calculate_hire_cost ---

OG_UNIT_TEST(test_calculate_hire_cost_base)
{
    init_family_registry();
    // A fresh recruit with base stats should cost exactly the hiring_cost
    guy recruit(FAMILY_SOLDIER);
    std::uint32_t cost = og::ui::calculate_hire_cost(recruit);
    auto* fd = get_family_descriptor(FAMILY_SOLDIER);
    // Base cost + level 1 XP (calculate_exp(1) == 0)
    OG_ASSERT(cost == static_cast<std::uint32_t>(fd->hiring_cost));

    // Check another family
    guy elf(FAMILY_ELF);
    cost = og::ui::calculate_hire_cost(elf);
    fd = get_family_descriptor(FAMILY_ELF);
    OG_ASSERT(cost == static_cast<std::uint32_t>(fd->hiring_cost));
}

OG_UNIT_TEST(test_calculate_hire_cost_with_stats)
{
    init_family_registry();
    auto* fd = get_family_descriptor(FAMILY_SOLDIER);

    guy recruit(FAMILY_SOLDIER);
    std::uint32_t base_cost = og::ui::calculate_hire_cost(recruit);

    // Bump strength above base — cost should increase
    recruit.strength = static_cast<short>(fd->base_stats[0] + 5);
    std::uint32_t upgraded_cost = og::ui::calculate_hire_cost(recruit);
    OG_ASSERT(upgraded_cost > base_cost);

    // Bump more — cost should increase further
    recruit.strength = static_cast<short>(fd->base_stats[0] + 10);
    std::uint32_t more_cost = og::ui::calculate_hire_cost(recruit);
    OG_ASSERT(more_cost > upgraded_cost);
}

// --- calculate_train_cost ---

OG_UNIT_TEST(test_calculate_train_cost_delta)
{
    init_family_registry();
    guy original(FAMILY_SOLDIER);
    guy trained(original);

    // No changes — zero cost
    std::uint32_t cost = og::ui::calculate_train_cost(trained, original);
    OG_ASSERT(cost == 0);

    // Increase strength — positive cost
    auto* fd = get_family_descriptor(FAMILY_SOLDIER);
    trained.strength = static_cast<short>(original.strength + 5);
    cost = og::ui::calculate_train_cost(trained, original);
    OG_ASSERT(cost > 0);
}

OG_UNIT_TEST(test_calculate_train_cost_no_downgrade)
{
    init_family_registry();
    guy original(FAMILY_SOLDIER);
    guy trained(original);

    // Decrease strength below original — cost should be 0 (effective clamps to original)
    trained.strength = static_cast<short>(original.strength - 3);
    std::uint32_t cost = og::ui::calculate_train_cost(trained, original);
    OG_ASSERT(cost == 0);
}

// --- count_family_members ---

OG_UNIT_TEST(test_count_family_members)
{
    SaveData save;
    save.team_size = 0;

    OG_ASSERT(og::ui::count_family_members(FAMILY_SOLDIER, save) == 0);

    // Add two soldiers and a mage
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[1] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_size = 3;

    OG_ASSERT(og::ui::count_family_members(FAMILY_SOLDIER, save) == 2);
    OG_ASSERT(og::ui::count_family_members(FAMILY_MAGE, save) == 1);
    OG_ASSERT(og::ui::count_family_members(FAMILY_ARCHER, save) == 0);
}

// --- add_recruit_to_team ---

OG_UNIT_TEST(test_add_recruit_to_team)
{
    SaveData save;
    save.team_size = 0;

    auto recruit = std::make_unique<guy>(FAMILY_SOLDIER);
    recruit->name = "TestGuy";
    int slot = og::ui::add_recruit_to_team(save, std::move(recruit), 0);

    OG_ASSERT(slot == 0);
    OG_ASSERT(save.team_size == 1);
    OG_ASSERT(save.team_list[0] != nullptr);
    OG_ASSERT(save.team_list[0]->name == "TestGuy");
    OG_ASSERT(save.team_list[0]->teamnum == 0);

    // Add another
    auto recruit2 = std::make_unique<guy>(FAMILY_MAGE);
    recruit2->name = "Wizard";
    int slot2 = og::ui::add_recruit_to_team(save, std::move(recruit2), 1);

    OG_ASSERT(slot2 == 1);
    OG_ASSERT(save.team_size == 2);
    OG_ASSERT(save.team_list[1]->teamnum == 1);

    // Test filling gaps: remove slot 0, add should fill it
    save.team_list[0].reset();
    save.team_size = 1; // only slot 1 occupied
    auto recruit3 = std::make_unique<guy>(FAMILY_ARCHER);
    int slot3 = og::ui::add_recruit_to_team(save, std::move(recruit3), 0);
    OG_ASSERT(slot3 == 0);
    OG_ASSERT(save.team_size == 2);
}

// --- create_recruit ---

OG_UNIT_TEST(test_create_recruit_unique_name)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    std::srand(42);
    auto recruit1 = og::ui::create_recruit(FAMILY_SOLDIER, 0, save);
    OG_ASSERT(recruit1 != nullptr);
    OG_ASSERT(!recruit1->name.empty());
    OG_ASSERT(recruit1->family == FAMILY_SOLDIER);
    OG_ASSERT(recruit1->teamnum == 0);

    // Add it to team, then create another — name should differ
    std::string first_name = recruit1->name;
    og::ui::add_recruit_to_team(save, std::move(recruit1), 0);

    auto recruit2 = og::ui::create_recruit(FAMILY_SOLDIER, 0, save);
    OG_ASSERT(recruit2 != nullptr);
    // If same random name comes up, get_unique_name should append a number or retry
    // Either way, it should not match an existing team member
    bool name_is_unique = true;
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[i] && save.team_list[i]->name == recruit2->name) {
            name_is_unique = false;
            break;
        }
    }
    OG_ASSERT(name_is_unique);
}

// --- reset_for_new_game ---

OG_UNIT_TEST(test_reset_for_new_game)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_size = 2;
    save.m_totalcash[0] = 9999;

    og::ui::reset_for_new_game(save);

    OG_ASSERT(save.team_size == 0);
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        OG_ASSERT(save.team_list[i] == nullptr);
    }
}

// --- family_display_name ---

OG_UNIT_TEST(test_family_display_name)
{
    init_family_registry();
    OG_ASSERT(std::strcmp(og::ui::family_display_name(FAMILY_SOLDIER), "SOLDIER") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_display_name(FAMILY_BIG_ORC), "ORC CAPTAIN") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_display_name(255), "BEAST") == 0);
}

// --- family_short_name ---

OG_UNIT_TEST(test_family_short_name)
{
    OG_ASSERT(std::strcmp(og::ui::family_short_name(FAMILY_SOLDIER), "SOLDIER") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_short_name(FAMILY_BIG_ORC), "ORC CAP.") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_short_name(FAMILY_BARBARIAN), "BARBAR.") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_short_name(FAMILY_FIREELEMENTAL), "ELEMENT.") == 0);
    OG_ASSERT(std::strcmp(og::ui::family_short_name(99), "BEAST") == 0);
}

// --- family_hiring_base_cost ---

OG_UNIT_TEST(test_family_hiring_base_cost)
{
    init_family_registry();
    OG_ASSERT(og::ui::family_hiring_base_cost(FAMILY_SOLDIER) == 250);
    OG_ASSERT(og::ui::family_hiring_base_cost(FAMILY_ELF) == 150);
    OG_ASSERT(og::ui::family_hiring_base_cost(FAMILY_MAGE) == 450);
    OG_ASSERT(og::ui::family_hiring_base_cost(999) == 0);
}

// --- kAllowableGuys ---

OG_UNIT_TEST(test_allowable_guys_constants)
{
    OG_ASSERT(og::ui::kAllowableGuys.size() == 14);
    OG_ASSERT(og::ui::kAllowableGuys[0] == FAMILY_SOLDIER);
    OG_ASSERT(og::ui::kAllowableGuys[1] == FAMILY_BARBARIAN);
    OG_ASSERT(og::ui::kAllowableGuys[13] == FAMILY_GHOST);
}

// --- kDifficultyNames ---

OG_UNIT_TEST(test_difficulty_names)
{
    OG_ASSERT(std::strcmp(og::ui::kDifficultyNames[0], "Skirmish") == 0);
    OG_ASSERT(std::strcmp(og::ui::kDifficultyNames[1], "Battle") == 0);
    OG_ASSERT(std::strcmp(og::ui::kDifficultyNames[2], "Slaughter") == 0);
}

// --- get_random_name ---

OG_UNIT_TEST(test_get_random_name_all_families)
{
    std::srand(42);
    for (int fam : og::ui::kAllowableGuys) {
        const char* name = og::ui::get_random_name(static_cast<unsigned char>(fam));
        OG_ASSERT(name != nullptr);
        OG_ASSERT(std::strlen(name) > 0);
    }
}
