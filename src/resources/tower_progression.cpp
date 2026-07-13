/* TowerProgression — the Tower Climb og::mode::IProgression instance
 * (tower-triple spec §5.5). STATELESS: all run state lives in SaveData
 * (cursor = scen_num, seed/best = the GTL v13 fields) or is derived from
 * world.id; the singleton is dispatched from game_mode.cpp's exhaustive
 * switch whenever the mounted campaign's campaign.yaml says `mode: tower`.
 *
 * Lifecycle recap:
 *  - GO at the Gate (scen_num <= 700) = FRESH RUN: draw a new run seed from
 *    the session RNG (never the sim RNG — only regeneration needs to be
 *    deterministic, not the choice), prune stale user-dir floors, prefetch
 *    floor 1 (D8: the Gate's exit prompt reads 701's title). A failed
 *    floor-1 write vetoes the GO (loud at the picker, not a silent
 *    Gate-replay ghost).
 *  - GO mid-run = RESUME: heal the current floor's files (unhealable =>
 *    veto — nothing to load), prefetch the next (failure logs only;
 *    advance_cursor retries and holds).
 *  - advance_cursor: best = max(best, next - 700) (floors climbed = highest
 *    floor REACHED), regenerate missing destination files, HOLD the cursor
 *    on a write failure (replay the floor rather than wrap the campaign).
 *    Idempotent — both local finalize sites fold the same SaveData (§2.4).
 *  - on_run_ended: the ONE enumerated exception to "losses persist nothing"
 *    (D10): reset the cursor to the Gate via a field-merge save0 write so a
 *    relaunch cannot resume the death floor. Roster on disk stays the last
 *    floor-win checkpoint.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/mapgen/tower_floor_gen.h>

#include <openglad/core/tower_constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <format>
#include <random>
#include <set>
#include <string>

namespace og::mode {

namespace {

// The GTL campaign list serializes completed_levels' KEYS, and D9 means the
// tower never completes a level — without an (empty) entry the tower's
// per-campaign cursor row would never reach disk at all. The set stays
// empty forever (D9 intact); only the key must exist.
void ensure_tower_campaign_entry(SaveData& save)
{
    save.completed_levels.insert(
        {std::string(og::kTowerCampaignId), std::set<int>()});
}

// Draw the fresh-run seed. Session RNG when one is installed (GameContext
// IRandom via the gameplay context's session_rng_ref); std::random_device
// otherwise. NEVER world.rng_ / libc rand: the CHOICE need not be
// deterministic, only regeneration from it (spec §5.4).
std::uint32_t draw_run_seed()
{
    if (current_game != nullptr && current_game->session_rng_ref != nullptr &&
        *current_game->session_rng_ref != nullptr)
    {
        IRandom& rng = **current_game->session_rng_ref;
        return (rng.next(0x10000u) << 16) | rng.next(0x10000u);
    }
    std::random_device rd;
    return static_cast<std::uint32_t>(rd());
}

// Regenerate floor N's files from (seed, N) unless they already exist.
// True when the files are present afterwards.
bool ensure_floor_files(std::uint32_t run_seed, int floor_number)
{
    const int id = og::kTowerGateLevel + floor_number;
    if (og::data::tower_floor_files_exist(id))
        return true;
    const og::tower::TowerFloorReport report =
        og::tower::generate_tower_floor_to_user_dir(run_seed, floor_number);
    return report.written;
}

class TowerProgression final : public IProgression
{
public:
    ProgressionKind kind() const override { return ProgressionKind::Tower; }

    bool marks_level_completed() const override { return false; } // D9
    bool persist_after_win() const override { return true; }      // D10
    bool suppress_retry() const override { return true; }

    // D3: respawns clamped OFF in tower — with respawns active, team-wipe
    // endgame is suppressed and an endless score mode could never end.
    // Owner may revert by deleting this one-line override.
    short clamp_respawn_mode(short requested) const override
    {
        (void)requested;
        return 0;
    }

    bool prepare_launch(SaveData& save, bool networked_session) override
    {
        if (networked_session)
            return false; // veto; the picker shows "TOWER CLIMB is local-only"

        if (save.scen_num <= og::kTowerGateLevel)
        {
            // Fresh run at the Gate: new seed, prune stale floors (they are
            // contiguous — stop at the first miss), prefetch floor 1. A
            // failed floor-1 write VETOES the GO: the Gate would win into a
            // held cursor and the player would replay the Gate believing
            // it's Floor 1 — fail at GO time instead, where the picker can
            // say so.
            save.tower_run_seed = draw_run_seed();
            for (int id = og::kTowerFirstFloorLevel;
                 og::data::delete_tower_floor_files(id); ++id)
            {
            }
            if (!ensure_floor_files(save.tower_run_seed, 1))
            {
                LogError("tower: could not provision floor 1 — vetoing GO\n");
                return false;
            }
            return true;
        }
        // Resume: heal the current floor, prefetch the next (the in-level
        // exit prompt reads the destination's title — D8). An unhealable
        // CURRENT floor vetoes the GO (there is nothing to load); a failed
        // NEXT-floor prefetch only logs — advance_cursor retries it and
        // holds the cursor if it still fails.
        const int current = save.scen_num - og::kTowerGateLevel;
        if (!ensure_floor_files(save.tower_run_seed, current))
        {
            LogError("tower: could not heal floor {} — vetoing GO\n", current);
            return false;
        }
        if (!ensure_floor_files(save.tower_run_seed, current + 1))
        {
            LogError("tower: floor {} prefetch failed; advance will retry\n",
                     current + 1);
        }
        return true;
    }

    void ensure_level_available(SaveData& save) override
    {
        if (save.scen_num <= og::kTowerGateLevel)
            return; // the Gate ships in the mounted .glad
        (void)ensure_floor_files(save.tower_run_seed,
                                 save.scen_num - og::kTowerGateLevel);
    }

    short advance_cursor(SaveData& save, const GameWorld& world,
                         short next_level) override
    {
        (void)world;
        if (next_level < og::kTowerFirstFloorLevel)
            return next_level; // not a generated floor; nothing to provision
        ensure_tower_campaign_entry(save); // the win autosave persists the cursor
        // Floors climbed = highest floor REACHED, recorded when the cursor
        // advances to it. max() keeps the second local finalize a no-op.
        save.tower_best_floor = std::max(
            save.tower_best_floor,
            static_cast<short>(next_level - og::kTowerGateLevel));
        if (!ensure_floor_files(save.tower_run_seed,
                                next_level - og::kTowerGateLevel))
        {
            // Provisioning failed: HOLD — the player replays the current
            // floor instead of the levels.front() wrap silently resetting
            // the run.
            LogError("tower: could not provision floor {} — holding at {}\n",
                     next_level - og::kTowerGateLevel, save.scen_num);
            return save.scen_num;
        }
        return next_level;
    }

    void on_run_ended(SaveData& save, const GameWorld& world,
                      const LevelOutcome& outcome) override
    {
        (void)world;
        (void)outcome;
        // Mid-run only: a Gate withdraw ends nothing.
        if (save.current_campaign != og::kTowerCampaignId ||
            save.scen_num <= og::kTowerGateLevel)
            return;

        const short session_best = save.tower_best_floor;

        // The live session save: back to the Gate, best retained. Results
        // surfaces derive the death floor from world.id, so this ordering
        // is safe even though the screen site runs before the popup.
        save.scen_num = og::kTowerGateLevel;
        save.current_levels[std::string(og::kTowerCampaignId)] =
            og::kTowerGateLevel;
        ensure_tower_campaign_entry(save);

        // The ONE field-merge save0 write (D10; withdraw-rewrite pattern):
        // a fresh disk copy keeps the roster at the last floor-win
        // checkpoint — losses persist nothing else. Mid-run the disk save's
        // campaign IS the tower, so the SaveData::load-mounts trap is moot.
        SaveData disk;
        if (disk.load("save0"))
        {
            disk.scen_num = og::kTowerGateLevel;
            disk.current_levels[std::string(og::kTowerCampaignId)] =
                og::kTowerGateLevel;
            ensure_tower_campaign_entry(disk);
            disk.tower_best_floor =
                std::max(disk.tower_best_floor, session_best);
            disk.tower_run_seed = save.tower_run_seed; // keep (shareable)
            if (!disk.save("save0"))
                LogError("tower: run-end save0 write failed\n");
            save.tower_best_floor = disk.tower_best_floor;
        }
        else
        {
            // No readable checkpoint: best-effort write of the reset live
            // save so a relaunch still starts at the Gate.
            LogError("tower: run-end save0 reload failed; writing live\n");
            (void)save.save("save0");
        }
    }

    std::optional<ModePopup> ending_popup(
        const SaveData& save, const GameWorld& world,
        const LevelOutcome& outcome) const override
    {
        const int floor_number = world.id - og::kTowerGateLevel;
        if (outcome.ending == 0)
        {
            // The win fold (advance_cursor) runs BEFORE the results dispatch
            // (§2.4), so save.scen_num is the POST-advance cursor. When
            // advance_cursor HELD the cursor (destination provisioning
            // failed), never claim the next floor awaits — the player is
            // about to replay the floor they just won.
            const int cursor_floor = save.scen_num - og::kTowerGateLevel;
            const bool advanced = cursor_floor > floor_number;
            if (!advanced)
            {
                if (floor_number <= 0)
                    return ModePopup{
                        "THE TOWER OPENS",
                        "The stair could not be raised.\n"
                        "The Gate stands again."};
                return ModePopup{
                    "FLOOR CLEARED",
                    std::format("Floor {} conquered.\n"
                                "The stair could not be raised -\n"
                                "Floor {} stands again.",
                                floor_number, floor_number)};
            }
            if (floor_number <= 0)
                return ModePopup{"THE TOWER OPENS", "Floor 1 awaits."};
            return ModePopup{
                "FLOOR CLEARED",
                std::format("Floor {} conquered.\nFloor {} awaits.",
                            floor_number, floor_number + 1)};
        }
        if (floor_number <= 0)
            return std::nullopt; // Gate loss: the generic popups suffice
        return ModePopup{
            outcome.withdrawn ? "THE CLIMB ABANDONED" : "THE TOWER CLAIMS YOU",
            format_tower_loss(world.id, save.tower_best_floor,
                              save.tower_run_seed, outcome.withdrawn)};
    }

    std::vector<std::string> results_summary_lines(
        const SaveData& save, const GameWorld& world) const override
    {
        if (world.id <= og::kTowerGateLevel)
            return {};
        return {format_tower_summary(world.id, save.tower_best_floor)};
    }
};

} // namespace

IProgression& tower_progression()
{
    static TowerProgression instance;
    return instance;
}

std::string format_tower_summary(int world_id, short best_floor)
{
    return std::format("Floor {} conquered - best {}",
                       world_id - og::kTowerGateLevel, best_floor);
}

std::string format_tower_loss(int world_id, short best_floor,
                              std::uint32_t run_seed, bool withdrawn)
{
    const int floor_number = world_id - og::kTowerGateLevel;
    if (withdrawn)
        return std::format("You left the Tower on Floor {}.\nBest climb: {}.",
                           floor_number, best_floor);
    return std::format("You fell on Floor {}.\nBest climb: {}.\nSeed {:08X}",
                       floor_number, best_floor, run_seed);
}

} // namespace og::mode
