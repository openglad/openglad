/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/input_state.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameWorld;
class SaveData;
class guy;

namespace og::sim {
class GameClient;
}

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
    // CTF match outcome (filled when the ended world was an active CTF
    // match). A scripted match end always carries the classic WIN shape
    // (ending == 0) even when the local player LOST, so the post-level
    // verdict must come from these fields, never from `ending` alone.
    bool mode_match = false;     // world was an active scripted match at level end
    int mode_winner_team = -1;   // mode.winner_team at level end
    int local_team = -1;        // followed walker's team at level end (-1 unknown)
    bool mode_rematch = false;   // loss shape: next_level == the finished level
};

// "Victory!" / "Defeat." / "Match over." for the post-level dialog, CTF-aware
// per the GameRunResult contract above (neutral fallback when the local team
// or winner is unknown). When the finished save/world pair is supplied, the
// game mode's results_summary_lines are appended space-joined (Classic adds
// nothing, so legacy callers passing nothing are unchanged).
std::string mission_verdict_line(const GameRunResult& result,
                                 const SaveData* save = nullptr,
                                 const GameWorld* world = nullptr);

// True for the CTF loss/rematch end shape: a decided CTF match that ended
// with the classic WIN shape but whose next level IS the finished level —
// the match was LOST and must not mark the campaign level completed.
bool is_mode_rematch_end(const GameWorld& world, int ending, int next_level);

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
    // §4.5 follow camera: true while the local seat has no controllable
    // walker (0-deploy, all-dead, spectator) and followed_entity_id() names
    // a WATCHED target instead of the player's avatar — the HUD renders
    // "(following NAME)" and the seat's SwitchChar binding cycles the target
    // (Shift = reverse). Local sessions never follow.
    virtual bool follow_engaged() const { return false; }
    // The walker the player drives: followed_entity_id(), or 0 while the
    // camera follows a watched walker (that body is not ours to cast with or
    // to edit). The one place that rule is asked.
    std::uint32_t driven_entity_id() const
    {
        return follow_engaged() ? 0 : followed_entity_id();
    }
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

    // The in-game PAUSED menu (Esc). The level loop asks for the pause once
    // when the menu opens and keeps calling advance() without input while it
    // is open, the ABILITIES and KEYS pages included (re-asking as the
    // keep-alive); closing the menu sends the pause response. Every real
    // session implements these once, through CursesClientSession below; only
    // test doubles keep the do-nothing defaults.
    virtual void request_pause() {}
    virtual void send_pause_response() {}
    // ABILITIES from that menu: send the layout of the walker the player
    // drives (one BeltChange naming driven_entity_id()).
    virtual void set_belt(const og::sim::belt::Bytes& /*belt*/) {}

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

// The PAUSED menu's three calls, written once over the session's GameClient.
// The local session, the networked host and the networked joiner each hold
// one GameClient and one mirror, so all three derive from this instead of
// repeating the calls.
class CursesClientSession : public CursesGameSession
{
public:
    void request_pause() override;
    // Sent whether or not the mirror has seen the pause land yet: the server
    // treats a response as "clear any pause", which is harmless with none
    // pending, and over a real network a quick Esc, R closes the menu before
    // the pause reaches the mirror (skipping the response then would leave
    // the world paused for everyone until the server's auto-resume).
    void send_pause_response() override;
    // Sends nothing while the player drives no walker (driven_entity_id() 0).
    void set_belt(const og::sim::belt::Bytes& belt) override;

protected:
    virtual og::sim::GameClient& session_client() const = 0;
};

// Build a local single-player session: an in-process GameServer + GameClient,
// the level loaded and the team spawned from `save`. Returns nullptr and fills
// `error` on failure.
//
// `match_seed` is the session's latched match seed (#218): the solo picker
// passes the same seed its VIEW LEVEL preview staged with, so the world this
// launch adopts IS the world the preview showed. Left empty (tests, and any
// caller with no preview to honour) the session draws a fresh per-round seed.
std::unique_ptr<CursesGameSession> make_local_session(
    SaveData& save, int difficulty, std::string* error,
    std::optional<std::uint32_t> match_seed = std::nullopt);

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
