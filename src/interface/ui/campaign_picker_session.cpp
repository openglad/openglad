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
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/level_selection.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
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
            // The scenario-header read runs for EVERY level row, labelled or
            // not: whether the campaign carries the road is a fact about the
            // road, never about the book's labelling style. Deriving it only
            // for the rows the engine had to name left a labelled row with no
            // [CLOSED] marker, a "Level set to ..." confirmation, and the
            // loader's own error at GO — and every shipped book labels its
            // level rows.
            // The earned-roads gate rides the same flag: a shipped road the
            // company has not earned is as closed as a missing one, so every
            // surface inherits the [CLOSED] face and the refusal from here.
            std::string title;
            const bool file_ok =
                og::data::load_scenario_title_with_error(
                    ("scen" + std::to_string(row.level)).c_str(), title) ==
                og::data::LevelFileIoError::None;
            row.available =
                file_ok && og::data::level_selection_allowed(save, row.level);
            if (row.label.empty())
            {
                // The save-side label fill: the scenario title, with the
                // provider's ""-on-failure contract (load_scenario_title's
                // "none" sentinel would read as a real title). A gate-closed
                // road keeps its real title — the road exists, it is just
                // not open yet.
                if (!file_ok || title.empty())
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
            {
                return {OutcomeKind::Refused, 0,
                        std::string(kCampaignPageUnreadableMessage)};
            }
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
                                     std::size_t budget,
                                     bool spell_unaffordable)
{
    std::string text = row.label;
    if (!row.note.empty())
    {
        text += " - ";
        text += row.note;
    }
    // A retired purchase stops quoting its price (the "KIT OWNED 60g" sin).
    std::string tail;
    if (row.cost > 0 && !row.done)
        tail = std::format("  {}g", row.cost);
    // Every row kind carries its own tail marker, so a player reading down a
    // list can tell the door from the purchase from the row that starts a
    // battle without clicking one to find out.
    switch (row.kind)
    {
        case CampaignPickerSession::Kind::Level:
            if (!row.available)
                tail += "  [CLOSED]";
            else if (row.current)
                tail += "  [CURRENT]";
            else if (row.cleared)
                tail += "  [CLEARED]";
            break;
        case CampaignPickerSession::Kind::Page:
            tail += "  >";  // the repo's door grammar ("TEAM >")
            break;
        case CampaignPickerSession::Kind::Action:
            if (row.done)
                tail += "  [DONE]";
            else if (spell_unaffordable && !row.affordable)
                tail += std::string("  ") + std::string(kCampaignNeedGoldMark);
            break;
    }
    // The tail is GRAMMAR, not detail. A row that overruns its face loses
    // its own words first and keeps the marker: a clip that eats the " >"
    // turns a door into a dead label, and one that eats "[CURRENT]" hides
    // the fact the row exists to state. The words say what this row is; the
    // tail says what clicking it does.
    if (text.size() + tail.size() > budget && tail.size() < budget)
        text = clip_with_ellipsis(std::move(text), budget - tail.size());
    text += tail;
    return clip_with_ellipsis(std::move(text), budget);
}

std::string campaign_oath_toast(const std::string& label, bool stood_down)
{
    std::string toast = std::format("Sworn to {}.", label);
    if (stood_down)
    {
        // The oath's price, said out loud. Swearing un-deploys, and the
        // roster's DEP count sliding from 1/1 to 0/1 while the toast talks
        // only about the oath is how a player reaches GO with an empty
        // muster and no idea why.
        toast += ' ';
        toast += kCampaignOathStoodDownMessage;
    }
    return toast;
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

// The BOOK-DOOR composition: the default roster plus ONE page row onto the
// book's root page. Every surface enters a campaign's book through a camp
// page row, so a campaign that registered a book but composed no camp would
// keep a book nobody can open — on any client. The door is named by the
// book's own root title, so it speaks in the campaign's voice rather than in
// an invented one, and it retires by itself the moment the campaign composes
// a base_camp of its own.
bool book_door_campaign_zone(hooks::CampaignZone& out)
{
    if (!hooks::campaign_picker_registered())
        return false;
    hooks::CampaignPage root;
    if (!hooks::campaign_picker_page(std::string(), root))
        return false;
    hooks::CampaignZoneWidget door;
    door.kind = hooks::CampaignZoneWidget::Kind::Actions;
    hooks::CampaignPageEntry entry;
    entry.kind = hooks::CampaignPageEntry::Kind::Page;
    entry.id.clear();  // "" is the book's root page (open_at's own spelling)
    // A root page with no title cannot happen through the parser (title is
    // required); the fallback is for a book handed in by a test seam.
    entry.label = root.title.empty() ? "THE BOOK" : root.title;
    door.entries.push_back(std::move(entry));
    out.widgets.push_back(std::move(door));
    out.widgets.emplace_back();  // Kind::Roster, every capability true
    return true;
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

// The lock made mechanical. Own, editable slots only: the tags come from
// THIS machine's book, so another player's hero is never stood down by it.
void CampaignZoneSession::enforce_deploy_locks()
{
    if (roster_.locks.empty())
        return;
    for (std::size_t slot = 0; slot < save_.team_list.size(); ++slot)
    {
        guy* const member = save_.team_list[slot].get();
        if (member == nullptr)
            continue;
        if (!member->deployed)
            continue;
        if (!picker_lobby_save_slot_editable(static_cast<int>(slot)))
            continue;
        if (deploy_lock_for_tag(member->campaign_tag) == nullptr)
            continue;
        member->deployed = false;
    }
}

void CampaignZoneSession::fetch(Enforce enforce)
{
    fetch_composition();
    if (enforce == Enforce::Locks)
        enforce_deploy_locks();
}

void CampaignZoneSession::fetch_composition()
{
    hooks::CampaignZone raw;
    if (hooks::campaign_zone(raw) && adopt(raw))
    {
        scripted_ = true;
        composed_ = true;
        return;
    }
    scripted_ = false;
    // No zone but a book: the transitional door onto the book's root page,
    // so a campaign that scripted a book before the camps existed keeps it
    // reachable on every client.
    hooks::CampaignZone door;
    if (book_door_campaign_zone(door) && adopt(door))
    {
        composed_ = true;
        return;
    }
    // No hook, an erroring hook, a malformed composition, or an
    // over-budget layout: the built-in default composition — the SAME
    // renderer, full capability.
    composed_ = false;
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

// --- Terminal campaign surfaces (openglad_text + openglad_curses) ----------

namespace {

// The §3.8 autosave tail's networked-lobby flag for BOTH terminal clients.
// Neither picker holds a networked lobby (the text client's networking is a
// stub and the curses network lobby is a separate flow), so every camp/book
// mutation is a plain owner-write, and the §4.3 ready-clear that rides the
// networked shape is a no-op. This constant is the seam: if a terminal ever
// grows a real lobby, the flag has to be threaded from the client through
// TerminalCampaignPickerIo — every tail below reads it from here.
constexpr bool kTerminalNetworkedLobbyActive = false;

// The camp's ONE prompt noun, at every depth. A book page is a room inside
// the camp, not a screen of its own, so it asks for a camp row exactly like
// the camp does — the missions door that owned the word "mission" retired
// with the v1 architecture, and no player-visible surface keeps the noun.
std::string terminal_camp_prompt_label(std::size_t choices)
{
    if (choices == 0)
        return "Camp # (0 = back): ";
    return std::format("Camp # [1-{}] (0 = back): ", choices);
}

// The out-of-range/garbage answer, same words at every depth.
constexpr std::string_view kTerminalCampInvalidRowMessage =
    "Invalid camp row.";

// The header strip both terminal camp surfaces carry: the company, its
// muster count and the PURSE. A shop that quotes prices over a screen with
// no gold on it makes the player back out to Team Build to read their own
// balance, so the strip rides the camp screen AND every book page — the
// gold cell is C++-owned on every surface, never something a campaign has
// to remember to compose.
std::string terminal_camp_header_strip(const SaveData& save)
{
    return std::format("COMPANY  DEP {}/{}  {}", count_deployed_members(save),
                       static_cast<int>(collect_base_camp_slots(save).size()),
                       format_base_camp_gold_label(save));
}

// The camp roster row's fixed stat block, in characters:
//
//   "[X] " + name(12) + " " + class(9) + " " + "L=" + level(<=3) + " " +
//   "XP=" + exp(6)
//
// The level field is TWO characters up to 99 and three beyond, so the block
// is padded (never cut) to its widest shape: everything after it — the oath
// cell, the lock reason, and the heading the header strip pads out to — is a
// column, and a column whose left edge moves when a hero dings 100 is not a
// column.
constexpr std::size_t kCampRosterStatChars =
    4 + (12 + 1) + (9 + 1) + (2 + 3 + 1) + (3 + 6);
// ...and the "  NN. " / "      " ordinal prefix every camp roster LINE wears,
// plus the two-space gap, put the oath cell here.
constexpr std::size_t kCampRosterLinePrefixChars = 6;
constexpr std::size_t kCampRosterOathColumn =
    kCampRosterLinePrefixChars + kCampRosterStatChars + 2;

// The page as terminal lines: the header strip, the narrative lines, a
// separating blank, then the numbered rows in the Company List "  NN. "
// shape, composed with the shared row-text helper so text and curses stay
// byte-identical.
std::vector<std::string> terminal_campaign_page_lines(
    const SaveData& save, const CampaignPickerSession::DecoratedPage& page)
{
    std::vector<std::string> lines;
    lines.push_back(terminal_camp_header_strip(save));
    if (!page.lines.empty() || !page.rows.empty())
        lines.emplace_back();
    lines.insert(lines.end(), page.lines.begin(), page.lines.end());
    if (!page.lines.empty() && !page.rows.empty())
        lines.emplace_back();
    for (std::size_t i = 0; i < page.rows.size(); ++i)
    {
        lines.push_back(std::format(
            "  {:2}. {}", i + 1,
            campaign_picker_row_text(page.rows[i],
                                     kCampaignPickerTerminalRowBudget, true)));
    }
    return lines;
}

// The page render/prompt loop over an already-open session. Shared by the
// root-book entry and the camp's page-row door so both spell every line,
// every guard and every tail exactly once.
void run_terminal_campaign_page_loop(CampaignPickerSession& session,
                                     SaveData& save,
                                     const TerminalCampaignPickerIo& io)
{
    for (;;)
    {
        const CampaignPickerSession::DecoratedPage& page = session.page();
        const std::optional<std::string> answer = io.prompt(
            page.title, terminal_campaign_page_lines(save, page),
            terminal_camp_prompt_label(page.rows.size()));
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
            io.notice(std::string(kTerminalCampInvalidRowMessage));
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(*choice - 1);
        // Copy what the outcome report needs: choose() may replace the page.
        const std::string chosen_label = page.rows[index].label;
        const bool chosen_closed =
            page.rows[index].is_level() && !page.rows[index].available;
        const bool chosen_current =
            page.rows[index].is_level() && page.rows[index].current;

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
                if (chosen_closed)
                {
                    // The same refusal the camp's docket gives, one click
                    // deeper: the terminal tail only moves the cursor, so a
                    // road the campaign does not carry has to be refused
                    // HERE — the SDL surface's load-with-rollback would have
                    // caught it at the click, and a row that already reads
                    // [CLOSED] must never answer "Level set to".
                    io.notice(std::string(kCampaignLevelClosedMessage));
                    break;
                }
                if (chosen_current)
                {
                    // The row the cursor is already parked on. The SDL
                    // surfaces refuse this click rather than reload the
                    // level under the player; a terminal that answered
                    // "Level set to ..." instead would be telling one
                    // player two stories about one click.
                    io.notice(std::string(kCampaignLevelUnchangedMessage));
                    break;
                }
                io.apply_level(outcome.level);
                // CURRENT markers re-derive from the new cursor
                // (fetch-per-action, never per frame).
                session.refresh();
                io.notice(campaign_level_set_message(chosen_label));
                break;
            case Outcome::Acted:
            {
                // The session already debited and refetched; persist the
                // mutation (§3.8 — the campaign decision book and the
                // wallet ride the company file in the terminal clients).
                (void)company_autosave_after_mutation(
                    save, kTerminalNetworkedLobbyActive);
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

} // namespace

void run_terminal_campaign_page(SaveData& save,
                                const TerminalCampaignPickerIo& io,
                                const std::string& page_id)
{
    CampaignPickerSession session(save);
    if (!session.open_at(page_id))
    {
        io.notice(std::string(kCampaignPageUnreadableMessage));
        return;
    }
    run_terminal_campaign_page_loop(session, save, io);
}

std::optional<std::string> terminal_roster_refusal(
    SaveData& save, TerminalRosterCommand command, int slot)
{
    // No composed camp, no rules: every unscripted campaign keeps the
    // terminals' own roster commands exactly as they were, and the fetch
    // (one Lua dispatch per command) never runs.
    if (!hooks::campaign_zone_registered())
        return std::nullopt;
    CampaignZoneSession zone(save);
    // A question, not a screen: this fetch must not stand anyone down. The
    // camp the player is looking at already enforced its locks, and moving
    // the sortie here would invert the very toggle being asked about.
    zone.fetch(CampaignZoneSession::Enforce::None);
    const CampaignZoneSession::RosterLayout& roster = zone.roster();
    switch (command)
    {
        case TerminalRosterCommand::Deploy:
        {
            if (slot < 0 || slot >= static_cast<int>(save.team_list.size()))
                return std::nullopt;  // the caller's own bounds check refuses
            const guy* member =
                save.team_list[static_cast<std::size_t>(slot)].get();
            if (member == nullptr || member->deployed)
                return std::nullopt;  // benching a deployed hero stays legal
            if (!roster.can_deploy)
                return std::string(kCampaignRosterDeployClosedMessage);
            const hooks::CampaignRosterLock* lock =
                zone.deploy_lock_for_tag(member->campaign_tag);
            if (lock == nullptr)
                return std::nullopt;
            return lock->reason.empty()
                ? std::string(kCampaignRosterDeployLockedMessage)
                : lock->reason;
        }
        case TerminalRosterCommand::Train:
            if (!roster.can_train)
                return std::string(kCampaignRosterTrainClosedMessage);
            return std::nullopt;
        case TerminalRosterCommand::Hire:
            if (!roster.can_hire)
                return std::string(kCampaignRosterHireClosedMessage);
            return std::nullopt;
    }
    return std::nullopt;
}

// --- The terminal CAMP (docs/basecamp-zones-design.md "Terminals") ---------

std::string format_campaign_camp_roster_row(
    const guy& member, const hooks::CampaignAssignSpec& assign,
    const hooks::CampaignRosterLock* lock, std::size_t budget)
{
    const BaseCampRowText columns = format_base_camp_row(member);
    // The deploy cell doubles as the padlock the SDL roster draws.
    const char deploy_cell =
        member.deployed ? 'X' : (lock != nullptr ? 'L' : ' ');
    std::string text =
        std::format("[{}] {:<12} {:<9} L={} XP={}", deploy_cell, columns.name,
                    columns.cls, columns.level, columns.exp);
    if (assign.active || (lock != nullptr && !lock->reason.empty()))
    {
        // Pad the stat block to its widest shape so every trailing column
        // starts at the same place on every row (the level field is 2 chars
        // up to 99, 3 beyond). Rows with nothing trailing keep the old
        // ragged right edge — trailing blanks are not a column.
        if (text.size() < kCampRosterStatChars)
            text.resize(kCampRosterStatChars, ' ');
    }
    if (assign.active)
    {
        text += "  ";
        text += CampaignZoneSession::assign_cell_text(
            assign, member.campaign_tag, kCampaignOathCellChars);
    }
    if (lock != nullptr && !lock->reason.empty())
    {
        // Column gap, never a dash. The unsworn oath cell IS "-", and a
        // dash separator behind it produced a "- - " stutter that reads as
        // a typo mid-sentence; every other cell on this row is separated by
        // space alone, and the heading above the oath column is what tells
        // the player what the "-" is.
        text += "  ";
        text += lock->reason;
    }
    return clip_with_ellipsis(std::move(text), budget);
}

namespace {

// The camp's screen titles. The camp is one screen with one voice; the swear
// sub-prompt is the oath row's room.
constexpr std::string_view kTerminalCampTitle = "Camp";
constexpr std::string_view kTerminalCampOathTitle = "Swear";

// What ordinal N on the camp screen does. Docket rows carry their widget
// coordinates (the zone session dispatches by widget + row); the oath row is
// the roster widget's assign door.
struct CampChoice {
    enum class Kind : std::uint8_t { Row, Oath };
    Kind kind = Kind::Row;
    int widget = 0;
    int row = 0;
};

std::string terminal_camp_oath_prompt_label(std::size_t rows)
{
    if (rows == 0)
        return "Swear # (0 = done): ";
    return std::format("Swear # [1-{}] (0 = done): ", rows);
}

// The readout as ONE line — the panel's heading in a terminal's only
// typeface: "WAGES 4350g | DEBT none | COINS 2".
std::string terminal_camp_readout_line(
    const CampaignZoneSession::ReadoutLayout& readout)
{
    std::string line;
    for (const hooks::CampaignZoneWidget::ReadoutItem& item : readout.items)
    {
        if (!line.empty())
            line += " | ";
        if (!item.label.empty())
        {
            line += item.label;
            line += ' ';
        }
        line += item.value;
    }
    return clip_with_ellipsis(std::move(line),
                              kCampaignPickerTerminalRowBudget);
}

// The oath DOOR row: what the camp screen shows for an active assign spec.
// A frozen spec keeps the column readable and states its reason on the row
// instead of wearing the door marker — the door is shut, and saying so
// before the click is the whole point of the marker vocabulary.
std::string terminal_camp_oath_row_text(const hooks::CampaignAssignSpec& spec)
{
    std::string text = "SWEAR ";
    text += CampaignZoneSession::assign_header_text(spec,
                                                    kCampaignOathCellChars);
    if (spec.frozen.empty())
        text += "  >";  // the repo's door grammar ("TEAM >")
    else
    {
        text += " - ";
        text += spec.frozen;
    }
    return clip_with_ellipsis(std::move(text),
                              kCampaignPickerTerminalRowBudget);
}

// The own roster as camp lines. `numbered` prefixes the "  NN. " ordinal the
// swear prompt addresses; the camp screen lists the same rows unnumbered
// (its ordinals belong to the docket) but column-aligned with them.
std::vector<std::string> terminal_camp_roster_lines(
    const SaveData& save, const CampaignZoneSession& zone,
    const std::vector<int>& slots, bool numbered)
{
    const hooks::CampaignAssignSpec& assign = zone.roster().assign;
    std::vector<std::string> lines;
    std::string header = terminal_camp_header_strip(save);
    if (assign.active)
    {
        // Pad the summary out to the oath column so the channel's name is a
        // COLUMN HEADING, not a third fact in the strip.
        if (header.size() + 1 < kCampRosterOathColumn)
            header.resize(kCampRosterOathColumn, ' ');
        else
            header += "  ";
        header += CampaignZoneSession::assign_header_text(
            assign, kCampaignOathCellChars);
    }
    lines.push_back(std::move(header));
    if (slots.empty())
    {
        lines.emplace_back("      (empty)");
        return lines;
    }
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        const guy& member =
            *save.team_list[static_cast<std::size_t>(slots[i])];
        // Deploy locks refuse the toggle ON; benching a deployed hero stays
        // allowed. A locked hero is never deployed here anyway (adopt stood
        // him down), so the padlock and the sortie always agree.
        const hooks::CampaignRosterLock* lock =
            member.deployed ? nullptr
                            : zone.deploy_lock_for_tag(member.campaign_tag);
        const std::string row = format_campaign_camp_roster_row(
            member, assign, lock, kCampaignPickerTerminalRowBudget);
        lines.push_back(numbered ? std::format("  {:2}. {}", i + 1, row)
                                 : "      " + row);
    }
    return lines;
}

// The whole camp screen: every widget in the zone's own layout order (row
// units), so the terminal reads top-to-bottom exactly like the panel — a
// hoisted readout leads as the heading. Docket ordinals run down the screen
// in that same order, and the oath door takes the next one after the roster.
std::vector<std::string> terminal_camp_lines(const SaveData& save,
                                             const CampaignZoneSession& zone,
                                             const std::vector<int>& slots,
                                             std::vector<CampChoice>& choices)
{
    enum class Block : std::uint8_t { Readout, Text, Actions, Roster };
    struct Placed {
        int unit = 0;
        Block block = Block::Roster;
        int index = 0;
    };
    std::vector<Placed> placed;
    if (const CampaignZoneSession::ReadoutLayout* readout = zone.readout();
        readout != nullptr)
    {
        // A hoisted readout left the row grid for the panel's header band:
        // it leads whatever sits in row unit 0.
        placed.push_back({readout->in_header_band ? -1 : readout->start_unit,
                          Block::Readout, 0});
    }
    for (std::size_t i = 0; i < zone.texts().size(); ++i)
        placed.push_back({zone.texts()[i].start_unit, Block::Text,
                          static_cast<int>(i)});
    for (std::size_t i = 0; i < zone.actions().size(); ++i)
        placed.push_back({zone.actions()[i].start_unit, Block::Actions,
                          static_cast<int>(i)});
    placed.push_back({zone.roster().start_unit, Block::Roster, 0});
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Placed& a, const Placed& b) {
                         return a.unit < b.unit;
                     });

    choices.clear();
    std::vector<std::string> lines;
    const auto separate = [&lines] {
        if (!lines.empty())
            lines.emplace_back();
    };
    for (const Placed& entry : placed)
    {
        switch (entry.block)
        {
            case Block::Readout:
                separate();
                lines.push_back(terminal_camp_readout_line(*zone.readout()));
                break;
            case Block::Text:
            {
                const CampaignZoneSession::TextLayout& text =
                    zone.texts()[static_cast<std::size_t>(entry.index)];
                // The band clips exactly like the SDL draw: an under-weight
                // widget is a legal composition, and the rows below it
                // belong to the next widget on both surfaces.
                const std::size_t shown = std::min<std::size_t>(
                    text.lines.size(),
                    static_cast<std::size_t>(std::max(
                        0, CampaignZoneSession::text_lines_in_band(
                               text.units))));
                if (shown == 0)
                    break;
                separate();
                for (std::size_t i = 0; i < shown; ++i)
                {
                    lines.push_back(clip_with_ellipsis(
                        text.lines[i], kCampaignPickerTerminalRowBudget));
                }
                break;
            }
            case Block::Actions:
            {
                const CampaignZoneSession::ActionsLayout& actions =
                    zone.actions()[static_cast<std::size_t>(entry.index)];
                if (actions.rows.empty())
                    break;
                separate();
                // The docket lists the WHOLE widget: the PageModel window is
                // a pixel-band constraint of the panel, and a terminal that
                // hid rows behind a pager would be inventing a limit its
                // surface does not have.
                for (std::size_t i = 0; i < actions.rows.size(); ++i)
                {
                    choices.push_back({CampChoice::Kind::Row, entry.index,
                                       static_cast<int>(i)});
                    lines.push_back(std::format(
                        "  {:2}. {}", choices.size(),
                        campaign_picker_row_text(
                            actions.rows[i],
                            kCampaignPickerTerminalRowBudget, true)));
                }
                break;
            }
            case Block::Roster:
            {
                separate();
                const std::vector<std::string> roster =
                    terminal_camp_roster_lines(save, zone, slots, false);
                lines.insert(lines.end(), roster.begin(), roster.end());
                if (zone.roster().assign.active)
                {
                    choices.push_back({CampChoice::Kind::Oath, 0, 0});
                    lines.push_back(std::format(
                        "  {:2}. {}", choices.size(),
                        terminal_camp_oath_row_text(zone.roster().assign)));
                }
                break;
            }
        }
    }
    return lines;
}

// What the swear prompt teaches before the player commits. The oath is a
// cycle, not a menu of choices, so the words on offer appear NOWHERE on the
// screen unless the prompt says them — learning that the second option is
// "BURDEN" by swearing "WAR" first is not a choice. The glyph line names the
// deploy cell's letters for the same reason.
std::vector<std::string> terminal_camp_oath_legend(
    const hooks::CampaignAssignSpec& assign)
{
    std::vector<std::string> lines;
    std::string cycle = "A row number swears that hero: ";
    if (assign.labels.size() >= 2)
    {
        cycle += std::format("- -> {} -> {} -> {}", assign.labels[0],
                             assign.labels[1], assign.labels[0]);
    }
    else if (assign.labels.size() == 1)
    {
        cycle += std::format("- -> {}", assign.labels[0]);
    }
    else
    {
        return lines;
    }
    // The cycle string carries the "never back to unset" rule on its own:
    // the "-" appears only at the head, never again.
    lines.push_back(
        clip_with_ellipsis(std::move(cycle), kCampaignPickerTerminalRowBudget));
    lines.emplace_back("[X] deployed   [ ] benched   [L] locked   - unsworn");
    lines.push_back(std::string("Swearing a deployed hero stands them down: ") +
                    "the muster shrinks.");
    return lines;
}

// The swear prompt: the numbered own roster under the oath column, one
// cycle per answer. Cycling a DEPLOYED hero un-deploys first through the
// full roster tail (the SDL rule), then the tag lands through the assign
// provider and both writes ride the autosave tail.
void run_terminal_camp_oath(SaveData& save, CampaignZoneSession& zone,
                            const TerminalCampaignPickerIo& io)
{
    for (;;)
    {
        const hooks::CampaignAssignSpec& assign = zone.roster().assign;
        if (!assign.active)
            return;  // a refetch dropped the oath column
        if (!assign.frozen.empty())
        {
            // The freeze can land under an open prompt (the book decides it,
            // and the book refetches): say so and close rather than offering
            // a cycle that will refuse.
            io.notice(assign.frozen);
            return;
        }
        const std::vector<int> slots = collect_base_camp_slots(save);
        std::vector<std::string> lines =
            terminal_camp_roster_lines(save, zone, slots, true);
        const std::vector<std::string> legend =
            terminal_camp_oath_legend(assign);
        if (!legend.empty())
        {
            lines.emplace_back();
            lines.insert(lines.end(), legend.begin(), legend.end());
        }
        const std::optional<std::string> answer =
            io.prompt(std::string(kTerminalCampOathTitle), lines,
                      terminal_camp_oath_prompt_label(slots.size()));
        if (!answer || answer->empty() || *answer == "0")
            return;
        const std::optional<int> choice = parse_int_strict(*answer);
        if (!choice || *choice < 1 ||
            static_cast<std::size_t>(*choice) > slots.size())
        {
            io.notice("Invalid roster row.");
            continue;
        }
        const int slot = slots[static_cast<std::size_t>(*choice - 1)];
        guy& member = *save.team_list[static_cast<std::size_t>(slot)];
        const int next_tag =
            CampaignZoneSession::next_assign_tag(member.campaign_tag);
        bool stood_down = false;
        if (member.deployed)
        {
            (void)toggle_deploy_slot(save, slot);
            (void)company_autosave_after_mutation(
                save, kTerminalNetworkedLobbyActive);
            stood_down = true;
        }
        if (!hooks::campaign_assign_set(slot, next_tag))
        {
            // The provider refused the tag: no oath, no toast about one. The
            // un-deploy already landed, though, and a roster that quietly
            // shed a hero is exactly what the toast exists to prevent — and
            // an un-deploy is an own mutation, so the composition refetches
            // on it like any other. (`assign` is rebound at the loop top;
            // nothing below this point reads the stale reference.)
            if (stood_down)
            {
                io.notice(std::string(kCampaignOathStoodDownMessage));
                zone.refetch();
            }
            continue;
        }
        // The full-word toast: the cycle must never be a silent glyph flip —
        // and neither may the un-deploy it rides on.
        const std::size_t label_index = static_cast<std::size_t>(next_tag - 1);
        if (label_index < assign.labels.size())
        {
            io.notice(campaign_oath_toast(assign.labels[label_index],
                                          stood_down));
        }
        else if (stood_down)
        {
            io.notice(std::string(kCampaignOathStoodDownMessage));
        }
        (void)company_autosave_after_mutation(save,
                                              kTerminalNetworkedLobbyActive);
        zone.refetch();  // own mutation: the locks and the column re-derive
    }
}

} // namespace

void run_terminal_campaign_camp(SaveData& save,
                                const TerminalCampaignPickerIo& io)
{
    CampaignZoneSession zone(save);
    zone.fetch();
    if (!zone.composed())
    {
        // Nothing but the default roster behind the door — and both
        // terminals already carry that on their own Team Build rows.
        io.notice(std::string(kCampaignCampNoZoneMessage));
        return;
    }

    for (;;)
    {
        const std::vector<int> slots = collect_base_camp_slots(save);
        std::vector<CampChoice> choices;
        const std::vector<std::string> lines =
            terminal_camp_lines(save, zone, slots, choices);
        const std::optional<std::string> answer =
            io.prompt(std::string(kTerminalCampTitle), lines,
                      terminal_camp_prompt_label(choices.size()));
        if (!answer || answer->empty() || *answer == "0")
            return;  // blank/0/EOF closes the camp

        const std::optional<int> choice = parse_int_strict(*answer);
        if (!choice || *choice < 1 ||
            static_cast<std::size_t>(*choice) > choices.size())
        {
            io.notice("Invalid camp row.");
            continue;
        }
        const CampChoice picked =
            choices[static_cast<std::size_t>(*choice - 1)];
        if (picked.kind == CampChoice::Kind::Oath)
        {
            const hooks::CampaignAssignSpec& assign = zone.roster().assign;
            if (!assign.frozen.empty())
                io.notice(assign.frozen);
            else
                run_terminal_camp_oath(save, zone, io);
            continue;
        }

        const CampaignZoneSession::ActionsLayout* actions =
            zone.actions_widget(picked.widget);
        if (actions == nullptr ||
            picked.row >= static_cast<int>(actions->rows.size()))
        {
            continue;  // defensive: the composition moved under the answer
        }
        // Copy: an Acted refetch replaces the rows under the one we read.
        const CampaignZoneSession::Row row =
            actions->rows[static_cast<std::size_t>(picked.row)];

        switch (row.kind)
        {
            case CampaignPickerSession::Kind::Page:
                run_terminal_campaign_page(save, io, row.id);
                // Own navigation is a fetch trigger: the book may have acted
                // while the door was open.
                zone.refetch();
                break;
            case CampaignPickerSession::Kind::Level:
                // Host gate first (the SET LEVEL predicate) — the session
                // stays policy-free.
                if (!io.is_host())
                {
                    io.notice(std::string(kCampaignPickerHostGuardMessage));
                    break;
                }
                if (!row.available)
                {
                    // The campaign's own voice, never the loader's. The
                    // terminal tail only moves the cursor, so a road that is
                    // not in the campaign has to be refused HERE — the SDL
                    // load-with-rollback would have caught it at the click.
                    io.notice(std::string(kCampaignLevelClosedMessage));
                    break;
                }
                if (row.current)
                {
                    // Same click, same answer as the SDL camp: the cursor
                    // is already here, so nothing is set and the row says
                    // so instead of confirming a move that never happened.
                    io.notice(std::string(kCampaignLevelUnchangedMessage));
                    break;
                }
                io.apply_level(row.level);
                zone.refetch();  // CURRENT markers re-derive
                io.notice(campaign_level_set_message(row.label));
                break;
            case CampaignPickerSession::Kind::Action:
            {
                using Outcome = CampaignPickerSession::OutcomeKind;
                const CampaignZoneSession::Outcome outcome =
                    zone.act(picked.widget, picked.row);
                if (outcome.kind == Outcome::Refused)
                {
                    io.notice(outcome.reason);
                    break;
                }
                if (outcome.kind != Outcome::Acted)
                    break;
                // The session already debited and refetched; persist it.
                (void)company_autosave_after_mutation(
                    save, kTerminalNetworkedLobbyActive);
                const std::string toast = zone.take_message();
                if (!toast.empty())
                    io.notice(toast);
                break;
            }
        }
    }
}

} // namespace og::ui
