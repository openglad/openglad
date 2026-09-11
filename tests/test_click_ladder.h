#ifndef _TEST_CLICK_LADDER_H__
#define _TEST_CLICK_LADDER_H__

// The acknowledged, bounded-retry click ladder, shared by the injector
// suites. It was written for tests/integration/test_campaign_zone_ui.cpp and
// hoisted here verbatim when a second file (test_ctf_ui.cpp) needed the same
// rules: one implementation of the rule, not three (the maintainer principle
// from PR #245). The commentary below is the ruling text and travels with the
// code; only the counter prefix (g_zone_* -> g_click_ladder_*), the log tag
// and click_until_edge's trace CATEGORY (a parameter now, "zone" by default)
// changed in the move.
//
// The three entry points:
//   acknowledge_press          — post the pointer-handoff reset, bounded.
//   click_and_acknowledge_trace — press until a NAMED trace arrives.
//   click_until_edge           — press until a NAMED on-screen edge arrives.
//
// The TESTING-only g_click_ladder_* counters are both the fault injectors the
// teeth tests arm and the counts those tests pin. They are test-local globals;
// nothing in src/ knows about them.

#include <openglad/core/test_trace.h>

#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <functional>
#include <mutex>
#include <string>

inline int count_trace_containing(const char* category, const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int count = 0;
    for (const TraceEntry& entry : g_trace_buffer) {
        if (entry.category == category &&
            entry.message.find(substring) != std::string::npos)
            ++count;
    }
    return count;
}

// Roster cyclers do not always change their face (the DEPLOY column is an X
// both ways), but every accepted or refused transition emits a named trace.
// Start from a pointer baseline, wait for a NEW matching trace, then reset on
// the menu thread after the click was observed. That reset is the
// acknowledgement that the full press/release is consumed before another
// press is allowed. This covers repeated traces too: the final BURDEN -> WAR
// assignment waits for count 2, not stale count 1.
//
// Bounded ladder on top of that: a press that evaporated on a starved frame
// left NO new trace, so re-pressing it cannot double-cycle anything. A press
// that DID register (the trace arrived) is never repeated, whatever else went
// wrong — that is the toggle-safety rule this retry turns on.
inline int g_click_ladder_trace_click_retries = 0;
// TESTING-only fault injection, shared with click_until_edge below: make the
// next N presses evaporate the way a starved frame does.
inline int g_click_ladder_click_drops = 0;

// The other half of the same starvation: the press DID register (its trace
// arrived) but the reset posted back to the menu thread was cancelled unrun,
// because the thread pumped nothing inside the ceiling — the deploy autosave
// under load is the observed case. Every wait that follows then runs against
// a pointer baseline nobody cleared, so the next press evaporates too. A
// reset never presses anything, so re-posting it is safe by the same
// toggle-safety rule the press ladder states: retry the post, never the
// press.
//
// Counted, never clocked: `zone_ack_post_retries` is the number of
// acknowledge posts that had to be re-sent. A thread that pumps nothing for
// the WHOLE ladder is not charged to the click at all — see the last resort
// at the bottom of acknowledge_press.
inline int g_click_ladder_ack_post_retries = 0;
// TESTING-only fault injection for that half: make the next N acknowledge
// posts come back cancelled the way a starved menu thread cancels them.
inline int g_click_ladder_ack_drops = 0;

inline bool acknowledge_press(int timeout_ms, int attempts = 3,
                       bool injectable = false)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (injectable && g_click_ladder_ack_drops > 0) {
            --g_click_ladder_ack_drops;
            fprintf(stderr,
                    "  [ladder] cancelling the acknowledge post (injected)\n");
        } else if (run_on_main_thread([] { reset_mouse_click_tracking(); },
                                      timeout_ms)) {
            return true;
        } else {
            fprintf(stderr,
                    "  [interact] acknowledge post did not run within %d ms\n",
                    timeout_ms);
        }
        if (attempt + 1 < attempts)
            ++g_click_ladder_ack_post_retries;
    }
    // Last resort, for the stall that outlives the whole ladder: the menu
    // thread pumped NOTHING for the full budget (a >15 s gap has been seen
    // on this box, with the flow otherwise healthy — the press had already
    // traced and the toggle had already autosaved). The reset captures
    // nothing, so unlike a lambda over the caller's locals it does not need
    // the cancelling wait at all: post it and leave it queued. The pump runs
    // at frame top BEFORE the event poll (menu_screen_runner.cpp), so a
    // queued reset still lands ahead of the next press whenever the thread
    // comes back — the ordering this acknowledgement exists for is a
    // property of the queue, not of the observer's clock. Report the stall,
    // never charge it to the click.
    fprintf(stderr,
            "  [interact] the menu thread pumped nothing for %d x %d ms; "
            "the reset stays queued for the next frame\n",
            attempts, timeout_ms);
    (void)post_main_thread_task([] { reset_mouse_click_tracking(); });
    return true;
}

inline bool click_and_acknowledge_trace(const std::string& id, const char* category,
                                 const char* trace_substring,
                                 bool waits_for_autosave = true,
                                 int timeout_ms = 5000, int attempts = 3)
{
    const int before = count_trace_containing(category, trace_substring);
    const int saves_before = trace_count("save");
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (!run_on_main_thread([] { reset_mouse_click_tracking(); },
                                timeout_ms))
            return false;
        if (g_click_ladder_click_drops > 0) {
            --g_click_ladder_click_drops;
            fprintf(stderr, "  [ladder] dropping the press on '%s' (injected)\n",
                    id.c_str());
        } else {
            (void)interact(id);
        }
        int elapsed = 0;
        while (elapsed < timeout_ms &&
               (count_trace_containing(category, trace_substring) <= before ||
                (waits_for_autosave && trace_count("save") <= saves_before))) {
            SDL_Delay(50);
            elapsed += 50;
        }
        const bool traced =
            count_trace_containing(category, trace_substring) > before;
        if (!traced) {
            fprintf(stderr,
                    "  [interact] TIMEOUT waiting for new %s trace '%s'\n",
                    category, trace_substring);
            if (attempt + 1 < attempts) {
                ++g_click_ladder_trace_click_retries;
                continue;  // nothing registered: the press may be re-sent
            }
            (void)run_on_main_thread([] { reset_mouse_click_tracking(); },
                                     timeout_ms);
            return false;
        }
        const bool autosaved =
            !waits_for_autosave || trace_count("save") > saves_before;
        if (!autosaved) {
            fprintf(stderr,
                    "  [interact] TIMEOUT waiting for cycler autosave\n");
        }
        const bool acknowledged =
            acknowledge_press(timeout_ms, attempts, /*injectable=*/true);
        return autosaved && acknowledged;
    }
    return false;
}

// --- The acknowledged, bounded-retry click the injector flows use ---------
//
// A bare interact() sends one press/release pair and never looks back. Under
// a starved menu thread one of the two can land on the wrong side of the
// engine's pointer-handoff reset (menu_screen_runner.cpp) and the click
// simply evaporates; nothing retries, and every wait that follows then
// expires against a screen the flow never entered — one dropped press turns
// a 5.7 s capture into 98 s of stacked ceilings. The idiom that survives it
// is the file's own click_and_acknowledge_trace: baseline the pointer on the
// menu thread, press, wait for a NAMED edge, acknowledge. Wrapped in a
// bounded ladder, a dropped press costs one attempt instead of the cascade.
//
// The same toggle-safety rule click_and_acknowledge_trace states, applied
// to the press half here: a press may only be RE-SENT when there is
// evidence it did not land. A door and a cycler row are pressed the same
// way, but a cycler is not idempotent — a second press walks the wheel one
// stop past the target and the flow then waits for a face the row has
// already gone by. Observed on this box: three `zone_row_0` presses all
// landed (each traced its autosave) while the menu thread was too starved
// to republish the label inside the wait, and the TEAMS wheel went
// 4 -> 2 -> 3 -> 4 past its own target. So every call that can name its
// landing names it: the zone screen traces `submenu_opened` when a door
// lands and `acted_autosave` when an action row lands, both BEFORE the
// label the edge waits on is republished. Once the witness arrives the
// ladder stops pressing and spends the rest of its attempts WAITING.
//
// Counted, never clocked: `zone_click_retries` is the number of attempts
// that re-pressed because nothing registered; `zone_edge_waits` is the
// number that only waited, because the press had already landed.
inline int g_click_ladder_click_retries = 0;
inline int g_click_ladder_edge_waits = 0;
// TESTING-only fault injection for that half, mirroring g_click_ladder_click_drops:
// make the next N edge observations come back false although the press
// landed — exactly how a label the menu thread has not republished yet
// looks from the injector.
inline int g_click_ladder_edge_blinds = 0;

inline bool click_until_edge(const std::string& id,
                             const std::function<bool(int)>& edge_reached,
                             const char* landed_trace = nullptr,
                             int attempts = 3, int wait_ms = 2500,
                             const char* landed_category = "zone")
{
    const int landed_before =
        landed_trace ? count_trace_containing(landed_category, landed_trace)
                     : 0;
    const auto has_landed = [&] {
        return landed_trace && count_trace_containing(landed_category,
                                                      landed_trace) >
                                   landed_before;
    };
    bool spent = false;  // the press landed: never send another one
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (!spent) {
            (void)run_on_main_thread([] { reset_mouse_click_tracking(); },
                                     wait_ms);
            if (g_click_ladder_click_drops > 0) {
                --g_click_ladder_click_drops;
                fprintf(stderr,
                        "  [ladder] dropping the press on '%s' (injected)\n",
                        id.c_str());
            } else {
                (void)interact(id);
            }
        }
        bool reached = edge_reached(wait_ms);
        if (reached && g_click_ladder_edge_blinds > 0) {
            --g_click_ladder_edge_blinds;
            fprintf(stderr,
                    "  [ladder] blinding the edge observation on '%s' "
                    "(injected)\n",
                    id.c_str());
            reached = false;
        }
        if (reached) {
            // Same bounded re-post: a cancelled acknowledgement here leaves
            // the next press to evaporate against a stale baseline.
            (void)acknowledge_press(wait_ms);
            return true;
        }
        if (has_landed()) {
            spent = true;
            ++g_click_ladder_edge_waits;
            fprintf(stderr,
                    "  [ladder] attempt %d: '%s' landed but its edge lagged; "
                    "waiting, not re-pressing\n",
                    attempt + 1, id.c_str());
            continue;
        }
        ++g_click_ladder_click_retries;
        fprintf(stderr, "  [ladder] attempt %d: '%s' did not reach its edge\n",
                attempt + 1, id.c_str());
    }
    return false;
}

#endif  // _TEST_CLICK_LADDER_H__
