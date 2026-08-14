// #202 — Base Camp seat-card team chip. End-to-end flow through picker_main
// and the REAL click dispatch: a pointer click on the card's team-square
// region cycles the seat's team in place (mouse coordinates stamped by
// vbutton::leftclick, routed by base_camp_on_spec_row's chip zone), while a
// click on the card's P#/name region still opens the seat editor. The fast
// direct-drive pins (value sequence, wrap, denial, foreign/spectator gates,
// keyboard-FIRE stale-pointer rule) live in test_view_team.cpp; this flow
// proves the real dispatch path end to end.
//
// Lives in the og_test_basecamp group (design G10: heavyweight Layer-F flows
// never ride og_test_menu_ui).

#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <string>

// Forward declarations from picker.cpp.
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {

constexpr int kTeamMenuTimeoutMs = 20000;

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

bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("hire_troops") && has_interactable("networking"))
            return true;
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n",
            timeout_ms);
    return false;
}

bool wait_for_trace(const char* category, const char* substring,
                    int timeout_ms)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (trace_contains(category, substring))
            return true;
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for trace %s/%s (%d ms)\n",
            category, substring, timeout_ms);
    return false;
}

// The sanctioned coordinate injector, aimed at the card's team-square
// region instead of the button center: last-few-pixels of the face, well
// inside the chip zone (card_x+46 .. card right edge), mapped through the
// same UI-canvas-pinned transform interact() uses.
bool click_seat_card_chip(const std::string& id)
{
    og::runtime::ensure_thread_session();
    int win_x = -1, win_y = -1;
    bool found = false;
    {
        AllButtonsLock lock;
        for (int i = 0; i < MAX_BUTTONS; i++) {
            vbutton* const b =
                og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
            if (!b || b->id != id || b->hidden)
                continue;
            const int game_x = b->xloc + b->width - 5;
            const int game_y = b->yloc + b->height / 2;
            const auto [mapped_x, mapped_y] =
                ui_canvas_to_window(static_cast<float>(game_x),
                                    static_cast<float>(game_y));
            win_x = static_cast<int>(mapped_x);
            win_y = static_cast<int>(mapped_y);
            fprintf(stderr,
                    "  [interact] chip-clicking '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            found = true;
            break;
        }
    }
    if (found)
        inject_click(win_x, win_y, 100);
    else
        fprintf(stderr, "  [interact] WARNING: '%s' not found\n", id.c_str());
    return found;
}

struct SeatChipFlowState {
    bool started = false;
    bool finished = false;
    bool saw_base_camp = false;
    bool saw_first_cycle = false;
    bool saw_second_cycle = false;
    bool editor_opened_on_chip = false;
    bool editor_opened_on_center = false;
    std::string founded_slot;
};

int seat_chip_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<SeatChipFlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("begin_new_game", 5000))
        return 0;
    SDL_Delay(750);
    interact("begin_new_game");
    if (!accept_generated_company_name())
        return 0;
    if (!wait_for_team_menu())
        return 0;
    state->saw_base_camp = true;
    state->founded_slot = og::data::active_company_slot();
    SDL_Delay(750);
    if (!wait_for_interactable("seat_card_0", 5000))
        return 0;
    SDL_Delay(300);

    // Chip click 1: the fresh solo seat starts on team 1; classic campaigns
    // expose all four teams, so the first cycle lands on team 2.
    fprintf(stderr, "  [test] chip click 1 on seat_card_0\n");
    if (!click_seat_card_chip("seat_card_0"))
        return 0;
    state->saw_first_cycle =
        wait_for_trace("basecamp", "seat_team player=1 team=2", 5000);
    SDL_Delay(300);
    state->editor_opened_on_chip = has_interactable("seat_settings_back");

    // Chip click 2 cycles onward (2 -> 3): in-place cycling is repeatable.
    fprintf(stderr, "  [test] chip click 2 on seat_card_0\n");
    if (!click_seat_card_chip("seat_card_0"))
        return 0;
    state->saw_second_cycle =
        wait_for_trace("basecamp", "seat_team player=1 team=3", 5000);
    SDL_Delay(300);
    state->editor_opened_on_chip = state->editor_opened_on_chip ||
        has_interactable("seat_settings_back");

    // A center click (the P#/name region) still opens the seat editor.
    fprintf(stderr, "  [test] center click on seat_card_0\n");
    interact("seat_card_0");
    state->editor_opened_on_center =
        wait_for_interactable("seat_settings_back", 5000);
    if (state->editor_opened_on_center) {
        SDL_Delay(750);
        interact("seat_settings_back");
        if (!wait_for_team_menu())
            return 0;
        SDL_Delay(300);
    }

    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(SeatChip, chip_click_cycles_team_and_card_click_still_opens_editor)
{
    struct ClockReset {
        ~ClockReset() { og::data::set_company_clock_for_tests(std::nullopt); }
    } clock_reset;
    struct FoundedCompanyCleanup {
        std::string slot;
        ~FoundedCompanyCleanup()
        {
            if (slot.empty() || slot == "save0")
                return;
            (void)og::data::set_active_company_slot("save0");
            (void)og::data::delete_company(slot);
        }
    } company_cleanup;

    trace_clear();
    // Outrank any stray company created earlier in this process, even when
    // the suite runs shuffled within the same second.
    og::data::set_company_clock_for_tests(4102444800LL); // 2100-01-01 UTC

    SeatChipFlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(seat_chip_injector, "seat_chip", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    company_cleanup.slot = state.founded_slot;

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.saw_base_camp) << "flow must reach Base Camp";
    EXPECT_TRUE(state.saw_first_cycle)
        << "chip click must cycle the seat team in place (team=2 trace)";
    EXPECT_TRUE(state.saw_second_cycle)
        << "a second chip click must cycle onward (team=3 trace)";
    EXPECT_FALSE(state.editor_opened_on_chip)
        << "the chip click must not open the seat editor";
    EXPECT_TRUE(state.editor_opened_on_center)
        << "a card-center click must still open the seat editor";
    ASSERT_TRUE(state.finished) << "the complete flow should unwind";
}
