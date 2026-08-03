// Browser-safe default controls (issue #144): pure-header logic.
//
// Player 1's native factory keymap puts FIRE on Left Ctrl, so "attack while
// walking up" is literally Ctrl+W — the close-tab chord Firefox reserves at
// browser-chrome level where no page script can intercept it. Ctrl is the
// only problem key: web builds substitute profile 0's FIRE default (and the
// keys it displaces) at the table read sites, leave SPECIAL on Left Alt in
// the 8-direction map, and run a one-shot version-keyed migration over
// persisted bindings still equal to the old factory default.
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
constexpr int kYell = og::input::kWebKeySlotYell;
constexpr int kLookup = og::input::kWebKeySlotLookup;
constexpr int kLCtrl = og::input::kWebLegacyFireKey;
constexpr int kLAlt = og::input::kWebLegacySpecialKey;
} // namespace

TEST(WebControlDefaults, web_mode_moves_p1_four_dir_fire_and_special)
{
    // 4-direction map: FIRE=Z, SPECIAL=X, both free next to WASD.
    ASSERT_EQ(og::input::kWebFourDirFireKey,
              browser_safe_default_key(0, kFour, kFire, kLCtrl, true));
    ASSERT_EQ(og::input::kWebFourDirSpecialKey,
              browser_safe_default_key(0, kFour, kSpecial, kLAlt, true));
}

TEST(WebControlDefaults, web_mode_eight_dir_moves_fire_to_s_and_shifts_yell)
{
    // Z/X are P1's own diagonals in the 8-direction map, so FIRE lands on S,
    // the center of the movement diamond. That displaces YELL (natively S)
    // onto V, which in turn leaves look-up (natively V) unbound.
    ASSERT_EQ(og::input::kWebEightDirFireKey,
              browser_safe_default_key(0, kEight, kFire, kLCtrl, true));
    ASSERT_EQ(og::input::kWebEightDirYellKey,
              browser_safe_default_key(0, kEight, kYell,
                                       og::input::kWebLegacyEightDirYellKey,
                                       true));
    ASSERT_EQ(og::input::kWebEightDirLookupKey,
              browser_safe_default_key(0, kEight, kLookup,
                                       og::input::kWebLegacyEightDirLookupKey,
                                       true));
}

TEST(WebControlDefaults, web_mode_eight_dir_special_stays_left_alt)
{
    // Left Alt is not browser-reserved (SDL's Emscripten layer
    // preventDefaults it), so the 8-direction SPECIAL passes straight
    // through — no substitution at all.
    ASSERT_EQ(kLAlt, browser_safe_default_key(0, kEight, kSpecial, kLAlt, true));
    // And it passes whatever the native table holds, not a pinned constant.
    ASSERT_EQ(4242, browser_safe_default_key(0, kEight, kSpecial, 4242, true));
}

TEST(WebControlDefaults, web_mode_four_dir_yell_and_lookup_pass_through)
{
    // Only the 8-direction map shuffles yell/look-up; the 4-direction map's
    // E / V defaults are untouched.
    ASSERT_EQ(101, browser_safe_default_key(0, kFour, kYell, 101, true));
    ASSERT_EQ(118, browser_safe_default_key(0, kFour, kLookup, 118, true));
}

TEST(WebControlDefaults, web_replacements_are_the_documented_keycodes)
{
    // The SDL keycode values the web defaults land on: z, x, s, v, unbound.
    ASSERT_EQ(122, og::input::kWebFourDirFireKey);
    ASSERT_EQ(120, og::input::kWebFourDirSpecialKey);
    ASSERT_EQ(115, og::input::kWebEightDirFireKey);
    ASSERT_EQ(118, og::input::kWebEightDirYellKey);
    ASSERT_EQ(0, og::input::kWebEightDirLookupKey);
    // And the native defaults they replace: LCtrl, LAlt, s, v.
    ASSERT_EQ(1073742048, kLCtrl);
    ASSERT_EQ(1073742050, kLAlt);
    ASSERT_EQ(115, og::input::kWebLegacyEightDirYellKey);
    ASSERT_EQ(118, og::input::kWebLegacyEightDirLookupKey);
}

TEST(WebControlDefaults, native_mode_passes_everything_through)
{
    for (int mode : {kFour, kEight})
    {
        ASSERT_EQ(kLCtrl,
                  browser_safe_default_key(0, mode, kFire, kLCtrl, false));
        ASSERT_EQ(kLAlt,
                  browser_safe_default_key(0, mode, kSpecial, kLAlt, false));
        ASSERT_EQ(og::input::kWebLegacyEightDirYellKey,
                  browser_safe_default_key(
                      0, mode, kYell, og::input::kWebLegacyEightDirYellKey,
                      false));
        ASSERT_EQ(og::input::kWebLegacyEightDirLookupKey,
                  browser_safe_default_key(
                      0, mode, kLookup,
                      og::input::kWebLegacyEightDirLookupKey, false));
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
    // Exactly four (slot, mode) pairs are substituted; every other slot in
    // either map hands the native default straight back.
    constexpr int kNumKeys = 17; // NUM_KEYS
    for (int k = 0; k < kNumKeys; ++k)
    {
        for (int mode : {kFour, kEight})
        {
            const bool substituted =
                (k == kFire) || (k == kSpecial && mode == kFour) ||
                (mode == kEight && (k == kYell || k == kLookup));
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

TEST(WebControlDefaults, migration_covers_the_eight_dir_knock_on_slots)
{
    // Yell: the stale S migrates to V, a rebind does not.
    ASSERT_EQ(og::input::kWebEightDirYellKey,
              migrate_persisted_control_key(
                  og::input::kWebLegacyEightDirYellKey,
                  og::input::kWebLegacyEightDirYellKey,
                  og::input::kWebEightDirYellKey, true));
    ASSERT_EQ(999, migrate_persisted_control_key(
                       999, og::input::kWebLegacyEightDirYellKey,
                       og::input::kWebEightDirYellKey, true));

    // Look-up: the stale V migrates to unbound, a rebind does not. Migrating
    // TO an unbound value must still work — 0 is a legitimate target.
    ASSERT_EQ(og::input::kWebEightDirLookupKey,
              migrate_persisted_control_key(
                  og::input::kWebLegacyEightDirLookupKey,
                  og::input::kWebLegacyEightDirLookupKey,
                  og::input::kWebEightDirLookupKey, true));
    ASSERT_EQ(999, migrate_persisted_control_key(
                       999, og::input::kWebLegacyEightDirLookupKey,
                       og::input::kWebEightDirLookupKey, true));
}

TEST(WebControlDefaults, native_build_flag_off)
{
    // This test binary is a native build: the build-mode flag must leave
    // native behavior untouched (mirrors test_web_back_key.cpp).
    ASSERT_FALSE(og::input::kWebControlDefaults);
    ASSERT_EQ(1, og::input::kWebControlDefaultsVersion);
}
