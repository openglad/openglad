#include <gtest/gtest.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
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

TEST(PickerCommon, cycle_ctf_team_count_wraps_2_3_4)
{
    SaveData save;
    ASSERT_EQ(2, (int)save.ctf_team_count) << "classic default is 2 teams";

    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(3, (int)save.ctf_team_count);
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(4, (int)save.ctf_team_count);
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(2, (int)save.ctf_team_count) << "cycle wraps back to 2";

    // Out-of-range values normalize back into the cycle.
    save.ctf_team_count = 0;
    og::ui::cycle_ctf_team_count(save);
    ASSERT_EQ(2, (int)save.ctf_team_count);
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
    ASSERT_EQ("CTF Teams: 2", og::ui::format_ctf_teams_label(save));
    save.ctf_team_count = 4;
    ASSERT_EQ("CTF Teams: 4", og::ui::format_ctf_teams_label(save));

    ASSERT_EQ("Capture Limit: Map default", og::ui::format_ctf_caps_label(save));
    save.ctf_capture_limit = 5;
    ASSERT_EQ("Capture Limit: 5", og::ui::format_ctf_caps_label(save));
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
