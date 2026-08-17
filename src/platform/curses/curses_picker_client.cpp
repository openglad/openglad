/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Terminal (TUI) picker front-end for the ncurses client.
 *
 * Implements og::ui::IPickerClient by rendering the shared, data-driven menu
 * model (menu_model.h) to an ITerminal and reading key presses from it, while
 * reusing every piece of business logic the SDL and text pickers use
 * (picker_common: HireSession / TrainSession / team ops). It is the curses
 * analogue of TextPickerClient (src/platform/text/text_picker.cpp): the menu
 * *flows* are identical, only the rendering/input backend differs.
 *
 * The header (curses_picker_client.h) is a fixed contract — only the
 * IPickerClient overrides and the data members it declares exist. All the
 * interaction helpers therefore live here as file-local free functions that
 * take the picker's state (terminal, clock, save, config, options) by
 * reference; the thin overrides simply forward to them.
 */
#include <openglad/platform/curses/curses_picker_client.h>

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/text_wrap.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/platform/curses/curses_input.h>
#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/level_selection.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace og::curses {
namespace {

using og::ui::PickerMenuCommand;
using og::ui::PickerMenuId;
using og::ui::PickerMenuItem;
using og::ui::TextPickerConfig;

// Colors used consistently across the menus so the look is stable.
constexpr Color kTitleColor = Color::Yellow;
constexpr Color kSelectedFg = Color::Black; // foreground when highlighted
constexpr Color kSelectedBg = Color::Cyan;
constexpr Color kNormalColor = Color::Default;
constexpr Color kHintColor = Color::Blue;

// A renderable list entry: a label plus whether it is selectable. Non-selectable
// rows are section headers / stat sheets the cursor skips over.
struct ListEntry {
    std::string label;
    bool selectable = true;
};

// A small, reusable TUI helper bundling the terminal + clock and the three
// interaction primitives the picker needs: a selectable list, a scrollable text
// screen, and a single line of text input. All input goes through
// ITerminal::poll_key so it is fully scriptable with HeadlessTerminal in tests.
class Menu
{
public:
    Menu(ITerminal& term, IClock& clock) : term_(term), clock_(clock) {}

    // Render `entries` under `title`, let the user move the highlight and pick
    // one. Returns the chosen entry index, or -1 on cancel (Esc/q/Backspace or
    // exhausted scripted input). `initial` is the starting highlight (clamped to
    // a selectable entry).
    // `extra_keys`/`out_key`: opt-in single-key actions (the §2.5 roster's
    // d=deploy / r=ready) — a listed key returns the cursor row with
    // *out_key set; Enter/space return with *out_key == 0.
    int choose(std::string_view title, const std::vector<ListEntry>& entries,
               std::string_view hint, int initial,
               std::u32string_view extra_keys = {}, char32_t* out_key = nullptr)
    {
        const int start = clamp_to_selectable(entries, initial, +1);
        if (start < 0)
            return -1; // nothing selectable
        int cursor = start;
        if (out_key != nullptr)
            *out_key = 0;

        for (;;) {
            draw_list(title, entries, hint, cursor);

            const Key key = term_.poll_key(true);
            if (key.is_none())
                return -1; // scripted input exhausted -> treat as cancel
            if (key.is_release())
                continue; // act on presses/repeats only; ignore key-up + focus
            if (out_key != nullptr && key.is_char() &&
                extra_keys.find(key.ch) != std::u32string_view::npos) {
                *out_key = key.ch;
                return cursor;
            }
            if (is_cancel(key))
                return -1;
            if (key.is_enter() || key.is_char(U' '))
                return cursor;
            if (is_up(key)) {
                cursor = step(entries, cursor, -1);
                continue;
            }
            if (is_down(key)) {
                cursor = step(entries, cursor, +1);
                continue;
            }
            // Digit jump: '1'..'9' selects the Nth selectable entry.
            if (key.is_char() && key.ch >= U'1' && key.ch <= U'9') {
                const int target = nth_selectable(entries,
                    static_cast<int>(key.ch - U'1'));
                if (target >= 0)
                    cursor = target;
            }
        }
    }

    // Render a block of text with a "press a key" footer; block until any key is
    // pressed (or scripted input is exhausted).
    void show_text(std::string_view title, const std::vector<std::string>& lines)
    {
        term_.clear();
        int row = 0;
        if (!title.empty()) {
            term_.put_str(row++, 0, title, kTitleColor, Color::Default, true);
            ++row;
        }
        const int footer = term_.rows() - 1;
        for (const std::string& line : lines) {
            if (row >= footer)
                break;
            if (line.empty()) {
                ++row;
                continue;
            }
            // Defensive terminal-width wrap (issue #152): a line wider than
            // the terminal wraps instead of being cut off at the edge.
            for (const std::string& wrapped :
                 og::core::wrap_text(line, term_.cols())) {
                if (row >= footer)
                    break;
                term_.put_str(row++, 0, wrapped, kNormalColor, Color::Default,
                              false);
            }
        }
        if (footer >= 0)
            term_.put_str(footer, 0, "[ press any key ]", kHintColor, Color::Default, false);
        term_.present();
        (void)clock_.now_ms();
        // Wait for a FRESH key press. Crucially we dismiss only on Press, not on
        // repeat: when a level ends because the player walked into the exit, the
        // movement key is still held, and its auto-repeat would otherwise dismiss
        // this "press any key" prompt instantly (the modal would never be seen).
        for (;;) {
            const Key key = term_.poll_key(true);
            if (key.is_none() || key.is_press())
                break;
        }
    }

    // Prompt for a single line of text. On Enter, sets `accepted` and returns the
    // buffer; on Esc (or exhausted input), returns `initial` with `accepted`
    // false. Backspace erases the last character. `context` lines (optional)
    // render between the title and the input row — the scripted camp/book
    // page surface (#206, the Company List "rows above the prompt" shape).
    //
    // The context block SCROLLS (Up/Down, PageUp/PageDown) and wraps like
    // show_text: a camp composes as many lines as the campaign wants — a
    // 24-row roster alone outruns a stock 24x80 terminal — and every one of
    // those lines carries a live ordinal the prompt still accepts. Cutting
    // the tail off silently would hide controls the player is expected to
    // type. The arrow keys are free here: the prompt appends only printable
    // characters, so scrolling never touches the typed buffer.
    std::string prompt(std::string_view title, std::string_view label,
                       std::string_view initial, bool& accepted,
                       const std::vector<std::string>& context = {})
    {
        std::string buffer(initial);
        accepted = false;
        int scroll = 0;
        term_.set_cursor_visible(true);
        std::vector<std::string> wrapped;
        for (const std::string& line : context) {
            if (line.empty()) {
                wrapped.emplace_back();
                continue;
            }
            for (std::string& piece : og::core::wrap_text(line, term_.cols()))
                wrapped.push_back(std::move(piece));
        }
        for (;;) {
            term_.clear();
            int row = 0;
            term_.put_str(row++, 0, title, kTitleColor, Color::Default, true);
            ++row;
            const int input_limit = term_.rows() - 2;
            const int total = static_cast<int>(wrapped.size());
            const int capacity = std::max(0, input_limit - row);
            // An overflowing block spends its last visible row on the marker:
            // a truncation the player cannot see is the whole bug.
            const bool overflow = total > capacity;
            const int shown = overflow ? std::max(0, capacity - 1)
                                       : std::min(total, capacity);
            scroll = std::clamp(scroll, 0, std::max(0, total - shown));
            for (int i = scroll; i < scroll + shown; ++i) {
                term_.put_str(row++, 0,
                              wrapped[static_cast<std::size_t>(i)],
                              kNormalColor, Color::Default, false);
            }
            if (overflow && shown > 0) {
                term_.put_str(row++, 0,
                              std::format("-- {}-{} of {} | Up/Down scroll --",
                                          scroll + 1, scroll + shown, total),
                              kHintColor, Color::Default, false);
            }
            if (!context.empty() && row < input_limit)
                ++row;
            term_.put_str(row, 0, std::string(label) + buffer,
                          kNormalColor, Color::Default, false);
            term_.put_str(term_.rows() - 1, 0,
                          "[ Enter accept | Esc cancel | Backspace erase ]",
                          kHintColor, Color::Default, false);
            term_.present();
            (void)clock_.now_ms();

            const Key key = term_.poll_key(true);
            if (key.is_release())
                continue; // ignore key-up so a release doesn't double-type / cancel
            if (key.is_none() || key.code == KeyCode::Escape || key.is_char(U'\x1b')) {
                term_.set_cursor_visible(false);
                return std::string(initial);
            }
            // Scrolling is arrows-only ('j'/'k' are letters a player may be
            // typing, unlike in choose()).
            if (key.code == KeyCode::Up) {
                --scroll;
                continue;
            }
            if (key.code == KeyCode::Down) {
                ++scroll;
                continue;
            }
            if (key.code == KeyCode::PageUp) {
                scroll -= std::max(1, shown);
                continue;
            }
            if (key.code == KeyCode::PageDown) {
                scroll += std::max(1, shown);
                continue;
            }
            if (key.is_enter()) {
                term_.set_cursor_visible(false);
                accepted = true;
                return buffer;
            }
            if (key.code == KeyCode::Backspace || key.ch == U'\b' || key.ch == 127) {
                if (!buffer.empty())
                    buffer.pop_back();
                continue;
            }
            if (key.is_char() && key.ch >= U' ' && key.ch < 127)
                buffer.push_back(static_cast<char>(key.ch));
        }
    }

private:
    static bool is_cancel(const Key& k)
    {
        return k.code == KeyCode::Escape || k.code == KeyCode::Backspace ||
               k.is_char(U'q') || k.is_char(U'Q') || k.is_char(U'\x1b');
    }
    static bool is_up(const Key& k)
    {
        return k.code == KeyCode::Up || k.is_char(U'k') || k.is_char(U'K');
    }
    static bool is_down(const Key& k)
    {
        return k.code == KeyCode::Down || k.is_char(U'j') || k.is_char(U'J');
    }

    static int nth_selectable(const std::vector<ListEntry>& entries, int nth)
    {
        int seen = 0;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            if (!entries[static_cast<size_t>(i)].selectable)
                continue;
            if (seen == nth)
                return i;
            ++seen;
        }
        return -1;
    }

    // From `from`, advance in `dir` (+1/-1) to the next selectable entry,
    // wrapping. Returns `from` when no other selectable entry exists.
    static int step(const std::vector<ListEntry>& entries, int from, int dir)
    {
        const int n = static_cast<int>(entries.size());
        for (int i = 1; i <= n; ++i) {
            const int idx = ((from + dir * i) % n + n) % n;
            if (entries[static_cast<size_t>(idx)].selectable)
                return idx;
        }
        return from;
    }

    // Clamp `start` onto a selectable entry, searching `dir` first. -1 if none.
    static int clamp_to_selectable(const std::vector<ListEntry>& entries,
                                   int start, int dir)
    {
        const int n = static_cast<int>(entries.size());
        if (n == 0)
            return -1;
        int s = start < 0 ? 0 : (start >= n ? n - 1 : start);
        if (entries[static_cast<size_t>(s)].selectable)
            return s;
        const int next = step(entries, s, dir);
        return entries[static_cast<size_t>(next)].selectable ? next : -1;
    }

    void draw_list(std::string_view title, const std::vector<ListEntry>& entries,
                   std::string_view hint, int cursor)
    {
        term_.clear();
        int row = 0;
        term_.put_str(row++, 0, title, kTitleColor, Color::Default, true);
        ++row;
        const int footer = term_.rows() - 1;
        // Scroll-follow: when the list outgrows the terminal (e.g. the Matchup
        // roster with a full 24-member team), start drawing from an offset
        // that keeps the cursor row visible instead of truncating the tail.
        const int capacity = footer - row;
        const int total = static_cast<int>(entries.size());
        int first = 0;
        if (capacity > 0 && total > capacity && cursor >= capacity)
            first = std::min(cursor - capacity + 1, total - capacity);
        for (int i = first; i < total; ++i) {
            if (row >= footer)
                break;
            const ListEntry& e = entries[static_cast<size_t>(i)];
            if (!e.selectable) {
                term_.put_str(row++, 0, e.label, kHintColor, Color::Default, true);
                continue;
            }
            const bool sel = (i == cursor);
            const Color fg = sel ? kSelectedFg : kNormalColor;
            const Color bg = sel ? kSelectedBg : Color::Default;
            term_.put_str(row++, 0, (sel ? "> " : "  ") + e.label, fg, bg, sel);
        }
        if (!hint.empty() && footer >= 0)
            term_.put_str(footer, 0, hint, kHintColor, Color::Default, false);
        term_.present();
        (void)clock_.now_ms();
    }

    ITerminal& term_;
    IClock& clock_;
};

// --- dynamic menu labels (shared binding layer) --------------------------

// Borrow bundle for the shared label/gate layer (menu_binding.h): labels,
// context lines, and guard messages all come from the same strings the text
// client prints.
og::ui::MenuLabelContext label_context(const TextPickerConfig& config,
                                       const CursesPickerOptions& options,
                                       const SaveData& save)
{
    og::ui::MenuLabelContext context;
    context.save = &save;
    context.session_difficulty = options.difficulty;
    context.spectator = og::ui::is_spectator_mode(save);
    context.campaign = config.campaign;
    context.level = config.level;
    return context;
}

// Mount-guarded level display over the session campaign (the shared
// level_display_guarded helper carries the guard rule).
std::string level_display(const TextPickerConfig& config)
{
    return og::ui::level_display_guarded(config.campaign, config.level);
}

// §2.3: prompt for a 1-based company row number; -1 on cancel/invalid.
int prompt_company_index(Menu& menu, int count, std::string_view title)
{
    bool accepted = false;
    const std::string entered = menu.prompt(title,
        std::format("Company # [1-{}]: ", count), "1", accepted);
    if (!accepted)
        return -1;
    const auto choice = parse_int_strict(entered);
    if (!choice || *choice < 1 || *choice > count) {
        menu.show_text(title, {"Invalid company selection."});
        return -1;
    }
    return *choice - 1;
}

// §2.4: prompt for a 1-based backup row number; -1 on cancel/invalid.
int prompt_backup_index(Menu& menu, int count, std::string_view title)
{
    bool accepted = false;
    const std::string entered = menu.prompt(title,
        std::format("Backup # [1-{}]: ", count), "1", accepted);
    if (!accepted)
        return -1;
    const auto choice = parse_int_strict(entered);
    if (!choice || *choice < 1 || *choice > count) {
        menu.show_text(title, {"Invalid backup selection."});
        return -1;
    }
    return *choice - 1;
}

// --- team views: roster / hire / train -----------------------------------

// §3.8: every curses base-camp mutation autosaves the active company slot
// ([SAVE-R2]: the curses client's slot authority points it at the user's
// chosen quicksave slot). The §4.3 ready-clear is a no-op here — the curses
// picker holds no networked lobby (the network lobby's 'r' key is separate).
void autosave_company_after_mutation(SaveData& save)
{
    (void)og::ui::company_autosave_after_mutation(save, false);
}

void train_team(Menu& menu, SaveData& save, int seed_slot = -1);

// §2.5 command roster (curses shape): deploy flags lead each row;
// Enter = train that character, 'd' = toggle deploy, 'r' = ready (guarded
// outside networked lobbies), Esc = back.
void view_team_roster(Menu& menu, SaveData& save)
{
    int cursor = 0;
    for (;;) {
        const std::vector<int> slots = og::ui::collect_base_camp_slots(save);
        if (slots.empty()) {
            menu.show_text("Team Roster", {"(empty)", "",
                std::format("Gold: {}",
                    og::ui::format_wallet_amount(save, 0))});
            return;
        }

        std::vector<ListEntry> entries;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const guy& member =
                *save.team_list[static_cast<std::size_t>(slots[i])];
            entries.push_back(ListEntry{std::format(
                "[{}] {:<14} {:<14} L={:>2} STR={} DEX={} CON={} INT={} ARM={}",
                member.deployed ? 'X' : ' ', member.name,
                og::ui::family_display_name(member.family), member.level,
                member.strength, member.dexterity, member.constitution,
                member.intelligence, member.armor), true});
        }
        entries.push_back(ListEntry{std::format("DEP {}/{}   Gold: {}",
            og::ui::count_deployed_members(save),
            static_cast<int>(slots.size()),
            og::ui::format_wallet_amount(save, 0)), false});

        char32_t key = 0;
        const int choice = menu.choose("Team Roster", entries,
            "Enter train | d deploy | r ready | Esc back", cursor,
            U"dDrR", &key);
        if (choice < 0)
            return;
        cursor = choice;
        if (choice >= static_cast<int>(slots.size()))
            continue;
        const int slot = slots[static_cast<std::size_t>(choice)];

        if (key == U'd' || key == U'D') {
            // The camp's deploy rules bind this key too (the Camp screen
            // shows the padlock and its reason; deploying the locked hero
            // from the roster anyway would be a different campaign).
            if (const std::optional<std::string> refused =
                    og::ui::terminal_roster_refusal(
                        save, og::ui::TerminalRosterCommand::Deploy, slot)) {
                menu.show_text("Deploy", {*refused});
                continue;
            }
            const bool deployed = og::ui::toggle_deploy_slot(save, slot);
            (void)deployed;
            autosave_company_after_mutation(save);
            continue;
        }
        if (key == U'r' || key == U'R') {
            // §2.6 state 1: no ready machinery outside a networked lobby
            // (the curses network lobby keeps its own 'r' key).
            menu.show_text("Ready",
                {"Ready applies to networked lobbies only."});
            continue;
        }
        train_team(menu, save, slot);
    }
}

// Returns the updated team_families so the caller can re-sync config.
void hire_troops(Menu& menu, SaveData& save, TextPickerConfig& config)
{
    // The camp's can_hire capability — the flag that hides HIRE on the SDL
    // panel — answers this row too.
    if (const std::optional<std::string> refused =
            og::ui::terminal_roster_refusal(
                save, og::ui::TerminalRosterCommand::Hire, -1)) {
        menu.show_text("Hire Troops", {*refused});
        return;
    }
    og::ui::HireSession session(save, 0);
    if (session.team_full()) {
        menu.show_text("Hire Troops",
            {std::format("Team is already at max size ({}).", MAX_TEAM_SIZE)});
        return;
    }

    for (;;) {
        const guy* r = session.current_recruit();
        if (!r)
            break;

        const std::string title = std::format("Hire: {} ({}/{})",
            og::ui::family_display_name(r->family),
            session.family_index() + 1,
            static_cast<int>(og::ui::kAllowableGuys.size()));

        std::vector<ListEntry> entries;
        entries.push_back(ListEntry{std::format("Name: {}", r->name), false});
        entries.push_back(ListEntry{std::format(
            "STR {}  DEX {}  CON {}  INT {}  ARM {}  LVL {}",
            r->strength, r->dexterity, r->constitution,
            r->intelligence, r->armor, r->level), false});
        entries.push_back(ListEntry{std::format("Cost: {}   Gold: {}",
            static_cast<unsigned>(session.current_cost()),
            og::ui::format_wallet_amount(save, 0)), false});
        const int first_action = static_cast<int>(entries.size());
        entries.push_back(ListEntry{"Hire this recruit", true});
        entries.push_back(ListEntry{"Next family", true});
        entries.push_back(ListEntry{"Previous family", true});
        entries.push_back(ListEntry{"Back", true});

        const int choice = menu.choose(title, entries,
            "Enter select | digits jump | Esc back", first_action);
        if (choice < 0)
            return;
        switch (choice - first_action) {
        case 0: { // Hire
            const int slot = session.hire();
            if (slot < 0) {
                menu.show_text("Hire Troops",
                    {"Can't hire (not enough gold or team full)."});
                break;
            }
            menu.show_text("Hire Troops",
                {std::format("Hired {}!",
                    save.team_list[static_cast<size_t>(slot)]->name)});
            config.team_families = og::ui::collect_team_families(save);
            autosave_company_after_mutation(save);  // §3.8
            if (session.team_full()) {
                menu.show_text("Hire Troops", {"Team is now full."});
                return;
            }
            break;
        }
        case 1:
            session.next_family();
            break;
        case 2:
            session.prev_family();
            break;
        default:
            return; // Back
        }
    }
}

// The §2.5 'deploy' item (was Load Team): prompt for a roster row and flip
// its mission-deploy flag (§3.8 autosave rides the flip).
void deploy_prompt(Menu& menu, SaveData& save)
{
    const std::vector<int> slots = og::ui::collect_base_camp_slots(save);
    if (slots.empty()) {
        menu.show_text("Deploy", {"(empty roster - hire someone first)"});
        return;
    }
    bool accepted = false;
    const std::string entered = menu.prompt("Deploy",
        std::format("Toggle deploy for roster row [1-{}]: ", slots.size()),
        "1", accepted);
    if (!accepted)
        return;
    const auto value = parse_int_strict(entered);
    if (!value || *value < 1 ||
        static_cast<std::size_t>(*value) > slots.size()) {
        menu.show_text("Deploy", {"Invalid roster row."});
        return;
    }
    const int slot = slots[static_cast<std::size_t>(*value - 1)];
    if (const std::optional<std::string> refused =
            og::ui::terminal_roster_refusal(
                save, og::ui::TerminalRosterCommand::Deploy, slot)) {
        menu.show_text("Deploy", {*refused});
        return;
    }
    const bool deployed = og::ui::toggle_deploy_slot(save, slot);
    menu.show_text("Deploy", {std::format("{} {}.",
        save.team_list[static_cast<std::size_t>(slot)]->name,
        deployed ? "deployed" : "benched")});
    autosave_company_after_mutation(save);
}

void train_team(Menu& menu, SaveData& save, int seed_slot)
{
    // A camp that retired training refuses in words: the SDL panel simply
    // has no train affordance to click, but a prompt cannot hide.
    if (const std::optional<std::string> refused =
            og::ui::terminal_roster_refusal(
                save, og::ui::TerminalRosterCommand::Train, -1)) {
        menu.show_text("Train Team", {*refused});
        return;
    }
    og::ui::TrainSession session(save);
    if (session.empty()) {
        menu.show_text("Train Team", {"No team members available to train."});
        return;
    }
    if (seed_slot >= 0)
        (void)session.seek_slot(seed_slot);  // §2.5 Enter-on-row direct-open

    using S = og::ui::TrainSession::Stat;
    for (;;) {
        const guy& w = session.working_copy();
        const guy& o = session.original();
        const char* slock = session.level_increased() ? " [locked]" : "";
        const char* llock = session.stats_increased() ? " [locked]" : "";

        const std::string title = std::format("Train: {} ({})",
            w.name, og::ui::family_display_name(w.family));

        // Indices 0..5 are the six stat rows (Enter raises that stat by 1).
        std::vector<ListEntry> entries;
        entries.push_back(ListEntry{
            std::format("STR  {:5} (was {:5}){}", w.strength, o.strength, slock), true});
        entries.push_back(ListEntry{
            std::format("DEX  {:5} (was {:5}){}", w.dexterity, o.dexterity, slock), true});
        entries.push_back(ListEntry{
            std::format("CON  {:5} (was {:5}){}", w.constitution, o.constitution, slock), true});
        entries.push_back(ListEntry{
            std::format("INT  {:5} (was {:5}){}", w.intelligence, o.intelligence, slock), true});
        entries.push_back(ListEntry{
            std::format("ARM  {:5} (was {:5}){}", w.armor, o.armor, slock), true});
        entries.push_back(ListEntry{
            std::format("LVL  {:5} (was {:5}){}", w.level, o.level, llock), true});
        entries.push_back(ListEntry{std::format("Cost: {}   Gold: {}",
            static_cast<unsigned>(session.current_cost()),
            og::ui::format_wallet_amount(save, 0)), false});
        const int first_action = static_cast<int>(entries.size());
        entries.push_back(ListEntry{"Accept training", true});
        entries.push_back(ListEntry{"Next member", true});
        entries.push_back(ListEntry{"Previous member", true});
        entries.push_back(ListEntry{"Back", true});

        const int choice = menu.choose(title, entries,
            "Enter +1 stat | select action | Esc back", 0);
        if (choice < 0)
            return;
        if (choice >= 0 && choice <= 5) {
            const S stats[] = {S::Strength, S::Dexterity, S::Constitution,
                               S::Intelligence, S::Armor, S::Level};
            session.increase_stat(stats[choice]);
            continue;
        }
        switch (choice - first_action) {
        case 0: // Accept
            if (session.accept()) {
                menu.show_text("Train Team", {"Training accepted."});
                autosave_company_after_mutation(save);  // §3.8
            } else {
                menu.show_text("Train Team", {"Can't afford training."});
            }
            break;
        case 1:
            session.next_member();
            break;
        case 2:
            session.prev_member();
            break;
        default:
            return; // Back
        }
    }
}

// MATCHUP groups the roster by team. A curses process owns at most one local
// seat, so its leading row cycles P1's real launch/lobby assignment
// (SaveData::my_team); Enter on a character still cycles that roster member's
// combat team.
void teams_screen(Menu& menu, SaveData& save)
{
    if (save.my_team < 0 || save.my_team >= SCORE_TEAM_COUNT)
        save.my_team = 0;

    int cursor = 0;
    for (;;) {
        struct RowAction {
            bool cycle_player = false;
            int cycle_slot = -1;
        };
        std::vector<ListEntry> entries;
        std::vector<RowAction> actions;

        const bool has_local_player = save.numplayers != 0;
        if (has_local_player) {
            entries.push_back(ListEntry{"Seats", false});
            actions.push_back(RowAction{});
            entries.push_back(ListEntry{std::format(
                "P1 plays {}",
                og::sim::team_color_name(save.my_team)), true});
            actions.push_back(RowAction{.cycle_player = true});
        }

        for (short t = 0; t < 4; ++t) {
            const std::string seat_tag =
                has_local_player && save.my_team == t ? "(P1)" : "";

            int hero_count = 0;
            std::vector<std::pair<int, std::string>> members;
            for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot) {
                const auto& member = save.team_list[static_cast<std::size_t>(slot)];
                if (!member || member->teamnum != t)
                    continue;
                ++hero_count;
                members.emplace_back(slot, std::format("    {} ({})",
                    member->name,
                    og::ui::family_display_name(member->family)));
            }

            // No world is loaded in the curses picker, so no map tags.
            entries.push_back(ListEntry{og::ui::format_team_row_label(
                t, hero_count, false, false, false, seat_tag), false});
            actions.push_back(RowAction{});
            for (auto& [slot, label] : members) {
                entries.push_back(ListEntry{std::move(label), true});
                actions.push_back(RowAction{.cycle_slot = slot});
            }
        }

        const int choice = menu.choose("Matchup", entries,
            "Enter: cycle seat / character team | Esc back", cursor);
        if (choice < 0)
            return;
        cursor = choice;

        const RowAction& action = actions[static_cast<size_t>(choice)];
        if (action.cycle_player) {
            save.my_team =
                static_cast<short>((save.my_team + 1) % SCORE_TEAM_COUNT);
        } else if (action.cycle_slot >= 0) {
            if (og::ui::cycle_guy_team(save, action.cycle_slot, 1) >= 0)
                autosave_company_after_mutation(save);  // §3.8 team cycle
        }
    }
}

// Read-only roster report of the current level from a scratch headless load
// (the same shared report the SDL VIEW LEVEL screen renders).
void view_scenario(Menu& menu, const SaveData& save)
{
    if (get_mounted_campaign() != save.current_campaign) {
        menu.show_text("View Scenario", {std::format(
            "Campaign '{}' is not mounted.", save.current_campaign)});
        return;
    }

    LevelRuntimeData scenario(save.scen_num, false,
        &headless_level_data_hooks());
    if (!scenario.load()) {
        menu.show_text("View Scenario", {std::format(
            "Could not load level {}.", static_cast<int>(save.scen_num))});
        return;
    }

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(scenario.world(), save);
    std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    lines.insert(lines.begin(), std::format("SCEN {}: {}",
        static_cast<int>(save.scen_num), scenario.world().title));
    menu.show_text("View Scenario", lines);
}

// Split a popup-shaped message ('\n' separated) into list lines.
std::vector<std::string> split_message_lines(const std::string& message)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char ch : message) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    return lines;
}

// #155: hooks for the shared cloud flows — bridge HTTP (absent in the curses
// bridge -> the flows report unavailability), the NO-first list confirm, and
// show_text notify. `notified` lets the caller show the returned status only
// for silent paths (the explicit-No cancels).
og::ui::cloud::CloudHooks curses_cloud_hooks(Menu& menu, bool& notified)
{
    og::ui::cloud::CloudHooks hooks;
    const PlatformBridge& bridge = platform_bridge();
    if (bridge.cloud_http_get)
        hooks.http_get = bridge.cloud_http_get;
    if (bridge.cloud_http_post)
        hooks.http_post = bridge.cloud_http_post;
    hooks.confirm = [&menu](const std::string& title,
                            const std::string& message) {
        // NO-first confirm (U3): the highlight starts on No.
        std::vector<ListEntry> entries;
        for (const std::string& line : split_message_lines(message))
            entries.push_back(ListEntry{line, false});
        const int no_index = static_cast<int>(entries.size());
        entries.push_back(ListEntry{"No", true});
        entries.push_back(ListEntry{"Yes", true});
        const int verdict = menu.choose(title, entries,
            "Enter select | Esc back", no_index);
        return verdict == no_index + 1;
    };
    hooks.notify = [&menu, &notified](const std::string& title,
                                      const std::string& message) {
        menu.show_text(title, split_message_lines(message));
        notified = true;
    };
    return hooks;
}

// #206 CAMP, curses projection: the shared terminal driver over this
// client's save. The camp renders as prompt context lines (the Company List
// "dynamic rows + prompt" shape — never Menu::choose, whose digit jump
// stops at row 9) and notices ride show_text; every camp line, docket
// ordinal and roster row is composed by the driver, byte-identical with the
// text client.
void campaign_camp_flow(Menu& menu, SaveData& save,
                        TextPickerConfig& config,
                        const CursesPickerOptions& options)
{
    og::ui::TerminalCampaignPickerIo io;
    io.prompt = [&menu](const std::string& title,
                        const std::vector<std::string>& lines,
                        const std::string& label)
        -> std::optional<std::string> {
        bool accepted = false;
        const std::string entered = menu.prompt(title, label, "", accepted,
                                                lines);
        if (!accepted)
            return std::nullopt;
        return entered;
    };
    io.notice = [&menu](const std::string& line) {
        menu.show_text("Camp", {line});
    };
    // The SET LEVEL host predicate: this picker screen is only reachable
    // locally (the curses network lobby is a separate flow), so the shared
    // context always answers host here.
    io.is_host = [&config, &options, &save] {
        return label_context(config, options, save).is_host;
    };
    io.apply_level = [&config, &save](int level, bool replay_arm) {
        // The curses "Set level" tail: session config + save cursor. The
        // replay arm (#207) re-checks cleared at the write choke — the row
        // decoration is fetch-time state. A plain write abandons any
        // excursion in flight (a stale arm must never skip a purge).
        config.level = level;
        if (replay_arm && save.is_level_completed(level))
            save.arm_replay(static_cast<short>(level));
        else
        {
            save.clear_replay_arm();
            save.scen_num = static_cast<short>(level);
        }
    };
    og::ui::run_terminal_campaign_camp(save, io);
}

} // namespace

// --- construction --------------------------------------------------------

CursesPickerClient::CursesPickerClient(ITerminal& term, IClock& clock,
                                       TextPickerConfig& config,
                                       const CursesPickerOptions& options)
    : term_(term), clock_(clock), config_(config), options_(options)
{
    // Match TextPickerClient: guarantee a starting team exists immediately.
    if (config_.team_families.empty())
        config_.team_families.push_back(FAMILY_SOLDIER);
    og::ui::initialize_starting_team(save_data_, config_.team_families);
    // Terminal slot authority ([SAVE-R2]): company-level writes must target
    // this client's chosen slot, never save0. An unsafe name is rejected by
    // the setter and leaves the previous active slot in place.
    assert_company_slot_authority();
    // Campaign scripting (#206): point the process-global og.campaign_*
    // providers at THIS client's SaveData — the same object the picker
    // mutates — so campaign hooks always read/write the live save (the
    // design doc's install-site list: the curses picker's session, beside
    // the [SAVE-R2] slot repoint).
    og::script::hooks::install_campaign_providers(
        og::data::make_campaign_providers(save_data_));
}

CursesPickerClient::~CursesPickerClient()
{
    // #206: the providers borrow save_data_ — clear before it dies.
    og::script::hooks::clear_campaign_providers();
}

void CursesPickerClient::assert_company_slot_authority() const
{
    (void)og::data::set_active_company_slot(config_.save_name);
}

// --- IPickerClient: menu presentation ------------------------------------

const PickerMenuItem* CursesPickerClient::present_menu(PickerMenuId menu_id)
{
    Menu menu(term_, clock_);

    if (config_.team_families.empty())
        config_.team_families.push_back(FAMILY_SOLDIER);
    og::ui::initialize_starting_team(save_data_, config_.team_families);

    const og::ui::TerminalMenuModel model = og::ui::build_terminal_menu_model(
        menu_id, label_context(config_, options_, save_data_));

    // Build the entry list: team-build context as non-selectable headers, then
    // the menu items with their dynamic labels.
    std::vector<ListEntry> entries;
    for (const std::string& line : model.context_lines)
        entries.push_back(ListEntry{line, false});
    const int first_item = static_cast<int>(entries.size());
    for (const og::ui::TerminalMenuEntry& entry : model.entries)
        entries.push_back(ListEntry{entry.label, true});

    const int choice = menu.choose(model.title, entries,
        "Up/Down or j/k move | Enter select | digits jump | Esc/q back",
        first_item);

    // Cancel: Quit from Main, Back elsewhere (mirrors TextPickerClient).
    if (choice < 0)
        return model.cancel_item;
    return model.entries[static_cast<size_t>(choice - first_item)].item;
}

void CursesPickerClient::handle_menu_item(PickerMenuId menu_id,
                                          const PickerMenuItem& item)
{
    Menu menu(term_, clock_);
    if (menu_id == PickerMenuId::Main) {
        switch (item.command) {
        case PickerMenuCommand::SetPlayerMode:
            if (item.arg > 1) {
                og::ui::set_player_count(save_data_, 1);
                menu.show_text("Players",
                    {"The curses client supports one local player.",
                     "Network players join from separate clients."});
            } else {
                og::ui::set_player_count(save_data_, item.arg);
                menu.show_text("Players",
                    {std::format("Player mode set to {}.", item.arg)});
            }
            break;
        case PickerMenuCommand::ToggleAlliedMode:
            // Compatibility-only command: no current menu exposes it. If an
            // older caller dispatches the enum, point at the new per-level
            // surface without mutating the legacy persisted bit.
            menu.show_text("Base Camp",
                {"Choose this player's team from Matchup."});
            break;
        case PickerMenuCommand::LevelEdit:
            menu.show_text("Level Editor",
                {"The level editor is not available in the curses client.",
                 "Use the standalone 'openscen' tool instead."});
            break;
        case PickerMenuCommand::OpenCloudMenu:
            // #155: the CLOUD submenu — the shared nested presentation loop
            // until Back.
            show_submenu(PickerMenuId::CloudSave);
            break;
        default:
            break;
        }
        return;
    }

    // #155 cloud saves, curses projection: passphrase via the line prompt,
    // upload/download through the shared pure flows with the NO-first list
    // confirm. The curses bridge installs no cloud HTTP callbacks, so the
    // flows degrade with the D8 unavailable notice.
    if (menu_id == PickerMenuId::CloudSave) {
        switch (item.command) {
        case PickerMenuCommand::CloudSetPassphrase: {
            bool accepted = false;
            const std::string entered = menu.prompt("Cloud Passphrase",
                "Passphrase (8-64 chars): ", "", accepted);
            if (!accepted || entered.empty()) {
                menu.show_text("Cloud Save", {"Passphrase unchanged."});
                break;
            }
            const std::string key =
                og::ui::cloud::derive_cloud_save_key(entered);
            if (key.empty()) {
                menu.show_text("Cloud Save",
                    {"Passphrase must be 8-64 characters."});
                break;
            }
            og::ui::cloud::store_cloud_key(key);
            menu.show_text("Cloud Save", {"Passphrase set."});
            break;
        }
        case PickerMenuCommand::CloudUpload: {
            bool notified = false;
            const std::string status = og::ui::cloud::run_cloud_upload(
                {}, curses_cloud_hooks(menu, notified));
            if (!notified)
                menu.show_text("Cloud Save", {status});
            break;
        }
        case PickerMenuCommand::CloudDownload: {
            bool notified = false;
            const std::string status = og::ui::cloud::run_cloud_download(
                {}, curses_cloud_hooks(menu, notified),
                [this](const std::string& slot) {
                    return open_downloaded_company(slot);
                });
            if (!notified)
                menu.show_text("Cloud Save", {status});
            break;
        }
        default:
            break;
        }
        return;
    }

    // The Difficulty submenu: session difficulty stays an option int; the
    // match rules (respawns, delay, permadeath, generators) live on the save
    // via the shared pure helpers, so a hosted lobby broadcasts them.
    if (menu_id == PickerMenuId::Difficulty) {
        switch (item.command) {
        case PickerMenuCommand::SetDifficulty:
        {
            options_.difficulty = og::ui::cycle_difficulty(options_.difficulty);
            const int difficulty_index =
                ((options_.difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) % DIFFICULTY_SETTINGS;
            menu.show_text("Difficulty",
                {std::format("Difficulty set to {}.",
                    og::ui::kDifficultyNames[difficulty_index])});
            autosave_company_after_mutation(save_data_); // §3.8 settings tail
            break;
        }
        case PickerMenuCommand::CycleRespawnMode:
            og::ui::cycle_respawn_mode(save_data_);
            menu.show_text("Respawns",
                {og::ui::format_respawn_mode_label(save_data_)});
            autosave_company_after_mutation(save_data_); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleRespawnDelay:
            og::ui::cycle_respawn_delay(save_data_);
            menu.show_text("Respawn Delay",
                {og::ui::format_respawn_delay_label(save_data_)});
            autosave_company_after_mutation(save_data_); // §3.8 settings tail
            break;
        case PickerMenuCommand::TogglePermadeath:
            og::ui::toggle_permadeath(save_data_);
            menu.show_text("Permadeath",
                {og::ui::format_permadeath_label(save_data_)});
            autosave_company_after_mutation(save_data_); // §3.8 settings tail
            break;
        case PickerMenuCommand::CycleGeneratorRate:
            og::ui::cycle_generator_rate(save_data_);
            menu.show_text("Generators",
                {og::ui::format_generator_rate_label(save_data_)});
            autosave_company_after_mutation(save_data_); // §3.8 settings tail
            break;
        case PickerMenuCommand::ToggleInfiniteGold:
            // SESSION-ONLY (never in the GTL file), so unlike every other row
            // here this one deliberately skips the settings autosave tail.
            og::ui::toggle_infinite_gold(save_data_);
            menu.show_text("Infinite Gold",
                {og::ui::format_infinite_gold_label(save_data_)});
            break;
        default:
            break;
        }
        return;
    }

    // Team Build menu. Gated items (READY outside a networked lobby) show
    // their shared guard message instead of acting.
    const std::string_view guard = og::ui::terminal_gate_message(
        item, label_context(config_, options_, save_data_));
    if (!guard.empty()) {
        menu.show_text(std::string(item.label), {std::string(guard)});
        return;
    }

    switch (item.command) {
    case PickerMenuCommand::ViewTeam:
        view_team_roster(menu, save_data_);
        break;
    case PickerMenuCommand::TrainTeam:
        train_team(menu, save_data_);
        break;
    case PickerMenuCommand::HireTroops:
        hire_troops(menu, save_data_, config_);
        break;
    case PickerMenuCommand::ToggleDeploy:
        deploy_prompt(menu, save_data_);
        break;
    // ToggleReady is gated NetworkedOnly: the guard above already showed
    // its message (the curses picker holds no networked lobby).
    case PickerMenuCommand::ShowProgress:
        menu.show_text("Progress",
            {std::format("Campaign: {}",
                 og::data::campaign_display_title(config_.campaign)),
             std::format("Level: {}", level_display(config_))});
        break;
    case PickerMenuCommand::Networking:
        (void)configure_networking();
        break;
    case PickerMenuCommand::SetLevel: {
        bool accepted = false;
        const std::string entered = menu.prompt("Set Level",
            std::format("Level (current {}): ", level_display(config_)),
            std::to_string(config_.level), accepted);
        if (accepted && !entered.empty()) {
            const auto level = parse_int_strict(entered);
            std::string title;
            if (!level || *level < 1)
                menu.show_text("Set Level", {"Invalid level."});
            // Existence + the earned-roads gate: the free-typed id gets the
            // same answer the camp's docket gives the same road, before the
            // cursor (or any autosave tail) moves.
            else if (og::data::load_scenario_title_with_error(
                         ("scen" + std::to_string(*level)).c_str(), title) !=
                         og::data::LevelFileIoError::None ||
                     !og::data::level_selection_allowed(save_data_, *level))
                menu.show_text("Set Level",
                    {std::string(og::ui::kCampaignLevelClosedMessage)});
            else {
                config_.level = *level;
                // A plain Set Level abandons any replay excursion in flight.
                save_data_.clear_replay_arm();
                save_data_.scen_num = static_cast<short>(*level);
            }
        }
        break;
    }
    case PickerMenuCommand::ReplayLevel: {
        // #207: the terminal replay prompt — Set Level's gates plus one
        // more: the level must already be CLEARED (a replay re-fights a
        // beaten level; the frontier is GO's job). Arming moves the cursor
        // itself, so everything downstream behaves like a set.
        bool accepted = false;
        const std::string entered = menu.prompt("Replay Level",
            "Level (must be cleared): ", std::string(), accepted);
        if (accepted && !entered.empty()) {
            const auto level = parse_int_strict(entered);
            std::string title;
            if (!level || *level < 1)
                menu.show_text("Replay Level", {"Invalid level."});
            else if (og::data::load_scenario_title_with_error(
                         ("scen" + std::to_string(*level)).c_str(), title) !=
                         og::data::LevelFileIoError::None ||
                     !og::data::level_selection_allowed(save_data_, *level))
                menu.show_text("Replay Level",
                    {std::string(og::ui::kCampaignLevelClosedMessage)});
            else if (!save_data_.is_level_completed(*level))
                menu.show_text("Replay Level",
                    {"That level is not cleared yet."});
            else {
                config_.level = *level;
                save_data_.arm_replay(static_cast<short>(*level));
                menu.show_text("Replay Level",
                    {og::ui::campaign_replay_set_message(
                        title.empty() ? std::format("SCEN {}", *level)
                                      : title)});
            }
        }
        break;
    }
    case PickerMenuCommand::SetCampaign:
        (void)show_campaign_select();
        break;
    case PickerMenuCommand::OpenDifficultyMenu:
        // The DIFFICULTY submenu: the shared nested presentation loop
        // (show_submenu in picker_state) until Back. It answers here now —
        // the door moved off the main menu to Team Build.
        show_submenu(PickerMenuId::Difficulty);
        break;
    // Match teams and target score left the flat team-build list for the
    // modes camp's MATCH SETUP page; the SCENARIO submenu still routes its
    // troops row through here.
    case PickerMenuCommand::ToggleCtfScenarioTroops:
        og::ui::toggle_ctf_scenario_troops(save_data_);
        menu.show_text("Scenario Troops",
            {og::ui::format_ctf_troops_label(save_data_)});
        autosave_company_after_mutation(save_data_); // §3.8 settings tail
        break;
    case PickerMenuCommand::ViewScenario:
        view_scenario(menu, save_data_);
        break;
    case PickerMenuCommand::Teams:
        teams_screen(menu, save_data_);
        config_.team_families = og::ui::collect_team_families(save_data_);
        break;
    case PickerMenuCommand::CampaignCamp:
        // #206: the Base Camp gameplay zone (guard + flow live in the shared
        // terminal driver).
        campaign_camp_flow(menu, save_data_, config_, options_);
        break;
    default:
        break;
    }
}

// --- IPickerClient: lifecycle hooks --------------------------------------

bool CursesPickerClient::prepare_new_game()
{
    // §2.2 name entry: a generated fantasy name the user can edit inline or
    // reroll (clear the field to get a fresh suggestion). Esc cancels —
    // nothing is created, so the loaded game survives untouched.
    Menu menu(term_, clock_);
    SeededRandom rng(
        static_cast<std::uint32_t>(og::data::company_clock_now_s()));
    std::string company_name = og::ui::generate_company_name(rng);
    for (;;) {
        bool accepted = false;
        // Deliberately NO "file: <slug>.gtl" preview in the label (§9.3/F2
        // removed it on every client; here it was also simply false — the
        // curses client persists in place to its own slot (config_.save_name,
        // [SAVE-R2]), so the SDL slug preview would name a file that is
        // never created).
        const std::string label = "Name: ";
        std::string result =
            menu.prompt("Found Your Company", label, company_name, accepted);
        if (!accepted)
            return false;
        if (result.empty()) {
            company_name = og::ui::generate_company_name(rng);
            continue;
        }
        if (result.size() > og::ui::kCompanyNameMaxLen)
            result.resize(og::ui::kCompanyNameMaxLen);
        company_name = std::move(result);
        break;
    }

    og::ui::reset_for_new_game(save_data_);
    og::ui::ensure_team_populated(save_data_);
    // The display name lives in the 40-byte save_name; the filename stays this
    // client's own slot (config_.save_name, [SAVE-R2]).
    save_data_.save_name = company_name;
    assert_company_slot_authority(); // [SAVE-R2]
    // A new game always starts on the default campaign: drop any campaign a
    // previously loaded save left in the session config (run_game copies
    // config_.campaign back over the freshly reset save) and re-point the
    // mount so the in-picker scenario viewer stays coherent.
    config_.campaign = save_data_.current_campaign;
    (void)og::ui::sync_campaign_mount_to_save(save_data_);
    // #162: glyph client, no pixies — refresh the loader with the mount so
    // walker sizes match the campaign the next level build will use.
    headless_entity_loader()->reload_graphics_if_stale();
    config_.team_families = og::ui::collect_team_families(save_data_);
    return true;
}

std::string CursesPickerClient::show_campaign_select()
{
    Menu menu(term_, clock_);
    std::list<std::string> campaigns = list_campaigns();
    og::ui::order_campaigns_for_select(campaigns);
    // Kept in lockstep with the SDL/curses shelves: networked lobbies must
    // not offer the (local-only) tower. The curses network lobby fixes its
    // campaign from the save at host/join time (this picker screen is only
    // reachable locally), so the flag is always false here today; the
    // LobbyServer sanitize backstop covers a tower save carried into a
    // hosted lobby.
    og::ui::filter_campaigns_for_networked_lobby(campaigns,
                                                 /*networked_session=*/false);
    std::vector<std::string> ids(campaigns.begin(), campaigns.end());

    if (ids.empty()) {
        // Title when known; campaign_display_title falls back to the raw id
        // when the kept campaign's package is itself missing.
        menu.show_text("Campaign Select",
            {std::format("No campaigns found; keeping '{}'.",
                og::data::campaign_display_title(config_.campaign))});
        return config_.campaign;
    }

    // Human titles, disambiguated with the raw id when two packages share a
    // title; the selection result stays the raw id.
    const std::vector<std::string> labels =
        og::ui::format_campaign_select_labels(ids);
    std::vector<ListEntry> entries;
    entries.reserve(ids.size());
    int initial = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        entries.push_back(ListEntry{labels[i], true});
        if (ids[i] == config_.campaign)
            initial = static_cast<int>(i);
    }

    const int choice = menu.choose("Campaign Select", entries,
        "Enter select | Esc keeps current", initial);
    if (choice < 0)
        return config_.campaign; // cancel keeps current

    config_.campaign = ids[static_cast<size_t>(choice)];
    // A campaign SWITCH abandons any replay excursion in flight — the arm's
    // origin is a cursor in the previous campaign and must never be
    // restored into this one (#207). Re-selecting the current campaign
    // changes nothing and keeps the arm.
    if (save_data_.current_campaign != config_.campaign)
        save_data_.clear_replay_arm();
    save_data_.current_campaign = config_.campaign;
    // The launch path self-heals the mount, but the in-picker scenario
    // viewer reads the mounted package directly — follow the selection now.
    (void)og::ui::sync_campaign_mount_to_save(save_data_);
    // #162: same for the sprite set (glyph client, no pixie borrows).
    headless_entity_loader()->reload_graphics_if_stale();
    return config_.campaign;
}

void CursesPickerClient::show_options()
{
    Menu menu(term_, clock_);

    // Titled after the door that opened it (GAME SETTINGS on the main
    // menu), not after the command's internal name.
    bool accepted = false;
    const std::string slot = menu.prompt("Game Settings", "Save slot: ",
        config_.save_name, accepted);
    if (accepted && !slot.empty())
    {
        config_.save_name = slot;
        assert_company_slot_authority(); // [SAVE-R2] slot command
    }

    accepted = false;
    const std::string seed = menu.prompt("Game Settings", "Seed: ",
        std::to_string(static_cast<unsigned>(config_.seed)), accepted);
    if (accepted && !seed.empty()) {
        const auto value = parse_int_strict(seed);
        if (!value || *value < 0)
            menu.show_text("Game Settings",
                           {"Invalid seed; keeping current."});
        else
            config_.seed = static_cast<std::uint32_t>(*value);
    }
}

void CursesPickerClient::show_help()
{
    Menu menu(term_, clock_);
    menu.show_text("Help",
        {"OpenGlad - terminal client.",
         "",
         "Begin New Game resets your team and enters Team Build.",
         "Continue Game opens Team Build; use GO! there to start playing.",
         "",
         "Menus: Up/Down or j/k move, Enter/Space select,",
         "       digit keys jump, Esc/q go back.",
         "",
         "In game: WASD/arrows move, space fires, f casts special,",
         "         tab switches character, q quits to the menu."});
}

void CursesPickerClient::run_game()
{
    if (config_.team_families.empty())
        config_.team_families.push_back(FAMILY_SOLDIER);
    og::ui::initialize_starting_team(save_data_, config_.team_families);
    config_.team_families = og::ui::collect_team_families(save_data_);
    save_data_.current_campaign = config_.campaign;
    save_data_.scen_num = static_cast<short>(config_.level);

    Menu menu(term_, clock_);
    std::string error;
    std::unique_ptr<CursesGameSession> session =
        make_local_session(save_data_, options_.difficulty, &error);
    if (!session) {
        // make_local_session may be unimplemented (nullptr) in a concurrent
        // build; fail gracefully rather than crashing.
        menu.show_text("Unable to start",
            {error.empty() ? "Could not start the game." : error});
        return;
    }

    CursesInput input;
    CursesRenderer renderer;
    const GameRunResult result =
        run_level_loop(*session, term_, clock_, input, renderer, LevelLoopOptions{});

    if (result.ended && result.ending == 0 && !result.mode_rematch) {
        // §3.8: the curses win commit (run_level_loop already folded the win
        // into save_data_ via commit_result_to_save, gated on this same
        // shape) persists through the company autosave choke point against
        // the client's active slot: stamp last_played, atomic write, one
        // retention-pruned backup snapshot. Failures are logged inside the
        // choke point; the committed roster stays in memory either way.
        assert_company_slot_authority(); // [SAVE-R2]
        (void)og::data::company_autosave(
            save_data_, og::data::CompanyAutosaveKind::LevelWin);
    }

    // #207 design point 5: a lost/quit replay restores the campaign cursor
    // on picker re-entry (a win already restored it inside the fold and the
    // commit copy-back carried the cleared arm). Persist the healed cursor
    // like any other picker mutation (§3.8).
    if (og::ui::replay_reentry_restore(save_data_)) {
        config_.level = save_data_.scen_num;
        autosave_company_after_mutation(save_data_);
    }

    if (result.ended) {
        // mission_verdict_line is CTF-aware: a CTF loss still ends with the
        // classic WIN shape, so ending==0 alone must not claim victory. The
        // rematch shape's "next level" is this same level — say so honestly.
        // The next level's title is read off whatever campaign the session
        // left mounted (the one just played); scenario_display_name falls
        // back to "N. Level N" if it cannot be read.
        menu.show_text("Mission complete",
            {mission_verdict_line(result),
             (result.next_level >= 0 && !result.mode_rematch)
                 ? std::format("Next level: {}",
                       og::data::scenario_display_name(result.next_level))
                 : std::string("Returning to team build.")});
    }
}

og::ui::PickerScreen CursesPickerClient::screen_after_game() const
{
    return og::ui::PickerScreen::TeamBuild;
}

// --- IPickerClient: save / load ------------------------------------------

bool CursesPickerClient::load_game()
{
    Menu menu(term_, clock_);
    assert_company_slot_authority(); // [SAVE-R2]
    const SaveDataIoError io = save_data_.load_with_error(config_.save_name);
    if (io != SaveDataIoError::None) {
        menu.show_text("Load failed",
            {std::format("Load failed for '{}' ({}).",
                config_.save_name, og::ui::save_error_string(io))});
        return false;
    }

    config_.campaign = save_data_.current_campaign;
    config_.level = save_data_.scen_num > 0 ? save_data_.scen_num : 1;

    og::ui::ensure_team_populated(save_data_);
    config_.team_families = og::ui::collect_team_families(save_data_);

    menu.show_text("Loaded",
        {std::format("Loaded '{}'", config_.save_name),
         // load_with_error mounted the save's campaign, so the titled level
         // form is in sync here.
         std::format("Campaign: {}  Level: {}",
             og::data::campaign_display_title(config_.campaign),
             level_display(config_)),
         std::format("Team: {}  Gold: {}",
             static_cast<int>(save_data_.team_size),
             og::ui::format_wallet_amount(save_data_, 0))});
    return true;
}

bool CursesPickerClient::open_downloaded_company(const std::string& slot)
{
    const std::string previous_slot = config_.save_name;
    config_.save_name = slot;
    if (load_game())
        return true;
    config_.save_name = previous_slot;
    assert_company_slot_authority(); // [SAVE-R2]
    return false;
}

bool CursesPickerClient::save_game()
{
    Menu menu(term_, clock_);
    assert_company_slot_authority(); // [SAVE-R2]
    if (config_.team_families.empty())
        config_.team_families.push_back(FAMILY_SOLDIER);
    og::ui::initialize_starting_team(save_data_, config_.team_families);
    save_data_.current_campaign = config_.campaign;
    save_data_.scen_num = static_cast<short>(config_.level);
    save_data_.numplayers = 1;

    const SaveDataIoError io = save_data_.save_with_error(config_.save_name);
    if (io != SaveDataIoError::None) {
        menu.show_text("Save failed",
            {std::format("Save failed for '{}' ({}).",
                config_.save_name, og::ui::save_error_string(io))});
        return false;
    }

    menu.show_text("Saved", {std::format("Saved '{}'.", config_.save_name)});
    return true;
}

// §2.3 Company List, curses projection: the joined company rows (shared row
// formatter — byte-identical with the text client) sit as non-selectable
// context lines above the LoadCompany chrome (open / backups / delete /
// back); open and delete prompt for a row number. Opening repoints this
// client's slot ([SAVE-R2]) via load_game and returns true so the state
// machine proceeds to team build.
bool CursesPickerClient::show_company_list()
{
    Menu menu(term_, clock_);
    for (;;) {
        const std::vector<og::data::CompanyInfo> companies =
            og::data::list_companies();
        if (companies.empty()) {
            menu.show_text("Companies", {"No companies yet."});
            return false;
        }

        const og::ui::TerminalMenuModel model =
            og::ui::build_terminal_menu_model(
                PickerMenuId::LoadCompany,
                label_context(config_, options_, save_data_));
        std::vector<ListEntry> entries;
        entries.push_back(ListEntry{
            og::ui::format_company_list_title(
                static_cast<int>(companies.size())), false});
        for (const og::data::CompanyInfo& info : companies) {
            entries.push_back(ListEntry{
                og::ui::format_company_row_line(info,
                    info.slot == og::data::active_company_slot()), false});
        }
        const int first_item = static_cast<int>(entries.size());
        for (const og::ui::TerminalMenuEntry& entry : model.entries)
            entries.push_back(ListEntry{entry.label, true});

        const int choice = menu.choose(model.title, entries,
            "Up/Down or j/k move | Enter select | digits jump | Esc/q back",
            first_item);
        if (choice < 0)
            return false;
        const PickerMenuItem* item =
            model.entries[static_cast<size_t>(choice - first_item)].item;

        switch (item->command) {
        case PickerMenuCommand::Back:
            return false;
        case PickerMenuCommand::OpenCompany: {
            const int idx = prompt_company_index(
                menu, static_cast<int>(companies.size()), "Open Company");
            if (idx < 0)
                break;
            const og::data::CompanyInfo& info =
                companies[static_cast<size_t>(idx)];
            if (!info.valid) {
                // Never silently switch to a corrupt company ([SAVE-R6]).
                menu.show_text("Open Company",
                    {std::format("Company file '{}' is damaged.", info.slot),
                     "Restore a backup or delete it."});
                break;
            }
            const std::string previous_slot = config_.save_name;
            config_.save_name = info.slot;
            if (load_game())
                return true; // -> team build (base camp)
            // load_game showed the error; restore the slot authority.
            config_.save_name = previous_slot;
            assert_company_slot_authority(); // [SAVE-R2]
            break;
        }
        case PickerMenuCommand::OpenCompanyBackups: {
            // §2.4 Backups sub-view for one company (corrupt rows keep the
            // door — restore-from-backup IS their recovery path).
            const int idx = prompt_company_index(
                menu, static_cast<int>(companies.size()), "Backups");
            if (idx < 0)
                break;
            if (show_company_backups(companies[static_cast<size_t>(idx)]))
                return true; // restored -> team build (base camp)
            break;
        }
        case PickerMenuCommand::DeleteCompany: {
            const int idx = prompt_company_index(
                menu, static_cast<int>(companies.size()), "Delete Company");
            if (idx < 0)
                break;
            const og::data::CompanyInfo& info =
                companies[static_cast<size_t>(idx)];
            if (info.slot == og::data::active_company_slot()) {
                // §3.7: delete_company refuses the active slot.
                menu.show_text("Delete Company",
                    {"Cannot delete the open company; switch first."});
                break;
            }
            const og::ui::CompanyRowText row =
                og::ui::format_company_row(info);
            // NO-first confirm (U3): the highlight starts on No.
            const std::vector<ListEntry> confirm = {
                ListEntry{"Backups are deleted too.", false},
                ListEntry{"No", true},
                ListEntry{"Yes", true},
            };
            const int verdict = menu.choose(
                std::format("Delete company '{}'?", row.name), confirm,
                "Enter select | Esc back", 1);
            if (verdict != 2)
                break;
            if (og::data::delete_company(info.slot)) {
                menu.show_text("Delete Company",
                    {std::format("Deleted '{}'.", info.slot)});
            } else {
                menu.show_text("Delete Company",
                    {std::format("Delete failed for '{}'.", info.slot)});
            }
            break;
        }
        default:
            break;
        }
    }
}

// §2.4 Backups sub-view, curses projection: the joined snapshot rows (shared
// row formatter — byte-identical with the text client) sit as non-selectable
// context lines above the Backups chrome (restore / delete / back); restore
// and delete prompt for a row number behind NO-first confirms. A successful
// restore repoints this client's slot ([SAVE-R2]) and reloads the rewound
// company, so the caller proceeds to team build. An empty snapshot list
// backs out (backups are level-win products, §3.7).
bool CursesPickerClient::show_company_backups(
    const og::data::CompanyInfo& company)
{
    Menu menu(term_, clock_);
    const og::ui::CompanyRowText company_row =
        og::ui::format_company_row(company);
    for (;;) {
        const std::vector<og::data::CompanyBackupInfo> backups =
            og::data::list_company_backups(company.slot);
        if (backups.empty()) {
            menu.show_text("Backups",
                {"No backups yet - backups snapshot on level wins."});
            return false;
        }

        const og::ui::TerminalMenuModel model =
            og::ui::build_terminal_menu_model(
                PickerMenuId::Backups,
                label_context(config_, options_, save_data_));
        std::vector<ListEntry> entries;
        entries.push_back(ListEntry{
            og::ui::format_backup_list_title(
                company_row.name, static_cast<int>(backups.size())), false});
        for (const og::data::CompanyBackupInfo& backup : backups) {
            entries.push_back(
                ListEntry{og::ui::format_backup_row_line(backup), false});
        }
        const int first_item = static_cast<int>(entries.size());
        for (const og::ui::TerminalMenuEntry& entry : model.entries)
            entries.push_back(ListEntry{entry.label, true});

        const int choice = menu.choose(model.title, entries,
            "Up/Down or j/k move | Enter select | digits jump | Esc/q back",
            first_item);
        if (choice < 0)
            return false;
        const PickerMenuItem* item =
            model.entries[static_cast<size_t>(choice - first_item)].item;

        switch (item->command) {
        case PickerMenuCommand::Back:
            return false;
        case PickerMenuCommand::RestoreBackup: {
            const int idx = prompt_backup_index(
                menu, static_cast<int>(backups.size()), "Restore Backup");
            if (idx < 0)
                break;
            const og::data::CompanyBackupInfo& backup =
                backups[static_cast<size_t>(idx)];
            if (!backup.header.valid) {
                // The §3.7 step-0 validation is the real guard; refuse up
                // front to spare a confirm that can only fail.
                menu.show_text("Restore Backup",
                    {std::format("Backup file '{}' is damaged.",
                                 backup.filename)});
                break;
            }
            // NO-first confirm (U3): the highlight starts on No.
            const std::vector<ListEntry> confirm = {
                ListEntry{"The current state is backed up first.", false},
                ListEntry{"No", true},
                ListEntry{"Yes", true},
            };
            const int verdict = menu.choose("Rewind to this backup?",
                confirm, "Enter select | Esc back", 1);
            if (verdict != 2)
                break;
            const std::string previous_slot = config_.save_name;
            config_.save_name = company.slot;
            assert_company_slot_authority(); // [SAVE-R2]
            const og::data::CompanyRestoreError error =
                og::data::restore_company_backup(save_data_, company.slot,
                                                 backup.seq);
            // RestampFailed included: the rewind itself finished (the next
            // autosave re-stamps).
            if (error == og::data::CompanyRestoreError::None
                || error == og::data::CompanyRestoreError::RestampFailed) {
                if (load_game())
                    return true; // -> team build (base camp)
            } else {
                menu.show_text("Restore Backup",
                    {std::format("Restore failed ({}).",
                        og::ui::company_restore_error_string(error))});
            }
            // Not rewound: restore the slot authority (the open-flow
            // discipline).
            config_.save_name = previous_slot;
            assert_company_slot_authority(); // [SAVE-R2]
            break;
        }
        case PickerMenuCommand::DeleteBackup: {
            const int idx = prompt_backup_index(
                menu, static_cast<int>(backups.size()), "Delete Backup");
            if (idx < 0)
                break;
            const og::data::CompanyBackupInfo& backup =
                backups[static_cast<size_t>(idx)];
            // NO-first confirm (U3): the highlight starts on No.
            const std::vector<ListEntry> confirm = {
                ListEntry{"No", true},
                ListEntry{"Yes", true},
            };
            const int verdict = menu.choose(
                std::format("Delete backup {} of '{}'?", backup.seq,
                            company.slot),
                confirm, "Enter select | Esc back", 0);
            if (verdict != 1)
                break;
            if (og::data::delete_company_backup(company.slot, backup.seq)) {
                menu.show_text("Delete Backup",
                    {std::format("Deleted backup {}.", backup.seq)});
            } else {
                menu.show_text("Delete Backup",
                    {std::format("Delete failed for backup {}.",
                                 backup.seq)});
            }
            break;
        }
        default:
            break;
        }
    }
}

// --- IPickerClient: networking -------------------------------------------

bool CursesPickerClient::configure_networking()
{
    Menu menu(term_, clock_);
    const std::vector<ListEntry> entries = {
        ListEntry{"Host Game", true},
        ListEntry{"Join Game", true},
        ListEntry{"Back", true},
    };
    switch (menu.choose("Networking", entries, "Enter select | Esc back", 0)) {
    case 0:
        return host_game();
    case 1:
        return join_game();
    default:
        return false;
    }
}

bool CursesPickerClient::host_game()
{
    Menu menu(term_, clock_);
    HostOptions opt;
    opt.difficulty = options_.difficulty;
    opt.port = options_.host_port;
    std::string error;
    std::unique_ptr<CursesLobby> lobby = make_host_lobby(save_data_, opt, &error);
    if (!lobby) {
        menu.show_text("Networking unavailable",
            {error.empty() ? "Hosting is not available in this build yet." : error});
        return false;
    }
    run_network_lobby(std::move(lobby));
    return true;
}

bool CursesPickerClient::join_game()
{
    Menu menu(term_, clock_);
    bool accepted = false;
    const std::string url = menu.prompt("Join Game", "Host URL: ", "", accepted);
    if (!accepted || url.empty())
        return false;

    JoinOptions opt;
    opt.url = url;
    std::string error;
    std::unique_ptr<CursesLobby> lobby = make_join_lobby(save_data_, opt, &error);
    if (!lobby) {
        menu.show_text("Networking unavailable",
            {error.empty() ? "Joining is not available in this build yet." : error});
        return false;
    }
    run_network_lobby(std::move(lobby));
    return true;
}

// Pump a host/join lobby until a game starts (or the user cancels), then run the
// negotiated networked level and report the result.
void CursesPickerClient::run_network_lobby(std::unique_ptr<CursesLobby> lobby)
{
    const GameRunResult result = run_curses_lobby(*lobby, term_, clock_);
    if (result.ended) {
        Menu menu(term_, clock_);
        // CTF-aware verdict (see run_game): the local player's team vs the
        // match winner decides, with a neutral fallback.
        menu.show_text("Mission complete", {mission_verdict_line(result)});
    }
}

// --- entry point ---------------------------------------------------------

void run_curses_picker(ITerminal& term, IClock& clock,
                       TextPickerConfig& config,
                       const CursesPickerOptions& options)
{
    CursesPickerClient client(term, clock, config, options);
    og::ui::run_picker(client);
}

} // namespace og::curses
