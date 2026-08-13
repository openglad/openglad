#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/button.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/scroll_view_layout.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <atomic>
#include <list>
#include <string>
#include <memory>
#include <unistd.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// From help.cpp
short read_scenario(screen* scr);
short read_campaign_intro(screen* scr);
short show_campaign_description(screen* scr, const std::string& campaign_id);
Sint32 show_general_help();
std::unique_ptr<og::ui::IPickerClient> picker_testing_create_sdl_client();
Sint32 help_testing_exercise_internal_paths();
Sint32 help_testing_exercise_menu_hooks();
void help_testing_set_force_scroll_text(bool enabled);
void help_testing_set_page_state(bool page_up, bool page_down);
Sint32 help_testing_controls_flowed_max_width();
void help_testing_set_line_key_state(bool line_up, bool line_down);
void help_testing_set_jump_key_state(bool home_down, bool end_down);
Sint32 help_testing_query_linesdown();
bool help_testing_query_scroll_chrome();
Sint32 help_testing_query_active_tab();
Sint32 help_testing_query_help_page();

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
        help_testing_set_line_key_state(false, false);
        help_testing_set_jump_key_state(false, false);
    }

    ~HelpTestingInputGuard()
    {
        help_testing_set_page_state(false, false);
        help_testing_set_line_key_state(false, false);
        help_testing_set_jump_key_state(false, false);
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
    // Probe values observed by the injector (-1 = the wait timed out and
    // the tab/page never reached the expected value).
    std::atomic<int> page_after_wheel_down{-1};
    std::atomic<int> page_after_wheel_up{-1};
    std::atomic<int> page_after_next{-1};
    std::atomic<int> page_after_prev{-1};
    std::atomic<int> tab_after_classes{-1};
    std::atomic<int> tab_after_editor{-1};
    std::atomic<int> tab_after_controls{-1};
    std::atomic<bool> back_clicked{false};
};

// Poll the help screen's probes until they reach `expected` (the engine loop
// iterates every ~10ms); report the final observed value either way.
static int settle_help_page(int expected, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 10)
    {
        if (help_testing_query_help_page() == expected)
            return expected;
        SDL_Delay(10);
    }
    return help_testing_query_help_page();
}

static int settle_help_tab(int expected, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 10)
    {
        if (help_testing_query_active_tab() == expected)
            return expected;
        SDL_Delay(10);
    }
    return help_testing_query_active_tab();
}

// #168: the full-screen help is an engine screen — drive it by interactable
// id (tabs, pagers, BACK), never by raw coordinates.
static int help_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<HelpInjectorState*>(data);

    if (wait_for_interactable("help_tab_classes", 5000))
    {
        SDL_Delay(300);

        // Wheel pages the multi-page Controls tab down, then back up.
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = -3;
        wheel.wheel.integer_y = -3;
        SDL_PushEvent(&wheel);
        state->page_after_wheel_down.store(settle_help_page(1, 2000),
                                           std::memory_order_release);

        wheel.wheel.y = 1;
        wheel.wheel.integer_y = 1;
        SDL_PushEvent(&wheel);
        state->page_after_wheel_up.store(settle_help_page(0, 2000),
                                         std::memory_order_release);

        // The PREV/NEXT pagers (visible: Controls flows to several pages).
        if (wait_for_interactable("help_page_next", 2000))
        {
            SDL_Delay(300);
            interact("help_page_next");
            state->page_after_next.store(settle_help_page(1, 2000),
                                         std::memory_order_release);
            SDL_Delay(300);
            interact("help_page_prev");
            state->page_after_prev.store(settle_help_page(0, 2000),
                                         std::memory_order_release);
        }

        // All three tabs, including the return to Controls.
        SDL_Delay(300);
        interact("help_tab_classes");
        state->tab_after_classes.store(settle_help_tab(1, 2000),
                                       std::memory_order_release);
        SDL_Delay(300);
        interact("help_tab_editor");
        state->tab_after_editor.store(settle_help_tab(2, 2000),
                                      std::memory_order_release);
        SDL_Delay(300);
        interact("help_tab_controls");
        state->tab_after_controls.store(settle_help_tab(0, 2000),
                                        std::memory_order_release);
        SDL_Delay(300);
    }

    // Dismiss via BACK (its Escape hotkey is keystate-polled, which an
    // injected SDL event cannot reach).
    state->back_clicked.store(true, std::memory_order_release);
    interact("help_back");
    return 0;
}

// Shared assertions for every flow that runs the injector against the open
// help screen: tab switches, wheel paging, and pager clicks all happened at
// their expected values, and the screen stayed open until BACK.
static void expect_help_injector_flow(const HelpInjectorState& state)
{
    EXPECT_EQ(1, state.page_after_wheel_down.load(std::memory_order_acquire))
        << "wheel down must page the Controls tab to page 1";
    EXPECT_EQ(0, state.page_after_wheel_up.load(std::memory_order_acquire))
        << "wheel up must page back to page 0";
    EXPECT_EQ(1, state.page_after_next.load(std::memory_order_acquire))
        << "NEXT must flip to page 1";
    EXPECT_EQ(0, state.page_after_prev.load(std::memory_order_acquire))
        << "PREV must flip back to page 0";
    EXPECT_EQ(1, state.tab_after_classes.load(std::memory_order_acquire))
        << "the CLASSES tab must activate";
    EXPECT_EQ(2, state.tab_after_editor.load(std::memory_order_acquire))
        << "the EDITOR tab must activate";
    EXPECT_EQ(0, state.tab_after_controls.load(std::memory_order_acquire))
        << "the CONTROLS tab must re-activate";
    EXPECT_TRUE(state.back_clicked.load(std::memory_order_acquire))
        << "help must stay open until BACK dismisses it";
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
    expect_help_injector_flow(injector_state);
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
    expect_help_injector_flow(injector_state);
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
    expect_help_injector_flow(injector_state);
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
    campaign = "gladiator";
    HelpTestingInputGuard input_guard;

    SdlThreadJoinGuard thread(
        SDL_CreateThread(intro_injector_thread, "intro_injector", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(192, read_campaign_intro(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
}

// The campaign browser's MORE control routes here with an explicit id; the
// forced view must render the same scroller read_campaign_intro shows for
// that campaign (same 192-scanline gladiator description), independent of
// the save's current campaign.
TEST(HelpSmoke, help_show_campaign_description_forced_view_scrolls)
{
    std::string& campaign = og::runtime::current_session->myscreen_->save_data.current_campaign;
    CurrentCampaignGuard campaign_guard(campaign);
    campaign = "tower";  // NOT the id under view
    ForceScrollTextGuard force_scroll;
    HelpTestingInputGuard input_guard;

    SdlThreadJoinGuard thread(
        SDL_CreateThread(intro_injector_thread, "desc_injector", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(192, show_campaign_description(
                       og::runtime::current_session->myscreen_, "gladiator"));
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

// #168: the engine-hook exerciser — the null-state (bare-sweep) shape, tab
// dispatch, pager clamping, and wheel paging, checked step by step.
TEST(HelpSmoke, help_menu_hooks_cover_dispatch_paging_and_null_state)
{
    EXPECT_EQ(0, help_testing_exercise_menu_hooks());
}

// #168: the full-screen frame flows text to the 50-char budget
// (help_char_budget(kHelpTextBoxWidth)). Every authored Controls line fits —
// the helper returns -1 if any wrapped — and the widest line exceeds the old
// 39-char dialog budget, proving the full-width flow is the one measured.
TEST(HelpSmoke, controls_help_lines_flow_within_frame_budget)
{
    const Sint32 max_width = help_testing_controls_flowed_max_width();
    ASSERT_GT(max_width, 39)
        << "a Controls line wrapped against the full-width budget (or the "
           "wide action-key lines were removed)";
    EXPECT_LE(max_width, 50);
}

static int overlong_scenario_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(150);
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

// ---------------------------------------------------------------------------
// #156: journal scrolling — keys are edge-triggered (no modulo throttle),
// arrows/Home/End work, and the on-screen scroll controls scroll without
// dismissing the dialog while a tap anywhere else still dismisses it.
// ---------------------------------------------------------------------------

struct ScrollProbeState
{
    std::atomic<bool> ok{true};
    std::atomic<int> failed_step{0};
    std::atomic<int> last_value{-1};
};

// Poll the viewer's scroll offset until it reaches `expected` (the scroll
// loop iterates every ~10ms; give each step a generous-but-short window).
static bool wait_for_linesdown(int expected, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 10)
    {
        if (help_testing_query_linesdown() == expected)
            return true;
        SDL_Delay(10);
    }
    return false;
}

static void probe_step(ScrollProbeState* state, int step, int expected)
{
    if (!state->ok.load(std::memory_order_acquire))
        return;
    if (!wait_for_linesdown(expected, 600))
    {
        state->ok.store(false, std::memory_order_release);
        state->failed_step.store(step, std::memory_order_release);
        state->last_value.store(help_testing_query_linesdown(),
                                std::memory_order_release);
    }
}

// One short tap of a seam-modeled key: on for ~40ms, off for ~30ms. Under
// the old phase-modulo throttle a tap this short was swallowed about half
// the time; with edge-triggering it always fires exactly once.
static void tap_page_key(bool up, bool down)
{
    help_testing_set_page_state(up, down);
    SDL_Delay(40);
    help_testing_set_page_state(false, false);
    SDL_Delay(30);
}

static void tap_line_key(bool up, bool down)
{
    help_testing_set_line_key_state(up, down);
    SDL_Delay(40);
    help_testing_set_line_key_state(false, false);
    SDL_Delay(30);
}

static void tap_jump_key(bool home_down, bool end_down)
{
    help_testing_set_jump_key_state(home_down, end_down);
    SDL_Delay(40);
    help_testing_set_jump_key_state(false, false);
    SDL_Delay(30);
}

static int scroll_controls_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<ScrollProbeState*>(data);
    SDL_Delay(120);

    // 40 flowed lines, box_width 200: bottomrow = 40*8 - 14*8 = 208.
    // Key taps: deterministic scanline ladder. A short tap fires exactly
    // once (the old modulo gate swallowed it half the time); a long hold
    // crosses the ~270ms first-delay and auto-repeats.
    tap_page_key(false, true);   probe_step(state, 1, 105);
    tap_page_key(true, false);   probe_step(state, 2, 0);
    help_testing_set_page_state(false, true);   // hold: edge 105, repeat 208
    probe_step(state, 3, 208);
    help_testing_set_page_state(false, false);
    SDL_Delay(30);
    tap_page_key(true, false);   probe_step(state, 4, 103);
    tap_page_key(true, false);   probe_step(state, 5, 0);
    tap_line_key(false, true);   probe_step(state, 6, 8);
    tap_line_key(true, false);   probe_step(state, 7, 0);
    tap_jump_key(false, true);   probe_step(state, 8, 208);
    tap_jump_key(true, false);   probe_step(state, 9, 0);

    // On-screen controls: same geometry the viewer computed.
    const og::ui::ScrollViewLayout layout =
        og::ui::compute_scroll_view_layout(40, 200, 0, 208, 0, 0, 320, 200);
    const int up_cx = layout.up.x + layout.up.w / 2;
    const int up_cy = layout.up.y + layout.up.h / 2;
    const int down_cx = layout.down.x + layout.down.w / 2;
    const int down_cy = layout.down.y + layout.down.h / 2;

    // A real click on the DOWN arrow scrolls one line — and must NOT
    // dismiss the dialog (the click sets input_continue; the viewer
    // swallows it because the click landed on a scroll control). The delay
    // before each click lets the viewer drain the previous release event:
    // a release and the next press merged into one pump reads as "held",
    // not a fresh edge.
    inject_click(down_cx, down_cy, 50);
    probe_step(state, 10, 8);

    // Collapsed tap (down+up inside one event pump — the web-touch shape):
    // caught via the pending-click queue, still a scroll, still no dismiss.
    SDL_Delay(80);
    inject_mouse_down(down_cx, down_cy);
    inject_mouse_up(down_cx, down_cy);
    probe_step(state, 11, 16);

    SDL_Delay(80);
    inject_click(up_cx, up_cy, 50);
    probe_step(state, 12, 8);

    // Track click below the thumb pages down: at linesdown=8 the thumb
    // spans y=55..78, so y=110 is below it -> +105.
    SDL_Delay(80);
    inject_click(layout.track.x + layout.track.w / 2, 110, 50);
    probe_step(state, 13, 113);

    // Track click above the thumb pages back up: at linesdown=113 the thumb
    // spans y=77..100, so y=60 is above it -> -105.
    SDL_Delay(80);
    inject_click(layout.track.x + layout.track.w / 2, 60, 50);
    probe_step(state, 14, 8);

    // A tap in the text area still dismisses (the wasm-touch e2e contract:
    // (160,100) must keep working).
    SDL_Delay(80);
    inject_click(160, 100, 30);
    SDL_Delay(100);
    // Failsafe so a missed dismiss cannot hang the binary.
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

TEST(HelpSmoke, help_scroll_controls_scroll_without_dismissing)
{
    ViewportGuard guard;
    ForceScrollTextGuard force_scroll;
    HelpTestingInputGuard input_guard;
    // Force 1:1 event coords so injected clicks land on the UI canvas.
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    auto& description = og::runtime::current_session->myscreen_->level_description();
    LevelDescriptionGuard description_guard(description);
    description.clear();
    for (int i = 0; i < 40; ++i)
        description.push_back("Scenario line " + std::to_string(i));

    ScrollProbeState state;
    SdlThreadJoinGuard thread(SDL_CreateThread(
        scroll_controls_injector_thread, "scroll_controls", &state));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(320, read_scenario(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
    EXPECT_TRUE(help_testing_query_scroll_chrome())
        << "40 lines must grow the scrollbar gutter";
    // Every ladder step expects a CHANGED offset, so a click that dismissed
    // instead of scrolling (or a swallowed key tap) reads as a failed step.
    EXPECT_TRUE(state.ok.load(std::memory_order_acquire))
        << "scroll ladder failed at step "
        << state.failed_step.load(std::memory_order_acquire)
        << " (linesdown = "
        << state.last_value.load(std::memory_order_acquire) << ")";
}

static int short_briefing_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(150);
    // Tap the middle of the text: with no scroll chrome this must dismiss.
    inject_click(160, 100, 30);
    SDL_Delay(100);
    inject_key_press(SDLK_ESCAPE, 10); // failsafe
    return 0;
}

// Byte-identity guard: a briefing that fits the 14-line window computes NO
// scroll chrome (the unit pins in test_scroll_view_layout.cpp hold the
// literal legacy frame), and a tap anywhere dismisses as it always has.
TEST(HelpSmoke, help_short_briefing_has_no_chrome_and_tap_dismisses)
{
    ViewportGuard guard;
    ForceScrollTextGuard force_scroll;
    HelpTestingInputGuard input_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    auto& description = og::runtime::current_session->myscreen_->level_description();
    LevelDescriptionGuard description_guard(description);
    description.clear();
    for (int i = 0; i < 6; ++i)
        description.push_back("Short line " + std::to_string(i));

    SdlThreadJoinGuard thread(SDL_CreateThread(
        short_briefing_injector_thread, "short_briefing", nullptr));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";

    ASSERT_EQ(48, read_scenario(og::runtime::current_session->myscreen_));
    ASSERT_EQ(0, thread.join());
    EXPECT_FALSE(help_testing_query_scroll_chrome())
        << "short briefings must render the legacy dialog, no gutter";
    EXPECT_EQ(0, help_testing_query_linesdown());
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
