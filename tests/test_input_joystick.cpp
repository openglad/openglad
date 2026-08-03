#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <algorithm>
extern void wait_for_key(int somekey);
extern void resetJoystick(int player_num);
extern og::input_native::JoystickHandle joysticks[10];

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

struct JoystickHandleGuard
{
    og::input_native::JoystickHandle old[10];

    JoystickHandleGuard()
    {
        for (int i = 0; i < 10; ++i)
        {
            old[i] = joysticks[i];
            joysticks[i] = nullptr;
        }
    }

    ~JoystickHandleGuard()
    {
        for (int i = 0; i < 10; ++i)
            joysticks[i] = old[i];
    }
};

struct CompleteInputStateGuard
{
    InputHardwareState hardware = input_hardware_state();
    int player_keys[4][NUM_KEYS]{};
    const bool* keyboard_state = og::runtime::current_session->keystates_;
    bool joystick_events_enabled = SDL_JoystickEventsEnabled();

    CompleteInputStateGuard()
    {
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                player_keys[player][key] =
                    og::runtime::current_session->player_keys_[player][key];
    }

    ~CompleteInputStateGuard()
    {
        input_hardware_state() = hardware;
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                og::runtime::current_session->player_keys_[player][key] =
                    player_keys[player][key];
        og::runtime::current_session->keystates_ = keyboard_state;
        SDL_SetJoystickEventsEnabled(joystick_events_enabled);
    }
};

class VirtualJoystick
{
public:
    VirtualJoystick(Uint16 axes, Uint16 buttons, Uint16 hats,
                    const char* name)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = axes;
        desc.nbuttons = buttons;
        desc.nhats = hats;
        desc.name = name;
        instance_id_ = SDL_AttachVirtualJoystick(&desc);
        if (instance_id_ != 0)
            joystick_ = SDL_OpenJoystick(instance_id_);
    }

    ~VirtualJoystick()
    {
        if (joystick_ != nullptr)
            SDL_CloseJoystick(joystick_);
        if (instance_id_ != 0)
            SDL_DetachVirtualJoystick(instance_id_);
    }

    VirtualJoystick(const VirtualJoystick&) = delete;
    VirtualJoystick& operator=(const VirtualJoystick&) = delete;

    SDL_Joystick* get() const { return joystick_; }

private:
    SDL_JoystickID instance_id_ = 0;
    SDL_Joystick* joystick_ = nullptr;
};
} // namespace

TEST(InputJoystick, input_joydata_setKeyFromEvent_and_takeover)
{
    PlayerJoyGuard guard;
    for (int i = 0; i < 4; ++i)
        player_joy[i] = JoyData();

    SDL_Event e{};

    // Diagonal key assignment is intentionally ignored.
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 1;
    e.jbutton.button = 3;
    player_joy[2].setKeyFromEvent(KEY_UP_RIGHT, e);
    ASSERT_EQ(JoyData::NONE, player_joy[2].key_type[KEY_UP_RIGHT]) << "diagonal key should be ignored";

    // Axis assignment should track direction and axis index.
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
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
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 6;
    e.jbutton.button = 9;
    player_joy[2].setKeyFromEvent(KEY_SWITCH, e);
    ASSERT_EQ(JoyData::BUTTON, player_joy[2].key_type[KEY_SWITCH]) << "button should map to BUTTON type";
    ASSERT_EQ(9, player_joy[2].key_index[KEY_SWITCH]) << "button index should be stored";
    ASSERT_EQ(6, player_joy[2].index) << "joystick index should update";

    // Hat assignment (valid + invalid).
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    e.jhat.which = 7;
    e.jhat.hat = 1;
    e.jhat.value = SDL_HAT_UP;
    player_joy[2].setKeyFromEvent(KEY_UP, e);
    ASSERT_EQ(JoyData::HAT_UP, player_joy[2].key_type[KEY_UP]) << "hat up should map to HAT_UP";
    ASSERT_EQ(1, player_joy[2].key_index[KEY_UP]) << "hat index should be stored";

    e.jhat.value = SDL_HAT_RIGHT;
    player_joy[2].setKeyFromEvent(KEY_RIGHT, e);
    ASSERT_EQ(JoyData::HAT_RIGHT, player_joy[2].key_type[KEY_RIGHT]) << "hat right should map to HAT_RIGHT";
    ASSERT_EQ(1, player_joy[2].key_index[KEY_RIGHT]) << "hat index should be stored";

    e.jhat.value = SDL_HAT_DOWN;
    player_joy[2].setKeyFromEvent(KEY_DOWN, e);
    ASSERT_EQ(JoyData::HAT_DOWN, player_joy[2].key_type[KEY_DOWN]) << "hat down should map to HAT_DOWN";
    ASSERT_EQ(1, player_joy[2].key_index[KEY_DOWN]) << "hat index should be stored";

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
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
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
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 2;
    e.jbutton.button = 4;
    ASSERT_TRUE(j.getPress(KEY_FIRE, e)) << "matching button down should press";
    e.jbutton.button = 3;
    ASSERT_TRUE(!j.getPress(KEY_FIRE, e)) << "non-matching button down should not press";

    e.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    e.jbutton.which = 2;
    e.jbutton.button = 4;
    ASSERT_TRUE(j.getRelease(KEY_FIRE, e)) << "matching button up should release";
    e.jbutton.which = 1;
    ASSERT_TRUE(!j.getRelease(KEY_FIRE, e)) << "non-matching joystick should not release";

    j.key_type[KEY_UP] = JoyData::POS_AXIS;
    j.key_index[KEY_UP] = 0;
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
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
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
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
    key_event.type = SDL_EVENT_KEY_DOWN;
    ASSERT_TRUE(isKeyboardEvent(key_event)) << "keydown should be keyboard event";
    ASSERT_TRUE(!isJoystickEvent(key_event)) << "keydown should not be joystick event";

    SDL_Event joy_event{};
    joy_event.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    ASSERT_TRUE(!isKeyboardEvent(joy_event)) << "joy hat should not be keyboard event";
    ASSERT_TRUE(isJoystickEvent(joy_event)) << "joy hat should be joystick event";

    wait_for_key(SDLK_SPACE); // TESTING build: should return immediately.

    resetJoystick(0);
    const bool has_device = og::input_native::num_joysticks() > 0;
    ASSERT_EQ(has_device, playerHasJoystick(0))
        << "playerHasJoystick should reflect whether a joystick device was detected";
}


TEST(InputJoystick, input_joydata_getState_handles_all_mapping_types_with_neutral_joystick)
{
    JoystickHandleGuard joystick_guard;

    JoyData j;
    j.index = 0;

    j.key_index[KEY_FIRE] = 0;
    j.key_type[KEY_FIRE] = JoyData::POS_AXIS;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "neutral positive axis should not be held";

    j.key_type[KEY_FIRE] = JoyData::NEG_AXIS;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "neutral negative axis should not be held";

    j.key_type[KEY_FIRE] = JoyData::BUTTON;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "neutral button should not be held";

    j.key_type[KEY_FIRE] = JoyData::HAT_UP;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "centered hat should not hold up";

    j.key_type[KEY_FIRE] = JoyData::HAT_RIGHT;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "centered hat should not hold right";

    j.key_type[KEY_FIRE] = JoyData::HAT_DOWN;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "centered hat should not hold down";

    j.key_type[KEY_FIRE] = JoyData::HAT_LEFT;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "centered hat should not hold left";

    j.key_type[KEY_FIRE] = JoyData::HAT_UP_RIGHT;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "diagonal hat mappings are ignored";

    j.key_type[KEY_FIRE] = JoyData::NONE;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "unmapped keys should not be held";

    j.index = -1;
    j.key_type[KEY_FIRE] = JoyData::BUTTON;
    ASSERT_FALSE(j.getState(KEY_FIRE)) << "unbound joystick should not be held";
}

namespace
{
// This test used to be order-dependent (failed standalone / under shuffle):
// nothing in it initialized SDL's joystick subsystem, so it only worked when
// an earlier test happened to leave the subsystem up (resetJoystick in the
// press/release test), and SDL_UpdateJoysticks() is a silent no-op while the
// subsystem is down — the virtual button press never landed. Subsystem init
// is refcounted, so the paired init/quit restores the prior state exactly.
struct JoystickSubsystemGuard
{
    JoystickSubsystemGuard()
    {
        og::input_native::joystick_init_subsystem();
    }
    ~JoystickSubsystemGuard()
    {
        og::input_native::joystick_quit_subsystem();
    }
};

// Second, independent gate: SDL drops joystick button PRESSES while its
// window lacks keyboard focus (SDL_PrivateJoystickShouldIgnoreEvent). Focus
// timing depends on which tests pumped events earlier, so allow background
// events for the duration of the virtual-button reads.
struct BackgroundJoystickEventsGuard
{
    BackgroundJoystickEventsGuard()
    {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    ~BackgroundJoystickEventsGuard()
    {
        SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }
};
} // namespace

TEST(InputJoystick, joydata_discovers_and_reads_real_virtual_joystick_capabilities)
{
    // Declared first so its teardown runs after the virtual devices detach.
    JoystickSubsystemGuard subsystem_guard;
    PlayerJoyGuard player_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    // SDL virtual devices exercise the same public joystick API as hardware,
    // while remaining deterministic and self-contained on headless CI.
    VirtualJoystick axes_device(2, 6, 0, "OpenGlad axes test pad");
    VirtualJoystick hat_device(1, 0, 1, "OpenGlad hat test pad");
    ASSERT_NE(nullptr, axes_device.get()) << SDL_GetError();
    ASSERT_NE(nullptr, hat_device.get()) << SDL_GetError();

    // Restore every process-global handle exactly after the test.  Declare
    // this guard after the virtual devices so restoration happens before the
    // devices themselves are closed.
    JoystickHandleGuard handle_guard;
    joysticks[0] = axes_device.get();

    JoyData axes(0);
    ASSERT_EQ(0, axes.index);
    ASSERT_EQ(2, axes.numAxes);
    ASSERT_EQ(6, axes.numButtons);
    ASSERT_EQ(0, axes.numHats);
    EXPECT_EQ(JoyData::POS_AXIS, axes.key_type[KEY_RIGHT]);
    EXPECT_EQ(JoyData::NEG_AXIS, axes.key_type[KEY_LEFT]);
    EXPECT_EQ(JoyData::NEG_AXIS, axes.key_type[KEY_UP]);
    EXPECT_EQ(JoyData::POS_AXIS, axes.key_type[KEY_DOWN]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_FIRE]);
    EXPECT_EQ(0, axes.key_index[KEY_FIRE]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_SPECIAL]);
    EXPECT_EQ(1, axes.key_index[KEY_SPECIAL]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_SPECIAL_SWITCH]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_YELL]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_SHIFTER]);
    EXPECT_EQ(JoyData::BUTTON, axes.key_type[KEY_SWITCH]);

    player_joy[0] = axes;
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(axes_device.get(), 0, true));
    SDL_UpdateJoysticks();
    EXPECT_TRUE(isPlayerHoldingKey(0, KEY_FIRE))
        << "a held virtual button should flow through JoyData::getState";
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(axes_device.get(), 0, false));
    SDL_UpdateJoysticks();
    EXPECT_FALSE(isPlayerHoldingKey(0, KEY_FIRE));

    // A one-axis controller falls back to its hat for all eight movement
    // directions.  This is common for simple USB/D-pad-only controllers.
    joysticks[0] = hat_device.get();
    JoyData hat(0);
    ASSERT_EQ(1, hat.numAxes);
    ASSERT_EQ(0, hat.numButtons);
    ASSERT_EQ(1, hat.numHats);
    EXPECT_EQ(JoyData::HAT_UP, hat.key_type[KEY_UP]);
    EXPECT_EQ(JoyData::HAT_UP_RIGHT, hat.key_type[KEY_UP_RIGHT]);
    EXPECT_EQ(JoyData::HAT_RIGHT, hat.key_type[KEY_RIGHT]);
    EXPECT_EQ(JoyData::HAT_DOWN_RIGHT, hat.key_type[KEY_DOWN_RIGHT]);
    EXPECT_EQ(JoyData::HAT_DOWN, hat.key_type[KEY_DOWN]);
    EXPECT_EQ(JoyData::HAT_DOWN_LEFT, hat.key_type[KEY_DOWN_LEFT]);
    EXPECT_EQ(JoyData::HAT_LEFT, hat.key_type[KEY_LEFT]);
    EXPECT_EQ(JoyData::HAT_UP_LEFT, hat.key_type[KEY_UP_LEFT]);
}

TEST(InputJoystick, init_input_opens_attached_virtual_devices)
{
    CompleteInputStateGuard input_guard;
    VirtualJoystick device(2, 6, 1, "OpenGlad init test pad");
    ASSERT_NE(nullptr, device.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;

    init_input();

    const int count = std::min(
        og::input_native::num_joysticks(), 10);
    ASSERT_GT(count, 0);
    bool opened_device = false;
    for (int i = 0; i < count; ++i)
        opened_device = opened_device || joysticks[i] != nullptr;
    EXPECT_TRUE(opened_device);
    EXPECT_TRUE(SDL_JoystickEventsEnabled());

    // init_input owns no shutdown phase, so close the handles acquired by
    // this test before the global slot guard restores the process state.
    for (int i = 0; i < 10; ++i)
    {
        if (joysticks[i] != nullptr)
            SDL_CloseJoystick(static_cast<SDL_Joystick*>(joysticks[i]));
        joysticks[i] = nullptr;
    }
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
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 3;
    e.jbutton.button = 6;
    ASSERT_TRUE(didPlayerPressKey(0, KEY_SPECIAL, e)) << "didPlayerPressKey should use joystick mapping";

    e.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    ASSERT_TRUE(didPlayerReleaseKey(0, KEY_SPECIAL, e)) << "didPlayerReleaseKey should use joystick mapping";

    e.jbutton.button = 7;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "wrong joystick button should not press";
    ASSERT_TRUE(!didPlayerReleaseKey(0, KEY_SPECIAL, e)) << "wrong joystick button should not release";
}

TEST(InputJoystick, input_joydata_null_and_wrong_event_paths)
{
    JoyData j;
    j.index = 2;
    const void* const no_event = nullptr;

    j.key_type[KEY_FIRE] = JoyData::BUTTON;
    j.key_index[KEY_FIRE] = 4;
    ASSERT_TRUE(!j.getPress(KEY_FIRE, no_event)) << "null event should not press";
    ASSERT_TRUE(!j.getRelease(KEY_FIRE, no_event)) << "null event should not release";

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    ASSERT_TRUE(!j.getPress(KEY_FIRE, key)) << "keyboard event should not press button mapping";
    ASSERT_TRUE(!j.getRelease(KEY_FIRE, key)) << "keyboard event should not release button mapping";

    SDL_Event axis{};
    axis.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    axis.jaxis.which = 2;
    axis.jaxis.axis = 0;
    axis.jaxis.value = 0;
    j.key_type[KEY_UP] = JoyData::POS_AXIS;
    j.key_index[KEY_UP] = 0;
    ASSERT_TRUE(!j.getPress(KEY_UP, axis)) << "centered positive axis should not press";
    j.key_type[KEY_DOWN] = JoyData::NEG_AXIS;
    j.key_index[KEY_DOWN] = 0;
    ASSERT_TRUE(!j.getPress(KEY_DOWN, axis)) << "centered negative axis should not press";

    axis.jaxis.which = 1;
    axis.jaxis.value = 12000;
    ASSERT_TRUE(!j.getPress(KEY_UP, axis)) << "wrong joystick should not press";
    ASSERT_TRUE(!j.getRelease(KEY_UP, axis)) << "wrong joystick should not release";

    SDL_Event hat{};
    hat.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    hat.jhat.which = 2;
    hat.jhat.hat = 0;
    hat.jhat.value = SDL_HAT_RIGHT;
    j.key_type[KEY_LEFT] = JoyData::HAT_LEFT;
    j.key_index[KEY_LEFT] = 0;
    ASSERT_TRUE(!j.getPress(KEY_LEFT, hat)) << "wrong hat direction should not press";
    ASSERT_TRUE(!j.getRelease(KEY_LEFT, hat)) << "wrong hat direction should not release";

    j.key_type[KEY_SPECIAL] = JoyData::NONE;
    ASSERT_TRUE(!j.getPress(KEY_SPECIAL, hat)) << "NONE mapping should not press";
    ASSERT_TRUE(!j.getRelease(KEY_SPECIAL, hat)) << "NONE mapping should not release";

    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, no_event)) << "null player press event should not press";
    ASSERT_TRUE(!didPlayerReleaseKey(0, KEY_SPECIAL, no_event)) << "null player release event should not release";
    handle_joy_event(no_event);

    SDL_Event unknown{};
    unknown.type = SDL_EVENT_USER;
    handle_joy_event(unknown);

    clear_keyboard();
    axis.jaxis.which = 0;
    axis.jaxis.value = -9000;
    handle_joy_event(axis);
    EXPECT_EQ(1, query_key_press_event())
        << "crossing the negative joystick dead zone is an input press";
}
