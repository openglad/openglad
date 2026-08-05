#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <list>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// From level_editor.cpp
bool prompt_for_string_block(const std::string& message, std::list<std::string>& result);
void level_editor_testing_prompt_block_input_reset();
void level_editor_testing_prompt_block_set_held_key(int key_state);
void level_editor_testing_prompt_block_click(int x, int y);
std::uint64_t level_editor_testing_prompt_block_entered_count();
std::uint64_t level_editor_testing_prompt_block_input_observed_count();
std::uint64_t level_editor_testing_prompt_block_input_completed_count();

namespace
{
struct PromptBlockInjectState
{
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> handshake_failed{false};
};

struct PromptBlockInjectData
{
    PromptBlockInjectState* state = nullptr;
    bool click_cancel = false;
    bool click_done = false;
};

bool wait_for_counter_advance(std::uint64_t (*counter)(),
                              std::uint64_t baseline)
{
    constexpr Uint64 kHandshakeTimeoutMs = 5000;
    const Uint64 started_at = SDL_GetTicks();
    while (counter() <= baseline)
    {
        if (SDL_GetTicks() - started_at >= kHandshakeTimeoutMs)
            return false;
        SDL_Delay(1);
    }
    return true;
}

bool inject_prompt_key_press(int keycode)
{
    const std::uint64_t completed_before =
        level_editor_testing_prompt_block_input_completed_count();
    inject_key_down(keycode);
    inject_key_up(keycode);
    return wait_for_counter_advance(
        level_editor_testing_prompt_block_input_completed_count,
        completed_before);
}

bool inject_prompt_text(const char* text)
{
    const std::uint64_t completed_before =
        level_editor_testing_prompt_block_input_completed_count();
    inject_text_input(text);
    return wait_for_counter_advance(
        level_editor_testing_prompt_block_input_completed_count,
        completed_before);
}

bool click_prompt_button(int x, int y)
{
    const std::uint64_t completed_before =
        level_editor_testing_prompt_block_input_completed_count();
    level_editor_testing_prompt_block_click(x, y);
    return wait_for_counter_advance(
        level_editor_testing_prompt_block_input_completed_count,
        completed_before);
}

void fail_safe_cancel_prompt()
{
    // Do not wait for an acknowledgement here: this is reached only after a
    // handshake timeout, and leaving the click pending lets the prompt consume
    // it as soon as its polling loop is able to make progress.
    level_editor_testing_prompt_block_click(220, 6);
}

bool pulse_physical_key(int key_state)
{
    const std::uint64_t observed_before =
        level_editor_testing_prompt_block_input_observed_count();
    const std::uint64_t completed_before =
        level_editor_testing_prompt_block_input_completed_count();

    level_editor_testing_prompt_block_set_held_key(key_state);
    const bool observed = wait_for_counter_advance(
        level_editor_testing_prompt_block_input_observed_count,
        observed_before);
    level_editor_testing_prompt_block_set_held_key(-1);
    if (!observed)
        return false;

    return wait_for_counter_advance(
        level_editor_testing_prompt_block_input_completed_count,
        completed_before);
}

int prompt_block_escape_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started.store(true, std::memory_order_release);

    bool ok = wait_for_counter_advance(
        level_editor_testing_prompt_block_entered_count, 0);
    if (d->click_cancel || d->click_done)
    {
        if (ok)
            ok = click_prompt_button(d->click_cancel ? 220 : 290, 6);
    }
    else if (ok)
        ok = pulse_physical_key(KEYSTATE_ESCAPE);

    if (!ok)
        fail_safe_cancel_prompt();
    st->handshake_failed.store(!ok, std::memory_order_release);
    st->finished.store(true, std::memory_order_release);
    return 0;
}

int prompt_block_editing_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started.store(true, std::memory_order_release);

    bool ok = wait_for_counter_advance(
        level_editor_testing_prompt_block_entered_count, 0);
    constexpr std::array<int, 6> kKeys{
        SDLK_LEFT, SDLK_RIGHT, SDLK_DOWN,
        SDLK_UP, SDLK_LEFT, SDLK_DELETE,
    };
    for (const int key : kKeys)
    {
        if (!ok)
            break;
        ok = inject_prompt_key_press(key);
    }
    if (ok)
        ok = inject_prompt_text("Z");
    if (ok)
        ok = click_prompt_button(290, 6);

    if (!ok)
        fail_safe_cancel_prompt();
    st->handshake_failed.store(!ok, std::memory_order_release);
    st->finished.store(true, std::memory_order_release);
    return 0;
}

int prompt_block_multiline_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started.store(true, std::memory_order_release);

    bool ok = wait_for_counter_advance(
        level_editor_testing_prompt_block_entered_count, 0);
    const auto press = [&ok](int key) {
        if (ok)
            ok = inject_prompt_key_press(key);
    };
    press(SDLK_RETURN);    // split at the beginning
    press(SDLK_BACKSPACE); // merge the split line back
    press(SDLK_RIGHT);
    press(SDLK_BACKSPACE); // delete within a line
    for (int i = 0; i < 5; ++i)
        press(SDLK_RETURN);
    press(SDLK_UP);
    press(SDLK_DOWN);
    press(SDLK_RIGHT);
    press(SDLK_DELETE);
    if (ok)
        ok = click_prompt_button(290, 6);

    if (!ok)
        fail_safe_cancel_prompt();
    st->handshake_failed.store(!ok, std::memory_order_release);
    st->finished.store(true, std::memory_order_release);
    return 0;
}

int prompt_block_physical_navigation_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PromptBlockInjectData* d = static_cast<PromptBlockInjectData*>(data);
    PromptBlockInjectState* st = d->state;
    st->started.store(true, std::memory_order_release);

    // Start at line 0, column 0.  Drive the held-key paths one at a time so
    // every mutation is acknowledged by the prompt's polling loop before the
    // key is released and the next one begins.
    bool ok = wait_for_counter_advance(
        level_editor_testing_prompt_block_entered_count, 0);
    constexpr std::array<int, 14> kSteps{
        KEYSTATE_RIGHT,  // column 1
        KEYSTATE_DELETE, // "ab" -> "a"
        KEYSTATE_DOWN,   // line 1, column 1
        KEYSTATE_LEFT,   // line 1, column 0
        KEYSTATE_LEFT,   // previous line, at end
        KEYSTATE_RIGHT,  // next line, column 0
        KEYSTATE_RIGHT,  // line 1, column 1
        KEYSTATE_RIGHT,  // clamp at final column
        KEYSTATE_UP,     // line 0
        KEYSTATE_UP,     // already at the top
        KEYSTATE_DOWN,   // line 1
        KEYSTATE_DOWN,   // already at the bottom
        KEYSTATE_DELETE, // no character at end
        KEYSTATE_LEFT,   // insert at column 0
    };
    for (const int key_state : kSteps)
    {
        if (!ok)
            break;
        ok = pulse_physical_key(key_state);
    }

    if (ok)
        ok = inject_prompt_text("Z");
    const bool click_consumed =
        ok && click_prompt_button(290, 6);
    if (!ok || !click_consumed)
        fail_safe_cancel_prompt();

    st->handshake_failed.store(!ok || !click_consumed,
                               std::memory_order_release);
    st->finished.store(true, std::memory_order_release);
    return 0;
}
} // namespace

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_escape_cancel)
{
    (void)og::runtime::current_session->myscreen_;
    level_editor_testing_prompt_block_input_reset();

    std::list<std::string> original{
        "Line one",
        "Line two",
    };
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_escape_injector, "prompt_block_escape_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    bool accepted = prompt_for_string_block("Edit multi-line text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started.load(std::memory_order_acquire))
        << "injector should have started";
    ASSERT_TRUE(st.finished.load(std::memory_order_acquire))
        << "injector should have finished";
    ASSERT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    ASSERT_TRUE(accepted) << "ESC should exit prompt_for_string_block";
    ASSERT_TRUE(edited == original) << "ESC exit should preserve original text";
}


TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_done_button)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> original{"keep me"};
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, true};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_escape_injector, "prompt_block_done_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create done injector thread";

    bool accepted = prompt_for_string_block("Done prompt", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started.load(std::memory_order_acquire))
        << "injector should have started";
    ASSERT_TRUE(st.finished.load(std::memory_order_acquire))
        << "injector should have finished";
    ASSERT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    ASSERT_TRUE(accepted) << "DONE button should return true";
    ASSERT_TRUE(edited == original) << "done without edits should preserve content";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_cancel_button_restores_original)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> original{"first", "second"};
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, true, false};
    SDL_Thread* thread = SDL_CreateThread(
        prompt_block_escape_injector, "prompt_block_cancel_injector",
        &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create cancel injector";

    const bool accepted =
        prompt_for_string_block("Cancel prompt", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(st.started.load(std::memory_order_acquire));
    EXPECT_TRUE(st.finished.load(std::memory_order_acquire));
    EXPECT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    EXPECT_TRUE(accepted);
    EXPECT_EQ(original, edited)
        << "CANCEL must discard every in-progress prompt edit";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_empty_input_creates_editable_line)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited;

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, true};
    SDL_Thread* thread = SDL_CreateThread(
        prompt_block_escape_injector, "prompt_block_empty_done_injector",
        &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create empty-input injector";

    const bool accepted =
        prompt_for_string_block("Empty prompt", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(st.started.load(std::memory_order_acquire));
    EXPECT_TRUE(st.finished.load(std::memory_order_acquire));
    EXPECT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    EXPECT_TRUE(accepted);
    ASSERT_EQ(1u, edited.size());
    EXPECT_TRUE(edited.front().empty())
        << "an empty block still exposes one editable line";
}


TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_editing_keys_and_text)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"abc", "xyz"};

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_editing_injector, "prompt_block_editing_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create editing injector thread";

    bool accepted = prompt_for_string_block("Edit text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started.load(std::memory_order_acquire))
        << "injector should have started";
    ASSERT_TRUE(st.finished.load(std::memory_order_acquire))
        << "injector should have finished";
    ASSERT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    ASSERT_TRUE(accepted) << "DONE button should accept after editing keys/text";
    ASSERT_TRUE(!edited.empty()) << "edited block should remain non-empty";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_held_navigation_edits_exact_lines)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"ab", "c"};

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, false};
    SDL_Thread* thread = SDL_CreateThread(
        prompt_block_physical_navigation_injector,
        "prompt_block_physical_navigation", &inject_data);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create physical-navigation injector";

    const bool accepted =
        prompt_for_string_block("Navigate text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(st.started.load(std::memory_order_acquire));
    EXPECT_TRUE(st.finished.load(std::memory_order_acquire));
    EXPECT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    EXPECT_TRUE(accepted);
    EXPECT_EQ(16u, level_editor_testing_prompt_block_input_observed_count());
    EXPECT_EQ(16u, level_editor_testing_prompt_block_input_completed_count());
    EXPECT_EQ((std::list<std::string>{"a", "Zc"}), edited)
        << "held arrow/delete input must move across lines and edit at the cursor";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_multiline_editing_paths)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"abc"};

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, false};
    SDL_Thread* thread = SDL_CreateThread(prompt_block_multiline_injector, "prompt_block_multiline_injector", &inject_data);
    ASSERT_TRUE(thread != nullptr) << "failed to create multiline injector thread";

    bool accepted = prompt_for_string_block("Edit multiline text", edited);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started.load(std::memory_order_acquire))
        << "injector should have started";
    ASSERT_TRUE(st.finished.load(std::memory_order_acquire))
        << "injector should have finished";
    ASSERT_FALSE(st.handshake_failed.load(std::memory_order_acquire));
    ASSERT_TRUE(accepted) << "DONE button should accept multiline edits";
    ASSERT_GE(edited.size(), 2u) << "return key should create multiple lines";
}
