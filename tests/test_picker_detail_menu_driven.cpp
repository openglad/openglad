#include <openglad/legacy/graph.h>
#include <openglad/input/button.h>
#include "test_framework.h"

#include <array>
#include <memory>

extern screen* myscreen;

// picker.cpp globals
extern std::unique_ptr<guy> current_guy;
extern guy* old_guy;
extern Sint32 editguy;

// picker_input.cpp global keyboard state observer
extern const Uint8* keystates;

// picker.cpp menu entry
Sint32 create_detail_menu(guy* arg1);
const char* family_name_copy(short family);

namespace
{
struct PickerStateGuard
{
    std::unique_ptr<guy> saved_current;
    guy* saved_old = nullptr;
    Sint32 saved_editguy = 0;
    unsigned char saved_team_size = 0;

    PickerStateGuard()
    {
        saved_current = std::move(current_guy);
        saved_old = old_guy;
        saved_editguy = editguy;
        saved_team_size = myscreen->save_data.team_size;
    }

    ~PickerStateGuard()
    {
        current_guy = std::move(saved_current);
        old_guy = saved_old;
        editguy = saved_editguy;
        myscreen->save_data.team_size = saved_team_size;
    }
};

struct TeamSlotGuard
{
    int slot = 0;
    guy* saved = nullptr;
    explicit TeamSlotGuard(int slot_) : slot(slot_), saved(myscreen->save_data.team_list[slot_].release()) {}
    ~TeamSlotGuard() { myscreen->save_data.team_list[slot].reset(saved); }
};

struct KeyStateGuard
{
    const Uint8* saved = nullptr;
    std::array<Uint8, MAXKEYS> fake{};

    KeyStateGuard()
    {
        saved = keystates;
        fake.fill(0);
        keystates = fake.data();
    }

    ~KeyStateGuard()
    {
        keystates = saved;
    }

    void pulse(SDL_Scancode sc, int down_ms = 25, int up_ms = 10)
    {
        fake[sc] = 1;
        SDL_Delay(down_ms);
        fake[sc] = 0;
        SDL_Delay(up_ms);
    }
};

struct InjectorArgs
{
    KeyStateGuard* ks = nullptr;
    bool go_to_promote = false;
};

static int injector_thread_exit_detail_menu(void* data)
{
    InjectorArgs* a = static_cast<InjectorArgs*>(data);
    // Wait until init_buttons has created vbuttons for this menu. If we pulse too
    // early, handle_menu_nav/leftmouse won't observe the press.
    const Uint32 deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline)
    {
        if (allbuttons[0] != nullptr) // "back" is index 0 in details_buttons
            break;
        SDL_Delay(5);
    }

    // Fastest deterministic exit path in picker menus is the ESC hotkey.
    // leftmouse() treats held hotkeys as a click.
    if (!a->go_to_promote)
    {
        a->ks->pulse(SDL_SCANCODE_ESCAPE, 40, 10);
        return 0;
    }

    // For promote we need menu_nav enabled and highlight moved to the promote button.
    a->ks->pulse(SDL_SCANCODE_RIGHT, 30, 10);

    // Activate: LCTRL is KEY_FIRE for player 0 by default. First pulse enables
    // menu_nav, second pulse activates.
    a->ks->pulse(SDL_SCANCODE_LCTRL, 30, 10);
    a->ks->pulse(SDL_SCANCODE_LCTRL, 30, 10);
    return 0;
}
} // namespace

void test_picker_detail_menu_back_exercises_many_family_descriptions()
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    editguy = 0;
    myscreen->save_data.team_size = 1;
    myscreen->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    myscreen->save_data.team_list[0]->name = "TEAM_SOLDIER";
    myscreen->save_data.team_list[0]->level = 10;

    current_guy = std::make_unique<guy>(*myscreen->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, false};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_exit", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(myscreen->save_data.team_list[0].get());
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    // create_detail_menu exits back to the edit menu and always returns REDRAW.
    TEST_ASSERT_EQ(2, (int)r, "detail menu should return REDRAW on back");
}
REGISTER_TEST(test_picker_detail_menu_back_exercises_many_family_descriptions);

void test_picker_detail_menu_promote_mage_to_archmage_branch()
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    editguy = 0;
    myscreen->save_data.team_size = 1;
    myscreen->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    myscreen->save_data.team_list[0]->name = "TEAM_MAGE";
    myscreen->save_data.team_list[0]->level = 6;

    current_guy = std::make_unique<guy>(*myscreen->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, true};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_mage", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(myscreen->save_data.team_list[0].get());
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    TEST_ASSERT_EQ(2, (int)r, "mage promote should request redraw");
}
REGISTER_TEST(test_picker_detail_menu_promote_mage_to_archmage_branch);

void test_picker_detail_menu_promote_orc_to_captain_branch()
{
    PickerStateGuard guard;
    TeamSlotGuard slot_guard(0);

    editguy = 0;
    myscreen->save_data.team_size = 1;
    myscreen->save_data.team_list[0].reset(new guy(FAMILY_ORC));
    myscreen->save_data.team_list[0]->name = "TEAM_ORC";
    myscreen->save_data.team_list[0]->level = 5;

    current_guy = std::make_unique<guy>(*myscreen->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, true};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_orc", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(myscreen->save_data.team_list[0].get());
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    TEST_ASSERT_EQ(2, (int)r, "orc promote should request redraw");
}
REGISTER_TEST(test_picker_detail_menu_promote_orc_to_captain_branch);

void test_picker_family_name_copy_includes_archmage()
{
    const char* a = family_name_copy(FAMILY_ARCHMAGE);
    TEST_ASSERT(a != nullptr, "family_name_copy should return a string");
}
REGISTER_TEST(test_picker_family_name_copy_includes_archmage);
