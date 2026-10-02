#include <SDL3/SDL.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include "test_input_helpers.h"
#include "test_escape_tail.h"
#include "test_prompt_hover.h"
#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <span>
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

// ---------------------------------------------------------------------------
// Issue #328 fold guard: input_string and input_string_ex share one edit core
// but NOT one draw. The plain prompt (hire-rename at 176,20, company name)
// draws only its field box; the dialog frame, ACCEPT and CANCEL belong to the
// _ex prompt alone. Folding input_string into "_ex with an empty message"
// would paint that chrome over the caller's hand-drawn box, so the plain
// prompt is captured while it blocks and compared with the _ex prompt at the
// same geometry (the paired control that proves the probe can see chrome).
// ---------------------------------------------------------------------------
namespace
{
constexpr Sint32 kChromeX = 58;
constexpr Sint32 kChromeY = 60;
constexpr short kChromeLen = 29;
constexpr unsigned char kChromeBackdrop = 77;
constexpr unsigned char kPlainFieldColor = 13; // input_string's default backcolor

struct ChromeProbeRun
{
    PromptHoverProbe probe;
    std::atomic<bool> main_returned{false};
    bool saw_text_input = false;
    bool captured = false;
};

int injector_thread_chrome_probe(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const run = static_cast<ChromeProbeRun*>(data);
    const Uint64 active_deadline = SDL_GetTicks() + 5000;
    while (!og::input_native::text_input_is_active() &&
           SDL_GetTicks() < active_deadline)
        SDL_Delay(5);
    run->saw_text_input = og::input_native::text_input_is_active();
    if (run->saw_text_input)
        run->captured = prompt_hover_capture(run->probe);

    const auto prompt_hold = [] {
        if (!og::input_native::text_input_is_active())
            return false;
        inject_key_press(SDLK_ESCAPE, 10);
        return true;
    };
    return escape_to_the_main_thread(
        run->main_returned, run->captured ? 0 : 1,
        "the blocked prompt was never captured",
        std::span<const EscapeDoor>{}, prompt_hold);
}

PromptHoverPixel palette_rgb(unsigned char index)
{
    const auto& palette = og::runtime::current_session->myscreen_->ourpalette;
    const size_t offset = static_cast<size_t>(index) * 3;
    return {static_cast<Uint8>(palette[offset] * 4),
            static_cast<Uint8>(palette[offset + 1] * 4),
            static_cast<Uint8>(palette[offset + 2] * 4)};
}

// Paints a known backdrop, opens one prompt at the chrome geometry, captures
// the action rects (and their one-pixel surround) plus row y+1 of the field
// from x-1 to one pixel past the field box while the prompt blocks, then
// Escapes out. Returns the prompt's own result so the caller can confirm it
// was the cancel path.
std::optional<std::string> capture_prompt_chrome(bool extended,
                                                 ChromeProbeRun& run,
                                                 Sint32& field_width,
                                                 std::vector<PromptHoverRect>& buttons)
{
    text t(TEXT_1);
    screen* const out = og::runtime::current_session->myscreen_;
    out->fastbox(0, 0, 320, 200, kChromeBackdrop);
    field_width = kChromeLen * (t.sizex + 1);
    const og::ui::PromptDialogLayout layout =
        og::ui::prompt_dialog_layout(kChromeX, kChromeY, field_width, t.sizey);
    buttons = {
        {layout.actions.accept.x, layout.actions.accept.y,
         layout.actions.accept.w, layout.actions.accept.h},
        {layout.actions.cancel.x, layout.actions.cancel.y,
         layout.actions.cancel.w, layout.actions.cancel.h},
    };
    // Field row: x-1 .. x+field_width+1, one pixel high.
    init_prompt_hover_probe(run.probe, buttons,
                            {kChromeX - 1, kChromeY + 1, field_width + 3, 1});

    SDL_Thread* const thread = SDL_CreateThread(
        injector_thread_chrome_probe, "text_chrome_probe", &run);
    if (thread == nullptr)
    {
        ADD_FAILURE() << "failed to create the chrome probe injector";
        return std::nullopt;
    }
    std::optional<std::string> result = extended
        ? t.input_string_ex_value(kChromeX, kChromeY, kChromeLen, "NAME", "")
        : t.input_string_value(kChromeX, kChromeY, kChromeLen, "");
    run.main_returned.store(true, std::memory_order_release);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    SDL_PumpEvents();
    escape_tail_join_hygiene();
    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(run.saw_text_input)
        << "the injector must wait for the real blocking prompt";
    EXPECT_TRUE(run.captured)
        << "the capture must run on the main thread while the prompt blocks";
    return result;
}
} // namespace

TEST(TextInputAndWidth, plain_prompt_draws_no_dialog_chrome)
{
    const PromptHoverPixel backdrop = palette_rgb(kChromeBackdrop);
    const PromptHoverPixel field = palette_rgb(kPlainFieldColor);
    ASSERT_NE(backdrop, field) << "the backdrop must be distinguishable from the field";

    // Control: the _ex prompt at the same geometry covers every probed
    // action pixel with its frame and buttons.
    {
        ChromeProbeRun run;
        Sint32 field_width = 0;
        std::vector<PromptHoverRect> buttons;
        const std::optional<std::string> value =
            capture_prompt_chrome(true, run, field_width, buttons);
        ASSERT_FALSE(value.has_value()) << "Escape cancels the _ex prompt";
        ASSERT_TRUE(run.captured);
        size_t backdrop_action_pixels = 0;
        for (size_t i = 0; i < run.probe.field_begin; ++i)
            if (run.probe.pixels[i] == backdrop)
                ++backdrop_action_pixels;
        EXPECT_EQ(0u, backdrop_action_pixels)
            << "the _ex prompt draws its frame and ACCEPT/CANCEL over every probed pixel";
    }

    // Rule: the plain prompt leaves every one of those pixels untouched and
    // draws exactly its field box, x .. x + maxlength * (sizex + 1) inclusive.
    ChromeProbeRun run;
    Sint32 field_width = 0;
    std::vector<PromptHoverRect> buttons;
    const std::optional<std::string> value =
        capture_prompt_chrome(false, run, field_width, buttons);
    ASSERT_FALSE(value.has_value()) << "Escape cancels the plain prompt";
    ASSERT_TRUE(run.captured);
    size_t backdrop_action_pixels = 0;
    for (size_t i = 0; i < run.probe.field_begin; ++i)
        if (run.probe.pixels[i] == backdrop)
            ++backdrop_action_pixels;
    EXPECT_EQ(run.probe.field_begin, backdrop_action_pixels)
        << "input_string must not draw the dialog frame or ACCEPT/CANCEL";

    const size_t row = run.probe.field_begin;
    ASSERT_EQ(static_cast<size_t>(field_width + 3), run.probe.pixels.size() - row);
    EXPECT_EQ(backdrop, run.probe.pixels[row])
        << "the pixel left of the field (x-1) is untouched backdrop";
    for (Sint32 dx = 0; dx <= field_width; ++dx)
        EXPECT_EQ(field, run.probe.pixels[row + 1 + static_cast<size_t>(dx)])
            << "field box pixel x+" << dx << " is the field colour";
    EXPECT_EQ(backdrop, run.probe.pixels[row + 2 + static_cast<size_t>(field_width)])
        << "the pixel right of the field (x+maxlength*(sizex+1)+1) is untouched backdrop";
}
