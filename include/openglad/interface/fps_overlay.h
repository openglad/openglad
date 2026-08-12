#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class screen;

// Sliding-window frame-rate estimator: one update() call per presented frame,
// timestamps in SDL_GetTicks() milliseconds. Exposed here because the demo
// compositor needs the same measurement for its whole-grid readout; keeping one
// definition keeps the two overlays reporting the same quantity.
struct FpsCounter {
    // Ring capacity. Samples leave the window by age, but a full ring
    // overwrites its oldest entry, so the readout is only honest up to
    // kCap / (kWindowMs / 1000) frames per second and saturates above it.
    static constexpr std::size_t kCap = 2048;
    static constexpr std::uint64_t kWindowMs = 500;
    std::array<std::uint64_t, kCap> ts{};
    std::size_t head = 0;
    std::size_t size = 0;
    std::uint64_t first_seen_ms = 0;
    bool initialized = false;
    // Returns frames per second over the trailing kWindowMs; during the first
    // 250ms after the first call it extrapolates from the samples so far.
    int update(std::uint64_t now_ms);
};

// Draws the measured render FPS at the top-right of the frame, one row below
// the TEAM/FOES counter box. counter_bottom_y is the box's bottom edge in
// canvas coords (the caller — new_score_panel — knows the box height, which
// grows with the NEXT WAVE and FLR rows); the readout renders at
// counter_bottom_y + 2 so the developer overlay never sits on top of the live
// foe count. Tracks frame timestamps internally; call once per render frame.
void draw_fps_overlay(screen& s, int counter_bottom_y);
