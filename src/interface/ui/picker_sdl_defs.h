#pragma once
#include <array>
#include <cstddef>
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
inline constexpr int32_t PICKER_NETWORKING_FRAME_BORDER = 6;
inline constexpr int32_t PICKER_NETWORKING_TITLE_Y = 26;

inline constexpr int32_t PICKER_NETWORKING_FIELD_X = 128;
inline constexpr int32_t PICKER_NETWORKING_FIELD_WIDTH = 132;
inline constexpr int32_t PICKER_NETWORKING_ACTION_WIDTH = 74;
inline constexpr int32_t PICKER_NETWORKING_LABEL_GAP = 8;
inline constexpr int32_t PICKER_NETWORKING_INSTRUCTION_GAP = 8;

// --- NETWORKING subscreen layout contract -----------------------------------
// Relay-first shape: the ROOM CODE field and the ACTIVE GAMES relay-room list
// lead on both builds. The web build drops the direct JOIN IP / PORT rows and
// the ROOM CODE toggle entirely (an https:// page cannot open ws:// LAN
// sockets, so the relay is the only web join path); native keeps them below
// the list under a "DIRECT (LAN)" header. Positional indices differ per build
// but every surviving id is stable: network_back / network_room_value /
// network_host / network_join (+ native network_ip / network_port /
// network_room_toggle), and the list rows are network_room_0..4.
inline constexpr int kNetworkingMenuRoomSlots = 5;
#ifdef __EMSCRIPTEN__
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y2 = 190;
inline constexpr int32_t PICKER_NETWORKING_ACTION_Y = 168;
inline constexpr int kNetworkingMenuBackIndex = 0;
inline constexpr int kNetworkingMenuRoomValueIndex = 1;
inline constexpr int kNetworkingMenuHostIndex = 2;
inline constexpr int kNetworkingMenuJoinIndex = 3;
inline constexpr int kNetworkingMenuRoomFirstIndex = 4;

// Wider room-code field so the tap prompt placeholder fits (6px/char).
inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_X = 110;
inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_WIDTH = 160;
inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_Y = 40;
inline constexpr int32_t PICKER_NETWORKING_ROOMS_HEADER_Y = 61;
inline constexpr int32_t PICKER_NETWORKING_ROOM_X = 40;
inline constexpr int32_t PICKER_NETWORKING_ROOM_WIDTH = 240;
// A full-size bevel is easier to tap on a narrow portrait phone. Five rows
// still leave a clean gap for the guidance and action row below.
inline constexpr int32_t PICKER_NETWORKING_ROOM_H = 15;
inline constexpr int32_t PICKER_NETWORKING_ROOM_Y = 70;
inline constexpr int32_t PICKER_NETWORKING_ROOM_PITCH = 16;
#else
inline constexpr int32_t PICKER_NETWORKING_FRAME_Y2 = 184;
inline constexpr int32_t PICKER_NETWORKING_ACTION_Y = 156;
inline constexpr int kNetworkingMenuBackIndex = 0;
inline constexpr int kNetworkingMenuIpIndex = 1;
inline constexpr int kNetworkingMenuPortIndex = 2;
inline constexpr int kNetworkingMenuToggleIndex = 3;
inline constexpr int kNetworkingMenuRoomValueIndex = 4;
inline constexpr int kNetworkingMenuHostIndex = 5;
inline constexpr int kNetworkingMenuJoinIndex = 6;
inline constexpr int kNetworkingMenuRoomFirstIndex = 7;

inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_X =
    PICKER_NETWORKING_FIELD_X;
inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_WIDTH =
    PICKER_NETWORKING_FIELD_WIDTH;
inline constexpr int32_t PICKER_NETWORKING_ROOM_CODE_Y = 36;
inline constexpr int32_t PICKER_NETWORKING_ROOMS_HEADER_Y = 54;
inline constexpr int32_t PICKER_NETWORKING_ROOM_X = 30;
inline constexpr int32_t PICKER_NETWORKING_ROOM_WIDTH = 252;
inline constexpr int32_t PICKER_NETWORKING_ROOM_H = 10;
inline constexpr int32_t PICKER_NETWORKING_ROOM_Y = 64;
inline constexpr int32_t PICKER_NETWORKING_ROOM_PITCH = 11;
inline constexpr int32_t PICKER_NETWORKING_DIRECT_HEADER_Y = 121;
inline constexpr int32_t PICKER_NETWORKING_DIRECT_IP_Y = 131;
inline constexpr int32_t PICKER_NETWORKING_DIRECT_ROW2_Y = 143;
inline constexpr int32_t PICKER_NETWORKING_DIRECT_FIELD_H = 10;
inline constexpr int32_t PICKER_NETWORKING_PORT_WIDTH = 60;
inline constexpr int32_t PICKER_NETWORKING_TOGGLE_X = 196;
inline constexpr int32_t PICKER_NETWORKING_TOGGLE_WIDTH = 86;
#endif
inline constexpr int kNetworkingMenuButtonCount =
    kNetworkingMenuRoomFirstIndex + kNetworkingMenuRoomSlots;
// Room-row label budget (6px/char, centered with no clipping).
inline constexpr std::size_t kNetworkingMenuRoomLabelChars = 39;

struct button;

// Deterministically rewires the NETWORKING subscreen nav graph for the
// current number of visible ACTIVE GAMES rows (0..kNetworkingMenuRoomSlots):
// every visible button stays keyboard-reachable and no link ever points at a
// hidden room row.
void picker_wire_networking_menu_nav(button* buttons, int count,
                                     int visible_rooms);

// CONTROLS subscreen header text position. Must clear the BACK button's
// animated keyboard highlight (which extends up to 3px beyond the bevel) and
// stay above the first player row; pinned by test_menu_layout.
inline constexpr int32_t PICKER_CONTROLS_HEADER_X = 10;
inline constexpr int32_t PICKER_CONTROLS_HEADER_Y = 28;

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
button* picker_display_settings_buttons();
int picker_display_settings_button_count();
int picker_main_options_button_count();
button* picker_control_options_buttons();
int picker_control_options_button_count();
button* picker_gameplay_fx_options_buttons();
int picker_gameplay_fx_options_button_count();
button* picker_ui_fx_options_buttons();
int picker_ui_fx_options_button_count();
button* picker_graphics_fx_options_buttons();
int picker_graphics_fx_options_button_count();
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
button* picker_difficulty_menu_buttons();
int picker_difficulty_menu_button_count();

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

// --- GRAPHICS FX subscreen layout contract ----------------------------------
// Positional index of the depth-selector cycle row (id "depth_fx") in
// k_graphics_fx_options_buttons / picker_graphics_fx_options_buttons()
// (BACK = 0, the twelve grid entries are 1..12). change_depth_fx() writes
// the row's label by this index on both surfaces.
inline constexpr int kGraphicsFxDepthFxIndex = 8;

// --- DISPLAY subscreen layout contract ---------------------------------------
// Rows of k_display_settings_buttons / picker_display_settings_buttons(). The
// display_settings_options() loop addresses labels and platform nav rewires
// through these; keep them in lockstep with the table.
inline constexpr int kDisplayMenuBackIndex = 0;
inline constexpr int kDisplayMenuModeIndex = 1;
inline constexpr int kDisplayMenuResolutionIndex = 2;
inline constexpr int kDisplayMenuOverscanMinusIndex = 3;
inline constexpr int kDisplayMenuOverscanPlusIndex = 4;
inline constexpr int kDisplayMenuZoomIndex = 5;
inline constexpr int kDisplayMenuSmoothingIndex = 6;

// --- DIFFICULTY subscreen layout contract -----------------------------------
// Positional indices into k_difficulty_menu_buttons /
// picker_difficulty_menu_buttons(). Single 140px column (23-char label budget
// at 6px/char); every row's label is re-derived per frame from session/save.
inline constexpr int kDifficultyMenuBackIndex = 0;
inline constexpr int kDifficultyMenuDifficultyIndex = 1;
inline constexpr int kDifficultyMenuRespawnModeIndex = 2;
inline constexpr int kDifficultyMenuRespawnDelayIndex = 3;
inline constexpr int kDifficultyMenuPermadeathIndex = 4;
inline constexpr int kDifficultyMenuGeneratorRateIndex = 5;
inline constexpr int kDifficultyMenuButtonCount = 6;

// Per-frame host gating for the DIFFICULTY subscreen: every settings row is
// LobbySettings-backed (difficulty included), so a non-host joiner sees only
// BACK; BACK's vertical cycle is rewired so nav never targets a hidden row.
void sync_difficulty_menu_visibility(button* buttons, int num_buttons,
                                     int& highlighted_button);

// A remote (host) GO landing while this peer is parked on the main menu or
// one of its subscreens: select the Main CONTINUE item and exit, so the
// shared picker state machine re-enters team build, whose loop-top
// remote-start check launches the game (team_build_remote_start_requested's
// main-menu-scope sibling). Returns true and sets retvalue to MENU_EXIT when
// the start should preempt the current loop.
bool picker_main_scope_remote_start_requested(int32_t& retvalue);
