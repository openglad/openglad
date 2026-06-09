#include "SDL.h"
#include <openglad/legacy/base.h>
#include <openglad/interface/render/text.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>


static int injector_thread_return(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    // Enter some text, then commit with Return.
    SDL_Event ev{};
    ev.type = SDL_TEXTINPUT;
    SDL_strlcpy(ev.text.text, "ab", sizeof(ev.text.text));
    SDL_PushEvent(&ev);

    SDL_Delay(20);
    ev = SDL_Event{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_RETURN;
    SDL_PushEvent(&ev);
    return 0;
}

static int injector_thread_escape(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&ev);
    return 0;
}

TEST(TextInputAndWidth, text_query_width_big_font_varies_by_case)
{
    text big(TEXT_BIG);
    const Sint32 wA = big.query_width("A");
    const Sint32 wa = big.query_width("a");
    ASSERT_TRUE(wA > 0 && wa > 0) << "query_width should be positive";
    // Uppercase path uses sizex, non-uppercase uses (sizex-1) in big-font mode.
    ASSERT_TRUE(wA != wa) << "uppercase and lowercase should have different widths in big-font mode";
}


TEST(TextInputAndWidth, text_write_variants_smoke)
{
    text t(TEXT_1);
    ASSERT_GT(t.write_xy(10, 10, "Hi", WHITE), 0);
    ASSERT_GT(t.write_xy_shadow(10, 20, WHITE, "%s", "Shadow"), 0);
    ASSERT_GT(t.write_xy_center(160, 30, WHITE, "%s", "Center"), 0);
    ASSERT_GT(t.write_xy_center_alpha(160, 40, WHITE, 128, "%s", "Alpha"), 0);
    ASSERT_GT(t.write_xy_center_shadow(160, 50, WHITE, "%s", "CenterShadow"), 0);
    ASSERT_EQ(1, t.write_char_xy_alpha(10, 60, 'Z', WHITE, 128));
}


TEST(TextInputAndWidth, text_input_string_value_accepts_textinput_and_return)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_return, "text_inject_return", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v = t.input_string_value(10, 10, 16, "");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(v.has_value()) << "input_string_value should return a value";
    if (v.has_value())
    {
        ASSERT_TRUE(*v == "ab") << "input_string_value should capture injected text";
    }
}


TEST(TextInputAndWidth, text_input_string_value_escape_returns_nullopt)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_escape, "text_inject_escape", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v = t.input_string_value(10, 10, 16, "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(!v.has_value()) << "escape should cancel and return nullopt";
}
