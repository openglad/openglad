// Named input mappings (docs/pause-menu-design.md §3): derivation, the
// cfg-persisted library, and the seat operations behind the INPUT cycler.
//
// Headless on purpose — the module is SDL-free so openglad_text/_server/
// _curses share the names the SDL client shows, and every function here is
// exercised without a window. The library tests drive a LOCAL cfg_store and
// never call save_settings(), so nothing reaches cfg/openglad.yaml.

#include <openglad/interface/input_mappings.h>

#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test_key_tables.h"

using og::input::assign_mapping_to_player;
using og::input::available_mapping_names;
using og::input::current_mapping_name;
using og::input::derive_mapping_name;
using og::input::erase_mapping;
using og::input::factory_default_key;
using og::input::factory_default_mode;
using og::input::factory_mapping;
using og::input::factory_mapping_name;
using og::input::factory_profile_for_name;
using og::input::joystick_mapping_name;
using og::input::mapping_display_name;
using og::input::KeyMap;
using og::input::load_mapping_library;
using og::input::MappingDefinition;
using og::input::mapping_short_name;
using og::input::resolve_mapping;
using og::input::save_mapping;
using og::input::save_player_mapping_to_library;

namespace
{

constexpr int kFour = og::input::kWebModeFourIndex;
constexpr int kEight = og::input::kWebModeEightIndex;
constexpr int kFourMode = static_cast<int>(ControlDirectionMode::FourDirection);
constexpr int kEightMode = static_cast<int>(ControlDirectionMode::EightDirection);

// Player controls live in the shared unit-test session, so every test that
// touches them starts from a known factory state and puts the session back
// exactly as it found it (--gtest_shuffle safety).
struct ControlStateGuard
{
    InputHardwareState hardware = input_hardware_state();
    int player_keys[4][NUM_KEYS]{};

    ControlStateGuard()
    {
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                player_keys[player][key] =
                    og::runtime::current_session->player_keys_[player][key];
        reset_default_player_controls();
    }

    ~ControlStateGuard()
    {
        input_hardware_state() = hardware;
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                og::runtime::current_session->player_keys_[player][key] =
                    player_keys[player][key];
    }
};

KeyMap factory_keys(int profile, int mode_index)
{
    KeyMap keys{};
    for (int k = 0; k < NUM_KEYS; ++k)
        keys[static_cast<std::size_t>(k)] = factory_default_key(profile, mode_index, k);
    return keys;
}

KeyMap cluster(int up, int left, int down, int right)
{
    KeyMap keys{};
    keys.fill(KEYCODE_UNKNOWN);
    keys[static_cast<std::size_t>(KEY_UP)] = up;
    keys[static_cast<std::size_t>(KEY_LEFT)] = left;
    keys[static_cast<std::size_t>(KEY_DOWN)] = down;
    keys[static_cast<std::size_t>(KEY_RIGHT)] = right;
    return keys;
}

MappingDefinition custom_mapping(const std::string& name, const KeyMap& keys)
{
    MappingDefinition mapping;
    mapping.name = name;
    mapping.mode = kFourMode;
    mapping.mode4_keys = keys;
    mapping.mode8_keys = keys;
    return mapping;
}

bool has_name(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::size_t count_name(const std::vector<std::string>& names, const std::string& name)
{
    return static_cast<std::size_t>(std::count(names.begin(), names.end(), name));
}

} // namespace

// --- Factory accessors ----------------------------------------------------

TEST(InputMappings, factory_tables_read_back_through_the_accessors)
{
    // The tables stay file-static in input_state.cpp next to their
    // static_asserts; these readers are the only way in.
    ASSERT_EQ(KEYCODE_w, factory_default_key(0, kFour, KEY_UP));
    ASSERT_EQ(KEYCODE_a, factory_default_key(0, kFour, KEY_LEFT));
    ASSERT_EQ(KEYCODE_s, factory_default_key(0, kFour, KEY_DOWN));
    ASSERT_EQ(KEYCODE_d, factory_default_key(0, kFour, KEY_RIGHT));
    // Profile 0's 8-direction DOWN is 'x' — 's' is its YELL.
    ASSERT_EQ(KEYCODE_x, factory_default_key(0, kEight, KEY_DOWN));
    ASSERT_EQ(KEYCODE_UP, factory_default_key(1, kFour, KEY_UP));
    ASSERT_EQ(KEYCODE_i, factory_default_key(2, kFour, KEY_UP));
    ASSERT_EQ(KEYCODE_t, factory_default_key(3, kFour, KEY_UP));

    for (int profile = 0; profile < 4; ++profile)
        ASSERT_EQ(kFourMode, factory_default_mode(profile)) << "profile " << profile;
}

TEST(InputMappings, factory_accessors_reject_out_of_range_indices)
{
    ASSERT_EQ(KEYCODE_UNKNOWN, factory_default_key(-1, kFour, KEY_UP));
    ASSERT_EQ(KEYCODE_UNKNOWN, factory_default_key(4, kFour, KEY_UP));
    ASSERT_EQ(KEYCODE_UNKNOWN, factory_default_key(0, kFour, -1));
    ASSERT_EQ(KEYCODE_UNKNOWN, factory_default_key(0, kFour, NUM_KEYS));
    ASSERT_EQ(kFourMode, factory_default_mode(-1));
    ASSERT_EQ(kFourMode, factory_default_mode(4));
}

TEST(InputMappings, factory_names_round_trip_to_profile_indices)
{
    ASSERT_EQ("WASD", factory_mapping_name(0));
    ASSERT_EQ("ARROWS", factory_mapping_name(1));
    ASSERT_EQ("IJKL", factory_mapping_name(2));
    ASSERT_EQ("TFGH", factory_mapping_name(3));
    ASSERT_EQ("", factory_mapping_name(-1));
    ASSERT_EQ("", factory_mapping_name(4));

    for (int profile = 0; profile < 4; ++profile)
        ASSERT_EQ(profile, factory_profile_for_name(factory_mapping_name(profile)));
    ASSERT_EQ(-1, factory_profile_for_name("WQSE"));
    ASSERT_EQ(-1, factory_profile_for_name("JOY1"));
    ASSERT_EQ(-1, factory_profile_for_name(og::input::kCustomMappingName));
}

// --- Name derivation ------------------------------------------------------

TEST(InputMappings, derives_every_factory_name_in_both_direction_modes)
{
    // A mapping is named after the factory CLUSTER it matches, in either
    // mode: the 8-direction tables move DOWN off the 4-direction key (P1's
    // 'x', P3's ',', P4's 'b') and are still the same named mapping.
    for (int profile = 0; profile < 4; ++profile)
    {
        const std::string expected = factory_mapping_name(profile);
        ASSERT_EQ(expected, derive_mapping_name(factory_keys(profile, kFour)))
            << "profile " << profile << " 4-direction";
        ASSERT_EQ(expected, derive_mapping_name(factory_keys(profile, kEight)))
            << "profile " << profile << " 8-direction";
    }
}

TEST(InputMappings, remapping_a_non_movement_key_keeps_the_name)
{
    KeyMap keys = factory_keys(0, kFour);
    keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_z;
    keys[static_cast<std::size_t>(KEY_YELL)] = KEYCODE_UNKNOWN;
    ASSERT_EQ("WASD", derive_mapping_name(keys));
}

TEST(InputMappings, spells_out_a_movement_cluster_it_does_not_recognize)
{
    // UP, LEFT, DOWN, RIGHT — in that order.
    ASSERT_EQ("WQSE", derive_mapping_name(cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e)));
    ASSERT_EQ("8456", derive_mapping_name(cluster(KEYCODE_8, KEYCODE_4, KEYCODE_5, KEYCODE_6)));
    // Punctuation keycodes are their own names too.
    ASSERT_EQ("W;/'", derive_mapping_name(cluster(KEYCODE_w, KEYCODE_SEMICOLON,
                                                  KEYCODE_SLASH, KEYCODE_QUOTE)));
}

TEST(InputMappings, unspellable_clusters_fall_back_to_custom)
{
    const std::string custom = og::input::kCustomMappingName;
    // One arrow among letters: arrows have no one-character name.
    ASSERT_EQ(custom, derive_mapping_name(cluster(KEYCODE_UP, KEYCODE_a, KEYCODE_s, KEYCODE_d)));
    // Unbound movement.
    ASSERT_EQ(custom,
              derive_mapping_name(cluster(KEYCODE_w, KEYCODE_a, KEYCODE_UNKNOWN, KEYCODE_d)));
    // Space and modifiers need words.
    ASSERT_EQ(custom, derive_mapping_name(cluster(KEYCODE_w, KEYCODE_a, KEYCODE_SPACE, KEYCODE_d)));
    ASSERT_EQ(custom,
              derive_mapping_name(cluster(KEYCODE_w, KEYCODE_a, KEYCODE_s, KEYCODE_LSHIFT)));
    ASSERT_EQ(custom, derive_mapping_name(static_cast<const int*>(nullptr)));
}

TEST(InputMappings, short_names_fit_the_base_camp_seat_card)
{
    // ARROWS renders as the font's arrow glyphs (up/left/down/right) on
    // every SDL display surface; "P2 \x01\x03\x02\x04  " is nine of the eleven
    // characters the 70px seat-slot face holds (three of them the P# prefix,
    // two the trailing pads that center the ink over the chip-free zone).
    ASSERT_EQ(og::input::kArrowGlyphs, mapping_short_name("ARROWS"));
    ASSERT_EQ("\x01\x03\x02\x04", mapping_short_name("ARROWS"));
    ASSERT_EQ("WASD", mapping_short_name("WASD"));
    ASSERT_EQ("IJKL", mapping_short_name("IJKL"));
    ASSERT_EQ("TFGH", mapping_short_name("TFGH"));
    ASSERT_EQ("JOY1", mapping_short_name("JOY1"));
    ASSERT_EQ("WQSE", mapping_short_name("WQSE"));
    ASSERT_EQ("CUST", mapping_short_name(og::input::kCustomMappingName));

    for (const std::string& name : {std::string("WASD"), std::string("ARROWS"),
                                    std::string("IJKL"), std::string("TFGH"),
                                    std::string("JOY10"), std::string("WQSE"),
                                    std::string(og::input::kCustomMappingName)})
    {
        ASSERT_LE(mapping_short_name(name).size(),
                  static_cast<std::size_t>(og::input::kMappingShortNameMaxLength))
            << name;
    }
}

TEST(InputMappings, display_name_substitutes_arrow_glyphs)
{
    // The canonical name stays ASCII "ARROWS" (cfg/library identity); only
    // the display form becomes glyphs. Nothing else is rewritten.
    ASSERT_EQ(og::input::kArrowGlyphs, mapping_display_name("ARROWS"));
    ASSERT_EQ("WASD", mapping_display_name("WASD"));
    ASSERT_EQ("JOY1", mapping_display_name("JOY1"));
    ASSERT_EQ("WQSE", mapping_display_name("WQSE"));
    ASSERT_EQ(og::input::kCustomMappingName,
              mapping_display_name(og::input::kCustomMappingName));
}

TEST(InputMappings, joystick_names_are_one_based_device_slots)
{
    ASSERT_EQ("JOY1", joystick_mapping_name(0));
    ASSERT_EQ("JOY2", joystick_mapping_name(1));
    ASSERT_EQ("JOY16", joystick_mapping_name(15));
    ASSERT_EQ("JOY", joystick_mapping_name(-1));
}

// --- Factory mappings -----------------------------------------------------

TEST(InputMappings, factory_mapping_applies_the_web_substitution_to_wasd_only)
{
    const MappingDefinition native = factory_mapping(0, /*web_mode=*/false);
    ASSERT_EQ("WASD", native.name);
    ASSERT_EQ(kFourMode, native.mode);
    ASSERT_EQ(KEYCODE_LCTRL, native.mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);

    const MappingDefinition web = factory_mapping(0, /*web_mode=*/true);
    ASSERT_EQ(KEYCODE_z, web.mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);
    ASSERT_EQ(KEYCODE_1, web.mode4_keys[static_cast<std::size_t>(KEY_SPECIAL)]);
    ASSERT_EQ(KEYCODE_s, web.mode8_keys[static_cast<std::size_t>(KEY_FIRE)]);
    ASSERT_EQ(KEYCODE_TAB, web.mode8_keys[static_cast<std::size_t>(KEY_YELL)]);
    ASSERT_EQ(KEYCODE_UNKNOWN, web.mode8_keys[static_cast<std::size_t>(KEY_LOOKUP)]);
    // Movement is untouched, so the name survives the substitution.
    ASSERT_EQ("WASD", derive_mapping_name(web.mode4_keys));

    // ARROWS moves only its LOOK UP, Right Ctrl -> Delete (#391); profiles
    // 2-3 never move.
    MappingDefinition arrows_web = factory_mapping(1, true);
    ASSERT_EQ(KEYCODE_DELETE, arrows_web.mode4_keys[static_cast<std::size_t>(KEY_LOOKUP)]);
    ASSERT_EQ(KEYCODE_DELETE, arrows_web.mode8_keys[static_cast<std::size_t>(KEY_LOOKUP)]);
    arrows_web.mode4_keys[static_cast<std::size_t>(KEY_LOOKUP)] = KEYCODE_RCTRL;
    arrows_web.mode8_keys[static_cast<std::size_t>(KEY_LOOKUP)] = KEYCODE_RCTRL;
    ASSERT_EQ(factory_mapping(1, false), arrows_web);
    for (int profile = 2; profile < 4; ++profile)
    {
        ASSERT_EQ(factory_mapping(profile, false), factory_mapping(profile, true))
            << "profile " << profile;
    }
    // Out-of-range profiles clamp to WASD rather than yielding a blank seat.
    ASSERT_EQ(factory_mapping(0, false), factory_mapping(-1, false));
    ASSERT_EQ(factory_mapping(0, false), factory_mapping(4, false));
}

// --- Library over a local cfg_store ---------------------------------------

TEST(InputMappings, library_round_trips_through_a_cfg_store)
{
    cfg_store config;
    ASSERT_TRUE(load_mapping_library(config).empty());

    MappingDefinition mapping =
        custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e));
    mapping.mode = kEightMode;
    mapping.mode4_keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_SPACE;
    mapping.mode8_keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_LCTRL;
    ASSERT_TRUE(save_mapping(config, mapping));

    ASSERT_EQ("WQSE", config.get_setting("mappings", "mapping0_name"));
    ASSERT_EQ("8", config.get_setting("mappings", "mapping0_mode"));
    ASSERT_EQ(std::to_string(KEYCODE_SPACE), config.get_setting("mappings", "mapping0_mode4_key8"));

    const std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(1u, library.size());
    ASSERT_EQ(mapping, library[0]);
}

TEST(InputMappings, save_mapping_reuses_the_slot_of_an_existing_name)
{
    cfg_store config;
    ASSERT_TRUE(save_mapping(
        config, custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e))));
    ASSERT_TRUE(save_mapping(
        config, custom_mapping("ZXCV", cluster(KEYCODE_z, KEYCODE_x, KEYCODE_c, KEYCODE_v))));

    MappingDefinition edited =
        custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e));
    edited.mode4_keys[static_cast<std::size_t>(KEY_YELL)] = KEYCODE_9;
    ASSERT_TRUE(save_mapping(config, edited));

    const std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(2u, library.size());
    ASSERT_EQ("WQSE", library[0].name);
    ASSERT_EQ(KEYCODE_9, library[0].mode4_keys[static_cast<std::size_t>(KEY_YELL)]);
    ASSERT_EQ("ZXCV", library[1].name);
}

TEST(InputMappings, save_mapping_refuses_a_blank_name_and_a_full_library)
{
    cfg_store config;
    ASSERT_FALSE(save_mapping(config, custom_mapping("", factory_keys(0, kFour))));

    for (int slot = 0; slot < og::input::kMaxLibraryMappings; ++slot)
    {
        ASSERT_TRUE(save_mapping(
            config, custom_mapping("M" + std::to_string(slot), factory_keys(0, kFour))))
            << "slot " << slot;
    }
    ASSERT_EQ(static_cast<std::size_t>(og::input::kMaxLibraryMappings),
              load_mapping_library(config).size());
    ASSERT_FALSE(save_mapping(config, custom_mapping("OVERFLOW", factory_keys(0, kFour))));
    // An existing name still saves — it reuses its own slot.
    ASSERT_TRUE(save_mapping(config, custom_mapping("M7", factory_keys(1, kFour))));
}

TEST(InputMappings, load_skips_blank_slots_and_defaults_incomplete_entries)
{
    cfg_store config;
    config.apply_setting("mappings", "mapping0_name", "");
    config.apply_setting("mappings", "mapping2_name", "SPARSE");
    config.apply_setting("mappings", "mapping2_mode", "not-a-number");
    config.apply_setting("mappings", "mapping2_mode4_key0", "119");
    config.apply_setting("mappings", "mapping2_mode8_key0", "oops");

    const std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(1u, library.size());
    ASSERT_EQ("SPARSE", library[0].name);
    ASSERT_EQ(kFourMode, library[0].mode);
    ASSERT_EQ(KEYCODE_w, library[0].mode4_keys[static_cast<std::size_t>(KEY_UP)]);
    ASSERT_EQ(KEYCODE_UNKNOWN, library[0].mode8_keys[static_cast<std::size_t>(KEY_UP)]);
    ASSERT_EQ(KEYCODE_UNKNOWN, library[0].mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);
}

TEST(InputMappings, transient_overrides_are_visible_but_not_erasable)
{
    cfg_store config;
    config.apply_override("mappings", "mapping0_name", "GHOST");
    ASSERT_EQ(1u, load_mapping_library(config).size());
    // Overrides are never persisted, so there is nothing in `data` to drop.
    ASSERT_FALSE(erase_mapping(config, "GHOST"));
}

TEST(InputMappings, erase_mapping_drops_every_key_of_the_named_entry)
{
    cfg_store config;
    ASSERT_TRUE(save_mapping(
        config, custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e))));
    ASSERT_FALSE(erase_mapping(config, "NOT-THERE"));
    ASSERT_TRUE(erase_mapping(config, "WQSE"));
    ASSERT_TRUE(load_mapping_library(config).empty());
    ASSERT_TRUE(config.data["mappings"].empty());
}

TEST(InputMappings, resolve_prefers_the_library_then_the_factory_table)
{
    cfg_store config;
    // No entry: the factory table, through the web filter.
    ASSERT_EQ(factory_mapping(1, false), resolve_mapping(config, "ARROWS", false));
    ASSERT_EQ(KEYCODE_z,
              resolve_mapping(config, "WASD", /*web_mode=*/true)
                  .mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);
    ASSERT_EQ(KEYCODE_LCTRL,
              resolve_mapping(config, "WASD", /*web_mode=*/false)
                  .mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);

    // A saved entry under a factory name wins, in web mode too: it is the
    // user's own customization of that layout.
    MappingDefinition customized = factory_mapping(0, false);
    customized.mode4_keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_9;
    ASSERT_TRUE(save_mapping(config, customized));
    ASSERT_EQ(KEYCODE_9,
              resolve_mapping(config, "WASD", /*web_mode=*/true)
                  .mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);

    // An unknown, non-factory name never yields an unplayable seat.
    ASSERT_EQ(factory_mapping(0, false), resolve_mapping(config, "NOPE", false));
}

// --- Seat operations ------------------------------------------------------

TEST(InputMappings, assign_writes_both_mode_maps_and_the_mapping_mode)
{
    ControlStateGuard guard;

    MappingDefinition mapping =
        custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e));
    mapping.mode8_keys[static_cast<std::size_t>(KEY_UP_RIGHT)] = KEYCODE_r;
    mapping.mode4_keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_1;
    mapping.mode8_keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_2;
    mapping.mode = kEightMode;

    ASSERT_TRUE(assign_mapping_to_player(1, mapping));
    ASSERT_EQ(kEightMode, get_player_control_mode(1));
    ASSERT_EQ(KEYCODE_1, get_player_key_binding_for_mode(1, kFourMode, KEY_FIRE));
    ASSERT_EQ(KEYCODE_2, get_player_key_binding_for_mode(1, kEightMode, KEY_FIRE));
    ASSERT_EQ(KEYCODE_r, get_player_key_binding_for_mode(1, kEightMode, KEY_UP_RIGHT));
    ASSERT_EQ(KEYCODE_UNKNOWN, get_player_key_binding_for_mode(1, kFourMode, KEY_UP_RIGHT));
    // The live mirror follows the activated mode.
    ASSERT_EQ(KEYCODE_2, og::runtime::current_session->player_keys_[1][KEY_FIRE]);
    ASSERT_EQ("WQSE", current_mapping_name(1));

    ASSERT_FALSE(assign_mapping_to_player(-1, mapping));
    ASSERT_FALSE(assign_mapping_to_player(4, mapping));
}

TEST(InputMappings, assigning_a_factory_name_swaps_the_reset_identity)
{
    ControlStateGuard guard;
    const int* profiles = input_hardware_state().player_control_default_profiles;

    ASSERT_TRUE(assign_mapping_to_player(0, factory_mapping(2, false)));
    ASSERT_EQ("IJKL", current_mapping_name(0));
    ASSERT_EQ(2, profiles[0]);
    // Seat 2 took seat 0's old identity: the persisted block stays the
    // permutation the cfg loader validates atomically.
    ASSERT_EQ(0, profiles[2]);
    std::array<int, 4> sorted{profiles[0], profiles[1], profiles[2], profiles[3]};
    std::sort(sorted.begin(), sorted.end());
    ASSERT_EQ((std::array<int, 4>{0, 1, 2, 3}), sorted);

    // RESET now restores the layout the seat is named after.
    ASSERT_TRUE(reset_default_player_controls_for_player(0, false));
    ASSERT_EQ("IJKL", current_mapping_name(0));

    // Re-assigning the identity a seat already holds is a no-op.
    ASSERT_TRUE(assign_mapping_to_player(0, factory_mapping(2, false)));
    ASSERT_EQ(2, profiles[0]);
    ASSERT_EQ(0, profiles[2]);

    // A custom mapping leaves the RESET identity alone.
    ASSERT_TRUE(assign_mapping_to_player(
        0, custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e))));
    ASSERT_EQ(2, profiles[0]);
}

TEST(InputMappings, current_name_follows_the_active_mode_and_the_joystick)
{
    ControlStateGuard guard;

    ASSERT_EQ("WASD", current_mapping_name(0));
    ASSERT_EQ("ARROWS", current_mapping_name(1));
    ASSERT_EQ("IJKL", current_mapping_name(2));
    ASSERT_EQ("TFGH", current_mapping_name(3));

    // Switching modes keeps the name: both factory clusters are the same
    // named mapping.
    set_player_control_mode(0, kEightMode);
    ASSERT_EQ("WASD", current_mapping_name(0));

    // A joystick-driven seat is named after its device, whatever its keys say.
    ASSERT_EQ("JOY1", current_mapping_name(0, /*has_joystick=*/true, /*joystick_device=*/0));
    ASSERT_EQ("JOY3", current_mapping_name(0, true, 2));

    ASSERT_EQ(og::input::kCustomMappingName, current_mapping_name(-1));
    ASSERT_EQ(og::input::kCustomMappingName, current_mapping_name(4));
}

TEST(InputMappings, factory_identical_seats_are_not_written_to_the_library)
{
    ControlStateGuard guard;
    cfg_store config;

    ASSERT_FALSE(save_player_mapping_to_library(config, 1, /*web_mode=*/false));
    ASSERT_TRUE(load_mapping_library(config).empty());

    // Customize a non-movement key: the seat is still named ARROWS, and that
    // is the library entry it writes through to.
    set_player_key_binding(1, KEY_FIRE, KEYCODE_9);
    ASSERT_TRUE(save_player_mapping_to_library(config, 1, false));
    std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(1u, library.size());
    ASSERT_EQ("ARROWS", library[0].name);
    ASSERT_EQ(KEYCODE_9, library[0].mode4_keys[static_cast<std::size_t>(KEY_FIRE)]);
    // Only the active mode's map moved; the other one is still factory.
    ASSERT_EQ(KEYCODE_RSHIFT, library[0].mode8_keys[static_cast<std::size_t>(KEY_FIRE)]);

    // RESET undoes the customization — and drops the stale entry, so cycling
    // back to ARROWS does not resurrect it.
    ASSERT_TRUE(reset_default_player_controls_for_player(1, false));
    ASSERT_FALSE(save_player_mapping_to_library(config, 1, false));
    ASSERT_TRUE(load_mapping_library(config).empty());

    ASSERT_FALSE(save_player_mapping_to_library(config, -1, false));
    ASSERT_FALSE(save_player_mapping_to_library(config, 4, false));
}

TEST(InputMappings, library_snapshots_compare_against_the_web_factory_in_web_mode)
{
    ControlStateGuard guard;
    cfg_store config;

    // Native factory keys are a customization as far as a web build is
    // concerned (its WASD puts FIRE on Z), so they get written...
    ASSERT_TRUE(save_player_mapping_to_library(config, 0, /*web_mode=*/true));
    ASSERT_EQ(1u, load_mapping_library(config).size());

    // ...and the web factory layout itself does not.
    ASSERT_TRUE(assign_mapping_to_player(0, factory_mapping(0, /*web_mode=*/true)));
    ASSERT_FALSE(save_player_mapping_to_library(config, 0, /*web_mode=*/true));
    ASSERT_TRUE(load_mapping_library(config).empty());
}

TEST(InputMappings, snapshot_folds_direct_writes_to_the_live_keymap_back_in)
{
    ControlStateGuard guard;
    cfg_store config;

    // player_keys_ is the hot read path; get_player_key_binding_for_mode reads
    // the backing store, so a raw write must be synced before snapshotting.
    og::runtime::current_session->player_keys_[2][KEY_YELL] = KEYCODE_8;
    ASSERT_TRUE(save_player_mapping_to_library(config, 2, false));
    const std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(1u, library.size());
    ASSERT_EQ("IJKL", library[0].name);
    ASSERT_EQ(KEYCODE_8, library[0].mode4_keys[static_cast<std::size_t>(KEY_YELL)]);
}

TEST(InputMappings, snapshot_uses_the_active_mode_cluster_for_the_name)
{
    ControlStateGuard guard;
    cfg_store config;

    // 8-direction seat with a hand-rolled cluster: the name comes from the
    // map that is live, not from the untouched 4-direction one.
    set_player_control_mode(0, kEightMode);
    set_player_key_binding(0, KEY_UP, KEYCODE_8);
    set_player_key_binding(0, KEY_LEFT, KEYCODE_4);
    set_player_key_binding(0, KEY_DOWN, KEYCODE_5);
    set_player_key_binding(0, KEY_RIGHT, KEYCODE_6);
    ASSERT_EQ("8456", current_mapping_name(0));
    ASSERT_TRUE(save_player_mapping_to_library(config, 0, false));

    const std::vector<MappingDefinition> library = load_mapping_library(config);
    ASSERT_EQ(1u, library.size());
    ASSERT_EQ("8456", library[0].name);
    ASSERT_EQ(kEightMode, library[0].mode);
    // The 4-direction map rode along untouched, so cycling back keeps it.
    ASSERT_EQ(KEYCODE_w, library[0].mode4_keys[static_cast<std::size_t>(KEY_UP)]);
}

// --- Cycler options -------------------------------------------------------

TEST(InputMappings, cycler_offers_the_four_factory_names_to_a_solo_seat)
{
    ControlStateGuard guard;
    cfg_store config;

    const std::vector<std::string> options = available_mapping_names(config, 0, 1);
    ASSERT_EQ((std::vector<std::string>{"WASD", "ARROWS", "IJKL", "TFGH"}), options);
}

TEST(InputMappings, cycler_hides_names_other_active_seats_answer_to)
{
    ControlStateGuard guard;
    cfg_store config;

    // Two seats: P2 is on ARROWS, so P1 cannot cycle onto it.
    const std::vector<std::string> two = available_mapping_names(config, 0, 2);
    ASSERT_EQ((std::vector<std::string>{"WASD", "IJKL", "TFGH"}), two);

    // Four seats: only your own name is left.
    ASSERT_EQ((std::vector<std::string>{"TFGH"}), available_mapping_names(config, 3, 4));

    // Inactive seats do not reserve anything.
    ASSERT_EQ(4u, available_mapping_names(config, 0, 1).size());
    // A seat index outside the active range still sees the active seats' claims.
    ASSERT_EQ((std::vector<std::string>{"IJKL", "TFGH"}),
              available_mapping_names(config, 7, 2));
}

TEST(InputMappings, cycler_appends_saved_customs_and_the_seats_own_unsaved_name)
{
    ControlStateGuard guard;
    cfg_store config;

    ASSERT_TRUE(save_mapping(
        config, custom_mapping("ZXCV", cluster(KEYCODE_z, KEYCODE_x, KEYCODE_c, KEYCODE_v))));
    // A saved customization of a factory layout must not double up: the four
    // factory names already cover it (resolve_mapping loads the saved keys).
    MappingDefinition customized_wasd = factory_mapping(0, false);
    customized_wasd.mode4_keys[static_cast<std::size_t>(KEY_YELL)] = KEYCODE_9;
    ASSERT_TRUE(save_mapping(config, customized_wasd));

    std::vector<std::string> options = available_mapping_names(config, 0, 1);
    ASSERT_EQ((std::vector<std::string>{"WASD", "ARROWS", "IJKL", "TFGH", "ZXCV"}), options);
    ASSERT_EQ(1u, count_name(options, "WASD"));

    // An unsaved custom seat still has a current position in the cycle.
    ASSERT_TRUE(assign_mapping_to_player(
        0, custom_mapping("WQSE", cluster(KEYCODE_w, KEYCODE_q, KEYCODE_s, KEYCODE_e))));
    options = available_mapping_names(config, 0, 1);
    ASSERT_TRUE(has_name(options, "WQSE"));
    ASSERT_EQ("WQSE", options.back());
    ASSERT_EQ(1u, count_name(options, "WQSE"));
}

TEST(InputMappings, cycler_keeps_your_own_name_even_when_a_seat_mate_shares_it)
{
    ControlStateGuard guard;
    cfg_store config;

    // Deliberate remapping can put two seats on the same mapping; the cycler
    // must still show each of them where they are.
    ASSERT_TRUE(assign_mapping_to_player(1, factory_mapping(0, false)));
    ASSERT_EQ("WASD", current_mapping_name(1));

    const std::vector<std::string> options = available_mapping_names(config, 0, 2);
    ASSERT_TRUE(has_name(options, "WASD"));
    ASSERT_EQ((std::vector<std::string>{"WASD", "ARROWS", "IJKL", "TFGH"}), options);
}

// --- One-time migration of saved keys (migrate_seat_keys) -----------------
// Pure: every input is a literal old table (tests/test_key_tables.h) and
// every expectation a literal new one.

namespace
{

using og::input::kKeyNotStored;
using og::input::migrate_seat_keys;
using og::input::SeatKeysToMigrate;
namespace tk = og::test_keys;

constexpr std::size_t sz(int k)
{
    return static_cast<std::size_t>(k);
}

// The keys a seat of `profile` had in cfg after any boot of the old game:
// every old factory value (the browser scheme's for web profile 0), the
// change-special key in slot 11 read straight from cfg, nothing in 17/18.
SeatKeysToMigrate pristine_old_seat(int profile, bool web)
{
    SeatKeysToMigrate seat;
    seat.profile = profile;
    for (int m = 0; m < 2; ++m)
    {
        const bool eight = (m == kEight);
        for (int k = 0; k < 17; ++k)
        {
            const int old = (web && profile == 0)
                ? tk::old_web_key(eight, k)
                : (eight ? tk::kOldEightDir[profile][k] : tk::kOldFourDir[profile][k]);
            if (k == KEY_SPECIAL_SWITCH)
                seat.change_special[sz(m)] = old;
            else if (k != KEY_PREFS)
                seat.keys[sz(m)][sz(k)] = old;
        }
    }
    return seat;
}

std::vector<SeatKeysToMigrate> pristine_old_cfg(bool web)
{
    std::vector<SeatKeysToMigrate> seats;
    for (int p = 0; p < 4; ++p)
        seats.push_back(pristine_old_seat(p, web));
    return seats;
}

int key_of(const std::vector<SeatKeysToMigrate>& seats, int p, int m, int k)
{
    return seats[sz(p)].keys[sz(m)][sz(k)];
}

void expect_new_tables(const std::vector<SeatKeysToMigrate>& seats, bool web)
{
    for (int p = 0; p < 4; ++p)
        for (int m = 0; m < 2; ++m)
            for (int k = 0; k < NUM_KEYS; ++k)
                EXPECT_EQ(tk::new_factory_key(p, m == kEight, k, web), key_of(seats, p, m, k))
                    << "P" << (p + 1) << (m == kEight ? " 8-dir" : " 4-dir") << " slot " << k
                    << (web ? " (web)" : " (native)");
}

} // namespace

TEST(MigrateSeatKeys, pristine_four_player_cfg_reaches_the_new_tables_in_both_modes)
{
    for (const bool web : {false, true})
    {
        std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(web);
        migrate_seat_keys(seats, web, 0);
        expect_new_tables(seats, web);
    }
}

TEST(MigrateSeatKeys, factory_slots_move_together_when_the_tables_hand_keys_around)
{
    // The old and new tables hand keys around inside one seat: P2's RSHIFT
    // goes SHIFTER -> FIRE, P4's 'b' LOOK-UP -> SHIFTER, P4's 'g' 8-dir YELL
    // -> 8-dir FIRE. A slot-by-slot rule refuses the move because the later
    // slot still holds the key; the whole-table rule must not.
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    ASSERT_EQ(KEYCODE_RSHIFT, key_of(seats, 1, kFour, KEY_SHIFTER));
    ASSERT_EQ(KEYCODE_b, key_of(seats, 3, kFour, KEY_LOOKUP));
    ASSERT_EQ(KEYCODE_g, key_of(seats, 3, kEight, KEY_YELL));
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_RSHIFT, key_of(seats, 1, kFour, KEY_FIRE));
    EXPECT_EQ(KEYCODE_SLASH, key_of(seats, 1, kFour, KEY_SHIFTER));
    EXPECT_EQ(KEYCODE_b, key_of(seats, 3, kFour, KEY_SHIFTER));
    EXPECT_EQ(KEYCODE_r, key_of(seats, 3, kFour, KEY_LOOKUP));
    EXPECT_EQ(KEYCODE_g, key_of(seats, 3, kEight, KEY_FIRE));
    EXPECT_EQ(KEYCODE_7, key_of(seats, 3, kEight, KEY_YELL));

    // And no seat's result depends on the others when nobody chose a key.
    for (int p = 0; p < 4; ++p)
    {
        std::vector<SeatKeysToMigrate> alone{pristine_old_seat(p, false)};
        migrate_seat_keys(alone, false, 0);
        EXPECT_EQ(seats[sz(p)].keys, alone[0].keys) << "P" << (p + 1);
    }
}

TEST(MigrateSeatKeys, never_stored_slots_are_factory_and_reserved_slots_come_out_unbound)
{
    // A brand-new cfg: nothing stored at all.
    std::vector<SeatKeysToMigrate> seats(4);
    for (int p = 0; p < 4; ++p)
        seats[sz(p)].profile = p;
    // A stale value in a reserved slot is dropped, whatever it was.
    seats[0].keys[sz(kFour)][sz(KEY_PREFS)] = KEYCODE_1;
    seats[0].keys[sz(kFour)][sz(KEY_SPECIAL_SWITCH)] = KEYCODE_TAB;
    migrate_seat_keys(seats, false, 0);
    expect_new_tables(seats, false);
}

TEST(MigrateSeatKeys, a_custom_key_beats_the_factory_key_it_collides_with_same_seat)
{
    // P1 bound YELL to '2' itself: it keeps it, and SPEC 2 (factory '2')
    // goes unbound in that map instead of taking the player's key.
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    seats[0].keys[sz(kFour)][sz(KEY_YELL)] = KEYCODE_2;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_2, key_of(seats, 0, kFour, KEY_YELL));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_SPEC2));
    // The 8-direction map is never active with the 4-direction one: its
    // SPEC 2 keeps the factory '2'.
    EXPECT_EQ(KEYCODE_2, key_of(seats, 0, kEight, KEY_SPEC2));
    // Everything else still reached the new table.
    EXPECT_EQ(KEYCODE_1, key_of(seats, 0, kFour, KEY_SPECIAL));
    EXPECT_EQ(KEYCODE_3, key_of(seats, 0, kFour, KEY_SPEC3));
    EXPECT_EQ(KEYCODE_q, key_of(seats, 0, kFour, KEY_LOOKUP));
}

TEST(MigrateSeatKeys, a_custom_key_beats_a_factory_key_across_seats_in_either_mode)
{
    // P2 chose '8' for FIRE in its 8-direction map only: P3's SPEC 1 is '8'
    // in BOTH of P3's maps, and both go unbound (any pair of modes can be
    // live on a shared keyboard). P2 keeps its key.
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    seats[1].keys[sz(kEight)][sz(KEY_FIRE)] = KEYCODE_8;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_8, key_of(seats, 1, kEight, KEY_FIRE));
    EXPECT_EQ(KEYCODE_RSHIFT, key_of(seats, 1, kFour, KEY_FIRE));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 2, kFour, KEY_SPECIAL));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 2, kEight, KEY_SPECIAL));
    EXPECT_EQ(KEYCODE_9, key_of(seats, 2, kFour, KEY_SPEC2));
}

TEST(MigrateSeatKeys, custom_against_custom_is_left_as_found)
{
    // Two players who chose the same key before the migration keep it; the
    // migration only ever yields FACTORY slots.
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    seats[0].keys[sz(kFour)][sz(KEY_CHEAT)] = KEYCODE_F12;
    seats[1].keys[sz(kFour)][sz(KEY_CHEAT)] = KEYCODE_F12;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_F12, key_of(seats, 0, kFour, KEY_CHEAT));
    EXPECT_EQ(KEYCODE_F12, key_of(seats, 1, kFour, KEY_CHEAT));
}

TEST(MigrateSeatKeys, custom_change_special_read_from_cfg_becomes_spec2)
{
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    // P1 had moved change special to F9 in the 4-direction map; its
    // 8-direction map kept the factory Tab.
    seats[0].change_special[sz(kFour)] = KEYCODE_F9;
    // P3 had cleared change special: no custom key, so SPEC 2 is factory.
    seats[2].change_special = {KEYCODE_UNKNOWN, KEYCODE_UNKNOWN};
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_F9, key_of(seats, 0, kFour, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_2, key_of(seats, 0, kEight, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_SPECIAL_SWITCH));
    EXPECT_EQ(KEYCODE_9, key_of(seats, 2, kFour, KEY_SPEC2));

    // A custom change-special key is a custom key like any other: P2's
    // factory SPEC 1 '[' yields to P1's change-special '['.
    seats = pristine_old_cfg(false);
    seats[0].change_special[sz(kFour)] = KEYCODE_LEFTBRACKET;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_LEFTBRACKET, key_of(seats, 0, kFour, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 1, kFour, KEY_SPECIAL));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 1, kEight, KEY_SPECIAL));
}

TEST(MigrateSeatKeys, a_spec_key_already_stored_is_kept)
{
    // A cfg that already carries SPEC 2/3 (the marker was lost after a
    // migrated boot): the player's own SPEC key stays, and beats the old
    // change-special key; the factory value is simply factory.
    std::vector<SeatKeysToMigrate> seats = pristine_old_cfg(false);
    seats[0].keys[sz(kFour)][sz(KEY_SPEC2)] = KEYCODE_F10;
    seats[0].change_special[sz(kFour)] = KEYCODE_F9;
    seats[0].keys[sz(kFour)][sz(KEY_SPEC3)] = KEYCODE_3;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_F10, key_of(seats, 0, kFour, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_3, key_of(seats, 0, kFour, KEY_SPEC3));
}

TEST(MigrateSeatKeys, web_pre_v1_and_v1_values_are_both_factory)
{
    // Profile 0 on web: the native values (before the browser scheme, the
    // "pre-v1" cfg) and the browser scheme's values (v1) both move.
    for (const bool v1 : {false, true})
    {
        std::vector<SeatKeysToMigrate> seats{pristine_old_seat(0, /*web=*/v1)};
        migrate_seat_keys(seats, /*web_mode=*/true, 0);
        for (int m = 0; m < 2; ++m)
            for (int k = 0; k < NUM_KEYS; ++k)
                EXPECT_EQ(tk::new_factory_key(0, m == kEight, k, true), key_of(seats, 0, m, k))
                    << (v1 ? "v1" : "pre-v1") << " mode " << m << " slot " << k;
    }

    // Natively the browser scheme's x is nobody's factory key: kept.
    std::vector<SeatKeysToMigrate> seats{pristine_old_seat(0, false)};
    seats[0].keys[sz(kFour)][sz(KEY_SPECIAL)] = KEYCODE_x;
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_x, key_of(seats, 0, kFour, KEY_SPECIAL));
    // And on web the browser values are factory only for profile 0.
    seats = {pristine_old_seat(1, false)};
    seats[0].keys[sz(kFour)][sz(KEY_FIRE)] = KEYCODE_z;
    migrate_seat_keys(seats, true, 0);
    EXPECT_EQ(KEYCODE_z, key_of(seats, 0, kFour, KEY_FIRE));
}

TEST(MigrateSeatKeys, custom_entry_loads_17_from_11_and_18_unbound)
{
    // profile -1: a CUSTOM library entry, nothing in it is factory.
    SeatKeysToMigrate entry;
    entry.profile = -1;
    entry.keys[sz(kFour)][sz(KEY_UP)] = KEYCODE_w;
    entry.keys[sz(kFour)][sz(KEY_SPECIAL)] = KEYCODE_LALT;
    entry.change_special = {KEYCODE_TAB, KEYCODE_UNKNOWN};
    std::vector<SeatKeysToMigrate> seats{entry};
    migrate_seat_keys(seats, false, 0);
    EXPECT_EQ(KEYCODE_w, key_of(seats, 0, kFour, KEY_UP));
    EXPECT_EQ(KEYCODE_LALT, key_of(seats, 0, kFour, KEY_SPECIAL))
        << "an old factory value is still the entry's own choice";
    EXPECT_EQ(KEYCODE_TAB, key_of(seats, 0, kFour, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_SPEC3));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kEight, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_FIRE)) << "never stored: unbound";
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_SPECIAL_SWITCH));
}

TEST(MigrateSeatKeys, legacy_reader_is_the_d758bcb1_table)
{
    for (int p = 0; p < 4; ++p)
        for (int k = 0; k < 17; ++k)
        {
            EXPECT_EQ(tk::kOldFourDir[p][k], og::input::legacy_factory_key(p, kFour, k));
            EXPECT_EQ(tk::kOldEightDir[p][k], og::input::legacy_factory_key(p, kEight, k));
        }
    EXPECT_EQ(KEYCODE_UNKNOWN, og::input::legacy_factory_key(0, kFour, KEY_SPEC2));
    EXPECT_EQ(KEYCODE_UNKNOWN, og::input::legacy_factory_key(-1, kFour, KEY_UP));
    EXPECT_EQ(KEYCODE_UNKNOWN, og::input::legacy_factory_key(4, kFour, KEY_UP));
    EXPECT_EQ(KEYCODE_UNKNOWN, og::input::legacy_factory_key(0, kFour, -1));
}

// --- Version 1 -> 2 (#391): web P2 LOOK UP leaves Right Ctrl -------------

namespace
{

// A seat as a version-1 build (New Ability Select) left it in cfg: every
// slot written, today's factory keys, except web P2 LOOK UP still on Right
// Ctrl, and the reserved slots unbound.
SeatKeysToMigrate v1_seat(int profile, bool web)
{
    SeatKeysToMigrate seat;
    seat.profile = profile;
    for (int m = 0; m < 2; ++m)
    {
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            int key = tk::new_factory_key(profile, m == kEight, k, web);
            if (web && profile == 1 && k == KEY_LOOKUP)
                key = KEYCODE_RCTRL;
            seat.keys[sz(m)][sz(k)] = key;
        }
        seat.change_special[sz(m)] = KEYCODE_UNKNOWN;
    }
    return seat;
}

std::vector<SeatKeysToMigrate> v1_cfg(bool web)
{
    std::vector<SeatKeysToMigrate> seats;
    for (int p = 0; p < 4; ++p)
        seats.push_back(v1_seat(p, web));
    return seats;
}

} // namespace

TEST(MigrateSeatKeys, v1_to_v2_web_moves_p2_look_up_to_delete_in_both_maps)
{
    ASSERT_EQ(2, og::input::kDefaultKeysVersion);
    std::vector<SeatKeysToMigrate> seats = v1_cfg(true);
    ASSERT_EQ(KEYCODE_RCTRL, key_of(seats, 1, kFour, KEY_LOOKUP));
    ASSERT_EQ(KEYCODE_RCTRL, key_of(seats, 1, kEight, KEY_LOOKUP));
    migrate_seat_keys(seats, /*web_mode=*/true, 1);
    EXPECT_EQ(KEYCODE_DELETE, key_of(seats, 1, kFour, KEY_LOOKUP));
    EXPECT_EQ(KEYCODE_DELETE, key_of(seats, 1, kEight, KEY_LOOKUP));
    expect_new_tables(seats, true);
}

TEST(MigrateSeatKeys, v1_to_v2_web_keeps_a_chosen_look_up_and_yields_delete_to_a_chosen_key)
{
    // P2 had moved LOOK UP off Right Ctrl itself: that is P2's choice, kept.
    std::vector<SeatKeysToMigrate> seats = v1_cfg(true);
    seats[1].keys[sz(kFour)][sz(KEY_LOOKUP)] = KEYCODE_F9;
    migrate_seat_keys(seats, true, 1);
    EXPECT_EQ(KEYCODE_F9, key_of(seats, 1, kFour, KEY_LOOKUP));
    EXPECT_EQ(KEYCODE_DELETE, key_of(seats, 1, kEight, KEY_LOOKUP));

    // P3 had bound YELL to Delete: P2's LOOK UP stays factory but has no key
    // to go to, so it comes out unbound in both maps (the C.5 rule), and
    // P3 keeps Delete.
    seats = v1_cfg(true);
    seats[2].keys[sz(kFour)][sz(KEY_YELL)] = KEYCODE_DELETE;
    migrate_seat_keys(seats, true, 1);
    EXPECT_EQ(KEYCODE_DELETE, key_of(seats, 2, kFour, KEY_YELL));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 1, kFour, KEY_LOOKUP));
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 1, kEight, KEY_LOOKUP));
    // Nothing else yielded.
    EXPECT_EQ(KEYCODE_RSHIFT, key_of(seats, 1, kFour, KEY_FIRE));
    EXPECT_EQ(KEYCODE_o, key_of(seats, 2, kEight, KEY_UP_RIGHT));
}

TEST(MigrateSeatKeys, v1_to_v2_native_changes_nothing)
{
    // A pristine native v1 cfg is already on the version-2 tables.
    std::vector<SeatKeysToMigrate> seats = v1_cfg(false);
    migrate_seat_keys(seats, false, 1);
    expect_new_tables(seats, false);

    // A player's own key that equals ANOTHER seat's factory key: P2 bound
    // YELL to 'w', P1's UP. Version 1 already accepted that pair, and P1's
    // UP does not move, so nothing may unbind it. (Re-running the version-0
    // rule here unbinds P1's UP: under the old tables 'w' is still a
    // factory key and P2's 'w' a custom one.)
    seats = v1_cfg(false);
    seats[1].keys[sz(kFour)][sz(KEY_YELL)] = KEYCODE_w;
    // And P1 bound YELL to 'r', P4's 4-direction LOOK UP.
    seats[0].keys[sz(kFour)][sz(KEY_YELL)] = KEYCODE_r;
    const std::vector<SeatKeysToMigrate> before = seats;
    migrate_seat_keys(seats, false, 1);
    for (int p = 0; p < 4; ++p)
        EXPECT_EQ(before[sz(p)].keys, seats[sz(p)].keys) << "P" << (p + 1);
    EXPECT_EQ(KEYCODE_w, key_of(seats, 0, kFour, KEY_UP));
    EXPECT_EQ(KEYCODE_r, key_of(seats, 3, kFour, KEY_LOOKUP));
}

TEST(MigrateSeatKeys, current_version_moves_nothing)
{
    std::vector<SeatKeysToMigrate> seats = v1_cfg(true);
    seats[0].keys[sz(kFour)][sz(KEY_SPEC3)] = kKeyNotStored;
    migrate_seat_keys(seats, true, og::input::kDefaultKeysVersion);
    EXPECT_EQ(KEYCODE_RCTRL, key_of(seats, 1, kFour, KEY_LOOKUP))
        << "a current cfg's keys are the player's";
    EXPECT_EQ(KEYCODE_UNKNOWN, key_of(seats, 0, kFour, KEY_SPEC3));
}

TEST(InputMappings, library_entry_stamped_v1_migrates_p2_look_up_on_web_only)
{
    // The factory-named ARROWS entry saved by a version-1 build, with SPEC 3
    // never stored (the library's own "not stored" case): on web its Right
    // Ctrl LOOK UP moves to Delete and SPEC 3 reads the factory key; natively
    // Right Ctrl stays.
    cfg_store config;
    config.apply_setting("mappings", "mapping0_name", "ARROWS");
    config.apply_setting("mappings", "mapping0_mode", "4");
    config.apply_setting("mappings", "mapping0_default_keys_version", "1");
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        if (k == KEY_SPEC3)
            continue;
        const int four = (k == KEY_LOOKUP) ? KEYCODE_RCTRL : tk::kNewFourDir[1][k];
        const int eight = (k == KEY_LOOKUP) ? KEYCODE_RCTRL : tk::kNewEightDir[1][k];
        config.apply_setting("mappings", std::format("mapping0_mode4_key{}", k),
                             std::to_string(four));
        config.apply_setting("mappings", std::format("mapping0_mode8_key{}", k),
                             std::to_string(eight));
    }

    const std::vector<MappingDefinition> web = load_mapping_library(config, true);
    ASSERT_EQ(1u, web.size());
    EXPECT_EQ(KEYCODE_DELETE, web[0].mode4_keys[sz(KEY_LOOKUP)]);
    EXPECT_EQ(KEYCODE_DELETE, web[0].mode8_keys[sz(KEY_LOOKUP)]);
    EXPECT_EQ(KEYCODE_BACKSLASH, web[0].mode4_keys[sz(KEY_SPEC3)]);
    EXPECT_EQ(KEYCODE_UP, web[0].mode4_keys[sz(KEY_UP)]);

    const std::vector<MappingDefinition> native = load_mapping_library(config, false);
    ASSERT_EQ(1u, native.size());
    EXPECT_EQ(KEYCODE_RCTRL, native[0].mode4_keys[sz(KEY_LOOKUP)]);
    EXPECT_EQ(KEYCODE_BACKSLASH, native[0].mode4_keys[sz(KEY_SPEC3)]);

    // Stamped current: the stored Right Ctrl is the entry's own choice.
    config.apply_setting("mappings", "mapping0_default_keys_version",
                         std::to_string(og::input::kDefaultKeysVersion));
    EXPECT_EQ(KEYCODE_RCTRL, load_mapping_library(config, true)[0].mode4_keys[sz(KEY_LOOKUP)]);
}

// --- The library migrates its pre-New Ability Select entries --------------

namespace
{

// A library entry as the old game wrote it: slots 0..16 and no version.
void write_old_entry(cfg_store& config, int slot, const std::string& name,
                     const int (&four)[17], const int (&eight)[17])
{
    config.apply_setting("mappings", std::format("mapping{}_name", slot), name);
    config.apply_setting("mappings", std::format("mapping{}_mode", slot), "4");
    for (int k = 0; k < 17; ++k)
    {
        config.apply_setting("mappings", std::format("mapping{}_mode4_key{}", slot, k),
                             std::to_string(four[k]));
        config.apply_setting("mappings", std::format("mapping{}_mode8_key{}", slot, k),
                             std::to_string(eight[k]));
    }
}

} // namespace

TEST(InputMappings, library_entry_from_before_loads_spec2_3_at_factory)
{
    // A customized WASD (FIRE on F9) saved by the old game: the custom key
    // stays, every factory key moves, SPEC 2/3 arrive at their factory keys.
    int four[17];
    int eight[17];
    for (int k = 0; k < 17; ++k)
    {
        four[k] = tk::kOldFourDir[0][k];
        eight[k] = tk::kOldEightDir[0][k];
    }
    four[KEY_FIRE] = KEYCODE_F9;
    cfg_store config;
    write_old_entry(config, 0, "WASD", four, eight);

    const std::vector<MappingDefinition> library = load_mapping_library(config, false);
    ASSERT_EQ(1u, library.size());
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        const int expected4 = (k == KEY_FIRE) ? KEYCODE_F9 : tk::kNewFourDir[0][k];
        EXPECT_EQ(expected4, library[0].mode4_keys[sz(k)]) << "4-dir slot " << k;
        EXPECT_EQ(tk::kNewEightDir[0][k], library[0].mode8_keys[sz(k)]) << "8-dir slot " << k;
    }
    // The web flavour of the same entry reaches the web table.
    const std::vector<MappingDefinition> web = load_mapping_library(config, true);
    EXPECT_EQ(KEYCODE_TAB, web[0].mode8_keys[sz(KEY_YELL)]);
    EXPECT_EQ(KEYCODE_F9, web[0].mode4_keys[sz(KEY_FIRE)]);
}

TEST(InputMappings, library_entries_migrate_by_factory_name)
{
    // The profile is the entry's NAME: an "IJKL" entry migrates against
    // P3's old table, and a CUSTOM entry keeps every key it has, with SPEC 2
    // from its change-special key and SPEC 3 unbound.
    int four[17];
    int eight[17];
    for (int k = 0; k < 17; ++k)
    {
        four[k] = tk::kOldFourDir[2][k];
        eight[k] = tk::kOldEightDir[2][k];
    }
    cfg_store config;
    write_old_entry(config, 0, "IJKL", four, eight);
    write_old_entry(config, 1, "ZXCV", four, eight);

    const std::vector<MappingDefinition> library = load_mapping_library(config, false);
    ASSERT_EQ(2u, library.size());
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        EXPECT_EQ(tk::kNewFourDir[2][k], library[0].mode4_keys[sz(k)]) << "IJKL 4-dir slot " << k;
        EXPECT_EQ(tk::kNewEightDir[2][k], library[0].mode8_keys[sz(k)]) << "IJKL 8-dir slot " << k;
    }
    for (int k = 0; k < 17; ++k)
    {
        if (k == KEY_SPECIAL_SWITCH || k == KEY_PREFS)
            continue;
        EXPECT_EQ(tk::kOldFourDir[2][k], library[1].mode4_keys[sz(k)]) << "custom slot " << k;
    }
    EXPECT_EQ(KEYCODE_9, library[1].mode4_keys[sz(KEY_SPEC2)]);
    EXPECT_EQ(KEYCODE_UNKNOWN, library[1].mode4_keys[sz(KEY_SPEC3)]);
    EXPECT_EQ(KEYCODE_UNKNOWN, library[1].mode4_keys[sz(KEY_SPECIAL_SWITCH)]);
}

TEST(InputMappings, saved_entries_are_stamped_and_never_migrated_again)
{
    // An entry saved now carries mapping{K}_keys_version, so a deliberate old
    // factory key in it (LAlt on SPEC 1) survives every later load, and the
    // change-special slot is dropped on load like slot 14.
    cfg_store config;
    MappingDefinition mapping = factory_mapping(0, false);
    mapping.mode4_keys[sz(KEY_SPECIAL)] = KEYCODE_LALT;
    mapping.mode4_keys[sz(KEY_SPECIAL_SWITCH)] = KEYCODE_TAB;
    ASSERT_TRUE(save_mapping(config, mapping));
    EXPECT_EQ(std::to_string(og::input::kDefaultKeysVersion),
              config.get_setting("mappings", "mapping0_default_keys_version"));

    const std::vector<MappingDefinition> library = load_mapping_library(config, false);
    ASSERT_EQ(1u, library.size());
    EXPECT_EQ(KEYCODE_LALT, library[0].mode4_keys[sz(KEY_SPECIAL)]);
    EXPECT_EQ(KEYCODE_UNKNOWN, library[0].mode4_keys[sz(KEY_SPECIAL_SWITCH)]);
    EXPECT_EQ(KEYCODE_2, library[0].mode4_keys[sz(KEY_SPEC2)]);

    // Erasing drops the stamp with the rest of the entry.
    ASSERT_TRUE(erase_mapping(config, "WASD"));
    EXPECT_TRUE(config.data["mappings"].empty());
}

// The web touch overlay flag starts down: only openglad_web_touch_enable
// raises it, so native, text and curses clients never hide seat 0's hotbar.
TEST(InputHardwareState, touch_overlay_starts_down)
{
    EXPECT_FALSE(InputHardwareState{}.touch_overlay_active);
}

// --- One key at a time (#358) ---------------------------------------------

// The pure rule: the other slots that held the key come out unbound; the
// slot itself, the reserved slots and an unbound key are left alone.
//
// RED (planted): unbind_slots_holding returns without touching the map.
TEST(InputMappings, unbind_slots_holding_unbinds_only_the_other_holders)
{
    KeyMap keys{};
    keys.fill(KEYCODE_UNKNOWN);
    keys[static_cast<std::size_t>(KEY_FIRE)] = KEYCODE_9;
    keys[static_cast<std::size_t>(KEY_SPEC2)] = KEYCODE_9;
    keys[static_cast<std::size_t>(KEY_YELL)] = KEYCODE_8;
    keys[static_cast<std::size_t>(KEY_SPECIAL_SWITCH)] = KEYCODE_9;
    keys[static_cast<std::size_t>(KEY_PREFS)] = KEYCODE_9;

    const std::vector<int> unbound =
        og::input::unbind_slots_holding(keys, KEY_SPECIAL, KEYCODE_9);
    EXPECT_EQ((std::vector<int>{KEY_FIRE, KEY_SPEC2}), unbound);
    EXPECT_EQ(KEYCODE_UNKNOWN, keys[static_cast<std::size_t>(KEY_FIRE)]);
    EXPECT_EQ(KEYCODE_UNKNOWN, keys[static_cast<std::size_t>(KEY_SPEC2)]);
    EXPECT_EQ(KEYCODE_8, keys[static_cast<std::size_t>(KEY_YELL)]);
    EXPECT_EQ(KEYCODE_9, keys[static_cast<std::size_t>(KEY_SPECIAL_SWITCH)])
        << "reserved slots are never touched";
    EXPECT_EQ(KEYCODE_9, keys[static_cast<std::size_t>(KEY_PREFS)]);

    // The slot's own key is not "another holder".
    keys[static_cast<std::size_t>(KEY_YELL)] = KEYCODE_8;
    EXPECT_TRUE(og::input::unbind_slots_holding(keys, KEY_YELL, KEYCODE_8).empty());
    EXPECT_EQ(KEYCODE_8, keys[static_cast<std::size_t>(KEY_YELL)]);
    // An unbound key unbinds nothing (every "--" slot holds it).
    EXPECT_TRUE(
        og::input::unbind_slots_holding(keys, KEY_FIRE, KEYCODE_UNKNOWN).empty());
}

// The seat writer: the current mode's map only, the key on the slot, the
// seat's other holder unbound, another seat's same key untouched.
TEST(InputMappings, bind_player_key_moves_the_key_inside_one_seat_and_mode)
{
    ControlStateGuard guard;
    set_player_control_mode(0, kFourMode);
    const int special = get_player_key_binding_for_mode(0, kFourMode, KEY_SPECIAL);
    const int eight_special =
        get_player_key_binding_for_mode(0, kEightMode, KEY_SPECIAL);
    ASSERT_NE(KEYCODE_UNKNOWN, special);
    set_player_key_binding(1, KEY_YELL, special);

    og::input::bind_player_key(0, KEY_FIRE, special);
    EXPECT_EQ(special, get_player_key_binding_for_mode(0, kFourMode, KEY_FIRE));
    EXPECT_EQ(special, og::runtime::current_session->player_keys_[0][KEY_FIRE]);
    EXPECT_EQ(KEYCODE_UNKNOWN,
              get_player_key_binding_for_mode(0, kFourMode, KEY_SPECIAL));
    EXPECT_EQ(KEYCODE_UNKNOWN,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL]);
    EXPECT_EQ(eight_special,
              get_player_key_binding_for_mode(0, kEightMode, KEY_SPECIAL))
        << "the other direction mode's map is its own";
    EXPECT_EQ(special, og::runtime::current_session->player_keys_[1][KEY_YELL])
        << "another seat keeps its key";

    // Out of range: nothing happens.
    og::input::bind_player_key(4, KEY_FIRE, KEYCODE_9);
    og::input::bind_player_key(0, NUM_KEYS, KEYCODE_9);
    EXPECT_EQ(special, og::runtime::current_session->player_keys_[0][KEY_FIRE]);
}
