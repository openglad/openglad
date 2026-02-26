#include "SDL.h"
#include <openglad/interface/render/text.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <optional>
#include <string>

namespace
{
static int injector_thread_backspace_text_and_return(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    // Force "first key is backspace" branch (has_typed=0, current_length>0 when begin is non-empty),
    // then type a few characters and commit with Return.
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_BACKSPACE;
    SDL_PushEvent(&ev);

    SDL_Delay(10);
    ev = SDL_Event{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_LEFT; // deselect whole line path
    SDL_PushEvent(&ev);

    SDL_Delay(10);
    ev = SDL_Event{};
    ev.type = SDL_TEXTINPUT;
    SDL_strlcpy(ev.text.text, "xy", sizeof(ev.text.text));
    SDL_PushEvent(&ev);

    SDL_Delay(10);
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
} // namespace

void test_text_input_string_ex_value_accepts_backspace_then_text_and_return()
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_backspace_text_and_return, "text_ex_backspace", nullptr);
    TEST_ASSERT(th != nullptr, "injector thread should start");

    std::optional<std::string> v = t.input_string_ex_value(10, 30, 16, "MSG", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    TEST_ASSERT(v.has_value(), "input_string_ex_value should return a value");
    if (v.has_value())
        TEST_ASSERT(*v == "xy", "backspace-first should clear seed and capture injected text");
}
REGISTER_TEST(test_text_input_string_ex_value_accepts_backspace_then_text_and_return);

void test_text_input_string_ex_value_escape_returns_nullopt()
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(injector_thread_escape, "text_ex_escape", nullptr);
    TEST_ASSERT(th != nullptr, "injector thread should start");

    std::optional<std::string> v = t.input_string_ex_value(10, 30, 16, "MSG", "seed");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    TEST_ASSERT(!v.has_value(), "escape should cancel and return nullopt");
}
REGISTER_TEST(test_text_input_string_ex_value_escape_returns_nullopt);
