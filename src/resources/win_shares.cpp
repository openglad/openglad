/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Networked win-pot money split. Contract in win_shares.h and §4.6-§4.7 of
// docs/company-basecamp-design.md.

#include <openglad/resources/win_shares.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/company.h>
#include <openglad/resources/save_data.h>

#include <cstddef>

namespace og::progression {

namespace {

constexpr std::size_t kMaxPlayers = og::sim::kMaxGlobalPlayers;

// Split `delta` across the counted contributors: each gets floor(delta*count/
// total); the remainder goes to the largest contributor, ties broken to the
// lowest player index (achieved by strict-greater over an ascending scan).
void distribute_delta(std::uint32_t delta,
                      const std::array<int, kMaxPlayers>& count,
                      int total,
                      std::array<std::uint64_t, kMaxPlayers>& out)
{
    if (total <= 0)
        return;

    std::uint64_t assigned = 0;
    int largest_count = 0;
    int largest_player = -1;
    for (std::size_t player = 0; player < kMaxPlayers; ++player)
    {
        if (count[player] <= 0)
            continue;
        const std::uint64_t share = static_cast<std::uint64_t>(delta) *
            static_cast<std::uint64_t>(count[player]) /
            static_cast<std::uint64_t>(total);
        out[player] = share;
        assigned += share;
        if (count[player] > largest_count)
        {
            largest_count = count[player];
            largest_player = static_cast<int>(player);
        }
    }
    if (largest_player >= 0)
        out[static_cast<std::size_t>(largest_player)] +=
            static_cast<std::uint64_t>(delta) - assigned;
}

} // namespace

std::vector<WinShareContributor> collect_deployed_contributors(
    const GameWorld& world)
{
    std::vector<WinShareContributor> contributors;
    for (const auto& uptr : world.oblist)
    {
        const walker* const entity = uptr.get();
        if (entity == nullptr || entity->myguy == nullptr)
            continue;
        contributors.push_back(WinShareContributor{
            .owner = entity->myguy->owner_player_index,
            .team = static_cast<short>(entity->team_num()),
        });
    }
    return contributors;
}

NetWinShareTable compute_networked_win_shares(const NetWinFoldCapture& capture)
{
    NetWinShareTable table;

    for (std::size_t team = 0; team < 4; ++team)
    {
        std::array<int, kMaxPlayers> count{};
        int total = 0;
        for (const WinShareContributor& contributor : capture.deployed)
        {
            if (contributor.team != static_cast<short>(team))
                continue;
            if (contributor.owner >= kMaxPlayers)
                continue;
            ++count[contributor.owner];
            ++total;
        }
        if (total == 0)
            continue; // no deployed contributor on this team: delta undistributed
        distribute_delta(capture.cash_delta[team], count, total,
                         table.cash[team]);
        distribute_delta(capture.score_delta[team], count, total,
                         table.score[team]);
    }

    for (const WinShareContributor& contributor : capture.deployed)
    {
        if (contributor.owner < kMaxPlayers)
            ++table.deployed_count[contributor.owner];
    }

    return table;
}

int apply_networked_win_shares(SaveData& save, const NetWinFoldCapture& capture,
                               std::span<const std::uint8_t> own_player_indices,
                               std::optional<std::size_t> primary_team)
{
    const NetWinShareTable table = compute_networked_win_shares(capture);

    int own_deployed = 0;
    for (const std::uint8_t player : own_player_indices)
    {
        if (player >= kMaxPlayers)
            continue;
        own_deployed += table.deployed_count[player];
    }

    for (std::size_t team = 0; team < 4; ++team)
    {
        std::uint64_t cash = 0;
        std::uint64_t score = 0;
        for (const std::uint8_t player : own_player_indices)
        {
            if (player >= kMaxPlayers)
                continue;
            cash += table.cash[team][player];
            score += table.score[team][player];
        }
        save.m_totalcash[team] += static_cast<std::uint32_t>(cash);
        save.m_totalscore[team] += static_cast<std::uint32_t>(score);
    }

    if (primary_team.has_value() && *primary_team < 4)
    {
        save.totalcash = save.m_totalcash[*primary_team];
        save.totalscore = save.m_totalscore[*primary_team];
    }

    return own_deployed;
}

std::uint64_t machine_cash_share(const NetWinFoldCapture& capture,
                                 std::span<const std::uint8_t> own_player_indices)
{
    const NetWinShareTable table = compute_networked_win_shares(capture);
    std::uint64_t total = 0;
    for (std::size_t team = 0; team < 4; ++team)
        for (const std::uint8_t player : own_player_indices)
            if (player < kMaxPlayers)
                total += table.cash[team][player];
    return total;
}

bool persist_networked_win(const std::string& slot, const SaveData& session,
                           const GameWorld& world,
                           std::span<const std::uint8_t> own_player_indices,
                           std::optional<std::size_t> primary_team,
                           const NetWinFoldCapture& capture, int completed_level)
{
    const int reporting_player =
        own_player_indices.empty() ? -1
                                   : static_cast<int>(own_player_indices.front());

    SaveData merged;
    if (merged.load_with_error(slot) != SaveDataIoError::None)
    {
        // No untouched pre-level roster to merge into; skip rather than clobber
        // the company save with a partial team.
        LogError("net_persist_skipped reason=no_prelevel_save player={}\n",
                 reporting_player);
        return false;
    }

    if (!own_player_indices.empty())
    {
        // The permadeath rule is a lobby-negotiated MATCH setting: read it from
        // the session save (which carries the negotiated flag), never from
        // whatever the on-disk copy last stored.
        // Deliberately persisted below with the rest of the merged save (like
        // the campaign cursor), so the company reflects the last session's rules.
        merged.keep_fallen_heroes = session.keep_fallen_heroes;
        merged.merge_owned_guys_from(world.oblist, own_player_indices);

        // §4.6 Edit 2: bank the disk baseline PLUS only this machine's
        // deploy-ratio share of the fold delta (never the whole session pot).
        const int own_deployed = apply_networked_win_shares(
            merged, capture, own_player_indices, primary_team);

        // Keep this peer's private history and add only the level this machine
        // just finished. A 0-deploy machine earns no completion credit (§4.7)
        // even though its cursor still advances below.
        const auto session_history =
            session.completed_levels.find(session.current_campaign);
        if (own_deployed > 0 && completed_level >= 0 &&
            session_history != session.completed_levels.end() &&
            session_history->second.contains(completed_level))
        {
            merged.add_level_completed(session.current_campaign, completed_level);
        }
    }

    // Advance this machine's OWN campaign cursor whether it owns characters or
    // is spectating, so a zero-seat host/client still rejoins at the next
    // destination.
    merged.current_campaign = session.current_campaign;
    merged.scen_num = session.scen_num;

    // §3.8: persist through the LevelWin choke point (stamp last_played, atomic
    // write, exactly one backup snapshot per machine per win). `merged` is the
    // private on-disk company already overlaid with this machine's own
    // roster/wallet/cursor, so the default (plain) autosave context is correct.
    if (og::data::company_autosave(
            merged, og::data::CompanyAutosaveKind::LevelWin) !=
        SaveDataIoError::None)
    {
        LogError("net_persist_save_failed player={}\n", reporting_player);
        return false;
    }
    return true;
}

} // namespace og::progression
