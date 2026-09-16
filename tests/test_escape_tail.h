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

// The escape tail, shared by both injectors (the shape of
// tests/integration/test_pause_menu.cpp and test_overpowered_team.cpp): keep
// closing whatever screen is currently published until the MAIN thread says it
// is out of the blocking menu for good, and report the leg that gave up.
//
// It has no wall-clock bound on purpose. The main thread cannot leave
// picker_main / picker_mainmenu_loop on its own, so a tail that stopped trying
// early would GUARANTEE the wedge it exists to prevent.
//
// The happy path ends in this same loop (escape(0)), so the FINAL back of
// every flow is driven by one exit rule and never by click_until_edge: after
// picker_main returns there is no pump left to service a ladder's acknowledge
// post, and the ladder would spend 3 x kAckPostCeilingMs there.
inline int escape_to_the_main_thread(std::atomic<bool>& test_finished, int leg,
                                     const char* why)
{
    if (leg != 0)
        fprintf(stderr, "  [test] ERROR: leg %d: %s\n", leg, why);
    const auto press_and_watch = [&test_finished](const std::string& click_id,
                                                  const std::string& watched) {
        (void)interact(click_id);
        for (int tick = 0; tick < kEscapeConfirmTicks &&
                           !test_finished.load() && has_interactable(watched);
             ++tick)
            SDL_Delay(kEscapePollMs);
    };
    while (!test_finished.load()) {
        // "Closing" the main menu means going FORWARD through it: mainmenu()
        // blocks until the player picks something, and with the iteration cap
        // reached the next visit is answered with QUIT without drawing, so
        // CONTINUE -> base camp -> BACK is the way OUT.
        if (has_interactable("go"))
            press_and_watch("back", "go");
        else if (has_interactable("continue_game"))
            press_and_watch("continue_game", "continue_game");
        else
            SDL_Delay(kEscapePollMs);
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
