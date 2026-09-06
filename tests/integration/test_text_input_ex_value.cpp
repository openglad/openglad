#include <openglad/interface/render/text.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

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
