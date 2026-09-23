#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_escape_tail.h"
#include "test_prompt_hover.h"
#include "test_frame_capture.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
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
    // Text typed into the prompt before the exit is triggered. Without an edit
    // in flight, DONE/ESC (which keep edits) and CANCEL (which restores
    // original_text) all leave the caller's list untouched, so the exit
    // branches are indistinguishable.
    const char* type_text = nullptr;
};

struct PromptBlockHoverState
{
    bool click_done = true;
    PromptHoverProbe probe;
    std::vector<PromptHoverRect> buttons;
    PromptWindowPoint outside;
    PromptWindowPoint done_point;
    PromptWindowPoint cancel_point;
    std::vector<PromptHoverPixel> baseline;
    std::atomic<bool> main_returned{false};
    std::atomic<bool> pointer_closed{false};
    std::atomic<bool> handshake_failed{false};
    bool baseline_ready = false;
    bool faces_and_bevels_ready = false;
    bool arbitrary_backdrop_ready = false;
    bool moved_done = false;
    bool moved_cancel = false;
    bool moved_outside = false;
    bool moved_back = false;
    bool field_changed = false;
    bool stationary_ring = false;
    bool capture_failed = false;
};

bool wait_for_counter_advance(std::uint64_t (*counter)(),
                              std::uint64_t baseline);
bool inject_prompt_text(const char* text);

bool prompt_block_hover_faces_ready(const PromptHoverProbe& probe,
                                    const std::vector<PromptHoverRect>& buttons)
{
    const PromptHoverPixel yellow = prompt_hover_yellow_rgb();
    const auto pixel_at = [&probe](int x, int y) -> const PromptHoverPixel* {
        for (size_t i = 0; i < probe.points.size(); ++i)
            if (probe.points[i].x == x && probe.points[i].y == y)
                return &probe.pixels[i];
        return nullptr;
    };
    for (const PromptHoverRect& button : buttons)
    {
        const PromptHoverPixel* const face =
            pixel_at(button.x + 2, button.y + 2);
        const PromptHoverPixel* const bevel = pixel_at(button.x + 2, button.y);
        if (face == nullptr || bevel == nullptr || *face == yellow ||
            *bevel == yellow || *face == *bevel)
            return false;
    }
    return true;
}

bool prompt_block_hover_arbitrary_backdrop_ready(
    const PromptHoverProbe& probe)
{
    constexpr std::array<PromptHoverPoint, 6> kSeedPoints{{
        {267, 1}, {267, 5}, {267, 10}, {267, 14}, {215, 0}, {319, 0},
    }};
    std::array<PromptHoverPixel, kSeedPoints.size()> sampled{};
    bool saw_off_palette = false;
    const auto& prompt_palette = og::runtime::current_session->myscreen_->ourpalette;
    for (size_t i = 0; i < kSeedPoints.size(); ++i)
    {
        bool found = false;
        for (size_t j = 0; j < probe.points.size(); ++j)
            if (probe.points[j].x == kSeedPoints[i].x &&
                probe.points[j].y == kSeedPoints[i].y)
            {
                sampled[i] = probe.pixels[j];
                found = true;
                break;
            }
        if (!found)
            return false;
        bool palette_match = false;
        for (size_t color = 0; color < 256; ++color)
        {
            const size_t offset = color * 3;
            const PromptHoverPixel palette_rgb{
                static_cast<Uint8>(prompt_palette[offset] * 4),
                static_cast<Uint8>(prompt_palette[offset + 1] * 4),
                static_cast<Uint8>(prompt_palette[offset + 2] * 4)};
            if (sampled[i] == palette_rgb)
            {
                palette_match = true;
                break;
            }
        }
        saw_off_palette = saw_off_palette || !palette_match;
    }
    for (size_t i = 0; i < sampled.size(); ++i)
        for (size_t j = i + 1; j < sampled.size(); ++j)
            if (sampled[i] == sampled[j])
                return false;
    return saw_off_palette;
}

bool prompt_block_hover_capture_and_match(PromptBlockHoverState& state,
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
            prompt_hover_matches(state.probe, state.baseline, state.buttons,
                                 hovered_button))
            return true;
        SDL_Delay(5);
    }
    return false;
}

int prompt_block_hover_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<PromptBlockHoverState*>(data);
    bool ok = wait_for_counter_advance(
        level_editor_testing_prompt_block_entered_count, 0);
    if (ok)
        inject_mouse_motion(state->outside.x, state->outside.y);
    const Uint64 ready_deadline = SDL_GetTicks() + 5000;
    while (ok && SDL_GetTicks() < ready_deadline)
    {
        if (!prompt_hover_capture(state->probe, true))
        {
            state->capture_failed = true;
            break;
        }
        if (prompt_hover_pointer_at(state->probe, 10.0f, 180.0f) &&
            prompt_block_hover_faces_ready(state->probe, state->buttons))
        {
            state->baseline_ready = true;
            state->faces_and_bevels_ready = true;
            state->arbitrary_backdrop_ready =
                prompt_block_hover_arbitrary_backdrop_ready(state->probe);
            state->baseline = state->probe.pixels;
            break;
        }
        SDL_Delay(5);
    }
    ok = ok && state->baseline_ready && state->faces_and_bevels_ready &&
        state->arbitrary_backdrop_ready;
    if (ok)
    {
        inject_mouse_motion(state->done_point.x, state->done_point.y);
        state->moved_done = ok && prompt_block_hover_capture_and_match(
            *state, 0, 293.0f, 6.0f);
        ok = state->moved_done;
        if (ok)
        {
            const char* const output_dir =
                std::getenv("OG_PROMPT_HOVER_CAPTURE_DIR");
            capture_presented_frame(
                state->click_done ? "editor_prompt_done_hover_done_test"
                                  : "editor_prompt_done_hover_cancel_test",
                output_dir);
        }
        inject_mouse_motion(state->cancel_point.x, state->cancel_point.y);
        state->moved_cancel = ok && prompt_block_hover_capture_and_match(
            *state, 1, 241.0f, 6.0f);
        ok = state->moved_cancel;
        if (ok)
        {
            const char* const output_dir =
                std::getenv("OG_PROMPT_HOVER_CAPTURE_DIR");
            capture_presented_frame(
                state->click_done ? "editor_prompt_cancel_hover_done_test"
                                  : "editor_prompt_cancel_hover_cancel_test",
                output_dir);
        }
        inject_mouse_motion(state->outside.x, state->outside.y);
        state->moved_outside = ok && prompt_block_hover_capture_and_match(
            *state, -1, 10.0f, 180.0f);
        ok = state->moved_outside;

        const bool click_done = state->click_done;
        const int final_button = click_done ? 0 : 1;
        const PromptWindowPoint final_point = click_done
            ? state->done_point : state->cancel_point;
        const float final_x = click_done ? 293.0f : 241.0f;
        inject_mouse_motion(final_point.x, final_point.y);
        state->moved_back = ok && prompt_block_hover_capture_and_match(
            *state, final_button, final_x, 6.0f);
        ok = state->moved_back;

        const std::vector<PromptHoverPixel> before_text = state->baseline;
        if (ok)
            ok = inject_prompt_text("Z");
        bool edit_capture_acknowledged = false;
        const Uint64 edit_deadline = SDL_GetTicks() + 5000;
        while (ok && SDL_GetTicks() < edit_deadline)
        {
            if (!prompt_hover_capture(state->probe, true))
            {
                state->capture_failed = true;
                ok = false;
                break;
            }
            edit_capture_acknowledged = true;
            if (prompt_hover_pointer_at(state->probe, final_x, 6.0f) &&
                prompt_hover_field_changed(state->probe, before_text))
                break;
            SDL_Delay(5);
        }
        state->field_changed = edit_capture_acknowledged &&
            !state->capture_failed &&
            prompt_hover_field_changed(state->probe, before_text);
        state->stationary_ring = state->field_changed &&
            !state->capture_failed &&
            prompt_hover_matches(state->probe, state->baseline,
                                 state->buttons, final_button);
        ok = ok && state->field_changed && state->stationary_ring;

        if (ok)
        {
            const std::uint64_t completed_before =
                level_editor_testing_prompt_block_input_completed_count();
            inject_mouse_down(final_point.x, final_point.y);
            ok = wait_for_counter_advance(
                level_editor_testing_prompt_block_input_completed_count,
                completed_before);
            inject_mouse_up(final_point.x, final_point.y);
            const Uint64 close_deadline = SDL_GetTicks() + 5000;
            while (ok && og::input_native::text_input_is_active() &&
                   SDL_GetTicks() < close_deadline)
                SDL_Delay(5);
            ok = ok && !og::input_native::text_input_is_active();
        }
        state->pointer_closed.store(ok, std::memory_order_release);
    }
    state->handshake_failed.store(!ok, std::memory_order_release);

    const auto hold_prompt = [state] {
        if (!state->handshake_failed.load(std::memory_order_acquire) ||
            state->main_returned.load(std::memory_order_acquire))
            return false;
        level_editor_testing_prompt_block_set_held_key(KEYSTATE_ESCAPE);
        SDL_Delay(10);
        level_editor_testing_prompt_block_set_held_key(-1);
        return true;
    };
    return escape_to_the_main_thread(
        state->main_returned, state->handshake_failed.load() ? 1 : 0,
        "hover sequence or pointer action did not complete",
        std::span<const EscapeDoor>{}, hold_prompt);
}

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
    if (ok && d->type_text != nullptr)
        ok = inject_prompt_text(d->type_text);
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

bool run_prompt_hover_flow(bool click_done, PromptBlockHoverState& state,
                           std::list<std::string>& edited)
{
    (void)take_captured_frames();
    state.click_done = click_done;
    state.buttons = {{268, 0, 50, 14}, {216, 0, 50, 14}};
    init_prompt_hover_probe(state.probe, state.buttons, {40, 60, 240, 80});
    state.outside = prompt_hover_window_point(10.0f, 180.0f);
    state.done_point = prompt_hover_window_point(293.0f, 6.0f);
    state.cancel_point = prompt_hover_window_point(241.0f, 6.0f);

    inject_mouse_motion(state.outside.x, state.outside.y);
    get_input_events(POLL);
    const MouseState& outside_mouse = query_mouse_no_poll();
    EXPECT_NEAR(10.0f, outside_mouse.x, 0.01f)
        << "the multiline prompt must enter with the pointer outside its buttons";
    EXPECT_NEAR(180.0f, outside_mouse.y, 0.01f);

    // Put several distinct non-palette RGB values under the two top-row
    // outlines. The overlapping x=267 column and clipped y=0 edge are part
    // of the sample and must survive both hover transitions exactly.
    screen* const target = og::runtime::current_session->myscreen_;
    target->pointb(267, 1, 37, 91, 143);
    target->pointb(267, 5, 181, 47, 109);
    target->pointb(267, 10, 73, 157, 29);
    target->pointb(267, 14, 211, 83, 53);
    target->pointb(215, 0, 19, 137, 223);
    target->pointb(319, 0, 149, 31, 197);

    SDL_Thread* const thread = SDL_CreateThread(
        prompt_block_hover_injector, "prompt_block_hover", &state);
    if (thread == nullptr)
        return false;
    const bool accepted = prompt_for_string_block("Hover prompt", edited);
    state.main_returned.store(true, std::memory_order_release);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    SDL_PumpEvents();
    escape_tail_join_hygiene();

    EXPECT_EQ(0, thread_result);
    EXPECT_FALSE(state.handshake_failed.load(std::memory_order_acquire));
    EXPECT_TRUE(state.pointer_closed.load(std::memory_order_acquire))
        << "the held pointer click must be consumed by the hovered action";
    EXPECT_TRUE(state.baseline_ready)
        << "capture waits for both action buttons after outside motion";
    EXPECT_TRUE(state.faces_and_bevels_ready);
    EXPECT_TRUE(state.arbitrary_backdrop_ready)
        << "the sampled overlap and clipped edges must retain varied RGB pixels";
    EXPECT_TRUE(state.moved_done)
        << "DONE motion must paint its exact yellow outline";
    EXPECT_TRUE(state.moved_cancel)
        << "CANCEL motion must restore the shared outline and paint its ring";
    EXPECT_TRUE(state.moved_outside)
        << "outside motion must restore every sampled outline pixel";
    EXPECT_TRUE(state.moved_back)
        << "the selected action must repaint after leaving both buttons";
    EXPECT_TRUE(state.field_changed)
        << "the prompt must render Z before it accepts the pointer click";
    EXPECT_TRUE(state.stationary_ring)
        << "text editing must preserve the hovered ring and action labels";
    verify_captured_frames(click_done ? "editor_prompt_hover_done"
                                      : "editor_prompt_hover_cancel", 2);
    return accepted;
}
} // namespace

// ESC is NOT a cancel: prompt_for_string_block's KEYSTATE_ESCAPE branch
// (level_editor_ui.cpp, `done = true; break;`) never restores original_text, so
// it commits every in-prompt edit exactly like DONE. Only the CANCEL button
// restores. The typed "Z" is what tells the two apart; the old name and the
// old "should preserve original text" message described a rule the product
// does not have.
TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_escape_commits_edits)
{
    (void)og::runtime::current_session->myscreen_;
    level_editor_testing_prompt_block_input_reset();

    std::list<std::string> edited{
        "Line one",
        "Line two",
    };

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, false, "Z"};
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
    EXPECT_TRUE(accepted)
        << "prompt_for_string_block returns !cancel, and ESC never sets cancel";
    EXPECT_EQ(2u, level_editor_testing_prompt_block_input_completed_count())
        << "the prompt consumed exactly the typed text and the ESC pulse";
    EXPECT_EQ((std::list<std::string>{"ZLine one", "Line two"}), edited)
        << "ESC ends the prompt WITHOUT restoring original_text, so the edit "
           "typed at line 0 column 0 survives";
}


// The mymouse.in(done_button) branch sets done = true and leaves `result`
// exactly as edited -- no restore from original_text. Typing first is what
// separates DONE from CANCEL: without an edit in flight both leave {"keep me"}.
TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_done_button_keeps_edits)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"keep me"};

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, false, true, "Z"};
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
    EXPECT_TRUE(accepted) << "DONE never sets cancel, so the prompt returns true";
    EXPECT_EQ(2u, level_editor_testing_prompt_block_input_completed_count())
        << "the prompt consumed exactly the typed text and the DONE click";
    EXPECT_EQ((std::list<std::string>{"Zkeep me"}), edited)
        << "DONE commits the in-prompt edit instead of restoring original_text";
}

TEST(LevelEditorPromptBlock, level_editor_prompt_for_string_block_cancel_button_restores_original)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> original{"first", "second"};
    std::list<std::string> edited = original;

    PromptBlockInjectState st{};
    PromptBlockInjectData inject_data{&st, true, false, "Z"};
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
    EXPECT_EQ(2u, level_editor_testing_prompt_block_input_completed_count())
        << "the prompt consumed exactly the typed text and the CANCEL click";
    // The injector typed "Z" at line 0 column 0 before clicking CANCEL, so
    // without `result = original_text` this list would read {"Zfirst",...}.
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
    EXPECT_TRUE(accepted) << "DONE never sets cancel, so the prompt returns true";
    // The key-press branch handles only KEYCODE_RETURN and KEYCODE_BACKSPACE;
    // arrows and DELETE are read from keystates_ (SDL_GetKeyboardState), which
    // a pushed SDL key event never moves (tests/test_input_helpers.h). So all
    // six pushed keys are acknowledged and inert, and the only edit is the
    // SDL_EVENT_TEXT_INPUT insert at cursor_pos 0 of line 0.
    EXPECT_EQ(8u, level_editor_testing_prompt_block_input_completed_count())
        << "six pushed keys + one text event + one DONE click were consumed";
    EXPECT_EQ((std::list<std::string>{"Zabc", "xyz"}), edited)
        << "text input inserts at the cursor and the inert keys change nothing";
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
    EXPECT_TRUE(accepted) << "DONE never sets cancel, so the prompt returns true";
    // From {"abc"}: RETURN at column 0 splits into {"", "abc"} and lands on
    // line 1; BACKSPACE at column 0 with more than one line merges back to
    // {"abc"}; the pushed RIGHT is inert (keystate-driven), so the following
    // BACKSPACE is still at column 0 of a one-line block and is a no-op; the
    // five RETURNs then each split at column 0, leaving five empty lines above
    // "abc"; the trailing UP/DOWN/RIGHT/DELETE are inert too.
    EXPECT_EQ(14u, level_editor_testing_prompt_block_input_completed_count())
        << "thirteen pushed keys + one DONE click were consumed";
    EXPECT_EQ((std::list<std::string>{"", "", "", "", "", "abc"}), edited)
        << "RETURN splits at the cursor and BACKSPACE merges/no-ops exactly";
}

TEST(LevelEditorPromptBlock, hovered_done_redraws_and_commits_edited_text)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"seed"};
    PromptBlockHoverState state;
    const bool accepted = run_prompt_hover_flow(true, state, edited);
    EXPECT_TRUE(accepted)
        << "DONE preserves the prompt's existing successful return value";
    EXPECT_EQ((std::list<std::string>{"Zseed"}), edited)
        << "the stationary-hover edit must be committed by DONE";
}

TEST(LevelEditorPromptBlock, hovered_cancel_redraws_and_restores_original_text)
{
    level_editor_testing_prompt_block_input_reset();
    std::list<std::string> edited{"seed"};
    PromptBlockHoverState state;
    const bool accepted = run_prompt_hover_flow(false, state, edited);
    EXPECT_TRUE(accepted)
        << "CANCEL preserves the prompt's existing successful return value";
    EXPECT_EQ((std::list<std::string>{"seed"}), edited)
        << "CANCEL restores original text after the stationary-hover edit";
}
