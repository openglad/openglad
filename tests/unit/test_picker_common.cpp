#include <gtest/gtest.h>
#include <algorithm>
#include <openglad/core/text_wrap.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include "test_game_world_fixture.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
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

TEST(PickerCommon, calculate_sell_value_matches_death_salvage_basis)
{
    init_family_registry();
    guy member(FAMILY_SOLDIER);
    EXPECT_EQ(187u, og::ui::calculate_sell_value(member))
        << "a base soldier sells for 75% of its 250-gold heart value";

    member.strength = static_cast<short>(member.strength + 8);
    guy valued(member);
    const std::uint32_t heart_value =
        static_cast<std::uint32_t>(valued.query_heart_value());
    EXPECT_EQ(static_cast<std::uint32_t>(
                  static_cast<std::uint64_t>(heart_value) * 3u / 4u),
              og::ui::calculate_sell_value(member));

    member.family = 99;
    EXPECT_EQ(0u, og::ui::calculate_sell_value(member));
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
        no_empty_slots.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);

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
        if (save.team_list[static_cast<std::size_t>(i)] && save.team_list[static_cast<std::size_t>(i)]->name == recruit2->name) {
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
        ASSERT_TRUE(save.team_list[static_cast<std::size_t>(i)] == nullptr);
    }
}

// --- class description budget (issue #152) ---

// Every playable family's description, flowed exactly as the HIRE screen
// flows it (Paragraphs mode at the description box's 27-char budget:
// description_box_content.w / 6 = 164 / 6), must fit the box — at most 10
// lines (the 8px fallback pitch ceiling) and no line over 27 chars. This is
// the regression guard that a future pack edit cannot silently overflow the
// box.
TEST(PickerCommon, playable_descriptions_flow_within_hire_box)
{
    init_family_registry();
    int checked = 0;
    for (int family_id = 0; family_id < 256; family_id++)
    {
        const auto* fd = get_family_descriptor(family_id);
        if (fd == nullptr || !fd->is_playable || fd->description == nullptr)
            continue;
        const std::vector<std::string> flowed = og::core::wrap_text(
            fd->description, 27, og::core::WrapMode::Paragraphs);
        EXPECT_LE(flowed.size(), 10u)
            << "family " << family_id
            << " description overflows the HIRE box (27 chars x 10 lines)";
        for (const std::string& line : flowed)
            EXPECT_LE(line.size(), 27u)
                << "family " << family_id << " over-wide line: " << line;
        checked++;
    }
    EXPECT_GE(checked, 10) << "core class pack not installed";
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

namespace {

std::vector<std::string> name_pool_of(int family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    std::vector<std::string> names;
    if (fd != nullptr && fd->name_pool != nullptr)
        for (int i = 0; i < fd->name_pool_size; i++)
            names.emplace_back(fd->name_pool[i]);
    return names;
}

} // namespace

// Several families deliberately draw from the SAME name list: the archmage
// from the mage names, the orc captain from the orc names, all three slimes
// from one slime list. The hardcoded switch spelled those aliases out; now the
// descriptors carry them, and this pins that the aliasing survived the move.
TEST(PickerCommon, families_that_share_a_name_pool_still_share_it)
{
    EXPECT_FALSE(name_pool_of(FAMILY_MAGE).empty());
    EXPECT_EQ(name_pool_of(FAMILY_MAGE), name_pool_of(FAMILY_ARCHMAGE));
    EXPECT_FALSE(name_pool_of(FAMILY_ORC).empty());
    EXPECT_EQ(name_pool_of(FAMILY_ORC), name_pool_of(FAMILY_BIG_ORC));
    EXPECT_FALSE(name_pool_of(FAMILY_SLIME).empty());
    EXPECT_EQ(name_pool_of(FAMILY_SLIME), name_pool_of(FAMILY_MEDIUM_SLIME));
    EXPECT_EQ(name_pool_of(FAMILY_SLIME), name_pool_of(FAMILY_SMALL_SLIME));
}

// The names come off the descriptor, so a class-pack family names its own
// recruits with no engine change: patch the pool a pack install would write
// and every generated name comes from it.
TEST(PickerCommon, get_random_name_follows_the_descriptor_name_pool)
{
    const FamilyDescriptor* original = get_family_descriptor(FAMILY_GOLEM);
    ASSERT_NE(nullptr, original);
    const FamilyDescriptor saved = *original;

    static const char* const kPackNames[] = {"Quartzite", "Basalt"};
    FamilyDescriptor patched = saved;
    patched.name_pool = kPackNames;
    patched.name_pool_size = 2;
    ASSERT_TRUE(set_family_descriptor(FAMILY_GOLEM, patched));

    std::set<std::string> seen;
    std::srand(7);
    for (int i = 0; i < 200; i++)
        seen.insert(og::ui::get_random_name(FAMILY_GOLEM));

    ASSERT_TRUE(set_family_descriptor(FAMILY_GOLEM, saved));
    EXPECT_EQ(std::set<std::string>({"Quartzite", "Basalt"}), seen);
}

// A family with no pool of its own borrows the soldier pool — the fallback the
// replaced switch spelled out as its `default:` branch, and what the core pack
// relies on for the three unhireable families (golem, giant skeleton, tower).
TEST(PickerCommon, get_random_name_without_a_pool_borrows_the_soldier_pool)
{
    const std::vector<std::string> soldier = name_pool_of(FAMILY_SOLDIER);
    ASSERT_FALSE(soldier.empty());
    const std::set<std::string> soldier_set(soldier.begin(), soldier.end());

    for (int family : {FAMILY_GOLEM, FAMILY_GIANT_SKELETON, FAMILY_TOWER1}) {
        ASSERT_TRUE(name_pool_of(family).empty())
            << "family " << family << " is expected to ship no names";
    }

    std::srand(19);
    for (int i = 0; i < 100; i++) {
        const char* name = og::ui::get_random_name(FAMILY_GOLEM);
        ASSERT_NE(nullptr, name);
        EXPECT_EQ(1u, soldier_set.count(name)) << "unexpected name " << name;
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
        save.team_list[static_cast<std::size_t>(slot)] = std::make_unique<guy>(FAMILY_SLIME);
        save.team_list[static_cast<std::size_t>(slot)]->name = name;
        ++slot;
        save.team_list[static_cast<std::size_t>(slot)] = std::make_unique<guy>(FAMILY_SLIME);
        save.team_list[static_cast<std::size_t>(slot)]->name = name + "2";
        ++slot;
    }
    save.team_size = static_cast<unsigned char>(slot);

    std::srand(3);
    const std::string unique_name = og::ui::get_unique_name(FAMILY_SLIME, save);
    EXPECT_TRUE(unique_name.ends_with("3"));
    for (int i = 0; i < save.team_size; ++i)
        ASSERT_NE(save.team_list[static_cast<std::size_t>(i)]->name, unique_name);
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
        ASSERT_TRUE(session.current_recruit()->family == og::ui::kAllowableGuys[static_cast<std::size_t>(i)]);
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
    ASSERT_TRUE(save.team_list[static_cast<std::size_t>(slot)]->name == "CustomName");

    session.rename_hired(-1, "Ignored");
    session.rename_hired(MAX_TEAM_SIZE, "Ignored");
    session.rename_hired(3, "Ignored");
    ASSERT_TRUE(save.team_list[static_cast<std::size_t>(slot)]->name == "CustomName");
}

TEST(PickerCommon, hire_session_team_full)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 999999;

    // Fill the team
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
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
        save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
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

TEST(PickerCommon, train_session_sell_checkpoint_failure_is_atomic)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Alpha";
    save.team_list[3] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[3]->name = "Bravo";
    save.team_list[3]->teamnum = 2;
    save.team_list[7] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[7]->name = "Charlie";
    save.team_list[7]->teamnum = 1;
    save.team_size = 3;
    save.m_totalcash[2] = 1000;

    og::ui::TrainSession session(save);
    ASSERT_TRUE(session.seek_slot(3));
    const std::uint32_t payout = session.current_sell_value();
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 9);
    EXPECT_EQ(payout, session.current_sell_value())
        << "unaccepted edits must not inflate the real member's sale price";

    bool checkpoint_saw_intact_roster = false;
    EXPECT_EQ(
        og::ui::TrainSession::SellResult::CheckpointFailed,
        session.sell_current([&] {
            checkpoint_saw_intact_roster =
                save.team_size == 3 && save.team_list[3] &&
                save.team_list[3]->name == "Bravo" &&
                save.m_totalcash[2] == 1000;
            return false;
        }));
    EXPECT_TRUE(checkpoint_saw_intact_roster)
        << "checkpoint must run before the first sale mutation";
    EXPECT_EQ(3, save.team_size);
    ASSERT_NE(nullptr, save.team_list[3]);
    EXPECT_EQ("Bravo", save.team_list[3]->name);
    EXPECT_EQ(1000u, save.m_totalcash[2]);
    EXPECT_EQ(3, session.current_slot());
    EXPECT_GT(session.working_copy().strength, session.original().strength);

    EXPECT_EQ(
        og::ui::TrainSession::SellResult::Sold,
        session.sell_current([&] {
            EXPECT_EQ(3, save.team_size);
            EXPECT_EQ("Bravo", save.team_list[3]->name);
            EXPECT_EQ(1000u, save.m_totalcash[2]);
            return true;
        }));
    EXPECT_EQ(2, save.team_size);
    EXPECT_EQ(1000u + payout, save.m_totalcash[2]);
    ASSERT_NE(nullptr, save.team_list[0]);
    ASSERT_NE(nullptr, save.team_list[1]);
    EXPECT_EQ("Alpha", save.team_list[0]->name);
    EXPECT_EQ("Charlie", save.team_list[1]->name);
    for (int slot = 2; slot < MAX_TEAM_SIZE; ++slot)
        EXPECT_EQ(nullptr, save.team_list[static_cast<std::size_t>(slot)])
            << "sale must leave the serialized roster prefix dense";
    EXPECT_EQ(1, session.current_slot());
    EXPECT_EQ("Charlie", session.working_copy().name);
}

TEST(PickerCommon, train_session_sell_last_member_clamps_and_saturates_wallet)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 99;
    save.team_size = 1;
    save.m_totalcash[SCORE_TEAM_COUNT - 1] =
        std::numeric_limits<std::uint32_t>::max() - 10u;

    og::ui::TrainSession session(save);
    int checkpoint_calls = 0;
    EXPECT_EQ(
        og::ui::TrainSession::SellResult::Sold,
        session.sell_current([&] {
            ++checkpoint_calls;
            return true;
        }));
    EXPECT_EQ(1, checkpoint_calls);
    EXPECT_TRUE(session.empty());
    EXPECT_EQ(0, save.team_size);
    EXPECT_EQ(nullptr, save.team_list[0]);
    EXPECT_EQ(std::numeric_limits<std::uint32_t>::max(),
              save.m_totalcash[SCORE_TEAM_COUNT - 1]);
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

// The retired TEAMS knob (amendment A3): the wheel is gone, so the cycler
// writes the only value the field holds and the label says so. A legacy
// 2/3/4 in a loaded save heals the moment either one is touched, exactly
// as the lobby sanitizer heals it.
TEST(PickerCommon, cycle_ctf_team_count_is_retired_and_answers_auto)
{
    SaveData save;
    ASSERT_EQ(0, (int)save.ctf_team_count)
        << "default is Auto (every team the map authors)";

    og::ui::cycle_ctf_team_count(save);
    EXPECT_EQ(0, (int)save.ctf_team_count) << "no wheel left to turn";

    save.ctf_team_count = 3;  // a v18 save from before the amendment
    og::ui::cycle_ctf_team_count(save);
    EXPECT_EQ(0, (int)save.ctf_team_count) << "the legacy value heals";
    EXPECT_EQ("Teams: Auto", og::ui::format_ctf_teams_label(save));
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
    ASSERT_EQ("Teams: Auto", og::ui::format_ctf_teams_label(save))
        << "retired (A3): the label never speaks for a stale save value";

    // A5: "SCORE: MAP" / "SCORE: n" — the score limit, named as such.
    ASSERT_EQ("SCORE: MAP", og::ui::format_ctf_score_label(save));
    save.ctf_capture_limit = 5;
    ASSERT_EQ("SCORE: 5", og::ui::format_ctf_score_label(save));

    // The SDL team-build buttons are 80px faces drawing 6px/char centered
    // text with no clipping: every label must stay inside the classic
    // 12-char budget (longest is "Teams: Auto" / "SCORE: MAP").
    save.ctf_team_count = 0;
    save.ctf_capture_limit = 10;
    ASSERT_LE(og::ui::format_ctf_teams_label(save).size(), 12u);
    ASSERT_LE(og::ui::format_ctf_score_label(save).size(), 12u);
    save.ctf_capture_limit = 0;
    ASSERT_LE(og::ui::format_ctf_score_label(save).size(), 12u);
}

TEST(PickerCommon, is_versus_campaign_reads_matchup_key)
{
    SaveData save;
    save.current_campaign = "gladiator";
    ASSERT_FALSE(og::ui::is_versus_campaign(save))
        << "the shipped classic campaign is cooperative";
    save.current_campaign = "";
    ASSERT_FALSE(og::ui::is_versus_campaign(save));
    save.current_campaign = "pc_no_such_pkg";
    ASSERT_FALSE(og::ui::is_versus_campaign(save));

    namespace fs = std::filesystem;
    const std::string id = "pc_versus_probe";
    const fs::path staging =
        fs::path(get_user_path()) / "pc_versus_staging" / id;
    std::error_code ec;
    fs::create_directories(staging, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream out(staging / "campaign.yaml");
        out << "format_version: 1\ntitle: Versus Probe\nversion: 1\n"
               "matchup: versus\n";
        ASSERT_TRUE(out.good());
    }
    const fs::path archive =
        fs::path(get_user_path()) / "campaigns" / (id + ".glad");
    fs::remove(archive, ec);
    ASSERT_EQ(ArchiveIoError::None,
              zip_contents_with_error(staging.string(), archive.string()));

    og::data::clear_campaign_metadata_cache();
    save.current_campaign = id;
    EXPECT_TRUE(og::ui::is_versus_campaign(save))
        << "matchup: versus makes the campaign a versus matchup";

    fs::remove(archive, ec);
    fs::remove_all(fs::path(get_user_path()) / "pc_versus_staging", ec);
    og::data::clear_campaign_metadata_cache();
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
    ASSERT_TRUE(og::ui::format_allied_mode_label(save) == "SEATS: SPLIT");

    save.allied_mode = 1;
    ASSERT_TRUE(og::ui::format_allied_mode_label(save) == "SEATS: TOGETHER");
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

TEST(PickerCommon, next_ctf_scenario_troops_cycle_orders)
{
    // Three states, and every state applies on every campaign:
    // ALL -> OWN -> FAIR -> ALL (matched-teams D28).
    EXPECT_EQ(2, og::ui::next_ctf_scenario_troops(0));
    EXPECT_EQ((short)og::sim::kTroopsMatched,
              og::ui::next_ctf_scenario_troops(2))
        << "after OWN comes TROOPS: FAIR";
    EXPECT_EQ(0, og::ui::next_ctf_scenario_troops(og::sim::kTroopsMatched))
        << "the cycle wraps back to ALL";

    // The retired middle state and any junk value read as OWN everywhere
    // else, so cycling off them lands on ALL.
    EXPECT_EQ(0, og::ui::next_ctf_scenario_troops(1));
    EXPECT_EQ(0, og::ui::next_ctf_scenario_troops(7));
    EXPECT_EQ(0, og::ui::next_ctf_scenario_troops(9));
    EXPECT_EQ(0, og::ui::next_ctf_scenario_troops(-1));
}

TEST(PickerCommon, toggle_ctf_scenario_troops_walks_three_states)
{
    SaveData save;
    ASSERT_EQ(0, save.ctf_strip_scenario_troops);
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ(2, save.ctf_strip_scenario_troops)
        << "the menus write 2 so networked peers agree on OWN";
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ((short)og::sim::kTroopsMatched, save.ctf_strip_scenario_troops)
        << "the menus write 3 (kTroopsMatched) for TROOPS: FAIR";
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ(0, save.ctf_strip_scenario_troops);

    // A save carrying the retired middle state cycles back to ALL.
    save.ctf_strip_scenario_troops = 1;
    og::ui::toggle_ctf_scenario_troops(save);
    ASSERT_EQ(0, save.ctf_strip_scenario_troops);
}

TEST(PickerCommon, format_ctf_troops_label_strings_fit_budget)
{
    SaveData save;
    ASSERT_EQ("TROOPS: ALL", og::ui::format_ctf_troops_label(save));
    save.ctf_strip_scenario_troops = 2;
    ASSERT_EQ("TROOPS: OWN", og::ui::format_ctf_troops_label(save));
    // A stored legacy 1 strips everything, so it must not read as ALL.
    save.ctf_strip_scenario_troops = 1;
    ASSERT_EQ("TROOPS: OWN", og::ui::format_ctf_troops_label(save));

    // FAIR (D28): the label is exactly "TROOPS: FAIR" — 12 chars, filling
    // the 80px/12-char face budget tight. The literal-equality assert is
    // deliberate: test_menu_layout's budget sweep only walks STATIC label
    // strings and never sees this formatted one, so this is where a future
    // rename re-trips the budget consciously ("TROOPS: EVEN" is the
    // recorded alternate).
    save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
    const std::string fair_label = og::ui::format_ctf_troops_label(save);
    ASSERT_EQ("TROOPS: FAIR", fair_label);
    ASSERT_LE(fair_label.size(), 12u);

    // Every state fits the 80px (12-character) SCENARIO face.
    for (short state = 0; state <= og::sim::kTroopsMatched; ++state)
    {
        save.ctf_strip_scenario_troops = state;
        ASSERT_LE(og::ui::format_ctf_troops_label(save).size(), 12u) << state;
    }
}

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

TEST(PickerCommon, local_seat_derivation_ignores_benched_only_teams)
{
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[0]->deployed = true;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 2;
    save.team_list[1]->deployed = false;
    save.team_size = 2;
    save.my_team = 1;

    EXPECT_EQ((std::vector<short>{1}), og::ui::derive_local_seat_teams(save));

    save.my_team = 2;
    EXPECT_EQ((std::vector<short>{1}), og::ui::derive_local_seat_teams(save))
        << "a preferred but benched-only color must not create an empty view";
}

TEST(PickerCommon, local_gameplay_seat_matrix_requires_one_deployed_control_per_view)
{
    // Exhaust every deployment subset, roster-color assignment, player count,
    // preferred team, and Together/Split setting for a four-character company.
    // The expected verdict is calculated only from the returned seat teams and
    // independently counted deployed characters, so repeated Together seats
    // and padded Split seats are both covered.
    for (int encoded_teams = 0; encoded_teams < 256; ++encoded_teams)
    {
        for (int deployed_mask = 0; deployed_mask < 16; ++deployed_mask)
        {
            for (short preferred = 0; preferred < 4; ++preferred)
            {
                for (short allied : {short{0}, short{1}})
                {
                    for (int player_count = 1; player_count <= 4; ++player_count)
                    {
                        SaveData save;
                        std::array<int, 4> available{};
                        int encoded = encoded_teams;
                        for (int slot = 0; slot < 4; ++slot)
                        {
                            const short team = static_cast<short>(encoded & 3);
                            encoded >>= 2;
                            auto member = std::make_unique<guy>(FAMILY_SOLDIER);
                            member->teamnum = team;
                            member->deployed = (deployed_mask & (1 << slot)) != 0;
                            if (member->deployed)
                                ++available[static_cast<std::size_t>(team)];
                            save.team_list[static_cast<std::size_t>(slot)] =
                                std::move(member);
                        }
                        save.team_size = 4;
                        save.my_team = preferred;
                        save.allied_mode = allied;
                        save.numplayers = static_cast<unsigned char>(player_count);

                        const std::vector<short> seats =
                            og::ui::derive_local_gameplay_seat_teams(save);
                        ASSERT_EQ(static_cast<std::size_t>(player_count), seats.size());
                        if (allied != 0)
                        {
                            EXPECT_TRUE(std::all_of(
                                seats.begin(), seats.end(),
                                [first = seats.front()](short team) {
                                    return team == first;
                                }));
                        }
                        else
                        {
                            std::set<short> distinct(seats.begin(), seats.end());
                            EXPECT_EQ(seats.size(), distinct.size());
                        }

                        bool expected = true;
                        for (const short team : seats)
                        {
                            if (team < 0 || team >= 4 ||
                                available[static_cast<std::size_t>(team)]-- <= 0)
                            {
                                expected = false;
                                break;
                            }
                        }
                        EXPECT_EQ(expected,
                                  og::ui::local_seat_teams_have_controls(save, seats))
                            << "teams=" << encoded_teams
                            << " deployed=" << deployed_mask
                            << " preferred=" << preferred
                            << " allied=" << allied
                            << " players=" << player_count;
                    }
                }
            }
        }
    }
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
    // campaigns, the multiplayer package, concept trailing.
    std::list<std::string> ids = {
        "concept",
        "gladiator",
        "imaginations",
        "longseason",
        "modes",
        "tower",
        "tryxian",
        "westlands",
    };
    const std::list<std::string> shelf = {
        "gladiator",
        "tryxian",
        "westlands",
        "longseason",
        "modes",
        "tower",
        "imaginations",
        "concept",
    };
    og::ui::order_campaigns_for_select(ids);
    ASSERT_EQ(shelf, ids);

    // Already ordered: stable no-op.
    og::ui::order_campaigns_for_select(ids);
    ASSERT_EQ(shelf, ids);

    // User-made packages keep their relative order after every shelved id.
    std::list<std::string> with_extras = {
        "a.campaign",
        "modes",
        "b.campaign",
        "gladiator",
    };
    og::ui::order_campaigns_for_select(with_extras);
    ASSERT_EQ((std::list<std::string>{
                  "gladiator",
                  "modes",
                  "a.campaign",
                  "b.campaign",
              }),
              with_extras);

    // No shelved ids at all: untouched.
    std::list<std::string> no_anchors = {"a.campaign", "b.campaign"};
    og::ui::order_campaigns_for_select(no_anchors);
    ASSERT_EQ((std::list<std::string>{"a.campaign", "b.campaign"}), no_anchors);

    // Only the modes package: a single-element list stays put.
    std::list<std::string> only_modes = {"modes"};
    og::ui::order_campaigns_for_select(only_modes);
    ASSERT_EQ((std::list<std::string>{"modes"}), only_modes);

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
        "gladiator",
        "tower",
        "modes",
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
                  "gladiator",
                  "modes",
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
        "test_absent_a",
        "test_absent_b",
        "test_absent_a",
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
        world().type = ctf_map ? GameWorld::TYPE_SCRIPTED : 0;
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
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());

    EXPECT_FALSE(report.is_versus);
    EXPECT_FALSE(report.staged);
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
    EXPECT_FALSE(any_line_contains(lines, "MATCH:"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// The census names entities through the shared precedence helper
// (entity_display_name, walker.cpp): the fighter's own myguy name first, the
// authored stats name second. A company fighter therefore gets its own row
// instead of grouping into a "3x SOLDIER" line, an authored NPC is unchanged,
// and a walker with neither name still groups.
TEST(PickerCommon, scenario_report_names_follow_the_shared_precedence)
{
    ReportWorld fx(false);
    // Both names present: the myguy name wins.
    walker* company = fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, "BARRACKS");
    auto owned = std::make_unique<guy>(FAMILY_SOLDIER);
    owned->name = "Striker";
    company->set_owned_myguy(std::move(owned));
    // Stats name only: the authored-NPC shape, unchanged.
    fx.spawn_living_named(FAMILY_ARCHER, 0, 5, "GONZO");
    // Neither name: still grouped by (team, family, level).
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    // The 11-char wire clamp against the longest family word — the widest
    // named row the report can produce.
    walker* widest = fx.spawn_living_named(FAMILY_BIG_ORC, 1, 12, nullptr);
    auto widest_guy = std::make_unique<guy>(FAMILY_BIG_ORC);
    widest_guy->name = "MOONSHADOWS"; // 11 chars, the clamp ceiling
    widest->set_owned_myguy(std::move(widest_guy));

    SaveData save;
    save.my_team = 0;
    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());

    const auto* company_row = find_named_row(report, "Striker");
    ASSERT_TRUE(company_row != nullptr)
        << "the myguy name wins over the stats name";
    EXPECT_EQ(1, company_row->count) << "a named fighter never groups";
    EXPECT_EQ(0, company_row->team);
    EXPECT_EQ(3, company_row->level);
    EXPECT_EQ(FAMILY_SOLDIER, company_row->family);
    EXPECT_TRUE(find_named_row(report, "BARRACKS") == nullptr)
        << "the stats name is the SECOND choice, not an extra row";

    const auto* npc_row = find_named_row(report, "GONZO");
    ASSERT_TRUE(npc_row != nullptr) << "an authored NPC still names itself";
    EXPECT_EQ(FAMILY_ARCHER, npc_row->family);

    const auto* grouped = find_group_row(report, 0, FAMILY_SOLDIER, 3);
    ASSERT_TRUE(grouped != nullptr) << "nameless walkers still group";
    EXPECT_EQ(2, grouped->count)
        << "the named fighter is not part of the grouped row";

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "  Striker - SOLDIER Lv 3"));
    EXPECT_TRUE(any_line_contains(lines, "  GONZO - ARCHER Lv 5"));
    EXPECT_TRUE(any_line_contains(lines, "  2x SOLDIER Lv 3"));
    EXPECT_TRUE(any_line_contains(lines, "  MOONSHADOWS - ORC CAPTAIN Lv 12"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

TEST(PickerCommon, scenario_report_versus_sections_fallback)
{
    ReportWorld fx(true);
    fx.spawn_anchor(0);
    fx.spawn_anchor(0);
    fx.spawn_anchor(1);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr); // roster team troops
    fx.spawn_generator(FAMILY_TENT, 0);
    fx.spawn_living_named(FAMILY_ORC, 1, 4, nullptr);     // bot team, kept
    fx.spawn_living_named(FAMILY_ELF, 2, 2, nullptr);     // no marker team
    fx.spawn_living_named(FAMILY_ELF, 5, 9, nullptr);     // non-score team

    SaveData save;
    save.current_campaign = "modes";
    save.my_team = 0;
    save.ctf_strip_scenario_troops = 0; // TROOPS: ALL
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());

    EXPECT_TRUE(report.is_versus);
    EXPECT_FALSE(report.staged);
    EXPECT_TRUE(report.will_activate);
    EXPECT_TRUE(report.team_authored[0]);
    EXPECT_TRUE(report.team_authored[1]);
    EXPECT_FALSE(report.team_authored[2]);
    EXPECT_TRUE(report.team_active[0]);
    EXPECT_TRUE(report.team_active[1]);
    EXPECT_FALSE(report.team_active[2]);
    EXPECT_EQ(2, report.team_anchor_count[0]);
    EXPECT_EQ(1, report.team_anchor_count[1]);

    ASSERT_TRUE(find_group_row(report, 0, FAMILY_SOLDIER, 3) != nullptr);
    ASSERT_TRUE(find_generator_row(report, 0) != nullptr);
    ASSERT_TRUE(find_group_row(report, 1, FAMILY_ORC, 4) != nullptr);
    ASSERT_TRUE(find_group_row(report, 2, FAMILY_ELF, 2) != nullptr);
    ASSERT_TRUE(find_group_row(report, 5, FAMILY_ELF, 9) != nullptr);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "MATCH: 2 AUTHORED TEAMS"));
    EXPECT_TRUE(any_line_contains(lines, "RED TEAM  MARKERS: 2  ACTIVE"));
    EXPECT_TRUE(any_line_contains(lines, "RED TEAM (YOURS)"));
    EXPECT_TRUE(any_line_contains(lines, "BLUE TEAM"))
        << "score teams keep color-name headers";
    EXPECT_TRUE(any_line_contains(lines, "TEAM 5"))
        << "non-score teams keep the raw index header";
    EXPECT_TRUE(any_line_contains(lines, "1x SOLDIER Lv 3"));
    // The strip-annotation machinery died with the plan phase (#218): a
    // STAGED world already contains the stripped truth, and the fallback
    // arm never re-derives the strip rule — no '+'/'!' suffix, no legend.
    for (const auto& line : lines)
    {
        EXPECT_EQ(std::string::npos, line.find('+')) << line;
        EXPECT_EQ(std::string::npos, line.find('!')) << line;
    }
    EXPECT_FALSE(any_line_contains(lines, "REMOVED:"));
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

TEST(PickerCommon, scenario_report_preserves_local_team_colors_in_allied_mode)
{
    // NO-PACK FALLBACK ARM (issue #218): activation is the count-only
    // clamp; the fallback census lists the authored cast verbatim (strip
    // outcomes are visible only in a STAGED world — the machinery that
    // re-derived them here died with the plan phase).
    ReportWorld fx(true);
    fx.spawn_anchor(0);
    fx.spawn_anchor(1);
    fx.spawn_anchor(3);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_ORC, 1, 4, nullptr);
    fx.spawn_living_named(FAMILY_ARCHER, 3, 5, nullptr);

    SaveData save;
    save.current_campaign = "modes";
    save.allied_mode = 1;
    save.my_team = 2;
    save.ctf_capture_limit = 5;
    save.ctf_strip_scenario_troops = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 3;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());

    EXPECT_EQ(2, report.your_team)
        << "local Together mode keeps the selected playing team";

    ASSERT_TRUE(find_group_row(report, 0, FAMILY_SOLDIER, 3) != nullptr);
    ASSERT_TRUE(find_group_row(report, 1, FAMILY_ORC, 4) != nullptr);
    ASSERT_TRUE(find_group_row(report, 3, FAMILY_ARCHER, 5) != nullptr);
}

TEST(PickerCommon, scenario_report_non_activating_ctf_keeps_classic_rules)
{
    ReportWorld fx(true);
    fx.spawn_anchor(0); // single authored team: the match will not activate
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);

    SaveData save;
    save.current_campaign = "modes";
    save.ctf_strip_scenario_troops = 0;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());

    EXPECT_TRUE(report.is_versus);
    EXPECT_FALSE(report.will_activate);
    ASSERT_TRUE(find_group_row(report, 0, FAMILY_SOLDIER, 3) != nullptr);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    EXPECT_TRUE(any_line_contains(lines, "MATCH INACTIVE"));
}

TEST(PickerCommon, scenario_report_no_pack_fallback_is_the_count_clamp)
{
    // The documented NO-PACK fallback arm (issue #218): with no STAGED
    // world the preview falls back to the count-only
    // og::sim::effective_team_mask clamp for EVERY TROOPS value — the
    // roster rule lives in the mode Lua alone and answers only through a
    // real staged init (the staged-report suite pins those shapes). These
    // pins are fallback-behavior pins, not activation oracles.
    ReportWorld fx(true);
    for (int team = 0; team < 4; ++team)
        fx.spawn_anchor(team);

    SaveData save;
    save.current_campaign = "modes";
    save.my_team = 0;
    save.ctf_strip_scenario_troops = 2;  // TROOPS: OWN
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[1]->teamnum = 2;
    save.team_list[2] = std::make_unique<guy>(FAMILY_ELF);
    save.team_list[2]->teamnum = 1;
    save.team_list[2]->deployed = false;  // held back: never in the roster
    save.team_size = 3;

    // TEAMS: Auto — the clamp answers the full authored mask.
    save.ctf_team_count = 0;
    {
        const og::ui::ScenarioRosterReport report =
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());
        EXPECT_FALSE(report.staged) << "no staged world = the fallback";
        EXPECT_FALSE(report.stage_failed)
            << "no staging session is a fallback, not a failure";
        EXPECT_EQ("", report.mode_name);
        EXPECT_TRUE(report.team_active[0]);
        EXPECT_TRUE(report.team_active[1]);
        EXPECT_TRUE(report.team_active[2]);
        EXPECT_TRUE(report.team_active[3]);
        const std::vector<std::string> lines =
            og::ui::format_scenario_report_lines(report);
        EXPECT_TRUE(any_line_contains(lines, "MATCH: 4 AUTHORED TEAMS"))
            << "the fallback keeps today's exact lines";
        EXPECT_TRUE(any_line_contains(lines, "GREEN TEAM  MARKERS: 1  ACTIVE"));
        EXPECT_TRUE(any_line_contains(lines, "BLUE TEAM  MARKERS: 1  ACTIVE"));
        EXPECT_FALSE(any_line_contains(lines, "MATCH RULES UNAVAILABLE"));
    }

    // TEAMS: 2 — the count clamp takes the first two AUTHORED teams
    // ({0, 1}); the deployed rosters {0, 2} do not steer the fallback
    // (the old twin previewed {0, 2} here — that rule now answers only
    // through a registered plan).
    save.ctf_team_count = 2;
    {
        const og::ui::ScenarioRosterReport report =
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());
        EXPECT_TRUE(report.team_active[0]);
        EXPECT_TRUE(report.team_active[1]);
        EXPECT_FALSE(report.team_active[2])
            << "count-only fallback: a roster team beyond the clamp count "
               "previews inactive without a plan";
        EXPECT_FALSE(report.team_active[3]);
    }

    // TEAMS: 3 — the first three authored teams.
    save.ctf_team_count = 3;
    {
        const og::ui::ScenarioRosterReport report =
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());
        EXPECT_TRUE(report.team_active[0]);
        EXPECT_TRUE(report.team_active[1]);
        EXPECT_TRUE(report.team_active[2]);
        EXPECT_FALSE(report.team_active[3]);
    }

    // FAIR falls back identically to OWN (both are plan-side rules).
    save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
    save.ctf_team_count = 2;
    {
        const og::ui::ScenarioRosterReport report =
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world());
        EXPECT_TRUE(report.team_active[0]);
        EXPECT_TRUE(report.team_active[1]);
        EXPECT_FALSE(report.team_active[2]);
        EXPECT_FALSE(report.team_active[3]);
    }
}

// The refusal sentence is chosen by the banked REASON, not by the mode's
// free-text error (lineup review L1): the team modes are short of TEAMS,
// a band mode (FFA/mutant) is short of FIGHTERS. The digit rides the
// shared facts slot, so a joiner's mirror — which holds the same mode
// vars — prints the same sentence as the host. Pinned over the report
// struct here; the staged suite pins the same strings end to end.
TEST(PickerCommon, scenario_report_refusal_sentence_follows_the_reason)
{
    og::ui::ScenarioRosterReport report;
    report.staged = true;
    report.is_versus = true;
    report.refusing = true;

    {
        const std::vector<std::string> lines =
            og::ui::format_scenario_report_lines(report);
        EXPECT_TRUE(any_line_contains(
            lines, "MATCH WILL NOT START: FEWER THAN 2 TEAMS"))
            << "reason 0 (and every world that banks nothing) is the teams "
               "sentence";
        EXPECT_FALSE(any_line_contains(lines, "FIGHTERS"));
    }

    report.refusal_fighters = true;
    {
        const std::vector<std::string> lines =
            og::ui::format_scenario_report_lines(report);
        EXPECT_TRUE(any_line_contains(
            lines, "MATCH WILL NOT START: FEWER THAN 2 FIGHTERS"));
        EXPECT_FALSE(any_line_contains(lines, "FEWER THAN 2 TEAMS"))
            << "a band has no teams to be short of";
        for (const auto& line : lines)
            EXPECT_LE(line.size(), 48u) << line;
    }
}

TEST(PickerCommon, scenario_report_degradation_lines_are_honest)
{
    // The staged preview's degradation shapes (#218): a failed owner stage
    // leads with STAGING FAILED over the fallback census; no world at all
    // renders the refusal lines only.
    ReportWorld fx(true);
    fx.spawn_anchor(0);
    fx.spawn_anchor(1);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);

    SaveData save;
    save.current_campaign = "modes";
    save.ctf_team_count = 0;

    const og::ui::ScenarioRosterReport failed =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::Failed, save, &fx.world());
    EXPECT_FALSE(failed.staged);
    EXPECT_TRUE(failed.stage_failed);
    EXPECT_TRUE(failed.will_activate) << "the count clamp still answers";
    const std::vector<std::string> failed_lines =
        og::ui::format_scenario_report_lines(failed);
    ASSERT_FALSE(failed_lines.empty());
    EXPECT_EQ("STAGING FAILED", failed_lines[0])
        << "the failure announcement leads the report";
    EXPECT_TRUE(any_line_contains(failed_lines, "MATCH: 2 AUTHORED TEAMS"))
        << "the fallback census still renders below";
    EXPECT_TRUE(any_line_contains(failed_lines, "1x SOLDIER Lv 3"));

    const og::ui::ScenarioRosterReport nothing =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, nullptr);
    EXPECT_TRUE(nothing.unavailable);
    const std::vector<std::string> nothing_lines =
        og::ui::format_scenario_report_lines(nothing);
    ASSERT_EQ(1u, nothing_lines.size());
    EXPECT_EQ("PREVIEW UNAVAILABLE", nothing_lines[0]);

    const og::ui::ScenarioRosterReport failed_nothing =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::Failed, save, nullptr);
    const std::vector<std::string> failed_nothing_lines =
        og::ui::format_scenario_report_lines(failed_nothing);
    ASSERT_EQ(2u, failed_nothing_lines.size());
    EXPECT_EQ("STAGING FAILED", failed_nothing_lines[0]);
    EXPECT_EQ("PREVIEW UNAVAILABLE", failed_nothing_lines[1]);
}

// --- Player seats in the View Level report (#218 seat block) ----------------

namespace {

og::sim::LobbyPlayer make_seat_player(std::uint8_t index, short team,
                                      std::string company, bool ready)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.name = std::format("net-{:016x}", index);  // opaque, never shown
    player.company = std::move(company);
    player.team = team;
    player.ready = ready;
    player.is_host = index == 0;
    return player;
}

} // namespace

// The ported seat vocabulary, pinned exactly: the 3-letter public company
// abbreviation with its "NET" fallback, the "P{n} YOU/ABC [RDY]" identity,
// and the match-shape summary strings. The LobbyPlayer -> row resolution
// (is_local decides YOU) is pinned through the production builder in
// seat_block_you_vs_abbreviation_is_per_client below.
TEST(PickerCommon, seat_vocabulary_labels_pin)
{
    EXPECT_EQ("BLU", og::ui::company_abbreviation("blue-company"));
    EXPECT_EQ("IRO", og::ui::company_abbreviation("Iron kettle band"))
        << "first three alphanumerics, upper-cased — not initials";
    EXPECT_EQ("NET", og::ui::company_abbreviation("  --  "))
        << "no alphanumerics falls back to NET, never the net-<hex> name";
    EXPECT_EQ("NET", og::ui::company_abbreviation(""));

    // The row is the single format authority the formatter uses.
    EXPECT_EQ("P1 YOU [RDY]", og::ui::seat_identity_label(og::ui::ScenarioSeatRow{
                  .player_index = 0,
                  .company = "Iron Kettle",
                  .team = 0,
                  .ready = true,
                  .is_local = true,
              }));
    EXPECT_EQ("P4 DEL", og::ui::seat_identity_label(og::ui::ScenarioSeatRow{
                  .player_index = 3,
                  .company = "Delta Guild",
                  .team = 1,
                  .ready = false,
                  .is_local = false,
              }));
    EXPECT_EQ("P4 DEL [RDY]",
              og::ui::seat_identity_label(og::ui::ScenarioSeatRow{
                  .player_index = 3,
                  .company = "Delta Guild",
                  .team = 1,
                  .ready = true,
                  .is_local = false,
              }));
    EXPECT_EQ("P1 YOU", og::ui::seat_identity_label(og::ui::ScenarioSeatRow{
                  .player_index = 0,
                  .company = "Iron Kettle",
                  .team = 0,
                  .ready = false,
                  .is_local = true,
              }));

    EXPECT_EQ("NO PLAYER SEATS", og::ui::format_seat_summary({}));
    EXPECT_EQ("CO-OP", og::ui::format_seat_summary(
                           {make_seat_player(0, 2, "A", false),
                            make_seat_player(1, 2, "B", false)}));
    EXPECT_EQ("2 VS 2", og::ui::format_seat_summary(
                            {make_seat_player(0, 0, "A", false),
                             make_seat_player(1, 0, "B", false),
                             make_seat_player(2, 1, "C", false),
                             make_seat_player(3, 1, "D", false)}));
    EXPECT_EQ("FREE-FOR-ALL", og::ui::format_seat_summary(
                                  {make_seat_player(0, 0, "A", false),
                                   make_seat_player(1, 1, "B", false),
                                   make_seat_player(2, 2, "C", false)}));
    EXPECT_EQ("MIXED TEAMS", og::ui::format_seat_summary(
                                 {make_seat_player(0, 0, "A", false),
                                  make_seat_player(1, 0, "B", false),
                                  make_seat_player(2, 1, "C", false)}));
}

// The shared local/solo seat synthesis (the deduplicated empty-lobby
// fallback and the text/curses seat source): one seat per numplayers, teams
// from derive_local_gameplay_seat_teams, company = save_name, P1 host.
TEST(PickerCommon, synthesize_local_lobby_players_pins)
{
    SaveData save;
    save.save_name = "Iron Kettle Band";
    save.numplayers = 2;
    save.allied_mode = 0;
    save.my_team = 0;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    save.team_list[1]->teamnum = 1;
    save.team_list[1]->deployed = true;
    save.team_size = 2;

    const std::vector<short> expected_teams =
        og::ui::derive_local_gameplay_seat_teams(save);
    ASSERT_EQ(2u, expected_teams.size());

    const std::vector<og::sim::LobbyPlayer> players =
        og::ui::synthesize_local_lobby_players(save);
    ASSERT_EQ(2u, players.size());
    for (std::size_t i = 0; i < players.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::uint8_t>(i), players[i].player_index);
        EXPECT_EQ(std::format("Player {}", i + 1), players[i].name);
        EXPECT_EQ("Iron Kettle Band", players[i].company)
            << "the synthesized company is the save's display name";
        EXPECT_EQ(expected_teams[i], players[i].team)
            << "seat teams come from derive_local_gameplay_seat_teams";
        EXPECT_FALSE(players[i].ready);
        EXPECT_EQ(i == 0, players[i].is_host) << "P1 is the host seat";
    }

    save.numplayers = 0;
    EXPECT_TRUE(og::ui::synthesize_local_lobby_players(save).empty())
        << "spectator/autoplay saves synthesize no seats";
}

// The builder resolves a seat context into the report and the formatter
// leads the non-versus report with it: "SEATS: {summary}" then one P#-sorted
// line per seat, then the existing blank + roster block. Exact positions —
// this is the seat block's shape contract.
TEST(PickerCommon, scenario_report_seat_block_leads_non_versus_reports)
{
    ReportWorld fx(false);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);

    SaveData save;
    save.my_team = 0;

    og::ui::ScenarioSeatContext seats;
    // Deliberately unsorted: the report is P#-sorted regardless of the
    // lobby list's arrival order.
    seats.players = {
        make_seat_player(3, 1, "Blue Banners", true),
        make_seat_player(0, 0, "Iron Kettle", false),
        make_seat_player(2, 1, "Blue Banners", false),
        make_seat_player(1, 0, "Keepers Rest", true),
    };
    seats.local_player_indices = {0};

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            nullptr, og::ui::StagePreviewStatus::None, save, &fx.world(),
            &seats);
    ASSERT_EQ(4u, report.seats.size());
    EXPECT_EQ("2 VS 2", report.seat_summary);
    EXPECT_EQ(0, report.seats[0].player_index);
    EXPECT_EQ(3, report.seats[3].player_index) << "P#-sorted";
    EXPECT_TRUE(report.seats[0].is_local);
    EXPECT_FALSE(report.seats[1].is_local);

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    ASSERT_GE(lines.size(), 7u);
    EXPECT_EQ("SEATS: 2 VS 2", lines[0]);
    EXPECT_EQ("  P1 YOU - RED TEAM", lines[1]);
    EXPECT_EQ("  P2 KEE [RDY] - RED TEAM", lines[2]);
    EXPECT_EQ("  P3 BLU - GREEN TEAM", lines[3]);
    EXPECT_EQ("  P4 BLU [RDY] - GREEN TEAM", lines[4]);
    EXPECT_EQ("", lines[5])
        << "the roster block keeps its blank separation below the seats";
    EXPECT_EQ("RED TEAM (YOURS)", lines[6]);
    for (const auto& line : lines)
        EXPECT_LE(line.size(), 48u) << line;
}

// Production host and joiner pass the SAME replicated seat records with
// DIFFERENT local_player_indices: the YOU-vs-abbreviation asymmetry is
// per-client presentation by design, never a replication divergence — only
// the seat FACTS (P#, team, ready, company) are shared state.
TEST(PickerCommon, seat_block_you_vs_abbreviation_is_per_client)
{
    ReportWorld host_world(false);
    ReportWorld joiner_world(false);

    SaveData save;
    const std::vector<og::sim::LobbyPlayer> shared_players = {
        make_seat_player(0, 0, "Iron Kettle", false),
        make_seat_player(1, 0, "Keepers Rest", true),
    };

    og::ui::ScenarioSeatContext host_seats;
    host_seats.players = shared_players;
    host_seats.local_player_indices = {0};
    og::ui::ScenarioSeatContext joiner_seats;
    joiner_seats.players = shared_players;
    joiner_seats.local_player_indices = {1};

    const std::vector<std::string> host_lines =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save,
                &host_world.world(), &host_seats));
    const std::vector<std::string> joiner_lines =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save,
                &joiner_world.world(), &joiner_seats));

    ASSERT_GE(host_lines.size(), 3u);
    ASSERT_GE(joiner_lines.size(), 3u);
    EXPECT_EQ("  P1 YOU - RED TEAM", host_lines[1]);
    EXPECT_EQ("  P2 KEE [RDY] - RED TEAM", host_lines[2]);
    EXPECT_EQ("  P1 IRO - RED TEAM", joiner_lines[1])
        << "the joiner sees the host's seat abbreviated, never YOU";
    EXPECT_EQ("  P2 YOU [RDY] - RED TEAM", joiner_lines[2]);
    EXPECT_EQ(host_lines[0], joiner_lines[0])
        << "the summary is a fact of the shared seats";
}

// No seat context (the defaulted parameter) and an EMPTY context are both
// byte-identical to the pre-seat report — the guard that every existing
// exact-line and exact-size pin keeps holding.
TEST(PickerCommon, scenario_report_without_seats_is_byte_identical)
{
    ReportWorld fx(false);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);
    fx.spawn_living_named(FAMILY_ARCHER, 1, 5, "GONZO");

    SaveData save;
    save.my_team = 0;

    const std::vector<std::string> defaulted =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save,
                &fx.world()));
    const og::ui::ScenarioSeatContext empty_seats;
    const std::vector<std::string> with_empty_context =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world(),
                &empty_seats));
    const std::vector<std::string> with_null_context =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world(),
                nullptr));
    ASSERT_FALSE(defaulted.empty());
    EXPECT_EQ(defaulted, with_empty_context);
    EXPECT_EQ(defaulted, with_null_context);
    for (const auto& line : defaulted)
        EXPECT_EQ(std::string::npos, line.find("SEATS:")) << line;
}

// The 48-char sweep at the seat block's widest shape: a full 16-seat global
// lobby on YELLOW, every seat ready and remote — the worst-case line is 29
// chars ("  P16 ABC [RDY] - YELLOW TEAM") and every line clears the budget.
TEST(PickerCommon, seat_block_sixteen_seat_worst_case_fits_the_budget)
{
    ReportWorld fx(false);
    fx.spawn_living_named(FAMILY_SOLDIER, 0, 3, nullptr);

    SaveData save;
    og::ui::ScenarioSeatContext seats;
    for (std::uint8_t index = 0; index < 16; ++index)
    {
        seats.players.push_back(make_seat_player(
            index, 3, "Absurdly Long Company Name Overflow", true));
    }

    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(
            og::ui::build_scenario_roster_report(
                nullptr, og::ui::StagePreviewStatus::None, save, &fx.world(),
                &seats));
    bool worst_case_seen = false;
    for (const auto& line : lines)
    {
        EXPECT_LE(line.size(), 48u) << line;
        worst_case_seen =
            worst_case_seen || line == "  P16 ABS [RDY] - YELLOW TEAM";
    }
    EXPECT_TRUE(worst_case_seen)
        << "the two-digit P# + [RDY] + YELLOW TEAM worst case renders";
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
    ASSERT_EQ(3, (int)save.respawn_mode)
        << "Everyone -> Team 1 Heroes Only";
    og::ui::cycle_respawn_mode(save);
    ASSERT_EQ(0, (int)save.respawn_mode)
        << "Team 1 Heroes Only wraps back to Off";
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
    save.respawn_mode = 3;
    ASSERT_EQ("Respawns: Team 1 Heroes",
              og::ui::format_respawn_mode_label(save));

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

// --- Infinite gold (session-only free purchases) ---

TEST(PickerCommon, toggle_infinite_gold_round_trips)
{
    SaveData save;
    EXPECT_EQ(0, static_cast<int>(save.infinite_gold))
        << "the classic economy is the default";
    EXPECT_FALSE(og::ui::gold_is_infinite(save));

    og::ui::toggle_infinite_gold(save);
    EXPECT_EQ(1, static_cast<int>(save.infinite_gold));
    EXPECT_TRUE(og::ui::gold_is_infinite(save));

    og::ui::toggle_infinite_gold(save);
    EXPECT_EQ(0, static_cast<int>(save.infinite_gold));
    EXPECT_FALSE(og::ui::gold_is_infinite(save));

    // Any out-of-set stored value counts as ON and normalizes to OFF.
    save.infinite_gold = 7;
    EXPECT_TRUE(og::ui::gold_is_infinite(save));
    og::ui::toggle_infinite_gold(save);
    EXPECT_EQ(0, static_cast<int>(save.infinite_gold));
}

TEST(PickerCommon, format_infinite_gold_label)
{
    SaveData save;
    ASSERT_EQ("Infinite Gold: Off", og::ui::format_infinite_gold_label(save));
    save.infinite_gold = 1;
    ASSERT_EQ("Infinite Gold: On", og::ui::format_infinite_gold_label(save));

    EXPECT_LE(og::ui::format_infinite_gold_label(save).size(), 23u);
    save.infinite_gold = 0;
    EXPECT_LE(og::ui::format_infinite_gold_label(save).size(), 23u);
}

TEST(PickerCommon, can_afford_and_format_wallet_amount)
{
    SaveData save;
    save.m_totalcash[0] = 100;
    save.m_totalcash[2] = 0;

    EXPECT_TRUE(og::ui::can_afford(save, 0, 100u));
    EXPECT_FALSE(og::ui::can_afford(save, 0, 101u));
    EXPECT_FALSE(og::ui::can_afford(save, 2, 1u));
    EXPECT_EQ("100", og::ui::format_wallet_amount(save, 0));
    EXPECT_EQ("0", og::ui::format_wallet_amount(save, 2));

    // Out-of-range teams clamp instead of reading past m_totalcash[4].
    EXPECT_EQ(og::ui::format_wallet_amount(save, 0),
              og::ui::format_wallet_amount(save, -3));
    EXPECT_EQ(og::ui::format_wallet_amount(save, MAX_PLAYERS - 1),
              og::ui::format_wallet_amount(save, 99));
    EXPECT_TRUE(og::ui::can_afford(save, -3, 100u));

    save.infinite_gold = 1;
    EXPECT_TRUE(og::ui::can_afford(save, 2, 4000000000u));
    EXPECT_EQ("INF", og::ui::format_wallet_amount(save, 0));
    EXPECT_EQ("INF", og::ui::format_wallet_amount(save, 99));
}

TEST(PickerCommon, base_camp_gold_label_reads_inf_when_infinite)
{
    SaveData save;
    save.m_totalcash[0] = 12345;
    save.infinite_gold = 1;
    EXPECT_EQ("GOLD INF", og::ui::format_base_camp_gold_label(save));
    EXPECT_LE(og::ui::format_base_camp_gold_label(save).size(), 11u);
}

TEST(PickerCommon, hire_with_infinite_gold_is_free_and_never_writes_the_wallet)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 0; // broke
    save.infinite_gold = 1;

    og::ui::HireSession session(save, 0);
    ASSERT_GT(session.current_cost(), 0u);

    const int slot = session.hire();
    EXPECT_EQ(0, slot) << "an unaffordable recruit is hired for free";
    EXPECT_EQ(1, save.team_size);
    ASSERT_NE(nullptr, save.team_list[0]);
    EXPECT_EQ(0u, save.m_totalcash[0])
        << "the wallet is never written, so no autosave can bake in a cheat "
           "balance";

    // The team-full guard still applies with infinite gold on.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (!save.team_list[static_cast<std::size_t>(i)]) {
            save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_size++;
        }
    }
    EXPECT_TRUE(session.team_full());
    EXPECT_EQ(-1, session.hire());
}

TEST(PickerCommon, hire_without_infinite_gold_still_rejects_and_charges)
{
    init_family_registry();
    SaveData save;
    save.team_size = 0;
    save.m_totalcash[0] = 0;
    save.infinite_gold = 0;

    og::ui::HireSession broke(save, 0);
    EXPECT_EQ(-1, broke.hire());
    EXPECT_EQ(0, save.team_size);
    EXPECT_EQ(nullptr, save.team_list[0]);

    save.m_totalcash[0] = 50000;
    og::ui::HireSession rich(save, 0);
    const std::uint32_t cost = rich.current_cost();
    ASSERT_GT(cost, 0u);
    EXPECT_EQ(0, rich.hire());
    EXPECT_EQ(50000u - cost, save.m_totalcash[0]);
}

TEST(PickerCommon, train_accept_with_infinite_gold_is_free)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 0;

    og::ui::TrainSession session(save);
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 5);
    ASSERT_GT(session.current_cost(), 0u);

    // Broke and classic: the raise is rejected outright.
    EXPECT_FALSE(session.accept(false));
    EXPECT_EQ(0u, save.m_totalcash[0]);

    save.infinite_gold = 1;
    og::ui::TrainSession free_session(save);
    const short strength_before = save.team_list[0]->strength;
    free_session.increase_stat(og::ui::TrainSession::Stat::Strength, 5);
    ASSERT_GT(free_session.current_cost(), 0u);
    EXPECT_TRUE(free_session.accept(false));
    EXPECT_GT(save.team_list[0]->strength, strength_before);
    EXPECT_EQ(0u, save.m_totalcash[0])
        << "training on the house never touches the wallet";
}

TEST(PickerCommon, selling_still_credits_the_wallet_with_infinite_gold)
{
    init_family_registry();
    SaveData save;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    save.m_totalcash[0] = 100;
    save.infinite_gold = 1;

    og::ui::TrainSession session(save);
    const std::uint32_t payout = session.current_sell_value();
    ASSERT_GT(payout, 0u);
    EXPECT_EQ(og::ui::TrainSession::SellResult::Sold,
              session.sell_current([] { return true; }));
    EXPECT_EQ(100u + payout, save.m_totalcash[0])
        << "infinite gold makes purchases free; it does not disable income";
}

TEST(PickerCommon, difficulty_submenu_labels_fit_140px_rows)
{
    // The SDL DIFFICULTY subscreen draws 140px-wide single-column rows at
    // 6px/char = 23-character budget; labels are centered with no clipping,
    // so every variant of every row label must fit.
    SaveData save;
    std::vector<std::string> labels;
    for (short mode : {short(0), short(1), short(2), short(3)})
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
    for (short gold : {short(0), short(1)})
    {
        save.infinite_gold = gold;
        labels.push_back(og::ui::format_infinite_gold_label(save));
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

// --- GAME SETTINGS speed selector (cfg gameplay/timer_wait) --------------

TEST(PickerCommon, game_speed_maps_the_stored_wait_to_the_classic_number)
{
    // The number the retired in-game options menu showed: (20 - wait)/2 + 1.
    ASSERT_EQ(8, og::ui::game_speed_from_timer_wait(6));
    ASSERT_EQ(1, og::ui::game_speed_from_timer_wait(20));
    ASSERT_EQ(11, og::ui::game_speed_from_timer_wait(0));
    // Out-of-range waits clamp instead of producing off-scale numbers.
    ASSERT_EQ(11, og::ui::game_speed_from_timer_wait(-4));
    ASSERT_EQ(1, og::ui::game_speed_from_timer_wait(99));

    ASSERT_EQ(6, og::ui::timer_wait_from_game_speed(8));
    ASSERT_EQ(20, og::ui::timer_wait_from_game_speed(1));
    ASSERT_EQ(0, og::ui::timer_wait_from_game_speed(11));
    ASSERT_EQ(20, og::ui::timer_wait_from_game_speed(0));
    ASSERT_EQ(0, og::ui::timer_wait_from_game_speed(50));
}

TEST(PickerCommon, parse_timer_wait_falls_back_to_the_shipped_default)
{
    ASSERT_EQ(6, og::ui::parse_timer_wait("6"));
    ASSERT_EQ(0, og::ui::parse_timer_wait("0"));
    ASSERT_EQ(20, og::ui::parse_timer_wait("20"));
    // An absent key, a hand-edited word, or a partially numeric value all
    // read as the shipped default rather than 0 (the FASTEST speed).
    ASSERT_EQ(6, og::ui::parse_timer_wait(""));
    ASSERT_EQ(6, og::ui::parse_timer_wait("fast"));
    ASSERT_EQ(6, og::ui::parse_timer_wait("6x"));
    // Out of range clamps to the wire-legal 0..20 the server enforces.
    ASSERT_EQ(20, og::ui::parse_timer_wait("400"));
    ASSERT_EQ(0, og::ui::parse_timer_wait("-3"));
}

TEST(PickerCommon, cycle_game_speed_laps_in_eleven_clicks)
{
    std::string wait = "6";
    std::vector<int> seen;
    for (int step = 0; step < 11; ++step)
    {
        wait = og::ui::cycle_game_speed(wait);
        seen.push_back(og::ui::game_speed_from_timer_wait(
            og::ui::parse_timer_wait(wait)));
    }
    ASSERT_EQ("6", wait) << "eleven clicks must return to the default wait";
    const std::vector<int> expected{9, 10, 11, 1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(expected, seen) << "one speed step per click, wrapping 11 -> 1";

    // A hand-edited odd wait normalizes onto the even lap at the first click.
    ASSERT_EQ("4", og::ui::cycle_game_speed("5"));
}

TEST(PickerCommon, format_game_speed_label_exact_strings)
{
    ASSERT_EQ("SPEED: 8", og::ui::format_game_speed_label("6"));
    ASSERT_EQ("SPEED: 11", og::ui::format_game_speed_label("0"));
    ASSERT_EQ("SPEED: 1", og::ui::format_game_speed_label("20"));
    ASSERT_EQ("SPEED: 8", og::ui::format_game_speed_label(""));
    // The 90px face is a 15-character budget; the widest is 9.
    std::string wait = "6";
    for (int step = 0; step < 11; ++step)
    {
        EXPECT_LE(og::ui::format_game_speed_label(wait).size(), 15u) << wait;
        wait = og::ui::cycle_game_speed(wait);
    }
}

// --- DISPLAY brightness (cfg graphics/brightness) ------------------------

TEST(PickerCommon, brightness_steps_parse_and_clamp)
{
    ASSERT_EQ(0, og::ui::parse_brightness_steps("0"));
    ASSERT_EQ(3, og::ui::parse_brightness_steps("3"));
    ASSERT_EQ(-2, og::ui::parse_brightness_steps("-2"));
    ASSERT_EQ(0, og::ui::parse_brightness_steps(""));
    ASSERT_EQ(0, og::ui::parse_brightness_steps("bright"));
    // Past the saturation range a stored value clamps, so the row can never
    // present a value the palette transform cannot show.
    ASSERT_EQ(og::ui::kBrightnessStepMax, og::ui::parse_brightness_steps("40"));
    ASSERT_EQ(og::ui::kBrightnessStepMin, og::ui::parse_brightness_steps("-40"));
}

TEST(PickerCommon, adjust_brightness_steps_moves_one_step_and_stops_at_the_ends)
{
    ASSERT_EQ("1", og::ui::adjust_brightness_steps("0", 1));
    ASSERT_EQ("-1", og::ui::adjust_brightness_steps("0", -1));
    // The old change_gamma took +-2 because its guards were > 1 / < -1; the
    // pair passes a plain sign and every click moves exactly one step.
    ASSERT_EQ("1", og::ui::adjust_brightness_steps("0", 2));
    ASSERT_EQ("-1", og::ui::adjust_brightness_steps("0", -2));
    ASSERT_EQ("0", og::ui::adjust_brightness_steps("0", 0));

    ASSERT_EQ(std::to_string(og::ui::kBrightnessStepMax),
              og::ui::adjust_brightness_steps(
                  std::to_string(og::ui::kBrightnessStepMax), 1));
    ASSERT_EQ(std::to_string(og::ui::kBrightnessStepMin),
              og::ui::adjust_brightness_steps(
                  std::to_string(og::ui::kBrightnessStepMin), -1));

    // A full sweep from one end to the other takes exactly the range.
    std::string value = std::to_string(og::ui::kBrightnessStepMin);
    for (int step = og::ui::kBrightnessStepMin; step < og::ui::kBrightnessStepMax;
         ++step)
        value = og::ui::adjust_brightness_steps(value, 1);
    ASSERT_EQ(std::to_string(og::ui::kBrightnessStepMax), value);
}

TEST(PickerCommon, format_brightness_label_signs_the_offset)
{
    ASSERT_EQ("Brightness: 0", og::ui::format_brightness_label(0));
    ASSERT_EQ("Brightness: +2", og::ui::format_brightness_label(2));
    ASSERT_EQ("Brightness: -3", og::ui::format_brightness_label(-3));
    // The text starts at x=200 on a 320px canvas: 20 characters of room.
    for (int step = og::ui::kBrightnessStepMin;
         step <= og::ui::kBrightnessStepMax; ++step)
        EXPECT_LE(og::ui::format_brightness_label(step).size(), 20u) << step;
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
    info.header.campaign_id = "never-mounted";
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
    info.header.campaign_id = "never-mounted";
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

TEST(BaseCampRoster, move_up_swaps_with_previous_occupied_slot)
{
    SaveData save;
    save.team_list[1] = make_base_camp_member("FIRST", FAMILY_SOLDIER);
    save.team_list[4] = make_base_camp_member("SECOND", FAMILY_ARCHER);
    save.team_list[7] = make_base_camp_member("THIRD", FAMILY_MAGE);
    save.team_size = 3;

    EXPECT_EQ(-1, og::ui::move_team_member_up(save, -1));
    EXPECT_EQ(-1, og::ui::move_team_member_up(save, 0));
    EXPECT_EQ(-1, og::ui::move_team_member_up(save, 2))
        << "an empty slot is never a reorder target";
    EXPECT_EQ(-1, og::ui::move_team_member_up(save, 1))
        << "the first occupied slot cannot move farther up";

    EXPECT_EQ(4, og::ui::move_team_member_up(save, 7))
        << "sparse holes are skipped when finding the predecessor";
    ASSERT_NE(nullptr, save.team_list[4]);
    ASSERT_NE(nullptr, save.team_list[7]);
    EXPECT_EQ("THIRD", save.team_list[4]->name);
    EXPECT_EQ("SECOND", save.team_list[7]->name);
    EXPECT_EQ(1, og::ui::move_team_member_up(save, 4));
    EXPECT_EQ("THIRD", save.team_list[1]->name);
    EXPECT_EQ("FIRST", save.team_list[4]->name);
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

TEST(BaseCampRoster, family_ramp_starts_match_master_view_team)
{
    // Master's View Team applied this exact palette-ramp formula to both the
    // character name and its family label. Base Camp now carries the same
    // ramps in compact swatches; pin every playable family.
    for (short family = FAMILY_SOLDIER; family <= FAMILY_TOWER1; ++family) {
        const unsigned char expected =
            static_cast<unsigned char>(((family + 1) << 4) & 255);
        EXPECT_EQ(expected, og::ui::base_camp_family_ramp_start(family))
            << "family " << family;
    }

    EXPECT_EQ(16, og::ui::base_camp_family_ramp_start(FAMILY_SOLDIER));
    EXPECT_EQ(32, og::ui::base_camp_family_ramp_start(FAMILY_ELF));
    EXPECT_EQ(48, og::ui::base_camp_family_ramp_start(FAMILY_ARCHER));
    EXPECT_EQ(64, og::ui::base_camp_family_ramp_start(FAMILY_MAGE));
}

TEST(BaseCampRoster, header_lines_budget_and_content)
{
    SaveData save;
    save.m_totalcash[0] = 12345;
    EXPECT_EQ("GOLD 12345", og::ui::format_base_camp_gold_label(save));
    save.m_totalcash[0] = 4000000000u;
    EXPECT_LE(og::ui::format_base_camp_gold_label(save).size(), 11u)
        << "the gold block clips to the 11-char right column";

    // An empty roster falls back to the player's seat wallet.
    save.my_team = 2;
    save.m_totalcash[2] = 777;
    EXPECT_EQ("GOLD 777", og::ui::format_base_camp_gold_label(save));

    // Network Together seats can be red while the private company is yellow;
    // the label follows the banked company wallet, not the remapped seat.
    save.team_list[0] = make_base_camp_member("Yellow", FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.my_team = 0;
    save.m_totalcash[1] = 888;
    EXPECT_EQ("GOLD 888", og::ui::format_base_camp_gold_label(save));

    save.team_list[0] = make_base_camp_member("A", FAMILY_SOLDIER);
    save.team_list[1] = make_base_camp_member("B", FAMILY_MAGE);
    save.team_size = 2;
    save.team_list[1]->deployed = false;
    save.scen_num = 7;

    const std::string line =
        og::ui::format_base_camp_scen_line(save, "THE FORTRESS");
    EXPECT_EQ("SCEN 7: THE FORTRESS  DEP 1/2", line);
    // The solo header shares line B with the roster header band's HIRE
    // command, so it takes the conservative (HIRE-visible) budget — the
    // draw site clips nothing, every source composes to its band.
    EXPECT_LE(line.size(),
              static_cast<std::size_t>(
                  og::ui::kBaseCampLineBCharsHireVisible));

    // An over-long title clips so the DEP block always fits the budget.
    const std::string clipped = og::ui::format_base_camp_scen_line(
        save, "AN ABSURDLY LONG SCENARIO TITLE THAT CANNOT FIT");
    EXPECT_LE(clipped.size(),
              static_cast<std::size_t>(
                  og::ui::kBaseCampLineBCharsHireVisible));
    EXPECT_NE(std::string::npos, clipped.find("  DEP 1/2"))
        << "the DEP block survives the clip: " << clipped;
    // A cut title SAYS it was cut. Before the marker, "THE RASPBERRY ISLE"
    // rendered "SCEN 7: THE RASPBERRY IS" — a corrupted-looking title, not
    // a shortened one, on the only line of story the screen carries.
    EXPECT_EQ("SCEN 7: THE RASPBERRY..  DEP 1/2",
              og::ui::format_base_camp_scen_line(save,
                                                 "THE RASPBERRY ISLE"));
    // A word that nearly fits is not thrown away for the marker's sake:
    // below two thirds of the room the cut goes mid-word and the marker
    // carries the honesty.
    EXPECT_EQ("SCEN 7: SOUTH OF TALWOO..  DEP 1/2",
              og::ui::format_base_camp_scen_line(
                  save, "SOUTH OF TALWOOD FOREST"));
}

TEST(BaseCampRoster, clip_with_ellipsis_marks_every_cut)
{
    // Fits: untouched, no marker.
    EXPECT_EQ("THE CIRCLE", og::ui::clip_with_ellipsis("THE CIRCLE", 10));
    // Whole-word cut while two thirds of the room survives.
    EXPECT_EQ("THE RASPBERRY..",
              og::ui::clip_with_ellipsis("THE RASPBERRY ISLE", 17));
    // Mid-word cut below that threshold (the whole-word cut would drop a
    // word that nearly fit).
    EXPECT_EQ("SOUTH OF TALWO..",
              og::ui::clip_with_ellipsis("SOUTH OF TALWOOD FOREST", 16));
    // No word boundary at all: still marked.
    EXPECT_EQ("ABCD..", og::ui::clip_with_ellipsis("ABCDEFGHIJ", 6));
    // Budgets with no room for the marker clip hard rather than lie.
    EXPECT_EQ("AB", og::ui::clip_with_ellipsis("ABCDEF", 2));
    EXPECT_EQ("", og::ui::clip_with_ellipsis("ABCDEF", 0));
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
        EXPECT_EQ("BENCHED", save.team_list[static_cast<std::size_t>(before[0])]->name);
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
    EXPECT_EQ("FIGHTER", save.team_list[static_cast<std::size_t>(after[0])]->name);
    EXPECT_TRUE(save.team_list[static_cast<std::size_t>(after[0])]->deployed);
    EXPECT_EQ("BENCHED", save.team_list[static_cast<std::size_t>(after[1])]->name);
    EXPECT_FALSE(save.team_list[static_cast<std::size_t>(after[1])]->deployed)
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
                                     int benched_slots,
                                     og::sim::LobbyMachineId machine_id =
                                         og::sim::kInvalidLobbyMachineId)
{
    og::sim::LobbyPlayer player;
    player.player_index = player_index;
    player.machine_id = machine_id;
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
    // Host machine has 2 seats (seat 1 is is_host=false but must NOT count
    // as a gating machine — the §4.3 multi-seat-host rule). Machine 2
    // has 2 seats and counts ready only when BOTH are. Machine "net-c" is a
    // single ready seat; "net-d" is an unready spectator (0 slots).
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0, 1),
        make_lobby_seat(1, "net-host#1", "HOST CO", false, false, 1, 0, 1),
        make_lobby_seat(2, "net-b", "B CO", false, true, 1, 0, 2),
        make_lobby_seat(3, "net-b#1", "B CO", false, false, 1, 0, 2),
        make_lobby_seat(4, "net-c", "C CO", false, true, 1, 0, 3),
        make_lobby_seat(5, "net-d", "D CO", false, false, 0, 0, 4),
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

TEST(BaseCampMpDisplay,
     machine_ids_keep_exact_name_and_company_spoof_separate_from_host)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(
            0, "same-name", "SAME COMPANY", true, false, 1, 0, 41),
        make_lobby_seat(
            1, "same-name", "SAME COMPANY", false, false, 1, 0, 42),
    };

    const og::ui::BaseCampReadyCounts ready =
        og::ui::count_base_camp_ready_machines(players);
    EXPECT_EQ(1, ready.machines);
    EXPECT_EQ(0, ready.ready);
    EXPECT_EQ(2, og::ui::count_base_camp_session_census(players).machines);
    EXPECT_EQ("SAME COMPANY", og::ui::format_go_blockers(players))
        << "the foreign machine must not disappear into the host grouping";
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
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0, 1),
        make_lobby_seat(1, "net-host#1", "HOST CO", false, false, 1, 0, 1),
        make_lobby_seat(2, "net-b", "B CO", false, true, 1, 0, 2),
        make_lobby_seat(3, "net-b#1", "B CO", false, false, 1, 0, 2),
        make_lobby_seat(4, "net-c", "C CO", false, true, 1, 0, 3),
    };
    const og::ui::BaseCampSessionCensus census =
        og::ui::count_base_camp_session_census(players);
    EXPECT_EQ(3, census.machines);
    EXPECT_EQ(5, census.players);
}

TEST(BaseCampMpDisplay, session_status_shapes_hold_the_line_b_budget)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(
            0, "net-h", "IRON KETTLE BAND", true, false, 2, 0, 1),
        make_lobby_seat(
            1, "net-j", "JOIN RIVER BAND", false, false, 1, 0, 2),
        make_lobby_seat(
            2, "net-j#1", "JOIN RIVER BAND", false, false, 1, 0, 2),
    };

    // The band is 34 characters wide beside HIRE and 41 without it, both
    // DERIVED from the wall each control stands on: ink starts at x=10 and
    // the readability strip pads 2px, so N characters must satisfy
    // 10 + 6N + 2 <= wall.
    constexpr int kWide = og::ui::kBaseCampLineBCharsHireHidden;
    constexpr int kNarrow = og::ui::kBaseCampLineBCharsHireVisible;
    EXPECT_EQ(34, kNarrow);
    EXPECT_EQ(41, kWide);
    const auto ink_right_edge = [](std::size_t chars) {
        return og::ui::kBaseCampLineBTextX +
            og::ui::kBaseCampLineBGlyphAdvance * static_cast<int>(chars) +
            og::ui::kBaseCampLineBStripPad;
    };
    EXPECT_LE(ink_right_edge(kNarrow), og::ui::kBaseCampLineBHireWallX);
    EXPECT_GT(ink_right_edge(kNarrow + 1), og::ui::kBaseCampLineBHireWallX)
        << "the narrow budget must be the WIDEST that clears HIRE";
    EXPECT_LE(ink_right_edge(kWide), og::ui::kBaseCampLineBPagerWallX);
    EXPECT_GT(ink_right_edge(kWide + 1), og::ui::kBaseCampLineBPagerWallX)
        << "the wide budget must be the WIDEST that clears the pagers";

    // The rail shows THIS machine's seats only, so line B is where the size
    // of the lobby lives — players first, machines second. Every rung below
    // is a whole spelling; the ladder falls between them and never cuts one.
    //
    // WIDE band, everyday counts: the spelled-out rung lands on exactly 41.
    EXPECT_EQ("HOSTING GLAD-7Q2F: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(true, "GLAD-7Q2F",
                                                      players, kWide));
    EXPECT_EQ(static_cast<std::size_t>(kWide),
              og::ui::format_base_camp_session_status(true, "GLAD-7Q2F",
                                                      players, kWide).size())
        << "the widest rung is sized to the widest band";
    // Relay-less (LAN) host: no room code to carry.
    EXPECT_EQ("HOSTING: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(true, "", players,
                                                      kWide));

    // The joiner's line carries the same census. "HOST: <company>" is gone:
    // the host's company is on every one of its roster rows, and the count
    // of everyone in the lobby is on none of them.
    EXPECT_EQ("IN GLAD-7Q2F: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      players, kWide));
    // Direct (LAN) joiner: no room code.
    EXPECT_EQ("JOINED: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(false, "", players,
                                                      kWide));
    // Pre-first-state: an empty roster still answers honestly.
    EXPECT_EQ("IN GLAD-7Q2F: 0 PLAYERS / 0 MACHINES",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      {}, kWide));
    EXPECT_EQ("JOINED: 0 PLAYERS / 0 MACHINES",
              og::ui::format_base_camp_session_status(false, "", {}, kWide));

    // The NARROW band (the shape every shipped campaign renders today — the
    // default composition shows HIRE): "MACHINES" costs six characters more
    // than the room code is worth, so the second rung says PCS and lands on
    // exactly 34.
    EXPECT_EQ("HOSTING GLAD-7Q2F: 3 PLAYERS/2 PCS",
              og::ui::format_base_camp_session_status(true, "GLAD-7Q2F",
                                                      players, kNarrow));
    EXPECT_EQ(static_cast<std::size_t>(kNarrow),
              og::ui::format_base_camp_session_status(
                  true, "GLAD-7Q2F", players, kNarrow).size())
        << "the everyday rung is sized to the everyday band";
    EXPECT_EQ("HOSTING: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(true, "", players,
                                                      kNarrow))
        << "a shape that already fits keeps the spelled-out census";
    EXPECT_EQ("IN GLAD-7Q2F: 3 PLAYERS/2 PCS",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      players, kNarrow));
    EXPECT_EQ("JOINED: 3 PLAYERS / 2 MACHINES",
              og::ui::format_base_camp_session_status(false, "", players,
                                                      kNarrow));

    // Budget pins: the absolute worst shapes fit BOTH bands — the 16-seat
    // global cap on 16 machines, and a pathological room code that display-
    // clips at 12.
    std::vector<og::sim::LobbyPlayer> sixteen;
    for (int i = 0; i < 16; ++i) {
        sixteen.push_back(make_lobby_seat(
            static_cast<std::uint8_t>(i),
            std::format("net-{:02}", i).c_str(), "C", i == 0, false, 1, 0,
            static_cast<og::sim::LobbyMachineId>(i + 1)));
    }
    for (const int budget : {kNarrow, kWide}) {
        EXPECT_LE(og::ui::format_base_camp_session_status(
                      true, "GLAD-XXXX", sixteen, budget).size(),
                  static_cast<std::size_t>(budget))
            << "16-seat host at budget " << budget;
        EXPECT_LE(og::ui::format_base_camp_session_status(
                      false, "GLAD-XXXX", sixteen, budget).size(),
                  static_cast<std::size_t>(budget))
            << "16-seat joiner at budget " << budget;
        EXPECT_LE(og::ui::format_base_camp_session_status(
                      true, "GLAD-TOO-LONG-CODE", sixteen, budget).size(),
                  static_cast<std::size_t>(budget))
            << "pathological room code at budget " << budget;
    }
    // Two-digit counts push the spelled-out rung past even the wide band, so
    // the ladder drops a whole rung at a time.
    EXPECT_EQ("HOSTING GLAD-XXXX: 16 PLAYERS/16 PCS",
              og::ui::format_base_camp_session_status(true, "GLAD-XXXX",
                                                      sixteen, kWide));
    EXPECT_EQ("HOSTING GLAD-XXXX: 16P/16M",
              og::ui::format_base_camp_session_status(true, "GLAD-XXXX",
                                                      sixteen, kNarrow));
    // Room 12 + two-digit counts is the joiner's exact-fit worst case at 34.
    EXPECT_EQ("IN GLAD-TOO-LON: 16 PLAYERS/16 PCS",
              og::ui::format_base_camp_session_status(
                  false, "GLAD-TOO-LONG-CODE", sixteen, kNarrow));
}

// The composer's budget is a parameter, not a constant: narrower callers
// (and any future band) drop a whole rung, then the room code, rather than
// emitting a byte-cut line.
TEST(BaseCampMpDisplay, session_status_degrades_by_shape_not_by_byte_cut)
{
    std::vector<og::sim::LobbyPlayer> sixteen;
    for (int i = 0; i < 16; ++i) {
        sixteen.push_back(make_lobby_seat(
            static_cast<std::uint8_t>(i),
            std::format("net-{:02}", i).c_str(), "C", i == 0, false, 1, 0,
            static_cast<og::sim::LobbyMachineId>(i + 1)));
    }
    // Too narrow even for the compact census beside a room code: the room
    // half goes (the NETWORKING screen keeps the authoritative code).
    EXPECT_EQ("HOSTING: 16P/16M",
              og::ui::format_base_camp_session_status(true, "GLAD-7Q2F",
                                                      sixteen, 20));
    EXPECT_EQ("JOINED: 16P/16M",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      sixteen, 20));

    const std::vector<og::sim::LobbyPlayer> kettle = {
        make_lobby_seat(0, "net-h", "IRON KETTLE BAND", true, false, 1, 0,
                        1)};
    // Each rung is taken whole while it fits and abandoned whole when it
    // does not — never a prefix of the rung above. ONE IS ONE: a lobby of one
    // says PLAYER / MACHINE / PC, never the "1 PLAYERS" that told the reader
    // the line could not count. The singular is always the shorter spelling,
    // so it can never push a rung off a band it used to fit — here it makes
    // the widest rung 34 characters instead of 38.
    EXPECT_EQ("IN GLAD-7Q2F: 1 PLAYER / 1 MACHINE",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 36));
    EXPECT_EQ(std::size_t{34},
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 34).size());
    EXPECT_EQ("IN GLAD-7Q2F: 1 PLAYER / 1 MACHINE",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 34))
        << "the singular rung fits a band the plural one would have missed";
    EXPECT_EQ("IN GLAD-7Q2F: 1 PLAYER/1 PC",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 33));
    EXPECT_EQ("IN GLAD-7Q2F: 1P/1M",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 26));
    // A machine with two seats in it: the players half pluralizes on its own
    // count, the machines half does not.
    const std::vector<og::sim::LobbyPlayer> two_on_one = {
        make_lobby_seat(0, "net-h", "IRON KETTLE BAND", true, false, 1, 0, 1),
        make_lobby_seat(1, "net-h2", "IRON KETTLE BAND", false, false, 1, 0,
                        1)};
    EXPECT_EQ("IN GLAD-7Q2F: 2 PLAYERS / 1 MACHINE",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      two_on_one, 40));
    EXPECT_EQ("IN GLAD-7Q2F: 2 PLAYERS/1 PC",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      two_on_one, 34));
    EXPECT_EQ("JOINED: 1P/1M",
              og::ui::format_base_camp_session_status(false, "GLAD-7Q2F",
                                                      kettle, 18));
    // The hard floor: every shape still honors the budget.
    for (int budget = 1; budget <= 45; ++budget) {
        EXPECT_LE(og::ui::format_base_camp_session_status(
                      true, "GLAD-7Q2F", sixteen, budget).size(),
                  static_cast<std::size_t>(budget))
            << "host at budget " << budget;
        EXPECT_LE(og::ui::format_base_camp_session_status(
                      false, "GLAD-7Q2F", kettle, budget).size(),
                  static_cast<std::size_t>(budget))
            << "joiner at budget " << budget;
    }
}

TEST(BaseCampMpDisplay, line_b_gives_the_alert_slot_and_color_precedence)
{
    const std::vector<og::sim::LobbyPlayer> players = {
        make_lobby_seat(
            0, "net-h", "IRON KETTLE BAND", true, false, 1, 0, 1)};

    // Healthy: the session status, plain color.
    const og::ui::BaseCampLineB healthy = og::ui::compose_base_camp_line_b(
        std::nullopt, true, "GLAD-7Q2F", players,
        og::ui::kBaseCampLineBCharsHireHidden);
    EXPECT_FALSE(healthy.alert);
    EXPECT_EQ("HOSTING GLAD-7Q2F: 1 PLAYER / 1 MACHINE", healthy.text);

    // Degraded: the alert takes the slot AND the color (§9.12 precedence —
    // the ORANGE mapping rides the alert flag).
    const og::ui::BaseCampLineB degraded = og::ui::compose_base_camp_line_b(
        std::optional<std::string>("Status: connection lost"), true,
        "GLAD-7Q2F", players, og::ui::kBaseCampLineBCharsHireHidden);
    EXPECT_TRUE(degraded.alert);
    EXPECT_EQ("Status: connection lost", degraded.text);

    // Alert prose is transport-authored (a pack-install failure carries the
    // installer's reason), so it takes the band budget like every other
    // line-B source — never ink under HIRE.
    const og::ui::BaseCampLineB verbose = og::ui::compose_base_camp_line_b(
        std::optional<std::string>(
            "Packs: install failed for a very long pack name"),
        false, "GLAD-7Q2F", players,
        og::ui::kBaseCampLineBCharsHireVisible);
    EXPECT_TRUE(verbose.alert);
    EXPECT_EQ(static_cast<std::size_t>(
                  og::ui::kBaseCampLineBCharsHireVisible),
              verbose.text.size());
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
    character.teamnum = 2;

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
    EXPECT_EQ(2, display->teamnum);
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
        save.team_list[static_cast<std::size_t>(i)] = make_base_camp_member("M", FAMILY_SOLDIER);
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

TEST(ReadyGoSlot, state_5_spectator_formatter_has_no_deploy_caption)
{
    // [NET-R9]: the pure formatter's spectator shape has no local deployment
    // warning. Base Camp separately hides the READY button when the machine
    // has no active seat.
    const og::ui::ReadyGoPresentation p =
        ready_go(true, false, false, false, 3, 0, false, true);
    EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, p.state);
    EXPECT_EQ("READY", p.label);
    EXPECT_EQ(og::ui::kReadyGoFaceUnready, p.face_color);
    EXPECT_TRUE(p.caption.empty()) << "spectator shape has no deploy warning";
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
        make_lobby_seat(0, "net-host", "HOST CO", true, false, 1, 0, 1),
        make_lobby_seat(
            1, "net-host#1", "HOST CO", false, false, 1, 0, 1),
        make_lobby_seat(2, "net-b", "BRAVO BAND", false, true, 1, 0, 2),
        make_lobby_seat(
            3, "net-b#1", "BRAVO BAND", false, false, 0, 1, 2),
        make_lobby_seat(
            4, "net-c", "CHARLIE BAND", false, true, 1, 0, 3),
        make_lobby_seat(5, "net-d", "", false, false, 0, 0, 4),
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
    players.push_back(make_lobby_seat(
        0, "net-host", "HOST CO", true, false, 1, 0, 1));
    for (int i = 0; i < 6; ++i) {
        players.push_back(make_lobby_seat(
            static_cast<std::uint8_t>(1 + i),
            std::format("net-m{}", i).c_str(),
            "A VERY LONG COMPANY NAME INDEED", false, false, 1, 0,
            static_cast<og::sim::LobbyMachineId>(i + 2)));
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

// --- Campaign browser (SET CAMPAIGN) layout pins ---
//
// The reported bug: long campaign titles ran under the ENTER ID face. The
// title row and the top control row share pixels (title y = 13 height 8,
// controls y = 10 height 10), and the centered title reached x = 208 — the
// old ENTER ID left edge — at 17 characters. Both shipped offenders
// ("MULTIPLAYER GAME MODES", 22; "CONCEPT PLAYGROUND", 18) cleared that.
// ENTER ID now stacks under DELETE/RESET and the title is fitted, so the
// pins below assert the property for EVERY title length, not just the ones
// that ship.

TEST(CampaignPickerLayout, enter_id_sits_below_the_delete_reset_cell)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();

    EXPECT_EQ(270, layout.delete_button.x);
    EXPECT_EQ(10, layout.delete_button.y);
    EXPECT_EQ(38, layout.delete_button.w);
    EXPECT_EQ(10, layout.delete_button.h);

    // RESET shares the DELETE cell (only one of the two is ever visible).
    EXPECT_EQ(layout.delete_button.x, layout.reset_button.x);
    EXPECT_EQ(layout.delete_button.y, layout.reset_button.y);

    EXPECT_EQ(256, layout.id_button.x);
    EXPECT_EQ(22, layout.id_button.y);
    EXPECT_EQ(52, layout.id_button.w);
    EXPECT_EQ(10, layout.id_button.h);

    // Strictly below, right edges flush.
    EXPECT_GE(layout.id_button.y,
              layout.delete_button.y + layout.delete_button.h);
    EXPECT_EQ(layout.delete_button.x + layout.delete_button.w,
              layout.id_button.x + layout.id_button.w);
    EXPECT_FALSE(og::ui::picker_rects_overlap(layout.id_button,
                                              layout.delete_button));
    // ...and still on screen.
    EXPECT_LE(layout.id_button.x + layout.id_button.w, 320);
}

TEST(CampaignPickerLayout, no_control_overlaps_another_control)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    std::vector<og::ui::PickerRect> distinct = {
        layout.prev, layout.next, layout.choose,
        layout.cancel, layout.delete_button, layout.id_button,
        layout.more_button, layout.desc_box, layout.icon};
    for (int row = 0; row < layout.list_rows; ++row)
        distinct.push_back(og::ui::campaign_picker_row_rect(row));
    for (std::size_t i = 0; i < distinct.size(); ++i)
    {
        for (std::size_t j = i + 1; j < distinct.size(); ++j)
        {
            EXPECT_FALSE(og::ui::picker_rects_overlap(distinct[i], distinct[j]))
                << "controls " << i << " and " << j << " overlap";
        }
    }
}

TEST(CampaignPickerLayout, list_pane_is_a_grid_beside_the_detail_pane)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();

    ASSERT_GT(layout.list_rows, 1);
    const og::ui::PickerRect row0 = og::ui::campaign_picker_row_rect(0);
    for (int row = 0; row < layout.list_rows; ++row)
    {
        const og::ui::PickerRect rect = og::ui::campaign_picker_row_rect(row);
        // Alignment as a RELATION: every row shares row 0's edges and pitch.
        EXPECT_EQ(row0.x, rect.x) << "row " << row << " breaks the left edge";
        EXPECT_EQ(row0.w, rect.w) << "row " << row << " breaks the width";
        EXPECT_EQ(row0.y + layout.list_row_pitch * row, rect.y)
            << "row " << row << " breaks the pitch";
        // ...and stays inside the declared list block and left of the
        // detail pane.
        EXPECT_GE(rect.x, layout.list.x);
        EXPECT_LE(rect.y + rect.h, layout.list.y + layout.list.h);
        EXPECT_LE(rect.x + rect.w, layout.detail.x)
            << "list rows must not reach the detail pane";
        EXPECT_FALSE(og::ui::picker_rects_overlap(rect, layout.detail));
    }

    // The pagers sit below the list, sharing its outer edges, with room for
    // the position readout between them.
    EXPECT_GE(layout.prev.y, layout.list.y + layout.list.h);
    EXPECT_EQ(layout.prev.y, layout.next.y) << "pagers share a band";
    EXPECT_EQ(layout.list.x, layout.prev.x);
    EXPECT_EQ(layout.list.x + layout.list.w, layout.next.x + layout.next.w);
    EXPECT_GE(layout.next.x - (layout.prev.x + layout.prev.w), 8 * 6)
        << "the 'NN of NN' readout needs 8 glyphs between the pagers";

    // The detail pane's stack: icon and description box inside the pane,
    // MORE below the box flush with the pane's right edge.
    EXPECT_FALSE(og::ui::picker_rects_overlap(layout.desc_box, layout.icon));
    EXPECT_GE(layout.icon.y, layout.detail.y);
    EXPECT_GE(layout.desc_box.x, layout.detail.x);
    EXPECT_LE(layout.desc_box.x + layout.desc_box.w,
              layout.detail.x + layout.detail.w);
    EXPECT_GE(layout.more_button.y, layout.desc_box.y + layout.desc_box.h);
    EXPECT_EQ(layout.detail.x + layout.detail.w,
              layout.more_button.x + layout.more_button.w);

    // Row labels: a 6px marker column then an 18-char label budget.
    EXPECT_EQ(18, layout.list_label_max_chars);
    EXPECT_EQ(4, layout.desc_rows);
    EXPECT_EQ(26, layout.desc_max_chars);

    // Everything stays on the 320x200 canvas.
    for (const og::ui::PickerRect& rect :
         {layout.list, layout.detail, layout.prev, layout.next,
          layout.more_button, layout.desc_box})
    {
        EXPECT_GE(rect.x, 0);
        EXPECT_GE(rect.y, 0);
        EXPECT_LE(rect.x + rect.w, 320);
        EXPECT_LE(rect.y + rect.h, 200);
    }
}

TEST(CampaignPickerLayout, list_paging_helpers_window_the_shelf)
{
    using og::ui::campaign_list_clamp_cursor;
    using og::ui::campaign_list_clamp_offset;
    using og::ui::campaign_list_offset_for_cursor;
    using og::ui::campaign_list_page_step;

    // Clamp: a shelf that fits one page never scrolls.
    EXPECT_EQ(0, campaign_list_clamp_offset(3, 4, 6));
    EXPECT_EQ(0, campaign_list_clamp_offset(-2, 10, 6));
    EXPECT_EQ(4, campaign_list_clamp_offset(9, 10, 6));
    EXPECT_EQ(2, campaign_list_clamp_offset(2, 10, 6));
    EXPECT_EQ(0, campaign_list_clamp_offset(5, 0, 6));

    // Bringing a cursor on screen scrolls minimally, in both directions.
    EXPECT_EQ(0, campaign_list_offset_for_cursor(0, 0, 8, 6));
    EXPECT_EQ(2, campaign_list_offset_for_cursor(7, 0, 8, 6));
    EXPECT_EQ(2, campaign_list_offset_for_cursor(3, 4, 8, 6))
        << "the stale offset first clamps to the last page, where 3 is visible";
    EXPECT_EQ(1, campaign_list_offset_for_cursor(5, 1, 8, 6));

    // Page steps move by a full page and clamp at the shelf's ends.
    EXPECT_EQ(2, campaign_list_page_step(0, 8, 6, 1));
    EXPECT_EQ(0, campaign_list_page_step(2, 8, 6, -1));
    EXPECT_EQ(6, campaign_list_page_step(0, 13, 6, 1));
    EXPECT_EQ(7, campaign_list_page_step(6, 13, 6, 1));
    EXPECT_EQ(0, campaign_list_page_step(3, 13, 6, -1));

    // After a page step the cursor is pulled into the window.
    EXPECT_EQ(2, campaign_list_clamp_cursor(0, 2, 8, 6));
    EXPECT_EQ(5, campaign_list_clamp_cursor(7, 0, 8, 6));
    EXPECT_EQ(4, campaign_list_clamp_cursor(4, 2, 8, 6));
    EXPECT_EQ(0, campaign_list_clamp_cursor(3, 0, 0, 6));

    // Position readout.
    EXPECT_EQ("3 of 8", og::ui::format_campaign_position_label(2, 8));
    EXPECT_EQ("1 of 1", og::ui::format_campaign_position_label(0, 1));
    EXPECT_EQ("8 of 8", og::ui::format_campaign_position_label(9, 8))
        << "an out-of-range cursor must clamp, not overflow the label";
    EXPECT_EQ("0 of 0", og::ui::format_campaign_position_label(0, 0));
}

TEST(CampaignPickerLayout, row_labels_and_description_overflow)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();

    // fit_text_to_chars is the shared trimmer.
    EXPECT_EQ("abc", og::ui::fit_text_to_chars("abc", 5));
    EXPECT_EQ("abcde", og::ui::fit_text_to_chars("abcde", 5));
    EXPECT_EQ("ab...", og::ui::fit_text_to_chars("abcdef", 5));
    EXPECT_EQ("abc", og::ui::fit_text_to_chars("abcdef", 3))
        << "budgets of <= 3 clip without an ellipsis";
    EXPECT_EQ("", og::ui::fit_text_to_chars("abc", 0));

    // Row labels use the list budget.
    EXPECT_EQ("Gladiator", og::ui::fit_campaign_row_label("Gladiator"));
    const std::string long_title(40, 'W');
    const std::string fitted = og::ui::fit_campaign_row_label(long_title);
    EXPECT_EQ(static_cast<std::size_t>(layout.list_label_max_chars),
              fitted.size());
    EXPECT_EQ("...", fitted.substr(fitted.size() - 3));

    // MORE shows exactly when the wrapped description leaves the box.
    EXPECT_FALSE(og::ui::campaign_description_overflows("Short and sweet."));
    std::string long_desc;
    for (int line = 0; line < 12; ++line)
        long_desc += "A reasonably long line of prose for the box.\n";
    EXPECT_TRUE(og::ui::campaign_description_overflows(long_desc));
    EXPECT_FALSE(og::ui::campaign_description_overflows(""));
}

TEST(CampaignPickerLayout, title_row_clears_every_control_at_any_length)
{
    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    std::vector<og::ui::PickerRect> controls = {
        layout.prev, layout.next, layout.choose, layout.cancel,
        layout.delete_button, layout.reset_button, layout.id_button,
        layout.more_button, layout.icon, layout.desc_box};
    for (int row = 0; row < layout.list_rows; ++row)
        controls.push_back(og::ui::campaign_picker_row_rect(row));

    for (int chars = 0; chars <= layout.title_max_chars; ++chars)
    {
        const og::ui::PickerRect title = og::ui::campaign_title_rect(chars);
        EXPECT_GE(title.x, 0) << "title of " << chars << " runs off screen";
        EXPECT_LE(title.x + title.w, 320)
            << "title of " << chars << " runs off screen";
        // ...and stays inside the detail pane, above the icon.
        EXPECT_GE(title.x, layout.detail.x);
        EXPECT_LE(title.x + title.w, layout.detail.x + layout.detail.w);
        for (const og::ui::PickerRect& control : controls)
        {
            EXPECT_FALSE(og::ui::picker_rects_overlap(title, control))
                << "title of " << chars << " chars overlaps a control";
        }
    }
}

TEST(CampaignPickerLayout, old_geometry_really_did_clip_the_shipped_titles)
{
    // Regression witness, all literals: the pre-list-redesign browser drew
    // the title centered on x=160 at y=13 and put ENTER ID at {208,10}. A
    // title of L glyphs spanned 160 +/- 3L on that row, so 17+ characters
    // reached the button. The redesigned title lives in the detail pane
    // (y=36), a row no control shares.
    const og::ui::PickerRect old_id_button{208, 10, 52, 10};
    const auto old_title_rect = [](int chars) {
        return og::ui::PickerRect{160 - chars * 3, 13, chars * 6, 8};
    };
    for (const int shipped : {22, 20, 18, 17})
    {
        EXPECT_TRUE(og::ui::picker_rects_overlap(
            old_title_rect(shipped), old_id_button))
            << shipped << "-char title used to clip ENTER ID";
        EXPECT_FALSE(og::ui::picker_rects_overlap(
            og::ui::campaign_title_rect(shipped),
            og::ui::campaign_picker_layout().id_button))
            << shipped << "-char title must clear the moved ENTER ID";
    }
    // 16 chars was the longest that already fit.
    EXPECT_FALSE(og::ui::picker_rects_overlap(
        old_title_rect(16), old_id_button));
}

TEST(CampaignPickerLayout, fit_campaign_title_budget)
{
    const int budget = og::ui::campaign_picker_layout().title_max_chars;
    EXPECT_EQ(29, budget);

    // Every shipped title is under budget and passes through untouched.
    for (const char* shipped : {"Gladiator", "The Long Season",
                                "Concept Playground", "The Endless Tower",
                                "War of the Westlands",
                                "Multiplayer Game Modes",
                                "The Tryxian Chronicles"})
    {
        EXPECT_EQ(std::string(shipped), og::ui::fit_campaign_title(shipped))
            << shipped << " must not be cut";
    }

    // A user-installed over-budget title is cut to exactly the budget.
    const std::string huge(80, 'W');
    const std::string fitted = og::ui::fit_campaign_title(huge);
    EXPECT_EQ(static_cast<std::size_t>(budget), fitted.size());
    EXPECT_EQ("...", fitted.substr(fitted.size() - 3));
    EXPECT_FALSE(og::ui::picker_rects_overlap(
        og::ui::campaign_title_rect(static_cast<int>(fitted.size())),
        og::ui::campaign_picker_layout().delete_button))
        << "a fitted title must never reach RESET/DELETE";

    // Exactly at the budget: untouched.
    const std::string exact(static_cast<std::size_t>(budget), 'W');
    EXPECT_EQ(exact, og::ui::fit_campaign_title(exact));
    EXPECT_EQ(std::string(), og::ui::fit_campaign_title(""));
}

TEST(CampaignPickerLayout, rect_overlap_helper_edges)
{
    const og::ui::PickerRect a{10, 10, 10, 10};
    EXPECT_TRUE(og::ui::picker_rects_overlap(a, a));
    EXPECT_FALSE(og::ui::picker_rects_overlap(a, og::ui::PickerRect{20, 10, 10, 10}))
        << "edge-touching rects do not overlap";
    EXPECT_FALSE(og::ui::picker_rects_overlap(a, og::ui::PickerRect{10, 20, 10, 10}));
    EXPECT_TRUE(og::ui::picker_rects_overlap(a, og::ui::PickerRect{19, 19, 10, 10}));
    EXPECT_FALSE(og::ui::picker_rects_overlap(a, og::ui::PickerRect{0, 0, 10, 10}));
}

// Keyboard reachability for the browser's nav graph across every COHERENT
// visibility variant the SDL loop can produce (an empty shelf hides the
// pagers, OK, and MORE; MORE needs a highlighted entry, so it needs rows).
// handle_menu_nav IGNORES a link into a hidden button (it does not follow it
// and does not strand focus), so the property worth pinning is that every
// VISIBLE button is still reachable from the default highlight.
TEST(CampaignPickerLayout, nav_variants_keyboard_reachable)
{
    using og::ui::CampaignPickerVisibility;
    using og::ui::kCampaignPickerButtonCount;
    using og::ui::kCampaignPickerRowBaseIndex;
    using og::ui::kCampaignPickerRowCount;

    int variants_checked = 0;
    for (int visible_rows = 0; visible_rows <= kCampaignPickerRowCount;
         ++visible_rows)
    for (const bool prev_hidden : {false, true})
    for (const bool next_hidden : {false, true})
    for (const bool choose_hidden : {false, true})
    for (const bool delete_hidden : {false, true})
    for (const bool more_hidden : {false, true})
    {
        // Coherence: OK shows exactly when the shelf has entries; no rows
        // means an empty shelf (nothing to page or expand); a partially
        // filled page is always the LAST page.
        if ((visible_rows == 0) != choose_hidden)
            continue;
        if (visible_rows == 0 && !(prev_hidden && next_hidden && more_hidden))
            continue;
        if (visible_rows > 0 && visible_rows < kCampaignPickerRowCount &&
            !next_hidden)
            continue;
        ++variants_checked;

        const CampaignPickerVisibility visibility{prev_hidden, next_hidden,
                                                  choose_hidden, delete_hidden,
                                                  visible_rows, more_hidden};
        // RESET occupies the DELETE cell, so exactly one of the pair shows.
        std::array<bool, kCampaignPickerButtonCount> hidden{};
        hidden[og::ui::kCampaignPickerPrevIndex] = prev_hidden;
        hidden[og::ui::kCampaignPickerNextIndex] = next_hidden;
        hidden[og::ui::kCampaignPickerChooseIndex] = choose_hidden;
        hidden[og::ui::kCampaignPickerCancelIndex] = false;
        hidden[og::ui::kCampaignPickerDeleteIndex] = delete_hidden;
        hidden[og::ui::kCampaignPickerIdIndex] = false;
        hidden[og::ui::kCampaignPickerResetIndex] = !delete_hidden;
        for (int row = 0; row < kCampaignPickerRowCount; ++row)
            hidden[static_cast<std::size_t>(kCampaignPickerRowBaseIndex + row)] =
                row >= visible_rows;
        hidden[og::ui::kCampaignPickerMoreIndex] = more_hidden;

        const auto nav = og::ui::campaign_picker_nav(visibility);
        const std::string variant = std::string("rows=") +
            std::to_string(visible_rows) + " prev=" +
            (prev_hidden ? "hidden" : "shown") + " next=" +
            (next_hidden ? "hidden" : "shown") + " ok=" +
            (choose_hidden ? "hidden" : "shown") + " delete=" +
            (delete_hidden ? "hidden" : "shown") + " more=" +
            (more_hidden ? "hidden" : "shown");

        for (const auto& links : nav)
        {
            for (const int target :
                 {links.up, links.down, links.left, links.right})
            {
                EXPECT_TRUE(target < kCampaignPickerButtonCount)
                    << variant << ": nav index out of range";
            }
        }

        // BFS from the browser's default highlight, following links into
        // visible buttons only (handle_menu_nav's real rule).
        std::array<bool, kCampaignPickerButtonCount> reached{};
        std::vector<int> frontier{og::ui::kCampaignPickerCancelIndex};
        reached[og::ui::kCampaignPickerCancelIndex] = true;
        while (!frontier.empty())
        {
            const int current = frontier.back();
            frontier.pop_back();
            for (const int target : {nav[static_cast<std::size_t>(current)].up, nav[static_cast<std::size_t>(current)].down,
                                     nav[static_cast<std::size_t>(current)].left, nav[static_cast<std::size_t>(current)].right})
            {
                if (target < 0 || hidden[static_cast<std::size_t>(target)] || reached[static_cast<std::size_t>(target)])
                    continue;
                reached[static_cast<std::size_t>(target)] = true;
                frontier.push_back(target);
            }
        }
        for (int i = 0; i < kCampaignPickerButtonCount; ++i)
        {
            if (!hidden[static_cast<std::size_t>(i)])
            {
                EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                    << variant << ": button " << i << " is unreachable";
            }
        }

        // ENTER ID's up-link always lands on whichever of DELETE/RESET shows.
        EXPECT_EQ(delete_hidden ? og::ui::kCampaignPickerResetIndex
                                : og::ui::kCampaignPickerDeleteIndex,
                  nav[og::ui::kCampaignPickerIdIndex].up)
            << variant;
        EXPECT_EQ(og::ui::kCampaignPickerIdIndex,
                  nav[og::ui::kCampaignPickerDeleteIndex].down)
            << variant << ": DELETE drops onto the button below it";
        EXPECT_EQ(og::ui::kCampaignPickerIdIndex,
                  nav[og::ui::kCampaignPickerResetIndex].down)
            << variant << ": RESET drops onto the button below it";
        // The visible list rows chain up/down, exiting only at their ends.
        for (int row = 1; row < visible_rows; ++row)
        {
            const int index = kCampaignPickerRowBaseIndex + row;
            EXPECT_EQ(index - 1, nav[static_cast<std::size_t>(index)].up)
                << variant << ": row " << row << " must step up its neighbor";
            EXPECT_EQ(index,
                      nav[static_cast<std::size_t>(index - 1)].down)
                << variant << ": row " << (row - 1)
                << " must step down its neighbor";
        }
    }
    ASSERT_EQ(58, variants_checked)
        << "the coherent-variant enumeration changed shape";
}

// --- Level browser (SET LEVEL) layout pins (issue #186) ---
//
// The reported defects: the legacy pitch pushed row 2's preview frame to the
// screen bottom, and the description box {130,35,185,110} was painted AFTER
// the entries, overprinting the stats column (x=75..165) of rows 0-1. The
// pins below hold the two repaired properties for every row: previews fully
// on the 200px screen, and the description box strictly right of the widest
// stats line.

TEST(LevelPickerLayout, preview_rows_fit_the_screen_at_uniform_pitch)
{
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();

    ASSERT_EQ(3, layout.row_count);
    for (int row = 0; row < layout.row_count; ++row)
    {
        const int row_y = og::ui::level_picker_row_y(row);
        EXPECT_EQ(layout.row0_y + layout.row_pitch * row, row_y)
            << "row " << row << " breaks the pitch";
        // Title line, then the radar with its 2px frame, fully on screen
        // even for the largest radar viewport.
        const int radar_bottom = row_y + layout.radar_dy + layout.radar_max_h;
        EXPECT_LE(radar_bottom + 2, 200)
            << "row " << row << "'s preview frame runs off the screen";
        EXPECT_GE(row_y, 0);
        // The next row's title clears this row's radar frame.
        if (row + 1 < layout.row_count)
        {
            EXPECT_GE(og::ui::level_picker_row_y(row + 1), radar_bottom + 2);
        }
    }

    // The old geometry really did clip: at the legacy pitch the third
    // preview frame ended below the old OK/CANCEL row's top.
    const int old_row2_frame_bottom = 5 + 65 * 2 + 10 + 44 + 2;
    EXPECT_GT(old_row2_frame_bottom, 190)
        << "regression witness lost its meaning";
}

TEST(LevelPickerLayout, description_box_clears_the_stats_column)
{
    const og::ui::LevelPickerLayout layout = og::ui::level_picker_layout();

    // The widest stats line the browser can draw spans stats_max_chars from
    // stats_x; the box must start strictly right of it, on every row band.
    const int stats_right = layout.stats_x + layout.stats_max_chars * 6;
    EXPECT_LE(stats_right, layout.desc_box.x)
        << "the description box reaches the stats column";
    EXPECT_GE(layout.stats_max_chars, 16)
        << "\"Difficulty: NNNN\" (16 chars) must fit the stats budget";

    // The stats columns of all rows share one left edge, right of the
    // widest radar frame.
    EXPECT_GE(layout.stats_x, layout.row_x + layout.radar_max_w + 2);

    // The status column sits right of the longest fitted title and left of
    // the description box.
    EXPECT_GE(layout.status_x, layout.row_x + (layout.title_max_chars + 3) * 6);
    EXPECT_LE(layout.status_x + 7 * 6, layout.desc_box.x)
        << "\"CLEARED\" must end before the description box";

    // Controls and box never overlap each other.
    const std::vector<og::ui::PickerRect> distinct = {
        layout.prev, layout.next, layout.choose, layout.cancel,
        layout.delete_button, layout.id_button, layout.desc_box};
    for (std::size_t i = 0; i < distinct.size(); ++i)
    {
        for (std::size_t j = i + 1; j < distinct.size(); ++j)
        {
            EXPECT_FALSE(og::ui::picker_rects_overlap(distinct[i], distinct[j]))
                << "controls " << i << " and " << j << " overlap";
        }
        EXPECT_GE(distinct[i].x, 0);
        EXPECT_GE(distinct[i].y, 0);
        EXPECT_LE(distinct[i].x + distinct[i].w, 320);
        EXPECT_LE(distinct[i].y + distinct[i].h, 200);
    }

    // The right column shares one left edge (prev, next, desc box), and the
    // army-power readout fits between prev and the screen edge.
    EXPECT_EQ(layout.prev.x, layout.next.x);
    EXPECT_EQ(layout.prev.x, layout.desc_box.x);
    EXPECT_GE(layout.army_x, layout.prev.x + layout.prev.w);
    EXPECT_LE(layout.army_x + 16 * 6, 320)
        << "\"Army power: NNNN\" (16 chars) must stay on screen";
}

TEST(LevelPickerLayout, status_labels_match_the_progress_report)
{
    // Same precedence as the PROGRESS report: CLEARED wins over CURRENT.
    EXPECT_STREQ("CLEARED", og::ui::level_row_status_label(true, false));
    EXPECT_STREQ("CLEARED", og::ui::level_row_status_label(true, true));
    EXPECT_STREQ("CURRENT", og::ui::level_row_status_label(false, true));
    EXPECT_STREQ("", og::ui::level_row_status_label(false, false));
}

// ---------------------------------------------------------------------------
// The Base Camp seat rail. It used to window the whole lobby behind a [+]
// and two pagers, so its geometry changed with the number of players and a
// card could slide sideways under a finger already on its way down. The rail
// is THIS MACHINE'S four seats now, on a grid that never moves; these pin the
// grid and the rule that decides what each slot is.

TEST(SeatRailLayout, the_four_slots_are_a_fixed_grid_across_the_panel)
{
    // Four 70px faces, 8px gutters, opening on BACK's left edge and closing
    // on the panel's right rail with no remainder. Every one of these numbers
    // is also written into the static spec table (test_menu_layout pins the
    // rects); they must not move by a pixel.
    const og::ui::SeatRailLayout rail =
        og::ui::base_camp_seat_rail_layout(4, 0);
    EXPECT_EQ(8, rail.slot_x[0]);
    EXPECT_EQ(86, rail.slot_x[1]);
    EXPECT_EQ(164, rail.slot_x[2]);
    EXPECT_EQ(242, rail.slot_x[3]);
    EXPECT_EQ(70, rail.card_w);
    EXPECT_EQ(og::ui::kSeatRailX0, rail.slot_x[0]);
    EXPECT_EQ(og::ui::kSeatRailRightX, rail.slot_x[3] + rail.card_w);
    for (int slot = 1; slot < og::ui::kSeatRailSlots; ++slot) {
        EXPECT_EQ(og::ui::kSeatRailGap,
                  rail.slot_x[static_cast<std::size_t>(slot)] -
                      (rail.slot_x[static_cast<std::size_t>(slot - 1)] +
                       rail.card_w))
            << "gutter " << slot;
    }
    // The face IS the label budget: 70/6 = 11 characters, which is what makes
    // "ADD PLAYER" and "LOBBY FULL" (10 each) fit a bare slot.
    EXPECT_EQ(11, og::ui::kSeatRailCardWidth / 6);
    EXPECT_LE(std::string_view("ADD PLAYER").size() * 6,
              static_cast<std::size_t>(og::ui::kSeatRailCardWidth));
    EXPECT_LE(std::string_view("LOBBY FULL").size() * 6,
              static_cast<std::size_t>(og::ui::kSeatRailCardWidth));
}

TEST(SeatRailLayout, a_slot_keeps_its_x_however_many_neighbours_are_live)
{
    // The whole point of the fixed grid: slot 2 is at 164 whether it is the
    // last card, the first placeholder, or hidden behind a device cap. The
    // old rail justified and packed, so claiming a seat moved every card on
    // the row out from under the pointer that claimed it.
    const og::ui::SeatRailLayout full =
        og::ui::base_camp_seat_rail_layout(4, 0);
    int checked = 0;
    for (int cards = 0; cards <= og::ui::kSeatRailSlots; ++cards) {
        for (int placeholders = 0;
             placeholders <= og::ui::kSeatRailSlots; ++placeholders) {
            const og::ui::SeatRailLayout rail =
                og::ui::base_camp_seat_rail_layout(cards, placeholders);
            ++checked;
            const std::string shape = std::format(
                "cards={} placeholders={}", cards, placeholders);
            EXPECT_EQ(full.slot_x, rail.slot_x) << shape;
            EXPECT_EQ(70, rail.card_w) << shape;
            EXPECT_EQ(cards, rail.shown_cards) << shape;
            // A slot is a card or a placeholder, never both, and the row
            // never grows a fifth.
            EXPECT_EQ(std::min(placeholders, og::ui::kSeatRailSlots - cards),
                      rail.placeholder_count)
                << shape;
            EXPECT_LE(rail.slot_count(), og::ui::kSeatRailSlots) << shape;
            EXPECT_GE(rail.slot_count(), cards) << shape;
        }
    }
    EXPECT_EQ(25, checked) << "every card/placeholder pairing was exercised";

    // A phone's lone slot opens on the left rail, where BACK and every other
    // left-aligned control on the screen opens (#249).
    const og::ui::SeatRailLayout phone =
        og::ui::base_camp_seat_rail_layout(1, 0);
    EXPECT_EQ(og::ui::kSeatRailX0, phone.slot_x[0]);
    EXPECT_EQ(1, phone.slot_count());
}

TEST(SeatRailSlots, the_device_decides_how_many_slots_the_rail_has)
{
    using og::ui::base_camp_seat_rail_slot_cap;
    using og::ui::SeatClaimability;

    // Desktop: the build limit.
    EXPECT_EQ(4, base_camp_seat_rail_slot_cap(SeatClaimability{}));
    // Phone with nothing attached, then one pad, then two, then plenty.
    EXPECT_EQ(1, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.local_seat_cap = 1}));
    EXPECT_EQ(2, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.local_seat_cap = 2}));
    EXPECT_EQ(3, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.local_seat_cap = 3}));
    EXPECT_EQ(4, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.local_seat_cap = 9}))
        << "the rail has four slots however many pads are plugged in";
    // A build with no multiplayer has one seat and no door to a second.
    EXPECT_EQ(1, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.multiplayer_enabled = false}));
    // The LOBBY the machine is in never removes a slot — that is the dimmed
    // face's job, not the grid's.
    EXPECT_EQ(4, base_camp_seat_rail_slot_cap(
                     SeatClaimability{.local_count = 1, .global_count = 16}));
}

TEST(SeatRailSlots, placeholders_fill_every_slot_the_device_can_still_seat)
{
    using og::ui::base_camp_seat_rail_placeholder_count;
    using og::ui::SeatClaimability;

    // Desktop, one seat claimed: three bare slots.
    EXPECT_EQ(3, base_camp_seat_rail_placeholder_count(
                     SeatClaimability{.local_count = 1, .global_count = 1}));
    // Two claimed, two bare.
    EXPECT_EQ(2, base_camp_seat_rail_placeholder_count(
                     SeatClaimability{.local_count = 2, .global_count = 2}));
    // Four claimed: the rail is full and offers nothing.
    EXPECT_EQ(0, base_camp_seat_rail_placeholder_count(
                     SeatClaimability{.local_count = 4, .global_count = 4}));
    // A phone with nothing attached: one card and NO bare slot — an offer
    // the hardware cannot accept is worse than no offer (#249).
    EXPECT_EQ(0, base_camp_seat_rail_placeholder_count(SeatClaimability{
                     .local_count = 1, .local_seat_cap = 1,
                     .global_count = 1}));
    // A pad opens exactly one more.
    EXPECT_EQ(1, base_camp_seat_rail_placeholder_count(SeatClaimability{
                     .local_count = 1, .local_seat_cap = 2,
                     .global_count = 1}));
    // A DISABLE_MULTIPLAYER build: one seat, nothing beside it.
    EXPECT_EQ(0, base_camp_seat_rail_placeholder_count(SeatClaimability{
                     .multiplayer_enabled = false, .local_count = 1}));
    // THE FULL LOBBY STILL SHOWS ITS SLOTS. The rail is this machine's
    // hardware; a full lobby dims the face (LOBBY FULL) rather than deleting
    // a seat the player can see they own the room for.
    EXPECT_EQ(3, base_camp_seat_rail_placeholder_count(
                     SeatClaimability{.local_count = 1, .global_count = 16}));
    EXPECT_EQ(0, og::ui::seats_still_claimable(
                     SeatClaimability{.local_count = 1, .global_count = 16}))
        << "which is exactly what the dimmed face says";
    // Nothing claimed yet (a connected spectator): four bare slots.
    EXPECT_EQ(4, base_camp_seat_rail_placeholder_count(
                     SeatClaimability{.global_count = 3}));
    // A seat that outlived its cap (a pad unplugged under a live seat) never
    // produces a negative count.
    EXPECT_EQ(0, base_camp_seat_rail_placeholder_count(SeatClaimability{
                     .local_count = 3, .local_seat_cap = 1,
                     .global_count = 3}));
}

TEST(SeatRailSlots, claimability_answers_the_dimmed_face_question)
{
    using og::ui::SeatClaimability;
    using og::ui::seats_still_claimable;

    EXPECT_EQ(2, seats_still_claimable(
                     SeatClaimability{.local_count = 2, .global_count = 2}));
    EXPECT_EQ(0, seats_still_claimable(
                     SeatClaimability{.local_count = 4, .global_count = 4}));
    // The lobby ceiling binds before the local cap does.
    EXPECT_EQ(2, seats_still_claimable(
                     SeatClaimability{.local_count = 1, .global_count = 14}));
    EXPECT_EQ(0, seats_still_claimable(
                     SeatClaimability{.local_count = 1, .global_count = 16}));
    // A DISABLE_MULTIPLAYER build has no second seat to offer at all.
    EXPECT_EQ(0, seats_still_claimable(
                     SeatClaimability{.multiplayer_enabled = false}));
}
