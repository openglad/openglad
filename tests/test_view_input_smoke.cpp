#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/util.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/render/view.h>
#include "test_framework.h"
#include <cstring>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living_on_team(unsigned char team)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->dead = 0;
    w->user = -1;
    w->setxy(80, 80);
    return w;
}

static SDL_Event make_keydown(SDL_Keycode k)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    e.key.repeat = false;
    return e;
}

void test_viewscreen_input_f3_f4_smoke()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    auto control = create_living_on_team(0);
    TEST_ASSERT(control != nullptr, "control walker should be created");
    walker* controlp = control.get();
    v->control = controlp;
    v->mynum = 0;
    v->my_team = 0;

    // Avoid divide-by-zero in the F3 FPS branch.
    og::runtime::current_session->myscreen_->timerstart = query_timer_control() - 200;
    og::runtime::current_session->myscreen_->framecount = 1234;

    // Exercise a couple of key-event branches.
    (void)v->input(make_keydown(SDLK_F3));
    (void)v->input(make_keydown(SDLK_F4));

    v->control = nullptr;
}
REGISTER_TEST(test_viewscreen_input_f3_f4_smoke);

void test_viewscreen_input_consumes_bonus_rounds()
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");

    auto control = create_living_on_team(0);
    TEST_ASSERT(control != nullptr, "control walker should be created");
    walker* controlp = control.get();
    v->control = controlp;
    v->mynum = 0;
    v->my_team = 0;

    // Ensure the bonus-round walk() path runs via process_input().
    controlp->bonus_rounds = 1;
    controlp->lastx = 1.0f;
    controlp->lasty = 0.0f;
    InputState empty_input = {};
    v->process_input(empty_input);
    TEST_ASSERT_EQ(0, (int)controlp->bonus_rounds, "bonus rounds should decrement");

    v->control = nullptr;
}
REGISTER_TEST(test_viewscreen_input_consumes_bonus_rounds);
