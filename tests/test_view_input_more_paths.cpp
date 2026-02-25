#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <array>

#include "SDL.h"

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct KeystateOverride
{
    const Uint8* prev = nullptr;
    std::array<Uint8, SDL_NUM_SCANCODES> fake{};

    KeystateOverride()
    {
        prev = og::runtime::current_session->keystates_;
        fake.fill(0);
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
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    return e;
}

static SDL_Event dummy_event()
{
    SDL_Event e{};
    e.type = SDL_USEREVENT;
    return e;
}
} // namespace

void test_viewscreen_input_switch_yell_and_special_switch_paths()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    // Ensure we have a few living walkers to switch between.
    walker* w0 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* w1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ELF);
    walker* w2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    TEST_ASSERT(w0 && w1 && w2, "walkers created");
    if (!(w0 && w1 && w2))
        return;

    w0->team_num = 0;
    w1->team_num = 0;
    w2->team_num = 0;

    w0->real_team_num = 255;
    w1->real_team_num = 255;
    w2->real_team_num = 255;

    w0->user = 0;
    w1->user = -1;
    w2->user = -1;

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
    ks.fake[SDL_SCANCODE_LSHIFT] = 0;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SWITCH])));
    TEST_ASSERT(vs->control != nullptr, "control should remain valid after switch");

    // Reset the debounce flag by providing a non-switch event.
    (void)vs->input(dummy_event());

    // Switch control reverse by holding shifter.
    ks.fake[SDL_SCANCODE_LSHIFT] = 1;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SWITCH])));
    TEST_ASSERT(vs->control != nullptr, "control should remain valid after reverse switch");
    ks.fake[SDL_SCANCODE_LSHIFT] = 0;

    // Yell for help: KEY_YELL is 's' for player 0, requires not holding shifter.
    if (vs->control)
        vs->control->yo_delay = 0;
    (void)vs->input(dummy_event());
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));

    // Summon defense: hold shifter + KEY_YELL (case 0).
    (void)vs->input(dummy_event());
    ks.fake[SDL_SCANCODE_LSHIFT] = 1;
    if (vs->control)
        vs->control->action = 0;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));
    ks.fake[SDL_SCANCODE_LSHIFT] = 0;

    // Release men: case ACTION_FOLLOW.
    (void)vs->input(dummy_event());
    ks.fake[SDL_SCANCODE_LSHIFT] = 1;
    if (vs->control)
        vs->control->action = ACTION_FOLLOW;
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_YELL])));
    ks.fake[SDL_SCANCODE_LSHIFT] = 0;

    // Special switch: cycle current special (TAB for player 0).
    (void)vs->input(dummy_event());
    (void)vs->input(keydown(static_cast<SDL_Keycode>(og::runtime::current_session->player_keys_[0][KEY_SPECIAL_SWITCH])));
}
REGISTER_TEST(test_viewscreen_input_switch_yell_and_special_switch_paths);
