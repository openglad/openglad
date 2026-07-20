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
        if (!og::runtime::current_session->allbuttons_[i])
            continue; // allbuttons[] can contain holes during transitions
        Interactable item;
        item.id = og::runtime::current_session->allbuttons_[i]->id;
        item.label = og::runtime::current_session->allbuttons_[i]->label;
        item.x = og::runtime::current_session->allbuttons_[i]->xloc;
        item.y = og::runtime::current_session->allbuttons_[i]->yloc;
        item.width = og::runtime::current_session->allbuttons_[i]->width;
        item.height = og::runtime::current_session->allbuttons_[i]->height;
        item.hidden = og::runtime::current_session->allbuttons_[i]->hidden;
        result.push_back(item);
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
        if (!og::runtime::current_session->allbuttons_[i])
            continue;
        if (og::runtime::current_session->allbuttons_[i]->id == id && !og::runtime::current_session->allbuttons_[i]->hidden) {
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
        SDL_Delay(poll_interval);
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
            if (!og::runtime::current_session->allbuttons_[i])
                continue;
            if (og::runtime::current_session->allbuttons_[i]->id == id && !og::runtime::current_session->allbuttons_[i]->hidden) {
                // Compute center in game coords (320x200 space)
                int game_x = og::runtime::current_session->allbuttons_[i]->xloc + og::runtime::current_session->allbuttons_[i]->width / 2;
                int game_y = og::runtime::current_session->allbuttons_[i]->yloc + og::runtime::current_session->allbuttons_[i]->height / 2;

                // Menus live on the fixed UI canvas. Use the same aspect-fit
                // transform as production input/presentation so widescreen
                // pillarboxes and narrow-window letterboxes are not clicked.
                const auto [mapped_x, mapped_y] =
                    active_canvas_to_window(static_cast<float>(game_x),
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
