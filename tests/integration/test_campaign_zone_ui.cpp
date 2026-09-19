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
#include "test_camp_save_fixture.h"
#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
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
// The presenter pause handshake these flows capture through is declared in
// tests/test_frame_capture.h.

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
        // The arena deal memo (lineup amendment 7) must not leak a dealt
        // cursor into the next fixture either.
        snapshot_fields.arena_lineup_dealt_campaign =
            save.arena_lineup_dealt_campaign;
        snapshot_fields.arena_lineup_dealt_scen = save.arena_lineup_dealt_scen;

        // ...and, once everything above is safely snapshotted, the
        // PROCESS-WIDE lobby. The standalone picker lobby client caches the
        // settings every cycler stamps into it
        // (picker_lobby_sync_settings_from_save), nothing in a test binary
        // tears it down, and its apply writes that cached campaign_id back
        // over save.current_campaign and REMOUNTS it — rebuilding the
        // pack-script registry underneath a scripted book. Re-stamping it
        // from the save this flow inherits is enough, and is far cheaper
        // than shutting the singleton down (a shutdown makes the next lobby
        // use rebuild the server, its peers and the mount).
        picker_lobby_sync_settings_from_save();
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
        save.arena_lineup_dealt_campaign =
            snapshot_fields.arena_lineup_dealt_campaign;
        save.arena_lineup_dealt_scen = snapshot_fields.arena_lineup_dealt_scen;
        for (int t = 0; t < 4; ++t)
            save.m_totalcash[t] = snapshot_fields.m_totalcash[t];
        save.campaign_state = campaign_state;
    }
};

// Save/restore the pack-script registry around a synthetic registration.
// The gladiator campaign is mounted and the save0 load inside picker_main
// never re-walks the registry from disk, so the synthetic chunk survives an
// ordinary flow. It does NOT survive a remount of a DIFFERENT package: that
// rebuilds the registry and the chunk is gone, which is how a stale lobby
// campaign stamp used to turn a scripted zone into the default composition
// (see roster_mutations_survive_a_stale_lobby_settings_stamp). The chunk
// name deliberately does NOT start with `packs/` (the pack-Lua coverage
// inventory rule).
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

// The capture handshake, the ledger, the nonblank bar and the main-thread
// verification moved VERBATIM to tests/test_frame_capture.h when the
// menu-capture scenes needed the same rules: one implementation, not five
// (PR #245). Every call site below resolves this suite's own output
// directory (UXSHOTS_DIR) and hands it to the shared capture.

// write_save0_with_two_soldiers moved VERBATIM to
// tests/test_camp_save_fixture.h when the SETUP wizard's flows in this same
// binary wanted the same starting company: one fixture, not a twin that
// drifts the day one copy gains a field.

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
    -- Direct-drive fixtures below; no interactive flow opens these pages,
    -- so no existing row index moves. "long" overflows the 8-row window
    -- for the pager tests, "roads" carries one deliberately over-budget
    -- level label for the host-marker tests, and "void" is the page the
    -- book refuses to hand back at all.
    if page_id == "long" then
      local entries = {}
      for i = 1, 12 do
        entries[i] = { id = "shelf" .. i, label = "SHELF " .. i,
                       kind = "action" }
      end
      return { title = "LONG SHELF", entries = entries }
    end
    if page_id == "roads" then
      return {
        title = "ROADS",
        entries = {
          { id = "milestone", kind = "level", level = 1,
            label = "ROADROADROADROADROADROADROADROADROADROADROADRO" },
        },
      }
    end
    if page_id == "void" then
      return nil
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
    bool cycler_edges_acknowledged = true;
};

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
    capture_presented_frame("zone_scripted_camp", std::getenv("UXSHOTS_DIR"));

    // Deploy-lock refusal: Alpha starts DEPLOYED, so bench first (allowed —
    // locks gate the toggle-ON only), then the re-deploy refuses with the
    // toast while the hero is unassigned.
    state->cycler_edges_acknowledged &=
        click_and_acknowledge_trace("roster_dep_0", "basecamp",
                                    "deploy slot=0 off");
    capture_presented_frame("uxr_after_bench", std::getenv("UXSHOTS_DIR"));
    state->cycler_edges_acknowledged &= click_and_acknowledge_trace(
        "roster_dep_0", "zone", "deploy_locked slot=0",
        /*waits_for_autosave=*/false);
    SDL_Delay(150);
    capture_presented_frame("uxr_lock_toast", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(300);

    // The assign chip: unset -> WAR (undeployed cycle rides the autosave
    // tail), then WAR -> BURDEN.
    state->cycler_edges_acknowledged &=
        click_and_acknowledge_trace("roster_team_0", "zone",
                                    "assign slot=0 tag=1");
    SDL_Delay(150);
    capture_presented_frame("uxr_assign_war", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(300);
    state->cycler_edges_acknowledged &=
        click_and_acknowledge_trace("roster_team_0", "zone",
                                    "assign slot=0 tag=2");
    SDL_Delay(150);
    capture_presented_frame("uxr_assign_burden", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(300);

    // Assigned heroes clear the unset-lock: the deploy sticks now.
    state->cycler_edges_acknowledged &=
        click_and_acknowledge_trace("roster_dep_0", "basecamp",
                                    "deploy slot=0 on");

    // Without the kit the road row points at a level that is not there:
    // the load-with-rollback failure arm toasts and restores the cursor.
    state->ghost_row_seen = wait_for_interactable_label(
        "zone_action_2", "SCEN 9999  [CLOSED]", 5000);
    SDL_Delay(300);
    interact("zone_action_2");
    SDL_Delay(200);
    capture_presented_frame("uxr_level_fail_toast", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(600);

    // The costed action: debit + state write-through + refetch retire (the
    // row stops quoting a price and stops dispatching).
    interact("zone_action_1");
    state->kit_label_flipped =
        wait_for_interactable_label("zone_action_1", "FIELD KIT  [DONE]",
                                    5000);
    capture_presented_frame("uxr_kit_toast", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(300);

    // Clicking a retired purchase refuses in the campaign's voice instead
    // of charging again.
    interact("zone_action_1");
    SDL_Delay(200);
    capture_presented_frame("uxr_kit_done_toast", std::getenv("UXSHOTS_DIR"));
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
    capture_presented_frame("uxr_level_current", std::getenv("UXSHOTS_DIR"));
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
    capture_presented_frame("zone_submenu_stores", std::getenv("UXSHOTS_DIR"));
    // The purchase confirms exactly as it does at the root: a non-modal
    // message-line toast, no OK button to dismiss.
    interact("zone_row_0");
    SDL_Delay(400);
    capture_presented_frame("uxr_submenu_after_buy", std::getenv("UXSHOTS_DIR"));
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
    capture_presented_frame("uxr_submenu_level_fail", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(600);

    interact("back");
    state->returned_from_submenu =
        wait_for_interactable_label("zone_action_0", "STORES  >", 10000);
    state->cycler_edges_acknowledged &=
        run_on_main_thread([] { reset_mouse_click_tracking(); });
    SDL_Delay(150);
    capture_presented_frame("uxr_back_at_root", std::getenv("UXSHOTS_DIR"));
    SDL_Delay(300);

    // Cycling a DEPLOYED hero first un-deploys through the full roster
    // tail (ready clears — correct), then the tag applies: Alpha is
    // deployed with tag BURDEN, so this cycle benches her and swears WAR.
    state->cycler_edges_acknowledged &=
        click_and_acknowledge_trace("roster_team_0", "zone",
                                    "assign slot=0 tag=1");

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
    verify_captured_frames("scripted_zone_flow", 13);

    SaveData& save = test_screen()->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
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
    EXPECT_TRUE(state.cycler_edges_acknowledged)
        << "every roster cycler must finish its press/release before the next";
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
    bool deploy_edges_acknowledged = true;
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
    capture_presented_frame("zone_default_camp", std::getenv("UXSHOTS_DIR"));

    // Deploy toggle by id (the classic flow).
    state->deploy_edges_acknowledged &=
        click_and_acknowledge_trace("roster_dep_0", "basecamp",
                                    "deploy slot=0 off");
    // The production row deliberately debounces the same deploy slot for
    // 250 ms after an accepted toggle; wait past that rule before toggling it
    // back on. This is product behavior, not an injector-settle delay.
    SDL_Delay(300);
    state->deploy_edges_acknowledged &=
        click_and_acknowledge_trace("roster_dep_0", "basecamp",
                                    "deploy slot=0 on");

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

    verify_captured_frames("default_zone_flow", 1);

    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.hire_seen)
        << "the default zone renders HIRE (id hire_troops)";
    EXPECT_TRUE(state.train_opened)
        << "the row-body train door survives the zone split";
    EXPECT_TRUE(state.hire_opened)
        << "HIRE still opens the hire screen from its header-band rect";
    EXPECT_TRUE(state.deploy_edges_acknowledged)
        << "both deploy toggles must finish before the train-door click";
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 off"));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"));
}

// Teeth for the trace ladder: the first deploy press evaporates, and the flow
// still lands both toggles — with exactly one retry, and no double-toggle
// (the second edge, "deploy slot=0 on", proves the first one landed once).
TEST(CampaignZoneUi, deploy_toggle_survives_a_dropped_press)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    write_save0_with_two_soldiers("gladiator", 1);

    g_click_ladder_trace_click_retries = 0;
    g_click_ladder_ack_post_retries = 0;
    g_click_ladder_ack_drops = 0;
    g_click_ladder_click_drops = 1;

    DefaultZoneFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        default_zone_injector, "default_zone_drop", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    // The injector this shares with default_zone_keeps_the_classic_roster_flows
    // records a capture, and the shot ledger is process-wide: answer for it
    // here or the next verifying flow inherits it.
    verify_captured_frames("default_zone_drop", 1);

    EXPECT_EQ(0, g_click_ladder_click_drops) << "the injected drop must be consumed";
    EXPECT_TRUE(state.deploy_edges_acknowledged)
        << "a dropped press must cost a retry, not the toggle";
    EXPECT_EQ(1, g_click_ladder_trace_click_retries)
        << "exactly one press left no trace and was re-sent";
    EXPECT_TRUE(state.finished);
}

// The other tooth, for the other half of the ladder: the press lands and
// traces, but its acknowledgement — the pointer reset posted back to the
// menu thread — is cancelled unrun. That is the mode seen under load at
// CampaignZoneUi.default_zone_keeps_the_classic_roster_flows: the deploy
// autosave leaves the menu thread unpumped past the ceiling, the post is
// cancelled while still queued, and the toggle was reported unacknowledged.
// Re-posting the reset is toggle-safe, so the flow still finishes both
// toggles — with exactly one extra post and NO extra press. The re-post
// COUNT is what this pins: the click's own verdict no longer depends on a
// clock (a fully stalled thread leaves the reset queued instead), so the
// counter is the teeth.
TEST(CampaignZoneUi, deploy_toggle_survives_a_cancelled_acknowledge)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    write_save0_with_two_soldiers("gladiator", 1);

    g_click_ladder_trace_click_retries = 0;
    g_click_ladder_ack_post_retries = 0;
    g_click_ladder_click_drops = 0;
    g_click_ladder_ack_drops = 1;

    DefaultZoneFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        default_zone_injector, "default_zone_ack_drop", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    // Shared injector, process-wide shot ledger: answer for the capture here
    // or the next verifying flow inherits it.
    verify_captured_frames("default_zone_ack_drop", 1);

    EXPECT_EQ(0, g_click_ladder_ack_drops)
        << "the injected cancellation must be consumed";
    EXPECT_TRUE(state.deploy_edges_acknowledged)
        << "a cancelled acknowledge must cost a re-post, not the toggle";
    EXPECT_EQ(1, g_click_ladder_ack_post_retries)
        << "exactly one acknowledge post was re-sent";
    EXPECT_EQ(0, g_click_ladder_trace_click_retries)
        << "the press itself registered: it must never be re-pressed";
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 off"));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"));
    EXPECT_TRUE(state.finished);
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

// Every refusal the submenu's shared level-set tail can give, on the same
// page and through the same dispatch a click takes. Each one is one line on
// the message strip and NO cursor movement — a book that quietly moved the
// level under a refused click would launch the wrong arena at GO.
TEST(CampaignZoneUi, zone_submenu_level_refusals_speak_and_move_no_cursor)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.my_team = 0;
    save.scen_num = 1;
    save.completed_levels.clear();
    save.add_level_completed("gladiator", 2);
    screen* const game = test_screen();
    game->world().id = 1;
    ASSERT_TRUE(game->load_level());

    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ(5u, session.page().rows.size());
    ASSERT_EQ("ghost", session.page().rows[2].id);
    ASSERT_EQ(9999, session.page().rows[2].level);
    ASSERT_EQ("dice", session.page().rows[3].id);

    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    // 1. HOST GATE. A joiner's click on a level row is refused before the
    // loader is ever consulted: the GHOST row points at a level that does
    // not exist, and the joiner is still told the HOST line, not the
    // closed-road one. The gate is the first thing in the tail for a
    // reason — a joiner must never learn about the host's level list by
    // watching which rows fail differently.
    {
        JoinerZoneLobbyClient lobby;
        og::ui::IPickerLobbyClient* const saved_client =
            og::ui::active_picker_lobby_client();
        og::ui::install_active_picker_lobby_client(&lobby);
        EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(2, &st));
        og::ui::install_active_picker_lobby_client(saved_client);

        EXPECT_EQ(1, save.scen_num)
            << "a joiner's level click must not move the cursor";
        EXPECT_EQ(1, game->world().id)
            << "and must not load anything either";
        EXPECT_TRUE(trace_contains("zone", "level_denied_nonhost 9999"));
        EXPECT_EQ(std::string(og::ui::kCampaignPickerHostGuardMessage),
                  st.toast);
    }

    // The paired control: the SAME row as HOST gets the campaign's own
    // closed-road voice from the load-with-rollback arm instead.
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(2, &st));
    EXPECT_EQ(1, save.scen_num);
    EXPECT_EQ(std::string(og::ui::kCampaignLevelClosedMessage), st.toast)
        << "as host the same row answers with the loader's refusal, in the "
           "campaign's voice";

    // 2. ALREADY THERE. DICE answers with level 2, which is earned, so the
    // first click commits...
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(3, &st));
    EXPECT_EQ(2, save.scen_num);
    ASSERT_EQ(2, game->world().id);

    // ...and the second click on the same row is refused as unchanged. The
    // refusal owns the line: "Already on that level. GO when ready." plus
    // the roll's own "The dice land." is 52 glyphs against a 41-glyph
    // strip, so the pack's flavour is dropped whole rather than cut, and
    // never in the refusal's place.
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(3, &st));
    EXPECT_EQ(2, save.scen_num) << "an unchanged set moves nothing";
    EXPECT_TRUE(trace_contains("zone", "level_unchanged 2"));
    EXPECT_EQ(std::string(og::ui::kCampaignLevelUnchangedMessage), st.toast);
    EXPECT_FALSE(trace_contains("zone", "toast The dice land."))
        << "the pack's line must never speak in the refusal's place";

    og::ui::install_zone_submenu_state_for_screen(nullptr);
}

// A purchase the company cannot afford must cost it nothing and must not
// run the persistence tail: a refused BREAD that still debited (or still
// autosaved a half-applied state) is a shop that charges for nothing.
TEST(CampaignZoneUi, zone_submenu_refused_purchase_debits_nothing)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.my_team = 0;
    save.scen_num = 1;
    save.m_totalcash[0] = 0;

    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at("stores"));
    ASSERT_EQ("bread", session.page().rows[0].id);
    ASSERT_EQ(10, session.page().rows[0].cost);
    ASSERT_FALSE(session.page().rows[0].affordable);

    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(0, &st));
    EXPECT_EQ(0u, save.m_totalcash[0]) << "a refused purchase debits nothing";
    EXPECT_EQ("Not enough gold.", st.toast);
    EXPECT_TRUE(trace_contains("zone", "refused Not enough gold."));
    EXPECT_FALSE(trace_contains("zone", "acted_autosave"))
        << "a refusal never runs the Acted persistence tail";
    EXPECT_FALSE(trace_contains("zone", "toast Bread eaten."))
        << "and the book's own line never speaks for a purchase that did "
           "not happen";

    // The paired control: with coin in the purse the same row buys.
    trace_clear();
    save.m_totalcash[0] = 100;
    session.refresh();
    ASSERT_TRUE(session.page().rows[0].affordable);
    EXPECT_EQ(MENU_REDRAW, spec.on_spec_row(0, &st));
    EXPECT_EQ(90u, save.m_totalcash[0]) << "the accepted purchase debits 10";
    EXPECT_EQ("Bread eaten.", st.toast);
    EXPECT_TRUE(trace_contains("zone", "acted_autosave"));

    og::ui::install_zone_submenu_state_for_screen(nullptr);
}

// The submenu pagers step the 8-row window and SATURATE at both ends: a
// PREV at page 0 (or a NEXT on the last page) is not a page change, so it
// must not trace one and must not move the window. A pager that wrapped —
// or that traced a flip it never made — makes a long shelf unreadable.
TEST(CampaignZoneUi, zone_submenu_pagers_step_and_saturate)
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

    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at("long"));
    ASSERT_EQ(12u, session.page().rows.size());

    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);
    ASSERT_TRUE(st.page.multi_page());
    ASSERT_EQ(0, st.page.first_index());

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    // PREV on the first page: no move, no flip trace.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kZoneSubmenuPrevIndex, &st));
    EXPECT_EQ(0, st.page.first_index());
    EXPECT_EQ(0, count_trace_containing("zone", "submenu_page"));

    // NEXT: the window moves to rows 8..11 and the flip is announced.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kZoneSubmenuNextIndex, &st));
    EXPECT_EQ(8, st.page.first_index());
    EXPECT_EQ(12, st.page.end_index());
    EXPECT_EQ(1, count_trace_containing("zone", "submenu_page"));
    EXPECT_TRUE(trace_contains("zone", "submenu_page 2/2"));

    // NEXT on the last page: saturated, same silence as PREV at the top.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kZoneSubmenuNextIndex, &st));
    EXPECT_EQ(8, st.page.first_index());
    EXPECT_EQ(1, count_trace_containing("zone", "submenu_page"));

    // PREV comes home.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kZoneSubmenuPrevIndex, &st));
    EXPECT_EQ(0, st.page.first_index());
    EXPECT_EQ(2, count_trace_containing("zone", "submenu_page"));
    EXPECT_TRUE(trace_contains("zone", "submenu_page 1/2"));

    og::ui::install_zone_submenu_state_for_screen(nullptr);
}

namespace {

// A camp whose only docket row is a door to a page the book will not hand
// back: the Base Camp half of the unreadable-page rule.
constexpr const char* kVoidPageZoneScript = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "void", label = "THE VOID", kind = "page" },
        } },
      { kind = "roster" },
    } }
  end,
  picker_menu = function(page_id)
    if page_id == "void" then
      return nil
    end
    return { title = "SOMEWHERE", entries = {} }
  end,
}))LUA";

} // namespace

// A page door the book refuses to open must say so on the message line and
// build no screen at all. The refusal is deliberately NOT a modal: a modal
// here strands a networked joiner mid-GO behind an OK button nobody else
// can see.
TEST(CampaignZoneUi, unreadable_page_speaks_and_opens_no_screen)
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

    // The fixture's own control: only "void" is unreadable — "stores" on
    // the same book opens fine, so the refusal below is the page, not the
    // registration.
    {
        og::ui::CampaignPickerSession probe(save);
        EXPECT_TRUE(probe.open_at("stores"));
        EXPECT_FALSE(probe.open_at("void"));
    }

    // The blocking wrapper never reaches its screen: `opened` starts true
    // (what the real caller passes), and nothing is installed in the
    // engine's live button table.
    clear_allbuttons();
    bool opened = true;
    EXPECT_EQ(MENU_REDRAW, og::ui::run_campaign_zone_submenu("void", &opened));
    EXPECT_FALSE(opened) << "the wrapper must report the failed open";
    EXPECT_TRUE(trace_contains("zone", "submenu_unreadable void"));
    EXPECT_EQ(nullptr, og::runtime::current_session->allbuttons_[0])
        << "a page that cannot be read builds no screen";

    // ...and the Base Camp caller turns that report into the message-line
    // toast the player actually reads.
    SyntheticCampaignScriptGuard::install(kVoidPageZoneScript);
    trace_clear();
    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(1u, zone.actions()[0].rows.size());
    ASSERT_EQ("void", zone.actions()[0].rows[0].id);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& camp =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, camp.on_spec_row);
    EXPECT_EQ(MENU_REDRAW,
              camp.on_spec_row(kBaseCampZoneActionBase + 0, &state));
    EXPECT_EQ(std::string(og::ui::kCampaignPageUnreadableMessage),
              state.toast);
    EXPECT_TRUE(trace_contains("zone", "page_row void"));

    og::ui::install_base_camp_state_for_screen(nullptr);
}

namespace {

// A camp docket whose single row is a level with a deliberately over-budget
// label, for the Base Camp half of the host-marker rule.
constexpr const char* kLongRoadZoneScript = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "milestone", kind = "level", level = 1,
            label = "ROADROADROADROADROADROADROADROADROADROADROADRO" },
        } },
      { kind = "roster" },
    } }
  end,
}))LUA";

} // namespace

// A joiner cannot set the level, so every level row it can see says so —
// and the marker is paid for out of the row's OWN label budget, never added
// past the face. A marker bolted on top would push the last glyphs of every
// long road name outside the bevel on both sides (labels are drawn centered
// and unclipped). Both surfaces that draw level rows answer the same way.
TEST(CampaignZoneUi, joiner_level_rows_pay_for_the_host_marker_out_of_the_label)
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

    og::ui::CampaignPickerSession session(save);
    ASSERT_TRUE(session.open_at("roads"));
    ASSERT_EQ(1u, session.page().rows.size());
    ASSERT_EQ(46u, session.page().rows[0].label.size())
        << "the fixture label must overrun both budgets";
    ASSERT_TRUE(session.page().rows[0].available);
    ASSERT_TRUE(session.page().rows[0].current)
        << "the [CURRENT] tail is part of the budget being measured";

    og::ui::ZoneSubmenuScreenState st;
    st.session = &session;
    st.page = og::ui::PageModel::make(
        static_cast<int>(session.page().rows.size()),
        kZoneSubmenuRowsPerPage);
    og::ui::install_zone_submenu_state_for_screen(&st);

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.nav.rewire);
    button* const buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kZoneSubmenuBackIndex;

    // JOINER first, from a freshly initialized live surface.
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, count);
    ASSERT_NE(nullptr, og::runtime::current_session->allbuttons_[0]);
    const unsigned char resting_face =
        og::runtime::current_session->allbuttons_[0]->color;
    {
        JoinerZoneLobbyClient lobby;
        og::ui::IPickerLobbyClient* const saved_client =
            og::ui::active_picker_lobby_client();
        og::ui::install_active_picker_lobby_client(&lobby);
        spec.nav.rewire(buttons, count, highlighted);
        og::ui::install_active_picker_lobby_client(saved_client);
    }
    EXPECT_EQ("ROADROADROADROADROADROADROAD..  [CURRENT] (HOST)",
              buttons[0].label);
    EXPECT_EQ(kZoneSubmenuRowLabelChars, buttons[0].label.size())
        << "the marked label still fits the face exactly";
    EXPECT_EQ(buttons[0].label,
              og::runtime::current_session->allbuttons_[0]->label)
        << "both label surfaces carry the marker";
    EXPECT_EQ(resting_face,
              og::runtime::current_session->allbuttons_[0]->color)
        << "a row the joiner cannot activate never wears the GO face";

    // HOST: the same row, same face width, seven more glyphs of road name.
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, count);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("ROADROADROADROADROADROADROADROADROA..  [CURRENT]",
              buttons[0].label);
    EXPECT_EQ(kZoneSubmenuRowLabelChars, buttons[0].label.size());
    EXPECT_EQ(buttons[0].label,
              og::runtime::current_session->allbuttons_[0]->label);
    EXPECT_EQ(og::ui::kReadyGoFaceGo,
              og::runtime::current_session->allbuttons_[0]->color)
        << "an actionable level row wears the green launch face";

    og::ui::install_zone_submenu_state_for_screen(nullptr);

    // The Base Camp docket band is the same rule at its own 42-glyph face.
    SyntheticCampaignScriptGuard::install(kLongRoadZoneScript);
    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(1u, zone.actions()[0].rows.size());

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& camp =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, camp.nav.rewire);
    button* const camp_buttons = camp.buttons_accessor();
    const int camp_count = camp.count_accessor();
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ =
        init_buttons(camp_buttons, camp_count);
    int camp_highlight = camp.default_highlight;

    {
        JoinerZoneLobbyClient lobby;
        og::ui::IPickerLobbyClient* const saved_client =
            og::ui::active_picker_lobby_client();
        og::ui::install_active_picker_lobby_client(&lobby);
        camp.nav.rewire(camp_buttons, camp_count, camp_highlight);
        og::ui::install_active_picker_lobby_client(saved_client);
    }
    EXPECT_EQ("ROADROADROADROADROADRO..  [CURRENT] (HOST)",
              camp_buttons[kBaseCampZoneActionBase].label);

    camp.nav.rewire(camp_buttons, camp_count, camp_highlight);
    EXPECT_EQ("ROADROADROADROADROADROADROADR..  [CURRENT]",
              camp_buttons[kBaseCampZoneActionBase].label)
        << "the host reads seven more glyphs of the same road name";

    og::ui::install_base_camp_state_for_screen(nullptr);
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
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
    // A refetch that lost the scripted book falls back to the DEFAULT
    // composition, which carries no Text widget at all: read texts()[0]
    // unguarded and the empty vector hands back freed TextLayout storage
    // (the og_test_matchup SIGSEGV). Name the fallback instead.
    ASSERT_EQ(1u, zone.texts().size())
        << "a base-camp mutation must not swap the scripted composition "
           "for the default";
    EXPECT_EQ("LEAD Alpha/0 SWORN 1", zone.texts()[0].lines[0])
        << "the assign site must refetch the composition";

    // Move-up on row 1 swaps Beta ahead of Alpha, so the lead changes.
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampMoveUpBase + 1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "move_up slot=1 to=0"));
    EXPECT_TRUE(trace_contains("zone", "refetch"));
    ASSERT_EQ(1u, zone.texts().size())
        << "a base-camp mutation must not swap the scripted composition "
           "for the default";
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

// The same roster mutation, run against a lobby whose cached settings still
// name a DIFFERENT campaign. The standalone picker lobby client is a
// process-wide singleton whose settings are stamped by
// picker_lobby_sync_settings_from_save() — the tail of every settings
// cycler (change_ctf_caps, set_difficulty, ...) — and nothing in a test
// binary ever tears it down. A stamp left behind by an earlier flow reaches
// this mutation through picker_base_camp_after_roster_mutation's lobby
// sync, whose apply writes settings.campaign_id back over save.current_campaign
// and REMOUNTS that package; the remount rebuilds the pack-script registry,
// the scripted book vanishes, and the zone silently falls back to the
// default composition. Pinning it here keeps the mutation tail honest
// whatever the lobby is holding.
TEST(CampaignZoneUi, roster_mutations_survive_a_stale_lobby_settings_stamp)
{
    trace_clear();

    // Staged BEFORE the fixture, because that is where it comes from: an
    // EARLIER test's settings cycle. This is the exact shape
    // src/interface/ui/picker.cpp change_ctf_caps leaves behind — sync the
    // lobby under a foreign campaign, then restore only the save FIELD.
    {
        SaveData& live = test_screen()->save_data;
        const std::string before = live.current_campaign;
        live.current_campaign = "modes";
        picker_lobby_sync_settings_from_save();
        live.current_campaign = before;
    }

    SaveData& save = test_screen()->save_data;
    SavedPickerSave save_guard;

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    save.current_campaign = "gladiator";
    ASSERT_EQ("gladiator", get_mounted_campaign());

    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kRosterEchoScript);
    save.scen_num = 1;
    seed_three_benched_soldiers(save);

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec = team_build_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampTeamChipBase, &state));
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampMoveUpBase + 1, &state));

    ASSERT_EQ(1u, zone.texts().size())
        << "a base-camp mutation must not swap the scripted composition "
           "for the default";
    EXPECT_EQ("gladiator", get_mounted_campaign())
        << "the mutation tail must not remount a stale lobby campaign";
    ASSERT_EQ(1u, zone.texts()[0].lines.size());
    EXPECT_EQ("LEAD Beta/0 SWORN 1", zone.texts()[0].lines[0]);

    og::ui::install_base_camp_state_for_screen(nullptr);
}

namespace {

// The same LEAD echo, over a roster whose reorder/deploy controls the
// composition may retire. The two flags are the ONLY difference between
// the locked and unlocked fixtures below.
std::string roster_lock_script(const char* controls_live)
{
    return std::string(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local team = og.campaign_team()
    local lead = "-"
    if #team > 0 then lead = team[1].name end
    return {
      widgets = {
        { kind = "text", lines = { "LEAD " .. lead } },
        { kind = "roster", can_reorder = )LUA") +
        controls_live + ", can_deploy = " + controls_live + R"LUA( },
      },
    }
  end,
}))LUA";
}

} // namespace

// A composition that retires the reorder or deploy control hides its face —
// but the click that was already in flight when the composition changed
// (a lobby poll can swap the book under the open Base Camp) still reaches
// the dispatcher. That stale click must be INERT: a retired control that
// still reordered the company, or still stood a hero up, would undo the
// state the book just took away, silently and without a trace.
TEST(CampaignZoneUi, retired_roster_controls_are_inert_for_a_stale_click)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;

    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    seed_three_benched_soldiers(save);

    const og::ui::MenuScreenSpec& spec = team_build_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    // --- Locked composition: both controls retired. ---
    SyntheticCampaignScriptGuard::install(
        roster_lock_script("false").c_str());
    og::ui::CampaignZoneSession locked(save);
    locked.fetch();
    ASSERT_TRUE(locked.scripted());
    ASSERT_FALSE(locked.roster().can_reorder);
    ASSERT_FALSE(locked.roster().can_deploy);
    ASSERT_EQ("LEAD Alpha", locked.texts()[0].lines[0]);

    og::ui::BaseCampScreenState locked_state;
    locked_state.zone = &locked;
    og::ui::base_camp_refresh_rows(locked_state);
    og::ui::install_base_camp_state_for_screen(&locked_state);

    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampMoveUpBase + 1, &locked_state));
    EXPECT_EQ("Alpha", save.team_list[0]->name)
        << "a retired MOVE UP must not reorder the company";
    EXPECT_EQ("Beta", save.team_list[1]->name);
    EXPECT_FALSE(trace_contains("basecamp", "move_up"))
        << "and must not announce a move it did not make";
    EXPECT_EQ("LEAD Alpha", locked.texts()[0].lines[0]);

    EXPECT_EQ(MENU_OK, spec.on_spec_row(0, &locked_state));
    EXPECT_FALSE(save.team_list[0]->deployed)
        << "a retired deploy toggle must not stand a hero up";
    EXPECT_FALSE(trace_contains("basecamp", "deploy"))
        << "and must not announce a toggle it did not make";

    og::ui::install_base_camp_state_for_screen(nullptr);

    // --- The paired control: the SAME two dispatches on a composition
    // that keeps both controls do exactly what the player asked. ---
    SyntheticCampaignScriptGuard::install(
        roster_lock_script("true").c_str());
    og::ui::CampaignZoneSession open_roster(save);
    open_roster.fetch();
    ASSERT_TRUE(open_roster.scripted());
    ASSERT_TRUE(open_roster.roster().can_reorder);
    ASSERT_TRUE(open_roster.roster().can_deploy);

    og::ui::BaseCampScreenState open_state;
    open_state.zone = &open_roster;
    og::ui::base_camp_refresh_rows(open_state);
    og::ui::install_base_camp_state_for_screen(&open_state);

    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampMoveUpBase + 1, &open_state));
    EXPECT_EQ("Beta", save.team_list[0]->name)
        << "the live MOVE UP reorders the company";
    EXPECT_EQ("Alpha", save.team_list[1]->name);
    EXPECT_TRUE(trace_contains("basecamp", "move_up slot=1 to=0"));
    EXPECT_EQ("LEAD Beta", open_roster.texts()[0].lines[0]);

    EXPECT_EQ(MENU_OK, spec.on_spec_row(0, &open_state));
    EXPECT_TRUE(save.team_list[0]->deployed)
        << "the live deploy toggle stands the hero up";
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 on"));

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
    // D28's classic half, pinned beside its versus twin
    // (versus_docket_page_rows_open_the_wizard_not_the_submenu): a page
    // row on a CLASSIC campaign keeps the chassis it has always had. The
    // wizard is the versus campaigns' alone, and its shortcut trace must
    // be nowhere near this flow.
    EXPECT_FALSE(trace_contains("setup", "docket_shortcut"))
        << "the SETUP wizard must not answer a classic campaign's book";
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
    capture_presented_frame("uxr_docket_pager_page1", std::getenv("UXSHOTS_DIR"));

    interact("zone_pager_next_0");
    state->second_window =
        wait_for_interactable_label("zone_action_0", "ROW THREE", 10000);
    SDL_Delay(400);
    capture_presented_frame("uxr_docket_pager_page2", std::getenv("UXSHOTS_DIR"));

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

    verify_captured_frames("docket_pager", 2);

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.first_window) << "the band renders its first window";
    EXPECT_TRUE(state.window_is_two_rows)
        << "a two-unit band shows two rows and parks the rest";
    EXPECT_TRUE(state.pager_shown)
        << "an overflowing band grows its pager pair";
    EXPECT_TRUE(state.second_window)
        << "the pager pages the docket IN PLACE, never onto a new screen";
    EXPECT_TRUE(state.wrapped_home) << "and back again";

    // ...and COUNTS itself. state.pager_shown only proves the two arrows
    // exist; the gutter strip under them prints
    // ActionsLayout::page.indicator() for every multi-page band of 2+ units
    // (src/interface/ui/menu_screen_specs.cpp, the docket-pager gutter
    // loop). Compose the same docket the flow just paged and pin the count
    // that strip has to ink -- without this, deleting the strip (the very
    // thing the comment above kPagedDocketScript says two bare arrows cannot
    // replace) left every expectation above green.
    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.scripted());
    ASSERT_EQ(1u, zone.actions().size());
    og::ui::CampaignZoneSession::ActionsLayout band = zone.actions()[0];
    EXPECT_EQ(2, band.units) << "weight 2 buys a two-row band";
    EXPECT_EQ(5u, band.rows.size()) << "all five authored rows are carried";
    ASSERT_TRUE(band.page.multi_page())
        << "five rows in a two-row window overflow";
    EXPECT_EQ(3, band.page.page_count())
        << "five rows across a two-row window is three pages";
    EXPECT_EQ(std::string("1/3"), band.page.indicator())
        << "the gutter must open on page one of three";
    ASSERT_TRUE(band.page.step(1));
    EXPECT_EQ(std::string("2/3"), band.page.indicator())
        << "one NEXT moves the printed count with the window";
    ASSERT_TRUE(band.page.step(1));
    EXPECT_EQ(std::string("3/3"), band.page.indicator());
    EXPECT_FALSE(band.page.step(1))
        << "the count saturates on the last page instead of wrapping";
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
    capture_presented_frame(state->shot, std::getenv("UXSHOTS_DIR"));

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
        // The arena docket composes GAME: / ARENA: / RANDOM ARENA —
        // THREE rows on the panel's first face. The fourth left with #304:
        // the retired page's four knob rows were the camp's only rules
        // digest,
        // and the whole match is stated on the wizard's MATCH step now,
        // one click from the strip. The camp still spends no text line, so
        // its last row is never parked behind a pager arrow on the screen
        // a player lives on.
        {"modes", "zone_default_modes", 300, 3, nullptr, {}},
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

        verify_captured_frames(tour.campaign, 1);

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

// --- ZONE_SHOTS: the big-roster pager stills -------------------------------
//
// Not a temporary UX-review scratch, whatever the fence here used to say:
// uxr_big_roster_p1 and uxr_big_roster_p2 are two of the ZONE_SHOTS
// scripts/media/capture_campaign_scripting.sh lists (:118-119) and it
// "refuses to finish with any of them missing". This flow is the only place
// the roster pager is driven with a company big enough to need it, so the
// two stills — page 1 and page 2 of a 14-hero band — come from here or from
// nowhere.
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

// Everything the flow observed, read back on the main thread: an EXPECT
// raised from an injector that then dies mid-flow takes its message with it,
// so this file's capture flows all report this way.
struct UxrBigRosterState {
    bool stores_seen = false;      // the camp composed its STORES door
    bool pager_enabled = false;    // the roster pager is live, not inert
    bool page_flipped = false;     // the '>' press moved the window
    std::string page_indicator;    // the newest "page p/N" the pager spoke
    bool finished = false;         // the flow came back to the camp strip
    // Set by the MAIN thread after picker_main returns. The escape tail has
    // no wall-clock bound on purpose: picker_main blocks until a click takes
    // it out, so a tail that stopped trying early would guarantee the wedge
    // it exists to prevent.
    std::atomic<bool> test_finished{false};
    // Point one leg at an id the flow never publishes, so the give-up path
    // itself is testable.
    int sabotage_leg = 0;
};

// Poll tick, never a settle: the trace edge below is a wait-on-condition.
constexpr int kUxrTracePollMs = 50;
// The sabotaged leg waits for an id nothing publishes; nothing is coming, and
// the point of that run is the tail.
constexpr int kUxrSabotageWaitMs = 500;

// The newest "basecamp"/"page ..." trace message, "" when the pager has not
// spoken. The pager cluster's rows are drawn as bare arrows with no label to
// wait on (menu_screen_specs.cpp:2839-2851), so the ONLY witness a page flip
// publishes is TRACE("basecamp", "page %s", indicator) at :5284 — and the
// indicator is "{page+1}/{page_count}".
std::string newest_basecamp_page_trace()
{
    const std::lock_guard<std::mutex> lock(g_trace_mutex);
    for (auto entry = g_trace_buffer.rbegin(); entry != g_trace_buffer.rend();
         ++entry) {
        if (entry->category == "basecamp" &&
            entry->message.find("page ") != std::string::npos)
            return entry->message;
    }
    return std::string();
}

int uxr_big_roster_injector(void* data)
{
    og::runtime::ensure_thread_session();
    UxrBigRosterState* state = static_cast<UxrBigRosterState*>(data);

    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    // -- Leg 1: the main menu is up --
    if (!wait_for_interactable(
            state->sabotage_leg == 1 ? "never_published_door" : "continue_game",
            state->sabotage_leg == 1 ? kUxrSabotageWaitMs : 5000) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published continue_game");
    // The composed Page-kind face: the book's word plus the door grammar the
    // row text appends (two spaces, then '>' —
    // campaign_picker_row_text in src/interface/ui/campaign_picker_session.cpp,
    // pinned at tests/unit/test_campaign_picker_session.cpp:562-564). The bare
    // "STORES" this wait used to ask for never arrives: the wait burned its
    // whole 10 s ceiling on every green run and the result was thrown away.
    state->stores_seen = click_until_edge(
        "continue_game",
        [](int wait_ms) {
            return wait_for_interactable_label("zone_action_0", "STORES  >",
                                               wait_ms);
        },
        nullptr, 3, 10000);
    if (!state->stores_seen)
        return escape(
            2, "CONTINUE never composed the scripted camp's STORES door");
    (void)wait_for_menu_frames(2);
    capture_presented_frame("uxr_big_roster_p1", std::getenv("UXSHOTS_DIR"));

    // 14 heroes over the scripted zone's 3-row roster band: the pager is not
    // optional here. The row itself is ALWAYS published — an inert
    // placeholder, because base_camp_page_row_state
    // (menu_screen_specs.cpp:2310) answers Disabled rather than Hidden for a
    // single-page roster so HIRE keeps its home — so only the ENABLED
    // question can tell a paging fixture from one that quietly stopped
    // paging and would drop the second still.
    state->pager_enabled = has_enabled_interactable("roster_page_next");

    const int pages_before = count_trace_containing("basecamp", "page ");
    state->page_flipped = click_until_edge(
        "roster_page_next",
        [pages_before](int wait_ms) {
            int elapsed = 0;
            while (elapsed < wait_ms) {
                if (count_trace_containing("basecamp", "page ") > pages_before)
                    return true;
                SDL_Delay(static_cast<Uint32>(kUxrTracePollMs));
                elapsed += kUxrTracePollMs;
            }
            return false;
        },
        nullptr, 3, 10000);
    state->page_indicator = newest_basecamp_page_trace();
    (void)wait_for_menu_frames(2);
    capture_presented_frame("uxr_big_roster_p2", std::getenv("UXSHOTS_DIR"));

    state->finished = wait_for_interactable("go", 10000);
    if (!state->finished)
        return escape(
            3, "the camp's command strip never came back after the pager");
    (void)wait_for_menu_frames(2);

    // The exit, and the only loop with no bound: picker_main blocks on the
    // main thread and only a click makes it return, so the final `back` is
    // driven from here rather than through the ladder — after picker_main
    // returns there is no pump left to service an acknowledge post.
    return escape(0, "");
}

} // namespace

// The give-up half of the same injector, and the reason every leg above
// routes through the shared tail: picker_main blocks on the MAIN thread and
// only a click makes it return. Replace `return escape(1, ...)` with a bare
// `return 1` and this test hangs until the CTest ceiling instead of failing.
//
// No synthetic zone script is installed here -- the sabotaged leg never gets
// past the main menu, so the tail walks the stock campaign's
// CONTINUE -> base camp -> BACK and nothing downstream of leg 1 runs.
TEST(CampaignZoneUi, a_leg_that_gives_up_frees_the_main_thread)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    // Base camp needs a company to open, so the tail has a door to press.
    uxr_write_save0_with_many("gladiator", 1);

    UxrBigRosterState state;
    state.sabotage_leg = 1;
    SDL_Thread* thread =
        SDL_CreateThread(uxr_big_roster_injector, "uxr_big_escape", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true, std::memory_order_release);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(1, thread_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_FALSE(state.stores_seen)
        << "a flow that gave up at leg 1 never reached the STORES door";
    EXPECT_FALSE(state.pager_enabled)
        << "a flow that gave up at leg 1 never reached the roster pager";
    EXPECT_FALSE(state.page_flipped)
        << "a flow that gave up at leg 1 never pressed the pager's '>'";
    EXPECT_EQ("", state.page_indicator)
        << "a flow that gave up at leg 1 never heard the pager speak";
    EXPECT_FALSE(state.finished)
        << "a flow that gave up at leg 1 never reached the camp's strip";
}

TEST(CampaignZoneUi, zzz_uxr_capture_scripted_zone_with_full_roster)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kZoneScript);
    uxr_write_save0_with_many("gladiator", 1);

    UxrBigRosterState state;
    SDL_Thread* thread =
        SDL_CreateThread(uxr_big_roster_injector, "uxr_big", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true, std::memory_order_release);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    // The escape tail clicks into a queue nobody reads once picker_main is
    // out; leave nothing behind for the next test's first frame.
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    EXPECT_TRUE(state.stores_seen)
        << "the scripted camp composes its STORES page door as 'STORES  >'";
    EXPECT_TRUE(state.pager_enabled)
        << "14 heroes over a 3-row band must ENABLE the roster pager";
    EXPECT_TRUE(state.page_flipped)
        << "the '>' press must move the roster window";
    // 14 heroes over the scripted zone's 3-row band (the roster takes the 8
    // unit budget's remainder after the hoisted readout, the one text line
    // and the three action rows, less its own header) is ceil(14/3) = 5
    // pages, and one '>' press lands on the second of them.
    EXPECT_EQ("page 2/5", state.page_indicator)
        << "one pager click steps the 14-hero roster exactly one page";
    EXPECT_TRUE(state.finished)
        << "the flow must come back to the camp's command strip";
    verify_captured_frames("uxr_full_roster", 2);
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

} // namespace


namespace {

// Index of the FIRST matching trace, or -1. The trace buffer is append-only
// and ordered, so two indices compare as "this happened before that" —
// which is the whole claim of the test below.
int first_trace_index(const char* category, const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    for (std::size_t i = 0; i < g_trace_buffer.size(); ++i) {
        if (g_trace_buffer[i].category == category &&
            g_trace_buffer[i].message.find(substring) != std::string::npos)
            return static_cast<int>(i);
    }
    return -1;
}

// Walk into the camp and straight back out: the flow exists for the ORDER
// of what the entry does, not for anything clicked inside it.
int camp_entry_order_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const finished = static_cast<bool*>(data);

    (void)wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);  // fadeblack eats events; the only settle left here
    (void)interact("continue_game");
    (void)wait_for_interactable_label_containing("zone_action_1",
                                                 "ARENA:", 10000);
    (void)wait_for_interactable("go", 10000);
    SDL_Delay(300);
    (void)interact("back");
    *finished = true;
    return 0;
}

} // namespace

namespace {

// D28: on a VERSUS campaign the docket's page rows are shortcuts INTO the
// SETUP wizard, positioned at the page they name. On every other campaign
// they still open the zone submenu over the book. The two chassis are told
// apart by what comes up, not by what the click looked like.
struct DocketShortcutState
{
    std::atomic<bool> test_finished{false};
    bool camp_seen = false;
    bool row_seen = false;
    bool wizard_opened = false;
    bool submenu_opened = false;
    bool finished = false;
};

int versus_docket_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DocketShortcutState*>(data);
    const auto escape = [state](int leg, const char* why) {
        static constexpr EscapeDoor kSetupDoors[] = {
            {"setup_back", "setup_back"},
            {"back", "back"},
            {"go", "back"},
            {"continue_game", "continue_game"},
        };
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupDoors);
    };

    state->camp_seen = wait_for_interactable("continue_game", 10000);
    if (!state->camp_seen)
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");

    state->row_seen = wait_for_interactable("zone_action_0", 15000);
    if (!state->row_seen)
        return escape(2, "the versus docket never composed its GAME: row");
    trace_clear();
    state->wizard_opened = click_until_edge(
        "zone_action_0",
        [](int wait_ms) {
            return wait_for_interactable("setup_tab_0", wait_ms);
        },
        "docket_shortcut", 3, 10000, "setup");
    if (!state->wizard_opened)
        return escape(3, "the GAME: row did not open the wizard");
    state->submenu_opened = trace_contains("zone", "submenu_opened");

    (void)click_until_edge("setup_back", [](int wait_ms) {
        return wait_for_interactable("go", wait_ms);
    });
    state->finished = true;
    return escape(0, "");
}

} // namespace

TEST(CampaignZoneUi, versus_docket_page_rows_open_the_wizard_not_the_submenu)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_save0_with_two_soldiers("modes", 820);

    DocketShortcutState state;
    SDL_Thread* thread =
        SDL_CreateThread(versus_docket_injector, "versus_docket", &state);
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

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.wizard_opened)
        << "the versus docket's GAME: row must land on the wizard's GAME "
           "step — two doors from one screen into the same pages on two "
           "chassis is the clutter #304 names";
    EXPECT_FALSE(state.submenu_opened)
        << "the zone submenu must not open on a versus campaign's page row";
    EXPECT_TRUE(trace_contains("setup", "docket_shortcut games"))
        << "the shortcut names the page it carries";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// The camp's ENTRY composition must read a save the arena deal has already
// dealt (amendment 7, #276).
//
// create_team_menu composes the zone once at screen entry (fetch trigger 1)
// and only then runs the loop, whose FIRST frame_tick holds the level-reload
// guard that loads the arena and deals its FILL: FAIR bands. Everything the
// entry composed — the camp's own rows and, through them, the step a
// docket door opens — therefore read an UNDEALT save, and a click
// dispatched on the loop's first iteration (dispatch runs before
// frame_tick) opens that step before the deal ever lands. On a fast box the injector's 50 ms poll
// never wins that race; on the instrumented four-slot CI runners of PR #291
// it won every time — the retired
// CampaignZoneUi.zzz_uxr_capture_modes_match_setup_page (its page is the
// SETUP wizard's TEAMS step now, #304)
// read TEAMS: 1 / FILL: NONE and failed 3/3 attempts in BOTH the Coverage
// (run 34684325469) and the ASan (run 34684325459) lane, with the deal's
// autosave appearing in the log only after the page was closed again.
// That page is the SETUP wizard's TEAMS step now (#304) and the docket's
// own ARENA: row is what this flow waits on, but the ORDER it pins is the
// same one and for the same reason: the wizard's first composition reads
// the save the entry left behind.
//
// Ordered traces, not a clock: the deal must be recorded before the entry
// fetch it feeds.
TEST(CampaignZoneUi, base_camp_entry_deals_the_arena_before_it_composes)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE CIRCLE (scen 300) authors all four teams: a fresh save on that
    // cursor is exactly one deal away from TEAMS: 4 / FILL: FAIR.
    write_save0_with_two_soldiers("modes", 300);

    bool finished = false;
    SDL_Thread* thread =
        SDL_CreateThread(camp_entry_order_injector, "camp_entry", &finished);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    const int deal_index = first_trace_index("lineup", "arena_deal");
    const int fetch_index = first_trace_index("zone", "entry_fetch");
    EXPECT_TRUE(finished);
    ASSERT_NE(-1, deal_index)
        << "the arena deal must run on a versus campaign's fresh cursor";
    ASSERT_NE(-1, fetch_index)
        << "create_team_menu composes the zone once on entry";
    EXPECT_LT(deal_index, fetch_index)
        << "the camp's entry composition read a save whose arena FILL deal "
           "had not been dealt yet: the deal belongs to screen ENTRY, not to "
           "the first frame tick, or a door activated on the loop's first "
           "iteration serves an undealt SETUP: TEAMS step";

    // Leave the default campaign mounted for whatever runs next.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

namespace {

// Open the SETUP wizard through the ladder and come straight back out. The
// press the flow starts with is dropped on purpose
// (g_click_ladder_click_drops), so only a retry can reach the screen.
int setup_retry_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* opened = static_cast<bool*>(data);

    (void)wait_for_interactable("continue_game", 5000);
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    (void)wait_for_interactable("setup", 10000);

    *opened = click_until_edge(
        "setup",
        [](int wait_ms) {
            return wait_for_interactable("setup_tab_0", wait_ms);
        },
        "opened", 3, 10000, "setup");

    if (*opened) {
        (void)click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        });
    }
    (void)interact("back");
    return 0;
}

// The same ladder, pointed at a button that is not on this screen: it must
// spend its three attempts and REPORT, never hang against the group's
// 420 s budget.
int setup_wrong_id_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* reached = static_cast<bool*>(data);

    (void)wait_for_interactable("continue_game", 5000);
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    (void)wait_for_interactable("setup", 10000);

    // No witness: a press that is nowhere near a button cannot land, so
    // this ladder is allowed to spend every attempt on a fresh press.
    *reached = click_until_edge(
        "zone_action_99_not_a_row",
        [](int wait_ms) {
            return wait_for_interactable("setup_tab_0", wait_ms);
        },
        nullptr, 3, 500);

    (void)interact("back");
    return 0;
}

struct BlindCyclerState
{
    // false: the press names its landing with a trace (the wizard's own
    // TRACE("setup", "turned <knob>"), written by the dispatch before the
    // face is republished).
    // true: the same press with NO witness at all — the ladder then has only
    // its re-check-before-re-press to keep the wheel from overshooting.
    bool witnessless = false;
    bool opened = false;
    bool stepped = false;
    bool wheel_still_on_two = false;
};

// The cycler half of the same ladder. Open the wizard's RULES step, then
// step the SCORE wheel exactly ONCE with the first edge observation blinded
// (g_click_ladder_edge_blinds) — the starved menu thread that has not
// republished the row's label yet, seen from here. A ladder that re-presses
// a landed cycler walks the wheel MAP -> 1 -> 3 and never reads SCORE: 1
// again.
int setup_blind_cycler_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BlindCyclerState*>(data);

    (void)wait_for_interactable("continue_game", 5000);
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    (void)wait_for_interactable("setup", 10000);

    state->opened = open_setup_step(3, "RULES", 15000);
    if (state->opened) {
        (void)wait_for_interactable_label_containing("setup_row_0",
                                                     "SCORE: MAP", 10000);
        // Armed HERE, not in the test body: the blind belongs to the cycler
        // press, and the door ladder above would otherwise eat it.
        g_click_ladder_edge_blinds = 1;
        state->stepped = click_until_edge(
            "setup_row_0",
            [](int wait_ms) {
                return wait_for_interactable_label_containing(
                    "setup_row_0", "SCORE: 1", wait_ms);
            },
            state->witnessless ? nullptr : "turned", 3, 2500, "setup");
        // Where the wheel actually stands once the ladder is done: one
        // press, one stop. An overshoot reads 3 or 5 here and this stays
        // false however the ladder reported.
        state->wheel_still_on_two = wait_for_interactable_label_containing(
            "setup_row_0", "SCORE: 1", 5000);
        (void)click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        });
    }
    (void)interact("back");
    return 0;
}

} // namespace

// Teeth for the ladder: a press that evaporates costs one attempt, and the
// flow still reaches the screen. Counts, never clocks.
TEST(CampaignZoneUi, setup_click_helper_retries_a_dropped_press)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE CIRCLE (scen 300) is a TEAM DEATHMATCH arena, so the wizard's
    // RULES step leads with the SCORE wheel: MAP -> 1 -> 3 -> 5 -> 10.
    write_save0_with_two_soldiers("modes", 300);

    g_click_ladder_click_retries = 0;
    g_click_ladder_click_drops = 1;
    g_click_ladder_edge_waits = 0;
    g_click_ladder_edge_blinds = 0;

    bool opened = false;
    SDL_Thread* thread =
        SDL_CreateThread(setup_retry_injector, "setup_retry", &opened);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, g_click_ladder_click_drops) << "the injected drop must be consumed";
    EXPECT_TRUE(opened)
        << "a dropped press must cost a retry, not the whole flow";
    EXPECT_EQ(1, g_click_ladder_click_retries)
        << "exactly one attempt missed its edge";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

TEST(CampaignZoneUi, setup_click_helper_reports_a_ladder_that_never_lands)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE CIRCLE (scen 300) is a TEAM DEATHMATCH arena, so the wizard's
    // RULES step leads with the SCORE wheel: MAP -> 1 -> 3 -> 5 -> 10.
    write_save0_with_two_soldiers("modes", 300);

    g_click_ladder_click_retries = 0;
    g_click_ladder_click_drops = 0;
    g_click_ladder_edge_waits = 0;
    g_click_ladder_edge_blinds = 0;

    bool reached = true;
    SDL_Thread* thread = SDL_CreateThread(setup_wrong_id_injector,
                                          "setup_wrong_id", &reached);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_FALSE(reached) << "a wrong id can never reach the screen";
    EXPECT_EQ(3, g_click_ladder_click_retries)
        << "the ladder spends its three attempts and reports, never hangs";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// The other half of the same rule: a press that DID land is never re-sent,
// however long its edge takes to show up. The blind makes the first
// observation lie exactly the way a starved menu thread does, and a cycler
// is the one row where a second press is not free — it costs the wheel a
// stop it can only get back by going all the way round.
TEST(CampaignZoneUi, setup_click_helper_waits_out_a_landed_cycler)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE CIRCLE (scen 300) is a TEAM DEATHMATCH arena, so the wizard's
    // RULES step leads with the SCORE wheel: MAP -> 1 -> 3 -> 5 -> 10.
    write_save0_with_two_soldiers("modes", 300);

    g_click_ladder_click_retries = 0;
    g_click_ladder_click_drops = 0;
    g_click_ladder_edge_waits = 0;
    // The injector arms the blind on the cycler press itself; nothing is
    // blinded on the way there.
    g_click_ladder_edge_blinds = 0;

    BlindCyclerState state;
    SDL_Thread* thread = SDL_CreateThread(setup_blind_cycler_injector,
                                          "setup_blind_cycler", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.opened) << "the SETUP door still opens on RULES";
    EXPECT_EQ(0, g_click_ladder_edge_blinds) << "the injected blind must be consumed";
    EXPECT_TRUE(state.stepped)
        << "a landed press whose label lagged must still reach its edge";
    EXPECT_TRUE(state.wheel_still_on_two)
        << "the ladder must not press a landed cycler again: a second press "
           "walks the SCORE wheel past the stop the flow asked for";
    EXPECT_EQ(1, g_click_ladder_edge_waits)
        << "exactly one attempt waited on a press that had already landed";
    EXPECT_EQ(0, g_click_ladder_click_retries)
        << "a landed press is never charged as a re-press";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// The SAME rule for a row that publishes NO landing witness. Not every cycler
// can name its landing — this one is asked to prove it without trying — and
// for those the ladder's guard is the re-check it runs immediately before
// every RE-press: the edge that arrived after the wait gave up is found
// there, and the press is cancelled instead of sent.
//
// Without that re-check this flow presses a second time on a wheel that has
// already moved, and SCORE walks MAP -> 1 -> 3 while the flow waits for a
// face the row has gone by.
TEST(CampaignZoneUi, setup_click_helper_recheck_saves_a_witnessless_cycler)
{
    trace_clear();
    SavedPickerSave save_guard;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE CIRCLE (scen 300) is a TEAM DEATHMATCH arena, so the wizard's
    // RULES step leads with the SCORE wheel: MAP -> 1 -> 3 -> 5 -> 10.
    write_save0_with_two_soldiers("modes", 300);

    g_click_ladder_click_retries = 0;
    g_click_ladder_click_drops = 0;
    g_click_ladder_edge_waits = 0;
    g_click_ladder_edge_blinds = 0;

    BlindCyclerState state;
    state.witnessless = true;  // no landed_trace on the cycler press
    SDL_Thread* thread = SDL_CreateThread(setup_blind_cycler_injector,
                                          "setup_recheck_cycler", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.opened) << "the SETUP door still opens on RULES";
    EXPECT_EQ(0, g_click_ladder_edge_blinds)
        << "the injected blind must be consumed";
    EXPECT_TRUE(state.stepped)
        << "the re-check must report the late edge as an arrival";
    EXPECT_TRUE(state.wheel_still_on_two)
        << "a witnessless cycler must not be pressed again either: the "
           "re-check found SCORE: 1 before the re-press went out";
    EXPECT_EQ(1, g_click_ladder_click_retries)
        << "exactly one attempt expired with no witness and no edge";
    EXPECT_EQ(1, g_click_ladder_edge_waits)
        << "the second attempt waited instead of pressing, because the "
           "re-check found the edge already there";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}
