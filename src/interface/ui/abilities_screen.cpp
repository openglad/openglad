/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// ABILITIES: see abilities_screen.h.

#include <openglad/interface/ui/abilities_screen.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ability_icons.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/ui/ability_pick.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>

#include "picker_sdl_defs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace og::ui {

namespace {

namespace belt = og::sim::belt;

PickerState& pks() { return *og::runtime::current_session->picker_; }

// --- the grid (picker_sdl_defs.h constants) --------------------------------

constexpr int belt_ordinal(int belt_slot)
{
    return kAbilitiesBeltBase + 2 * (belt_slot % 3) + (belt_slot >= 3 ? 1 : 0);
}

constexpr int ordinal_belt_slot(int ordinal)
{
    const int r = ordinal - kAbilitiesBeltBase;
    return r / 2 + (r % 2 == 1 ? 3 : 0);
}

constexpr int belt_x(int belt_slot)
{
    return belt_slot >= 3 ? kAbilitiesRightColX : kAbilitiesLeftColX;
}

constexpr int belt_y(int belt_slot)
{
    return kAbilitiesBeltTopY + (belt_slot % 3) * kAbilitiesBeltPitch;
}

constexpr int bench_x(int row)
{
    return row % 2 == 1 ? kAbilitiesRightColX : kAbilitiesLeftColX;
}

constexpr int bench_y(int row)
{
    return kAbilitiesBenchTopY + (row / 2) * kAbilitiesBenchPitch;
}

static_assert(ordinal_belt_slot(belt_ordinal(0)) == 0 &&
              ordinal_belt_slot(belt_ordinal(4)) == 4 &&
              belt_ordinal(5) == kAbilitiesBenchBase - 1,
              "belt ordinals are row-major: 1 | S1, 2 | S2, 3 | S3");

// --- the open screen's state -------------------------------------------------

// The read-only notice (a joiner looking at a hero this machine does not
// own): <= kAbilityStatusChars glyphs.
constexpr const char* kReadOnlyNotice = "Read only: this hero is not yours.";

// What the face shows while a slot is selected: a leading '>' on both
// label surfaces (the row is cut by one glyph if it would leave its face).
constexpr char kSelectedMark = '>';

struct AbilitiesScreenState {
    // Base Camp: the save team_list slot. The PAUSED menu's copy: -1, and
    // `pause` names the seat whose body the screen shows.
    int team_slot = -1;
    const PauseAbilitiesHost* pause = nullptr;
    std::uint32_t entity_id = 0;  // the body the pause copy opened on
    bool body_has_record = true;  // false: a pause body with no guy
    bool session_ended = false;
    bool control_changed = false;
    bool editable = true;
    belt::Bytes working{};
    // A belt slot 0..5, a bench row kSlots + bench index, or -1.
    int selected = -1;
    PageModel bench_window{};
    // What each face's icon trace last said (belt faces, then bench rows),
    // so the trace names a change once instead of every frame.
    std::array<std::string, belt::kSlots + kAbilitiesBenchRows> icon_traced{};
};

AbilitiesScreenState* g_abilities = nullptr;

// What the screen needs to know about the hero it shows, from wherever the
// hero lives: a save record (Base Camp) or the body a seat drives (PAUSED).
struct AbilitiesHero {
    std::string name;
    int family = 0;
    int level = 1;
    short new_specials = 0;
    belt::Bytes stored{};  // the layout as it is now
};

// The body the pause copy opened on, if the seat still drives it. The one
// "same body" rule: the frame tick and the commit both ask it.
const walker* pause_body(const AbilitiesScreenState& state)
{
    const walker* const body = seat_driven_walker(state.pause->seat);
    return body != nullptr && body->entity_id() == state.entity_id ? body
                                                                   : nullptr;
}

// A body's hero (ability_hero_of_body, shared with the curses page) with
// New Specials as this world plays it, never the save's.
AbilitiesHero hero_of_body(const walker& body)
{
    AbilityBodyHero shown = ability_hero_of_body(body);
    return AbilitiesHero{
        .name = std::move(shown.name),
        .family = shown.family,
        .level = shown.level,
        .new_specials =
            og::runtime::current_session->myscreen_->world().new_specials,
        .stored = shown.stored};
}

// The hero the screen shows NOW: a lobby poll rebuilds team_list at new
// addresses every frame, so the Base Camp screen keys on the slot, never a
// pointer; the pause copy re-finds its body through the seat every time.
std::optional<AbilitiesHero> abilities_hero()
{
    if (g_abilities == nullptr)
        return std::nullopt;
    if (g_abilities->pause != nullptr) {
        const walker* const body = pause_body(*g_abilities);
        if (body == nullptr)
            return std::nullopt;
        return hero_of_body(*body);
    }
    if (g_abilities->team_slot < 0 || g_abilities->team_slot >= MAX_TEAM_SIZE)
        return std::nullopt;
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const guy* const record =
        save.team_list[static_cast<std::size_t>(g_abilities->team_slot)].get();
    if (record == nullptr)
        return std::nullopt;
    return AbilitiesHero{.name = record->name,
                         .family = record->family,
                         .level = record->level,
                         .new_specials = save.new_specials,
                         .stored = record->belt};
}

const FamilyDescriptor* abilities_family(const AbilitiesHero& hero)
{
    return get_family_descriptor(hero.family);
}

// The page the screen shows: the hero's name, family and level over the
// WORKING copy of the belt.
AbilityPage abilities_page(const AbilitiesHero& hero)
{
    return ability_page(hero.name, abilities_family(hero), hero.level,
                        hero.new_specials, g_abilities->working);
}

// The bench window: four rows when the bench fits, otherwise three and the
// MORE row on the fourth (only a class pack with more than four abilities
// off the belt reaches it).
void sync_bench_window(int bench_count)
{
    PageModel fresh = make_row_window(bench_count, kAbilitiesBenchRows);
    fresh.page = std::min(g_abilities->bench_window.page,
                          std::max(0, fresh.page_count() - 1));
    g_abilities->bench_window = fresh;
}

// Bench row `row` (0..3) -> index into AbilityPage::bench, or -1 for a row
// with nothing on it; kMoreRow for the pager row.
constexpr int kMoreRow = -2;

int bench_row_index(int bench_count, int row)
{
    const PageModel& window = g_abilities->bench_window;
    if (window.multi_page() && row == kAbilitiesBenchRows - 1)
        return kMoreRow;
    const int index = window.first_index() + row;
    if (row >= window.rows_per_page || index >= window.end_index() ||
        index >= bench_count)
        return -1;
    return index;
}

std::string with_selection_mark(std::string text)
{
    if (static_cast<int>(text.size()) >= kAbilityRowChars)
        text.resize(static_cast<std::size_t>(kAbilityRowChars - 1));
    return kSelectedMark + text;
}

// --- label and row-state bindings (one instance per row) ---------------------

template <int BeltSlot>
std::string belt_label(const MenuLabelContext& /*context*/)
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return ability_key_label(BeltSlot) + " --";
    const AbilityPage page = abilities_page(*hero);
    const std::string& text =
        page.belt[static_cast<std::size_t>(BeltSlot)].text;
    return g_abilities->selected == BeltSlot ? with_selection_mark(text)
                                             : text;
}

template <int Row>
std::string bench_label(const MenuLabelContext& /*context*/)
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return {};
    const AbilityPage page = abilities_page(*hero);
    sync_bench_window(static_cast<int>(page.bench.size()));
    const int index = bench_row_index(static_cast<int>(page.bench.size()), Row);
    if (index == kMoreRow)
        return campaign_picker_row_text(
            make_more_row(kMoreRowLabel, g_abilities->bench_window),
            static_cast<std::size_t>(kAbilityRowChars), false);
    if (index < 0)
        return {};
    const std::string& text = page.bench[static_cast<std::size_t>(index)].text;
    return g_abilities->selected == belt::kSlots + index
        ? with_selection_mark(text)
        : text;
}

template <int Row>
RowState bench_row_state(const MenuLabelContext& /*context*/)
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return RowState::Hidden;
    const AbilityPage page = abilities_page(*hero);
    sync_bench_window(static_cast<int>(page.bench.size()));
    return bench_row_index(static_cast<int>(page.bench.size()), Row) == -1
        ? RowState::Hidden
        : RowState::Visible;
}

RowState defaults_row_state(const MenuLabelContext& /*context*/)
{
    return g_abilities != nullptr && !g_abilities->editable
        ? RowState::Disabled
        : RowState::Visible;
}

// --- the rows ----------------------------------------------------------------

template <int BeltSlot>
constexpr MenuButtonSpec belt_row(const char* id)
{
    return {.id = id, .label = "",
            .x = belt_x(BeltSlot), .y = belt_y(BeltSlot),
            .w = kAbilitiesColW, .h = kAbilitiesBeltH,
            .action = ButtonAction::MenuSpecRow,
            .arg = belt_ordinal(BeltSlot),
            .label_binding = {.formatter = &belt_label<BeltSlot>},
            // abilities_draw_content draws the whole face (slot, icon,
            // label), so a click never flashes the bare centred label.
            .no_draw = true};
}

template <int Row>
constexpr MenuButtonSpec bench_row(const char* id)
{
    return {.id = id, .label = "",
            .x = bench_x(Row), .y = bench_y(Row),
            .w = kAbilitiesColW, .h = kAbilitiesBenchH,
            .action = ButtonAction::MenuSpecRow,
            .arg = kAbilitiesBenchBase + Row,
            .label_binding = {.formatter = &bench_label<Row>},
            .state_override = &bench_row_state<Row>,
            .no_draw = true,
            .hidden = true};
}

// The static nav is the all-visible variant (four bench rows): two column
// chains, top to bottom and round again through the footer, with
// left/right between the columns. abilities_rewire re-derives it every
// frame around the hidden bench rows.
constexpr MenuButtonSpec kAbilitiesRows[] = {
    {.id = "abilities_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = kAbilitiesLeftColX, .y = kAbilitiesFooterY,
     .w = kAbilitiesBackW, .h = kAbilitiesFooterH,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT},
    {.id = "abilities_defaults", .label = "DEFAULTS",
     .x = kAbilitiesRightColX, .y = kAbilitiesFooterY,
     .w = kAbilitiesColW, .h = kAbilitiesFooterH,
     .action = ButtonAction::MenuSpecRow, .arg = kAbilitiesDefaultsIndex,
     .state_override = &defaults_row_state},
    belt_row<0>("belt_0"),
    belt_row<3>("belt_3"),
    belt_row<1>("belt_1"),
    belt_row<4>("belt_4"),
    belt_row<2>("belt_2"),
    belt_row<5>("belt_5"),
    bench_row<0>("bench_0"),
    bench_row<1>("bench_1"),
    bench_row<2>("bench_2"),
    bench_row<3>("bench_3"),
};

static_assert(std::size(kAbilitiesRows) == kAbilitiesRowCount,
              "ABILITIES ordinals: BACK, DEFAULTS, six belt, four bench");

// One column's chain, top to bottom, then the footer button, linked round.
void link_column(button* buttons, const std::vector<int>& chain)
{
    const int n = static_cast<int>(chain.size());
    for (int i = 0; i < n; ++i) {
        MenuNav& nav = buttons[chain[static_cast<std::size_t>(i)]].nav;
        nav.up = chain[static_cast<std::size_t>((i + n - 1) % n)];
        nav.down = chain[static_cast<std::size_t>((i + 1) % n)];
    }
}

// The per-frame nav program: the two column chains over the VISIBLE rows,
// and left/right to the same row of the other column when it is visible.
void abilities_rewire(button* buttons, int count, int& highlighted_button)
{
    if (buttons == nullptr || count != kAbilitiesRowCount)
        return;
    std::vector<int> left;
    std::vector<int> right;
    for (int ordinal = kAbilitiesBeltBase; ordinal < kAbilitiesRowCount;
         ++ordinal) {
        if (buttons[ordinal].hidden)
            continue;
        ((ordinal - kAbilitiesBeltBase) % 2 == 0 ? left : right)
            .push_back(ordinal);
    }
    left.push_back(kAbilitiesBackIndex);
    right.push_back(kAbilitiesDefaultsIndex);
    link_column(buttons, left);
    link_column(buttons, right);
    for (int ordinal = 0; ordinal < kAbilitiesRowCount; ++ordinal) {
        // Pairs: (BACK, DEFAULTS), then (2, 3), (4, 5), ... row-major.
        const bool is_left = ordinal < kAbilitiesBeltBase
            ? ordinal == kAbilitiesBackIndex
            : (ordinal - kAbilitiesBeltBase) % 2 == 0;
        const int partner = is_left ? ordinal + 1 : ordinal - 1;
        MenuNav& nav = buttons[ordinal].nav;
        const int link = buttons[partner].hidden ? -1 : partner;
        nav.left = is_left ? -1 : link;
        nav.right = is_left ? link : -1;
    }
    (void)highlighted_button;  // the runner pulls a hidden highlight back
}

// --- drawing ------------------------------------------------------------------

// The ink a locked ability's name is written in: a grey from the face's own
// ramp, a few shades under the face, so the row reads "not yet" while the
// name stays legible.
constexpr unsigned char kLockedInk = static_cast<unsigned char>(BUTTON_FACING - 5);
// The selected slot's ring: red, and inside the face, so it never reads as
// (or meets) the yellow hover and keyboard rings drawn outside a face.
constexpr unsigned char kSelectedRing = RED;

// The live face of ordinal `ordinal` (what the runner laid out), or null.
const vbutton* live_face(int ordinal)
{
    const vbutton* const live = og::runtime::current_session
        ->allbuttons_[static_cast<std::size_t>(ordinal)];
    return live != nullptr && !live->hidden ? live : nullptr;
}

// The icons this screen draws: the session's set (screen::ability_icons),
// the one the HUD's hotbar draws from, so a picture either has read is not
// read again. Loaded on first use, read again when the sprite sources change.
AbilityIcons& abilities_icon_set()
{
    return og::runtime::current_session->myscreen_->ability_icons();
}

// The face itself, through vbutton::vdisplay (the one bevel every button
// has), carrying `label` centred as every button does; the belt and bench
// rows pass "" and letter themselves.
void draw_plain_face(const vbutton& live, const std::string& label)
{
    constexpr Sint32 kNoAction = 0;
    vbutton face(live.xloc, live.yloc, live.width, live.height, kNoAction,
                 kNoAction, label, KEYSTATE_UNKNOWN);
    face.vdisplay();
}

void draw_selected_ring(const vbutton& face)
{
    og::runtime::current_session->myscreen_->draw_box(
        face.xloc + 1, face.yloc + 1, face.xend - 2, face.yend - 2,
        kSelectedRing, 0, 1);
}

// The 18x18 slot (the HUD's: fill 18, black frame) and its picture: the
// icon as drawn, its grey copy when locked, nothing for an empty slot.
void draw_icon_slot(const vbutton& face, const AbilityIcon* icon, bool locked)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    const int x = face.xloc + kAbilitiesSlotX;
    const int y = face.yloc + kAbilitiesSlotY;
    scr->draw_box(x, y, x + kAbilitiesSlotSize - 1, y + kAbilitiesSlotSize - 1,
                  kAbilitiesSlotFill, 1, 1);
    scr->draw_box(x, y, x + kAbilitiesSlotSize - 1, y + kAbilitiesSlotSize - 1,
                  kAbilitiesSlotFrame, 0, 1);
    if (icon == nullptr)
        return;
    const PixieData& picture = locked ? icon->locked : icon->ready;
    scr->putdata(face.xloc + kAbilitiesIconX, face.yloc + kAbilitiesIconY,
                 kAbilityIconSize, kAbilityIconSize,
                 std::span<const unsigned char>(
                     picture.data.get(),
                     static_cast<std::size_t>(kAbilityIconSize) *
                         kAbilityIconSize));
}

// The face's label as drawn: the formatter's text without the selection
// mark (the red ring marks the selected face), in one line of at most
// `per_line` glyphs, or two when it is longer: beside the slot a line holds
// 20 glyphs, and a locked "2 EXPLODING BOULDER L4" is 22, so its level goes
// to the second line. The break is the last space that fits; a word longer
// than a line is cut.
std::pair<std::string, std::string> face_label_lines(std::string_view label,
                                                     std::size_t per_line)
{
    if (!label.empty() && label.front() == kSelectedMark)
        label.remove_prefix(1);
    if (label.size() <= per_line)
        return {std::string(label), {}};
    std::size_t split = label.rfind(' ', per_line);
    std::size_t rest = split + 1;
    if (split == std::string_view::npos || split == 0) {
        split = per_line;
        rest = per_line;
    }
    return {std::string(label.substr(0, split)),
            std::string(label.substr(rest, per_line))};
}

// The label beside the slot, left-aligned, in the normal ink or the locked
// one; one line centred on the face, or two centred as a pair.
void draw_face_label(const vbutton& face, bool locked)
{
    text& ink = og::runtime::current_session->myscreen_->text_normal;
    const int glyph_w = static_cast<int>(ink.letters->w) + 1;
    const int glyph_h = static_cast<int>(ink.letters->h);
    const std::size_t per_line = static_cast<std::size_t>(
        (kAbilitiesLabelEndX - kAbilitiesLabelX + 2) / glyph_w);
    const auto [first, second] = face_label_lines(face.label, per_line);
    const unsigned char color = locked ? kLockedInk : DARK_BLUE;
    const int x = face.xloc + kAbilitiesLabelX;
    if (second.empty()) {
        ink.write_xy(x, face.yloc + (face.height - glyph_h) / 2, first, color,
                     static_cast<short>(1));
        return;
    }
    const int top = face.yloc + (face.height - (2 * glyph_h + 1)) / 2;
    ink.write_xy(x, top, first, color, static_cast<short>(1));
    ink.write_xy(x, top + glyph_h + 1, second, color, static_cast<short>(1));
}

// Names the icon on a face once per change: what the family declares and
// what was drawn (the declared file, the generic tile, or none).
void trace_face_icon([[maybe_unused]] const char* face_kind,
                     [[maybe_unused]] int index, std::string& last,
                     const char* declared, const AbilityIcon* icon,
                     bool locked)
{
    AbilityIcons& icons = abilities_icon_set();
    const char* drawn = "none";
    if (icon != nullptr)
        drawn = icon == icons.icon_at(kMissingIconPath) ? kMissingIconPath
                                                        : declared;
    std::string now = std::string(declared != nullptr ? declared : "(none)") +
                      " drawn=" + drawn + (locked ? " locked=1" : " locked=0");
    if (now == last)
        return;
    last = std::move(now);
    TRACE("abilities_icon", "face=%s_%d icon=%s", face_kind, index,
          last.c_str());
}

// One belt or bench face: the face, the slot with its icon, the label
// beside it, the selection ring over them (the slot fills the face's inner
// rows, so a ring drawn first would vanish under it), then the hover ring
// draw_buttons would have drawn (these rows are no_draw: the runner draws
// none of it).
void draw_ability_face(const vbutton& face, const FamilyDescriptor* fd,
                       const AbilityRow& row, bool selected, const char* kind,
                       int index, std::string& traced)
{
    draw_plain_face(face, std::string{});
    const AbilityIcon* icon = nullptr;
    const char* declared = nullptr;
    if (row.entry.has_value() && fd != nullptr) {
        declared = declared_icon_path(*fd, *row.entry);
        icon = abilities_icon_set().icon(*fd, *row.entry);
    }
    draw_icon_slot(face, icon, row.locked);
    draw_face_label(face, row.locked);
    if (selected)
        draw_selected_ring(face);
    if (face.had_focus)
        draw_button_hover(*og::runtime::current_session->myscreen_, face.xloc,
                          face.yloc, face.width, face.height);
    if (row.entry.has_value())
        trace_face_icon(kind, index, traced, declared, icon, row.locked);
    else
        trace_face_icon(kind, index, traced, nullptr, nullptr, false);
}

void abilities_draw_content(void* /*screen_state*/)
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return;
    const AbilityPage page = abilities_page(*hero);
    const FamilyDescriptor* const fd = abilities_family(*hero);

    for (int b = 0; b < belt::kSlots; ++b) {
        const vbutton* const face = live_face(belt_ordinal(b));
        if (face == nullptr)
            continue;
        draw_ability_face(*face, fd, page.belt[static_cast<std::size_t>(b)],
                          g_abilities->selected == b, "belt", b,
                          g_abilities->icon_traced[static_cast<std::size_t>(b)]);
    }
    for (int r = 0; r < kAbilitiesBenchRows; ++r) {
        const int index =
            bench_row_index(static_cast<int>(page.bench.size()), r);
        const vbutton* const face = live_face(kAbilitiesBenchBase + r);
        std::string& traced =
            g_abilities->icon_traced[static_cast<std::size_t>(belt::kSlots + r)];
        if (face == nullptr || index < 0 || index == kMoreRow) {
            // No ability on this face now: forget what its trace last said,
            // so the ability that comes back to it is named again.
            traced.clear();
        }
        if (face == nullptr)
            continue;
        if (index == kMoreRow) {
            // The pager: a plain face with its label, as every button.
            draw_plain_face(*face, face->label);
            if (face->had_focus)
                draw_button_hover(*og::runtime::current_session->myscreen_,
                                  face->xloc, face->yloc, face->width,
                                  face->height);
            continue;
        }
        if (index < 0)
            continue;
        draw_ability_face(*face, fd, page.bench[static_cast<std::size_t>(index)],
                          g_abilities->selected == belt::kSlots + index,
                          "bench", r, traced);
    }

    // Text last.
    text& ink = og::runtime::current_session->myscreen_->text_normal;
    ink.write_xy_center(160, kAbilitiesTitleY, DARK_BLUE, "%s",
                        page.title.c_str());
    ink.write_xy_center(160, kAbilitiesSubtitleY, DARK_BLUE, "%s",
                        page.subtitle.c_str());
    ink.write_xy(kAbilitiesLeftColX, kAbilitiesBenchHeaderY, DARK_BLUE, "%s",
                 page.bench.empty() ? "BENCH: (none)" : "BENCH");
    ink.write_xy_center(160, kAbilitiesStatusY, DARK_BLUE, "%s",
                        abilities_screen_status_line().c_str());
}

// --- the edits -----------------------------------------------------------------

// Every edit runs on the working copy; BACK commits it. Which edit a pick
// means is the shared rule (ability_pick.h); the traces name what it did.
void trace_pick(const AbilityPick& pick, [[maybe_unused]] int target)
{
    switch (pick.edit) {
    case AbilityPickEdit::Cleared:
        TRACE("abilities", "clear slot=%d", target);
        break;
    case AbilityPickEdit::Swapped:
        TRACE("abilities", "swap slot=%d", target);
        break;
    case AbilityPickEdit::Placed:
        TRACE("abilities", "place slot=%d", target);
        break;
    case AbilityPickEdit::None:
        TRACE("abilities", "select %d", pick.selected);
        break;
    }
}

void abilities_click_belt(const AbilitiesHero& hero, int belt_slot)
{
    AbilitiesScreenState& state = *g_abilities;
    const AbilityPick pick = ability_pick_belt(
        state.working, abilities_family(hero), hero.new_specials,
        abilities_page(hero), state.selected, belt_slot);
    state.selected = pick.selected;
    trace_pick(pick, belt_slot);
}

void abilities_click_bench(const AbilitiesHero& hero, int row)
{
    AbilitiesScreenState& state = *g_abilities;
    const AbilityPage page = abilities_page(hero);
    const int index = bench_row_index(static_cast<int>(page.bench.size()), row);
    if (index == kMoreRow) {
        step_row_window(state.bench_window);
        if (state.selected >= belt::kSlots)
            state.selected = -1;
        TRACE("abilities", "bench page %s",
              state.bench_window.indicator().c_str());
        return;
    }
    const int target = state.selected;
    const AbilityPick pick =
        ability_pick_bench(state.working, abilities_family(hero),
                           hero.new_specials, page, state.selected,
                           index);
    state.selected = pick.selected;
    trace_pick(pick, target);
}

Sint32 abilities_on_spec_row(int row, void* /*screen_state*/)
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return MENU_OK;
    if (row == kAbilitiesDefaultsIndex) {
        if (g_abilities->editable) {
            ability_reset(g_abilities->working);
            g_abilities->selected = -1;
            TRACE("abilities", "defaults");
        }
        return MENU_OK;
    }
    if (row >= kAbilitiesBenchBase && row < kAbilitiesRowCount) {
        const int bench = row - kAbilitiesBenchBase;
        if (g_abilities->editable)
            abilities_click_bench(*hero, bench);
        else
            TRACE("abilities", "read_only bench=%d", bench);
        return MENU_OK;
    }
    if (row >= kAbilitiesBeltBase && row < kAbilitiesBenchBase) {
        const int belt_slot = ordinal_belt_slot(row);
        if (g_abilities->editable)
            abilities_click_belt(*hero, belt_slot);
        else
            TRACE("abilities", "read_only slot=%d", belt_slot);
    }
    return MENU_OK;
}

// A right-click on a spec row reaches the runner as do_call_right's default
// answer (MENU_OK) with no row stashed (button.cpp: spec rows have no
// right-click direction). Here it means "empty the slot under the pointer".
Sint32 abilities_consume_click(Sint32 retvalue, void* /*screen_state*/)
{
    if (retvalue != MENU_OK || pks().menu_spec_clicked_row >= 0 ||
        g_abilities == nullptr || !g_abilities->editable ||
        !abilities_hero())
        return retvalue;
    for (int b = 0; b < belt::kSlots; ++b) {
        vbutton* const live = og::runtime::current_session
            ->allbuttons_[static_cast<std::size_t>(belt_ordinal(b))];
        if (live == nullptr || !live->mouse_on())
            continue;
        ability_clear(g_abilities->working, b);
        g_abilities->selected = -1;
        TRACE("abilities", "clear slot=%d", b);
        break;
    }
    return retvalue;
}

// The hero left the slot (sold, or the lobby dropped it): nothing to show.
bool abilities_frame_tick(void* /*screen_state*/, int /*frame*/)
{
    return abilities_hero().has_value();
}

// The PAUSED copy's frame: one paused-session pump (the menu's own keep-alive
// rides it), then the seat must still drive the body the screen opened on. A
// session end or a new body closes the screen; BACK's commit never runs.
bool abilities_pause_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<AbilitiesScreenState*>(screen_state);
    if (state == nullptr || state->pause == nullptr)
        return false;
    if (state->pause->pump != nullptr &&
        !state->pause->pump(state->pause->pump_context)) {
        state->session_ended = true;
        TRACE("abilities", "pause closed: session ended");
        return false;
    }
    if (pause_body(*state) == nullptr) {
        state->control_changed = true;
        TRACE("abilities", "pause closed: control changed seat=%d",
              state->pause->seat);
        return false;
    }
    return true;
}

std::vector<button>& abilities_buttons()
{
    static std::vector<button> buttons;
    return buttons;
}

}  // namespace

const MenuScreenSpec& abilities_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "abilities",
        .rows = kAbilitiesRows,
        .row_count = static_cast<int>(std::size(kAbilitiesRows)),
        .buttons_accessor = &picker_abilities_buttons,
        .count_accessor = &picker_abilities_button_count,
        .nav = {.kind = NavProgramKind::Rewire, .rewire = &abilities_rewire},
        // Opened from TRAIN inside Base Camp: a joiner parked here follows
        // the host's GO like every team-build screen.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .default_highlight = belt_ordinal(0),
        // A right-click empties the slot under the pointer.
        .right_click_enabled = true,
        .polls_lobby = true,
        // The rows fill the canvas, so the screen is one panel: the options
        // family's chrome, shared (no backdrop shows around a full panel).
        .draw_background = &options_panel_draw_background,
        .draw_content = &abilities_draw_content,
        .frame_tick = &abilities_frame_tick,
        .consume_click = &abilities_consume_click,
        .on_spec_row = &abilities_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

std::string abilities_screen_status_line()
{
    const std::optional<AbilitiesHero> hero = abilities_hero();
    if (!hero)
        return {};
    if (!g_abilities->editable)
        return g_abilities->body_has_record ? kReadOnlyNotice
                                            : kNoSavedLayoutNotice;
    return ability_status_line(abilities_page(*hero), g_abilities->selected);
}

Sint32 run_abilities_menu(int team_slot)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    if (team_slot < 0 || team_slot >= MAX_TEAM_SIZE ||
        !save.team_list[static_cast<std::size_t>(team_slot)])
        return MENU_REDRAW;

    AbilitiesScreenState state{
        .team_slot = team_slot,
        .editable = picker_lobby_save_slot_editable(team_slot),
        .working = save.team_list[static_cast<std::size_t>(team_slot)]->belt,
    };
    AbilitiesScreenState* const previous = g_abilities;
    g_abilities = &state;
    TRACE("abilities", "open slot=%d editable=%d", team_slot,
          state.editable ? 1 : 0);
    const Sint32 result = run_menu_screen(abilities_menu_screen_spec(), &state);
    g_abilities = previous;
    if (result == MENU_EXIT)
        return MENU_EXIT;  // a host's GO: the match starts, nothing to commit

    guy* const hero = save.team_list[static_cast<std::size_t>(team_slot)].get();
    if (state.editable && hero != nullptr && hero->belt != state.working) {
        hero->belt = state.working;
        TRACE("abilities", "commit slot=%d", team_slot);
        picker_base_camp_after_roster_mutation();
    }
    return MENU_REDRAW;
}

walker* seat_driven_walker(int seat)
{
    og::runtime::SessionState* const session = og::runtime::current_session;
    if (session == nullptr || session->myscreen_ == nullptr)
        return nullptr;
    screen& view_screen = *session->myscreen_;
    if (seat < 0 || seat >= view_screen.numviews || seat >= MAX_VIEWS ||
        !view_screen.viewob[static_cast<std::size_t>(seat)])
        return nullptr;
    walker* const control =
        view_screen.viewob[static_cast<std::size_t>(seat)]->control;
    if (control == nullptr || control->dead() || control->stats() == nullptr)
        return nullptr;
    return control;
}

const MenuScreenSpec& abilities_pause_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "abilities_pause",
        .rows = kAbilitiesRows,
        .row_count = static_cast<int>(std::size(kAbilitiesRows)),
        .buttons_accessor = &picker_abilities_buttons,
        .count_accessor = &picker_abilities_button_count,
        .nav = {.kind = NavProgramKind::Rewire, .rewire = &abilities_rewire},
        .remote_start = RemoteStartScope::None,
        // Pause family: a mid-mission modal card, never faded (#237).
        .kind = MenuScreenKind::Overlay,
        .default_highlight = belt_ordinal(0),
        .right_click_enabled = true,
        .polls_lobby = false,
        .draw_background = &options_panel_draw_background,
        .draw_content = &abilities_draw_content,
        .frame_tick = &abilities_pause_frame_tick,
        .consume_click = &abilities_consume_click,
        .on_spec_row = &abilities_on_spec_row,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

PauseAbilitiesResult run_pause_abilities_screen(const PauseAbilitiesHost& host)
{
    PauseAbilitiesResult result;
    const walker* const body = seat_driven_walker(host.seat);
    if (body == nullptr)
        return result;  // the door is dimmed; nothing to show

    const AbilitiesHero opened = hero_of_body(*body);
    AbilitiesScreenState state{
        .pause = &host,
        .entity_id = body->entity_id(),
        .body_has_record = body->myguy != nullptr,
        .editable = body->myguy != nullptr,
        .working = opened.stored,
    };
    result.opened = true;
    AbilitiesScreenState* const previous = g_abilities;
    g_abilities = &state;
    TRACE("abilities", "pause open seat=%d entity=%u editable=%d", host.seat,
          static_cast<unsigned>(state.entity_id), state.editable ? 1 : 0);
    (void)run_menu_screen(abilities_pause_menu_screen_spec(), &state);
    g_abilities = previous;

    result.session_ended = state.session_ended;
    result.control_changed = state.control_changed;
    if (state.session_ended || state.control_changed || !state.editable ||
        state.working == opened.stored)
        return result;
    // BACK: the same body still under the seat, or nothing is sent (a
    // Switch or a possession between the last frame and the click).
    if (pause_body(state) == nullptr) {
        result.control_changed = true;
        TRACE("abilities", "pause closed: control changed seat=%d", host.seat);
        return result;
    }
    if (host.set_belt == nullptr ||
        !host.set_belt(host.seat, state.entity_id, state.working))
    {
        TRACE("abilities", "pause commit refused seat=%d", host.seat);
        return result;
    }
    // A SPEC key held through the pause latched its entry from the OLD
    // layout; drop it so that key cannot cast the old entry after RESUME.
    if (host.seat >= 0 && host.seat < 4)
        input_hardware_state().belt_latch[host.seat] = og::sim::belt::Latch{};
    result.committed = true;
    TRACE("abilities", "pause commit seat=%d entity=%u", host.seat,
          static_cast<unsigned>(state.entity_id));
    return result;
}

}  // namespace og::ui

button* picker_abilities_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::abilities_menu_screen_spec(),
                                     og::ui::abilities_buttons());
    return og::ui::abilities_buttons().data();
}

int picker_abilities_button_count()
{
    return static_cast<int>(og::ui::abilities_buttons().size());
}
