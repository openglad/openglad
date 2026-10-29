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

#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

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
    // Profiles 1-3 keep every native default on web, except P2's LOOK UP,
    // which leaves Right Ctrl for Delete in both maps (#391).
    for (int profile = 1; profile < 4; ++profile)
        for (int mode : {kFour, kEight})
        {
            ASSERT_EQ(46, browser_safe_default_key(profile, mode, kFire, 46, true))
                << "profile " << profile << " FIRE must keep its native default";
            ASSERT_EQ(47, browser_safe_default_key(profile, mode, kSpecial, 47, true))
                << "profile " << profile << " SPECIAL must keep its native default";
            ASSERT_EQ(48, browser_safe_default_key(profile, mode, kYell, 48, true))
                << "profile " << profile << " YELL must keep its native default";
            const int lookup = profile == 1 ? og::input::kWebP2LookupKey : 49;
            ASSERT_EQ(lookup, browser_safe_default_key(profile, mode, kLookup, 49, true))
                << "profile " << profile << " look-up";
            for (int k = 0; k < 19; ++k)
            {
                if (profile == 1 && k == kLookup)
                    continue;
                ASSERT_EQ(500 + k, browser_safe_default_key(profile, mode, k, 500 + k, true))
                    << "profile " << profile << " slot " << k;
            }
        }
    // The new key and the stored value it replaces.
    ASSERT_EQ(127, og::input::kWebP2LookupKey);              // KEYCODE_DELETE
    ASSERT_EQ(1073742052, og::input::kV1WebP2LookupKey);     // KEYCODE_RCTRL
    // Natively P2 still looks up on Right Ctrl.
    ASSERT_EQ(og::input::kV1WebP2LookupKey,
              browser_safe_default_key(1, kFour, kLookup, og::input::kV1WebP2LookupKey, false));
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

// --- Cross-seat modifier chords (#391) -------------------------------------
//
// The keyboard is shared, so a chord is any modifier one player holds plus
// any key another player (or the same one) presses. This walks every seat's
// default keys in every combination of direction modes (each player picks
// 4- or 8-direction on their own: 2^4 combinations), on web and natively,
// through the same reads the game uses (factory_default_key +
// browser_safe_default_key), with the PAUSE key in the walked set (Escape
// natively, Backspace on web: web_back_key.h).
//
// On web, no chord may be one the page cannot stop: the browser acts on it
// before (or regardless of) the page's preventDefault. Playwright's synthetic
// keys never trigger browser accelerators, so this pure walk is the guard;
// the e2e suite can only prove the keys the game does use arrive.
//
// The OS-level Alt chords that remain (Left Alt stays P4's 8-direction
// SHIFTER, PR #256) are listed by name: a new one fails here.

namespace
{

struct Mods
{
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

bool is_ctrl(int key) { return key == KEYCODE_LCTRL || key == KEYCODE_RCTRL; }
bool is_shift(int key) { return key == KEYCODE_LSHIFT || key == KEYCODE_RSHIFT; }
bool is_alt(int key) { return key == KEYCODE_LALT || key == KEYCODE_RALT; }
bool is_modifier(int key) { return is_ctrl(key) || is_shift(key) || is_alt(key); }

std::string key_label(int key)
{
    struct Named { int code; const char* name; };
    static constexpr Named kNamed[] = {
        {KEYCODE_TAB, "Tab"},         {KEYCODE_SPACE, "Space"},
        {KEYCODE_BACKSPACE, "Backspace"}, {KEYCODE_ESCAPE, "Esc"},
        {KEYCODE_RETURN, "Return"},   {KEYCODE_DELETE, "Delete"},
        {KEYCODE_PAGEUP, "PgUp"},     {KEYCODE_PAGEDOWN, "PgDn"},
        {KEYCODE_UP, "Up"},           {KEYCODE_DOWN, "Down"},
        {KEYCODE_LEFT, "Left"},       {KEYCODE_RIGHT, "Right"},
        {KEYCODE_F1, "F1"}, {KEYCODE_F2, "F2"}, {KEYCODE_F3, "F3"}, {KEYCODE_F4, "F4"},
        {KEYCODE_F5, "F5"}, {KEYCODE_F6, "F6"}, {KEYCODE_F7, "F7"}, {KEYCODE_F8, "F8"},
        {KEYCODE_F9, "F9"}, {KEYCODE_F10, "F10"}, {KEYCODE_F11, "F11"}, {KEYCODE_F12, "F12"},
    };
    for (const Named& named : kNamed)
        if (named.code == key)
            return named.name;
    if (key > KEYCODE_SPACE && key < KEYCODE_DELETE)
    {
        const char c = static_cast<char>(key);
        return std::string(1, (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return "#" + std::to_string(key);
}

std::string chord_name(const Mods& mods, const std::string& key)
{
    std::string name;
    if (mods.ctrl)
        name += "Ctrl+";
    if (mods.shift)
        name += "Shift+";
    if (mods.alt)
        name += "Alt+";
    return name + key;
}

// Every chord the default keys can form with these direction modes: each
// non-empty set of held modifier kinds (from any seats) with each non-
// modifier key, plus Shift+Alt on its own (the Windows layout switch).
std::set<std::string> chords_for(bool web, unsigned eight_dir_mask)
{
    std::vector<int> keys;
    for (int profile = 0; profile < 4; ++profile)
    {
        const int mode = (eight_dir_mask & (1u << profile)) ? kEight : kFour;
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            const int key = browser_safe_default_key(
                profile, mode, k, og::input::factory_default_key(profile, mode, k), web);
            if (key != KEYCODE_UNKNOWN)
                keys.push_back(key);
        }
    }
    keys.push_back(web ? KEYCODE_BACKSPACE : KEYCODE_ESCAPE); // the PAUSE key

    Mods available;
    for (int key : keys)
    {
        available.ctrl = available.ctrl || is_ctrl(key);
        available.shift = available.shift || is_shift(key);
        available.alt = available.alt || is_alt(key);
    }

    std::set<std::string> chords;
    for (unsigned held = 1; held < 8; ++held)
    {
        const Mods mods{(held & 1u) != 0, (held & 2u) != 0, (held & 4u) != 0};
        if ((mods.ctrl && !available.ctrl) || (mods.shift && !available.shift) ||
            (mods.alt && !available.alt))
            continue;
        // Shift + a key is just typing; only chords with Ctrl or Alt matter.
        if (!mods.ctrl && !mods.alt)
            continue;
        for (int key : keys)
            if (!is_modifier(key))
                chords.insert(chord_name(mods, key_label(key)));
    }
    if (available.shift && available.alt)
        chords.insert("Shift+Alt");
    if (available.ctrl && available.shift)
        chords.insert("Ctrl+Shift");
    return chords;
}

// The chords a page cannot intercept (Chromium's reserved accelerators in
// windowed play, Firefox's reserved set) plus the back/close chords on the
// PAUSE key.
const std::set<std::string>& page_uninterceptable()
{
    static const std::set<std::string> kSet = [] {
        std::set<std::string> set;
        for (const char* key : {"W", "T", "N", "Q", "Tab", "PgUp", "PgDn", "F4", "Backspace"})
        {
            set.insert(std::string("Ctrl+") + key);
            set.insert(std::string("Ctrl+Alt+") + key);
        }
        for (const char* key : {"W", "T", "N", "Q", "P", "Tab", "PgUp", "PgDn", "Backspace"})
        {
            set.insert(std::string("Ctrl+Shift+") + key);
            set.insert(std::string("Ctrl+Shift+Alt+") + key);
        }
        set.insert("Alt+F4");
        set.insert("Shift+Alt+F4");
        return set;
    }();
    return kSet;
}

// Alt + one of these keys leaves the page (or the window) on some desktop:
// window switching, the window menu, GNOME's window actions, closing.
bool alt_os_key(const std::string& chord)
{
    static const std::vector<std::string> kKeys = {
        "Tab", "Space", "`", "Esc", "Backspace", "F1", "F2", "F3", "F4", "F5", "F6",
        "F7", "F8", "F9", "F10", "F11", "F12"};
    const std::size_t plus = chord.rfind('+');
    if (plus == std::string::npos || chord.find("Alt+") == std::string::npos)
        return false;
    return std::find(kKeys.begin(), kKeys.end(), chord.substr(plus + 1)) != kKeys.end();
}

// The Alt chords the default keys still form, all of them only with P4 in
// 8-direction (its SHIFTER is Left Alt). Documented in web/help.html and
// docs/ability-select-design.md C.2; not moved (question 12 of the design).
//   Alt+Tab        web P1 8-direction YELL / native P1 8-direction LOOK UP
//   Alt+Space      P3 FIRE
//   Alt+`          P1 SWITCH
//   Alt+F5..F8     the CHEAT keys
//   Alt+Backspace  the web PAUSE key (the page takes it: SDL prevents the
//                  default of every key it queues)
//   Alt+Esc        the native PAUSE key (Windows cycles windows; the
//                  native keyboard grab holds it while two keyboard seats
//                  play)
// Shift+ variants of each come from P1's or P2's SHIFTER/FIRE.
bool known_alt_chord(const std::string& chord)
{
    static const std::set<std::string> kKnown = [] {
        std::set<std::string> set;
        for (const char* key :
             {"Tab", "Space", "`", "F5", "F6", "F7", "F8", "Backspace", "Esc"})
        {
            set.insert(std::string("Alt+") + key);
            set.insert(std::string("Shift+Alt+") + key);
            // Natively Ctrl is bound too (P1 FIRE, P2 LOOK UP).
            set.insert(std::string("Ctrl+Alt+") + key);
            set.insert(std::string("Ctrl+Shift+Alt+") + key);
        }
        return set;
    }();
    return kKnown.contains(chord);
}

} // namespace

TEST(CrossSeatChords, no_page_uninterceptable_chord_in_the_web_defaults)
{
    for (unsigned mask = 0; mask < 16; ++mask)
    {
        for (const std::string& chord : chords_for(/*web=*/true, mask))
            EXPECT_FALSE(page_uninterceptable().contains(chord))
                << chord << " (P1-P4 8-direction mask " << mask << ")";
        // No web default binds Ctrl at all since P2's LOOK UP left Right Ctrl.
        EXPECT_FALSE(chords_for(true, mask).contains("Ctrl+W")) << "mask " << mask;
    }
}

TEST(CrossSeatChords, os_alt_chords_are_the_known_set)
{
    for (const bool web : {true, false})
    {
        for (unsigned mask = 0; mask < 16; ++mask)
        {
            for (const std::string& chord : chords_for(web, mask))
            {
                if (!alt_os_key(chord))
                    continue;
                EXPECT_TRUE(known_alt_chord(chord))
                    << chord << (web ? " (web" : " (native") << ", 8-direction mask "
                    << mask << ") is a new OS-level Alt chord";
            }
        }
    }
    // Every one of them needs P4 in 8-direction: with P4 in 4-direction no
    // default binds Alt.
    for (const bool web : {true, false})
        for (unsigned mask = 0; mask < 8; ++mask)
            for (const std::string& chord : chords_for(web, mask))
                EXPECT_EQ(std::string::npos, chord.find("Alt"))
                    << chord << (web ? " (web)" : " (native)") << " with P4 in 4-direction";
}

TEST(CrossSeatChords, the_walk_sees_the_native_ctrl_chords)
{
    // Paired control: natively P2 still looks up on Right Ctrl, and Ctrl+W
    // is an ordinary key there (no OS takes it). The walk must see it, or
    // the web test above proves nothing.
    EXPECT_TRUE(chords_for(/*web=*/false, 0).contains("Ctrl+W"));
    EXPECT_TRUE(chords_for(false, 0).contains("Ctrl+Shift+T"));
    EXPECT_TRUE(page_uninterceptable().contains("Ctrl+W"));
    // And it walks the PAUSE key: Ctrl+Esc natively.
    EXPECT_TRUE(chords_for(false, 0).contains("Ctrl+Esc"));
}

// --- Native keyboard grab rule (#391) --------------------------------------

TEST(KeyboardGrab, only_a_mission_with_two_keyboard_seats_grabs)
{
    using og::input_native::want_keyboard_grab;
    EXPECT_FALSE(want_keyboard_grab(false, 0));
    EXPECT_FALSE(want_keyboard_grab(false, 4));
    EXPECT_FALSE(want_keyboard_grab(true, 0)) << "pads only";
    EXPECT_FALSE(want_keyboard_grab(true, 1)) << "a solo keyboard keeps Alt+Tab";
    EXPECT_TRUE(want_keyboard_grab(true, 2));
    EXPECT_TRUE(want_keyboard_grab(true, 4));
}
