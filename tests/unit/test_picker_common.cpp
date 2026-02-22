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

// --- statscopy ---

OG_UNIT_TEST(test_statscopy)
{
    init_family_registry();
    guy src(FAMILY_MAGE);
    src.name = "Gandalf";
    src.strength = 10;
    src.dexterity = 20;
    src.constitution = 30;
    src.intelligence = 40;
    src.armor = 5;
    src.level = 3;
    src.exp = 999;
    src.kills = 7;
    src.teamnum = 2;

    guy dst(FAMILY_SOLDIER);
    og::ui::statscopy(&dst, &src);

    OG_ASSERT(dst.family == FAMILY_MAGE);
    OG_ASSERT(dst.name == "Gandalf");
    OG_ASSERT(dst.strength == 10);
    OG_ASSERT(dst.dexterity == 20);
    OG_ASSERT(dst.constitution == 30);
    OG_ASSERT(dst.intelligence == 40);
    OG_ASSERT(dst.armor == 5);
    OG_ASSERT(dst.level == 3);
    OG_ASSERT(dst.exp == 999);
    OG_ASSERT(dst.kills == 7);
    OG_ASSERT(dst.teamnum == 2);
}

// --- HireSession ---

OG_UNIT_TEST(test_hire_session_cycle)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);

    // Should start at index 0 (SOLDIER)
    OG_ASSERT(session.family_index() == 0);
    OG_ASSERT(session.current_recruit() != nullptr);
    OG_ASSERT(session.current_recruit()->family == FAMILY_SOLDIER);

    // Cycle forward through all 14 families
    for (int i = 1; i < 14; i++) {
        session.next_family();
        OG_ASSERT(session.family_index() == i);
        OG_ASSERT(session.current_recruit() != nullptr);
        OG_ASSERT(session.current_recruit()->family == og::ui::kAllowableGuys[i]);
    }

    // Wraps back to 0
    session.next_family();
    OG_ASSERT(session.family_index() == 0);
    OG_ASSERT(session.current_recruit()->family == FAMILY_SOLDIER);

    // Cycle backward wraps to 13 (GHOST)
    session.prev_family();
    OG_ASSERT(session.family_index() == 13);
    OG_ASSERT(session.current_recruit()->family == FAMILY_GHOST);
}

OG_UNIT_TEST(test_hire_session_hire)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);
    std::uint32_t cost = session.current_cost();
    OG_ASSERT(cost > 0);

    std::uint32_t gold_before = save.m_totalcash[0];
    int slot = session.hire();
    OG_ASSERT(slot == 0);
    OG_ASSERT(save.team_size == 1);
    OG_ASSERT(save.team_list[0] != nullptr);
    OG_ASSERT(save.team_list[0]->family == FAMILY_SOLDIER);
    OG_ASSERT(save.m_totalcash[0] == gold_before - cost);

    // After hiring, session auto-creates next recruit with same family
    OG_ASSERT(session.current_recruit() != nullptr);
    OG_ASSERT(session.current_recruit()->family == FAMILY_SOLDIER);
}

OG_UNIT_TEST(test_hire_session_rename_hired)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);
    int slot = session.hire();
    OG_ASSERT(slot >= 0);

    session.rename_hired(slot, "CustomName");
    OG_ASSERT(save.team_list[slot]->name == "CustomName");
}

OG_UNIT_TEST(test_hire_session_team_full)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 999999;

    // Fill the team
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_size++;
    }

    og::ui::HireSession session(save, 0);
    OG_ASSERT(session.team_full());
    OG_ASSERT(session.hire() == -1);
}

OG_UNIT_TEST(test_hire_session_not_enough_gold)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 0; // no gold

    og::ui::HireSession session(save, 0);
    OG_ASSERT(session.hire() == -1);
    OG_ASSERT(save.team_size == 0);
}

// --- TrainSession ---

OG_UNIT_TEST(test_train_session_cycle)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    // Add 3 team members in non-contiguous slots
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Alpha";
    save.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[2]->name = "Beta";
    save.team_list[5] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[5]->name = "Gamma";
    save.team_size = 3;
    save.m_totalcash[0] = 50000;

    og::ui::TrainSession session(save);
    OG_ASSERT(!session.empty());
    OG_ASSERT(session.current_slot() == 0);
    OG_ASSERT(session.working_copy().name == "Alpha");

    session.next_member();
    OG_ASSERT(session.current_slot() == 2);
    OG_ASSERT(session.working_copy().name == "Beta");

    session.next_member();
    OG_ASSERT(session.current_slot() == 5);
    OG_ASSERT(session.working_copy().name == "Gamma");

    // Wraps back to first
    session.next_member();
    OG_ASSERT(session.current_slot() == 0);
    OG_ASSERT(session.working_copy().name == "Alpha");

    // Backward wraps to last
    session.prev_member();
    OG_ASSERT(session.current_slot() == 5);
    OG_ASSERT(session.working_copy().name == "Gamma");
}

OG_UNIT_TEST(test_train_session_empty)
{
    SaveData save;
    save.team_size = 0;

    og::ui::TrainSession session(save);
    OG_ASSERT(session.empty());
}

OG_UNIT_TEST(test_train_session_increase_decrease)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 50000;

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 3);
    OG_ASSERT(session.working_copy().strength == orig_str + 3);
    OG_ASSERT(session.current_cost() > 0);

    // Decrease back to original — cost should be 0
    session.decrease_stat(og::ui::TrainSession::Stat::Strength, 3);
    OG_ASSERT(session.working_copy().strength == orig_str);
    OG_ASSERT(session.current_cost() == 0);

    // Decrease below original — clamped to original
    session.decrease_stat(og::ui::TrainSession::Stat::Strength, 5);
    OG_ASSERT(session.working_copy().strength == orig_str);
}

OG_UNIT_TEST(test_train_session_level_locks_stats)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);

    // Increase level
    session.increase_stat(og::ui::TrainSession::Stat::Level, 1);
    OG_ASSERT(session.level_increased());

    // Now stats should be locked — increase should be no-op
    short str_before = session.working_copy().strength;
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    OG_ASSERT(session.working_copy().strength == str_before);
}

OG_UNIT_TEST(test_train_session_stats_lock_level)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);

    // Increase a stat
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    OG_ASSERT(session.stats_increased());

    // Now level should be locked — increase should be no-op
    short level_before = session.working_copy().level;
    session.increase_stat(og::ui::TrainSession::Stat::Level, 1);
    OG_ASSERT(session.working_copy().level == level_before);
}

OG_UNIT_TEST(test_train_session_accept)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 5);
    std::uint32_t cost = session.current_cost();
    OG_ASSERT(cost > 0);

    std::uint32_t gold_before = save.m_totalcash[0];
    OG_ASSERT(session.accept());
    OG_ASSERT(save.m_totalcash[0] == gold_before - cost);
    // Original team member should now have updated stats
    OG_ASSERT(save.team_list[0]->strength == orig_str + 5);
}

OG_UNIT_TEST(test_train_session_accept_cant_afford)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 1; // almost no gold

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 10);
    OG_ASSERT(!session.accept()); // can't afford
    // Original should be unchanged
    OG_ASSERT(save.team_list[0]->strength == orig_str);
}

OG_UNIT_TEST(test_train_session_accept_force)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 0; // no gold

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 5);
    OG_ASSERT(session.accept(true)); // force=true bypasses cost
    OG_ASSERT(save.team_list[0]->strength == orig_str + 5);
    OG_ASSERT(save.m_totalcash[0] == 0); // gold unchanged
}
