// Unit tests for the web Backspace-as-Escape translation matrix.
//
// The remap itself is only ENGAGED under __EMSCRIPTEN__ (web_mode=true at the
// SDL event source in src/platform/sdl/native_input.cpp), which native gtest
// cannot execute. The logic is a pure, build-flag-parameterized helper, so
// the full matrix — including the web half — is covered here natively.

#include <gtest/gtest.h>

#include <openglad/interface/input.h>
#include <openglad/interface/web_back_key.h>

using og::input::BackKeyEventTranslation;
using og::input::BackKeyStates;
using og::input::back_key_label;
using og::input::back_key_label_short;
using og::input::kBackKeycodeBackspace;
using og::input::kBackKeycodeEscape;
using og::input::kBackScancodeBackspace;
using og::input::kBackScancodeEscape;
using og::input::translate_back_key;
using og::input::translate_back_key_states;

namespace
{
constexpr int kKeycodeA = 97;  // SDLK_A
constexpr int kScancodeA = 4;  // SDL_SCANCODE_A
} // namespace

// The duplicated SDL constants must agree with the interface KEYCODE_/
// KEYSTATE_ constants used across menus and text loops. (The SDL platform
// layer additionally static_asserts them against the real SDL enums.)
TEST(WebBackKey, constants_match_interface_key_constants)
{
    ASSERT_EQ(KEYCODE_ESCAPE, kBackKeycodeEscape);
    ASSERT_EQ(KEYCODE_BACKSPACE, kBackKeycodeBackspace);
    ASSERT_EQ(KEYSTATE_ESCAPE, kBackScancodeEscape);
}

// ---------------------------------------------------------------------------
// Native mode (web_mode=false): everything is identity, nothing is swallowed.
// ---------------------------------------------------------------------------

TEST(WebBackKey, native_mode_passes_all_keys_through)
{
    for (const bool text_active : {false, true})
    {
        const BackKeyEventTranslation esc = translate_back_key(
            kBackKeycodeEscape, kBackScancodeEscape, false, text_active);
        ASSERT_FALSE(esc.swallow) << "native Escape must never be swallowed";
        ASSERT_EQ(kBackKeycodeEscape, esc.keycode);
        ASSERT_EQ(kBackScancodeEscape, esc.scancode);

        const BackKeyEventTranslation bksp = translate_back_key(
            kBackKeycodeBackspace, kBackScancodeBackspace, false, text_active);
        ASSERT_FALSE(bksp.swallow);
        ASSERT_EQ(kBackKeycodeBackspace, bksp.keycode)
            << "native Backspace must stay Backspace";
        ASSERT_EQ(kBackScancodeBackspace, bksp.scancode);

        const BackKeyEventTranslation other =
            translate_back_key(kKeycodeA, kScancodeA, false, text_active);
        ASSERT_FALSE(other.swallow);
        ASSERT_EQ(kKeycodeA, other.keycode);
        ASSERT_EQ(kScancodeA, other.scancode);
    }
}

TEST(WebBackKey, native_mode_key_states_are_identity)
{
    for (const bool text_active : {false, true})
    {
        for (const bool esc : {false, true})
        {
            for (const bool bksp : {false, true})
            {
                const BackKeyStates st =
                    translate_back_key_states(esc, bksp, false, text_active);
                ASSERT_EQ(esc, st.escape_down);
                ASSERT_EQ(bksp, st.backspace_down);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Web mode, no text input: Backspace acts as Escape; Escape is a no-op.
// ---------------------------------------------------------------------------

TEST(WebBackKey, web_mode_backspace_becomes_escape)
{
    const BackKeyEventTranslation t = translate_back_key(
        kBackKeycodeBackspace, kBackScancodeBackspace, true, false);
    ASSERT_FALSE(t.swallow);
    ASSERT_EQ(kBackKeycodeEscape, t.keycode);
    ASSERT_EQ(kBackScancodeEscape, t.scancode);
}

// Synthetic events may carry only one of keycode/scancode; either must match.
TEST(WebBackKey, web_mode_backspace_matches_by_keycode_or_scancode_alone)
{
    const BackKeyEventTranslation by_key =
        translate_back_key(kBackKeycodeBackspace, 0, true, false);
    ASSERT_FALSE(by_key.swallow);
    ASSERT_EQ(kBackKeycodeEscape, by_key.keycode);
    ASSERT_EQ(kBackScancodeEscape, by_key.scancode);

    const BackKeyEventTranslation by_scan =
        translate_back_key(0, kBackScancodeBackspace, true, false);
    ASSERT_FALSE(by_scan.swallow);
    ASSERT_EQ(kBackKeycodeEscape, by_scan.keycode);
    ASSERT_EQ(kBackScancodeEscape, by_scan.scancode);
}

TEST(WebBackKey, web_mode_swallows_physical_escape)
{
    // Regardless of text input state: a browser fullscreen-exit Escape must
    // not ALSO back out of a menu or cancel a prompt (the double-action bug).
    for (const bool text_active : {false, true})
    {
        ASSERT_TRUE(translate_back_key(kBackKeycodeEscape, kBackScancodeEscape,
                                       true, text_active)
                        .swallow);
        ASSERT_TRUE(translate_back_key(kBackKeycodeEscape, 0, true, text_active)
                        .swallow)
            << "keycode-only Escape must be swallowed";
        ASSERT_TRUE(translate_back_key(0, kBackScancodeEscape, true, text_active)
                        .swallow)
            << "scancode-only Escape must be swallowed";
    }
}

TEST(WebBackKey, web_mode_leaves_other_keys_alone)
{
    for (const bool text_active : {false, true})
    {
        const BackKeyEventTranslation t =
            translate_back_key(kKeycodeA, kScancodeA, true, text_active);
        ASSERT_FALSE(t.swallow);
        ASSERT_EQ(kKeycodeA, t.keycode);
        ASSERT_EQ(kScancodeA, t.scancode);
    }
}

TEST(WebBackKey, web_mode_key_states_route_backspace_to_escape_slot)
{
    // Physical Backspace held: KEYSTATE_ESCAPE consumers (BACK buttons,
    // menu release-wait loops) must see it...
    const BackKeyStates held =
        translate_back_key_states(false, true, true, false);
    ASSERT_TRUE(held.escape_down);
    // ...and the physical Backspace slot must be zeroed so a Backspace
    // gameplay binding (default: switch guys) cannot fire in the same press.
    ASSERT_FALSE(held.backspace_down);

    const BackKeyStates released =
        translate_back_key_states(false, false, true, false);
    ASSERT_FALSE(released.escape_down);
    ASSERT_FALSE(released.backspace_down);
}

TEST(WebBackKey, web_mode_key_states_ignore_physical_escape)
{
    for (const bool text_active : {false, true})
    {
        const BackKeyStates st =
            translate_back_key_states(true, false, true, text_active);
        ASSERT_FALSE(st.escape_down)
            << "physical Escape must never register on web";
        ASSERT_FALSE(st.backspace_down);
    }
}

// ---------------------------------------------------------------------------
// Web mode, text input active: Backspace keeps delete-character semantics.
// ---------------------------------------------------------------------------

TEST(WebBackKey, web_mode_text_input_keeps_backspace_deleting)
{
    const BackKeyEventTranslation t = translate_back_key(
        kBackKeycodeBackspace, kBackScancodeBackspace, true, true);
    ASSERT_FALSE(t.swallow);
    ASSERT_EQ(kBackKeycodeBackspace, t.keycode)
        << "Backspace in a text prompt must delete, not cancel";
    ASSERT_EQ(kBackScancodeBackspace, t.scancode);
}

TEST(WebBackKey, web_mode_text_input_key_states_keep_backspace_slot)
{
    const BackKeyStates st = translate_back_key_states(false, true, true, true);
    ASSERT_FALSE(st.escape_down)
        << "held Backspace must not read as Escape while typing";
    ASSERT_TRUE(st.backspace_down);
}

// ---------------------------------------------------------------------------
// User-visible key names.
// ---------------------------------------------------------------------------

TEST(WebBackKey, back_key_labels)
{
    ASSERT_STREQ("ESC", back_key_label(false));
    ASSERT_STREQ("BACKSPACE", back_key_label(true));
    ASSERT_STREQ("ESC", back_key_label_short(false));
    ASSERT_STREQ("BKSP", back_key_label_short(true));
}

TEST(WebBackKey, native_build_defaults_to_escape)
{
    // This test binary is a native build: the zero-arg conveniences and the
    // build-mode flag must leave native behavior untouched.
    ASSERT_FALSE(og::input::kWebBackKeyMode);
    ASSERT_STREQ("ESC", back_key_label());
    ASSERT_STREQ("ESC", back_key_label_short());
}
