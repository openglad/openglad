#include <openglad/ui/ui_state.h>

#include "unit.h"

OG_UNIT_TEST(test_ui_command_enum_values)
{
    // Verify enum values are distinct and castable.
    OG_ASSERT(static_cast<std::uint32_t>(og::ui::Command::None) == 0);
    OG_ASSERT(static_cast<std::uint32_t>(og::ui::Command::StartGame) == 1);
    OG_ASSERT(og::ui::Command::QuitApp != og::ui::Command::StartGame);
}

OG_UNIT_TEST(test_picker_state_enum)
{
    // Verify picker states are distinct.
    OG_ASSERT(og::ui::PickerState::MainMenu != og::ui::PickerState::TeamMenu);
    OG_ASSERT(og::ui::PickerState::Playing != og::ui::PickerState::Quitting);
}

OG_UNIT_TEST(test_menu_view_model)
{
    og::ui::MenuViewModel vm;
    vm.title = "Main Menu";
    vm.buttons.push_back(og::ui::ButtonViewModel{"start", "Start Game", true, true, true});
    vm.buttons.push_back(og::ui::ButtonViewModel{"quit", "Quit", true, true, false});
    vm.focused_button = 0;

    OG_ASSERT(vm.buttons.size() == 2);
    OG_ASSERT(vm.buttons[0].focused == true);
    OG_ASSERT(vm.buttons[1].focused == false);
    OG_ASSERT(vm.title == "Main Menu");
}

OG_UNIT_TEST(test_button_view_model_defaults)
{
    og::ui::ButtonViewModel btn;
    OG_ASSERT(btn.enabled == true);
    OG_ASSERT(btn.visible == true);
    OG_ASSERT(btn.focused == false);
    OG_ASSERT(btn.id.empty());
    OG_ASSERT(btn.label.empty());
}
