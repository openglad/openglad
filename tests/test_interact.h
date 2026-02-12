#ifndef _TEST_INTERACT_H__
#define _TEST_INTERACT_H__

#include "input/button.h"
#include "input/input.h"
#include "test_input_helpers.h"
#include <string>
#include <vector>

// Mutex to synchronize access to allbuttons[] between injector threads
// and the main thread during menu transitions. Defined in test_framework.cpp.
SDL_mutex* get_allbuttons_mutex();

struct AllButtonsLock final
{
    AllButtonsLock() : m_(get_allbuttons_mutex()) { SDL_LockMutex(m_); }
    ~AllButtonsLock() { SDL_UnlockMutex(m_); }
    AllButtonsLock(const AllButtonsLock&) = delete;
    AllButtonsLock& operator=(const AllButtonsLock&) = delete;
private:
    SDL_mutex* m_;
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
    AllButtonsLock lock;
    std::vector<Interactable> result;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!allbuttons[i])
            break;
        Interactable item;
        item.id = allbuttons[i]->id;
        item.label = allbuttons[i]->label;
        item.x = allbuttons[i]->xloc;
        item.y = allbuttons[i]->yloc;
        item.width = allbuttons[i]->width;
        item.height = allbuttons[i]->height;
        item.hidden = allbuttons[i]->hidden;
        result.push_back(item);
    }
    return result;
}

// Check if an interactable with this ID exists and is not hidden
inline bool has_interactable(const std::string& id)
{
    AllButtonsLock lock;
    bool found = false;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!allbuttons[i])
            break;
        if (allbuttons[i]->id == id && !allbuttons[i]->hidden) {
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
    AllButtonsLock lock;
    int win_x = -1, win_y = -1;
    bool found = false;
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (!allbuttons[i])
            break;
        if (allbuttons[i]->id == id && !allbuttons[i]->hidden) {
            // Compute center in game coords (320x200 space)
            int game_x = allbuttons[i]->xloc + allbuttons[i]->width / 2;
            int game_y = allbuttons[i]->yloc + allbuttons[i]->height / 2;

            // Convert game coords to window coords using viewport globals
            win_x = static_cast<int>(static_cast<float>(game_x) * (viewport_w / 320.0f) + viewport_offset_x);
            win_y = static_cast<int>(static_cast<float>(game_y) * (viewport_h / 200.0f) + viewport_offset_y);

            fprintf(stderr, "  [interact] clicking '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            found = true;
            break;
        }
    }
    if (found)
        inject_click(win_x, win_y);
    else
        fprintf(stderr, "  [interact] WARNING: '%s' not found in allbuttons\n", id.c_str());
}

#endif // _TEST_INTERACT_H__
