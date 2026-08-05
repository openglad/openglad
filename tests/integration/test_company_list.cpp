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

struct ActiveCompanySlotRestore {
    std::string slot = og::data::active_company_slot();
    ~ActiveCompanySlotRestore()
    {
        (void)og::data::set_active_company_slot(slot);
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

// --- cross-company lobby re-seed flow (WP7 must-fix) -----------------------

int open_other_company_and_toggle_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);  // FadeWithInitialDraw settle
    interact("load_company");

    if (wait_for_interactable("company_row_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening row 0 (the NON-boot company)\n");
        interact("company_row_0");

        if (wait_for_team_menu()) {
            state->saw_team_menu = true;
            SDL_Delay(750);
            // One §3.8 roster mutation: bench roster row 0. The autosave this
            // triggers must write the OPENED company's roster into the OPENED
            // company's file.
            fprintf(stderr, "  [test] toggling deploy on roster row 0\n");
            interact("roster_dep_0");
            SDL_Delay(400);
            fprintf(stderr, "  [test] clicking back from base camp\n");
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

    // 11 companies span two eight-row pages: the pagers must be live.
    if (wait_for_interactable("company_page_next", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] flipping to page 2\n");
        interact("company_page_next");
        SDL_Delay(400);
        // Page 2 starts with the 9th company in recency order.
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

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    if (wait_for_interactable("company_bak_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] clicking the BK door\n");
        interact("company_bak_0");  // §2.4: opens the (empty) Backups view
        if (wait_for_backups_view()) {
            SDL_Delay(750);  // FadeAroundEntry settle
            fprintf(stderr, "  [test] backing out of the empty backups view\n");
            interact("back");
        }
        if (wait_for_interactable("company_del_0", 5000)) {
            SDL_Delay(400);
            fprintf(stderr,
                    "  [test] deleting the last company (confirm YES)\n");
            interact("company_del_0");  // empties the list -> screen exits
        }
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

// --- backups restore flows --------------------------------------------------

int restore_backup_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    if (wait_for_interactable("company_bak_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening the backups view\n");
        interact("company_bak_0");
        if (wait_for_interactable("backup_row_0", 5000)) {
            SDL_Delay(750);  // FadeAroundEntry settle
            // First click: the queued NO leaves everything alone.
            fprintf(stderr, "  [test] restoring row 0 (confirm NO)\n");
            interact("backup_row_0");
            SDL_Delay(400);
            // Second click: the queued YES rewinds and opens base camp.
            fprintf(stderr, "  [test] restoring row 0 (confirm YES)\n");
            interact("backup_row_0");
            if (wait_for_team_menu()) {
                state->saw_team_menu = true;
                SDL_Delay(750);
                fprintf(stderr, "  [test] clicking back from team menu\n");
                interact("back");
            }
        }
    }

    state->finished = true;
    return 0;
}

int corrupt_backup_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    if (wait_for_interactable("company_bak_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening the backups view\n");
        interact("company_bak_0");
        if (wait_for_interactable("backup_row_0", 5000)) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] clicking the corrupt backup row\n");
            interact("backup_row_0");  // popup (trace-only), no confirm
            SDL_Delay(400);
            fprintf(stderr, "  [test] backing out of the backups view\n");
            interact("back");
        }
        if (wait_for_interactable("company_bak_0", 5000)) {
            SDL_Delay(400);
            fprintf(stderr, "  [test] backing out of the company list\n");
            interact("back");
        }
    }

    state->finished = true;
    return 0;
}

int recover_corrupt_company_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("load_company", 5000);
    SDL_Delay(750);
    interact("load_company");

    // Row 0 is the corrupt company; its BK door stays available (§2.3 —
    // restore-from-backup IS the recovery path).
    if (wait_for_interactable("company_bak_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] opening the corrupt company's backups\n");
        interact("company_bak_0");
        if (wait_for_interactable("backup_row_0", 5000)) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] restoring the good backup (YES)\n");
            interact("backup_row_0");  // queued YES
            if (wait_for_team_menu()) {
                state->saw_team_menu = true;
                SDL_Delay(750);
                interact("back");
            }
        }
    }

    state->finished = true;
    return 0;
}

// --- CONTINUE failure fallback (§2.9 flow 2) --------------------------------

int continue_torn_newest_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);  // FadeWithInitialDraw settle
    fprintf(stderr, "  [test] clicking CONTINUE (torn newest)\n");
    interact("continue_game");

    // The failure popup is trace-only under TESTING; CONTINUE falls through
    // to the Company List (rows ts desc: 0 = torn newest, 1 = good previous).
    if (wait_for_interactable("company_row_1", 5000)) {
        SDL_Delay(750);  // FadeAroundEntry settle
        fprintf(stderr, "  [test] opening the good row from the fallback list\n");
        interact("company_row_1");
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

int continue_corrupt_only_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking CONTINUE (corrupt only company)\n");
    interact("continue_game");

    // The fallback list presents the single CORRUPT row; BACK re-presents
    // the main menu (nothing opened, nothing switched).
    if (wait_for_interactable("company_row_0", 5000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] backing out of the fallback list\n");
        interact("back");
    }
    if (wait_for_interactable("begin_new_game", 10000)) {
        SDL_Delay(750);
        fprintf(stderr, "  [test] quitting from the re-entered main menu\n");
        interact("quit");
    }

    state->finished = true;
    return 0;
}

} // namespace

// Player count is picker-session state, not company state. Exercise the real
// header validation + full GTL body load for every supported live mode while
// deliberately making the retired on-disk byte disagree.
TEST(CompanyList, open_company_slot_preserves_live_player_mode)
{
    CompanySlotCleanup cleanup{{"runtimecountdirect"}};
    ActiveCompanySlotRestore active_slot_restore;
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
    CompanySlotCleanup cleanup{{"runtimecountcontinue"}};
    ActiveCompanySlotRestore active_slot_restore;
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
    CompanySlotCleanup cleanup{{"wp7lobbya", "wp7lobbyb"}};
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

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        open_other_company_and_toggle_injector, "company_open_other", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

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
    CompanySlotCleanup cleanup{{"wp7set"}};
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

// Pagination: 11 companies span two eight-row pages; NEXT is keyboard-live
// (a real MenuSpecRow action), the page window retargets the row buttons, and
// opening page 2's row 0 opens the 9th company.
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
    ASSERT_TRUE(trace_contains("company_backups", "back"))
        << "BACK must leave the (empty) backups sub-view";
    ASSERT_FALSE(trace_contains("confirm", "REWIND TO THIS BACKUP?"))
        << "an empty backups view has nothing to confirm";
    ASSERT_FALSE(user_file_exists("save/wp3lastd.gtl"));
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
    CompanySlotCleanup cleanup{{"wp3resx"}};
    // OLD state -> snapshot (seq 1) -> NEW state: the restore rewinds NEW
    // back to OLD.
    ASSERT_TRUE(seed_company("wp3resx", "OLD GUARD", 5000));
    ASSERT_TRUE(og::data::backup_company_now("wp3resx"));
    ASSERT_TRUE(seed_company("wp3resx", "NEW GUARD", 6000));
    og::data::set_company_clock_for_tests(777777);

    picker_testing_yes_or_no_queue_push(false);  // first confirm: NO
    picker_testing_yes_or_no_queue_push(true);   // second confirm: YES

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(restore_backup_injector, "backup_restore", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    og::data::set_company_clock_for_tests(std::nullopt);

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

// §2.4 corrupt-backup rows: the click refuses up front (popup, no confirm
// ever reached — the §3.7 step-0 API validation stays the real guard), the
// company file is untouched, and the sub-view stays open.
TEST(CompanyList, corrupt_backup_row_refuses_without_confirm)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    CompanySlotCleanup cleanup{{"wp3bkc"}};
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

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(corrupt_backup_injector,
                                          "backup_corrupt", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

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
    CompanySlotCleanup cleanup{{"wp3rcv"}};
    // Good state -> snapshot (seq 1) -> the company file gets corrupted.
    ASSERT_TRUE(seed_company("wp3rcv", "SAVED BAND", 4000));
    ASSERT_TRUE(og::data::backup_company_now("wp3rcv"));
    ASSERT_TRUE(seed_corrupt_company("wp3rcv"));
    picker_testing_yes_or_no_queue_push(true);  // restore confirm: YES

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(recover_corrupt_company_injector,
                                          "backup_recover", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

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

    // Mounted + scenario present: "L2 <title <= 14ch>".
    const og::ui::BackupRowText mounted = og::ui::format_backup_row(info);
    EXPECT_TRUE(mounted.level.rfind("L2 ", 0) == 0)
        << "got: " << mounted.level;
    EXPECT_LE(mounted.level.size(), std::string("L2 ").size() + 14u)
        << "title must clip to 14 chars (§2.4)";

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
    CompanySlotCleanup cleanup{{"wp3ctg", "wp3ctt"}};
    ASSERT_TRUE(seed_company("wp3ctg", "GOOD BAND", 1000));
    ASSERT_TRUE(seed_torn_company("wp3ctt", "TORN BAND", 4000));
    // The good company is the one currently open (picker startup loads it).
    ASSERT_TRUE(og::data::set_active_company_slot("wp3ctg"));

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(continue_torn_newest_injector,
                                          "continue_torn", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

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
    CompanySlotCleanup cleanup{{"wp3cfo"}};
    ASSERT_TRUE(seed_corrupt_company("wp3cfo"));
    const std::string slot_before = og::data::active_company_slot();

    FlowState state;
    SDL_Thread* thread = SDL_CreateThread(continue_corrupt_only_injector,
                                          "continue_corrupt", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;  // BACK re-presents the main menu
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(trace_contains("popup", "CONTINUE: COMPANY FILE DAMAGED"))
        << "the corrupt newest must popup before the fallback list (§3.5)";
    ASSERT_EQ(slot_before, og::data::active_company_slot())
        << "a corrupt CONTINUE target must never repoint the active slot";
    ASSERT_TRUE(user_file_exists("save/wp3cfo.gtl"))
        << "the corrupt file stays on disk (restore/delete are explicit)";
}
