#include <openglad/data/gparser.h>
#include <openglad/data/level_data.h>
#include <openglad/entities/treasure.h>
#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/screen_lifecycle.h>
#include <openglad/sim/event.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_world.h>

#include "test_framework.h"

extern screen* myscreen;
extern int raw_key;
extern short key_press_event;
extern bool input_continue;

// TESTING-only helpers from picker_dialogs.cpp.
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace {

void clear_level_lists()
{
    myscreen->level_data.delete_objects();
    myscreen->level_data.level_done = 0;
}

walker* add_living(unsigned char team)
{
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
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
    walker* w = myscreen->level_data.add_fx_ob(Order::Treasure, family);
    if (!w)
        return nullptr;
    w->stats()->level = level;
    w->setxy(100, 100);
    return static_cast<treasure*>(w);
}

} // namespace

void test_input_bridge_window_and_key_paths()
{
    const float saved_window_w = window_w;
    const float saved_window_h = window_h;
    const float saved_overscan = overscan_percentage;
    const bool saved_continue = input_continue;
    const short saved_key_press_event = key_press_event;
    const int saved_raw_key = raw_key;

    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_MINIMIZED;
    handle_window_event(e);

    e.window.event = SDL_WINDOWEVENT_CLOSE;
    handle_window_event(e);

    e.window.event = SDL_WINDOWEVENT_RESTORED;
    handle_window_event(e);

    overscan_percentage = -1.0f;
    e.window.event = SDL_WINDOWEVENT_RESIZED;
    e.window.data1 = 1280;
    e.window.data2 = 720;
    handle_window_event(e);
    TEST_ASSERT_EQ(1280, static_cast<int>(window_w), "resize should update window_w");
    TEST_ASSERT_EQ(720, static_cast<int>(window_h), "resize should update window_h");
    TEST_ASSERT(overscan_percentage == 0.0f, "resize path should clamp overscan");

    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_F10;
    key.key.keysym.mod = 0;
    handle_key_event(key);

    cfg.apply_setting("graphics", "overscan_percentage", "25");
    overscan_percentage = 0.0f;
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_F12;
    key.key.keysym.mod = KMOD_CTRL;
    handle_key_event(key);
    TEST_ASSERT(overscan_percentage >= 0.0f && overscan_percentage <= 0.25f,
                "F12+Ctrl should reload and clamp overscan");

    input_continue = false;
    key_press_event = 0;
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_ESCAPE;
    key.key.keysym.mod = 0;
    handle_key_event(key);
    TEST_ASSERT(input_continue, "escape keydown should set continue");
    TEST_ASSERT_EQ(1, static_cast<int>(key_press_event), "keydown should set key_press_event");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_ESCAPE), raw_key, "keydown should update raw_key");

    key.type = SDL_KEYUP;
    key.key.keysym.sym = SDLK_ESCAPE;
    handle_key_event(key);

    window_w = saved_window_w;
    window_h = saved_window_h;
    overscan_percentage = saved_overscan;
    input_continue = saved_continue;
    key_press_event = saved_key_press_event;
    raw_key = saved_raw_key;
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
    TEST_ASSERT_EQ(7, standalone.query_frame(), "set_direct_frame should update frame");
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
    myscreen->level_data.set_sim_context(
        &myscreen->save_data, &myscreen->enemy_freeze, &sim_events, &rng, &cfg);

    // Exit flow: no enemies left + prompt accepted should emit EndGame.
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->level_data.level_done = 1;
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

void test_sim_world_tick_branches_for_end_freeze_and_cleanup()
{
    clear_level_lists();

    og::sim::SimWorld world(1337);
    og::sim::SimEventLog events;
    SaveData save;
    save.my_team = 0;

    std::int32_t enemy_freeze = 2;
    char end = 0;

    walker* ally = add_living(0);
    (void)ally;
    walker* foe = add_living(1);
    foe->team_num = 1;

    og::sim::TickResult r = world.tick(myscreen->level_data, save, enemy_freeze, end, events);
    TEST_ASSERT_EQ(1, static_cast<int>(enemy_freeze), "enemy_freeze should decrement");
    TEST_ASSERT_EQ(1, static_cast<int>(world.tick_count()), "first tick should increment tick_count");

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
    r = world.tick(myscreen->level_data, save, enemy_freeze, end, events);
    TEST_ASSERT(r.game_ended, "end flag should terminate tick when battle not auto-finished");

    // Level_done==1 path via exit treasure and no enemies.
    clear_level_lists();
    add_treasure(FAMILY_EXIT, 2);
    enemy_freeze = 0;
    end = 0;
    events.clear();
    r = world.tick(myscreen->level_data, save, enemy_freeze, end, events);
    TEST_ASSERT(r.level_done == 1, "exit with no foes should set level_done=1");
    TEST_ASSERT(!r.game_ended, "level_done=1 should not auto-end game");

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
    walker* dead_fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* dead_weap = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(dead_fx != nullptr, "expected fx walker");
    TEST_ASSERT(dead_weap != nullptr, "expected weapon walker");
    dead_fx->dead = 1;
    dead_weap->dead = 1;

    enemy_freeze = 0;
    end = 0;
    events.clear();
    r = world.tick(myscreen->level_data, save, enemy_freeze, end, events);
    TEST_ASSERT(owner->foe == nullptr, "dead foe pointer should be cleared");
    TEST_ASSERT(owner->leader == nullptr, "dead leader pointer should be cleared");
    clear_level_lists();
}
REGISTER_TEST(test_sim_world_tick_branches_for_end_freeze_and_cleanup);
