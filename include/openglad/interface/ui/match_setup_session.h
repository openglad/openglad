/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The SETUP wizard's step machine (#304, docs/match-setup-design.md §3.1).
// SDL-free: GAME -> ARENA -> TEAMS -> RULES -> MATCH, composed once here and
// rendered by all three surfaces (the SDL engine screen, openglad_text and
// openglad_curses through the shared terminal driver below).
//
// NO RULE LIVES HERE. Every face is a picker_common formatter, every write a
// picker_common cycler, every level answer the CampaignPickerSession contract
// (a level row answers SetLevel; the renderer runs its own gated tail and
// calls level_applied). The session's whole job is WHICH rows and lines a
// step carries, in what order, for whom — and which campaign knobs shape
// that (the match_knobs hook).

#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class SaveData;

namespace og::ui {

// The model's grid facts — the ONE home of these nine names in og::ui.
// picker_sdl_defs.h is ALSO `namespace og::ui`, so it must NOT redefine them
// (a redefinition would not compile); it includes this header and adds only
// the x/w/tab/cell/footer/team-column/ordinal constants, with static_asserts
// tying these to the zone constants (kSetupLinePitch ==
// CampaignZoneSession::kTextLinePitch, kSetupRowPitch == kZoneSubmenuRowPitch,
// kSetupPanelBottomY == kZoneSubmenuPanelBottomY). docs/match-setup-design.md
// §2.0's "declared once, picker_sdl_defs.h" is satisfied for the SDL-only
// names; the MODEL needs rows_fit (it windows its own rows), so these live
// here, where the model can see them without an SDL header.
inline constexpr int kSetupLinesMax = 10;  // C++ steps; hosted Lua pages keep
                                           // kCampaignPageMaxLines (6)
inline constexpr int kSetupContentY0 = 47, kSetupLinePitch = 8,
                     kSetupLineToRowGap = 4, kSetupRowPitch = 12,
                     kSetupPanelBottomY = 158, kSetupRowsMax = 9;
[[nodiscard]] constexpr int setup_row_y0(int lines)
{
    return lines == 0 ? kSetupContentY0
                      : kSetupContentY0 + kSetupLinePitch * lines +
                            kSetupLineToRowGap;
}
// 9,8,7,6,6,5,4,4,3,2,2 for lines 0..10.
[[nodiscard]] constexpr int setup_rows_fit(int lines)
{
    return (kSetupPanelBottomY - setup_row_y0(lines)) / kSetupRowPitch;
}
static_assert(setup_rows_fit(0) == kSetupRowsMax);
static_assert(setup_rows_fit(1) == 8 && setup_rows_fit(2) == 7 &&
              setup_rows_fit(3) == 6 && setup_rows_fit(6) == 4 &&
              setup_rows_fit(7) == 4);

// Every player-visible string the session composes ITSELF. Budgets
// (docs/match-setup-design.md §2.8): a row label plus " - " plus its note is
// <= 42 glyphs; a line is <= 48.
inline constexpr std::string_view kSetupStepWords[] = {"GAME", "ARENA",
                                                       "TEAMS", "RULES",
                                                       "MATCH"};
inline constexpr std::string_view kSetupTitlePrefix = "SETUP: ";
inline constexpr std::string_view kSetupPointerLine =
    "Deploy and seats: the Base Camp roster and rail.";  // 47 <= 48
inline constexpr std::string_view kSetupLineupRowLabel = "LINEUP";
inline constexpr std::string_view kSetupLineupRowNote = "fill per team, map units";
inline constexpr std::string_view kSetupViewLevelRowLabel = "VIEW LEVEL";
inline constexpr std::string_view kSetupViewLevelRowNote = "the arena and every team";
inline constexpr std::string_view kSetupGoFace = "GO";
// 28. The popup title and the dimmed face cannot drift: the face IS
// "GO - " + WP2's kDeployForEveryPlayerTitle, asserted below.
inline constexpr std::string_view kSetupGoDeployFace =
    "GO - DEPLOY FOR EVERY PLAYER";
static_assert(kSetupGoDeployFace.substr(0, 5) == std::string_view("GO - ") &&
              kSetupGoDeployFace.substr(5) == kDeployForEveryPlayerTitle);
inline constexpr std::string_view kSetupGoStagingFace = "GO - STAGING";  // 12
inline constexpr std::string_view kSetupGoStagingFailedFace =
    "GO - STAGING FAILED";  // 19
inline constexpr std::string_view kSetupJoinerReadyRow =
    "READY is on the Base Camp strip.";  // 31

// NO kSetupFillBandNote / kSetupSidesNote* here: the FILL note is WP2's
// kMatchFillNote and the SIDES note is WP2's match_sides_note(mask) — one
// spelling each.

// The RULES notes, one per row (§2.5), keyed by the shared kRulesRow* ids.
inline constexpr std::string_view kSetupNoteScore = "map, 1, 3, 5, 10";
inline constexpr std::string_view kSetupNoteTime = "map, 5 to 20 min";
inline constexpr std::string_view kSetupNoteRespawns = "off to team 1";
inline constexpr std::string_view kSetupNoteSpawnDelay = "normal, fast, slow";
inline constexpr std::string_view kSetupNotePermadeath = "on, off";
inline constexpr std::string_view kSetupNoteGenerators = "calm to frenzy";
inline constexpr std::string_view kSetupNoteDifficulty = "up to slaughter";
inline constexpr std::string_view kSetupNoteInfiniteGold = "gold never runs out";
inline constexpr std::string_view kSetupNoteCrossControl = "own, all";

// The two terminal Custom-gate guards (the SDL strip twin's prompt face):
// one door for the match rules per campaign kind on every client.
inline constexpr std::string_view kSetupClassicGuardMessage =
    "This campaign has no arena setup.";  // 33
inline constexpr std::string_view kSetupDifficultyVersusGuardMessage =
    "The fight's rules are on SETUP: RULES.";  // 38
// The terminal driver's answer to an unparsable prompt line.
inline constexpr std::string_view kSetupInvalidRowNotice = "Invalid setup row.";
// TRACE("setup", ...) when match_knobs.arena_page names no root page row.
inline constexpr std::string_view kSetupArenaPageUnresolvedTrace =
    "arena_page_unresolved";

// The row ids the C++ steps own. A book page's rows keep the BOOK's ids
// (the wizard hosts the campaign's own page; renaming its rows would break
// every campaign that keys on them); the manifest ARENA list uses the level
// id spelled as a string.
inline constexpr std::string_view kSetupRowSides = "sides";
inline constexpr std::string_view kSetupRowFill = "fill";
inline constexpr std::string_view kSetupRowLineup = "lineup";
inline constexpr std::string_view kSetupRowViewLevel = "view_level";
inline constexpr std::string_view kSetupRowGo = "go";
inline constexpr std::string_view kSetupRowReadyPointer = "ready_pointer";

// One SDL-free SETUP wizard. The renderer owns every tail (lobby sync,
// autosave, the level-set gate, the difficulty write); the session owns the
// step, the window and the composition.
class MatchSetupSession {
public:
    enum class Step : std::uint8_t { Game, Arena, Teams, Rules, Match };

    struct Row {
        // id/label/note/kind/level/cleared/current/available/replay — the
        // decorated row every surface already knows how to draw.
        CampaignPickerSession::Row base;
        enum class Extra : std::uint8_t { None, Cycler, Door, Go } extra =
            Extra::None;
        enum class Knob : std::uint8_t {
            None, Sides, Fill, BandFill, Score, Time, Respawns, SpawnDelay,
            Permadeath, Generators, Difficulty, InfiniteGold, CrossControl
        } knob = Knob::None;
        // Disabled = the gated GO with its reason on its face, the joiner's
        // READY pointer, the joiner's read-only CROSS CONTROL row.
        RowState state = RowState::Visible;
    };

    // The three text cells behind a team line's colour swatch. On the MATCH
    // step `label` is the whole report line and the other two are empty.
    struct TeamLine {
        int team = 0;
        std::string label, seats, census;
        bool diag = false;
    };

    struct Page {
        std::string title;                 // "SETUP: TEAMS"
        std::vector<std::string> lines;    // <= kSetupLinesMax
        std::vector<TeamLine> team_lines;
        // The index in `lines` BEFORE which team_lines render (TEAMS: 0;
        // MATCH: 1, after the title line). Both renderers and the terminal
        // projection obey it; nothing else reorders.
        std::size_t team_lines_at = 0;
        std::vector<Row> rows;
        PageModel page;
        bool can_next = false;
        bool can_prev = false;
    };

    // Borrowed per call, never cached.
    struct Inputs {
        // The save the session reads AND writes. It must be the same object
        // the constructor borrowed; a null field falls back to it, so a
        // caller that only has the session can still refetch.
        const SaveData* save = nullptr;
        bool is_host = true;
        bool networked = false;
        int my_team = 0;
        int session_difficulty = 0;
        // The loaded arena's authored_team_mask (the deal's own reader).
        std::uint8_t authored_mask = 0;
        std::span<const og::sim::LobbyPlayer> players;
        std::span<const std::uint8_t> local_indices;
        std::span<const int> map_unit_counts;
        const ScenarioRosterReport* staged = nullptr;  // nullptr = none
        IPickerLobbyClient::StagedPreviewHealth staged_health =
            IPickerLobbyClient::StagedPreviewHealth::None;
    };

    enum class OutcomeKind : std::uint8_t {
        Stayed, Advanced, SetLevel, Turned, SetDifficulty, OpenLineup,
        OpenViewLevel, Go, Refused, Closed
    };

    struct Outcome {
        OutcomeKind kind = OutcomeKind::Stayed;
        int level = -1;
        bool replay_arm = false;
        Row::Knob knob = Row::Knob::None;
        int difficulty = 0;
        std::string message;
    };

    explicit MatchSetupSession(SaveData& save);

    // Open at the book's root (the GAME step). `entry_page` names a root row
    // to descend into (the docket's ARENA: shortcut) — an id that names no
    // root PAGE row (the docket's "games" alias, "") stays on the root.
    // FALSE only for a classic campaign: the SDL door never opens there and
    // the terminal gate prints kSetupClassicGuardMessage. A campaign with no
    // book is not a failure — its ARENA step lists the mount's manifest.
    bool open(const Inputs& inputs, std::string_view entry_page = "");

    [[nodiscard]] const Page& page() const { return page_; }
    [[nodiscard]] std::span<const Step> steps() const { return steps_; }
    [[nodiscard]] Step step() const { return step_; }
    // The campaign's current answer. WP7's GAME-entry highlight reads
    // knobs().arena_page to find the row to highlight.
    [[nodiscard]] const og::script::hooks::CampaignMatchKnobs& knobs() const
    {
        return knobs_;
    }

    // dir = +1 (a click) / -1 (the "<" cell, the terminal "N-", right-click).
    Outcome choose(std::size_t row, int dir, const Inputs& inputs);
    Outcome next(const Inputs& inputs);
    Outcome prev(const Inputs& inputs);
    // The tab strip / the terminal Prev-Next. Arena descends into
    // knobs_.arena_page.
    Outcome goto_step(Step target, const Inputs& inputs);
    // The ARENA window pagers.
    void page_step(int delta);
    // The surface's gated tail landed: refetch the book + match_knobs and
    // advance to TEAMS.
    void level_applied(const Inputs& inputs);
    // Cadence: entry, own mutation, the level-reload guard, the settings
    // fingerprint, a stage generation bump.
    void refetch(const Inputs& inputs);
    // The pending toast, cleared by the read.
    std::string take_message();

    [[nodiscard]] static std::string_view step_word(Step step);

    // Where next()/prev() land — the ONE spelling of the step order, so the
    // terminal's "Next: RULES" item can never name a step the stepper does
    // not go to. NEXT on GAME skips ARENA ("keep what is set"); on the last
    // present step each answers its own step.
    [[nodiscard]] Step next_step() const;
    [[nodiscard]] Step prev_step() const;

private:
    // How a recompose treats the row window.
    enum class Window : std::uint8_t {
        Reset,      // page 0 (a fresh step)
        Keep,       // the same page index, clamped (a refetch)
        OnCurrent,  // the window holding the row whose `current` is true
    };

    [[nodiscard]] const SaveData& read(const Inputs& inputs) const;
    void fetch_knobs();
    void build_steps();
    void compose(const Inputs& inputs, Window window);
    void compose_book_page(const Inputs& inputs);
    void compose_manifest(const Inputs& inputs);
    void compose_teams(const Inputs& inputs);
    void compose_rules(const Inputs& inputs);
    void compose_match(const Inputs& inputs);
    void enter_arena(const Inputs& inputs);
    [[nodiscard]] bool has_step(Step step) const;
    Outcome turn(Row::Knob knob, int dir, const Inputs& inputs);

    SaveData& save_;
    CampaignPickerSession book_;  // depth 1 = GAME, depth >= 2 = ARENA
    bool has_book_ = false;
    // The ARENA step is showing the mount's manifest, not a book page
    // (bookless campaign, or an arena_page that named no root page row).
    bool arena_manifest_ = false;
    Step step_ = Step::Game;
    Page page_;
    std::string message_;
    og::script::hooks::CampaignMatchKnobs knobs_;
    std::vector<Step> steps_;
    std::vector<int> manifest_levels_;
};

// --- The shared terminal driver ------------------------------------------
//
// ONE prompt loop for both terminal clients (the run_terminal_campaign_camp
// precedent). The clients supply I/O and their own tails; every line, guard
// and dispatch arm lives here, so the text and curses wizards are
// byte-identical by construction.
struct TerminalMatchSetupIo {
    // prompt / notice / is_host / apply_level.
    TerminalCampaignPickerIo base;
    // The per-prompt deal's level reader (the present_menu cadence).
    std::function<const LevelDataHooks&()> level_hooks;
    // autosave_company_after_mutation.
    std::function<void()> autosave;
    // The client writes the VALUE and says "Difficulty set to X." — one
    // value-taking tail per client, called by the wizard with the session's
    // computed value and by the client's own cycling case with
    // cycle_difficulty(current). One tail, two callers.
    std::function<void(int)> set_difficulty;
    // The session difficulty the RULES page reads.
    std::function<int()> difficulty;
    // The staged census: fills `counts` (map units per team) and `report`,
    // and answers how healthy the preview is.
    std::function<IPickerLobbyClient::StagedPreviewHealth(
        std::array<int, 4>& counts, ScenarioRosterReport& report)>
        census;
};

// The terminal wizard's pointer notices. Terminals reach LINEUP and VIEW
// LEVEL from their own Team Build / Scenario menus and launch from Team
// Build item 6, so the wizard's doors say where the page is instead of
// nesting a second copy of it. Whether WP8 nests the existing pages
// instead is that brief's call; the words are here so the two clients
// cannot answer differently in the meantime.
inline constexpr std::string_view kSetupTerminalLineupNotice =
    "LINEUP is Team Build item 12.";
inline constexpr std::string_view kSetupTerminalViewLevelNotice =
    "VIEW LEVEL is on the Scenario menu.";
inline constexpr std::string_view kSetupTerminalGoNotice =
    "GO is Team Build item 6.";

void run_terminal_match_setup(SaveData& save, const TerminalMatchSetupIo& io);

} // namespace og::ui
