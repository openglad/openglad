/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/lobby_state.h>
#include <openglad/platform/curses/curses_game_runtime.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameWorld;
class SaveData;

namespace og::curses {

class ITerminal;
class IClock;

// Networking configuration parsed from the menu or command line.
struct HostOptions {
    int port = 0;             // 0 => default WebSocket port
    std::string relay_url;    // optional relay URL ("" => no relay)
    int difficulty = 1;
};

struct JoinOptions {
    std::string url;          // ws:// or wss:// direct URL, or relay room URL
    bool via_relay = false;
};

// A lobby front-end: drives the pre-game LobbyServer/LobbyClient exchange in the
// terminal (roster, settings, ready/start) and, on start, yields a session.
// Returns the live session via take_session() once start() reports true.
class CursesLobby
{
public:
    virtual ~CursesLobby() = default;

    // Pump the lobby and render its status to the terminal. Returns true once a
    // game start has been negotiated (the session is ready via take_session()).
    virtual bool poll(ITerminal& term, IClock& clock) = 0;
    // Whether the local peer may press "start" (host only).
    virtual bool is_host() const = 0;
    // Request the game to start (host only). No-op for a joiner.
    virtual void request_start() = 0;
    // After poll() returns true, take ownership of the started session.
    virtual std::unique_ptr<CursesGameSession> take_session() = 0;
    // Human-readable status lines for the lobby screen.
    virtual std::vector<std::string> status_lines() const = 0;
    // Tear down transports (called when the user cancels).
    virtual void cancel() = 0;
    // True once the user has cancelled (so the lobby loop can stop polling).
    virtual bool cancelled() const { return false; }
    // Move one owned seat to `team`. player_index is the current display
    // handle; implementations resolve it to the server-issued stable seat ID
    // from the recipient-specific LobbyState and reject foreign/stale values.
    virtual bool request_seat_team_change(std::uint8_t player_index,
                                          short team) = 0;
    virtual bool request_seat_team_change(std::uint8_t player_index,
                                          og::sim::LobbySeatId seat_id,
                                          short team) = 0;
    // This machine's current global player indices, in local seat order.
    virtual std::vector<std::uint8_t> local_player_indices() const = 0;
    // Toggle this peer's informational ready flag (LobbyReadyMessage).
    virtual bool set_ready(bool ready) = 0;
    // This peer's current replicated ready flag.
    virtual bool local_ready() const = 0;
    // The replicated lobby roster (empty before any state broadcast).
    virtual std::vector<og::sim::LobbyPlayer> players() const = 0;
    // LINEUP §6: the host removes one foreign MACHINE from the lobby (every
    // seat that machine owns goes with it). False for a joiner, for a lobby
    // with no live link, and for this machine's own id — leaving is
    // disconnect(), not a kick.
    virtual bool kick_machine(og::sim::LobbyMachineId machine_id)
    {
        (void)machine_id;
        return false;
    }
    // LINEUP §6: why the link died, when the reason is known. "KICKED BY
    // HOST" outranks the ordinary status lines; empty on a healthy lobby.
    virtual std::optional<std::string> connection_alert() const { return {}; }
    // --- Staged lobby (#218, C9) --------------------------------------------
    // The staged world this client's preview reads: the host's own MatchStage
    // world, or the joiner's preview mirror healed from the broadcast pair.
    // nullptr while nothing is staged/applied.
    virtual const GameWorld* staged_world() const { return nullptr; }
    // Monotonic preview refresh counter (moves on every restage / mirror
    // apply attempt, honest failures included). 0 = nothing yet.
    virtual std::uint32_t stage_generation() const { return 0; }
    // The honest degradation line when staged_world() is null: empty while
    // nothing has been attempted, "STAGING FAILED" for a host whose stage
    // failed, "STAGING PREVIEW UNAVAILABLE" for a joiner whose mirror could
    // not apply the retained pair.
    virtual std::string stage_failure_line() const { return {}; }
};

// Construct a hosting lobby from the local save (team + settings). Builds the
// in-process loopback + WebSocket server (+ optional relay) MultiplexTransport
// and a LobbyServer. Returns nullptr / fills `error` on failure.
std::unique_ptr<CursesLobby> make_host_lobby(SaveData& save, const HostOptions& opt,
                                             std::string* error);

// Construct a joining lobby that connects to a remote host.
std::unique_ptr<CursesLobby> make_join_lobby(SaveData& save, const JoinOptions& opt,
                                             std::string* error);

// Drive a constructed lobby until a game starts (or the user cancels), then run
// the negotiated networked level to completion. Returns the level outcome; a
// default-constructed result (ended == false) means the lobby was cancelled
// before any game began. Shared by the picker's networking menu and the
// --host/--join command-line shortcuts.
GameRunResult run_curses_lobby(CursesLobby& lobby, ITerminal& term, IClock& clock);

} // namespace og::curses
