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
#include "test_interact.h"
#include "test_input_helpers.h"
#include "test_save_state_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/base.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// campaign_picker.cpp TESTING hooks (same contract as
// test_campaign_and_level_picker.cpp, which lives in a different binary).
void campaign_picker_testing_input_reset();
void campaign_picker_testing_abort();
std::uint64_t campaign_picker_testing_entered_count();
std::uint64_t campaign_picker_testing_action_count();
// Deterministic prompt answers (pick_campaign's ENTER ID path).
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

namespace {

constexpr const char* kFixtureId = "org.test.sprite162.sdl";

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

bool wait_for_counter_above(std::uint64_t (*counter)(), std::uint64_t baseline)
{
    constexpr Uint64 kTimeoutMs = 8000;
    const Uint64 started_at = SDL_GetTicks();
    while (counter() <= baseline)
    {
        if (SDL_GetTicks() - started_at >= kTimeoutMs)
        {
            campaign_picker_testing_abort();
            return false;
        }
        SDL_Delay(1);
    }
    return true;
}

bool push_picker_mouse_event(Uint32 event_type, int x, int y)
{
    SDL_Event event{};
    event.type = event_type;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = event_type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    return SDL_PushEvent(&event);
}

bool click_campaign_picker(int x, int y)
{
    const std::uint64_t before = campaign_picker_testing_action_count();
    if (!push_picker_mouse_event(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }
    const bool acknowledged =
        wait_for_counter_above(campaign_picker_testing_action_count, before);
    if (!push_picker_mouse_event(SDL_EVENT_MOUSE_BUTTON_UP, x, y))
    {
        campaign_picker_testing_abort();
        return false;
    }
    return acknowledged;
}

bool wait_for_trace(const char* category, const char* needle)
{
    constexpr Uint64 kTimeoutMs = 10000;
    const Uint64 started_at = SDL_GetTicks();
    while (!trace_contains(category, needle))
    {
        if (SDL_GetTicks() - started_at >= kTimeoutMs)
            return false;
        SDL_Delay(10);
    }
    return true;
}

// SCENARIO menu injector: SET CAMPAIGN -> campaign picker ENTER ID (the
// prompt queue answers with the fixture id) -> wait for the handler's
// reload -> BACK out.
int set_campaign_flow_injector(void*)
{
    og::runtime::ensure_thread_session();

    if (!wait_for_interactable("set_campaign", 8000))
        return 1;
    SDL_Delay(750); // fadeblack eats events
    const std::uint64_t entered_before =
        campaign_picker_testing_entered_count();
    interact("set_campaign");

    if (!wait_for_counter_above(campaign_picker_testing_entered_count,
                                entered_before))
        return 2;
    // The SET CAMPAIGN click is still HELD when the picker enters (the
    // entered counter bumps before interact()'s release lands), and the
    // picker's entry baseline holds fire until that click has been seen up
    // once (pointer handoff). Let the release land in its own picker frame
    // before pressing again — same cadence idiom as the BACK click below.
    SDL_Delay(300);
    // ENTER ID sits under DELETE/RESET; take its center from the pure layout.
    const og::ui::PickerRect id_button =
        og::ui::campaign_picker_layout().id_button;
    if (!click_campaign_picker(id_button.x + id_button.w / 2,
                               id_button.y + id_button.h / 2))
        return 3;

    // The do_pick_campaign handler mounts the fixture and reloads; only
    // after that is the scenario menu live again.
    if (!wait_for_trace("gloader", "reloaded stale"))
        return 4;
    if (!wait_for_interactable("back", 8000))
        return 5;
    SDL_Delay(300); // release the previous click before pressing again
    interact("back");
    return 0;
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
    campaign_picker_testing_input_reset();
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
    SDL_Thread* thread = SDL_CreateThread(set_campaign_flow_injector,
                                          "set_campaign_flow", nullptr);
    ASSERT_NE(nullptr, thread);

    const Sint32 ret = create_scenario_menu(0);

    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    cleanup_picker_state();

    EXPECT_EQ(0, injector_result)
        << "injector step failed (1=set_campaign, 2=picker entry, "
           "3=enter-id click, 4=reload trace, 5=back)";
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
