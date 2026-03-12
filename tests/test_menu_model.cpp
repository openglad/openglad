#include <openglad/interface/ui/menu_model.h>
#include <gtest/gtest.h>

TEST(MenuModel, main_definition_and_lookup)
{
    using namespace og::ui;
    const PickerMenuDefinition& def = picker_menu_definition(PickerMenuId::Main);

    ASSERT_EQ(static_cast<int>(PickerMenuId::Main), static_cast<int>(def.id)) << "main menu definition should report main id";
    ASSERT_TRUE(def.items.size() >= 10) << "main menu should expose expected item count";

    const PickerMenuItem* begin = find_picker_menu_item(PickerMenuId::Main, "begin_new_game");
    ASSERT_TRUE(begin != nullptr) << "begin_new_game id should resolve";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::BeginNewGame), static_cast<int>(begin->command)) << "begin_new_game should map to BeginNewGame command";

    const PickerMenuItem* p4 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 4);
    ASSERT_TRUE(p4 != nullptr) << "set player mode with arg=4 should resolve";
    ASSERT_TRUE(p4->id == "4_player") << "set player mode arg=4 should resolve to 4_player item";

    const PickerMenuItem* missing = find_picker_menu_item(PickerMenuId::Main, "missing-item");
    ASSERT_TRUE(missing == nullptr) << "missing id should return nullptr";
}


TEST(MenuModel, team_build_lookup)
{
    using namespace og::ui;
    const PickerMenuDefinition& def = picker_menu_definition(PickerMenuId::TeamBuild);

    ASSERT_EQ(static_cast<int>(PickerMenuId::TeamBuild), static_cast<int>(def.id)) << "team build definition should report team build id";
    ASSERT_TRUE(def.items.size() >= 9) << "team build should expose expected item count";

    const PickerMenuItem* start = find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::StartGame);
    ASSERT_TRUE(start != nullptr) << "start game command should resolve in team build";
    ASSERT_TRUE(start->id == "go") << "start game item id should be go";

    const PickerMenuItem* back = find_picker_menu_item(PickerMenuId::TeamBuild, "back");
    ASSERT_TRUE(back != nullptr) << "back id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::Back), static_cast<int>(back->command)) << "back item should map to Back command";

    const PickerMenuItem* wrong_arg = find_picker_menu_item(
        PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 99);
    ASSERT_TRUE(wrong_arg == nullptr) << "unknown arg variant should return nullptr";
}


TEST(MenuModel, invalid_menu_id_falls_back_to_main)
{
    using namespace og::ui;
    const PickerMenuId invalid = static_cast<PickerMenuId>(999);
    const PickerMenuDefinition& def = picker_menu_definition(invalid);

    ASSERT_EQ(static_cast<int>(PickerMenuId::Main), static_cast<int>(def.id)) << "unknown menu id should fall back to main definition";
}


TEST(MenuModel, lookup_miss_paths_and_fallback_item)
{
    using namespace og::ui;
    const PickerMenuId invalid = static_cast<PickerMenuId>(-7);

    const PickerMenuItem* begin_from_invalid =
        find_picker_menu_item(invalid, "begin_new_game");
    ASSERT_TRUE(begin_from_invalid != nullptr) << "invalid menu id should fall back to main menu items";

    const PickerMenuItem* command_miss =
        find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::SetDifficulty, 0);
    ASSERT_TRUE(command_miss == nullptr) << "nonexistent command lookup should return nullptr";

    const PickerMenuItem* id_miss =
        find_picker_menu_item(PickerMenuId::TeamBuild, "definitely_missing_item");
    ASSERT_TRUE(id_miss == nullptr) << "missing id lookup should return nullptr";
}


TEST(MenuModel, round10_set_player_mode_arg_variants_and_invalid_command_lookup)
{
    using namespace og::ui;

    const PickerMenuItem* p1 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 1);
    const PickerMenuItem* p2 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 2);
    const PickerMenuItem* p3 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 3);
    ASSERT_TRUE(p1 && p2 && p3) << "set-player-mode items for args 1/2/3 should resolve";

    const PickerMenuItem* invalid_cmd =
        find_picker_menu_item(static_cast<PickerMenuId>(777), PickerMenuCommand::SetCampaign, 0);
    ASSERT_TRUE(invalid_cmd == nullptr) << "invalid menu id plus unmatched command should return nullptr via fallback lookup";
}

