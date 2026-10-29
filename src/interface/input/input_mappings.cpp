// Named input mappings: derivation, the cfg-persisted library, and the seat
// operations the INPUT cycler is built from. See input_mappings.h for the
// model and docs/pause-menu-design.md §3 for the design.
//
// SDL-free: everything here works off KEYCODE_* integers and cfg_store, so the
// headless clients share the names the SDL client shows.

#include <openglad/interface/input_mappings.h>

#include <openglad/core/util.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace og::input
{
namespace
{

constexpr int kProfileCount = 4;
constexpr int kModeFour = kWebModeFourIndex;
constexpr int kModeEight = kWebModeEightIndex;

// Factory names, indexed by profile. These are the names the derivation rule
// answers with whenever a cluster matches that profile's factory movement keys
// in EITHER mode — profile 0's 8-direction DOWN is 'x', not 's', and it is
// still WASD.
constexpr const char* kFactoryNames[kProfileCount] = {
    "WASD", "ARROWS", "IJKL", "TFGH",
};

std::size_t key_slot(int key_enum)
{
    return static_cast<std::size_t>(key_enum);
}

// A keycode's one-character name, or '\0' when it has none. Printable ASCII
// keycodes are their own name (letters, digits, punctuation); everything else
// — arrows, modifiers, function keys, Space, unbound — needs a word, which is
// exactly what makes a cluster unspellable.
char single_char_key_name(int keycode)
{
    if (keycode <= ' ' || keycode > '~')
        return '\0';
    char c = static_cast<char>(keycode);
    if (c >= 'a' && c <= 'z')
        c = static_cast<char>(c - ('a' - 'A'));
    return c;
}

bool cluster_matches_factory(const int* keys, int profile, int mode_index)
{
    return keys[key_slot(KEY_UP)] == factory_default_key(profile, mode_index, KEY_UP) &&
           keys[key_slot(KEY_LEFT)] == factory_default_key(profile, mode_index, KEY_LEFT) &&
           keys[key_slot(KEY_DOWN)] == factory_default_key(profile, mode_index, KEY_DOWN) &&
           keys[key_slot(KEY_RIGHT)] == factory_default_key(profile, mode_index, KEY_RIGHT);
}

std::string slot_key(int slot, const std::string& suffix)
{
    return std::format("mapping{}_{}", slot, suffix);
}

// Presence test that does not go through cfg_store::get_setting: that logs
// every miss, and the library scan probes 32 slots that are normally empty.
const std::string* find_setting(cfg_store& config, const std::string& setting)
{
    const auto overridden = config.overrides.find(kMappingCfgCategory);
    if (overridden != config.overrides.end())
    {
        const auto entry = overridden->second.find(setting);
        if (entry != overridden->second.end())
            return &entry->second;
    }
    const auto stored = config.data.find(kMappingCfgCategory);
    if (stored != config.data.end())
    {
        const auto entry = stored->second.find(setting);
        if (entry != stored->second.end())
            return &entry->second;
    }
    return nullptr;
}

// A stored key, or `fallback` when the entry does not hold one.
int parse_key_setting(cfg_store& config, int slot, const std::string& suffix, int fallback)
{
    const std::string* raw = find_setting(config, slot_key(slot, suffix));
    if (raw == nullptr)
        return fallback;
    return parse_int_strict(*raw).value_or(fallback);
}

int normalized_mode(int mode)
{
    return (mode == static_cast<int>(ControlDirectionMode::EightDirection))
        ? static_cast<int>(ControlDirectionMode::EightDirection)
        : static_cast<int>(ControlDirectionMode::FourDirection);
}

// Slot holding this name, or -1.
int find_named_slot(cfg_store& config, const std::string& name)
{
    for (int slot = 0; slot < kMaxLibraryMappings; ++slot)
    {
        const std::string* stored = find_setting(config, slot_key(slot, "name"));
        if (stored != nullptr && *stored == name)
            return slot;
    }
    return -1;
}

int find_free_slot(cfg_store& config)
{
    for (int slot = 0; slot < kMaxLibraryMappings; ++slot)
    {
        if (find_setting(config, slot_key(slot, "name")) == nullptr)
            return slot;
    }
    return -1;
}

bool contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

std::string factory_mapping_name(int profile)
{
    if (profile < 0 || profile >= kProfileCount)
        return std::string();
    return kFactoryNames[profile];
}

int factory_profile_for_name(const std::string& name)
{
    for (int profile = 0; profile < kProfileCount; ++profile)
    {
        if (name == kFactoryNames[profile])
            return profile;
    }
    return -1;
}

std::string derive_mapping_name(const int* keys)
{
    if (keys == nullptr)
        return kCustomMappingName;

    for (int profile = 0; profile < kProfileCount; ++profile)
    {
        if (cluster_matches_factory(keys, profile, kModeFour) ||
            cluster_matches_factory(keys, profile, kModeEight))
            return kFactoryNames[profile];
    }

    std::string name;
    for (const int key_enum : {KEY_UP, KEY_LEFT, KEY_DOWN, KEY_RIGHT})
    {
        const char spelled = single_char_key_name(keys[key_slot(key_enum)]);
        if (spelled == '\0')
            return kCustomMappingName;
        name.push_back(spelled);
    }
    // Four one-character names never overflow the budget; the clamp is the
    // contract, not a live path.
    if (name.size() > static_cast<std::size_t>(kMappingNameMaxLength)) name.resize(static_cast<std::size_t>(kMappingNameMaxLength));
    return name;
}

std::string derive_mapping_name(const KeyMap& keys)
{
    return derive_mapping_name(keys.data());
}

std::string mapping_display_name(const std::string& name)
{
    if (name == "ARROWS")
        return kArrowGlyphs;
    return name;
}

std::string mapping_short_name(const std::string& name)
{
    // The arrow glyph cluster (4 chars) beats the "ARROW" truncation, which
    // read like a typo on a seat card.
    if (name == "ARROWS")
        return kArrowGlyphs;
    // "CUSTOM" truncated is "CUSTO" — same problem.
    if (name == kCustomMappingName)
        return "CUST";
    if (name.size() <= static_cast<std::size_t>(kMappingShortNameMaxLength))
        return name;
    return name.substr(0, static_cast<std::size_t>(kMappingShortNameMaxLength));
}

std::string joystick_mapping_name(int device_index)
{
    if (device_index < 0)
        return "JOY";
    return std::format("JOY{}", device_index + 1);
}

MappingDefinition factory_mapping(int profile, bool web_mode)
{
    if (profile < 0 || profile >= kProfileCount)
        profile = 0;

    MappingDefinition mapping;
    mapping.name = kFactoryNames[profile];
    mapping.mode = factory_default_mode(profile);
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        mapping.mode4_keys[key_slot(k)] = browser_safe_default_key(
            profile, kModeFour, k, factory_default_key(profile, kModeFour, k), web_mode);
        mapping.mode8_keys[key_slot(k)] = browser_safe_default_key(
            profile, kModeEight, k, factory_default_key(profile, kModeEight, k), web_mode);
    }
    return mapping;
}

std::vector<MappingDefinition> load_mapping_library(cfg_store& config, bool web_mode)
{
    std::vector<MappingDefinition> library;
    for (int slot = 0; slot < kMaxLibraryMappings; ++slot)
    {
        const std::string* name = find_setting(config, slot_key(slot, "name"));
        if (name == nullptr || name->empty())
            continue;

        MappingDefinition mapping;
        mapping.name = *name;
        const std::string* mode = find_setting(config, slot_key(slot, "mode"));
        mapping.mode = normalized_mode(
            mode != nullptr ? parse_int_strict(*mode).value_or(
                                  static_cast<int>(ControlDirectionMode::FourDirection))
                            : static_cast<int>(ControlDirectionMode::FourDirection));

        // Saved under older factory tables (before New Ability Select, or
        // before #391 moved web P2 LOOK UP): move it to the current ones.
        const std::string* version = find_setting(config, slot_key(slot, kDefaultKeysVersionSetting));
        const int from_version =
            version == nullptr ? 0 : parse_int_strict(*version).value_or(0);
        const bool migrate = from_version < kDefaultKeysVersion;
        const int stored_fallback = migrate ? kKeyNotStored : KEYCODE_UNKNOWN;
        std::vector<SeatKeysToMigrate> entry(1);
        entry[0].profile = factory_profile_for_name(mapping.name);

        for (int k = 0; k < NUM_KEYS; ++k)
        {
            const int four =
                parse_key_setting(config, slot, std::format("mode4_key{}", k), stored_fallback);
            const int eight =
                parse_key_setting(config, slot, std::format("mode8_key{}", k), stored_fallback);
            // KEY_PREFS and KEY_SPECIAL_SWITCH are reserved wire slots with
            // nothing behind them. Drop any persisted value the same way the
            // controls loader does, so a stored mapping cannot resurrect the
            // old 1/2/3/4 key thief (the migration still reads the old
            // change-special key as the entry's SPEC 2 key).
            if (k == KEY_PREFS || k == KEY_SPECIAL_SWITCH)
            {
                if (k == KEY_SPECIAL_SWITCH)
                    entry[0].change_special = {four, eight};
                mapping.mode4_keys[key_slot(k)] = KEYCODE_UNKNOWN;
                mapping.mode8_keys[key_slot(k)] = KEYCODE_UNKNOWN;
                continue;
            }
            mapping.mode4_keys[key_slot(k)] = four;
            mapping.mode8_keys[key_slot(k)] = eight;
        }
        if (migrate)
        {
            entry[0].keys = {mapping.mode4_keys, mapping.mode8_keys};
            migrate_seat_keys(entry, web_mode, from_version);
            mapping.mode4_keys = entry[0].keys[kModeFour];
            mapping.mode8_keys = entry[0].keys[kModeEight];
        }
        library.push_back(std::move(mapping));
    }
    return library;
}

MappingDefinition resolve_mapping(cfg_store& config, const std::string& name, bool web_mode)
{
    for (auto& entry : load_mapping_library(config, web_mode))
    {
        if (entry.name == name)
            return entry;
    }
    const int profile = factory_profile_for_name(name);
    return factory_mapping(profile >= 0 ? profile : 0, web_mode);
}

bool save_mapping(cfg_store& config, const MappingDefinition& mapping)
{
    if (mapping.name.empty())
        return false;

    int slot = find_named_slot(config, mapping.name);
    if (slot < 0)
        slot = find_free_slot(config);
    if (slot < 0)
        return false;

    config.apply_setting(kMappingCfgCategory, slot_key(slot, "name"), mapping.name);
    config.apply_setting(kMappingCfgCategory, slot_key(slot, "mode"),
                         std::to_string(normalized_mode(mapping.mode)));
    config.apply_setting(kMappingCfgCategory, slot_key(slot, kDefaultKeysVersionSetting),
                         std::to_string(kDefaultKeysVersion));
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        config.apply_setting(kMappingCfgCategory, slot_key(slot, std::format("mode4_key{}", k)),
                             std::to_string(mapping.mode4_keys[key_slot(k)]));
        config.apply_setting(kMappingCfgCategory, slot_key(slot, std::format("mode8_key{}", k)),
                             std::to_string(mapping.mode8_keys[key_slot(k)]));
    }
    return true;
}

bool erase_mapping(cfg_store& config, const std::string& name)
{
    const int slot = find_named_slot(config, name);
    if (slot < 0)
        return false;

    const auto category = config.data.find(kMappingCfgCategory);
    if (category == config.data.end())
        return false;

    category->second.erase(slot_key(slot, "name"));
    category->second.erase(slot_key(slot, "mode"));
    category->second.erase(slot_key(slot, kDefaultKeysVersionSetting));
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        category->second.erase(slot_key(slot, std::format("mode4_key{}", k)));
        category->second.erase(slot_key(slot, std::format("mode8_key{}", k)));
    }
    return true;
}

bool assign_mapping_to_player(int player_index, const MappingDefinition& mapping)
{
    if (player_index < 0 || player_index >= kProfileCount)
        return false;

    // set_player_key_binding only ever writes the CURRENT mode's map, so each
    // map is filled while its own mode is active; the mapping's own mode is
    // activated last.
    set_player_control_mode(player_index, static_cast<int>(ControlDirectionMode::FourDirection));
    for (int k = 0; k < NUM_KEYS; ++k)
        set_player_key_binding(player_index, k, mapping.mode4_keys[key_slot(k)]);
    set_player_control_mode(player_index, static_cast<int>(ControlDirectionMode::EightDirection));
    for (int k = 0; k < NUM_KEYS; ++k)
        set_player_key_binding(player_index, k, mapping.mode8_keys[key_slot(k)]);
    set_player_control_mode(player_index, mapping.mode);

    // A factory name also moves the seat's RESET identity, so per-seat RESET
    // restores the layout the seat is actually named after. The identities are
    // a permutation the cfg loader validates atomically (one duplicate resets
    // all four), so claim the profile by SWAPPING it with its current holder
    // rather than overwriting.
    const int profile = factory_profile_for_name(mapping.name);
    if (profile >= 0)
    {
        int* profiles = input_hardware_state().player_control_default_profiles;
        const int previous = profiles[player_index];
        if (previous != profile)
        {
            for (int p = 0; p < kProfileCount; ++p)
            {
                if (p != player_index && profiles[p] == profile)
                    profiles[p] = previous;
            }
            profiles[player_index] = profile;
        }
    }
    return true;
}

bool save_player_mapping_to_library(cfg_store& config, int player_index, bool web_mode)
{
    if (player_index < 0 || player_index >= kProfileCount)
        return false;

    // Fold any direct writes to the live keys back into the active mode's map
    // before snapshotting it: get_player_key_binding_for_mode reads the
    // backing store, which a raw player_keys_ write leaves stale. Re-setting
    // the mode a seat already has syncs and re-activates, and nothing else.
    const int mode = get_player_control_mode(player_index);
    set_player_control_mode(player_index, mode);

    MappingDefinition mapping;
    mapping.mode = mode;
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        mapping.mode4_keys[key_slot(k)] = get_player_key_binding_for_mode(
            player_index, static_cast<int>(ControlDirectionMode::FourDirection), k);
        mapping.mode8_keys[key_slot(k)] = get_player_key_binding_for_mode(
            player_index, static_cast<int>(ControlDirectionMode::EightDirection), k);
    }
    mapping.name = derive_mapping_name(
        mode == static_cast<int>(ControlDirectionMode::EightDirection) ? mapping.mode8_keys
                                                                       : mapping.mode4_keys);

    // An untouched factory layout is implicit — writing it would grow the cfg
    // for nothing, and a stale entry left over from an earlier customization
    // would outlive the RESET that undid it.
    const int profile = factory_profile_for_name(mapping.name);
    if (profile >= 0 && mapping == factory_mapping(profile, web_mode))
    {
        erase_mapping(config, mapping.name);
        return false;
    }
    return save_mapping(config, mapping);
}

std::string current_mapping_name(int player_index, bool has_joystick, int joystick_device)
{
    if (has_joystick)
        return joystick_mapping_name(joystick_device);
    if (player_index < 0 || player_index >= kProfileCount)
        return kCustomMappingName;

    KeyMap keys{};
    const int mode = get_player_control_mode(player_index);
    for (int k = 0; k < NUM_KEYS; ++k)
        keys[key_slot(k)] = get_player_key_binding_for_mode(player_index, mode, k);
    return derive_mapping_name(keys);
}

std::vector<std::string> available_mapping_names(cfg_store& config, int for_player,
                                                 int active_player_count)
{
    const int seats = std::clamp(active_player_count, 0, kProfileCount);
    std::vector<std::string> taken;
    for (int p = 0; p < seats; ++p)
    {
        if (p != for_player)
            taken.push_back(current_mapping_name(p));
    }

    std::string mine;
    if (for_player >= 0 && for_player < kProfileCount)
        mine = current_mapping_name(for_player);

    std::vector<std::string> options;
    const auto offer = [&](const std::string& name) {
        if (name.empty() || contains(options, name))
            return;
        if (name != mine && contains(taken, name))
            return;
        options.push_back(name);
    };

    for (int profile = 0; profile < kProfileCount; ++profile)
        offer(kFactoryNames[profile]);
    for (const auto& entry : load_mapping_library(config))
        offer(entry.name);
    // The seat's own name is an option even when it is an unsaved custom, so
    // the cycler always has a current position to start from.
    if (!mine.empty() && !contains(options, mine))
        options.push_back(mine);
    return options;
}

// --- One key at a time (input_mappings.h) ---------------------------------

std::vector<int> unbind_slots_holding(KeyMap& keys, int slot, int keycode)
{
    std::vector<int> unbound;
    if (keycode == KEYCODE_UNKNOWN)
        return unbound;
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        if (k == slot || k == KEY_SPECIAL_SWITCH || k == KEY_PREFS)
            continue;
        int& held = keys[key_slot(k)];
        if (held == keycode)
        {
            held = KEYCODE_UNKNOWN;
            unbound.push_back(k);
        }
    }
    return unbound;
}

void bind_player_key(int player_index, int slot, int keycode)
{
    if (player_index < 0 || player_index >= 4 || slot < 0 || slot >= NUM_KEYS)
        return;
    const int mode = get_player_control_mode(player_index);
    KeyMap keys{};
    for (int k = 0; k < NUM_KEYS; ++k)
        keys[key_slot(k)] = get_player_key_binding_for_mode(player_index, mode, k);
    for (const int loser : unbind_slots_holding(keys, slot, keycode))
        set_player_key_binding(player_index, loser, KEYCODE_UNKNOWN);
    set_player_key_binding(player_index, slot, keycode);
}

// --- One-time migration of saved keys (input_mappings.h) -----------------

namespace
{

constexpr int kModes = 2;

bool bound(int key)
{
    return key != kKeyNotStored && key != KEYCODE_UNKNOWN;
}

bool is_reserved_slot(int k)
{
    return k == KEY_SPECIAL_SWITCH || k == KEY_PREFS;
}

// The factory key a version-1 build (New Ability Select) wrote for this
// slot: today's, except web P2 LOOK UP, which was still Right Ctrl.
int v1_factory_key(int profile, int mode, int k, bool web_mode)
{
    if (web_mode && profile == 1 && k == KEY_LOOKUP)
        return kV1WebP2LookupKey;
    return browser_safe_default_key(profile, mode, k, factory_default_key(profile, mode, k),
                                    web_mode);
}

// Step 1 for one stored key of a factory profile.
bool stored_key_is_factory(int profile, int mode, int k, int stored, bool web_mode,
                           int from_version)
{
    if (stored == kKeyNotStored)
        return true;
    if (from_version >= 1)
        return stored == v1_factory_key(profile, mode, k, web_mode);
    if (k == KEY_SPEC2 || k == KEY_SPEC3)
        return stored == browser_safe_default_key(
            profile, mode, k, factory_default_key(profile, mode, k), web_mode);
    const int legacy = legacy_factory_key(profile, mode, k);
    if (stored == legacy)
        return true;
    return web_mode && profile == 0 && stored == legacy_web_default_key(mode, k, legacy);
}

} // namespace

void migrate_seat_keys(std::vector<SeatKeysToMigrate>& seats, bool web_mode, int from_version)
{
    const std::size_t count = seats.size();
    if (from_version >= kDefaultKeysVersion)
    {
        // Already current: only fill what was never stored. (The loaders
        // never call this for a current cfg; the contract is still total.)
        for (SeatKeysToMigrate& seat : seats)
            for (int m = 0; m < kModes; ++m)
                for (int k = 0; k < NUM_KEYS; ++k)
                {
                    int& key = seat.keys[static_cast<std::size_t>(m)][key_slot(k)];
                    if (key == kKeyNotStored || is_reserved_slot(k))
                        key = KEYCODE_UNKNOWN;
                }
        return;
    }
    // Step 1: classify every slot of every seat before anything moves.
    std::vector<std::array<std::array<bool, NUM_KEYS>, kModes>> factory(count);
    std::vector<std::array<KeyMap, kModes>> result(count);
    for (std::size_t s = 0; s < count; ++s)
    {
        const SeatKeysToMigrate& seat = seats[s];
        for (int m = 0; m < kModes; ++m)
        {
            const KeyMap& stored = seat.keys[static_cast<std::size_t>(m)];
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                const int value = stored[key_slot(k)];
                bool is_factory = false;
                if (is_reserved_slot(k))
                    result[s][static_cast<std::size_t>(m)][key_slot(k)] = KEYCODE_UNKNOWN;
                else if (seat.profile < 0)
                    result[s][static_cast<std::size_t>(m)][key_slot(k)] =
                        value == kKeyNotStored ? KEYCODE_UNKNOWN : value;
                else
                {
                    is_factory = stored_key_is_factory(seat.profile, m, k, value, web_mode,
                                                       from_version);
                    result[s][static_cast<std::size_t>(m)][key_slot(k)] = value;
                }
                factory[s][static_cast<std::size_t>(m)][key_slot(k)] = is_factory;
            }

            // The player's own change-special key becomes their SPEC 2 key.
            // (Version 0 only: from version 1 on, slot 11 is saved unbound.)
            const int change = from_version < 1
                ? seat.change_special[static_cast<std::size_t>(m)]
                : kKeyNotStored;
            const bool spec2_custom =
                seat.profile >= 0 && !factory[s][static_cast<std::size_t>(m)][key_slot(KEY_SPEC2)];
            if (bound(change) && !spec2_custom &&
                (seat.profile < 0 ||
                 change != legacy_factory_key(seat.profile, m, KEY_SPECIAL_SWITCH)))
            {
                factory[s][static_cast<std::size_t>(m)][key_slot(KEY_SPEC2)] = false;
                result[s][static_cast<std::size_t>(m)][key_slot(KEY_SPEC2)] = change;
            }
        }
    }

    // The keys the players chose, per seat and mode (step 3's referee).
    const auto custom_keys = [&](std::size_t s, int m) {
        std::vector<int> keys;
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            const int value = result[s][static_cast<std::size_t>(m)][key_slot(k)];
            if (!factory[s][static_cast<std::size_t>(m)][key_slot(k)] && !is_reserved_slot(k) &&
                bound(value))
                keys.push_back(value);
        }
        return keys;
    };

    for (std::size_t s = 0; s < count; ++s)
    {
        for (int m = 0; m < kModes; ++m)
        {
            std::vector<int> taken = custom_keys(s, m);
            for (std::size_t other = 0; other < count; ++other)
            {
                if (other == s)
                    continue;
                for (int om = 0; om < kModes; ++om)
                {
                    const std::vector<int> theirs = custom_keys(other, om);
                    taken.insert(taken.end(), theirs.begin(), theirs.end());
                }
            }
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                if (!factory[s][static_cast<std::size_t>(m)][key_slot(k)])
                    continue;
                // Step 2: the current factory key ...
                const int fresh = browser_safe_default_key(
                    seats[s].profile, m, k, factory_default_key(seats[s].profile, m, k), web_mode);
                // ... step 3: unless a player chose it. From version 1 on,
                // only a slot whose key actually moves is refereed: a slot
                // already on its factory key keeps it (a v1 cfg was already
                // collision-checked against the same keys).
                const int stored = result[s][static_cast<std::size_t>(m)][key_slot(k)];
                const bool moves = from_version < 1 || stored != fresh;
                const bool collides =
                    moves && std::find(taken.begin(), taken.end(), fresh) != taken.end();
                seats[s].keys[static_cast<std::size_t>(m)][key_slot(k)] =
                    collides ? KEYCODE_UNKNOWN : fresh;
            }
        }
    }
    for (std::size_t s = 0; s < count; ++s)
    {
        for (int m = 0; m < kModes; ++m)
        {
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                if (!factory[s][static_cast<std::size_t>(m)][key_slot(k)])
                    seats[s].keys[static_cast<std::size_t>(m)][key_slot(k)] =
                        result[s][static_cast<std::size_t>(m)][key_slot(k)];
            }
        }
    }
}

} // namespace og::input
