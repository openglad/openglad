#include <SDL3/SDL.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/text.h>
#include "test_input_helpers.h"
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>


static int injector_thread_return(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    // Enter some text, then commit with Return.
    inject_text_input("ab");

    SDL_Delay(20);
    SDL_Event ev{};
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

static int injector_thread_utf8_backspace(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(50);

    inject_text_input("A\xc3\xa9");

    SDL_Delay(20);
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_BACKSPACE;
    SDL_PushEvent(&ev);

    SDL_Delay(20);
    ev = SDL_Event{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
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

TEST(TextInputAndWidth, backspace_removes_one_complete_utf8_codepoint)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(
        injector_thread_utf8_backspace, "text_inject_utf8_backspace", nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v = t.input_string_value(10, 10, 16, "");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "A");
}

TEST(TextInputAndWidth, extended_prompt_backspace_removes_complete_utf8_codepoint)
{
    text t(TEXT_1);

    SDL_Thread* th = SDL_CreateThread(
        injector_thread_utf8_backspace,
        "text_inject_ex_utf8_backspace",
        nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread should start";

    std::optional<std::string> v =
        t.input_string_ex_value(10, 20, 16, "ROOM CODE", "");
    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);

    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "A");
}

namespace
{
// One injector for the "seed selection" rules: an optional key before the
// Backspace, then Return.
SDL_Keycode g_pre_backspace_key = SDLK_UNKNOWN;

int injector_thread_seed_backspace(void*)
{
    og::runtime::ensure_thread_session();
    SDL_Delay(50);
    if (g_pre_backspace_key != SDLK_UNKNOWN)
    {
        SDL_Event pre{};
        pre.type = SDL_EVENT_KEY_DOWN;
        pre.key.key = g_pre_backspace_key;
        SDL_PushEvent(&pre);
        SDL_Delay(20);
    }
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_BACKSPACE;
    SDL_PushEvent(&ev);
    SDL_Delay(20);
    ev = SDL_Event{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    SDL_PushEvent(&ev);
    return 0;
}

// SDL truncates a pushed SDL_EVENT_TEXT_INPUT payload at 32 bytes, so a long
// value arrives the way a real IME/paste does: several chunks.
std::vector<std::string> g_text_chunks;

int injector_thread_chunks(void*)
{
    og::runtime::ensure_thread_session();
    SDL_Delay(50);
    for (const std::string& chunk : g_text_chunks)
    {
        inject_text_input(chunk.c_str());
        SDL_Delay(30);
    }
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = SDLK_RETURN;
    SDL_PushEvent(&ev);
    return 0;
}
} // namespace

// A prompt opens with the previous value pre-selected, the way a rename box
// does: the FIRST key replaces the whole thing. Backspace as that first key
// therefore clears the entire seed, not one character. Any cursor key cancels
// the selection first, so the same Backspace afterwards deletes one character
// and the rest of the old value survives.
TEST(TextInputAndWidth, first_backspace_clears_the_seed_but_a_cursor_key_deselects_it)
{
    text t(TEXT_1);

    g_pre_backspace_key = SDLK_UNKNOWN;
    SDL_Thread* th = SDL_CreateThread(injector_thread_seed_backspace,
                                      "text_seed_backspace", nullptr);
    ASSERT_NE(nullptr, th);
    std::optional<std::string> cleared = t.input_string_value(10, 10, 16, "abc");
    SDL_WaitThread(th, nullptr);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_EQ("", *cleared)
        << "Backspace as the first key clears the whole selected seed";

    g_pre_backspace_key = SDLK_LEFT;
    th = SDL_CreateThread(injector_thread_seed_backspace,
                          "text_deselect_backspace", nullptr);
    ASSERT_NE(nullptr, th);
    std::optional<std::string> trimmed = t.input_string_value(10, 10, 16, "abc");
    SDL_WaitThread(th, nullptr);
    g_pre_backspace_key = SDLK_UNKNOWN;
    ASSERT_TRUE(trimmed.has_value());
    EXPECT_EQ("ab", *trimmed)
        << "a cursor key deselects, so Backspace then removes one character";
}

// input_string edits inside a fixed 100-byte buffer. A caller that asks for a
// longer field (a pasted room code, a caller passing a pixel width by mistake)
// must be clamped to what the buffer holds, or typing past 99 characters
// writes off the end of it. The limit is otherwise exactly the caller's:
// maxlength characters INCLUDING the terminator.
TEST(TextInputAndWidth, oversized_maxlength_clamps_to_the_edit_buffer)
{
    text t(TEXT_1);

    // 98 characters is everything the 100-byte buffer can hold beside its
    // terminator; the 99th is refused.
    g_text_chunks = {std::string(31, 'x'), std::string(31, 'x'),
                     std::string(31, 'x'), std::string(5, 'x'), "y"};
    SDL_Thread* th = SDL_CreateThread(injector_thread_chunks,
                                      "text_overlong", nullptr);
    ASSERT_NE(nullptr, th);
    std::optional<std::string> clamped = t.input_string_value(10, 10, 500, "");
    SDL_WaitThread(th, nullptr);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_EQ(98u, clamped->size())
        << "maxlength 500 must be clamped to the 100-byte edit buffer";
    EXPECT_EQ(std::string(98, 'x'), *clamped);

    // The same rule at a caller's own small limit: 11 characters fit a
    // 12-character field, and the 12th is refused.
    g_text_chunks = {std::string(11, 'z'), "w"};
    th = SDL_CreateThread(injector_thread_chunks, "text_small_limit",
                          nullptr);
    ASSERT_NE(nullptr, th);
    std::optional<std::string> small = t.input_string_value(10, 10, 12, "");
    SDL_WaitThread(th, nullptr);
    ASSERT_TRUE(small.has_value());
    EXPECT_EQ(std::string(11, 'z'), *small)
        << "a small field keeps exactly maxlength-1 characters";
}
