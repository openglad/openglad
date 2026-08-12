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

// "WASD" / "ARROWS" / "IJKL" / "TFGH"; empty for an out-of-range profile.
std::string factory_mapping_name(int profile);
// Inverse: the factory profile index behind a name, or -1 when the name is a
// custom (or joystick) one.
int factory_profile_for_name(const std::string& name);

// --- Names ----------------------------------------------------------------
// keys must hold NUM_KEYS entries; only the four cardinal slots are read.
std::string derive_mapping_name(const int* keys);
std::string derive_mapping_name(const KeyMap& keys);
// Base Camp seat-card form (<= kMappingShortNameMaxLength chars):
// "ARROWS" -> "ARROW", everything else truncated.
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
// until somebody customizes one.
std::vector<MappingDefinition> load_mapping_library(cfg_store& config);
// Library entry when one exists under this name, else the factory layout the
// name denotes. An unknown, non-factory name resolves to the WASD factory
// layout rather than an unplayable seat.
MappingDefinition resolve_mapping(cfg_store& config, const std::string& name,
                                  bool web_mode = og::input::kWebControlDefaults);
// Writes mapping{K}_name/_mode/_mode4_key{0..16}/_mode8_key{0..16}, reusing K
// when an entry of the same name exists. False for an unnamed mapping or a
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

} // namespace og::input
