// §2.3 Company List (Load) — SDL flow tests (docs/company-basecamp-design.md,
// WP3). Drives the LOAD door end to end through picker_main with an injector
// thread: open (row 0 == what CONTINUE opens), the NO-first delete confirm
// with its retarget/empty-exit consequences, the corrupt-row and active-slot
// guards (never silently switch / switch-first), and the §2.4 BK door stub.
//
// Lives in the og_test_basecamp group (design G10: heavyweight Layer-F flows
// never ride og_test_menu_ui). The binary gets a fresh temp config dir per
// run; each test still seeds its own slots and reaps them so the flows stay
// order-independent under --gtest_shuffle.

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

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {

constexpr int kTeamMenuTimeoutMs = 20000;

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
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
        if (has_interactable("view_team") && has_interactable("networking"))
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
    sd.current_campaign = "org.openglad.gladiator";
    sd.last_played_unix_s = last_played;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

std::filesystem::path company_path(const std::string& slot)
{
    return std::filesystem::path(get_user_path()) / "save" / (slot + ".gtl");
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

// Reaps a test's slots (file + any backups) so list ordering stays
// deterministic for every other test in the binary under shuffle.
struct CompanySlotCleanup {
    std::vector<std::string> slots;
    ~CompanySlotCleanup()
    {
        for (const std::string& slot : slots) {
            for (const og::data::CompanyBackupInfo& backup :
                 og::data::list_company_backups(slot))
                (void)og::data::delete_company_backup(slot, backup.seq);
            (void)remove_user_file("save/" + slot + ".gtl");
        }
    }
};

struct FlowState {
    bool started = false;
    bool finished = false;
    bool saw_team_menu = false;
    bool saw_load_hidden_after_empty = false;
    bool saw_continue_hidden_after_empty = false;
};

// --- open flow -------------------------------------------------------------

int open_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);  // FadeWithInitialDraw settle
    fprintf(stderr, "  [test] clicking LOAD\n");
    interact("load_company");

    // The Company List fades in (FadeAroundEntry).
    if (wait_for_interactable("company_row_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening company row 0\n");
        interact("company_row_0");

        if (wait_for_team_menu()) {
            state->saw_team_menu = true;
            SDL_Delay(750);
            fprintf(stderr, "  [test] clicking back from team menu\n");
            interact("back");
        }
    }

    state->finished = true;
    return 0;
}

// --- delete flow -----------------------------------------------------------

int delete_rows_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    if (wait_for_interactable("company_row_1", 5000)) {
        SDL_Delay(750);
        // First X click: the queued NO leaves the company alone.
        fprintf(stderr, "  [test] deleting row 0 (confirm NO)\n");
        interact("company_del_0");
        SDL_Delay(400);
        // Second X click: the queued YES deletes it (+ its backups).
        fprintf(stderr, "  [test] deleting row 0 (confirm YES)\n");
        interact("company_del_0");

        // Poll the filesystem for the deletion, then leave.
        int elapsed = 0;
        while (elapsed < 5000 && user_file_exists("save/wp3delb.gtl")) {
            SDL_Delay(50);
            elapsed += 50;
        }
        SDL_Delay(400);
        fprintf(stderr, "  [test] clicking back from the company list\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

// --- guard flow (torn body, corrupt header, active-slot delete) ------------

int guard_rows_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    // Rows (ts desc; corrupt sorts last with ts 0): 0 = torn, 1 = active
    // good company, 2 = corrupt.
    if (wait_for_interactable("company_row_2", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening the torn-body row\n");
        interact("company_row_0");  // popup (trace-only), stays listed
        SDL_Delay(400);
        fprintf(stderr, "  [test] opening the corrupt row\n");
        interact("company_row_2");  // popup COMPANY FILE DAMAGED
        SDL_Delay(400);
        fprintf(stderr, "  [test] deleting the active company's row\n");
        interact("company_del_1");  // popup SWITCH FIRST, no confirm
        SDL_Delay(400);
        interact("back");
    }

    state->finished = true;
    return 0;
}

// --- pagination flow --------------------------------------------------------

int pagination_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    // 11 companies span two pages: the pagers must be live.
    if (wait_for_interactable("company_page_next", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] flipping to page 2\n");
        interact("company_page_next");
        SDL_Delay(400);
        // Page 2 shows one row: the 11th (least recent) company.
        fprintf(stderr, "  [test] opening the page-2 row\n");
        interact("company_row_0");
        if (wait_for_team_menu()) {
            state->saw_team_menu = true;
            SDL_Delay(750);
            interact("back");
        }
    }

    state->finished = true;
    return 0;
}

// --- backups door + delete-last flow ---------------------------------------

int backups_and_empty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    if (wait_for_interactable("company_bak_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] clicking the BK door\n");
        interact("company_bak_0");  // §2.4 door stub: stash + trace
        SDL_Delay(400);
        fprintf(stderr, "  [test] deleting the last company (confirm YES)\n");
        interact("company_del_0");  // empties the list -> screen exits
    }

    // Back on a re-entered main menu whose gate must hide CONTINUE/LOAD.
    if (wait_for_interactable("begin_new_game", 10000)) {
        SDL_Delay(750);
        state->saw_load_hidden_after_empty = !has_interactable("load_company");
        state->saw_continue_hidden_after_empty =
            !has_interactable("continue_game");
        fprintf(stderr, "  [test] quitting from the main menu\n");
        interact("quit");
    }

    state->finished = true;
    return 0;
}

} // namespace

// Open: row 0 is the most-recent company — exactly what CONTINUE opens — and
// opening it repoints the active slot, loads the save, and lands on team
// build (base camp).
TEST(CompanyList, open_row_zero_repoints_active_company)
{
    trace_clear();
    CompanySlotCleanup cleanup{{"wp3opena", "wp3openb"}};
    ASSERT_TRUE(seed_company("wp3opena", "ALPHA BAND", 1000));
    ASSERT_TRUE(seed_company("wp3openb", "BRAVO BAND", 2000));

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(open_row_injector, "company_open", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu)
        << "opening a company should land on team build (base camp)";
    ASSERT_TRUE(trace_contains("company_list", "open wp3openb"))
        << "row 0 must be the most-recent company";
    ASSERT_EQ("wp3openb", og::data::active_company_slot())
        << "opening repoints the active slot";
    ASSERT_EQ("BRAVO BAND",
              og::runtime::current_session->myscreen_->save_data.save_name)
        << "opening loads the company's save";
}

// Delete: NO-first confirm (a queued NO leaves the file), YES deletes the
// company AND its backups, and the re-scan retargets row 0 to the next
// company (what CONTINUE would now open).
TEST(CompanyList, delete_confirms_no_first_and_reaps_backups)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    CompanySlotCleanup cleanup{{"wp3dela", "wp3delb"}};
    ASSERT_TRUE(seed_company("wp3dela", "ALPHA BAND", 1000));
    ASSERT_TRUE(seed_company("wp3delb", "BRAVO BAND", 2000));
    ASSERT_TRUE(og::data::backup_company_now("wp3delb"))
        << "the doomed company needs a backup to prove the reap";

    picker_testing_yes_or_no_queue_push(false);  // first confirm: NO
    picker_testing_yes_or_no_queue_push(true);   // second confirm: YES

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(delete_rows_injector, "company_delete", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

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
    CompanySlotCleanup cleanup{{"wp3guarda", "wp3guardt", "wp3guardc"}};
    ASSERT_TRUE(seed_company("wp3guarda", "ALPHA BAND", 2000));
    ASSERT_TRUE(seed_torn_company("wp3guardt", "TORN BAND", 4000));
    ASSERT_TRUE(seed_corrupt_company("wp3guardc"));
    // The good company is the ACTIVE one (picker startup loads it).
    ASSERT_TRUE(og::data::set_active_company_slot("wp3guarda"));

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(guard_rows_injector, "company_guards", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished);
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

// Pagination: 11 companies span two pages; NEXT is keyboard-live (a real
// MenuSpecRow action), the page window retargets the row buttons, and
// opening page 2's row 0 opens the 11th (least recent) company.
TEST(CompanyList, pagination_flips_pages_and_opens_windowed_row)
{
    trace_clear();
    CompanySlotCleanup cleanup;
    for (int i = 0; i < 11; ++i)
        cleanup.slots.push_back("wp3page" + std::to_string(i));
    for (int i = 0; i < 11; ++i) {
        ASSERT_TRUE(seed_company("wp3page" + std::to_string(i),
                                 "PAGE BAND " + std::to_string(i),
                                 10000 - i));
    }

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(pagination_injector, "company_pages", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_team_menu);
    ASSERT_TRUE(trace_contains("company_list", "page 2/2"))
        << "NEXT must flip to the second page";
    ASSERT_TRUE(trace_contains("company_list", "open wp3page10"))
        << "page 2 row 0 is the 11th (least recent) company";
    ASSERT_EQ("wp3page10", og::data::active_company_slot());
}

// The §2.4 BK door stub traces its slot (the next WP3 stage repoints it to
// the Backups sub-view), and deleting the LAST company exits the list to a
// main menu whose gate hides CONTINUE and LOAD.
TEST(CompanyList, backups_door_traces_and_empty_delete_exits)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    CompanySlotCleanup cleanup{{"wp3lastd"}};
    ASSERT_TRUE(seed_company("wp3lastd", "LAST BAND", 1500));
    picker_testing_yes_or_no_queue_push(true);  // delete confirm: YES

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(backups_and_empty_injector,
                                          "company_backups_door", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;  // the post-delete main menu re-presents
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("company_list", "backups_door wp3lastd"))
        << "the BK door must stash + trace its row's slot";
    ASSERT_FALSE(user_file_exists("save/wp3lastd.gtl"));
    ASSERT_TRUE(state.saw_load_hidden_after_empty)
        << "no companies left: the main-menu gate must hide LOAD";
    ASSERT_TRUE(state.saw_continue_hidden_after_empty)
        << "no companies left: the main-menu gate must hide CONTINUE";
}
