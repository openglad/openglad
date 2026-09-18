#ifndef _TEST_ESCAPE_TAIL_H__
#define _TEST_ESCAPE_TAIL_H__

// The injector escape tail, shared by the picker-driven suites.
//
// It was written for tests/integration/test_back_to_mainmenu.cpp and hoisted
// here verbatim when a second file needed the same rules: one implementation
// of the rule, not three (the maintainer principle from PR #245). The
// commentary below is the ruling text and travels with the code; nothing but
// the linkage (`inline`) changed in the move. The per-file cancellation
// ceilings did NOT move -- they are each file's leg budgets, not this rule.
//
// Why it exists: picker_main and picker_mainmenu_loop block on the MAIN
// thread and only a click can make them return, so an injector that gave up
// silently took the whole binary to the 420 s CTest ceiling.
// BackToMainmenu.a_leg_that_gives_up_frees_the_main_thread is the regression
// for that shape.

#include "test_interact.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <span>
#include <string>

// Poll ticks, not settles. Every screen transition in the consuming files
// settles on wait_for_menu_frames(2) -- the main menu and base camp are both
// run_menu_screen-hosted (menu_screen_specs.cpp) -- and every click is proven
// consumed by the screen it opens. These are the ticks of wait-on-condition
// loops, which is why they are spelled with "poll"
// (scripts/check_injector_settles.sh, tier 2).
inline constexpr Uint32 kEscapePollMs = 100;
// One press per screen, not one per tick: after each press the tail watches
// for the screen it just acted on to go away, and only then presses again.
// A blind re-press is a RAW COORDINATE click on whatever came up in the
// meantime -- base camp's roster band sits where CONTINUE was -- which is how
// an escape tail opens a submenu it does not know how to leave. Bounded, so a
// press that evaporated is still re-sent.
inline constexpr int kEscapeConfirmTicks = 20;

// A screen with no exit this tail knows would otherwise be a silent
// spin. Every ~5 s, name what IS on screen, so the log says which
// screen stranded the flow instead of going quiet until the group
// timeout.
//
// 50 idle polls of kEscapePollMs: ~5 s of a tail that finds no door it knows.
// It never fires in a tail that is closing screens, and adds no delay.
inline constexpr int kEscapeSpinReportLaps = 50;

// A door is (the id whose presence says a screen is up, the id to press to
// close it). A table is in PRECEDENCE order and the tail presses only the
// FIRST door whose watched id is published -- a screen that publishes two of
// them is one screen, not two, and pressing both would leave the flow
// somewhere neither arm names.
struct EscapeDoor
{
    const char* watched;
    const char* press;
};

// "Closing" the main menu means going FORWARD through it: mainmenu()
// blocks until the player picks something, and with the iteration cap
// reached the next visit is answered with QUIT without drawing, so
// CONTINUE -> base camp -> BACK is the way OUT.
inline constexpr EscapeDoor kPickerEscapeDoors[] = {
    {"go", "back"},
    {"continue_game", "continue_game"},
};

// A blocking menu driven from the MAIN thread (create_team_menu, picker_main)
// returns only when a click walks it out. An injector that presses BACK once
// and then returns therefore hands the process a permanent hang the moment
// that one press evaporates on a starved frame: the injector thread is gone,
// nobody presses again, SDL_WaitThread has already been satisfied, and the
// main thread sits in the menu loop until the group's ctest cap kills the
// binary. og_test_lineup did exactly that once, reaching the 900 s cap inside
// basecamp_chip_cycles_own_row_networked_and_resyncs with the log ending on
// "[interact] clicking 'back' at game(30,187)".
//
// The tail must NOT be bounded by a clock: a wall-clock ceiling only relocates
// the hang (the tail stops clicking, the menu never leaves). It presses until
// the MAIN thread signals that the menu call returned, which the caller does
// between the menu call and SDL_WaitThread -- exactly the window this loop
// covers. Counted, never clocked.
inline std::atomic<int> g_escape_tail_presses{0};
// TESTING-only fault injection: make the next N escape presses evaporate the
// way a starved frame does. This is the fault the tail exists for, so the
// teeth test arms it rather than waiting for the box to supply it.
inline std::atomic<int> g_escape_tail_drops{0};

// The escape tail, shared by every injector that can leave the MAIN thread
// blocked in a menu (the picker flows, the pause-menu flows, the founding
// flow, the SET CAMPAIGN flow, the LINEUP flows): keep closing whatever
// screen is currently published until the MAIN thread says it is out of the
// blocking menu for good, and report the leg that gave up.
//
// It has no wall-clock bound on purpose. The main thread cannot leave
// picker_main / picker_mainmenu_loop on its own, so a tail that stopped trying
// early would GUARANTEE the wedge it exists to prevent.
//
// The happy path ends in this same loop (escape(0)), so the FINAL back of
// every flow is driven by one exit rule and never by click_until_edge: after
// picker_main returns there is no pump left to service a ladder's acknowledge
// post, and the ladder would spend 3 x kAckPostCeilingMs there.
//
// The one flow that returns 0 directly instead is test_pause_menu.cpp: its
// main thread consumes the injector's last click (RESUME / QUIT) as a
// specific action and then plays on, so a tail pressing "whatever is up" in
// that window would race the frame that consumes it. Its give-ups still route
// here.
//
// `hold_lap` is a lap the tail must spend polling instead of pressing because
// there is nothing to close right now -- a real match is running
// (test_overpowered_team.cpp's g_test_in_game) or a screen that is NOT
// run_menu_screen-hosted owns the main thread and its buttons are not in
// allbuttons (test_campaign_sprite_uaf.cpp's browser, which the predicate
// tells to abort); the predicate is called once per lap BEFORE any door is
// consulted.
inline int escape_to_the_main_thread(std::atomic<bool>& test_finished, int leg,
                                     const char* why,
                                     std::span<const EscapeDoor> doors =
                                         kPickerEscapeDoors,
                                     const std::function<bool()>& hold_lap = {})
{
    if (leg != 0)
        fprintf(stderr, "  [test] ERROR: leg %d: %s\n", leg, why);
    const auto press_and_watch = [&test_finished](const std::string& click_id,
                                                  const std::string& watched) {
        if (g_escape_tail_drops.load() > 0) {
            g_escape_tail_drops.fetch_sub(1);
            fprintf(stderr, "  [escape] dropping the '%s' press (injected)\n",
                    click_id.c_str());
        } else {
            g_escape_tail_presses.fetch_add(1);
            (void)interact(click_id);
        }
        for (int tick = 0; tick < kEscapeConfirmTicks &&
                           !test_finished.load() && has_interactable(watched);
             ++tick)
            SDL_Delay(kEscapePollMs);
    };
    int idle_laps = 0;
    while (!test_finished.load()) {
        if (hold_lap && hold_lap()) {
            SDL_Delay(kEscapePollMs);
            continue;
        }
        bool pressed = false;
        for (const EscapeDoor& door : doors) {
            if (has_interactable(door.watched)) {
                press_and_watch(door.press, door.watched);
                pressed = true;
                break;
            }
        }
        if (!pressed) {
            if (++idle_laps % kEscapeSpinReportLaps == 0) {
                std::string ids;
                for (const std::string& id : get_button_ids())
                    ids += id + " ";
                fprintf(stderr,
                        "  [escape] tail still spinning after %d idle laps; "
                        "live ids: %s\n",
                        idle_laps, ids.c_str());
            }
            SDL_Delay(kEscapePollMs);
        }
    }
    return leg;
}

// The post-join half of the same rule, called by the MAIN thread immediately
// after SDL_WaitThread on an injector that ran the tail.
//
// The tail's last press may have been HALF consumed: picker_main returned
// between its DOWN and its UP. Poll that release through the engine --
// reset_mouse_click_tracking() consumes the queued events before it
// re-baselines -- instead of flushing it. A FLUSHED release leaves
// mouse_state.left stuck true, so the next flow's first press makes no
// up->down edge and simply evaporates (measured: the legacy-loop CONTINUE
// burned its whole 10 s edge wait and landed only on the ladder's second
// attempt). What the flush is still for is the rest: motion and any press
// pushed after the menu stopped reading.
inline void escape_tail_join_hygiene()
{
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
}

#endif  // _TEST_ESCAPE_TAIL_H__
