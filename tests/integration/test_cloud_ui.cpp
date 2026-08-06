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
};

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

int cloud_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FlowState* state = static_cast<FlowState*>(data);

    wait_for_interactable("cloud", 10000);
    SDL_Delay(750);  // FadeWithInitialDraw settle
    fprintf(stderr, "  [test] clicking CLOUD\n");
    interact("cloud");

    if (wait_for_interactable("cloud_passphrase", 5000)) {
        state->saw_cloud_screen = true;
        SDL_Delay(750);
        fprintf(stderr, "  [test] setting the passphrase (queued)\n");
        interact("cloud_passphrase");

        // UPLOAD stays disabled until the passphrase lands, so wait for the
        // button instead of guessing a delay.
        bool ok = wait_for_interactable("cloud_upload", 5000);
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
        }
        if (ok) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] clicking DOWNLOAD (queued YES)\n");
            interact("cloud_download");
            ok = wait_for_trace("popup", "Downloaded", 15000);
        }
        if (ok) {
            SDL_Delay(750);
            fprintf(stderr, "  [test] leaving the cloud screen\n");
            interact("back");
        }
        state->clicked_all = ok;
    }

    if (wait_for_interactable("begin_new_game", 10000)) {
        SDL_Delay(750);
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
