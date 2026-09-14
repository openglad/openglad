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

#include <atomic>
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
    // Published by the test body the instant picker_main returns, read by the
    // escape tail on the injector thread — hence atomic (peeking at
    // g_picker_mainmenu_calls would be a race on a plain int the menu thread
    // writes every frame).
    std::atomic<bool> main_left{false};
};

// --- stages ----------------------------------------------------------------
//
// One injector, so the ids are 101..: the number in the failure names which
// wait died. Every wait that the flow cannot continue past has one; the two
// cycle traces deliberately do NOT abort (they are the test's pins, recorded
// into the state and asserted by name in the body, and the rest of the flow
// still tells the reviewer whether the click opened the editor instead).
constexpr int kStageMainMenu = 101;
constexpr int kStageMainMenuSettle = 102;
constexpr int kStageCompanyName = 103;
constexpr int kStageTeamMenu = 104;
constexpr int kStageSeatCard = 105;
constexpr int kStageSeatCardSettle = 106;
constexpr int kStageChipClickOne = 107;
constexpr int kStageChipOneSettle = 108;
constexpr int kStageChipClickTwo = 109;
constexpr int kStageChipTwoSettle = 110;
constexpr int kStageCenterClick = 111;
constexpr int kStageEditorSettle = 112;
constexpr int kStageEditorReturn = 113;
constexpr int kStageEditorReturnSettle = 114;

// --- the escape tail -------------------------------------------------------
//
// A failed wait used to mean `return 0`: the injector died quietly while the
// main thread stayed blocked inside picker_main on a screen nobody would ever
// click out of, and og_test_basecamp burned to its 420 s group TIMEOUT with
// rc=124, naming no test (B1). The tail says which wait died, then clicks its
// way out of whatever screen is up, until the test body publishes main_left.
// No wall-clock bound on purpose (openglad-test-integrity "Tests that hang"
// §1; the precedent is tests/integration/test_pause_menu.cpp and G2's tail in
// tests/integration/test_company_list.cpp): a tail that gave up would strand
// the thread it exists to release.
//
// Which is why the exit id is a LIST, most specific first. The seat editor's
// door is "seat_settings_back", NOT "back" — proven, not theorised: the
// chip-zone planted break that proves this test's teeth opens the editor on
// the chip click, and the first draft of this tail, which knew only "back",
// spun there for the full 900 s.
constexpr const char* kExitIds[] = {"back", "seat_settings_back", "quit"};

int abort_flow(SeatChipFlowState* state, int stage)
{
    fprintf(stderr,
            "  [test] FLOW ABORT at stage %d — unwinding so picker_main can "
            "return\n",
            stage);
    int spins = 0;
    while (!state->main_left.load()) {
        for (const char* exit_id : kExitIds) {
            if (has_interactable(exit_id)) {
                interact(exit_id);
                break;
            }
        }
        (void)wait_for_menu_frames(1, 250);
        // A screen with no exit this tail knows would otherwise be a silent
        // spin. Every ~5 s, name what IS on screen, so the log says which
        // screen stranded the flow instead of going quiet until the group
        // timeout.
        if (++spins % 20 == 0) {
            std::string ids;
            for (const std::string& id : get_button_ids())
                ids += id + " ";
            fprintf(stderr,
                    "  [test] escape tail still spinning after %d frames; "
                    "live ids: %s\n",
                    spins, ids.c_str());
        }
    }
    return stage;
}

// The exit click has nobody left to notice it, so it is a condition too:
// re-sent one completed frame at a time until picker_main publishes
// main_left, with every re-send logged. A dropped last press used to mean the
// group timeout arriving through the happy path.
int finish_flow(SeatChipFlowState* state, const char* exit_id)
{
    interact(exit_id);
    int attempts = 1;
    while (!state->main_left.load()) {
        (void)wait_for_menu_frames(1, 250);
        if (state->main_left.load())
            break;
        if (!has_interactable(exit_id))
            continue;  // the screen took it; picker_main is unwinding
        ++attempts;
        fprintf(stderr,
                "  [test] exit click '%s' not consumed — re-sending "
                "(attempt %d)\n",
                exit_id, attempts);
        interact(exit_id);
    }
    state->finished = true;
    return 0;
}

int seat_chip_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<SeatChipFlowState*>(data);
    state->started = true;

    // Every settle below is a CONDITION, never a clock: wait_for_menu_frames
    // returns once run_menu_screen has COMPLETED that many frames, which is
    // what proves the incoming screen composed. The flat 750 ms sleeps this
    // replaced were waiting for a fadeblack animation that a TESTING build
    // never runs (FadeBetween is a single SDL_BlitSurface — the #ifdef TESTING
    // branch of src/platform/sdl/video_sdl.cpp).
    if (!wait_for_interactable("begin_new_game", 5000))
        return abort_flow(state, kStageMainMenu);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageMainMenuSettle);
    interact("begin_new_game");
    if (!accept_generated_company_name())
        return abort_flow(state, kStageCompanyName);
    if (!wait_for_team_menu())
        return abort_flow(state, kStageTeamMenu);
    state->saw_base_camp = true;
    state->founded_slot = og::data::active_company_slot();
    if (!wait_for_interactable("seat_card_0", 5000))
        return abort_flow(state, kStageSeatCard);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageSeatCardSettle);

    // Chip click 1: the fresh solo seat starts on team 1; classic campaigns
    // expose all four teams, so the first cycle lands on team 2.
    fprintf(stderr, "  [test] chip click 1 on seat_card_0\n");
    if (!click_seat_card_chip("seat_card_0"))
        return abort_flow(state, kStageChipClickOne);
    // The consumed-click handshake: the trace the product writes while
    // DISPATCHING the chip click, then one completed frame so the screen the
    // next click lands on has been rewired. Recorded, not aborted — an
    // unconsumed chip click is the defect this test exists to catch, and the
    // remaining clicks say whether the editor swallowed it instead.
    state->saw_first_cycle =
        wait_for_trace("basecamp", "seat_team player=1 team=2", 5000);
    if (!wait_for_menu_frames(1))
        return abort_flow(state, kStageChipOneSettle);
    state->editor_opened_on_chip = has_interactable("seat_settings_back");

    // Chip click 2 cycles onward (2 -> 3): in-place cycling is repeatable.
    fprintf(stderr, "  [test] chip click 2 on seat_card_0\n");
    if (!click_seat_card_chip("seat_card_0"))
        return abort_flow(state, kStageChipClickTwo);
    state->saw_second_cycle =
        wait_for_trace("basecamp", "seat_team player=1 team=3", 5000);
    if (!wait_for_menu_frames(1))
        return abort_flow(state, kStageChipTwoSettle);
    state->editor_opened_on_chip = state->editor_opened_on_chip ||
        has_interactable("seat_settings_back");

    // A center click (the P#/name region) still opens the seat editor.
    fprintf(stderr, "  [test] center click on seat_card_0\n");
    if (!interact("seat_card_0"))
        return abort_flow(state, kStageCenterClick);
    state->editor_opened_on_center =
        wait_for_interactable("seat_settings_back", 5000);
    if (state->editor_opened_on_center) {
        if (!wait_for_menu_frames(2))
            return abort_flow(state, kStageEditorSettle);
        interact("seat_settings_back");
        if (!wait_for_team_menu())
            return abort_flow(state, kStageEditorReturn);
        if (!wait_for_menu_frames(2))
            return abort_flow(state, kStageEditorReturnSettle);
    }

    return finish_flow(state, "back");
}

} // namespace

TEST(SeatChip, chip_click_cycles_team_and_card_click_still_opens_editor)
{
    trace_clear();
    // Outrank any stray company created earlier in this process, even when
    // the suite runs shuffled within the same second. This is SETUP, not
    // teardown: the founded company must outrank the [SAVE-R5](c) stray
    // slots. The clock pin, the founded company file and the live save's
    // stamp all go back to the process baseline at the next reset
    // ([SAVE-R9], tests/integration/integration_main.cpp).
    og::data::set_company_clock_for_tests(4102444800LL); // 2100-01-01 UTC

    SeatChipFlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(seat_chip_injector, "seat_chip", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    // Release the escape tail before joining: it spins until this is set.
    state.main_left.store(true);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_EQ(0, thread_result)
        << "the injector stalled at stage " << thread_result;
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
    // The founded company is what makes this test a [SAVE-R9] leaker: BEGIN
    // NEW GAME repoints the active company to its own derived slug, so the
    // file it leaves behind is not save0 and a teardown reaper had to know
    // its name. Pinned rather than dropped with the reaper, because it is
    // also the rule the flow's whole premise rests on.
    ASSERT_FALSE(state.founded_slot.empty())
        << "the injector must have read the active slot at Base Camp";
    ASSERT_NE("save0", state.founded_slot)
        << "BEGIN NEW GAME must repoint the active company to the slug it "
           "derived from the generated name, not leave it on the default "
           "save0";
}
