/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The in-game PAUSED menu and its per-player settings screen
// (docs/pause-menu-design.md §2.1/§2.2), hosted from the game loop's Esc
// branch over a darkened world snapshot. Deliberately NOT in the
// MenuScreenId registry: the registry sweep would run it in picker context.
//
// The interface component may not include platform headers (dependency
// direction), so every transport operation — the paused pump, the pause
// keep-alive, seat add/remove — is injected through PauseMenuHost function
// pointers, implemented in src/platform/sdl/game_loop.cpp over the local
// transport shadow.

#include <openglad/interface/ui/menu_screen_spec.h>

#include <cstdint>
#include <string>

namespace og::ui {

enum class PauseMenuResult : std::uint8_t {
    Resumed,       // closed via RESUME/Esc; the pause was released
    SessionEnded,  // the session/world ended underneath the open menu
    Quit,          // Abort Mission confirmed — the caller runs the abort
    Restart,       // Restart Mission confirmed — the caller runs the restart
};

struct PauseMenuHost {
    // One transport pump per menu frame (server step + full mirror drain).
    // False = the session/world ended; the menu must close.
    bool (*pump_paused)() = nullptr;
    // Send one pause request: opens the menu's pause, keeps it alive (~20s
    // cadence defeats the server's auto-resume), and re-pauses when the
    // baseline unpaused underneath the open menu.
    void (*request_pause)() = nullptr;
    // Send the pause response iff currently paused (RESUME/Esc close).
    void (*resume)() = nullptr;
    // Mirror-side pause state (display client baseline).
    bool (*is_paused)() = nullptr;
    // Display name of a REMOTE peer owning the pause; "" when local/none.
    std::string (*remote_pause_owner)() = nullptr;
    bool (*can_add_player)() = nullptr;
    bool (*add_player)() = nullptr;
    bool (*can_remove_player)(int seat) = nullptr;
    bool (*remove_player)(int seat) = nullptr;
    bool networked = false;
    bool restart_allowed = false;  // hidden networked / retry-suppressed modes
};

// Blocks until the menu closes. Requests the pause at entry; on RESUME/Esc it
// releases the pause before returning. Exit hygiene (mouse release, transient
// input clear, button teardown, world-frame rebuild, redrawme) runs on every
// path. Under TESTING with no forced-real menu, consumes a queued scripted
// outcome instead of running the UI (the dialog-seam pattern).
PauseMenuResult run_pause_menu(const PauseMenuHost& host);

// Spec accessors: exact-table pins and nav sweeps drive these directly.
const MenuScreenSpec& pause_menu_screen_spec();
const MenuScreenSpec& pause_player_menu_screen_spec();

// PAUSED screen row ordinals (every actionable row is MenuSpecRow with
// arg == its own ordinal).
inline constexpr int kPauseMenuResumeIndex = 0;
inline constexpr int kPauseMenuRestartIndex = 1;
inline constexpr int kPauseMenuQuitIndex = 2;
inline constexpr int kPauseMenuPlayerBaseIndex = 3;  // .. +MAX_PLAYERS-1
inline constexpr int kPauseMenuAddPlayerIndex = 7;
inline constexpr int kPauseMenuButtonCount = 8;

// Player screen row ordinals.
inline constexpr int kPausePlayerBackIndex = 0;
inline constexpr int kPausePlayerInputIndex = 1;
inline constexpr int kPausePlayerModeIndex = 2;
inline constexpr int kPausePlayerRemapIndex = 3;
inline constexpr int kPausePlayerResetIndex = 4;
inline constexpr int kPausePlayerRemoveIndex = 5;
inline constexpr int kPausePlayerButtonCount = 6;

// One local seat shown as a PAUSED-screen player row: the controller-profile
// slot this machine samples (seat) and the displayed P# (global player index
// + 1 — a one-seat networked joiner may be P3 on profile slot 0).
struct PauseSeatInfo {
    int seat = 0;
    int player_number = 1;
};

// The local seats, in view/profile order. Networked sessions enumerate the
// session's own player indices; local sessions enumerate save numplayers.
std::vector<PauseSeatInfo> collect_pause_seats(bool networked);

// "P{n}: {mapping name}" (the font has no U+00B7 glyph — frames 0..124 —
// so the design's "P1 · WASD" separator renders as ':').
std::string pause_player_row_label(const PauseSeatInfo& seat);

// Screen states, public so headless tests can drive the rewires, gates and
// row handlers through the installed-state seam without a blocking UI thread
// (the seat-settings pattern; a null installed state renders a safe shape).
struct PauseMenuScreenState {
    const PauseMenuHost* host = nullptr;
    PauseMenuResult outcome = PauseMenuResult::Resumed;
    // The darkened world snapshot as raw RGB triples, captured once on the
    // first background pass and restored every frame. Pixel restore (not
    // re-seed + re-darken) because at default canvas dims the world/UI
    // canvases share a surface — the re-seed silently no-ops there and
    // repeated darkening compounds to black. Raw RGB (not palette indices)
    // because darkening blends to colors outside the palette.
    std::vector<unsigned char> backdrop;
    // Esc-close edge machine: require an observed release after entry so the
    // opening press (or a held web Backspace autorepeating) cannot close the
    // menu it just opened.
    bool esc_seen_up = false;
    // Last pause request stamp: ~20s keep-alive while paused, 1s re-request
    // cadence when the baseline unpaused underneath the open menu.
    std::uint32_t last_keepalive_ms = 0;
};

struct PausePlayerScreenState {
    const PauseMenuHost* host = nullptr;
    int seat = 0;
    int player_number = 1;
    bool session_ended = false;
    bool removed = false;
    std::uint32_t last_keepalive_ms = 0;
};

void install_pause_menu_state_for_screen(PauseMenuScreenState* state);
void install_pause_player_state_for_screen(PausePlayerScreenState* state);

#ifdef TESTING
// Scripted outcomes consumed by run_pause_menu instead of running the real
// menu (the picker_testing_yes_or_no_queue pattern). release_pause=false
// leaves the server pause pending — the "menu is open" state — so frame
// tests can assert the pause without a blocking loop. An empty queue behaves
// like {Resumed, release_pause=true}.
void pause_menu_testing_queue_outcome(PauseMenuResult outcome,
                                      bool release_pause);
void pause_menu_testing_clear_queue();
int pause_menu_testing_queue_remaining();
// Force the real blocking menu under TESTING (injector-thread flows).
void pause_menu_testing_set_force_real(bool force);
// Override the ~20s keep-alive cadence; 0 restores the default.
void pause_menu_testing_set_keepalive_interval_ms(std::uint32_t interval_ms);
#endif

} // namespace og::ui
