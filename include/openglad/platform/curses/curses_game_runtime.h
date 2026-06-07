/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/input_state.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GameWorld;
class SaveData;

namespace og::curses {

class ITerminal;
class IClock;
class CursesInput;
class CursesRenderer;

// One level's outcome, mirroring GameWorld::ending / next_level semantics.
struct GameRunResult {
    bool ended = false;     // the level loop terminated normally (world.end)
    int ending = 0;         // 0 = win, 1 = lose (GameWorld::ending)
    int next_level = -1;    // requested next scenario, or -1
    bool withdrew = false;  // player chose "quit this mission"
    bool quit_app = false;  // player asked to exit the whole client
};

// Abstracts one in-flight game session (local in-process host, networked host, or
// networked join). The level loop drives it uniformly; only construction differs.
class CursesGameSession
{
public:
    virtual ~CursesGameSession() = default;

    // Queue this peer's input for the next tick.
    virtual void send_input(const InputState& input) = 0;
    // Advance one frame: host/local step the authoritative server, then every
    // session polls its client mirror. Safe to call repeatedly.
    virtual void advance() = 0;

    // The mirror world the renderer reads.
    virtual GameWorld& mirror_world() = 0;
    // Entity id the local view follows (the player's avatar), or 0 if none yet.
    virtual std::uint32_t followed_entity_id() const = 0;
    // Server tick used to stamp outgoing input.
    virtual std::uint32_t next_input_tick() const = 0;

    // Level lifecycle (read from the mirror world after advance()).
    virtual bool ended() const = 0;
    virtual int ending() const = 0;
    virtual int next_level() const = 0;

    // Ask the server to end this mission for all players (a deliberate retreat).
    virtual void request_abort() = 0;
    // Drain human-readable notifications accumulated from sim/game-flow events.
    virtual std::vector<std::string> drain_messages() = 0;

    // Exit prompt: when a player steps on the level exit, the server asks for
    // confirmation before leaving. While one is pending, the level loop shows a
    // yes/no prompt and answers via respond_to_exit_prompt — so the player can
    // DECLINE and keep playing. Sessions that auto-consent leave pending false.
    virtual bool exit_prompt_pending() const { return false; }
    virtual std::string exit_prompt_text() const { return {}; }
    virtual void respond_to_exit_prompt(bool /*accept*/) {}

    // After the level loop ends, write the outcome (campaign progress, gold,
    // updated character stats) back into the SaveData the session was created
    // from. No-op by default (networked sessions persist via their own path).
    virtual void commit_result_to_save() {}
};

// Build a local single-player session: an in-process GameServer + GameClient,
// the level loaded and the team spawned from `save`. Returns nullptr and fills
// `error` on failure.
std::unique_ptr<CursesGameSession> make_local_session(SaveData& save, int difficulty,
                                                      std::string* error);

struct LevelLoopOptions {
    // Stop after this many advanced frames regardless of world.end (-1 = unlimited).
    // Used by tests to bound the loop.
    int max_frames = -1;
    // When true, do not block on input or sleep between frames (test/headless).
    bool no_pacing = false;
    // When true, the loop renders each frame; tests may disable to skip drawing.
    bool render = true;
};

// Run a session's per-level loop until the level ends (or the frame cap is hit):
// read input -> send -> advance -> render -> pace. Returns the outcome.
GameRunResult run_level_loop(CursesGameSession& session, ITerminal& term, IClock& clock,
                             CursesInput& input, CursesRenderer& renderer,
                             const LevelLoopOptions& opt);

// Apply a level win to `save`: sync the per-team score from the finished world,
// award gold (2x score), mark the level completed, advance scen_num to
// `next_level` (or +1), and persist surviving characters' XP/levels. Mirrors the
// engine's complete_headless_level_and_load_next save mutation (minus the
// in-session reload). Exposed so the campaign-progression logic is unit-testable.
void advance_save_after_win(SaveData& save, const GameWorld& finished_world, int next_level);

} // namespace og::curses
