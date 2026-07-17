#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
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
    bool* keys = nullptr;
    KeyStateGuard()
    {
        const bool* ro = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<bool*>(ro);
    }
    void set(SDL_Keycode key, bool pressed)
    {
        if (!keys) return;
        SDL_Scancode sc = SDL_GetScancodeFromKey(key, nullptr);
        if (sc >= 0 && sc < numkeys)
            keys[sc] = pressed;
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
    og::runtime::ensure_thread_session();
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
    og::runtime::ensure_thread_session();
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

    inject_text_input("Z");

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

int prompt_block_multiline_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started = true;

    SDL_Delay(90);
    inject_key_press(SDLK_RETURN, 20);    // split at the beginning
    inject_key_press(SDLK_BACKSPACE, 20); // merge the split line back
    inject_key_press(SDLK_RIGHT, 20);
    inject_key_press(SDLK_BACKSPACE, 20); // delete within a line
    for (int i = 0; i < 5; ++i)
        inject_key_press(SDLK_RETURN, 15);
    inject_key_press(SDLK_UP, 20);
    inject_key_press(SDLK_DOWN, 20);
    inject_key_press(SDLK_RIGHT, 20);
    inject_key_press(SDLK_DELETE, 20);

    SDL_Delay(40);
    MouseState& m = query_mouse_no_poll();
    m.x = 290.0f;
    m.y = 6.0f;
    m.left = true;
    SDL_Delay(40);
    m.left = false;

    st->finished = true;
    return 0;
}
} // namespace

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_escape_cancel)
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
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    bool accepted = prompt_for_string_block("Edit multi-line text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
    ASSERT_TRUE(accepted) << "ESC should exit prompt_for_string_block";
    ASSERT_TRUE(edited == original) << "ESC exit should preserve original text";
}


TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_done_button)
{
    std::list<std::string> original{"keep me"};
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, true};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_escape_injector, "prompt_block_done_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create done injector thread";

    bool accepted = prompt_for_string_block("Done prompt", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
    ASSERT_TRUE(accepted) << "DONE button should return true";
    ASSERT_TRUE(edited == original) << "done without edits should preserve content";
}


TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_editing_keys_and_text)
{
    std::list<std::string> edited{"abc", "xyz"};

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_editing_injector, "prompt_block_editing_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create editing injector thread";

    bool accepted = prompt_for_string_block("Edit text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
    ASSERT_TRUE(accepted) << "DONE button should accept after editing keys/text";
    ASSERT_TRUE(!edited.empty()) << "edited block should remain non-empty";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_multiline_editing_paths)
{
    std::list<std::string> edited{"abc"};

    PromptBlockInjectState st{};
    KeyStateGuard keyguard;
    PromptBlockInjectData inject_data{&st, &keyguard, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_multiline_injector, "prompt_block_multiline_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create multiline injector thread";

    bool accepted = prompt_for_string_block("Edit multiline text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
    ASSERT_TRUE(accepted) << "DONE button should accept multiline edits";
    ASSERT_GE(edited.size(), 2u) << "return key should create multiple lines";
}
