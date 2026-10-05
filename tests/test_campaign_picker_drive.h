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
// Two binaries drive it (og_test_menu_light through
// tests/integration/test_campaign_and_level_picker.cpp, og_test_game_core
// through tests/integration/test_campaign_sprite_uaf.cpp) and both used to
// carry their own copy of these helpers. One rule, one implementation.
//
// EVERY waiter here aborts the browser when it gives up. That is not
// politeness: pick_campaign blocks the MAIN thread, and only the loop itself
// can end it, so a driver that gave up quietly would wedge the whole binary
// until the CTest ceiling. The ceiling is a cancellation bound on a loop that
// has stopped pumping, never a budget — do not raise it to buy time on an
// instrumented lane.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>

#include <openglad/interface/session_state.h>

// campaign_picker.cpp TESTING hooks (tests/coverage_internal/
// campaign_picker_internal.inc).
void campaign_picker_testing_input_reset();
void campaign_picker_testing_abort();
void campaign_picker_testing_set_auto_accept(bool enabled);
std::uint64_t campaign_picker_testing_entered_count();
std::uint64_t campaign_picker_testing_action_count();
std::uint64_t campaign_picker_testing_frame_count();

// One stall ceiling for every handshake in this header, and it measures ONE
// thing: how long the browser may go without STARTING a loop iteration
// (campaign_picker_testing_frame_count). Each waiter below restarts this
// clock whenever that count moves, so the number can only expire on a loop
// that has stopped pumping — a main thread parked in the kernel, a dead
// pump — and never on one that is merely slow under an instrumented lane.
// It is a cancellation bound on a dead pump, never a budget: do not raise
// it to buy time.
inline constexpr Uint64 kCampaignPickerHandshakeMs = 5000;
// ENTERING the browser is not a handshake on a live loop — it is real work
// (pick_campaign enumerates every campaign, and every entry mount reinstalls
// the class packs) that an instrumented lane can stretch for seconds, so it
// gets its own ceiling rather than the handshake's. The number is not a new
// budget: it is the ceiling this wait already carried, carried over verbatim
// from the local wait_for_counter_above that used to live in
// tests/integration/test_campaign_sprite_uaf.cpp (kTimeoutMs = 8000).
inline constexpr Uint64 kCampaignPickerEntryMs = 8000;
// The other bound, for a browser that keeps ITERATING without ever giving the
// waiter what it waits for: counted in started iterations, not clocked, so a
// slow box spends the same budget as a fast one. Each iteration polls the
// whole SDL queue, so anything the browser is going to do with an event it
// has done many times over inside this many frames.
inline constexpr std::uint64_t kCampaignPickerFrameBudget = 240;
// A press the browser took and did nothing with. The iteration that polls a
// DOWN computes every do_* from it and bumps the action counter BEFORE
// anything that can block (campaign_picker.cpp: get_input_events(POLL),
// query_mouse, the do_* rects, campaign_picker_testing_mark_action), and
// only then parks in wait_for_mouse_release while the button is held —
// acted on or not. So a dropped press does not show as frames that go on
// without an action: the loop stops starting frames until the release, the
// same face a dead pump shows. What tells the two apart is that the parked
// loop still POLLS. Once the DOWN is gone from the queue, the probe pushes
// this many marker events one after another, each only after the last was
// drained: the iteration has at most one poll left before its action
// decision (query_mouse), so a second marker drained with no action means
// the decision is made and was "nothing" — the press was MISSED.
inline constexpr int kCampaignPickerMissProbes = 2;
// Poll tick, not a settle (scripts/check_injector_settles.sh, tier 2).
inline constexpr Uint32 kCampaignPickerPollMs = 1;

// The one waiter every helper below is built on: poll `done` until it holds,
// and give up — aborting the browser, because pick_campaign blocks the main
// thread and only its own loop can end it — with a verdict that says WHICH
// bound expired: a browser that stopped producing frames (`stall_ms` without
// a new iteration) or one that kept iterating without `what` happening
// (kCampaignPickerFrameBudget iterations).
template <typename Done>
inline bool wait_for_campaign_picker(const char* what, Done done,
                                     Uint64 stall_ms = kCampaignPickerHandshakeMs)
{
    const std::uint64_t first_frame = campaign_picker_testing_frame_count();
    std::uint64_t last_frame = first_frame;
    Uint64 last_frame_at = SDL_GetTicks();
    while (!done())
    {
        const std::uint64_t frame = campaign_picker_testing_frame_count();
        if (frame != last_frame)
        {
            last_frame = frame;
            last_frame_at = SDL_GetTicks();
        }
        if (frame - first_frame >= kCampaignPickerFrameBudget)
        {
            fprintf(stderr,
                    "  [picker-drive] %s did not happen within %llu browser "
                    "frames\n",
                    what, static_cast<unsigned long long>(
                              kCampaignPickerFrameBudget));
            campaign_picker_testing_abort();
            return false;
        }
        if (SDL_GetTicks() - last_frame_at >= stall_ms)
        {
            fprintf(stderr,
                    "  [picker-drive] %s did not happen; the browser produced "
                    "no frame for %llu ms\n",
                    what, static_cast<unsigned long long>(stall_ms));
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(kCampaignPickerPollMs);
    }
    return true;
}

// Wait until one of the browser's counters has moved past `baseline`.
inline bool wait_for_campaign_picker_counter(
    std::uint64_t (*counter)(), std::uint64_t baseline,
    Uint64 timeout_ms = kCampaignPickerHandshakeMs)
{
    return wait_for_campaign_picker(
        "the browser counter moving", [&] { return counter() > baseline; },
        timeout_ms);
}

// The browser has entered its loop at least once.
inline bool wait_for_campaign_picker_ready()
{
    return wait_for_campaign_picker(
        "the browser entering its loop",
        [] { return campaign_picker_testing_entered_count() > 0; });
}

// The browser opened by a door click this caller just made.
inline bool wait_for_campaign_picker_entry(std::uint64_t baseline)
{
    return wait_for_campaign_picker(
        "the browser entering its loop",
        [baseline] {
            return campaign_picker_testing_entered_count() > baseline;
        },
        kCampaignPickerEntryMs);
}

// Wait until no event of `event_type` is left in the SDL queue, i.e. the
// browser's get_input_events(POLL) has taken it.
inline bool wait_for_campaign_picker_event_consumed(Uint32 event_type)
{
    return wait_for_campaign_picker(
        "the browser taking the queued event",
        [event_type] { return !SDL_HasEvent(event_type); });
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
    return wait_for_campaign_picker(
        "the browser starting its next frames", [target] {
            return campaign_picker_testing_frame_count() >= target;
        });
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
//
// A press the browser took WITHOUT acting on it is not waited out: the
// iteration that polls the DOWN decides on it before it can block (see
// kCampaignPickerMissProbes), so once the DOWN has left the queue and the
// probes have been polled behind it with no action, the click is reported
// MISSED — with the frame count, the pushed coordinates and the viewport
// the input layer mapped them through, which is what decides whether a
// window point lands inside the active canvas at all
// (window_point_in_active_canvas) — and the browser is aborted.
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

    bool drained = false;
    bool missed = false;
    int probes_drained = 0;
    bool probe_pending = false;
    std::uint64_t drained_at_frame = 0;
    std::uint64_t missed_at_frame = 0;
    const bool waited = wait_for_campaign_picker(
        "the browser acknowledging the click", [&] {
            if (campaign_picker_testing_action_count() > action_before)
                return true;
            if (!drained)
            {
                if (SDL_HasEvent(SDL_EVENT_MOUSE_BUTTON_DOWN))
                    return false;
                drained = true;
                drained_at_frame = campaign_picker_testing_frame_count();
            }
            if (probe_pending)
            {
                if (SDL_HasEvent(SDL_EVENT_USER))
                    return false;
                probe_pending = false;
                ++probes_drained;
            }
            if (probes_drained >= kCampaignPickerMissProbes)
            {
                // Read the action AFTER the last probe drained: the poll
                // that took it comes after the iteration's action decision.
                if (campaign_picker_testing_action_count() > action_before)
                    return true;
                missed = true;
                missed_at_frame = campaign_picker_testing_frame_count();
                return true;
            }
            SDL_Event probe{};
            probe.type = SDL_EVENT_USER;
            probe_pending = SDL_PushEvent(&probe);
            return false;
        });
    const bool acknowledged = waited && !missed;
    if (missed)
    {
        const auto* session = og::runtime::current_session;
        fprintf(stderr,
                "  [picker-drive] the click at window (%d,%d) was MISSED: the "
                "browser took the press at frame %llu and polled %d probes "
                "after it with no action (%llu frames later; window %gx%g, "
                "viewport offset (%g,%g) size %gx%g)\n",
                x, y, static_cast<unsigned long long>(drained_at_frame),
                kCampaignPickerMissProbes,
                static_cast<unsigned long long>(missed_at_frame -
                                                drained_at_frame),
                session ? static_cast<double>(session->window_w_) : -1.0,
                session ? static_cast<double>(session->window_h_) : -1.0,
                session ? static_cast<double>(session->viewport_offset_x_)
                        : -1.0,
                session ? static_cast<double>(session->viewport_offset_y_)
                        : -1.0,
                session ? static_cast<double>(session->viewport_w_) : -1.0,
                session ? static_cast<double>(session->viewport_h_) : -1.0);
        campaign_picker_testing_abort();
    }
    if (!push_campaign_picker_mouse_event(
            SDL_EVENT_MOUSE_BUTTON_UP, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }
    // An unacknowledged press has already aborted the browser: there is no
    // loop left to take the release, and waiting for one would only stack a
    // second ceiling on the verdict above.
    if (!acknowledged)
        return false;
    return wait_for_campaign_picker_event_consumed(
        SDL_EVENT_MOUSE_BUTTON_UP);
}

// The browser's input hygiene, on both edges. It owns the main thread and
// its own pointer handoff, so a mouse-button event left in the queue by the
// screen BEFORE it is a click it will consume as its own, and one left
// behind by it is a click the next screen will. Every driver of the browser
// brackets its visit with this.
struct CampaignPickerInputGuard
{
    CampaignPickerInputGuard()
    {
        campaign_picker_testing_input_reset();
        if (SDL_HasEvents(
                SDL_EVENT_MOUSE_BUTTON_DOWN,
                SDL_EVENT_MOUSE_BUTTON_UP))
        {
            ADD_FAILURE()
                << "campaign picker inherited stale mouse-button events";
        }
    }

    ~CampaignPickerInputGuard()
    {
        campaign_picker_testing_input_reset();
        SDL_FlushEvents(
            SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP);
    }

    void reset()
    {
        if (SDL_HasEvents(
                SDL_EVENT_MOUSE_BUTTON_DOWN,
                SDL_EVENT_MOUSE_BUTTON_UP))
        {
            ADD_FAILURE()
                << "previous campaign picker left mouse-button events queued";
        }
        campaign_picker_testing_input_reset();
    }
};

// Auto-accept is the suite's default: a browser that is only passed
// THROUGH takes the first frame's entry and returns. A driver that wants
// to LOOK at the browser turns it off for the visit and aborts the loop
// itself on the way out — pick_campaign blocks the main thread, so only
// the loop can end it.
struct CampaignPickerAutoAcceptOff
{
    CampaignPickerAutoAcceptOff()
    {
        campaign_picker_testing_set_auto_accept(false);
    }
    ~CampaignPickerAutoAcceptOff()
    {
        campaign_picker_testing_set_auto_accept(true);
    }
};
