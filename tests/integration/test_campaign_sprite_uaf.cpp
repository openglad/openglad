/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Issue #162, the SDL half. Three contracts:
//
// 1. The SCENARIO > SET CAMPAIGN flow — driven end to end through the real
//    menu frame skeleton — reloads campaign-shipped entity art WITHOUT any
//    draw touching freed sprite pixels. Under ci-asan this is the genuine
//    use-after-free detector for the reload's sequencing (the mirror of
//    GloaderFuncs.gore_swap_does_not_free_live_pixels for the campaign
//    path): the handler frees loader buffers menu buttons still borrow, and
//    only reset_buttons re-creating every pixie before the frame's draw
//    makes that legal.
// 2. The gameplay-entry safety net (game.cpp) reloads a stale loader before
//    any walker spawns — the backstop for the lobby-poll campaign sync,
//    which deliberately never reloads mid-menu-frame.
// 3. The create_walker_owned TESTING tripwire actually fires for a missed
//    flow, so contracts 1 and 2 asserting its silence mean something.

#include <gtest/gtest.h>

#include "campaign_sprite_fixture.h"
#include "test_campaign_picker_drive.h"
#include "test_interact.h"
#include "test_input_helpers.h"
#include "test_save_state_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/base.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The campaign browser's TESTING hooks and the waiters built on them live in
// tests/test_campaign_picker_drive.h, shared with the og_test_menu_ui flows.
// Deterministic prompt answers (pick_campaign's ENTER ID path).
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

namespace {

constexpr const char* kFixtureId = "org.test.sprite162.sdl";

// Poll ticks, not settles (scripts/check_injector_settles.sh, tier 2).
constexpr Uint32 kEscapePollMs = 100;
// Cancellation ceilings for a pump that has stopped, never budgets.
constexpr int kScenarioDoorWaitMs = 8000;
// SET CAMPAIGN mounts a package and reloads every sprite; the reload trace is
// the only thing that says it finished.
constexpr int kReloadTraceWaitMs = 10000;
// The sabotaged leg waits for an id nothing publishes. Nothing is coming, and
// the point of that run is the tail, so its window is short on purpose.
constexpr int kSabotageWaitMs = 500;
// One BACK press per confirmation window, not one per tick.
constexpr int kEscapeConfirmTicks = 20;


inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

struct PixieSnapshot {
    unsigned char frames = 0;
    unsigned char w = 0;
    unsigned char h = 0;
    std::vector<unsigned char> pixels;

    bool operator==(const PixieSnapshot&) const = default;
};

PixieSnapshot snapshot_pixie(const PixieData* pix)
{
    PixieSnapshot snap;
    if (pix == nullptr || !pix->valid())
        return snap;
    snap.frames = pix->frames;
    snap.w = pix->w;
    snap.h = pix->h;
    const std::size_t len =
        static_cast<std::size_t>(pix->frames) * pix->w * pix->h;
    snap.pixels.assign(pix->data.get(), pix->data.get() + len);
    return snap;
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

// Runs LAST (declare before ScopedCampaignMountState): once the mount is
// restored, bring the process-wide loader back in sync so later tests in
// this binary never render the fixture's art, then drop the fixture files.
struct FixtureCleanup
{
    ~FixtureCleanup()
    {
        sdl_entity_loader()->reload_graphics_if_stale();
        og::test162::remove_sprite_campaign(kFixtureId);
        cleanup_unpacked_campaign();
    }
};

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;

    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;
    }
    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

struct PromptQueueGuard
{
    PromptQueueGuard() { level_editor_testing_prompt_queue_clear(); }
    ~PromptQueueGuard() { level_editor_testing_prompt_queue_clear(); }
};

// pick_campaign's abort flag is ONE-SHOT and consumed at the browser's loop
// top. The escape tail can raise it after the browser has already closed (a
// leg that gave up once the ENTER ID click had ended the loop), which leaves
// it pending: the next test in this binary to open the browser would break
// out on frame one. The between-tests listener re-arms auto-accept but does
// not clear this, so clear it on the way in and on the way out.
struct PickerInputResetGuard
{
    PickerInputResetGuard() { campaign_picker_testing_input_reset(); }
    ~PickerInputResetGuard()
    {
        campaign_picker_testing_input_reset();
        // Not the flush the body warns about: each body already polled its
        // tail's release through reset_mouse_click_tracking() + pump before
        // this guard unwinds, so what is left here is at most a lone press
        // the browser never saw -- never a release whose loss would pin
        // mouse_state.left true.
        SDL_FlushEvents(SDL_EVENT_MOUSE_BUTTON_DOWN,
                        SDL_EVENT_MOUSE_BUTTON_UP);
    }
};

bool wait_for_trace(const char* category, const char* needle)
{
    const Uint64 started_at = SDL_GetTicks();
    while (!trace_contains(category, needle))
    {
        if (SDL_GetTicks() - started_at >=
            static_cast<Uint64>(kReloadTraceWaitMs))
        {
            return false;
        }
        SDL_Delay(10);
    }
    return true;
}

// ---------------------------------------------------------------------------
// The SCENARIO > SET CAMPAIGN flow, driven leg by leg.
//
// Both blocking loops in this flow run on the MAIN thread: create_scenario_menu
// (engine-hosted, so wait_for_menu_frames is its oracle) and, inside the
// SET CAMPAIGN handler, pick_campaign (NOT engine-hosted, so the counters in
// tests/test_campaign_picker_drive.h are its oracle). Neither can end itself.
// That is why no leg here may simply `return n`: the numbered give-ups this
// file used to carry left the main thread wedged and took the whole
// og_test_game_core binary to the CTest ceiling, which is also why the
// EXPECT_EQ(0, injector_result) message enumerating the legs was unreachable.
// Every give-up now routes through the escape tail below, and
// CampaignSpriteUaf.a_leg_that_gives_up_frees_the_main_thread is its
// regression.
// ---------------------------------------------------------------------------

struct SpriteFlowState
{
    // Set by the MAIN thread once create_scenario_menu has returned, never by
    // the injector.
    std::atomic<bool> test_finished{false};
    bool started = false;
    bool finished = false;
    // Point one leg at an id the flow never publishes, so the give-up path is
    // itself testable.
    int sabotage_leg = 0;
};

// Keep closing whatever is currently blocking the main thread until that
// thread says it is out for good, then report the leg that gave up. No
// wall-clock bound, on purpose: nothing else can end create_scenario_menu.
//
// Two doors, and they are not interchangeable. While pick_campaign owns the
// main thread its buttons are its own local array -- allbuttons still holds
// the SCENARIO screen's, so a click aimed at "back" would land inside the
// browser at whatever sits under those coordinates. The browser's abort flag
// is its only legitimate door, and it is one-shot (consumed at the loop top),
// so it is raised at most ONCE per browser entry this tail has not already
// seen, and BACK is clicked only when no unseen entry is outstanding.
int escape_from_the_campaign_flow(SpriteFlowState& state,
                                  std::uint64_t& seen_entries, int leg,
                                  const char* why)
{
    if (leg != 0)
        fprintf(stderr, "  [test] ERROR: leg %d: %s\n", leg, why);
    while (!state.test_finished.load())
    {
        const std::uint64_t entered = campaign_picker_testing_entered_count();
        if (entered > seen_entries)
        {
            campaign_picker_testing_abort();
            seen_entries = entered;
            SDL_Delay(kEscapePollMs);
            continue;
        }
        if (!has_interactable("back") || !interact("back"))
        {
            SDL_Delay(kEscapePollMs);
            continue;
        }
        // BACK stays published in allbuttons even while the browser owns the
        // main thread, so there is no "the button went away" edge to watch
        // here: watch the main thread instead, and press again only if it is
        // still blocked when the window closes.
        for (int tick = 0;
             tick < kEscapeConfirmTicks && !state.test_finished.load(); ++tick)
        {
            SDL_Delay(kEscapePollMs);
        }
    }
    return leg;
}

int set_campaign_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    SpriteFlowState* const state = static_cast<SpriteFlowState*>(data);
    state->started = true;

    // The tail must start from the entry count the flow started with, so an
    // entry it has not seen is exactly "the browser this flow opened".
    std::uint64_t seen_entries = campaign_picker_testing_entered_count();
    const auto escape = [state, &seen_entries](int leg, const char* why) {
        return escape_from_the_campaign_flow(*state, seen_entries, leg, why);
    };

    // -- Leg 1: the SCENARIO screen is up AND has composed a frame --
    const char* const start_id =
        state->sabotage_leg == 1 ? "never_published_row" : "set_campaign";
    const int start_wait_ms =
        state->sabotage_leg == 1 ? kSabotageWaitMs : kScenarioDoorWaitMs;
    if (!wait_for_interactable(start_id, start_wait_ms) ||
        !wait_for_menu_frames(2))
    {
        return escape(1, "the SCENARIO screen never published set_campaign");
    }

    // -- Leg 2: SET CAMPAIGN, acknowledged by the browser it opens --
    // Re-baseline the pointer on the menu thread first: a press that makes no
    // up->down edge (a release flushed by an earlier test) simply evaporates.
    if (!run_on_main_thread(reset_mouse_click_tracking))
        return escape(2, "the menu thread never ran the pointer re-baseline");
    const std::uint64_t entered_before =
        campaign_picker_testing_entered_count();
    if (!interact("set_campaign"))
        return escape(2, "set_campaign vanished before the click");
    if (!wait_for_campaign_picker_entry(entered_before))
    {
        return escape(2, "the campaign browser never entered its loop");
    }

    // -- Leg 3: the pointer handoff --
    // The SET CAMPAIGN click is still HELD when the browser enters (the entry
    // counter bumps before interact()'s release lands), and the browser holds
    // fire until that click has been seen up once. Two facts make the next
    // press safe, and neither alone does: the release is gone from the SDL
    // queue (some iteration polled it), and one more iteration has begun (so
    // the iteration that polled it also ran its query_mouse, which is what
    // sets saw_left_release).
    if (!wait_for_campaign_picker_event_consumed(SDL_EVENT_MOUSE_BUTTON_UP) ||
        !wait_for_campaign_picker_frames(1))
    {
        return escape(3,
                      "the browser never finished the frame that took the "
                      "door click's release");
    }

    // -- Leg 4: ENTER ID (the prompt queue answers with the fixture id) --
    // Coordinates come from the pure layout, so a geometry change moves the
    // click with it instead of silently missing.
    const og::ui::PickerRect id_button =
        og::ui::campaign_picker_layout().id_button;
    if (!click_campaign_picker_action(id_button.x + id_button.w / 2,
                                      id_button.y + id_button.h / 2))
    {
        return escape(4, "the ENTER ID button never acknowledged the click");
    }

    // -- Leg 5: the handler mounts the fixture and reloads --
    if (!wait_for_trace("gloader", "reloaded stale"))
    {
        return escape(
            5, "the SET CAMPAIGN handler never reloaded the stale loader");
    }

    // -- Leg 6: the SCENARIO screen is live and composing again --
    if (!wait_for_interactable("back", kScenarioDoorWaitMs) ||
        !wait_for_menu_frames(2))
    {
        return escape(6,
                      "the SCENARIO screen never resumed after the browser "
                      "closed");
    }

    state->finished = true;
    // The BACK that ends the flow belongs to the tail: create_scenario_menu
    // returns under it, and after that no pump is left to service a ladder.
    return escape(0, "");
}

} // namespace

TEST(CampaignSpriteUaf, set_campaign_flow_reloads_shipped_art_safely)
{
    trace_clear();
    FixtureCleanup cleanup_last;
    og::test::ScopedCampaignMountState mount_restore;
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;
    PickerInputResetGuard picker_input_guard;
    PromptQueueGuard prompt_queue;
    level_editor_testing_prompt_queue_push(kFixtureId);

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    sdl_entity_loader()->reload_graphics_if_stale();
    ASSERT_TRUE(og::test162::install_playable_sprite_campaign(kFixtureId));

    screen* const scr = og::runtime::current_session->myscreen_;
    SaveData& save = scr->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;

    const PixieData donor = read_pixie_file("elf.png");
    ASSERT_TRUE(donor.valid());
    const PixieSnapshot donor_snap = snapshot_pixie(&donor);
    const PixieSnapshot stock_snap = snapshot_pixie(
        sdl_entity_loader()->graphics_for(Order::Living, FAMILY_SOLDIER));
    ASSERT_FALSE(stock_snap.pixels.empty());
    ASSERT_NE(stock_snap, donor_snap);

    trace_clear();
    SpriteFlowState state;
    SDL_Thread* thread = SDL_CreateThread(set_campaign_flow_injector,
                                          "set_campaign_flow", &state);
    ASSERT_NE(nullptr, thread);

    const Sint32 ret = create_scenario_menu(0);

    // The tail keeps pressing BACK until it is told the menu is gone for good.
    state.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    // The tail's last press may have been HALF consumed (the menu returned
    // between its DOWN and its UP). Poll that release through the engine --
    // reset_mouse_click_tracking consumes the queued events before it
    // re-baselines -- instead of flushing it: a FLUSHED release leaves
    // mouse_state.left stuck true, and the next flow's first press then makes
    // no up->down edge and evaporates.
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
    cleanup_picker_state();

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_TRUE(state.finished)
        << "the injector must have completed every leg of the flow";
    EXPECT_EQ(0, injector_result)
        << "the injector gave up at leg " << injector_result
        << " (1=SCENARIO composed, 2=SET CAMPAIGN acknowledged by the "
           "browser, 3=pointer handoff, 4=ENTER ID click, 5=reload trace, "
           "6=SCENARIO resumed)";
    EXPECT_TRUE(ret & 2) << "BACK must propagate MENU_REDRAW";
    EXPECT_EQ(kFixtureId, get_mounted_campaign())
        << "the flow must land on the entered campaign";
    EXPECT_TRUE(trace_contains("gloader", "reloaded stale"))
        << "the SET CAMPAIGN handler must reload the stale loader";
    EXPECT_EQ(donor_snap,
              snapshot_pixie(sdl_entity_loader()->graphics_for(
                  Order::Living, FAMILY_SOLDIER)))
        << "after the flow the campaign's shipped soldier art must be live";
    EXPECT_FALSE(trace_contains("gloader", "create_walker stale"))
        << "no walker may be created from a stale loader anywhere in the "
           "SET CAMPAIGN flow";

    save.reset();
    save.current_campaign = "gladiator";
}

// The regression for the wedge itself. A leg that gives up while the main
// thread is blocked inside create_scenario_menu -- and, one level deeper,
// possibly inside pick_campaign -- must free that thread and report its
// number. Leg 1 is pointed at an id the SCENARIO screen never publishes, so
// the escape tail has to click BACK for it and create_scenario_menu returns.
//
// Replace `return escape(1, ...)` with a bare `return 1` (the shape this file
// carried before PR #292) and this test hangs until the CTest ceiling instead
// of failing -- which is exactly why the flow test's leg-by-leg EXPECT
// message was unreachable.
TEST(CampaignSpriteUaf, a_leg_that_gives_up_frees_the_main_thread)
{
    trace_clear();
    FixtureCleanup cleanup_last;
    og::test::ScopedCampaignMountState mount_restore;
    ViewportGuard viewport_guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;
    PickerInputResetGuard picker_input_guard;
    PromptQueueGuard prompt_queue;
    level_editor_testing_prompt_queue_push(kFixtureId);

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    sdl_entity_loader()->reload_graphics_if_stale();
    ASSERT_TRUE(og::test162::install_playable_sprite_campaign(kFixtureId));

    screen* const scr = og::runtime::current_session->myscreen_;
    SaveData& save = scr->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;

    trace_clear();
    SpriteFlowState state;
    state.sabotage_leg = 1;
    SDL_Thread* thread = SDL_CreateThread(set_campaign_flow_injector,
                                          "set_campaign_escape", &state);
    ASSERT_NE(nullptr, thread);

    const Sint32 ret = create_scenario_menu(0);

    state.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
    cleanup_picker_state();

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_FALSE(state.finished)
        << "a flow that gave up at leg 1 never completed";
    // Reaching here at all is the rule under test: the tail's BACK ended the
    // blocking menu, and BACK folds MENU_EXIT to MENU_REDRAW.
    EXPECT_TRUE(ret & 2) << "BACK must propagate MENU_REDRAW";
    EXPECT_EQ("gladiator", get_mounted_campaign())
        << "a flow that never reached SET CAMPAIGN must not change the mount";
    EXPECT_FALSE(trace_contains("gloader", "reloaded stale"))
        << "a flow that never reached SET CAMPAIGN must not reload anything";

    save.reset();
    save.current_campaign = "gladiator";
}

TEST(CampaignSpriteUaf, gameplay_entry_net_reloads_before_walkers_spawn)
{
    FixtureCleanup cleanup_last;
    og::test::ScopedCampaignMountState mount_restore;

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    sdl_entity_loader()->reload_graphics_if_stale();
    ASSERT_TRUE(og::test162::install_playable_sprite_campaign(kFixtureId));

    // The lobby-sync shape: the campaign switches mounts with NO reload
    // (picker_lobby_client.cpp deliberately cannot reload mid-frame).
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));
    // The stale window opens HERE; every later wire point (the SDL
    // level-construction chokepoint included) may close it, and the pin is
    // that SOME render-safe reload runs before any walker spawns.
    trace_clear();

    og::runtime::GameSession::Config session_config;
    session_config.allocate_screen = true;
    session_config.create_display = false;
    session_config.allocate_prefs = true;
    session_config.install_legacy_globals = false;
    og::runtime::GameSession isolated_session(session_config);
    auto isolated_scope = isolated_session.activate();
    screen* const scr = isolated_session.screen_ptr();
    ASSERT_NE(nullptr, scr);

    SaveData& save = scr->save_data;
    save.reset();
    save.current_campaign = kFixtureId;
    save.scen_num = 1;
    save.numplayers = 0; // spectator: authored objects only

    ASSERT_EQ(LoadSavedGameError::None,
              load_saved_game_with_error(nullptr, scr));

    EXPECT_TRUE(trace_contains("gloader", "reloaded stale"))
        << "the gameplay-entry net must reload the stale loader";
    EXPECT_FALSE(trace_contains("gloader", "create_walker stale"))
        << "every walker of the level load must be created AFTER the net's "
           "reload";
    EXPECT_EQ(24,
              static_cast<int>(scr->myloader
                                   ->graphics_for(Order::Living,
                                                  FAMILY_SOLDIER)
                                   ->frames))
        << "the soldier must wear the campaign's shipped (elf-donor) art";
}

TEST(CampaignSpriteUaf, tripwire_fires_for_a_missed_flow)
{
    FixtureCleanup cleanup_last;
    og::test::ScopedCampaignMountState mount_restore;

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    sdl_entity_loader()->reload_graphics_if_stale();
    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));

    trace_clear();
    std::unique_ptr<walker> stale =
        sdl_entity_loader()->create_walker_owned(Order::Living,
                                                 FAMILY_SOLDIER);
    ASSERT_NE(nullptr, stale);
    EXPECT_TRUE(trace_contains("gloader", "create_walker stale"))
        << "creating a walker from a stale loader must trip the detector — "
           "without this, the flow tests asserting its silence prove nothing";

    EXPECT_TRUE(sdl_entity_loader()->reload_graphics_if_stale());
    trace_clear();
    std::unique_ptr<walker> fresh =
        sdl_entity_loader()->create_walker_owned(Order::Living,
                                                 FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fresh);
    EXPECT_FALSE(trace_contains("gloader", "create_walker stale"))
        << "a current loader must create walkers silently";
}
