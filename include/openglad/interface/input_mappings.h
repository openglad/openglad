#pragma once

// Named input mappings (docs/pause-menu-design.md §3).
//
// A control mapping's NAME is a pure function of its movement cluster, not a
// stored label: UP/LEFT/DOWN/RIGHT matching a factory table's cluster (in
// EITHER direction mode) yields that table's name, anything else spells the
// four cardinal keys out, and an unspellable cluster is "CUSTOM". So remapping
// FIRE leaves a seat on WASD, while remapping movement moves the name with the
// keys — no name field to keep in sync.
//
// On top of the naming rule sits a small cfg-persisted LIBRARY (category
// "mappings"): a customized layout is written back under its derived name, so
// cycling away and back — or restarting the game — restores the customization.
// The four factory layouts are implicit and never written unless customized.
//
// SDL-FREE by construction: this module is compiled into openglad_text,
// openglad_server and openglad_curses alongside input_state.cpp, so the
// headless clients resolve the same names from the same config. It never
// reaches for SDL_GetKeyName (which does not exist headlessly) — the spelled
// names come from the keycode values themselves.

#include <openglad/interface/input.h>
#include <openglad/interface/web_control_defaults.h>

#include <array>
#include <limits>
#include <string>
#include <vector>

class cfg_store;

namespace og::input
{

// One direction mode's worth of bindings, indexed by KEY_* .
using KeyMap = std::array<int, NUM_KEYS>;

// The cfg category the mapping library lives in.
inline constexpr const char* kMappingCfgCategory = "mappings";
// Library slots scanned/allocated: mapping0_* .. mapping31_* .
inline constexpr int kMaxLibraryMappings = 32;
// A derived (non-factory) name is at most this long...
inline constexpr int kMappingNameMaxLength = 6;
// ...and its Base Camp seat-card form at most this long.
inline constexpr int kMappingShortNameMaxLength = 5;
// Fallback name for a movement cluster that cannot be spelled out.
inline constexpr const char* kCustomMappingName = "CUSTOM";

// --- Factory tables -------------------------------------------------------
// Thin readers for kDefault{Four,Eight}DirKeys / kDefaultControlModes, which
// stay file-static next to their static_asserts in input_state.cpp. Defined
// there so this module cannot fork a second copy of the factory data.
// mode_index is kWebModeFourIndex / kWebModeEightIndex; out-of-range profiles
// and key slots read back KEYCODE_UNKNOWN / FourDirection.
int factory_default_key(int profile, int mode_index, int key_enum);
int factory_default_mode(int profile);
// The factory tables as they shipped before New Ability Select (slots 0..16;
// KEYCODE_UNKNOWN past them and for out-of-range indices). Only the one-time
// migration below reads them.
int legacy_factory_key(int profile, int mode_index, int key_enum);

// "WASD" / "ARROWS" / "IJKL" / "TFGH"; empty for an out-of-range profile.
std::string factory_mapping_name(int profile);
// Inverse: the factory profile index behind a name, or -1 when the name is a
// custom (or joystick) one.
int factory_profile_for_name(const std::string& name);

// --- Names ----------------------------------------------------------------
// keys must hold NUM_KEYS entries; only the four cardinal slots are read.
std::string derive_mapping_name(const int* keys);
std::string derive_mapping_name(const KeyMap& keys);
// The bitmap font's arrow glyphs in up/left/down/right order — the SDL
// display form of the canonical "ARROWS" name (glyph bytes never enter cfg
// or the library; they are meaningless outside the bitmap font).
inline constexpr const char* kArrowGlyphs = "\x01\x03\x02\x04";
// SDL display form of a canonical mapping name: "ARROWS" renders as the four
// arrow glyphs, every other name renders as itself.
std::string mapping_display_name(const std::string& name);
// Base Camp seat-card form (<= kMappingShortNameMaxLength chars):
// "ARROWS" -> arrow glyphs, "CUSTOM" -> "CUST", everything else truncated.
std::string mapping_short_name(const std::string& name);
// 1-based device slot: device 0 -> "JOY1". A negative device yields "JOY".
std::string joystick_mapping_name(int device_index);

// --- Library --------------------------------------------------------------
struct MappingDefinition
{
    std::string name;
    int mode = static_cast<int>(ControlDirectionMode::FourDirection);
    KeyMap mode4_keys{};
    KeyMap mode8_keys{};

    friend bool operator==(const MappingDefinition&, const MappingDefinition&) = default;
};

// The factory layout for a profile, through the browser-safe substitution the
// factory reset applies (web_mode rewrites profile 0 = WASD only).
MappingDefinition factory_mapping(int profile, bool web_mode = og::input::kWebControlDefaults);

// Saved entries only, in slot order. Factory layouts are implicit and absent
// until somebody customizes one. An entry saved before New Ability Select (no
// mapping{K}_default_keys_version) is moved to the current tables on load by
// migrate_seat_keys, as a seat of its own: a factory-named entry by that
// profile, a CUSTOM one keeping every key (SPEC 2 from its change-special
// key, SPEC 3 unbound).
std::vector<MappingDefinition> load_mapping_library(
    cfg_store& config, bool web_mode = og::input::kWebControlDefaults);
// Library entry when one exists under this name, else the factory layout the
// name denotes. An unknown, non-factory name resolves to the WASD factory
// layout rather than an unplayable seat.
MappingDefinition resolve_mapping(cfg_store& config, const std::string& name,
                                  bool web_mode = og::input::kWebControlDefaults);
// Writes mapping{K}_name/_mode/_default_keys_version/_mode4_key{0..18}/
// _mode8_key{0..18}, reusing K when an entry of the same name exists. False for an unnamed mapping or a
// full library.
bool save_mapping(cfg_store& config, const MappingDefinition& mapping);
// Drops a named entry's keys from the config; false when no such entry.
bool erase_mapping(cfg_store& config, const std::string& name);

// --- Seats ----------------------------------------------------------------
// Loads a mapping into a seat: both mode maps, then the mapping's own mode.
// A factory name also claims that factory profile as the seat's RESET
// identity, swapping it with whichever seat held it so the persisted
// default_profile block stays the permutation its loader validates.
bool assign_mapping_to_player(int player_index, const MappingDefinition& mapping);
// Snapshots a seat's two mode maps under its derived name. A snapshot still
// identical to the factory layout is not written (and drops any stale entry of
// that name), keeping the library to genuine customizations. Returns whether
// an entry was written.
bool save_player_mapping_to_library(cfg_store& config, int player_index,
                                    bool web_mode = og::input::kWebControlDefaults);
// The seat's display name; a joystick-driven seat is named after its device.
std::string current_mapping_name(int player_index, bool has_joystick = false,
                                 int joystick_device = -1);
// INPUT cycler options for a seat: the four factory names, then saved customs,
// minus any name another active seat currently answers to (two seats can only
// share a mapping through deliberate remapping, never through the cycler). The
// seat's own current name is always present, even when unsaved. Joystick
// entries are appended by the UI layer, which owns device enumeration.
std::vector<std::string> available_mapping_names(cfg_store& config, int for_player,
                                                 int active_player_count);

// --- One-time migration of saved keys (New Ability Select) -----------------
// The factory tables changed (input_state.cpp: kDefault*DirKeys, with the
// pre-change kLegacy*DirKeys kept beside them), and every returning player
// has the old factory keys frozen in cfg. This is the ONE rule that moves
// them, for the controls block (load_player_control_settings_from_cfg, all
// four seats at once) and for each mapping-library entry (a seat of its own).
//
// Per seat and direction mode, against the WHOLE old table (the old and new
// tables hand keys around inside a seat, so slot-by-slot would refuse a slot
// because a later slot still holds the key it wants):
//   1. Classify. A slot is FACTORY when its stored value is any old factory
//      value for (profile, mode, slot): the native legacy table, and for web
//      profile 0 also the browser scheme's value; or when it was never
//      stored. Otherwise it is CUSTOM. SPEC 2/3 (slots 17/18) are factory
//      unless they hold a stored key other than today's factory key. A bound
//      change-special key (slot 11) other than the old factory one is the
//      player's own SPEC 2 key: slot 17 becomes custom with that value.
//   2. Assign, all together: every factory slot takes the current factory
//      key (through browser_safe_default_key); every custom slot keeps its
//      value.
//   3. Collide. A factory slot whose new key is a custom key of this seat in
//      this mode, or of any other seat in either mode, goes UNBOUND: the
//      player's choice beats the factory's (the binding panel shows "--" and
//      REMAP binds it). Custom-against-custom collisions are left as found.
// Slots 11 and 14 come out unbound. A seat with profile -1 (a CUSTOM library
// entry) has no factory slots: every stored key is kept, a never-stored
// slot is unbound, slot 17 takes the change-special key when bound.
//
// The rules above are version 0 (keys saved before New Ability Select). The
// keys a cfg was stamped with decide which old factory table step 1 reads:
//   from_version 0: the d758bcb1 tables (plus the browser scheme), as above.
//   from_version 1: the New Ability Select tables. Those are today's tables
//      except web P2 LOOK UP, which was Right Ctrl and is Delete since #391
//      (kV1WebP2LookupKey, web_control_defaults.h). A slot is factory when
//      never stored or stored == its version-1 value; slot 11 is ignored
//      (a v1 build saves it unbound). Step 3 referees only a factory slot whose key
//      actually MOVES, so a native v1 -> v2 run changes nothing (a v1 cfg
//      was already checked against the same keys), and on web only P2's
//      LOOK UP can yield: to Delete, or unbound when Delete is some seat's
//      own key.
//   from_version >= kDefaultKeysVersion: nothing moves; never-stored and
//      reserved slots come out unbound.
// Version history: 1 = New Ability Select (#380); 2 = web P2 LOOK UP on
// Delete (#391).
inline constexpr int kDefaultKeysVersion = 2;
// cfg controls/<this>; also mappings/mapping{K}_default_keys_version per entry.
inline constexpr const char* kDefaultKeysVersionSetting = "default_keys_version";
// A slot the saved keys never mention (or hold something that is not a key).
inline constexpr int kKeyNotStored = std::numeric_limits<int>::min();

struct SeatKeysToMigrate
{
    // Factory profile 0..3, or -1 when nothing in the seat is factory.
    int profile = 0;
    // Stored keys per mode index (kWebModeFourIndex / kWebModeEightIndex);
    // kKeyNotStored where nothing was stored. Slots 11 and 14 are ignored.
    std::array<KeyMap, 2> keys = [] {
        std::array<KeyMap, 2> unset{};
        for (KeyMap& map : unset)
            map.fill(kKeyNotStored);
        return unset;
    }();
    // The stored change-special key (slot 11) per mode index.
    std::array<int, 2> change_special{kKeyNotStored, kKeyNotStored};
};

// Rewrites every seat's `keys` in place to the migrated maps (no
// kKeyNotStored left). Pure: reads only the factory tables. `from_version`
// is the default-keys version the keys were saved under (0 when unstamped).
void migrate_seat_keys(std::vector<SeatKeysToMigrate>& seats, bool web_mode,
                       int from_version);

} // namespace og::input
