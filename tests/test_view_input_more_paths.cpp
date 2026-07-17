#include <SDL3/SDL.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <array>


// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct KeystateOverride
{
    const bool* prev = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake{};

    KeystateOverride()
    {
        prev = og::runtime::current_session->keystates_;
        fake.fill(false);
        og::runtime::current_session->keystates_ = fake.data();
    }

    ~KeystateOverride()
    {
        og::runtime::current_session->keystates_ = prev;
    }
};

static SDL_Event keydown(SDL_Keycode key)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    return e;
}

static SDL_Event dummy_event()
{
    SDL_Event e{};
    e.type = SDL_EVENT_USER;
    return e;
}
} // namespace

TEST(ViewInputMorePaths, viewscreen_input_switch_yell_and_special_switch_paths)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    // Ensure we have a few living walkers to switch between.
    walker* w0 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* w1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ELF);
    walker* w2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(w0 && w1 && w2) << "walkers created";
    if (!(w0 && w1 && w2))
        return;

    w0->set_team_num(0);
    w1->set_team_num(0);
    w2->set_team_num(0);

    w0->set_real_team_num(255);
    w1->set_real_team_num(255);
    w2->set_real_team_num(255);

    w0->set_user(0);
    w1->set_user(-1);
    w2->set_user(-1);

    w0->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
    w1->setxy(GRID_SIZE * 4, GRID_SIZE * 3);
    w2->setxy(GRID_SIZE * 2, GRID_SIZE * 3);

    w0->set_act_type(ACT_CONTROL);
    w1->set_act_type(ACT_RANDOM);
    w2->set_act_type(ACT_RANDOM);

    vs->mynum = 0;
    vs->control = w0;

    KeystateOverride ks;

    // Switch control forward (KEY_SWITCH is backquote for player 0).
    ks.fake[SDL_SCANCODE_LSHIFT] = false;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SWITCH])));
    ASSERT_TRUE(vs->control != nullptr) << "control should remain valid after switch";

    // Reset the debounce flag by providing a non-switch event.
    (void)vs->input(dummy_event());

    // Switch control reverse by holding shifter.
    ks.fake[SDL_SCANCODE_LSHIFT] = true;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SWITCH])));
    ASSERT_TRUE(vs->control != nullptr) << "control should remain valid after reverse switch";
    ks.fake[SDL_SCANCODE_LSHIFT] = false;

    // Yell for help: KEY_YELL is 's' for player 0, requires not holding shifter.
    if (vs->control)
        vs->control->set_yo_delay(0);
    (void)vs->input(dummy_event());
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));

    // Summon defense: hold shifter + KEY_YELL (case 0).
    (void)vs->input(dummy_event());
    ks.fake[SDL_SCANCODE_LSHIFT] = true;
    if (vs->control)
        vs->control->set_action(0);
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));
    ks.fake[SDL_SCANCODE_LSHIFT] = false;

    // Release men: case ACTION_FOLLOW.
    (void)vs->input(dummy_event());
    ks.fake[SDL_SCANCODE_LSHIFT] = true;
    if (vs->control)
        vs->control->set_action(ACTION_FOLLOW);
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));
    ks.fake[SDL_SCANCODE_LSHIFT] = false;

    // Special switch: cycle current special (TAB for player 0).
    (void)vs->input(dummy_event());
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SPECIAL_SWITCH])));
}

