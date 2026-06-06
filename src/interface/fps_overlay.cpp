#include <openglad/interface/fps_overlay.h>

#include <openglad/interface/base.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/screen.h>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace {

struct FpsCounter {
    static constexpr std::size_t kCap = 256;
    static constexpr std::uint32_t kWindowMs = 500;
    std::array<std::uint32_t, kCap> ts{};
    std::size_t head = 0;
    std::size_t size = 0;
    std::uint32_t first_seen_ms = 0;
    bool initialized = false;
    int update(std::uint32_t now_ms);
};

int FpsCounter::update(std::uint32_t now_ms)
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
        const std::uint32_t cutoff = now_ms - kWindowMs;
        while (size > 0)
        {
            const std::size_t tail = (head + kCap - size) % kCap;
            if (ts[tail] < cutoff)
                --size;
            else
                break;
        }
    }

    const std::uint32_t elapsed = now_ms - first_seen_ms;
    if (elapsed < 250)
    {
        return static_cast<int>(size * 1000 /
            std::max<std::uint32_t>(1, elapsed));
    }
    return static_cast<int>(size * 1000 / kWindowMs);
}

// new_score_panel draws the TEAM/FOES counter in the top-right corner of the
// top viewport as a box spanning y in [yloc+pad+1, yloc+pad+16]. The top row's
// viewport always has yloc == 0, so that counter's bottom edge is at pad+16.
// Place the FPS readout one row below it so the developer overlay never sits on
// top of the live foe count.
#ifdef REDUCE_OVERSCAN
inline constexpr int kOverscanPadding = 6;
#else
inline constexpr int kOverscanPadding = 0;
#endif
inline constexpr int kFoesCounterBottom = kOverscanPadding + 16;
inline constexpr int kFpsOverlayY = kFoesCounterBottom + 2;

} // namespace

void draw_fps_overlay(screen& s)
{
    static FpsCounter c;
    const std::uint32_t now_ms = SDL_GetTicks();
    const int fps = c.update(now_ms);
    const std::string msg = std::format("FPS: {}", fps);
    const int w = s.text_normal.query_width(msg);
    s.text_normal.write_xy(320 - w - 2, kFpsOverlayY, msg, YELLOW,
                           static_cast<short>(1));
}
