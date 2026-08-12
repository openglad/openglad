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
#include <string>
#include <vector>

#include <gtest/gtest.h>

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
    // "P2 ARROW " is exactly the nine characters the 57px card face holds.
    ASSERT_EQ("ARROW", mapping_short_name("ARROWS"));
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
    ASSERT_EQ(KEYCODE_x, web.mode4_keys[static_cast<std::size_t>(KEY_SPECIAL)]);
    ASSERT_EQ(KEYCODE_s, web.mode8_keys[static_cast<std::size_t>(KEY_FIRE)]);
    ASSERT_EQ(KEYCODE_v, web.mode8_keys[static_cast<std::size_t>(KEY_YELL)]);
    ASSERT_EQ(KEYCODE_UNKNOWN, web.mode8_keys[static_cast<std::size_t>(KEY_LOOKUP)]);
    // Movement is untouched, so the name survives the substitution.
    ASSERT_EQ("WASD", derive_mapping_name(web.mode4_keys));

    // Profiles 1-3 never move.
    for (int profile = 1; profile < 4; ++profile)
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
    ASSERT_EQ(KEYCODE_PERIOD, library[0].mode8_keys[static_cast<std::size_t>(KEY_FIRE)]);

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
