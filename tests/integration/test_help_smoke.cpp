#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/button.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/interface/ui/scroll_view_layout.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <atomic>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <memory>
#include "test_ability_select_scope.h"
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
Sint32 help_testing_controls_flowed_max_width(bool ability_select);
std::string help_testing_controls_text();
Sint32 help_testing_classes_flowed_max_width();
std::string help_testing_classes_text();
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
    // The exerciser returns the number of checks it RAN (negated failing
    // index on failure), so pinning the count catches a helper that bailed
    // out early as well as one that failed a check. The literal moves only
    // when a check is deliberately added or removed.
    EXPECT_EQ(21, help_testing_exercise_internal_paths())
        << "all 21 loader/tab/bounds checks must run and pass";
}

// #168: the engine-hook exerciser — the null-state (bare-sweep) shape, tab
// dispatch, pager clamping, and wheel paging, checked step by step.
TEST(HelpSmoke, help_menu_hooks_cover_dispatch_paging_and_null_state)
{
    // Same contract as above: the count proves the exerciser ran to the end.
    EXPECT_EQ(31, help_testing_exercise_menu_hooks())
        << "all 31 null-state/dispatch/paging/wheel checks must run and pass";
}

// #168: the full-screen frame flows text to the 50-char budget
// (help_char_budget(kHelpTextBoxWidth)). Every authored Controls line fits —
// the helper returns -1 if any wrapped — and the widest line exceeds the old
// 39-char dialog budget, proving the full-width flow is the one measured.
// Both tables (New Ability Select off and on) are held to the rule; the
// widest line of each is the 50-glyph Fire line with its "(web: Z)".
TEST(HelpSmoke, controls_help_lines_flow_within_frame_budget)
{
    for (const bool ability_select : {false, true})
    {
        const Sint32 max_width =
            help_testing_controls_flowed_max_width(ability_select);
        ASSERT_GT(max_width, 39)
            << "a Controls line wrapped against the full-width budget (or the "
               "wide action-key lines were removed); ability_select="
            << ability_select;
        EXPECT_LE(max_width, 50) << "ability_select=" << ability_select;
    }
}

// The Controls tab shows the action table for the setting in force: on,
// one row per SPEC key and the two lines that say how the second row and
// the ABILITIES screen work; off, the classic Special / Next sp rows on the
// same keys. Both name the new default keys (P2 yells on Enter, P3 on O).
//
// RED: get_tab_content ignoring the setting (always the classic table).
TEST(HelpSmoke, controls_help_follows_ability_select)
{
    {
        ScopedAbilitySelect on(true);
        const std::string text = help_testing_controls_text();
        EXPECT_NE(std::string::npos, text.find("ACTION KEYS (NEW ABILITY SELECT ON)\n"));
        EXPECT_NE(std::string::npos, text.find("\nSpec 3:  3       \\       0       6\n"));
        EXPECT_NE(std::string::npos, text.find("\nSHIFTER + SPEC 1/2/3: the second row.\n"));
        EXPECT_NE(std::string::npos, text.find("\nTRAIN > ABILITIES sets each key.\n"));
        EXPECT_EQ(std::string::npos, text.find("Next sp:"));
        EXPECT_NE(std::string::npos, text.find("Yell defaults: P1=E, P2=Enter, P3=O, P4=Y"));
    }
    {
        ScopedAbilitySelect off;
        const std::string text = help_testing_controls_text();
        EXPECT_NE(std::string::npos, text.find("\nSpecial: 1       [       8       4\n"));
        EXPECT_NE(std::string::npos, text.find("\nNext sp: 2       ]       9       5\n"));
        EXPECT_EQ(std::string::npos, text.find("Spec 3:"));
        EXPECT_EQ(std::string::npos, text.find("~"))
            << "the font has no ~ (it renders as ?)";
        EXPECT_NE(std::string::npos, text.find("Yell defaults: P1=E, P2=Enter, P3=O, P4=Y"));
    }
}

// The CLASSES tab (cfg/help_classes.txt) is written to fit the same 50-char
// frame budget: nothing wraps. A file that failed to load is replaced by a
// one-line error message that would also fit, so the test checks that the
// real file's first line is there as well.
// RED: append a 51-character line to cfg/help_classes.txt -> -1.
TEST(HelpSmoke, classes_help_lines_flow_within_frame_budget)
{
    const Sint32 max_width = help_testing_classes_flowed_max_width();
    ASSERT_NE(-1, max_width)
        << "a line of cfg/help_classes.txt wrapped against the full-width "
           "budget";
    EXPECT_LE(max_width, 50);
    EXPECT_NE(std::string::npos,
              help_testing_classes_text().find("*** CHARACTER CLASSES ***"))
        << "cfg/help_classes.txt did not load";
}

// The CLASSES tab names every special the New Specials setting adds, and
// says once that they need the setting. The list comes from the specials
// table the game loads, so a special added to a class pack without a line
// in the help file fails here. An alternate counts as new when its own row
// is new or when it sits on a new slot (a hidden slot hides its alternate).
// REDs: delete the GHOST entry (Siphon, Possess, Phase are missing); delete
// the text "Haste Self"; delete the note's two lines.
TEST(HelpSmoke, classes_help_names_every_new_special)
{
    const std::string text = help_testing_classes_text();
    int checked = 0;
    const auto expect_named = [&](const char* hud_name, int family_id) {
        const std::string title = og::ui::special_title(hud_name);
        ++checked;
        EXPECT_NE(std::string::npos, text.find(title))
            << "family " << family_id << ": the HELP tab's CLASSES page "
            << "does not name \"" << title << "\"";
    };
    for (int family_id = 0; family_id < 256; family_id++)
    {
        const FamilyDescriptor* fd = get_family_descriptor(family_id);
        if (fd == nullptr)
            continue;
        for (int slot = 1; slot < FD_NUM_SPECIALS; slot++)
        {
            if (fd->special_new_kit[slot])
                expect_named(fd->special_names[slot], family_id);
            const bool new_alternate =
                fd->special_new_kit[slot] || fd->alternate_new_kit[slot];
            if (new_alternate &&
                std::string_view(fd->alternate_names[slot]) != "NONE")
                expect_named(fd->alternate_names[slot], family_id);
        }
    }
    // 25 starred names in the file plus Haste Self. This moves only when a
    // new special is added or removed; it also proves the family table was
    // loaded, so the loop above cannot pass by checking nothing.
    EXPECT_EQ(26, checked);
    EXPECT_NE(std::string::npos,
              text.find("* needs Game Settings -> Gameplay FX\n"
                        "  -> New Specials (on by default)."))
        << "the note naming the New Specials setting is missing";
}

// The CLASSES tab says what SHIFTER means with New Ability Select on (the
// default layout keeps a second form on SHIFTER + its own key when free,
// so every "SHIFTER:" line stays true), and names the cleric's shared Turn
// Undead once: the ABILITIES screen lists it once.
TEST(HelpSmoke, classes_help_explains_shifter_under_ability_select)
{
    const std::string text = help_testing_classes_text();
    EXPECT_NE(std::string::npos,
              text.find("SHIFTER: marks an ability's second\n"
                        "form. With New Ability Select (UI FX,\n"))
        << "the paragraph naming the setting is missing";
    EXPECT_NE(std::string::npos,
              text.find("TRAIN > ABILITIES shows and changes\n"));
    EXPECT_NE(std::string::npos,
              text.find("   SHIFTER: Turn Undead, as Raise Undead's.\n"));
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
    // 5000 ms is this suite's ceiling convention, and it is a SAFETY NET, not
    // a budget: wait_for_linesdown returns the instant the offset reaches the
    // expected value, so the happy path costs exactly what it did at 600 ms.
    // The raise lands together with the observation-driven key hold below —
    // never on its own. The old 600 ms was a real deadline on a viewer loop
    // that advances once per YIELD_SLEEP; under a loaded coverage lane one
    // iteration can outlast it, and the step then failed for machine reasons
    // rather than for a swallowed tap.
    if (!wait_for_linesdown(expected, 5000))
    {
        state->ok.store(false, std::memory_order_release);
        state->failed_step.store(step, std::memory_order_release);
        state->last_value.store(help_testing_query_linesdown(),
                                std::memory_order_release);
    }
}

// The viewer must also see the RELEASE before the next press, or a release
// and the next press merged into one pump read as "held" instead of a fresh
// edge. Same counts-not-clocks form; replaces the flat SDL_Delay(30)/(80).
static void drain_one_viewer_iteration()
{
    const unsigned long before = og::input_native::yield_count();
    for (int waited = 0;
         waited < 5000 && og::input_native::yield_count() < before + 2;
         ++waited)
        SDL_Delay(1);
}

// One tap of a seam-modeled key, released BY OBSERVATION rather than by a
// stopwatch.
//
// These keys are a STATE FLAG, not an event: if the viewer does not sample
// the flag inside the hold window there is no queued event to catch up on and
// the tap is simply invisible — which is why the old flat 40 ms hold was
// "swallowed about half the time" under the pre-edge-trigger throttle, and
// why it stayed a bet afterwards. og::input_native::yield_count() counts the
// viewer's own YIELD_SLEEP calls (src/interface/ui/help.cpp, once per loop
// iteration), so waiting for two increments proves a full iteration ran with
// the flag set. Edge triggering then fires it exactly once, and the release
// is caused by the observation instead of by a clock.
//
// This is also what makes the clamp steps honest: the last line-down at the
// bottom moves nothing, so a "wait for the offset to change" hold could never
// return — the loop-iteration count is the right oracle for both.
static void hold_for_one_viewer_iteration(
    const std::function<void(bool, bool)>& set, bool first, bool second)
{
    const unsigned long yields_before = og::input_native::yield_count();
    const int lines_before = help_testing_query_linesdown();
    set(first, second);
    // Release on whichever comes first: the offset moved (the edge fired), or
    // the viewer completed a loop iteration with the flag set (a CLAMPED step
    // moves nothing, so the iteration count is its only oracle). Taking the
    // earlier of the two keeps the hold far below the ~270 ms auto-repeat
    // first-delay even on a loaded box — a hold that outlived it would turn
    // a single-step tap into a page.
    for (int waited = 0; waited < 5000; ++waited) {
        if (help_testing_query_linesdown() != lines_before)
            break;
        if (og::input_native::yield_count() >= yields_before + 2)
            break;
        SDL_Delay(1);
    }
    set(false, false);
    drain_one_viewer_iteration();
}

static void tap_page_key(bool up, bool down)
{
    hold_for_one_viewer_iteration(
        [](bool a, bool b) { help_testing_set_page_state(a, b); }, up, down);
}

static void tap_line_key(bool up, bool down)
{
    hold_for_one_viewer_iteration(
        [](bool a, bool b) { help_testing_set_line_key_state(a, b); }, up, down);
}

static void tap_jump_key(bool home_down, bool end_down)
{
    hold_for_one_viewer_iteration(
        [](bool a, bool b) { help_testing_set_jump_key_state(a, b); },
        home_down, end_down);
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
    drain_one_viewer_iteration();
    tap_page_key(true, false);   probe_step(state, 4, 103);
    tap_page_key(true, false);   probe_step(state, 5, 0);
    // The TOP clamp of the line-up key, sandwiched like the bottom clamp
    // below: at 0 a line-up stays put, so the line-down after it lands on 8.
    // Without the clamp the pair would read -8 then 0.
    tap_line_key(true, false);   // already at the top: clamped, not -8
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
    // The UP arrow's clamp first: at 0 a click on UP stays put (drained
    // like a clamped key tap), so the DOWN click after it lands on 8.
    // Without the clamp the pair would read -8 then 0.
    drain_one_viewer_iteration();
    inject_click(up_cx, up_cy, 50);
    drain_one_viewer_iteration();
    drain_one_viewer_iteration();
    inject_click(down_cx, down_cy, 50);
    probe_step(state, 10, 8);

    // Collapsed tap (down+up inside one event pump — the web-touch shape):
    // caught via the pending-click queue, still a scroll, still no dismiss.
    drain_one_viewer_iteration();
    inject_mouse_down(down_cx, down_cy);
    inject_mouse_up(down_cx, down_cy);
    probe_step(state, 11, 16);

    drain_one_viewer_iteration();
    inject_click(up_cx, up_cy, 50);
    probe_step(state, 12, 8);

    // Track click below the thumb pages down: at linesdown=8 the thumb
    // spans y=55..78, so y=110 is below it -> +105.
    drain_one_viewer_iteration();
    inject_click(layout.track.x + layout.track.w / 2, 110, 50);
    probe_step(state, 13, 113);

    // Track click above the thumb pages back up: at linesdown=113 the thumb
    // spans y=77..100, so y=60 is above it -> -105.
    drain_one_viewer_iteration();
    inject_click(layout.track.x + layout.track.w / 2, 60, 50);
    probe_step(state, 14, 8);

    // The BOTTOM clamp, sandwiched by a step that DOES move so "nothing
    // happened" cannot pass for "clamped": at the last line one more
    // line-down must stay put, and the line-up after it must land on 200.
    // Without the clamp the pair would read 216 then 208.
    tap_jump_key(false, true);   probe_step(state, 15, 208);
    tap_line_key(false, true);   // already at the end: clamped, not 216
    tap_line_key(true, false);   probe_step(state, 16, 200);

    // The DOWN arrow's clamp: at the end a click on DOWN stays put, so the
    // UP click after it lands on 200 (216 then 208 without the clamp).
    tap_jump_key(false, true);   probe_step(state, 17, 208);
    drain_one_viewer_iteration();
    inject_click(down_cx, down_cy, 50);
    drain_one_viewer_iteration();
    drain_one_viewer_iteration();
    inject_click(up_cx, up_cy, 50);
    probe_step(state, 18, 200);

    // A track page below the thumb clamps at the end: from 113 (thumb
    // y=77..100, the step-13 geometry) a page (+105) lands on 208, not 218;
    // the line-up after it proves the offset is live.
    tap_jump_key(true, false);   probe_step(state, 19, 0);
    tap_page_key(false, true);   probe_step(state, 20, 105);
    tap_line_key(false, true);   probe_step(state, 21, 113);
    drain_one_viewer_iteration();
    inject_click(layout.track.x + layout.track.w / 2, 110, 50);
    probe_step(state, 22, 208);
    tap_line_key(true, false);   probe_step(state, 23, 200);

    // A track page above the thumb clamps at the top: from 97 (thumb
    // around y=73..96, between the step-12 and step-13 geometries) a page
    // (-105) lands on 0, not -8; the line-down after it proves the offset is
    // live. y=60 is the step-14 click point, clear of the UP arrow's padded
    // rect (a click there would take the arrow's own clamp instead).
    tap_jump_key(true, false);   probe_step(state, 24, 0);
    tap_page_key(false, true);   probe_step(state, 25, 105);
    tap_line_key(true, false);   probe_step(state, 26, 97);
    drain_one_viewer_iteration();
    inject_click(layout.track.x + layout.track.w / 2, 60, 50);
    probe_step(state, 27, 0);
    tap_line_key(false, true);   probe_step(state, 28, 8);

    // A tap in the text area still dismisses (the wasm-touch e2e contract:
    // (160,100) must keep working).
    drain_one_viewer_iteration();
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

// ---- Stills for the pull request: the CLASSES tab --------------------------
//
// Freezes the HELP screen on its CLASSES tab twice: page 1, with the note
// that the starred specials need the New Specials setting, and the page that
// holds the GHOST entry. With OG_FX_CAPTURE_DIR set the two frames are
// written there (help_stills/help_classes_p1.ppm, help_classes_ghost.ppm);
// without it they are only checked.
#include "test_frame_capture.h"
#include "../../src/interface/ui/picker_sdl_defs.h"

namespace {

struct ClassesStillArgs
{
    const char* dir = nullptr;
    int ghost_page = -1;
    std::atomic<int> tab{-1};
    std::atomic<int> page_at_first{-1};
    std::atomic<int> page_at_ghost{-1};
};

// Parks the pointer on the text, off every button, so the last click's
// highlight (on the tab, on NEXT) does not show in a still.
void park_pointer_on_text()
{
    const auto [x, y] = ui_canvas_to_window(160.0f, 90.0f);
    inject_mouse_motion(static_cast<int>(x), static_cast<int>(y));
}

int classes_still_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* a = static_cast<ClassesStillArgs*>(data);
    if (wait_for_interactable("help_tab_classes", 5000))
    {
        wait_for_menu_frames(2);
        interact("help_tab_classes");
        a->tab.store(settle_help_tab(1, 2000), std::memory_order_release);
        park_pointer_on_text();
        wait_for_menu_frames(2);
        a->page_at_first.store(help_testing_query_help_page(),
                               std::memory_order_release);
        capture_presented_frame("help_classes_p1", a->dir);

        // One NEXT per page, each proven by the page the screen reports and
        // pressed again (on a settled frame) only while the page has not
        // moved.
        for (int page = 1; page <= a->ghost_page; ++page)
        {
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                wait_for_menu_frames(2);
                if (help_testing_query_help_page() >= page)
                    break;
                interact("help_page_next");
                if (settle_help_page(page, 1000) == page)
                    break;
            }
            if (help_testing_query_help_page() != page)
                break;
        }
        park_pointer_on_text();
        wait_for_menu_frames(2);
        a->page_at_ghost.store(help_testing_query_help_page(),
                               std::memory_order_release);
        capture_presented_frame("help_classes_ghost", a->dir);
    }
    interact("help_back");
    return 0;
}

// Text pixels in the box: everything that is not the box's own background
// (its most common colour).
std::size_t help_text_ink(const std::vector<Uint8>& rgb, int x1, int x2,
                          int y1, int y2)
{
    std::vector<std::pair<std::uint32_t, std::size_t>> counts;
    std::size_t total = 0;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * 320u +
                                   static_cast<std::size_t>(x)) * 3u;
            const std::uint32_t c = (static_cast<std::uint32_t>(rgb[i]) << 16) |
                                    (static_cast<std::uint32_t>(rgb[i + 1]) << 8) |
                                    rgb[i + 2];
            bool found = false;
            for (auto& [colour, n] : counts)
                if (colour == c)
                {
                    ++n;
                    found = true;
                }
            if (!found)
                counts.emplace_back(c, 1u);
            ++total;
        }
    std::size_t most = 0;
    for (const auto& [colour, n] : counts)
        most = std::max(most, n);
    return total - most;
}

} // namespace

TEST(HelpSmoke, zz_capture_help_classes_stills)
{
    ViewportGuard guard;
    HelpTestingInputGuard input_guard;
    take_captured_frames(); // answer only for this test's stills

    // The GHOST entry's page, from the lines the screen loads (they do not
    // wrap: classes_help_lines_flow_within_frame_budget).
    const std::string text = help_testing_classes_text();
    int ghost_line = -1;
    int index = 0;
    std::size_t from = 0;
    while (from <= text.size())
    {
        const std::size_t end = text.find('\n', from);
        const std::string line =
            text.substr(from, end == std::string::npos ? std::string::npos
                                                        : end - from);
        if (line == "GHOST")
        {
            ghost_line = index;
            break;
        }
        if (end == std::string::npos)
            break;
        from = end + 1;
        ++index;
    }
    ASSERT_GE(ghost_line, 0) << "the CLASSES page has a GHOST entry";
    const int ghost_page = ghost_line / kHelpLinesPerPage;
    ASSERT_GT(ghost_page, 0) << "the GHOST entry is past page 1";

    const char* base = getenv("OG_FX_CAPTURE_DIR");
    const std::string dir =
        base != nullptr && base[0] != '\0' ? std::string(base) + "/help_stills" : "";
    ClassesStillArgs args;
    args.dir = dir.empty() ? nullptr : dir.c_str();
    args.ghost_page = ghost_page;
    SdlThreadJoinGuard thread(
        SDL_CreateThread(classes_still_injector, "help_classes_still", &args));
    ASSERT_NE(nullptr, thread.get()) << "failed to create injector thread";
    ASSERT_EQ(1, show_general_help());
    ASSERT_EQ(0, thread.join());

    EXPECT_EQ(1, args.tab.load()) << "the CLASSES tab opened";
    EXPECT_EQ(0, args.page_at_first.load()) << "the first still is page 1";
    EXPECT_EQ(ghost_page, args.page_at_ghost.load())
        << "the second still is the page holding GHOST (line " << ghost_line
        << ")";
    const std::vector<CapturedFrame> frames = take_captured_frames();
    ASSERT_EQ(2u, frames.size()) << "both stills reached their capture";
    for (const CapturedFrame& f : frames)
    {
        ASSERT_TRUE(f.captured) << f.name << " froze a presented frame";
        ASSERT_EQ(320u * 200u * 3u, f.rgb.size()) << f.name;
        EXPECT_EQ(!dir.empty(), f.written)
            << f.name << ": a file only when OG_FX_CAPTURE_DIR asks for one";
        // The text inside the frame (its border is at x 5 and 314, y 20
        // and 160).
        const std::size_t ink = help_text_ink(f.rgb, kHelpTextX, kHelpFrameRight - 2,
                                              kHelpTextTop, kHelpFrameBottom - 2);
        printf("  %s: text ink %zu\n", f.name.c_str(), ink);
        // An empty frame reads about 430 (its bevel); a page of text, thousands.
        EXPECT_GT(ink, 1500u) << f.name << ": the page has text on it";
    }
}
