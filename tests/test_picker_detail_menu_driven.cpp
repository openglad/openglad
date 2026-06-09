#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>

#include <array>
#include <atomic>
#include <cmath>
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
    std::atomic<bool>* done = nullptr;
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

    const int logical_x = a->go_to_promote ? 200 : 20;
    const int logical_y = a->go_to_promote ? 30 : 180;
    const int click_x = static_cast<int>(std::lround(
        static_cast<float>(logical_x) * og::runtime::current_session->viewport_w_ / 320.0f
        + og::runtime::current_session->viewport_offset_x_));
    const int click_y = static_cast<int>(std::lround(
        static_cast<float>(logical_y) * og::runtime::current_session->viewport_h_ / 200.0f
        + og::runtime::current_session->viewport_offset_y_));
    do
    {
        og::input_native::push_mouse_button_event(true, og::input_native::kMouseButtonLeft, click_x, click_y);
        SDL_Delay(5);
    } while (a->done && !a->done->load(std::memory_order_relaxed));
    og::input_native::push_mouse_button_event(false, og::input_native::kMouseButtonLeft, click_x, click_y);
    return 0;
}

void prepare_detail_menu_mouse_click()
{
    clear_events();
    auto& input_hw = input_hardware_state();
    input_hw.mouse.left = 0;
    input_hw.mouse.right = 0;
    input_hw.picker_was_left_down = false;
    input_hw.picker_was_right_down = false;
}
} // namespace

TEST(PickerDetailMenuDriven, picker_detail_menu_back_exercises_many_family_descriptions)
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
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, false, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_exit", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    // create_detail_menu exits back to the edit menu and always returns REDRAW.
    ASSERT_EQ(2, (int)r) << "detail menu should return REDRAW on back";
}


TEST(PickerDetailMenuDriven, picker_detail_menu_promote_mage_to_archmage_branch)
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
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, true, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_mage", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_EQ(2, (int)r) << "mage promote should request redraw";
    ASSERT_EQ(FAMILY_ARCHMAGE, og::runtime::current_session->myscreen_->save_data.team_list[0]->family);
}


TEST(PickerDetailMenuDriven, picker_detail_menu_promote_orc_to_captain_branch)
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
    std::atomic<bool> done{false};
    prepare_detail_menu_mouse_click();
    InjectorArgs args{&ks, true, &done};
    SDL_Thread* th = SDL_CreateThread(injector_thread_exit_detail_menu, "picker_detail_promote_orc", &args);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    Sint32 r = create_detail_menu(og::runtime::current_session->myscreen_->save_data.team_list[0].get());
    done.store(true, std::memory_order_relaxed);
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_EQ(2, (int)r) << "orc promote should request redraw";
    ASSERT_EQ(FAMILY_BIG_ORC, og::runtime::current_session->myscreen_->save_data.team_list[0]->family);
}


TEST(PickerDetailMenuDriven, picker_family_name_copy_includes_archmage)
{
    const char* a = family_name_copy(FAMILY_ARCHMAGE);
    ASSERT_TRUE(a != nullptr) << "family_name_copy should return a string";
}
