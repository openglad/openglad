#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/util.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/render/view.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <openglad/gameplay/input_action.h>
#include <cstring>
#include <list>
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

namespace
{
// input() runs sanitize_control_pointer first, and a control that is not in
// one of the world's owning lists is dropped with an early return — a walker
// held only by the test's unique_ptr never reaches the F-key branches at all.
struct OblistSwap
{
    std::list<std::unique_ptr<walker>> saved;
    OblistSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.splice_into(
            saved);
    }
    ~OblistSwap()
    {
        GameWorld& w = og::runtime::current_session->myscreen_->world();
        w.oblist.splice(w.oblist.end(), saved);
    }
};
} // namespace

// F4 without the cheat modifier is screen::report_mem: it posts the memory
// line to viewob[0]'s feed for 25 cycles (screen.cpp report_mem). F3 is
// pinned by ViewInputPaths.view_input_f3_posts_the_measured_frame_rate, so
// this case owns the F4 half.
TEST(ViewInputSmoke, viewscreen_input_f4_posts_the_memory_report)
{
    screen* const game = og::runtime::current_session->myscreen_;
    viewscreen* v = game->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";

    OblistSwap swap;
    auto control = create_living_on_team(0);
    ASSERT_TRUE(control != nullptr) << "control walker should be created";
    walker* controlp = control.get();
    game->world().oblist.push_back(std::move(control));
    v->control = controlp;
    v->mynum = 0;
    v->my_team = 0;

    // The cheat modifier reroutes F-keys into handle_cheat_keys.
    const bool saved_cheat =
        ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)];
    ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)] = false;

    v->clear_text();
    EXPECT_EQ(1, (int)v->input(make_keydown(SDLK_F4)))
        << "input() consumes the event";
    EXPECT_EQ("Free Linear address: 0 pages", v->textlist[0])
        << "F4 must post the memory report onto the feed";
    EXPECT_EQ(25, (int)v->textcycles[0])
        << "report_mem posts its line for 25 cycles";
    EXPECT_TRUE(v->textlist[1].empty())
        << "F4 posts exactly one line";

    // Negative control: with the cheat modifier held, F4 is not the memory
    // report at all — the branch is gated on !Cheat.
    ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)] = true;
    v->clear_text();
    (void)v->input(make_keydown(SDLK_F4));
    EXPECT_TRUE(v->textlist[0].empty())
        << "cheat-held F4 must not post the memory report";

    ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)] =
        saved_cheat;
    v->clear_text();
    v->control = nullptr;
    game->world().remove_ob(controlp);
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

