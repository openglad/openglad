#include "test_framework.h"

// picker_dialogs.cpp symbols
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

TEST(PickerDialogsReal, picker_dialogs_yes_or_no_queued_override_paths)
{
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    picker_testing_yes_or_no_queue_push(false);

    ASSERT_TRUE(yes_or_no_prompt("Delete", "Proceed with deletion?", false)) << "queued true override should force yes result";
    ASSERT_TRUE(!yes_or_no_prompt("Delete", "Proceed with deletion?", true)) << "queued false override should force no result";

    picker_testing_yes_or_no_queue_clear();
}


TEST(PickerDialogsReal, picker_dialogs_no_or_yes_default_paths)
{
    ASSERT_TRUE(no_or_yes_prompt("Reset", "Reset current progress?", true)) << "no_or_yes_prompt should return default=true in test mode";
    ASSERT_TRUE(!no_or_yes_prompt("Reset", "Reset current progress?", false)) << "no_or_yes_prompt should return default=false in test mode";
}


TEST(PickerDialogsReal, picker_dialogs_popup_dialog_testmode_noop)
{
    popup_dialog("Information", "This is a test popup\\nwith two lines.");
    ASSERT_TRUE(true) << "popup_dialog should be non-blocking in test mode";
}

