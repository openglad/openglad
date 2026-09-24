#include <openglad/interface/render/text.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_escape_tail.h"
#include "test_prompt_hover.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <atomic>
#include <string>
#include <vector>

namespace
{
void capture_prompt_if_requested()
{
    const char* const path = std::getenv("OG_PROMPT_CAPTURE_PATH");
    if (path == nullptr || path[0] == '\0')
        return;
    FILE* const output = std::fopen(path, "wb");
    if (output == nullptr)
        return;
    std::fprintf(output, "P6\n320 200\n255\n");
    screen* const scr = og::runtime::current_session->myscreen_;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
        {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            std::fputc(r, output);
            std::fputc(g, output);
            std::fputc(b, output);
        }
    std::fclose(output);
}

static int injector_thread_backspace_text_and_return(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    // Force "first key is backspace" branch (has_typed=0, current_length>0 when begin is non-empty),
    // then type a few characters and commit with Return.
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_BACKSPACE;
    SDL_PushEvent(&ev);

    SDL_Delay(10);
    ev = SDL_Event{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_LEFT; // deselect whole line path
    SDL_PushEvent(&ev);

    SDL_Delay(10);
    inject_text_input("xy");

    SDL_Delay(10);
    ev = SDL_Event{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    SDL_PushEvent(&ev);
    return 0;
}

static int injector_thread_escape(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_ESCAPE;
    SDL_PushEvent(&ev);
    return 0;
}

static int injector_thread_accept_click(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);
    capture_prompt_if_requested();
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    // prompt_for_string's production grid places ACCEPT at x=151..232,
    // y=74..88 in the shared prompt footer.
    // The dummy test window is 640x400 while the prompt canvas is 320x200.
    ev.button.x = 382.0f;
    ev.button.y = 162.0f;
    SDL_PushEvent(&ev);
    return 0;
}

static int injector_thread_cancel_click(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    // CANCEL is x=58..139 beside ACCEPT in the production prompt.
    ev.button.x = 196.0f;
    ev.button.y = 162.0f;
    SDL_PushEvent(&ev);
    return 0;
}

struct PromptCursorInjector
{
    bool accept = true;
    bool saw_text_input = false;
    bool saw_cursor_during_prompt = false;
    std::atomic<bool> pointer_closed{false};
    std::atomic<bool> main_returned{false};
    std::atomic<bool> cursor_observed{false};
    std::atomic<bool> cursor_visible{false};
    int pointer_x = 0;
    int pointer_y = 0;
};

struct PromptHoverInjector
{
    bool accept = true;
    PromptHoverProbe probe;
    std::vector<PromptHoverRect> buttons;
    PromptWindowPoint outside;
    PromptWindowPoint accept_point;
    PromptWindowPoint cancel_point;
    float accept_canvas_x = 0.0f;
    float accept_canvas_y = 0.0f;
    float cancel_canvas_x = 0.0f;
    float cancel_canvas_y = 0.0f;
    std::vector<PromptHoverPixel> baseline;
    std::atomic<bool> main_returned{false};
    std::atomic<bool> pointer_closed{false};
    bool baseline_ready = false;
    bool moved_accept = false;
    bool moved_cancel = false;
    bool moved_outside = false;
    bool moved_back = false;
    bool field_changed = false;
    bool stationary_ring = false;
    bool face_and_bevel_ready = false;
    bool saw_text_input = false;
    bool capture_failed = false;
};

static const PromptHoverPixel* prompt_pixel_at(
    const PromptHoverProbe& probe, int x, int y)
{
    for (size_t i = 0; i < probe.points.size(); ++i)
        if (probe.points[i].x == x && probe.points[i].y == y)
            return &probe.pixels[i];
    return nullptr;
}

static bool prompt_button_faces_ready(const PromptHoverProbe& probe,
                                     const std::vector<PromptHoverRect>& buttons)
{
    const PromptHoverPixel yellow = prompt_hover_yellow_rgb();
    for (const PromptHoverRect& button : buttons)
    {
        const PromptHoverPixel* const face =
            prompt_pixel_at(probe, button.x + 2, button.y + 2);
        const PromptHoverPixel* const bevel =
            prompt_pixel_at(probe, button.x + 2, button.y);
        if (face == nullptr || bevel == nullptr || *face == yellow ||
            *bevel == yellow || *face == *bevel)
            return false;
    }
    return true;
}

static bool prompt_hover_capture_and_match(PromptHoverInjector& state,
                                           int hovered_button,
                                           float canvas_x, float canvas_y)
{
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (!state.capture_failed && SDL_GetTicks() < deadline)
    {
        if (!prompt_hover_capture(state.probe, true))
        {
            state.capture_failed = true;
            return false;
        }
        if (prompt_hover_pointer_at(state.probe, canvas_x, canvas_y) &&
            prompt_hover_matches(state.probe, state.baseline,
                                 state.buttons, hovered_button))
            return true;
        SDL_Delay(5);
    }
    return false;
}

static int injector_thread_prompt_hover(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<PromptHoverInjector*>(data);
    const Uint64 active_deadline = SDL_GetTicks() + 5000;
    while (!og::input_native::text_input_is_active() &&
           SDL_GetTicks() < active_deadline)
        SDL_Delay(5);
    state->saw_text_input = og::input_native::text_input_is_active();

    if (state->saw_text_input)
    {
        inject_mouse_motion(state->outside.x, state->outside.y);
        const bool baseline_captured = prompt_hover_capture(state->probe, true);
        state->capture_failed = !baseline_captured;
        state->baseline_ready = baseline_captured &&
            prompt_hover_pointer_at(state->probe, 10.0f, 180.0f);
        state->face_and_bevel_ready =
            state->baseline_ready &&
            prompt_button_faces_ready(state->probe, state->buttons);
        if (state->baseline_ready)
            state->baseline = state->probe.pixels;

        bool sequence_ok = state->baseline_ready &&
            state->face_and_bevel_ready;
        if (sequence_ok)
        {
            inject_mouse_motion(state->accept_point.x, state->accept_point.y);
            state->moved_accept = prompt_hover_capture_and_match(
                *state, 0, state->accept_canvas_x,
                state->accept_canvas_y);
            sequence_ok = state->moved_accept;
        }
        if (sequence_ok)
        {
            inject_mouse_motion(state->cancel_point.x, state->cancel_point.y);
            state->moved_cancel = prompt_hover_capture_and_match(
                *state, 1, state->cancel_canvas_x,
                state->cancel_canvas_y);
            sequence_ok = state->moved_cancel;
        }
        if (sequence_ok)
        {
            inject_mouse_motion(state->outside.x, state->outside.y);
            state->moved_outside = prompt_hover_capture_and_match(
                *state, -1, 10.0f, 180.0f);
            sequence_ok = state->moved_outside;
        }

        const int final_button = state->accept ? 0 : 1;
        const PromptWindowPoint final_point = state->accept
            ? state->accept_point : state->cancel_point;
        const float final_x = state->accept
            ? state->accept_canvas_x : state->cancel_canvas_x;
        const float final_y = state->accept
            ? state->accept_canvas_y : state->cancel_canvas_y;
        if (sequence_ok)
        {
            inject_mouse_motion(final_point.x, final_point.y);
            state->moved_back = prompt_hover_capture_and_match(
                *state, final_button, final_x, final_y);
            sequence_ok = state->moved_back;
        }

        const std::vector<PromptHoverPixel> before_text = state->baseline;
        bool edit_capture_acknowledged = false;
        if (sequence_ok)
        {
            inject_text_input("Z");
            const Uint64 edit_deadline = SDL_GetTicks() + 5000;
            while (SDL_GetTicks() < edit_deadline)
            {
                if (!prompt_hover_capture(state->probe, true))
                {
                    state->capture_failed = true;
                    break;
                }
                edit_capture_acknowledged = true;
                if (prompt_hover_pointer_at(state->probe, final_x, final_y) &&
                    prompt_hover_field_changed(state->probe, before_text))
                    break;
                SDL_Delay(5);
            }
        }
        state->field_changed = edit_capture_acknowledged &&
            !state->capture_failed &&
            prompt_hover_field_changed(state->probe, before_text);
        state->stationary_ring = state->field_changed &&
            !state->capture_failed &&
            prompt_hover_matches(state->probe, state->baseline,
                                 state->buttons, final_button);

        if (state->stationary_ring && !state->capture_failed)
            inject_mouse_down(final_point.x, final_point.y);
        const Uint64 close_deadline = SDL_GetTicks() + 5000;
        while (state->stationary_ring && !state->capture_failed &&
               og::input_native::text_input_is_active() &&
               SDL_GetTicks() < close_deadline)
            SDL_Delay(5);
        if (state->stationary_ring && !state->capture_failed)
            inject_mouse_up(final_point.x, final_point.y);
        state->pointer_closed.store(
            !og::input_native::text_input_is_active(),
            std::memory_order_release);
    }

    const auto prompt_hold = [] {
        if (!og::input_native::text_input_is_active())
            return false;
        inject_key_press(SDLK_ESCAPE, 10);
        return true;
    };
    return escape_to_the_main_thread(
        state->main_returned, state->pointer_closed.load() ? 0 : 1,
        "hovered pointer action did not close the text prompt",
        std::span<const EscapeDoor>{}, prompt_hold);
}

static std::optional<std::string> run_prompt_with_hover(bool accept,
                                                        PromptHoverInjector& state)
{
    text t(TEXT_1);
    state.accept = accept;
    const int field_width = 29 * (t.sizex + 1);
    const og::ui::PromptDialogLayout layout = og::ui::prompt_dialog_layout(
        58, 60, field_width, t.sizey);
    const og::ui::PromptActionLayout& actions = layout.actions;
    state.buttons = {
        {actions.accept.x, actions.accept.y,
         actions.accept.w, actions.accept.h},
        {actions.cancel.x, actions.cancel.y,
         actions.cancel.w, actions.cancel.h},
    };
    init_prompt_hover_probe(state.probe, state.buttons,
                            {58, 60, field_width, t.sizey});
    state.outside = prompt_hover_window_point(10.0f, 180.0f);
    state.accept_canvas_x = static_cast<float>(actions.accept.x +
                                                actions.accept.w / 2);
    state.accept_canvas_y = static_cast<float>(actions.accept.y +
                                                actions.accept.h / 2);
    state.cancel_canvas_x = static_cast<float>(actions.cancel.x +
                                                actions.cancel.w / 2);
    state.cancel_canvas_y = static_cast<float>(actions.cancel.y +
                                                actions.cancel.h / 2);
    state.accept_point = prompt_hover_window_point(
        state.accept_canvas_x, state.accept_canvas_y);
    state.cancel_point = prompt_hover_window_point(
        state.cancel_canvas_x, state.cancel_canvas_y);

    inject_mouse_motion(state.outside.x, state.outside.y);
    get_input_events(POLL);
    const MouseState& outside_mouse = query_mouse_no_poll();
    EXPECT_NEAR(10.0f, outside_mouse.x, 0.01f);
    EXPECT_NEAR(180.0f, outside_mouse.y, 0.01f);

    SDL_Thread* const thread = SDL_CreateThread(
        injector_thread_prompt_hover, "text_prompt_hover", &state);
    if (thread == nullptr)
    {
        ADD_FAILURE() << "failed to create the prompt hover injector";
        return std::nullopt;
    }
    std::optional<std::string> result =
        t.input_string_ex_value(58, 60, 29, "NAME THIS CHARACTER", "seed");
    state.main_returned.store(true, std::memory_order_release);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    SDL_PumpEvents();
    escape_tail_join_hygiene();
    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(state.saw_text_input)
        << "the injector must wait for the real blocking prompt";
    EXPECT_TRUE(state.pointer_closed.load(std::memory_order_acquire))
        << "the real pointer click must close the prompt without Escape";
    EXPECT_TRUE(state.baseline_ready)
        << "the baseline must be sampled after outside motion is consumed";
    EXPECT_TRUE(state.face_and_bevel_ready)
        << "both actual action faces and bevels must be present before capture";
    EXPECT_TRUE(state.moved_accept)
        << "motion onto ACCEPT must paint its exact yellow ring";
    EXPECT_TRUE(state.moved_cancel)
        << "motion onto CANCEL must restore ACCEPT and paint CANCEL";
    EXPECT_TRUE(state.moved_outside)
        << "motion outside both buttons must restore every outline pixel";
    EXPECT_TRUE(state.moved_back)
        << "motion back to the selected action must repaint only its ring";
    EXPECT_TRUE(state.field_changed)
        << "the injected text must be rendered before the action click";
    EXPECT_TRUE(state.stationary_ring)
        << "editing while stationary must preserve the exact hover ring and labels";
    return result;
}

static void SDLCALL observe_cursor_on_main_thread(void* data)
{
    auto* const state = static_cast<PromptCursorInjector*>(data);
    if (!og::input_native::text_input_is_active())
        return;
    const auto [x, y] = ui_canvas_to_window(state->accept ? 191.0f : 98.0f, 81.0f);
    state->pointer_x = static_cast<int>(x);
    state->pointer_y = static_cast<int>(y);
    state->cursor_visible.store(SDL_CursorVisible(), std::memory_order_relaxed);
    state->cursor_observed.store(true, std::memory_order_release);
}

static int injector_thread_prompt_cursor(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<PromptCursorInjector*>(data);
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline &&
           !og::input_native::text_input_is_active())
        SDL_Delay(5);

    state->saw_text_input = og::input_native::text_input_is_active();
    // The prompt's event loop services this callback while it is active. The
    // injector keeps its state alive until the test thread joins it and pumps
    // any callback that remained queued after the modal returned.
    const bool callback_queued =
        SDL_RunOnMainThread(observe_cursor_on_main_thread, state, false);
    const Uint64 cursor_deadline = SDL_GetTicks() + 5000;
    while (callback_queued &&
           !state->cursor_observed.load(std::memory_order_acquire) &&
           SDL_GetTicks() < cursor_deadline)
        SDL_Delay(5);
    state->saw_cursor_during_prompt =
        state->cursor_observed.load(std::memory_order_acquire);

    if (state->saw_cursor_during_prompt)
    {
        if (state->accept)
            inject_text_input("POINTERNAME");
        // Map the prompt action through the current viewport on the main thread,
        // then move onto the action before clicking.
        const Uint64 click_deadline = SDL_GetTicks() + 5000;
        while (og::input_native::text_input_is_active() &&
               SDL_GetTicks() < click_deadline)
        {
            inject_mouse_motion(state->pointer_x, state->pointer_y);
            inject_mouse_down(state->pointer_x, state->pointer_y);
            inject_mouse_up(state->pointer_x, state->pointer_y);
            const Uint64 settle_deadline = SDL_GetTicks() + 300;
            while (og::input_native::text_input_is_active() &&
                   SDL_GetTicks() < settle_deadline)
                SDL_Delay(5);
        }
        state->pointer_closed.store(!og::input_native::text_input_is_active(),
                                    std::memory_order_release);
    }
    const auto prompt_hold = [] {
        if (!og::input_native::text_input_is_active())
            return false;
        inject_key_press(SDLK_ESCAPE, 10);
        return true;
    };
    return escape_to_the_main_thread(
        state->main_returned, state->pointer_closed.load() ? 0 : 1,
        "pointer action did not close the text prompt",
        std::span<const EscapeDoor>{}, prompt_hold);
}

static std::optional<std::string> run_prompt_with_cursor(bool initially_visible,
                                                         bool accept,
                                                         bool& saw_text_input,
                                                         bool& cursor_observed_in_prompt,
                                                         bool& cursor_visible_in_prompt)
{
    og::input_native::show_cursor(initially_visible);
    PromptCursorInjector state;
    state.accept = accept;
    SDL_Thread* const thread = SDL_CreateThread(injector_thread_prompt_cursor,
                                                "text_prompt_cursor", &state);
    if (thread == nullptr) {
        og::input_native::show_cursor(initially_visible);
        return std::nullopt;
    }

    text t(TEXT_1);
    std::optional<std::string> result =
        t.input_string_ex_value(58, 60, 29, "NAME THIS CHARACTER", "seed");
    state.main_returned.store(true, std::memory_order_release);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    SDL_PumpEvents();
    escape_tail_join_hygiene();
    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(state.pointer_closed.load(std::memory_order_acquire))
        << "the pointer action must close the prompt without Escape fallback";
    saw_text_input = state.saw_text_input;
    cursor_observed_in_prompt = state.saw_cursor_during_prompt;
    cursor_visible_in_prompt =
        state.cursor_visible.load(std::memory_order_acquire);
    return result;
}
} // namespace

TEST(TextInputExValue, prompt_dialog_grid_matches_new_company_naming)
{
    constexpr int kFieldX = 58;
    constexpr int kFieldY = 60;
    constexpr int kFieldWidth = 29 * 6;
    constexpr int kFieldHeight = 6;
    constexpr og::ui::PromptDialogLayout layout =
        og::ui::prompt_dialog_layout(
            kFieldX, kFieldY, kFieldWidth, kFieldHeight);
    constexpr og::ui::PromptActionLayout actions = layout.actions;

    EXPECT_EQ(kFieldX, actions.cancel.x);
    EXPECT_EQ(70, actions.cancel.y);
    EXPECT_EQ(81, actions.cancel.w);
    EXPECT_EQ(14, actions.cancel.h);
    EXPECT_EQ(151, actions.accept.x);
    EXPECT_EQ(actions.cancel.y, actions.accept.y);
    EXPECT_EQ(actions.cancel.w, actions.accept.w);
    EXPECT_EQ(actions.cancel.h, actions.accept.h);
    EXPECT_EQ(og::ui::kPromptActionGap,
              actions.accept.x - (actions.cancel.x + actions.cancel.w));
    EXPECT_EQ(kFieldX + kFieldWidth,
              actions.accept.x + actions.accept.w);
    EXPECT_EQ(5, layout.field.x - layout.frame.x);
    EXPECT_EQ(5, layout.frame.x + layout.frame.w -
                     (layout.field.x + layout.field.w));
    EXPECT_EQ(5, layout.frame.y + layout.frame.h -
                     (actions.accept.y + actions.accept.h))
        << "the prompt frame must contain the complete action row";
}

TEST(TextInputExValue, text_input_string_ex_value_accepts_backspace_then_text_and_return)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_backspace_text_and_return, "text_ex_backspace", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v = t.input_string_ex_value(10, 30, 16, "MSG", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(v.has_value()) << "input_string_ex_value should return a value";
    if (v.has_value())
    {
        ASSERT_TRUE(*v == "xy") << "backspace-first should clear seed and capture injected text";
    }
}


TEST(TextInputExValue, text_input_string_ex_value_escape_returns_nullopt)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_escape, "text_ex_escape", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v = t.input_string_ex_value(10, 30, 16, "MSG", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(!v.has_value()) << "escape should cancel and return nullopt";
}

TEST(TextInputExValue, text_input_string_ex_value_accept_button_returns_value)
{
    text t(TEXT_1);
    og::runtime::current_session->myscreen_->clearbuffer();

    SDL_Thread* th = SDL_CreateThread(injector_thread_accept_click,
                                      "text_ex_accept_click", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v =
        t.input_string_ex_value(58, 60, 29, "NAME THIS CHARACTER", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(v.has_value())
        << "the on-canvas ACCEPT affordance should commit the value";
    if (v.has_value()) {
        ASSERT_EQ("seed", *v) << "ACCEPT preserves an unchanged name";
    }
}

TEST(TextInputExValue, text_input_string_ex_value_cancel_button_returns_nullopt)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_cancel_click,
                                      "text_ex_cancel_click", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v =
        t.input_string_ex_value(58, 60, 29, "NAME THIS CHARACTER", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_FALSE(v.has_value())
        << "the on-canvas CANCEL affordance should preserve cancellation";
}

TEST(TextInputExValue, prompt_pointer_accepts_text_and_restores_hidden_cursor)
{
    bool saw_text_input = false;
    bool cursor_observed_in_prompt = false;
    bool cursor_visible_in_prompt = false;
    const std::optional<std::string> value =
        run_prompt_with_cursor(false, true, saw_text_input,
                               cursor_observed_in_prompt,
                               cursor_visible_in_prompt);

    ASSERT_TRUE(saw_text_input) << "injector must wait for the real prompt loop";
    EXPECT_TRUE(cursor_observed_in_prompt)
        << "main-thread cursor inspection callback should run during the prompt";
    ASSERT_TRUE(value.has_value()) << "pointer ACCEPT should commit the name";
    EXPECT_EQ("POINTERNAME", *value)
        << "text must be committed before the pointer accepts the prompt";
    EXPECT_TRUE(cursor_visible_in_prompt)
        << "the prompt must show the native cursor while text entry is active";
    EXPECT_FALSE(SDL_CursorVisible())
        << "the prompt should restore the cursor state that preceded entry";
}

TEST(TextInputExValue, prompt_cancel_restores_visible_cursor)
{
    bool saw_text_input = false;
    bool cursor_observed_in_prompt = false;
    bool cursor_visible_in_prompt = false;
    const std::optional<std::string> value =
        run_prompt_with_cursor(true, false, saw_text_input,
                               cursor_observed_in_prompt,
                               cursor_visible_in_prompt);

    ASSERT_TRUE(saw_text_input) << "injector must wait for the real prompt loop";
    EXPECT_TRUE(cursor_observed_in_prompt)
        << "main-thread cursor inspection callback should run during the prompt";
    EXPECT_FALSE(value.has_value()) << "pointer CANCEL should preserve cancellation";
    EXPECT_TRUE(cursor_visible_in_prompt)
        << "the prompt must show the native cursor while text entry is active";
    EXPECT_TRUE(SDL_CursorVisible())
        << "cancel should restore the cursor state that preceded entry";
}

TEST(TextInputExValue, prompt_hover_redraws_actions_and_accepts_held_click)
{
    PromptHoverInjector state;
    const std::optional<std::string> value = run_prompt_with_hover(true, state);
    ASSERT_TRUE(value.has_value()) << "hovered ACCEPT must commit the edited name";
    EXPECT_EQ("Z", *value)
        << "the rendered edit must be the value committed by ACCEPT";
}

TEST(TextInputExValue, prompt_hover_redraws_actions_and_cancels_in_letterboxed_viewport)
{
    auto* const session = og::runtime::current_session;
    const float old_window_w = session->window_w_;
    const float old_window_h = session->window_h_;
    const float old_overscan = session->overscan_percentage_;
    struct ViewportRestore
    {
        float window_w;
        float window_h;
        float overscan;
        ~ViewportRestore()
        {
            auto* const session = og::runtime::current_session;
            session->window_w_ = window_w;
            session->window_h_ = window_h;
            session->overscan_percentage_ = overscan;
            update_overscan_setting();
        }
    } restore{old_window_w, old_window_h, old_overscan};
    session->window_w_ = 800.0f;
    session->window_h_ = 400.0f;
    session->overscan_percentage_ = 0.0f;
    update_overscan_setting();

    PromptHoverInjector state;
    const std::optional<std::string> value = run_prompt_with_hover(false, state);
    EXPECT_FALSE(value.has_value())
        << "hovered CANCEL must preserve cancellation after the edit";
}
