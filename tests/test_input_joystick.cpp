#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <gtest/gtest.h>
#include <SDL.h>
extern void wait_for_key(int somekey);
extern void resetJoystick(int player_num);

namespace
{
struct PlayerJoyGuard
{
    JoyData old[4];

    PlayerJoyGuard()
    {
        for (int i = 0; i < 4; ++i)
            old[i] = player_joy[i];
    }

    ~PlayerJoyGuard()
    {
        for (int i = 0; i < 4; ++i)
            player_joy[i] = old[i];
    }
};
} // namespace

TEST(InputJoystick, input_joydata_setKeyFromEvent_and_takeover)
{
    PlayerJoyGuard guard;
    for (int i = 0; i < 4; ++i)
        player_joy[i] = JoyData();

    SDL_Event e{};

    // Diagonal key assignment is intentionally ignored.
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 1;
    e.jbutton.button = 3;
    player_joy[2].setKeyFromEvent(KEY_UP_RIGHT, e);
    ASSERT_EQ(JoyData::NONE, player_joy[2].key_type[KEY_UP_RIGHT]) << "diagonal key should be ignored";

    // Axis assignment should track direction and axis index.
    e.type = SDL_JOYAXISMOTION;
    e.jaxis.which = 4;
    e.jaxis.axis = 2;
    e.jaxis.value = 12000;
    player_joy[2].setKeyFromEvent(KEY_FIRE, e);
    ASSERT_EQ(JoyData::POS_AXIS, player_joy[2].key_type[KEY_FIRE]) << "positive axis should map to POS_AXIS";
    ASSERT_EQ(2, player_joy[2].key_index[KEY_FIRE]) << "axis index should be stored";
    ASSERT_EQ(4, player_joy[2].index) << "joystick index should be stored";

    e.jaxis.which = 5;
    e.jaxis.axis = 1;
    e.jaxis.value = -12000;
    player_joy[2].setKeyFromEvent(KEY_SPECIAL, e);
    ASSERT_EQ(JoyData::NEG_AXIS, player_joy[2].key_type[KEY_SPECIAL]) << "negative axis should map to NEG_AXIS";
    ASSERT_EQ(1, player_joy[2].key_index[KEY_SPECIAL]) << "axis index should be stored";
    ASSERT_EQ(5, player_joy[2].index) << "joystick index should update";

    // Button assignment.
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 6;
    e.jbutton.button = 9;
    player_joy[2].setKeyFromEvent(KEY_SWITCH, e);
    ASSERT_EQ(JoyData::BUTTON, player_joy[2].key_type[KEY_SWITCH]) << "button should map to BUTTON type";
    ASSERT_EQ(9, player_joy[2].key_index[KEY_SWITCH]) << "button index should be stored";
    ASSERT_EQ(6, player_joy[2].index) << "joystick index should update";

    // Hat assignment (valid + invalid).
    e.type = SDL_JOYHATMOTION;
    e.jhat.which = 7;
    e.jhat.hat = 1;
    e.jhat.value = SDL_HAT_LEFT;
    player_joy[2].setKeyFromEvent(KEY_LEFT, e);
    ASSERT_EQ(JoyData::HAT_LEFT, player_joy[2].key_type[KEY_LEFT]) << "hat left should map to HAT_LEFT";
    ASSERT_EQ(1, player_joy[2].key_index[KEY_LEFT]) << "hat index should be stored";
    ASSERT_EQ(7, player_joy[2].index) << "hat event should set joystick index";

    player_joy[2].key_type[KEY_RIGHT] = JoyData::BUTTON;
    player_joy[2].key_index[KEY_RIGHT] = 11;
    e.jhat.value = SDL_HAT_RIGHTUP;  // unsupported diagonal hat value
    player_joy[2].setKeyFromEvent(KEY_RIGHT, e);
    ASSERT_EQ(JoyData::BUTTON, player_joy[2].key_type[KEY_RIGHT]) << "unsupported hat value should leave mapping unchanged";
    ASSERT_EQ(11, player_joy[2].key_index[KEY_RIGHT]) << "unsupported hat value should leave index unchanged";

    // Taking over a joystick should unbind other players using that same index.
    player_joy[0].index = 3;
    player_joy[1].index = 7;
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 7;
    e.jbutton.button = 1;
    player_joy[2].setKeyFromEvent(KEY_YELL, e);
    ASSERT_EQ(7, player_joy[2].index) << "player should now use joystick 7";
    ASSERT_EQ(-1, player_joy[1].index) << "other player bound to same joystick should be released";
    ASSERT_EQ(3, player_joy[0].index) << "unrelated player joystick should remain unchanged";
}


TEST(InputJoystick, input_joydata_press_release_helpers_and_player_queries)
{
    PlayerJoyGuard guard;
    for (int i = 0; i < 4; ++i)
        player_joy[i] = JoyData();

    JoyData j;
    j.index = 2;

    SDL_Event e{};

    j.key_type[KEY_FIRE] = JoyData::BUTTON;
    j.key_index[KEY_FIRE] = 4;
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 2;
    e.jbutton.button = 4;
    ASSERT_TRUE(j.getPress(KEY_FIRE, e)) << "matching button down should press";
    e.jbutton.button = 3;
    ASSERT_TRUE(!j.getPress(KEY_FIRE, e)) << "non-matching button down should not press";

    e.type = SDL_JOYBUTTONUP;
    e.jbutton.which = 2;
    e.jbutton.button = 4;
    ASSERT_TRUE(j.getRelease(KEY_FIRE, e)) << "matching button up should release";
    e.jbutton.which = 1;
    ASSERT_TRUE(!j.getRelease(KEY_FIRE, e)) << "non-matching joystick should not release";

    j.key_type[KEY_UP] = JoyData::POS_AXIS;
    j.key_index[KEY_UP] = 0;
    e.type = SDL_JOYAXISMOTION;
    e.jaxis.which = 2;
    e.jaxis.axis = 0;
    e.jaxis.value = 12000;
    ASSERT_TRUE(j.getPress(KEY_UP, e)) << "positive axis crossing deadzone should press";
    e.jaxis.value = 1000;
    ASSERT_TRUE(j.getRelease(KEY_UP, e)) << "positive axis returning below deadzone should release";

    j.key_type[KEY_DOWN] = JoyData::NEG_AXIS;
    j.key_index[KEY_DOWN] = 1;
    e.jaxis.axis = 1;
    e.jaxis.value = -12000;
    ASSERT_TRUE(j.getPress(KEY_DOWN, e)) << "negative axis crossing deadzone should press";
    e.jaxis.value = -1000;
    ASSERT_TRUE(j.getRelease(KEY_DOWN, e)) << "negative axis returning above -deadzone should release";

    j.key_type[KEY_LEFT] = JoyData::HAT_LEFT;
    j.key_index[KEY_LEFT] = 0;
    e.type = SDL_JOYHATMOTION;
    e.jhat.which = 2;
    e.jhat.hat = 0;
    e.jhat.value = SDL_HAT_LEFT;
    ASSERT_TRUE(j.getPress(KEY_LEFT, e)) << "matching hat left should press";
    ASSERT_TRUE(j.getRelease(KEY_LEFT, e)) << "matching hat left should release";
    e.jhat.value = SDL_HAT_UP;
    ASSERT_TRUE(!j.getPress(KEY_LEFT, e)) << "non-matching hat direction should not press";

    j.key_type[KEY_UP] = JoyData::HAT_UP;
    j.key_type[KEY_RIGHT] = JoyData::HAT_RIGHT;
    j.key_type[KEY_DOWN] = JoyData::HAT_DOWN;
    e.jhat.value = SDL_HAT_UP;
    ASSERT_TRUE(j.getPress(KEY_UP, e)) << "hat up should press KEY_UP";
    ASSERT_TRUE(j.getRelease(KEY_UP, e)) << "hat up should release KEY_UP";
    e.jhat.value = SDL_HAT_RIGHT;
    (void)j.getPress(KEY_RIGHT, e);
    (void)j.getRelease(KEY_RIGHT, e);
    e.jhat.value = SDL_HAT_DOWN;
    (void)j.getPress(KEY_DOWN, e);
    (void)j.getRelease(KEY_DOWN, e);

    j.key_type[KEY_UP_LEFT] = JoyData::HAT_UP_LEFT;
    ASSERT_TRUE(!j.getPress(KEY_UP_LEFT, e)) << "diagonal hat mapping should be ignored for press";
    ASSERT_TRUE(!j.getRelease(KEY_UP_LEFT, e)) << "diagonal hat mapping should be ignored for release";

    j.index = -1;
    ASSERT_TRUE(!j.hasButtonSet(KEY_FIRE)) << "negative index means no joystick binding";
    j.index = 2;
    j.key_type[KEY_FIRE] = JoyData::NONE;
    ASSERT_TRUE(!j.hasButtonSet(KEY_FIRE)) << "NONE type means no key binding";
    j.key_type[KEY_FIRE] = JoyData::BUTTON;
    ASSERT_TRUE(j.hasButtonSet(KEY_FIRE)) << "valid index + non-NONE means binding exists";

    // Route through public player helpers.
    player_joy[0] = j;
    ASSERT_TRUE(playerHasJoystick(0)) << "playerHasJoystick should reflect bound joystick";
    disablePlayerJoystick(0);
    ASSERT_TRUE(!playerHasJoystick(0)) << "disablePlayerJoystick should clear joystick";

    // Input helper predicates and TESTING wait path.
    SDL_Event key_event{};
    key_event.type = SDL_KEYDOWN;
    ASSERT_TRUE(isKeyboardEvent(key_event)) << "keydown should be keyboard event";
    ASSERT_TRUE(!isJoystickEvent(key_event)) << "keydown should not be joystick event";

    SDL_Event joy_event{};
    joy_event.type = SDL_JOYHATMOTION;
    ASSERT_TRUE(!isKeyboardEvent(joy_event)) << "joy hat should not be keyboard event";
    ASSERT_TRUE(isJoystickEvent(joy_event)) << "joy hat should be joystick event";

    wait_for_key(SDLK_SPACE); // TESTING build: should return immediately.

    resetJoystick(0);
    const bool has_device = og::input_native::num_joysticks() > 0;
    ASSERT_EQ(has_device, playerHasJoystick(0))
        << "playerHasJoystick should reflect whether a joystick device was detected";
}


TEST(InputJoystick, input_didPlayerPressReleaseKey_uses_joystick_mapping_when_bound)
{
    PlayerJoyGuard guard;
    for (int i = 0; i < 4; ++i)
        player_joy[i] = JoyData();

    player_joy[0].index = 3;
    player_joy[0].key_type[KEY_SPECIAL] = JoyData::BUTTON;
    player_joy[0].key_index[KEY_SPECIAL] = 6;

    SDL_Event e{};
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 3;
    e.jbutton.button = 6;
    ASSERT_TRUE(didPlayerPressKey(0, KEY_SPECIAL, e)) << "didPlayerPressKey should use joystick mapping";

    e.type = SDL_JOYBUTTONUP;
    ASSERT_TRUE(didPlayerReleaseKey(0, KEY_SPECIAL, e)) << "didPlayerReleaseKey should use joystick mapping";

    e.jbutton.button = 7;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "wrong joystick button should not press";
    ASSERT_TRUE(!didPlayerReleaseKey(0, KEY_SPECIAL, e)) << "wrong joystick button should not release";
}

