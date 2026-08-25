#ifndef _TEST_INTERACT_H__
#define _TEST_INTERACT_H__

#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include "test_input_helpers.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Mutex to synchronize access to allbuttons[] between injector threads
// and the main thread during menu transitions. Defined in integration_main.cpp.
std::mutex& get_allbuttons_mutex();

// Main-thread task queue (issue #257). An injector thread that mutates state
// the menu loop reads every frame (save_data, cfg, the hire session, the live
// roster) must NOT write it directly: SaveData::completed_levels and cfg are
// node-based maps, and one concurrent insert dangles an iterator the main
// thread is walking. Post the mutation instead — the menu runner drains the
// queue at the top of each frame (og::ui::g_picker_main_thread_pump), so the
// write lands between frames, ordered against the lobby poll, the stage
// digest, and the label sync.
//
// Only engine-hosted screens (run_menu_screen) pump; a task posted while no
// menu loop is running waits out its timeout and is CANCELLED there and then.
// All three are defined in integration_main.cpp.
using MainThreadTaskTicket = std::uint64_t;
MainThreadTaskTicket post_main_thread_task(std::function<void()> task);
// Blocks until the posted task has finished running. Returns true only when
// the task actually ran on the menu thread; the ceiling is generous on
// purpose. On timeout the ticket is cancelled before this returns — a task
// that missed its window never runs afterwards, so a lambda may capture the
// caller's locals by reference — and the return is false. False also comes
// back for a task the between-tests drain threw away unrun; a discarded task
// is never reported as a success.
bool wait_for_main_thread_task(MainThreadTaskTicket ticket,
                               int timeout_ms = 15000);
// Discards every queued-but-unrun task and releases its waiters with false.
// Runs between tests so a task left behind by an injector can never execute
// inside the next test's menu loop.
void drain_main_thread_tasks();

// Post + wait in one call, the shape nearly every injector wants.
inline bool run_on_main_thread(std::function<void()> task,
                               int timeout_ms = 15000)
{
    return wait_for_main_thread_task(post_main_thread_task(std::move(task)),
                                     timeout_ms);
}

struct AllButtonsLock final
{
    AllButtonsLock() : lock_(get_allbuttons_mutex()) {}
    AllButtonsLock(const AllButtonsLock&) = delete;
    AllButtonsLock& operator=(const AllButtonsLock&) = delete;
private:
    std::lock_guard<std::mutex> lock_;
};

struct Interactable {
    std::string id;
    std::string label;
    int x, y, width, height;
    bool hidden;
};

// Get all currently active interactables from allbuttons[]
inline std::vector<Interactable> get_interactables()
{
    og::runtime::ensure_thread_session();
    AllButtonsLock lock;
    std::vector<Interactable> result;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)])
            continue; // allbuttons[] can contain holes during transitions
        Interactable item;
        item.id = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->id;
        item.label = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->label;
        item.x = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->xloc;
        item.y = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->yloc;
        item.width = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->width;
        item.height = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->height;
        item.hidden = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->hidden;
        result.push_back(item);
    }
    return result;
}

// Snapshot immutable button IDs without touching labels or row state, which
// engine menus may refresh between frames. This is the safe screen-identity
// probe for injector threads during menu transitions.
inline std::vector<std::string> get_button_ids()
{
    og::runtime::ensure_thread_session();
    AllButtonsLock lock;
    std::vector<std::string> result;
    for (int i = 0; i < MAX_BUTTONS; ++i) {
        if (og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)])
            result.push_back(
                og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->id);
    }
    return result;
}

// Check if an interactable with this ID exists and is not hidden
inline bool has_interactable(const std::string& id)
{
    og::runtime::ensure_thread_session();
    AllButtonsLock lock;
    bool found = false;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)])
            continue;
        if (og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->id == id && !og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->hidden) {
            found = true;
            break;
        }
    }
    return found;
}

// Block until an interactable appears (polls allbuttons[]), returns false on timeout
inline bool wait_for_interactable(const std::string& id, int timeout_ms = 5000)
{
    int elapsed = 0;
    int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable(id))
            return true;
        SDL_Delay(static_cast<Uint32>(poll_interval));
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' (%d ms)\n", id.c_str(), timeout_ms);
    return false;
}

// The live label of a visible interactable, or "" when it is absent/hidden.
// Base Camp's seat slots are one ordinal wearing two faces (a seat card or an
// ADD PLAYER door), so presence alone no longer tells an injector what a slot
// IS — the label does.
inline std::string interactable_label(const std::string& id)
{
    og::runtime::ensure_thread_session();
    AllButtonsLock lock;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        vbutton* const live =
            og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
        if (live == nullptr || live->id != id || live->hidden)
            continue;
        return live->label;
    }
    return {};
}

// Block until a visible interactable carries an exact label. The wait-on-
// condition form of "the click landed": a slot that became a seat card names
// its controller, and one that did not still says ADD PLAYER.
inline bool wait_for_interactable_label(const std::string& id,
                                        const std::string& label,
                                        int timeout_ms = 5000)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (interactable_label(id) == label)
            return true;
        SDL_Delay(static_cast<Uint32>(poll_interval));
        elapsed += poll_interval;
    }
    fprintf(stderr,
            "  [interact] TIMEOUT waiting for '%s' to read '%s' (%d ms; now "
            "'%s')\n",
            id.c_str(), label.c_str(), timeout_ms,
            interactable_label(id).c_str());
    return false;
}

// Block until a visible interactable STOPS carrying a label — the "this slot
// changed identity" wait, for cases where the new text is not known up front.
inline bool wait_for_interactable_label_change(const std::string& id,
                                               const std::string& old_label,
                                               int timeout_ms = 5000)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        const std::string now = interactable_label(id);
        if (!now.empty() && now != old_label)
            return true;
        SDL_Delay(static_cast<Uint32>(poll_interval));
        elapsed += poll_interval;
    }
    fprintf(stderr,
            "  [interact] TIMEOUT waiting for '%s' to stop reading '%s' "
            "(%d ms)\n",
            id.c_str(), old_label.c_str(), timeout_ms);
    return false;
}

// Click an interactable by ID. Finds the button, computes center in game coords,
// converts to window coords, injects SDL click event.
inline void interact(const std::string& id)
{
    og::runtime::ensure_thread_session();
    int win_x = -1, win_y = -1;
    bool found = false;
    {
        AllButtonsLock lock;
        for (int i = 0; i < MAX_BUTTONS; i++) {
            if (!og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)])
                continue;
            if (og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->id == id && !og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->hidden) {
                // Compute center in game coords (320x200 space)
                int game_x = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->xloc + og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->width / 2;
                int game_y = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->yloc + og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->height / 2;

                // Menus live on the fixed UI canvas. Use the UI-canvas-pinned
                // aspect-fit transform: this thread races the main thread's
                // per-frame World<->UI canvas flip, so the active-canvas
                // transform can sample the World state and mismap the click
                // whenever the window aspect differs from the canvas aspect
                // (the seed-23 og_test_menu_ui wedge — a bottom-strip click
                // forward-mapped with World(320x240) inverse-maps to game
                // y=204, off-canvas, and is silently dropped forever).
                const auto [mapped_x, mapped_y] =
                    ui_canvas_to_window(static_cast<float>(game_x),
                                        static_cast<float>(game_y));
                win_x = static_cast<int>(mapped_x);
                win_y = static_cast<int>(mapped_y);

                fprintf(stderr, "  [interact] clicking '%s' at game(%d,%d) win(%d,%d)\n",
                        id.c_str(), game_x, game_y, win_x, win_y);
                found = true;
                break;
            }
        }
    }
    if (found)
        inject_click(win_x, win_y, 100);
    else
        fprintf(stderr, "  [interact] WARNING: '%s' not found in allbuttons\n", id.c_str());
}

// §2.2: after clicking BEGIN NEW GAME, the flow opens the name-entry screen
// (generated company name, REROLL, editable, ACCEPT). Every injector flow that
// founds a new game must clear this screen; accepting the generated name is
// the canonical path into team build (the campaign intro moved behind the
// campaign select, which the TESTING short-circuit skips). Returns whether
// the screen appeared and was accepted.
inline bool accept_generated_company_name(int timeout_ms = 5000)
{
    if (!wait_for_interactable("company_name_accept", timeout_ms))
        return false;
    SDL_Delay(750);  // menu-entry settle (fades are instant under TESTING)
    fprintf(stderr, "  [test] accepting generated company name\n");
    interact("company_name_accept");
    return true;
}

#endif // _TEST_INTERACT_H__
