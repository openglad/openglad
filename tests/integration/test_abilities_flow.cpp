/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// ABILITIES (New Ability Select, docs/ability-select-design.md E.1) in real
// SDL: the screen's grid and nav, the TRAIN door (lit with the setting on,
// dimmed with it off), and the edits reaching the saved hero — place, swap,
// clear, DEFAULTS, BACK, then the belt bytes in team_list after ten menu
// frames of lobby polls and in the autosaved company file.
//
// Every flow drives the screen from an injector thread through the
// acknowledged ladders (tests/test_click_ladder.h) against named edges —
// a face label, a trace, a screen's id — and every give-up and every happy
// path leaves through the shared escape tail (tests/test_escape_tail.h)
// with this screen's door table bound. On tier 2 of
// scripts/check_injector_settles.sh from birth: no flat settle, ever.
//
// Captures (the TRAIN door lit, Archmage with MIND CONTROL on the bench, a
// level-1 mage, a level-1 barbarian, the TRAIN door dimmed) are frozen and
// checked for ink on every run, and written as PPMs when UXSHOTS_DIR names
// a directory.

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ability_icons.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/abilities_screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_ability_select_scope.h"
#include "test_camp_save_fixture.h"
#include "test_click_ladder.h"
#include "test_company_cleanup.h"
#include "test_escape_tail.h"
#include "test_fake_lobby_client.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "test_menu_highlight.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

namespace
{

namespace belt = og::sim::belt;

inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

const char* uxshots_dir()
{
    return std::getenv("UXSHOTS_DIR");
}

// The door table, in PRECEDENCE order: ABILITIES' own BACK, then TRAIN's
// (its stat buttons say it is up), then Base Camp's, then the picker's own
// way forward out of the main menu.
constexpr EscapeDoor kAbilitiesEscapeDoors[] = {
    {"abilities_back", "abilities_back"},
    {"inc_str", "back"},
    {"go", "back"},
    {"continue_game", "continue_game"},
};

// The company every flow opens: an Archmage (slot 0) and a level-1 Mage
// (slot 1), on gladiator, as the most recent company on disk so CONTINUE
// opens it.
void write_ability_company(short archmage_level = 12)
{
    write_save0_with_soldiers("gladiator", 1, 2, {}, "ABILITY HALL");
    SaveData& save = test_screen()->save_data;
    auto archmage = std::make_unique<guy>(FAMILY_ARCHMAGE);
    archmage->name = "MERLIN";
    archmage->level = archmage_level;
    archmage->deployed = true;
    auto mage = std::make_unique<guy>(FAMILY_MAGE);
    mage->name = "NIMUE";
    mage->level = 1;
    mage->deployed = true;
    save.team_list[0] = std::move(archmage);
    save.team_list[1] = std::move(mage);
    save.new_specials = 1;
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";
}

// The entry the family names `name` (a primary or an alternate).
belt::Entry entry_named(int family, const std::string& name)
{
    const FamilyDescriptor* const fd = get_family_descriptor(family);
    for (const belt::Entry& entry : belt::catalog(fd, 1)) {
        const char* const named =
            entry.alt ? og::sim::alternate_name(fd, entry.slot, 1)
                      : og::sim::special_name(fd, entry.slot, 1);
        if (named != nullptr && name == named)
            return entry;
    }
    ADD_FAILURE() << "no ability named " << name;
    return {};
}

std::string main_thread_status_line()
{
    std::string line = "<unread>";
    (void)run_on_main_thread(
        [&line] { line = og::ui::abilities_screen_status_line(); });
    return line;
}

// A click whose edge is one row's face reading `want`.
bool click_until_face(const std::string& id, const std::string& face_id,
                      const std::string& want, const char* landed = nullptr)
{
    return click_until_edge(
        id,
        [&face_id, &want](int ms) {
            return wait_for_interactable_label(face_id, want, ms);
        },
        landed, 3, 2500, "abilities");
}

// The same, pressed with the RIGHT button.
bool right_click_until_face(const std::string& id, const std::string& want,
                            const char* landed)
{
    return click_until_edge(
        [&id] { (void)interact_right(id); }, id,
        [&id, &want](int ms) {
            return wait_for_interactable_label(id, want, ms);
        },
        landed, 3, 2500, "abilities");
}

// Main thread: the stored belt of save team_list slot `slot`.
bool main_thread_belt(int slot, belt::Bytes& out)
{
    bool ok = false;
    const bool ran = run_on_main_thread([slot, &out, &ok] {
        const auto& member =
            test_screen()->save_data.team_list[static_cast<std::size_t>(slot)];
        if (member == nullptr)
            return;
        out = member->belt;
        ok = true;
    });
    return ran && ok;
}

// Palette indices in one live face's LABEL area (x + 22 .. x + 144, inside
// the bevel; the icon slot left of it is not counted, so TELEPORT's blue
// ring, index 74, cannot pass for label ink), read on the main thread at
// frame top, when the buffer still holds the frame the window last showed.
struct FaceInk {
    int dark_blue = 0;  // the normal label ink (DARK_BLUE's ramp, 72..76)
    int locked = 0;     // the locked ink's own shades (below the bevel ramp)
};

bool main_thread_face_ink(const std::string& id, FaceInk& ink)
{
    bool ok = false;
    const bool ran = run_on_main_thread([&id, &ink, &ok] {
        const vbutton* face = nullptr;
        for (vbutton* const live : og::runtime::current_session->allbuttons_) {
            if (live != nullptr && live->id == id && !live->hidden)
                face = live;
        }
        if (face == nullptr)
            return;
        for (int y = face->yloc + 1; y < face->yend - 1; ++y) {
            for (int x = face->xloc + kAbilitiesLabelX;
                 x <= face->xloc + kAbilitiesLabelEndX; ++x) {
                int index = 0;
                test_screen()->get_pixel(x, y, &index);
                if (index >= DARK_BLUE && index <= DARK_BLUE + 4)
                    ++ink.dark_blue;
                // BUTTON_FACING - 5 and the two shades past it: the locked
                // ink's ramp, short of the face/bevel shades 11..15.
                if (index >= BUTTON_FACING - 5 && index <= BUTTON_FACING - 3)
                    ++ink.locked;
            }
        }
        ok = true;
    });
    return ran && ok;
}

// get_pixel's index read-back returns the FIRST palette index matching the
// pixel's RGB, so an index whose RGB repeats an earlier entry (black 16 is
// black 0) reads back as that earlier one. Expectations are canonicalized
// the same way (test_glad_hud.cpp does the same).
int canonical_palette_index(int color)
{
    int r = 0, g = 0, b = 0;
    query_palette_reg(static_cast<unsigned char>(color), &r, &g, &b);
    for (int i = 0; i < 256; ++i) {
        int tr = 0, tg = 0, tb = 0;
        query_palette_reg(static_cast<unsigned char>(i), &tr, &tg, &tb);
        if (r == tr && g == tg && b == tb)
            return i;
    }
    return color;
}

// One face's icon slot as drawn: the 18x18 cell at (x + 2, y + 1), read on
// the main thread at frame top.
struct SlotPixels {
    bool read = false;
    std::array<int, kAbilitiesSlotSize * kAbilitiesSlotSize> index{};
    int at(int x, int y) const
    {
        return index[static_cast<std::size_t>(y * kAbilitiesSlotSize + x)];
    }
};

bool main_thread_slot(const std::string& id, SlotPixels& slot)
{
    const bool ran = run_on_main_thread([&id, &slot] {
        for (vbutton* const live : og::runtime::current_session->allbuttons_) {
            if (live == nullptr || live->id != id || live->hidden)
                continue;
            for (int y = 0; y < kAbilitiesSlotSize; ++y) {
                for (int x = 0; x < kAbilitiesSlotSize; ++x) {
                    int index = 0;
                    test_screen()->get_pixel(live->xloc + kAbilitiesSlotX + x,
                                             live->yloc + kAbilitiesSlotY + y,
                                             &index);
                    slot.index[static_cast<std::size_t>(
                        y * kAbilitiesSlotSize + x)] = index;
                }
            }
            slot.read = true;
        }
    });
    return ran && slot.read;
}

// The dark-blue ink `label` leaves when written alone in the face font: what
// a face's label area must hold, no more (an icon pixel in the window would
// add to it). Drawn on the main thread over a corner the next frame repaints.
bool main_thread_label_ink(const std::string& label, int& ink)
{
    return run_on_main_thread([&label, &ink] {
        screen* const scr = test_screen();
        scr->draw_box(0, 0, 159, 11, 0, 1, 1);
        scr->text_normal.write_xy(2, 2, label, DARK_BLUE, static_cast<short>(1));
        ink = 0;
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 160; ++x) {
                int index = 0;
                scr->get_pixel(x, y, &index);
                if (index >= DARK_BLUE && index <= DARK_BLUE + 4)
                    ++ink;
            }
        }
    });
}

// The label in one face: the rows inside the bevel that carry anything but
// the face grey in the label area, and the label ink (either ramp) that
// spilled right of it (over the bevel, into the gutter and the next
// column).
struct LabelRows {
    bool read = false;
    int rows_with_ink = 0;
    int spilled = 0;
};

bool main_thread_label_rows(const std::string& id, LabelRows& out)
{
    const auto is_ink = [](int index) {
        return (index >= DARK_BLUE && index <= DARK_BLUE + 4) ||
               (index >= BUTTON_FACING - 5 && index <= BUTTON_FACING - 3);
    };
    const int face_grey = canonical_palette_index(BUTTON_FACING);
    const bool ran = run_on_main_thread([&id, &out, &is_ink, face_grey] {
        for (vbutton* const live : og::runtime::current_session->allbuttons_) {
            if (live == nullptr || live->id != id || live->hidden)
                continue;
            for (int y = live->yloc + 1; y < live->yend - 1; ++y) {
                // A glyph's lower rows are shaded off the ink ramp, so a
                // row "has ink" when anything but the face grey is on it.
                bool row_ink = false;
                for (int x = live->xloc + kAbilitiesLabelX;
                     x <= live->xloc + kAbilitiesLabelEndX; ++x) {
                    int index = 0;
                    test_screen()->get_pixel(x, y, &index);
                    row_ink = row_ink || index != face_grey;
                }
                out.rows_with_ink += row_ink ? 1 : 0;
                // Past the label area: the right bevel, the gutter and the
                // next column's bevel, up to its slot.
                for (int x = live->xloc + kAbilitiesLabelEndX + 1;
                     x < live->xend + kAbilitiesColGutter + kAbilitiesSlotX;
                     ++x) {
                    int index = 0;
                    test_screen()->get_pixel(x, y, &index);
                    out.spilled += is_ink(index) ? 1 : 0;
                }
            }
            out.read = true;
        }
    });
    return ran && out.read;
}

// What every picker_main flow records on the injector thread and asserts on
// the main thread.
struct FlowState {
    std::atomic<bool> test_finished{false};
    bool finished = false;
    bool train_seen = false;
    bool door_enabled = false;
    bool screen_opened = false;
    std::array<std::string, belt::kSlots> opened_faces;
    std::string opened_bench_0;
    std::string opened_bench_1;
    bool bench_1_shown = true;
    bool selected_key3 = false;
    std::string selected_status;
    bool placed = false;
    std::string bench_after_place;
    std::string placed_status;
    bool cleared_by_right_click = false;
    bool defaults_restored = false;
    bool placed_again = false;
    bool cleared_s1 = false;
    bool back_to_train = false;
    bool committed = false;
    bool polled_belt_read = false;
    belt::Bytes belt_after_polls{};
    bool reopened = false;
    std::string reopened_key3;
    std::string reopened_s1;
    bool reclosed = false;
    // The setting-off flow.
    bool door_dimmed_seen = false;
    bool door_inert = false;
    bool still_on_train = false;
    int captures = 0;
};

// Main menu -> Base Camp -> roster row `row` -> TRAIN. 0 or the leg.
int enter_train(FlowState* state, const std::string& row,
                const std::function<int(int, const char*)>& escape)
{
    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable(row, 15000))
        return escape(2, "Base Camp never showed the roster row");
    (void)wait_for_menu_frames(2);
    state->train_seen = click_until_edge(row, [](int ms) {
        return wait_for_interactable("abilities", ms);
    });
    if (!state->train_seen)
        return escape(3, "the roster row never opened TRAIN");
    (void)wait_for_menu_frames(2);
    return 0;
}

// --- 1. The whole road: TRAIN door -> edits -> BACK -> the saved hero ------

int full_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<FlowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kAbilitiesEscapeDoors);
    };
    if (const int failed = enter_train(state, "roster_row_0", escape))
        return failed;

    state->door_enabled = has_enabled_interactable("abilities");
    capture_presented_frame("abilities_train_door_on", uxshots_dir());
    ++state->captures;
    state->screen_opened = click_until_edge("abilities", [](int ms) {
        return wait_for_interactable("belt_0", ms);
    });
    if (!state->screen_opened)
        return escape(4, "the ABILITIES door never opened the screen");
    (void)wait_for_menu_frames(2);
    for (int b = 0; b < belt::kSlots; ++b)
        state->opened_faces[static_cast<std::size_t>(b)] =
            interactable_label("belt_" + std::to_string(b));
    state->opened_bench_0 = interactable_label("bench_0");
    state->opened_bench_1 = interactable_label("bench_1");
    state->bench_1_shown = has_interactable("bench_1");
    capture_presented_frame("abilities_archmage", uxshots_dir());
    ++state->captures;

    // Pick key 3: its face gains the '>' and the status line names it.
    state->selected_key3 =
        click_until_face("belt_2", "belt_2", ">3 SUMMON IMAGE", "select 2");
    if (!state->selected_key3)
        return escape(5, "picking key 3 never marked it");
    state->selected_status = main_thread_status_line();
    capture_presented_frame("abilities_archmage_selected", uxshots_dir());
    ++state->captures;

    // Then the benched MIND CONTROL: it takes key 3, SUMMON IMAGE benches.
    state->placed = click_until_face("bench_0", "belt_2", "3 MIND CONTROL",
                                     "place slot=2");
    if (!state->placed)
        return escape(6, "the bench pick never reached key 3");
    state->bench_after_place = interactable_label("bench_0");
    state->placed_status = main_thread_status_line();
    capture_presented_frame("abilities_archmage_placed", uxshots_dir());
    ++state->captures;

    // A right-click empties a slot.
    state->cleared_by_right_click =
        right_click_until_face("belt_0", "1 --", "clear slot=0");
    if (!state->cleared_by_right_click)
        return escape(7, "the right-click never emptied key 1");

    // DEFAULTS: the class default, every slot.
    state->defaults_restored = click_until_edge(
        "abilities_defaults",
        [](int ms) {
            return wait_for_interactable_label("belt_0", "1 TELEPORT", ms) &&
                   wait_for_interactable_label("belt_2", "3 SUMMON IMAGE", ms);
        },
        "defaults", 3, 2500, "abilities");
    if (!state->defaults_restored)
        return escape(8, "DEFAULTS never put the default back");

    // The layout the flow commits: MIND CONTROL on key 3, SHIFTER+1 empty
    // (the selected slot picked again empties it).
    state->placed_again =
        click_until_face("belt_2", "belt_2", ">3 SUMMON IMAGE", "select 2") &&
        click_until_face("bench_0", "belt_2", "3 MIND CONTROL",
                         "place slot=2");
    if (!state->placed_again)
        return escape(9, "the second placement never landed");
    state->cleared_s1 =
        click_until_face("belt_3", "belt_3", ">S1 TELEPORT MARKER",
                         "select 3") &&
        click_until_face("belt_3", "belt_3", "S1 --", "clear slot=3");
    if (!state->cleared_s1)
        return escape(10, "picking S1 twice never emptied it");

    // BACK commits once and returns to TRAIN.
    state->back_to_train = click_until_edge("abilities_back", [](int ms) {
        return wait_for_interactable("inc_str", ms);
    });
    if (!state->back_to_train)
        return escape(11, "BACK never returned to TRAIN");
    state->committed = wait_for_trace("abilities", "commit slot=0", 5000);
    // Ten TRAIN frames, each one a picker_lobby_poll that rebuilds the
    // roster from the lobby: the belt must ride the rebuild.
    (void)wait_for_menu_frames(10);
    state->polled_belt_read = main_thread_belt(0, state->belt_after_polls);

    // The door again: the screen reads the hero's saved belt.
    state->reopened = click_until_edge("abilities", [](int ms) {
        return wait_for_interactable("belt_0", ms);
    });
    if (!state->reopened)
        return escape(12, "the door never reopened the screen");
    (void)wait_for_menu_frames(2);
    state->reopened_key3 = interactable_label("belt_2");
    state->reopened_s1 = interactable_label("belt_3");
    state->reclosed = click_until_edge("abilities_back", [](int ms) {
        return wait_for_interactable("inc_str", ms);
    });
    if (!state->reclosed)
        return escape(13, "the second BACK never returned to TRAIN");

    state->finished = true;
    return escape(0, "");
}

// --- 2. The setting off: the TRAIN door is dimmed and inert ----------------

int door_off_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<FlowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kAbilitiesEscapeDoors);
    };
    if (const int failed = enter_train(state, "roster_row_0", escape))
        return failed;

    state->door_dimmed_seen = has_interactable("abilities") &&
                              !has_enabled_interactable("abilities");
    capture_presented_frame("abilities_train_door_off", uxshots_dir());
    ++state->captures;
    (void)run_on_main_thread([] { reset_mouse_click_tracking(); });
    (void)interact("abilities");
    (void)wait_for_menu_frames(3);
    state->door_inert = !trace_contains("abilities", "open slot=");
    state->still_on_train =
        has_interactable("inc_str") && !has_interactable("belt_0");
    state->finished = true;
    return escape(0, "");
}

// --- 3. Direct runs: the screen on one hero, no picker around it ----------

struct DirectState {
    std::atomic<bool> test_finished{false};
    bool finished = false;
    bool opened = false;
    std::array<std::string, belt::kSlots> faces;
    std::string bench_0;
    std::string status_before;
    std::string status_after;
    bool defaults_enabled = true;
    bool refused_select = false;
    bool refused_right_click = false;
    FaceInk locked_ink;
    FaceInk open_ink;
    bool ink_read = false;
    // The icon slots: key 1's ready icon and SHIFTER+3's locked one.
    SlotPixels key1_slot;
    SlotPixels s3_slot;
    std::string key1_label;
    int key1_label_ink = -1;
    // The two-line label face (a long locked row) and its id.
    std::string long_face_id;
    LabelRows long_face;
    std::string landing_start;
    std::string landing_down;
    std::string landing_right;
    // The host-GO flow: arm the fake lobby's start once the edit has landed
    // and hold (press nothing) while the screen leaves by itself.
    FakeLobbyClient* lobby = nullptr;
    bool arm_start = false;
    int captures = 0;
};

// How long a held tail waits for the screen to leave by itself before it
// falls back to pressing BACK (a give-up path, never the happy one).
constexpr Uint64 kHeldTailCeilingMs = 15000;

constexpr EscapeDoor kDirectEscapeDoors[] = {
    {"abilities_back", "abilities_back"},
};

int read_only_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DirectState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kDirectEscapeDoors);
    };
    state->opened = wait_for_interactable("belt_0", 10000);
    if (!state->opened)
        return escape(1, "the screen never came up");
    (void)wait_for_menu_frames(2);
    state->status_before = main_thread_status_line();
    state->defaults_enabled = has_enabled_interactable("abilities_defaults");
    // A pick is answered by the refusal trace and changes no face.
    state->refused_select = click_and_acknowledge_trace(
        "belt_2", "abilities", "read_only slot=2", false);
    (void)wait_for_menu_frames(2);
    for (int b = 0; b < belt::kSlots; ++b)
        state->faces[static_cast<std::size_t>(b)] =
            interactable_label("belt_" + std::to_string(b));
    // A right-click on a slot changes nothing either.
    (void)run_on_main_thread([] { reset_mouse_click_tracking(); });
    (void)interact_right("belt_0");
    (void)wait_for_menu_frames(3);
    state->refused_right_click =
        interactable_label("belt_0") == "1 TELEPORT" &&
        !trace_contains("abilities", "clear slot=");
    state->status_after = main_thread_status_line();
    state->finished = true;
    return escape(0, "");
}

int mage_page_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DirectState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kDirectEscapeDoors);
    };
    state->opened = wait_for_interactable("belt_0", 10000);
    if (!state->opened)
        return escape(1, "the screen never came up");
    (void)wait_for_menu_frames(2);
    for (int b = 0; b < belt::kSlots; ++b)
        state->faces[static_cast<std::size_t>(b)] =
            interactable_label("belt_" + std::to_string(b));
    state->bench_0 = interactable_label("bench_0");
    state->ink_read = main_thread_face_ink("belt_2", state->locked_ink) &&
                      main_thread_face_ink("belt_0", state->open_ink);
    (void)main_thread_slot("belt_0", state->key1_slot);
    (void)main_thread_slot("belt_5", state->s3_slot);
    state->key1_label = interactable_label("belt_0");
    (void)main_thread_label_ink(state->key1_label, state->key1_label_ink);
    capture_presented_frame("abilities_mage", uxshots_dir());
    ++state->captures;

    // The keyboard: the screen lands on key 1; DOWN walks the left column
    // to key 2, RIGHT crosses to SHIFTER+2.
    state->landing_start = highlighted_id();
    if (press_menu_nav(KEY_DOWN))
        state->landing_down = highlighted_id();
    if (press_menu_nav(KEY_RIGHT))
        state->landing_right = highlighted_id();
    state->finished = true;
    return escape(0, "");
}

int long_label_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DirectState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kDirectEscapeDoors);
    };
    state->opened = wait_for_interactable("belt_0", 10000);
    if (!state->opened)
        return escape(1, "the screen never came up");
    (void)wait_for_menu_frames(2);
    (void)main_thread_label_rows(state->long_face_id, state->long_face);
    capture_presented_frame("abilities_barbarian", uxshots_dir());
    ++state->captures;
    state->finished = true;
    return escape(0, "");
}

int remote_start_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DirectState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kDirectEscapeDoors);
    };
    state->opened = wait_for_interactable("belt_0", 10000);
    if (!state->opened)
        return escape(1, "the screen never came up");
    (void)wait_for_menu_frames(2);
    // An edit: BACK commits it, a host's GO must not.
    if (!right_click_until_face("belt_0", "1 --", "clear slot=0"))
        return escape(2, "the right-click never emptied key 1");
    state->finished = true;
    if (!state->arm_start)
        return escape(0, "");
    // The host's GO arrives on the fake lobby's next poll; the screen leaves
    // by itself, so the tail holds (presses nothing) until it has, or until
    // the ceiling says the start never came.
    state->lobby->start_on_next_poll.store(true);
    const Uint64 give_up = SDL_GetTicks() + kHeldTailCeilingMs;
    return escape_to_the_main_thread(
        state->test_finished, 0, "", kDirectEscapeDoors,
        [give_up] { return SDL_GetTicks() < give_up; });
}

// Runs run_abilities_menu(slot) on the main thread under `injector`.
// Returns the screen's answer; the injector's result is in `thread_result`.
template <typename State>
Sint32 run_direct(int slot, int (*injector)(void*), State& state,
                  int& thread_result)
{
    SDL_Thread* thread = SDL_CreateThread(injector, "abilities_direct", &state);
    EXPECT_NE(nullptr, thread);
    if (thread == nullptr)
        return 0;
    const Sint32 result = og::ui::run_abilities_menu(slot);
    state.test_finished.store(true);
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    return result;
}

// The fake lobby the direct runs sit on (no roster rebuild, a READY log),
// and the active company they autosave into.
struct DirectRunScope {
    og::ui::IPickerLobbyClient* saved_client =
        og::ui::active_picker_lobby_client();
    og::data::ScopedActiveCompany company{"save0"};
    FakeLobbyClient lobby;

    DirectRunScope()
    {
        og::ui::install_active_picker_lobby_client(&lobby);
        trace_clear();
    }
    ~DirectRunScope()
    {
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
        pks().menu_spec_clicked_row = -1;
        og::ui::install_active_picker_lobby_client(saved_client);
        clear_allbuttons();
        og::runtime::current_session->localbuttons_ = nullptr;
    }
};

// The slot as the HUD draws it: a black frame all round, and inside it the
// picture over the 18 fill (the fill shows where the picture is clear).
void expect_slot_holds(const SlotPixels& slot, const PixieData& picture,
                       const char* what)
{
    ASSERT_TRUE(slot.read) << what;
    ASSERT_TRUE(picture.valid()) << what;
    int frame_misses = 0;
    int picture_misses = 0;
    for (int y = 0; y < kAbilitiesSlotSize; ++y) {
        for (int x = 0; x < kAbilitiesSlotSize; ++x) {
            const bool edge = x == 0 || y == 0 || x == kAbilitiesSlotSize - 1 ||
                              y == kAbilitiesSlotSize - 1;
            if (edge) {
                frame_misses += slot.at(x, y) ==
                                        canonical_palette_index(
                                            kAbilitiesSlotFrame)
                    ? 0
                    : 1;
                continue;
            }
            const int drawn = picture.data[static_cast<std::size_t>(
                (y - 1) * og::ui::kAbilityIconSize + (x - 1))];
            const int want =
                canonical_palette_index(drawn != 0 ? drawn : kAbilitiesSlotFill);
            picture_misses += slot.at(x, y) == want ? 0 : 1;
        }
    }
    EXPECT_EQ(0, frame_misses) << what << ": the slot's black frame";
    EXPECT_EQ(0, picture_misses)
        << what << ": the 16x16 window is the icon over the slot fill";
}

} // namespace

// ---------------------------------------------------------------------------
// Layout: the exact table, the grid relations, and the faces' budgets.

TEST(AbilitiesLayout, exact_table_and_grid_relations)
{
    button* const buttons = picker_abilities_buttons();
    const int count = picker_abilities_button_count();
    ASSERT_EQ(kAbilitiesRowCount, count);

    const auto row = [buttons](int ordinal) -> const button& {
        return buttons[ordinal];
    };
    EXPECT_EQ("abilities_back", row(kAbilitiesBackIndex).id);
    EXPECT_EQ("BACK", row(kAbilitiesBackIndex).label);
    EXPECT_EQ(KEYSTATE_ESCAPE, row(kAbilitiesBackIndex).hotkey);
    EXPECT_EQ(button_action_id(ButtonAction::ReturnMenu),
              row(kAbilitiesBackIndex).myfun);
    EXPECT_EQ("abilities_defaults", row(kAbilitiesDefaultsIndex).id);
    EXPECT_EQ("DEFAULTS", row(kAbilitiesDefaultsIndex).label);

    // Row-major ids: 1 | S1, 2 | S2, 3 | S3, then the bench pairs.
    const std::array<std::string, 6> belt_ids{
        "belt_0", "belt_3", "belt_1", "belt_4", "belt_2", "belt_5"};
    for (int i = 0; i < 6; ++i) {
        const button& face = row(kAbilitiesBeltBase + i);
        EXPECT_EQ(belt_ids[static_cast<std::size_t>(i)], face.id);
        EXPECT_EQ(button_action_id(ButtonAction::MenuSpecRow), face.myfun);
        EXPECT_EQ(kAbilitiesBeltBase + i, face.arg1);
        EXPECT_EQ(kAbilitiesColW, face.sizex);
        EXPECT_EQ(kAbilitiesBeltH, face.sizey);
        EXPECT_EQ(i % 2 == 0 ? kAbilitiesLeftColX : kAbilitiesRightColX,
                  face.x) << face.id;
        EXPECT_EQ(kAbilitiesBeltTopY + (i / 2) * kAbilitiesBeltPitch, face.y)
            << face.id;
        EXPECT_FALSE(face.hidden) << face.id;
        EXPECT_TRUE(face.no_draw)
            << face.id << ": the screen draws the face, slot and label";
    }
    for (int i = 0; i < kAbilitiesBenchRows; ++i) {
        const button& face = row(kAbilitiesBenchBase + i);
        EXPECT_EQ("bench_" + std::to_string(i), face.id);
        EXPECT_EQ(kAbilitiesBenchBase + i, face.arg1);
        EXPECT_EQ(kAbilitiesColW, face.sizex);
        EXPECT_EQ(kAbilitiesBenchH, face.sizey);
        EXPECT_EQ(i % 2 == 0 ? kAbilitiesLeftColX : kAbilitiesRightColX,
                  face.x) << face.id;
        EXPECT_EQ(kAbilitiesBenchTopY + (i / 2) * kAbilitiesBenchPitch, face.y)
            << face.id;
        EXPECT_TRUE(face.hidden) << face.id << " starts hidden";
        EXPECT_TRUE(face.no_draw) << face.id;
    }

    // The relations a crooked edit would break: one left edge, one right
    // edge, one face width per column, inside the panel bevel; the footer
    // shares the columns' edges.
    std::set<int> left_x;
    std::set<int> right_end;
    for (int i = kAbilitiesBeltBase; i < count; ++i) {
        const bool left = (i - kAbilitiesBeltBase) % 2 == 0;
        if (left)
            left_x.insert(row(i).x);
        else
            right_end.insert(row(i).x + row(i).sizex);
    }
    left_x.insert(row(kAbilitiesBackIndex).x);
    right_end.insert(row(kAbilitiesDefaultsIndex).x +
                     row(kAbilitiesDefaultsIndex).sizex);
    EXPECT_EQ(1u, left_x.size()) << "one left edge for every left-column row";
    EXPECT_EQ(1u, right_end.size()) << "one right edge for every right row";
    EXPECT_LE(*right_end.begin(), kAbilitiesPanelX2 - 4)
        << "the right column ends inside the panel bevel";
    EXPECT_EQ(kAbilitiesColGutter,
              row(kAbilitiesBeltBase + 1).x -
                  (row(kAbilitiesBeltBase).x + row(kAbilitiesBeltBase).sizex));
    EXPECT_EQ(kAbilitiesFooterY, row(kAbilitiesBackIndex).y);
    EXPECT_EQ(kAbilitiesFooterY, row(kAbilitiesDefaultsIndex).y);

    // The vertical rhythm: the last bench row ends above the status line,
    // the status line above the footer.
    const button& last_bench = row(kAbilitiesRowCount - 1);
    EXPECT_LT(last_bench.y + last_bench.sizey, kAbilitiesStatusY);
    EXPECT_LT(kAbilitiesStatusY + 7, kAbilitiesFooterY);
    const button& last_belt = row(kAbilitiesBenchBase - 1);
    EXPECT_LT(last_belt.y + last_belt.sizey, kAbilitiesBenchHeaderY);

    // The budgets: a 24-glyph row (the model's cut, '>' included) fits a
    // face; the status line fits the panel.
    EXPECT_LE(6 * og::ui::kAbilityRowChars, kAbilitiesColW);
    // Beside the icon slot, in the face font itself, a row of the model's
    // budget (its '>' never drawn) fits the label area on two lines, and
    // the two lines fit the face's height inside the bevel.
    const text& font = test_screen()->text_normal;
    const int advance = static_cast<int>(font.letters->w) + 1;
    const int per_line =
        (kAbilitiesLabelEndX - kAbilitiesLabelX + 2) / advance;
    EXPECT_LE(og::ui::kAbilityRowChars, 2 * per_line)
        << "a row of the model's budget needs a third line";
    EXPECT_LE(2 * static_cast<int>(font.letters->h) + 1, kAbilitiesBeltH - 2)
        << "two label lines do not fit inside the face's bevel";
    EXPECT_LE(6 * og::ui::kAbilityStatusChars,
              kAbilitiesPanelX2 - kAbilitiesPanelX1 - 8);
    EXPECT_LE(6 * og::ui::kAbilityTitleChars,
              kAbilitiesPanelX2 - kAbilitiesPanelX1 - 8);
    EXPECT_LE(6 * static_cast<int>(row(kAbilitiesDefaultsIndex).label.size()),
              row(kAbilitiesDefaultsIndex).sizex);
    EXPECT_LE(6 * static_cast<int>(row(kAbilitiesBackIndex).label.size()),
              row(kAbilitiesBackIndex).sizex);
}

// Every core family's page, every row, at levels 1 and 20 with New Specials
// on and off, keeps its last glyph when the selection mark goes in front:
// at most 23 glyphs, so ">" + row is at most the 24 a face holds.
TEST(AbilitiesLayout, every_core_row_keeps_its_name_when_selected)
{
    for (int family = 0; family < NUM_FAMILIES; ++family) {
        const FamilyDescriptor* const fd = get_family_descriptor(family);
        if (fd == nullptr)
            continue;
        for (const short ns : {short{0}, short{1}}) {
            for (const int level : {1, 20}) {
                const og::ui::AbilityPage page = og::ui::ability_page(
                    "MAXIMILIANUS", fd, level, ns, belt::default_bytes());
                std::vector<const og::ui::AbilityRow*> rows;
                for (const og::ui::AbilityRow& r : page.belt)
                    rows.push_back(&r);
                for (const og::ui::AbilityRow& r : page.bench)
                    rows.push_back(&r);
                for (const og::ui::AbilityRow* r : rows)
                    EXPECT_LE(r->text.size() + 1,
                              static_cast<std::size_t>(og::ui::kAbilityRowChars))
                        << "family " << family << ": '" << r->text << "'";
                EXPECT_EQ(0, page.cut) << "family " << family;
            }
        }
    }
}

// The nav program over every bench shape the screen can show: no bench,
// one to four rows, and the paged window (three rows and MORE on the
// fourth, with the last page short). Every visible row is reachable from
// BACK, and no link lands on a hidden row.
TEST(AbilitiesLayout, nav_reaches_every_visible_row_for_every_bench_shape)
{
    const og::ui::MenuScreenSpec& spec = og::ui::abilities_menu_screen_spec();
    ASSERT_EQ(og::ui::NavProgramKind::Rewire, spec.nav.kind);
    ASSERT_NE(nullptr, spec.nav.rewire);

    std::vector<std::vector<bool>> shapes;
    for (int visible = 0; visible <= kAbilitiesBenchRows; ++visible) {
        std::vector<bool> shown(kAbilitiesBenchRows, false);
        for (int i = 0; i < visible; ++i)
            shown[static_cast<std::size_t>(i)] = true;
        shapes.push_back(shown);
    }
    for (int last_page = 1; last_page < kAbilitiesBenchRows - 1; ++last_page) {
        std::vector<bool> shown(kAbilitiesBenchRows, false);
        for (int i = 0; i < last_page; ++i)
            shown[static_cast<std::size_t>(i)] = true;
        shown[kAbilitiesBenchRows - 1] = true;  // MORE
        shapes.push_back(shown);
    }

    for (const std::vector<bool>& shown : shapes) {
        std::vector<button> rows;
        og::ui::materialize_menu_buttons(spec, rows);
        for (int i = 0; i < kAbilitiesBenchRows; ++i)
            rows[static_cast<std::size_t>(kAbilitiesBenchBase + i)].hidden =
                !shown[static_cast<std::size_t>(i)];
        int highlighted = 0;
        spec.nav.rewire(rows.data(), static_cast<int>(rows.size()),
                        highlighted);

        std::string where;
        for (bool s : shown)
            where += s ? '1' : '0';
        std::set<int> seen{kAbilitiesBackIndex};
        std::deque<int> todo{kAbilitiesBackIndex};
        while (!todo.empty()) {
            const int at = todo.front();
            todo.pop_front();
            const MenuNav& nav = rows[static_cast<std::size_t>(at)].nav;
            for (const int link : {nav.up, nav.down, nav.left, nav.right}) {
                if (link < 0)
                    continue;
                ASSERT_LT(link, static_cast<int>(rows.size())) << where;
                EXPECT_FALSE(rows[static_cast<std::size_t>(link)].hidden)
                    << rows[static_cast<std::size_t>(at)].id
                    << " links to a hidden row, bench " << where;
                if (seen.insert(link).second)
                    todo.push_back(link);
            }
        }
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (!rows[static_cast<std::size_t>(i)].hidden) {
                EXPECT_TRUE(seen.count(i) != 0)
                    << rows[static_cast<std::size_t>(i)].id
                    << " is unreachable, bench " << where;
            }
        }
        // The column pairs: key N's left/right partner is SHIFTER + key N.
        EXPECT_EQ(kAbilitiesBeltBase + 1, rows[kAbilitiesBeltBase].nav.right);
        EXPECT_EQ(kAbilitiesBeltBase, rows[kAbilitiesBeltBase + 1].nav.left);
        EXPECT_EQ(kAbilitiesDefaultsIndex,
                  rows[kAbilitiesBackIndex].nav.right);
    }
}

// The TRAIN door: row 20 between ACCEPT and SELL, dimmed with the setting
// off and lit with it on.
TEST(AbilitiesLayout, train_door_row_follows_the_setting)
{
    button* const buttons = picker_trainmenu_buttons();
    ASSERT_GT(picker_trainmenu_button_count(), kTrainMenuAbilitiesIndex);
    const button& door = buttons[kTrainMenuAbilitiesIndex];
    EXPECT_EQ("abilities", door.id);
    EXPECT_EQ("ABILITIES", door.label);
    EXPECT_LE(6 * static_cast<int>(door.label.size()), door.sizex);
    const button& accept = buttons[14];
    const button& sell = buttons[kTrainMenuSellIndex];
    EXPECT_EQ(accept.y, door.y);
    EXPECT_EQ(sell.y, door.y);
    EXPECT_GT(door.x, accept.x + accept.sizex) << "clear of ACCEPT";
    EXPECT_LT(door.x + door.sizex, sell.x) << "clear of SELL";
    EXPECT_EQ(kTrainMenuAbilitiesIndex, accept.nav.right);
    EXPECT_EQ(kTrainMenuAbilitiesIndex, sell.nav.left);

    const og::ui::MenuButtonSpec& row =
        og::ui::train_menu_screen_spec().rows[kTrainMenuAbilitiesIndex];
    ASSERT_NE(nullptr, row.state_override);
    const og::ui::MenuLabelContext context{};
    {
        ScopedAbilitySelect off;
        EXPECT_EQ(og::ui::RowState::Disabled, row.state_override(context));
    }
    {
        ScopedAbilitySelect on(true);
        EXPECT_EQ(og::ui::RowState::Visible, row.state_override(context));
    }
}

// ---------------------------------------------------------------------------
// The flows.

TEST(AbilitiesFlow, train_door_opens_the_screen_and_the_edit_reaches_the_save)
{
    // A flow that failed before its own verification leaves its frames in
    // the ledger; each flow answers for its own captures only.
    (void)take_captured_frames();
    trace_clear();
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied());
    write_ability_company();

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(full_flow_injector, "abilities_full", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    ASSERT_TRUE(state.finished);
    EXPECT_TRUE(state.door_enabled) << "the door is lit with the setting on";

    // The Archmage's default (docs/ability-select-design.md A.5), MIND
    // CONTROL on the bench.
    EXPECT_EQ("1 TELEPORT", state.opened_faces[0]);
    EXPECT_EQ("2 HEARTBURST", state.opened_faces[1]);
    EXPECT_EQ("3 SUMMON IMAGE", state.opened_faces[2]);
    EXPECT_EQ("S1 TELEPORT MARKER", state.opened_faces[3]);
    EXPECT_EQ("S2 CHAIN LIGHTNING", state.opened_faces[4]);
    EXPECT_EQ("S3 SUMMON ELEMENTAL", state.opened_faces[5]);
    EXPECT_EQ("MIND CONTROL", state.opened_bench_0);
    EXPECT_EQ("", state.opened_bench_1) << "one ability on the bench";
    EXPECT_FALSE(state.bench_1_shown) << "an empty bench row is hidden";

    // Every face wears its ability's icon, and the icon follows the
    // ability: MIND CONTROL's moves to key 3 with it, an emptied slot has
    // none.
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_0 icon=icons/teleport.png "
                               "drawn=icons/teleport.png locked=0"));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_1 icon=icons/heartburst.png "
                               "drawn=icons/heartburst.png locked=0"));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_3 icon=icons/teleport_marker.png "));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_2 icon=icons/summon_image.png "));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=bench_0 icon=icons/mind_control.png "));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_2 icon=icons/mind_control.png "))
        << "the placed ability's icon moved onto key 3";
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_0 icon=(none) drawn=none"))
        << "an emptied slot shows no picture";

    EXPECT_EQ("SUMMON IMAGE is on key 3.", state.selected_status);
    EXPECT_EQ("SUMMON IMAGE", state.bench_after_place)
        << "the displaced ability goes to the bench";
    EXPECT_EQ("Click a slot, then an ability.", state.placed_status)
        << "a placement lets go of the selection";
    EXPECT_TRUE(state.cleared_by_right_click);
    EXPECT_TRUE(state.defaults_restored);
    EXPECT_TRUE(state.placed_again);
    EXPECT_TRUE(state.cleared_s1);
    EXPECT_TRUE(state.back_to_train);
    EXPECT_TRUE(state.committed) << "BACK commits the edited copy";
    EXPECT_EQ(1, count_trace_containing("abilities", "commit slot=0"))
        << "one commit for the visit; the unchanged second visit commits "
           "nothing";

    // What BACK wrote: DEFAULT bytes except MIND CONTROL on key 3 and an
    // EMPTY SHIFTER+1.
    belt::Bytes expected = belt::default_bytes();
    expected[2] = belt::encode(entry_named(FAMILY_ARCHMAGE, "MIND CONTROL"));
    expected[3] = belt::kEmptyByte;
    ASSERT_TRUE(state.polled_belt_read);
    EXPECT_EQ(expected, state.belt_after_polls)
        << "the belt survives ten menu frames of lobby roster rebuilds";
    EXPECT_EQ("3 MIND CONTROL", state.reopened_key3);
    EXPECT_EQ("S1 --", state.reopened_s1);

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load(og::data::active_company_slot()))
        << "the commit autosaved the active company";
    ASSERT_NE(nullptr, reloaded.team_list[0].get());
    EXPECT_EQ(expected, reloaded.team_list[0]->belt)
        << "the belt bytes are on disk";
    ASSERT_NE(nullptr, reloaded.team_list[1].get());
    EXPECT_EQ(belt::default_bytes(), reloaded.team_list[1]->belt)
        << "the other hero is untouched";
    verify_captured_frames("abilities_full", 4);
}

TEST(AbilitiesFlow, setting_off_dims_the_train_door)
{
    // A flow that failed before its own verification leaves its frames in
    // the ledger; each flow answers for its own captures only.
    (void)take_captured_frames();
    trace_clear();
    ScopedAbilitySelect off;
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied());
    write_ability_company();

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(door_off_injector, "abilities_off", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    ASSERT_TRUE(state.finished);
    EXPECT_TRUE(state.door_dimmed_seen)
        << "with the setting off the door is shown, dimmed and inert";
    EXPECT_TRUE(state.door_inert) << "a click on the dimmed door opens nothing";
    EXPECT_TRUE(state.still_on_train);
    EXPECT_TRUE(trace_contains("menu_engine", "disabled_row_click abilities"))
        << "the click landed on the dimmed row";

    // The frozen TRAIN frame: the dimmed door reads as unavailable, not as
    // a slightly darker live door. Its label is written in the pale
    // BUTTON_DIMMED_LABEL grey with no DARK_BLUE ink at all, on the GREY
    // face; the live SELL door beside it keeps its blue label on the
    // normal face.
    std::vector<Uint8> rgb;
    {
        const std::lock_guard<std::mutex> lock(g_captured_frame_mutex);
        if (!g_captured_frames.empty())
            rgb = g_captured_frames.back().rgb;
    }
    verify_captured_frames("abilities_off", 1);
    ASSERT_EQ(320u * 200u * 3u, rgb.size()) << "no TRAIN frame was frozen";
    struct Ink {
        int blue = 0;
        int pale = 0;
        std::array<Uint8, 3> face{};
    };
    const auto ink_of = [&rgb](const og::ui::MenuButtonSpec& row) {
        Ink ink;
        const auto at = [&rgb](int x, int y) {
            const std::size_t i = (static_cast<std::size_t>(y) * 320u +
                                   static_cast<std::size_t>(x)) * 3u;
            return std::array<Uint8, 3>{rgb[i], rgb[i + 1], rgb[i + 2]};
        };
        for (int y = row.y + 1; y < row.y + row.h - 1; ++y)
            for (int x = row.x + 1; x < row.x + row.w - 1; ++x) {
                const std::array<Uint8, 3> c = at(x, y);
                if (c[2] > c[0] + 40 && c[2] > c[1] + 40)
                    ++ink.blue;
                if (c[0] == 196 && c[1] == 196 && c[2] == 196)
                    ++ink.pale;
            }
        ink.face = at(row.x + 2, row.y + 2);
        return ink;
    };
    const auto& rows = og::ui::train_menu_screen_spec().rows;
    const Ink door = ink_of(rows[kTrainMenuAbilitiesIndex]);
    const Ink sell = ink_of(rows[kTrainMenuSellIndex]);
    EXPECT_EQ(0, door.blue) << "the dimmed door's label is still DARK_BLUE";
    EXPECT_GE(door.pale, 20) << "the dimmed door's label is not the pale grey";
    EXPECT_GE(sell.blue, 20) << "the live SELL door lost its blue label";
    EXPECT_EQ(0, sell.pale);
    EXPECT_NE(door.face, sell.face) << "the dimmed door kept the live face";
}

TEST(AbilitiesFlow, mage_page_greys_locked_rows_and_the_keyboard_walks_it)
{
    // A flow that failed before its own verification leaves its frames in
    // the ledger; each flow answers for its own captures only.
    (void)take_captured_frames();
    DirectRunScope scope;
    write_ability_company();

    DirectState state;
    int thread_result = 0;
    const Sint32 result =
        run_direct(1, mage_page_injector, state, thread_result);
    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    EXPECT_EQ(MENU_REDRAW, result);
    ASSERT_TRUE(state.finished);

    const guy& mage = *test_screen()->save_data.team_list[1];
    const og::ui::AbilityPage page = og::ui::ability_page(mage, 1);
    for (int b = 0; b < belt::kSlots; ++b)
        EXPECT_EQ(page.belt[static_cast<std::size_t>(b)].text,
                  state.faces[static_cast<std::size_t>(b)])
            << "slot " << b << " shows the model's row";
    ASSERT_TRUE(page.belt[2].locked) << "a level-1 mage's key 3 is locked: "
                                     << page.belt[2].text;
    ASSERT_FALSE(page.belt[0].locked);
    EXPECT_EQ("", state.bench_0) << "the mage benches nothing";

    ASSERT_TRUE(state.ink_read);
    EXPECT_EQ(0, state.locked_ink.dark_blue)
        << "a locked row is lettered in the grey ink, not the normal blue";
    EXPECT_GT(state.locked_ink.locked, 0);
    EXPECT_GT(state.open_ink.dark_blue, 0)
        << "an open row keeps the normal ink";
    // The label window holds the label and nothing else: key 1's TELEPORT
    // (its ring is 74, inside the dark-blue ramp) sits in the slot, outside
    // the window, and adds nothing to the count.
    EXPECT_EQ(page.belt[0].text, state.key1_label);
    EXPECT_EQ(state.key1_label_ink, state.open_ink.dark_blue)
        << "the label window counted pixels that are not the label's";

    // The icons: key 1 is TELEPORT as drawn; SHIFTER+3 is the locked
    // HEARTBURST (it opens past level 1), the grey copy, never melting into
    // the slot's fill.
    ASSERT_EQ("TELEPORT", page.belt[0].name);
    ASSERT_EQ("HEARTBURST", page.belt[5].name);
    ASSERT_TRUE(page.belt[5].locked) << page.belt[5].text;
    og::ui::AbilityIcons icons;
    const FamilyDescriptor& mage_fd = *get_family_descriptor(FAMILY_MAGE);
    const og::ui::AbilityIcon* const teleport =
        icons.icon(mage_fd, *page.belt[0].entry);
    const og::ui::AbilityIcon* const heartburst =
        icons.icon(mage_fd, *page.belt[5].entry);
    ASSERT_NE(nullptr, teleport);
    ASSERT_NE(nullptr, heartburst);
    expect_slot_holds(state.key1_slot, teleport->ready, "key 1 TELEPORT");
    expect_slot_holds(state.s3_slot, heartburst->locked,
                      "SHIFTER+3 HEARTBURST, locked");
    int off_palette = 0;
    int off_fill = 0;
    for (int y = 1; y < kAbilitiesSlotSize - 1; ++y) {
        for (int x = 1; x < kAbilitiesSlotSize - 1; ++x) {
            const int index = state.s3_slot.at(x, y);
            bool grey = index == canonical_palette_index(16) ||
                        index == canonical_palette_index(kAbilitiesSlotFill);
            for (int g = 21; g <= 31; ++g)
                grey = grey || index == canonical_palette_index(g);
            off_palette += grey ? 0 : 1;
            off_fill +=
                index != canonical_palette_index(kAbilitiesSlotFill) ? 1 : 0;
        }
    }
    EXPECT_EQ(0, off_palette)
        << "a locked icon is drawn in 16, the fill 18 and the greys 21..31";
    EXPECT_GE(off_fill, 30) << "the locked icon melted into the slot fill";
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_0 icon=icons/teleport.png "
                               "drawn=icons/teleport.png locked=0"));
    EXPECT_TRUE(trace_contains("abilities_icon",
                               "face=belt_5 icon=icons/heartburst.png "
                               "drawn=icons/heartburst.png locked=1"));

    EXPECT_EQ("belt_0", state.landing_start);
    EXPECT_EQ("belt_1", state.landing_down) << "DOWN: key 1 -> key 2";
    EXPECT_EQ("belt_4", state.landing_right)
        << "RIGHT: key 2 -> SHIFTER + key 2";
    EXPECT_FALSE(trace_contains("abilities", "commit slot="))
        << "a visit that changed nothing commits nothing";
    verify_captured_frames("abilities_mage", 1);
}

TEST(AbilitiesFlow, a_hero_this_machine_does_not_own_opens_read_only)
{
    DirectRunScope scope;
    write_ability_company();
    const auto saved_callback = og::ui::g_picker_save_slot_editable_callback;
    og::ui::g_picker_save_slot_editable_callback = [](int) { return false; };
    struct RestoreCallback {
        og::ui::PickerSaveSlotEditableCallback saved;
        ~RestoreCallback()
        {
            og::ui::g_picker_save_slot_editable_callback = saved;
        }
    } restore{saved_callback};
    const belt::Bytes before = test_screen()->save_data.team_list[0]->belt;

    DirectState state;
    int thread_result = 0;
    const Sint32 result =
        run_direct(0, read_only_injector, state, thread_result);
    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    EXPECT_EQ(MENU_REDRAW, result);
    ASSERT_TRUE(state.finished);
    EXPECT_EQ("Read only: this hero is not yours.", state.status_before);
    EXPECT_EQ(state.status_before, state.status_after);
    EXPECT_FALSE(state.defaults_enabled) << "DEFAULTS is dimmed";
    EXPECT_TRUE(state.refused_select);
    EXPECT_EQ("3 SUMMON IMAGE", state.faces[2]) << "no '>' on a refused pick";
    EXPECT_TRUE(state.refused_right_click);
    EXPECT_EQ(before, test_screen()->save_data.team_list[0]->belt);
    EXPECT_FALSE(trace_contains("abilities", "commit slot="));
    EXPECT_TRUE(scope.lobby.ready_requests.empty())
        << "nothing to push, so READY stands";
}

TEST(AbilitiesFlow, back_commits_once_and_clears_ready)
{
    DirectRunScope scope;
    write_ability_company();

    DirectState state;
    int thread_result = 0;
    // The remote-start injector only empties key 1 and leaves by the tail's
    // BACK when no start is armed.
    const Sint32 result =
        run_direct(0, remote_start_injector, state, thread_result);
    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(MENU_REDRAW, result);
    ASSERT_TRUE(state.finished);
    EXPECT_EQ(belt::kEmptyByte, test_screen()->save_data.team_list[0]->belt[0]);
    EXPECT_EQ(1, count_trace_containing("abilities", "commit slot=0"));
    ASSERT_FALSE(scope.lobby.ready_requests.empty())
        << "the commit runs the Base Camp mutation tail";
    EXPECT_FALSE(scope.lobby.ready_requests.back()) << "READY is cleared";
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_NE(nullptr, reloaded.team_list[0].get());
    EXPECT_EQ(belt::kEmptyByte, reloaded.team_list[0]->belt[0])
        << "and the autosave has it";
}

TEST(AbilitiesFlow, a_host_go_while_parked_starts_and_commits_nothing)
{
    DirectRunScope scope;
    write_ability_company();
    const belt::Bytes before = test_screen()->save_data.team_list[0]->belt;

    DirectState state;
    state.lobby = &scope.lobby;
    state.arm_start = true;
    int thread_result = 0;
    const Sint32 result =
        run_direct(0, remote_start_injector, state, thread_result);

    EXPECT_EQ(0, thread_result);
    ASSERT_TRUE(state.finished) << "the edit never landed";
    EXPECT_EQ(MENU_EXIT, result) << "the host's GO propagates";
    EXPECT_EQ(before, test_screen()->save_data.team_list[0]->belt)
        << "an edit in flight when the match starts is not committed";
    EXPECT_FALSE(trace_contains("abilities", "commit slot="));
    EXPECT_TRUE(scope.lobby.ready_requests.empty());
}

// The longest core row, a level-1 Barbarian's locked "2 EXPLODING BOULDER
// L4" (22 glyphs; 20 fit beside the slot), goes on two lines inside its
// face, and no ink leaves the label area.
TEST(AbilitiesFlow, the_longest_row_wraps_inside_its_face)
{
    // A flow that failed before its own verification leaves its frames in
    // the ledger; each flow answers for its own captures only.
    (void)take_captured_frames();
    DirectRunScope scope;
    write_ability_company();
    auto barbarian = std::make_unique<guy>(FAMILY_BARBARIAN);
    barbarian->name = "CONAN";
    barbarian->level = 1;
    barbarian->deployed = true;
    test_screen()->save_data.team_list[2] = std::move(barbarian);

    const og::ui::AbilityPage page =
        og::ui::ability_page(*test_screen()->save_data.team_list[2], 1);
    int long_slot = 0;
    for (int b = 1; b < belt::kSlots; ++b) {
        if (page.belt[static_cast<std::size_t>(b)].text.size() >
            page.belt[static_cast<std::size_t>(long_slot)].text.size())
            long_slot = b;
    }
    ASSERT_GE(page.belt[static_cast<std::size_t>(long_slot)].text.size(), 22u)
        << "the Barbarian's longest row: "
        << page.belt[static_cast<std::size_t>(long_slot)].text;

    DirectState state;
    state.long_face_id = "belt_" + std::to_string(long_slot);
    int thread_result = 0;
    const Sint32 result =
        run_direct(2, long_label_injector, state, thread_result);
    test_screen()->save_data.team_list[2].reset();
    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    EXPECT_EQ(MENU_REDRAW, result);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.long_face.read);
    const int glyph_h =
        static_cast<int>(test_screen()->text_normal.letters->h);
    EXPECT_GT(state.long_face.rows_with_ink, glyph_h)
        << page.belt[static_cast<std::size_t>(long_slot)].text
        << " is on two lines";
    EXPECT_EQ(0, state.long_face.spilled)
        << page.belt[static_cast<std::size_t>(long_slot)].text
        << " ran out of its face";
    verify_captured_frames("abilities_barbarian", 1);
}

// The Base Camp seat panel's ACTIONS column is the shared row list
// (binding_panel_action_rows): eight rows with the setting on (SPEC 1/2/3),
// the classic seven with it off. Drawn for real; the eighth line's band
// carries ink in the first case only.
TEST(AbilitiesLayout, seat_panel_actions_follow_the_setting)
{
    DirectRunScope scope;
    og::sim::LobbyPlayer p1;
    p1.player_index = 0;
    p1.seat_id = 1;
    scope.lobby.players = {p1};
    scope.lobby.local_seats = 1;
    og::ui::SeatSettingsScreenState state{
        .seat_id = p1.seat_id, .player_index = 0, .local_slot = 0};
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec();
    ASSERT_NE(nullptr, spec.draw_content);

    const auto ink_on_action_line = [&spec, &state](int line) {
        test_screen()->clearbuffer();
        spec.draw_content(&state);
        int ink = 0;
        const int y0 = kPlayerScreenPanelLineY + line * 8;
        for (int y = y0; y < y0 + 7; ++y) {
            for (int x = 104; x < kPlayerScreenPanelRightX; ++x) {
                int index = 0;
                test_screen()->get_pixel(x, y, &index);
                if (index >= DARK_BLUE && index <= DARK_BLUE + 4)
                    ++ink;
            }
        }
        return ink;
    };
    {
        ScopedAbilitySelect on(true);
        EXPECT_GT(ink_on_action_line(6), 0);
        EXPECT_GT(ink_on_action_line(7), 0)
            << "the eighth ACTIONS row (CHAR SW) is drawn with the setting on";
    }
    {
        ScopedAbilitySelect off;
        EXPECT_GT(ink_on_action_line(6), 0);
        EXPECT_EQ(0, ink_on_action_line(7))
            << "the classic panel has seven ACTIONS rows";
    }
    og::ui::install_seat_settings_state_for_screen(nullptr);
}
