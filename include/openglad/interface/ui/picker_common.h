/* Shared picker business logic.
 *
 * Cost calculations, name generation, team management, and constants
 * used by both the SDL and text picker clients.
 */
#pragma once

#include <openglad/core/constants.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/resources/company.h>
#include <openglad/resources/save_data.h>
#include <array>
#include <cstddef>
#include <cstdint>
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
// mutations become owner-preserving merge writes. No screen sets the flag
// yet (WP4 flips it on); until then nothing calls this in production and
// legacy flows stay byte-identical.
[[nodiscard]] SaveDataIoError company_autosave_after_mutation(
    SaveData& save, bool networked_lobby_active);

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

// The original View Team identity color: NAME and family/class text used the
// family-numbered 16-color palette ramp. Base Camp deliberately preserves
// that exact mapping rather than inventing a new roster palette.
unsigned char base_camp_family_text_color(short family);

// §2.5 header line A right block: "GOLD {n}" (clipped to the 11-char block).
std::string format_base_camp_gold_label(const SaveData& save);

// §2.5 header line B, solo shape: "SCEN {n}: {title}  DEP {dep}/{total}",
// title clipped so the whole line fits the 34-char budget.
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

// Client-side machine grouping of the replicated lobby players. Seat
// naming convention (the wire's find_local_seats contract): seat 0 keeps
// the machine's base name, seat k is "base#k" — so the machine key is the
// name up to the first '#'.
std::string_view lobby_player_machine_key(std::string_view player_name);

// READY n/m over NON-HOST machines only (a multi-seat host's extra seats
// are is_host=false but never gate the start — §4.3; the server excludes
// them by peer, this mirrors it by machine key). A machine counts ready
// when ALL of its seats are ready.
struct BaseCampReadyCounts {
    int ready = 0;
    int machines = 0;
};
BaseCampReadyCounts count_base_camp_ready_machines(
    const std::vector<og::sim::LobbyPlayer>& players);

// §9.12 (G5) lobby census over the replicated players: machines = distinct
// machine keys (the HOST machine included, unlike BaseCampReadyCounts),
// players = total seats.
struct BaseCampSessionCensus {
    int machines = 0;
    int players = 0;
};
BaseCampSessionCensus count_base_camp_session_census(
    const std::vector<og::sim::LobbyPlayer>& players);

// The host machine's human identity for the joiner's "HOST:" display:
// its company name (players carry machine-generated "net-<hex>" wire names),
// falling back to the machine key when the company is empty — the
// format_go_blockers naming convention. Clipped to the 16-char COMPANY
// column budget. Empty until a host seat exists in the replicated state.
std::string base_camp_host_display_name(
    const std::vector<og::sim::LobbyPlayer>& players);

// §9.12 header line B, networked HEALTHY shape — the G5 session status
// (role + room code + census), clipped to the 42-char line-B band:
//   host:   "HOSTING <ROOM> - <n> MACH / <p> PLYR"  (no room: the census
//           alone after HOSTING)
//   joiner: "IN <ROOM> - HOST: <name16>"  (no room: "JOINED - HOST: ...";
//           host not yet known: the room half alone)
// Room codes display-clip at 12 chars (relay codes are "GLAD-XXXX").
std::string format_base_camp_session_status(
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players);

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
    const std::vector<og::sim::LobbyPlayer>& players);

// --- §2.6 GO / READY slot (the base-camp dual-role button) ---

// The six presentation states of the shared (244,178,68,18) slot: the host
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
// means this machine contributes ZERO character slots to the lobby
// (numplayers==0 spectator seat or an empty roster): such machines ready
// freely [NET-R9]. `cross_control` ON removes the per-machine deploy
// minimum for the client ready gate; the global >= 1 rule (host states)
// always applies. Solo/local (`networked` false) never consults ready and
// keeps the plain grey GO byte-identical (states 1-2).
ReadyGoPresentation format_ready_go_button(bool networked,
                                           bool is_host,
                                           bool my_ready,
                                           bool all_other_machines_ready,
                                           int global_deployed,
                                           int own_deployed,
                                           bool cross_control,
                                           bool spectator);

// §2.6 state-3 popup body: the not-ready machines' company names (machine
// key = seat-name convention; a machine's company = its first seat's
// non-empty company, falling back to the machine key), one per line,
// clipped to 26 chars, at most 4 lines with an "AND n MORE" tail.
std::string format_go_blockers(
    const std::vector<og::sim::LobbyPlayer>& players);

// §2.7 cross-control toggle label: "CTRL: OWN" (only the owner machine
// controls its characters) / "CTRL: ALL" (players may control others'
// characters in-level). Shared by the SDL TEAMS row and the curses lobby
// status line.
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

// Display-only guy copy of a replicated foreign character (derived-stat
// input + family chip). NOT the roster-assembly builder — no id/teamnum
// semantics, never enters a save or a level.
std::unique_ptr<guy> make_base_camp_display_guy(
    const og::sim::LobbyCharacterData& character);

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

// --- Company screens: label formatters (design §2.2/§2.3) ---
// (The §2.2 "file: <slug>.gtl" preview formatter was DELETED — §9.3/F2:
// the filename teaches nothing; companies are fully managed in-game on
// every client. Slug derivation itself stays og::data::derive_company_slot.)

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
