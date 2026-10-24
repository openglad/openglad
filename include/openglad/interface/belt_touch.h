/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// New Ability Select on a touch screen: the web page's 1 / 2 / 3 buttons
// show seat 0's hotbar slots (issue #384). The HUD resolves the three slots
// once per frame (resolve_belt_cells in score_panel.cpp, the only place a
// slot's state, picture and label are decided) and hands them here; the web
// bridge (src/platform/emscripten/web_touch_bridge.cpp) registers the
// observer and pushes the pictures to the page. og_interface stays free of
// Emscripten: with no observer registered nothing is resolved or published.

#include <array>
#include <functional>
#include <string>

class PixieData;

namespace og::ui {

// One slot as the hotbar would draw it.
struct BeltTouchCell {
    int key = 0;                        // 1..3
    bool shift = false;                 // the SHIFTER layer
    const char* drawn = "none";         // the path whose pixels the slot shows
    bool locked_copy = false;           // `picture` is the grey locked copy
    const char* state = "empty";        // castable, short, locked, disabled
    bool pressed = false;               // the seat holds this SPEC key
    std::string label;                  // "1", "S1", or "L7" when locked
    const PixieData* picture = nullptr; // valid during the observer call only
};

struct BeltTouchState {
    bool belt_hud = false;   // New Ability Select is on
    bool hud_hidden = false; // seat 0's pane draws no hotbar (touch overlay)
    bool shift = false;      // the SHIFTER row is showing
    std::array<BeltTouchCell, 3> cells{};
};

using BeltTouchObserver = std::function<void(const BeltTouchState&)>;

// One observer at a time; an empty function clears it. The web bridge
// registers it when the page first turns its overlay on and keeps it, so
// the page also hears hud_hidden go false when the overlay goes away.
void set_belt_touch_observer(BeltTouchObserver observer);
// True while an observer is registered (never on a native build): only then
// does new_score_panel resolve and publish seat 0's slots.
[[nodiscard]] bool belt_touch_observed();
// Called by new_score_panel for seat 0, once per HUD frame, while observed.
void publish_belt_touch(const BeltTouchState& state);

} // namespace og::ui
