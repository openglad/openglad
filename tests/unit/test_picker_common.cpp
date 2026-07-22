#include <gtest/gtest.h>
#include <algorithm>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>
#include "test_game_world_fixture.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

class EditableSlotPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override
    {
        return false;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        return slot_index < editable_slots.size() && editable_slots[slot_index];
    }

    std::array<bool, MAX_TEAM_SIZE> editable_slots{};
};

struct ActivePickerLobbyClientGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;

    explicit ActivePickerLobbyClientGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }

    ~ActivePickerLobbyClientGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

} // namespace

// --- calculate_hire_cost ---

TEST(PickerCommon, calculate_hire_cost_base)
{
    init_family_registry();
    // A fresh recruit with base stats should cost exactly the hiring_cost
    guy recruit(FAMILY_SOLDIER);
    std::uint32_t cost = og::ui::calculate_hire_cost(recruit);
    auto* fd = get_family_descriptor(FAMILY_SOLDIER);
    // Base cost + level 1 XP (calculate_exp(1) == 0)
    ASSERT_TRUE(cost == static_cast<std::uint32_t>(fd->hiring_cost));

    // Check another family
    guy elf(FAMILY_ELF);
    cost = og::ui::calculate_hire_cost(elf);
    fd = get_family_descriptor(FAMILY_ELF);
    ASSERT_TRUE(cost == static_cast<std::uint32_t>(fd->hiring_cost));
}

TEST(PickerCommon, calculate_hire_cost_with_stats)
{
    init_family_registry();
    auto* fd = get_family_descriptor(FAMILY_SOLDIER);

    guy recruit(FAMILY_SOLDIER);
    std::uint32_t base_cost = og::ui::calculate_hire_cost(recruit);

    // Bump strength above base — cost should increase
    recruit.strength = static_cast<short>(fd->base_stats[0] + 5);
    std::uint32_t upgraded_cost = og::ui::calculate_hire_cost(recruit);
    ASSERT_TRUE(upgraded_cost > base_cost);

    // Bump more — cost should increase further
    recruit.strength = static_cast<short>(fd->base_stats[0] + 10);
    std::uint32_t more_cost = og::ui::calculate_hire_cost(recruit);
    ASSERT_TRUE(more_cost > upgraded_cost);
}

TEST(PickerCommon, calculate_costs_return_zero_for_unknown_family)
{
    init_family_registry();
    guy recruit(FAMILY_SOLDIER);
    recruit.family = 99;
    guy original(recruit);

    EXPECT_EQ(0u, og::ui::calculate_hire_cost(recruit));
    EXPECT_EQ(0u, og::ui::calculate_train_cost(recruit, original));
}

// --- calculate_train_cost ---

TEST(PickerCommon, calculate_train_cost_delta)
{
    init_family_registry();
    guy original(FAMILY_SOLDIER);
    guy trained(original);

    // No changes — zero cost
    std::uint32_t cost = og::ui::calculate_train_cost(trained, original);
    ASSERT_TRUE(cost == 0);

    // Increase strength — positive cost
    trained.strength = static_cast<short>(original.strength + 5);
    cost = og::ui::calculate_train_cost(trained, original);
    ASSERT_TRUE(cost > 0);
}

TEST(PickerCommon, calculate_train_cost_no_downgrade)
{
    init_family_registry();
    guy original(FAMILY_SOLDIER);
    guy trained(original);

    // Decrease strength below original — cost should be 0 (effective clamps to original)
    trained.strength = static_cast<short>(original.strength - 3);
    std::uint32_t cost = og::ui::calculate_train_cost(trained, original);
    ASSERT_TRUE(cost == 0);
}

// --- count_family_members ---

TEST(PickerCommon, count_family_members)
{
    SaveData save;
    save.team_size = 0;

    ASSERT_TRUE(og::ui::count_family_members(FAMILY_SOLDIER, save) == 0);

    // Add two soldiers and a mage
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[1] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_size = 3;

    ASSERT_TRUE(og::ui::count_family_members(FAMILY_SOLDIER, save) == 2);
    ASSERT_TRUE(og::ui::count_family_members(FAMILY_MAGE, save) == 1);
    ASSERT_TRUE(og::ui::count_family_members(FAMILY_ARCHER, save) == 0);
}

// --- add_recruit_to_team ---

TEST(PickerCommon, add_recruit_to_team)
{
    SaveData save;
    save.team_size = 0;

    auto recruit = std::make_unique<guy>(FAMILY_SOLDIER);
    recruit->name = "TestGuy";
    int slot = og::ui::add_recruit_to_team(save, std::move(recruit), 0);

    ASSERT_TRUE(slot == 0);
    ASSERT_TRUE(save.team_size == 1);
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_TRUE(save.team_list[0]->name == "TestGuy");
    ASSERT_TRUE(save.team_list[0]->teamnum == 0);

    // Add another
    auto recruit2 = std::make_unique<guy>(FAMILY_MAGE);
    recruit2->name = "Wizard";
    int slot2 = og::ui::add_recruit_to_team(save, std::move(recruit2), 1);

    ASSERT_TRUE(slot2 == 1);
    ASSERT_TRUE(save.team_size == 2);
    ASSERT_TRUE(save.team_list[1]->teamnum == 1);

    // Test filling gaps: remove slot 0, add should fill it
    save.team_list[0].reset();
    save.team_size = 1; // only slot 1 occupied
    auto recruit3 = std::make_unique<guy>(FAMILY_ARCHER);
    int slot3 = og::ui::add_recruit_to_team(save, std::move(recruit3), 0);
    ASSERT_TRUE(slot3 == 0);
    ASSERT_TRUE(save.team_size == 2);
}

TEST(PickerCommon, add_recruit_to_team_rejects_full_and_inconsistent_roster)
{
    SaveData full;
    full.team_size = MAX_TEAM_SIZE;
    EXPECT_EQ(-1, og::ui::add_recruit_to_team(
        full, std::make_unique<guy>(FAMILY_SOLDIER), 2));

    SaveData no_empty_slots;
    no_empty_slots.team_size = 0;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        no_empty_slots.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);

    EXPECT_EQ(-1, og::ui::add_recruit_to_team(
        no_empty_slots, std::make_unique<guy>(FAMILY_MAGE), 1));
    EXPECT_EQ(0, no_empty_slots.team_size);
}

TEST(PickerCommon, train_session_skips_non_editable_lobby_slots)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Remote";
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Local";
    save.team_size = 2;

    EditableSlotPickerLobbyClient client;
    client.editable_slots.fill(false);
    client.editable_slots[1] = true;
    ActivePickerLobbyClientGuard guard(&client);

    og::ui::TrainSession session(save);
    ASSERT_FALSE(session.empty());
    EXPECT_EQ(1, session.current_slot());
    EXPECT_EQ("Local", session.original().name);

    session.next_member();
    EXPECT_EQ(1, session.current_slot());
    session.prev_member();
    EXPECT_EQ(1, session.current_slot());
}

TEST(PickerCommon, train_session_empty_when_no_editable_lobby_slots)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Remote";
    save.team_size = 1;

    EditableSlotPickerLobbyClient client;
    client.editable_slots.fill(false);
    ActivePickerLobbyClientGuard guard(&client);

    og::ui::TrainSession session(save);
    EXPECT_TRUE(session.empty());
}

// --- create_recruit ---

TEST(PickerCommon, create_recruit_unique_name)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    std::srand(42);
    auto recruit1 = og::ui::create_recruit(FAMILY_SOLDIER, 0, save);
    ASSERT_TRUE(recruit1 != nullptr);
    ASSERT_TRUE(!recruit1->name.empty());
    ASSERT_TRUE(recruit1->family == FAMILY_SOLDIER);
    ASSERT_TRUE(recruit1->teamnum == 0);

    // Add it to team, then create another — name should differ
    std::string first_name = recruit1->name;
    og::ui::add_recruit_to_team(save, std::move(recruit1), 0);

    auto recruit2 = og::ui::create_recruit(FAMILY_SOLDIER, 0, save);
    ASSERT_TRUE(recruit2 != nullptr);
    // If same random name comes up, get_unique_name should append a number or retry
    // Either way, it should not match an existing team member
    bool name_is_unique = true;
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[i] && save.team_list[i]->name == recruit2->name) {
            name_is_unique = false;
            break;
        }
    }
    ASSERT_TRUE(name_is_unique);
}

// --- reset_for_new_game ---

TEST(PickerCommon, reset_for_new_game)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_size = 2;
    save.m_totalcash[0] = 9999;

    og::ui::reset_for_new_game(save);

    ASSERT_TRUE(save.team_size == 0);
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        ASSERT_TRUE(save.team_list[i] == nullptr);
    }
}

// --- family_display_name ---

TEST(PickerCommon, family_display_name)
{
    init_family_registry();
    ASSERT_TRUE(std::strcmp(og::ui::family_display_name(FAMILY_SOLDIER), "SOLDIER") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_display_name(FAMILY_BIG_ORC), "ORC CAPTAIN") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_display_name(255), "BEAST") == 0);
}

// --- family_short_name ---

TEST(PickerCommon, family_short_name)
{
    ASSERT_TRUE(std::strcmp(og::ui::family_short_name(FAMILY_SOLDIER), "SOLDIER") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_short_name(FAMILY_BIG_ORC), "ORC CAP.") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_short_name(FAMILY_BARBARIAN), "BARBAR.") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_short_name(FAMILY_FIREELEMENTAL), "ELEMENT.") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::family_short_name(99), "BEAST") == 0);
}

// --- family_hiring_base_cost ---

TEST(PickerCommon, family_hiring_base_cost)
{
    init_family_registry();
    ASSERT_TRUE(og::ui::family_hiring_base_cost(FAMILY_SOLDIER) == 250);
    ASSERT_TRUE(og::ui::family_hiring_base_cost(FAMILY_ELF) == 150);
    ASSERT_TRUE(og::ui::family_hiring_base_cost(FAMILY_MAGE) == 450);
    ASSERT_TRUE(og::ui::family_hiring_base_cost(999) == 0);
}

// --- kAllowableGuys ---

TEST(PickerCommon, allowable_guys_constants)
{
    ASSERT_TRUE(og::ui::kAllowableGuys.size() == 14);
    ASSERT_TRUE(og::ui::kAllowableGuys[0] == FAMILY_SOLDIER);
    ASSERT_TRUE(og::ui::kAllowableGuys[1] == FAMILY_BARBARIAN);
    ASSERT_TRUE(og::ui::kAllowableGuys[13] == FAMILY_GHOST);
}

// --- kDifficultyNames ---

TEST(PickerCommon, difficulty_names)
{
    ASSERT_TRUE(std::strcmp(og::ui::kDifficultyNames[0], "Skirmish") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::kDifficultyNames[1], "Battle") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::kDifficultyNames[2], "Slaughter") == 0);
}

// --- get_random_name ---

TEST(PickerCommon, get_random_name_all_families)
{
    std::srand(42);
    for (int fam : og::ui::kAllowableGuys) {
        const char* name = og::ui::get_random_name(static_cast<unsigned char>(fam));
        ASSERT_TRUE(name != nullptr);
        ASSERT_TRUE(std::strlen(name) > 0);
    }
}

TEST(PickerCommon, get_random_name_covers_non_hirelist_and_default_families)
{
    std::srand(11);
    const int families[] = {
        FAMILY_ARCHMAGE,
        FAMILY_BIG_ORC,
        FAMILY_SLIME,
        FAMILY_MEDIUM_SLIME,
        199,
    };

    for (int family : families) {
        const char* name = og::ui::get_random_name(static_cast<unsigned char>(family));
        ASSERT_NE(nullptr, name);
        EXPECT_GT(std::strlen(name), 0u);
    }
}

TEST(PickerCommon, get_unique_name_falls_back_to_numbered_duplicate)
{
    SaveData save;
    std::vector<std::string> slime_names;
    std::srand(3);
    for (int attempts = 0; attempts < 200 && slime_names.size() < 6; ++attempts) {
        std::string name = og::ui::get_random_name(FAMILY_SLIME);
        if (std::find(slime_names.begin(), slime_names.end(), name) == slime_names.end())
            slime_names.push_back(name);
    }
    ASSERT_EQ(6u, slime_names.size());

    int slot = 0;
    for (const std::string& name : slime_names) {
        save.team_list[slot] = std::make_unique<guy>(FAMILY_SLIME);
        save.team_list[slot]->name = name;
        ++slot;
        save.team_list[slot] = std::make_unique<guy>(FAMILY_SLIME);
        save.team_list[slot]->name = name + "2";
        ++slot;
    }
    save.team_size = static_cast<unsigned char>(slot);

    std::srand(3);
    const std::string unique_name = og::ui::get_unique_name(FAMILY_SLIME, save);
    EXPECT_TRUE(unique_name.ends_with("3"));
    for (int i = 0; i < save.team_size; ++i)
        ASSERT_NE(save.team_list[i]->name, unique_name);
}

// --- statscopy ---

TEST(PickerCommon, statscopy)
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

    ASSERT_TRUE(dst.family == FAMILY_MAGE);
    ASSERT_TRUE(dst.name == "Gandalf");
    ASSERT_TRUE(dst.strength == 10);
    ASSERT_TRUE(dst.dexterity == 20);
    ASSERT_TRUE(dst.constitution == 30);
    ASSERT_TRUE(dst.intelligence == 40);
    ASSERT_TRUE(dst.armor == 5);
    ASSERT_TRUE(dst.level == 3);
    ASSERT_TRUE(dst.exp == 999);
    ASSERT_TRUE(dst.kills == 7);
    ASSERT_TRUE(dst.teamnum == 2);
}

// --- HireSession ---

TEST(PickerCommon, hire_session_cycle)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);

    // Should start at index 0 (SOLDIER)
    ASSERT_TRUE(session.family_index() == 0);
    ASSERT_TRUE(session.current_recruit() != nullptr);
    ASSERT_TRUE(session.current_recruit()->family == FAMILY_SOLDIER);

    // Cycle forward through all 14 families
    for (int i = 1; i < 14; i++) {
        session.next_family();
        ASSERT_TRUE(session.family_index() == i);
        ASSERT_TRUE(session.current_recruit() != nullptr);
        ASSERT_TRUE(session.current_recruit()->family == og::ui::kAllowableGuys[i]);
    }

    // Wraps back to 0
    session.next_family();
    ASSERT_TRUE(session.family_index() == 0);
    ASSERT_TRUE(session.current_recruit()->family == FAMILY_SOLDIER);

    // Cycle backward wraps to 13 (GHOST)
    session.prev_family();
    ASSERT_TRUE(session.family_index() == 13);
    ASSERT_TRUE(session.current_recruit()->family == FAMILY_GHOST);
}

TEST(PickerCommon, hire_session_hire)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);
    std::uint32_t cost = session.current_cost();
    ASSERT_TRUE(cost > 0);

    std::uint32_t gold_before = save.m_totalcash[0];
    int slot = session.hire();
    ASSERT_TRUE(slot == 0);
    ASSERT_TRUE(save.team_size == 1);
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_SOLDIER);
    ASSERT_TRUE(save.m_totalcash[0] == gold_before - cost);

    // After hiring, session auto-creates next recruit with same family
    ASSERT_TRUE(session.current_recruit() != nullptr);
    ASSERT_TRUE(session.current_recruit()->family == FAMILY_SOLDIER);
}

TEST(PickerCommon, hire_session_rename_hired)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 50000;

    og::ui::HireSession session(save, 0);
    int slot = session.hire();
    ASSERT_TRUE(slot >= 0);

    session.rename_hired(slot, "CustomName");
    ASSERT_TRUE(save.team_list[slot]->name == "CustomName");

    session.rename_hired(-1, "Ignored");
    session.rename_hired(MAX_TEAM_SIZE, "Ignored");
    session.rename_hired(3, "Ignored");
    ASSERT_TRUE(save.team_list[slot]->name == "CustomName");
}

TEST(PickerCommon, hire_session_team_full)
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
    ASSERT_TRUE(session.team_full());
    ASSERT_TRUE(session.hire() == -1);
}

TEST(PickerCommon, hire_session_not_enough_gold)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 0; // no gold

    og::ui::HireSession session(save, 0);
    ASSERT_TRUE(session.hire() == -1);
    ASSERT_TRUE(save.team_size == 0);
}

TEST(PickerCommon, hire_session_reports_team_and_handles_missing_empty_slot)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[2] = 50000;

    og::ui::HireSession session(save, 2);
    EXPECT_EQ(2, session.team_num());

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = MAX_TEAM_SIZE - 1;
    EXPECT_FALSE(session.team_full());
    EXPECT_EQ(-1, session.hire());
}

// --- TrainSession ---

TEST(PickerCommon, train_session_cycle)
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
    ASSERT_TRUE(!session.empty());
    ASSERT_TRUE(session.current_slot() == 0);
    ASSERT_TRUE(session.working_copy().name == "Alpha");

    session.next_member();
    ASSERT_TRUE(session.current_slot() == 2);
    ASSERT_TRUE(session.working_copy().name == "Beta");

    session.next_member();
    ASSERT_TRUE(session.current_slot() == 5);
    ASSERT_TRUE(session.working_copy().name == "Gamma");

    // Wraps back to first
    session.next_member();
    ASSERT_TRUE(session.current_slot() == 0);
    ASSERT_TRUE(session.working_copy().name == "Alpha");

    // Backward wraps to last
    session.prev_member();
    ASSERT_TRUE(session.current_slot() == 5);
    ASSERT_TRUE(session.working_copy().name == "Gamma");
}

TEST(PickerCommon, train_session_empty)
{
    SaveData save;
    save.team_size = 0;

    og::ui::TrainSession session(save);
    ASSERT_TRUE(session.empty());
}

TEST(PickerCommon, train_session_increase_decrease)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 50000;

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 3);
    ASSERT_TRUE(session.working_copy().strength == orig_str + 3);
    ASSERT_TRUE(session.current_cost() > 0);

    // Decrease back to original — cost should be 0
    session.decrease_stat(og::ui::TrainSession::Stat::Strength, 3);
    ASSERT_TRUE(session.working_copy().strength == orig_str);
    ASSERT_TRUE(session.current_cost() == 0);

    // Decrease below original — clamped to original
    session.decrease_stat(og::ui::TrainSession::Stat::Strength, 5);
    ASSERT_TRUE(session.working_copy().strength == orig_str);
}

TEST(PickerCommon, train_session_all_stat_arms_and_clamps)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_size = 1;
    save.m_totalcash[0] = 50000;

    og::ui::TrainSession session(save);
    const guy& original = session.original();
    const short orig_str = original.strength;
    const short orig_dex = original.dexterity;
    const short orig_con = original.constitution;
    const short orig_int = original.intelligence;
    const short orig_arm = original.armor;

    using Stat = og::ui::TrainSession::Stat;
    session.increase_stat(Stat::Strength);
    session.increase_stat(Stat::Dexterity);
    session.increase_stat(Stat::Constitution);
    session.increase_stat(Stat::Intelligence);
    session.increase_stat(Stat::Armor);
    EXPECT_GT(session.working_copy().strength, orig_str);
    EXPECT_GT(session.working_copy().dexterity, orig_dex);
    EXPECT_GT(session.working_copy().constitution, orig_con);
    EXPECT_GT(session.working_copy().intelligence, orig_int);
    EXPECT_GT(session.working_copy().armor, orig_arm);

    session.decrease_stat(Stat::Strength, 100);
    session.decrease_stat(Stat::Dexterity, 100);
    session.decrease_stat(Stat::Constitution, 100);
    session.decrease_stat(Stat::Intelligence, 100);
    session.decrease_stat(Stat::Armor, 100);
    EXPECT_EQ(orig_str, session.working_copy().strength);
    EXPECT_EQ(orig_dex, session.working_copy().dexterity);
    EXPECT_EQ(orig_con, session.working_copy().constitution);
    EXPECT_EQ(orig_int, session.working_copy().intelligence);
    EXPECT_EQ(orig_arm, session.working_copy().armor);
}

TEST(PickerCommon, train_session_handles_null_working_copy_paths)
{
    SaveData save;
    save.team_size = 1;

    og::ui::TrainSession session(save);
    ASSERT_TRUE(session.empty());

    using Stat = og::ui::TrainSession::Stat;
    session.next_member();
    session.prev_member();
    session.increase_stat(Stat::Strength);
    session.decrease_stat(Stat::Strength);
    session.set_team(3);

    EXPECT_EQ(0u, session.current_cost());
    EXPECT_FALSE(session.level_increased());
    EXPECT_FALSE(session.stats_increased());
    EXPECT_FALSE(session.accept());
}

TEST(PickerCommon, train_session_level_locks_stats)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);

    // Increase level
    session.increase_stat(og::ui::TrainSession::Stat::Level, 1);
    ASSERT_TRUE(session.level_increased());

    // Now stats should be locked — increase should be no-op
    short str_before = session.working_copy().strength;
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    ASSERT_TRUE(session.working_copy().strength == str_before);
}

TEST(PickerCommon, train_session_level_decrease_restores_original_exp_and_accepts)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    const std::uint32_t original_exp = session.original().exp;
    const short original_level = session.original().level;

    session.increase_stat(og::ui::TrainSession::Stat::Level, 2);
    ASSERT_GT(session.working_copy().level, original_level);
    session.decrease_stat(og::ui::TrainSession::Stat::Level, 2);
    EXPECT_EQ(original_level, session.working_copy().level);
    EXPECT_EQ(original_exp, session.working_copy().exp);

    session.increase_stat(og::ui::TrainSession::Stat::Level, 1);
    ASSERT_TRUE(session.accept(true));
    EXPECT_GT(save.team_list[0]->level, original_level);
}

TEST(PickerCommon, train_session_stats_lock_level)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);

    // Increase a stat
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    ASSERT_TRUE(session.stats_increased());

    // Now level should be locked — increase should be no-op
    short level_before = session.working_copy().level;
    session.increase_stat(og::ui::TrainSession::Stat::Level, 1);
    ASSERT_TRUE(session.working_copy().level == level_before);
}

TEST(PickerCommon, train_session_accept)
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
    ASSERT_TRUE(cost > 0);

    std::uint32_t gold_before = save.m_totalcash[0];
    ASSERT_TRUE(session.accept());
    ASSERT_TRUE(save.m_totalcash[0] == gold_before - cost);
    // Original team member should now have updated stats
    ASSERT_TRUE(save.team_list[0]->strength == orig_str + 5);
}

TEST(PickerCommon, train_session_accept_cant_afford)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 1; // almost no gold

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 10);
    ASSERT_TRUE(!session.accept()); // can't afford
    // Original should be unchanged
    ASSERT_TRUE(save.team_list[0]->strength == orig_str);
}

TEST(PickerCommon, train_session_accept_force)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 0; // no gold

    og::ui::TrainSession session(save);
    short orig_str = session.original().strength;

    session.increase_stat(og::ui::TrainSession::Stat::Strength, 5);
    ASSERT_TRUE(session.accept(true)); // force=true bypasses cost
    ASSERT_TRUE(save.team_list[0]->strength == orig_str + 5);
    ASSERT_TRUE(save.m_totalcash[0] == 0); // gold unchanged
}

// Bug A9: the DETAILS submenu's promote button mutates the REAL team member
// (family + level) while a TrainSession working copy is live. Without a
// resync, the stale working copy hides the promotion on screen and a later
// accept() statscopy()s the old family back over it.
TEST(PickerCommon, train_session_resync_if_promoted_adopts_external_promotion)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->level = 6;
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    ASSERT_EQ(FAMILY_MAGE, static_cast<int>(session.working_copy().family));

    // External promotion, exactly as create_detail_menu does it.
    save.team_list[0]->upgrade_to_level(1);
    save.team_list[0]->family = FAMILY_ARCHMAGE;

    ASSERT_TRUE(session.resync_if_promoted()) << "family mismatch must reload";
    ASSERT_EQ(FAMILY_ARCHMAGE, static_cast<int>(session.working_copy().family));
    ASSERT_EQ(1, static_cast<int>(session.working_copy().level));

    // The regression: accept() after a promotion must NOT revert the family.
    ASSERT_TRUE(session.accept());
    ASSERT_EQ(FAMILY_ARCHMAGE, static_cast<int>(save.team_list[0]->family));
    ASSERT_EQ(1, static_cast<int>(save.team_list[0]->level));
}

TEST(PickerCommon, train_session_resync_if_promoted_noop_preserves_pending_edits)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    const short orig_str = session.original().strength;
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 3);

    // No family change -> no reload; pending unaccepted edits survive.
    ASSERT_FALSE(session.resync_if_promoted());
    ASSERT_EQ(orig_str + 3, static_cast<int>(session.working_copy().strength));
}

TEST(PickerCommon, train_session_resync_if_promoted_handles_empty_session)
{
    SaveData save;
    save.team_size = 0;

    og::ui::TrainSession session(save);
    ASSERT_TRUE(session.empty());
    ASSERT_FALSE(session.resync_if_promoted());
}

// Issue #133 (self-heal): a stat edit made AFTER an external promotion must
// compose on the fresh post-promotion stats — no explicit resync call. Before
// the self-heal, the edit clamped the stale mage working copy against the
// promoted original and "put the old mage stats back" on accept.
TEST(PickerCommon, train_session_stat_edit_after_external_promotion_self_heals)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->upgrade_to_level(6);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    ASSERT_EQ(FAMILY_MAGE, static_cast<int>(session.working_copy().family));

    // External promotion, exactly as create_detail_menu does it.
    save.team_list[0]->upgrade_to_level(1);
    save.team_list[0]->family = FAMILY_ARCHMAGE;
    const short promoted_str = save.team_list[0]->strength;
    const short promoted_lvl = save.team_list[0]->level;

    // No explicit resync_if_promoted() — the edit itself must self-heal.
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);

    ASSERT_EQ(FAMILY_ARCHMAGE, static_cast<int>(session.working_copy().family));
    ASSERT_EQ(promoted_str + 1, static_cast<int>(session.working_copy().strength));
    ASSERT_EQ(static_cast<int>(promoted_lvl),
              static_cast<int>(session.working_copy().level));

    // And the composed edit commits on top of the promotion.
    ASSERT_TRUE(session.accept());
    ASSERT_EQ(FAMILY_ARCHMAGE, static_cast<int>(save.team_list[0]->family));
    ASSERT_EQ(promoted_str + 1, static_cast<int>(save.team_list[0]->strength));
    ASSERT_EQ(static_cast<int>(promoted_lvl),
              static_cast<int>(save.team_list[0]->level));
}

// Issue #133 (self-heal): accept() reached with a stale (pre-promotion)
// working copy must not statscopy the old family/stats back over the
// promotion. The pending stale edit is discarded and the accept is free.
TEST(PickerCommon, train_session_accept_after_external_promotion_self_heals)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->upgrade_to_level(6);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    og::ui::TrainSession session(save);
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 2); // pending edit

    save.team_list[0]->upgrade_to_level(1);
    save.team_list[0]->family = FAMILY_ARCHMAGE;
    const short promoted_str = save.team_list[0]->strength;

    const std::uint32_t gold_before = save.m_totalcash[0];
    ASSERT_TRUE(session.accept());
    ASSERT_EQ(FAMILY_ARCHMAGE, static_cast<int>(save.team_list[0]->family));
    ASSERT_EQ(static_cast<int>(promoted_str),
              static_cast<int>(save.team_list[0]->strength))
        << "stale pending edit must be discarded, not composed";
    ASSERT_EQ(1, static_cast<int>(save.team_list[0]->level));
    ASSERT_EQ(gold_before, save.m_totalcash[0]) << "no-op accept must be free";
}

TEST(PickerCommon, train_session_set_team_clamps_and_lobby_revocation_invalidates_original)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 999999;

    EditableSlotPickerLobbyClient client;
    client.editable_slots.fill(false);
    client.editable_slots[0] = true;
    ActivePickerLobbyClientGuard guard(&client);

    og::ui::TrainSession session(save);
    ASSERT_FALSE(session.empty());

    session.set_team(-10);
    EXPECT_EQ(0, session.working_copy().teamnum);
    session.set_team(99);
    EXPECT_EQ(SCORE_TEAM_COUNT - 1, session.working_copy().teamnum);

    client.editable_slots[0] = false;
    EXPECT_TRUE(session.empty());
    EXPECT_EQ(0u, session.current_cost());
    EXPECT_FALSE(session.accept());
}

// --- compute_derived_stats ---

TEST(PickerCommon, compute_derived_stats)
{
    init_family_registry();
    guy g(FAMILY_SOLDIER);

    auto ds = og::ui::compute_derived_stats(g, 100.0f, 20.0f, 5.0f, 8.0f);
    // HP = ceil(100 + hp_bonus), MP = ceil(mp_bonus)
    ASSERT_TRUE(ds.hp >= 100.0f);
    ASSERT_TRUE(ds.mp >= 0.0f);
    ASSERT_TRUE(ds.atk >= 20.0f);
    ASSERT_TRUE(ds.def >= 0.0f);
    ASSERT_TRUE(ds.spd >= 5.0f);
    ASSERT_TRUE(ds.atk_spd > 0.0f);
}

TEST(PickerCommon, compute_derived_stats_min_fire_freq)
{
    init_family_registry();
    guy g(FAMILY_SOLDIER);
    // base_fire_freq of 0 should be clamped to 1 to avoid div-by-zero
    auto ds = og::ui::compute_derived_stats(g, 50.0f, 10.0f, 3.0f, 0.0f);
    ASSERT_TRUE(ds.atk_spd > 0.0f);
    ASSERT_TRUE(ds.atk_spd <= 10.0f);
}

// --- cycle_difficulty ---

TEST(PickerCommon, cycle_difficulty)
{
    ASSERT_TRUE(og::ui::cycle_difficulty(0) == 1);
    ASSERT_TRUE(og::ui::cycle_difficulty(1) == 2);
    ASSERT_TRUE(og::ui::cycle_difficulty(2) == 0); // wraps
}

// --- toggle_allied_mode / is_allied_mode ---

TEST(PickerCommon, toggle_allied_mode)
{
    SaveData save;
    // SaveData constructor sets allied_mode = 1
    bool initial = og::ui::is_allied_mode(save);

    og::ui::toggle_allied_mode(save);
    ASSERT_TRUE(og::ui::is_allied_mode(save) != initial);

    og::ui::toggle_allied_mode(save);
    ASSERT_TRUE(og::ui::is_allied_mode(save) == initial);
}

// --- CTF match settings ---

TEST(PickerCommon, cycle_ctf_team_count_wraps_auto_2_3_4)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.ctf_team_count)
        << "default is Auto (every team the map authors)";

    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(2, (int)save.ctf_team_count);
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(3, (int)save.ctf_team_count);
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(4, (int)save.ctf_team_count);
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(0, (int)save.ctf_team_count) << "cycle wraps back to Auto";

    // Out-of-range values normalize back into the cycle.
    save.ctf_team_count = 9;
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(0, (int)save.ctf_team_count);
}

TEST(PickerCommon, cycle_ctf_capture_limit_sequence)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.ctf_capture_limit) << "default is map/default (0)";

    const int expected[] = {1, 3, 5, 10, 0, 1};
    for (int step : expected)
    {
        og::ui::cycle_ctf_capture_limit(save);
        ASSERT_EQ(step, (int)save.ctf_capture_limit);
    }

    // Unknown stored values fall back to the map-default sentinel.
    save.ctf_capture_limit = 42;
    og::ui::cycle_ctf_capture_limit(save);
    ASSERT_EQ(0, (int)save.ctf_capture_limit);
}

TEST(PickerCommon, format_ctf_labels)
{
    SaveData save;
    ASSERT_EQ("Teams: Auto", og::ui::format_ctf_teams_label(save));
    save.ctf_team_count = 4;
    ASSERT_EQ("Teams: 4", og::ui::format_ctf_teams_label(save));

    ASSERT_EQ("Limit: Map", og::ui::format_ctf_caps_label(save));
    save.ctf_capture_limit = 5;
    ASSERT_EQ("Limit: 5", og::ui::format_ctf_caps_label(save));

    // The SDL team-build buttons are 80px faces drawing 6px/char centered
    // text with no clipping: every label must stay inside the classic
    // 12-char budget (longest is "Limit: 10" / "Teams: Auto").
    save.ctf_team_count = 0;
    save.ctf_capture_limit = 10;
    ASSERT_LE(og::ui::format_ctf_teams_label(save).size(), 12u);
    ASSERT_LE(og::ui::format_ctf_caps_label(save).size(), 12u);
}

TEST(PickerCommon, is_ctf_campaign_matches_id_exactly)
{
    SaveData save;
    ASSERT_FALSE(og::ui::is_ctf_campaign(save))
        << "the classic campaign is not CTF";
    save.current_campaign = "org.openglad.ctf";
    ASSERT_TRUE(og::ui::is_ctf_campaign(save));
    save.current_campaign = "org.openglad.ctf2";
    ASSERT_FALSE(og::ui::is_ctf_campaign(save))
        << "the match is exact, not a prefix";
}

// --- set_player_count ---

TEST(PickerCommon, set_player_count)
{
    SaveData save;
    og::ui::set_player_count(save, 3);
    ASSERT_TRUE(save.numplayers == 3);

    og::ui::set_player_count(save, 1);
    ASSERT_TRUE(save.numplayers == 1);
}

// --- set_player_count with 0 (spectator mode) ---

TEST(PickerCommon, set_player_count_zero)
{
    SaveData save;
    // Start at a normal count
    og::ui::set_player_count(save, 2);
    ASSERT_TRUE(save.numplayers == 2);

    // Set to 0 (spectator)
    og::ui::set_player_count(save, 0);
    ASSERT_TRUE(save.numplayers == 0);

    // Round-trip: set back to a normal count
    og::ui::set_player_count(save, 4);
    ASSERT_TRUE(save.numplayers == 4);

    // And back to 0
    og::ui::set_player_count(save, 0);
    ASSERT_TRUE(save.numplayers == 0);
}

// --- is_spectator_mode ---

TEST(PickerCommon, is_spectator_mode)
{
    SaveData save;

    // Default numplayers is 1
    ASSERT_TRUE(!og::ui::is_spectator_mode(save));

    og::ui::set_player_count(save, 0);
    ASSERT_TRUE(og::ui::is_spectator_mode(save));

    og::ui::set_player_count(save, 1);
    ASSERT_TRUE(!og::ui::is_spectator_mode(save));

    og::ui::set_player_count(save, 4);
    ASSERT_TRUE(!og::ui::is_spectator_mode(save));
}

// --- ensure_team_populated ---

TEST(PickerCommon, ensure_team_populated_empty_families)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    // Empty families list -> should add a FAMILY_SOLDIER
    og::ui::ensure_team_populated(save);
    ASSERT_TRUE(save.team_size == 1);
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_SOLDIER);
}

TEST(PickerCommon, ensure_team_populated_with_families)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    std::vector<int> families = {FAMILY_MAGE, FAMILY_ARCHER};
    og::ui::ensure_team_populated(save, families);
    ASSERT_TRUE(save.team_size == 2);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_MAGE);
    ASSERT_TRUE(save.team_list[1]->family == FAMILY_ARCHER);
}

TEST(PickerCommon, ensure_team_populated_noop_if_has_members)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_ELF);
    save.team_size = 1;

    // Should be a no-op since team already has members
    og::ui::ensure_team_populated(save, {FAMILY_MAGE});
    ASSERT_TRUE(save.team_size == 1);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_ELF);
}

// --- for_each_team_member ---

TEST(PickerCommon, for_each_team_member)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Alpha";
    save.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[2]->name = "Beta";
    save.team_size = 2;

    int count = 0;
    std::vector<int> slots;
    og::ui::for_each_team_member(save, [&](int slot, const guy& member) {
        count++;
        slots.push_back(slot);
        if (slot == 0)
        {
            ASSERT_TRUE(member.name == "Alpha");
        }
        if (slot == 2)
        {
            ASSERT_TRUE(member.name == "Beta");
        }
    });

    ASSERT_TRUE(count == 2);
    ASSERT_TRUE(slots.size() == 2);
    ASSERT_TRUE(slots[0] == 0);
    ASSERT_TRUE(slots[1] == 2);
}

TEST(PickerCommon, for_each_team_member_empty)
{
    SaveData save;
    save.team_size = 0;

    int count = 0;
    og::ui::for_each_team_member(save, [&](int, const guy&) {
        count++;
    });
    ASSERT_TRUE(count == 0);
}

// --- format_difficulty_label ---

TEST(PickerCommon, format_difficulty_label)
{
    ASSERT_TRUE(og::ui::format_difficulty_label(0) == "Difficulty: Skirmish");
    ASSERT_TRUE(og::ui::format_difficulty_label(1) == "Difficulty: Battle");
    ASSERT_TRUE(og::ui::format_difficulty_label(2) == "Difficulty: Slaughter");
}

// --- format_allied_mode_label ---

TEST(PickerCommon, format_allied_mode_label)
{
    SaveData save;
    save.allied_mode = 0;
    ASSERT_TRUE(og::ui::format_allied_mode_label(save) == "PVP: Enemy");

    save.allied_mode = 1;
    ASSERT_TRUE(og::ui::format_allied_mode_label(save) == "PVP: Ally");
}

// --- collect_team_families ---

TEST(PickerCommon, collect_team_families)
{
    SaveData save;
    save.team_size = 0;

    // Empty team -> empty vector
    std::vector<int> families = og::ui::collect_team_families(save);
    ASSERT_TRUE(families.empty());

    // Add some team members
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[2] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[4] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_size = 3;

    families = og::ui::collect_team_families(save);
    ASSERT_TRUE(families.size() == 3);
    ASSERT_TRUE(families[0] == FAMILY_SOLDIER);
    ASSERT_TRUE(families[1] == FAMILY_MAGE);
    ASSERT_TRUE(families[2] == FAMILY_ARCHER);
}

// --- initialize_starting_team ---

TEST(PickerCommon, initialize_starting_team_empty)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 0;
    save.totalcash = 0;

    // Should set gold and populate
    og::ui::initialize_starting_team(save);
    ASSERT_TRUE(save.team_size >= 1);
    ASSERT_TRUE(save.m_totalcash[0] == og::ui::kNewGameStartingGold);
    ASSERT_TRUE(save.totalcash == og::ui::kNewGameStartingGold);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_SOLDIER);
}

TEST(PickerCommon, initialize_starting_team_noop_if_populated)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_ELF);
    save.team_size = 1;
    save.m_totalcash[0] = 100;

    // Should be no-op
    og::ui::initialize_starting_team(save, {FAMILY_MAGE});
    ASSERT_TRUE(save.team_size == 1);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_ELF);
    ASSERT_TRUE(save.m_totalcash[0] == 100); // unchanged
}

TEST(PickerCommon, initialize_starting_team_with_families)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;

    og::ui::initialize_starting_team(save, {FAMILY_MAGE, FAMILY_ARCHER});
    ASSERT_TRUE(save.team_size == 2);
    ASSERT_TRUE(save.team_list[0]->family == FAMILY_MAGE);
    ASSERT_TRUE(save.team_list[1]->family == FAMILY_ARCHER);
    ASSERT_TRUE(save.m_totalcash[0] == og::ui::kNewGameStartingGold);
}

// --- save_error_string ---

TEST(PickerCommon, save_error_string)
{
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::None), "none") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::OpenReadFailed), "open_read_failed") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::OpenWriteFailed), "open_write_failed") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::ReadFailed), "read_failed") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::WriteFailed), "write_failed") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::InvalidHeader), "invalid_header") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::UnsupportedVersion), "unsupported_version") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(SaveDataIoError::CampaignLoadFailed), "campaign_load_failed") == 0);
    ASSERT_TRUE(std::strcmp(og::ui::save_error_string(static_cast<SaveDataIoError>(999)), "unknown") == 0);
}

// --- reset_for_new_game sets gold ---

TEST(PickerCommon, reset_for_new_game_sets_gold)
{
    SaveData save;
    save.totalcash = 0;
    save.m_totalcash[0] = 0;

    og::ui::reset_for_new_game(save);

    // m_totalcash is set by SaveData::reset() to 5000
    ASSERT_TRUE(save.m_totalcash[0] == 5000);
    // totalcash is now also set by reset_for_new_game
    ASSERT_TRUE(save.totalcash == og::ui::kNewGameStartingGold);
}

// --- CTF scenario-troops toggle & label ---

TEST(PickerCommon, toggle_ctf_scenario_troops_cycles_binary)
{
    SaveData save;
    ASSERT_EQ(0, save.ctf_strip_scenario_troops);
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ(1, save.ctf_strip_scenario_troops);
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ(0, save.ctf_strip_scenario_troops);
}

TEST(PickerCommon, format_ctf_troops_label_strings_fit_budget)
{
    SaveData save;
    ASSERT_EQ("Troops: Scen", og::ui::format_ctf_troops_label(save));
    save.ctf_strip_scenario_troops = 1;
    ASSERT_EQ("Troops: Own", og::ui::format_ctf_troops_label(save));
    ASSERT_LE(og::ui::format_ctf_troops_label(save).size(), 12u);
    save.ctf_strip_scenario_troops = 0;
    ASSERT_LE(og::ui::format_ctf_troops_label(save).size(), 12u);
}

TEST(PickerCommon, is_ctf_campaign_matches_constant)
{
    SaveData save;
    save.current_campaign = std::string(og::kCtfCampaignId);
    ASSERT_TRUE(og::ui::is_ctf_campaign(save));
    ASSERT_EQ("org.openglad.ctf", std::string(og::kCtfCampaignId));
    save.current_campaign = "org.openglad.gladiator";
    ASSERT_FALSE(og::ui::is_ctf_campaign(save));
}

// --- Team choice helpers ---

TEST(PickerCommon, team_has_members_and_set_preferred_team)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 2;
    save.team_size = 2;
    save.my_team = 1;

    ASSERT_TRUE(og::ui::team_has_members(save, 1));
    ASSERT_TRUE(og::ui::team_has_members(save, 2));
    ASSERT_FALSE(og::ui::team_has_members(save, 0));
    ASSERT_FALSE(og::ui::team_has_members(save, 3));

    ASSERT_TRUE(og::ui::set_preferred_team(save, 2));
    ASSERT_EQ(2, save.my_team);

    // Empty team and out-of-range teams are rejected without mutation.
    ASSERT_FALSE(og::ui::set_preferred_team(save, 0));
    ASSERT_EQ(2, save.my_team);
    ASSERT_FALSE(og::ui::set_preferred_team(save, -1));
    ASSERT_FALSE(og::ui::set_preferred_team(save, 4));
    ASSERT_EQ(2, save.my_team);
}

TEST(PickerCommon, cycle_guy_team_wraps_and_rejects_empty)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 3;
    save.team_size = 1;

    ASSERT_EQ(0, og::ui::cycle_guy_team(save, 0, +1)) << "3 + 1 wraps to 0";
    ASSERT_EQ(0, save.team_list[0]->teamnum);
    ASSERT_EQ(3, og::ui::cycle_guy_team(save, 0, -1)) << "0 - 1 wraps to 3";
    ASSERT_EQ(3, save.team_list[0]->teamnum);

    ASSERT_EQ(-1, og::ui::cycle_guy_team(save, 1, +1)) << "empty slot";
    ASSERT_EQ(-1, og::ui::cycle_guy_team(save, -1, +1));
    ASSERT_EQ(-1, og::ui::cycle_guy_team(save, MAX_TEAM_SIZE, +1));
}

TEST(PickerCommon, derive_local_seat_teams_hoists_my_team)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 2;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 1;
    save.team_list[2] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[2]->teamnum = 2;
    save.team_size = 3;

    // Without a hoist (my_team has no members): slot order.
    save.my_team = 3;
    ASSERT_EQ((std::vector<short>{2, 1}), og::ui::derive_local_seat_teams(save));

    // my_team = 1 hoists team 1 to the P1 seat.
    save.my_team = 1;
    ASSERT_EQ((std::vector<short>{1, 2}), og::ui::derive_local_seat_teams(save));
}

TEST(PickerCommon, derive_local_seat_teams_excludes_team_zero_unless_hoisted)
{
    // Mirrors game.cpp's view_teams rule: nonzero teams only, except the
    // preferred team which is hoisted even when it is 0.
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 1;
    save.team_size = 2;

    save.my_team = 1;
    ASSERT_EQ((std::vector<short>{1}), og::ui::derive_local_seat_teams(save));

    save.my_team = 0;
    ASSERT_EQ((std::vector<short>{0, 1}), og::ui::derive_local_seat_teams(save));
}

TEST(PickerCommon, format_team_row_label_variants_fit_budget)
{
    const std::string yours =
        og::ui::format_team_row_label(0, 2, false, false, true, "(P1)");
    ASSERT_EQ("RED TEAM (P1) 2 HEROES", yours);

    const std::string not_on_map =
        og::ui::format_team_row_label(3, 0, true, false, false, "(P4)");
    ASSERT_EQ("YELLOW TEAM (P4) NOT ON MAP", not_on_map);

    const std::string bots =
        og::ui::format_team_row_label(1, 0, true, true, false, "");
    ASSERT_EQ("GREEN TEAM BOTS", bots);

    const std::string ctf_humans =
        og::ui::format_team_row_label(2, 24, true, true, true, "(P2)");
    ASSERT_EQ("BLUE TEAM (P2) 24 HEROES", ctf_humans);

    for (const std::string& label : {yours, not_on_map, bots, ctf_humans})
        ASSERT_LE(label.size(), 30u) << label;
}

// --- Campaign ordering ---

TEST(PickerCommon, order_campaigns_for_select_uses_the_shelf_order)
{
    // The full shipped set arrives in alphabetical list_campaigns() order
    // and leaves in shelf order: classics, the two original story
    // campaigns, the multiplayer packages, concept trailing.
    std::list<std::string> ids = {
        "org.openglad.arenas",
        "org.openglad.concept",
        "org.openglad.ctf",
        "org.openglad.gladiator",
        "org.openglad.longseason",
        "org.openglad.tower",
        "org.openglad.tryxian",
        "org.openglad.westlands",
    };
    const std::list<std::string> shelf = {
        "org.openglad.gladiator",
        "org.openglad.tryxian",
        "org.openglad.westlands",
        "org.openglad.longseason",
        "org.openglad.ctf",
        "org.openglad.arenas",
        "org.openglad.tower",
        "org.openglad.concept",
    };
    og::ui::order_campaigns_for_select(ids);
    ASSERT_EQ(shelf, ids);

    // Already ordered: stable no-op.
    og::ui::order_campaigns_for_select(ids);
    ASSERT_EQ(shelf, ids);

    // User-made packages keep their relative order after every shelved id.
    std::list<std::string> with_extras = {
        "a.campaign",
        "org.openglad.ctf",
        "b.campaign",
        "org.openglad.gladiator",
    };
    og::ui::order_campaigns_for_select(with_extras);
    ASSERT_EQ((std::list<std::string>{
                  "org.openglad.gladiator",
                  "org.openglad.ctf",
                  "a.campaign",
                  "b.campaign",
              }),
              with_extras);

    // No shelved ids at all: untouched.
    std::list<std::string> no_anchors = {"a.campaign", "b.campaign"};
    og::ui::order_campaigns_for_select(no_anchors);
    ASSERT_EQ((std::list<std::string>{"a.campaign", "b.campaign"}), no_anchors);

    // Only CTF: a single-element list stays put.
    std::list<std::string> only_ctf = {"org.openglad.ctf"};
    og::ui::order_campaigns_for_select(only_ctf);
    ASSERT_EQ((std::list<std::string>{"org.openglad.ctf"}), only_ctf);

    // Empty list survives.
    std::list<std::string> empty;
    og::ui::order_campaigns_for_select(empty);
    ASSERT_TRUE(empty.empty());
}

// The networked-lobby filter drops ONLY the tower (local-only mode) and only
// when the session is actually networked; local shelves keep everything.
TEST(PickerCommon, filter_campaigns_for_networked_lobby_drops_tower_only)
{
    const std::list<std::string> full = {
        "org.openglad.gladiator",
        "org.openglad.tower",
        "org.openglad.ctf",
        "a.campaign",
    };

    // Local session: untouched (tower is playable locally).
    std::list<std::string> local = full;
    og::ui::filter_campaigns_for_networked_lobby(local, false);
    ASSERT_EQ(full, local);

    // Networked session: tower removed, everything else keeps its order.
    std::list<std::string> networked = full;
    og::ui::filter_campaigns_for_networked_lobby(networked, true);
    ASSERT_EQ((std::list<std::string>{
                  "org.openglad.gladiator",
                  "org.openglad.ctf",
                  "a.campaign",
              }),
              networked);

    // No tower present: a networked filter is a no-op.
    og::ui::filter_campaigns_for_networked_lobby(networked, true);
    ASSERT_EQ(3u, networked.size());

    // Empty list survives both ways.
    std::list<std::string> empty;
    og::ui::filter_campaigns_for_networked_lobby(empty, true);
    og::ui::filter_campaigns_for_networked_lobby(empty, false);
    ASSERT_TRUE(empty.empty());
}

// Unknown packages fall back to raw-id labels with no "[id]" stutter, even
// when those raw ids collide as "titles". (Real duplicate-title packages are
// exercised in test_platform_headless.cpp, which can install fixtures.)
TEST(PickerCommon, campaign_select_labels_fall_back_to_raw_ids)
{
    const std::vector<std::string> ids = {
        "org.openglad.test_absent_a",
        "org.openglad.test_absent_b",
        "org.openglad.test_absent_a",
    };
    ASSERT_EQ(ids, og::ui::format_campaign_select_labels(ids));
    ASSERT_TRUE(og::ui::format_campaign_select_labels({}).empty());
}

// --- Scenario roster report (View Level) ---

namespace {

loader& report_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

// TestGameWorld wired to a CTF-aware loader so flag/control-point spawns run
// the production entity factory path (mirrors test_ctf_core's fixture).
struct ReportWorld : TestGameWorld
{
    explicit ReportWorld(bool ctf_map)
        : TestGameWorld(ctf_map ? 510 : 1)
    {
        loader* game_loader = &report_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = ctf_map ? GameWorld::TYPE_CTF : 0;
    }

    walker* spawn_living_named(int family, int team, int guy_level,
                               const char* name)
    {
        walker* w = world().add_ob(Order::Living, family);
        w->setxy(160, 160);
        w->set_team_num(static_cast<unsigned char>(team));
        if (w->stats() != nullptr)
        {
            w->stats()->set_level(guy_level);
            if (name != nullptr)
                w->stats()->name = name;
        }
        return w;
    }

    walker* spawn_generator(int family, int team)
    {
        walker* w = world().add_ob(Order::Generator, family);
        w->setxy(256, 256);
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }

    walker* spawn_flag(int team, int flag_level = 0)
    {
        walker* flag = world().add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
        flag->setxy(96, 96);
        flag->set_team_num(static_cast<unsigned char>(team));
        if (flag_level > 0 && flag->stats() != nullptr)
            flag->stats()->set_level(flag_level);
        return flag;
    }

    walker* spawn_point()
    {
        walker* point = world().add_fx_ob(Order::Treasure, og::FAMILY_CTF_POINT);
        point->setxy(320, 320);
        return point;
    }

    walker* spawn_anchor(int team)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        marker->setxy(128, 128);
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }
};

const og::ui::ScenarioRosterRow* find_named_row(
    const og::ui::ScenarioRosterReport& report, const std::string& name)
{
    for (const auto& row : report.rows)
    {
        if (row.named && row.name == name)
            return &row;
    }
    return nullptr;
}

const og::ui::ScenarioRosterRow* find_group_row(
    const og::ui::ScenarioRosterReport& report, short team, short family,
    int level)
{
    for (const auto& row : report.rows)
    {
        if (!row.named && !row.is_generator && row.team == team &&
            row.family == family && row.level == level)
        {
            return &row;
        }
    }
    return nullptr;
}

const og::ui::ScenarioRosterRow* find_generator_row(
    const og::ui::ScenarioRosterReport& report, short team)
{
    for (const auto& row : report.rows)
    {
        if (row.is_generator && row.team == team)
            return &row;
    }
    return nullptr;
}

bool any_line_contains(const std::vector<std::string>& lines,
                       const std::string& needle)
{
    for (const auto& line : lines)
    {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST(PickerCommon, scenario_report_groups_classic_roster)
{
    ReportWorld fx(false);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 5, nullptr);
    fx.spawn_living_named(FAMILY_ARCHER, 1, 5, "GONZO");
    fx.spawn_generator(FAMILY_TENT, 1);
    fx.spawn_generator(FAMILY_TOWER, 1);
    walker* corpse = fx.spawn_living_named(FAMILY_ORC, 1, 2, nullptr);
    corpse->set_dead(1);

    SaveData save;
    save.my_team = 0;
    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_FALSE(report.is_ctf);
    EXPECT_EQ(0, report.your_team);

    const auto* grouped = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(grouped != nullptr);
    EXPECT_EQ(2, grouped->count) << "same family+level group";
    const auto* solo = find_group_row(report, 0, FAMILY_SOLDIER, 5);
    ASSERT_TRUE(solo != nullptr);
    EXPECT_EQ(1, solo->count) << "different level is a separate row";

    const auto* named = find_named_row(report, "GONZO");
    ASSERT_TRUE(named != nullptr);
    EXPECT_EQ(1, named->team);
    EXPECT_EQ(5, named->level);
    EXPECT_EQ(FAMILY_ARCHER, named->family);

    const auto* generators = find_generator_row(report, 1);
    ASSERT_TRUE(generators != nullptr);
    EXPECT_EQ(2, generators->count) << "generators aggregate per team";

    EXPECT_TRUE(find_group_row(report, 1, FAMILY_ORC, 2) == nullptr)
        << "dead walkers are not part of the roster";

    // Rows arrive team-major.
    for (std::size_t i = 1; i < report.rows.size(); ++i)
        EXPECT_LE(report.rows[i - 1].team, report.rows[i].team);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "RED TEAM (YOURS)"))
        << "score-team headers use the shared color names";
    EXPECT_TRUE(any_line_contains(lines, "GREEN TEAM"));
    EXPECT_TRUE(any_line_contains(lines, "2x SOLDIER Lv 3"));
    EXPECT_TRUE(any_line_contains(lines, "GONZO - ARCHER Lv 5"));
    EXPECT_TRUE(any_line_contains(lines, "2x GENERATOR"));
    EXPECT_FALSE(any_line_contains(lines, "CTF"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

TEST(PickerCommon, scenario_report_ctf_sections_and_strip_annotations)
{
    ReportWorld fx(true);
    fx.spawn_flag(0, /*level=*/7); // map capture limit 7
    fx.spawn_flag(1);
    fx.spawn_point();
    fx.spawn_anchor(0);
    fx.spawn_anchor(0);
    fx.spawn_anchor(1);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr); // roster team troops
    fx.spawn_generator(FAMILY_TENT, 0);
    fx.spawn_living_named(FAMILY_ORC, 1, 4, nullptr);     // bot team, kept
    fx.spawn_living_named(FAMILY_ELF, 2, 2, nullptr);     // no flag: inactive
    fx.spawn_living_named(FAMILY_ELF, 5, 9, nullptr);     // non-score team

    SaveData save;
    save.current_campaign = std::string(og::kCtfCampaignId);
    save.my_team = 0;
    save.ctf_strip_scenario_troops = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.is_ctf);
    EXPECT_TRUE(report.ctf_will_activate);
    EXPECT_TRUE(report.team_has_flag[0]);
    EXPECT_TRUE(report.team_has_flag[1]);
    EXPECT_FALSE(report.team_has_flag[2]);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2]);
    EXPECT_EQ(1, report.cp_count);
    EXPECT_EQ(2, report.team_anchor_count[0]);
    EXPECT_EQ(1, report.team_anchor_count[1]);
    EXPECT_EQ(7, report.capture_limit) << "map flag level drives the limit";

    const auto* roster_troops = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(roster_troops != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::TroopsOff,
              roster_troops->strip_reason);
    const auto* roster_gen = find_generator_row(report, 0);
    ASSERT_TRUE(roster_gen != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::TroopsOff, roster_gen->strip_reason);

    const auto* bot_troops = find_group_row(report, 1, FAMILY_ORC, 4);
    ASSERT_TRUE(bot_troops != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::None, bot_troops->strip_reason);

    const auto* inactive = find_group_row(report, 2, FAMILY_ELF, 2);
    ASSERT_TRUE(inactive != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::InactiveTeam, inactive->strip_reason);

    // The sim's inactive-team strip also removes non-score teams (>= 4) on
    // an activating map; the preview must annotate them the same way.
    const auto* non_score = find_group_row(report, 5, FAMILY_ELF, 9);
    ASSERT_TRUE(non_score != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::InactiveTeam,
              non_score->strip_reason);

    EXPECT_TRUE(report.any_troops_off);
    EXPECT_TRUE(report.any_inactive);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "CTF: 2 FLAG TEAMS, 1 CONTROL POINTS"));
    EXPECT_TRUE(any_line_contains(lines, "CAPTURE LIMIT: 7"));
    EXPECT_TRUE(any_line_contains(lines, "RED FLAG  ANCHORS: 2  ACTIVE"));
    EXPECT_TRUE(any_line_contains(lines, "RED TEAM (YOURS)"));
    EXPECT_TRUE(any_line_contains(lines, "BLUE TEAM"))
        << "score teams keep color-name headers";
    EXPECT_TRUE(any_line_contains(lines, "TEAM 5"))
        << "non-score teams keep the raw index header";
    EXPECT_TRUE(any_line_contains(lines, "1x SOLDIER Lv 3*"));
    EXPECT_TRUE(any_line_contains(lines, "1x ELF Lv 2+"));
    EXPECT_TRUE(any_line_contains(lines, "1x ELF Lv 9+"))
        << "non-score teams carry the inactive strip suffix";
    EXPECT_TRUE(any_line_contains(lines, "* REMOVED: TROOPS OFF"));
    EXPECT_TRUE(any_line_contains(lines, "+ REMOVED: INACTIVE TEAM"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

TEST(PickerCommon, scenario_report_requested_limit_overrides_map_and_allied_collapses)
{
    ReportWorld fx(true);
    fx.spawn_flag(0, /*level=*/7);
    fx.spawn_flag(1);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_ORC, 1, 4, nullptr);

    SaveData save;
    save.current_campaign = std::string(og::kCtfCampaignId);
    save.allied_mode = 1;
    save.my_team = 2; // ignored when allied
    save.ctf_capture_limit = 5;
    save.ctf_strip_scenario_troops = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 3; // allied collapses roster teams to {0}
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_EQ(5, report.capture_limit) << "explicit request beats the map";
    EXPECT_EQ(0, report.your_team) << "allied mode plays as team 0";

    const auto* team0 = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(team0 != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::TroopsOff, team0->strip_reason)
        << "allied roster-team predicate collapses to team 0";
    const auto* team1 = find_group_row(report, 1, FAMILY_ORC, 4);
    ASSERT_TRUE(team1 != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::None, team1->strip_reason);
}

TEST(PickerCommon, scenario_report_non_activating_ctf_keeps_classic_rules)
{
    ReportWorld fx(true);
    fx.spawn_flag(0); // single flag team: the match will not activate
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);

    SaveData save;
    save.current_campaign = std::string(og::kCtfCampaignId);
    save.ctf_strip_scenario_troops = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.is_ctf);
    EXPECT_FALSE(report.ctf_will_activate);
    const auto* troops = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(troops != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::None, troops->strip_reason)
        << "the strip is inert on non-activating CTF maps";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "CTF INACTIVE"));
}

TEST(PickerCommon, scenario_report_troops_strip_annotates_outside_ctf_campaign)
{
    // The sim consumes ctf_strip_scenario_troops on ANY TYPE_CTF map (no
    // campaign gate); the preview must mirror it exactly or a custom CTF map
    // outside the shipped campaign strips in-sim with no '*' in the viewer.
    ReportWorld fx(true);
    fx.spawn_flag(0);
    fx.spawn_flag(1);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_ORC, 1, 4, nullptr);

    SaveData save;
    save.current_campaign = std::string(og::kDefaultCampaignId);
    save.my_team = 0;
    save.ctf_strip_scenario_troops = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(fx.world(), save);

    EXPECT_TRUE(report.is_ctf);
    EXPECT_TRUE(report.ctf_will_activate);
    const auto* troops = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(troops != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::TroopsOff, troops->strip_reason);
    const auto* bot_troops = find_group_row(report, 1, FAMILY_ORC, 4);
    ASSERT_TRUE(bot_troops != nullptr);
    EXPECT_EQ(og::ui::ScenarioStripReason::None, bot_troops->strip_reason);
}

// --- TEAMS detail pagination (the per-team '>' pager) -----------------------

TEST(PickerCommon, paginate_team_detail_packs_greedily_and_never_overflows)
{
    const std::vector<std::string> names = {
        "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
        "Hotel"};

    // 56 joined chars fit one 56-char slice...
    const auto one = og::ui::paginate_team_detail_pages(names, 56);
    ASSERT_EQ(1u, one.size());
    EXPECT_EQ("Alpha, Bravo, Charlie, Delta, Echo, Foxtrot, Golf, Hotel",
              one[0]);

    // ... and split greedily at the TEAMS screen's 26-char paged budget.
    const auto pages = og::ui::paginate_team_detail_pages(names, 26);
    ASSERT_EQ(3u, pages.size());
    EXPECT_EQ("Alpha, Bravo, Charlie", pages[0]);
    EXPECT_EQ("Delta, Echo, Foxtrot, Golf", pages[1]);
    EXPECT_EQ("Hotel", pages[2]);
    for (const std::string& page : pages)
        EXPECT_LE(page.size(), 26u) << page;
}

TEST(PickerCommon, paginate_team_detail_edge_cases)
{
    // Empty input still yields exactly one (empty) page: page math never
    // divides by zero and the screen draws nothing.
    const auto empty = og::ui::paginate_team_detail_pages({}, 26);
    ASSERT_EQ(1u, empty.size());
    EXPECT_TRUE(empty[0].empty());

    // A single oversized item is clipped inside the budget with a visible
    // '..' marker — this is the one truncation the unpaged screen would
    // otherwise hide (one page means no '>' pager and no p/N indicator).
    const auto truncated = og::ui::paginate_team_detail_pages(
        {"AbsurdlyLongLobbyPlayerName [RDY]"}, 10);
    ASSERT_EQ(1u, truncated.size());
    EXPECT_EQ("Absurdly..", truncated[0]);
    EXPECT_LE(truncated[0].size(), 10u);

    // Items exactly at the budget each take their own page.
    const auto exact = og::ui::paginate_team_detail_pages(
        {"0123456789", "abcdefghij"}, 10);
    ASSERT_EQ(2u, exact.size());
    EXPECT_EQ("0123456789", exact[0]);
    EXPECT_EQ("abcdefghij", exact[1]);

    // A degenerate budget still terminates with one truncated char per item
    // (budgets of <= 2 chars have no room for the '..' marker).
    const auto tiny = og::ui::paginate_team_detail_pages({"ab", "cd"}, 0);
    ASSERT_EQ(2u, tiny.size());
    EXPECT_EQ("a", tiny[0]);
    EXPECT_EQ("c", tiny[1]);
}

// --- Difficulty submenu match rules (cyclers + exact label pins) ---

TEST(PickerCommon, cycle_respawn_mode_sequence)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.respawn_mode) << "default is Off (classic behavior)";

    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(1, (int)save.respawn_mode) << "Off -> Heroes";
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(2, (int)save.respawn_mode) << "Heroes -> Everyone";
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(0, (int)save.respawn_mode) << "Everyone wraps back to Off";
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(1, (int)save.respawn_mode) << "the cycle repeats";

    // Out-of-set stored values normalize back to the Off default.
    save.respawn_mode = 7;
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(0, (int)save.respawn_mode);
    save.respawn_mode = -3;
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(0, (int)save.respawn_mode);
}

TEST(PickerCommon, cycle_respawn_delay_sequence)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.ctf_respawn_ticks)
        << "default is 0 (normal = map/default delay)";

    og::ui::cycle_respawn_delay(save);
    ASSERT_EQ(60, (int)save.ctf_respawn_ticks) << "Normal -> Fast";
    og::ui::cycle_respawn_delay(save);
    ASSERT_EQ(360, (int)save.ctf_respawn_ticks) << "Fast -> Slow";
    og::ui::cycle_respawn_delay(save);
    ASSERT_EQ(0, (int)save.ctf_respawn_ticks) << "Slow wraps back to Normal";
    og::ui::cycle_respawn_delay(save);
    ASSERT_EQ(60, (int)save.ctf_respawn_ticks) << "the cycle repeats";

    // A stored non-cycle tick count (e.g. the 120 CTF engine default) steps
    // back to the 0 sentinel rather than jumping to an arbitrary set member.
    save.ctf_respawn_ticks = 120;
    og::ui::cycle_respawn_delay(save);
    ASSERT_EQ(0, (int)save.ctf_respawn_ticks);
}

TEST(PickerCommon, toggle_permadeath)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.keep_fallen_heroes)
        << "default is permadeath ON (classic drop-dead-heroes behavior)";

    og::ui::toggle_permadeath(save);
    ASSERT_EQ(1, (int)save.keep_fallen_heroes) << "On -> Off keeps fallen heroes";
    og::ui::toggle_permadeath(save);
    ASSERT_EQ(0, (int)save.keep_fallen_heroes) << "Off -> On restores the default";

    // Any nonzero stored value toggles back to permadeath ON.
    save.keep_fallen_heroes = 5;
    og::ui::toggle_permadeath(save);
    ASSERT_EQ(0, (int)save.keep_fallen_heroes);
}

TEST(PickerCommon, cycle_generator_rate_sequence)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.generator_rate)
        << "default is 0 (normal = 100 percent)";

    og::ui::cycle_generator_rate(save);
    ASSERT_EQ(50, (int)save.generator_rate) << "Normal -> Calm";
    og::ui::cycle_generator_rate(save);
    ASSERT_EQ(200, (int)save.generator_rate) << "Calm -> Frenzy";
    og::ui::cycle_generator_rate(save);
    ASSERT_EQ(0, (int)save.generator_rate) << "Frenzy wraps back to Normal";
    og::ui::cycle_generator_rate(save);
    ASSERT_EQ(50, (int)save.generator_rate) << "the cycle repeats";

    // An explicit 100 (or any out-of-set value) normalizes to the 0 sentinel.
    save.generator_rate = 100;
    og::ui::cycle_generator_rate(save);
    ASSERT_EQ(0, (int)save.generator_rate);
}

TEST(PickerCommon, format_respawn_mode_label)
{
    SaveData save;
    save.respawn_mode = 0;
    ASSERT_EQ("Respawns: Off", og::ui::format_respawn_mode_label(save));
    save.respawn_mode = 1;
    ASSERT_EQ("Respawns: Heroes", og::ui::format_respawn_mode_label(save));
    save.respawn_mode = 2;
    ASSERT_EQ("Respawns: Everyone", og::ui::format_respawn_mode_label(save));

    // Out-of-set values render as the nearest sane end of the range.
    save.respawn_mode = -1;
    ASSERT_EQ("Respawns: Off", og::ui::format_respawn_mode_label(save));
    save.respawn_mode = 9;
    ASSERT_EQ("Respawns: Everyone", og::ui::format_respawn_mode_label(save));
}

TEST(PickerCommon, format_respawn_delay_label)
{
    SaveData save;
    save.ctf_respawn_ticks = 0;
    ASSERT_EQ("Spawn Delay: Normal", og::ui::format_respawn_delay_label(save));
    save.ctf_respawn_ticks = 60;
    ASSERT_EQ("Spawn Delay: Fast", og::ui::format_respawn_delay_label(save));
    save.ctf_respawn_ticks = 360;
    ASSERT_EQ("Spawn Delay: Slow", og::ui::format_respawn_delay_label(save));

    // Non-cycle tick counts (a map-authored 120, say) read as Normal.
    save.ctf_respawn_ticks = 120;
    ASSERT_EQ("Spawn Delay: Normal", og::ui::format_respawn_delay_label(save));
}

TEST(PickerCommon, format_permadeath_label)
{
    SaveData save;
    save.keep_fallen_heroes = 0;
    ASSERT_EQ("Permadeath: On", og::ui::format_permadeath_label(save));
    save.keep_fallen_heroes = 1;
    ASSERT_EQ("Permadeath: Off", og::ui::format_permadeath_label(save));
}

TEST(PickerCommon, format_generator_rate_label)
{
    SaveData save;
    save.generator_rate = 0;
    ASSERT_EQ("Generators: Normal", og::ui::format_generator_rate_label(save));
    save.generator_rate = 50;
    ASSERT_EQ("Generators: Calm", og::ui::format_generator_rate_label(save));
    save.generator_rate = 200;
    ASSERT_EQ("Generators: Frenzy", og::ui::format_generator_rate_label(save));

    // A raw 100 percent (or any out-of-set value) reads as Normal.
    save.generator_rate = 100;
    ASSERT_EQ("Generators: Normal", og::ui::format_generator_rate_label(save));
}

TEST(PickerCommon, difficulty_submenu_labels_fit_140px_rows)
{
    // The SDL DIFFICULTY subscreen draws 140px-wide single-column rows at
    // 6px/char = 23-character budget; labels are centered with no clipping,
    // so every variant of every row label must fit.
    SaveData save;
    std::vector<std::string> labels;
    for (short mode : {short(0), short(1), short(2)})
    {
        save.respawn_mode = mode;
        labels.push_back(og::ui::format_respawn_mode_label(save));
    }
    for (short ticks : {short(0), short(60), short(360)})
    {
        save.ctf_respawn_ticks = ticks;
        labels.push_back(og::ui::format_respawn_delay_label(save));
    }
    for (short keep : {short(0), short(1)})
    {
        save.keep_fallen_heroes = keep;
        labels.push_back(og::ui::format_permadeath_label(save));
    }
    for (short rate : {short(0), short(50), short(200)})
    {
        save.generator_rate = rate;
        labels.push_back(og::ui::format_generator_rate_label(save));
    }
    for (int difficulty : {0, 1, 2})
        labels.push_back(og::ui::format_difficulty_label(difficulty));

    for (const std::string& label : labels)
        EXPECT_LE(label.size(), 23u) << label;
}

// --- GRAPHICS FX depth selector (cfg effects/depth_fx) ---

TEST(PickerCommon, cycle_depth_fx_sequence)
{
    // The five-way selector lap, starting from the default.
    ASSERT_EQ("haze", og::ui::cycle_depth_fx("fog"));
    ASSERT_EQ("mist", og::ui::cycle_depth_fx("haze"));
    ASSERT_EQ("tint", og::ui::cycle_depth_fx("mist"));
    ASSERT_EQ("off", og::ui::cycle_depth_fx("tint"));
    ASSERT_EQ("fog", og::ui::cycle_depth_fx("off"));

    // Five clicks restore any in-set starting value.
    std::string value = "mist";
    for (int i = 0; i < 5; ++i)
        value = og::ui::cycle_depth_fx(value);
    ASSERT_EQ("mist", value);

    // Out-of-set values — including the empty string an absent cfg key
    // reads as — normalize to the default (fog) before stepping, matching
    // depth_fx_mode_from_setting in the renderer.
    ASSERT_EQ("haze", og::ui::cycle_depth_fx(""));
    ASSERT_EQ("haze", og::ui::cycle_depth_fx("on"));
    ASSERT_EQ("haze", og::ui::cycle_depth_fx("bogus"));
}

TEST(PickerCommon, format_depth_fx_label_exact_strings)
{
    ASSERT_EQ("Depth: Fog", og::ui::format_depth_fx_label("fog"));
    ASSERT_EQ("Depth: Haze", og::ui::format_depth_fx_label("haze"));
    ASSERT_EQ("Depth: Mist", og::ui::format_depth_fx_label("mist"));
    ASSERT_EQ("Depth: Tint", og::ui::format_depth_fx_label("tint"));
    ASSERT_EQ("Depth: Off", og::ui::format_depth_fx_label("off"));

    // Unknown/absent values read as the default treatment.
    ASSERT_EQ("Depth: Fog", og::ui::format_depth_fx_label(""));
    ASSERT_EQ("Depth: Fog", og::ui::format_depth_fx_label("on"));
}

TEST(PickerCommon, depth_fx_labels_fit_90px_button_face)
{
    // The GRAPHICS FX grid draws 90px faces at 6px/char = 15-character
    // budget; labels are centered with no clipping.
    std::string value = "fog";
    for (int step = 0; step < 5; ++step)
    {
        const std::string label = og::ui::format_depth_fx_label(value);
        EXPECT_LE(label.size(), 15u) << label;
        value = og::ui::cycle_depth_fx(value);
    }
}

TEST(PickerCommon, depth_fx_is_active_only_off_is_inactive)
{
    EXPECT_FALSE(og::ui::depth_fx_is_active("off"));
    EXPECT_TRUE(og::ui::depth_fx_is_active("fog"));
    EXPECT_TRUE(og::ui::depth_fx_is_active("haze"));
    EXPECT_TRUE(og::ui::depth_fx_is_active("mist"));
    EXPECT_TRUE(og::ui::depth_fx_is_active("tint"));
    // Unknown/absent values normalize to fog, which is active.
    EXPECT_TRUE(og::ui::depth_fx_is_active(""));
    EXPECT_TRUE(og::ui::depth_fx_is_active("bogus"));
}

// --- DISPLAY zoom selector (cfg graphics/zoom) ---

TEST(PickerCommon, cycle_zoom_sequence)
{
    // Each click zooms OUT one 0.1 step, wrapping from the deepest 0.1 back
    // to the classic 1.0.
    ASSERT_EQ("0.9", og::ui::cycle_zoom("1.0"));
    ASSERT_EQ("0.8", og::ui::cycle_zoom("0.9"));
    ASSERT_EQ("0.5", og::ui::cycle_zoom("0.6"));
    ASSERT_EQ("0.1", og::ui::cycle_zoom("0.2"));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.1"));

    // Ten clicks restore any in-set starting value.
    std::string value = "0.7";
    for (int i = 0; i < 10; ++i)
        value = og::ui::cycle_zoom(value);
    ASSERT_EQ("0.7", value);

    // Out-of-set values — including the empty string an absent cfg key reads
    // as — normalize to the classic 1.0 before stepping, matching
    // parse_zoom_steps in the renderer.
    ASSERT_EQ("0.9", og::ui::cycle_zoom(""));
    ASSERT_EQ("0.9", og::ui::cycle_zoom("bogus"));
    ASSERT_EQ("0.4", og::ui::cycle_zoom("0.45")); // quantize then step
}

TEST(PickerCommon, cycle_zoom_wraps_at_the_runtime_safe_minimum)
{
    ASSERT_EQ("0.2", og::ui::cycle_zoom("0.3", 2));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.2", 2));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.1", 2));

    ASSERT_EQ("0.5", og::ui::cycle_zoom("0.6", 5));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.5", 5));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.2", 5));

    ASSERT_EQ("1.0", og::ui::cycle_zoom("1.0", 10));
    ASSERT_EQ("1.0", og::ui::cycle_zoom("0.5", 10));
}

TEST(PickerCommon, display_mode_parse_cycle_and_labels)
{
    using og::ui::DisplayMode;
    // Legacy boolean cfg: "on" was the borderless desktop fullscreen.
    ASSERT_EQ(DisplayMode::Borderless, og::ui::parse_display_mode("on"));
    ASSERT_EQ(DisplayMode::Borderless, og::ui::parse_display_mode("borderless"));
    ASSERT_EQ(DisplayMode::Exclusive, og::ui::parse_display_mode("exclusive"));
    ASSERT_EQ(DisplayMode::Exclusive, og::ui::parse_display_mode("fullscreen"));
    ASSERT_EQ(DisplayMode::Windowed, og::ui::parse_display_mode("off"));
    ASSERT_EQ(DisplayMode::Windowed, og::ui::parse_display_mode(""));
    ASSERT_EQ(DisplayMode::Windowed, og::ui::parse_display_mode("bogus"));

    ASSERT_EQ(DisplayMode::Borderless, og::ui::next_display_mode(DisplayMode::Windowed));
    ASSERT_EQ(DisplayMode::Exclusive, og::ui::next_display_mode(DisplayMode::Borderless));
    ASSERT_EQ(DisplayMode::Windowed, og::ui::next_display_mode(DisplayMode::Exclusive));

    ASSERT_EQ("off", og::ui::display_mode_cfg_value(DisplayMode::Windowed));
    ASSERT_EQ("borderless", og::ui::display_mode_cfg_value(DisplayMode::Borderless));
    ASSERT_EQ("exclusive", og::ui::display_mode_cfg_value(DisplayMode::Exclusive));

    ASSERT_EQ("Mode: Windowed", og::ui::format_display_mode_label(""));
    ASSERT_EQ("Mode: Borderless", og::ui::format_display_mode_label("on"));
    ASSERT_EQ("Mode: Borderless", og::ui::format_display_mode_label("borderless"));
    ASSERT_EQ("Mode: Fullscreen", og::ui::format_display_mode_label("exclusive"));
    // 102px button face at 6px/char: every label must fit 17 characters.
    for (const char* v : {"", "on", "borderless", "exclusive", "bogus"})
        ASSERT_LE(og::ui::format_display_mode_label(v).size(), 17u) << v;
}

TEST(PickerCommon, resolution_parse_next_and_labels)
{
    // Absent/garbage cfg reads as the 640x400 boot default.
    ASSERT_EQ(std::make_pair(640, 400), og::ui::parse_resolution("", ""));
    ASSERT_EQ(std::make_pair(640, 400), og::ui::parse_resolution("abc", "400"));
    ASSERT_EQ(std::make_pair(640, 400), og::ui::parse_resolution("100", "80"));
    ASSERT_EQ(std::make_pair(1920, 1200), og::ui::parse_resolution("1920", "1200"));

    const std::vector<std::pair<int, int>> list = og::ui::fallback_resolutions({0, 0});
    ASSERT_GE(list.size(), 2u);

    // Desktop-derived fallback: native + aspect-preserving fractions.
    const auto derived = og::ui::fallback_resolutions({1920, 1080});
    ASSERT_EQ(3u, derived.size());
    ASSERT_EQ(std::make_pair(1920, 1080), derived[0]);
    ASSERT_EQ(std::make_pair(1440, 810), derived[1]);
    ASSERT_EQ(std::make_pair(960, 540) , derived[2]);

    // The lap walks the list in order and wraps.
    ASSERT_EQ(std::make_pair(960, 600), og::ui::next_resolution(list, "640", "400"));
    ASSERT_EQ(std::make_pair(640, 400), og::ui::next_resolution(list, "1920", "1200"));
    // A hand-edited size re-enters at the first entry; an empty list echoes
    // the current resolution back.
    ASSERT_EQ(list.front(), og::ui::next_resolution(list, "800", "600"));
    ASSERT_EQ(std::make_pair(800, 600), og::ui::next_resolution({}, "800", "600"));

    // One full lap of clicks restores any in-list starting point.
    std::pair<int, int> r{1280, 800};
    for (std::size_t i = 0; i < list.size(); ++i)
        r = og::ui::next_resolution(list, std::to_string(r.first), std::to_string(r.second));
    ASSERT_EQ(std::make_pair(1280, 800), r);

    ASSERT_EQ("Res: 640x400", og::ui::format_resolution_label("", ""));
    ASSERT_EQ("Res: 2560x1440", og::ui::format_resolution_label("2560", "1440"));
    // 102px face at 6px/char: 17 characters, even for 8K-wide values.
    ASSERT_LE(og::ui::format_resolution_label("7680", "4800").size(), 17u);
}

TEST(PickerCommon, resolution_choices_use_only_enumerated_exclusive_modes)
{
    using og::ui::DisplayMode;
    const std::pair<int, int> desktop{1920, 1080};
    const std::pair<int, int> hand_edited{1366, 768};

    // A compositor exposing only its current desktop still gets useful
    // aspect-preserving window sizes outside exclusive fullscreen.
    const auto windowed = og::ui::build_resolution_choices(
        {desktop}, desktop, hand_edited, DisplayMode::Windowed);
    EXPECT_NE(windowed.end(), std::find(windowed.begin(), windowed.end(), desktop));
    EXPECT_NE(windowed.end(), std::find(windowed.begin(), windowed.end(),
                                        std::make_pair(1440, 810)));
    EXPECT_NE(windowed.end(), std::find(windowed.begin(), windowed.end(), hand_edited));

    // Those derived fractions and the hand-edited cfg size are not real SDL
    // video modes, so the exclusive lap must contain only the desktop here.
    const auto exclusive = og::ui::build_resolution_choices(
        {desktop}, desktop, hand_edited, DisplayMode::Exclusive);
    ASSERT_EQ(1u, exclusive.size());
    EXPECT_EQ(desktop, exclusive.front());

    // An actual desktop size is still synthetic when the fullscreen mode
    // list omitted it, so Exclusive must not add it independently.
    const auto incomplete = og::ui::build_resolution_choices(
        {{2560, 1440}, {1280, 720}}, desktop, hand_edited,
        DisplayMode::Exclusive);
    const std::vector<std::pair<int, int>> expected_real_modes{
        {2560, 1440}, {1280, 720}};
    EXPECT_EQ(expected_real_modes, incomplete);

    // Entering Exclusive prefers desktop only when it is real/enumerated.
    // If not, it selects the largest real mode; no modes produces no request.
    EXPECT_EQ(desktop, og::ui::preferred_exclusive_resolution(
        {{2560, 1440}, desktop, {1280, 720}}, desktop));
    EXPECT_EQ(std::make_pair(2560, 1440),
              og::ui::preferred_exclusive_resolution(
                  {{1280, 720}, {2560, 1440}}, desktop));
    EXPECT_EQ(std::make_pair(0, 0),
              og::ui::preferred_exclusive_resolution({}, desktop));
}

TEST(PickerCommon, format_zoom_label_exact_strings)
{
    ASSERT_EQ("Zoom: 1.0x", og::ui::format_zoom_label("1.0"));
    ASSERT_EQ("Zoom: 0.9x", og::ui::format_zoom_label("0.9"));
    ASSERT_EQ("Zoom: 0.5x", og::ui::format_zoom_label("0.5"));
    ASSERT_EQ("Zoom: 0.1x", og::ui::format_zoom_label("0.1"));

    // Unknown/absent values read as the classic default.
    ASSERT_EQ("Zoom: 1.0x", og::ui::format_zoom_label(""));
    ASSERT_EQ("Zoom: 1.0x", og::ui::format_zoom_label("double"));
}

TEST(PickerCommon, cycle_smoothing_sequence_and_labels)
{
    // Three-way lap: off -> sai -> eagle -> off.
    ASSERT_EQ("sai", og::ui::cycle_smoothing("off"));
    ASSERT_EQ("eagle", og::ui::cycle_smoothing("sai"));
    ASSERT_EQ("off", og::ui::cycle_smoothing("eagle"));
    // Absent/garbage reads as off, so the first click lands on sai.
    ASSERT_EQ("sai", og::ui::cycle_smoothing(""));
    ASSERT_EQ("sai", og::ui::cycle_smoothing("bogus"));

    ASSERT_EQ("Smooth: Off", og::ui::format_smoothing_label("off"));
    ASSERT_EQ("Smooth: SAI", og::ui::format_smoothing_label("sai"));
    ASSERT_EQ("Smooth: Eagle", og::ui::format_smoothing_label("eagle"));
    ASSERT_EQ("Smooth: Off", og::ui::format_smoothing_label(""));
    ASSERT_EQ("Smooth: Off", og::ui::format_smoothing_label("bogus"));
	ASSERT_EQ("Smooth: SAI N/A",
	          og::ui::format_smoothing_label("sai", false));
	ASSERT_EQ("Smooth: Eagle N/A",
	          og::ui::format_smoothing_label("eagle", false));
}

TEST(PickerCommon, legacy_render_only_supplies_an_absent_smoothing_key)
{
    ASSERT_EQ("sai", og::ui::effective_smoothing_setting("", "sai"));
    ASSERT_EQ("eagle", og::ui::effective_smoothing_setting("", "eagle"));
    ASSERT_EQ("", og::ui::effective_smoothing_setting("", "normal"));
    ASSERT_EQ("", og::ui::effective_smoothing_setting("", "double"));
    ASSERT_EQ("off", og::ui::effective_smoothing_setting("off", "sai"));
    ASSERT_EQ("eagle", og::ui::effective_smoothing_setting("eagle", "sai"));
}

TEST(PickerCommon, zoom_and_smoothing_labels_fit_the_button_face)
{
    // The DISPLAY rows draw a 102px face at 6px/char = 17-character budget
    // (labels are centered with no clipping).
    std::string zoom = "1.0";
    for (int step = 0; step < 10; ++step)
    {
        EXPECT_LE(og::ui::format_zoom_label(zoom).size(), 17u) << zoom;
        zoom = og::ui::cycle_zoom(zoom);
    }
    ASSERT_EQ("1.0", zoom) << "ten steps must complete the lap";

    std::string smoothing = "off";
    for (int step = 0; step < 3; ++step)
    {
        EXPECT_LE(og::ui::format_smoothing_label(smoothing).size(), 17u)
            << smoothing;
        smoothing = og::ui::cycle_smoothing(smoothing);
    }
    ASSERT_EQ("off", smoothing) << "three steps must complete the lap";
}

// --- Company autosave context (design §3.8 [SAVE-F1] / §1.2 G12) ----------

TEST(PickerCommon, company_autosave_context_local_is_plain)
{
    SaveData save;
    save.my_team = 2;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 2;

    const og::data::CompanyAutosaveContext context =
        og::ui::company_autosave_context(save, /*networked_lobby=*/false);
    EXPECT_FALSE(context.networked_lobby)
        << "without a networked lobby the write stays a plain save";
    for (const bool owned : context.owned_teams)
        EXPECT_FALSE(owned) << "owned teams are meaningless off the merge path";
}

TEST(PickerCommon, company_autosave_context_owned_teams_from_roster_and_seat)
{
    SaveData save;
    save.my_team = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    save.team_list[1]->teamnum = 3;
    save.team_list[2] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[2]->teamnum = 9; // out of wallet range: ignored
    save.team_size = 3;

    const og::data::CompanyAutosaveContext context =
        og::ui::company_autosave_context(save, /*networked_lobby=*/true);
    ASSERT_TRUE(context.networked_lobby);
    EXPECT_FALSE(context.owned_teams[0]);
    EXPECT_TRUE(context.owned_teams[1]) << "my_team + roster team";
    EXPECT_FALSE(context.owned_teams[2]);
    EXPECT_TRUE(context.owned_teams[3]) << "second seat's roster team";
}

TEST(PickerCommon, company_autosave_context_empty_roster_uses_my_team)
{
    SaveData save;
    save.my_team = 0; // team 0 is a REAL wallet index (unlike seat labels)

    const og::data::CompanyAutosaveContext context =
        og::ui::company_autosave_context(save, /*networked_lobby=*/true);
    ASSERT_TRUE(context.networked_lobby);
    EXPECT_TRUE(context.owned_teams[0])
        << "hiring into an empty roster spends from my_team's wallet";
    EXPECT_FALSE(context.owned_teams[1]);
    EXPECT_FALSE(context.owned_teams[2]);
    EXPECT_FALSE(context.owned_teams[3]);

    save.my_team = 7; // out of wallet range: no team marked
    const og::data::CompanyAutosaveContext clamped =
        og::ui::company_autosave_context(save, /*networked_lobby=*/true);
    for (const bool owned : clamped.owned_teams)
        EXPECT_FALSE(owned);
}

// --- Company name generator (design §2.2) ---

namespace {

// EXPECT-only bank sweep (helpers that return values cannot use ASSERT_*);
// returns the longest word so the combined-budget pin can sum the maxima.
std::size_t check_company_bank(std::span<const char* const> bank,
                               std::size_t per_word_budget,
                               const char* what)
{
    std::size_t longest = 0;
    std::set<std::string> unique;
    for (const char* word : bank)
    {
        if (word == nullptr)
        {
            ADD_FAILURE() << what << " contains a null word";
            continue;
        }
        const std::string text(word);
        EXPECT_FALSE(text.empty()) << what << " contains an empty word";
        EXPECT_LE(text.size(), per_word_budget) << what << ": " << text;
        longest = std::max(longest, text.size());
        for (const char c : text)
            EXPECT_TRUE(c >= 'A' && c <= 'Z')
                << what << " word has a non-A-Z character: " << text;
        unique.insert(text);
    }
    EXPECT_EQ(unique.size(), bank.size()) << what << " has duplicate words";
    return longest;
}

std::vector<std::string> split_words(const std::string& text)
{
    std::vector<std::string> words;
    std::string current;
    for (const char c : text)
    {
        if (c == ' ')
        {
            words.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    words.push_back(current);
    return words;
}

bool bank_contains(std::span<const char* const> bank, const std::string& word)
{
    for (const char* entry : bank)
    {
        if (word == entry)
            return true;
    }
    return false;
}

} // namespace

TEST(CompanyNameGen, banks_respect_the_18_char_budget_and_charset)
{
    const og::ui::CompanyNameBanks banks = og::ui::company_name_banks();
    ASSERT_FALSE(banks.adjectives.empty());
    ASSERT_FALSE(banks.nouns.empty());
    ASSERT_FALSE(banks.groups.empty());

    // Per-bank word budgets: 5 + 6 + 5 (+ two spaces) == the 18-char cap.
    const std::size_t adj = check_company_bank(banks.adjectives, 5, "adjectives");
    const std::size_t noun = check_company_bank(banks.nouns, 6, "nouns");
    const std::size_t group = check_company_bank(banks.groups, 5, "groups");
    EXPECT_LE(adj + noun + group + 2, og::ui::kCompanyNameMaxLen)
        << "the longest <ADJ> <NOUN> <GROUP> combination must fit the cap "
           "(the generator never truncates)";

    // The design's own example must stay generatable.
    EXPECT_TRUE(bank_contains(banks.adjectives, "IRON"));
    EXPECT_TRUE(bank_contains(banks.nouns, "KETTLE"));
    EXPECT_TRUE(bank_contains(banks.groups, "BAND"));
}

TEST(CompanyNameGen, deterministic_under_seed_and_reroll_advances)
{
    SeededRandom first_rng(42u);
    SeededRandom second_rng(42u);
    std::vector<std::string> first;
    std::vector<std::string> second;
    for (int i = 0; i < 12; ++i)
    {
        first.push_back(og::ui::generate_company_name(first_rng));
        second.push_back(og::ui::generate_company_name(second_rng));
    }
    EXPECT_EQ(first, second)
        << "same seed, same call sequence => byte-identical names";

    // Reroll support: repeated calls on the ADVANCING rng vary the name.
    const std::set<std::string> distinct(first.begin(), first.end());
    EXPECT_GE(distinct.size(), 2u) << "reroll must be able to change the name";
}

TEST(CompanyNameGen, fixed_rng_picks_the_indexed_words)
{
    const og::ui::CompanyNameBanks banks = og::ui::company_name_banks();

    FixedRandom zero(0u);
    EXPECT_EQ(std::format("{} {} {}", banks.adjectives[0], banks.nouns[0],
                          banks.groups[0]),
              og::ui::generate_company_name(zero));

    FixedRandom three(3u);
    EXPECT_EQ(std::format("{} {} {}",
                          banks.adjectives[3u % banks.adjectives.size()],
                          banks.nouns[3u % banks.nouns.size()],
                          banks.groups[3u % banks.groups.size()]),
              og::ui::generate_company_name(three));
}

TEST(CompanyNameGen, seeded_sweep_stays_in_budget_and_covers_every_word)
{
    const og::ui::CompanyNameBanks banks = og::ui::company_name_banks();
    SeededRandom rng(1234u);
    std::set<std::string> seen_adjectives;
    std::set<std::string> seen_nouns;
    std::set<std::string> seen_groups;
    std::set<std::string> distinct_names;
    for (int i = 0; i < 2000; ++i)
    {
        const std::string name = og::ui::generate_company_name(rng);
        ASSERT_LE(name.size(), og::ui::kCompanyNameMaxLen) << name;
        ASSERT_FALSE(name.empty());

        const std::vector<std::string> words = split_words(name);
        ASSERT_EQ(3u, words.size()) << "shape is <ADJ> <NOUN> <GROUP>: " << name;
        ASSERT_TRUE(bank_contains(banks.adjectives, words[0])) << name;
        ASSERT_TRUE(bank_contains(banks.nouns, words[1])) << name;
        ASSERT_TRUE(bank_contains(banks.groups, words[2])) << name;

        seen_adjectives.insert(words[0]);
        seen_nouns.insert(words[1]);
        seen_groups.insert(words[2]);
        distinct_names.insert(name);
    }
    EXPECT_EQ(seen_adjectives.size(), banks.adjectives.size())
        << "every adjective must be reachable";
    EXPECT_EQ(seen_nouns.size(), banks.nouns.size())
        << "every noun must be reachable";
    EXPECT_EQ(seen_groups.size(), banks.groups.size())
        << "every group word must be reachable";
    EXPECT_GE(distinct_names.size(), 100u) << "the space must have variety";
}

// --- Company label formatters (design §2.2/§2.3) ---
// (The slug-preview formatter pin was deleted with the formatter — §9.3/F2.)

TEST(CompanyFormat, list_title_and_row_columns)
{
    EXPECT_EQ("COMPANIES (12)", og::ui::format_company_list_title(12));
    EXPECT_EQ("COMPANIES (0)", og::ui::format_company_list_title(0));

    og::data::CompanyInfo info;
    info.slot = "iron-kettle-band";
    info.display_name = "IRON KETTLE BAND";
    info.roster_size = 12;
    info.last_played_unix_s = 1000000000; // 2001-09-09 01:46:40 UTC
    info.valid = true;

    const og::ui::CompanyRowText row = og::ui::format_company_row(info);
    EXPECT_FALSE(row.corrupt);
    EXPECT_EQ("IRON KETTLE BAND", row.name);
    EXPECT_EQ("12", row.roster);
    EXPECT_EQ("2001-09-09", row.played) << "dates are UTC, never local time";

    // Column budgets (§2.3): name <= 18 chars, roster exactly 2, date <= 10.
    EXPECT_LE(row.name.size(), og::ui::kCompanyNameMaxLen);
    EXPECT_EQ(2u, row.roster.size());
    EXPECT_LE(row.played.size(), 10u);
}

TEST(CompanyFormat, row_edges_clip_pad_and_mark_corruption)
{
    og::data::CompanyInfo info;
    info.slot = "long";
    info.display_name = "AN EXTREMELY LONG COMPANY NAME"; // 30 chars
    info.roster_size = 3;
    info.last_played_unix_s = 0; // never played
    info.valid = true;

    og::ui::CompanyRowText row = og::ui::format_company_row(info);
    EXPECT_EQ(og::ui::kCompanyNameMaxLen, row.name.size())
        << "over-budget names clip to the column";
    EXPECT_EQ(0u, row.name.find("AN EXTREMELY LONG"));
    EXPECT_EQ(" 3", row.roster) << "single digits right-align to 2 chars";
    EXPECT_EQ("", row.played) << "never-played rows draw no date";

    info.roster_size = 250; // out-of-range header value
    row = og::ui::format_company_row(info);
    EXPECT_EQ("99", row.roster) << "roster clamps to the 2-char column";

    info.roster_size = -1;
    row = og::ui::format_company_row(info);
    EXPECT_EQ(" 0", row.roster);

    // Absurd timestamps (beyond year 9999) blank rather than widen the
    // 10-char date column.
    info.roster_size = 3;
    info.last_played_unix_s = INT64_C(400000000000000);
    row = og::ui::format_company_row(info);
    EXPECT_EQ("", row.played);
    info.last_played_unix_s = -5;
    row = og::ui::format_company_row(info);
    EXPECT_EQ("", row.played);

    // Corrupt rows: keep the parsed name, mark CORRUPT in the date column.
    info.valid = false;
    info.last_played_unix_s = 0;
    row = og::ui::format_company_row(info);
    EXPECT_TRUE(row.corrupt);
    EXPECT_EQ(0u, row.name.find("AN EXTREMELY LONG"));
    EXPECT_EQ("--", row.roster);
    EXPECT_EQ("CORRUPT", row.played);

    // Corrupt before the name parsed: fall back to the slot.
    info.display_name.clear();
    info.slot = "damaged-company";
    row = og::ui::format_company_row(info);
    EXPECT_TRUE(row.corrupt);
    EXPECT_EQ("damaged-company", row.name);

    // A valid save with an empty stored name also identifies by slot.
    og::data::CompanyInfo unnamed;
    unnamed.slot = "save0";
    unnamed.roster_size = 1;
    unnamed.valid = true;
    row = og::ui::format_company_row(unnamed);
    EXPECT_FALSE(row.corrupt);
    EXPECT_EQ("save0", row.name);
}

// §2.3 terminal projection: both terminal clients print the SAME joined row
// line (name padded to the 18-char column, 2-char roster, date; '*' marks
// the active company — the terminal projection of the SDL red outline, U4).
TEST(CompanyFormat, row_line_joins_columns_for_terminals)
{
    og::data::CompanyInfo info;
    info.slot = "iron-kettle-band";
    info.display_name = "IRON KETTLE BAND";
    info.roster_size = 12;
    info.last_played_unix_s = 1000000000; // 2001-09-09 UTC
    info.valid = true;

    EXPECT_EQ("* IRON KETTLE BAND   12 2001-09-09",
              og::ui::format_company_row_line(info, true));
    EXPECT_EQ("  IRON KETTLE BAND   12 2001-09-09",
              og::ui::format_company_row_line(info, false));

    // Never played: the date column is omitted entirely (no trailing pad).
    info.last_played_unix_s = 0;
    info.roster_size = 3;
    EXPECT_EQ("  IRON KETTLE BAND    3",
              og::ui::format_company_row_line(info, false));

    // Corrupt rows join the "--"/"CORRUPT" markers.
    info.valid = false;
    EXPECT_EQ("  IRON KETTLE BAND   -- CORRUPT",
              og::ui::format_company_row_line(info, false));
}

// --- §2.4 Backups sub-view formatters --------------------------------------

TEST(BackupFormat, list_title_carries_the_retention_display)
{
    // The "/20" IS the §2.4 retention display.
    static_assert(og::data::kCompanyBackupRetention == 20);
    EXPECT_EQ("BACKUPS: IRON KETTLE BAND (3/20)",
              og::ui::format_backup_list_title("IRON KETTLE BAND", 3));
    EXPECT_EQ("BACKUPS:  (0/20)", og::ui::format_backup_list_title("", 0));
    // Over-long names clip to the 18-char cap like every other surface.
    EXPECT_EQ("BACKUPS: ABCDEFGHIJKLMNOPQR (20/20)",
              og::ui::format_backup_list_title("ABCDEFGHIJKLMNOPQRSTU", 20));
}

TEST(BackupFormat, row_columns_level_tag_and_utc_datetime)
{
    og::data::CompanyBackupInfo info;
    info.slot = "iron-kettle-band";
    info.seq = 7;
    info.filename = "iron-kettle-band.007.gtl";
    info.header.slot = "iron-kettle-band";
    info.header.campaign_id = "org.openglad.never-mounted";
    info.header.scen_num = 7;
    info.header.valid = true;
    // 1970-01-01T23:59:59Z: pins BOTH that the format is "MM-DD HH:MM" and
    // that it is UTC (any timezone offset would move the day or the hour).
    info.header.last_played_unix_s = 86399;

    og::ui::BackupRowText row = og::ui::format_backup_row(info);
    EXPECT_FALSE(row.corrupt);
    // No mounted-campaign match (the level_display_guarded rule): bare tag.
    // (The mounted-title branch is pinned in og_test_basecamp where a real
    // mount exists.)
    EXPECT_EQ("L7", row.level);
    EXPECT_EQ("01-01 23:59", row.saved);

    // Unstamped snapshots (a pre-v14 company backed up before any stamp)
    // carry no saved column at all.
    info.header.last_played_unix_s = 0;
    EXPECT_EQ("", og::ui::format_backup_row(info).saved);
    // Out-of-calendar values never wrap into a bogus in-range datetime.
    info.header.last_played_unix_s = 999999999999999;
    EXPECT_EQ("", og::ui::format_backup_row(info).saved);

    // Corrupt snapshots mark themselves instead of level context they never
    // parsed (§2.4 corrupt-backup rows).
    info.header.valid = false;
    row = og::ui::format_backup_row(info);
    EXPECT_TRUE(row.corrupt);
    EXPECT_EQ("CORRUPT", row.level);
    EXPECT_EQ("--", row.saved);
}

// §2.4 terminal projection: both terminal clients print the SAME joined
// backup line (level tag padded to a 20-char column, then the saved
// datetime).
TEST(BackupFormat, row_line_joins_columns_for_terminals)
{
    og::data::CompanyBackupInfo info;
    info.slot = "iron-kettle-band";
    info.seq = 3;
    info.filename = "iron-kettle-band.003.gtl";
    info.header.slot = "iron-kettle-band";
    info.header.campaign_id = "org.openglad.never-mounted";
    info.header.scen_num = 12;
    info.header.valid = true;
    info.header.last_played_unix_s = 86399;

    EXPECT_EQ("L12                  01-01 23:59",
              og::ui::format_backup_row_line(info));

    // Unstamped: the saved column is omitted entirely (no trailing pad).
    info.header.last_played_unix_s = 0;
    EXPECT_EQ("L12", og::ui::format_backup_row_line(info));

    // Corrupt rows join the "CORRUPT"/"--" markers.
    info.header.valid = false;
    EXPECT_EQ("CORRUPT              --",
              og::ui::format_backup_row_line(info));
}

TEST(BackupFormat, restore_error_strings)
{
    using E = og::data::CompanyRestoreError;
    EXPECT_STREQ("", og::ui::company_restore_error_string(E::None));
    EXPECT_STREQ("BACKUP FILE DAMAGED",
                 og::ui::company_restore_error_string(E::InvalidBackup));
    EXPECT_STREQ("COULD NOT BACK UP CURRENT STATE",
                 og::ui::company_restore_error_string(
                     E::PreRestoreBackupFailed));
    EXPECT_STREQ("COPY FAILED - COMPANY UNCHANGED",
                 og::ui::company_restore_error_string(E::CopyFailed));
    EXPECT_STREQ("RELOAD FAILED - REWIND UNDONE",
                 og::ui::company_restore_error_string(E::ReloadFailed));
    EXPECT_STREQ("REWOUND, BUT THE TIMESTAMP WRITE FAILED",
                 og::ui::company_restore_error_string(E::RestampFailed));
}

// --- §2.5 base camp roster helpers (WP4) -----------------------------------

namespace {

std::unique_ptr<guy> make_base_camp_member(const char* name, int family)
{
    auto member = std::make_unique<guy>(family);
    member->name = name;
    return member;
}

} // namespace

TEST(BaseCampRoster, collect_slots_and_deploy_counts_follow_the_save)
{
    SaveData save;
    EXPECT_TRUE(og::ui::collect_base_camp_slots(save).empty());
    EXPECT_EQ(0, og::ui::count_deployed_members(save));

    // Sparse occupancy: display order is slot order, holes skipped.
    save.team_list[1] = make_base_camp_member("ONE", FAMILY_SOLDIER);
    save.team_list[4] = make_base_camp_member("FOUR", FAMILY_MAGE);
    save.team_list[7] = make_base_camp_member("SEVEN", FAMILY_ARCHER);
    save.team_size = 3;

    const std::vector<int> slots = og::ui::collect_base_camp_slots(save);
    ASSERT_EQ(3u, slots.size());
    EXPECT_EQ(1, slots[0]);
    EXPECT_EQ(4, slots[1]);
    EXPECT_EQ(7, slots[2]);

    // New members default deployed (v14 default; §2.5 hires deploy).
    EXPECT_EQ(3, og::ui::count_deployed_members(save));
}

TEST(BaseCampRoster, toggle_deploy_slot_flips_and_guards)
{
    SaveData save;
    save.team_list[2] = make_base_camp_member("GORT", FAMILY_SOLDIER);
    save.team_size = 1;

    // Empty / out-of-range slots: no flip, false.
    EXPECT_FALSE(og::ui::toggle_deploy_slot(save, 0));
    EXPECT_FALSE(og::ui::toggle_deploy_slot(save, -1));
    EXPECT_FALSE(og::ui::toggle_deploy_slot(save, MAX_TEAM_SIZE));

    ASSERT_TRUE(save.team_list[2]->deployed);
    EXPECT_FALSE(og::ui::toggle_deploy_slot(save, 2)) << "flip to benched";
    EXPECT_FALSE(save.team_list[2]->deployed);
    EXPECT_EQ(0, og::ui::count_deployed_members(save));
    EXPECT_TRUE(og::ui::toggle_deploy_slot(save, 2)) << "flip back";
    EXPECT_TRUE(save.team_list[2]->deployed);
}

TEST(BaseCampRoster, format_row_clips_every_column)
{
    guy member(FAMILY_SOLDIER);
    member.name = "AVERYLONGNAMEINDEED";
    member.level = 1234;
    member.exp = 32200;

    // §9.5.3: no HP column (derived max HP was redundant with CLASS+LVL).
    const og::ui::BaseCampRowText row = og::ui::format_base_camp_row(member);
    EXPECT_EQ("AVERYLONGNAM", row.name) << "name clips to 12";
    EXPECT_EQ("SOLDIER", row.cls) << "uppercased family display name";
    EXPECT_LE(row.cls.size(), 9u);
    EXPECT_EQ("123", row.level) << "level clips to 3";
    EXPECT_EQ(" 32200", row.exp) << "exp left-pads to 6 (graft b)";
    EXPECT_LE(row.exp.size(), 6u);

    // §9.9 graft (b): small numerics left-pad to fixed width so the digit
    // columns right-align down the page on all three clients.
    member.level = 7;
    member.exp = 950;
    const og::ui::BaseCampRowText padded =
        og::ui::format_base_camp_row(member);
    EXPECT_EQ(" 7", padded.level) << "level left-pads to 2";
    EXPECT_EQ("   950", padded.exp) << "exp left-pads to 6";

    // Over-wide exp still clips to the 6-char budget.
    member.exp = 1234567;
    EXPECT_EQ("123456", og::ui::format_base_camp_row(member).exp);
}

TEST(BaseCampRoster, family_text_colors_match_master_view_team)
{
    // Master's View Team applied this exact palette-ramp formula to both the
    // character name and its family label. Pin every playable family so the
    // nostalgic mapping cannot quietly collapse into a uniform row color.
    for (short family = FAMILY_SOLDIER; family <= FAMILY_TOWER1; ++family) {
        const unsigned char expected =
            static_cast<unsigned char>(((family + 1) << 4) & 255);
        EXPECT_EQ(expected, og::ui::base_camp_family_text_color(family))
            << "family " << family;
    }

    EXPECT_EQ(16, og::ui::base_camp_family_text_color(FAMILY_SOLDIER));
    EXPECT_EQ(32, og::ui::base_camp_family_text_color(FAMILY_ELF));
    EXPECT_EQ(48, og::ui::base_camp_family_text_color(FAMILY_ARCHER));
    EXPECT_EQ(64, og::ui::base_camp_family_text_color(FAMILY_MAGE));
}

TEST(BaseCampRoster, header_lines_budget_and_content)
{
    SaveData save;
    save.m_totalcash[0] = 12345;
    EXPECT_EQ("GOLD 12345", og::ui::format_base_camp_gold_label(save));
    save.m_totalcash[0] = 4000000000u;
    EXPECT_LE(og::ui::format_base_camp_gold_label(save).size(), 11u)
        << "the gold block clips to the 11-char right column";

    // The gold label reads the player's wallet (my_team, clamped).
    save.my_team = 2;
    save.m_totalcash[2] = 777;
    EXPECT_EQ("GOLD 777", og::ui::format_base_camp_gold_label(save));

    save.team_list[0] = make_base_camp_member("A", FAMILY_SOLDIER);
    save.team_list[1] = make_base_camp_member("B", FAMILY_MAGE);
    save.team_size = 2;
    save.team_list[1]->deployed = false;
    save.scen_num = 7;

    const std::string line =
        og::ui::format_base_camp_scen_line(save, "THE FORTRESS");
    EXPECT_EQ("SCEN 7: THE FORTRESS  DEP 1/2", line);
    EXPECT_LE(line.size(), 34u);

    // An over-long title clips so the DEP block always fits the budget.
    const std::string clipped = og::ui::format_base_camp_scen_line(
        save, "AN ABSURDLY LONG SCENARIO TITLE THAT CANNOT FIT");
    EXPECT_LE(clipped.size(), 34u);
    EXPECT_NE(std::string::npos, clipped.find("  DEP 1/2"))
        << "the DEP block survives the clip: " << clipped;
}

TEST(BaseCampRoster, train_session_seek_slot_seats_directly)
{
    SaveData save;
    save.team_list[0] = make_base_camp_member("FRONT", FAMILY_SOLDIER);
    save.team_list[3] = make_base_camp_member("PICKED", FAMILY_MAGE);
    save.team_size = 2;

    og::ui::TrainSession session(save);
    ASSERT_FALSE(session.empty());
    EXPECT_EQ(0, session.current_slot());

    // §2.5 per-row TRAIN: seek lands exactly on the clicked slot.
    EXPECT_TRUE(session.seek_slot(3));
    EXPECT_EQ(3, session.current_slot());
    EXPECT_EQ("PICKED", session.working_copy().name);

    // Empty / out-of-range seeks refuse and keep the position.
    EXPECT_FALSE(session.seek_slot(1));
    EXPECT_FALSE(session.seek_slot(-1));
    EXPECT_FALSE(session.seek_slot(MAX_TEAM_SIZE));
    EXPECT_EQ(3, session.current_slot());
}

// §3.3 positional-refresh rule: update_guys' held-back pass REORDERS the
// roster (survivors first, held-back members appended), so display rows must
// be re-collected — never held — across a win fold. collect_base_camp_slots
// re-derives from the save by construction; this pins the reorder it must
// absorb.
TEST(BaseCampRoster, positional_rows_refresh_after_update_guys_reorder)
{
    SaveData save;
    save.team_list[0] = make_base_camp_member("BENCHED", FAMILY_SOLDIER);
    save.team_list[0]->deployed = false;
    save.team_list[1] = make_base_camp_member("FIGHTER", FAMILY_MAGE);
    save.team_size = 2;

    // Pre-fold display order: slot order (BENCHED leads).
    {
        const std::vector<int> before = og::ui::collect_base_camp_slots(save);
        ASSERT_EQ(2u, before.size());
        EXPECT_EQ("BENCHED", save.team_list[before[0]]->name);
    }

    // The win fold: only FIGHTER deployed into the level and survived.
    std::list<std::unique_ptr<walker>> oblist;
    {
        auto w = std::make_unique<walker>();
        auto g = std::make_unique<guy>(FAMILY_MAGE);
        g->name = "FIGHTER";
        w->set_dead(0);
        w->set_owned_myguy(std::move(g));
        oblist.push_back(std::move(w));
    }
    save.update_guys(oblist);

    // Post-fold: pass 1 copies the survivor into slot 0, pass 2 appends the
    // held-back member — the display order FLIPPED; a stale pre-fold row
    // index would now point at the wrong character.
    const std::vector<int> after = og::ui::collect_base_camp_slots(save);
    ASSERT_EQ(2u, after.size());
    EXPECT_EQ("FIGHTER", save.team_list[after[0]]->name);
    EXPECT_TRUE(save.team_list[after[0]]->deployed);
    EXPECT_EQ("BENCHED", save.team_list[after[1]]->name);
    EXPECT_FALSE(save.team_list[after[1]]->deployed)
        << "the held-back flag survives the fold (§3.3 [SAVE-R4])";
}

// ---------------------------------------------------------------------------
// §2.5 MP display model (stage mp-columns): the merged lobby roster, the
// machine-grouped READY counts, the networked header line, and the U7
// COMPANY-column row shape.
// ---------------------------------------------------------------------------

namespace {

og::sim::LobbyPlayer make_lobby_seat(std::uint8_t player_index,
                                     const char* name,
                                     const char* company,
                                     bool is_host,
                                     bool ready,
                                     int deployed_slots,
                                     int benched_slots)
{
    og::sim::LobbyPlayer player;
    player.player_index = player_index;
    player.name = name;
    player.company = company;
    player.is_host = is_host;
    player.ready = ready;
    for (int i = 0; i < deployed_slots + benched_slots; ++i) {
        og::sim::LobbyCharacterSlot slot;
        slot.slot_index = static_cast<std::uint8_t>(i);
        slot.deployed = i < deployed_slots;
        slot.character.name = std::format("{}-{}", name, i);
        slot.character.family = FAMILY_ELF;
        slot.character.level = 3;
        player.character_slots.push_back(std::move(slot));
    }
    return player;
}

} // namespace

TEST(BaseCampMpDisplay, solo_collect_is_an_owned_passthrough)
{
    SaveData save;
    save.team_list[2] = make_base_camp_member("TWO", FAMILY_SOLDIER);
    save.team_list[5] = make_base_camp_member("FIVE", FAMILY_MAGE);
    save.team_list[5]->deployed = false;
    save.team_size = 2;

    // players would be a foreign roster — ignored when not networked.
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(9, "other", "OTHER CO", false, false, 2, 0)};
    const auto slots = og::ui::collect_base_camp_display_slots(
        save, players, {}, /*networked=*/false);
    ASSERT_EQ(2u, slots.size());
    EXPECT_TRUE(slots[0].owned);
    EXPECT_EQ(2, slots[0].save_slot);
    EXPECT_TRUE(slots[0].deployed);
    EXPECT_TRUE(slots[1].owned);
    EXPECT_EQ(5, slots[1].save_slot);
    EXPECT_FALSE(slots[1].deployed);
    EXPECT_TRUE(slots[0].company.empty()) << "solo rows carry no company";
}

TEST(BaseCampMpDisplay, networked_collect_merges_own_first_then_owner_index)
{
    SaveData save;
    save.save_name = "MY BAND";
    save.team_list[0] = make_base_camp_member("MINE", FAMILY_SOLDIER);
    save.team_size = 1;

    // Foreign machines arrive out of order; the local seat (index 4) must
    // be skipped — the private save is the authority for own rows.
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(9, "net-b", "BRAVO BAND", false, false, 1, 1),
        make_lobby_seat(4, "net-me", "MY BAND", false, false, 1, 0),
        make_lobby_seat(2, "net-a", "ALPHA BAND", true, false, 1, 0),
    };
    const auto slots = og::ui::collect_base_camp_display_slots(
        save, players, {4}, /*networked=*/true);
    ASSERT_EQ(4u, slots.size());
    EXPECT_TRUE(slots[0].owned);
    EXPECT_EQ(0, slots[0].save_slot);
    EXPECT_EQ("MY BAND", slots[0].company)
        << "networked own rows carry the own company for the column";
    EXPECT_FALSE(slots[1].owned);
    EXPECT_EQ(2, slots[1].owner_player_index)
        << "foreign rows sort by owner player index";
    EXPECT_EQ("ALPHA BAND", slots[1].company);
    EXPECT_EQ("net-a-0", slots[1].character.name)
        << "foreign display data rides the wire copy";
    EXPECT_EQ(9, slots[2].owner_player_index);
    EXPECT_TRUE(slots[2].deployed);
    EXPECT_EQ(9, slots[3].owner_player_index);
    EXPECT_FALSE(slots[3].deployed)
        << "the wire deploy flag reaches the display slot";
}

TEST(BaseCampMpDisplay, deploy_counts_read_the_live_save_flag_for_own_rows)
{
    SaveData save;
    save.save_name = "MY BAND";
    save.team_list[0] = make_base_camp_member("MINE", FAMILY_SOLDIER);
    save.team_size = 1;
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(2, "net-a", "ALPHA BAND", true, false, 2, 1)};
    const auto slots = og::ui::collect_base_camp_display_slots(
        save, players, {}, /*networked=*/true);
    ASSERT_EQ(4u, slots.size());

    og::ui::BaseCampDeployCounts counts =
        og::ui::count_base_camp_display_deploys(slots, save);
    EXPECT_EQ(3, counts.deployed);
    EXPECT_EQ(4, counts.total);

    // A toggle (or a §4.2 deploy_reconcile adoption) flips the SAVE flag;
    // the count follows it without re-collecting — own rows are live.
    save.team_list[0]->deployed = false;
    counts = og::ui::count_base_camp_display_deploys(slots, save);
    EXPECT_EQ(2, counts.deployed);
    EXPECT_EQ(4, counts.total);
}

TEST(BaseCampMpDisplay, ready_counts_group_seats_into_machines)
{
    EXPECT_EQ("net-a", og::ui::lobby_player_machine_key("net-a"));
    EXPECT_EQ("net-a", og::ui::lobby_player_machine_key("net-a#3"));
    EXPECT_EQ("x", og::ui::lobby_player_machine_key("x#1#2"));

    // Host machine has 2 seats (seat 1 is is_host=false but must NOT count
    // as a gating machine — the §4.3 multi-seat-host rule). Machine "net-b"
    // has 2 seats and counts ready only when BOTH are. Machine "net-c" is a
    // single ready seat; "net-d" is an unready spectator (0 slots).
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0),
        make_lobby_seat(1, "net-host#1", "HOST CO", false, false, 1, 0),
        make_lobby_seat(2, "net-b", "B CO", false, true, 1, 0),
        make_lobby_seat(3, "net-b#1", "B CO", false, false, 1, 0),
        make_lobby_seat(4, "net-c", "C CO", false, true, 1, 0),
        make_lobby_seat(5, "net-d", "D CO", false, false, 0, 0),
    };
    og::ui::BaseCampReadyCounts counts =
        og::ui::count_base_camp_ready_machines(players);
    EXPECT_EQ(3, counts.machines) << "net-b, net-c, net-d (host excluded)";
    EXPECT_EQ(1, counts.ready) << "only net-c has every seat ready";

    // Both net-b seats ready => the machine counts.
    std::vector<og::sim::LobbyPlayer> all = players;
    all[3].ready = true;
    counts = og::ui::count_base_camp_ready_machines(all);
    EXPECT_EQ(2, counts.ready);
    EXPECT_EQ(3, counts.machines);
}

TEST(BaseCampMpDisplay, session_census_counts_machines_and_players)
{
    // Empty lobby (pre-first-state): 0/0.
    const og::ui::BaseCampSessionCensus none =
        og::ui::count_base_camp_session_census({});
    EXPECT_EQ(0, none.machines);
    EXPECT_EQ(0, none.players);

    // §9.12: machines = distinct machine keys with the HOST machine
    // INCLUDED (unlike the §4.3 ready counts); players = total seats.
    // net-host has 2 seats, net-b has 2 seats, net-c one — 3 machines,
    // 5 players.
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0),
        make_lobby_seat(1, "net-host#1", "HOST CO", false, false, 1, 0),
        make_lobby_seat(2, "net-b", "B CO", false, true, 1, 0),
        make_lobby_seat(3, "net-b#1", "B CO", false, false, 1, 0),
        make_lobby_seat(4, "net-c", "C CO", false, true, 1, 0),
    };
    const og::ui::BaseCampSessionCensus census =
        og::ui::count_base_camp_session_census(players);
    EXPECT_EQ(3, census.machines);
    EXPECT_EQ(5, census.players);
}

TEST(BaseCampMpDisplay, host_display_name_prefers_company_over_wire_name)
{
    // §9.12: player names are machine-generated "net-<hex>" wire ids, so
    // the joiner's HOST: display uses the host machine's company (the
    // format_go_blockers naming convention), 16-char COMPANY clip.
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-h", "A COMPANY NAME PAST SIXTEEN", true,
                        false, 1, 0),
        make_lobby_seat(1, "net-j", "JOIN CO", false, false, 1, 0),
    };
    EXPECT_EQ("A COMPANY NAME P",
              og::ui::base_camp_host_display_name(players));

    // Empty company falls back to the machine key (seat suffix stripped).
    const std::vector<og::sim::LobbyPlayer> bare = {
        make_lobby_seat(0, "net-h#2", "", true, false, 1, 0)};
    EXPECT_EQ("net-h", og::ui::base_camp_host_display_name(bare));

    // No host seat yet (pre-election): empty.
    const std::vector<og::sim::LobbyPlayer> unelected = {
        make_lobby_seat(1, "net-j", "JOIN CO", false, false, 1, 0)};
    EXPECT_EQ("", og::ui::base_camp_host_display_name(unelected));
}

TEST(BaseCampMpDisplay, session_status_shapes_hold_the_line_b_budget)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-h", "IRON KETTLE BAND", true, false, 2, 0),
        make_lobby_seat(1, "net-j", "JOIN RIVER BAND", false, false, 1, 0),
        make_lobby_seat(2, "net-j#1", "JOIN RIVER BAND", false, false, 1, 0),
    };

    // §9.12 host shape: role + room + census. "MACH / PLYR" is the
    // recorded budget latitude (spelled-out overruns 42 at double digits).
    const std::string host = og::ui::format_base_camp_session_status(
        true, "GLAD-7Q2F", players);
    EXPECT_EQ("HOSTING GLAD-7Q2F - 2 MACH / 3 PLYR", host);
    // Relay-less (LAN) host: the census alone.
    EXPECT_EQ("HOSTING 2 MACH / 3 PLYR",
              og::ui::format_base_camp_session_status(true, "", players));

    // §9.12 joiner shape: room + the host machine's company.
    EXPECT_EQ("IN GLAD-7Q2F - HOST: IRON KETTLE BAND",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      players));
    // Direct (LAN) joiner: no room code.
    EXPECT_EQ("JOINED - HOST: IRON KETTLE BAND",
              og::ui::format_base_camp_session_status(false, "", players));
    // Host not yet known (pre-election on a dedicated server).
    EXPECT_EQ("IN GLAD-7Q2F",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      {}));
    EXPECT_EQ("JOINED",
              og::ui::format_base_camp_session_status(false, "", {}));

    // Budget pins: the absolute worst shapes fit the 42-char line-B band
    // (x=8 text, pager wall at x=263 => 42 chars) — host at the 16-seat
    // global cap, joiner at the full 16-char company clip; a pathological
    // room code display-clips at 12 and the whole line at 42.
    std::vector<og::sim::LobbyPlayer> sixteen;
    for (int i = 0; i < 16; ++i) {
        sixteen.push_back(make_lobby_seat(
            static_cast<std::uint8_t>(i),
            std::format("net-{:02}", i).c_str(), "C", i == 0, false, 1, 0));
    }
    const std::string worst_host = og::ui::format_base_camp_session_status(
        true, "GLAD-XXXX", sixteen);
    EXPECT_EQ("HOSTING GLAD-XXXX - 16 MACH / 16 PLYR", worst_host);
    EXPECT_LE(worst_host.size(), 42u);
    const std::string worst_join = og::ui::format_base_camp_session_status(
        false, "GLAD-XXXX", players);
    EXPECT_LE(worst_join.size(), 42u);
    EXPECT_LE(og::ui::format_base_camp_session_status(
                  true, "GLAD-TOO-LONG-CODE", sixteen)
                  .size(),
              42u);
}

TEST(BaseCampMpDisplay, line_b_gives_the_alert_slot_and_color_precedence)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-h", "IRON KETTLE BAND", true, false, 1, 0)};

    // Healthy: the session status, plain color.
    const og::ui::BaseCampLineB healthy = og::ui::compose_base_camp_line_b(
        std::nullopt, true, "GLAD-7Q2F", players);
    EXPECT_FALSE(healthy.alert);
    EXPECT_EQ("HOSTING GLAD-7Q2F - 1 MACH / 1 PLYR", healthy.text);

    // Degraded: the alert takes the slot AND the color (§9.12 precedence —
    // the ORANGE mapping rides the alert flag).
    const og::ui::BaseCampLineB degraded = og::ui::compose_base_camp_line_b(
        std::optional<std::string>("Status: connection lost"), true,
        "GLAD-7Q2F", players);
    EXPECT_TRUE(degraded.alert);
    EXPECT_EQ("Status: connection lost", degraded.text);
}

TEST(BaseCampMpDisplay, net_row_formats_hold_their_budgets)
{
    // §9.5.3: no HP column in the networked shape either.
    const og::ui::BaseCampNetRowText row = og::ui::format_base_camp_net_row(
        "TWELVECHARSNAME", "A COMPANY NAME PAST SIXTEEN", 123);
    EXPECT_EQ("TWELVECHAR", row.name) << "MP name budget is 10 chars (U7)";
    EXPECT_EQ("A COMPANY NAME P", row.company) << "COMPANY column is 16 chars";
    EXPECT_EQ("123", row.level);

    // §9.9 graft (b): the MP level left-pads to 2 like the solo shape.
    EXPECT_EQ(" 5", og::ui::format_base_camp_net_row("A", "B", 5).level);
}

TEST(BaseCampMpDisplay, display_guy_maps_the_wire_fields)
{
    og::sim::LobbyCharacterData character;
    character.name = "Wire Elf";
    character.family = FAMILY_ELF;
    character.strength = 11;
    character.dexterity = 12;
    character.constitution = 13;
    character.intelligence = 14;
    character.armor = 15;
    character.exp = 777;
    character.level = 6;

    const std::unique_ptr<guy> display =
        og::ui::make_base_camp_display_guy(character);
    ASSERT_NE(nullptr, display);
    EXPECT_EQ("Wire Elf", display->name);
    EXPECT_EQ(FAMILY_ELF, static_cast<int>(display->family));
    EXPECT_EQ(11, display->strength);
    EXPECT_EQ(12, display->dexterity);
    EXPECT_EQ(13, display->constitution);
    EXPECT_EQ(14, display->intelligence);
    EXPECT_EQ(15, display->armor);
    EXPECT_EQ(777u, display->exp);
    EXPECT_EQ(6, display->level);
}

TEST(BaseCampMpDisplay, display_slots_page_defensively_past_24)
{
    // Two well-stocked machines: 20 own + 20 replicated = 40 display slots
    // (§4.2: full rosters always replicate for display). The page window
    // derives from the display size — 5 pages at the §9.14 8-row grid,
    // never a 24-row clamp.
    SaveData save;
    save.save_name = "MY BAND";
    for (int i = 0; i < 20; ++i) {
        save.team_list[i] = make_base_camp_member("M", FAMILY_SOLDIER);
    }
    save.team_size = 20;
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(2, "net-a", "ALPHA BAND", true, false, 20, 0)};
    const auto slots = og::ui::collect_base_camp_display_slots(
        save, players, {}, /*networked=*/true);
    ASSERT_EQ(40u, slots.size());

    const og::ui::PageModel page =
        og::ui::PageModel::make(static_cast<int>(slots.size()), 9);
    EXPECT_EQ(5, page.page_count());
}

// ---------------------------------------------------------------------------
// §2.6 GO/READY slot (stage ready-go-slot): the full pure state table (U10),
// the blocked-GO popup body, and the §2.7 cross-control label.
//
// Face-color decision record (§2.0 U1, verified by the mandated one-frame
// TESTING capture): 61 (green run) and 93 (yellow run) PASSED the DARK_BLUE
// contrast check and ship as designed; 45 (the dark special red) FAILED
// (1.23:1 vs DARK_BLUE) and its state takes the sanctioned fallback
// grammar's shipped RED=40. These pins are the table's oracle.
// ---------------------------------------------------------------------------

namespace {

og::ui::ReadyGoPresentation ready_go(bool networked,
                                     bool is_host,
                                     bool my_ready,
                                     bool all_other_machines_ready,
                                     int global_deployed,
                                     int own_deployed,
                                     bool cross_control,
                                     bool spectator)
{
    return og::ui::format_ready_go_button(networked, is_host, my_ready,
                                          all_other_machines_ready,
                                          global_deployed, own_deployed,
                                          cross_control, spectator);
}

} // namespace

TEST(ReadyGoSlot, state_1_solo_go_stays_grey_and_uncaptioned)
{
    // §2.6 state 1 (pinned byte-identical): solo/local multi never consults
    // ready — my_ready/all_ready/cross/spectator are ignored.
    for (const bool noise : {false, true}) {
        const og::ui::ReadyGoPresentation p =
            ready_go(false, noise, noise, noise, 3, 3, noise, noise);
        EXPECT_EQ(og::ui::ReadyGoState::LocalGo, p.state);
        EXPECT_EQ("GO", p.label);
        EXPECT_EQ(og::ui::kReadyGoFaceGrey, p.face_color);
        EXPECT_EQ(13, og::ui::kReadyGoFaceGrey) << "BUTTON_FACING";
        EXPECT_TRUE(p.caption.empty());
    }
}

TEST(ReadyGoSlot, state_2_solo_no_deploy_keeps_grey_and_captions)
{
    const og::ui::ReadyGoPresentation p =
        ready_go(false, true, false, true, 0, 0, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::LocalGoNoDeploy, p.state);
    EXPECT_EQ("GO", p.label);
    EXPECT_EQ(og::ui::kReadyGoFaceGrey, p.face_color)
        << "state 2 keeps the grey face; only the click popups";
    EXPECT_EQ("DEPLOY AT LEAST ONE", p.caption);
}

TEST(ReadyGoSlot, state_3_host_gated_yellow_rule3_outranks_rule4)
{
    // Machines not ready (even with deploys) => WAITING caption.
    const og::ui::ReadyGoPresentation waiting =
        ready_go(true, true, false, false, 5, 2, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::HostGated, waiting.state);
    EXPECT_EQ("GO", waiting.label);
    EXPECT_EQ(og::ui::kReadyGoFaceGated, waiting.face_color);
    EXPECT_EQ(93, og::ui::kReadyGoFaceGated) << "yellow run (passed U1)";
    EXPECT_EQ("WAITING FOR OTHERS", waiting.caption);

    // All ready but nobody deployed => the rule-4 caption.
    const og::ui::ReadyGoPresentation undeployed =
        ready_go(true, true, false, true, 0, 0, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::HostGated, undeployed.state);
    EXPECT_EQ(og::ui::kReadyGoFaceGated, undeployed.face_color);
    EXPECT_EQ("NO ONE IS DEPLOYED", undeployed.caption);

    // Both unmet => rule 3 outranks rule 4 (the server's start_allowed
    // order).
    const og::ui::ReadyGoPresentation both =
        ready_go(true, true, false, false, 0, 0, false, false);
    EXPECT_EQ("WAITING FOR OTHERS", both.caption);
}

TEST(ReadyGoSlot, state_4_host_go_green)
{
    const og::ui::ReadyGoPresentation p =
        ready_go(true, true, false, true, 1, 0, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::HostGo, p.state);
    EXPECT_EQ("GO", p.label);
    EXPECT_EQ(og::ui::kReadyGoFaceGo, p.face_color);
    EXPECT_EQ(61, og::ui::kReadyGoFaceGo) << "green run (passed U1)";
    EXPECT_TRUE(p.caption.empty());
}

TEST(ReadyGoSlot, state_5_client_unready_red_with_deploy_gate)
{
    // Deployed characters: the click acts directly (no caption).
    const og::ui::ReadyGoPresentation free =
        ready_go(true, false, false, false, 3, 1, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, free.state);
    EXPECT_EQ("READY", free.label);
    EXPECT_EQ(og::ui::kReadyGoFaceUnready, free.face_color);
    EXPECT_EQ(40, og::ui::kReadyGoFaceUnready)
        << "the U1 fallback RED (45 failed the contrast capture)";
    EXPECT_TRUE(free.caption.empty());

    // Brought characters, none deployed, cross-control OFF => gated click.
    const og::ui::ReadyGoPresentation gated =
        ready_go(true, false, false, false, 3, 0, false, false);
    EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, gated.state);
    EXPECT_EQ("DEPLOY AT LEAST ONE", gated.caption);

    // Cross-control ON removes the per-machine minimum (§0.6).
    const og::ui::ReadyGoPresentation cross =
        ready_go(true, false, false, false, 3, 0, true, false);
    EXPECT_TRUE(cross.caption.empty());
}

TEST(ReadyGoSlot, state_5_spectator_machines_ready_freely)
{
    // [NET-R9]: the spectator machine (zero contributed character slots)
    // gets the READY button and may ready with nothing deployed.
    const og::ui::ReadyGoPresentation p =
        ready_go(true, false, false, false, 3, 0, false, true);
    EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, p.state);
    EXPECT_EQ("READY", p.label);
    EXPECT_EQ(og::ui::kReadyGoFaceUnready, p.face_color);
    EXPECT_TRUE(p.caption.empty()) << "spectators ready freely";
}

TEST(ReadyGoSlot, state_6_client_ready_green_unready_action)
{
    // Label = the action, color = the state: a ready client shows the
    // UNREADY action on the green face — including while everyone waits.
    for (const bool all_ready : {false, true}) {
        const og::ui::ReadyGoPresentation p =
            ready_go(true, false, true, all_ready, 3, 0, false, false);
        EXPECT_EQ(og::ui::ReadyGoState::ClientReady, p.state);
        EXPECT_EQ("UNREADY", p.label);
        EXPECT_EQ(og::ui::kReadyGoFaceGo, p.face_color);
        EXPECT_TRUE(p.caption.empty());
    }
}

TEST(ReadyGoSlot, labels_fit_the_68px_face_budget)
{
    // floor((68-8)/6) = 10 chars.
    for (const char* label : {"GO", "READY", "UNREADY"})
        EXPECT_LE(std::string_view(label).size(), 10u) << label;
}

TEST(ReadyGoSlot, go_blockers_lists_unready_machines_only)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0),
        make_lobby_seat(1, "net-host#1", "HOST CO", false, false, 1, 0),
        make_lobby_seat(2, "net-b", "BRAVO BAND", false, true, 1, 0),
        make_lobby_seat(3, "net-b#1", "BRAVO BAND", false, false, 0, 1),
        make_lobby_seat(4, "net-c", "CHARLIE BAND", false, true, 1, 0),
        make_lobby_seat(5, "net-d", "", false, false, 0, 0),
    };
    const std::string body = og::ui::format_go_blockers(players);
    // Host machine excluded even with unready seats; BRAVO gates because
    // seat #1 is unready (all-seats rule); CHARLIE is ready; net-d falls
    // back to its machine key when the company is empty.
    EXPECT_EQ("BRAVO BAND\nnet-d", body);
}

TEST(ReadyGoSlot, go_blockers_clips_and_caps_the_popup)
{
    std::vector<og::sim::LobbyPlayer> players;
    players.push_back(make_lobby_seat(0, "net-host", "HOST CO", true, false,
                                      1, 0));
    for (int i = 0; i < 6; ++i) {
        players.push_back(make_lobby_seat(
            static_cast<std::uint8_t>(1 + i),
            std::format("net-m{}", i).c_str(),
            "A VERY LONG COMPANY NAME INDEED", false, false, 1, 0));
    }
    const std::string body = og::ui::format_go_blockers(players);
    // Six blockers: 4 named lines (each clipped to 26 chars) + the tail.
    std::size_t lines = 1;
    for (const char c : body)
        lines += (c == '\n') ? 1u : 0u;
    EXPECT_EQ(5u, lines);
    EXPECT_NE(std::string::npos, body.find("AND 2 MORE"));
    EXPECT_NE(std::string::npos, body.find("A VERY LONG COMPANY NAME I"))
        << "26-char clip";
    EXPECT_EQ(std::string::npos, body.find("INDEED"));
}

TEST(ReadyGoSlot, cross_control_label_states)
{
    EXPECT_EQ("CTRL: OWN", og::ui::format_cross_control_label(false));
    EXPECT_EQ("CTRL: ALL", og::ui::format_cross_control_label(true));
}
