/* Match-plan phase: the plain-data census handed to on_mode_plan and the
 * parsed summary of the plan table it answers.
 *
 * The plan phase (docs/matched-teams-design.md) is a PURE Lua level hook —
 * on_mode_plan(level, inputs) -> plan — dispatched under a machine-enforced
 * fence that errors every world og.* entry. Its inputs therefore carry only
 * numbers and tables, produced by ONE C++ projection,
 * build_match_plan_inputs(world), shared by the launch path
 * (hooks::level_mode_init chains plan -> init through the Lua stack) and
 * the picker preview (hooks::level_mode_plan parses the plan into
 * MatchPlanSummary). The preview overwrites only the four request fields
 * (from SaveData) and the per-team roster counts (from the save or the
 * lobby rosters) — the one sanctioned input-marshaling remnant; every
 * activation/fill RULE lives in the mode Lua alone.
 *
 * Leaf-header discipline: includes only <array>/<cstdint>/<string>/<vector>,
 * forward-declares GameWorld.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class GameWorld;

namespace og::sim {

// The CTF flag treasure's wire byte — the slot campaigns author and the
// modes.core pack claims outright (families/treasure-flag.lua wire_id 13,
// the retired C++ CTF engine's og::FAMILY_FLAG). The census carries the
// raw {team, level} rows of these entities so the first-flag-per-team
// domain rule (kept flags, surplus kills, the per-map capture limit) stays
// 100% in the pack Lua.
inline constexpr int kMatchPlanFlagFamily = 13;

// Longest fill label the summary parser keeps verbatim for an unknown
// (future-pack) fill vocabulary entry.
inline constexpr std::size_t kMatchPlanFillLabelMax = 20;

struct MatchPlanTeamInputs
{
    std::int32_t anchors = 0;     // respawn.anchor_count[t] (dead markers included, capped)
    std::int32_t roster = 0;      // live has_guy livings (launch) / deployed company count (preview)
    std::int32_t npcs = 0;        // live guy-less livings
    std::int32_t generators = 0;  // live generators
};

struct MatchPlanFlagInput
{
    std::int32_t team = 0;        // raw team byte, unfiltered (out-of-range included)
    std::int32_t level = 0;       // stats level (the per-map capture-limit channel)
};

struct MatchPlanInputs
{
    std::int32_t team_count = 0;     // raw ctf_requested_team_count (0 = Auto)
    std::int32_t strip_troops = 0;   // raw (0 KEEP / 2 OWN / 3 FAIR)
    std::int32_t score_limit = 0;    // raw requested (0 = unset)
    std::int32_t respawn_ticks = 0;  // raw requested (0 = unset)
    std::array<MatchPlanTeamInputs, 4> teams{};
    std::vector<MatchPlanFlagInput> flags;  // live flag treasures, fx-list order
};

// The one census producer. Reads the oblist (live livings split by myguy,
// live generators), the fx list (flag rows), the engine anchor counts
// (respawn_scan_anchors must have run — launch step 0 does; the preview
// runs it on its disposable scratch world) and the four ctf_requested_*
// knobs. Never mutates.
MatchPlanInputs build_match_plan_inputs(const GameWorld& world);

// Fill vocabulary of plan.teams[t].fill. Unknown strings map to Other with
// the raw label carried verbatim (clipped to kMatchPlanFillLabelMax) so a
// future pack can extend the vocabulary without an engine change.
enum class MatchPlanFill : std::uint8_t {
    Company,     // "company"  — deployed roster walkers
    Troops,      // "troops"   — authored map troops survive (TROOPS: ALL)
    Bots,        // "bots"     — legacy difficulty squad
    Matched,     // "matched"  — FAIR measure-and-solve squad
    Generators,  // "generators" — onslaught foundries only (D17 no-bots)
    Empty,       // "empty"    — active with no forces (onslaught E8)
    Other,       // anything else — label carried verbatim
};

struct MatchPlanTeamSummary
{
    bool active = false;
    MatchPlanFill fill = MatchPlanFill::Empty;
    std::string fill_label;  // raw plan string (clipped), "" when absent
    std::int32_t count = 0;
};

struct MatchPlanSummary
{
    std::string mode_name;       // plan.mode ("SOCCER", ...)
    bool starts = false;
    std::string reason;          // plan.reason when starts is false
    std::uint8_t authored_mask = 0;
    std::uint8_t active_mask = 0;   // meaningful only when starts
    bool matched = false;
    std::int32_t matched_size = 0;
    std::int32_t score_limit = 0;   // fully resolved (request > map > default)
    bool seeded_squads = false;     // bot classes drawn at first spawn
    std::array<MatchPlanTeamSummary, 4> teams{};
};

} // namespace og::sim
