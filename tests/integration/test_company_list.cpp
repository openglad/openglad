// §2.3 Company List (Load) — SDL flow tests (docs/company-basecamp-design.md,
// WP3). Drives the LOAD door end to end through picker_main with an injector
// thread: open (row 0 == what CONTINUE opens), the NO-first delete confirm
// with its retarget/empty-exit consequences, the corrupt-row and active-slot
// guards (never silently switch / switch-first), and the §2.4 BK door stub.
//
// Lives in the og_test_basecamp group (design G10: heavyweight Layer-F flows
// never ride og_test_menu_ui). The binary gets a fresh temp config dir per
// run; each test seeds its own slots and the harness reaps every non-baseline
// company file and backup between tests ([SAVE-R9] in
// tests/integration/integration_main.cpp), so the flows stay order-independent
// under --gtest_shuffle without a teardown reaper of their own.
//
// Two rules hold this file together, because every flow here clicks rows by
// POSITION and blocks the main thread inside picker_main while it does:
//  - expect_company_rows(...) states the exact list each flow is about to
//    drive, at the top of the test, so a re-targeted click is a named failure
//    here instead of a puzzle inside the flow;
//  - abort_flow(...) + run_company_list_flow(...) mean a wait that dies ends
//    the test with its stage id (ASSERT_EQ(0, rc)) instead of stranding
//    picker_main and burning the group to its 420 s timeout (B1).

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Forward declarations from picker.cpp / picker_dialogs.cpp.
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();
Sint32 change_respawn_mode();

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

// Writes a loadable company file with a pinned last-played timestamp (the
// list sorts most-recent-first on it). The writer serializes the struct
// field; only company_autosave stamps it in production.
bool seed_company(const std::string& slot, const std::string& name,
                  std::int64_t last_played)
{
    SaveData sd;
    sd.reset();
    sd.save_name = name;
    sd.current_campaign = "gladiator";
    sd.last_played_unix_s = last_played;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

std::filesystem::path company_path(const std::string& slot)
{
    return std::filesystem::path(get_user_path()) / "save" / (slot + ".gtl");
}

// GTL keeps the retired player-count byte at offset 132 so old readers retain
// their fixed layout. New readers must consume it for compatibility without
// letting one company's historical local-player choice replace this session's
// live picker mode.
bool patch_legacy_player_count(const std::string& slot, std::uint8_t count)
{
    constexpr std::streamoff kLegacyPlayerCountOffset = 132;
    std::fstream file(company_path(slot),
                      std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
        return false;
    file.seekp(kLegacyPlayerCountOffset);
    const char byte = static_cast<char>(count);
    file.write(&byte, 1);
    return static_cast<bool>(file);
}

// Loadable company with one named, deployed soldier on team 0 — the roster
// shape the cross-company lobby re-seed flow asserts on (the roster is the
// field the polled lobby cache used to clobber).
bool seed_company_with_soldier(const std::string& slot, const std::string& name,
                               const std::string& soldier,
                               std::int64_t last_played)
{
    SaveData sd;
    sd.reset();
    sd.save_name = name;
    sd.current_campaign = "gladiator";
    sd.current_levels[sd.current_campaign] = 1;
    sd.scen_num = 1;
    sd.numplayers = 1;
    sd.my_team = 0;
    sd.last_played_unix_s = last_played;
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = soldier;
    member->teamnum = 0;
    member->deployed = true;
    sd.team_list[0] = std::move(member);
    sd.team_size = 1;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

// Header-valid, body-torn fixture (the WP2 recipe): real writer bytes
// truncated to the 164-byte header with listsize patched to 2 at offset 130.
// read_company_header says valid; SaveData::load hits EOF in the roster.
bool seed_torn_company(const std::string& slot, const std::string& name,
                       std::int64_t last_played)
{
    if (!seed_company(slot, name, last_played))
        return false;
    const std::filesystem::path path = company_path(slot);
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes(164);
    if (!in.read(bytes.data(), 164))
        return false;
    in.close();
    bytes[130] = 2;  // listsize (host-endian short, low byte)
    bytes[131] = 0;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), 164);
    return static_cast<bool>(out);
}

// Bad-magic garbage: read_company_header marks it corrupt (valid == false).
bool seed_corrupt_company(const std::string& slot)
{
    std::ofstream out(company_path(slot), std::ios::binary | std::ios::trunc);
    out << "not a save file";
    return static_cast<bool>(out);
}

struct FlowState {
    bool started = false;
    bool finished = false;
    bool saw_team_menu = false;
    bool saw_load_hidden_after_empty = false;
    bool saw_continue_hidden_after_empty = false;
    // Set by run_company_list_flow() the instant picker_main returns, read by
    // the escape tail on the injector thread — hence atomic (the old habit of
    // peeking at g_picker_mainmenu_calls, a plain int the menu thread writes
    // every frame, is a data race).
    std::atomic<bool> main_left{false};
    // The stage the flow died at, mirrored out of abort_flow for the log; the
    // authoritative copy is the injector's thread return code.
    int stage = 0;
    // #237 derivation pins, counted around the real doors this flow already
    // drives. -1 means the injector never reached the read, which fails the
    // exact assertions in the test body.
    int fades_added_by_load_door = -1;
    int fades_added_by_backups_door = -1;
    int fades_added_by_load_return = -1;
    int fades_added_by_open_row = -1;
};

// Under TESTING every fadeblack lands in FadeBetween's test-mode branch,
// which traces exactly one "video" line per fade — so counting those lines
// counts fades. (#237: the rule is derived in run_menu_screen, never
// declared per screen, so only a real door driven through the real screens
// can pin which side of it a screen is on.)
int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer) {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
            ++fades;
    }
    return fades;
}

// --- the escape tail -------------------------------------------------------
//
// Every wait below can fail, and a failed wait used to mean `return 0`: the
// injector thread died quietly, the main thread stayed blocked inside
// picker_main on a screen nobody would ever click out of, and the binary
// burned to the 420 s group TIMEOUT with rc=124 — the most expensive red CI
// can produce, and one that names no test. (B1: og_test_basecamp
// --gtest_shuffle seed 7 hung exactly this way, in the backups view, inside
// CompanyList.restore_rewinds_and_opens_base_camp.)
//
// The rule now: a failed wait returns abort_flow(state, kStage...), which
// says which wait died and then clicks its way out — BACK when the live
// screen publishes one, else QUIT — until run_company_list_flow's main_left
// flag says picker_main has returned. The loop carries NO wall-clock bound on
// purpose (openglad-test-integrity "Tests that hang" §1; the precedent is the
// tail at tests/integration/test_pause_menu.cpp:3790): a tail that gave up
// would leave the main thread blocked forever, which is the hang it exists to
// prevent. Every flow test asserts ASSERT_EQ(0, rc), so a stranded flow is a
// NAMED failure carrying the stage that stalled instead of a group timeout.
//
// Stage ids are <injector ordinal>*100 + <wait ordinal>: unique across the
// file, so the number in the message says which wait of which flow died.
// Every wait in every injector has one — settles included — because a wait
// that fails without a stage is a flow that clicks on into a stalled pump.
constexpr int kStageOpenRowLoadDoor = 101;
constexpr int kStageOpenRowLoadDoorSettle = 102;
constexpr int kStageOpenRowRowZero = 103;
constexpr int kStageOpenRowRowZeroSettle = 104;
constexpr int kStageOpenRowTeamMenu = 105;
constexpr int kStageOpenRowTeamMenuSettle = 106;
constexpr int kStageOpenOtherLoadDoor = 201;
constexpr int kStageOpenOtherLoadDoorSettle = 202;
constexpr int kStageOpenOtherRowZero = 203;
constexpr int kStageOpenOtherRowZeroSettle = 204;
constexpr int kStageOpenOtherTeamMenu = 205;
constexpr int kStageOpenOtherTeamMenuSettle = 206;
constexpr int kStageOpenOtherRosterRow = 207;
constexpr int kStageOpenOtherDeployTrace = 208;
constexpr int kStageOpenOtherDeploySettle = 209;
constexpr int kStageDeleteLoadDoor = 301;
constexpr int kStageDeleteLoadDoorSettle = 302;
constexpr int kStageDeleteRowOne = 303;
constexpr int kStageDeleteRowOneSettle = 304;
constexpr int kStageDeleteConfirmNo = 305;
constexpr int kStageDeleteConfirmNoSettle = 306;
constexpr int kStageDeleteReapSettle = 307;
constexpr int kStageGuardLoadDoor = 401;
constexpr int kStageGuardLoadDoorSettle = 402;
constexpr int kStageGuardRowTwo = 403;
constexpr int kStageGuardRowTwoSettle = 404;
constexpr int kStageGuardTornPopup = 405;
constexpr int kStageGuardTornSettle = 406;
constexpr int kStageGuardCorruptPopup = 407;
constexpr int kStageGuardCorruptSettle = 408;
constexpr int kStageGuardActivePopup = 409;
constexpr int kStageGuardActiveSettle = 410;
constexpr int kStagePageLoadDoor = 501;
constexpr int kStagePageLoadDoorSettle = 502;
constexpr int kStagePageNext = 503;
constexpr int kStagePageNextSettle = 504;
constexpr int kStagePageFlipped = 505;
constexpr int kStagePageFlippedSettle = 506;
constexpr int kStagePageTeamMenu = 507;
constexpr int kStagePageTeamMenuSettle = 508;
constexpr int kStageBackupsLoadDoor = 601;
constexpr int kStageBackupsLoadDoorSettle = 602;
constexpr int kStageBackupsBkDoor = 603;
constexpr int kStageBackupsBkDoorSettle = 604;
constexpr int kStageBackupsEmptyView = 605;
constexpr int kStageBackupsEmptyViewSettle = 606;
constexpr int kStageBackupsViewBack = 607;
constexpr int kStageBackupsListReturn = 608;
constexpr int kStageBackupsListReturnSettle = 609;
constexpr int kStageBackupsMainMenu = 610;
constexpr int kStageBackupsMainMenuSettle = 611;
constexpr int kStageRestoreLoadDoor = 701;
constexpr int kStageRestoreLoadDoorSettle = 702;
constexpr int kStageRestoreBkDoor = 703;
constexpr int kStageRestoreBkDoorSettle = 704;
constexpr int kStageRestoreBackupRowMissing = 705;
constexpr int kStageRestoreBackupRowSettle = 706;
constexpr int kStageRestoreConfirmNo = 707;
constexpr int kStageRestoreConfirmNoSettle = 708;
constexpr int kStageRestoreTeamMenu = 709;
constexpr int kStageRestoreTeamMenuSettle = 710;
constexpr int kStageCorruptBkLoadDoor = 801;
constexpr int kStageCorruptBkLoadDoorSettle = 802;
constexpr int kStageCorruptBkDoor = 803;
constexpr int kStageCorruptBkDoorSettle = 804;
constexpr int kStageCorruptBkRow = 805;
constexpr int kStageCorruptBkRowSettle = 806;
constexpr int kStageCorruptBkPopup = 807;
constexpr int kStageCorruptBkPopupSettle = 808;
constexpr int kStageCorruptBkViewBack = 809;
constexpr int kStageCorruptBkListReturn = 810;
constexpr int kStageCorruptBkListReturnSettle = 811;
constexpr int kStageRecoverLoadDoor = 901;
constexpr int kStageRecoverLoadDoorSettle = 902;
constexpr int kStageRecoverBkDoor = 903;
constexpr int kStageRecoverBkDoorSettle = 904;
constexpr int kStageRecoverBackupRow = 905;
constexpr int kStageRecoverBackupRowSettle = 906;
constexpr int kStageRecoverTeamMenu = 907;
constexpr int kStageRecoverTeamMenuSettle = 908;
constexpr int kStageContinueTornDoor = 1001;
constexpr int kStageContinueTornDoorSettle = 1002;
constexpr int kStageContinueTornGoodRow = 1003;
constexpr int kStageContinueTornGoodRowSettle = 1004;
constexpr int kStageContinueTornTeamMenu = 1005;
constexpr int kStageContinueTornTeamMenuSettle = 1006;
constexpr int kStageContinueCorruptDoor = 1101;
constexpr int kStageContinueCorruptDoorSettle = 1102;
constexpr int kStageContinueCorruptRow = 1103;
constexpr int kStageContinueCorruptRowSettle = 1104;
constexpr int kStageContinueCorruptMainMenu = 1105;
constexpr int kStageContinueCorruptMainMenuSettle = 1106;

int abort_flow(FlowState* state, int stage)
{
    state->stage = stage;
    fprintf(stderr,
            "  [test] FLOW ABORT at stage %d — unwinding so picker_main can "
            "return\n",
            stage);
    while (!state->main_left.load()) {
        if (has_interactable("back"))
            interact("back");
        else if (has_interactable("quit"))
            interact("quit");
        (void)wait_for_menu_frames(1, 250);
    }
    state->finished = true;
    return stage;
}

// --- the exit click ---------------------------------------------------------
//
// A flow's LAST click has nobody left to notice it. The injector returns the
// instant it is sent, so a press the engine dropped — one that landed on the
// frame a screen was rewiring itself, which the flat settles used to make
// unlikely by sheer idleness rather than by any rule — leaves picker_main
// blocked on a screen nobody will ever click again. That is the rc=124 group
// timeout of B1 arriving through the one door abort_flow does not cover: the
// happy path. (Seen once, non-deterministically, in a full og_test_basecamp
// run of the converted file: "clicking back from base camp" was the last line
// in the log for 870 s.)
//
// So the exit click is a CONDITION too: re-sent, one completed frame per
// attempt, until the main thread publishes main_left. Bounded by that flag
// and never by a clock — a tail that gave up would strand the very thread it
// exists to release — and every re-send is logged, so a dropped press leaves
// evidence instead of a mystery.
int finish_flow(FlowState* state, const char* exit_id)
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

// --- settles and handshakes -------------------------------------------------
//
// Every wait below is a CONDITION, never a clock. The flows used to settle
// each screen with a flat 750 ms sleep ("menu-entry settle") and each
// click-to-click gap with a 400 ms one, on the authority of a fade that does
// not exist in a TESTING build (FadeBetween is a single SDL_BlitSurface
// — the #ifdef TESTING branch of src/platform/sdl/video_sdl.cpp). Those 48
// sleeps proved nothing: not that the incoming screen had composed, not that
// the click before them had been consumed. Two shapes replace them:
//
//  - the screen settle: wait_for_interactable(id) says the button table
//    carries the id, wait_for_menu_frames(2) says run_menu_screen has
//    COMPLETED that many frames with it — the incoming screen really
//    composed, which is what the sleep was pretending to guarantee;
//
//  - the consumed-click handshake: after a click whose effect the button
//    table cannot show (a confirm, a popup, a page flip, a deploy toggle),
//    wait_for_trace(category, text) on the line the product writes while
//    DISPATCHING that click, then completed frames so the next click lands on
//    a rewired screen. A press the engine dropped leaves the trace absent and
//    fails the wait by name, where the sleep used to hand the next click to
//    whatever screen happened to be up.
//
// Both settles wait for TWO frames, not one. The dispatch trace is written
// while the press is being handled, which is before the matching mouse-UP has
// necessarily been drained; the first completed frame can be the one that was
// already mid-flight when that UP arrived. Two completions put a whole frame
// between the previous click's release and the next press, so no frame ever
// sees an UP and a DOWN of two different clicks in one input drain.
//
// Both shapes route a failure through abort_flow with their own stage id, so
// no wait in this file can pass — or die — silently.

// --- open flow -------------------------------------------------------------

int open_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageOpenRowLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenRowLoadDoorSettle);
    fprintf(stderr, "  [test] clicking LOAD\n");
    interact("load_company");

    // The Company List fades in (#237: LOAD is a main-menu door).
    if (!wait_for_interactable("company_row_0", 5000))
        return abort_flow(state, kStageOpenRowRowZero);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenRowRowZeroSettle);
    fprintf(stderr, "  [test] opening company row 0\n");
    // #237 symmetry leg: opening a company leaves the list for Base Camp
    // — a main-menu-boundary crossing, so it fades out and in like every
    // other door off the main menu.
    const int fades_before_open = count_fade_between_traces();
    interact("company_row_0");

    if (!wait_for_team_menu())
        return abort_flow(state, kStageOpenRowTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenRowTeamMenuSettle);
    state->fades_added_by_open_row =
        count_fade_between_traces() - fades_before_open;
    fprintf(stderr, "  [test] clicking back from team menu\n");
    return finish_flow(state, "back");
}

// --- cross-company lobby re-seed flow (WP7 must-fix) -----------------------

int open_other_company_and_toggle_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageOpenOtherLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenOtherLoadDoorSettle);
    interact("load_company");

    if (!wait_for_interactable("company_row_0", 5000))
        return abort_flow(state, kStageOpenOtherRowZero);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenOtherRowZeroSettle);
    fprintf(stderr, "  [test] opening row 0 (the NON-boot company)\n");
    interact("company_row_0");

    if (!wait_for_team_menu())
        return abort_flow(state, kStageOpenOtherTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenOtherTeamMenuSettle);
    // One §3.8 roster mutation: bench roster row 0. The autosave this
    // triggers must write the OPENED company's roster into the OPENED
    // company's file.
    fprintf(stderr, "  [test] toggling deploy on roster row 0\n");
    if (!interact("roster_dep_0"))
        return abort_flow(state, kStageOpenOtherRosterRow);
    // The toggle's own dispatch trace is the consumption proof: the opened
    // company's single soldier is deployed, so row 0 benches it
    // (src/interface/ui/menu_screen_specs.cpp, base_camp deploy row).
    if (!wait_for_trace("basecamp", "deploy slot=0 off", 5000))
        return abort_flow(state, kStageOpenOtherDeployTrace);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageOpenOtherDeploySettle);
    fprintf(stderr, "  [test] clicking back from base camp\n");
    return finish_flow(state, "back");
}

// --- delete flow -----------------------------------------------------------

int delete_rows_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageDeleteLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageDeleteLoadDoorSettle);
    interact("load_company");

    if (!wait_for_interactable("company_row_1", 5000))
        return abort_flow(state, kStageDeleteRowOne);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageDeleteRowOneSettle);
    // First X click: the queued NO leaves the company alone.
    fprintf(stderr, "  [test] deleting row 0 (confirm NO)\n");
    interact("company_del_0");
    // The confirm prompt is trace-only under TESTING and fires inside the row
    // dispatch, so this line IS "the first X was consumed"; without it the
    // second X could land before the first had been dispatched and the queued
    // NO would answer nothing.
    if (!wait_for_trace("confirm", "DELETE COMPANY?", 5000))
        return abort_flow(state, kStageDeleteConfirmNo);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageDeleteConfirmNoSettle);
    // Second X click: the queued YES deletes it (+ its backups).
    fprintf(stderr, "  [test] deleting row 0 (confirm YES)\n");
    interact("company_del_0");

    // Poll the filesystem for the deletion, then leave. A deletion that
    // never lands is the test body's ASSERT_FALSE(user_file_exists) to
    // report: BACK still works, so this leg never strands the main thread.
    int elapsed = 0;
    while (elapsed < 5000 && user_file_exists("save/wp3delb.gtl")) {
        SDL_Delay(50);
        elapsed += 50;
    }
    // One completed frame after the reap: the post-delete re-scan rewires the
    // row buttons, and BACK below is clicked by position.
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageDeleteReapSettle);
    fprintf(stderr, "  [test] clicking back from the company list\n");
    return finish_flow(state, "back");
}

// --- guard flow (torn body, corrupt header, active-slot delete) ------------

int guard_rows_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageGuardLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageGuardLoadDoorSettle);
    interact("load_company");

    // Rows (ts desc; corrupt sorts last with ts 0): 0 = torn, 1 = active
    // good company, 2 = corrupt.
    if (!wait_for_interactable("company_row_2", 5000))
        return abort_flow(state, kStageGuardRowTwo);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageGuardRowTwoSettle);
    fprintf(stderr, "  [test] opening the torn-body row\n");
    interact("company_row_0");  // popup (trace-only), stays listed
    // Each guard click is proven by ITS OWN refusal popup before the next one
    // is sent: three clicks into the same screen, three distinct traces.
    if (!wait_for_trace("popup", "LOAD COMPANY: read_failed", 5000))
        return abort_flow(state, kStageGuardTornPopup);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageGuardTornSettle);
    fprintf(stderr, "  [test] opening the corrupt row\n");
    interact("company_row_2");  // popup COMPANY FILE DAMAGED
    if (!wait_for_trace("popup", "COMPANY FILE DAMAGED", 5000))
        return abort_flow(state, kStageGuardCorruptPopup);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageGuardCorruptSettle);
    fprintf(stderr, "  [test] deleting the active company's row\n");
    interact("company_del_1");  // popup SWITCH FIRST, no confirm
    if (!wait_for_trace("popup", "THIS COMPANY IS OPEN - SWITCH FIRST", 5000))
        return abort_flow(state, kStageGuardActivePopup);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageGuardActiveSettle);
    return finish_flow(state, "back");
}

// --- pagination flow --------------------------------------------------------

int pagination_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStagePageLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStagePageLoadDoorSettle);
    interact("load_company");

    // 11 companies span two eight-row pages: the pagers must be live.
    if (!wait_for_interactable("company_page_next", 5000))
        return abort_flow(state, kStagePageNext);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStagePageNextSettle);
    fprintf(stderr, "  [test] flipping to page 2\n");
    interact("company_page_next");
    // The page indicator trace is the consumed-click proof AND the retarget
    // proof: company_row_0 below means the 9th company only on page 2.
    if (!wait_for_trace("company_list", "page 2/2", 5000))
        return abort_flow(state, kStagePageFlipped);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStagePageFlippedSettle);
    // Page 2 starts with the 9th company in recency order.
    fprintf(stderr, "  [test] opening the page-2 row\n");
    interact("company_row_0");
    if (!wait_for_team_menu())
        return abort_flow(state, kStagePageTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStagePageTeamMenuSettle);
    return finish_flow(state, "back");
}

// --- backups door + delete-last flow ---------------------------------------

// The §2.4 Backups sub-view replaces the Company List's buttons while it is
// open; the EMPTY view shows only BACK (rows and pagers hidden), so entry is
// detected by the list's own buttons going away.
bool wait_for_backups_view(int timeout_ms = 5000)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("back") && !has_interactable("company_del_0")
            && !has_interactable("load_company"))
            return true;
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [interact] TIMEOUT entering the backups view (%d ms)\n",
            timeout_ms);
    return false;
}

int backups_and_empty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageBackupsLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageBackupsLoadDoorSettle);
    // #237: LOAD is a main-menu door — the Company List crosses the boundary
    // and fades, and the main menu fades again behind it.
    const int fades_before_load = count_fade_between_traces();
    interact("load_company");

    if (!wait_for_interactable("company_bak_0", 5000))
        return abort_flow(state, kStageBackupsBkDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageBackupsBkDoorSettle);
    state->fades_added_by_load_door =
        count_fade_between_traces() - fades_before_load;
    fprintf(stderr, "  [test] clicking the BK door\n");
    // #237: the Backups view is opened from the open Company List — a
    // nested run_menu_screen, which never fades whatever it is.
    const int fades_before_backups = count_fade_between_traces();
    interact("company_bak_0");  // §2.4: opens the (empty) Backups view
    if (!wait_for_backups_view())
        return abort_flow(state, kStageBackupsEmptyView);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageBackupsEmptyViewSettle);
    state->fades_added_by_backups_door =
        count_fade_between_traces() - fades_before_backups;
    fprintf(stderr, "  [test] backing out of the empty backups view\n");
    interact("back");
    // The sub-view's own BACK trace: the list buttons coming back proves a
    // screen is up, this proves it was THIS click that left the sub-view.
    if (!wait_for_trace("company_backups", "back", 5000))
        return abort_flow(state, kStageBackupsViewBack);

    if (!wait_for_interactable("company_del_0", 5000))
        return abort_flow(state, kStageBackupsListReturn);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageBackupsListReturnSettle);
    // The return leg of the LOAD door: emptying the list exits the
    // screen and re-presents the main menu. Nothing between here and
    // the main menu fades (the confirm is trace-only under TESTING).
    const int fades_inside_list = count_fade_between_traces();
    fprintf(stderr, "  [test] deleting the last company (confirm YES)\n");
    interact("company_del_0");  // empties the list -> screen exits

    // Back on a re-entered main menu whose gate must hide CONTINUE/LOAD.
    if (!wait_for_interactable("begin_new_game", 10000))
        return abort_flow(state, kStageBackupsMainMenu);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageBackupsMainMenuSettle);
    state->fades_added_by_load_return =
        count_fade_between_traces() - fades_inside_list;
    state->saw_load_hidden_after_empty = !has_interactable("load_company");
    state->saw_continue_hidden_after_empty =
        !has_interactable("continue_game");
    fprintf(stderr, "  [test] quitting from the main menu\n");
    return finish_flow(state, "quit");
}

// --- backups restore flows --------------------------------------------------

int restore_backup_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageRestoreLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRestoreLoadDoorSettle);
    interact("load_company");

    if (!wait_for_interactable("company_bak_0", 5000))
        return abort_flow(state, kStageRestoreBkDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRestoreBkDoorSettle);
    fprintf(stderr, "  [test] opening the backups view\n");
    interact("company_bak_0");
    // The stranding wait: a company with NO snapshots opens an empty backups
    // view where backup_row_0 can never appear, and the tail is what gets the
    // main thread out of it (CompanyList.stranded_injector_escapes_the_
    // backups_view drives exactly that).
    if (!wait_for_interactable("backup_row_0", 5000))
        return abort_flow(state, kStageRestoreBackupRowMissing);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRestoreBackupRowSettle);
    // First click: the queued NO leaves everything alone.
    fprintf(stderr, "  [test] restoring row 0 (confirm NO)\n");
    interact("backup_row_0");
    if (!wait_for_trace("confirm", "REWIND TO THIS BACKUP?", 5000))
        return abort_flow(state, kStageRestoreConfirmNo);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRestoreConfirmNoSettle);
    // Second click: the queued YES rewinds and opens base camp.
    fprintf(stderr, "  [test] restoring row 0 (confirm YES)\n");
    interact("backup_row_0");
    if (!wait_for_team_menu())
        return abort_flow(state, kStageRestoreTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRestoreTeamMenuSettle);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    return finish_flow(state, "back");
}

int corrupt_backup_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageCorruptBkLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageCorruptBkLoadDoorSettle);
    interact("load_company");

    if (!wait_for_interactable("company_bak_0", 5000))
        return abort_flow(state, kStageCorruptBkDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageCorruptBkDoorSettle);
    fprintf(stderr, "  [test] opening the backups view\n");
    interact("company_bak_0");
    if (!wait_for_interactable("backup_row_0", 5000))
        return abort_flow(state, kStageCorruptBkRow);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageCorruptBkRowSettle);
    fprintf(stderr, "  [test] clicking the corrupt backup row\n");
    interact("backup_row_0");  // popup (trace-only), no confirm
    if (!wait_for_trace("popup", "BACKUP FILE DAMAGED", 5000))
        return abort_flow(state, kStageCorruptBkPopup);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageCorruptBkPopupSettle);
    fprintf(stderr, "  [test] backing out of the backups view\n");
    interact("back");
    if (!wait_for_trace("company_backups", "back", 5000))
        return abort_flow(state, kStageCorruptBkViewBack);

    if (!wait_for_interactable("company_bak_0", 5000))
        return abort_flow(state, kStageCorruptBkListReturn);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageCorruptBkListReturnSettle);
    fprintf(stderr, "  [test] backing out of the company list\n");
    return finish_flow(state, "back");
}

int recover_corrupt_company_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("load_company", 5000))
        return abort_flow(state, kStageRecoverLoadDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRecoverLoadDoorSettle);
    interact("load_company");

    // Row 0 is the corrupt company; its BK door stays available (§2.3 —
    // restore-from-backup IS the recovery path).
    if (!wait_for_interactable("company_bak_0", 5000))
        return abort_flow(state, kStageRecoverBkDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRecoverBkDoorSettle);
    fprintf(stderr, "  [test] opening the corrupt company's backups\n");
    interact("company_bak_0");
    if (!wait_for_interactable("backup_row_0", 5000))
        return abort_flow(state, kStageRecoverBackupRow);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRecoverBackupRowSettle);
    fprintf(stderr, "  [test] restoring the good backup (YES)\n");
    interact("backup_row_0");  // queued YES
    if (!wait_for_team_menu())
        return abort_flow(state, kStageRecoverTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageRecoverTeamMenuSettle);
    return finish_flow(state, "back");
}

// --- CONTINUE failure fallback (§2.9 flow 2) --------------------------------

int continue_torn_newest_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("continue_game", 5000))
        return abort_flow(state, kStageContinueTornDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueTornDoorSettle);
    fprintf(stderr, "  [test] clicking CONTINUE (torn newest)\n");
    interact("continue_game");

    // The failure popup is trace-only under TESTING; CONTINUE falls through
    // to the Company List (rows ts desc: 0 = torn newest, 1 = good previous).
    if (!wait_for_interactable("company_row_1", 5000))
        return abort_flow(state, kStageContinueTornGoodRow);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueTornGoodRowSettle);
    fprintf(stderr, "  [test] opening the good row from the fallback list\n");
    interact("company_row_1");
    if (!wait_for_team_menu())
        return abort_flow(state, kStageContinueTornTeamMenu);
    state->saw_team_menu = true;
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueTornTeamMenuSettle);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    return finish_flow(state, "back");
}

int continue_corrupt_only_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    if (!wait_for_interactable("continue_game", 5000))
        return abort_flow(state, kStageContinueCorruptDoor);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueCorruptDoorSettle);
    fprintf(stderr, "  [test] clicking CONTINUE (corrupt only company)\n");
    interact("continue_game");

    // The fallback list presents the single CORRUPT row; BACK re-presents
    // the main menu (nothing opened, nothing switched).
    if (!wait_for_interactable("company_row_0", 5000))
        return abort_flow(state, kStageContinueCorruptRow);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueCorruptRowSettle);
    fprintf(stderr, "  [test] backing out of the fallback list\n");
    interact("back");

    if (!wait_for_interactable("begin_new_game", 10000))
        return abort_flow(state, kStageContinueCorruptMainMenu);
    if (!wait_for_menu_frames(2))
        return abort_flow(state, kStageContinueCorruptMainMenuSettle);
    fprintf(stderr, "  [test] quitting from the re-entered main menu\n");
    return finish_flow(state, "quit");
}

// --- the flow harness -------------------------------------------------------

// Owns the thread/picker_main/flag/join boilerplate every flow test repeated,
// so no flow can forget to publish main_left and leave its own escape tail
// spinning. Returns the injector's thread result: 0 on the happy path, the
// stage id of the wait that stalled otherwise.
int run_company_list_flow(int (*injector)(void*), FlowState& state,
                          int max_mainmenu_calls = 1)
{
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = max_mainmenu_calls;
    SDL_Thread* thread = SDL_CreateThread(injector, "company_flow", &state);
    if (thread == nullptr) {
        g_picker_max_mainmenu_calls = 0;
        fprintf(stderr, "  [test] SDL_CreateThread FAILED: %s\n",
                SDL_GetError());
        return -1;
    }
    picker_main(0, nullptr);
    // Published BEFORE the join: an injector unwinding through abort_flow is
    // blocked on exactly this flag.
    state.main_left.store(true);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    return thread_result;
}

// Every flow below clicks rows by POSITION (company_row_0, company_del_1,
// backup_row_0), so the list the flow is about to drive is a precondition of
// the test, not an incidental. Asserting it at entry turns a re-targeted
// click — a leaked company from another test, a [SAVE-R5](c) stray slot —
// into a named failure at the top of this test instead of a mystery
// somewhere inside the flow (or, before the escape tail, a group timeout).
void expect_company_rows(const std::vector<std::string>& slots_in_row_order)
{
    std::vector<std::string> listed;
    for (const og::data::CompanyInfo& info : og::data::list_companies())
        listed.push_back(info.slot);
    ASSERT_EQ(slots_in_row_order, listed)
        << "the Company List rows this flow clicks by position must be "
           "exactly these slots, in this order";
}

} // namespace

// Player count is picker-session state, not company state. Exercise the real
// header validation + full GTL body load for every supported live mode while
// deliberately making the retired on-disk byte disagree.
TEST(CompanyList, open_company_slot_preserves_live_player_mode)
{
    ASSERT_TRUE(seed_company("runtimecountdirect", "RUNTIME COUNT", 1000));

    for (int live_count = 0; live_count <= MAX_PLAYERS; ++live_count) {
        SCOPED_TRACE(std::string("live_count=") + std::to_string(live_count));
        const std::uint8_t legacy_count = static_cast<std::uint8_t>(
            live_count == MAX_PLAYERS ? 0 : live_count + 1);
        ASSERT_TRUE(
            patch_legacy_player_count("runtimecountdirect", legacy_count));

        SaveData live;
        live.numplayers = static_cast<unsigned char>(live_count);
        SaveDataIoError io = SaveDataIoError::None;
        ASSERT_EQ(og::ui::ContinueResult::Opened,
                  og::ui::open_company_slot(
                      live, "runtimecountdirect", &io));
        EXPECT_EQ(SaveDataIoError::None, io);
        EXPECT_EQ(live_count, static_cast<int>(live.numplayers))
            << "the legacy GTL byte belongs to the file format, not this "
               "picker session";
    }
}

// CONTINUE adds startup-company selection ahead of the same full load. Pin
// this fixture far enough into the future to outrank any wall-clock-stamped
// company left by another shuffled flow, then prove the selection layer also
// cannot import a stale client-local player count.
TEST(CompanyList, open_most_recent_company_preserves_live_player_mode)
{
    ASSERT_TRUE(seed_company(
        "runtimecountcontinue", "RUNTIME CONTINUE",
        INT64_C(400000000000000)));

    for (int live_count = 0; live_count <= MAX_PLAYERS; ++live_count) {
        SCOPED_TRACE(std::string("live_count=") + std::to_string(live_count));
        const std::uint8_t legacy_count = static_cast<std::uint8_t>(
            live_count == MAX_PLAYERS ? 0 : live_count + 1);
        ASSERT_TRUE(
            patch_legacy_player_count("runtimecountcontinue", legacy_count));

        SaveData live;
        live.numplayers = static_cast<unsigned char>(live_count);
        SaveDataIoError io = SaveDataIoError::None;
        ASSERT_EQ(og::ui::ContinueResult::Opened,
                  og::ui::open_most_recent_company(live, &io));
        EXPECT_EQ(SaveDataIoError::None, io);
        EXPECT_EQ("runtimecountcontinue",
                  og::data::active_company_slot());
        EXPECT_EQ(live_count, static_cast<int>(live.numplayers))
            << "CONTINUE must preserve spectator and every local seat count";
    }
}

// Open: row 0 is the most-recent company — exactly what CONTINUE opens — and
// opening it repoints the active slot, loads the save, and lands on team
// build (base camp).
TEST(CompanyList, open_row_zero_repoints_active_company)
{
    trace_clear();
    ASSERT_TRUE(seed_company("wp3opena", "ALPHA BAND", 1000));
    ASSERT_TRUE(seed_company("wp3openb", "BRAVO BAND", 2000));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3openb", "wp3opena"}));

    FlowState state;
    const int rc = run_company_list_flow(open_row_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu)
        << "opening a company should land on team build (base camp)";
    EXPECT_EQ(2, state.fades_added_by_open_row)
        << "#237: the company list -> Base Camp leg crosses the main-menu "
           "boundary — it fades out and back in, like every main-menu door";
    ASSERT_TRUE(trace_contains("company_list", "open wp3openb"))
        << "row 0 must be the most-recent company";
    ASSERT_EQ("wp3openb", og::data::active_company_slot())
        << "opening repoints the active slot";
    ASSERT_EQ("BRAVO BAND",
              og::runtime::current_session->myscreen_->save_data.save_name)
        << "opening loads the company's save";
}

// WP7 must-fix (cross-company save corruption, §2.9 flow 2): opening a
// company OTHER than the boot-loaded one must re-seed the local lobby cache
// from the opened save. Without the re-seed the polled apply_state_to_save
// keeps rebuilding the base-camp roster from the BOOT company's cached lobby
// state, and the first §3.8 mutation autosave persists that stale roster
// INTO the opened company's file (save_name is the one field the cache does
// not overwrite — hence the roster + file-byte assertions here).
TEST(CompanyList, open_other_company_reseeds_lobby_and_autosave_targets_it)
{
    trace_clear();
    // The opened company must be row 0 (most recent) even if an unrelated
    // test in this binary stamped a live wall-clock timestamp on save0.
    const std::int64_t now_s = og::data::company_clock_now_s();
    ASSERT_TRUE(seed_company_with_soldier(
        "wp7lobbya", "BOOT BAND", "BootGuy", now_s + 500000));
    ASSERT_TRUE(seed_company_with_soldier(
        "wp7lobbyb", "OTHER BAND", "OpenGuy", now_s + 1000000));

    // Boot on company A: picker_main's startup sequence loads the ACTIVE
    // slot and seeds the lobby cache from it.
    ASSERT_TRUE(og::data::set_active_company_slot("wp7lobbya"));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp7lobbyb", "wp7lobbya"}));

    FlowState state;
    const int rc =
        run_company_list_flow(open_other_company_and_toggle_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu)
        << "opening the other company should land on team build (base camp)";
    ASSERT_TRUE(trace_contains("company_list", "open wp7lobbyb"))
        << "row 0 must be the most-recent (non-boot) company";
    ASSERT_EQ("wp7lobbyb", og::data::active_company_slot());

    // The base camp must have served the OPENED company: save_name AND
    // roster (the previously untested field).
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_EQ("OTHER BAND", save.save_name);
    bool memory_has_openguy = false;
    bool memory_has_bootguy = false;
    for (const auto& member : save.team_list) {
        if (member == nullptr)
            continue;
        memory_has_openguy |= member->name == "OpenGuy";
        memory_has_bootguy |= member->name == "BootGuy";
    }
    EXPECT_TRUE(memory_has_openguy)
        << "base camp must serve the opened company's roster";
    EXPECT_FALSE(memory_has_bootguy)
        << "the boot company's cached roster must not leak into the base camp";

    // The deploy-toggle autosave persisted the OPENED roster (with the
    // toggled flag) into the OPENED company's file.
    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None, reloaded.load_with_error("wp7lobbyb"));
    bool file_openguy_present = false;
    bool file_openguy_benched = false;
    bool file_has_bootguy = false;
    for (const auto& member : reloaded.team_list) {
        if (member == nullptr)
            continue;
        if (member->name == "OpenGuy") {
            file_openguy_present = true;
            file_openguy_benched = !member->deployed;
        }
        file_has_bootguy |= member->name == "BootGuy";
    }
    EXPECT_TRUE(file_openguy_present)
        << "the opened company's file keeps its own roster";
    EXPECT_TRUE(file_openguy_benched)
        << "the deploy toggle must persist into the opened company's file";
    EXPECT_FALSE(file_has_bootguy)
        << "the boot company's roster must never be written into the opened "
           "company's file";

    // The boot company's own file is untouched by the whole flow.
    SaveData boot_reloaded;
    ASSERT_EQ(SaveDataIoError::None, boot_reloaded.load_with_error("wp7lobbya"));
    EXPECT_EQ("BOOT BAND", boot_reloaded.save_name);
    bool boot_still_has_bootguy = false;
    for (const auto& member : boot_reloaded.team_list)
        if (member != nullptr && member->name == "BootGuy")
            boot_still_has_bootguy = true;
    EXPECT_TRUE(boot_still_has_bootguy);
}

// E4 (WP7 must-fix): a persisted match-settings mutation autosaves the
// active company — no explicit save, no quit hook (§0.3 "saving is
// automatic"; §3.8 hook inventory: "difficulty/CTF setting callbacks").
TEST(CompanyList, settings_mutation_autosaves_active_company)
{
    ASSERT_TRUE(seed_company("wp7set", "SETTINGS BAND", 3000));
    ASSERT_TRUE(og::data::set_active_company_slot("wp7set"));

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    ASSERT_EQ(SaveDataIoError::None, save.load_with_error("wp7set"));
    const short before = save.respawn_mode;

    ASSERT_EQ(4, static_cast<int>(change_respawn_mode()))
        << "change_respawn_mode should return MENU_OK";

    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None, reloaded.load_with_error("wp7set"));
    EXPECT_NE(before, reloaded.respawn_mode)
        << "the settings toggle must reach the company file without an "
           "explicit save";
    EXPECT_EQ(save.respawn_mode, reloaded.respawn_mode);

    // Undo the in-memory drift so this binary stays shuffle-stable (the
    // fixture resets the SLOT, not the loaded save).
    save.respawn_mode = before;
}

// The §3.8 settings-tail guard: with no active-company FILE on disk, a
// settings toggle must NOT conjure a company — BEGIN NEW GAME's creation
// write stays the only company creator (§2.2), and the empty-state main-menu
// gating must not flip because someone cycled a difficulty row.
TEST(CompanyList, settings_mutation_without_company_file_creates_nothing)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const short before = save.respawn_mode;
    ASSERT_TRUE(og::data::set_active_company_slot("wp7ghost"));
    ASSERT_FALSE(user_file_exists("save/wp7ghost.gtl"));

    ASSERT_EQ(4, static_cast<int>(change_respawn_mode()));

    EXPECT_FALSE(user_file_exists("save/wp7ghost.gtl"))
        << "a settings toggle must never create a company file";

    save.respawn_mode = before;
}

// Delete: NO-first confirm (a queued NO leaves the file), YES deletes the
// company AND its backups, and the re-scan retargets row 0 to the next
// company (what CONTINUE would now open).
TEST(CompanyList, delete_confirms_no_first_and_reaps_backups)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    ASSERT_TRUE(seed_company("wp3dela", "ALPHA BAND", 1000));
    ASSERT_TRUE(seed_company("wp3delb", "BRAVO BAND", 2000));
    ASSERT_TRUE(og::data::backup_company_now("wp3delb"))
        << "the doomed company needs a backup to prove the reap";
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3delb", "wp3dela"}));

    picker_testing_yes_or_no_queue_push(false);  // first confirm: NO
    picker_testing_yes_or_no_queue_push(true);   // second confirm: YES

    FlowState state;
    const int rc = run_company_list_flow(delete_rows_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_EQ(0, picker_testing_yes_or_no_queue_remaining())
        << "both queued confirm answers must have been consumed";
    ASSERT_TRUE(trace_contains("confirm", "DELETE COMPANY?"))
        << "the NO-first confirm should have fired";
    ASSERT_TRUE(trace_contains("confirm", "BACKUPS ARE DELETED TOO"))
        << "the confirm names the backups (U3)";
    ASSERT_TRUE(trace_contains("company_list", "deleted wp3delb"));
    ASSERT_FALSE(user_file_exists("save/wp3delb.gtl"))
        << "YES deletes the company file";
    ASSERT_TRUE(og::data::list_company_backups("wp3delb").empty())
        << "delete reaps the company's backups too";
    ASSERT_TRUE(user_file_exists("save/wp3dela.gtl"))
        << "the surviving company keeps its file (NO answered first)";
}

// Guards: a torn-body row surfaces the load error and restores the previous
// company; a corrupt row never silently switches; X on the ACTIVE company's
// row refuses with switch-first (no confirm even reached).
TEST(CompanyList, corrupt_torn_and_active_guards_never_switch)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    ASSERT_TRUE(seed_company("wp3guarda", "ALPHA BAND", 2000));
    ASSERT_TRUE(seed_torn_company("wp3guardt", "TORN BAND", 4000));
    ASSERT_TRUE(seed_corrupt_company("wp3guardc"));
    // The good company is the ACTIVE one (picker startup loads it).
    ASSERT_TRUE(og::data::set_active_company_slot("wp3guarda"));
    ASSERT_NO_FATAL_FAILURE(
        expect_company_rows({"wp3guardt", "wp3guarda", "wp3guardc"}));

    FlowState state;
    const int rc = run_company_list_flow(guard_rows_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    // The torn row's OPEN action must SURFACE the body-load failure, not
    // silently no-op: open_company_slot reports SaveDataIoError::ReadFailed
    // for a 164-byte body claiming listsize 2, and the row action pops it up
    // through save_error_string(). This is the pin that proves the click on
    // company_row_0 was consumed at all.
    ASSERT_TRUE(trace_contains("popup", "LOAD COMPANY: read_failed"))
        << "the torn row must surface the load error, not silently no-op";
    ASSERT_TRUE(trace_contains("popup", "COMPANY FILE DAMAGED"))
        << "the corrupt row must popup instead of opening";
    ASSERT_TRUE(trace_contains("popup", "THIS COMPANY IS OPEN - SWITCH FIRST"))
        << "deleting the active company's row must refuse up front";
    ASSERT_EQ(0, picker_testing_yes_or_no_queue_remaining());
    ASSERT_TRUE(user_file_exists("save/wp3guarda.gtl"));
    ASSERT_EQ("wp3guarda", og::data::active_company_slot())
        << "neither the torn nor the corrupt row may switch the active slot";
    ASSERT_EQ("ALPHA BAND",
              og::runtime::current_session->myscreen_->save_data.save_name)
        << "the previously open company must survive the failed opens";
}

// Pagination: 11 companies span two eight-row pages; NEXT is keyboard-live
// (a real MenuSpecRow action), the page window retargets the row buttons, and
// opening page 2's row 0 opens the 9th company.
TEST(CompanyList, pagination_flips_pages_and_opens_windowed_row)
{
    trace_clear();
    for (int i = 0; i < 11; ++i) {
        ASSERT_TRUE(seed_company("wp3page" + std::to_string(i),
                                 "PAGE BAND " + std::to_string(i),
                                 10000 - i));
    }
    std::vector<std::string> expected_rows;
    for (int i = 0; i < 11; ++i)
        expected_rows.push_back("wp3page" + std::to_string(i));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows(expected_rows));

    FlowState state;
    const int rc = run_company_list_flow(pagination_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu);
    ASSERT_TRUE(trace_contains("company_list", "page 2/2"))
        << "NEXT must flip to the second page";
    ASSERT_TRUE(trace_contains("company_list", "open wp3page8"))
        << "page 2 row 0 is the 9th company";
    ASSERT_EQ("wp3page8", og::data::active_company_slot());
}

// The §2.4 BK door opens the Backups sub-view (empty here — no level wins,
// no snapshots: only BACK shows), backing out returns to the intact list,
// and deleting the LAST company exits the list to a main menu whose gate
// hides CONTINUE and LOAD.
TEST(CompanyList, backups_door_opens_empty_view_and_empty_delete_exits)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    ASSERT_TRUE(seed_company("wp3lastd", "LAST BAND", 1500));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3lastd"}));
    picker_testing_yes_or_no_queue_push(true);  // delete confirm: YES

    FlowState state;
    // max 2 main-menu calls: the post-delete main menu re-presents.
    const int rc =
        run_company_list_flow(backups_and_empty_injector, state, 2);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("company_list", "backups_door wp3lastd"))
        << "the BK door must stash + trace its row's slot";
    ASSERT_TRUE(trace_contains("company_backups", "back"))
        << "BACK must leave the (empty) backups sub-view";
    ASSERT_FALSE(trace_contains("confirm", "REWIND TO THIS BACKUP?"))
        << "an empty backups view has nothing to confirm";
    ASSERT_FALSE(user_file_exists("save/wp3lastd.gtl"));
    // #237 derivation, on the real doors rather than on a spec field. (The
    // kind assertions in test_menu_engine name the screens; these say what
    // the engine does with them.)
    EXPECT_EQ(2, state.fades_added_by_load_door)
        << "#237: LOAD is a main-menu door — the Company List crosses the "
           "boundary and fades out + in";
    EXPECT_EQ(2, state.fades_added_by_load_return)
        << "#237 symmetry: the way back out of the Company List must fade "
           "exactly as much as the way in";
    EXPECT_EQ(0, state.fades_added_by_backups_door)
        << "#237: COMPANY BACKUPS is opened from the open Company List — a "
           "nested entry never fades, in either direction";
    ASSERT_TRUE(state.saw_load_hidden_after_empty)
        << "no companies left: the main-menu gate must hide LOAD";
    ASSERT_TRUE(state.saw_continue_hidden_after_empty)
        << "no companies left: the main-menu gate must hide CONTINUE";
}

// §2.4 restore round trip: the NO-first confirm leaves everything alone, the
// YES rewinds in place (the §3.7 validated sequence: pre-restore state
// snapshotted first, last-played re-stamped so CONTINUE keeps pointing
// here), the active slot repoints, and the rewound company opens straight
// into base camp.
TEST(CompanyList, restore_rewinds_and_opens_base_camp)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    // OLD state -> snapshot (seq 1) -> NEW state: the restore rewinds NEW
    // back to OLD.
    ASSERT_TRUE(seed_company("wp3resx", "OLD GUARD", 5000));
    ASSERT_TRUE(og::data::backup_company_now("wp3resx"));
    ASSERT_TRUE(seed_company("wp3resx", "NEW GUARD", 6000));
    og::data::set_company_clock_for_tests(777777);
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3resx"}));

    picker_testing_yes_or_no_queue_push(false);  // first confirm: NO
    picker_testing_yes_or_no_queue_push(true);   // second confirm: YES

    FlowState state;
    const int rc = run_company_list_flow(restore_backup_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu)
        << "a rewound company opens straight into base camp (§2.4)";
    ASSERT_EQ(0, picker_testing_yes_or_no_queue_remaining())
        << "both queued confirm answers must have been consumed";
    ASSERT_TRUE(trace_contains("confirm", "REWIND TO THIS BACKUP?"))
        << "the NO-first confirm should have fired";
    ASSERT_TRUE(trace_contains("confirm", "CURRENT STATE IS BACKED UP FIRST"))
        << "the confirm names the pre-restore snapshot (U3)";
    ASSERT_TRUE(trace_contains("company_backups", "restored wp3resx seq 1"));
    ASSERT_EQ("wp3resx", og::data::active_company_slot())
        << "a restore repoints the active slot at the rewound company";
    ASSERT_EQ("OLD GUARD",
              og::runtime::current_session->myscreen_->save_data.save_name)
        << "the in-memory save must hold the rewound state";

    // The pre-restore state became the newest snapshot (§3.7 step 1)...
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("wp3resx");
    ASSERT_EQ(2u, backups.size());
    EXPECT_EQ(2, backups.front().seq) << "no seq reuse after a rewind";
    EXPECT_EQ("NEW GUARD", backups.front().header.display_name)
        << "the newest snapshot holds the pre-restore state";
    // ...and the rewound slot file was re-stamped (§3.7 step 4: a pure byte
    // copy would resurrect the old timestamp and CONTINUE would drift).
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("wp3resx");
    ASSERT_TRUE(header && header->valid);
    EXPECT_EQ("OLD GUARD", header->display_name);
    EXPECT_EQ(777777, header->last_played_unix_s)
        << "restore must re-stamp last-played with the (pinned) clock";
}

// B1, turned into a test. The og_test_basecamp seed-7 shuffle left a company
// ahead of this flow's fixture, restore_backup_injector's backup_row_0 wait
// died on a backups view that had no rows to click, the injector returned 0
// — and picker_main sat in that view until the 420 s group TIMEOUT killed the
// whole binary with rc=124, naming no test at all. The fixture below IS that
// shape (a company with NO snapshots), driven by the SAME injector: what is
// pinned here is the escape tail, i.e. that a stranded flow ends as a named,
// bounded failure carrying the stage that stalled.
TEST(CompanyList, stranded_injector_escapes_the_backups_view)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    ASSERT_TRUE(seed_company("wp3strand", "STRANDED BAND", 5000));
    ASSERT_TRUE(og::data::list_company_backups("wp3strand").empty())
        << "the stranding shape is a company with no snapshots at all";
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3strand"}));

    const auto started = std::chrono::steady_clock::now();
    FlowState state;
    const int rc = run_company_list_flow(restore_backup_injector, state);
    const long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count();

    ASSERT_EQ(kStageRestoreBackupRowMissing, rc)
        << "a stranded injector must return the stage id of the wait that "
           "died, not the happy path's 0";
    ASSERT_TRUE(state.finished)
        << "the escape tail owns the exit of a stranded flow, so it is the "
           "tail that marks it finished";
    ASSERT_TRUE(trace_contains("company_backups", "back"))
        << "the tail must click BACK out of the empty backups view — that "
           "click is what lets picker_main return";
    ASSERT_FALSE(state.saw_team_menu)
        << "nothing was restorable, so nothing may have opened into base camp";
    ASSERT_FALSE(trace_contains("company_backups", "restored"))
        << "an empty backups view has nothing to restore";
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("wp3strand");
    ASSERT_TRUE(header && header->valid);
    EXPECT_EQ("STRANDED BAND", header->display_name)
        << "the company the stranded flow gave up on must be untouched";
    ASSERT_LT(elapsed_ms, 30000)
        << "the stranded flow must unwind in seconds (it took " << elapsed_ms
        << " ms); before the escape tail this shape ran to the 420 s group "
           "timeout";
}

// §2.4 corrupt-backup rows: the click refuses up front (popup, no confirm
// ever reached — the §3.7 step-0 API validation stays the real guard), the
// company file is untouched, and the sub-view stays open.
TEST(CompanyList, corrupt_backup_row_refuses_without_confirm)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    ASSERT_TRUE(seed_company("wp3bkc", "INTACT BAND", 5000));
    {
        // Bad-magic snapshot: lists as a CORRUPT row.
        const std::filesystem::path backups_dir =
            std::filesystem::path(get_user_path()) / "save" / "backups";
        std::error_code ec;
        std::filesystem::create_directories(backups_dir, ec);
        std::ofstream corrupt(backups_dir / "wp3bkc.001.gtl",
                              std::ios::binary | std::ios::trunc);
        corrupt << "not a backup";
        ASSERT_TRUE(corrupt.good());
    }
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3bkc"}));

    FlowState state;
    const int rc = run_company_list_flow(corrupt_backup_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("popup", "BACKUP FILE DAMAGED"))
        << "the corrupt snapshot must popup instead of restoring";
    ASSERT_FALSE(trace_contains("confirm", "REWIND TO THIS BACKUP?"))
        << "the refusal happens BEFORE any confirm";
    ASSERT_FALSE(trace_contains("company_backups", "restored"))
        << "nothing may be restored from a corrupt snapshot";
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("wp3bkc");
    ASSERT_TRUE(header && header->valid);
    EXPECT_EQ("INTACT BAND", header->display_name)
        << "the company file must be untouched";
}

// §3.5 recovery path: a CORRUPT company keeps its BK door, and restoring a
// good snapshot rewinds the damage away — the corrupt bytes themselves get
// snapshotted first (§3.7 step 1 backs up whatever is there), the company
// validates again, and it opens straight into base camp.
TEST(CompanyList, restore_recovers_corrupt_company)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    // Good state -> snapshot (seq 1) -> the company file gets corrupted.
    ASSERT_TRUE(seed_company("wp3rcv", "SAVED BAND", 4000));
    ASSERT_TRUE(og::data::backup_company_now("wp3rcv"));
    ASSERT_TRUE(seed_corrupt_company("wp3rcv"));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3rcv"}));
    picker_testing_yes_or_no_queue_push(true);  // restore confirm: YES

    FlowState state;
    const int rc =
        run_company_list_flow(recover_corrupt_company_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu)
        << "the recovered company opens straight into base camp";
    ASSERT_EQ(0, picker_testing_yes_or_no_queue_remaining());
    ASSERT_TRUE(trace_contains("company_backups", "restored wp3rcv seq 1"));
    ASSERT_EQ("wp3rcv", og::data::active_company_slot());
    ASSERT_EQ("SAVED BAND",
              og::runtime::current_session->myscreen_->save_data.save_name);
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("wp3rcv");
    ASSERT_TRUE(header && header->valid)
        << "the slot file must validate again after the recovery";
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("wp3rcv");
    ASSERT_EQ(2u, backups.size());
    EXPECT_EQ(2, backups.front().seq);
    EXPECT_FALSE(backups.front().header.valid)
        << "the pre-restore snapshot holds the corrupt bytes (nothing is "
           "ever destroyed)";
}

// §2.4 format_backup_row's mounted-title branch (the level_display_guarded
// mount-match rule), pinned where a real mount exists: a mounted campaign's
// scenario titles caption the row; a missing scenario drops the redundant
// "Level N" fallback; a foreign campaign shows the bare tag.
TEST(CompanyList, backup_row_level_titles_follow_the_mount_guard)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    og::data::CompanyBackupInfo info;
    info.slot = "wp3fmt";
    info.seq = 1;
    info.filename = "wp3fmt.001.gtl";
    info.header.slot = "wp3fmt";
    info.header.campaign_id = "gladiator";
    info.header.scen_num = 2;
    info.header.valid = true;

    // Mounted + scenario present: "L<n> " + the scenario title with its
    // "<n>. " prefix stripped and clipped to 14 chars. Gladiator scen2 is
    // "THROUGH TALWOOD FOREST", so the row reads exactly "L2 THROUGH TALWOO".
    const og::ui::BackupRowText mounted = og::ui::format_backup_row(info);
    EXPECT_EQ("L2 THROUGH TALWOO", mounted.level)
        << "mounted scen2 title, prefix-stripped and clipped to 14 (§2.4)";

    // Mounted + scenario missing: the "Level N" fallback is dropped.
    info.header.scen_num = 9999;
    EXPECT_EQ("L9999", og::ui::format_backup_row(info).level);

    // Foreign campaign: the mount guard keeps the bare tag.
    info.header.scen_num = 2;
    info.header.campaign_id = "never-mounted";
    EXPECT_EQ("L2", og::ui::format_backup_row(info).level);
}

// §2.9 flow 2 failure leg: CONTINUE on a header-valid, body-torn newest
// company pops up the load error, restores the previously open company
// (slot AND memory), and falls through to the Company List, where opening
// a good row proceeds to base camp.
TEST(CompanyList, continue_torn_newest_pops_up_and_falls_back_to_list)
{
    trace_clear();
    ASSERT_TRUE(seed_company("wp3ctg", "GOOD BAND", 1000));
    ASSERT_TRUE(seed_torn_company("wp3ctt", "TORN BAND", 4000));
    // The good company is the one currently open (picker startup loads it).
    ASSERT_TRUE(og::data::set_active_company_slot("wp3ctg"));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3ctt", "wp3ctg"}));

    FlowState state;
    const int rc = run_company_list_flow(continue_torn_newest_injector, state);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("popup", "CONTINUE:"))
        << "the CONTINUE failure must surface a popup (§2.1)";
    ASSERT_TRUE(state.saw_team_menu)
        << "the fallback list must open the good row into base camp";
    ASSERT_EQ("wp3ctg", og::data::active_company_slot())
        << "CONTINUE must never leave the slot on the torn company";
    ASSERT_EQ("GOOD BAND",
              og::runtime::current_session->myscreen_->save_data.save_name)
        << "the good company's save must be the one loaded";
}

// §3.5 corrupt-most-recent policy: with only a corrupt (bad-magic) company
// on disk, CONTINUE pops up COMPANY FILE DAMAGED and opens the Load list
// with the CORRUPT row — it never switches the active slot, and BACK
// re-presents the main menu.
TEST(CompanyList, continue_corrupt_only_pops_up_and_never_switches)
{
    trace_clear();
    ASSERT_TRUE(seed_corrupt_company("wp3cfo"));
    ASSERT_NO_FATAL_FAILURE(expect_company_rows({"wp3cfo"}));
    const std::string slot_before = og::data::active_company_slot();

    FlowState state;
    // max 2 main-menu calls: BACK re-presents the main menu.
    const int rc =
        run_company_list_flow(continue_corrupt_only_injector, state, 2);
    ASSERT_EQ(0, rc) << "injector stalled at stage " << rc;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("popup", "CONTINUE: COMPANY FILE DAMAGED"))
        << "the corrupt newest must popup before the fallback list (§3.5)";
    ASSERT_EQ(slot_before, og::data::active_company_slot())
        << "a corrupt CONTINUE target must never repoint the active slot";
    ASSERT_TRUE(user_file_exists("save/wp3cfo.gtl"))
        << "the corrupt file stays on disk (restore/delete are explicit)";
}
