#include <openglad/resources/gparser.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/screen_lifecycle.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/constants.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/game_world.h>

#include <gtest/gtest.h>
#include <SDL.h>

// myscreen is now a macro defined in base.h (via game_session.h)
short new_score_panel(screen* s, short do_it);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace {

void clear_level_lists()
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0;
    og::runtime::current_session->myscreen_->world_.withdraw_requested = false;
    og::runtime::current_session->myscreen_->world_.withdraw_level = -1;
}

GameWorld& setup_tick_world(std::uint32_t seed)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.tick_count_ = 0;
    world.rng_.state_ = seed;
    world.reset_level_progress();
    world.level_done = 0;
    world.game_ended = false;
    world.next_level = -1;
    world.ending = 0;
    world.enemy_freeze = 0;
    world.end = 0;
    world.retry = false;
    world.control_hp = 0.0f;
    world.timer_wait = 6;
    world.withdraw_requested = false;
    world.withdraw_level = -1;
    return world;
}

walker* add_living(unsigned char team)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->user = -1;
    w->setxy(100, 100);
    return w;
}

treasure* add_treasure(char family, short level)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, family);
    if (!w)
        return nullptr;
    w->stats()->level = level;
    w->setxy(100, 100);
    return static_cast<treasure*>(w);
}

void set_world_tile(short world_x, short world_y, unsigned char tile)
{
    auto& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.world().grid.w || gy >= level.world().grid.h)
        return;
    level.world().grid.data[gx + level.world().grid.w * gy] = tile;
}

} // namespace

TEST(RuntimeCoveragePaths, input_bridge_window_and_key_paths)
{
    const float saved_window_w = og::runtime::current_session->window_w_;
    const float saved_window_h = og::runtime::current_session->window_h_;
    const float saved_overscan = og::runtime::current_session->overscan_percentage_;
    const bool saved_continue = og::runtime::current_session->input_continue_;
    const short saved_key_press_event = og::runtime::current_session->key_press_event_;
    const int saved_raw_key = og::runtime::current_session->raw_key_;
    const bool saved_gameplay_active = og::runtime::current_session->gameplay_active_;

    screen* s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr) << "active screen should be available";
    if (!s)
        return;

    const short saved_scen_num = s->save_data.scen_num;
    const short saved_allied_mode = s->save_data.allied_mode;
    const std::uint32_t saved_score0 = s->save_data.m_score[0];
    const std::string saved_campaign = s->save_data.current_campaign;
    const auto saved_completed_levels = s->save_data.completed_levels;

    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;

    s->save_data.current_campaign = "org.openglad.gladiator";
    s->save_data.completed_levels[s->save_data.current_campaign].clear();
    s->save_data.scen_num = 2;
    s->save_data.allied_mode = 0;
    s->save_data.m_score[0] = 11;

    s->world_.current_scenario = 9;
    s->world_.allied_mode = 3;
    s->world_.m_score[0] = 42;
    s->world_.completed_levels = {3, 5};

    // In picker/menu flows, SaveData is authoritative: window autosave must
    // persist SaveData directly and not overwrite it from stale GameWorld.
    og::runtime::current_session->gameplay_active_ = false;

    e.window.event = SDL_WINDOWEVENT_MINIMIZED;
    handle_window_event(e);
    ASSERT_EQ(2, static_cast<int>(s->save_data.scen_num)) << "minimize autosave should preserve menu scen_num when gameplay is inactive";
    ASSERT_EQ(0, static_cast<int>(s->save_data.allied_mode)) << "minimize autosave should preserve menu allied_mode when gameplay is inactive";
    ASSERT_EQ(11, static_cast<int>(s->save_data.m_score[0])) << "minimize autosave should preserve menu score when gameplay is inactive";
    ASSERT_TRUE(s->save_data.completed_levels[s->save_data.current_campaign].empty()) << "minimize autosave should preserve menu completed levels when gameplay is inactive";

    e.window.event = SDL_WINDOWEVENT_CLOSE;
    handle_window_event(e);
    ASSERT_EQ(2, static_cast<int>(s->save_data.scen_num)) << "close autosave should preserve menu scen_num when gameplay is inactive";
    ASSERT_EQ(0, static_cast<int>(s->save_data.allied_mode)) << "close autosave should preserve menu allied_mode when gameplay is inactive";
    ASSERT_EQ(11, static_cast<int>(s->save_data.m_score[0])) << "close autosave should preserve menu score when gameplay is inactive";
    ASSERT_TRUE(s->save_data.completed_levels[s->save_data.current_campaign].empty()) << "close autosave should preserve menu completed levels when gameplay is inactive";

    // During active gameplay, GameWorld is authoritative and autosave must sync
    // world-backed progress into SaveData before writing.
    og::runtime::current_session->gameplay_active_ = true;
    s->world_.current_scenario = 12;
    s->world_.allied_mode = 1;
    s->world_.m_score[0] = 77;
    s->world_.completed_levels = {1, 2, 4};

    e.window.event = SDL_WINDOWEVENT_MINIMIZED;
    handle_window_event(e);
    ASSERT_EQ(12, static_cast<int>(s->save_data.scen_num)) << "minimize autosave should sync scen_num from world during gameplay";
    ASSERT_EQ(1, static_cast<int>(s->save_data.allied_mode)) << "minimize autosave should sync allied_mode from world during gameplay";
    ASSERT_EQ(77, static_cast<int>(s->save_data.m_score[0])) << "minimize autosave should sync score from world during gameplay";
    ASSERT_TRUE(s->save_data.completed_levels[s->save_data.current_campaign] == s->world_.completed_levels) << "minimize autosave should sync completed levels from world during gameplay";

    s->world_.current_scenario = 14;
    s->world_.allied_mode = 2;
    s->world_.m_score[0] = 99;
    s->world_.completed_levels = {2, 6};
    e.window.event = SDL_WINDOWEVENT_CLOSE;
    handle_window_event(e);
    ASSERT_EQ(14, static_cast<int>(s->save_data.scen_num)) << "close autosave should sync scen_num from world during gameplay";
    ASSERT_EQ(2, static_cast<int>(s->save_data.allied_mode)) << "close autosave should sync allied_mode from world during gameplay";
    ASSERT_EQ(99, static_cast<int>(s->save_data.m_score[0])) << "close autosave should sync score from world during gameplay";
    ASSERT_TRUE(s->save_data.completed_levels[s->save_data.current_campaign] == s->world_.completed_levels) << "close autosave should sync completed levels from world during gameplay";

    e.window.event = SDL_WINDOWEVENT_RESTORED;
    handle_window_event(e);

    og::runtime::current_session->overscan_percentage_ = -1.0f;
    e.window.event = SDL_WINDOWEVENT_RESIZED;
    e.window.data1 = 1280;
    e.window.data2 = 720;
    handle_window_event(e);
    ASSERT_EQ(1280, static_cast<int>(og::runtime::current_session->window_w_)) << "resize should update window_w";
    ASSERT_EQ(720, static_cast<int>(og::runtime::current_session->window_h_)) << "resize should update window_h";
    ASSERT_TRUE(og::runtime::current_session->overscan_percentage_ == 0.0f) << "resize path should clamp overscan";

    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_F10;
    key.key.keysym.mod = 0;
    handle_key_event(key);

    cfg.apply_setting("graphics", "overscan_percentage", "25");
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_F12;
    key.key.keysym.mod = KMOD_CTRL;
    handle_key_event(key);
    ASSERT_TRUE(og::runtime::current_session->overscan_percentage_ >= 0.0f && og::runtime::current_session->overscan_percentage_ <= 0.25f) << "F12+Ctrl should reload and clamp overscan";

    og::runtime::current_session->input_continue_ = false;
    og::runtime::current_session->key_press_event_ = 0;
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_ESCAPE;
    key.key.keysym.mod = 0;
    handle_key_event(key);
    ASSERT_TRUE(og::runtime::current_session->input_continue_) << "escape keydown should set continue";
    ASSERT_EQ(1, static_cast<int>(og::runtime::current_session->key_press_event_)) << "keydown should set key_press_event";
    ASSERT_EQ(static_cast<int>(SDLK_ESCAPE), og::runtime::current_session->raw_key_) << "keydown should update raw_key";

    key.type = SDL_KEYUP;
    key.key.keysym.sym = SDLK_ESCAPE;
    handle_key_event(key);

    og::runtime::current_session->window_w_ = saved_window_w;
    og::runtime::current_session->window_h_ = saved_window_h;
    og::runtime::current_session->overscan_percentage_ = saved_overscan;
    og::runtime::current_session->input_continue_ = saved_continue;
    og::runtime::current_session->key_press_event_ = saved_key_press_event;
    og::runtime::current_session->raw_key_ = saved_raw_key;
    og::runtime::current_session->gameplay_active_ = saved_gameplay_active;
    s->save_data.scen_num = saved_scen_num;
    s->save_data.allied_mode = saved_allied_mode;
    s->save_data.m_score[0] = saved_score0;
    s->save_data.current_campaign = saved_campaign;
    s->save_data.completed_levels = saved_completed_levels;
    s->sync_world_from_save_data();
    update_overscan_setting();
}


TEST(RuntimeCoveragePaths, screen_lifecycle_session_owner_paths)
{
    destroy_global_screen();
    ASSERT_TRUE(global_session_owner() == nullptr) << "destroy_global_screen should clear owner";

    screen* recreated = create_global_screen(1);
    ASSERT_TRUE(recreated != nullptr) << "create_global_screen should recreate session";
    ASSERT_TRUE(global_session_owner() != nullptr) << "session owner should exist after create";

    destroy_global_session();
    ASSERT_TRUE(global_session_owner() == nullptr) << "destroy_global_session should clear owner";

    // Restore expected global screen for subsequent tests.
    ASSERT_TRUE(create_global_screen(1) != nullptr) << "recreate global screen for remaining tests";
}


TEST(RuntimeCoveragePaths, game_session_auto_wires_level_data_sim_context)
{
    screen* baseline_screen = og::runtime::current_session->myscreen_;

    {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.create_display = false;

        og::runtime::GameSession session(session_cfg);
        screen* session_screen = session.screen_ptr();
        ASSERT_TRUE(session_screen != nullptr) << "session should allocate a screen";
        ASSERT_TRUE(current_game == &session.game_) << "session should install current_game";
        ASSERT_TRUE(session.game_.world == &session_screen->world_) << "session gameplay world should match screen world";
        ASSERT_TRUE(session.game_.save == &session_screen->save_data) << "session gameplay save should match screen save";
        ASSERT_TRUE(session.game_.sim_events == session.ctx_.sim_events.get()) << "session gameplay events should match session context";

        walker* spawned = session_screen->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(spawned != nullptr) << "spawned entity should exist";
    }

    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen) << "session teardown should restore previous screen";
}


TEST(RuntimeCoveragePaths, game_session_subsession_construction_keeps_host_context)
{
    screen* host_screen = og::runtime::current_session->myscreen_;
    options* host_prefs = og::runtime::current_session->theprefs_;
    ASSERT_TRUE(host_screen != nullptr) << "host session should have an active screen";
    ASSERT_TRUE(host_prefs != nullptr) << "host session should have active prefs";
    if (!host_screen || !host_prefs)
        return;

    viewscreen* host_view0 = host_screen->viewob[0].get();
    ASSERT_TRUE(host_view0 != nullptr) << "host session should have view 0";
    if (!host_view0)
        return;

    int* host_view0_keys = host_view0->mykeys;

    {
        og::runtime::GameSession::Config sub_cfg;
        sub_cfg.create_display = false;
        sub_cfg.install_legacy_globals = false;
        og::runtime::GameSession sub_session(sub_cfg);

        screen* sub_screen = sub_session.screen_ptr();
        ASSERT_TRUE(sub_screen != nullptr) << "sub-session should allocate a screen";
        ASSERT_TRUE(sub_screen != host_screen) << "sub-session screen should not alias host";
        ASSERT_TRUE(sub_screen->viewob[0] != nullptr) << "sub-session should initialize view 0";
        ASSERT_TRUE(sub_screen->viewob[0]->mykeys == sub_session.allkeys_[0]) << "sub-session view should bind to sub-session key state";

        ASSERT_TRUE(og::runtime::current_session->myscreen_ == host_screen) << "sub-session construction must not overwrite host myscreen";
        ASSERT_TRUE(og::runtime::current_session->theprefs_ == host_prefs) << "sub-session construction must not overwrite host prefs";
        ASSERT_TRUE(host_view0->mykeys == host_view0_keys) << "host view key mapping should remain unchanged";
    }

    ASSERT_TRUE(og::runtime::current_session->myscreen_ == host_screen) << "host myscreen should remain active after sub-session teardown";
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == host_prefs) << "host prefs should remain active after sub-session teardown";
    ASSERT_TRUE(host_view0->mykeys == host_view0_keys) << "host view key mapping should remain unchanged after teardown";
}


TEST(RuntimeCoveragePaths, treasure_core_methods_and_teleport_target_search)
{
    clear_level_lists();

    treasure standalone;
    ASSERT_TRUE(standalone.act()) << "treasure::act should return true";
    standalone.set_direct_frame(7);
    ASSERT_EQ(7, standalone.frame) << "set_direct_frame should update frame";
    walker eater_default;
    ASSERT_TRUE(standalone.eat_me(&eater_default)) << "eat_me should safely return true without descriptor";

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 3);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 3);
    ASSERT_TRUE(tele_a->find_teleport_target() == tele_b) << "teleporter should find next live matching target";

    tele_b->dead = 1;
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 3);
    ASSERT_TRUE(tele_a->find_teleport_target() == tele_c) << "teleporter should skip dead targets";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_exit_and_teleporter_navigation_paths)
{
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->sync_world_from_save_data();
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 1;
    treasure* exit_fx = add_treasure(FAMILY_EXIT, 2);
    walker* controller = add_living(0);
    controller->set_act_type(ACT_CONTROL);
    controller->skip_exit = 0;
    controller->in_act = false;

    ASSERT_TRUE(exit_fx->eat_me(controller)) << "exit eater path should return true";
    ASSERT_EQ(10, static_cast<int>(controller->skip_exit)) << "exit path should set skip_exit debounce";
    bool saw_request_confirmation = false;
    bool saw_withdraw_request = false;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation &&
            static_cast<short>(ev.a) == 2 && ev.b == 0)
        {
            saw_request_confirmation = true;
        }
        if (ev.kind == og::sim::EventKind::WithdrawToLevel)
            saw_withdraw_request = true;
    }
    ASSERT_TRUE(saw_request_confirmation) << "exit should emit RequestExitConfirmation event";
    ASSERT_TRUE(!saw_withdraw_request) << "normal exit should not emit WithdrawToLevel";

    // Teleporter: near target path sets skip_exit and centers on destination.
    clear_level_lists();
    sim_events.clear();
    treasure* tele_1 = add_treasure(FAMILY_TELEPORTER, 4);
    treasure* tele_2 = add_treasure(FAMILY_TELEPORTER, 4);
    tele_1->setxy(100, 100);
    tele_2->setxy(130, 100);
    walker* mover = add_living(0);
    mover->setxy(103, 100);
    mover->skip_exit = 0;
    tele_1->eat_me(mover);
    ASSERT_TRUE(mover->skip_exit >= 20) << "teleport should increase skip_exit cooldown";
    ASSERT_TRUE(tele_1->leader() == tele_2) << "teleport should select the linked target";

    // Teleporter close-range debounce path.
    mover->setxy(100, 100);
    mover->skip_exit = 1;
    tele_1->eat_me(mover);
    ASSERT_EQ(8, static_cast<int>(mover->skip_exit)) << "close + skip_exit path should set skip_exit=8";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_navigation_early_returns_and_withdraw_decline)
{
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    // Exit early return: eater currently in act.
    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3);
    walker* eater = add_living(0);
    ASSERT_TRUE(exit_fx != nullptr && eater != nullptr) << "exit/eater created";
    if (!(exit_fx && eater))
        return;
    eater->set_act_type(ACT_CONTROL);
    eater->skip_exit = 0;
    eater->in_act = true;
    ASSERT_TRUE(exit_fx->eat_me(eater)) << "in_act early return should succeed";
    ASSERT_EQ(0, static_cast<int>(eater->skip_exit)) << "in_act path should not update skip_exit";

    // Exit early return: not ACT_CONTROL.
    eater->in_act = false;
    eater->set_act_type(0);
    ASSERT_TRUE(exit_fx->eat_me(eater)) << "non-control early return should succeed";
    ASSERT_EQ(0, static_cast<int>(eater->skip_exit)) << "non-control path should not update skip_exit";

    // Withdraw branch with decline: level is completed, current isn't, enemies still present.
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    og::runtime::current_session->myscreen_->sync_world_from_save_data();
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0; // enemies still present
    eater->set_act_type(ACT_CONTROL);
    eater->skip_exit = 0;
    exit_fx->stats()->level = 3;
    sim_events.clear();
    ASSERT_TRUE(exit_fx->eat_me(eater)) << "withdraw decline path should return true";
    ASSERT_EQ(10, static_cast<int>(eater->skip_exit)) << "withdraw prompt path should set skip_exit debounce";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world_.withdraw_requested) << "withdraw branch should set world.withdraw_requested";
    bool saw_withdraw_prompt = false;
    bool saw_withdraw_event = false;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation && ev.b == 1)
            saw_withdraw_prompt = true;
        if (ev.kind == og::sim::EventKind::WithdrawToLevel &&
            static_cast<short>(ev.a) == 3)
            saw_withdraw_event = true;
    }
    ASSERT_TRUE(saw_withdraw_prompt) << "withdraw branch should emit withdraw confirmation request";
    ASSERT_TRUE(saw_withdraw_event) << "withdraw branch should emit WithdrawToLevel event";

    // Teleporter early returns: skip_exit > 1 and distance too far.
    clear_level_lists();
    treasure* tele = add_treasure(FAMILY_TELEPORTER, 9);
    walker* mover = add_living(0);
    ASSERT_TRUE(tele != nullptr && mover != nullptr) << "teleporter/mover created";
    if (!(tele && mover))
        return;

    tele->setxy(200, 200);
    mover->setxy(200, 200);
    mover->skip_exit = 5;
    ASSERT_TRUE(tele->eat_me(mover)) << "teleporter skip_exit guard should return true";
    ASSERT_EQ(5, static_cast<int>(mover->skip_exit)) << "skip_exit guard should not alter cooldown";

    mover->skip_exit = 0;
    mover->setxy(400, 400);
    ASSERT_TRUE(tele->eat_me(mover)) << "teleporter far-distance guard should return true";
    ASSERT_EQ(0, static_cast<int>(mover->skip_exit)) << "far-distance path should not alter cooldown";

    // No target teleporter available.
    mover->setxy(200, 200);
    ASSERT_TRUE(tele->eat_me(mover)) << "teleporter without target should return true";
    ASSERT_TRUE(mover->skip_exit >= 20) << "no-target path still applies cooldown increment";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_batch3_find_target_wraparound_and_no_match)
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 7);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 8);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 7);
    ASSERT_TRUE(tele_a && tele_b && tele_c) << "teleporters created";
    if (!(tele_a && tele_b && tele_c))
        return;

    tele_b->dead = 1;
    ASSERT_TRUE(tele_c->find_teleport_target() == tele_a) << "teleporter should wrap to earlier matching target when no later target matches";

    tele_a->dead = 1;
    ASSERT_TRUE(tele_c->find_teleport_target() == nullptr) << "teleporter should return nullptr when no live matching target exists";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_batch3_teleporter_leader_and_blocked_destination)
{
    clear_level_lists();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    treasure* tele_src = add_treasure(FAMILY_TELEPORTER, 4);
    treasure* tele_dst = add_treasure(FAMILY_TELEPORTER, 4);
    walker* mover = add_living(0);
    ASSERT_TRUE(tele_src && tele_dst && mover) << "teleporter source/destination and mover created";
    if (!(tele_src && tele_dst && mover))
        return;

    tele_src->setxy(100, 100);
    tele_dst->setxy(160, 160);
    tele_src->set_leader(tele_dst); // Force the "use leader" branch.
    mover->setxy(100, 100);
    mover->skip_exit = 0;

    // Make destination impassable so teleporter recenters mover back to source.
    set_world_tile(160, 160, PIX_H_WALL1);

    ASSERT_TRUE(tele_src->eat_me(mover)) << "teleporter eat should still return true when destination blocked";
    ASSERT_EQ(100, (int)mover->xpos) << "blocked destination should recenter mover to source X";
    ASSERT_EQ(100, (int)mover->ypos) << "blocked destination should recenter mover to source Y";
    ASSERT_TRUE(tele_src->leader() == tele_dst) << "leader-based destination should remain set";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_batch3_exit_withdraw_accept_path)
{
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 8; // Current level is not marked complete.
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 5);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");
    og::runtime::current_session->myscreen_->sync_world_from_save_data();

    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0; // enemies still present -> guys_here != 0

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 5);
    walker* eater = add_living(0);
    walker* ally = add_living(0);
    ASSERT_TRUE(exit_fx && eater && ally) << "exit/eater/ally created";
    if (!(exit_fx && eater && ally))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    ASSERT_TRUE(exit_fx->eat_me(eater)) << "withdraw accept path should return true";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world_.withdraw_requested) << "withdraw path should mark withdraw_requested for tick short-circuit";
    bool saw_withdraw_event = false;
    bool saw_prompt_event = false;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::WithdrawToLevel &&
            static_cast<short>(ev.a) == 5)
        {
            saw_withdraw_event = true;
        }
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation &&
            static_cast<short>(ev.a) == 5 && ev.b == 1)
        {
            saw_prompt_event = true;
        }
    }
    ASSERT_TRUE(saw_withdraw_event) << "withdraw accept path should emit WithdrawToLevel";
    ASSERT_TRUE(saw_prompt_event) << "withdraw accept path should emit withdraw prompt request";

    ASSERT_EQ(8, (int)og::runtime::current_session->myscreen_->save_data.scen_num) << "withdraw transition should be deferred to runtime prompt handling";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, screen_withdraw_aborts_when_autosave_load_fails)
{
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    screen* s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s != nullptr) << "active screen should be available";
    if (!s)
        return;

    s->save_data.reset();
    s->save_data.current_campaign = "org.openglad.missing-campaign";
    s->save_data.scen_num = 7;
    ASSERT_TRUE(s->save_data.save("save0")) << "fixture save with missing campaign should write";
    s->sync_world_from_save_data();

    walker* ally = add_living(0);
    walker* foe = add_living(1);
    ASSERT_TRUE(ally && foe) << "ally/foe should exist so the level does not auto-end";
    if (!(ally && foe))
        return;

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);

    sim_events.push_with_text(og::sim::EventKind::RequestExitConfirmation,
                              "Withdraw to Level 4?", 4, 1);
    sim_events.push(og::sim::EventKind::WithdrawToLevel, 4, 0);

    ASSERT_TRUE(s->act()) << "act should continue after failed withdraw load";
    ASSERT_EQ(7, static_cast<int>(s->save_data.scen_num)) << "failed withdraw load should not change scen_num";
    ASSERT_EQ(7, static_cast<int>(s->world_.current_scenario)) << "failed withdraw load should keep world scenario unchanged";
    ASSERT_EQ(static_cast<int>(SaveDataIoError::CampaignLoadFailed), static_cast<int>(s->save_data.last_io_error())) << "withdraw load failure should be surfaced as campaign load failure";
    ASSERT_TRUE(!s->world_.withdraw_requested) << "failed withdraw load should clear world.withdraw_requested";
    ASSERT_EQ(-1, static_cast<int>(s->world_.withdraw_level)) << "failed withdraw load should clear world.withdraw_level";

    picker_testing_yes_or_no_queue_clear();
    clear_level_lists();

    // Restore a valid baseline save for subsequent tests.
    s->save_data.reset();
    s->sync_world_from_save_data();
    ASSERT_TRUE(s->save_data.save("save0")) << "cleanup save0 should succeed";
}


TEST(RuntimeCoveragePaths, sim_world_tick_branches_for_end_freeze_and_cleanup)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(1337);
    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "current_game sim_events should be wired";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *current_game->sim_events;
    events.clear();
    world.my_team = 0;

    world.enemy_freeze = 2;
    world.end = 0;

    walker* ally = add_living(0);
    (void)ally;
    walker* foe = add_living(1);
    foe->team_num = 1;

    world.tick();
    ASSERT_EQ(1, static_cast<int>(world.enemy_freeze)) << "enemy_freeze should decrement";
    ASSERT_EQ(1, static_cast<int>(world.tick_count_)) << "first tick should increment tick_count";

    bool saw_set_palette = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::SetPalette)
            saw_set_palette = true;
    }
    ASSERT_TRUE(saw_set_palette) << "enemy_freeze transition to 1 should emit SetPalette";

    // end flag branch (when level_done is not 2).
    world.end = 1;
    events.clear();
    world.tick();
    ASSERT_TRUE(world.game_ended) << "end flag should terminate tick when battle not auto-finished";

    // Level_done==1 path via exit treasure and no enemies.
    clear_level_lists();
    add_treasure(FAMILY_EXIT, 2);
    world.enemy_freeze = 0;
    world.end = 0;
    events.clear();
    world.tick();
    ASSERT_TRUE(world.level_done == 1) << "exit with no foes should set level_done=1";
    ASSERT_TRUE(!world.game_ended) << "level_done=1 should not auto-end game";

    // Cleanup path: dead refs are nulled and dead entities removed.
    clear_level_lists();
    walker* owner = add_living(0);
    walker* dead_foe = add_living(1);
    owner->set_foe(dead_foe);
    owner->set_leader(dead_foe);
    owner->set_owner(dead_foe);
    owner->set_collide_ob(dead_foe);
    dead_foe->dead = 1;
    dead_foe->myguy = nullptr;
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(dead_fx != nullptr) << "expected fx walker";
    ASSERT_TRUE(dead_weap != nullptr) << "expected weapon walker";
    dead_fx->dead = 1;
    dead_weap->dead = 1;

    world.enemy_freeze = 0;
    world.end = 0;
    events.clear();
    world.tick();
    ASSERT_TRUE(owner->foe() == nullptr) << "dead foe pointer should be cleared";
    ASSERT_TRUE(owner->leader() == nullptr) << "dead leader pointer should be cleared";
    clear_level_lists();
}


TEST(RuntimeCoveragePaths, treasure_find_teleport_target_wraparound_and_missing_self)
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 6);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 6);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 6);
    ASSERT_TRUE(tele_a && tele_b && tele_c) << "teleporters created";
    if (!(tele_a && tele_b && tele_c))
        return;

    ASSERT_TRUE(tele_c->find_teleport_target() == tele_a) << "last teleporter should wrap to first matching teleporter";

    tele_a->stats()->level = 7;
    tele_b->dead = 1;
    ASSERT_TRUE(tele_c->find_teleport_target() == nullptr) << "teleporter should return nullptr when no live same-level target exists";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, sim_world_freeze_countdown_notification_and_weap_cleanup)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(2026);
    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "current_game sim_events should be wired";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *current_game->sim_events;
    events.clear();
    world.my_team = 1; // team 0 is hostile from this perspective

    // Non-friendly living should be frozen while freeze is active.
    walker* hostile_team0 = add_living(0);
    ASSERT_TRUE(hostile_team0 != nullptr) << "hostile team0 living created";
    if (!hostile_team0)
        return;
    hostile_team0->set_act_type(ACT_CONTROL);

    // Additional frozen enemy for loop iteration volume.
    walker* frozen_enemy = add_living(2);
    ASSERT_TRUE(frozen_enemy != nullptr) << "frozen enemy created";

    world.enemy_freeze = 11;
    world.end = 0;
    world.tick();
    ASSERT_EQ(10, (int)world.enemy_freeze) << "enemy_freeze should decrement from 11 to 10";
    ASSERT_EQ(2, (int)world.level_done) << "hostile living during freeze should stay frozen and not keep level active";
    ASSERT_TRUE(world.game_ended) << "when only frozen hostiles remain, tick should report level completion";

    int time_left_messages = 0;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification && ev.text.find("TIME LEFT:") != std::string::npos)
            time_left_messages++;
    }
    ASSERT_EQ(1, time_left_messages) << "freeze countdown should emit only one TIME LEFT notification per tick";

    // Weapon cleanup branch: clear dead pointer links and erase dead weapon/fx.
    clear_level_lists();
    walker* owner = add_living(0);
    walker* dead_ref = add_living(2);
    ASSERT_TRUE(owner && dead_ref) << "owner and dead ref created";
    if (!(owner && dead_ref))
        return;
    dead_ref->dead = 1;
    owner->set_foe(dead_ref);
    owner->set_leader(dead_ref);
    owner->set_owner(dead_ref);
    owner->set_collide_ob(dead_ref);

    walker* weap_owner = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weap_owner && dead_fx && dead_weap) << "weapon/fx walkers created";
    if (weap_owner) {
        weap_owner->set_foe(dead_ref);
        weap_owner->set_leader(dead_ref);
        weap_owner->set_owner(dead_ref);
        weap_owner->set_collide_ob(dead_ref);
    }
    if (dead_fx)
        dead_fx->dead = 1;
    if (dead_weap)
        dead_weap->dead = 1;

    world.enemy_freeze = 0;
    world.end = 0;
    events.clear();
    world.tick();
    ASSERT_TRUE(owner->foe() == nullptr) << "oblist dead foe pointer should be cleared";
    ASSERT_TRUE(owner->leader() == nullptr) << "oblist dead leader pointer should be cleared";
    ASSERT_TRUE(weap_owner == nullptr || weap_owner->foe() == nullptr) << "weaplist dead foe pointer should be cleared";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, runtime_score_panel_null_control_score_overlay_safe)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!v)
    {
        ASSERT_TRUE(false) << "view should exist";
        return;
    }

    walker* old_control = v->control;
    const unsigned char old_overlay = v->prefs[PREF_OVERLAY];
    const unsigned char old_score = v->prefs[PREF_SCORE];
    const unsigned char old_foes = v->prefs[PREF_FOES];
    const unsigned char old_life = v->prefs[PREF_LIFE];

    v->control = nullptr;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_LIFE] = PREF_LIFE_BOTH;

    ASSERT_EQ(1, static_cast<int>(new_score_panel(og::runtime::current_session->myscreen_, 1))) << "new_score_panel should tolerate null control when score overlay is on";

    v->control = old_control;
    v->prefs[PREF_OVERLAY] = old_overlay;
    v->prefs[PREF_SCORE] = old_score;
    v->prefs[PREF_FOES] = old_foes;
    v->prefs[PREF_LIFE] = old_life;
}


TEST(RuntimeCoveragePaths, sim_world_batch5_game_end_paths)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(99);
    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "current_game sim_events should be wired";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *current_game->sim_events;
    events.clear();
    world.my_team = 0;

    // No foes and no exits => level_done stays 2 and game ends.
    world.enemy_freeze = 0;
    world.end = 0;
    world.tick();
    ASSERT_TRUE(world.game_ended) << "empty level should trigger game_ended path";
    ASSERT_EQ(0, (int)world.ending) << "empty level ending should be 0";

    // end flag path when level is not fully done.
    clear_level_lists();
    walker* foe = add_living(1);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (!foe)
        return;
    foe->set_act_type(ACT_CONTROL);
    world.end = 1;
    world.tick();
    ASSERT_TRUE(world.game_ended) << "end flag should force game end";
    ASSERT_EQ(0, (int)world.level_done) << "hostile living should keep level_done at 0 before end flag handling";
}


TEST(RuntimeCoveragePaths, treasure_batch5_default_eat_and_missing_self_target_lookup)
{
    clear_level_lists();

    // Unknown treasure family should take default eat_me() return path.
    treasure standalone;
    standalone.set_order_family(Order::Treasure, 127);
    walker eater;
    ASSERT_TRUE(standalone.eat_me(&eater)) << "unknown treasure family should use default eat_me return true";

    // find_teleport_target should return nullptr when object is not in fxlist.
    standalone.set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    standalone.stats()->level = 4;
    ASSERT_TRUE(standalone.find_teleport_target() == nullptr) << "teleport target lookup should fail when teleporter is not present in fxlist";
}


TEST(RuntimeCoveragePaths, treasure_batch6_find_teleport_target_full_loop_paths)
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 5);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 5);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 6);
    ASSERT_TRUE(tele_a && tele_b && tele_c) << "teleporters created";
    if (!(tele_a && tele_b && tele_c))
        return;

    // Forward scan success branch.
    ASSERT_TRUE(tele_a->find_teleport_target() == tele_b) << "first teleporter should find next same-level teleporter";

    // Wraparound scan success branch (mark later candidate dead first).
    tele_b->dead = 1;
    ASSERT_TRUE(tele_c->find_teleport_target() == nullptr) << "mismatched level with dead later target should return nullptr";
    tele_b->dead = 0;
    tele_c->stats()->level = 5;
    ASSERT_TRUE(tele_c->find_teleport_target() == tele_a) << "last teleporter should wrap around to first same-level teleporter";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(6060);
    world.my_team = 0;
    const short saved_allied_mode = world.allied_mode;
    world.allied_mode = 0;

    walker* ally = add_living(0);
    walker* hostile = add_living(1);
    (void)add_treasure(FAMILY_EXIT, 1);
    ASSERT_TRUE(ally && hostile) << "ally/hostile created";
    if (!(ally && hostile))
        return;
    ally->set_act_type(ACT_CONTROL);
    hostile->set_act_type(ACT_CONTROL);

    // Force find_far_foe path by clearing references.
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);

    // Dead linked object used for pointer cleanup.
    walker* dead_link = add_living(2);
    ASSERT_TRUE(dead_link != nullptr) << "dead link created";
    if (!dead_link)
        return;
    dead_link->dead = 1;
    ally->set_owner(dead_link);
    ally->set_collide_ob(dead_link);
    hostile->set_foe(dead_link);
    hostile->set_leader(dead_link);
    hostile->set_owner(dead_link);
    hostile->set_collide_ob(dead_link);

    walker* weap_owner = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weap_owner && dead_fx && dead_weap) << "weapon/fx created";
    if (!(weap_owner && dead_fx && dead_weap))
        return;
    weap_owner->set_foe(dead_link);
    weap_owner->set_leader(dead_link);
    weap_owner->set_owner(dead_link);
    weap_owner->set_collide_ob(dead_link);
    dead_fx->dead = 1;
    dead_weap->dead = 1;

    // Dead living without myguy should decrement numobs during erase.
    walker* dead_living = add_living(3);
    ASSERT_TRUE(dead_living != nullptr) << "dead living created";
    if (!dead_living)
        return;
    dead_living->myguy = nullptr;
    dead_living->dead = 1;

    world.enemy_freeze = 0;
    world.end = 0;
    world.tick();
    ASSERT_TRUE(ally->owner() == nullptr && ally->collide_ob() == nullptr) << "dead links should be cleared on oblist entities";
    ASSERT_TRUE(hostile->foe() == nullptr && hostile->leader() == nullptr) << "all dead references should be cleared";
    (void)weap_owner;

    clear_level_lists();
    world.allied_mode = saved_allied_mode;
}


TEST(RuntimeCoveragePaths, sim_world_freeze_branch_allows_non_living_actions)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(777);
    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "current_game sim_events should be wired";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *current_game->sim_events;
    events.clear();
    world.my_team = 0;

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen != nullptr) << "generator created";
    if (!gen)
        return;
    gen->team_num = 2;
    gen->set_act_type(ACT_CONTROL); // deterministic no-op-ish act path

    world.enemy_freeze = 11;
    world.end = 0;
    world.tick();
    ASSERT_EQ(10, (int)world.enemy_freeze) << "freeze counter should decrement";
    ASSERT_TRUE(world.game_ended) << "no hostile living and no exits should auto-end level";

    bool saw_time_left = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification && ev.text.find("TIME LEFT:") != std::string::npos)
            saw_time_left = true;
    }
    ASSERT_TRUE(saw_time_left) << "freeze branch should emit countdown notification on modulo-10 ticks";
}


TEST(RuntimeCoveragePaths, sim_world_assigns_far_foe_when_no_target_and_hostiles_present)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(9090);
    world.my_team = 0;

    walker* ally = add_living(0);
    walker* foe_near = add_living(1);
    walker* foe_far = add_living(1);
    ASSERT_TRUE(ally && foe_near && foe_far) << "ally and foes created";
    if (!(ally && foe_near && foe_far))
        return;

    ally->set_act_type(ACT_CONTROL);
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);
    foe_near->setxy(140, 100);
    foe_far->setxy(300, 100);

    world.enemy_freeze = 0;
    world.end = 0;
    world.tick();
    ASSERT_EQ(0, (int)world.level_done) << "hostile living should keep level unfinished";
    ASSERT_TRUE(ally->foe() != nullptr) << "sim world should assign a far foe when none is set";
    ASSERT_TRUE(ally->foe() == foe_near) << "nearest hostile should be selected as far foe";
}


TEST(RuntimeCoveragePaths, sim_world_round8_end_flag_short_circuit_and_palette_unfreeze_event)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(1234);
    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "current_game sim_events should be wired";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *current_game->sim_events;
    events.clear();
    world.my_team = 0;

    walker* ally = add_living(0);
    walker* hostile = add_living(1);
    ASSERT_TRUE(ally && hostile) << "ally/hostile created";
    if (!(ally && hostile))
        return;
    ally->set_act_type(ACT_CONTROL);
    hostile->set_act_type(ACT_CONTROL);

    world.enemy_freeze = 2; // decrements to 1 -> SetPalette(0) branch
    world.end = 1;          // explicit end short-circuit branch
    world.tick();
    ASSERT_TRUE(world.game_ended) << "end flag should force game_ended";
    ASSERT_EQ(1, (int)world.enemy_freeze) << "freeze should decrement before end short-circuit";

    bool saw_palette_reset = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::SetPalette && ev.a == 0)
            saw_palette_reset = true;
    }
    ASSERT_TRUE(saw_palette_reset) << "enemy_freeze transition to 1 should emit SetPalette reset event";

    clear_level_lists();
}


TEST(RuntimeCoveragePaths, sim_world_round9_no_hostiles_or_exit_sets_next_level_and_ending_zero)
{
    clear_level_lists();

    GameWorld& world = setup_tick_world(31337);
    world.my_team = 0;

    // Only friendly living => level_done remains 2 and should trigger game end path.
    walker* ally = add_living(0);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    if (!ally)
        return;
    ally->set_act_type(ACT_CONTROL);

    og::runtime::current_session->myscreen_->world().id = 41;
    world.enemy_freeze = 0;
    world.end = 0;
    world.tick();

    ASSERT_TRUE(world.game_ended) << "no hostiles and no exits should end level";
    ASSERT_EQ(0, (int)world.ending) << "auto-end path should set ending to zero";
    ASSERT_EQ(42, (int)world.next_level) << "auto-end path should advance to next level id";

    clear_level_lists();
}


// --- Issue #98 regression tests: exits triggerable without beating scenario ---

TEST(RuntimeCoveragePaths, issue98_can_exit_flag_should_show_exit_not_withdraw)
{
    // Regression test for GitHub issue #98.
    // When TYPE_CAN_EXIT_WHENEVER is set and Withdraw conditions are also met,
    // the Exit dialog should show (not Withdraw). Before the fix, the Withdraw
    // dialog fired first, and accepting it changed scen_num (retreat behavior).
    // After the fix, the Exit dialog fires instead and scen_num is unchanged
    // (normal level completion path).
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    // Setup: CAN_EXIT_WHENEVER flag, enemies still present, dest level completed,
    // current scenario NOT completed → both Withdraw AND Exit conditions met.
    og::runtime::current_session->myscreen_->world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0; // enemies still present

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5; // current level (not completed)
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");
    og::runtime::current_session->myscreen_->sync_world_from_save_data();

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3); // exit points to level 3
    walker* eater = add_living(0);
    ASSERT_TRUE(exit_fx && eater) << "exit/eater created";
    if (!(exit_fx && eater))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    exit_fx->eat_me(eater);
    bool saw_exit_prompt = false;
    bool saw_withdraw_event = false;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation &&
            static_cast<short>(ev.a) == 3 && ev.b == 0)
        {
            saw_exit_prompt = true;
        }
        if (ev.kind == og::sim::EventKind::WithdrawToLevel)
            saw_withdraw_event = true;
    }
    ASSERT_TRUE(saw_exit_prompt) << "CAN_EXIT_WHENEVER path should emit normal exit prompt";
    ASSERT_TRUE(!saw_withdraw_event) << "CAN_EXIT_WHENEVER path should not emit withdraw event";

    // Exit trigger should not mutate save immediately; transition is deferred.
    ASSERT_EQ(5, static_cast<int>(og::runtime::current_session->myscreen_->save_data.scen_num)) << "CAN_EXIT_WHENEVER should show Exit (scen_num unchanged), not Withdraw";

    og::runtime::current_session->myscreen_->world().type = 0;
    clear_level_lists();
}


TEST(RuntimeCoveragePaths, issue98_no_double_dialog_on_withdraw_exit)
{
    // Regression test for GitHub issue #98.
    // Before the fix, when CAN_EXIT_WHENEVER was set and Withdraw conditions
    // were met, BOTH Withdraw and Exit dialogs fired in sequence — consuming
    // two prompt answers. After the fix, only one dialog fires.
    //
    // We verify this by pushing 2 answers and checking how many remain:
    // - Buggy code: 2 dialogs fire → 2 answers consumed → 0 remaining
    // - Fixed code: 1 dialog fires → 1 answer consumed → 1 remaining
    clear_level_lists();

    ASSERT_TRUE(current_game != nullptr && current_game->sim_events != nullptr) << "sim events should be available";
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& sim_events = *current_game->sim_events;
    sim_events.clear();

    og::runtime::current_session->myscreen_->world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0; // enemies still present

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");
    og::runtime::current_session->myscreen_->sync_world_from_save_data();

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3);
    walker* eater = add_living(0);
    ASSERT_TRUE(exit_fx && eater) << "exit/eater created";
    if (!(exit_fx && eater))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    exit_fx->eat_me(eater);
    int confirmation_events = 0;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
            confirmation_events++;
    }
    ASSERT_EQ(1, confirmation_events) << "Only one confirmation event should be emitted";

    og::runtime::current_session->myscreen_->world().type = 0;
    clear_level_lists();
}

