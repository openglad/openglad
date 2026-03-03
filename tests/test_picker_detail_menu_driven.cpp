#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"

#include <array>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// picker.cpp globals
#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


// picker_input.cpp global keyboard state observer

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
        saved_current = std::move(og::runtime::current_session->current_guy_);
        saved_old = pks().old_guy;
        saved_editguy = og::runtime::current_session->editguy_;
        saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
    }

    ~PickerStateGuard()
    {
        og::runtime::current_session->current_guy_ = std::move(saved_current);
        pks().old_guy = saved_old;
        og::runtime::current_session->editguy_ = saved_editguy;
        og::runtime::current_session->myscreen_->save_data.team_size = saved_team_size;
    }
};

struct TeamSlotGuard
{
    int slot = 0;
    guy* saved = nullptr;
    explicit TeamSlotGuard(int slot_) : slot(slot_), saved(og::runtime::current_session->myscreen_->save_data.team_list[slot_].release()) {}
    ~TeamSlotGuard() { og::runtime::current_session->myscreen_->save_data.team_list[slot].reset(saved); }
};

struct KeyStateGuard
{
    const Uint8* saved = nullptr;
    std::array<Uint8, MAXKEYS> fake{};

    KeyStateGuard()
    {
        saved = og::runtime::current_session->keystates_;
        fake.fill(0);
        og::runtime::current_session->keystates_ = fake.data();
    }

    ~KeyStateGuard()
    {
        og::runtime::current_session->keystates_ = saved;
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
    og::runtime::ensure_thread_session();
    InjectorArgs* a = static_cast<InjectorArgs*>(data);
    // Wait until init_buttons has created vbuttons for this menu. If we pulse too
    // early, handle_menu_nav/leftmouse won't observe the press.
    const Uint32 deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline)
    {
        if (og::runtime::current_session->allbuttons_[0] != nullptr) // "back" is index 0 in details_buttons
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

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_SOLDIER));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_SOLDIER";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 10;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, false};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_exit", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
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

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_MAGE));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_MAGE";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 6;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, true};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_mage", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
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

    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(new guy(FAMILY_ORC));
    og::runtime::current_session->myscreen_->save_data.team_list[0]->name = "TEAM_ORC";
    og::runtime::current_session->myscreen_->save_data.team_list[0]->level = 5;

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(*og::runtime::current_session->myscreen_->save_data.team_list[0]);

    KeyStateGuard ks;
    InjectorArgs args{&ks, true};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_orc", &args);
    TEST_ASSERT(th != nullptr, "injector thread started");

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
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
