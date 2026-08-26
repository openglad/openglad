/* Shared picker business logic.
 *
 * Cost calculations, name generation, team management, and constants
 * used by both the SDL and text picker clients.
 */
#pragma once

#include <openglad/core/constants.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/resources/company.h>
#include <openglad/resources/save_data.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

class guy;
class GameWorld;
class IRandom;

namespace og::ui {

// --- Constants ---

// please also change in guy.cpp
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

// --- Campaign browser (SET CAMPAIGN) geometry ---
//
// The campaign browser is a fixed 320x200 screen whose controls used to be
// laid out with inline literals, so nothing could pin the one property that
// actually matters: the centered campaign title must not run under a button.
// It did — the title row (y = icon_y - 22 = 13, 8px tall) crosses the top
// control row (y = 10..20), and titles of 17 characters or more reached past
// x = 208 into the old ENTER ID face. "MULTIPLAYER GAME MODES" (22) and
// "CONCEPT PLAYGROUND" (18) both clipped. ENTER ID now sits under
// RESET/DELETE, and the title is fitted to the width that stays clear of the
// one control still on the title row.
struct PickerRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// True when the two rects share at least one pixel. Edge-touching rects
// (a.x + a.w == b.x) do not overlap.
bool picker_rects_overlap(const PickerRect& a, const PickerRect& b);

struct CampaignPickerLayout
{
    PickerRect prev;          // list page up
    PickerRect next;          // list page down
    PickerRect choose;   // OK
    PickerRect cancel;
    PickerRect delete_button;
    PickerRect reset_button;  // occupies the DELETE cell; the two never co-exist
    PickerRect id_button;     // ENTER ID, stacked under DELETE/RESET
    PickerRect icon;          // campaign icon (inside the detail pane)
    int title_y = 0;          // top pixel row of the centered title
    int title_center_x = 0;
    int title_max_chars = 0;  // widest title that clears every control

    // Left list pane (issue #186): one row per campaign, scrolled in pages
    // of list_rows by PREV/NEXT.
    PickerRect list;             // the block of list rows
    int list_rows = 0;           // visible rows per page
    int list_row_h = 0;
    int list_row_pitch = 0;
    int list_label_max_chars = 0;  // row title budget after the marker column
    int header_y = 0;              // "CAMPAIGNS" pane header row

    // Right detail pane for the highlighted entry.
    PickerRect detail;
    PickerRect desc_box;
    PickerRect more_button;   // opens the full description; shown on overflow
    int desc_rows = 0;        // description rows the box can show
    int desc_max_chars = 0;   // character budget of one description row
};

// The single source of the browser's control geometry. Pure, so the overlap
// contract is unit-testable without SDL.
CampaignPickerLayout campaign_picker_layout();

// Pixel rect of one visible list row (0-based, < list_rows).
PickerRect campaign_picker_row_rect(int row);

// Pixel rect a centered title of `chars` glyphs occupies (6px advance, 8px
// glyph height), given the layout above.
PickerRect campaign_title_rect(int chars);

// Text cut to a glyph budget: unchanged when it fits, otherwise clipped with
// a trailing "..." (budgets of <= 3 just clip).
std::string fit_text_to_chars(std::string_view text, int budget);

// Title as drawn in the detail pane: fit_text_to_chars at title_max_chars.
std::string fit_campaign_title(std::string_view title);

// A campaign's list-row label: the cached display title fitted to the row
// budget (falls back to the raw id inside campaign_display_title).
std::string fit_campaign_row_label(std::string_view title);

// --- List paging (pure; the SDL loop stores offset + cursor) ---

// First visible row, clamped so a full page shows whenever one exists.
int campaign_list_clamp_offset(int offset, int total, int rows);

// Minimal scroll of `offset` that brings `cursor` on screen.
int campaign_list_offset_for_cursor(int cursor, int offset, int total, int rows);

// Offset after one PREV (direction < 0) or NEXT (direction > 0) page step.
int campaign_list_page_step(int offset, int total, int rows, int direction);

// Cursor pulled into the visible window [offset, offset + rows).
int campaign_list_clamp_cursor(int cursor, int offset, int total, int rows);

// "3 of 8" position readout ("0 of 0" for an empty shelf).
std::string format_campaign_position_label(int cursor, int total);

// True when the wrapped description does not fit desc_rows rows of the
// detail pane's box — exactly when the MORE control shows.
bool campaign_description_overflows(const std::string& description);

// Positional indices into the browser's button table. Growth is append-only
// for the same reason every other picker screen's is.
inline constexpr int kCampaignPickerPrevIndex = 0;
inline constexpr int kCampaignPickerNextIndex = 1;
inline constexpr int kCampaignPickerChooseIndex = 2;
inline constexpr int kCampaignPickerCancelIndex = 3;
inline constexpr int kCampaignPickerDeleteIndex = 4;
inline constexpr int kCampaignPickerIdIndex = 5;
inline constexpr int kCampaignPickerResetIndex = 6;
inline constexpr int kCampaignPickerRowBaseIndex = 7;
inline constexpr int kCampaignPickerRowCount = 6;
inline constexpr int kCampaignPickerMoreIndex = 13;
inline constexpr int kCampaignPickerButtonCount = 14;

// Which of the browser's conditional buttons are hidden this frame. CANCEL
// and ENTER ID are always visible; DELETE and RESET share a cell, so RESET
// shows exactly when DELETE hides. visible_rows counts the list rows that
// have a campaign behind them this page; MORE shows only when the
// highlighted entry's description overflows the detail box.
struct CampaignPickerVisibility
{
    bool prev_hidden = false;    // on the first page
    bool next_hidden = false;    // on the last page
    bool choose_hidden = false;  // no selectable entry
    bool delete_hidden = false;  // browser opened without delete enabled
    int visible_rows = kCampaignPickerRowCount;
    bool more_hidden = true;
};

struct CampaignPickerNavLinks
{
    int up = -1;
    int down = -1;
    int left = -1;
    int right = -1;
};

// The browser's whole keyboard graph for one visibility state (skill pattern
// (b): recompute, don't patch). handle_menu_nav ignores a link into a hidden
// button rather than following it, so the property that matters — and that
// the BFS pin asserts over every variant — is that each VISIBLE button stays
// reachable from the default highlight (CANCEL).
std::array<CampaignPickerNavLinks, kCampaignPickerButtonCount>
campaign_picker_nav(const CampaignPickerVisibility& visibility);

// Commit a campaign chosen in a browser to the save: load (mount) it and, on
// success, write current_campaign and the resolved level cursor. On a failed
// load the save is left untouched and the previous campaign is remounted, so
// a bad ENTER ID can never write a negative scen_num into the save (issue
// #186). Callers surface the failure (popup) themselves.
bool apply_campaign_selection(SaveData& save, const std::string& campaign_id,
                              int first_level);

// --- Level browser (SET LEVEL) geometry ---
//
// Three radar-preview rows on the left, the selected level's description on
// the right. The legacy inline literals put row 2's preview frame against
// the screen bottom and drew the description box OVER rows 0-1's stats
// column; every rect below is derived from this one grid instead.
struct LevelPickerLayout
{
    PickerRect prev;
    PickerRect next;
    PickerRect choose;        // OK
    PickerRect cancel;
    PickerRect delete_button;
    PickerRect id_button;     // ENTER ID
    PickerRect desc_box;

    int row_x = 0;            // shared left edge of the preview rows
    int row0_y = 0;           // title row of preview row 0
    int row_pitch = 0;
    int row_count = 0;
    int radar_dy = 0;         // radar top offset below a row's title line
    int radar_max_w = 0;      // radar viewport clamps (RADAR_X/RADAR_Y)
    int radar_max_h = 0;
    int stats_x = 0;          // shared left edge of every stats column
    int stats_max_chars = 0;  // budget of one stats line before desc_box
    int status_x = 0;         // CLEARED/CURRENT column on the title line
    int title_max_chars = 0;  // row title budget before the status column
    int army_x = 0;           // "Army power" readout
    int army_y = 0;
    int desc_max_chars = 0;
};

LevelPickerLayout level_picker_layout();

// Title row y of preview row `row`.
int level_picker_row_y(int row);

// The status column text, derived exactly as the PROGRESS report derives its
// Status field: CLEARED wins over CURRENT; otherwise empty.
const char* level_row_status_label(bool cleared, bool current);

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

// Cash returned for selling a character. This uses the same 75% salvage
// basis as the life gem dropped on character death.
std::uint32_t calculate_sell_value(const guy& member);

// --- Name generation ---

// Return a random name appropriate for the given family.
const char* get_random_name(unsigned char family);

// Return a unique name not already in the save's team list.
std::string get_unique_name(unsigned char family, const SaveData& save);

// --- Company name generation (design §2.2) ---

// Hard cap on a generated company display name. 18 chars fits every later
// surface without truncation: the Load-list name column, the main-menu
// company strip, and the base-camp header all budget exactly 18.
inline constexpr std::size_t kCompanyNameMaxLen = 18;

// The three word banks behind generate_company_name, exposed so the unit
// pins can assert the budget contract structurally: every word is uppercase
// A-Z only, and max(adjective) + max(noun) + max(group) + 2 spaces
// <= kCompanyNameMaxLen, so no combination can ever overflow.
struct CompanyNameBanks {
    std::span<const char* const> adjectives;
    std::span<const char* const> nouns;
    std::span<const char* const> groups;
};
CompanyNameBanks company_name_banks();

// One generated fantasy company name, "<ADJ> <NOUN> <GROUP>" (e.g.
// "IRON KETTLE BAND"), always <= kCompanyNameMaxLen chars by the bank
// budget contract above — never truncated. Pure and deterministic under the
// injected rng (exactly three next() draws per call); the name screen's
// REROLL simply calls it again on the advancing rng.
std::string generate_company_name(IRandom& rng);

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

// The same derivation with the family's own combat bases resolved from the
// family registry (a family the registry does not carry prices as a
// soldier, the picker's rule). Headless: no loader, no session.
DerivedStats compute_derived_stats(const guy& g);

// The fire delay the derivation used: base fire_delay minus the guy's
// bonus, floored at 1 (lower is faster). DerivedStats keeps only the
// 10/delay display rate, and the campaign power hook is handed the raw
// stat the sim fields.
float derived_fire_delay(const guy& g);

// --- Difficulty ---

// Cycle to the next difficulty setting. Returns (current + 1) % DIFFICULTY_SETTINGS.
int cycle_difficulty(int current);
int difficulty_percent(int difficulty);

// --- Allied mode ---

void toggle_allied_mode(SaveData& save);
bool is_allied_mode(const SaveData& save);

// --- CTF match settings ---

// Cycle the requested team count: Auto -> 2 -> 3 -> 4 -> Auto.
void cycle_ctf_team_count(SaveData& save);

// Cycle the capture limit: 0 (map default) -> 1 -> 3 -> 5 -> 10 -> 0.
void cycle_ctf_capture_limit(SaveData& save);

// True when the save's current campaign is the CTF campaign.

// True when the save's current campaign declares `matchup: versus` in its
// campaign.yaml — the generic competitive-matchup predicate. New scripted-mode
// surfaces key on this; the retired CTF campaign-id compares (the match
// settings gate, the lobby shared-teams rule) stay untouched until
// the CTF engine retirement swaps them over.
bool is_versus_campaign(const SaveData& save);

// Authored flag-team mask for a level known to match this save. Returns zero
// when campaign/mount/scenario metadata is not yet synchronized; lobby
// authority deliberately treats that as the temporary four-team fallback.
std::uint8_t ctf_authored_team_mask_for_loaded_level(
    const SaveData& save,
    const GameWorld& world,
    std::string_view mounted_campaign);

// Scenario-troops knob, three states on the existing int16 field:
//   0 ALL  — the level's authored cast is untouched (default, classic)
//   2 OWN  — drop every authored living + generator with no roster guy, any
//            team, wildlife included (Onslaught keeps its generators, and
//            protected named NPCs are exempt on classic maps)
//   3 FAIR — strip exactly like OWN, and the scripted modes size the bot
//            squads they generate to the human census instead of the
//            difficulty formula (og::sim::kTroopsMatched, matched-teams
//            design D25-D28); classic maps play it as plain OWN
// Every state applies on every campaign, so the cycle is a plain walk:
// ALL -> OWN -> FAIR -> ALL. The field keeps accepting the retired middle
// state 1 off disk and off the wire; the strip rules read it as OWN, and
// cycling from it (or any junk) lands on ALL. Pure, so the order is
// unit-pinnable.
short next_ctf_scenario_troops(short current);

void toggle_ctf_scenario_troops(SaveData& save);

// --- Difficulty submenu match rules ---
// Every default (0) is bit-identical classic behavior; the cyclers walk the
// small closed value sets and normalize any out-of-set stored value back to
// the default on the next step.

// Cycle respawn mode: 0 (off) -> 1 (heroes) -> 2 (everyone) ->
// 3 (Team 1 heroes only) -> 0.
void cycle_respawn_mode(SaveData& save);

// Cycle the respawn delay ticks: 0 (normal/map default) -> 60 (fast)
// -> 360 (slow) -> 0. Rides the existing ctf_respawn_ticks field.
void cycle_respawn_delay(SaveData& save);

// Toggle permadeath: keep_fallen_heroes 0 (permadeath ON, classic) <-> 1.
void toggle_permadeath(SaveData& save);

// Cycle the generator rate percent: 0 (normal/100) -> 50 (calm)
// -> 200 (frenzy) -> 0.
void cycle_generator_rate(SaveData& save);

// Toggle infinite gold: infinite_gold 0 (classic economy) <-> 1 (free
// purchases). SESSION-ONLY, so no company autosave follows a toggle.
void toggle_infinite_gold(SaveData& save);

// True when hire/train purchases are free for this session.
[[nodiscard]] bool gold_is_infinite(const SaveData& save) noexcept;

// The one affordability question every purchase site asks: always true with
// infinite gold on, otherwise cost <= the team's wallet. `team` is clamped
// into [0, MAX_PLAYERS) because it comes from unvalidated save data.
[[nodiscard]] bool can_afford(const SaveData& save, int team,
                              std::uint32_t cost) noexcept;

// The wallet as the clients print it: "INF" with infinite gold on, the
// decimal balance otherwise.
std::string format_wallet_amount(const SaveData& save, int team);

// Scripted campaign picker wallet (issue #206): affordability and debit for
// priced picker actions, over the ACTING team — the lowest team present on
// the roster, my_team fallback (the same rule og::data::make_campaign_providers
// applies to og.campaign_gold). can_afford is infinite-gold aware; the debit
// is skipped entirely under infinite gold (the hire/train free-purchase
// discipline) and clamps rather than underflows.
[[nodiscard]] bool campaign_picker_can_afford(const SaveData& save, int cost);
void campaign_picker_debit(SaveData& save, int cost);
// Inverse of the debit, for an action row no registered picker_action can
// honor (saturating; no-op under infinite gold, matching the debit).
void campaign_picker_refund(SaveData& save, int cost);

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

// Write a roster slot's fighting team outright (docs/lineup-design.md §5):
// the ONE writer every team-assignment surface goes through, cycle_guy_team
// included. False — with no mutation — for a team outside [0,4) or a slot
// that is out of range or empty.
bool set_guy_team(SaveData& save, int slot_index, short team);

// The local seat order gameplay derives (game.cpp view_teams): distinct
// NONZERO DEPLOYED roster teams in slot order, with my_team hoisted to the
// front when it has a deployed member. Benched-only colors never create an
// empty gameplay view.
std::vector<short> derive_local_seat_teams(const SaveData& save);

// Legacy seed for a newly opened local lobby: saved Together repeats Player
// 1's team; saved Split uses distinct deployed colors and pads missing seats
// with otherwise-unused colors. Base Camp then owns the live assignments.
std::vector<short> derive_local_gameplay_seat_teams(const SaveData& save);

// True iff each requested seat can claim a distinct deployed character on its
// gameplay team. Repeated Together seats consume one character each.
bool local_seat_teams_have_controls(const SaveData& save,
                                    std::span<const short> seat_teams);

// --- Company autosave (design §3.8; §1.2 G12 autosave_on_mutation) ---

// Builds the [SAVE-F1] session context for og::data::company_autosave.
// `networked_lobby_active` is the caller's lobby flag (SDL passes
// picker_lobby_is_networked(); the terminal clients pass their own state —
// picker_common stays link-free of the lobby client on purpose). When it is
// set, the owned teams are every wallet index a base-camp mutation can spend
// from on this machine: the in-range teamnums of the (private-to-this-
// machine) in-memory roster, plus my_team for the empty-roster hire case.
og::data::CompanyAutosaveContext company_autosave_context(
    const SaveData& save, bool networked_lobby_active);

// The single mutation-autosave choke point the menu layer consumes: the
// declarative per-screen `autosave_on_mutation` obligation (design §1.2 G12)
// calls this ONCE after any handled click on a flagged screen — replacing
// per-callback save tails. Routes through og::data::company_autosave
// (BaseCampMutation) with the [SAVE-F1] context above, so networked-lobby
// mutations become owner-preserving merge writes. `additional_owned_team`
// preserves a wallet whose last roster member was removed by the mutation
// (character sale); -1 adds nothing.
[[nodiscard]] SaveDataIoError company_autosave_after_mutation(
    SaveData& save, bool networked_lobby_active,
    int additional_owned_team = -1);

// --- Base camp roster (design §2.5) ---

// The display-slot list behind the base-camp roster rows: the occupied
// team_list slot indices in slot order. Callers re-collect after every
// roster change (and after a win fold — §3.3: update_guys' held-back pass
// reorders the roster, so positional display indices must never be held
// across a merge/fold).
std::vector<int> collect_base_camp_slots(const SaveData& save);

// Number of roster members with the v14 deployed flag set.
int count_deployed_members(const SaveData& save);

// The §2.5 deploy-toggle setter: flips guy::deployed at team_list `slot`.
// Returns the NEW deployed state; false (and no change) for an empty slot.
// Pure flip — the caller owns the §3.8 autosave and the §4.3 ready-clear
// (a no-op in solo/local sessions).
bool toggle_deploy_slot(SaveData& save, int slot);

// Move the member at `slot` one occupied roster position toward the front.
// Returns the member's new SaveData::team_list slot, or -1 when the slot is
// empty/out of range or already names the first member. This only changes
// roster order; the caller owns autosave/lobby propagation.
int move_team_member_up(SaveData& save, int slot);

// One §2.5 roster row's text columns (solo shape, §9.5.3): the HP column is
// GONE (it was DERIVED max HP — damage never persists to base camp — and
// redundant with CLASS+LVL; HP lives one click away in TRAIN). Numeric
// fields left-pad to fixed width (level 2, exp 6) so the digit columns
// right-align down the page on all three clients (§9.9 graft b — a space
// advances 6px like every glyph).
struct BaseCampRowText {
    std::string name;  // <= 12 chars
    std::string cls;   // <= 9 chars, uppercased family display name
    std::string level; // <= 3 chars, left-padded to 2
    std::string exp;   // <= 6 chars, left-padded to 6
};
BaseCampRowText format_base_camp_row(const guy& member);

// Start of the original View Team family's 16-color palette ramp. Base Camp
// uses its first eight shades for the compact identity swatch.
unsigned char base_camp_family_ramp_start(short family);

// §2.5 header line A right block: "GOLD {n}" (clipped to the 11-char block).
std::string format_base_camp_gold_label(const SaveData& save);

// A display string that must lose characters, cut so a player can SEE that
// it was cut: whole words where a word boundary survives past half the
// budget, then the ".." marker inside the budget. "THE RASPBERRY ISLE" at 17
// reads "THE RASPBERRY.." — never "THE RASPBERRY IS", which reads as a
// corrupted title rather than a shortened one. Budgets under 4 have no room
// for the marker and clip hard.
std::string clip_with_ellipsis(std::string value, std::size_t max_chars);

// §2.5 header line B, solo shape: "SCEN {n}: {title}  DEP {dep}/{total}",
// title cut with the ellipsis marker so the whole line fits the 34-char
// budget.
std::string format_base_camp_scen_line(const SaveData& save,
                                       std::string_view level_title);

// --- Base camp MP display model (design §2.5 networked shape) ---

// One display row of the base-camp roster. Solo/local sessions: every row
// is owned and `save_slot` indexes the private team_list. Networked: the
// MERGED lobby roster — this machine's characters first (still read from
// the PRIVATE save via save_slot, so deploy toggles and train edits show
// the same frame), then every other machine's replicated slots ordered by
// owner player index (read-only per the §2.5 ownership rules; their display
// data rides in `character`/`deployed`/`company`).
struct BaseCampDisplaySlot {
    bool owned = true;
    int save_slot = -1;                     // own rows: team_list index
    std::uint8_t owner_player_index = 0xff; // foreign rows: owning seat
    bool deployed = true;                   // foreign rows (own rows read the save)
    std::string company;                    // owning machine's company name
    og::sim::LobbyCharacterData character;  // foreign row display data
};

// Build the display list. `players` is the replicated lobby roster
// (ignored unless `networked`); `local_player_indices` marks which lobby
// seats are THIS machine's (their slots are skipped — the private save is
// the authority for own rows). Two well-stocked machines can replicate
// more than 24 display slots (full rosters always replicate for display,
// §4.2) — callers page over the returned size defensively, never over a
// 24-row assumption.
std::vector<BaseCampDisplaySlot> collect_base_camp_display_slots(
    const SaveData& save,
    const std::vector<og::sim::LobbyPlayer>& players,
    const std::vector<std::uint8_t>& local_player_indices,
    bool networked);

// Deployed/total over the display list; owned rows read the save's LIVE
// deploy flag (an optimistic toggle or a server reconcile shows the same
// frame — the §4.2 deploy_reconcile adoption writes that flag).
struct BaseCampDeployCounts {
    int deployed = 0;
    int total = 0;
};
BaseCampDeployCounts count_base_camp_display_deploys(
    const std::vector<BaseCampDisplaySlot>& slots, const SaveData& save);

// READY n/m over NON-HOST machines only (a multi-seat host's extra seats
// are is_host=false but never gate the start — §4.3; the server excludes
// them by peer, this mirrors it by the server-issued LobbyMachineId). A
// machine counts ready when ALL of its seats are ready. Invalid IDs are
// conservatively treated as distinct seats, never grouped by display names.
struct BaseCampReadyCounts {
    int ready = 0;
    int machines = 0;
};
BaseCampReadyCounts count_base_camp_ready_machines(
    const std::vector<og::sim::LobbyPlayer>& players);

// §9.12 (G5) lobby census over the replicated players: machines = distinct
// server-issued machine IDs (the HOST machine included, unlike
// BaseCampReadyCounts), players = total seats.
struct BaseCampSessionCensus {
    int machines = 0;
    int players = 0;
};
BaseCampSessionCensus count_base_camp_session_census(
    const std::vector<og::sim::LobbyPlayer>& players);

// --- §9.12 header line-B character budgets ---
//
// The band's ink starts at x=10 and its readability strip pads 2px on each
// side, so N characters occupy [8, 10 + 6N + 2). Two walls stand to its
// right: the roster header band's HIRE command while a composition shows
// it, and the roster pager cluster while it does not. The budget is a
// DERIVED quantity — menu_screen_specs static_asserts its own geometry
// against the wall constants here, so relocating HIRE breaks the build
// instead of quietly amputating the line.
inline constexpr int kBaseCampLineBTextX = 10;
inline constexpr int kBaseCampLineBGlyphAdvance = 6;
inline constexpr int kBaseCampLineBStripPad = 2;
inline constexpr int kBaseCampLineBHireWallX = 220;
inline constexpr int kBaseCampLineBPagerWallX = 258;
inline constexpr int base_camp_line_b_budget(int wall_x)
{
    return (wall_x - kBaseCampLineBTextX - kBaseCampLineBStripPad) /
        kBaseCampLineBGlyphAdvance;
}
inline constexpr int kBaseCampLineBCharsHireVisible =
    base_camp_line_b_budget(kBaseCampLineBHireWallX);   // 34
inline constexpr int kBaseCampLineBCharsHireHidden =
    base_camp_line_b_budget(kBaseCampLineBPagerWallX);  // 41

// §9.12 header line B, networked HEALTHY shape — the G5 session status
// (role + room code + census), SHAPED to fit `max_chars`:
//   host:   "HOSTING <ROOM>: <p> PLAYERS / <n> MACHINES"
//   joiner: "IN <ROOM>: <p> PLAYERS / <n> MACHINES"  (no room: "JOINED: ...")
// PLAYERS lead: the seat rail shows this machine's seats only, so the size
// of the lobby has no other home on the screen. A narrower band takes a
// whole shorter spelling rather than a byte cut — "<p> PLAYERS/<n> PCS",
// then "<p>P/<n>M", then the room code goes. A count of one takes the
// singular ("1 PLAYER", "1 MACHINE", "1 PC") — always the shorter spelling,
// so it can never push a rung off a band the plural fit. Room codes
// display-clip at 12 chars (relay codes are "GLAD-XXXX").
std::string format_base_camp_session_status(
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players,
    int max_chars);

// The §2.5/§9.12 networked line-B priority stack: a degraded-link
// connection_alert takes the slot (and the ORANGE color) over the healthy
// session status. `alert` set => {alert text, alert=true}.
struct BaseCampLineB {
    std::string text;
    bool alert = false;
};
BaseCampLineB compose_base_camp_line_b(
    const std::optional<std::string>& alert,
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players,
    int max_chars);

// --- §2.6 GO / READY slot (the base-camp dual-role button) ---

// The six presentation states of the shared (262,178,50,18) slot: the host
// keeps GO (grey solo, colored networked), clients get the READY toggle in
// the SAME rect (exactly one of the two same-rect buttons is visible).
enum class ReadyGoState : std::uint8_t {
    LocalGo,         // state 1: solo/local multi — plain grey GO (pinned)
    LocalGoNoDeploy, // state 2: solo, 0 deployed — grey GO, click popups
    HostGated,       // state 3: networked host, gates unmet — yellow GO
    HostGo,          // state 4: networked host, all ready + deploy — green GO
    ClientUnready,   // state 5: joiner, not ready — red READY
    ClientReady,     // state 6: joiner, ready — green UNREADY
};

// §2.6 face colors (label text stays DARK_BLUE in every state; the bevel
// edges stay grey — only vbutton::color's front face is stamped).
// CONTRAST DECISION (§2.0 U1, recorded 2026-07-20): the mandated one-frame
// TESTING capture measured DARK_BLUE(0,0,168) against the candidate faces:
// 61 green (2.61:1, readable) PASS, 93 yellow (3.20:1) PASS, 45 dark red
// (1.23:1, illegible) FAIL — so the unready face takes the sanctioned
// fallback grammar's shipped RED=40 (2.75:1, strong hue contrast; the
// draw_button_colored FX-toggle red) while 61/93 ship as designed.
inline constexpr std::uint8_t kReadyGoFaceGrey = 13;    // BUTTON_FACING
inline constexpr std::uint8_t kReadyGoFaceGo = 61;      // green run
inline constexpr std::uint8_t kReadyGoFaceGated = 93;   // yellow run
inline constexpr std::uint8_t kReadyGoFaceUnready = 40; // RED (U1 fallback)

// One frame's presentation of the slot. `label` is the ACTION ("GO",
// "READY", "UNREADY" — label = the action, color = the state); `caption`
// is the §2.6 blocker/denial headline a click on the gated state surfaces
// ("WAITING FOR OTHERS" / "NO ONE IS DEPLOYED" / "DEPLOY AT LEAST ONE"),
// empty when the click acts directly.
struct ReadyGoPresentation {
    ReadyGoState state = ReadyGoState::LocalGo;
    std::string label;
    std::uint8_t face_color = kReadyGoFaceGrey;
    std::string caption;
};

// The §2.6 state table, pure (headlessly unit-tested — U10). `spectator`
// is the formatter's historical name for a machine that contributes ZERO
// character slots (an empty roster): that shape has no deploy minimum
// [NET-R9]. A true zero-seat Base Camp client has no READY action and never
// calls this formatter through that path. `cross_control` ON also removes the
// per-machine deploy minimum; the global >= 1 rule (host states) always
// applies. Solo/local (`networked` false) never consults ready and keeps the
// plain grey GO byte-identical (states 1-2).
ReadyGoPresentation format_ready_go_button(bool networked,
                                           bool is_host,
                                           bool my_ready,
                                           bool all_other_machines_ready,
                                           int global_deployed,
                                           int own_deployed,
                                           bool cross_control,
                                           bool spectator);

// §2.6 state-3 popup body: the not-ready machines' company names, grouped by
// authoritative machine_id (an invalid ID is isolated defensively). A
// machine's label is its first non-empty company, falling back to its display
// name, one per line, clipped to 26 chars, at most 4 lines with an "AND n MORE"
// tail.
std::string format_go_blockers(
    const std::vector<og::sim::LobbyPlayer>& players);

// §2.7 cross-control toggle label: "CTRL: OWN" (only the owner machine
// controls its characters) / "CTRL: ALL" (players may control others'
// characters in-level). Shared by the SDL DIFFICULTY row and the curses
// lobby status line.
std::string format_cross_control_label(bool cross_control_enabled);

// One §2.5 roster row's text columns, networked shape (U7: CLASS dropped,
// carried by the family chip; 16-char COMPANY). §9.5.3: no HP column, and
// the level left-pads to 2 like the solo shape (§9.9 graft b).
struct BaseCampNetRowText {
    std::string name;    // <= 10 chars
    std::string company; // <= 16 chars
    std::string level;   // <= 3 chars, left-padded to 2
};
BaseCampNetRowText format_base_camp_net_row(std::string_view name,
                                            std::string_view company,
                                            int level);

// Display-only guy copy of a replicated foreign character (derived-stat input,
// family/team swatch). NOT the roster-assembly builder: it never enters a save
// or a level and does not establish runtime identity.
std::unique_ptr<guy> make_base_camp_display_guy(
    const og::sim::LobbyCharacterData& character);

// One team row label, <= 30 chars (the text and curses Matchup screens):
// "{COLOR} TEAM {seat_tag} {status}"
// where status is "NOT ON MAP" (CTF, no authored flag), "BOTS" (CTF authored
// team with no humans and no local heroes), or "{n} HEROES".
std::string format_team_row_label(short team,
                                  int hero_count,
                                  bool is_ctf,
                                  bool authored,
                                  bool has_humans,
                                  std::string_view seat_tag);

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

// How the caller's staged world (or the lack of one) should be presented.
// A picker_common-local enum so this header stays free of the match_stage
// header: SDL/curses/text callers map og::server::StageStatus (owners) or
// MirrorStatus (joiners) onto it identically.
enum class StagePreviewStatus : std::uint8_t {
    None = 0,   // nothing staged yet (waiting / not a staging session)
    Staged,     // the staged world answers
    Failed,     // the owner's stage failed ("STAGING FAILED")
};

// The closed display vocabulary for a staged team's fill, censused from the
// staged world's observable facts (has_guy / BOT_MARK provenance /
// generators). The pack-extensible label channel died with the plan phase:
// the staged world IS the answer, so there is nothing verbatim to forward.
enum class ScenarioFill : std::uint8_t {
    Company = 0,   // any live has_guy walker (player rosters)
    Troops,        // guy-less unmarked livings (authored map troops)
    Bots,          // BOT_MARK-tagged squad, legacy difficulty shape
    Matched,       // BOT_MARK-tagged squad, FAIR-matched (MATCHED.SIZE > 0)
    Generators,    // generators only (onslaught foundries)
    Empty,         // an active team with no forces at all
};

struct ScenarioRosterRow {
    short team = 0;
    bool is_generator = false;
    bool named = false;
    std::string name;  // named NPCs only
    short family = 0;
    int level = 1;
    int count = 1;
};

// --- Base Camp player-seat rail geometry (#243, redesigned) ---
//
// The rail is THIS MACHINE'S four seat slots and nothing else. Remote seats
// are counted on the header line and listed in VIEW LEVEL's SEATS report;
// they never take a slot here, so the rail needs no pager and no [+] at its
// left end. A slot holding one of this machine's seats draws a card; a slot
// past them is a real ADD PLAYER button (or a dimmed LOBBY FULL one) whose
// activation is the add path itself.
//
// The grid is FIXED, so a slot never moves when its neighbour appears: four
// 70px faces at x = 8 / 86 / 164 / 242, gutter 8, closing exactly on the
// panel's right rail (4*70 + 3*8 = 304 = 312 - 8). The face is a label
// contract (kBaseCampSeatCardLabelBudget = 70/6 characters).
inline constexpr int kSeatRailX0 = 8;        // the panel's left rail (BACK's x)
inline constexpr int kSeatRailRightX = 312;  // exclusive: the panel's right rail
inline constexpr int kSeatRailGap = 8;
inline constexpr int kSeatRailCardWidth = 70;
inline constexpr int kSeatRailSlots = 4;     // this machine's seat ceiling
static_assert(kSeatRailSlots * kSeatRailCardWidth +
                      (kSeatRailSlots - 1) * kSeatRailGap ==
                  kSeatRailRightX - kSeatRailX0,
              "the four slots close on both of the panel's margins");
// og::sim::kMaxGlobalPlayers, restated so this pure header need not pull the
// transport contract into every picker TU. menu_screen_specs.cpp
// static_asserts that the two still agree.
inline constexpr int kSeatRailGlobalSeatCap = 16;

// The one question behind every bare slot: can another seat be claimed right
// now? Kept as data so the rail's chrome and the button's row state can never
// disagree about the answer.
struct SeatClaimability {
    bool multiplayer_enabled = true;  // false in a DISABLE_MULTIPLAYER build
    int local_count = 0;              // seats this machine already owns
    int local_seat_cap = MAX_PLAYERS; // og::input::local_seat_cap()
    int global_count = 0;             // seats across the whole lobby
    int global_cap = kSeatRailGlobalSeatCap;
};

// Seats this machine may still claim: the smaller headroom of the two caps,
// never negative, and zero when the build has no multiplayer at all.
int seats_still_claimable(const SeatClaimability& claim);

// Slots the rail shows at all. What limits the rail is the DEVICE, never the
// lobby: a phone with no pad shows exactly one slot (#249 — an offer the
// hardware cannot accept is worse than no offer), a phone with two pads
// three, a desktop four. A build with no multiplayer has one seat and no
// door to a second.
int base_camp_seat_rail_slot_cap(const SeatClaimability& claim);

// Bare slots after this machine's own seats: every remaining slot inside the
// device cap. A slot the lobby cannot currently fill still appears — dimmed,
// saying LOBBY FULL — because "there is a seat here and it is spoken for"
// is the honest answer; only hardware removes the slot entirely.
int base_camp_seat_rail_placeholder_count(const SeatClaimability& claim);

struct SeatRailLayout {
    int card_w = kSeatRailCardWidth;
    // The fixed four-slot grid. slot_x[0 .. shown_cards) hold this machine's
    // seats and the next placeholder_count are ADD PLAYER / LOBBY FULL
    // buttons; any slot past those is hidden (draw_buttons and leftmouse both
    // skip hidden rows) but keeps its grid x so nothing shifts when it
    // returns.
    std::array<int, kSeatRailSlots> slot_x{};
    int shown_cards = 0;
    int placeholder_count = 0;

    [[nodiscard]] int slot_count() const
    {
        return shown_cards + placeholder_count;
    }
};

// The rail's per-frame geometry: the fixed grid plus how many of its slots
// are live this frame. Nothing justifies and nothing packs — a slot's x is a
// property of the slot, not of how many neighbours it has, so a card cannot
// slide sideways when a player joins or leaves.
SeatRailLayout base_camp_seat_rail_layout(int visible_cards,
                                          int placeholder_count);

// --- Player seats (#218 seat block) ---

// One seat line of the View Level report: the display-relevant facts of a
// replicated lobby seat, resolved for THIS client (is_local decides YOU vs
// the company abbreviation — a per-client presentation, not lobby state).
struct ScenarioSeatRow {
    int player_index = 0;   // dense lobby-wide P# ordinal
    std::string company;    // the displayable identity (never the net name)
    short team = 0;
    bool ready = false;
    bool is_local = false;  // one of this machine's seats
};

// The seat inputs a client passes to build_scenario_roster_report: the
// replicated og::sim::LobbyPlayer list — the SAME seat records
// LobbyServer::build_player_bindings derives the launch bindings from, so
// showing them is display of an existing fact, never a rule twin — plus
// this machine's seat indices. Seats are an explicit parameter because the
// presentation is per-client: host and joiner hold identical seat records
// but different local_player_indices (YOU vs the abbreviation).
struct ScenarioSeatContext {
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_player_indices;
};

// The public 3-letter company abbreviation for seat labels: first three
// alphanumerics, upper-cased; "NET" when the name has none. Networked seat
// display never falls back to LobbyPlayer::name (an opaque net-<hex>
// transport identity).
std::string company_abbreviation(std::string_view company);

// "P{n} {YOU|ABC}" + optional " [RDY]" — the shared seat identity label
// (the retired MATCHUP screen's vocabulary, now the View Level seat block's
// home). The row is the single format authority; build_scenario_roster_report
// resolves is_local from the caller's ScenarioSeatContext before this runs.
std::string seat_identity_label(const ScenarioSeatRow& seat);

// The match-shape summary over the lobby seats: "CO-OP" / "2 VS 2" /
// "FREE-FOR-ALL" / "MIXED TEAMS", or "NO PLAYER SEATS" for an empty list.
std::string format_seat_summary(
    const std::vector<og::sim::LobbyPlayer>& players);

// The shared local/solo seat synthesis (the empty-lobby fallback the
// retired MATCHUP screen and Base Camp both used, deduplicated; also the
// text/curses seat source —
// their View Level paths stage locally, so save-derived seats ARE their
// staging input): one seat per save.numplayers, teams from
// derive_local_gameplay_seat_teams, company = save_name, P1 host. Empty
// when numplayers == 0.
std::vector<og::sim::LobbyPlayer> synthesize_local_lobby_players(
    const SaveData& save);

struct ScenarioRosterReport {
    bool is_versus = false;         // world.type & TYPE_SCRIPTED
    bool will_activate = false;     // staged: mode.active / fallback >= 2
    short your_team = 0;            // 0 when allied, else save.my_team
    std::array<bool, 4> team_authored = {};  // fallback arm only (markers)
    std::array<bool, 4> team_active = {};    // staged census / the clamp
    std::array<int, 4> team_anchor_count = {};
    std::vector<ScenarioRosterRow> rows; // grouped, team-major
    // --- Staged arm (#218): observations of the staged world -------------
    // True when a staged world answered: team_active is the live census,
    // the fill columns are meaningful and mode_name is ModeState::name.
    // False = the count-only fallback over the caller's scratch world.
    bool staged = false;
    // StagePreviewStatus::Failed — the formatter leads with the honest
    // "STAGING FAILED" line over the fallback census.
    bool stage_failed = false;
    // The staged MODE is active and the fill census answered (mode_name +
    // team_fill are meaningful). False for a staged hook-less scripted
    // level, whose count-only fallback block renders instead.
    bool mode_census = false;
    // The staged mode attempted init and refused (init_attempted && !active
    // with a registered on_mode_init): the verbatim refusal sentence. A
    // scripted level with NO on_mode_init hook is not refusing — it has no
    // mode, and the count-only fallback answers instead.
    bool refusing = false;
    // Neither a staged world nor a fallback world: refusal lines only.
    bool unavailable = false;
    std::string mode_name;          // ModeState::name when staged + active
    std::array<ScenarioFill, 4> team_fill = {};
    std::array<int, 4> team_fill_count = {};
    // --- Lineup facts (lineup §3.4): the APPLIED per-team bot facts the
    // spawn seam banked in the shared mode-var slot (mode_match.lua
    // bank_lineup_facts / kModeVarLineupFacts below) — preset name
    // resolved through the campaign lineup hook (empty when AUTO, the
    // ordinal is unregistered, or no hook), explicit level 1..9 (0 =
    // AUTO). Facts, never requests: a knob a mode ignored banks nothing.
    std::array<std::string, 4> team_squad_name = {};
    std::array<int, 4> team_squad_level = {};
    // --- Seat block (#218): the caller's lobby seats, P#-sorted, with the
    // format_seat_summary match shape. Both empty when the caller passed no
    // seat context — every seatless report is byte-identical to before.
    std::string seat_summary;
    std::vector<ScenarioSeatRow> seats;
};

// Read a STAGED world (host MatchStage world, joiner preview mirror, or a
// locally staged world — all carry the same bytes) into the roster report.
// Named NPCs get individual rows; unnamed livings group by (team, family,
// level); generators aggregate per team. Dormant (delayed-spawn) walkers
// are excluded exactly as the keyframe capture excludes them, so every
// client censuses the identical non-dormant world; they reveal at their
// authored tick after launch (the documented preview carve-out).
//
// Fill provenance is observable fact, never a rule twin: COMPANY = any live
// has_guy walker; MAP TROOPS = guy-less unmarked livings; BOT SQUAD /
// MATCHED BOTS = livings carrying the modes.core BOT_MARK stat bit (matched
// when the shared MATCHED.SIZE mode var is banked non-zero); GENERATORS =
// generators alone. Anchor counts read back from world.respawn — banked by
// the REAL mode_stage_init scan at stage time.
//
// staged == nullptr: the count-only og::sim::effective_team_mask fallback
// answers over `fallback_world` (the caller's disposable scratch load; the
// engine respawn_scan_anchors runs on it — the exact scan launch step 0
// runs), preceded by the honest STAGING FAILED line when status == Failed.
// Both worlds null => refusal lines only ("PREVIEW UNAVAILABLE").
//
// `seats` (#218 seat block): the caller's lobby seat context; nullptr or an
// empty player list emits no seat lines and leaves every line byte-identical
// to the seatless report.
ScenarioRosterReport build_scenario_roster_report(
    const GameWorld* staged, StagePreviewStatus status, const SaveData& save,
    GameWorld* fallback_world, const ScenarioSeatContext* seats = nullptr);

// Render the report as display lines, every line <= 48 chars.
std::vector<std::string> format_scenario_report_lines(
    const ScenarioRosterReport& report);

// --- Player count ---

void set_player_count(SaveData& save, int count);

// Returns true when numplayers == 0 (spectator / autoplay mode).
bool is_spectator_mode(const SaveData& save);

// --- Label formatting ---

// Format the difficulty button label (e.g. "Difficulty: Battle").
std::string format_difficulty_label(int difficulty);

// Compatibility formatter for the retired AlliedMode binding
// ("SEATS: TOGETHER" / "SEATS: SPLIT"). It is not exposed by current menus;
// combat allegiance always comes from character colors.
std::string format_allied_mode_label(const SaveData& save);

// Format the team count label ("Teams: N" / "Teams: Auto").
std::string format_ctf_teams_label(const SaveData& save);

// Format the capture limit label ("Capture Limit: Map default" or ": N").
std::string format_ctf_caps_label(const SaveData& save);

// Format the scenario-troops label ("TROOPS: ALL" when keeping the authored
// cast, "TROOPS: OWN" when stripping it, "TROOPS: FAIR" when stripping plus
// census-matched bot squads).
std::string format_ctf_troops_label(const SaveData& save);

// "Respawns: Off" / "Respawns: Heroes" / "Respawns: Everyone" /
// "Respawns: Team 1 Heroes".
std::string format_respawn_mode_label(const SaveData& save);

// "Spawn Delay: Normal" / "Spawn Delay: Fast" / "Spawn Delay: Slow".
std::string format_respawn_delay_label(const SaveData& save);

// "Permadeath: On" / "Permadeath: Off" (On == keep_fallen_heroes == 0).
std::string format_permadeath_label(const SaveData& save);

// "Generators: Normal" / "Generators: Calm" / "Generators: Frenzy".
std::string format_generator_rate_label(const SaveData& save);

// "Infinite Gold: Off" / "Infinite Gold: On".
std::string format_infinite_gold_label(const SaveData& save);

// --- Company screens: label formatters (design §2.2/§2.3) ---
// (The §2.2 "file: <slug>.gtl" preview formatter was DELETED — §9.3/F2:
// the filename teaches nothing; companies are fully managed in-game on
// every client. Slug derivation itself stays og::data::derive_company_slot.)

// "YYYY-MM-DD" (UTC — deterministic, never the machine's timezone), or ""
// for never-played (<= 0) and out-of-calendar values. The §2.3 company-row
// date column; the cloud-save confirm prompts (#155) reuse it.
std::string format_played_date_utc(std::int64_t unix_s);

// "PASSPHRASE: SET" / "PASSPHRASE: NOT SET" — the CLOUD SAVE screen's
// status line (#155), shared by all three clients.
std::string format_cloud_passphrase_status(bool key_set);

// "COMPANIES (N)" — the §2.3 Company List header line.
std::string format_company_list_title(int count);

// One §2.3 Company List row, pre-split into the three drawn columns (the
// SDL content pass draws them at x=27/141/155; terminals join them).
struct CompanyRowText {
    // Display name, <= kCompanyNameMaxLen (18) chars, clipped; empty
    // display names (and corrupt headers that never parsed one) fall back
    // to the slot name so the row still identifies its file.
    std::string name;
    // Roster count, right-aligned 2 chars ("12", " 3"); "--" on corrupt
    // rows (the count did not parse).
    std::string roster;
    // Last-played "YYYY-MM-DD" (UTC), 10 chars; "" when never played;
    // "CORRUPT" on corrupt rows — the §2.3 corrupt-row marking.
    std::string played;
    bool corrupt = false;
};
CompanyRowText format_company_row(const og::data::CompanyInfo& info);

// One §2.3 row joined into a single line for the terminal clients (both must
// stay byte-identical): "<marker> <name padded to 18> <roster 2> <played>".
// `active` marks the machine's active company with '*' (the terminal
// projection of the SDL red do_outline, U4). The played column is omitted
// entirely when empty, so never-played rows carry no trailing spaces.
std::string format_company_row_line(const og::data::CompanyInfo& info,
                                    bool active);

// --- Backups sub-view: label formatters (design §2.4) ---

// "BACKUPS: <name <= 18ch> (N/20)" — the §2.4 title line. The "/20"
// (kCompanyBackupRetention) IS the retention display: the user sees how full
// the ring is. `company_name` is clipped to kCompanyNameMaxLen.
std::string format_backup_list_title(const std::string& company_name,
                                     int count);

// One §2.4 backup row, pre-split into the two drawn columns (the SDL content
// pass draws them at x=27/151; terminals join them).
struct BackupRowText {
    // "L<nn>" plus the level title (<= 14 chars) when it is resolvable: the
    // title comes off the MOUNTED package (the level_display_guarded rule),
    // so a backup pointing at an unmounted campaign shows the bare "L<nn>".
    // The "Level N" fallback title is dropped (redundant with L<nn>).
    // "CORRUPT" on corrupt snapshots — the §2.4 corrupt-backup marking.
    std::string level;
    // "MM-DD HH:MM" (UTC — deterministic like the §2.3 date column) from the
    // snapshot header's last_played; "" when the snapshot predates any stamp;
    // "--" on corrupt snapshots.
    std::string saved;
    bool corrupt = false;
};
BackupRowText format_backup_row(const og::data::CompanyBackupInfo& info);

// One §2.4 row joined into a single line for the terminal clients (both must
// stay byte-identical): "<level padded to 20> <saved>"; the saved column is
// omitted entirely when empty, so unstamped rows carry no trailing spaces.
std::string format_backup_row_line(const og::data::CompanyBackupInfo& info);

// Human-readable string for CompanyRestoreError values (§3.7 [SAVE-R3]).
// RestampFailed is worded as the partial success it is: the rewind itself
// finished, only the timestamp write failed.
const char* company_restore_error_string(og::data::CompanyRestoreError error);

// Outcome of CONTINUE (§2.1): the caller acts on failures (the SDL surface
// keeps whatever is loaded rather than silently swapping in a broken file).
enum class ContinueResult {
    Opened,     // active company repointed and loaded
    NoCompany,  // select_startup_company() was empty (CONTINUE is gated hidden)
    Corrupt,    // the most-recent company header is invalid — do not switch
    LoadFailed, // header validated but the full load failed
};

// §2.3 open-one-company core shared by CONTINUE and the Company List row
// click: validate the slot's header first (never silently switch to a
// corrupt company), point the active-company slot at it, and load it into
// `save` (load mounts the company's campaign). On a load failure the
// previous active slot is restored — and its save best-effort reloaded — so
// the slot and the in-memory save never disagree about which company is
// open. `io_error`, when non-null, receives the load error for surfacing.
ContinueResult open_company_slot(SaveData& save, const std::string& slot,
                                 SaveDataIoError* io_error = nullptr);

// §2.1 CONTINUE: select the most-recent company (WP2 startup selection) and
// open it via open_company_slot. Returns why it stopped so the surface can
// react (popup + Company List fallback on Corrupt/LoadFailed, §2.9 flow 2).
// `io_error`, when non-null, receives the load error for surfacing.
ContinueResult open_most_recent_company(SaveData& save,
                                        SaveDataIoError* io_error = nullptr);

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

// --- GAME SETTINGS speed selector (cfg gameplay/timer_wait) ---
// The sim's per-tick wait, shown as the classic inverted 1..11 SPEED number
// ((20 - timer_wait) / 2 + 1). timer_wait 6 (the shipped default) reads as
// SPEED 8. The stored value is what GameWorld::timer_wait carries and what
// the host stamps into pending_timer_wait_request_.
inline constexpr int kTimerWaitFastest = 0;
inline constexpr int kTimerWaitSlowest = 20;
inline constexpr int kGameSpeedMin = 1;
inline constexpr int kGameSpeedMax = 11;

// Parse cfg gameplay/timer_wait: clamped to 0..20, anything unparseable
// (including the empty string an absent key reads as) falls back to the
// shipped default, og::sim::DEFAULT_TIMER_WAIT.
int parse_timer_wait(const std::string& value);
int game_speed_from_timer_wait(int timer_wait);
int timer_wait_from_game_speed(int speed);
// One click = one SPEED step faster, wrapping 11 -> 1. Returns the new cfg
// value (a timer_wait, not the display number).
std::string cycle_game_speed(const std::string& current);
// "SPEED: 8" — 9 chars at the widest ("SPEED: 11"), inside the 90px face.
std::string format_game_speed_label(const std::string& value);

// --- DISPLAY brightness (cfg graphics/brightness) ---
// Signed gamma steps handed to adjust_palette(). Clamped: the palette
// transform saturates to white/black past these, so further steps would be
// dead travel on the -/+ pair.
inline constexpr int kBrightnessStepMin = -5;
inline constexpr int kBrightnessStepMax = 5;
int parse_brightness_steps(const std::string& value);
// Step the stored value by one in `direction`'s sign, clamped to the range.
std::string adjust_brightness_steps(const std::string& current, int direction);
// "Brightness: 0" / "Brightness: +2" / "Brightness: -3" — the live text
// beside the -/+ pair.
std::string format_brightness_label(int steps);

// --- DISPLAY zoom and smoothing selectors ---
// Zoom selector (cfg graphics/zoom, 1.0 classic-density toward 0.1) and the
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
    enum class SellResult {
        Sold,
        NoMember,
        CheckpointFailed,
    };

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

    // §2.5 per-row TRAIN: seat the session directly on `slot` (no more
    // enter-then-cycle). Returns false (position unchanged) when the slot is
    // empty or not editable by this machine.
    bool seek_slot(int slot);

    // Accept: validates cost, deducts gold, copies working copy -> real team member.
    // If level changed, calls upgrade_to_level(). Returns false if can't afford.
    // force=true skips the cost check (for cheat mode).
    bool accept(bool force = false);

    // Sell the selected real roster member (never the unaccepted working
    // copy). `checkpoint` must durably snapshot the company; it is invoked
    // after validation and immediately before the irreversible roster
    // mutation. A failed checkpoint leaves the member and wallet untouched.
    SellResult sell_current(const std::function<bool()>& checkpoint);

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
    std::uint32_t current_sell_value() const;
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

// --- LINEUP (docs/lineup-design.md) ------------------------------------

class CampaignZoneSession;

// The eight per-team bot knobs as the LINEUP surfaces hold them. WP-B owns
// the persistence chain (SaveData -> LobbySettings -> world); the helpers
// here take the values as plain data so they stay headlessly testable.
// squad: 0 = AUTO, 1 = NONE, 2.. = preset ordinal. level: 0 = AUTO, 1..9.
struct LineupBotKnobs {
    std::array<short, 4> squad{};
    std::array<short, 4> level{};
};

// May this slot's fighting team be edited here? The single predicate behind
// BOTH the Base Camp roster chip (local) and the LINEUP fighter list (all
// modes): the save slot is editable, the campaign's zone composition allows
// team changes, and the roster is not in assign mode (the chip column is
// spoken for). Deliberately NOT gated on `networked`: repairing a colour
// mismatch between a seat and its own company is exactly what the fighter
// list exists for (§2.2).
bool lineup_fighter_team_editable(const SaveData& save, int slot_index,
                                  bool zone_can_team, bool assign_mode);
// Same predicate against a live zone session; a null zone = full capability.
bool lineup_fighter_team_editable(const SaveData& save, int slot_index,
                                  const CampaignZoneSession* zone,
                                  bool assign_mode);

// One fighter's price, or nothing when the campaign registers no metric.
using LineupPowerFn = std::function<std::optional<long long>(const guy&)>;

// og::ui::compute_derived_stats + the campaign `lineup.power` hook (§4).
// Nothing when no campaign hook is registered or the hook refuses — the
// band then shows `POWER --` and SPLIT FAIR falls back to level order.
std::optional<long long> lineup_power_for_guy(const guy& g);

// One LINEUP team band: who sits on the team, who fights for it, and what
// the page says about the mismatch between the two.
struct LineupTeamBand {
    int team = 0;
    bool has_seat = false;
    int seat_count = 0;
    std::vector<std::string> seat_labels;  // "P1 WASD", "P3 BOB"
    int fighter_count = 0;                 // deployed characters on the team
    std::optional<long long> power;        // nullopt = no metric
    // The two informational diagnostics (§2.1). They replace the census in
    // the disabled grey; GO keeps its own refusal.
    enum class Diag {
        None,
        NeedsFighters,  // seats outnumber deployed fighters (the M4 refusal)
        NoSeatAi,       // fighters with no seat: they fight under AI
    };
    Diag diag = Diag::None;
    int needs = 0;  // NeedsFighters: how many more the team wants
};

// The seat chip's owner label: "P{n} {short}". `short_name` is the local
// seat's input-mapping short name where the caller can resolve one (the
// base_camp_seat_label convention); an empty one falls back to the owning
// company's three-letter abbreviation, which is all a remote seat has.
std::string lineup_seat_label(const og::sim::LobbyPlayer& seat,
                              std::string_view short_name);

// The four bands. Seats come from `players` (every machine in the lobby);
// fighters are the deployed characters on each team — replicated across
// every player's slots when `networked`, this machine's own save when not,
// so a fighter is counted exactly once either way. `power` prices one
// fighter (empty = no metric); `local_seat_short_name` names a local seat's
// controller (empty = company abbreviations everywhere).
std::array<LineupTeamBand, 4> build_lineup_bands(
    const SaveData& own,
    std::span<const og::sim::LobbyPlayer> players,
    std::span<const std::uint8_t> local_player_indices,
    bool networked,
    const LineupPowerFn& power,
    const std::function<std::string(std::uint8_t)>& local_seat_short_name = {});

// --- LINEUP labels (exact strings; every one of them is pinned) ---

// "BOTS: AUTO" / "BOTS: NONE" / "BOTS: <NAME>" (12-char face, names clipped
// to 6). A preset ordinal the caller has no name for — a joiner clamps
// without ever seeing the list — renders "BOTS: #n" rather than lying AUTO.
std::string format_lineup_bots_label(short squad,
                                     std::span<const std::string> preset_names);
// "LV: AUTO" / "LV 5". Out-of-range levels read AUTO.
std::string format_lineup_level_label(short level);
// "5 FIGHTERS" / "1 FIGHTER" / "NO FIGHTERS", or the band's diagnostic:
// "NEEDS 2 FIGHTERS" / "NEEDS 1 FIGHTER" / "NO SEAT: AI".
std::string format_lineup_census(const LineupTeamBand& band);
// "POWER 4200" / "POWER --".
std::string format_lineup_power(std::optional<long long> power);

// The two cyclers. `preset_count` is clamped to kMaxBotPresets, so a squad
// cycles AUTO -> NONE -> presets -> AUTO and a level AUTO -> 1..9 -> AUTO.
// `dir` may be any step; a current value outside the range enters at AUTO.
short cycle_lineup_bots(short current, int preset_count, int dir);
short cycle_lineup_level(short current, int dir);

// --- SPLIT (§5) --------------------------------------------------------

enum class LineupSplit {
    Even,        // slot order, dealt round-robin
    Fair,        // power desc (tie: slot), snake draft
    AllToFirst,  // everyone onto the lowest-numbered seated team
};

// What a SPLIT would do: the full assignment (slot -> team, slot order for
// Even/AllToFirst, draft order for Fair) plus how many deployed characters
// the editable predicate refused to move.
struct LineupSplitPlan {
    std::vector<std::pair<int, short>> moves;
    int locked = 0;
};

// Deterministic, pure, and never applied on its own. `local_seat_teams` is
// this machine's seat->team derivation (duplicates and out-of-range values
// dropped, the rest ascending); benched characters are skipped; `editable`
// (empty = everything editable) keeps a locked slot where it is and counts
// it. A single seated team makes every mode ALL TO 1. Without a `power`
// metric, Fair sorts by level descending.
LineupSplitPlan split_company(const SaveData& save,
                              std::span<const short> local_seat_teams,
                              LineupSplit mode,
                              const LineupPowerFn& power,
                              const std::function<bool(int)>& editable);

// Write a plan through set_guy_team. Returns how many slots actually moved.
int apply_split(SaveData& save,
                std::span<const std::pair<int, short>> moves);

// --- Networking machine rows (§6) --------------------------------------

// One row of the Networking submenu's PLAYERS list: a MACHINE, not a seat.
struct NetworkingMachineRow {
    og::sim::LobbyMachineId machine_id = og::sim::kInvalidLobbyMachineId;
    std::string label;
    bool is_host = false;
    bool is_local = false;
};

// One row per machine, ordered by its lowest player_index. The label is
// "M1 <NAME> (HOST) (YOU)  P1 P2  <COMPANY>  READY", degraded WHOLE TOKEN
// to `label_chars` — READY goes first, then the company, and only then does
// what is left get clipped.
std::vector<NetworkingMachineRow> build_networking_machine_rows(
    std::span<const og::sim::LobbyPlayer> players,
    std::span<const std::uint8_t> local_player_indices,
    int label_chars = 39);

// --- Template implementations ---

// Requires full SaveData definition (include <openglad/resources/save_data.h>)
// at the point of instantiation.
template<typename Fn>
void for_each_team_member(const SaveData& save, Fn&& fn)
{
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[static_cast<std::size_t>(i)])
            fn(i, *save.team_list[static_cast<std::size_t>(i)]);
    }
}

} // namespace og::ui
