/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The shared level-win fold. Contract, ordering, and the idempotence /
// call-graph notes live in progression.h.

#include <openglad/resources/progression.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/resources/save_data.h>

#include <cstddef>
#include <iterator>

namespace og::progression {

bool ctf_rematch_shape(const GameWorld& world, short current_level,
                       short next_level)
{
    return (world.type & GameWorld::TYPE_CTF) && world.ctf.active &&
        world.ctf.winner_team >= 0 && next_level == current_level;
}

bool ctf_rematch_shape(const GameWorld& world, const SaveData& save,
                       short next_level)
{
    return ctf_rematch_shape(world, save.scen_num, next_level);
}

void apply_win_fold(SaveData& save, const GameWorld& world,
                    const WinFoldContext& ctx)
{
    apply_win_fold(save, world, ctx, og::mode::current_progression());
}

void apply_win_fold(SaveData& save, const GameWorld& world,
                    const WinFoldContext& ctx,
                    og::mode::IProgression& progression)
{
    // The level this fold finalizes. Callers fill ctx.finished_level from the
    // pre-fold cursor; -1 derives it here (fine for any single first pass).
    // on_finished false means the cursor already advanced past the finished
    // level — i.e. this is a re-entrant SECOND pass on the same SaveData —
    // so the bonus/mark/advance steps are skipped (see the contract in
    // progression.h). On every first pass on_finished is trivially true.
    const short finished =
        ctx.finished_level >= 0 ? ctx.finished_level : save.scen_num;
    const bool on_finished = (save.scen_num == finished);

    // 1. Completed-before? Decides the time bonus (first real win only).
    const bool already_completed = save.is_level_completed(finished);

    // 2. Fold the level score into the running totals (cash pays 2x score).
    // Update all the money!
    for (std::size_t team_index = 0; team_index < std::size(save.m_score);
         ++team_index)
    {
        save.m_totalscore[team_index] += save.m_score[team_index];
        save.m_totalcash[team_index] += save.m_score[team_index] * 2u;
    }

    // 3. Award the caller-computed time bonus on a first win, then zero the
    //    per-level score (a second fold pass therefore adds nothing).
    for (std::size_t team_index = 0; team_index < std::size(save.m_score);
         ++team_index)
    {
        if (!already_completed && on_finished)
            save.m_totalcash[team_index] += ctx.time_bonus[team_index];
        save.m_score[team_index] = 0;
    }

    // 4. Beat that level — except the CTF loss/rematch shape (a decided match
    //    whose next level IS this level): the map was not beaten, so keep
    //    level select honest and preserve the first-real-win time bonus.
    //    Modes that never mark completion (tower) skip this by policy.
    if (on_finished && progression.marks_level_completed() &&
        !ctx.rematch_shape)
    {
        save.add_level_completed(save.current_campaign, finished);
    }

    // 5. Advance the campaign cursor. The mode may redirect (or HOLD by
    //    returning save.scen_num, e.g. on a content-provisioning failure).
    if (on_finished && ctx.outcome.next_level >= 0)
    {
        const short next =
            progression.advance_cursor(save, world, ctx.outcome.next_level);
        save.scen_num = next;    // Fake jumping to next level ..
        save.current_levels[save.current_campaign] = next;
    }

    // 6. Rebuild the roster from the finished level (dead heroes dropped
    //    unless keep_fallen_heroes).
    // Grab our team out of the level
    save.update_guys(world.oblist);
}

} // namespace og::progression
