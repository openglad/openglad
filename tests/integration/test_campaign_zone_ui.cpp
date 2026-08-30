// Base Camp gameplay zone (docs/basecamp-zones-design.md): a synthetic
// base_camp registration drives the REAL screen through the injector — the
// scripted composition re-bands the roster, renders all four widget kinds,
// refuses locked deploys with the message-line toast, cycles the assign
// chip through the provider (GTL v16 campaign_tag + autosave), debits
// action rows (both dispatch sites running the #212 match-settings sync
// tail), opens the zone submenu from a page row and pops a nested page out
// of it, and drives a level row through BOTH arms of the
// load-with-rollback tail. Direct-dispatch tests cover the host gate, the
// frame-tick fetch triggers and the five roster refetch sites, and the zz
// tour walks every shipped campaign's default composition.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/button.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Picker entry points for the injector-driven flows.
void picker_testing_set_force_real_dialogs(bool enabled);
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
// Presenter pause handshake (TESTING; the uxshots capture seam).
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

void cleanup_picker_state()
{
    PickerState& state = *og::runtime::current_session->picker_;
    for (int i = 0; i < 5; i++) {
        state.backdrops[static_cast<std::size_t>(i)].reset();
        state.backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    state.main_columns_pix.reset();
    state.main_columns_data.free();
    state.main_title_logo_pix.reset();
    state.main_title_logo_data.free();
}

// Wait until the (visible) interactable `id` shows label `want`.
bool wait_for_interactable_label(const std::string& id, const std::string& want,
                                 int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.label == want)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' label '%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// Wait until a (visible) interactable `id` exists at game coords (x, y) —
// disambiguates the per-screen "back" buttons by their geometry.
bool wait_for_interactable_at(const std::string& id, int x, int y,
                              int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.x == x && item.y == y)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' at (%d,%d)\n",
            id.c_str(), x, y);
    return false;
}

// The engine-menu task pump runs at frame-top, before that frame's input
// poll. Requiring the exact quiescent pointer state on that thread proves the
// previous click's release has crossed leftmouse() before another press is
// published. `not_before` additionally honors the roster's deliberate 250ms
// same-row deploy debounce; it is a readiness condition, not proof that an
// action landed.
template <typename Predicate>
bool wait_for_menu_thread_condition(Predicate predicate, int timeout_ms = 5000)
{
    const Uint64 deadline =
        SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (SDL_GetTicks() < deadline) {
        bool satisfied = false;
        const Uint64 now = SDL_GetTicks();
        const int remaining = static_cast<int>(deadline - now);
        if (!run_on_main_thread([&] { satisfied = predicate(); }, remaining))
            return false;
        if (satisfied)
            return true;
    }
    return false;
}

bool wait_for_menu_pointer_ready(Uint64 not_before = 0)
{
    return wait_for_menu_thread_condition([not_before] {
        const InputHardwareState& input = input_hardware_state();
        return SDL_GetTicks() >= not_before && !input.mouse.left &&
            !input.picker_was_left_down &&
            og::input::testing_pending_left_clicks() == 0;
    });
}

bool interact_when_menu_ready(const std::string& id, Uint64 not_before = 0)
{
    if (!wait_for_interactable(id, 5000) ||
        !wait_for_menu_pointer_ready(not_before)) {
        fprintf(stderr, "  [zone] '%s' was not click-ready\n", id.c_str());
        return false;
    }
    interact(id);
    return true;
}

// Stash/restore the picker save across an injector flow (the test_ctf_ui
// pattern, plus the campaign-state book and the v16 campaign_tag bytes the
// zone flows write into).
struct SavedPickerSave
{
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    SaveData snapshot_fields;
    std::map<std::string, std::vector<std::pair<std::string, std::int32_t>>>
        campaign_state;

    SavedPickerSave()
    {
        SaveData& save = test_screen()->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[static_cast<std::size_t>(i)] =
                std::move(save.team_list[static_cast<std::size_t>(i)]);
        snapshot_fields.team_size = save.team_size;
        snapshot_fields.my_team = save.my_team;
        snapshot_fields.numplayers = save.numplayers;
        snapshot_fields.allied_mode = save.allied_mode;
        snapshot_fields.scen_num = save.scen_num;
        snapshot_fields.current_campaign = save.current_campaign;
        // #212 MATCHUP knobs: the zone's action tail writes these through
        // og.campaign_match_set, so they must not leak into the next test.
        snapshot_fields.ctf_capture_limit = save.ctf_capture_limit;
        snapshot_fields.ctf_team_count = save.ctf_team_count;
        for (int t = 0; t < 4; ++t)
            snapshot_fields.m_totalcash[t] = save.m_totalcash[t];
        campaign_state = save.campaign_state;
    }

    ~SavedPickerSave()
    {
        SaveData& save = test_screen()->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)] =
                std::move(team_list[static_cast<std::size_t>(i)]);
        save.team_size = snapshot_fields.team_size;
        save.my_team = snapshot_fields.my_team;
        save.numplayers = snapshot_fields.numplayers;
        save.allied_mode = snapshot_fields.allied_mode;
        save.scen_num = snapshot_fields.scen_num;
        save.current_campaign = snapshot_fields.current_campaign;
        save.ctf_capture_limit = snapshot_fields.ctf_capture_limit;
        save.ctf_team_count = snapshot_fields.ctf_team_count;
        for (int t = 0; t < 4; ++t)
            save.m_totalcash[t] = snapshot_fields.m_totalcash[t];
        save.campaign_state = campaign_state;
    }
};

// Save/restore the pack-script registry around a synthetic registration.
// The gladiator campaign is mounted (same-id remounts are no-ops), so the
// save0 load inside picker_main never re-walks the registry from disk and
// the synthetic chunk survives the whole flow. The chunk name deliberately
// does NOT start with `packs/` (the pack-Lua coverage inventory rule).
class SyntheticCampaignScriptGuard
{
public:
    SyntheticCampaignScriptGuard() : saved_(og::script::pack_scripts()) {}

    static void install(const char* source)
    {
        og::script::register_pack_script(
            {"test.zone", "zonetest/scripts/c.lua", source});
    }

    ~SyntheticCampaignScriptGuard()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : saved_)
            og::script::register_pack_script(script);
    }

private:
    std::vector<og::script::PackScript> saved_;
};

// One capture's outcome. Recorded on the INJECTOR thread and asserted on
// the main thread by verify_zone_shots: a gtest failure raised from an
// injector that then dies mid-flow takes its own message with it, and the
// capture flows already report their observations this way.
struct ZoneShotResult {
    std::string name;
    std::string path;       // "" = no UXSHOTS_DIR, nothing to write
    bool captured = false;  // the presenter handshake froze a real frame
    bool written = false;   // the PPM exists on disk afterwards
    std::size_t nonblack = 0;
};

std::mutex g_zone_shot_mutex;
std::vector<ZoneShotResult> g_zone_shots;

// The nonblank bar the uxshots probe uses (test_uxshots_probe.cpp): a
// settled 320x200 menu frame inks far more than this, a black or
// half-cleared one far less.
constexpr std::size_t kZoneShotMinNonblackPixels = 1000;

// Visual-verification capture (the uxshots PresentedFramePause handshake,
// minimal form): freeze the settled 320x200 frame, count its ink, and dump
// it as a PPM when UXSHOTS_DIR is set. The frame is read back either way —
// "the capture produced a blank screen" is a real failure whether or not
// anyone asked for the file. Runs on the injector thread.
void capture_zone_frame(const char* name)
{
    ZoneShotResult result;
    result.name = name;
    const char* output_dir = std::getenv("UXSHOTS_DIR");
    const bool want_file = output_dir != nullptr && output_dir[0] != '\0';
    if (want_file) {
        std::error_code error;
        std::filesystem::create_directories(output_dir, error);
        if (!error)
            result.path = std::string(output_dir) + "/" + name + ".ppm";
    }

    auto record = [&result] {
        const std::lock_guard<std::mutex> lock(g_zone_shot_mutex);
        g_zone_shots.push_back(std::move(result));
    };

    bool expected = false;
    if (!g_test_present_pause_requested.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        record();
        return;
    }
    const Uint64 deadline = SDL_GetTicks() + 30000;
    while (!g_test_present_paused.load(std::memory_order_acquire)) {
        if (SDL_GetTicks() >= deadline) {
            g_test_present_pause_requested.store(false,
                                                 std::memory_order_release);
            record();
            return;
        }
        SDL_Delay(1);
    }

    std::vector<Uint8> rgb;
    rgb.reserve(320 * 200 * 3);
    screen* scr = test_screen();
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            if (r != 0 || g != 0 || b != 0)
                ++result.nonblack;
            rgb.push_back(r);
            rgb.push_back(g);
            rgb.push_back(b);
        }
    }
    g_test_present_pause_requested.store(false, std::memory_order_release);
    result.captured = true;

    if (!result.path.empty()) {
        FILE* f = fopen(result.path.c_str(), "wb");
        if (f != nullptr) {
            fprintf(f, "P6\n320 200\n255\n");
            fwrite(rgb.data(), sizeof(Uint8), rgb.size(), f);
            fclose(f);
            std::error_code exists_error;
            result.written =
                std::filesystem::exists(result.path, exists_error) &&
                !exists_error;
            fprintf(stderr, "  [uxshot] wrote %s\n", result.path.c_str());
        }
    }
    record();
}

// Main-thread verification of every shot the finished flow recorded, and
// the reason the capture seam has teeth: a re-capture run that quietly
// stopped producing stills (or started producing black ones) now fails the
// test instead of leaving the media script nothing to convert. Clears the
// ledger, so each flow only ever answers for its own captures.
void verify_zone_shots(const char* flow, std::size_t expected_shots)
{
    std::vector<ZoneShotResult> shots;
    {
        const std::lock_guard<std::mutex> lock(g_zone_shot_mutex);
        shots.swap(g_zone_shots);
    }
    EXPECT_EQ(expected_shots, shots.size())
        << flow << ": the flow did not reach every capture point";
    for (const ZoneShotResult& shot : shots) {
        EXPECT_TRUE(shot.captured)
            << flow << ": " << shot.name << " never froze a presented frame";
        EXPECT_GE(shot.nonblack, kZoneShotMinNonblackPixels)
            << flow << ": " << shot.name << " is blank (" << shot.nonblack
            << " nonblack pixels)";
        if (shot.path.empty())
            continue;
        EXPECT_TRUE(shot.written)
            << flow << ": " << shot.name << " produced no file at "
            << shot.path;
    }
}

void write_save0_with_two_soldiers(const std::string& campaign, short scen_num,
                                   const std::vector<int>& completed = {})
{
    SaveData& save = test_screen()->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    const char* names[] = {"Alpha", "Beta"};
    for (std::size_t i = 0; i < 2; ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
        save.team_list[i]->deployed = true;
        save.team_list[i]->campaign_tag = 0;
    }
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    // A defined resting state includes the match knobs: in binary order an
    // earlier flow's fill/map_units would otherwise leak into this save and
    // the amendment-5 macro faces (derived from fill[]) would not be at
    // rest (caught by the ordered og_test_matchup run, invisible alone).
    save.fill = {};
    save.map_units = {};
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.m_totalcash[0] = 5000;
    save.campaign_state.clear();
    save.completed_levels.clear();
    for (int level : completed)
        save.add_level_completed(campaign, level);
    ASSERT_TRUE(save.save("save0"));
}

// The synthetic zone: all four widget kinds. Readout (coin + kit — hoisted
// into the panel's header band, so it costs no row unit), one text line, an
// actions widget (page door / costed action that RETIRES through `done`
// once the book has honored it / a level row whose TARGET re-derives too —
// without the kit the road points at a level that is not there, which
// renders CLOSED and drives the load-with-rollback failure arm), and a
// roster with an oath column (WAR/BURDEN) plus an unset-lock so unassigned
// heroes cannot deploy.
// Layout: 1 text + 3 actions + roster (1 header + 3 rows) = 8; the readout
// rides the header band.
//
// Both action hooks also write a #212 MATCHUP knob so the Acted tail's
// consume_match_settings_dirty() -> sync branch runs at BOTH dispatch sites
// (the zone's own rows and the zone submenu's).
//
// The stores page also carries the two DICE actions (rows 3 and 4, past
// everything the interactive flow clicks): submenu actions whose results
// carry a `level`, for the direct-drive D3 routing test below
// (submenu_action_result_level_routes_the_gated_set_tail).
constexpr const char* kZoneScript = R"LUA(og.register_campaign_hooks({
  vars = { "kit" },
  base_camp = function()
    local owned = og.campaign_state_get("kit") == 1
    local kit_value = "NO"
    local road_level = 9999
    if owned then
      kit_value = "YES"
      road_level = 2
    end
    return {
      widgets = {
        { kind = "readout",
          items = {
            { label = "COIN", value = tostring(og.campaign_gold()) },
            { label = "KIT", value = kit_value },
          } },
        { kind = "text", lines = { "The camp fire crackles." } },
        { kind = "actions",
          entries = {
            { id = "stores", label = "STORES", kind = "page" },
            { id = "buy_kit", label = "FIELD KIT", kind = "action",
              cost = 60, done = owned },
            { id = "road", kind = "level", level = road_level },
          } },
        { kind = "roster",
          assign = { key = "road", labels = { "WAR", "BURDEN" } },
          locks = { { unset = true, reason = "Swear at the fire first." } } },
      },
    }
  end,
  picker_menu = function(page_id)
    if page_id == "stores" then
      return {
        title = "STORES",
        lines = { "The shelves are thin.", "The keeper counts twice." },
        entries = {
          { id = "bread", label = "BREAD", kind = "action", cost = 10 },
          { id = "cellar", label = "CELLAR", kind = "page" },
          { id = "ghost", kind = "level", level = 9999 },
          { id = "dice", label = "DICE", kind = "action" },
          { id = "dice_far", label = "FAR DICE", kind = "action" },
        },
      }
    end
    if page_id == "cellar" then
      return {
        title = "CELLAR",
        entries = {
          { id = "sip", label = "SIP", kind = "action" },
        },
      }
    end
    return { title = "EMPTY" }
  end,
  picker_action = function(entry_id)
    if entry_id == "buy_kit" then
      og.campaign_state_set("kit", 1)
      og.campaign_match_set("score_limit", 15)
      return { message = "Kit stowed." }
    end
    if entry_id == "bread" then
      og.campaign_match_set("respawn_ticks", 300)
      return { message = "Bread eaten." }
    end
    if entry_id == "dice" then
      return { level = 2, message = "The dice land." }
    end
    if entry_id == "dice_far" then
      return { level = 15, message = "Dice gone cold." }
    end
    return { message = "Sipped." }
  end,
}))LUA";

struct ZoneFlowState
{
    bool started = false;
    bool finished = false;
    bool zone_rows_seen = false;
    bool ghost_row_seen = false;
    bool kit_label_flipped = false;
    bool level_current_seen = false;
    bool submenu_opened = false;
    bool submenu_row_seen = false;
    bool nested_page_opened = false;
    bool nested_page_popped = false;
    bool returned_from_submenu = false;
    std::string failure;
};

int fail_zone_flow(ZoneFlowState* state, const char* failure)
{
    state->failure = failure;
    fprintf(stderr, "  [zone] flow failed: %s\n", failure);

    // The injector must not abandon picker_main's blocking Base Camp loop.
    // This is one structural BACK during failure cleanup, never an action
    // retry. At the roster steps below Base Camp owns the unique (8,178)
    // BACK; if screen identity changed, leave the failed state untouched.
    if (wait_for_interactable_at("back", 8, 178, 5000) &&
        wait_for_menu_pointer_ready()) {
        interact("back");
    }
    return 0;
}

bool wait_for_alpha_roster_state(bool deployed, std::uint8_t campaign_tag,
                                 const char* trace_category,
                                 const char* trace_message,
                                 const char* toast_message = nullptr)
{
    const Uint64 deadline = SDL_GetTicks() + 5000;
    const std::string expected_label = deployed ? "X" : "";

    // Autosave/refetch runs inside the action frame, when the frame-top task
    // pump is intentionally unavailable. Observe only the mutex-protected
    // live button and trace surfaces until that action publishes its exact
    // result; then cross the next menu-frame barrier to verify SaveData and
    // the consumed release together. Posting the barrier before the action
    // finishes can strand it behind synchronous autosave under load.
    while (SDL_GetTicks() < deadline) {
        const bool exact_surface =
            has_interactable("roster_dep_0") &&
            interactable_label("roster_dep_0") == expected_label &&
            trace_contains(trace_category, trace_message) &&
            (toast_message == nullptr ||
             trace_contains("zone", toast_message));
        if (exact_surface) {
            const Uint64 now = SDL_GetTicks();
            if (now >= deadline)
                break;
            const int remaining = static_cast<int>(deadline - now);
            return wait_for_menu_thread_condition([=] {
                const SaveData& save = test_screen()->save_data;
                const guy* const alpha = save.team_list[0].get();
                const InputHardwareState& input = input_hardware_state();
                return alpha != nullptr && alpha->deployed == deployed &&
                    alpha->campaign_tag == campaign_tag &&
                    !input.mouse.left && !input.picker_was_left_down &&
                    og::input::testing_pending_left_clicks() == 0;
            }, remaining);
        }
        SDL_Delay(25);
    }

    fprintf(stderr,
            "  [zone] Alpha never reached deployed=%d tag=%u trace=%s/%s\n",
            deployed ? 1 : 0, static_cast<unsigned>(campaign_tag),
            trace_category, trace_message);
    return false;
}

int zone_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    ZoneFlowState* state = static_cast<ZoneFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // The scripted zone composes the Base Camp directly: the actions
    // widget's rows carry the book (no SCENARIO door hop).
    state->zone_rows_seen =
        wait_for_interactable_label("zone_action_0", "STORES  >", 10000) &&
        wait_for_interactable_label("zone_action_1", "FIELD KIT  60g", 5000);
    SDL_Delay(500);
    capture_zone_frame("zone_scripted_camp");

    // Deploy-lock refusal: Alpha starts DEPLOYED, so bench first (allowed —
    // locks gate the toggle-ON only), then the re-deploy refuses with the
    // toast while the hero is unassigned.
    if (!interact_when_menu_ready("roster_dep_0") ||
        !wait_for_alpha_roster_state(false, 0, "basecamp",
                                     "deploy slot=0 off")) {
        return fail_zone_flow(state, "Alpha was not acknowledged as benched");
    }
    capture_zone_frame("uxr_after_bench");
    // The accepted bench stamped the production 250ms same-row debounce.
    // Start the lock click only after both that boundary and the input-release
    // condition hold on the menu thread.
    const Uint64 lock_click_ready_at = SDL_GetTicks() + 251;
    if (!interact_when_menu_ready("roster_dep_0", lock_click_ready_at) ||
        !wait_for_alpha_roster_state(
            false, 0, "zone", "deploy_locked slot=0",
            "toast Swear at the fire first.")) {
        return fail_zone_flow(state,
                              "Alpha's unset deploy lock was not acknowledged");
    }
    capture_zone_frame("uxr_lock_toast");

    // The assign chip: unset -> WAR (undeployed cycle rides the autosave
    // tail), then WAR -> BURDEN.
    if (!interact_when_menu_ready("roster_team_0") ||
        !wait_for_alpha_roster_state(false, 1, "zone",
                                     "assign slot=0 tag=1",
                                     "toast Sworn to WAR.")) {
        return fail_zone_flow(state, "Alpha was not acknowledged as WAR");
    }
    capture_zone_frame("uxr_assign_war");
    if (!interact_when_menu_ready("roster_team_0") ||
        !wait_for_alpha_roster_state(false, 2, "zone",
                                     "assign slot=0 tag=2",
                                     "toast Sworn to BURDEN.")) {
        return fail_zone_flow(state, "Alpha was not acknowledged as BURDEN");
    }
    capture_zone_frame("uxr_assign_burden");

    // Assigned heroes clear the unset-lock: the deploy sticks now.
    if (!interact_when_menu_ready("roster_dep_0") ||
        !wait_for_alpha_roster_state(true, 2, "basecamp",
                                     "deploy slot=0 on")) {
        return fail_zone_flow(state,
                              "assigned Alpha was not acknowledged as deployed");
    }

    // Without the kit the road row points at a level that is not there:
    // the load-with-rollback failure arm toasts and restores the cursor.
    state->ghost_row_seen = wait_for_interactable_label(
        "zone_action_2", "SCEN 9999  [CLOSED]", 5000);
    SDL_Delay(300);
    interact("zone_action_2");
    SDL_Delay(200);
    capture_zone_frame("uxr_level_fail_toast");
    SDL_Delay(600);

    // The costed action: debit + state write-through + refetch retire (the
    // row stops quoting a price and stops dispatching).
    interact("zone_action_1");
    state->kit_label_flipped =
        wait_for_interactable_label("zone_action_1", "FIELD KIT  [DONE]",
                                    5000);
    capture_zone_frame("uxr_kit_toast");
    SDL_Delay(300);

    // Clicking a retired purchase refuses in the campaign's voice instead
    // of charging again.
    interact("zone_action_1");
    SDL_Delay(200);
    capture_zone_frame("uxr_kit_done_toast");
    SDL_Delay(600);

    // The level row: the load-with-rollback set tail commits scen_num and
    // the refetched zone decorates the row CURRENT.
    interact("zone_action_2");
    SDL_Delay(600);
    state->level_current_seen = [] {
        int elapsed = 0;
        while (elapsed < 10000) {
            for (const Interactable& item : get_interactables()) {
                if (item.id == "zone_action_2" && !item.hidden &&
                    item.label.find("[CURRENT]") != std::string::npos)
                    return true;
            }
            SDL_Delay(50);
            elapsed += 50;
        }
        return false;
    }();
    capture_zone_frame("uxr_level_current");
    SDL_Delay(300);

    // Clicking the road you are already on answers instead of going quiet
    // (the standing toast is dropped first, so nothing stale can be read
    // as this click's reply).
    interact("zone_action_2");
    SDL_Delay(200);
    SDL_Delay(600);

    // The page row opens the zone submenu (its BACK owns the unique
    // (10,169) rect): a costed action, a nested page, a level row that
    // cannot load, then BACK at the root closes it.
    interact("zone_action_0");
    state->submenu_opened =
        wait_for_interactable_at("back", 10, 169, 10000);
    state->submenu_row_seen =
        wait_for_interactable_label("zone_row_0", "BREAD  10g", 10000);
    SDL_Delay(500);
    capture_zone_frame("zone_submenu_stores");
    // The purchase confirms exactly as it does at the root: a non-modal
    // message-line toast, no OK button to dismiss.
    interact("zone_row_0");
    SDL_Delay(400);
    capture_zone_frame("uxr_submenu_after_buy");
    SDL_Delay(600);

    // A page row INSIDE the submenu pushes a second page; BACK there pops
    // one page instead of closing the submenu.
    interact("zone_row_1");
    state->nested_page_opened =
        wait_for_interactable_label("zone_row_0", "SIP", 10000);
    SDL_Delay(300);
    interact("back");
    state->nested_page_popped =
        wait_for_interactable_label("zone_row_0", "BREAD  10g", 10000);
    SDL_Delay(300);

    // A submenu level row that cannot load: the submenu's own
    // load-with-rollback arm (a modal dialog, trace-only under TESTING).
    interact("zone_row_2");
    SDL_Delay(200);
    capture_zone_frame("uxr_submenu_level_fail");
    SDL_Delay(600);

    interact("back");
    state->returned_from_submenu =
        wait_for_interactable_label("zone_action_0", "STORES  >", 10000);
    SDL_Delay(150);
    capture_zone_frame("uxr_back_at_root");
    SDL_Delay(300);

    // Cycling a DEPLOYED hero first un-deploys through the full roster
    // tail (ready clears — correct), then the tag applies: Alpha is
    // deployed with tag BURDEN, so this cycle benches her and swears WAR.
    if (!interact_when_menu_ready("roster_team_0") ||
        !wait_for_alpha_roster_state(false, 1, "zone",
                                     "assign_undeploys slot=0")) {
        return fail_zone_flow(
            state, "deployed Alpha's BURDEN-to-WAR cycle was not acknowledged");
    }

    // Base Camp -> main menu.
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, scripted_zone_flow_locks_assigns_acts_and_sets_level)
{
    trace_clear();
    SavedPickerSave save_guard;
    // Mount BEFORE registering: the save0 load inside picker_main then hits
    // the same-id mount no-op and never rescans the script registry.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);
    // The earned-roads gate: the kit road (level 2) is a replay of a
    // cleared level, and the ghost road (9999) is "cleared" too so its
    // click passes the gate and still exercises the load-with-rollback
    // failure arm (CLOSED masks CLEARED, so its face is unchanged).
    write_save0_with_two_soldiers("gladiator", 1, {2, 9999});

    ZoneFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        zone_flow_injector, "zone_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    // The 13 unconditional capture points of the flow above.
    verify_zone_shots("scripted_zone_flow", 13);

    SaveData& save = test_screen()->save_data;
    EXPECT_TRUE(state.finished)
        << "injector should complete the flow: " << state.failure;
    EXPECT_TRUE(state.zone_rows_seen)
        << "the scripted composition re-bands the parked action rows";
    EXPECT_TRUE(state.kit_label_flipped)
        << "the action refetch re-derives the label from the decision book";
    EXPECT_TRUE(state.level_current_seen)
        << "the level-set tail refetches with the CURRENT marker";
    EXPECT_TRUE(state.submenu_opened)
        << "a page-kind zone row opens the zone submenu";
    EXPECT_TRUE(state.submenu_row_seen);
    EXPECT_TRUE(state.nested_page_opened)
        << "a page row inside the submenu pushes a second page";
    EXPECT_TRUE(state.nested_page_popped)
        << "BACK below the root pops ONE page, not the whole submenu";
    EXPECT_TRUE(state.returned_from_submenu)
        << "BACK at the submenu root resumes the Base Camp";
    EXPECT_TRUE(trace_contains("zone", "submenu_back_to STORES"))
        << "the depth>1 BACK arm names the page it popped back to";
    EXPECT_TRUE(trace_contains("zone", "submenu_closed"))
        << "BACK at the root closes the submenu";

    // The load-with-rollback failure arm at BOTH level-set sites, and both
    // in the CAMPAIGN's voice — an engine diagnostic ("Invalid level
    // file.") never reaches the campaign's own message slot. Neither arm
    // moves the cursor off the level that is actually loaded.
    EXPECT_TRUE(state.ghost_row_seen)
        << "a level row with no scenario file renders CLOSED and clicks";
    EXPECT_TRUE(trace_contains("zone", "toast That road is not open yet."))
        << "both level-set tails roll back with the player-facing toast";
    EXPECT_FALSE(trace_contains("popup", "Invalid level file."))
        << "engine loader wording must not reach the player";
    EXPECT_FALSE(trace_contains("popup", "Also failed to reload"))
        << "the rollback reload must succeed";
    // A successful level set says so, so the PREVIOUS action's toast can
    // never be read as this one's answer.
    EXPECT_TRUE(trace_contains("zone", "toast Level set to"))
        << "setting the level must confirm itself";
    EXPECT_TRUE(trace_contains("zone", "toast Already on that level."))
        << "re-picking the current level answers rather than going quiet";

    // #212: BOTH action dispatch sites ran the match-settings sync tail
    // (the traces name their site so deleting one is not covered by the
    // other).
    EXPECT_TRUE(trace_contains("zone", "match_settings_synced zone"))
        << "a zone action that wrote a MATCHUP knob must sync it";
    EXPECT_TRUE(trace_contains("zone", "match_settings_synced submenu"))
        << "a submenu action that wrote a MATCHUP knob must sync it";
    EXPECT_EQ(15, save.ctf_capture_limit)
        << "the zone action's og.campaign_match_set landed";
    EXPECT_EQ(300, save.ctf_respawn_ticks)
        << "the submenu action's og.campaign_match_set landed";

    // The deploy lock refused the unassigned re-deploy with the toast.
    EXPECT_TRUE(trace_contains("zone", "deploy_locked slot=0"))
        << "an unset-lock must refuse the toggle-ON";
    EXPECT_TRUE(trace_contains("zone", "toast Swear at the fire first."))
        << "lock refusals ride the message-line toast, never a modal";

    // The assign chip cycled unset -> WAR -> BURDEN through the provider,
    // then (deployed) BURDEN -> WAR through the un-deploy-first tail.
    EXPECT_TRUE(trace_contains("zone", "assign slot=0 tag=1"));
    EXPECT_TRUE(trace_contains("zone", "assign slot=0 tag=2"));
    EXPECT_TRUE(trace_contains("zone", "toast Sworn to WAR."));
    EXPECT_TRUE(trace_contains("zone", "toast Sworn to BURDEN."));
    EXPECT_TRUE(trace_contains("zone", "assign_undeploys slot=0"))
        << "cycling a DEPLOYED hero first un-deploys through the full "
           "roster tail";
    ASSERT_TRUE(save.team_list[0]);
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->campaign_tag))
        << "the tag byte lands on the guy record";
    EXPECT_FALSE(save.team_list[0]->deployed)
        << "the deployed cycle benched the hero before swearing";
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"))
        << "an assigned hero deployed past the unset-lock mid-flow";

    // Purchases confirm the SAME way at every depth of the book: the
    // non-modal message line, never a modal one page in.
    EXPECT_TRUE(trace_contains("zone", "toast Bread eaten."))
        << "a submenu purchase toasts like a root purchase";
    EXPECT_FALSE(trace_contains("popup", "Bread eaten."))
        << "a submenu purchase must not raise a blocking dialog";
    // A retired purchase refuses instead of charging twice.
    EXPECT_TRUE(trace_contains("zone", "toast You have that already."))
        << "clicking a done row refuses in the campaign's voice";

    // The costed action debited once and autosaved; the submenu bread
    // debited once more (the second kit click must NOT debit again).
    EXPECT_EQ(4930u, save.m_totalcash[0]) << "5000 - 60g kit - 10g bread";
    EXPECT_EQ(1, save.campaign_state_get("gladiator", "kit"));
    EXPECT_TRUE(trace_contains("zone", "acted_autosave"))
        << "an Acted zone action must run the company autosave tail";
    // The level row committed the cursor through the set tail.
    EXPECT_EQ(2, save.scen_num);
    EXPECT_TRUE(trace_contains("zone", "level_set 2"));

    // GTL v16 round trip: the tag, the debit, and the decision are on disk
    // (the assign tail autosaves; a crash must not lose an oath).
    {
        SaveData reloaded;
        ASSERT_TRUE(reloaded.load("save0"));
        ASSERT_TRUE(reloaded.team_list[0]);
        EXPECT_EQ(1, static_cast<int>(reloaded.team_list[0]->campaign_tag))
            << "campaign_tag must persist through the company file";
        EXPECT_EQ(4930u, reloaded.m_totalcash[0]);
        EXPECT_EQ(1, reloaded.campaign_state_get("gladiator", "kit"));
    }
}

namespace {

// The default-zone injector: no registration => the built-in composition
// through the same widget path. The classic flows must read identically:
// deploy toggles by id, the row body trains, and HIRE (relocated to the
// roster header band, same id) opens the hire screen.
struct DefaultZoneFlowState
{
    bool finished = false;
    bool hire_seen = false;
    bool hire_opened = false;
    bool train_opened = false;
};

int default_zone_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DefaultZoneFlowState* state = static_cast<DefaultZoneFlowState*>(data);

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // The default composition: roster + HIRE, no zone action rows.
    state->hire_seen = wait_for_interactable("hire_troops", 10000);
    SDL_Delay(500);
    capture_zone_frame("zone_default_camp");

    // Deploy toggle by id (the classic flow).
    interact("roster_dep_0");
    SDL_Delay(300);
    interact("roster_dep_0");
    SDL_Delay(300);

    // Row-body train door.
    interact("roster_row_0");
    state->train_opened = wait_for_interactable("accept", 10000);
    SDL_Delay(300);
    interact("back");
    SDL_Delay(300);

    // HIRE at its new header-band rect, same id.
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(300);
    interact("hire_troops");
    state->hire_opened = wait_for_interactable("hire_me", 10000);
    SDL_Delay(300);
    interact("back");
    SDL_Delay(300);

    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, default_zone_keeps_the_classic_roster_flows)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    // No synthetic registration: the production packs register no base_camp
    // hook yet, so every campaign renders the default composition.
    write_save0_with_two_soldiers("gladiator", 1);

    DefaultZoneFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        default_zone_injector, "default_zone", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    verify_zone_shots("default_zone_flow", 1);

    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.hire_seen)
        << "the default zone renders HIRE (id hire_troops)";
    EXPECT_TRUE(state.train_opened)
        << "the row-body train door survives the zone split";
    EXPECT_TRUE(state.hire_opened)
        << "HIRE still opens the hire screen from its header-band rect";
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 off"));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"));
}

namespace {

// A joiner lobby client for the host-gate dispatch test (the seat-rail
// test pattern: drive on_spec_row directly, no blocking screen).
struct JoinerZoneLobbyClient final : og::ui::IPickerLobbyClient
{
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
};

} // namespace

// Level rows in the zone are host-gated at ACTIVATION: a joiner's click
// refuses with the message-line toast and never touches scen_num.
TEST(CampaignZoneUi, zone_level_row_is_host_gated_with_a_toast)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    // With the kit stowed the road row points at a real level, so the
    // refusal below is the HOST GATE and not a load failure.
    ASSERT_TRUE(save.campaign_state_set("gladiator", "kit", 1));

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    // Entry 2 of the actions widget is the level row.
    ASSERT_EQ(3u, zone.actions()[0].rows.size());
    ASSERT_EQ(2, zone.actions()[0].rows[2].level);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    JoinerZoneLobbyClient lobby;
    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.on_spec_row);
    const short before = save.scen_num;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampZoneActionBase + 2, &state));
    EXPECT_EQ(before, save.scen_num)
        << "a joiner's level click must not move the cursor";
    EXPECT_TRUE(trace_contains("zone", "level_denied_nonhost 2"));
    EXPECT_TRUE(
        trace_contains("zone", "toast Only the host may set the level."))
        << "the refusal is a toast, never a modal (a modal strands the "
           "joiner mid-GO)";

    og::ui::install_base_camp_state_for_screen(nullptr);
    og::ui::install_active_picker_lobby_client(saved_client);
}

namespace {

// #207: one replay-marked level row on the camp docket AND on the book's
// root page, so both SDL level-set tails can be driven on the same script.
constexpr const char* kReplayZoneScript = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return {
      title = "ROADS",
      entries = {
        { id = "2", label = "THE ROAD BACK", kind = "level", level = 2, replay = true },
      },
    }
  end,
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "2", label = "THE ROAD BACK", kind = "level", level = 2, replay = true },
        } },
      { kind = "roster" },
    } }
  end,
}))LUA";

// The same docket row WITHOUT the replay mark: the camp's plain level-set
// face, for the arm-abandonment pin below.
constexpr const char* kPlainZoneScript = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "2", label = "THE ROAD BACK", kind = "level", level = 2 },
        } },
      { kind = "roster" },
    } }
  end,
}))LUA";

} // namespace

// #207: a cleared `replay = true` row ARMS through both SDL level-set
// tails — the camp docket's and the zone submenu's — with the replay
// voice as the click's answer; a re-click of the (now CURRENT) armed row
// is exempt from the Unchanged refusal and keeps the FIRST origin.
TEST(CampaignZoneUi, replay_rows_arm_through_both_sdl_tails)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kReplayZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.completed_levels.clear();
    save.add_level_completed("gladiator", 2);
    screen* const game = test_screen();
    game->world().id = 1;
    ASSERT_TRUE(game->load_level());

    // --- The camp docket tail -------------------------------------------
    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(1u, zone.actions()[0].rows.size());
    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& camp_spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, camp_spec.on_spec_row);

    EXPECT_EQ(MENU_REDRAW,
              camp_spec.on_spec_row(kBaseCampZoneActionBase + 0, &state));
    EXPECT_EQ(2, save.scen_num) << "arming moves the cursor like a set";
    EXPECT_EQ(2, static_cast<int>(save.replay_level));
    EXPECT_EQ(1, static_cast<int>(save.replay_origin))
        << "origin = the cursor the player left";
    EXPECT_TRUE(trace_contains("zone", "level_replay_armed 2"));
    EXPECT_TRUE(trace_contains("zone", "toast Replaying"))
        << "the armed click answers in the replay voice";

    // Re-click the now-CURRENT armed row: exempt from the Unchanged
    // refusal, and the re-arm keeps the FIRST origin.
    EXPECT_EQ(MENU_REDRAW,
              camp_spec.on_spec_row(kBaseCampZoneActionBase + 0, &state));
    EXPECT_EQ(1, static_cast<int>(save.replay_origin))
        << "a re-arm keeps the origin, never the excursion cursor";
    EXPECT_FALSE(trace_contains("zone", "level_unchanged 2"))
        << "a replay row never answers 'Already on that level'";
    og::ui::install_base_camp_state_for_screen(nullptr);

    // --- The zone submenu tail ------------------------------------------
    save.clear_replay_arm();
    save.scen_num = 1;
    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at(""));
    ASSERT_EQ(1u, session.page().rows.size());
    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);
    const og::ui::MenuScreenSpec& sub_spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, sub_spec.on_spec_row);

    // The world is still parked on level 2 from the camp arm above, so
    // this click also covers the arm-without-reload branch.
    EXPECT_EQ(MENU_REDRAW, sub_spec.on_spec_row(0, &st));
    EXPECT_EQ(2, save.scen_num);
    EXPECT_EQ(2, static_cast<int>(save.replay_level))
        << "the submenu tail arms a cleared replay row too";
    EXPECT_EQ(1, static_cast<int>(save.replay_origin));

    og::ui::install_zone_submenu_state_for_screen(nullptr);
    save.clear_replay_arm();
}

// #207 arm lifecycle: the camp's PLAIN level row (no `replay = true`) is a
// plain cursor write, and a plain write abandons any excursion in flight —
// even one armed for the very level the row names. Without the clear, the
// stale arm would skip the purge this plain set promises and a later win
// would restore an abandoned origin.
TEST(CampaignZoneUi, plain_camp_level_set_abandons_the_replay_arm)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kPlainZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.completed_levels.clear();
    save.add_level_completed("gladiator", 2);
    screen* const game = test_screen();
    game->world().id = 1;
    ASSERT_TRUE(game->load_level());

    // An excursion in flight (armed for the same level the plain row sets).
    save.arm_replay(2);
    save.scen_num = 1;  // reopened camp, cursor re-pointed by the test
    ASSERT_EQ(2, static_cast<int>(save.replay_level));

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& camp_spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, camp_spec.on_spec_row);

    EXPECT_EQ(MENU_REDRAW,
              camp_spec.on_spec_row(kBaseCampZoneActionBase + 0, &state));
    EXPECT_EQ(2, save.scen_num) << "the plain set still writes the cursor";
    EXPECT_EQ(0, static_cast<int>(save.replay_level))
        << "the plain set must abandon the excursion";
    EXPECT_TRUE(trace_contains("zone", "level_set 2"))
        << "the click took the PLAIN branch, not the arm";

    og::ui::install_base_camp_state_for_screen(nullptr);
}

namespace {

// D3 Acted-level routing: a zone action whose result carries `level`. The
// spin uses og.campaign_random(1) — deterministically 1 on ANY provider,
// so the click exercises the SHIPPED wall-clock provider end to end — and
// the far wheel names a road the company has not earned, so its set
// refuses at the earned-roads gate.
constexpr const char* kRouletteScript = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        {
          kind = "actions",
          entries = {
            { id = "spin", label = "SPIN THE WHEEL", kind = "action" },
            { id = "spin_far", label = "FAR WHEEL", kind = "action" },
            { id = "spin_curt", label = "CURT WHEEL", kind = "action" },
          },
        },
        { kind = "roster" },
      },
    }
  end,
  picker_action = function(entry_id)
    if entry_id == "spin" then
      return { level = 1 + og.campaign_random(1),
               message = "The wheel spins." }
    end
    if entry_id == "spin_curt" then
      return { level = 15, message = "Sorry." }
    end
    return { level = 15, message = "No luck tonight." }
  end,
}))LUA";

} // namespace

// An Acted outcome carrying a level runs the SAME gated set tail as a
// level row click: on success the engine's "Level set to <title>." is the
// click's one answer (the action's own message is dropped); a set the
// earned-roads gate refuses speaks the refusal and only then the action's
// message.
TEST(CampaignZoneUi, zone_action_result_level_routes_the_gated_set_tail)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kRouletteScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.completed_levels.clear();
    // Level 2 is earned (a cleared road's replay); level 15 is not.
    save.add_level_completed("gladiator", 2);
    screen* const game = test_screen();
    game->world().id = 1;
    ASSERT_TRUE(game->load_level());

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(3u, zone.actions()[0].rows.size());

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.on_spec_row);

    // SPIN: og.campaign_random(1) + 1 = level 2 — earned, loads, commits.
    EXPECT_EQ(MENU_REDRAW,
              spec.on_spec_row(kBaseCampZoneActionBase + 0, &state));
    EXPECT_EQ(2, save.scen_num)
        << "the Acted-carried level commits through the set tail";
    EXPECT_TRUE(trace_contains("zone", "level_set 2"));
    EXPECT_TRUE(trace_contains("zone", "toast Level set to"))
        << "the engine toast speaks on a successful routed set";
    EXPECT_FALSE(trace_contains("zone", "toast The wheel spins."))
        << "the Lua message is dropped when the set lands — one click, "
           "one answer";
    EXPECT_TRUE(trace_contains("zone", "acted_autosave"))
        << "the routed action still runs the Acted persistence tail";

    // FAR WHEEL: level 15 is a road the company has not earned — the
    // routed set meets the same gate a level row would, and the action's
    // message keeps its slot only here, after the refusal.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampZoneActionBase + 1, &state));
    EXPECT_EQ(2, save.scen_num) << "a refused routed set never moves the "
                                   "cursor";
    EXPECT_TRUE(trace_contains("zone", "level_denied_gate 15"));
    // The toast is ONE slot with ONE timer. A second show_toast in the same
    // frame does not add a notice, it REPLACES one — the refusal used to be
    // written and then overwritten by the action's message, so the player
    // read "No luck tonight." and never learned the engine had refused. The
    // refusal leads the line, and a message that will not fit beside it is
    // dropped rather than cut in half.
    EXPECT_EQ("That road is not open yet.", state.toast)
        << "the refusal owns the line it shares with nothing else";
    EXPECT_FALSE(trace_contains("zone", "toast No luck tonight."))
        << "the pack's line must never speak in the refusal's place";

    // ... and a message short enough to share the slot rides after it, the
    // order the terminals print their two lines in.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampZoneActionBase + 2, &state));
    EXPECT_EQ(2, save.scen_num);
    EXPECT_EQ("That road is not open yet. Sorry.", state.toast)
        << "refusal first, then the roll's own word";

    og::ui::install_base_camp_state_for_screen(nullptr);
}

// D3 Acted-level routing at the SUBMENU dispatch site — the SDL client's
// fifth and last routed arm. A page-hosted action whose result carries
// `level` goes through zone_submenu_level_set_tail, the SAME gated tail a
// submenu level row's click takes: on a landed set the engine's "Level set
// to <arena>." is the click's one answer and the action's own message is
// dropped; a set the earned-roads gate refuses speaks the refusal and only
// then the message. Drives zone_submenu_on_spec_row directly (the
// host-gate test's pattern) on kZoneScript's stores page, whose DICE rows
// exist for exactly this test.
TEST(CampaignZoneUi, submenu_action_result_level_routes_the_gated_set_tail)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.completed_levels.clear();
    // Level 2 is earned (a cleared road's replay); level 15 is not.
    save.add_level_completed("gladiator", 2);
    screen* const game = test_screen();
    game->world().id = 1;
    ASSERT_TRUE(game->load_level());

    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ(5u, session.page().rows.size());
    ASSERT_EQ("dice", session.page().rows[3].id);
    ASSERT_EQ("dice_far", session.page().rows[4].id);

    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    // DICE: the carried level 2 is earned — the routed set commits, and
    // the engine's confirmation is the click's one answer.
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(3, &st));
    EXPECT_EQ(2, save.scen_num)
        << "the Acted-carried level commits through the submenu set tail";
    EXPECT_TRUE(trace_contains("zone", "level_set 2"));
    EXPECT_TRUE(trace_contains("zone", "toast Level set to"))
        << "the engine toast speaks on a successful routed set";
    EXPECT_FALSE(trace_contains("zone", "toast The dice land."))
        << "the Lua message is dropped when the set lands — one click, "
           "one answer";
    EXPECT_TRUE(trace_contains("zone", "acted_autosave"))
        << "the routed submenu action still runs the Acted persistence "
           "tail";

    // FAR DICE: level 15 is a road the company has not earned — the
    // routed set meets the same gate a submenu level row would, and the
    // action's message keeps its slot only here, after the refusal.
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(4, &st));
    EXPECT_EQ(2, save.scen_num)
        << "a refused routed set never moves the cursor";
    EXPECT_TRUE(trace_contains("zone", "level_denied_gate 15"));
    // One slot, one line (the Base Camp twin's rule, at this surface's
    // wider 41-glyph budget): "That road is not open yet. Dice gone cold."
    // is 42, so the refusal keeps the line and the roll's own words are
    // dropped whole. What may never happen is the shape this replaced —
    // the message overwriting the refusal in the same frame, leaving a
    // player who was refused reading only the pack's flavour.
    EXPECT_EQ("That road is not open yet.", st.toast)
        << "the refusal owns the line";
    EXPECT_FALSE(trace_contains("zone", "toast Dice gone cold."))
        << "a message that will not fit beside the refusal is dropped "
           "whole, never cut and never in its place";

    og::ui::install_zone_submenu_state_for_screen(nullptr);
}

// Fetch triggers 3 and 4 through the REAL frame hook: the level-reload
// guard firing (any scen_num source — a host SET LEVEL landing on a
// joiner) and an applied lobby-settings change both refetch the zone;
// a quiet frame does not (never per frame).
TEST(CampaignZoneUi, frame_tick_refetches_on_reload_guard_and_settings)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.frame_tick);

    // Settle: the first tick fires the entry reload (last_level_id == -1)
    // and seeds the settings fingerprint.
    ASSERT_TRUE(spec.frame_tick(&state, 1));
    trace_clear();

    // A quiet frame refetches nothing (the never-per-frame rule).
    ASSERT_TRUE(spec.frame_tick(&state, 2));
    EXPECT_FALSE(trace_contains("zone", "refetch"))
        << "a quiet frame must not dispatch Lua";

    // Trigger 3: the reload guard (any scen_num source).
    save.scen_num = 2;
    ASSERT_TRUE(spec.frame_tick(&state, 3));
    EXPECT_TRUE(trace_contains("zone", "refetch"))
        << "a landed level change must refetch the composition";
    trace_clear();

    // Trigger 4: an applied lobby-settings change (the poll rewrites the
    // synced knobs under the open screen). The fingerprint hashes twelve
    // knobs and a composition can read any of them, so one knob proving the
    // trigger would leave eleven that could silently drop out of the hash. Ride
    // a spread of them — a counted knob, a mode knob, a boolean toggle and
    // an allied-mode change — each on its own frame.
    struct SettingsKnob {
        const char* name;
        short SaveData::*field;
        // Two distinct settings; the flip picks whichever the save is not
        // already on, so the knob's shipped default cannot make the change
        // a no-op.
        short first;
        short second;
    };
    constexpr SettingsKnob kKnobs[] = {
        {"generator_rate", &SaveData::generator_rate, 200, 0},
        {"ctf_team_count", &SaveData::ctf_team_count, 3, 0},
        {"keep_fallen_heroes", &SaveData::keep_fallen_heroes, 1, 0},
        {"allied_mode", &SaveData::allied_mode, 1, 0},
        {"time_limit", &SaveData::time_limit, 7200, 0},
    };
    static_assert(std::size(kKnobs) >= 3,
                  "at least three hashed knobs must be proven live");
    int frame = 4;
    for (const SettingsKnob& knob : kKnobs) {
        const short restore = save.*(knob.field);
        const short target =
            restore == knob.first ? knob.second : knob.first;
        ASSERT_NE(restore, target) << knob.name << " does not change";
        trace_clear();
        save.*(knob.field) = target;
        ASSERT_TRUE(spec.frame_tick(&state, frame++));
        EXPECT_TRUE(trace_contains("zone", "refetch"))
            << "an applied " << knob.name
            << " change must refetch the composition";
        // ... and settle: restoring is itself a change, so the next knob
        // starts from a fingerprint that has already re-seeded.
        trace_clear();
        save.*(knob.field) = restore;
        ASSERT_TRUE(spec.frame_tick(&state, frame++));
        EXPECT_TRUE(trace_contains("zone", "refetch"))
            << knob.name << " must fire in both directions";
        trace_clear();
        ASSERT_TRUE(spec.frame_tick(&state, frame++));
        EXPECT_FALSE(trace_contains("zone", "refetch"))
            << "a quiet frame after " << knob.name << " must stay quiet";
    }

    og::ui::install_base_camp_state_for_screen(nullptr);
}

namespace {

// Fetch trigger 2 (own mutation) has five roster call sites. Three of them
// change something a composition can SEE, so this script echoes exactly
// those: the lead member's name (move-up reorders the list), its team
// (the classic chip cycler) and the count of sworn heroes (the assign
// cycler). A site that stops refetching leaves this line stale.
constexpr const char* kRosterEchoScript = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local team = og.campaign_team()
    local sworn = 0
    for i = 1, #team do
      if team[i].tag ~= 0 then sworn = sworn + 1 end
    end
    local lead = "-"
    if #team > 0 then
      lead = team[1].name .. "/" .. tostring(team[1].team)
    end
    return {
      widgets = {
        { kind = "text",
          lines = { "LEAD " .. lead .. " SWORN " .. tostring(sworn) } },
        { kind = "roster",
          assign = { key = "road", labels = { "WAR", "BURDEN" } } },
      },
    }
  end,
}))LUA";

// The same echo with no assign spec, so the chip cell is the CLASSIC team
// cycler (the assign fork short-circuits it).
constexpr const char* kRosterEchoSoloScript =
    R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local team = og.campaign_team()
    local lead = "-"
    if #team > 0 then
      lead = team[1].name .. "/" .. tostring(team[1].team)
    end
    return {
      widgets = {
        { kind = "text", lines = { "LEAD " .. lead } },
        { kind = "roster" },
      },
    }
  end,
}))LUA";

void seed_three_benched_soldiers(SaveData& save)
{
    for (auto& slot : save.team_list)
        slot.reset();
    const char* names[] = {"Alpha", "Beta", "Gamma"};
    for (std::size_t i = 0; i < 3; ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
        save.team_list[i]->deployed = false;
        save.team_list[i]->campaign_tag = 0;
    }
    save.team_size = 3;
    save.my_team = 0;
    save.campaign_state.clear();
}

const og::ui::MenuScreenSpec& team_build_spec()
{
    return *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
}

} // namespace

// Fetch trigger 2 (own mutation) at the roster sites: the assign chip, the
// move-up control, the deploy toggle and the nested-screen reset all
// refetch the composition. The first two are pinned by the echoed line
// (behavioral); deploy has no composition-visible field on
// CampaignRosterEntry today, so its refetch is pinned by the trace — a
// deliberate call: the site is insurance for the day a script can read
// `deployed`, and the pin keeps it from being deleted meanwhile.
TEST(CampaignZoneUi, roster_mutations_refetch_the_composition)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kRosterEchoScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    seed_three_benched_soldiers(save);

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.texts().size());
    ASSERT_EQ(1u, zone.texts()[0].lines.size());
    EXPECT_EQ("LEAD Alpha/0 SWORN 0", zone.texts()[0].lines[0]);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec = team_build_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    ASSERT_NE(nullptr, spec.on_reset);

    // The assign chip: the tag write must be followed by a refetch, or the
    // zone keeps describing an unsworn company.
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampTeamChipBase, &state));
    EXPECT_TRUE(trace_contains("zone", "assign slot=0 tag=1"));
    EXPECT_TRUE(trace_contains("zone", "refetch"));
    EXPECT_EQ("LEAD Alpha/0 SWORN 1", zone.texts()[0].lines[0])
        << "the assign site must refetch the composition";

    // Move-up on row 1 swaps Beta ahead of Alpha, so the lead changes.
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampMoveUpBase + 1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "move_up slot=1 to=0"));
    EXPECT_TRUE(trace_contains("zone", "refetch"));
    EXPECT_EQ("LEAD Beta/0 SWORN 1", zone.texts()[0].lines[0])
        << "the move-up site must refetch the composition";

    // Deploy: trace-only (see the note above).
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(0, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"));
    EXPECT_TRUE(trace_contains("zone", "refetch"))
        << "the deploy site must refetch the composition";

    // A nested screen (hire/train/zone submenu) may have changed the
    // roster or the book underneath us: the reset hook refetches too.
    trace_clear();
    spec.on_reset(&state);
    EXPECT_TRUE(trace_contains("zone", "refetch"))
        << "the reset site must refetch the composition";

    og::ui::install_base_camp_state_for_screen(nullptr);
}

// The fifth roster site: the CLASSIC team-color cycler (reachable only
// when the composition ships no assign spec — the assign fork owns the
// chip cell otherwise).
TEST(CampaignZoneUi, team_cycle_refetches_the_composition)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kRosterEchoSoloScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    seed_three_benched_soldiers(save);

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_FALSE(zone.roster().assign.active);
    ASSERT_EQ(1u, zone.texts().size());
    EXPECT_EQ("LEAD Alpha/0", zone.texts()[0].lines[0]);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec = team_build_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampTeamChipBase, &state));
    EXPECT_TRUE(trace_contains("basecamp", "team slot=0 team=1"));
    EXPECT_TRUE(trace_contains("zone", "refetch"));
    EXPECT_EQ("LEAD Alpha/1", zone.texts()[0].lines[0])
        << "the team-cycle site must refetch the composition";

    og::ui::install_base_camp_state_for_screen(nullptr);
}

namespace {

// A campaign with a BOOK but no base_camp hook: the transitional book-door
// composition puts one page row onto the book's root over the default
// roster, so a campaign scripted before the camps existed still reaches its
// book from the Base Camp — the only way into a book on any client.
constexpr const char* kBookOnlyScript = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return {
      title = "KETTLE'S BOOK",
      lines = { "The ledger lies open." },
      entries = {
        { id = "bread", label = "BREAD", kind = "action", cost = 10 },
      },
    }
  end,
  picker_action = function(entry_id)
    return { message = "Bread eaten." }
  end,
}))LUA";

struct BookDoorState
{
    bool finished = false;
    bool door_seen = false;
    bool submenu_opened = false;
    bool submenu_row_seen = false;
    bool returned_from_submenu = false;
};

int book_door_injector(void* data)
{
    og::runtime::ensure_thread_session();
    BookDoorState* state = static_cast<BookDoorState*>(data);

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // The door wears the book's OWN root title, so the campaign names its
    // book rather than the engine naming it.
    state->door_seen = wait_for_interactable_label(
        "zone_action_0", "KETTLE'S BOOK  >", 10000);
    SDL_Delay(400);

    interact("zone_action_0");
    state->submenu_opened = wait_for_interactable_at("back", 10, 169, 10000);
    state->submenu_row_seen =
        wait_for_interactable_label("zone_row_0", "BREAD  10g", 10000);
    SDL_Delay(300);
    interact("back");
    state->returned_from_submenu = wait_for_interactable_label(
        "zone_action_0", "KETTLE'S BOOK  >", 10000);
    SDL_Delay(300);

    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, book_without_a_zone_opens_through_the_camp_door)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kBookOnlyScript);
    write_save0_with_two_soldiers("gladiator", 1);

    BookDoorState state;
    SDL_Thread* thread = SDL_CreateThread(
        book_door_injector, "book_door", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.door_seen)
        << "a campaign with a book but no camp must still show its door";
    EXPECT_TRUE(state.submenu_opened)
        << "the door opens the zone submenu on the book's ROOT page";
    EXPECT_TRUE(state.submenu_row_seen);
    EXPECT_TRUE(state.returned_from_submenu)
        << "BACK at the book's root resumes the Base Camp";
    // The roster underneath keeps every capability (the door composition is
    // the default roster plus one row).
    EXPECT_TRUE(trace_contains("zone", "page_row "))
        << "the door dispatches as a page-kind zone row";
}

namespace {

// A docket that does not fit its band: five rows weighed two units. The
// shipped camps are composed NOT to reach this state, but a camp that does
// has to say so — two bare arrows tell a player a row can move, never that
// rows are hidden, so the pager's gutter carries the "p/N" count.
constexpr const char* kPagedDocketScript =
    R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions", weight = 2,
          entries = {
            { id = "one", label = "ROW ONE", kind = "action" },
            { id = "two", label = "ROW TWO", kind = "action" },
            { id = "three", label = "ROW THREE", kind = "action" },
            { id = "four", label = "ROW FOUR", kind = "action" },
            { id = "five", label = "ROW FIVE", kind = "action" },
          } },
        { kind = "roster" },
      },
    }
  end,
  picker_action = function(entry_id)
    return { message = "Noted." }
  end,
}))LUA";

struct PagedDocketState {
    bool finished = false;
    bool first_window = false;
    bool window_is_two_rows = false;
    bool pager_shown = false;
    bool second_window = false;
    bool wrapped_home = false;
};

int paged_docket_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PagedDocketState* state = static_cast<PagedDocketState*>(data);

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    state->first_window =
        wait_for_interactable_label("zone_action_0", "ROW ONE", 10000);
    state->window_is_two_rows = has_interactable("zone_action_1") &&
        !has_interactable("zone_action_2");
    state->pager_shown = wait_for_interactable("zone_pager_next_0", 5000);
    SDL_Delay(400);
    capture_zone_frame("uxr_docket_pager_page1");

    interact("zone_pager_next_0");
    state->second_window =
        wait_for_interactable_label("zone_action_0", "ROW THREE", 10000);
    SDL_Delay(400);
    capture_zone_frame("uxr_docket_pager_page2");

    interact("zone_pager_prev_0");
    state->wrapped_home =
        wait_for_interactable_label("zone_action_0", "ROW ONE", 10000);

    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, an_overflowing_docket_pages_in_place_and_counts_itself)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kPagedDocketScript);
    write_save0_with_two_soldiers("gladiator", 1);

    PagedDocketState state;
    SDL_Thread* thread =
        SDL_CreateThread(paged_docket_injector, "paged_docket", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    verify_zone_shots("docket_pager", 2);

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.first_window) << "the band renders its first window";
    EXPECT_TRUE(state.window_is_two_rows)
        << "a two-unit band shows two rows and parks the rest";
    EXPECT_TRUE(state.pager_shown)
        << "an overflowing band grows its pager pair";
    EXPECT_TRUE(state.second_window)
        << "the pager pages the docket IN PLACE, never onto a new screen";
    EXPECT_TRUE(state.wrapped_home) << "and back again";
}

namespace {

// The zz tour: walk the composition every shipped campaign renders on its
// Base Camp — the bare default for a campaign with no hooks at all, the
// book door for one that scripts only a book, its own camp for one that
// composes base_camp — asserting each one really renders and capturing it
// for the UXSHOTS read-back.
struct DefaultTourState
{
    const char* campaign;
    const char* shot;
    short scen_num;
    // Zone action rows this campaign's Base Camp shows in its FIRST window:
    // 0 with no hooks at all (the appended rows stay parked), 1 for the
    // transitional book door, and the composed band's own row count for a
    // campaign that scripts base_camp (an overflowing docket pages in
    // place, so this is the window, not the docket).
    int zone_rows = 0;
    // When set, the top zone row must carry exactly this composed label —
    // the count cannot be satisfied by the wrong composition.
    const char* expect_first_row_label = nullptr;
    // Levels this company has already won. A camp whose docket names the
    // road ahead only once the fight at its feet is won needs a save that
    // has won one.
    std::vector<int> completed;
    bool finished = false;
    bool continue_seen = false;
    bool camp_seen = false;
    bool hire_labeled = false;
    bool roster_row_seen = false;
    bool zone_rows_as_expected = false;
    bool first_row_labeled = false;
    bool go_seen = false;
};

int default_tour_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DefaultTourState* state = static_cast<DefaultTourState*>(data);

    state->continue_seen = wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Observations, not a postscript: a Base Camp that never renders,
    // renders blank, or renders someone else's composition must FAIL the
    // tour, not pass it because the injector reached its last line.
    state->camp_seen = wait_for_interactable("hire_troops", 10000);
    state->hire_labeled =
        wait_for_interactable_label("hire_troops", "HIRE", 5000);
    state->roster_row_seen = wait_for_interactable("roster_row_0", 5000);
    // Exactly the rows this campaign composes, and not one more: a bare
    // default parks every zone row, a book door shows one, and a scripted
    // camp fills its band's window.
    state->zone_rows_as_expected = true;
    for (int r = 0; r < state->zone_rows; r++)
    {
        const std::string id = "zone_action_" + std::to_string(r);
        if (!wait_for_interactable(id.c_str(), 5000))
            state->zone_rows_as_expected = false;
    }
    const std::string past_end =
        "zone_action_" + std::to_string(state->zone_rows);
    if (has_interactable(past_end.c_str()))
        state->zone_rows_as_expected = false;
    // ...and what the top row SAYS, so the count cannot be satisfied by
    // the wrong composition (see expect_first_row_label).
    state->first_row_labeled =
        state->expect_first_row_label == nullptr ||
        wait_for_interactable_label("zone_action_0",
                                    state->expect_first_row_label, 5000);
    SDL_Delay(500);
    capture_zone_frame(state->shot);

    state->go_seen = wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, zz_capture_default_zone_across_campaigns)
{
    trace_clear();
    SavedPickerSave save_guard;

    // Every shipped campaign gets its Base Camp walked in real SDL — the
    // bare default, the transitional book door, or a composed camp — and a
    // campaign whose mount left the screen empty must be caught here.
    DefaultTourState tours[] = {
        {"gladiator", "zone_default_gladiator", 1, 0, nullptr, {}},
        // The Gamesmaster's table composes GAME / FIELD / RANDOM SCENARIO
        // / MATCH SETUP — all FOUR on the panel's first face. The camp
        // spends no text line precisely so that its last row is not parked
        // behind a pager arrow on the screen a player lives on.
        {"modes", "zone_default_modes", 300, 4, nullptr, {}},
        // The Company Fire composes its camp: at the vale that is the fight
        // at your feet plus the QUARTERMASTER and THE LEDGER doors (the
        // road out is named only on the night it opens).
        {"westlands", "zone_camp_westlands", 1, 3, nullptr, {}},
        // ...and the night the fight at its feet is won: the fork above the
        // Refuge names BOTH roads out, so the docket outgrows the band and
        // the C++ pager takes over the third slot. This is the only night
        // the camp ever shows an arrow, which is the point of capturing it.
        {"westlands", "zone_camp_westlands_fork", 4, 3, nullptr,
         {1, 2, 3, 4}},
        // The open ledger composes its camp: the week's job, the STORES
        // door, and TAKE AN ADVANCE on a fresh spring save.
        {"longseason", "zone_camp_longseason", 1, 3, nullptr, {}},
        // The dream log composes its camp: one dream on a fresh save, and
        // its label pins the composition (not just the count).
        {"imaginations", "zone_default_imaginations", 1, 1,
         "The Raspberry Isle - tonight?  [CURRENT]", {}},
    };
    for (DefaultTourState& tour : tours)
    {
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(tour.campaign))
            << tour.campaign;
        write_save0_with_two_soldiers(tour.campaign, tour.scen_num,
                                      tour.completed);

        SDL_Thread* thread = SDL_CreateThread(
            default_tour_injector, "default_tour", &tour);
        ASSERT_NE(nullptr, thread);
        g_picker_mainmenu_calls = 0;
        g_picker_max_mainmenu_calls = 1;
        picker_main(0, nullptr);
        SDL_WaitThread(thread, nullptr);
        cleanup_picker_state();
        g_picker_max_mainmenu_calls = 0;

        verify_zone_shots(tour.campaign, 1);

        EXPECT_TRUE(tour.continue_seen) << tour.campaign << ": main menu";
        EXPECT_TRUE(tour.camp_seen)
            << tour.campaign << ": Base Camp must render";
        EXPECT_TRUE(tour.hire_labeled)
            << tour.campaign << ": HIRE must carry its label";
        EXPECT_TRUE(tour.roster_row_seen)
            << tour.campaign << ": the roster band must render its rows";
        EXPECT_TRUE(tour.zone_rows_as_expected)
            << tour.campaign << ": expected exactly " << tour.zone_rows
            << " zone action row(s) — a composed camp fills its band, a "
               "book keeps its door, and a campaign with no hooks parks "
               "every appended row";
        EXPECT_TRUE(tour.first_row_labeled)
            << tour.campaign << ": the top zone row must read '"
            << (tour.expect_first_row_label != nullptr
                    ? tour.expect_first_row_label
                    : "")
            << "'";
        EXPECT_TRUE(tour.go_seen)
            << tour.campaign << ": the command strip must render";
        EXPECT_TRUE(tour.finished) << tour.campaign;
    }

    // Leave the default campaign mounted for whatever runs next.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// --- TEMPORARY UX-REVIEW CAPTURE (not for commit) -------------------------
namespace {

void uxr_write_save0_with_many(const std::string& campaign, short scen_num)
{
    SaveData& save = test_screen()->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    const char* names[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon",
                           "Zeta",  "Eta",  "Theta", "Iota",  "Kappa",
                           "Lambda", "Mu",  "Nu",    "Xi"};
    for (std::size_t i = 0; i < 14; ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
        save.team_list[i]->deployed = (i % 2) == 0;
        save.team_list[i]->campaign_tag = 0;
    }
    save.team_size = 14;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.m_totalcash[0] = 5000;
    save.campaign_state.clear();
    save.save_name = "IRON KETTLE BAND";
    ASSERT_TRUE(save.save("save0"));
}

int uxr_big_roster_injector(void* data)
{
    og::runtime::ensure_thread_session();
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");
    wait_for_interactable_label("zone_action_0", "STORES", 10000);
    SDL_Delay(600);
    capture_zone_frame("uxr_big_roster_p1");
    // 14 heroes over the scripted zone's 3-row roster band: the pager is not
    // optional here, and a fixture that stopped paging would silently drop
    // the second still.
    *static_cast<bool*>(data) = has_interactable("roster_page_next");
    interact("roster_page_next");
    SDL_Delay(500);
    capture_zone_frame("uxr_big_roster_p2");
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    return 0;
}

} // namespace

TEST(CampaignZoneUi, zzz_uxr_capture_scripted_zone_with_full_roster)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);
    uxr_write_save0_with_many("gladiator", 1);

    bool roster_pages = false;
    SDL_Thread* thread =
        SDL_CreateThread(uxr_big_roster_injector, "uxr_big", &roster_pages);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(roster_pages)
        << "14 heroes over a 3-row band must show the roster pager";
    verify_zone_shots("uxr_full_roster", 2);
}

namespace {

// A knob row's face is "<label> - <note>" once the panel joins the two, so
// these waits key on the part the knob owns rather than the whole line.
bool wait_for_interactable_label_containing(const std::string& id,
                                            const std::string& want,
                                            int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden &&
                item.label.find(want) != std::string::npos)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' label ~'%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// MATCH SETUP, the modes camp's page of knobs: four rows that each read
// out what the match holds and step it on when clicked. The page replaced a
// row of named presets, so the still that documents it has to show the
// VALUES on the faces — and the later captures have to show them actually
// moving, since a page of labels that never change would look exactly the
// same from a screenshot. TEAMS retired with lineup amendment A1/A3 and
// TROOPS with B5, and amendment 5 (G2/G3) seats TEAMS and FILL back on
// top as MACROS over the per-team fill array: the page is now TEAMS and
// FILL over TARGET SCORE and the clock (#241). The macro shot steps TEAMS
// to 2 (dealing the lowest opponent FAIR) and FILL one stop past that
// FAIR face to STRONG; the clock still gets its own shot — MAP is the
// map's own limit, 5M is a host overriding it.
struct MatchSetupShotState
{
    bool camp_seen = false;
    bool setup_row_seen = false;
    bool page_opened = false;
    bool teams_row_read_one = false;
    bool fill_row_read_none = false;
    bool teams_stepped_to_two = false;
    bool fill_stepped_to_strong = false;
    bool score_row_read_map = false;
    bool score_row_stepped_to_one = false;
    bool time_row_read_map = false;
    bool time_row_stepped_to_five = false;
    bool finished = false;
};

int match_setup_injector(void* data)
{
    og::runtime::ensure_thread_session();
    MatchSetupShotState* state = static_cast<MatchSetupShotState*>(data);

    state->camp_seen = wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Row 3 of the Gamesmaster's four: GAME, FIELD, RANDOM SCENARIO, and
    // the door this shot is about.
    state->setup_row_seen = wait_for_interactable_label_containing(
        "zone_action_3", "MATCH SETUP", 10000);
    SDL_Delay(400);
    interact("zone_action_3");

    // The zone submenu's own BACK owns the unique (10,169) rect. At rest
    // the two macro rows lead the page (amendment 5): TEAMS: 1 — the
    // derived all-NONE face — over FILL: NONE, then the two knobs at MAP.
    state->page_opened = wait_for_interactable_at("back", 10, 169, 10000);
    state->teams_row_read_one = wait_for_interactable_label_containing(
        "zone_row_0", "TEAMS: 1", 10000);
    state->fill_row_read_none = wait_for_interactable_label_containing(
        "zone_row_1", "FILL: NONE", 10000);
    state->score_row_read_map = wait_for_interactable_label_containing(
        "zone_row_2", "TARGET SCORE: MAP", 10000);
    state->time_row_read_map = wait_for_interactable_label_containing(
        "zone_row_3", "TIME LIMIT: MAP", 10000);
    SDL_Delay(500);
    capture_zone_frame("zone_submenu_match_setup");

    // The macros move: one TEAMS click deals the lowest opponent FAIR
    // (both faces re-derive from the one fill array), and one FILL click
    // steps that FAIR face to STRONG.
    interact("zone_row_0");
    state->teams_stepped_to_two = wait_for_interactable_label_containing(
        "zone_row_0", "TEAMS: 2", 10000);
    SDL_Delay(400);
    interact("zone_row_1");
    state->fill_stepped_to_strong = wait_for_interactable_label_containing(
        "zone_row_1", "FILL: STRONG", 10000);
    SDL_Delay(400);
    capture_zone_frame("uxr_match_setup_macros");
    SDL_Delay(400);

    // One click walks the score cycle one stop (map -> 1) and speaks it.
    interact("zone_row_2");
    state->score_row_stepped_to_one = wait_for_interactable_label_containing(
        "zone_row_2", "TARGET SCORE: 1", 10000);
    SDL_Delay(400);
    capture_zone_frame("uxr_match_setup_cycled");
    SDL_Delay(400);

    // The clock: a fresh match wears MAP — the limit the level's own
    // manifest authored — and one click hands the host the shortest
    // override the cycle offers.
    interact("zone_row_3");
    state->time_row_stepped_to_five = wait_for_interactable_label_containing(
        "zone_row_3", "TIME LIMIT: 5M", 10000);
    SDL_Delay(400);
    capture_zone_frame("uxr_match_setup_time");
    SDL_Delay(400);

    interact("back");
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignZoneUi, zzz_uxr_capture_modes_match_setup_page)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_save0_with_two_soldiers("modes", 300);

    MatchSetupShotState state;
    SDL_Thread* thread =
        SDL_CreateThread(match_setup_injector, "uxr_setup", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    verify_zone_shots("match_setup", 4);

    EXPECT_TRUE(state.camp_seen) << "main menu";
    EXPECT_TRUE(state.setup_row_seen)
        << "the Gamesmaster's fourth row is the MATCH SETUP door";
    EXPECT_TRUE(state.page_opened)
        << "the MATCH SETUP row must open the zone submenu";
    EXPECT_TRUE(state.teams_row_read_one)
        << "the macro rows lead the page (amendment 5), and the all-NONE "
           "rest derives TEAMS: 1";
    EXPECT_TRUE(state.fill_row_read_none)
        << "FILL: NONE is the resting face of the second macro row";
    EXPECT_TRUE(state.teams_stepped_to_two)
        << "one TEAMS click deals the lowest opponent and re-derives the "
           "face";
    EXPECT_TRUE(state.fill_stepped_to_strong)
        << "one FILL click steps the dealt FAIR face to STRONG";
    EXPECT_TRUE(state.score_row_read_map)
        << "a fresh match plays to the map's own target, and the row that "
           "leads the page now says so on its face";
    EXPECT_TRUE(state.score_row_stepped_to_one)
        << "clicking a knob row steps its cycle and re-labels the row";
    EXPECT_TRUE(state.time_row_read_map)
        << "a fresh match runs the map's own clock, and the TIME LIMIT row "
           "says MAP on its face";
    EXPECT_TRUE(state.time_row_stepped_to_five)
        << "clicking the clock row steps it to the cycle's shortest "
           "override (5M)";
    EXPECT_TRUE(state.finished);

    // Leave the default campaign mounted for whatever runs next.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}
