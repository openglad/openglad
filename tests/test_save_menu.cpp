#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <atomic>
// myscreen is now a macro defined in base.h (via game_session.h)

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// Test: Open the Save Team menu directly, verify the save slots appear, then
// exit via the menu's BACK button.
//
// Note: We can't test actually saving via menu because do_save() calls
// prompt_for_string() which requires keyboard text input. But we CAN verify
// the real save menu opens, exposes the slot buttons, and unwinds cleanly.

struct SaveMenuState {
    bool finished;
    bool saw_save_menu;
    bool clicked_back;
    std::atomic<bool>* menu_done;
};

static int save_menu_injector(void* data)
{
    og::runtime::ensure_thread_session();
    SaveMenuState* state = static_cast<SaveMenuState*>(data);
    int elapsed = 0;
    const int poll_interval = 50;
    int esc_cooldown_ms = 0;
    int click_cooldown_ms = 0;

    while (!state->menu_done->load(std::memory_order_acquire)) {
        const auto interactables = get_interactables();

        for (const auto& item : interactables) {
            if (item.hidden)
                continue;

            if (item.id == "save_slot_1")
                state->saw_save_menu = true;

            if (click_cooldown_ms <= 0 && item.id == "back" && item.y >= 170) {
                const int game_x = item.x + item.width / 2;
                const int game_y = item.y + item.height / 2;
                const int win_x = static_cast<int>(static_cast<float>(game_x)
                    * (og::runtime::current_session->viewport_w_ / 320.0f)
                    + og::runtime::current_session->viewport_offset_x_);
                const int win_y = static_cast<int>(static_cast<float>(game_y)
                    * (og::runtime::current_session->viewport_h_ / 200.0f)
                    + og::runtime::current_session->viewport_offset_y_);
                fprintf(stderr, "  [test] clicking back from save menu\n");
                inject_click(win_x, win_y);
                state->clicked_back = true;
                click_cooldown_ms = 200;
                break;
            }
        }

        if (elapsed >= 5000 && esc_cooldown_ms <= 0) {
            inject_key_press(SDLK_ESCAPE, 10);
            esc_cooldown_ms = 200;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
        if (click_cooldown_ms > 0)
            click_cooldown_ms -= poll_interval;
        if (esc_cooldown_ms > 0)
            esc_cooldown_ms -= poll_interval;
    }

    state->finished = true;
    return 0;
}

TEST(SaveMenu, save_team_menu) {
    trace_clear();

    // Need some team data so the slot menu has a realistic save payload.
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    std::atomic<bool> menu_done{false};
    SaveMenuState state = { false, false, false, &menu_done };
    SDL_Thread* thread = SDL_CreateThread(save_menu_injector, "save_menu_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    const Sint32 ret = create_save_menu(0);
    menu_done.store(true, std::memory_order_release);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_save_menu) << "should have seen the save team menu";
    ASSERT_TRUE(state.clicked_back) << "should have exited via the save menu back button";
    ASSERT_TRUE(ret & 2) << "create_save_menu(back) should return REDRAW";
}
