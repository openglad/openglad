#pragma once
#include <array>
#include <cstdint>

// Menu loop return value flags (bitmask).
inline constexpr int32_t MENU_EXIT   = 1;
inline constexpr int32_t MENU_REDRAW = 2;
inline constexpr int32_t MENU_OK     = 4;

inline constexpr int32_t BUTTON_HEIGHT = 15;

// Networking menu enclosing panel frame. Wide enough to contain the longest
// copy (the centered instruction lines) with margin. draw_button() takes
// corners (x1,y1,x2,y2); draw_button_inverted() takes (x,y,WIDTH,HEIGHT), so the
// background is derived from these corners and can never disagree with the
// border.
inline constexpr int32_t PICKER_NETWORKING_FRAME_X1 = 18;
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y1 = 14;
inline constexpr int32_t PICKER_NETWORKING_FRAME_X2 = 302;
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y2 = 184;
inline constexpr int32_t PICKER_NETWORKING_FRAME_BORDER = 6;
inline constexpr int32_t PICKER_NETWORKING_TITLE_Y = 26;

inline constexpr int32_t PICKER_NETWORKING_FIELD_X = 128;
inline constexpr int32_t PICKER_NETWORKING_FIELD_WIDTH = 132;
inline constexpr int32_t PICKER_NETWORKING_FIELD_Y = 44;
inline constexpr int32_t PICKER_NETWORKING_FIELD_PITCH = 22;
inline constexpr int32_t PICKER_NETWORKING_ACTION_Y = 156;
inline constexpr int32_t PICKER_NETWORKING_ACTION_WIDTH = 74;
inline constexpr int32_t PICKER_NETWORKING_LABEL_GAP = 8;
inline constexpr int32_t PICKER_NETWORKING_INSTRUCTION_GAP = 8;

// Forward declare button for menu descriptor arrays.
struct button;

// Per-session mutable button descriptors (Phase 12).
button* picker_mainmenu_buttons();
int picker_mainmenu_button_count();
button* picker_createmenu_buttons();
int picker_createmenu_button_count();
button* picker_viewteam_buttons();
int picker_viewteam_button_count();
button* picker_saveteam_buttons();
int picker_saveteam_button_count();
button* picker_loadteam_buttons();
int picker_loadteam_button_count();

button* picker_main_options_buttons();
int picker_main_options_button_count();
button* picker_control_options_buttons();
int picker_control_options_button_count();
button* picker_trainmenu_buttons();
int picker_trainmenu_button_count();
button* picker_hiremenu_buttons();
int picker_hiremenu_button_count();
button* picker_networking_buttons();
int picker_networking_button_count();
button* picker_teamsmenu_buttons();
int picker_teamsmenu_button_count();
button* picker_viewscenario_buttons();
int picker_viewscenario_button_count();
button* picker_scenariomenu_buttons();
int picker_scenariomenu_button_count();

// --- Team-build layout contract -------------------------------------------
// 3x3 grid: VIEW/TRAIN/HIRE (y=70), LOAD/SAVE/GO (y=100),
// BACK | SCENARIO | NETWORKING (y=140). GO is the only host-gated button.
inline constexpr int kCreateMenuHireIndex = 2;
inline constexpr int kCreateMenuSaveIndex = 4;
inline constexpr int kCreateMenuGoIndex = 5;
inline constexpr int kCreateMenuBackIndex = 6;
inline constexpr int kCreateMenuScenarioIndex = 7;
inline constexpr int kCreateMenuNetworkingIndex = 8;
inline constexpr int kCreateMenuButtonCount = 9;

// --- SCENARIO subscreen layout contract ------------------------------------
// Positional indices into k_scenariomenu_buttons / picker_scenariomenu_buttons().
// SET CAMPAIGN / SET LEVEL keep their host-only visibility here (per-frame
// sync_scenario_menu_host_control_visibility); the rest are always visible.
inline constexpr int kScenarioMenuBackIndex = 0;
inline constexpr int kScenarioMenuSetCampaignIndex = 1;
inline constexpr int kScenarioMenuSetLevelIndex = 2;
inline constexpr int kScenarioMenuViewScenarioIndex = 3;
inline constexpr int kScenarioMenuTeamsIndex = 4;
inline constexpr int kScenarioMenuProgressIndex = 5;
inline constexpr int kScenarioMenuButtonCount = 6;

// --- TEAMS subscreen layout contract --------------------------------------
// Positional indices into k_teamsmenu_buttons / picker_teamsmenu_buttons().
inline constexpr int kTeamsMenuBackIndex = 0;
inline constexpr int kTeamsMenuCtfTeamsIndex = 1;
inline constexpr int kTeamsMenuCtfCapsIndex = 2;
inline constexpr int kTeamsMenuJoinFirstIndex = 3; // join_team_0..3 = 3..6
inline constexpr int kTeamsMenuGuyPrevIndex = 7;
inline constexpr int kTeamsMenuGuyNextIndex = 8;
inline constexpr int kTeamsMenuGuyTeamIndex = 9;
inline constexpr int kTeamsMenuReadyIndex = 10;
inline constexpr int kTeamsMenuCtfTroopsIndex = 11;
inline constexpr int kTeamsMenuPageFirstIndex = 12; // team_page_0..3 = 12..15
inline constexpr int kTeamsMenuButtonCount = 16;

// One frame's visibility state for the TEAMS subscreen. Keyboard nav does not
// skip hidden buttons, so the nav graph is rewired from this state every
// frame (picker_wire_teams_menu_nav) instead of routing around statically.
struct TeamsMenuWiring
{
    bool show_ctf = false;        // CTF campaign + lobby host
    bool networked = false;       // genuine networked session (READY shown)
    bool guy_row = false;         // local session with a non-empty roster
    std::array<bool, 4> join_visible = {false, false, false, false};
    // Per-team member pager ('>' at the row's right edge): shown only when
    // the team's detail line does not fit one slice.
    std::array<bool, 4> pager_visible = {false, false, false, false};
};

// Deterministically rewires the TEAMS subscreen nav graph so every visible
// button is keyboard-reachable and no link points at a hidden button.
void picker_wire_teams_menu_nav(button* buttons, int count,
                                const TeamsMenuWiring& wiring);

// Conditional rewiring for the host-gated buttons (same convention: nav
// never links to a hidden button). Team build gates GO; the SCENARIO
// subscreen gates SET CAMPAIGN / SET LEVEL.
void picker_wire_team_build_nav(button* buttons, int count,
                                bool host_controls_visible);
void picker_wire_scenario_menu_nav(button* buttons, int count,
                                   bool host_controls_visible);

// The TEAMS subscreen's selected roster slot, normalized onto an occupied
// slot (-1 when the roster is empty).
int teams_menu_selected_guy_slot();

// --- VIEW LEVEL (scenario viewer) layout contract --------------------------
inline constexpr int kViewScenarioBackIndex = 0;
inline constexpr int kViewScenarioPrevIndex = 1;
inline constexpr int kViewScenarioNextIndex = 2;
inline constexpr int kViewScenarioRowsPerPage = 23;
