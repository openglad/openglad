#include <openglad/runtime/screen.h>

#include "test_framework.h"
#include "test_input_helpers.h"

extern screen* myscreen;

// picker_dialogs.cpp symbols
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void picker_testing_set_force_real_dialogs(bool enabled);

namespace
{
struct DialogThreadState
{
    bool started = false;
    bool finished = false;
};

static int click_yes_thread(void* data)
{
    DialogThreadState* st = static_cast<DialogThreadState*>(data);
    st->started = true;
    SDL_Delay(80);
    for (int i = 0; i < 20; i++)
    {
        inject_click(180, 280, 5); // YES button center in 640x400 window coords
        SDL_Delay(10);
    }
    st->finished = true;
    return 0;
}

static int click_no_thread(void* data)
{
    DialogThreadState* st = static_cast<DialogThreadState*>(data);
    st->started = true;
    SDL_Delay(80);
    for (int i = 0; i < 20; i++)
    {
        inject_click(180, 280, 5); // NO button center in 640x400 window coords
        SDL_Delay(10);
    }
    st->finished = true;
    return 0;
}

static int click_ok_thread(void* data)
{
    DialogThreadState* st = static_cast<DialogThreadState*>(data);
    st->started = true;
    SDL_Delay(80);
    for (int i = 0; i < 20; i++)
    {
        inject_click(320, 280, 5); // OK button center in 640x400 window coords
        SDL_Delay(10);
    }
    st->finished = true;
    return 0;
}
} // namespace

void test_picker_dialogs_yes_or_no_real_dialog_yes_click()
{
    (void)myscreen;
    picker_testing_set_force_real_dialogs(true);

    DialogThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(click_yes_thread, "picker_yes_dialog", &st);
    TEST_ASSERT(thread != nullptr, "failed to create YES injector thread");

    const bool answer = yes_or_no_prompt("Delete", "Proceed with deletion?", false);

    int rc = 0;
    SDL_WaitThread(thread, &rc);
    picker_testing_set_force_real_dialogs(false);

    TEST_ASSERT(st.started && st.finished, "YES injector should run");
    TEST_ASSERT(answer, "yes_or_no_prompt should return true after YES click");
}
REGISTER_TEST(test_picker_dialogs_yes_or_no_real_dialog_yes_click);

void test_picker_dialogs_no_or_yes_real_dialog_no_click()
{
    picker_testing_set_force_real_dialogs(true);

    DialogThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(click_no_thread, "picker_no_dialog", &st);
    TEST_ASSERT(thread != nullptr, "failed to create NO injector thread");

    const bool answer = no_or_yes_prompt("Reset", "Reset current progress?", true);

    int rc = 0;
    SDL_WaitThread(thread, &rc);
    picker_testing_set_force_real_dialogs(false);

    TEST_ASSERT(st.started && st.finished, "NO injector should run");
    TEST_ASSERT(!answer, "no_or_yes_prompt should return false after NO click");
}
REGISTER_TEST(test_picker_dialogs_no_or_yes_real_dialog_no_click);

void test_picker_dialogs_popup_real_dialog_ok_click()
{
    picker_testing_set_force_real_dialogs(true);

    DialogThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(click_ok_thread, "picker_popup_dialog", &st);
    TEST_ASSERT(thread != nullptr, "failed to create OK injector thread");

    popup_dialog("Information", "This is a test popup\nwith two lines.");

    int rc = 0;
    SDL_WaitThread(thread, &rc);
    picker_testing_set_force_real_dialogs(false);

    TEST_ASSERT(st.started && st.finished, "popup injector should run");
}
REGISTER_TEST(test_picker_dialogs_popup_real_dialog_ok_click);
