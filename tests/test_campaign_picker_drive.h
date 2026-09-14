#pragma once

// Driving pick_campaign from an injector thread.
//
// The campaign browser (src/interface/ui/campaign_picker.cpp) is the one menu
// surface in the suite that run_menu_screen does NOT host: it owns its own
// `while (!done)` loop, its own button array and its own pointer handoff. So
// none of the engine oracles apply to it — wait_for_menu_frames can never be
// satisfied while it is up, and its buttons are not in allbuttons, so
// interact()/has_interactable() cannot see them either. What it does publish
// are the TESTING counters below, and every helper here is a wait on one of
// them.
//
// Two binaries drive it (og_test_menu_ui through
// tests/integration/test_campaign_and_level_picker.cpp, og_test_game_core
// through tests/integration/test_campaign_sprite_uaf.cpp) and both used to
// carry their own copy of these helpers. One rule, one implementation.
//
// EVERY waiter here aborts the browser when its ceiling expires. That is not
// politeness: pick_campaign blocks the MAIN thread, and only the loop itself
// can end it, so a driver that gave up quietly would wedge the whole binary
// until the CTest ceiling. The ceiling is a cancellation bound on a loop that
// has stopped pumping, never a budget — do not raise it to buy time on an
// instrumented lane.

#include <SDL3/SDL.h>

#include <cstdint>

// campaign_picker.cpp TESTING hooks (tests/coverage_internal/
// campaign_picker_internal.inc).
void campaign_picker_testing_input_reset();
void campaign_picker_testing_abort();
void campaign_picker_testing_set_auto_accept(bool enabled);
std::uint64_t campaign_picker_testing_entered_count();
std::uint64_t campaign_picker_testing_action_count();
std::uint64_t campaign_picker_testing_frame_count();

// One ceiling for every handshake in this header: these all wait on a loop
// that is already pumping, so anything past this is a loop that has stopped.
inline constexpr Uint64 kCampaignPickerHandshakeMs = 5000;
// ENTERING the browser is not a handshake on a live loop — it is real work
// (pick_campaign enumerates every campaign, and every entry mount reinstalls
// the class packs) that an instrumented lane can stretch for seconds. Its
// ceiling is separate and generous for exactly that reason.
inline constexpr Uint64 kCampaignPickerEntryMs = 15000;
// Poll tick, not a settle (scripts/check_injector_settles.sh, tier 2).
inline constexpr Uint32 kCampaignPickerPollMs = 1;

// Wait until one of the browser's counters has moved past `baseline`.
inline bool wait_for_campaign_picker_counter(
    std::uint64_t (*counter)(), std::uint64_t baseline,
    Uint64 timeout_ms = kCampaignPickerHandshakeMs)
{
    const Uint64 started_at = SDL_GetTicks();
    while (counter() <= baseline)
    {
        if (SDL_GetTicks() - started_at >= timeout_ms)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(kCampaignPickerPollMs);
    }
    return true;
}

// The browser has entered its loop at least once.
inline bool wait_for_campaign_picker_ready()
{
    return wait_for_campaign_picker_counter(
        campaign_picker_testing_entered_count, 0);
}

// The browser opened by a door click this caller just made.
inline bool wait_for_campaign_picker_entry(std::uint64_t baseline)
{
    return wait_for_campaign_picker_counter(
        campaign_picker_testing_entered_count, baseline,
        kCampaignPickerEntryMs);
}

// Wait until no event of `event_type` is left in the SDL queue, i.e. the
// browser's get_input_events(POLL) has taken it.
inline bool wait_for_campaign_picker_event_consumed(Uint32 event_type)
{
    const Uint64 started_at = SDL_GetTicks();
    while (SDL_HasEvent(event_type))
    {
        if (SDL_GetTicks() - started_at >= kCampaignPickerHandshakeMs)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(kCampaignPickerPollMs);
    }
    return true;
}

// Wait for `n` more loop iterations to START. Paired with the waiter above it
// closes the handoff race the browser's own comments call unprovable: "the up
// is gone from the queue" only says SOME iteration took it, and that
// iteration may still be mid-body with its query_mouse ahead of it. Once the
// count read after the drain has moved on by one, the iteration that took the
// up has run to completion — saw_left_release is set, and the next press
// cannot click through.
inline bool wait_for_campaign_picker_frames(int n)
{
    const std::uint64_t target =
        campaign_picker_testing_frame_count() +
        static_cast<std::uint64_t>(n < 0 ? 0 : n);
    const Uint64 started_at = SDL_GetTicks();
    while (campaign_picker_testing_frame_count() < target)
    {
        if (SDL_GetTicks() - started_at >= kCampaignPickerHandshakeMs)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(kCampaignPickerPollMs);
    }
    return true;
}

inline bool push_campaign_picker_mouse_event(Uint32 event_type, int x, int y)
{
    SDL_Event event{};
    event.type = event_type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = event_type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    return SDL_PushEvent(&event);
}

// A click on one of the browser's own rects, acknowledged on both edges: the
// press is proven consumed by the action counter it bumps, and the release is
// proven gone from the queue before the caller may press again.
inline bool click_campaign_picker_action(int x, int y)
{
    const std::uint64_t action_before =
        campaign_picker_testing_action_count();
    if (!push_campaign_picker_mouse_event(
            SDL_EVENT_MOUSE_BUTTON_DOWN, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }

    const bool acknowledged = wait_for_campaign_picker_counter(
        campaign_picker_testing_action_count, action_before);
    if (!push_campaign_picker_mouse_event(
            SDL_EVENT_MOUSE_BUTTON_UP, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }
    const bool released =
        wait_for_campaign_picker_event_consumed(
            SDL_EVENT_MOUSE_BUTTON_UP);
    return acknowledged && released;
}
