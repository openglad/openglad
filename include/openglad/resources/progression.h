/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The shared level-win fold: the ONE place a won level's score/cash/time
// bonus is tallied, the level is marked completed, and the campaign cursor
// advances. Converges the four previously duplicated finalize sites
// (screen::endgame, the local-transport-shadow finalize, the headless server
// complete-and-load-next, and the curses advance_save_after_win). Persistence
// policy (save0 / netsession + owned-merge / in-memory checkpoint) stays
// site-owned. See docs/game-modes.md.

#include <openglad/resources/game_mode.h>

#include <array>
#include <cstdint>

class GameWorld;
class SaveData;

namespace og::progression {

// CTF loss/rematch shape: a decided match whose next level IS this level.
// Centralizes the predicate previously duplicated at screen::endgame and the
// curses is_ctf_rematch_end (and missing from the shadow finalize). Only
// meaningful in the WIN shape (ending == 0) — callers evaluate it there.
bool mode_rematch_shape(const GameWorld& world, short current_level,
                       short next_level);
bool mode_rematch_shape(const GameWorld& world, const SaveData& save,
                       short next_level);

struct WinFoldContext {
    // CALLER-computed time bonus per team (sources differ legitimately per
    // frontend: get_time_bonus on a live session screen,
    // calculate_headless_time_bonus on the headless server, all-zero in the
    // curses client). Compute it BEFORE calling the fold, from the LIVE
    // m_score.
    std::array<std::uint32_t, 4> time_bonus{};
    bool rematch_shape = false; // caller fills via mode_rematch_shape
    // The level that was just FINISHED (fill from save.scen_num when the
    // context is built, i.e. before the fold moves the cursor). This is what
    // makes a literal second fold of the same context on the same SaveData a
    // genuine no-op: after the first fold advanced scen_num, the fold can
    // tell "cursor already left the finished level" and skips the
    // bonus/mark/advance steps instead of marking the DESTINATION level
    // completed. -1 (the default) derives it from save.scen_num at fold
    // entry — correct for any single first pass, but re-entrancy then relies
    // on the caller never re-presenting a stale context. All in-tree
    // finalize sites fill it.
    short finished_level = -1;
    og::mode::LevelOutcome outcome{};

    // OUT-params (§4.6 Edit 1): the per-team cash/score the fold ADDED this
    // pass, captured in steps 2-3 before m_score is zeroed. `mutable` so a
    // caller holding the context by const reference still reads them; the
    // networked money split reads these to size each machine's share, while
    // the additions themselves are unchanged (local byte-identity pins hold).
    // A second (idempotent) fold pass fills them with all zeros: m_score is
    // already zero and on_finished is false, so no delta is applied.
    mutable std::array<std::uint32_t, 4> applied_cash_delta{};
    mutable std::array<std::uint32_t, 4> applied_score_delta{};
};

// The deterministic time-bonus helper shared by every win executor (SDL live
// results, the headless/dedicated server, and the curses networked persist) so
// each machine sizes the same pot. Player > 0 and out-of-window frames score
// zero, matching the legacy get_time_bonus math. Pure (GameWorld + SaveData).
std::uint32_t calculate_win_time_bonus(const GameWorld& world,
                                       const SaveData& save, int player_index);

// The canonical win fold (transplanted from the shadow finalize, verbatim
// order). `finished` = ctx.finished_level, or save.scen_num when the caller
// left it at -1; `on_finished` = (save.scen_num == finished), i.e. the
// cursor has not already advanced past the finished level:
//  1. already = save.is_level_completed(finished)
//  2. for t: m_totalscore[t] += m_score[t]; m_totalcash[t] += m_score[t]*2
//  3. for t: if (!already && on_finished)
//                m_totalcash[t] += ctx.time_bonus[t];
//            m_score[t] = 0
//  4. if (on_finished && current_progression().marks_level_completed()
//         && !ctx.rematch_shape)
//         save.add_level_completed(save.current_campaign, finished)
//  5. if (on_finished && ctx.outcome.next_level >= 0):
//         next = current_progression().advance_cursor(save, world,
//                                                     ctx.outcome.next_level)
//         save.scen_num = next
//         save.current_levels[save.current_campaign] = next
//  6. save.update_guys(world.oblist)
//
// On every first pass on_finished is trivially true (all sites build the
// context before the fold moves the cursor), so the added gates change
// nothing; they exist purely so a SECOND pass is detectable (below).
//
// Callers do sync_save_data_from_world FIRST, compute time_bonus SECOND,
// call the fold THIRD, and keep their persist tails.
//
// IDEMPOTENCE CONTRACT (pinned by the apply-twice==apply-once unit test): in
// local play BOTH the server-side shadow finalize and the display-side
// screen::endgame run a fold against a SaveData of the same state (two
// mirrored COPIES in the live flows — see the call graph below). A literal
// second pass on the SAME object must also be a no-op, and with
// ctx.finished_level supplied it genuinely is: m_score is already zero
// (step 2 adds zero), the advanced cursor makes on_finished false (no
// bonus re-award even for modes that never mark completion, no
// destination-level add_level_completed, no re-advance), and update_guys is
// a rebuild (idempotent on an unchanged world). advance_cursor must still
// be idempotent per its own contract — the mirrored-copy double finalize
// runs it once per copy. Without finished_level (-1 default) a stale
// re-presented context would mark the DESTINATION level completed; no
// in-tree caller omits it.
//
// Local call graph (who finalizes first, traced from the live flows):
//  - Local single-player / split-screen: the shadow server's
//    on_level_transition / on_exit_accepted hook runs
//    finalize_level_and_advance_cursor (fold #1, server screen's SaveData,
//    writes save0), then the forwarded EndGame reaches the display and
//    screen::endgame folds the DISPLAY screen's SaveData (fold #2, same
//    mirrored state) and autosaves save0 again — last write wins, both
//    equivalent.
//  - Networked host: same as local (server fold persists netsession +
//    owner-merge into save0), display fold runs but its save0 write is
//    gated off by `networked`.
//  - Networked client: only the display fold runs locally (the host's
//    server folded remotely); the client persists via the owned-merge path.
//  - Headless dedicated server: only complete_headless_level_and_load_next
//    folds (each remote display folds its own mirror save).
//  - Curses local: only commit_result_to_save -> advance_save_after_win
//    folds (the in-process server's hooks are wired to no-ops).
void apply_win_fold(SaveData& save, const GameWorld& world,
                    const WinFoldContext& ctx);

// Injection seam for tests: same fold, explicit progression instance.
// The 3-arg overload uses og::mode::current_progression().
void apply_win_fold(SaveData& save, const GameWorld& world,
                    const WinFoldContext& ctx,
                    og::mode::IProgression& progression);

} // namespace og::progression
