#include <openglad/data/gparser.h>
#include <openglad/entities/treasure.h>
#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/screen_lifecycle.h>
#include <openglad/render/view.h>
#include <openglad/core/constants.h>
#include <openglad/core/terrain_types.h>
#include <openglad/sim/event.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>

#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)
short new_score_panel(screen* s, short do_it);

// TESTING-only helpers from picker_dialogs.cpp.
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();

namespace {

void tick_world(og::gameplay::GameWorld& world, og::sim::SimEventLog& events)
{
    og::gameplay::GameplayContext local_ctx;
    local_ctx.world = &world;
    local_ctx.sim_events = &events;
    og::gameplay::GameplayContext* prev = og::gameplay::current_game;
    og::gameplay::current_game = &local_ctx;
    world.tick();
    og::gameplay::current_game = prev;
}

void clear_level_lists()
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().level_done = 0;
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
    auto& level = og::runtime::current_session->myscreen_->world();
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.grid.w || gy >= level.grid.h)
        return;
    level.grid.data[gx + level.grid.w * gy] = tile;
}

} // namespace

void test_input_bridge_window_and_key_paths()
{
    const float saved_window_w = og::runtime::current_session->window_w_;
    const float saved_window_h = og::runtime::current_session->window_h_;
    const float saved_overscan = og::runtime::current_session->overscan_percentage_;
    const bool saved_continue = og::runtime::current_session->input_continue_;
    const short saved_key_press_event = og::runtime::current_session->key_press_event_;
    const int saved_raw_key = og::runtime::current_session->raw_key_;

    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_MINIMIZED;
    handle_window_event(e);

    e.window.event = SDL_WINDOWEVENT_CLOSE;
    handle_window_event(e);

    e.window.event = SDL_WINDOWEVENT_RESTORED;
    handle_window_event(e);

    og::runtime::current_session->overscan_percentage_ = -1.0f;
    e.window.event = SDL_WINDOWEVENT_RESIZED;
    e.window.data1 = 1280;
    e.window.data2 = 720;
    handle_window_event(e);
    TEST_ASSERT_EQ(1280, static_cast<int>(og::runtime::current_session->window_w_), "resize should update window_w");
    TEST_ASSERT_EQ(720, static_cast<int>(og::runtime::current_session->window_h_), "resize should update window_h");
    TEST_ASSERT(og::runtime::current_session->overscan_percentage_ == 0.0f, "resize path should clamp overscan");

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
    TEST_ASSERT(og::runtime::current_session->overscan_percentage_ >= 0.0f && og::runtime::current_session->overscan_percentage_ <= 0.25f,
                "F12+Ctrl should reload and clamp overscan");

    og::runtime::current_session->input_continue_ = false;
    og::runtime::current_session->key_press_event_ = 0;
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_ESCAPE;
    key.key.keysym.mod = 0;
    handle_key_event(key);
    TEST_ASSERT(og::runtime::current_session->input_continue_, "escape keydown should set continue");
    TEST_ASSERT_EQ(1, static_cast<int>(og::runtime::current_session->key_press_event_), "keydown should set key_press_event");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_ESCAPE), og::runtime::current_session->raw_key_, "keydown should update raw_key");

    key.type = SDL_KEYUP;
    key.key.keysym.sym = SDLK_ESCAPE;
    handle_key_event(key);

    og::runtime::current_session->window_w_ = saved_window_w;
    og::runtime::current_session->window_h_ = saved_window_h;
    og::runtime::current_session->overscan_percentage_ = saved_overscan;
    og::runtime::current_session->input_continue_ = saved_continue;
    og::runtime::current_session->key_press_event_ = saved_key_press_event;
    og::runtime::current_session->raw_key_ = saved_raw_key;
    update_overscan_setting();
}
REGISTER_TEST(test_input_bridge_window_and_key_paths);

void test_screen_lifecycle_session_owner_paths()
{
    destroy_global_screen();
    TEST_ASSERT(global_session_owner() == nullptr, "destroy_global_screen should clear owner");

    screen* recreated = create_global_screen(1);
    TEST_ASSERT(recreated != nullptr, "create_global_screen should recreate session");
    TEST_ASSERT(global_session_owner() != nullptr, "session owner should exist after create");

    destroy_global_session();
    TEST_ASSERT(global_session_owner() == nullptr, "destroy_global_session should clear owner");

    // Restore expected global screen for subsequent tests.
    TEST_ASSERT(create_global_screen(1) != nullptr, "recreate global screen for remaining tests");
}
REGISTER_TEST(test_screen_lifecycle_session_owner_paths);

void test_treasure_core_methods_and_teleport_target_search()
{
    clear_level_lists();

    treasure standalone;
    TEST_ASSERT(standalone.act(), "treasure::act should return true");
    standalone.set_direct_frame(7);
    TEST_ASSERT_EQ(7, standalone.frame, "set_direct_frame should update frame");
    walker eater_default;
    TEST_ASSERT(standalone.eat_me(&eater_default), "eat_me should safely return true without descriptor");

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 3);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 3);
    TEST_ASSERT(tele_a->find_teleport_target() == tele_b, "teleporter should find next live matching target");

    tele_b->dead = 1;
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 3);
    TEST_ASSERT(tele_a->find_teleport_target() == tele_c, "teleporter should skip dead targets");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_core_methods_and_teleport_target_search);

void test_treasure_exit_and_teleporter_navigation_paths()
{
    clear_level_lists();

    static og::sim::SimEventLog sim_events;
    static ProductionRandom rng;
    sim_events.clear();
    og::runtime::current_session->myscreen_->world().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze, &sim_events, &rng, &cfg);

    // Exit flow: no enemies left + prompt accepted should emit EndGame.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->world().level_done = 1;
    treasure* exit_fx = add_treasure(FAMILY_EXIT, 2);
    walker* controller = add_living(0);
    controller->set_act_type(ACT_CONTROL);
    controller->skip_exit = 0;
    controller->in_act = false;

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    TEST_ASSERT(exit_fx->eat_me(controller), "exit eater path should return true");
    TEST_ASSERT_EQ(10, static_cast<int>(controller->skip_exit), "exit path should set skip_exit debounce");

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
    TEST_ASSERT(mover->skip_exit >= 20, "teleport should increase skip_exit cooldown");
    TEST_ASSERT(tele_1->leader == tele_2, "teleport should select the linked target");

    // Teleporter close-range debounce path.
    mover->setxy(100, 100);
    mover->skip_exit = 1;
    tele_1->eat_me(mover);
    TEST_ASSERT_EQ(8, static_cast<int>(mover->skip_exit), "close + skip_exit path should set skip_exit=8");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_exit_and_teleporter_navigation_paths);

void test_treasure_navigation_early_returns_and_withdraw_decline()
{
    clear_level_lists();

    static og::sim::SimEventLog sim_events;
    static ProductionRandom rng;
    sim_events.clear();
    og::runtime::current_session->myscreen_->world().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze, &sim_events, &rng, &cfg);

    // Exit early return: eater currently in act.
    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3);
    walker* eater = add_living(0);
    TEST_ASSERT(exit_fx != nullptr && eater != nullptr, "exit/eater created");
    if (!(exit_fx && eater))
        return;
    eater->set_act_type(ACT_CONTROL);
    eater->skip_exit = 0;
    eater->in_act = true;
    TEST_ASSERT(exit_fx->eat_me(eater), "in_act early return should succeed");
    TEST_ASSERT_EQ(0, static_cast<int>(eater->skip_exit), "in_act path should not update skip_exit");

    // Exit early return: not ACT_CONTROL.
    eater->in_act = false;
    eater->set_act_type(0);
    TEST_ASSERT(exit_fx->eat_me(eater), "non-control early return should succeed");
    TEST_ASSERT_EQ(0, static_cast<int>(eater->skip_exit), "non-control path should not update skip_exit");

    // Withdraw branch with decline: level is completed, current isn't, enemies still present.
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    og::runtime::current_session->myscreen_->world().level_done = 0; // enemies still present
    eater->set_act_type(ACT_CONTROL);
    eater->skip_exit = 0;
    exit_fx->stats()->level = 3;
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    TEST_ASSERT(exit_fx->eat_me(eater), "withdraw decline path should return true");
    TEST_ASSERT_EQ(10, static_cast<int>(eater->skip_exit), "withdraw prompt path should set skip_exit debounce");

    // Teleporter early returns: skip_exit > 1 and distance too far.
    clear_level_lists();
    treasure* tele = add_treasure(FAMILY_TELEPORTER, 9);
    walker* mover = add_living(0);
    TEST_ASSERT(tele != nullptr && mover != nullptr, "teleporter/mover created");
    if (!(tele && mover))
        return;

    tele->setxy(200, 200);
    mover->setxy(200, 200);
    mover->skip_exit = 5;
    TEST_ASSERT(tele->eat_me(mover), "teleporter skip_exit guard should return true");
    TEST_ASSERT_EQ(5, static_cast<int>(mover->skip_exit), "skip_exit guard should not alter cooldown");

    mover->skip_exit = 0;
    mover->setxy(400, 400);
    TEST_ASSERT(tele->eat_me(mover), "teleporter far-distance guard should return true");
    TEST_ASSERT_EQ(0, static_cast<int>(mover->skip_exit), "far-distance path should not alter cooldown");

    // No target teleporter available.
    mover->setxy(200, 200);
    TEST_ASSERT(tele->eat_me(mover), "teleporter without target should return true");
    TEST_ASSERT(mover->skip_exit >= 20, "no-target path still applies cooldown increment");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_navigation_early_returns_and_withdraw_decline);

void test_treasure_batch3_find_target_wraparound_and_no_match()
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 7);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 8);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 7);
    TEST_ASSERT(tele_a && tele_b && tele_c, "teleporters created");
    if (!(tele_a && tele_b && tele_c))
        return;

    tele_b->dead = 1;
    TEST_ASSERT(tele_c->find_teleport_target() == tele_a,
                "teleporter should wrap to earlier matching target when no later target matches");

    tele_a->dead = 1;
    TEST_ASSERT(tele_c->find_teleport_target() == nullptr,
                "teleporter should return nullptr when no live matching target exists");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_batch3_find_target_wraparound_and_no_match);

void test_treasure_batch3_teleporter_leader_and_blocked_destination()
{
    clear_level_lists();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    treasure* tele_src = add_treasure(FAMILY_TELEPORTER, 4);
    treasure* tele_dst = add_treasure(FAMILY_TELEPORTER, 4);
    walker* mover = add_living(0);
    TEST_ASSERT(tele_src && tele_dst && mover, "teleporter source/destination and mover created");
    if (!(tele_src && tele_dst && mover))
        return;

    tele_src->setxy(100, 100);
    tele_dst->setxy(160, 160);
    tele_src->leader = tele_dst; // Force the "use leader" branch.
    mover->setxy(100, 100);
    mover->skip_exit = 0;

    // Make destination impassable so teleporter recenters mover back to source.
    set_world_tile(160, 160, PIX_H_WALL1);

    TEST_ASSERT(tele_src->eat_me(mover), "teleporter eat should still return true when destination blocked");
    TEST_ASSERT_EQ(100, (int)mover->xpos, "blocked destination should recenter mover to source X");
    TEST_ASSERT_EQ(100, (int)mover->ypos, "blocked destination should recenter mover to source Y");
    TEST_ASSERT(tele_src->leader == tele_dst, "leader-based destination should remain set");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_batch3_teleporter_leader_and_blocked_destination);

void test_treasure_batch3_exit_withdraw_accept_path()
{
    clear_level_lists();

    static og::sim::SimEventLog sim_events;
    static ProductionRandom rng;
    sim_events.clear();
    og::runtime::current_session->myscreen_->world().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze, &sim_events, &rng, &cfg);

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 8; // Current level is not marked complete.
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 5);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");
    og::runtime::current_session->myscreen_->world().current_scenario =
        og::runtime::current_session->myscreen_->save_data.scen_num;
    og::runtime::current_session->myscreen_->world().completed_levels = {5};

    og::runtime::current_session->myscreen_->world().level_done = 0; // enemies still present -> guys_here != 0

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 5);
    walker* eater = add_living(0);
    walker* ally = add_living(0);
    TEST_ASSERT(exit_fx && eater && ally, "exit/eater/ally created");
    if (!(exit_fx && eater && ally))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    TEST_ASSERT(exit_fx->eat_me(eater), "withdraw accept path should return true");
    TEST_ASSERT_EQ(8, static_cast<int>(og::runtime::current_session->myscreen_->save_data.scen_num),
                   "entity-side withdraw should defer transition; scen_num remains unchanged");
    TEST_ASSERT(og::runtime::current_session->myscreen_->world().withdraw_requested,
                "withdraw branch should set world.withdraw_requested");

    bool saw_request = false;
    bool saw_withdraw = false;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation &&
            ev.b == 1 &&
            static_cast<short>(ev.a) == static_cast<short>(exit_fx->stats()->level))
            saw_request = true;
        if (ev.kind == og::sim::EventKind::WithdrawToLevel &&
            static_cast<short>(ev.a) == static_cast<short>(exit_fx->stats()->level))
            saw_withdraw = true;
    }
    TEST_ASSERT(saw_request, "withdraw branch should emit RequestExitConfirmation");
    TEST_ASSERT(saw_withdraw, "withdraw branch should emit WithdrawToLevel");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_batch3_exit_withdraw_accept_path);

void test_game_world_tick_branches_for_end_freeze_and_cleanup()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 1337;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    std::int32_t enemy_freeze = 2;
    char end = 0;

    walker* ally = add_living(0);
    (void)ally;
    walker* foe = add_living(1);
    foe->team_num = 1;

    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT_EQ(1, static_cast<int>(enemy_freeze), "enemy_freeze should decrement");
    TEST_ASSERT_EQ(1, static_cast<int>(world.tick_count_), "first tick should increment tick_count");

    bool saw_set_palette = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::SetPalette)
            saw_set_palette = true;
    }
    TEST_ASSERT(saw_set_palette, "enemy_freeze transition to 1 should emit SetPalette");

    // end flag branch (when level_done is not 2).
    end = 1;
    events.clear();
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(world.game_ended, "end flag should terminate tick when battle not auto-finished");

    // Level_done==1 path via exit treasure and no enemies.
    clear_level_lists();
    add_treasure(FAMILY_EXIT, 2);
    enemy_freeze = 0;
    end = 0;
    events.clear();
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(world.level_done == 1, "exit with no foes should set level_done=1");
    TEST_ASSERT(!world.game_ended, "level_done=1 should not auto-end game");

    // Cleanup path: dead refs are nulled and dead entities removed.
    clear_level_lists();
    walker* owner = add_living(0);
    walker* dead_foe = add_living(1);
    owner->foe = dead_foe;
    owner->leader = dead_foe;
    owner->owner = dead_foe;
    owner->collide_ob = dead_foe;
    dead_foe->dead = 1;
    dead_foe->myguy = nullptr;
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(dead_fx != nullptr, "expected fx walker");
    TEST_ASSERT(dead_weap != nullptr, "expected weapon walker");
    dead_fx->dead = 1;
    dead_weap->dead = 1;

    enemy_freeze = 0;
    end = 0;
    events.clear();
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(owner->foe == nullptr, "dead foe pointer should be cleared");
    TEST_ASSERT(owner->leader == nullptr, "dead leader pointer should be cleared");
    clear_level_lists();
}
REGISTER_TEST(test_game_world_tick_branches_for_end_freeze_and_cleanup);

void test_treasure_find_teleport_target_wraparound_and_missing_self()
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 6);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 6);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 6);
    TEST_ASSERT(tele_a && tele_b && tele_c, "teleporters created");
    if (!(tele_a && tele_b && tele_c))
        return;

    TEST_ASSERT(tele_c->find_teleport_target() == tele_a,
                "last teleporter should wrap to first matching teleporter");

    tele_a->stats()->level = 7;
    tele_b->dead = 1;
    TEST_ASSERT(tele_c->find_teleport_target() == nullptr,
                "teleporter should return nullptr when no live same-level target exists");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_find_teleport_target_wraparound_and_missing_self);

void test_game_world_freeze_countdown_notification_and_weap_cleanup()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 2026;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 1; // team 0 is hostile from this perspective

    // Non-friendly living should be frozen while freeze is active.
    walker* hostile_team0 = add_living(0);
    TEST_ASSERT(hostile_team0 != nullptr, "hostile team0 living created");
    if (!hostile_team0)
        return;
    hostile_team0->set_act_type(ACT_CONTROL);

    // Additional frozen enemy for loop iteration volume.
    walker* frozen_enemy = add_living(2);
    TEST_ASSERT(frozen_enemy != nullptr, "frozen enemy created");

    std::int32_t enemy_freeze = 11;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT_EQ(10, (int)enemy_freeze, "enemy_freeze should decrement from 11 to 10");
    TEST_ASSERT_EQ(2, (int)world.level_done, "hostile living during freeze should stay frozen and not keep level active");
    TEST_ASSERT(world.game_ended, "when only frozen hostiles remain, tick should report level completion");

    int time_left_messages = 0;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification && ev.text.find("TIME LEFT:") != std::string::npos)
            time_left_messages++;
    }
    TEST_ASSERT_EQ(1, time_left_messages, "freeze countdown should emit only one TIME LEFT notification per tick");

    // Weapon cleanup branch: clear dead pointer links and erase dead weapon/fx.
    clear_level_lists();
    walker* owner = add_living(0);
    walker* dead_ref = add_living(2);
    TEST_ASSERT(owner && dead_ref, "owner and dead ref created");
    if (!(owner && dead_ref))
        return;
    dead_ref->dead = 1;
    owner->foe = dead_ref;
    owner->leader = dead_ref;
    owner->owner = dead_ref;
    owner->collide_ob = dead_ref;

    walker* weap_owner = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weap_owner && dead_fx && dead_weap, "weapon/fx walkers created");
    if (weap_owner) {
        weap_owner->foe = dead_ref;
        weap_owner->leader = dead_ref;
        weap_owner->owner = dead_ref;
        weap_owner->collide_ob = dead_ref;
    }
    if (dead_fx)
        dead_fx->dead = 1;
    if (dead_weap)
        dead_weap->dead = 1;

    enemy_freeze = 0;
    end = 0;
    events.clear();
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(owner->foe == nullptr, "oblist dead foe pointer should be cleared");
    TEST_ASSERT(owner->leader == nullptr, "oblist dead leader pointer should be cleared");
    TEST_ASSERT(weap_owner == nullptr || weap_owner->foe == nullptr, "weaplist dead foe pointer should be cleared");

    clear_level_lists();
}
REGISTER_TEST(test_game_world_freeze_countdown_notification_and_weap_cleanup);

void test_runtime_score_panel_null_control_score_overlay_safe()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!v)
    {
        TEST_ASSERT(false, "view should exist");
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

    TEST_ASSERT_EQ(1, static_cast<int>(new_score_panel(og::runtime::current_session->myscreen_, 1)),
                   "new_score_panel should tolerate null control when score overlay is on");

    v->control = old_control;
    v->prefs[PREF_OVERLAY] = old_overlay;
    v->prefs[PREF_SCORE] = old_score;
    v->prefs[PREF_FOES] = old_foes;
    v->prefs[PREF_LIFE] = old_life;
}
REGISTER_TEST(test_runtime_score_panel_null_control_score_overlay_safe);

void test_game_world_batch5_game_end_paths()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 99;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    // No foes and no exits => level_done stays 2 and game ends.
    std::int32_t enemy_freeze = 0;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(world.game_ended, "empty level should trigger game_ended path");
    TEST_ASSERT_EQ(0, (int)world.ending, "empty level ending should be 0");

    // end flag path when level is not fully done.
    clear_level_lists();
    walker* foe = add_living(1);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (!foe)
        return;
    foe->set_act_type(ACT_CONTROL);
    end = 1;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(world.game_ended, "end flag should force game end");
    TEST_ASSERT_EQ(0, (int)world.level_done, "hostile living should keep level_done at 0 before end flag handling");
}
REGISTER_TEST(test_game_world_batch5_game_end_paths);

void test_treasure_batch5_default_eat_and_missing_self_target_lookup()
{
    clear_level_lists();

    // Unknown treasure family should take default eat_me() return path.
    treasure standalone;
    standalone.set_order_family(Order::Treasure, 127);
    walker eater;
    TEST_ASSERT(standalone.eat_me(&eater), "unknown treasure family should use default eat_me return true");

    // find_teleport_target should return nullptr when object is not in fxlist.
    standalone.set_order_family(Order::Treasure, FAMILY_TELEPORTER);
    standalone.stats()->level = 4;
    TEST_ASSERT(standalone.find_teleport_target() == nullptr,
                "teleport target lookup should fail when teleporter is not present in fxlist");
}
REGISTER_TEST(test_treasure_batch5_default_eat_and_missing_self_target_lookup);

void test_treasure_batch6_find_teleport_target_full_loop_paths()
{
    clear_level_lists();

    treasure* tele_a = add_treasure(FAMILY_TELEPORTER, 5);
    treasure* tele_b = add_treasure(FAMILY_TELEPORTER, 5);
    treasure* tele_c = add_treasure(FAMILY_TELEPORTER, 6);
    TEST_ASSERT(tele_a && tele_b && tele_c, "teleporters created");
    if (!(tele_a && tele_b && tele_c))
        return;

    // Forward scan success branch.
    TEST_ASSERT(tele_a->find_teleport_target() == tele_b,
                "first teleporter should find next same-level teleporter");

    // Wraparound scan success branch (mark later candidate dead first).
    tele_b->dead = 1;
    TEST_ASSERT(tele_c->find_teleport_target() == nullptr,
                "mismatched level with dead later target should return nullptr");
    tele_b->dead = 0;
    tele_c->stats()->level = 5;
    TEST_ASSERT(tele_c->find_teleport_target() == tele_a,
                "last teleporter should wrap around to first same-level teleporter");

    clear_level_lists();
}
REGISTER_TEST(test_treasure_batch6_find_teleport_target_full_loop_paths);

void test_game_world_batch6_cleanup_and_erase_paths_with_hostiles_present()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 6060;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;
    const short saved_allied_mode = og::runtime::current_session->myscreen_->save_data.allied_mode;
    og::runtime::current_session->myscreen_->save_data.allied_mode = 0;

    walker* ally = add_living(0);
    walker* hostile = add_living(1);
    (void)add_treasure(FAMILY_EXIT, 1);
    TEST_ASSERT(ally && hostile, "ally/hostile created");
    if (!(ally && hostile))
        return;
    ally->set_act_type(ACT_CONTROL);
    hostile->set_act_type(ACT_CONTROL);

    // Force find_far_foe path by clearing references.
    ally->foe = nullptr;
    ally->leader = nullptr;

    // Dead linked object used for pointer cleanup.
    walker* dead_link = add_living(2);
    TEST_ASSERT(dead_link != nullptr, "dead link created");
    if (!dead_link)
        return;
    dead_link->dead = 1;
    ally->owner = dead_link;
    ally->collide_ob = dead_link;
    hostile->foe = dead_link;
    hostile->leader = dead_link;
    hostile->owner = dead_link;
    hostile->collide_ob = dead_link;

    walker* weap_owner = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    walker* dead_fx = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weap_owner && dead_fx && dead_weap, "weapon/fx created");
    if (!(weap_owner && dead_fx && dead_weap))
        return;
    weap_owner->foe = dead_link;
    weap_owner->leader = dead_link;
    weap_owner->owner = dead_link;
    weap_owner->collide_ob = dead_link;
    dead_fx->dead = 1;
    dead_weap->dead = 1;

    // Dead living without myguy should decrement numobs during erase.
    walker* dead_living = add_living(3);
    TEST_ASSERT(dead_living != nullptr, "dead living created");
    if (!dead_living)
        return;
    dead_living->myguy = nullptr;
    dead_living->dead = 1;

    std::int32_t enemy_freeze = 0;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(ally->owner == nullptr && ally->collide_ob == nullptr, "dead links should be cleared on oblist entities");
    TEST_ASSERT(hostile->foe == nullptr && hostile->leader == nullptr, "all dead references should be cleared");
    (void)weap_owner;

    clear_level_lists();
    og::runtime::current_session->myscreen_->save_data.allied_mode = saved_allied_mode;
}
REGISTER_TEST(test_game_world_batch6_cleanup_and_erase_paths_with_hostiles_present);

void test_game_world_freeze_branch_allows_non_living_actions()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 777;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    walker* gen = og::runtime::current_session->myscreen_->world().add_ob(Order::Generator, FAMILY_TENT);
    TEST_ASSERT(gen != nullptr, "generator created");
    if (!gen)
        return;
    gen->team_num = 2;
    gen->set_act_type(ACT_CONTROL); // deterministic no-op-ish act path

    std::int32_t enemy_freeze = 11;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT_EQ(10, (int)enemy_freeze, "freeze counter should decrement");
    TEST_ASSERT(world.game_ended, "no hostile living and no exits should auto-end level");

    bool saw_time_left = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::Notification && ev.text.find("TIME LEFT:") != std::string::npos)
            saw_time_left = true;
    }
    TEST_ASSERT(saw_time_left, "freeze branch should emit countdown notification on modulo-10 ticks");
}
REGISTER_TEST(test_game_world_freeze_branch_allows_non_living_actions);

void test_game_world_assigns_far_foe_when_no_target_and_hostiles_present()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 9090;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    walker* ally = add_living(0);
    walker* foe_near = add_living(1);
    walker* foe_far = add_living(1);
    TEST_ASSERT(ally && foe_near && foe_far, "ally and foes created");
    if (!(ally && foe_near && foe_far))
        return;

    ally->set_act_type(ACT_CONTROL);
    ally->foe = nullptr;
    ally->leader = nullptr;
    foe_near->setxy(140, 100);
    foe_far->setxy(300, 100);

    std::int32_t enemy_freeze = 0;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT_EQ(0, (int)world.level_done, "hostile living should keep level unfinished");
    TEST_ASSERT(ally->foe != nullptr, "sim world should assign a far foe when none is set");
    TEST_ASSERT(ally->foe == foe_near, "nearest hostile should be selected as far foe");
}
REGISTER_TEST(test_game_world_assigns_far_foe_when_no_target_and_hostiles_present);

void test_game_world_round8_end_flag_short_circuit_and_palette_unfreeze_event()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 1234;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    walker* ally = add_living(0);
    walker* hostile = add_living(1);
    TEST_ASSERT(ally && hostile, "ally/hostile created");
    if (!(ally && hostile))
        return;
    ally->set_act_type(ACT_CONTROL);
    hostile->set_act_type(ACT_CONTROL);

    std::int32_t enemy_freeze = 2; // decrements to 1 -> SetPalette(0) branch
    char end = 1;                  // explicit end short-circuit branch
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;
    TEST_ASSERT(world.game_ended, "end flag should force game_ended");
    TEST_ASSERT_EQ(1, (int)enemy_freeze, "freeze should decrement before end short-circuit");

    bool saw_palette_reset = false;
    for (const auto& ev : events.events())
    {
        if (ev.kind == og::sim::EventKind::SetPalette && ev.a == 0)
            saw_palette_reset = true;
    }
    TEST_ASSERT(saw_palette_reset, "enemy_freeze transition to 1 should emit SetPalette reset event");

    clear_level_lists();
}
REGISTER_TEST(test_game_world_round8_end_flag_short_circuit_and_palette_unfreeze_event);

void test_game_world_round9_no_hostiles_or_exit_sets_next_level_and_ending_zero()
{
    clear_level_lists();

    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.rng_.state_ = 31337;
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    // Only friendly living => level_done remains 2 and should trigger game end path.
    walker* ally = add_living(0);
    TEST_ASSERT(ally != nullptr, "ally created");
    if (!ally)
        return;
    ally->set_act_type(ACT_CONTROL);

    og::runtime::current_session->myscreen_->world().id = 41;
    std::int32_t enemy_freeze = 0;
    char end = 0;
    world.my_team = save.my_team;
    world.enemy_freeze = enemy_freeze;
    world.end = end;
    tick_world(world, events);
    enemy_freeze = world.enemy_freeze;

    TEST_ASSERT(world.game_ended, "no hostiles and no exits should end level");
    TEST_ASSERT_EQ(0, (int)world.ending, "auto-end path should set ending to zero");
    TEST_ASSERT_EQ(42, (int)world.next_level, "auto-end path should advance to next level id");

    clear_level_lists();
}
REGISTER_TEST(test_game_world_round9_no_hostiles_or_exit_sets_next_level_and_ending_zero);

// --- Issue #98 regression tests: exits triggerable without beating scenario ---

void test_issue98_can_exit_flag_should_show_exit_not_withdraw()
{
    // Regression test for GitHub issue #98.
    // When TYPE_CAN_EXIT_WHENEVER is set and Withdraw conditions are also met,
    // the Exit dialog should show (not Withdraw). Before the fix, the Withdraw
    // dialog fired first, and accepting it changed scen_num (retreat behavior).
    // After the fix, the Exit dialog fires instead and scen_num is unchanged
    // (normal level completion path).
    clear_level_lists();

    static og::sim::SimEventLog sim_events;
    static ProductionRandom rng;
    sim_events.clear();
    og::runtime::current_session->myscreen_->world().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze, &sim_events, &rng, &cfg);

    // Setup: CAN_EXIT_WHENEVER flag, enemies still present, dest level completed,
    // current scenario NOT completed → both Withdraw AND Exit conditions met.
    og::runtime::current_session->myscreen_->world().type = og::gameplay::GameWorld::TYPE_CAN_EXIT_WHENEVER;
    og::runtime::current_session->myscreen_->world().level_done = 0; // enemies still present

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5; // current level (not completed)
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3); // exit points to level 3
    walker* eater = add_living(0);
    TEST_ASSERT(exit_fx && eater, "exit/eater created");
    if (!(exit_fx && eater))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    // Push one "accept" answer. With the fix, the Exit dialog fires and
    // consumes this. Before the fix, the Withdraw dialog consumed it instead.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);

    exit_fx->eat_me(eater);

    // The Withdraw-accept path changes scen_num to the exit level (3) via
    // load("save0") + scen_num = exit_level + save("save0").
    // The Exit-accept path does NOT change scen_num.
    // With the fix (Exit fires, not Withdraw), scen_num should stay at 5.
    TEST_ASSERT_EQ(5, static_cast<int>(og::runtime::current_session->myscreen_->save_data.scen_num),
                   "CAN_EXIT_WHENEVER should show Exit (scen_num unchanged), not Withdraw");

    og::runtime::current_session->myscreen_->world().type = 0;
    clear_level_lists();
}
REGISTER_TEST(test_issue98_can_exit_flag_should_show_exit_not_withdraw);

void test_issue98_no_double_dialog_on_withdraw_exit()
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

    static og::sim::SimEventLog sim_events;
    static ProductionRandom rng;
    sim_events.clear();
    og::runtime::current_session->myscreen_->world().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze, &sim_events, &rng, &cfg);

    og::runtime::current_session->myscreen_->world().type = og::gameplay::GameWorld::TYPE_CAN_EXIT_WHENEVER;
    og::runtime::current_session->myscreen_->world().level_done = 0; // enemies still present

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.add_level_completed(og::runtime::current_session->myscreen_->save_data.current_campaign, 3);
    (void)og::runtime::current_session->myscreen_->save_data.save("save0");

    treasure* exit_fx = add_treasure(FAMILY_EXIT, 3);
    walker* eater = add_living(0);
    TEST_ASSERT(exit_fx && eater, "exit/eater created");
    if (!(exit_fx && eater))
        return;

    eater->set_act_type(ACT_CONTROL);
    eater->in_act = false;
    eater->skip_exit = 0;

    // Push two "decline" answers into the queue.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    picker_testing_yes_or_no_queue_push(false);

    exit_fx->eat_me(eater);

    // Event-driven exits defer prompting to the outer layer, so none of the
    // queued answers should be consumed by entity code.
    int remaining = picker_testing_yes_or_no_queue_remaining();
    TEST_ASSERT_EQ(2, remaining,
                   "entity code should not consume prompt answers directly");

    int request_count = 0;
    for (const auto& ev : sim_events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
            request_count++;
    }
    TEST_ASSERT_EQ(1, request_count, "only one RequestExitConfirmation should be emitted");

    og::runtime::current_session->myscreen_->world().type = 0;
    clear_level_lists();
}
REGISTER_TEST(test_issue98_no_double_dialog_on_withdraw_exit);
