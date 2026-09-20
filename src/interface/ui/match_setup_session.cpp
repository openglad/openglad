/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* The SETUP wizard's step machine and the shared terminal driver (#304,
 * docs/match-setup-design.md §3.1/§3.3). Three renderers stand on this one
 * file: the SDL engine screen, openglad_text and openglad_curses.
 *
 * NO RULE LIVES HERE. Every face is a picker_common formatter, every write a
 * picker_common cycler, every level answer the CampaignPickerSession
 * contract. What this file owns is WHICH rows and lines a step carries, in
 * what order and for whom — and nothing else.
 */

#include <openglad/interface/ui/match_setup_session.h>

#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

namespace og::ui {
namespace {

std::string upper(std::string text)
{
    uppercase(text);
    return text;
}

// The scenario title the MATCH step heads with, and the manifest ARENA
// row's label. An unreadable or nameless scenario says its number rather
// than the loader's "none".
std::string scenario_title_or_number(int level)
{
    std::string title;
    const bool ok = og::data::load_scenario_title_with_error(
                        ("scen" + std::to_string(level)).c_str(), title) ==
                    og::data::LevelFileIoError::None;
    if (!ok || title.empty())
        return std::format("SCEN {}", level);
    return title;
}

// The RULES note for a shared row id (§2.5). One note per row, budgeted
// against that row's longest label.
std::string_view rules_note(std::string_view id)
{
    if (id == kRulesRowScore)
        return kSetupNoteScore;
    if (id == kRulesRowTime)
        return kSetupNoteTime;
    return {};
}

MatchSetupSession::Row::Knob rules_knob(std::string_view id)
{
    using Knob = MatchSetupSession::Row::Knob;
    if (id == kRulesRowScore)
        return Knob::Score;
    if (id == kRulesRowTime)
        return Knob::Time;
    return Knob::None;
}

// An outcome that carries nothing but its kind.
MatchSetupSession::Outcome bare(MatchSetupSession::OutcomeKind kind)
{
    MatchSetupSession::Outcome out;
    out.kind = kind;
    return out;
}

// A plain C++ row: the label/note pair with no book decoration.
MatchSetupSession::Row plain_row(std::string_view id, std::string label,
                                 std::string_view note,
                                 CampaignPickerSession::Kind kind)
{
    MatchSetupSession::Row row;
    row.base.id = std::string(id);
    row.base.label = std::move(label);
    row.base.note = std::string(note);
    row.base.kind = kind;
    return row;
}

} // namespace

MatchSetupSession::MatchSetupSession(SaveData& save)
    : save_(save), book_(save)
{
}

std::string_view MatchSetupSession::step_word(Step step)
{
    return kSetupStepWords[static_cast<std::size_t>(step)];
}

const SaveData& MatchSetupSession::read(const Inputs& inputs) const
{
    return inputs.save != nullptr ? *inputs.save : save_;
}

void MatchSetupSession::fetch_knobs()
{
    // The hook resets `knobs_` to defaults on every call and answers false
    // when the campaign carries none — defaults are then the answer.
    (void)og::script::hooks::campaign_match_knobs(knobs_);
}

void MatchSetupSession::build_steps()
{
    steps_.clear();
    if (has_book_)
        steps_.push_back(Step::Game);
    steps_.push_back(Step::Arena);
    steps_.push_back(Step::Teams);
    steps_.push_back(Step::Rules);
    steps_.push_back(Step::Match);
}

bool MatchSetupSession::has_step(Step step) const
{
    return std::find(steps_.begin(), steps_.end(), step) != steps_.end();
}

bool MatchSetupSession::open(const Inputs& inputs, std::string_view entry_page)
{
    // The classic guard: a campaign with no matchup has no arena to set up.
    if (!is_versus_campaign(read(inputs)))
        return false;

    has_book_ = book_.open();
    fetch_knobs();
    build_steps();
    // The bookless ARENA list, fetched ONCE: the level browser's own source
    // (no new scan, and src/interface/ui/level_picker.cpp is untouched).
    manifest_levels_ = list_levels_v();
    arena_manifest_ = !has_book_;
    step_ = steps_.front();

    if (has_book_ && !entry_page.empty())
    {
        const std::vector<CampaignPickerSession::Row>& rows = book_.page().rows;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].id != entry_page ||
                rows[i].kind != CampaignPickerSession::Kind::Page)
            {
                continue;
            }
            if (book_.choose(i).kind ==
                CampaignPickerSession::OutcomeKind::OpenedPage)
            {
                step_ = Step::Arena;
                arena_manifest_ = false;
            }
            break;
        }
    }

    compose(inputs, step_ == Step::Arena ? Window::OnCurrent : Window::Reset);
    return true;
}

void MatchSetupSession::refetch(const Inputs& inputs)
{
    if (has_book_)
        book_.refresh();
    fetch_knobs();
    compose(inputs, Window::Keep);
}

void MatchSetupSession::level_applied(const Inputs& inputs)
{
    if (has_book_)
        book_.refresh();
    fetch_knobs();
    step_ = Step::Teams;
    compose(inputs, Window::Reset);
}

void MatchSetupSession::page_step(int delta)
{
    page_.page.step(delta);
}

// Every entry to ARENA resolves the page first: the book at depth >= 2 is
// already the player's own choice (a no-op refetch), otherwise the root row
// the campaign named in match_knobs.arena_page. A key that names no root
// page row falls back to the mount's manifest with its trace, so the step is
// never empty.
void MatchSetupSession::enter_arena(const Inputs& inputs)
{
    step_ = Step::Arena;
    if (!has_book_)
    {
        arena_manifest_ = true;
        return;
    }
    if (book_.depth() >= 2)
    {
        arena_manifest_ = false;
        return;
    }
    const std::string& key = knobs_.arena_page;
    if (!key.empty())
    {
        const std::vector<CampaignPickerSession::Row>& rows = book_.page().rows;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].id != key ||
                rows[i].kind != CampaignPickerSession::Kind::Page)
            {
                continue;
            }
            if (book_.choose(i).kind ==
                CampaignPickerSession::OutcomeKind::OpenedPage)
            {
                arena_manifest_ = false;
                return;
            }
            break;
        }
    }
    arena_manifest_ = true;
    TRACE("setup", "%s", std::string(kSetupArenaPageUnresolvedTrace).c_str());
    (void)inputs;
}

MatchSetupSession::Outcome MatchSetupSession::goto_step(Step target,
                                                        const Inputs& inputs)
{
    if (!has_step(target))
        return bare(OutcomeKind::Stayed);
    if (target == Step::Arena)
    {
        enter_arena(inputs);
        compose(inputs, Window::OnCurrent);
        return bare(OutcomeKind::Advanced);
    }
    if (target == Step::Game)
    {
        // The GAME tab / PREV pops the book back to its root.
        while (has_book_ && book_.depth() > 1)
        {
            if (!book_.back())
                break;
        }
        arena_manifest_ = !has_book_;
    }
    step_ = target;
    compose(inputs, Window::Reset);
    return bare(OutcomeKind::Advanced);
}

MatchSetupSession::Step MatchSetupSession::next_step() const
{
    switch (step_)
    {
        // NEXT on GAME means "keep what is set": it skips ARENA rather than
        // forcing a level choice the player did not ask to make.
        case Step::Game:
        case Step::Arena:
            return Step::Teams;
        case Step::Teams:
            return Step::Rules;
        case Step::Rules:
            return Step::Match;
        case Step::Match:
            break;
    }
    return step_;
}

MatchSetupSession::Step MatchSetupSession::prev_step() const
{
    switch (step_)
    {
        case Step::Match:
            return Step::Rules;
        case Step::Rules:
            return Step::Teams;
        case Step::Teams:
            return Step::Arena;
        case Step::Arena:
            if (has_book_)
                return Step::Game;
            break;
        case Step::Game:
            break;
    }
    return step_;
}

MatchSetupSession::Outcome MatchSetupSession::next(const Inputs& inputs)
{
    if (!page_.can_next)
        return bare(OutcomeKind::Stayed);
    return goto_step(next_step(), inputs);
}

MatchSetupSession::Outcome MatchSetupSession::prev(const Inputs& inputs)
{
    if (!page_.can_prev)
        return bare(OutcomeKind::Stayed);
    return goto_step(prev_step(), inputs);
}

// --- Composition ---------------------------------------------------------

void MatchSetupSession::compose(const Inputs& inputs, Window window)
{
    const int previous_page = page_.page.page;
    page_ = Page{};
    page_.title = std::string(kSetupTitlePrefix) + std::string(step_word(step_));

    switch (step_)
    {
        case Step::Game:
            compose_book_page(inputs);
            break;
        case Step::Arena:
            if (arena_manifest_)
                compose_manifest(inputs);
            else
                compose_book_page(inputs);
            break;
        case Step::Teams:
            compose_teams(inputs);
            break;
        case Step::Rules:
            compose_rules(inputs);
            break;
        case Step::Match:
            compose_match(inputs);
            break;
    }

    page_.can_next = step_ != Step::Match;
    page_.can_prev = step_ != steps_.front();

    const int lines = static_cast<int>(page_.lines.size() +
                                       page_.team_lines.size());
    page_.page = PageModel::make(static_cast<int>(page_.rows.size()),
                                 std::max(1, setup_rows_fit(lines)));
    if (window == Window::Keep)
    {
        page_.page.page = std::clamp(previous_page, 0,
                                     std::max(0, page_.page.page_count() - 1));
    }
    else if (window == Window::OnCurrent)
    {
        for (std::size_t i = 0; i < page_.rows.size(); ++i)
        {
            if (!page_.rows[i].base.current)
                continue;
            page_.page.page =
                static_cast<int>(i) / std::max(1, page_.page.rows_per_page);
            break;
        }
    }
}

// The book's own page, hosted: its lines and its rows, undecorated by the
// wizard. The campaign wrote them; the wizard only frames them.
void MatchSetupSession::compose_book_page(const Inputs& inputs)
{
    const SaveData& save = read(inputs);
    const CampaignPickerSession::DecoratedPage& book = book_.page();
    page_.lines = book.lines;
    for (const CampaignPickerSession::Row& row : book.rows)
    {
        Row out;
        out.base = row;
        // The wizard's [CLEARED] seam (R2-4), through the ONE predicate:
        // Multiplayer Arenas carries no progress vocabulary, and the
        // MatchSetupSession is versus-only by construction (open() refuses
        // a classic campaign), so every other campaign's book keeps its
        // tail. [CURRENT] stays — it is the only mark that says which
        // arena is armed.
        //
        // `cleared` ALSO feeds Row::replay_arms()
        // (campaign_picker_session.h), read by choose(), so on a versus
        // campaign no wizard level row ever arms a replay. That is correct
        // — versus has no earned roads and the campaign sets no `replay` —
        // and it is PINNED, not incidental.
        out.base.cleared = progress_marks_shown(save) && out.base.cleared;
        page_.rows.push_back(std::move(out));
    }
}

// The bookless ARENA list: every level of the mount in id order, with the
// save's CURRENT decoration, and CLEARED only where progress marks are
// shown (R2-4 — never on a versus campaign, which is the only kind that
// reaches this session).
void MatchSetupSession::compose_manifest(const Inputs& inputs)
{
    const SaveData& save = read(inputs);
    for (const int level : manifest_levels_)
    {
        Row out;
        out.base.id = std::to_string(level);
        out.base.label = scenario_title_or_number(level);
        out.base.kind = CampaignPickerSession::Kind::Level;
        out.base.level = level;
        out.base.cleared =
            progress_marks_shown(save) && save.is_level_completed(level);
        out.base.current = static_cast<int>(save.scen_num) == level;
        out.base.available = true;
        page_.rows.push_back(std::move(out));
    }
}

void MatchSetupSession::compose_teams(const Inputs& inputs)
{
    const SaveData& save = read(inputs);
    const std::array<LineupTeamBand, 4> bands = build_lineup_bands(
        save, inputs.players, inputs.local_indices, inputs.networked,
        lineup_power_for_guy, inputs.seat_short_name,
        inputs.map_unit_counts);

    if (knobs_.teams)
    {
        // One line per AUTHORED team, ascending. A mask of 0 (nothing loaded
        // or metadata not yet synchronized) would blank the step, so it
        // falls back to every band that has a seat, a fighter or a word.
        for (int team = 0; team < 4; ++team)
        {
            const LineupTeamBand& band =
                bands[static_cast<std::size_t>(team)];
            const bool authored =
                inputs.authored_mask != 0
                    ? (inputs.authored_mask & (1u << team)) != 0
                    : (band.seat_count > 0 || band.fighter_count > 0 ||
                       save.fill[static_cast<std::size_t>(team)] !=
                           og::sim::kFillNone);
            if (!authored)
                continue;
            const SetupTeamLineCells cells = compose_setup_team_line(
                band, inputs.staged, team, 18, 20);
            page_.team_lines.push_back(TeamLine{team, cells.label, cells.seats,
                                                cells.census, cells.diag});
        }
        page_.team_lines_at = 0;
        // The fix for NEEDS k FIGHTERS is not on this step, so the line says
        // where it is.
        const bool any_diag =
            std::any_of(page_.team_lines.begin(), page_.team_lines.end(),
                        [](const TeamLine& line) { return line.diag; });
        if (any_diag)
            page_.lines.emplace_back(kSetupPointerLine);
    }
    else if (inputs.staged != nullptr)
    {
        // No per-team macro: the staged report's own lines describe the band
        // mode or the onslaught army honestly, and VIEW LEVEL already owns
        // that composition.
        page_.lines = format_scenario_report_lines(*inputs.staged);
    }

    for (const std::string& line : knobs_.lines)
        page_.lines.push_back(line);
    if (!inputs.is_host)
        page_.lines.emplace_back(kHostSetsForEveryoneCaption);
    if (page_.lines.size() + page_.team_lines.size() >
        static_cast<std::size_t>(kSetupLinesMax))
    {
        page_.lines.resize(static_cast<std::size_t>(kSetupLinesMax) -
                           std::min(page_.team_lines.size(),
                                    static_cast<std::size_t>(kSetupLinesMax)));
    }

    // The knobs are the HOST's; a joiner keeps the read-only LINEUP door.
    if (inputs.is_host)
    {
        if (knobs_.teams &&
            match_sides_count(inputs.authored_mask) > 2)
        {
            // One legal value is not a wheel: a two-side arena has no row.
            Row sides = plain_row(
                kSetupRowSides,
                match_sides_face(save, inputs.my_team, inputs.authored_mask),
                match_sides_note(inputs.authored_mask),
                CampaignPickerSession::Kind::Action);
            sides.extra = Row::Extra::Cycler;
            sides.knob = Row::Knob::Sides;
            page_.rows.push_back(std::move(sides));
        }
        if (knobs_.fill == og::script::hooks::CampaignFillKnob::Macro)
        {
            Row fill = plain_row(
                kSetupRowFill,
                match_fill_face(save, inputs.my_team, inputs.authored_mask),
                kMatchFillNote, CampaignPickerSession::Kind::Action);
            fill.extra = Row::Extra::Cycler;
            fill.knob = Row::Knob::Fill;
            page_.rows.push_back(std::move(fill));
        }
        else if (knobs_.fill == og::script::hooks::CampaignFillKnob::Band)
        {
            // Team 1's own wheel, in the SAME word LINEUP uses for the same
            // knob — renaming it here would be a second vocabulary.
            // ...and in that wheel's OWN note: the band wheel keeps NONE,
            // the macro wheel does not (R2-2). One note per wheel.
            Row fill = plain_row(kSetupRowFill,
                                 format_lineup_fill_label(save.fill[0]),
                                 kLineupFillNote,
                                 CampaignPickerSession::Kind::Action);
            fill.extra = Row::Extra::Cycler;
            fill.knob = Row::Knob::BandFill;
            page_.rows.push_back(std::move(fill));
        }
    }

    Row lineup = plain_row(kSetupRowLineup, std::string(kSetupLineupRowLabel),
                           kSetupLineupRowNote,
                           CampaignPickerSession::Kind::Page);
    lineup.extra = Row::Extra::Door;
    page_.rows.push_back(std::move(lineup));
}

void MatchSetupSession::compose_rules(const Inputs& inputs)
{
    const SaveData& save = read(inputs);
    const MatchRulesInputs rules{&save, knobs_.score, knobs_.time,
                                 inputs.session_difficulty, inputs.networked};
    // R2-3: RULES is the two scenario knobs and nothing else. The other
    // seven faces the shared composer answers belong to the Base Camp
    // DIFFICULTY door, which is back on the strip for every campaign; the
    // MATCH step's recap still states all of them, because it states the
    // whole match.
    std::vector<MatchRuleFace> faces;
    for (const MatchRuleFace& face : match_rules_faces(rules))
    {
        if (std::find(kSetupRulesRows.begin(), kSetupRulesRows.end(),
                      face.id) != kSetupRulesRows.end())
        {
            faces.push_back(face);
        }
    }

    if (!inputs.is_host)
    {
        // Cut the row, print the line (the established joiner grammar).
        // The step carries no rows at all now, so there is no fact that
        // could be a line AND a row on it.
        page_.lines.emplace_back(kHostSetsForEveryoneCaption);
        for (std::string& line : format_match_rules_lines(faces))
            page_.lines.push_back(std::move(line));
        return;
    }

    // A campaign that hides both knobs still gets the step (the stepper is
    // hard-coded and NEXT/PREV must land somewhere), so it says why it is
    // empty before it says where the rest of the rules are.
    if (faces.empty())
        page_.lines.emplace_back(kSetupRulesMapLine);
    page_.lines.emplace_back(kSetupRulesPointerLine);

    for (const MatchRuleFace& face : faces)
    {
        // The shared formatter was upper-cased at its one composer; the
        // session never re-spells or re-cases a face.
        Row row = plain_row(face.id, face.face, rules_note(face.id),
                            CampaignPickerSession::Kind::Action);
        row.extra = Row::Extra::Cycler;
        row.knob = rules_knob(face.id);
        page_.rows.push_back(std::move(row));
    }
}

void MatchSetupSession::compose_match(const Inputs& inputs)
{
    const SaveData& save = read(inputs);
    page_.lines.push_back(upper(scenario_title_or_number(save.scen_num)));

    if (inputs.staged != nullptr)
    {
        // The report's own leading refusal lines, taken from the ONE
        // formatter so nothing here re-spells them; only HOW MANY lead is
        // known here.
        const std::size_t lead = (inputs.staged->stage_failed ? 1u : 0u) +
                                 (inputs.staged->unavailable ? 1u : 0u);
        if (lead > 0)
        {
            const std::vector<std::string> report_lines =
                format_scenario_report_lines(*inputs.staged);
            for (std::size_t i = 0; i < std::min(lead, report_lines.size());
                 ++i)
            {
                page_.lines.push_back(report_lines[i]);
            }
        }
        page_.team_lines_at = page_.lines.size();
        for (int team = 0; team < 4; ++team)
        {
            if (!inputs.staged->team_active[static_cast<std::size_t>(team)])
                continue;
            page_.team_lines.push_back(
                TeamLine{team,
                         format_scenario_report_team_line(*inputs.staged, team),
                         {}, {}, false});
        }
    }
    else
    {
        page_.team_lines_at = page_.lines.size();
    }

    // The rules recap, two faces per line, dropped LAST-first when the ten
    // lines run out (CROSS CONTROL's line goes first).
    const std::size_t spent = page_.lines.size() + page_.team_lines.size();
    const std::size_t room =
        spent >= static_cast<std::size_t>(kSetupLinesMax)
            ? 0u
            : static_cast<std::size_t>(kSetupLinesMax) - spent;
    std::vector<std::string> rules = format_match_rules_lines(
        MatchRulesInputs{&save, knobs_.score, knobs_.time,
                         inputs.session_difficulty, inputs.networked});
    if (rules.size() > room)
        rules.resize(room);
    for (std::string& line : rules)
        page_.lines.push_back(std::move(line));

    Row view = plain_row(kSetupRowViewLevel,
                         std::string(kSetupViewLevelRowLabel),
                         kSetupViewLevelRowNote,
                         CampaignPickerSession::Kind::Page);
    view.extra = Row::Extra::Door;
    page_.rows.push_back(std::move(view));

    if (!inputs.is_host)
    {
        // The joiner's GO slot holds the pointer row: it REPLACES the row,
        // so it takes the row's position, never a line.
        Row ready = plain_row(kSetupRowReadyPointer,
                              std::string(kSetupJoinerReadyRow), {},
                              CampaignPickerSession::Kind::Action);
        ready.state = RowState::Disabled;
        page_.rows.push_back(std::move(ready));
        return;
    }

    // GO is live exactly when the strip GO would launch, and says why when
    // it would not — the face IS the refusal (§2.6, first match wins).
    Row go = plain_row(kSetupRowGo, std::string(kSetupGoFace), {},
                       CampaignPickerSession::Kind::Action);
    go.extra = Row::Extra::Go;
    using Health = IPickerLobbyClient::StagedPreviewHealth;
    if (!local_seats_deployed_for_go(save, inputs.players, inputs.networked))
    {
        go.base.label = std::string(kSetupGoDeployFace);
        go.state = RowState::Disabled;
    }
    else if (inputs.staged_health == Health::None)
    {
        go.base.label = std::string(kSetupGoStagingFace);
        go.state = RowState::Disabled;
    }
    else if (inputs.staged_health == Health::Failed ||
             inputs.staged_health == Health::Unavailable)
    {
        go.base.label = std::string(kSetupGoStagingFailedFace);
        go.state = RowState::Disabled;
    }
    page_.rows.push_back(std::move(go));
}

// --- choose --------------------------------------------------------------

MatchSetupSession::Outcome MatchSetupSession::turn(Row::Knob knob, int dir,
                                                   const Inputs& inputs)
{
    Outcome out = bare(OutcomeKind::Turned);
    out.knob = knob;
    switch (knob)
    {
        case Row::Knob::Sides:
            turn_match_sides(save_, inputs.my_team, inputs.authored_mask,
                             dir);
            break;
        case Row::Knob::Fill:
            turn_match_fill(save_, inputs.my_team, inputs.authored_mask,
                            dir);
            break;
        case Row::Knob::BandFill:
            save_.fill[0] = cycle_lineup_fill(save_.fill[0], dir);
            break;
        case Row::Knob::Score:
            cycle_ctf_capture_limit(save_, dir);
            break;
        case Row::Knob::Time:
            cycle_time_limit(save_, dir);
            break;
        case Row::Knob::None:
            break;
    }
    return out;
}

MatchSetupSession::Outcome MatchSetupSession::choose(std::size_t row, int dir,
                                                     const Inputs& inputs)
{
    if (row >= page_.rows.size())
        return bare(OutcomeKind::Stayed);
    const Row picked = page_.rows[row];

    if (picked.state == RowState::Disabled)
    {
        TRACE("setup", "disabled_row %s", picked.base.id.c_str());
        Outcome out = bare(OutcomeKind::Refused);
        out.message = picked.base.label;
        return out;
    }

    // The book's own rows: the CampaignPickerSession contract, unchanged.
    const bool book_row = (step_ == Step::Game) ||
                          (step_ == Step::Arena && !arena_manifest_);
    if (book_row)
    {
        if (picked.base.kind == CampaignPickerSession::Kind::Level)
        {
            Outcome out = bare(OutcomeKind::SetLevel);
            out.level = picked.base.level;
            out.replay_arm = picked.base.replay_arms();
            return out;
        }
        const CampaignPickerSession::Outcome answer = book_.choose(row);
        switch (answer.kind)
        {
            case CampaignPickerSession::OutcomeKind::OpenedPage:
                if (step_ == Step::Game)
                {
                    step_ = Step::Arena;
                    arena_manifest_ = false;
                }
                compose(inputs, Window::OnCurrent);
                return bare(OutcomeKind::Advanced);
            case CampaignPickerSession::OutcomeKind::Acted:
            {
                if (answer.level >= 0)
                {
                    // R2-5 / D3: an action that answered with a level goes
                    // through the surface's EXISTING gated level tail,
                    // exactly as a level row does — one gated tail, not a
                    // second level path. An Acted level never arms a
                    // replay (#207: only level ROWS arm). No compose()
                    // here: level_applied() recomposes after the tail. The
                    // book's message is dropped on a level answer — the
                    // engine's "Level set to <arena>." is the click's one
                    // answer.
                    Outcome out = bare(OutcomeKind::SetLevel);
                    out.level = answer.level;
                    out.replay_arm = false;
                    return out;
                }
                // The book acted and refetched itself; its toast is the
                // campaign's own voice.
                Outcome out = bare(OutcomeKind::Stayed);
                out.message = book_.take_message();
                compose(inputs, Window::Keep);
                return out;
            }
            case CampaignPickerSession::OutcomeKind::Refused:
            {
                Outcome out = bare(OutcomeKind::Refused);
                out.message = answer.reason;
                return out;
            }
            case CampaignPickerSession::OutcomeKind::SetLevel:
            case CampaignPickerSession::OutcomeKind::None:
                break;
        }
        return bare(OutcomeKind::Stayed);
    }

    // The manifest ARENA list answers SetLevel exactly as a book level row
    // does, and writes nothing.
    if (step_ == Step::Arena)
    {
        Outcome out = bare(OutcomeKind::SetLevel);
        out.level = picked.base.level;
        out.replay_arm = picked.base.replay_arms();
        return out;
    }

    if (picked.base.id == kSetupRowLineup)
        return bare(OutcomeKind::OpenLineup);
    if (picked.base.id == kSetupRowViewLevel)
        return bare(OutcomeKind::OpenViewLevel);
    if (picked.extra == Row::Extra::Go)
        return bare(OutcomeKind::Go);

    if (picked.extra == Row::Extra::Cycler)
    {
        Outcome out = turn(picked.knob, dir, inputs);
        compose(inputs, Window::Keep);
        return out;
    }

    return bare(OutcomeKind::Stayed);
}

// --- The shared terminal driver -----------------------------------------

std::string terminal_seat_short_name(std::uint8_t player_index)
{
    const int slot = static_cast<int>(player_index);
    if (slot < 0 || slot >= MAX_PLAYERS)
        return {};
    if (og::runtime::current_session == nullptr ||
        og::runtime::current_session->input_hw_ == nullptr)
    {
        return {};
    }
    return og::input::mapping_short_name(
        og::input::current_mapping_name(slot));
}

void run_terminal_match_setup(SaveData& save, const TerminalMatchSetupIo& io)
{
    MatchSetupSession session(save);
    std::array<int, 4> counts{};
    ScenarioRosterReport report;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    bool opened = false;

    for (;;)
    {
        // The deal runs at the top of every prompt, exactly as present_menu
        // runs it, so the TEAMS prompt after an arena pick already reads the
        // dealt word.
        if (io.base.is_host() &&
            deal_arena_lineup_for_cursor(save, io.level_hooks()))
        {
            io.autosave();
        }

        counts = {};
        report = ScenarioRosterReport{};
        const IPickerLobbyClient::StagedPreviewHealth health =
            io.census(counts, report);

        players = synthesize_local_lobby_players(save);
        local_indices.clear();
        for (const og::sim::LobbyPlayer& player : players)
            local_indices.push_back(player.player_index);

        MatchSetupSession::Inputs inputs;
        inputs.save = &save;
        inputs.is_host = io.base.is_host();
        inputs.networked = false;  // terminal pickers are local-only today
        inputs.my_team = first_local_seat_team(save);
        inputs.session_difficulty = io.difficulty();
        inputs.authored_mask =
            ctf_authored_team_mask_for_save(save, io.level_hooks());
        inputs.players = players;
        inputs.local_indices = local_indices;
        inputs.map_unit_counts = counts;
        // The seat cell names the CONTROLLER (the client's own seat
        // source), never the company clipped to three glyphs.
        inputs.seat_short_name = io.seat_short_name;
        // The census writes `report` on EVERY arm, so the page always reads
        // it: a degraded preview still says so in the report's own words
        // (STAGING FAILED / PREVIEW UNAVAILABLE lead the MATCH step, and the
        // teams=false TEAMS step prints the report lines). `health` alone
        // drives the GO face.
        inputs.staged = &report;
        inputs.staged_health = health;

        if (!opened)
        {
            if (!session.open(inputs))
            {
                // Unreachable for a player: the terminal camp's SETUP row
                // calls the wizard only on a versus campaign. Kept as the
                // driver's own guard, and silent — the notice it printed
                // was Team Build item 13's gate line, and that item is
                // retired.
                TRACE("setup", "open_refused");
                return;
            }
            opened = true;
        }
        else
        {
            session.refetch(inputs);
        }

        const TerminalMatchSetupModel model =
            build_terminal_match_setup_model(session, inputs);
        std::vector<std::string> lines = model.lines;
        for (std::size_t i = 0; i < model.items.size(); ++i)
        {
            lines.push_back(
                std::format("  {:2}. {}", i + 1, model.items[i].label));
        }
        const std::optional<std::string> answer = io.base.prompt(
            model.title, lines,
            std::format("Setup # [1-{}] (0 = back, N- steps a wheel back): ",
                        model.items.size()));
        if (!answer || answer->empty() || *answer == "0")
            return;

        // "N" steps a wheel forward, "N-" steps it back — the `<` cell's
        // projection onto a prompt.
        std::string digits = *answer;
        int dir = 1;
        if (digits.size() > 1 && digits.back() == '-')
        {
            dir = -1;
            digits.pop_back();
        }
        const std::optional<int> choice = parse_int_strict(digits);
        if (!choice || *choice < 1 ||
            static_cast<std::size_t>(*choice) > model.items.size())
        {
            io.base.notice(std::string(kSetupInvalidRowNotice));
            continue;
        }
        const TerminalMatchSetupItem item =
            model.items[static_cast<std::size_t>(*choice - 1)];
        if (dir < 0 && !item.reversible)
        {
            io.base.notice(std::string(kSetupInvalidRowNotice));
            continue;
        }

        if (item.kind == TerminalMatchSetupItem::Kind::Back)
            return;
        if (item.kind == TerminalMatchSetupItem::Kind::Next)
        {
            (void)session.next(inputs);
            continue;
        }
        if (item.kind == TerminalMatchSetupItem::Kind::Prev)
        {
            (void)session.prev(inputs);
            continue;
        }

        // Copy what the outcome report needs: choose() replaces the page.
        const MatchSetupSession::Row picked = session.page().rows[item.row];
        using Kind = MatchSetupSession::OutcomeKind;
        const MatchSetupSession::Outcome outcome =
            session.choose(item.row, dir, inputs);
        switch (outcome.kind)
        {
            case Kind::SetLevel:
            {
                if (picked.base.kind == CampaignPickerSession::Kind::Action)
                {
                    // R2-D15: the camp's own two-kind convention — a level
                    // ROW speaks its row label
                    // (campaign_picker_session.cpp, the camp's level arm),
                    // an ACTION answering a level speaks the LOADED title.
                    // The wizard is not a third spelling.
                    terminal_route_acted_level(
                        save, io.base, outcome.level, std::string(),
                        [&] { session.level_applied(inputs); });
                    break;
                }
                const TerminalLevelSetGate gate = terminal_level_set_gate(
                    io.base, save, outcome.level, !picked.base.available,
                    picked.base.current, outcome.replay_arm);
                if (gate != TerminalLevelSetGate::Applied)
                    break;
                session.level_applied(inputs);
                io.base.notice(outcome.replay_arm
                                   ? campaign_replay_set_message(
                                         picked.base.label)
                                   : campaign_level_set_message(
                                         picked.base.label));
                break;
            }
            case Kind::Turned:
                // Every wizard knob rides the .gtl now: SIDES, FILL and the
                // band wheel write fill[], SCORE and TIME LIMIT their own
                // fields. The session-only knobs (INFINITE GOLD, CROSS
                // CONTROL) left with R2-3, so the exclusion left with them.
                // No notice: a turn says nothing, because its redrawn face
                // is the answer.
                io.autosave();
                break;
            case Kind::Refused:
                io.base.notice(outcome.message);
                break;
            case Kind::OpenLineup:
                io.base.notice(std::string(kSetupTerminalLineupNotice));
                break;
            case Kind::OpenViewLevel:
                io.base.notice(std::string(kSetupTerminalViewLevelNotice));
                break;
            case Kind::Go:
                io.base.notice(std::string(kSetupTerminalGoNotice));
                break;
            case Kind::Stayed:
            case Kind::Advanced:
            case Kind::Closed:
                // Outcome::message is the ONE channel, read exactly the way
                // the SDL Stayed arm reads it (menu_screen_specs.cpp): a
                // book action's Lua voice reaches a terminal now.
                if (!outcome.message.empty())
                    io.base.notice(outcome.message);
                break;
        }
    }
}

} // namespace og::ui
