// Browser-safe default controls (issue #144): pure-header logic.
//
// Player 1's native factory keymap puts FIRE on Left Ctrl, so "attack while
// walking up" is literally Ctrl+W — a close-tab chord the browser owns in
// windowed play: Firefox delivers the keydown and closes the tab anyway, and
// Chromium never delivers it to the page at all. Ctrl is the whole problem:
// web builds substitute profile 0's FIRE default (and the keys it displaces)
// at the table read sites. Since New Ability Select SPECIAL (SPEC 1) is the
// '1' key and no longer moves; the one-time migration of saved keys lives in
// input_mappings (migrate_seat_keys) and reads the legacy values pinned here.
//
// This is a native (headless) binary, so both web and native branches are
// covered by passing web_mode explicitly; input_state.cpp static_asserts
// the duplicated keycode constants against the real KEYCODE_* values.

#include <openglad/interface/web_control_defaults.h>

#include <gtest/gtest.h>

using og::input::browser_safe_default_key;
using og::input::legacy_web_default_key;

namespace
{
constexpr int kFour = og::input::kWebModeFourIndex;
constexpr int kEight = og::input::kWebModeEightIndex;
constexpr int kFire = og::input::kWebKeySlotFire;
constexpr int kSpecial = og::input::kWebKeySlotSpecial;
constexpr int kYell = og::input::kWebKeySlotYell;
constexpr int kLookup = og::input::kWebKeySlotLookup;
constexpr int kLCtrl = og::input::kLegacyNativeFireKey;
constexpr int kLAlt = og::input::kLegacyNativeSpecialKey;
constexpr int kKey1 = 49;   // KEYCODE_1, P1's SPEC 1
constexpr int kTab = 9;     // KEYCODE_TAB, P1's native 8-dir look-up
constexpr int kS = 115;     // KEYCODE_s, P1's native 8-dir yell
} // namespace

TEST(WebControlDefaults, web_mode_moves_p1_fire_to_z_in_four_dir)
{
    ASSERT_EQ(og::input::kWebFourDirFireKey,
              browser_safe_default_key(0, kFour, kFire, kLCtrl, true));
}

TEST(WebControlDefaults, web_mode_eight_dir_moves_fire_to_s_and_shifts_yell)
{
    // Z/X are P1's own diagonals in the 8-direction map, so FIRE lands on S,
    // the center of the movement diamond. That displaces YELL (natively S)
    // onto TAB, which in turn leaves look-up (natively TAB) unbound.
    ASSERT_EQ(og::input::kWebEightDirFireKey,
              browser_safe_default_key(0, kEight, kFire, kLCtrl, true));
    ASSERT_EQ(og::input::kWebEightDirYellKey,
              browser_safe_default_key(0, kEight, kYell, kS, true));
    ASSERT_EQ(og::input::kWebEightDirLookupKey,
              browser_safe_default_key(0, kEight, kLookup, kTab, true));
}

TEST(WebControlDefaults, special_is_never_substituted)
{
    // SPEC 1 is the '1' key in both maps; the browser scheme's X is gone.
    ASSERT_EQ(kKey1, browser_safe_default_key(0, kFour, kSpecial, kKey1, true));
    ASSERT_EQ(kKey1, browser_safe_default_key(0, kEight, kSpecial, kKey1, true));
    ASSERT_EQ(4242, browser_safe_default_key(0, kFour, kSpecial, 4242, true));
}

TEST(WebControlDefaults, web_mode_four_dir_yell_and_lookup_pass_through)
{
    // Only the 8-direction map shuffles yell/look-up; the 4-direction map's
    // E / Q defaults are untouched.
    ASSERT_EQ(101, browser_safe_default_key(0, kFour, kYell, 101, true));
    ASSERT_EQ(113, browser_safe_default_key(0, kFour, kLookup, 113, true));
}

TEST(WebControlDefaults, web_replacements_are_the_documented_keycodes)
{
    // The SDL keycode values the web defaults land on: z, s, Tab, unbound.
    ASSERT_EQ(122, og::input::kWebFourDirFireKey);
    ASSERT_EQ(115, og::input::kWebEightDirFireKey);
    ASSERT_EQ(9, og::input::kWebEightDirYellKey);
    ASSERT_EQ(0, og::input::kWebEightDirLookupKey);
    // And the stored values the migration may still rewrite: LCtrl, LAlt
    // (native) and the shipped browser scheme's x and v.
    ASSERT_EQ(1073742048, kLCtrl);
    ASSERT_EQ(1073742050, kLAlt);
    ASSERT_EQ(120, og::input::kLegacyWebFourDirSpecialKey);
    ASSERT_EQ(118, og::input::kLegacyWebEightDirYellKey);
}

TEST(WebControlDefaults, native_mode_passes_everything_through)
{
    for (int mode : {kFour, kEight})
    {
        for (int k = 0; k < 19; ++k)
        {
            ASSERT_EQ(1000 + k, browser_safe_default_key(0, mode, k, 1000 + k, false))
                << "slot " << k << " mode " << mode;
        }
    }
}

TEST(WebControlDefaults, other_profiles_untouched_even_in_web_mode)
{
    for (int profile = 1; profile < 4; ++profile)
        for (int mode : {kFour, kEight})
        {
            ASSERT_EQ(46, browser_safe_default_key(profile, mode, kFire, 46, true))
                << "profile " << profile << " FIRE must keep its native default";
            ASSERT_EQ(47, browser_safe_default_key(profile, mode, kSpecial, 47, true))
                << "profile " << profile << " SPECIAL must keep its native default";
            ASSERT_EQ(48, browser_safe_default_key(profile, mode, kYell, 48, true))
                << "profile " << profile << " YELL must keep its native default";
            ASSERT_EQ(49, browser_safe_default_key(profile, mode, kLookup, 49, true))
                << "profile " << profile << " look-up must keep its native default";
        }
}

TEST(WebControlDefaults, unsubstituted_slots_pass_through_in_web_mode)
{
    // Exactly three (slot, mode) pairs are substituted (FIRE in both maps,
    // YELL and LOOK-UP in the 8-direction map); every other slot in either
    // map, SPEC 1/2/3 included, hands the native default straight back.
    constexpr int kNumKeys = 19; // NUM_KEYS
    for (int k = 0; k < kNumKeys; ++k)
    {
        for (int mode : {kFour, kEight})
        {
            const bool substituted =
                (k == kFire) || (mode == kEight && (k == kYell || k == kLookup));
            if (substituted)
                continue;
            const int native_default = 1000 + k;
            ASSERT_EQ(native_default,
                      browser_safe_default_key(0, mode, k, native_default, true))
                << "key slot " << k << " (mode " << mode
                << ") must never be substituted";
        }
    }
}

TEST(WebControlDefaults, legacy_web_scheme_is_the_shipped_browser_values)
{
    // What the browser scheme put in profile 0's slots from August 2026 until
    // New Ability Select: 4-dir FIRE z, SPECIAL x; 8-dir FIRE s, YELL v,
    // look-up unbound. Every other slot reads its native legacy value.
    ASSERT_EQ(122, legacy_web_default_key(kFour, kFire, kLCtrl));
    ASSERT_EQ(120, legacy_web_default_key(kFour, kSpecial, kLAlt));
    ASSERT_EQ(115, legacy_web_default_key(kEight, kFire, kLCtrl));
    ASSERT_EQ(kLAlt, legacy_web_default_key(kEight, kSpecial, kLAlt));
    ASSERT_EQ(118, legacy_web_default_key(kEight, kYell, kS));
    ASSERT_EQ(0, legacy_web_default_key(kEight, kLookup, 118));
    ASSERT_EQ(101, legacy_web_default_key(kFour, kYell, 101));
    ASSERT_EQ(118, legacy_web_default_key(kFour, kLookup, 118));
    for (int k = 0; k < 19; ++k)
    {
        if (k == kFire || k == kSpecial || k == kYell || k == kLookup)
            continue;
        ASSERT_EQ(500 + k, legacy_web_default_key(kFour, k, 500 + k));
        ASSERT_EQ(500 + k, legacy_web_default_key(kEight, k, 500 + k));
    }
}

TEST(WebControlDefaults, native_build_flag_off)
{
    // This test binary is a native build: the build-mode flag must leave
    // native behavior untouched (mirrors test_web_back_key.cpp).
    ASSERT_FALSE(og::input::kWebControlDefaults);
}
