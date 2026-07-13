#pragma once

class screen;

// Draws the measured render FPS at the top-right of the frame, one row below
// the TEAM/FOES counter box. counter_bottom_y is the box's bottom edge in
// canvas coords (the caller — new_score_panel — knows the box height, which
// grows with the NEXT WAVE and FLR rows); the readout renders at
// counter_bottom_y + 2 so the developer overlay never sits on top of the live
// foe count. Tracks frame timestamps internally; call once per render frame.
void draw_fps_overlay(screen& s, int counter_bottom_y);
