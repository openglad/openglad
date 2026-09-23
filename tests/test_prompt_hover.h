#ifndef OPENGLAD_TEST_PROMPT_HOVER_H
#define OPENGLAD_TEST_PROMPT_HOVER_H

#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <cmath>
#include <vector>

struct PromptHoverRect
{
    int x;
    int y;
    int w;
    int h;
};

struct PromptHoverPixel
{
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;

    friend bool operator==(const PromptHoverPixel&, const PromptHoverPixel&) = default;
};

struct PromptHoverPoint
{
    int x;
    int y;
};

struct PromptWindowPoint
{
    int x;
    int y;
};

inline PromptWindowPoint prompt_hover_window_point(float canvas_x, float canvas_y)
{
    const auto [x, y] = ui_canvas_to_window(canvas_x, canvas_y);
    return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

struct PromptHoverProbe
{
    std::vector<PromptHoverPoint> points;
    std::vector<PromptHoverPixel> pixels;
    size_t field_begin = 0;
    std::atomic<bool> complete{false};
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool read_mouse = false;
};

inline void SDLCALL prompt_hover_capture_on_main_thread(void* data)
{
    auto* const probe = static_cast<PromptHoverProbe*>(data);
    screen* const target = og::runtime::current_session->myscreen_;
    for (size_t i = 0; i < probe->points.size(); ++i)
    {
        const PromptHoverPoint point = probe->points[i];
        target->get_pixel(point.x, point.y, &probe->pixels[i].r,
                          &probe->pixels[i].g, &probe->pixels[i].b);
    }
    if (probe->read_mouse)
    {
        const MouseState& mouse = query_mouse_no_poll();
        probe->mouse_x = mouse.x;
        probe->mouse_y = mouse.y;
    }
    probe->complete.store(true, std::memory_order_release);
}

inline bool prompt_hover_capture(PromptHoverProbe& probe, bool read_mouse = false)
{
    probe.read_mouse = read_mouse;
    probe.complete.store(false, std::memory_order_relaxed);
    if (!SDL_RunOnMainThread(prompt_hover_capture_on_main_thread, &probe, false))
        return false;
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (!probe.complete.load(std::memory_order_acquire) &&
           SDL_GetTicks() < deadline)
        SDL_Delay(5);
    return probe.complete.load(std::memory_order_acquire);
}

inline void init_prompt_hover_probe(
    PromptHoverProbe& probe, const std::vector<PromptHoverRect>& buttons,
    PromptHoverRect field)
{
    const auto add_rect = [&probe](int left, int top, int right, int bottom) {
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                if (x >= 0 && x < 320 && y >= 0 && y < 200)
                    probe.points.push_back({x, y});
    };
    for (const PromptHoverRect& button : buttons)
        add_rect(button.x - 1, button.y - 1,
                 button.x + button.w + 1, button.y + button.h + 1);
    probe.field_begin = probe.points.size();
    add_rect(field.x, field.y, field.x + field.w - 1,
             field.y + field.h - 1);
    probe.pixels.resize(probe.points.size());
}

inline PromptHoverPixel prompt_hover_yellow_rgb()
{
    const auto& prompt_palette = og::runtime::current_session->myscreen_->ourpalette;
    constexpr size_t kYellowOffset = static_cast<size_t>(YELLOW) * 3;
    return {static_cast<Uint8>(prompt_palette[kYellowOffset] * 4),
            static_cast<Uint8>(prompt_palette[kYellowOffset + 1] * 4),
            static_cast<Uint8>(prompt_palette[kYellowOffset + 2] * 4)};
}

inline bool prompt_hover_is_ring_pixel(PromptHoverPoint point,
                                       const PromptHoverRect& button)
{
    const bool in_outline_bounds = point.x >= button.x - 1 &&
        point.x <= button.x + button.w + 1 && point.y >= button.y - 1 &&
        point.y <= button.y + button.h + 1;
    const bool on_outline = point.x == button.x - 1 ||
        point.x == button.x + button.w + 1 || point.y == button.y - 1 ||
        point.y == button.y + button.h + 1;
    return in_outline_bounds && on_outline;
}

inline bool prompt_hover_pointer_at(const PromptHoverProbe& probe,
                                    float canvas_x, float canvas_y)
{
    return std::fabs(probe.mouse_x - canvas_x) < 0.01f &&
           std::fabs(probe.mouse_y - canvas_y) < 0.01f;
}

inline bool prompt_hover_matches(const PromptHoverProbe& probe,
                                 const std::vector<PromptHoverPixel>& baseline,
                                 const std::vector<PromptHoverRect>& buttons,
                                 int hovered_button)
{
    if (probe.pixels.size() != baseline.size())
        return false;
    const PromptHoverPixel yellow = prompt_hover_yellow_rgb();
    for (size_t i = 0; i < probe.field_begin; ++i)
    {
        bool should_be_yellow = false;
        if (hovered_button >= 0)
            should_be_yellow = prompt_hover_is_ring_pixel(
                probe.points[i], buttons[static_cast<size_t>(hovered_button)]);
        if (should_be_yellow ? probe.pixels[i] != yellow
                             : probe.pixels[i] != baseline[i])
            return false;
    }
    return true;
}

inline bool prompt_hover_field_changed(const PromptHoverProbe& probe,
                                       const std::vector<PromptHoverPixel>& before)
{
    if (probe.pixels.size() != before.size())
        return false;
    for (size_t i = probe.field_begin; i < probe.pixels.size(); ++i)
        if (probe.pixels[i] != before[i])
            return true;
    return false;
}

#endif
