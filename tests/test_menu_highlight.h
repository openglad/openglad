#ifndef _TEST_MENU_HIGHLIGHT_H__
#define _TEST_MENU_HIGHLIGHT_H__

// The keyboard-landing oracle, shared by the injector suites.
//
// It was written for tests/integration/test_match_setup_ui.cpp (the wizard's
// per-step landing pins) and hoisted here verbatim when the Base Camp
// docket's keyboard spine wanted the same two questions in another file:
// one implementation of the rule, not two (the maintainer principle from
// PR #245). The commentary below is the ruling text and travels with the
// code; only the file it lives in changed.

#include <openglad/interface/button.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/platform/game_session.h>

#include "test_input_helpers.h"
#include "test_interact.h"

#include <cstdint>
#include <string>

// The FX-capture nav hook (picker_input.cpp): an injector thread cannot
// send a real key — the blocking hold-and-release loops eat it.
extern int g_test_menu_nav_key;

// --- The keyboard landing, and how a CAPTURE can show it ------------------
//
// The runner keeps its highlight in a local and publishes
// menu_screen_testing_highlighted_button() next to each completed frame;
// that mirror is the oracle every assertion below reads. The pulsing RING,
// though, is only drawn once the player has actually used the keyboard
// (picker_input.cpp's menu_nav_enabled), which is why the wave-3 GAME
// capture showed no highlight at all while the mirror was already on the
// SOCCER row. A shot that has to SHOW the landing therefore wakes the ring
// first, with one nav step in a direction the landing has no link for — a
// row's LEFT is never wired (the "<" cell is on the RIGHT), and the footer
// NEXT's RIGHT is the end of its chain — and reads the id back to prove
// the wake moved nothing.
inline std::string highlighted_id()
{
    std::string id = "<unread>";
    if (!run_on_main_thread([&id] {
            AllButtonsLock lock;
            const int index =
                og::ui::menu_screen_testing_highlighted_button();
            id = "<no button at index " + std::to_string(index) + ">";
            if (index >= 0 && index < MAX_BUTTONS) {
                const vbutton* const row =
                    og::runtime::current_session
                        ->allbuttons_[static_cast<std::size_t>(index)];
                if (row != nullptr)
                    id = row->id;
            }
        }))
    {
        return "<the menu loop never read the highlight mirror>";
    }
    return id;
}

// One nav step, acknowledged: the hook is cleared by the menu thread when
// it consumes the key, and the step is not done until a frame has COMPLETED
// after that. Counts and frames, never a clock.
inline bool press_menu_nav(int key, int timeout_ms = 10000)
{
    const std::uint64_t target =
        og::ui::menu_screen_testing_completed_frames() + 1;
    if (!run_on_main_thread([key] { g_test_menu_nav_key = key; }))
        return false;
    const Uint64 deadline =
        SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    for (;;) {
        int hook = key;
        if (!run_on_main_thread([&hook] { hook = g_test_menu_nav_key; }))
            return false;
        if (hook == -1 &&
            og::ui::menu_screen_testing_completed_frames() >= target)
        {
            return true;
        }
        if (SDL_GetTicks() > deadline)
            return false;
        (void)wait_for_menu_frames(1);
    }
}

// The same step, named for what a CAPTURE uses it for: waking the ring
// with a direction the landing has no link for, so the shot shows the
// highlight without moving it.
inline bool wake_nav_highlight(int key, int timeout_ms = 10000)
{
    return press_menu_nav(key, timeout_ms);
}

// The landing, as a flow wants to talk about it: which button, and what
// that button says.
struct Landing {
    std::string id;
    std::string label;
};

inline Landing read_landing()
{
    Landing landing;
    landing.id = highlighted_id();
    landing.label = interactable_label(landing.id);
    return landing;
}

#endif // _TEST_MENU_HIGHLIGHT_H__
