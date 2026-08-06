#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/util.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/render/view.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
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
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(-1);
    w->setxy(80, 80);
    return w;
}

static SDL_Event make_keydown(SDL_Keycode k)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.key = k;
    e.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
    e.key.repeat = false;
    return e;
}

TEST(ViewInputSmoke, viewscreen_input_f3_f4_smoke)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    auto control = create_living_on_team(0);
    ASSERT_TRUE(control != nullptr) << "control walker should be created";
    walker* controlp = control.get();
    v->control = controlp;
    v->mynum = 0;
    v->my_team = 0;

    // Avoid divide-by-zero in the F3 FPS branch.
    og::runtime::current_session->myscreen_->timerstart = static_cast<Uint32>(query_timer_control() - 200);
    og::runtime::current_session->myscreen_->framecount = 1234;

    // Exercise a couple of key-event branches.
    (void)v->input(make_keydown(SDLK_F3));
    (void)v->input(make_keydown(SDLK_F4));

    v->control = nullptr;
}


TEST(ViewInputSmoke, viewscreen_input_consumes_bonus_rounds)
{
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    auto control = create_living_on_team(0);
    ASSERT_TRUE(control != nullptr) << "control walker should be created";
    walker* controlp = control.get();
    v->control = controlp;
    v->mynum = 0;
    v->my_team = 0;

    // Ensure the bonus-round walk() path runs via process_input().
    controlp->set_bonus_rounds(1);
    controlp->set_lastx(1.0f);
    controlp->set_lasty(0.0f);
    InputState empty_input = {};
    v->process_input(empty_input);
    ASSERT_EQ(0, (int)controlp->bonus_rounds()) << "bonus rounds should decrement";

    v->control = nullptr;
}

