// #155 CLOUD SAVE — SDL flow test. Drives the main-menu CLOUD door end to
// end through picker_main with an injector thread and a fake PlatformBridge
// HTTP seam: set passphrase -> UPLOAD (the fake receives the on-disk company
// bytes hex-verbatim) -> DOWNLOAD over an existing company (NO-first confirm
// queued YES; a fresh backup appears, the slot file is replaced exactly, and
// the opened company refreshes the main-menu view).

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations from picker.cpp / picker_dialogs.cpp /
// menu_screen_specs.cpp (declared locally by consumers — repo pattern).
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
void picker_testing_cloud_passphrase_queue_clear();
void picker_testing_cloud_passphrase_queue_push(const char* value);

#include "../../src/interface/ui/picker_sdl_defs.h"

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {

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

std::string read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

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

// Written by the fake bridge on the picker thread; read by the test only
// after picker_main returns.
struct FakeCloudTransport {
    std::vector<std::string> post_urls;
    std::vector<std::string> post_bodies;
    std::vector<std::string> get_urls;
    std::string get_body;
};

struct FlowState {
    bool finished = false;
    bool saw_cloud_screen = false;
    bool clicked_all = false;
    // The step that did not land, named. An injector whose exit path is
    // conditional on success reports a miss as a hang; this reports it as a
    // string the test can assert on.
    std::string failed_step;
    // Whether the UPLOAD row was on screen at the moment the upload step
    // gave up — "the button never came back" and "the button came back
    // dead" are different bugs.
    bool upload_row_on_screen = false;
    // #237 symmetry pin, the nested main-menu door (run_nested_menu_door):
    // CLOUD SAVES runs INSIDE the still-open main menu, so the depth rule
    // cannot see it and the door site brackets the fade by hand.
    int fades_added_by_cloud_door = -1;
    int fades_added_by_cloud_return = -1;
};

// Under TESTING every fadeblack takes FadeBetween's test-mode branch, which
// traces exactly one "video" line per fade — so counting those lines counts
// fades.
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

// Block until a trace line shows up, the way wait_for_interactable polls the
// button list. trace_contains takes the trace mutex, so the injector thread
// may poll it while the picker thread writes.
bool wait_for_trace(const char* category, const char* substring, int timeout_ms)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (trace_contains(category, substring))
            return true;
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [test] TIMEOUT waiting for trace %s/'%s' (%d ms)\n",
            category, substring, timeout_ms);
    return false;
}

// Leave whatever screen the flow is standing on and get back to the main
// menu. This runs whatever happened above, and that is the whole point.
//
// The CLOUD screen is a NESTED engine screen (run_nested_menu_door ->
// run_menu_screen, src/interface/ui/menu_screen_runner.cpp) that publishes
// its OWN four rows over allbuttons, so while it is up the main menu's
// begin_new_game does not exist. The injector used to click BACK only on the
// success path: one failed step left the flow inside the nested screen, the
// wait for begin_new_game could only expire, interact("quit") never ran, and
// picker_main never returned. A named failure became a group-wide hang, and
// og_test_menu_ui's CTest TIMEOUT is 420 s — the whole group dies with no
// attribution to the test that wedged it.
//
// BACK carries KEYSTATE_ESCAPE (src/interface/ui/menu_screen_specs.cpp), so
// the key press reaches the same row without a pointer. This is the ladder
// tests/integration/test_train_team.cpp:120-140 already uses.
bool unwind_to_main_menu(int attempts = 8)
{
    const auto main_menu_is_up = [] {
        for (int i = 0; i < 20; ++i) {
            if (has_interactable("begin_new_game"))
                return true;
            SDL_Delay(50);
        }
        return false;
    };
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (has_interactable("begin_new_game"))
            return true;
        if (!interact("back"))
            inject_key_press(SDLK_ESCAPE, 10);
        wait_for_menu_frames(2, 2000);
        if (main_menu_is_up())
            return true;
    }
    fprintf(stderr,
            "  [test] could not unwind to the main menu in %d attempts\n",
            attempts);
    return false;
}

int cloud_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);

    wait_for_interactable("cloud", 10000);
    SDL_Delay(750);  // menu-entry settle
    fprintf(stderr, "  [test] clicking CLOUD\n");
    const int fades_before_door = count_fade_between_traces();
    interact("cloud");

    int fades_inside_cloud = -1;
    if (!wait_for_interactable("cloud_passphrase", 5000)) {
        state->failed_step = "cloud_door";
    } else {
        state->saw_cloud_screen = true;
        SDL_Delay(750);
        state->fades_added_by_cloud_door =
            count_fade_between_traces() - fades_before_door;
        fprintf(stderr, "  [test] setting the passphrase (queued)\n");
        interact("cloud_passphrase");

        // UPLOAD stays disabled until the passphrase lands, so wait for the
        // button instead of guessing a delay.
        bool ok = wait_for_interactable("cloud_upload", 5000);
        if (!ok)
            state->failed_step = "upload";
        if (ok) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] clicking UPLOAD\n");
            interact("cloud_upload");
            // A request is answered on a LATER frame, and a click that lands
            // while the previous one is still in flight is swallowed. That is
            // the race this test hit on a loaded machine: DOWNLOAD was clicked
            // before "Uploaded ..." came back, the GET never fired, and the
            // transport.get_urls assertion below failed. Waiting on each
            // request's own completion popup is the causal condition — the
            // button being on screen is not.
            ok = wait_for_trace("popup", "Uploaded", 15000);
            if (!ok) {
                state->failed_step = "upload";
                state->upload_row_on_screen = has_interactable("cloud_upload");
            }
        }
        if (ok) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] clicking DOWNLOAD (queued YES)\n");
            interact("cloud_download");
            ok = wait_for_trace("popup", "Downloaded", 15000);
            if (!ok)
                state->failed_step = "download";
        }
        if (ok) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] leaving the cloud screen\n");
            fades_inside_cloud = count_fade_between_traces();
        }
        state->clicked_all = ok;
    }

    // Unconditional: a flow that stops clicking inside a nested screen never
    // lets picker_main return.
    if (!unwind_to_main_menu() && state->failed_step.empty())
        state->failed_step = "unwind";

    if (wait_for_interactable("begin_new_game", 10000)) {
        SDL_Delay(750);
        if (fades_inside_cloud >= 0) {
            state->fades_added_by_cloud_return =
                count_fade_between_traces() - fades_inside_cloud;
        }
        fprintf(stderr, "  [test] quitting from the main menu\n");
        interact("quit");
    }

    state->finished = true;
    return 0;
}

} // namespace

TEST(CloudUi, upload_then_download_through_the_cloud_screen)
{
    trace_clear();
    CompanySlotCleanup cleanup{{"cloudflow", "cloudremote"}};
    ActiveCompanySlotRestore active_slot_restore;
    cfg.data.erase("cloud");
    picker_testing_yes_or_no_queue_clear();
    picker_testing_cloud_passphrase_queue_clear();

    // The local company this machine uploads (most recent, so the picker
    // boots on it even if a shuffled sibling stamped a fresh save0).
    const std::int64_t now_s = og::data::company_clock_now_s();
    ASSERT_TRUE(seed_company("cloudflow", "LOCAL BAND", now_s + 1000000));
    ASSERT_TRUE(og::data::set_active_company_slot("cloudflow"));
    const std::string local_bytes = read_file_bytes(company_path("cloudflow"));
    ASSERT_FALSE(local_bytes.empty());

    // The cloud-side company the download must install over it: real writer
    // bytes (loadable by the §2.3 open path), staged through a scratch slot.
    ASSERT_TRUE(seed_company("cloudremote", "CLOUD BAND", now_s + 2000000));
    const std::string remote_bytes =
        read_file_bytes(company_path("cloudremote"));
    ASSERT_TRUE(remove_user_file("save/cloudremote.gtl"));
    std::vector<std::uint8_t> remote_raw(remote_bytes.begin(),
                                         remote_bytes.end());

    // Fake bridge HTTP: keep the real SDL bridge callbacks, swap the two
    // cloud transports for canned results.
    FakeCloudTransport transport;
    transport.get_body =
        R"({"revision":3,"uploaded_at":1754200000000,"slot":"cloudflow",)"
        R"("save_name":"CLOUD BAND","scen_num":1,"last_played":)" +
        std::to_string(now_s + 2000000) + R"(,"data_hex":")" +
        og::ui::cloud::hex_encode(remote_raw) + R"("})";
    PlatformBridge original = platform_bridge();
    PlatformBridge faked = platform_bridge();
    faked.cloud_http_post = [&transport](const std::string& url,
                                         const std::string& body) {
        transport.post_urls.push_back(url);
        transport.post_bodies.push_back(body);
        og::ui::cloud::CloudHttpResult result;
        result.status = 200;
        result.body = R"({"revision":1})";
        return result;
    };
    faked.cloud_http_get = [&transport](const std::string& url) {
        transport.get_urls.push_back(url);
        og::ui::cloud::CloudHttpResult result;
        result.status = 200;
        result.body = transport.get_body;
        return result;
    };
    set_platform_bridge(faked);

    picker_testing_cloud_passphrase_queue_push("correct horse battery");
    picker_testing_yes_or_no_queue_push(true);  // download overwrite: YES

    FlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(cloud_flow_injector, "cloud_flow", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    set_platform_bridge(original);
    picker_testing_yes_or_no_queue_clear();
    picker_testing_cloud_passphrase_queue_clear();

    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_cloud_screen) << "the CLOUD door must open";
    ASSERT_TRUE(state.clicked_all)
        << "every cloud button must have come back interactable in time";

    // #237: CLOUD SAVES is a main-menu door whose screen runs nested inside
    // the still-open main menu — no depth distinguishes it from a Base Camp
    // subscreen, so run_nested_menu_door brackets both halves by hand.
    EXPECT_EQ(2, state.fades_added_by_cloud_door)
        << "#237: the CLOUD door must fade on the way IN (fade-out + the "
           "nested entry's fade-in)";
    EXPECT_EQ(2, state.fades_added_by_cloud_return)
        << "#237 symmetry: and exactly as much on the way BACK — the door "
           "fades out and the main-menu loop fades its next frame in";

    // Passphrase: the DERIVED key persisted (D9), pinned to the D2 vector.
    EXPECT_EQ("73270125791ba273", cfg.get_setting("cloud", "key"));
    ASSERT_TRUE(trace_contains("cloud_save", "passphrase_set"));

    // Upload: one POST to the derived-key route carrying the on-disk bytes
    // hex-VERBATIM plus the header meta.
    ASSERT_EQ(1u, transport.post_urls.size());
    EXPECT_NE(std::string::npos,
              transport.post_urls[0].find("/api/save/73270125791ba273"));
    std::vector<std::uint8_t> local_raw(local_bytes.begin(),
                                        local_bytes.end());
    EXPECT_NE(std::string::npos,
              transport.post_bodies[0].find(
                  "\"data_hex\":\"" + og::ui::cloud::hex_encode(local_raw) +
                  "\""))
        << "upload ships the save file bytes verbatim";
    EXPECT_NE(std::string::npos,
              transport.post_bodies[0].find("\"slot\":\"cloudflow\""));
    ASSERT_TRUE(trace_contains("popup", "Uploaded 'LOCAL BAND'."));

    // Download: the NO-first confirm fired (queued YES), a fresh backup of
    // the pre-download company exists, and the slot now holds the cloud
    // bytes exactly.
    ASSERT_EQ(1u, transport.get_urls.size());
    ASSERT_TRUE(trace_contains("confirm", "OVERWRITE COMPANY?"));
    ASSERT_TRUE(trace_contains("popup", "Downloaded 'CLOUD BAND'."));
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("cloudflow");
    ASSERT_GE(backups.size(), 1u) << "download must back up before the swap";
    EXPECT_EQ(local_bytes,
              read_file_bytes(std::filesystem::path(get_user_path()) /
                              "save" / "backups" / backups.front().filename));
    EXPECT_EQ(remote_bytes, read_file_bytes(company_path("cloudflow")))
        << "the slot file is the downloaded blob byte-identical";
    EXPECT_EQ("3", cfg.get_setting("cloud", "revision"))
        << "the server revision persists for the next optimistic upload";

    // The §2.3 open sequence ran: the downloaded company is live and the
    // main-menu company view refreshed.
    EXPECT_EQ("cloudflow", og::data::active_company_slot());
    EXPECT_EQ("CLOUD BAND",
              og::runtime::current_session->myscreen_->save_data.save_name);
    ASSERT_TRUE(trace_contains("cloud_save", "opened cloudflow"));

    cfg.data.erase("cloud");
}

// D9 (the passphrase IS the vault address): a refused passphrase must leave
// the stored key exactly as it was. Overwriting it on a cancel or on a
// too-short entry would silently repoint the player's cloud slot at an empty
// vault — their band would still be up there, at an address nothing on the
// machine remembers any more. Direct row dispatch (no picker_main flow):
// og_test_menu_ui is the slowest group in the gate.
TEST(CloudUi, passphrase_refusals_never_overwrite_the_stored_key)
{
    trace_clear();
    cfg.data.erase("cloud");
    picker_testing_cloud_passphrase_queue_clear();

    // A key already in the vault-address slot: what the refusals must keep.
    // Seeded straight into cfg (store_cloud_key would also rewrite the
    // settings file, and this test's cost belongs to the row dispatch).
    constexpr const char* kExistingKey = "deadbeefdeadbeef";
    cfg.apply_setting("cloud", "key", kExistingKey);
    ASSERT_EQ(kExistingKey, og::ui::cloud::stored_cloud_key());

    const og::ui::MenuScreenSpec& spec = og::ui::cloud_save_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    Sint32 passphrase_row = -1;
    for (int i = 0; i < spec.row_count; ++i) {
        if (std::string(spec.rows[i].id) == "cloud_passphrase")
            passphrase_row = spec.rows[i].arg;
    }
    ASSERT_NE(-1, passphrase_row) << "the CLOUD screen must carry a "
                                     "PASSPHRASE row";

    og::ui::CloudSaveScreenState state;
    og::ui::install_cloud_save_state_for_screen(&state);

    // 1. Cancelled prompt (the TESTING queue is empty — the prompt said no).
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(passphrase_row, &state));
    EXPECT_EQ(kExistingKey, og::ui::cloud::stored_cloud_key())
        << "a cancelled prompt must not repoint the vault";
    EXPECT_EQ("", state.status_line)
        << "a cancel is not an action and reports nothing";
    EXPECT_FALSE(trace_contains("cloud_save", "passphrase_set"))
        << "nothing was set, so nothing may be announced";
    EXPECT_FALSE(state.key_set)
        << "a cancel runs no state refresh at all";

    // 2. A passphrase below the 8-character floor: refused in words, and the
    // stored key is still the old one.
    picker_testing_cloud_passphrase_queue_push("short");
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(passphrase_row, &state));
    EXPECT_TRUE(trace_contains("popup", "CLOUD SAVE: Passphrase must be"))
        << "a too-short passphrase says so";
    EXPECT_EQ(kExistingKey, og::ui::cloud::stored_cloud_key())
        << "a refused passphrase must not repoint the vault";
    EXPECT_EQ("", state.status_line)
        << "a refusal is not a result line";
    EXPECT_FALSE(trace_contains("cloud_save", "passphrase_set"));

    // 3. The paired positive arm: an accepted passphrase DOES replace the
    // key, with the D2 pinned derivation, and says so on the status line.
    picker_testing_cloud_passphrase_queue_push("correct horse battery");
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(passphrase_row, &state));
    EXPECT_EQ("73270125791ba273", og::ui::cloud::stored_cloud_key())
        << "an accepted passphrase derives and stores its own key";
    EXPECT_EQ("Passphrase set.", state.status_line);
    EXPECT_TRUE(state.key_set)
        << "the refreshed screen state knows a key is present";
    EXPECT_TRUE(trace_contains("cloud_save", "passphrase_set"));

    og::ui::install_cloud_save_state_for_screen(nullptr);
    picker_testing_cloud_passphrase_queue_clear();
    cfg.data.erase("cloud");
}
