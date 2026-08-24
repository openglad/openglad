// #237 fade ownership, the end-to-end pin: BEGIN NEW GAME -> name entry
// ACCEPT -> campaign select ACCEPT -> intro dismiss -> Base Camp, every leg
// counted separately. Two of those legs (campaign select -> intro, intro ->
// Base Camp) are the ones that shipped as hard cuts twice: a screen::clear()
// between the outgoing screen's last present and its fade-out turned the
// fade black-to-black. Under the ownership rule (docs/menu-engine.md,
// "Drawing and transitions") every screen fades itself out at its own exit,
// and the video layer's TESTING invariants flag any fade that reads a frame
// the window never showed — integration_main's listener turns those into a
// failure of THIS test, so the per-leg counts below are only half the pin.
//
// Under TESTING every fadeblack that runs takes FadeBetween's test-mode
// branch, which traces exactly one "video" FadeBetween line — so counting
// those lines between the legs' own trace markers counts fades per leg.
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <mutex>
#include <string>
#include <vector>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/save_data.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>

namespace
{

PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++)
    {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

bool is_fade(const TraceEntry& entry)
{
    return entry.category == "video" &&
        entry.message.find("FadeBetween") != std::string::npos;
}

bool is_marker(const TraceEntry& entry, const char* category,
               const char* needle)
{
    return entry.category == category &&
        entry.message.find(needle) != std::string::npos;
}

int count_fades(const std::vector<TraceEntry>& entries)
{
    int fades = 0;
    for (const TraceEntry& entry : entries)
        if (is_fade(entry))
            ++fades;
    return fades;
}

// Fades strictly between the first occurrence of marker A and the first
// occurrence of marker B after it. -1 when either marker is missing (the leg
// never ran), so a vanished leg fails loudly instead of counting 0.
int fades_between(const std::vector<TraceEntry>& entries,
                  const char* cat_a, const char* needle_a,
                  const char* cat_b, const char* needle_b)
{
    std::size_t i = 0;
    for (; i < entries.size(); ++i)
        if (is_marker(entries[i], cat_a, needle_a))
            break;
    if (i == entries.size())
        return -1;
    int fades = 0;
    for (++i; i < entries.size(); ++i)
    {
        if (is_marker(entries[i], cat_b, needle_b))
            return fades;
        if (is_fade(entries[i]))
            ++fades;
    }
    return -1;
}

// Fades after the first occurrence of a marker, to the end of the copy.
int fades_after(const std::vector<TraceEntry>& entries, const char* category,
                const char* needle)
{
    std::size_t i = 0;
    for (; i < entries.size(); ++i)
        if (is_marker(entries[i], category, needle))
            break;
    if (i == entries.size())
        return -1;
    int fades = 0;
    for (++i; i < entries.size(); ++i)
        if (is_fade(entries[i]))
            ++fades;
    return fades;
}

struct FlowState {
    bool started = false;
    bool finished = false;
    bool saw_name_entry = false;
    bool saw_base_camp = false;
    // Leg 1, measured live: main menu -> name entry.
    int fades_added_by_name_entry = -1;
    // The trace buffer as of Base Camp settled, before BACK: legs 2-4 are
    // read off this copy by marker on the main thread.
    std::vector<TraceEntry> traces_at_base_camp;
    int fades_at_new_game_click = -1;
};

bool wait_for_team_menu(int timeout_ms)
{
    int elapsed = 0;
    constexpr int poll_interval = 50;
    while (elapsed < timeout_ms)
    {
        if (has_interactable("hire_troops") && has_interactable("networking"))
            return true;
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    return false;
}

int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    return count_fades(g_trace_buffer);
}

int new_game_full_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("begin_new_game", 5000))
        return 1;
    SDL_Delay(750);
    state->fades_at_new_game_click = count_fade_between_traces();
    interact("begin_new_game");

    if (!wait_for_interactable("company_name_accept", 5000))
        return 2;
    state->saw_name_entry = true;
    SDL_Delay(750); // menu-entry settle
    state->fades_added_by_name_entry =
        count_fade_between_traces() - state->fades_at_new_game_click;
    interact("company_name_accept");

    // Campaign select auto-accepts and the intro auto-dismisses, each one
    // presented frame in; Base Camp follows with no further input.
    if (!wait_for_team_menu(20000))
        return 3;
    state->saw_base_camp = true;
    SDL_Delay(750); // Base Camp's entry settle
    {
        std::lock_guard<std::mutex> lock(g_trace_mutex);
        state->traces_at_base_camp = g_trace_buffer;
    }

    SDL_Delay(300);
    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(FadeOwnership, new_game_flow_fades_every_leg_symmetrically)
{
    trace_clear();
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(new_game_full_flow_injector,
                                          "new_game_full_flow", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);

    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_EQ(0, thread_result) << "the injector stalled at stage " << thread_result;
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_name_entry);
    ASSERT_TRUE(state.saw_base_camp)
        << "the flow must reach Base Camp through the real campaign select "
           "and intro";

    const std::vector<TraceEntry>& traces = state.traces_at_base_camp;

    // The legs ran for real: their markers are in the buffer.
    ASSERT_TRUE(trace_contains("name_entry", "accept"));
    ASSERT_TRUE(trace_contains("campaign_picker", "first frame presented"))
        << "campaign select must present a real frame under TESTING";
    ASSERT_TRUE(trace_contains("campaign_picker", "auto-accept gladiator"))
        << "the un-driven browser accepts the current campaign";
    ASSERT_TRUE(trace_contains("help", "scroller presented"))
        << "the campaign intro must present a real frame under TESTING";
    ASSERT_TRUE(trace_contains("help", "scroller auto-dismissed"));

    // Leg 1: main menu -> name entry. The main menu's exit fades out, name
    // entry fades in.
    EXPECT_EQ(2, state.fades_added_by_name_entry)
        << "leg 1 (main menu -> name entry): one fade-out + one fade-in";
    // Leg 2: name entry ACCEPT -> campaign select's first frame. Name entry's
    // exit fades out; the browser's entry fade-out is a no-op on the black
    // window, its first frame fades in.
    EXPECT_EQ(2, fades_between(traces, "name_entry", "accept",
                               "campaign_picker", "first frame presented"))
        << "leg 2 (name entry -> campaign select): one fade-out + one fade-in";
    // Leg 3: campaign select ACCEPT -> the intro's first frame. The browser
    // fades itself out; the intro fades in over black.
    EXPECT_EQ(2, fades_between(traces, "campaign_picker", "auto-accept",
                               "help", "scroller presented"))
        << "leg 3 (campaign select -> intro): the reported hard cut — the "
           "browser's own exit fade-out, then the intro's fade-in";
    // Leg 4: intro dismiss -> Base Camp. The intro fades itself out; Base
    // Camp's entry fade-out is a no-op on the black window, its cold frame
    // fades in; every later Base Camp frame presents plainly.
    EXPECT_EQ(2, fades_after(traces, "help", "scroller auto-dismissed"))
        << "leg 4 (intro -> Base Camp): the reported hard cut — the intro's "
           "own exit fade-out, then Base Camp's fade-in";
    // The whole door, main menu click to Base Camp: four symmetric legs.
    EXPECT_EQ(8, count_fades(traces) - state.fades_at_new_game_click)
        << "four legs, two fades each";

    // The other half of the pin: no fade in this flow read a frame the
    // window never showed, and no fade-in ran over a non-black window. The
    // listener fails this test on any violation; asserted here too so the
    // count above can never pass on a black-to-black fade.
    EXPECT_EQ(0, og::video_testing::g_fade_violations.load())
        << "fade-ownership invariant violated: "
        << (og::video_testing::fade_violation_messages().empty()
                ? std::string()
                : og::video_testing::fade_violation_messages().front());
}
