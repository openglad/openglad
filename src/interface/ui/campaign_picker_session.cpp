/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// CampaignPickerSession (issue #206): the SDL-free scripted-picker state
// machine. See the header for the contract and
// docs/campaign-scripting-design.md for the design.

#include <openglad/interface/ui/campaign_picker_session.h>

#include <openglad/core/util.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <format>
#include <utility>

namespace og::ui {

namespace hooks = og::script::hooks;

CampaignPickerSession::CampaignPickerSession(SaveData& save)
    : save_(save)
{
}

bool CampaignPickerSession::fetch(const std::string& page_id,
                                  DecoratedPage& out) const
{
    hooks::CampaignPage raw;
    if (!hooks::campaign_picker_page(page_id, raw))
        return false;

    out.title = std::move(raw.title);
    out.lines = std::move(raw.lines);
    out.rows.clear();
    out.rows.reserve(raw.entries.size());
    for (hooks::CampaignPageEntry& entry : raw.entries)
    {
        Row row;
        row.id = std::move(entry.id);
        row.label = std::move(entry.label);
        row.note = std::move(entry.note);
        row.kind = entry.kind;
        row.level = entry.level;
        row.cost = entry.cost;
        if (row.kind == Kind::Level)
        {
            if (row.label.empty())
            {
                // The save-side label fill: the scenario title, with the
                // provider's ""-on-failure contract (load_scenario_title's
                // "none" sentinel would read as a real title).
                std::string title;
                if (og::data::load_scenario_title_with_error(
                        ("scen" + std::to_string(row.level)).c_str(),
                        title) != og::data::LevelFileIoError::None ||
                    title.empty())
                {
                    title = std::format("SCEN {}", row.level);
                }
                row.label = std::move(title);
            }
            row.cleared = save_.is_level_completed(row.level);
            row.current = static_cast<int>(save_.scen_num) == row.level;
        }
        if (row.kind == Kind::Action)
            row.affordable = campaign_picker_can_afford(save_, row.cost);
        out.rows.push_back(std::move(row));
    }
    return true;
}

bool CampaignPickerSession::open()
{
    stack_.clear();
    message_.clear();
    if (!hooks::campaign_picker_registered())
        return false;
    DecoratedPage root;
    if (!fetch(std::string(), root))
        return false;
    stack_.emplace_back();
    page_ = std::move(root);
    return true;
}

CampaignPickerSession::Outcome
CampaignPickerSession::choose(std::size_t entry_index)
{
    if (!is_open() || entry_index >= page_.rows.size())
        return {};
    // Copy: an action's refetch replaces page_ under the row we read.
    const Row row = page_.rows[entry_index];

    switch (row.kind)
    {
        case Kind::Page:
        {
            if (depth() >= kMaxPageDepth)
                return {OutcomeKind::Refused, 0, "The book goes no deeper."};
            DecoratedPage next;
            if (!fetch(row.id, next))
                return {OutcomeKind::Refused, 0, "That page cannot be read."};
            stack_.push_back(row.id);
            page_ = std::move(next);
            return {OutcomeKind::OpenedPage, 0, {}};
        }
        case Kind::Level:
            // The session never writes scen_num — the caller applies its
            // client-specific level-set tail (host gate included).
            return {OutcomeKind::SetLevel, row.level, {}};
        case Kind::Action:
        {
            if (row.cost > 0 && !campaign_picker_can_afford(save_, row.cost))
                return {OutcomeKind::Refused, 0, "Not enough gold."};
            // C++ owns the cost: debit FIRST (skipped under infinite gold),
            // then dispatch. A spend already applied sticks even if the
            // action errors (docs/campaign-scripting-design.md).
            campaign_picker_debit(save_, row.cost);
            hooks::CampaignActionResult result;
            (void)hooks::campaign_picker_action(row.id, result);
            message_ = std::move(result.message);
            // Re-request the page so labels/state re-derive.
            DecoratedPage refreshed;
            if (fetch(stack_.back(), refreshed))
                page_ = std::move(refreshed);
            return {OutcomeKind::Acted, 0, {}};
        }
    }
    return {};
}

bool CampaignPickerSession::back()
{
    if (stack_.size() <= 1)
        return false;  // at the root: the caller closes the picker
    stack_.pop_back();
    DecoratedPage prev;
    if (!fetch(stack_.back(), prev))
    {
        // A page that no longer parses answers "no scripted picker": close.
        stack_.clear();
        return false;
    }
    page_ = std::move(prev);
    return true;
}

void CampaignPickerSession::refresh()
{
    if (!is_open())
        return;
    DecoratedPage refreshed;
    if (fetch(stack_.back(), refreshed))
        page_ = std::move(refreshed);
}

std::string CampaignPickerSession::take_message()
{
    std::string message = std::move(message_);
    message_.clear();
    return message;
}

std::string campaign_picker_row_text(const CampaignPickerSession::Row& row,
                                     std::size_t budget)
{
    std::string text = row.label;
    if (!row.note.empty())
    {
        text += " - ";
        text += row.note;
    }
    if (row.cost > 0)
        text += std::format("  {}g", row.cost);
    if (row.is_level())
    {
        if (row.current)
            text += "  [CURRENT]";
        else if (row.cleared)
            text += "  [CLEARED]";
    }
    if (text.size() > budget)
        text.resize(budget);
    return text;
}

// --- Terminal MISSIONS flow (openglad_text + openglad_curses) --------------

namespace {

std::string terminal_campaign_prompt_label(std::size_t rows)
{
    if (rows == 0)
        return "Mission # (0 = back): ";
    return std::format("Mission # [1-{}] (0 = back): ", rows);
}

// The page as terminal lines: the narrative lines, a separating blank, then
// the numbered rows in the Company List "  NN. " shape, composed with the
// shared row-text helper so text and curses stay byte-identical.
std::vector<std::string> terminal_campaign_page_lines(
    const CampaignPickerSession::DecoratedPage& page)
{
    std::vector<std::string> lines = page.lines;
    if (!lines.empty() && !page.rows.empty())
        lines.emplace_back();
    for (std::size_t i = 0; i < page.rows.size(); ++i)
    {
        lines.push_back(std::format(
            "  {:2}. {}", i + 1,
            campaign_picker_row_text(page.rows[i],
                                     kCampaignPickerTerminalRowBudget)));
    }
    return lines;
}

} // namespace

void run_terminal_campaign_picker(SaveData& save,
                                  const TerminalCampaignPickerIo& io)
{
    CampaignPickerSession session(save);
    if (!session.open())
    {
        // Not registered, an erroring hook, or a malformed root page: the
        // terminal contract prints the guard line and returns to the stock
        // SCENARIO menu.
        io.notice(std::string(kCampaignPickerNoBookMessage));
        return;
    }

    for (;;)
    {
        const CampaignPickerSession::DecoratedPage& page = session.page();
        const std::optional<std::string> answer = io.prompt(
            page.title, terminal_campaign_page_lines(page),
            terminal_campaign_prompt_label(page.rows.size()));
        if (!answer || answer->empty() || *answer == "0")
        {
            // Back (blank/0/EOF): pop one page; at the root, close the book.
            if (!session.back())
                return;
            continue;
        }

        const std::optional<int> choice = parse_int_strict(*answer);
        if (!choice || *choice < 1 ||
            static_cast<std::size_t>(*choice) > page.rows.size())
        {
            io.notice("Invalid mission row.");
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(*choice - 1);
        // Copy what the outcome report needs: choose() may replace the page.
        const std::string chosen_label = page.rows[index].label;

        using Outcome = CampaignPickerSession::OutcomeKind;
        const CampaignPickerSession::Outcome outcome = session.choose(index);
        switch (outcome.kind)
        {
            case Outcome::OpenedPage:  // the loop reprints the pushed page
            case Outcome::None:        // unreachable behind the range check
                break;
            case Outcome::SetLevel:
                // Host gate first (the SET LEVEL predicate) — the session
                // stays policy-free.
                if (!io.is_host())
                {
                    io.notice(std::string(kCampaignPickerHostGuardMessage));
                    break;
                }
                io.apply_level(outcome.level);
                // CURRENT markers re-derive from the new cursor
                // (fetch-per-action, never per frame).
                session.refresh();
                io.notice(std::format("Level set to {}.", chosen_label));
                break;
            case Outcome::Acted:
            {
                // The session already debited and refetched; persist the
                // mutation (§3.8 — the campaign decision book and the
                // wallet ride the company file in the terminal clients).
                (void)company_autosave_after_mutation(save, false);
                const std::string toast = session.take_message();
                if (!toast.empty())
                    io.notice(toast);
                break;
            }
            case Outcome::Refused:
                io.notice(outcome.reason);
                break;
        }
    }
}

} // namespace og::ui
