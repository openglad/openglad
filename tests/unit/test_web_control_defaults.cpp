// Browser-safe default controls (issue #144): pure-header logic.
//
// Player 1's native factory keymap puts FIRE on Left Ctrl, so "attack while
// walking up" is literally Ctrl+W — the close-tab chord Firefox reserves at
// browser-chrome level where no page script can intercept it. Web builds
// substitute browser-safe FIRE/SPECIAL defaults for profile 0 at the table
// read sites and run a one-shot version-keyed migration over persisted
// bindings still equal to the old factory default.
//
// This is a native (headless) binary, so both web and native branches are
// covered by passing web_mode explicitly; input_state.cpp static_asserts
// the duplicated keycode constants against the real KEYCODE_* values.

#include <openglad/interface/web_control_defaults.h>

#include <gtest/gtest.h>

using og::input::browser_safe_default_key;
using og::input::migrate_persisted_control_key;

namespace
{
constexpr int kFour = og::input::kWebModeFourIndex;
constexpr int kEight = og::input::kWebModeEightIndex;
constexpr int kFire = og::input::kWebKeySlotFire;
constexpr int kSpecial = og::input::kWebKeySlotSpecial;
constexpr int kLCtrl = og::input::kWebLegacyFireKey;
constexpr int kLAlt = og::input::kWebLegacySpecialKey;
} // namespace

TEST(WebControlDefaults, web_mode_moves_p1_fire_and_special)
{
    // 4-direction map: FIRE=Z, SPECIAL=X.
    ASSERT_EQ(og::input::kWebFourDirFireKey,
              browser_safe_default_key(0, kFour, kFire, kLCtrl, true));
    ASSERT_EQ(og::input::kWebFourDirSpecialKey,
              browser_safe_default_key(0, kFour, kSpecial, kLAlt, true));
    // 8-direction map: z/x are P1's diagonals there — FIRE=Space, SPECIAL=[.
    ASSERT_EQ(og::input::kWebEightDirFireKey,
              browser_safe_default_key(0, kEight, kFire, kLCtrl, true));
    ASSERT_EQ(og::input::kWebEightDirSpecialKey,
              browser_safe_default_key(0, kEight, kSpecial, kLAlt, true));
}

TEST(WebControlDefaults, web_replacements_are_the_documented_keycodes)
{
    // The SDL keycode values the web defaults land on: z, x, Space, '['.
    ASSERT_EQ(122, og::input::kWebFourDirFireKey);
    ASSERT_EQ(120, og::input::kWebFourDirSpecialKey);
    ASSERT_EQ(32, og::input::kWebEightDirFireKey);
    ASSERT_EQ(91, og::input::kWebEightDirSpecialKey);
    // And the old native defaults they replace: LCtrl, LAlt.
    ASSERT_EQ(1073742048, kLCtrl);
    ASSERT_EQ(1073742050, kLAlt);
}

TEST(WebControlDefaults, native_mode_passes_everything_through)
{
    for (int mode : {kFour, kEight})
    {
        ASSERT_EQ(kLCtrl,
                  browser_safe_default_key(0, mode, kFire, kLCtrl, false));
        ASSERT_EQ(kLAlt,
                  browser_safe_default_key(0, mode, kSpecial, kLAlt, false));
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
        }
}

TEST(WebControlDefaults, non_fire_special_slots_untouched_in_web_mode)
{
    constexpr int kNumKeys = 17; // NUM_KEYS
    for (int k = 0; k < kNumKeys; ++k)
    {
        if (k == kFire || k == kSpecial)
            continue;
        for (int mode : {kFour, kEight})
        {
            const int native_default = 1000 + k;
            ASSERT_EQ(native_default,
                      browser_safe_default_key(0, mode, k, native_default, true))
                << "key slot " << k << " must never be substituted";
        }
    }
}

TEST(WebControlDefaults, migration_moves_only_exact_old_default)
{
    const int new_key = og::input::kWebFourDirFireKey;
    ASSERT_EQ(new_key, migrate_persisted_control_key(kLCtrl, kLCtrl, new_key, true))
        << "a stored value still equal to the old factory default migrates";
    ASSERT_EQ(999, migrate_persisted_control_key(999, kLCtrl, new_key, true))
        << "a user's own rebind is never touched";
    ASSERT_EQ(kLCtrl, migrate_persisted_control_key(kLCtrl, kLCtrl, new_key, false))
        << "with the version marker current, even the old default stays";
    ASSERT_EQ(999, migrate_persisted_control_key(999, kLCtrl, new_key, false));
}

TEST(WebControlDefaults, native_build_flag_off)
{
    // This test binary is a native build: the build-mode flag must leave
    // native behavior untouched (mirrors test_web_back_key.cpp).
    ASSERT_FALSE(og::input::kWebControlDefaults);
    ASSERT_EQ(1, og::input::kWebControlDefaultsVersion);
}
