#include <openglad/interface/fps_overlay.h>

#include <openglad/interface/base.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/screen.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

int FpsCounter::update(std::uint64_t now_ms)
{
    if (!initialized)
    {
        first_seen_ms = now_ms;
        initialized = true;
    }

    ts[head] = now_ms;
    head = (head + 1) % kCap;
    if (size < kCap)
        ++size;

    if (now_ms >= kWindowMs)
    {
        const std::uint64_t cutoff = now_ms - kWindowMs;
        while (size > 0)
        {
            const std::size_t tail = (head + kCap - size) % kCap;
            if (ts[tail] < cutoff)
                --size;
            else
                break;
        }
    }

    const std::uint64_t elapsed = now_ms - first_seen_ms;
    if (elapsed < 250)
    {
        return static_cast<int>(size * 1000 /
            std::max<std::uint64_t>(1, elapsed));
    }
    return static_cast<int>(size * 1000 / kWindowMs);
}

// new_score_panel draws the TEAM/FOES counter in the top-right corner of the
// top viewport; counter_bottom_y is that box's bottom edge (it grows with the
// NEXT WAVE and FLR rows, so the caller computes it). Render one row below it
// so the developer overlay never sits on top of the live foe count.
void draw_fps_overlay(screen& s, int counter_bottom_y)
{
    static FpsCounter c;
    const std::uint64_t now_ms = SDL_GetTicks();
    const int fps = c.update(now_ms);
    const std::string msg = std::format("FPS: {}", fps);
    const int w = s.text_normal.query_width(msg);
    // Right-anchored to the active canvas (world during gameplay frames).
    s.text_normal.write_xy(s.canvas_w() - w - 2, counter_bottom_y + 2, msg,
                           YELLOW, static_cast<short>(1));
}
