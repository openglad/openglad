#include <openglad/input/input.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include "test_input_helpers.h"

#include <list>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// From level_editor.cpp
bool prompt_for_string_block(const std::string& message, std::list<std::string>& result);

namespace
{
struct PromptBlockInjectState
{
    bool started = false;
    bool finished = false;
};

struct KeyStateGuard
{
    int numkeys = 0;
    Uint8* keys = nullptr;
    KeyStateGuard()
    {
        const Uint8* ro = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<Uint8*>(ro);
    }
    void set(SDL_Keycode key, bool pressed)
    {
        if (!keys) return;
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        if (sc >= 0 && sc < numkeys)
            keys[sc] = pressed ? 1 : 0;
    }
};

struct PromptBlockInjectData
{
    PromptBlockInjectState* state = nullptr;
    KeyStateGuard* keyguard = nullptr;
    bool click_cancel = false;
    bool click_done = false;
};

int prompt_block_escape_injector(void* data)
{
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started = true;

    SDL_Delay(80);
    if (d->click_cancel || d->click_done)
    {
        MouseState& m = query_mouse_no_poll();
        m.x = d->click_cancel ? 220.0f : 290.0f;
        m.y = 6.0f;
        m.left = true;
        SDL_Delay(40);
        m.left = false;
    }
    else
    {
        d->keyguard->set(SDLK_ESCAPE, true);
        SDL_Delay(80);
        d->keyguard->set(SDLK_ESCAPE, false);
    }

    st->finished = true;
    return 0;
}

int prompt_block_editing_injector(void* data)
{
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started = true;

    SDL_Delay(90);
    inject_key_press(SDLK_LEFT, 20);
    inject_key_press(SDLK_RIGHT, 20);
    inject_key_press(SDLK_DOWN, 20);
    inject_key_press(SDLK_UP, 20);
    inject_key_press(SDLK_LEFT, 20);
    inject_key_press(SDLK_DELETE, 20);

    SDL_Event text_event{};
    text_event.type = SDL_TEXTINPUT;
    SDL_strlcpy(text_event.text.text, "Z", sizeof(text_event.text.text));
    SDL_PushEvent(&text_event);

    SDL_Delay(40);
    MouseState& m = query_mouse_no_poll();
    m.x = 290.0f; // DONE button
    m.y = 6.0f;
    m.left = true;
    SDL_Delay(40);
    m.left = false;

    st->finished = true;
    return 0;
}
} // namespace

void test_level_editor_prompt_for_string_block_escape_cancel()
{
    (void)og::runtime::current_session->myscreen_;

    std::list<std::string> original{
        "Line one",
        "Line two",
    };
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_escape_injector, "prompt_block_escape_injector", &inject_data);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    bool accepted = prompt_for_string_block("Edit multi-line text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    TEST_ASSERT(st.started, "injector should have started");
    TEST_ASSERT(st.finished, "injector should have finished");
    TEST_ASSERT(accepted, "ESC should exit prompt_for_string_block");
    TEST_ASSERT(edited == original, "ESC exit should preserve original text");
}
REGISTER_TEST(test_level_editor_prompt_for_string_block_escape_cancel);

void test_level_editor_prompt_for_string_block_done_button()
{
    std::list<std::string> original{"keep me"};
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, true};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_escape_injector, "prompt_block_done_injector", &inject_data);
    TEST_ASSERT(thread != nullptr, "failed to create done injector thread");

    bool accepted = prompt_for_string_block("Done prompt", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    TEST_ASSERT(st.started, "injector should have started");
    TEST_ASSERT(st.finished, "injector should have finished");
    TEST_ASSERT(accepted, "DONE button should return true");
    TEST_ASSERT(edited == original, "done without edits should preserve content");
}
REGISTER_TEST(test_level_editor_prompt_for_string_block_done_button);

void test_level_editor_prompt_for_string_block_editing_keys_and_text()
{
    std::list<std::string> edited{"abc", "xyz"};

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_editing_injector, "prompt_block_editing_injector", &inject_data);
    TEST_ASSERT(thread != nullptr, "failed to create editing injector thread");

    bool accepted = prompt_for_string_block("Edit text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    TEST_ASSERT(st.started, "injector should have started");
    TEST_ASSERT(st.finished, "injector should have finished");
    TEST_ASSERT(accepted, "DONE button should accept after editing keys/text");
    TEST_ASSERT(!edited.empty(), "edited block should remain non-empty");
}
REGISTER_TEST(test_level_editor_prompt_for_string_block_editing_keys_and_text);
