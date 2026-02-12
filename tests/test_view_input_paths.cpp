#include "graph.h"
#include "input.h"
#include "test_framework.h"

#include <list>
#include <memory>

extern screen* myscreen;
extern int player_keys[4][NUM_KEYS];

namespace
{
struct TeamListSwap
{
    std::list<std::unique_ptr<walker>> saved_ob;
    TeamListSwap() { saved_ob.splice(saved_ob.end(), myscreen->level_data.oblist); }
    ~TeamListSwap() { myscreen->level_data.oblist.splice(myscreen->level_data.oblist.end(), saved_ob); }
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;
    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old_key(player_keys[player_][key_enum_])
    {
        player_keys[player][key_enum] = new_key;
    }
    ~KeyBindingGuard() { player_keys[player][key_enum] = old_key; }
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

static walker* make_living(unsigned char family, unsigned char team, int x, int y)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
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

    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;

    walker* w1 = make_living(FAMILY_SOLDIER, 0, 20, 20);
    walker* w2 = make_living(FAMILY_ELF, 0, 40, 20);
    walker* w3 = make_living(FAMILY_ARCHER, 0, 60, 20);
    TEST_ASSERT(w1 && w2 && w3, "walkers should be created");

    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(w1));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(w2));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(w3));
    v->control = w1;

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.repeat = 0;
    e.key.keysym.sym = SDLK_TAB;
    v->input(e);
    TEST_ASSERT(v->control == w2, "switch key should move to next team member");

    // Reset debounce (changedchar) by sending a non-switch key event.
    e.key.keysym.sym = SDLK_F1;
    v->input(e);

    ks.set(SDLK_LSHIFT, true);
    e.key.keysym.sym = SDLK_TAB;
    v->input(e);
    ks.set(SDLK_LSHIFT, false);
    TEST_ASSERT(v->control == w1, "shift+switch should move to previous team member");
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

    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;

    walker* control = make_living(FAMILY_SOLDIER, 0, 20, 20);
    walker* ally = make_living(FAMILY_ELF, 0, 40, 20);
    TEST_ASSERT(control && ally, "walkers should be created");
    control->set_act_type(ACT_CONTROL);
    control->user = 0;
    ally->leader = nullptr;

    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(control));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(ally));
    v->control = control;

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.repeat = 0;
    e.key.keysym.sym = SDLK_y;

    // Plain YELL: followers should get leader+follow command, and yo_delay set.
    v->input(e);
    TEST_ASSERT(ally->leader == control, "plain yell should assign control as leader");
    TEST_ASSERT(control->yo_delay == 30, "plain yell should set yo_delay");

    // Shift+YELL toggles team defense mode.
    ks.set(SDLK_LSHIFT, true);
    v->input(e);
    TEST_ASSERT(control->action == ACTION_FOLLOW, "shift+yell should enter follow/defense mode");
    TEST_ASSERT(ally->action == ACTION_FOLLOW, "ally should enter follow action");

    // Repeat shift+yell should release defense mode.
    v->input(e);
    ks.set(SDLK_LSHIFT, false);
    TEST_ASSERT(control->action == 0, "second shift+yell should clear defense mode");
}
REGISTER_TEST(test_view_input_yell_and_shift_yell_team_actions);

void test_view_input_cheat_mode_switch_team_kill_and_level_keys()
{
    TeamListSwap swap;
    disablePlayerJoystick(0);

    KeyBindingGuard bind_switch(0, KEY_SWITCH, SDLK_TAB);
    KeyBindingGuard bind_cheat(0, KEY_CHEAT, SDLK_c);
    KeyStateGuard ks;

    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "view should exist");
    v->mynum = 0;
    v->my_team = 0;
    myscreen->save_data.my_team = 0;

    walker* control = make_living(FAMILY_SOLDIER, 0, 20, 20);
    walker* teammate = make_living(FAMILY_ELF, 0, 40, 20);
    walker* enemy = make_living(FAMILY_ORC, 1, 60, 20);
    TEST_ASSERT(control && teammate && enemy, "walkers should be created");

    control->user = 0;
    control->set_act_type(ACT_CONTROL);
    control->stats()->level = 5;

    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(control));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(teammate));
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(enemy));
    v->control = control;

    // Hold cheat key so cheat branch executes.
    ks.set(SDLK_c, true);

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.repeat = 0;

    // Cheat+switch: rotate to next team that has a living unit.
    e.key.keysym.sym = SDLK_TAB;
    v->input(e);
    TEST_ASSERT(v->control != nullptr, "control should remain valid after cheat-switch");

    // Cheat+F12: eliminate enemy living units.
    enemy->stats()->hitpoints = 25;
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
    const int freeze_before = myscreen->enemy_freeze;
    e.key.keysym.sym = SDLK_F1;
    v->input(e);
    TEST_ASSERT(myscreen->enemy_freeze >= freeze_before + 50, "F1 should increase enemy freeze time");

    const size_t ob_count_before = myscreen->level_data.oblist.size();
    e.key.keysym.sym = SDLK_F2;
    v->input(e);
    TEST_ASSERT(myscreen->level_data.oblist.size() >= ob_count_before, "F2 should keep oblist valid");

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
}
REGISTER_TEST(test_view_input_cheat_mode_switch_team_kill_and_level_keys);
