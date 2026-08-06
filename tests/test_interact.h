#ifndef _TEST_INTERACT_H__
#define _TEST_INTERACT_H__

#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include "test_input_helpers.h"
#include <mutex>
#include <string>
#include <vector>

// Mutex to synchronize access to allbuttons[] between injector threads
// and the main thread during menu transitions. Defined in integration_main.cpp.
std::mutex& get_allbuttons_mutex();

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
// the canonical path into the campaign intro + team build. Returns whether the
// screen appeared and was accepted.
inline bool accept_generated_company_name(int timeout_ms = 5000)
{
    if (!wait_for_interactable("company_name_accept", timeout_ms))
        return false;
    SDL_Delay(750);  // FadeAroundEntry settle (fadeblack eats events)
    fprintf(stderr, "  [test] accepting generated company name\n");
    interact("company_name_accept");
    return true;
}

#endif // _TEST_INTERACT_H__
