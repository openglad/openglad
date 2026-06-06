#pragma once

class screen;

// Draws the measured render FPS at the top-right of the 320x200 frame.
// Tracks frame timestamps internally; call once per render frame.
void draw_fps_overlay(screen& s);
