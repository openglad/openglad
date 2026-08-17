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
class guy;

namespace og::ui {

// Player-facing refusals for the two inert row states. Engine diagnostics
// ("Invalid level file.") never reach the campaign's own voice slot: a road
// that does not load is a road the campaign has not opened, and that is what
// the player is told — on every surface, from one place. The earned-roads
// gate (og::data::level_selection_allowed) speaks the same line: a shipped
// road the company has not earned is exactly as closed as a missing one.
inline constexpr std::string_view kCampaignLevelClosedMessage =
    "That road is not open yet.";
inline constexpr std::string_view kCampaignActionDoneMessage =
    "You have that already.";
// A page-kind row whose page the book will not hand back. Every surface says
// it in the same words: the SDL zone submenu door, the terminal camp's page
// rows, and CampaignPickerSession::choose one level down.
inline constexpr std::string_view kCampaignPageUnreadableMessage =
    "That page cannot be read.";
// The two answers a level row that CAN be clicked gives back, from one
// place for the same reason as the refusals above: a level row on the Base
// Camp is the same click on the SDL panel and at a terminal prompt, and one
// click may not have two answers. They also stay out of any one campaign's
// vocabulary — the first camp to put level rows on the Base Camp is a
// dream log, and a dream is not a "road". The unchanged answer is a
// signpost, not a dead end: on a one-level campaign the only replay row IS
// the current row, and GO has always replayed the current level — the
// toast says so instead of leaving the player hunting for a selector.
inline constexpr std::string_view kCampaignLevelUnchangedMessage =
    "Already on that level. GO when ready.";
[[nodiscard]] inline std::string campaign_level_set_message(
    std::string_view title)
{
    return std::string("Level set to ") + std::string(title) + ".";
}
// The oath cell's width in characters, shared by the SDL roster column and
// the terminal camp roster so one hero's oath reads the same on every
// surface (a word cut at six characters on one screen and at nine on
// another is two different words).
inline constexpr std::size_t kCampaignOathCellChars = 6;
// The terminal spelling of the dimmed face an unaffordable action row wears
// on the SDL panel. A prompt cannot dim a row, so it says the word: "Not
// enough gold." arriving only AFTER the click is a price tag the player was
// never shown.
inline constexpr std::string_view kCampaignNeedGoldMark = "[NEED GOLD]";
// The destructive side effect of swearing a DEPLOYED hero. The cycle un-
// deploys through the full roster tail by design, and the toast is the only
// thing that speaks on either surface — a player who swears an oath and then
// presses GO must never discover an empty muster in the level.
inline constexpr std::string_view kCampaignOathStoodDownMessage =
    "Stood down from the muster.";

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
// `spell_unaffordable` adds the [NEED GOLD] mark: the terminals pass true
// (a prompt has no dimmed face to draw), the SDL faces pass false and dim
// the row instead — the same fact, in each surface's own material.
std::string campaign_picker_row_text(const CampaignPickerSession::Row& row,
                                     std::size_t budget,
                                     bool spell_unaffordable = false);

// The oath toast both surfaces speak: the sworn word, plus the un-deploy
// when the cycle stood a deployed hero down. Composed once so the SDL panel
// and the terminals can never tell the player different stories about the
// same click.
std::string campaign_oath_toast(const std::string& label, bool stood_down);

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
    // The parser's weight ceiling IS this band: a widget asking for more
    // units than the whole zone holds is refused at parse time with a named
    // error, so the two numbers must never drift apart.
    static_assert(kZoneRowUnits == og::script::hooks::kCampaignZoneMaxWeight,
                  "the zone band and the parser's weight bound must agree");
    // One row unit in pixels, and the 8px pitch text lines ink on inside a
    // unit (the SDL draw static_asserts its own grid against these).
    static constexpr int kZoneRowPitch = 14;
    static constexpr int kTextLinePitch = 8;
    // Roster minimum (red-team rule): a composition that leaves the roster
    // fewer than 2 rows is over-budget and falls to the default zone.
    static constexpr int kRosterMinRows = 2;

    // The most text lines that ink inside `units` row units without crossing
    // the band's bottom edge. A widget's explicit weight may be SMALLER than
    // its line count — that is a legal composition the parser cannot refuse
    // (unlike an over-weight, which it does) — so the draw clips to its own
    // band rather than painting into the next widget's rows.
    [[nodiscard]] static constexpr int text_lines_in_band(int units)
    {
        return units <= 0 ? 0 : (units * kZoneRowPitch) / kTextLinePitch;
    }

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

    // What a fetch does about the composition's deploy locks. `Locks` (the
    // default) stands own heroes the composition refuses DOWN — that is
    // what makes a lock mechanical instead of a suggestion, and every
    // surface that shows the camp wants it. `None` composes without
    // touching the sortie: the roster-refusal composer is a question about
    // a command the player has already typed, and a question must not move
    // the state that command is about to toggle.
    enum class Enforce { Locks, None };

    // Fetch the composition: the registered base_camp hook when it parses
    // and lays out, the built-in default otherwise. Never fails.
    void fetch(Enforce enforce = Enforce::Locks);
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
    // True when this composition shows something beyond the bare default
    // roster: a scripted camp, or the transitional BOOK DOOR a campaign
    // that registered a picker book but no base_camp hook gets (one page
    // row onto the book's root, named by the book's own title). The
    // terminals' camp door opens on this, not on scripted() — a camp that
    // is only a second copy of the roster says nothing, but a campaign's
    // book has to stay reachable.
    [[nodiscard]] bool composed() const { return composed_; }
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
    // Stand down every own hero the freshly-fetched composition refuses.
    // Locks are declared per composition, but a company is deployed BEFORE
    // the composition that refuses it exists (the natural order: deploy,
    // fight, and only then does the camp learn which column is away), so a
    // lock consulted only on the deploy toggle would let the wrong hero
    // march while the camp narrated the opposite.
    void enforce_deploy_locks();
    // The composition half of a fetch (hook, book door, or default), with
    // no side effect on the roster.
    void fetch_composition();

    SaveData& save_;
    bool scripted_ = false;
    bool composed_ = false;
    RosterLayout roster_;
    std::vector<TextLayout> texts_;
    std::vector<ActionsLayout> actions_;
    std::optional<ReadoutLayout> readout_;
    std::string message_;
    std::uint64_t settings_fingerprint_ = 0;
    bool fingerprint_seeded_ = false;
};

// --- Terminal campaign surfaces (openglad_text + openglad_curses) ----------
//
// Two flows share one contract: the CAMP (the Base Camp gameplay zone's
// terminal face, opened by the TeamBuild "Camp" row) and the BOOK (a
// CampaignPage tree, reached from a camp page row). Both render through the
// Company List "dynamic rows + prompt" precedent: the client prints numbered
// rows and prompts for a 1-based row number — never Menu::choose, whose digit
// jump stops at row 9 while a camp may carry 16 action rows. Everything
// except the two I/O primitives lives in the drivers below, so the listings
// stay byte-identical across the text and curses clients.

// The terminal guard lines. The no-camp line is the camp door's own refusal
// (the DEFAULT composition is a full-capability roster, and both terminals
// already carry that on their own TeamBuild rows — a camp door onto a second
// copy of the roster would say nothing, so the door speaks only for a
// campaign that composed a camp or keeps a book). The host line is the
// terminal projection of the SET LEVEL host gate ("Only the host may\nset the
// level" on the SDL popup).
inline constexpr std::string_view kCampaignCampNoZoneMessage =
    "This campaign keeps no camp.";
inline constexpr std::string_view kCampaignPickerHostGuardMessage =
    "Only the host may set the level.";

// The camp's roster refusals for the terminals' own Team Build commands. The
// SDL panel hides a control its composition retired (an absent button refuses
// nothing); a prompt cannot hide anything, so the terminals refuse in words.
// A lock that carries no reason still has to say something — silence on a
// prompt reads as a broken command.
inline constexpr std::string_view kCampaignRosterDeployClosedMessage =
    "This camp does not muster.";
inline constexpr std::string_view kCampaignRosterDeployLockedMessage =
    "That hero is not free to deploy.";
inline constexpr std::string_view kCampaignRosterTrainClosedMessage =
    "This camp does not train.";
inline constexpr std::string_view kCampaignRosterHireClosedMessage =
    "This camp does not hire.";

// Row-text budget for the terminal listings: 80 columns minus the
// "  NN. " prefix, with slack for the curses list gutter.
inline constexpr std::size_t kCampaignPickerTerminalRowBudget = 72;

// One camp roster row, composed once for BOTH terminals (the recon's
// text-vs-curses roster divergence is a legacy of two hand-written screens;
// the camp has exactly one spelling). The stat columns come from the SDL row
// composer, so the camp reads the same on every surface:
//
//   [L] BRAM         SOLDIER   L= 3 XP=  1200  WAR  Waits at the Falls.
//
// The lock reason is a trailing COLUMN, separated by space like every other
// cell: a dash separator collided with the unsworn oath cell's own "-" and
// produced a "- - " stutter mid-row.
//
// `lock` (null = deploy allowed) puts the SDL padlock's meaning into the
// deploy cell as 'L' and spells its reason inline: a terminal cannot draw a
// glyph in a cell nobody clicks, and a lock the player can only learn from a
// refusal is a lock they never see. The oath cell renders only for an active
// assign spec.
std::string format_campaign_camp_roster_row(
    const guy& member, const og::script::hooks::CampaignAssignSpec& assign,
    const og::script::hooks::CampaignRosterLock* lock, std::size_t budget);

// The per-client I/O primitives of the terminal camp and book loops.
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

// Drive the whole BOOK flow over `save` rooted at `page_id` — a camp page
// row's door, and the only way into a book on a terminal ("" is the book's
// root page, the door the transitional book-door composition opens). Back at
// that page closes the door and returns to the camp (the zone submenu is a
// room inside the camp, not a screen of its own). Carries the page
// render/prompt loop, the host gate, the CLOSED-road refusal, the client
// level-set tail (+ CURRENT re-derive), the Acted autosave tail (§3.8 — the
// decision book and the wallet ride the company file in the terminal
// clients), refusals and toasts. A page the book will not hand back answers
// with kCampaignPageUnreadableMessage.
void run_terminal_campaign_page(SaveData& save,
                                const TerminalCampaignPickerIo& io,
                                const std::string& page_id);

// The camp's answer to one of the terminals' own roster commands, or nullopt
// when the campaign allows it. Both terminal clients ask before every deploy
// toggle, train and hire: the SDL panel enforces the roster widget's
// capability flags and deploy locks at its dispatch, and a camp that
// advertises a padlock on one client while the other happily deploys the
// same hero is two different campaigns. `slot` is the SaveData::team_list
// slot for Deploy (ignored by Train/Hire). Benching a deployed hero is
// always allowed — the lock is a deploy courtesy, not a cage.
enum class TerminalRosterCommand : std::uint8_t { Deploy, Train, Hire };
std::optional<std::string> terminal_roster_refusal(
    SaveData& save, TerminalRosterCommand command, int slot);

// Drive the CAMP over `save`: fetch the base_camp composition, render it as
// the readout line, the text blocks, the numbered docket and the roster
// block in the zone's own layout order, then dispatch one row per prompt.
// Level rows ride the host gate + the client's set-level tail; page rows open
// the book at that page; action rows debit-then-dispatch through the zone
// session and autosave; the oath row opens the swear prompt (cycle, full-word
// toast, un-deploy first). Returns when the camp closes (0/blank/EOF), or
// immediately with kCampaignCampNoZoneMessage when the campaign composed
// neither a camp nor a book (CampaignZoneSession::composed()).
void run_terminal_campaign_camp(SaveData& save,
                                const TerminalCampaignPickerIo& io);

} // namespace og::ui
