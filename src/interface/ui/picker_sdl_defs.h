#pragma once
#include <openglad/interface/ui/picker_common.h>

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
// LINEUP §6: the session-mode DISCONNECT row, appended after the room rows
// (index contract: growth is append-only). It is drawn in JOIN's exact rect —
// the two are never visible in the same mode, so the shared cell reads as one
// stable action slot.
inline constexpr int kNetworkingMenuDisconnectIndex =
    kNetworkingMenuRoomFirstIndex + kNetworkingMenuRoomSlots;
inline constexpr int kNetworkingMenuButtonCount =
    kNetworkingMenuDisconnectIndex + 1;
// Room-row label budget (6px/char, centered with no clipping).
inline constexpr std::size_t kNetworkingMenuRoomLabelChars = 39;
// Machine-row (PLAYERS list) label budget: the centered no-clip rule over the
// room-row face — (w-8)/6 = 38 on the web's 240px face; native's 252px face
// keeps the established 39-char room budget.
#ifdef __EMSCRIPTEN__
inline constexpr std::size_t kNetworkingMenuMachineRowLabelChars = 38;
#else
inline constexpr std::size_t kNetworkingMenuMachineRowLabelChars = 39;
#endif

// Forward declare button for menu descriptor arrays.
struct button;

// Deterministically rewires the NETWORKING subscreen nav graph for the
// current number of visible list rows (0..kNetworkingMenuRoomSlots): every
// visible button stays keyboard-reachable and no link ever points at a
// hidden row. Idle (`networked_session` false): the classic ACTIVE GAMES
// graph. Session (`networked_session` true, LINEUP §6): the PLAYERS graph —
// room-code value, the machine rows, BACK | DISCONNECT; HOST/JOIN and the
// native DIRECT (LAN) fields are hidden and never linked.
void picker_wire_networking_menu_nav(button* buttons, int count,
                                     int visible_rooms,
                                     bool networked_session = false);

// LINEUP §6: one frame's NETWORKING subscreen mode, derived from the lobby
// client ({Idle, Hosting, Joined} — Hosting/Joined = `networked` with `host`
// telling them apart). `list_rows` counts the visible list rows (relay rooms
// when idle, lobby machines in a session); `row_actionable[slot]` marks the
// session rows the host may kick (a foreign, non-host machine). Kept as a
// plain value so layout tests can drive every variant without a lobby.
struct NetworkingMenuModeState
{
    bool networked = false;
    bool host = false;
    int list_rows = 0;
    std::array<bool, kNetworkingMenuRoomSlots> row_actionable{};
};

// The per-frame mode sync for the static NETWORKING table (LINEUP §6):
// writes every mode-dependent hidden flag and action id (deterministic
// full-write — no variant inherits a stale one), makes inert rows
// keyboard-dead (myfun = 0), and rewires the graph through
// picker_wire_networking_menu_nav. The label pass and the live-vbutton
// mirror stay in configure_networking's sync (they read the live session).
void picker_apply_networking_menu_mode(button* buttons, int count,
                                       const NetworkingMenuModeState& mode);

// LINEUP §6: the per-frame kicked-joiner revert. When the OWNED lobby client
// (the one SdlPickerClient registered) is the active client and has latched
// was_kicked(), swap in a fresh local client re-initialized from the save
// (the picker_replace_lobby_client teardown/install order) and say why
// (popup "KICKED BY HOST"; trace "networking"). Inert when a test stub is
// installed as the active client. Returns true when a swap happened.
bool picker_revert_lobby_client_if_kicked();

// Per-session mutable button descriptors (Phase 12).
button* picker_mainmenu_buttons();
int picker_mainmenu_button_count();
button* picker_seat_settings_buttons();
int picker_seat_settings_button_count();
// GAME SETTINGS' materialized main-menu index, derived from the spec
// (replaces the retired OPTIONS_BUTTON_INDEX #defines). The legacy function
// name remains as an internal compatibility seam.
int picker_mainmenu_options_index();
button* picker_createmenu_buttons();
int picker_createmenu_button_count();

button* picker_main_options_buttons();
button* picker_display_settings_buttons();
int picker_display_settings_button_count();
int picker_main_options_button_count();
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
button* picker_viewscenario_buttons();
int picker_viewscenario_button_count();
button* picker_progressmenu_buttons();
int picker_progressmenu_button_count();
button* picker_scenariomenu_buttons();
int picker_scenariomenu_button_count();
button* picker_difficulty_menu_buttons();
int picker_difficulty_menu_button_count();
// §2.2 new-company name entry (Layer F engine screen).
button* picker_name_entry_buttons();
int picker_name_entry_button_count();
// §2.3 Company List (Load) — Layer F engine screen.
button* picker_company_list_buttons();
int picker_company_list_button_count();
// §2.4 Backups sub-view (per company) — Layer F engine screen.
button* picker_company_backups_buttons();
int picker_company_backups_button_count();
// #155 CLOUD SAVE subscreen — engine screen.
button* picker_cloud_save_buttons();
int picker_cloud_save_button_count();
// #168 full-screen HELP — engine screen.
button* picker_help_buttons();
int picker_help_button_count();
// Campaign zone submenu (the scripted page chassis) — engine screen.
button* picker_zone_submenu_buttons();
int picker_zone_submenu_button_count();
// LINEUP + FIGHTERS (docs/lineup-design.md §2) — engine screens.
button* picker_lineup_buttons();
int picker_lineup_button_count();
button* picker_lineup_fighters_buttons();
int picker_lineup_fighters_button_count();

// --- Base camp (team build) layout contract (design §2.5 as amended §9.5,
// regridded §9.10) -----------------------------------------------------------
// The command roster at the §9.10.1 grid (round 2: the roster block gains
// clear top/bottom margins): 8 roster rows/page (deploy toggle at x=23, team
// color/cycler at x=61, row-body train zone at x=84..297, and move-up at
// x=303, y=45+14r — flush with the panel's right inner edge, like the roster
// pager '>', the seat rail's '>' and GO), the page cluster top-right,
// and the bottom command strip BACK | DIFFICULTY | SCENARIO | NETWORK |
// GO at y=178 (HIRE moved up to the roster band header; the slot it left
// became the DIFFICULTY door when that setting came off the main menu, and
// the strip re-gridded so the full word inks). Spec ordinals group by kind
// so the MenuSpecRow arg (== ordinal, G3)
// decodes as row/kind directly. GO is the only host-gated button. Cap-24
// roster = 3 pages; the 40-slot defensive shape pages to 5.
inline constexpr int kBaseCampRosterRowsPerPage = 8; // roster_dep_r = 0..7
// §9.11 (G4): the per-row TRAIN column is deleted; the row BODY is the train
// affordance — roster_row_r = 8+r, a no_draw hit zone across the row text that
// opens the train screen seeded on that character (Enter trains from the
// keyboard row highlight — the curses roster grammar).
inline constexpr int kBaseCampRowBodyBase = 8;       // roster_row_r = 8+r
// The team-colored chip is a solo-only cycler. Multiplayer still renders
// the color but hides its mutation target because lobby teams are assigned.
inline constexpr int kBaseCampTeamChipBase = 16;     // roster_team_r = 16+r
inline constexpr int kBaseCampPagePrevIndex = 24;
inline constexpr int kBaseCampPageNextIndex = 25;
// Solo-only no-draw hit zone over the visible SCEN status line.
inline constexpr int kBaseCampScenarioLineIndex = 26;
inline constexpr int kCreateMenuBackIndex = 27;
inline constexpr int kCreateMenuHireIndex = 28;
inline constexpr int kCreateMenuScenarioIndex = 29;
inline constexpr int kCreateMenuNetworkingIndex = 30;
inline constexpr int kCreateMenuGoIndex = 31;
// §2.6: the READY twin shares GO's exact rect (262,178,50,18); exactly one
// of the pair is visible per frame (host => GO, networked joiner => READY).
inline constexpr int kCreateMenuReadyIndex = 32;
// Per-level player-seat assignment rail. Appended after the original
// ordinals so every established Base Camp action index stays stable.
// The rail is THIS MACHINE'S four seat slots, 35..38 — slot k IS local
// controller profile k. A slot with no seat in it is an ADD PLAYER button
// on the same ordinal, so a placeholder needs no ordinal of its own.
//
// PARKED. Ordinals 33, 34 and 39 carried the [+] and the two seat pagers
// before remote seats left the rail. Ordinals are append-only — a removed
// row would shift all 40 rows after it — so the three park exactly like
// ordinal 40 has since #236: zero-size rect, empty label, hidden, no nav,
// re-parked every frame by the rewire. Their names keep the history.
inline constexpr int kBaseCampAddSeatIndex = 33;
inline constexpr int kBaseCampSeatPagePrevIndex = 34;
inline constexpr int kBaseCampSeatCardBase = 35; // seat_card_0..3 = 35..38
inline constexpr int kBaseCampSeatPageNextIndex = 39;
inline constexpr int kBaseCampSeatRailSpareIndex = 40;
// The four parked rail ordinals, in table order, for the rewire's per-frame
// re-park (and the layout test's spare sweep).
inline constexpr std::array<int, 4> kBaseCampSeatRailSpares{
    kBaseCampAddSeatIndex, kBaseCampSeatPagePrevIndex,
    kBaseCampSeatPageNextIndex, kBaseCampSeatRailSpareIndex};
// Per-row move-up controls are appended so every established Base Camp
// ordinal remains stable. Only owned rows after the first owned roster member
// expose one; activating it swaps that member with its predecessor.
inline constexpr int kBaseCampMoveUpBase = 41; // roster_up_0..7 = 41..48
inline constexpr int kBaseCampSeatCardsPerPage = 4;
// The 70px seat card is drawn without the beveled inset, so its budget is the
// full face: 70 / 6 = 11 characters, a seat label's two trailing pads
// included. Eleven is what makes "ADD PLAYER" and "LOBBY FULL" (10 each) fit
// a bare slot, and what leaves the widest card ("P16 " + a 5-char mapping
// short name + both pads) exactly filling its face.
inline constexpr int kBaseCampSeatCardLabelBudget = 11;
// The compact seat-rail/move-up band's ordinal bounds: draw_menu_highlight
// keeps its interior focus ring INSIDE this range so a card's ring can never
// paint over its numbered team chip — appended zone rows past it take the
// normal exterior ring.
inline constexpr int kBaseCampInteriorRingFirstIndex = kBaseCampAddSeatIndex;
inline constexpr int kBaseCampInteriorRingLastIndex = 48;
namespace og::ui {
// True when rail slot k (0..3) is a SEAT CARD this frame — the only shape
// that packs a numbered team chip into the face's right end, and so the only
// one whose focus ring needs the chip clearance. A bare slot's ADD PLAYER /
// LOBBY FULL label is ten glyphs with no trailing pad: its ink runs to x+64,
// and a ring cut back over the chip zone would strike through it. Answers
// with the same count the chip pass paints, so the ring and the chip can
// never disagree about which slots have one.
bool base_camp_rail_slot_has_chip(int slot);
// Pixels at a seat card's right end the focus ring must give up so its
// (inclusive, +1-convention) right column lands on the last face pixel BEFORE
// the chip. Derived from the card width and the chip offset — a geometry
// change moves the ring with the chip instead of stranding a literal here.
int base_camp_seat_chip_ring_clearance();
} // namespace og::ui
// --- The appended gameplay-zone band (docs/basecamp-zones-design.md
// "Bounds arithmetic"): ordinals 49..71, statically PARKED at zero-size
// rects with empty labels (gate-lattice safe) and re-banded per frame by the
// rewire when a composition uses them. 16 action rows split 8 per actions
// widget (zone widget w, window row r -> ordinal base + 8w + r), one
// prev/next pager pair per actions widget, 3 spares.
inline constexpr int kBaseCampZoneActionBase = 49;    // zone_action_0..15
inline constexpr int kBaseCampZoneActionsPerWidget = 8;
inline constexpr int kBaseCampZoneActionRows = 16;
inline constexpr int kBaseCampZonePagerBase = 65;     // prev/next per widget
inline constexpr int kBaseCampZonePagerCount = 4;
inline constexpr int kBaseCampZoneSpareBase = 69;     // zone_spare_0..2
inline constexpr int kBaseCampZoneSpareCount = 3;
// The DIFFICULTY strip door (docs/camp-controls-design.md). Appended past the
// zone band rather than carved out of a spare: every established ordinal
// stays where it is, and the three spares stay a reserve. Its rect lives on
// the y=178 strip with the other doors — the ordinal ordering is the table's
// growth history, not the reading order.
inline constexpr int kCreateMenuDifficultyIndex = 72;
inline constexpr int kCreateMenuButtonCount = 73;

// --- LOCAL SEAT SETTINGS subscreen -----------------------------------------
// A clicked owned Base Camp seat resolves its stable LobbySeatId to a dense
// machine-local controller profile each frame. Multiplayer-disabled builds
// omit REMOVE/SPECTATE but keep the first five ordinals unchanged.
inline constexpr int kSeatSettingsBackIndex = 0;
inline constexpr int kSeatSettingsTeamIndex = 1;
inline constexpr int kSeatSettingsModeIndex = 2;
inline constexpr int kSeatSettingsRemapIndex = 3;
inline constexpr int kSeatSettingsResetIndex = 4;
inline constexpr int kSeatSettingsRemoveIndex = 5;
// The INPUT cycler (design §2.2) is APPENDED so ordinals 0..5 and the
// default_highlight stay put. Its MenuSpecRow arg is 6 in BOTH tables; the
// multiplayer-disabled table has no REMOVE row, so the same row materializes
// one slot earlier there. kSeatSettingsInputRow is the position the label
// sync and the nav links use; kSeatSettingsInputIndex is the dispatch arg.
inline constexpr int kSeatSettingsInputIndex = 6;
inline constexpr int kSeatSettingsInputRowMP = 6;
inline constexpr int kSeatSettingsInputRowNoMP = 5;
#ifndef DISABLE_MULTIPLAYER
inline constexpr int kSeatSettingsInputRow = kSeatSettingsInputRowMP;
#else
inline constexpr int kSeatSettingsInputRow = kSeatSettingsInputRowNoMP;
#endif
// §7.1 unified player screen: ZOOM (kPlayerScreenColBX, kPlayerScreenBand2Y)
// and the RADAR/HP/FOES/SCORE stack (kPlayerScreenColCX, kPlayerScreenHudTopY
// + 22px pitch) are APPENDED (index contract). Dispatch args are the MP row
// positions; the no-MP table has no REMOVE row, so each appended row
// materializes one slot earlier (the seat_input precedent).
inline constexpr int kSeatSettingsZoomIndex = 7;
inline constexpr int kSeatSettingsHudRadarIndex = 8;
inline constexpr int kSeatSettingsHudLifeIndex = 9;
inline constexpr int kSeatSettingsHudFoesIndex = 10;
inline constexpr int kSeatSettingsHudScoreIndex = 11;
// Both build variants of each table exist on every build (tests pin the
// uncompiled shape), so nav links inside each table use its EXPLICIT row
// positions; the unsuffixed aliases select the compiled variant for the
// label sync and dispatch.
inline constexpr int kSeatSettingsZoomRowMP = 7;
inline constexpr int kSeatSettingsHudRadarRowMP = 8;
inline constexpr int kSeatSettingsHudLifeRowMP = 9;
inline constexpr int kSeatSettingsHudFoesRowMP = 10;
inline constexpr int kSeatSettingsHudScoreRowMP = 11;
inline constexpr int kSeatSettingsZoomRowNoMP = 6;
inline constexpr int kSeatSettingsHudRadarRowNoMP = 7;
inline constexpr int kSeatSettingsHudLifeRowNoMP = 8;
inline constexpr int kSeatSettingsHudFoesRowNoMP = 9;
inline constexpr int kSeatSettingsHudScoreRowNoMP = 10;
#ifndef DISABLE_MULTIPLAYER
inline constexpr int kSeatSettingsZoomRow = kSeatSettingsZoomRowMP;
inline constexpr int kSeatSettingsHudRadarRow = kSeatSettingsHudRadarRowMP;
inline constexpr int kSeatSettingsHudLifeRow = kSeatSettingsHudLifeRowMP;
inline constexpr int kSeatSettingsHudFoesRow = kSeatSettingsHudFoesRowMP;
inline constexpr int kSeatSettingsHudScoreRow = kSeatSettingsHudScoreRowMP;
#else
inline constexpr int kSeatSettingsZoomRow = kSeatSettingsZoomRowNoMP;
inline constexpr int kSeatSettingsHudRadarRow = kSeatSettingsHudRadarRowNoMP;
inline constexpr int kSeatSettingsHudLifeRow = kSeatSettingsHudLifeRowNoMP;
inline constexpr int kSeatSettingsHudFoesRow = kSeatSettingsHudFoesRowNoMP;
inline constexpr int kSeatSettingsHudScoreRow = kSeatSettingsHudScoreRowNoMP;
#endif
inline constexpr int kSeatSettingsButtonCountMP = 12;
inline constexpr int kSeatSettingsButtonCountNoMP = 11;

// --- §7.1 unified player-screen grid ---------------------------------------
// One geometry behind BOTH player screens (Base Camp seat settings and the
// pause player screen — the unification is the point, pinned by the
// exact-table tests). Three columns with 6px gutters and a shared right edge
// at 304; two 18px-tall button bands above the binding panel; the
// RADAR/HP/FOES/SCORE stack top-aligns with the panel at y=78 and
// bottom-aligns with it exactly: an 18px-tall button at y=144 inks rows
// 144..161, so the panel's bottom corner is y=161. Vertical rhythm:
// BACK bottom (22) -7- band1 (30..47) -6- band2 (54..71) -6- panel/stack
// (78..161) -7- command band (169..186).
inline constexpr int kPlayerScreenColAX = 12;   // DIRECTION/INPUT/panel/TEAM
inline constexpr int kPlayerScreenColAW = 98;
inline constexpr int kPlayerScreenColBX = 116;  // REMAP/ZOOM; ends 208
inline constexpr int kPlayerScreenColBW = 92;
inline constexpr int kPlayerScreenColCX = 214;  // RESET + HUD stack
inline constexpr int kPlayerScreenColCW = 90;   // ends 304
inline constexpr int kPlayerScreenBandH = 18;
inline constexpr int kPlayerScreenBand1Y = 30;  // MODE | REMAP | RESET
inline constexpr int kPlayerScreenBand2Y = 54;  // INPUT | ZOOM
inline constexpr int kPlayerScreenPanelTopY = 78;
inline constexpr int kPlayerScreenPanelRightX = 208;  // == col B right edge
inline constexpr int kPlayerScreenPanelBottomY = 161; // == HUD stack bottom
inline constexpr int kPlayerScreenHudTopY = 78;
inline constexpr int kPlayerScreenHudPitch = 22;
inline constexpr int kPlayerScreenBottomBandY = 169;  // TEAM / REMOVE
// Binding-panel interior: headers at panel+4, first line at panel+15,
// 8px line pitch (columns kBindingPanel*Chars below).
inline constexpr int kPlayerScreenPanelHeaderY = kPlayerScreenPanelTopY + 4;
inline constexpr int kPlayerScreenPanelLineY = kPlayerScreenPanelTopY + 15;

// --- §7.1 shared per-player HUD/zoom row helpers ---------------------------
// One implementation (pause_menu.cpp) behind both player screens: the seat's
// live viewscreen prefs (viewob[seat]) are the runtime carrier when present
// (in-game pause; Base Camp view 0), cfg is the carrier otherwise (Base Camp
// seats without a live view) and always the persistence mirror. Callers own
// the cfg.save_settings() timing via their existing persist paths.
namespace og::ui {
enum class PlayerHudRow : std::uint8_t { Radar, Life, Foes, Score };
// "RADAR: ON" style live label, re-derived per frame.
std::string player_hud_row_label(int seat, PlayerHudRow row);
// Flip the pref on the seat's viewscreen (immediate: redrawme) + mirror to
// cfg. HP is ON/OFF only: legacy TEXT/BARS/SMALL display as ON and the first
// toggle normalizes to BOTH/OFF.
void toggle_player_hud_row(int seat, PlayerHudRow row);
// "ZOOM: GAME" / "ZOOM: 0.9X".
std::string player_view_zoom_label(int seat);
// Advance GAME -> 0.9 -> .. -> 0.5 -> GAME, skipping steps whose padded
// compositor window cannot fit the layer budget on the current world canvas
// (GAME is always allowed). Returns the new step.
int cycle_player_view_zoom(int seat);
// False when the render backend has no off-screen floor-layer compositor:
// the ZOOM rows show Disabled (never wrong output). Probed once.
bool per_view_zoom_available();
// One shared binding-panel line: "{label}: {value}" clamped to the column
// budget. Movement column x=20 must clear the ACTIONS column at x=104
// (14 chars); the actions column x=104 must clear the panel edge at x=208
// (17 chars). 6px/char, left-aligned, no clipping in the text writer.
inline constexpr int kBindingPanelMovementChars = 14;
inline constexpr int kBindingPanelActionsChars = 17;
std::string format_binding_panel_line(const char* label,
                                      const std::string& value,
                                      int budget_chars);
} // namespace og::ui

// --- SCENARIO subscreen layout contract ------------------------------------
// Positional indices into k_scenariomenu_buttons / picker_scenariomenu_buttons().
// SET CAMPAIGN / SET LEVEL / TROOPS keep their host-only visibility here and
// the re-homed TEAMS / LIMIT rows their versus-only visibility (per-frame
// sync_scenario_menu_host_control_visibility); BACK / VIEW LEVEL / PROGRESS
// / LINEUP are always visible (LINEUP is deliberately NOT host-gated:
// joiners open it read-only — docs/lineup-design.md §2.3).
inline constexpr int kScenarioMenuBackIndex = 0;
inline constexpr int kScenarioMenuSetCampaignIndex = 1;
inline constexpr int kScenarioMenuSetLevelIndex = 2;
inline constexpr int kScenarioMenuViewScenarioIndex = 3;
// The retired MATCHUP door's ordinal (#218), parked as a spare until the
// LINEUP door (docs/lineup-design.md §2) reclaimed it: the y=100 row now
// reads VIEW LEVEL | PROGRESS | LINEUP with no table growth and every
// index below keeping its value.
inline constexpr int kScenarioMenuLineupIndex = 4;
inline constexpr int kScenarioMenuProgressIndex = 5;
// Appended (index contract: growth is append-only). Host-gated like
// SET CAMPAIGN / SET LEVEL.
inline constexpr int kScenarioMenuTroopsIndex = 6;
// Match Teams / Score Limit, re-homed from MATCHUP (#218): they complete
// the y=140 match-settings band around TROOPS. Versus campaigns only;
// joiners get the read-only label (visible, host-actionable).
inline constexpr int kScenarioMenuCtfTeamsIndex = 7;
inline constexpr int kScenarioMenuCtfCapsIndex = 8;
inline constexpr int kScenarioMenuButtonCount = 9;

// --- Campaign zone submenu (docs/basecamp-zones-design.md) -----------------
// The scripted page chassis opened by page-kind zone action rows, which
// dispatch through the Base Camp's on_spec_row and name their own page (no
// ButtonAction id: nothing opens this at the book ROOT). It is a ROOM
// INSIDE the Base Camp, not a different game's menu: the same header lines
// (COMPANY + GOLD — you can see your purse while you shop), the same panel
// at (8,28)-(311,160), the same message-line toasts. Only the footer band at
// y=169 is its own (title + Escape-hotkeyed BACK + PREV/NEXT), so this
// screen's "back" shares no other screen's geometry (injector
// disambiguation rule).
inline constexpr int kZoneSubmenuRowsPerPage = 8;
inline constexpr int kZoneSubmenuBackIndex = 8;
inline constexpr int kZoneSubmenuPrevIndex = 9;
inline constexpr int kZoneSubmenuNextIndex = 10;
inline constexpr int kZoneSubmenuButtonCount = 11;
// Row faces run the panel's inner width (12..307) on a 12px pitch; the
// narrative lines above them take the panel's header band, so a page with
// many lines shows fewer rows per window rather than overrunning the panel.
inline constexpr int kZoneSubmenuRowX = 12;
inline constexpr int kZoneSubmenuRowWidth = 296;
inline constexpr int kZoneSubmenuRowPitch = 12;
inline constexpr int kZoneSubmenuLineY0 = 33;
// A page with no narrative lines starts its rows on the Base Camp zone's
// own grid anchor, so stepping into a submenu does not shift the eye.
inline constexpr int kZoneSubmenuRowY0 = 45;
inline constexpr int kZoneSubmenuPanelBottomY = 158;
inline constexpr std::size_t kZoneSubmenuRowLabelChars =
    (kZoneSubmenuRowWidth - 8) / 6;  // 48

// --- LINEUP screen layout contract (docs/lineup-design.md §2) --------------
// One engine-hosted screen: title band, FOUR team bands of equal pitch, one
// action strip. The grid is declared here and every rect derives from it
// (the menus discipline: a literal coordinate that appears once is a future
// 1px drift); the layout tests assert the RELATIONS (equal pitch, shared
// column x across bands, strip flush right) as well as the exact table.
// Positional indices into kLineupMenuRows / picker_lineup_buttons():
inline constexpr int kLineupBackIndex = 0;
inline constexpr int kLineupBotsBase = 1;   // lineup_bots_t = 1 + t
inline constexpr int kLineupLevelBase = 5;  // lineup_level_t = 5 + t
inline constexpr int kLineupFightersIndex = 9;
inline constexpr int kLineupSplitEvenIndex = 10;
inline constexpr int kLineupSplitFairIndex = 11;
inline constexpr int kLineupUniteIndex = 12;
inline constexpr int kLineupButtonCount = 13;
// The opaque grey ground painted PRE-buttons (a band of raw backdrop
// between two opaque things reads as a defect — the menus skill).
inline constexpr int kLineupPanelX1 = 8;
inline constexpr int kLineupPanelY1 = 20;
inline constexpr int kLineupPanelX2 = 311;
inline constexpr int kLineupPanelY2 = 170;
// Four bands at band_y(t) = 24 + 36*t (24/60/96/132), 34 px tall each.
inline constexpr int kLineupBandY0 = 24;
inline constexpr int kLineupBandPitch = 36;
inline constexpr int kLineupBandHeight = 34;
constexpr int lineup_band_y(int team)
{
    return kLineupBandY0 + kLineupBandPitch * team;
}
// Header line at y+2: team chip | "TEAM n" | POWER | seat run.
inline constexpr int kLineupHeaderDy = 2;
inline constexpr int kLineupChipX = 12;
inline constexpr int kLineupChipSize = 10;
inline constexpr int kLineupTeamTextX = 26;
inline constexpr int kLineupPowerTextX = 70;
inline constexpr int kLineupSeatRunX = 150;
inline constexpr int kLineupSeatRunRightX = 306;
inline constexpr int kLineupSeatRunChars =
    (kLineupSeatRunRightX - kLineupSeatRunX) / 6;  // 26, then "+n"
// Knob row at y+15: BOTS face | LV face | census/diagnostic text.
inline constexpr int kLineupKnobDy = 15;
inline constexpr int kLineupKnobH = 15;
inline constexpr int kLineupBotsX = 12;
inline constexpr int kLineupBotsW = 80;
inline constexpr int kLineupLevelX = 98;
inline constexpr int kLineupLevelW = 56;
inline constexpr int kLineupCensusX = 162;
inline constexpr int kLineupCensusDy = 19;
// The title band's right-hand slot: an active toast wins it, else the
// networked session census (right-aligned, ending on the panel edge).
inline constexpr int kLineupTitleCensusChars = 40;
// Action strip: BACK | FIGHTERS | SPLIT EVEN | SPLIT FAIR | UNITE, one 6px
// gutter, closing flush on the Base Camp panel rail (x=312).
inline constexpr int kLineupStripY = 176;
inline constexpr int kLineupStripH = 18;
inline constexpr int kLineupStripGap = 6;
inline constexpr int kLineupStripRightX = 312;
inline constexpr int kLineupBackX = 8;
inline constexpr int kLineupBackW = 44;
inline constexpr int kLineupFightersX =
    kLineupBackX + kLineupBackW + kLineupStripGap;              // 58
inline constexpr int kLineupFightersW = 62;
inline constexpr int kLineupSplitEvenX =
    kLineupFightersX + kLineupFightersW + kLineupStripGap;      // 126
inline constexpr int kLineupSplitW = 68;
inline constexpr int kLineupSplitFairX =
    kLineupSplitEvenX + kLineupSplitW + kLineupStripGap;        // 200
inline constexpr int kLineupUniteX =
    kLineupSplitFairX + kLineupSplitW + kLineupStripGap;        // 274
inline constexpr int kLineupUniteW = kLineupStripRightX - kLineupUniteX;  // 38
static_assert(kLineupUniteX + kLineupUniteW == kLineupStripRightX,
              "the action strip closes flush on the panel rail");
static_assert(lineup_band_y(3) + kLineupKnobDy + kLineupKnobH <
                  kLineupPanelY2,
              "the last band's knob row stays inside the panel");

// The lobby's seat picture as LINEUP consumes it. A LOCAL session's lobby
// owns every seat it lists but reports EMPTY local_player_indices by
// contract (picker_lobby_client.h), and an uninitialized local lobby lists
// nothing at all — this helper applies the Base Camp recipe (all seats
// local; synthesize from the save when the lobby is empty) so the bands,
// the knob occupancy check, and the SPLIT actions all read one picture.
struct LineupSeatView {
    std::vector<og::sim::LobbyPlayer> players;  // sorted by player_index
    std::vector<std::uint8_t> local_indices;    // THIS machine's seats
};
LineupSeatView picker_lineup_seat_view();

// --- FIGHTERS list layout contract (docs/lineup-design.md §2.2) ------------
// The Base Camp roster row grid verbatim (menu_screen_specs.cpp shares its
// kBaseCampRow* / column constants with this screen's table — never retyped
// literals): 8 rows/page at y=45+14r, deploy box x=23 w=14, chip x=61, name
// x=88, class x=164, level x=236, POWER where EXP sat at x=264. Footer at
// y=169 (BACK 10 / PREV 220 / NEXT 270 — the zone-submenu footer split, so
// this screen's "back" shares no other screen's geometry).
inline constexpr int kLineupFightersRowsPerPage = 8;
inline constexpr int kLineupFightersDeployBase = 0;  // fighter_dep_r = 0..7
inline constexpr int kLineupFightersBodyBase = 8;    // fighter_row_r = 8..15
inline constexpr int kLineupFightersBackIndex = 16;
inline constexpr int kLineupFightersPrevIndex = 17;
inline constexpr int kLineupFightersNextIndex = 18;
inline constexpr int kLineupFightersButtonCount = 19;

// Conditional rewiring for the host-gated buttons (same convention: nav
// never links to a hidden button). The base camp rewires its full roster
// graph per frame (pattern b — the rewire lives on the spec and reads the
// installed BaseCampScreenState); the SCENARIO subscreen has two visibility
// axes: SET CAMPAIGN / SET LEVEL / TROOPS gate on the host axis, and the
// re-homed TEAMS / LIMIT rows (#218) gate on the versus-campaign axis
// (match_settings_visible) — visible to joiners too, read-only there.
void picker_wire_scenario_menu_nav(button* buttons, int count,
                                   bool host_controls_visible,
                                   bool match_settings_visible);

// The SCENARIO screen's per-frame visibility/label/nav sync (the spec's
// Rewire program): host-gates SET CAMPAIGN / SET LEVEL / TROOPS,
// versus-gates TEAMS / LIMIT (visible read-only for joiners), re-derives
// the three settings labels from the save on both surfaces (LINEUP stays
// visible for everyone), and rewires the graph through
// picker_wire_scenario_menu_nav.
void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button);

// --- TRAIN screen layout contract ------------------------------------------
// Positional index of the team cycler ("Team N") in
// kTrainMenuRows / picker_trainmenu_buttons(). The train content pass and
// the ChangeTeam/cycle callbacks write its live label/outline by this index
// (the G8 sweep of the raw allbuttons_[18] writes when VIEW TEAM retired).
inline constexpr int kTrainMenuChangeTeamIndex = 17;
inline constexpr int kTrainMenuSellIndex = 19;

// --- VIEW LEVEL (scenario viewer) layout contract --------------------------
inline constexpr int kViewScenarioBackIndex = 0;
inline constexpr int kViewScenarioPrevIndex = 1;
inline constexpr int kViewScenarioNextIndex = 2;
// The staged-preview band (#218): the STAGED world renders inside the
// (5,5,314,160) report frame, above the census rows. Classic UI-canvas
// coordinates; the band is ALWAYS reserved (degradation states render
// their text into it — no dynamic page size).
inline constexpr int kViewScenarioFrameX = 5;
inline constexpr int kViewScenarioFrameY = 5;
inline constexpr int kViewScenarioFrameW = 314;
inline constexpr int kViewScenarioFrameH = 160;
inline constexpr int kViewScenarioPreviewBandX = 8;
inline constexpr int kViewScenarioPreviewBandY = 16;
inline constexpr int kViewScenarioPreviewBandW = 303;
inline constexpr int kViewScenarioPreviewBandH = 76;
// Census rows sit below the band at a 6 px pitch; 10 rows end at y=156,
// inside the frame's y extent (5..165).
inline constexpr int kViewScenarioCensusTopY = 96;
inline constexpr int kViewScenarioRowPitch = 6;
inline constexpr int kViewScenarioRowsPerPage = 10;

// --- HELP (full-screen, #168) layout contract --------------------------------
// The screen is three bands: the tab strip (three tab buttons on one
// baseline), the content frame (paged help text), and the command band
// (BACK bottom-left, PREV/NEXT bottom-right on the VIEW LEVEL footer
// geometry, with the key-hint and version strings between them). Every rect
// in the spec and every draw in help.cpp derives from these constants.
inline constexpr int kHelpMenuControlsTabIndex = 0;
inline constexpr int kHelpMenuClassesTabIndex = 1;
inline constexpr int kHelpMenuEditorTabIndex = 2;
inline constexpr int kHelpMenuBackIndex = 3;
inline constexpr int kHelpMenuPrevIndex = 4;
inline constexpr int kHelpMenuNextIndex = 5;
// Tab strip: 70px-wide faces on a 75px pitch from x=5 (the frame's left edge),
// occupying rows kHelpTabY .. kHelpTabY + kHelpTabHeight - 1. The rows between
// the strip and the frame are the connector band (kHelpConnector* below): the
// ACTIVE tab is merged into the frame across it, inactive tabs leave it as
// backdrop.
inline constexpr int kHelpTabY = 2;
inline constexpr int kHelpTabWidth = 70;
inline constexpr int kHelpTabHeight = 15;
inline constexpr int kHelpTabPitch = 75;
// Content frame and text grid: text rows at y = kHelpTextTop + i*7; the
// frame's right edge closes the 50-char flow budget (help_char_budget).
// The tab column derives from the frame's left edge so the strip and the
// frame share that column.
inline constexpr int kHelpFrameLeft = 5;
constexpr int kHelpTabX(int tab) { return kHelpFrameLeft + tab * kHelpTabPitch; }
inline constexpr int kHelpFrameTop = 20;
inline constexpr int kHelpFrameRight = 314;
inline constexpr int kHelpFrameBottom = 160;
// draw_button() paints this many nested bevel rings, so the frame's top bevel
// owns rows kHelpFrameTop .. kHelpFrameTop + kHelpFrameBorder - 1 and its face
// starts on the row below.
inline constexpr int kHelpFrameBorder = 3;
// Folder-tab merge: the active tab and the frame read as one surface. The
// connector repaints from the tab's own bottom bevel row down through the
// frame's last top-bevel row, so both of those edges disappear under the
// active tab (help_engine_draw_content, after the frame is drawn).
inline constexpr int kHelpConnectorTop = kHelpTabY + kHelpTabHeight - 1;
inline constexpr int kHelpConnectorBottom = kHelpFrameTop + kHelpFrameBorder - 1;
inline constexpr int kHelpTextX = 10;
inline constexpr int kHelpTextTop = 28;
inline constexpr int kHelpLinePitch = 7;
inline constexpr int kHelpLinesPerPage = 18;
// Command band (BACK/PREV/NEXT share the VIEW LEVEL footer geometry).
inline constexpr int kHelpFooterY = 170;
inline constexpr int kHelpBackX = 10;
inline constexpr int kHelpBackWidth = 44;
inline constexpr int kHelpPrevX = 220;
inline constexpr int kHelpNextX = 270;
inline constexpr int kHelpPagerWidth = 40;
inline constexpr int kHelpFooterHeight = 20;

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
// Positional indices into the engine spec rows (menu_screen_specs.cpp) /
// picker_difficulty_menu_buttons(). Single 140px column (23-char label budget
// at 6px/char); every row's label is re-derived per frame from session/save.
inline constexpr int kDifficultyMenuBackIndex = 0;
inline constexpr int kDifficultyMenuDifficultyIndex = 1;
inline constexpr int kDifficultyMenuRespawnModeIndex = 2;
inline constexpr int kDifficultyMenuRespawnDelayIndex = 3;
inline constexpr int kDifficultyMenuPermadeathIndex = 4;
inline constexpr int kDifficultyMenuGeneratorRateIndex = 5;
inline constexpr int kDifficultyMenuInfiniteGoldIndex = 6;
// §2.7 cross-control, re-homed from MATCHUP (#218). Unlike its host-gated
// siblings it is NETWORKED-gated: visible to every peer of a networked
// lobby (joiners keep sight of the mode that changes their rights),
// host-only actionable, hidden entirely in local sessions.
inline constexpr int kDifficultyMenuCrossControlIndex = 7;
inline constexpr int kDifficultyMenuButtonCount = 8;

// Per-frame gating for the DIFFICULTY subscreen: the six settings rows are
// LobbySettings-backed (difficulty included) and hide for a non-host
// joiner; the CTRL row instead shows for every peer of a networked lobby
// and hides in local sessions. BACK's vertical cycle is rewired so nav
// never targets a hidden row.
void sync_difficulty_menu_visibility(button* buttons, int num_buttons,
                                     int& highlighted_button);

// The line the DIFFICULTY panel prints under its heading. Empty for a host
// or a solo player — their rows say everything. A joiner's rows are all
// hidden by the gate above, so without this the door they can reach from
// the Base Camp strip opens on a heading over an empty panel; the caption
// says whose call it is, the way the modes book says "The host calls the
// rules." Drawn by difficulty_draw_content.
std::string difficulty_panel_caption();

// A remote (host) GO landing while this peer is parked on the main menu or
// one of its subscreens: select the Main CONTINUE item and exit, so the
// shared picker state machine re-enters team build, whose loop-top
// remote-start check launches the game (team_build_remote_start_requested's
// main-menu-scope sibling). Returns true and sets retvalue to MENU_EXIT when
// the start should preempt the current loop.
bool picker_main_scope_remote_start_requested(int32_t& retvalue);

// The team-build-scope sibling (defined in picker_team_build.cpp): selects
// TeamBuild START GAME and sets retvalue to MENU_EXIT when a remote (host)
// start must preempt the current loop. Used by the team-build family loops
// and the menu engine's RemoteStartScope::TeamBuildScope obligation.
bool team_build_remote_start_requested(int32_t& retvalue);

// §2.6 GO/READY slot presentation (defined in picker_team_build.cpp): the
// SDL-side gather over the lobby client + save feeding the pure
// format_ready_go_button state table. Consumed by the base-camp spec's
// label/color bindings and rewire, teams_toggle_ready's client gate, and
// go_menu's host pre-check.
og::ui::ReadyGoPresentation picker_compute_ready_go_presentation();

// The §3.8 base-camp mutation tail (defined in picker_team_build.cpp):
// lobby roster re-sync (a networked content change clears that machine's
// ready server-side, §4.3), optimistic local ready drop, then the company
// autosave ([SAVE-F1] merge write in networked lobbies). Call after EVERY
// roster mutation: deploy toggle, hire, train accept, rename, promote,
// per-character team cycle.
void picker_base_camp_after_roster_mutation();
// Sale overload: keeps the removed character's wallet in the networked
// owner-preserving merge even when no remaining member names that team.
void picker_base_camp_after_roster_mutation(int additional_owned_team);
