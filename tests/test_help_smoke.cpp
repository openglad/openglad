#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/button.h>
#include <openglad/interface/ui/picker_state.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <atomic>
#include <list>
#include <string>
#include <memory>
#include <unistd.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// From help.cpp
short read_scenario(screen* scr);
short read_campaign_intro(screen* scr);
Sint32 show_general_help();
std::unique_ptr<og::ui::IPickerClient> picker_testing_create_sdl_client();
Sint32 help_testing_exercise_internal_paths();
void help_testing_set_force_scroll_text(bool enabled);
void help_testing_set_page_state(bool page_up, bool page_down);
Sint32 help_testing_controls_flowed_max_width();

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;

    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;
    }

    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

struct ForceScrollTextGuard
{
    ForceScrollTextGuard() { help_testing_set_force_scroll_text(true); }
    ~ForceScrollTextGuard() { help_testing_set_force_scroll_text(false); }
};

class SdlThreadJoinGuard
{
public:
    explicit SdlThreadJoinGuard(SDL_Thread* thread) : thread_(thread) {}
    ~SdlThreadJoinGuard() { (void)join(); }

    SDL_Thread* get() const { return thread_; }

    int join()
    {
        if (thread_ != nullptr)
        {
            SDL_WaitThread(thread_, &result_);
            thread_ = nullptr;
        }
        return result_;
    }

private:
    SDL_Thread* thread_ = nullptr;
    int result_ = 0;
};

struct HelpTestingInputGuard
{
    HelpTestingInputGuard()
    {
        help_testing_set_page_state(false, false);
    }

    ~HelpTestingInputGuard()
    {
        help_testing_set_page_state(false, false);
    }
};

struct LevelDescriptionGuard
{
    std::list<std::string>& description;
    std::list<std::string> saved;

    explicit LevelDescriptionGuard(std::list<std::string>& description_)
        : description(description_), saved(description_) {}

    ~LevelDescriptionGuard() { description = std::move(saved); }
};

struct CurrentCampaignGuard
{
    std::string& campaign;
    std::string saved;

    explicit CurrentCampaignGuard(std::string& campaign_)
        : campaign(campaign_), saved(campaign_) {}

    ~CurrentCampaignGuard() { campaign = std::move(saved); }
};

struct HelpInjectorState
{
    std::atomic<bool> escape_queued{false};
};

static int help_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<HelpInjectorState*>(data);
    SDL_Delay(100);

    // Exercise both wheel directions while the long Controls tab is active.
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -3;
    wheel.wheel.integer_y = -3;
    SDL_PushEvent(&wheel);

    SDL_Delay(50);

    wheel.wheel.y = 1;
    wheel.wheel.integer_y = 1;
    SDL_PushEvent(&wheel);
    SDL_Delay(50);

    // Hold page keys long enough to cross the viewer's intentional 10-tick
    // repeat debounce.  The atomic seam models held input without writing to
    // SDL's thread-owned keyboard-state array.
    help_testing_set_page_state(false, true);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);
    help_testing_set_page_state(true, false);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);

    // Click all three tabs, including the return to Controls.
    inject_click(120, 22, 10);
    inject_click(182, 22, 10);
    inject_click(60, 22, 10);

    SDL_Delay(50);

    // Dismiss through the production key-event path. show_general_help also
    // retains held-key polling, while the key-event latch catches a short press.
    state->escape_queued.store(true, std::memory_order_release);
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

TEST(HelpSmoke, help_show_general_help_scrolls_tabs_and_exits_cleanly)
{
    ViewportGuard guard;
    HelpTestingInputGuard input_guard;
    // Force 1:1 event coords to simplify tab click injection.
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    HelpInjectorState injector_state;
    SdlThreadJoinGuard thread(
        SDL_CreateThread(help_injector_thread, "help_injector", &injector_state));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(1, show_general_help());
    EXPECT_TRUE(injector_state.escape_queued.load(std::memory_order_acquire))
        << "tab clicks must not dismiss help before the Escape event";
    ASSERT_EQ(0, thread.join());
}

TEST(HelpSmoke, concrete_sdl_picker_client_delegates_to_general_help)
{
    ViewportGuard guard;
    HelpTestingInputGuard input_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    std::unique_ptr<og::ui::IPickerClient> client =
        picker_testing_create_sdl_client();
    ASSERT_NE(nullptr, client);
    HelpInjectorState injector_state;
    SdlThreadJoinGuard thread(SDL_CreateThread(
        help_injector_thread, "sdl_client_help_injector", &injector_state));
    ASSERT_NE(nullptr, thread.get());
    client->show_help();
    EXPECT_TRUE(injector_state.escape_queued.load(std::memory_order_acquire))
        << "picker help must remain open until the Escape event";
    EXPECT_EQ(0, thread.join());
}

TEST(HelpSmoke, help_button_action_opens_viewer_and_requests_redraw)
{
    ViewportGuard guard;
    HelpTestingInputGuard input_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    HelpInjectorState injector_state;
    SdlThreadJoinGuard thread(SDL_CreateThread(
        help_injector_thread, "help_button_injector", &injector_state));
    ASSERT_NE(nullptr, thread.get());
    vbutton dispatcher;
    EXPECT_EQ(2, dispatcher.do_call(
                     button_action_id(ButtonAction::ShowHelp), 0));
    EXPECT_TRUE(injector_state.escape_queued.load(std::memory_order_acquire))
        << "button-dispatched help must remain open until the Escape event";
    EXPECT_EQ(0, thread.join());
}


static int intro_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(80);

    // The campaign intro uses the same scrolling viewer through a distinct
    // template specialization.  Verify that it supports the complete wheel
    // and page navigation contract, not only Escape.
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -3;
    wheel.wheel.integer_y = -3;
    SDL_PushEvent(&wheel);
    SDL_Delay(40);

    wheel.wheel.y = 1;
    wheel.wheel.integer_y = 1;
    SDL_PushEvent(&wheel);
    SDL_Delay(40);

    help_testing_set_page_state(false, true);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);
    help_testing_set_page_state(true, false);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);

    // Trigger early exit via input_continue (Escape sets it in handle_key_event).
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

static int scenario_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(80);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -3;
    wheel.wheel.integer_y = -3;
    SDL_PushEvent(&wheel);

    SDL_Delay(30);
    help_testing_set_page_state(false, true);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);

    SDL_Delay(30);
    wheel.wheel.y = 3;
    wheel.wheel.integer_y = 3;
    SDL_PushEvent(&wheel);

    SDL_Delay(30);
    help_testing_set_page_state(true, false);
    SDL_Delay(220);
    help_testing_set_page_state(false, false);

    SDL_Delay(30);
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

TEST(HelpSmoke, help_read_campaign_intro_smoke_exits_on_input)
{
    std::string& campaign = og::runtime::current_session->myscreen_->save_data.current_campaign;
    CurrentCampaignGuard campaign_guard(campaign);
    campaign = "org.openglad.gladiator";
    HelpTestingInputGuard input_guard;

    SdlThreadJoinGuard thread(
        SDL_CreateThread(intro_injector_thread, "intro_injector", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(192, read_campaign_intro(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
}

TEST(HelpSmoke, help_read_scenario_scroll_view_exits_on_input)
{
    ForceScrollTextGuard force_scroll;
    HelpTestingInputGuard input_guard;
    auto& description = og::runtime::current_session->myscreen_->level_description();
    LevelDescriptionGuard description_guard(description);
    description.clear();
    for (int i = 0; i < 40; ++i)
        description.push_back("Scenario line " + std::to_string(i));

    SdlThreadJoinGuard thread(SDL_CreateThread(
        scenario_injector_thread, "scenario_injector", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(320, read_scenario(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
}

TEST(HelpSmoke, help_internal_paths_cover_loading_tabs_and_scroll)
{
    EXPECT_EQ(0, help_testing_exercise_internal_paths());
}

// #152: the Controls tab shipped three 41-42 char lines against the 39-char
// frame. After render-time flowing, every line fits and the over-budget
// lines actually wrapped (the flowed line count grew).
TEST(HelpSmoke, controls_help_lines_flow_within_frame_budget)
{
    const Sint32 max_width = help_testing_controls_flowed_max_width();
    ASSERT_GT(max_width, 0)
        << "the over-budget Controls lines did not wrap at all";
    EXPECT_LE(max_width, 39);
}

static int overlong_scenario_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(150);
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

// #152: scenario lines wider than the 33-char frame budget re-wrap instead
// of painting past the dialog bevel. scroll_text_view returns
// flowed_line_count * 8, so the return value is the flow probe: 10 source
// lines of 49 chars flow to exactly 20 display lines.
TEST(HelpSmoke, help_read_scenario_flows_overlong_lines)
{
    ForceScrollTextGuard force_scroll;
    HelpTestingInputGuard input_guard;
    auto& description = og::runtime::current_session->myscreen_->level_description();
    LevelDescriptionGuard description_guard(description);
    description.clear();
    for (int i = 0; i < 10; ++i)
        description.push_back(
            "word word word word word word word word word word");

    SdlThreadJoinGuard thread(SDL_CreateThread(
        overlong_scenario_injector_thread, "overlong_scenario", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(160, read_scenario(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
}
