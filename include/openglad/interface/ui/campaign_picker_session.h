/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The scripted-campaign picker session (issue #206,
// docs/campaign-scripting-design.md "The picker contract"). SDL-free: the
// page-fetch/select/act state machine every surface (SDL, text, curses)
// drives. All navigation state lives here in C++ — the Lua hook is pure
// (page id in, page description out) and a page is fetched ONCE per
// navigation or action, never per frame.
//
// The session decorates fetched pages against the SaveData it borrows
// (CLEARED/CURRENT level markers, scenario-title label fill, action
// affordability) and owns the cost debit for priced actions (debit first,
// then dispatch — the C++-owns-the-cost rule). It never writes
// save.scen_num: a Level choice surfaces as Outcome::SetLevel and each
// client applies its own level-set tail (host gating included — the
// session stays policy-free).

#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/interface/ui/menu_binding.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SaveData;

namespace og::ui {

// Player-facing refusals for the two inert row states. Engine diagnostics
// ("Invalid level file.") never reach the campaign's own voice slot: a road
// that does not load is a road the campaign has not opened, and that is what
// the player is told — on every surface, from one place.
inline constexpr std::string_view kCampaignLevelClosedMessage =
    "That road is not open yet.";
inline constexpr std::string_view kCampaignActionDoneMessage =
    "You have that already.";

class CampaignPickerSession {
public:
    using Kind = og::script::hooks::CampaignPageEntry::Kind;

    // Page depth bound from the design doc ("page depth <= 4").
    static constexpr int kMaxPageDepth = 4;

    // One selectable row, decorated for display. `label` is the script's
    // label, or the scenario title (save-side fill) for level rows that
    // ship without one.
    struct Row {
        std::string id;
        std::string label;
        std::string note;
        Kind kind = Kind::Level;
        int level = 0;
        int cost = 0;
        // Level-row decoration from the save (false for other kinds).
        bool cleared = false;
        bool current = false;
        // Action-row affordability (cost == 0 or the acting team's wallet
        // covers it; always true for other kinds). Surfaces show refused
        // rows rather than hiding them — the terminal listing contract.
        bool affordable = true;
        // The book retired this costed action (`done` on the entry): the row
        // stops quoting a price and stops dispatching.
        bool done = false;
        // Level rows only: the scenario file behind `level` actually loaded
        // its header. A row pointing at a level that is not in the campaign
        // renders CLOSED instead of looking like every other road and
        // failing at the click.
        bool available = true;
        [[nodiscard]] bool is_level() const { return kind == Kind::Level; }
        // Rows a click cannot move: spent purchases and closed roads. They
        // stay visible and focusable (the listing contract) but draw on the
        // spent face and refuse with a reason instead of acting.
        [[nodiscard]] bool is_inert() const
        {
            return done || (is_level() && !available);
        }
    };

    struct DecoratedPage {
        std::string title;
        std::vector<std::string> lines;
        std::vector<Row> rows;
    };

    enum class OutcomeKind : std::uint8_t {
        None,        // no-op (bad index)
        OpenedPage,  // page() now shows the pushed page
        SetLevel,    // caller applies its client-specific level-set tail
        Acted,       // action ran; page() already refetched; toast may wait
        Refused,     // nothing happened; `reason` names why
    };

    struct Outcome {
        OutcomeKind kind = OutcomeKind::None;
        int level = 0;       // SetLevel: the chosen scenario id
        std::string reason;  // Refused
    };

    // Borrows the save for decoration and the wallet; keep the session
    // shorter-lived than the SaveData (the provider-install discipline).
    explicit CampaignPickerSession(SaveData& save);

    // Fetch the root page. False = no scripted picker (not registered,
    // hook errored, malformed page) — callers fall back to the stock UI.
    bool open();

    // Open directly on `page_id` (the zone submenu's entry: a Base Camp
    // page-kind action row names its page, so the submenu's ROOT is that
    // page — back() at it closes the submenu). open() == open_at("").
    bool open_at(const std::string& page_id);

    [[nodiscard]] bool is_open() const { return !stack_.empty(); }
    [[nodiscard]] const DecoratedPage& page() const { return page_; }
    // Page-stack depth; the root page is depth 1.
    [[nodiscard]] int depth() const { return static_cast<int>(stack_.size()); }

    // Activate row `entry_index` of the current page. Kind::Page pushes and
    // fetches (depth-capped); Kind::Level answers SetLevel WITHOUT touching
    // the save; Kind::Action debits first (skipped under infinite gold),
    // dispatches picker_action, stores the toast, and refetches the current
    // page so labels/state re-derive.
    Outcome choose(std::size_t entry_index);

    // Pop one page and refetch it. False when already at the root (the
    // caller closes the picker) — or when the refetch fails, which closes
    // the session (a malformed page answers "no scripted picker").
    bool back();

    // Refetch the current page in place (after a caller-applied level set,
    // so CURRENT markers re-derive). Fetch-per-action, never per frame.
    void refresh();

    // The pending action toast, cleared by the read ("" = none).
    std::string take_message();

private:
    bool fetch(const std::string& page_id, DecoratedPage& out) const;

    SaveData& save_;
    std::vector<std::string> stack_;  // page ids; front() is the "" root
    DecoratedPage page_;
    std::string message_;
};

// One row as a text line: label, note, cost ("60g") and the CLEARED/CURRENT
// level markers, clipped to `budget` chars. Shared by the surfaces so the
// SDL faces and the terminal listings agree on decoration.
std::string campaign_picker_row_text(const CampaignPickerSession::Row& row,
                                     std::size_t budget);

// --- Base Camp gameplay-zone session (docs/basecamp-zones-design.md) -------
//
// The SDL-free sibling of CampaignPickerSession behind the Base Camp's
// Lua-composable gameplay zone: fetch the base_camp hook's composition (or
// the built-in DEFAULT composition — a full-capability roster — when no hook
// is registered or the composition is malformed/over-budget, so there is ONE
// renderer), decorate the action rows against the borrowed SaveData with the
// same helpers as the picker session, and lay the widgets out as integer ROW
// UNITS on the zone's fixed 14px grid (kZoneRowUnits rows anchored at y=45
// inside the y=28..160 band). Never pixels, never per frame: the surface
// refetches on the four cadence triggers (screen entry, own
// mutation/action, the level-reload guard firing, an applied lobby-settings
// change — settings_fingerprint_changed()).
class CampaignZoneSession {
public:
    using Row = CampaignPickerSession::Row;
    using Outcome = CampaignPickerSession::Outcome;
    using OutcomeKind = CampaignPickerSession::OutcomeKind;

    // The zone band's whole-row budget: 8 units of 14px from y=45 (the
    // classic roster's exact grid — a default composition is byte-identical
    // to the pre-zone screen).
    static constexpr int kZoneRowUnits = 8;
    // Roster minimum (red-team rule): a composition that leaves the roster
    // fewer than 2 rows is over-budget and falls to the default zone.
    static constexpr int kRosterMinRows = 2;

    // The roster widget's capabilities + band, derived at fetch. When the
    // roster is not the FIRST widget its band spends one unit on the column
    // header (header_at_top false); first-widget rosters keep the classic
    // header outside the grid at y=33.
    struct RosterLayout {
        bool can_deploy = true;
        bool can_train = true;
        bool can_reorder = true;
        bool can_team = true;
        bool can_hire = true;
        std::vector<og::script::hooks::CampaignRosterLock> locks;
        og::script::hooks::CampaignAssignSpec assign;
        int start_unit = 0;      // band start (header unit included)
        int row_start_unit = 0;  // first roster ROW unit
        int rows_per_page = kZoneRowUnits;
        bool header_at_top = true;
    };
    // Readability-strip text lines (8px pitch inside a ceil(lines*8/14)-unit
    // band; strips paint in draw_background — the draw-order rule).
    struct TextLayout {
        std::vector<std::string> lines;
        int start_unit = 0;
        int units = 0;
    };
    // One actions widget: decorated rows windowed by its own PageModel
    // (rows_per_page == its band units; overflow pages in place through the
    // widget's two pager ordinals).
    struct ActionsLayout {
        std::vector<Row> rows;
        PageModel page{};
        int start_unit = 0;
        int units = 0;
    };
    // One zone row of up to 3 label/value cells (fetch-composed strings;
    // staleness bound = the fetch cadence). `in_header_band` hoists it out
    // of the row grid into the panel's own header band (the classic column-
    // header slot at y=33, which a non-leading roster leaves empty): the
    // summary reads as the panel's heading, the blank band at the top of the
    // panel disappears, and the freed row unit goes back to the roster.
    struct ReadoutLayout {
        std::vector<og::script::hooks::CampaignZoneWidget::ReadoutItem> items;
        int start_unit = 0;
        bool in_header_band = false;
    };

    explicit CampaignZoneSession(SaveData& save);

    // Fetch the composition: the registered base_camp hook when it parses
    // and lays out, the built-in default otherwise. Never fails.
    void fetch();
    // Fetch preserving each actions widget's page window (the
    // after-own-mutation trigger; windows clamp like every PageModel).
    void refetch();
    // Adopt one raw composition (decorate + lay out). False when the layout
    // is over-budget (roster under kRosterMinRows, weights past the band) —
    // fetch()'s fallback-to-default signal, public as the synthetic-zone
    // test seam.
    bool adopt(const og::script::hooks::CampaignZone& zone);

    // False = the built-in default composition is showing.
    [[nodiscard]] bool scripted() const { return scripted_; }
    [[nodiscard]] const RosterLayout& roster() const { return roster_; }
    [[nodiscard]] const std::vector<TextLayout>& texts() const
    {
        return texts_;
    }
    [[nodiscard]] const std::vector<ActionsLayout>& actions() const
    {
        return actions_;
    }
    // Mutable window access for the widget's pager dispatch (null when out
    // of range — stale clicks stay inert).
    ActionsLayout* actions_widget(int index);
    // Null when the composition carries no readout.
    [[nodiscard]] const ReadoutLayout* readout() const
    {
        return readout_.has_value() ? &*readout_ : nullptr;
    }
    [[nodiscard]] int roster_rows_per_page() const
    {
        return roster_.rows_per_page;
    }

    // Execute an ACTION row (widget-relative absolute row index): the
    // debit-then-dispatch order, refund on a book with no picker_action,
    // toast capture, and the refetch — the same machinery as
    // CampaignPickerSession::choose. Level/Page rows answer None: the
    // surface owns the level-set tail and the zone submenu.
    Outcome act(int widget_index, int row_index);

    // The pending action toast, cleared by the read ("" = none).
    std::string take_message();

    // The lock refusing deploy for a hero carrying `tag` (0 = unassigned),
    // or nullptr when deploy is allowed.
    [[nodiscard]] const og::script::hooks::CampaignRosterLock*
    deploy_lock_for_tag(int tag) const;

    // The assign cycle: unset(0) -> 1 -> 2 -> 1, never back to unset.
    static int next_assign_tag(int tag);
    // Chip glyph: '-' unassigned, else the label's first letter (upper).
    // Kept for the one-cell terminal listings; the SDL roster spells the
    // oath out (assign_cell_text) — a lone letter on a team-coloured chip
    // reads as a team number, and the word has to outlive the toast.
    static char assign_glyph(const og::script::hooks::CampaignAssignSpec& spec,
                             int tag);
    // The roster's oath CELL: "-" while unsworn, else the label in words,
    // uppercased and ellipsis-cut to `budget` characters.
    static std::string assign_cell_text(
        const og::script::hooks::CampaignAssignSpec& spec, int tag,
        std::size_t budget);
    // The oath COLUMN heading: the spec's channel key in words (uppercased,
    // cut to `budget`), so the column never reads "TEAM" while it holds
    // something that is not a team.
    static std::string assign_header_text(
        const og::script::hooks::CampaignAssignSpec& spec,
        std::size_t budget);

    // True when the save's lobby-synced settings changed since the last
    // call (the applied-lobby-settings fetch trigger; the first call seeds
    // and answers false).
    bool settings_fingerprint_changed();

private:
    SaveData& save_;
    bool scripted_ = false;
    RosterLayout roster_;
    std::vector<TextLayout> texts_;
    std::vector<ActionsLayout> actions_;
    std::optional<ReadoutLayout> readout_;
    std::string message_;
    std::uint64_t settings_fingerprint_ = 0;
    bool fingerprint_seeded_ = false;
};

// --- Terminal MISSIONS flow (openglad_text + openglad_curses) --------------
//
// The scripted pages render through the Company List "dynamic rows + prompt"
// precedent: the client prints numbered rows and prompts for a 1-based row
// number — never Menu::choose, whose digit jump stops at row 9 while a page
// may carry 24 rows. Everything except the two I/O primitives lives in
// run_terminal_campaign_picker, so the listings stay byte-identical across
// the text and curses clients.

// The terminal guard lines. The no-book line matches the SDL popup body;
// the host line is the terminal projection of the SET LEVEL host gate
// ("Only the host may\nset the level" on the SDL popup).
inline constexpr std::string_view kCampaignPickerNoBookMessage =
    "This campaign keeps no mission book.";
inline constexpr std::string_view kCampaignPickerHostGuardMessage =
    "Only the host may set the level.";

// Row-text budget for the terminal listings: 80 columns minus the
// "  NN. " prefix, with slack for the curses list gutter.
inline constexpr std::size_t kCampaignPickerTerminalRowBudget = 72;

// The per-client I/O primitives of the terminal MISSIONS loop.
struct TerminalCampaignPickerIo {
    // Render the current page (title banner + the composed lines) and
    // prompt with `label` for one input line; nullopt = cancel/EOF
    // (treated as back — at the root that closes the book).
    std::function<std::optional<std::string>(
        const std::string& title, const std::vector<std::string>& lines,
        const std::string& label)>
        prompt;
    // Print one notice line (guards, confirmations, refusals, toasts).
    std::function<void(const std::string&)> notice;
    // The SET LEVEL host gate: level rows publish scenario_id and are
    // host-only; pages and actions are open to every machine. Terminal
    // pickers are local-only today, so both clients answer their
    // MenuLabelContext::is_host (always true).
    std::function<bool()> is_host;
    // The client's own set-level tail — the same code its "Set level"
    // prompt runs (session-config level + save.scen_num write).
    std::function<void(int)> apply_level;
};

// Drive the whole MISSIONS flow over `save`: the no-book guard, page
// render/prompt loop, paging, the host gate, the client level-set tail
// (+ CURRENT re-derive), the Acted autosave tail (§3.8 — the decision book
// and the wallet ride the company file in the terminal clients), refusals
// and toasts. Returns when the book closes (back at the root, or EOF).
void run_terminal_campaign_picker(SaveData& save,
                                  const TerminalCampaignPickerIo& io);

} // namespace og::ui
