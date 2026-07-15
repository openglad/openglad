/* Shared picker business logic.
 *
 * Cost calculations, name generation, team management, and constants
 * used by both the SDL and text picker clients.
 */
#pragma once

#include <openglad/core/constants.h>
#include <openglad/resources/save_data.h>
#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

class guy;
class GameWorld;

namespace og::ui {

// --- Constants ---

inline constexpr float kStatCostExponent = 1.85f;

inline constexpr std::array<int, 14> kAllowableGuys = {
    FAMILY_SOLDIER,
    FAMILY_BARBARIAN,
    FAMILY_ELF,
    FAMILY_ARCHER,
    FAMILY_MAGE,
    FAMILY_CLERIC,
    FAMILY_THIEF,
    FAMILY_DRUID,
    FAMILY_ORC,
    FAMILY_SKELETON,
    FAMILY_FIREELEMENTAL,
    FAMILY_SMALL_SLIME,
    FAMILY_FAERIE,
    FAMILY_GHOST,
};

inline constexpr int kNewGameStartingGold = 5000;

extern const char* const kDifficultyNames[DIFFICULTY_SETTINGS];

// --- Family display helpers ---

// Full display name from FamilyDescriptor (e.g. "SOLDIER", "ORC CAPTAIN").
// Returns "BEAST" for unknown families.
const char* family_display_name(int family);

// Short label for picker UI (e.g. "SOLDIER", "BARBAR.", "ORC CAP.").
// Returns "BEAST" for unlisted families.
const char* family_short_name(short family);

// --- Cost calculations ---

// Base hiring cost for a family (from FamilyDescriptor::hiring_cost).
int family_hiring_base_cost(int family);

// Total hire cost including stat upgrades above base and level XP.
// Pure function: does not mutate the guy.
std::uint32_t calculate_hire_cost(const guy& recruit);

// Training cost: difference between current stats and original stats.
// Pure function: does not mutate either guy.
std::uint32_t calculate_train_cost(const guy& current, const guy& original);

// --- Name generation ---

// Return a random name appropriate for the given family.
const char* get_random_name(unsigned char family);

// Return a unique name not already in the save's team list.
std::string get_unique_name(unsigned char family, const SaveData& save);

// --- Team queries ---

// Count how many team members of the given family exist in save.
int count_family_members(int family, const SaveData& save);

// --- Team operations ---

// Add a recruit to save's team list. Returns slot index or -1 if full.
int add_recruit_to_team(SaveData& save, std::unique_ptr<guy> recruit, int team_num);

// Create a fresh recruit of the given family with a unique name.
std::unique_ptr<guy> create_recruit(int family, int team_num, const SaveData& save);

// Reset save data for a new game: clear team, reset gold.
void reset_for_new_game(SaveData& save);

// Ensure team has at least one member.  Creates recruits from the
// families list, falling back to FAMILY_SOLDIER if the list is empty.
void ensure_team_populated(SaveData& save, const std::vector<int>& families = {}, int team_num = 0);

// --- Derived stats ---

struct DerivedStats {
    float hp, mp, atk, def, spd, atk_spd;
};

// Compute derived stats from a guy's bonuses and base loader values.
// base_hp/damage/stepsize/fire_freq come from the loader arrays.
DerivedStats compute_derived_stats(const guy& g,
    float base_hp, float base_damage, float base_stepsize, float base_fire_freq);

// --- Difficulty ---

// Cycle to the next difficulty setting. Returns (current + 1) % DIFFICULTY_SETTINGS.
int cycle_difficulty(int current);
int difficulty_percent(int difficulty);

// --- Allied mode ---

void toggle_allied_mode(SaveData& save);
bool is_allied_mode(const SaveData& save);

// --- CTF match settings ---

// Cycle the requested CTF team count: 2 -> 3 -> 4 -> 2.
void cycle_ctf_team_count(SaveData& save);

// Cycle the capture limit: 0 (map default) -> 1 -> 3 -> 5 -> 10 -> 0.
void cycle_ctf_capture_limit(SaveData& save);

// True when the save's current campaign is the CTF campaign.
bool is_ctf_campaign(const SaveData& save);

// Toggle the CTF scenario-troops strip flag (0 = keep authored troops).
void toggle_ctf_scenario_troops(SaveData& save);

// --- Difficulty submenu match rules ---
// Every default (0) is bit-identical classic behavior; the cyclers walk the
// small closed value sets and normalize any out-of-set stored value back to
// the default on the next step.

// Cycle respawn mode: 0 (off) -> 1 (heroes) -> 2 (everyone) -> 0.
void cycle_respawn_mode(SaveData& save);

// Cycle the respawn delay ticks: 0 (normal/map default) -> 60 (fast)
// -> 360 (slow) -> 0. Rides the existing ctf_respawn_ticks field.
void cycle_respawn_delay(SaveData& save);

// Toggle permadeath: keep_fallen_heroes 0 (permadeath ON, classic) <-> 1.
void toggle_permadeath(SaveData& save);

// Cycle the generator rate percent: 0 (normal/100) -> 50 (calm)
// -> 200 (frenzy) -> 0.
void cycle_generator_rate(SaveData& save);

// --- Team choice helpers (local seats) ---

// True when any roster slot is on the given team.
bool team_has_members(const SaveData& save, short team);

// Set my_team (the P1 seat) to the given team. Rejects out-of-range teams
// and teams with no roster members; returns true on success.
bool set_preferred_team(SaveData& save, short team);

// Cycle a roster slot's teamnum by dir, wrapping over 0..3 (the same
// (t % 4 + 4) % 4 rule as the train menu). Returns the new team, or -1 when
// the slot is empty/out of range.
short cycle_guy_team(SaveData& save, int slot_index, int dir);

// The local seat order gameplay derives (game.cpp view_teams): distinct
// NONZERO roster teams in slot order, with my_team hoisted to the front when
// it has members. Single source of truth for seat labels (P1..P4) and the
// local lobby's synthetic peer teams.
std::vector<short> derive_local_seat_teams(const SaveData& save);

// One TEAMS-screen row label, <= 30 chars: "{COLOR} TEAM {seat_tag} {status}"
// where status is "NOT ON MAP" (CTF, no authored flag), "BOTS" (CTF authored
// team with no humans and no local heroes), or "{n} HEROES".
std::string format_team_row_label(short team,
                                  int hero_count,
                                  bool is_ctf,
                                  bool authored,
                                  bool has_humans,
                                  std::string_view seat_tag);

// Greedy ", "-joined pagination of a team's member/player names into slices
// of at most max_chars characters each. An item longer than max_chars is
// clipped inside the budget with a trailing '..' marker (so truncation stays
// visible even when everything fits one unpaged slice; budgets of <= 2 chars
// just clip). Always returns at least one (possibly empty) page, so page
// math never divides by zero. Drives the TEAMS screen's per-team pager.
std::vector<std::string> paginate_team_detail_pages(
    const std::vector<std::string>& items, int max_chars);

// --- Campaign ordering ---

// Display order for campaign selects: the default campaign (gladiator)
// leads and the CTF campaign trails, so extra campaigns sit in between in
// their existing (alphabetical) order. Applied at the user-facing campaign
// pickers only; list_campaigns() itself stays honest.
void order_campaigns_for_select(std::list<std::string>& campaign_ids);

// Networked lobbies must not offer local-only mode campaigns (v1: the
// Endless Tower — its run state lives in one player's save0 and its floors
// in one player's user dir). Removes those ids from a campaign-select list
// when networked_session is true; a no-op for local shelves. All three
// picker clients call this in lockstep at their shelf call sites; the
// prepare_launch veto and LobbyServer::sanitize_settings back it up.
void filter_campaigns_for_networked_lobby(std::list<std::string>& campaign_ids,
                                          bool networked_session);

// Index-aligned display labels for a campaign-select list: the packages'
// human titles, with the raw id appended ("Forest [org.x.forest]") when two
// packages share a title so they stay distinguishable.
std::vector<std::string> format_campaign_select_labels(
    const std::vector<std::string>& campaign_ids);

// Re-point the mounted campaign package at the save's campaign. The mount
// only follows SaveData::load() and the SET CAMPAIGN picker, so flows that
// change save.current_campaign behind its back — most importantly the
// new-game reset — call this to keep level data coherent. Returns false and
// restores the previous mount when the package is missing (e.g. a networked
// joiner without the host's campaign).
bool sync_campaign_mount_to_save(const SaveData& save);

// --- Scenario roster report (View Level) ---

enum class ScenarioStripReason : std::uint8_t {
    None = 0,
    TroopsOff,     // removed by the scenario-troops strip ('*')
    InactiveTeam,  // removed by the CTF inactive-team strip ('+')
};

struct ScenarioRosterRow {
    short team = 0;
    bool is_generator = false;
    bool named = false;
    std::string name;  // named NPCs only
    short family = 0;
    int level = 1;
    int count = 1;
    ScenarioStripReason strip_reason = ScenarioStripReason::None;
};

struct ScenarioRosterReport {
    bool is_ctf = false;            // world.type & TYPE_CTF
    bool ctf_will_activate = false; // >= 2 authored flag teams
    short your_team = 0;            // 0 when allied, else save.my_team
    int cp_count = 0;
    int capture_limit = 0;          // effective: requested > map > default
    std::array<bool, 4> team_has_flag = {};
    std::array<bool, 4> team_active = {}; // mirror of the init clamp
    std::array<int, 4> team_anchor_count = {};
    std::vector<ScenarioRosterRow> rows; // grouped, team-major
    bool any_troops_off = false;
    bool any_inactive = false;
};

// Scan a (scratch-loaded) world's authored entities into a roster report.
// Named NPCs get individual rows; unnamed livings group by (team, family,
// level); generators aggregate per team. Strip annotations mirror the CTF
// init rules using save-side knowledge (roster teams = distinct team_list
// teamnums, collapsed to {0} when allied).
ScenarioRosterReport build_scenario_roster_report(const GameWorld& world,
                                                  const SaveData& save);

// Render the report as display lines, every line <= 48 chars, with '*'/'+'
// strip suffixes and trailing legend lines.
std::vector<std::string> format_scenario_report_lines(
    const ScenarioRosterReport& report);

// --- Player count ---

void set_player_count(SaveData& save, int count);

// Returns true when numplayers == 0 (spectator / autoplay mode).
bool is_spectator_mode(const SaveData& save);

// --- Label formatting ---

// Format the difficulty button label (e.g. "Difficulty: Battle").
std::string format_difficulty_label(int difficulty);

// Format the allied mode button label ("PVP: Ally" or "PVP: Enemy").
std::string format_allied_mode_label(const SaveData& save);

// Format the CTF team count label ("CTF Teams: N").
std::string format_ctf_teams_label(const SaveData& save);

// Format the capture limit label ("Capture Limit: Map default" or ": N").
std::string format_ctf_caps_label(const SaveData& save);

// Format the scenario-troops label ("Troops: Scen" when keeping authored
// troops, "Troops: Own" when stripping them).
std::string format_ctf_troops_label(const SaveData& save);

// "Respawns: Off" / "Respawns: Heroes" / "Respawns: Everyone".
std::string format_respawn_mode_label(const SaveData& save);

// "Spawn Delay: Normal" / "Spawn Delay: Fast" / "Spawn Delay: Slow".
std::string format_respawn_delay_label(const SaveData& save);

// "Permadeath: On" / "Permadeath: Off" (On == keep_fallen_heroes == 0).
std::string format_permadeath_label(const SaveData& save);

// "Generators: Normal" / "Generators: Calm" / "Generators: Frenzy".
std::string format_generator_rate_label(const SaveData& save);

// --- GRAPHICS FX depth selector (cfg effects/depth_fx) ---
// Pure string helpers over the depth-effect selector values
// {fog, haze, mist, tint, off}. Any value outside the set — including the
// empty string an absent cfg key reads as — normalizes to the default,
// "fog", matching depth_fx_mode_from_setting in the renderer.

// Step to the next selector value: fog -> haze -> mist -> tint -> off -> fog.
std::string cycle_depth_fx(const std::string& current);

// "Depth: Fog" / "Depth: Haze" / "Depth: Mist" / "Depth: Tint" /
// "Depth: Off" — every label fits the 90px FX button face (15 chars).
std::string format_depth_fx_label(const std::string& value);

// True for every value but "off" (the button's green/red backing state).
bool depth_fx_is_active(const std::string& value);

// --- DISPLAY zoom and smoothing selectors ---
// Zoom selector (cfg graphics/zoom, 1.0 window-sized toward 0.1) and the
// world-canvas-only smoothing selector (cfg graphics/smoothing,
// off/sai/eagle). Cyclers step one value per click; formatters name the
// quantized value the renderer will actually apply.
std::string cycle_zoom(const std::string& current, int minimum_steps = 1);
std::string format_zoom_label(const std::string& value);
// Resolve the pre-zoom graphics/render SAI/Eagle value only when the new
// graphics/smoothing key is absent. Explicit smoothing always wins.
std::string effective_smoothing_setting(const std::string& value,
                                        const std::string& legacy_render);
std::string cycle_smoothing(const std::string& current);
std::string format_smoothing_label(const std::string& value,
                                   bool supported = true);

// DISPLAY settings (cfg graphics/fullscreen + graphics/width/height).
//
// Display mode: "off" (windowed), "borderless" (desktop fullscreen) and
// "exclusive" (a real fullscreen video mode). The legacy boolean "on" reads
// as borderless so old configs keep their meaning.
enum class DisplayMode { Windowed, Borderless, Exclusive };
DisplayMode parse_display_mode(const std::string& value);
std::string display_mode_cfg_value(DisplayMode mode);
DisplayMode next_display_mode(DisplayMode mode);
std::string format_display_mode_label(const std::string& value);

// Resolution: cfg width/height is a physical-pixel mode in Exclusive and an
// SDL logical size in Windowed. The platform supplies coordinates in the
// matching unit. When it cannot enumerate useful Windowed bounds,
// fallback_resolutions() derives sizes from the usable logical desktop: the
// desktop itself plus aspect-preserving fractions. Pass {0,0} when even the
// desktop is unknown to get the classic 16:10 presets.
// next_resolution steps the list; a cfg pair not on the list (hand edited)
// re-enters at the first entry. Absent keys mean the 640x400 boot default.
std::vector<std::pair<int, int>> fallback_resolutions(std::pair<int, int> desktop);
// Build the resolution selector lap for the active display mode. Windowed
// and Borderless may use logical desktop-derived window sizes. Exclusive
// contains only enumerated physical modes: neither the desktop nor the cfg
// size is added unless the platform enumerated it in display_modes.
std::vector<std::pair<int, int>> build_resolution_choices(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop,
    std::pair<int, int> current,
    DisplayMode mode);
// Choose the real mode requested by a Borderless -> Exclusive transition.
// Prefer the physical desktop when it was enumerated; otherwise use the
// largest enumerated mode. Returns {0,0} when no real modes are available.
std::pair<int, int> preferred_exclusive_resolution(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop);
std::pair<int, int> parse_resolution(const std::string& width, const std::string& height);
std::pair<int, int> next_resolution(const std::vector<std::pair<int, int>>& list,
                                    const std::string& width, const std::string& height);
std::string format_resolution_label(const std::string& width, const std::string& height);

// --- Team family extraction ---

// Collect family IDs from non-null team slots into a vector.
std::vector<int> collect_team_families(const SaveData& save);

// --- Team initialization ---

// If the team is empty, set starting gold and populate with recruits.
// No-op if team already has members.
void initialize_starting_team(SaveData& save, const std::vector<int>& families = {}, int team_num = 0);

// --- Save/Load error strings ---

// Human-readable string for SaveDataIoError values.
const char* save_error_string(SaveDataIoError error);

// --- Team iteration ---

// Iterate over non-null team members, calling fn(slot_index, member_ref).
template<typename Fn>
void for_each_team_member(const SaveData& save, Fn&& fn);

// --- Stats copy utility ---

// Copy all stats, name, and metadata between guy objects.
void statscopy(guy* dest, const guy* source);

// --- HireSession: cycle-through-families hiring flow ---

class HireSession {
public:
    HireSession(SaveData& save, int team_num);

    // Navigation — wrapping cycle through kAllowableGuys
    void next_family();
    void prev_family();

    // Hire the current recruit: deducts gold, places in team, auto-creates
    // next recruit with same family. Returns slot index or -1 on failure.
    int hire();

    // Rename the recruit that was just placed in the given slot.
    void rename_hired(int slot, const std::string& name);

    // State queries (for rendering)
    const guy* current_recruit() const;
    std::uint32_t current_cost() const;
    int family_index() const;
    int team_num() const;
    bool team_full() const;

private:
    SaveData& save_;
    int team_num_;
    int current_type_ = 0;
    std::unique_ptr<guy> recruit_;

    void make_recruit();
};

// --- TrainSession: constrained stat-editing flow ---

class TrainSession {
public:
    explicit TrainSession(SaveData& save);

    bool empty() const;

    // Navigation — cycle through non-null team slots
    void next_member();
    void prev_member();

    // Stat editing with constraint rules:
    //  - Can't modify stats if level increased above original
    //  - Can't modify level if any stat increased above original
    //  - Stats clamped to not go below original
    //  - Level decrease clamped to original level
    // Both editors (and accept()) first self-heal via resync_if_promoted()
    // so an external promotion is never clamped/copied against stale
    // cross-family stats (issue #133).
    enum class Stat { Strength, Dexterity, Constitution, Intelligence, Armor, Level };
    void increase_stat(Stat stat, int amount = 1);
    void decrease_stat(Stat stat, int amount = 1);
    void set_team(int team_num);

    // Accept: validates cost, deducts gold, copies working copy -> real team member.
    // If level changed, calls upgrade_to_level(). Returns false if can't afford.
    // force=true skips the cost check (for cheat mode).
    bool accept(bool force = false);

    // Re-snapshot the working copy when the real team member was promoted
    // (family changed) underneath this session — e.g. by the DETAILS
    // submenu's promote button, which mutates the real guy in place.
    // Training never edits family, so a family mismatch always means an
    // external promotion; without the resync the stale working copy hides
    // the promotion on screen and accept() statscopy()s the old family
    // back over it. increase_stat/decrease_stat/accept() also call this
    // internally as a self-heal. Returns true if the working copy was
    // reloaded.
    bool resync_if_promoted();

    // State queries (for rendering)
    const guy& working_copy() const;
    const guy& original() const;
    std::uint32_t current_cost() const;
    bool level_increased() const;
    bool stats_increased() const;
    int current_slot() const;

private:
    SaveData& save_;
    int edit_slot_ = 0;
    std::unique_ptr<guy> working_;

    [[nodiscard]] guy* original_member();
    [[nodiscard]] const guy* original_member() const;

    void select_current_slot();
    void clamp_working_stats();
};

// --- Template implementations ---

// Requires full SaveData definition (include <openglad/resources/save_data.h>)
// at the point of instantiation.
template<typename Fn>
void for_each_team_member(const SaveData& save, Fn&& fn)
{
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[i])
            fn(i, *save.team_list[i]);
    }
}

} // namespace og::ui
