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

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace og::ui {

namespace hooks = og::script::hooks;

namespace {

// The shared row decoration (level label fill + CLEARED/CURRENT + action
// affordability) behind BOTH sessions — the picker's pages and the zone's
// actions widgets must agree on decoration.
void decorate_campaign_entries(const SaveData& save,
                               std::vector<hooks::CampaignPageEntry>& entries,
                               std::vector<CampaignPickerSession::Row>& out)
{
    using Kind = CampaignPickerSession::Kind;
    out.clear();
    out.reserve(entries.size());
    for (hooks::CampaignPageEntry& entry : entries)
    {
        CampaignPickerSession::Row row;
        row.id = std::move(entry.id);
        row.label = std::move(entry.label);
        row.note = std::move(entry.note);
        row.kind = entry.kind;
        row.level = entry.level;
        row.cost = entry.cost;
        row.done = entry.done;
        if (row.kind == Kind::Level)
        {
            if (row.label.empty())
            {
                // The save-side label fill: the scenario title, with the
                // provider's ""-on-failure contract (load_scenario_title's
                // "none" sentinel would read as a real title). The SAME read
                // answers whether the road is in the campaign at all, so a
                // row the engine had to name for the book gets named CLOSED
                // when there is nothing there to name. A book that labels
                // its own level rows owns them — no extra read, no guess.
                std::string title;
                const bool loaded =
                    og::data::load_scenario_title_with_error(
                        ("scen" + std::to_string(row.level)).c_str(),
                        title) == og::data::LevelFileIoError::None;
                row.available = loaded;
                if (!loaded || title.empty())
                    title = std::format("SCEN {}", row.level);
                row.label = std::move(title);
            }
            row.cleared = save.is_level_completed(row.level);
            row.current = static_cast<int>(save.scen_num) == row.level;
        }
        if (row.kind == Kind::Action)
            row.affordable = campaign_picker_can_afford(save, row.cost);
        out.push_back(std::move(row));
    }
}

// The one action executor behind BOTH sessions (the C++-owns-the-cost rule:
// debit FIRST — skipped under infinite gold — then dispatch; a spend already
// applied sticks even if the action ERRORS, but a book with no picker_action
// hook at all must not charge for rows it can never honor). The caller owns
// its own refetch.
CampaignPickerSession::Outcome perform_campaign_entry_action(
    SaveData& save, const CampaignPickerSession::Row& row,
    std::string& message_out)
{
    using OutcomeKind = CampaignPickerSession::OutcomeKind;
    if (row.done)
    {
        return {OutcomeKind::Refused, 0,
                std::string(kCampaignActionDoneMessage)};
    }
    if (row.cost > 0 && !campaign_picker_can_afford(save, row.cost))
        return {OutcomeKind::Refused, 0, "Not enough gold."};
    campaign_picker_debit(save, row.cost);
    hooks::CampaignActionResult result;
    if (!hooks::campaign_picker_action(row.id, result))
    {
        campaign_picker_refund(save, row.cost);
        return {OutcomeKind::Refused, 0, "This book takes no orders."};
    }
    message_out = std::move(result.message);
    return {OutcomeKind::Acted, 0, {}};
}

} // namespace

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
    decorate_campaign_entries(save_, raw.entries, out.rows);
    return true;
}

bool CampaignPickerSession::open()
{
    return open_at(std::string());
}

bool CampaignPickerSession::open_at(const std::string& page_id)
{
    stack_.clear();
    message_.clear();
    if (!hooks::campaign_picker_registered())
        return false;
    DecoratedPage root;
    if (!fetch(page_id, root))
        return false;
    stack_.push_back(page_id);
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
            // A row pointing at a level that is not in the campaign is
            // already marked CLOSED and drawn spent by the surfaces; the
            // click still runs the caller's load-with-rollback tail (the
            // decoration is a fetch-old read, the load is the truth), which
            // refuses in the campaign's voice, not the loader's.
            // The session never writes scen_num — the caller applies its
            // client-specific level-set tail (host gate included).
            return {OutcomeKind::SetLevel, row.level, {}};
        case Kind::Action:
        {
            const Outcome outcome =
                perform_campaign_entry_action(save_, row, message_);
            if (outcome.kind == OutcomeKind::Acted)
            {
                // Re-request the page so labels/state re-derive.
                DecoratedPage refreshed;
                if (fetch(stack_.back(), refreshed))
                    page_ = std::move(refreshed);
            }
            return outcome;
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
    // A retired purchase stops quoting its price (the "KIT OWNED 60g" sin).
    if (row.cost > 0 && !row.done)
        text += std::format("  {}g", row.cost);
    // Every row kind carries its own tail marker, so a player reading down a
    // list can tell the door from the purchase from the row that starts a
    // battle without clicking one to find out.
    switch (row.kind)
    {
        case CampaignPickerSession::Kind::Level:
            if (!row.available)
                text += "  [CLOSED]";
            else if (row.current)
                text += "  [CURRENT]";
            else if (row.cleared)
                text += "  [CLEARED]";
            break;
        case CampaignPickerSession::Kind::Page:
            text += "  >";  // the repo's door grammar ("TEAM >")
            break;
        case CampaignPickerSession::Kind::Action:
            if (row.done)
                text += "  [DONE]";
            break;
    }
    return clip_with_ellipsis(std::move(text), budget);
}

// --- Base Camp gameplay-zone session (docs/basecamp-zones-design.md) -------

namespace {

// The built-in DEFAULT composition: one full-capability roster (all caps
// default on, no locks, no assign). Rendered through the SAME widget path
// as a scripted zone — one renderer, byte-identical to the pre-zone screen.
hooks::CampaignZone default_campaign_zone()
{
    hooks::CampaignZone zone;
    zone.widgets.emplace_back();  // Kind::Roster, every capability true
    return zone;
}

// A text widget's whole-row-unit share: lines render on an 8px pitch inside
// a 14px-unit band (1 line -> 1 unit, 6 lines -> 4 units).
int text_widget_units(int lines)
{
    const int ink = std::max(1, lines) * 8;
    return (ink + 13) / 14;
}

} // namespace

CampaignZoneSession::CampaignZoneSession(SaveData& save)
    : save_(save)
{
}

bool CampaignZoneSession::adopt(const hooks::CampaignZone& zone)
{
    roster_ = RosterLayout{};
    texts_.clear();
    actions_.clear();
    readout_.reset();

    // Pass 1 — per-widget row units (weight > 0 is an explicit unit count;
    // 0 takes the kind's default share; the roster resolves in pass 2 and
    // takes every remaining unit).
    struct Slot {
        const hooks::CampaignZoneWidget* widget = nullptr;
        int units = 0;
    };
    std::vector<Slot> slots;
    slots.reserve(zone.widgets.size());
    int roster_slot = -1;
    int used = 0;
    for (const hooks::CampaignZoneWidget& widget : zone.widgets)
    {
        Slot slot{&widget, 0};
        switch (widget.kind)
        {
            case hooks::CampaignZoneWidget::Kind::Roster:
                roster_slot = static_cast<int>(slots.size());
                break;
            case hooks::CampaignZoneWidget::Kind::Text:
                slot.units = widget.weight > 0
                    ? widget.weight
                    : text_widget_units(
                          static_cast<int>(widget.lines.size()));
                break;
            case hooks::CampaignZoneWidget::Kind::Actions:
                slot.units = widget.weight > 0
                    ? widget.weight
                    : std::clamp(static_cast<int>(widget.entries.size()), 1,
                                 3);
                break;
            case hooks::CampaignZoneWidget::Kind::Readout:
                slot.units = widget.weight > 0 ? widget.weight : 1;
                break;
        }
        used += slot.units;
        slots.push_back(slot);
    }
    if (roster_slot < 0)
        return false;  // parser-enforced for scripted zones; defensive here

    // Pass 1b — hoist the readout into the panel's header band. A roster
    // that does not lead the composition moves its column headings into the
    // grid and abandons the classic header slot at y=33; leaving that 14px
    // band blank at the top of the panel reads as a broken screen while the
    // roster below it pages two rows at a time. The readout is exactly one
    // row of summary cells, so it takes the band as the panel's heading and
    // gives its grid unit back to the roster.
    int hoisted_readout = -1;
    if (roster_slot != 0)
    {
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i].widget->kind !=
                hooks::CampaignZoneWidget::Kind::Readout)
            {
                continue;
            }
            used -= slots[i].units;
            slots[i].units = 0;
            hoisted_readout = static_cast<int>(i);
            break;
        }
    }

    // Pass 2 — the roster takes the remainder (its header spends one unit
    // whenever it is not the first widget; the first-widget header keeps
    // the classic y=33 spot outside the grid).
    const int header_units = roster_slot == 0 ? 0 : 1;
    const hooks::CampaignZoneWidget& roster_widget =
        *slots[static_cast<std::size_t>(roster_slot)].widget;
    const int roster_rows = roster_widget.weight > 0
        ? roster_widget.weight
        : kZoneRowUnits - used - header_units;
    if (roster_rows < kRosterMinRows ||
        used + header_units + roster_rows > kZoneRowUnits)
    {
        return false;  // over-budget: the caller falls to the default zone
    }
    slots[static_cast<std::size_t>(roster_slot)].units =
        roster_rows + header_units;

    // Pass 3 — assign band starts in script order and build the views.
    int unit = 0;
    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index)
    {
        const Slot& slot = slots[slot_index];
        const hooks::CampaignZoneWidget& widget = *slot.widget;
        switch (widget.kind)
        {
            case hooks::CampaignZoneWidget::Kind::Roster:
                roster_.can_deploy = widget.can_deploy;
                roster_.can_train = widget.can_train;
                roster_.can_reorder = widget.can_reorder;
                roster_.can_team = widget.can_team;
                roster_.can_hire = widget.can_hire;
                roster_.locks = widget.locks;
                roster_.assign = widget.assign;
                roster_.start_unit = unit;
                roster_.header_at_top = header_units == 0;
                roster_.row_start_unit = unit + header_units;
                roster_.rows_per_page = roster_rows;
                break;
            case hooks::CampaignZoneWidget::Kind::Text:
            {
                TextLayout text;
                text.lines = widget.lines;
                text.start_unit = unit;
                text.units = slot.units;
                texts_.push_back(std::move(text));
                break;
            }
            case hooks::CampaignZoneWidget::Kind::Actions:
            {
                ActionsLayout actions;
                std::vector<hooks::CampaignPageEntry> entries =
                    widget.entries;
                decorate_campaign_entries(save_, entries, actions.rows);
                actions.start_unit = unit;
                actions.units = slot.units;
                actions.page = PageModel::make(
                    static_cast<int>(actions.rows.size()), slot.units);
                actions_.push_back(std::move(actions));
                break;
            }
            case hooks::CampaignZoneWidget::Kind::Readout:
            {
                ReadoutLayout readout;
                readout.items = widget.items;
                readout.start_unit = unit;
                readout.in_header_band =
                    static_cast<int>(slot_index) == hoisted_readout;
                readout_ = std::move(readout);
                break;
            }
        }
        unit += slot.units;
    }
    return true;
}

void CampaignZoneSession::fetch()
{
    hooks::CampaignZone raw;
    if (hooks::campaign_zone(raw) && adopt(raw))
    {
        scripted_ = true;
        return;
    }
    // No hook, an erroring hook, a malformed composition, or an
    // over-budget layout: the built-in default composition — the SAME
    // renderer, full capability.
    scripted_ = false;
    const bool ok = adopt(default_campaign_zone());
    (void)ok;  // the default is one 8-row roster; it always lays out
}

void CampaignZoneSession::refetch()
{
    std::vector<int> pages;
    pages.reserve(actions_.size());
    for (const ActionsLayout& actions : actions_)
        pages.push_back(actions.page.page);
    fetch();
    for (std::size_t i = 0;
         i < actions_.size() && i < pages.size(); ++i)
    {
        actions_[i].page.page = std::clamp(
            pages[i], 0, actions_[i].page.page_count() - 1);
    }
}

CampaignZoneSession::ActionsLayout*
CampaignZoneSession::actions_widget(int index)
{
    if (index < 0 || index >= static_cast<int>(actions_.size()))
        return nullptr;
    return &actions_[static_cast<std::size_t>(index)];
}

CampaignZoneSession::Outcome CampaignZoneSession::act(int widget_index,
                                                      int row_index)
{
    const ActionsLayout* actions = actions_widget(widget_index);
    if (actions == nullptr || row_index < 0 ||
        row_index >= static_cast<int>(actions->rows.size()))
    {
        return {};
    }
    // Copy: the Acted refetch replaces the rows under the one we read.
    const Row row = actions->rows[static_cast<std::size_t>(row_index)];
    if (row.kind != CampaignPickerSession::Kind::Action)
        return {};  // Level/Page rows belong to the surface's own tails
    const Outcome outcome =
        perform_campaign_entry_action(save_, row, message_);
    if (outcome.kind == OutcomeKind::Acted)
        refetch();
    return outcome;
}

std::string CampaignZoneSession::take_message()
{
    std::string message = std::move(message_);
    message_.clear();
    return message;
}

const hooks::CampaignRosterLock*
CampaignZoneSession::deploy_lock_for_tag(int tag) const
{
    for (const hooks::CampaignRosterLock& lock : roster_.locks)
    {
        if (lock.unset ? tag == 0 : lock.tag == tag)
            return &lock;
    }
    return nullptr;
}

int CampaignZoneSession::next_assign_tag(int tag)
{
    // unset -> 1 -> 2 -> 1, never back to unset; junk normalizes to 1.
    return tag == 1 ? 2 : 1;
}

char CampaignZoneSession::assign_glyph(
    const hooks::CampaignAssignSpec& spec, int tag)
{
    if (tag < 1 || tag > static_cast<int>(spec.labels.size()))
        return '-';
    const std::string& label = spec.labels[static_cast<std::size_t>(tag - 1)];
    if (label.empty())
        return '-';
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(label.front())));
}

namespace {

std::string upper_clipped(std::string value, std::size_t budget)
{
    for (char& c : value)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return clip_with_ellipsis(std::move(value), budget);
}

} // namespace

std::string CampaignZoneSession::assign_cell_text(
    const hooks::CampaignAssignSpec& spec, int tag, std::size_t budget)
{
    if (tag < 1 || tag > static_cast<int>(spec.labels.size()))
        return "-";
    const std::string& label = spec.labels[static_cast<std::size_t>(tag - 1)];
    if (label.empty())
        return "-";
    return upper_clipped(label, budget);
}

std::string CampaignZoneSession::assign_header_text(
    const hooks::CampaignAssignSpec& spec, std::size_t budget)
{
    if (spec.key.empty())
        return "OATH";
    return upper_clipped(spec.key, budget);
}

bool CampaignZoneSession::settings_fingerprint_changed()
{
    // The lobby-synced knobs an applied settings change rewrites under the
    // open screen (recon: apply_state_to_save / the networked apply path).
    // scen_num is deliberately EXCLUDED: level changes already refetch
    // through the frame-tick reload guard, and double-triggering would hide
    // a broken guard from the tests.
    const std::string composed = std::format(
        "{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
        save_.current_campaign, save_.allied_mode, save_.ctf_team_count,
        save_.ctf_capture_limit, save_.ctf_respawn_ticks,
        save_.ctf_strip_scenario_troops, save_.respawn_mode,
        save_.generator_rate, save_.keep_fallen_heroes, save_.cross_control,
        save_.infinite_gold);
    const std::uint64_t fingerprint =
        static_cast<std::uint64_t>(std::hash<std::string>{}(composed));
    if (!fingerprint_seeded_)
    {
        fingerprint_seeded_ = true;
        settings_fingerprint_ = fingerprint;
        return false;
    }
    if (settings_fingerprint_ == fingerprint)
        return false;
    settings_fingerprint_ = fingerprint;
    return true;
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
