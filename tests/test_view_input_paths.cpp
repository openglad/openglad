#include "SDL.h"
#include <openglad/interface/input/input.h>
#include <openglad/interface/input/input_action.h>
#include <openglad/core/stats.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include "test_framework.h"

#include <list>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct TeamListSwap
{
    std::list<std::unique_ptr<walker>> saved_ob;
    TeamListSwap() { saved_ob.splice(saved_ob.end(), og::runtime::current_session->myscreen_->world().oblist); }
    ~TeamListSwap() { og::runtime::current_session->myscreen_->world().oblist.splice(og::runtime::current_session->myscreen_->world().oblist.end(), saved_ob); }
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;
    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }
    ~KeyBindingGuard() { og::runtime::current_session->player_keys_[player][key_enum] = old_key; }
};

struct KeyStateGuard
{
    int numkeys = 0;
    Uint8* keys = nullptr;
    explicit KeyStateGuard()
    {
        const Uint8* ro = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<Uint8*>(ro);
    }
    void set(SDL_Keycode key, bool pressed)
    {
        if (!keys) return;
        const SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        if (sc >= 0 && sc < numkeys)
            keys[sc] = pressed ? 1 : 0;
    }
};

static std::unique_ptr<walker> make_living(unsigned char family, unsigned char team, int x, int y)
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->user = -1;
    w->action = 0;
    w->set_act_type(ACT_RANDOM);
    w->setxy((short)x, (short)y);
    return w;
}
} // namespace

void test_view_input_switch_control_forward_and_reverse()
{
    TeamListSwap swap;
    disablePlayerJoystick(0);

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_shifter(0, KEY_SHIFTER, SDLK_LSHIFT);
    KeyStateGuard ks;

    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;

    auto w1 = make_living(FAMILY_SOLDIER, 0, 20, 20);
    auto w2 = make_living(FAMILY_ELF, 0, 40, 20);
    auto w3 = make_living(FAMILY_ARCHER, 0, 60, 20);
    TEST_ASSERT(w1 && w2 && w3, "walkers should be created");

    walker* w1p = w1.get();
    walker* w2p = w2.get();
    walker* w3p = w3.get();

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w1));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w2));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w3));
    v->control = w1p;

    // Use process_input() with InputState for switch control.
    InputState input = {};
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    input.players[0].held[static_cast<int>(InputAction::SwitchChar)] = true;
    v->process_input(input);
    TEST_ASSERT(v->control == w2p, "switch key should move to next team member");

    // Reset debounce by sending a frame with no switch pressed.
    InputState empty = {};
    v->process_input(empty);

    // Shift+switch should go backward.
    InputState shift_switch = {};
    shift_switch.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    shift_switch.players[0].held[static_cast<int>(InputAction::SwitchChar)] = true;
    shift_switch.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    v->process_input(shift_switch);
    TEST_ASSERT(v->control == w1p, "shift+switch should move to previous team member");
}
REGISTER_TEST(test_view_input_switch_control_forward_and_reverse);

void test_view_input_yell_and_shift_yell_team_actions()
{
    TeamListSwap swap;
    disablePlayerJoystick(0);

    KeyBindingGuard bind_yell(0, KEY_YELL, SDLK_y);
    KeyBindingGuard bind_shifter(0, KEY_SHIFTER, SDLK_LSHIFT);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_c);
    KeyStateGuard ks;

    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;

    auto control = make_living(FAMILY_SOLDIER, 0, 20, 20);
    auto ally = make_living(FAMILY_ELF, 0, 40, 20);
    TEST_ASSERT(control && ally, "walkers should be created");
    walker* controlp = control.get();
    walker* allyp = ally.get();

    controlp->set_act_type(ACT_CONTROL);
    controlp->user = 0;
    allyp->leader = nullptr;

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally));
    v->control = controlp;

    // Plain YELL via process_input: followers get leader+follow, yo_delay set.
    InputState yell_input = {};
    yell_input.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    yell_input.players[0].held[static_cast<int>(InputAction::Yell)] = true;
    v->process_input(yell_input);
    TEST_ASSERT(allyp->leader == controlp, "plain yell should assign control as leader");
    TEST_ASSERT(controlp->yo_delay == 30, "plain yell should set yo_delay");

    // Shift+YELL toggles team defense mode.
    InputState shift_yell = {};
    shift_yell.players[0].pressed[static_cast<int>(InputAction::Yell)] = true;
    shift_yell.players[0].held[static_cast<int>(InputAction::Yell)] = true;
    shift_yell.players[0].held[static_cast<int>(InputAction::Shift)] = true;
    v->process_input(shift_yell);
    TEST_ASSERT(controlp->action == ACTION_FOLLOW, "shift+yell should enter follow/defense mode");
    TEST_ASSERT(allyp->action == ACTION_FOLLOW, "ally should enter follow action");

    // Repeat shift+yell should release defense mode.
    v->process_input(shift_yell);
    TEST_ASSERT(controlp->action == 0, "second shift+yell should clear defense mode");
}
REGISTER_TEST(test_view_input_yell_and_shift_yell_team_actions);

void test_view_input_cheat_mode_switch_team_kill_and_level_keys()
{
    TeamListSwap swap;
    disablePlayerJoystick(0);

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_c);
    KeyStateGuard ks;

    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;
    og::runtime::current_session->myscreen_->save_data.my_team = 0;

    auto control = make_living(FAMILY_SOLDIER, 0, 20, 20);
    auto teammate = make_living(FAMILY_ELF, 0, 40, 20);
    auto enemy = make_living(FAMILY_ORC, 1, 60, 20);
    TEST_ASSERT(control && teammate && enemy, "walkers should be created");

    walker* controlp = control.get();
    walker* enemyp = enemy.get();

    controlp->user = 0;
    controlp->set_act_type(ACT_CONTROL);
    controlp->stats()->level = 5;

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(teammate));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(enemy));
    v->control = controlp;

    // Hold cheat key so cheat branch executes.
    // input() now reads from ctx().input, so populate it.
    ks.set(SDLK_c, true);
    ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)] = true;
    ctx().input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.repeat = 0;

    // Cheat+switch: rotate to next team that has a living unit.
    e.key.keysym.sym = SDLK_TAB;
    v->input(e);
    TEST_ASSERT(v->control != nullptr, "control should remain valid after cheat-switch");

    // Reset switch press for subsequent calls
    ctx().input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = false;

    // Cheat+F12: eliminate enemy living units.
    enemyp->stats()->hitpoints = 25;
    e.key.keysym.sym = SDLK_F12;
    v->input(e);
    TEST_ASSERT(v->control != nullptr, "cheat F12 path should keep control valid");

    // Cheat level tuning keys.
    const int level_before = v->control->stats()->level;
    e.key.keysym.sym = SDLK_RIGHTBRACKET;
    v->input(e);
    TEST_ASSERT(v->control->stats()->level >= level_before, "right bracket should not lower level");

    e.key.keysym.sym = SDLK_LEFTBRACKET;
    v->input(e);
    TEST_ASSERT(v->control->stats()->level >= 1, "left bracket should keep level >= 1");

    // Extra cheat keys for additional input branches.
    const int freeze_before = og::runtime::current_session->myscreen_->world().enemy_freeze;
    e.key.keysym.sym = SDLK_F1;
    v->input(e);
    TEST_ASSERT(og::runtime::current_session->myscreen_->world().enemy_freeze >= freeze_before + 50, "F1 should increase enemy freeze time");

    const size_t ob_count_before = og::runtime::current_session->myscreen_->world().oblist.size();
    e.key.keysym.sym = SDLK_F2;
    v->input(e);
    TEST_ASSERT(og::runtime::current_session->myscreen_->world().oblist.size() >= ob_count_before, "F2 should keep oblist valid");

    const bool flying_before = v->control->stats()->query_bit_flags(BIT_FLYING) != 0;
    e.key.keysym.sym = SDLK_f;
    v->input(e);
    TEST_ASSERT((v->control->stats()->query_bit_flags(BIT_FLYING) != 0) != flying_before,
                "f key should toggle flying bit");

    const float hp_before = v->control->stats()->hitpoints;
    e.key.keysym.sym = SDLK_h;
    v->input(e);
    TEST_ASSERT(v->control->stats()->hitpoints >= hp_before + 100.0f, "h key should increase hitpoints");

    const bool inv_before = v->control->stats()->query_bit_flags(BIT_INVINCIBLE) != 0;
    e.key.keysym.sym = SDLK_i;
    v->input(e);
    TEST_ASSERT((v->control->stats()->query_bit_flags(BIT_INVINCIBLE) != 0) != inv_before,
                "i key should toggle invincible bit");

    const float mp_before = v->control->stats()->magicpoints;
    e.key.keysym.sym = SDLK_m;
    v->input(e);
    TEST_ASSERT(v->control->stats()->magicpoints >= mp_before + 150.0f, "m key should increase magicpoints");

    const int speed_bonus_before = v->control->speed_bonus_left;
    e.key.keysym.sym = SDLK_s;
    v->input(e);
    TEST_ASSERT(v->control->speed_bonus_left >= speed_bonus_before + 20, "s key should increase speed bonus");

    ks.set(SDLK_c, false);
    ctx().input.players[0].held[static_cast<int>(InputAction::Cheat)] = false;
}
REGISTER_TEST(test_view_input_cheat_mode_switch_team_kill_and_level_keys);
