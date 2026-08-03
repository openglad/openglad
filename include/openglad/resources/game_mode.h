/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The two-tier game-mode seam. Full write-up: docs/game-modes.md.
//
// Tier A — sim-ruleset modes (CTF today) change what happens WITHIN a tick:
// win conditions, team-wipe suppression, per-tick scoring. They live in
// og_gameplay, are deterministic, snapshot-visible and parity-relevant.
// Tier A gets NO abstraction here — it gets a written recipe (see
// docs/game-modes.md §"Tier-A recipe"). The tick fork in game_world.cpp,
// respawn_suppress_team_wipe_endgame, and walker.cpp are untouched by this
// seam.
//
// Tier B — meta-progression modes (Tower Climb; future boss rush / time
// attack / draft) change what happens BETWEEN levels: sequencing, run
// lifecycle, persistence policy, results. They live here, in og_resources,
// behind og::mode::IProgression. Design theorem: a Tier-B mode must be
// implementable with no og_gameplay edits and no wire changes.
//
// Classic-respawn is NOT a mode. It is a shared engine (RespawnState
// substate with ctf.active false) that composes with modes via the
// respawn_mode knob; clamp_respawn_mode below is how a mode constrains it.
//
// Mode identity comes from campaign package metadata: the optional `mode:`
// key in campaign.yaml (absent/"classic" -> Classic; "tower" -> Tower;
// unknown -> Classic + one LogError). Nothing new is synced or wired: the
// lobby already syncs campaign_id, and the save already keys per-campaign
// cursors on campaign id.
//
// Forward sim identity: SCEN_TYPE_TOWER / GameWorld::TYPE_TOWER (bit 0x10)
// is authored into tower level files. It must NEVER be runtime-mutated
// (unlike CTF's activation-time bit drop). A level authored with both the
// CTF bit (0x8) and the tower bit (0x10) resolves CTF-FIRST; v1 content
// never authors both.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class GameWorld;
class SaveData;

namespace og::mode {

enum class ProgressionKind : short { Classic = 0, Tower = 1 };

// Filled by the calling frontend at endgame time.
struct LevelOutcome {
    short ending = 0;       // 0 = win, 1 = loss/team-wipe/timeout/withdraw,
                            // SCEN_TYPE_SAVE_ALL (4) = protect-fail
    short next_level = -1;  // sim next_level (-1 = none)
    bool networked = false;
    bool withdrawn = false; // ending==1 with a withdraw destination / quit-mission
};

struct ModePopup {
    std::string title;
    std::string body;
};

// The Tier-B interface. Instances are STATELESS singletons — all mode state
// lives in SaveData (cursor = scen_num, mode fields are version-gated
// SaveData additions) or is derived from world.id. Defaults are the Classic
// behaviors, so Classic itself carries no overrides.
class IProgression {
public:
    virtual ~IProgression() = default;
    virtual ProgressionKind kind() const = 0;

    // GO pressed with this campaign mounted, before the pre-launch save0
    // write. Ensure content exists (generate/heal/prefetch); may veto the
    // launch by returning false.
    virtual bool prepare_launch(SaveData& save, bool networked_session)
    {
        (void)save;
        (void)networked_session;
        return true;
    }

    // Idempotent content heal: make the level at save.scen_num loadable
    // (campaign-select / picker-preview hook; regenerates missing files).
    virtual void ensure_level_available(SaveData& save) { (void)save; }

    // Inside the shared win fold (og::progression::apply_win_fold):
    // provision/verify content for next_level and return the id the cursor
    // should advance to (== next_level normally; == save.scen_num to HOLD,
    // e.g. on a provisioning failure -> the player replays the current
    // level). MUST be idempotent: both screen::endgame and the shadow
    // finalize call the fold in local play.
    virtual short advance_cursor(SaveData& save, const GameWorld& world,
                                 short next_level)
    {
        (void)save;
        (void)world;
        return next_level;
    }

    // Whether a win marks completed_levels (tower: false).
    virtual bool marks_level_completed() const { return true; }

    // Whether frontends may autosave after this win (tower: true —
    // checkpointed climb; the strict-roguelike variant flips this AND gates
    // the enumerated autosave sites — documented, not built).
    virtual bool persist_after_win() const { return true; }

    // Per-mode world-knob clamps, applied in BOTH sync_world_from_save_data
    // twins right after the respawn_mode copy. Classic: identity.
    virtual short clamp_respawn_mode(short requested) const { return requested; }

    // Run terminated (team wipe / timeout / quit-mission / withdraw).
    // Classic: no-op (losses persist nothing — unchanged).
    virtual void on_run_ended(SaveData& save, const GameWorld& world,
                              const LevelOutcome& outcome)
    {
        (void)save;
        (void)world;
        (void)outcome;
    }

    // Results surfaces. Empty optional / empty vector = mode adds nothing.
    virtual bool suppress_retry() const { return false; }
    virtual std::optional<ModePopup> ending_popup(
        const SaveData&, const GameWorld&, const LevelOutcome&) const
    {
        return std::nullopt;
    }
    virtual std::vector<std::string> results_summary_lines(
        const SaveData&, const GameWorld&) const
    {
        return {};
    }
};

// campaign.yaml `mode:` string -> kind. Pure: absent/""/"classic" -> Classic,
// "tower" -> Tower, anything else -> Classic (the caller logs unknowns once).
ProgressionKind kind_for_mode_string(std::string_view mode);

// The static stateless instance for a kind. Exhaustive switch, no default:
// -Wswitch makes a missed ProgressionKind case a compile-time complaint.
IProgression& progression_for_kind(ProgressionKind kind);

// Dispatch on og::data::mounted_campaign_mode() (the mounted campaign's
// campaign.yaml `mode:` key). Unknown mode strings log once and fall back to
// Classic. No registry: modes are few, and resolution-from-mounted-campaign
// means zero staleness.
IProgression& current_progression();

// The static Classic instance (for tests).
IProgression& classic_progression();

} // namespace og::mode
