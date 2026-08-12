#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <format>
#include <string>
#include <vector>
extern void wait_for_key(int somekey);
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
        detach();
    }

    VirtualJoystick(const VirtualJoystick&) = delete;
    VirtualJoystick& operator=(const VirtualJoystick&) = delete;

    SDL_Joystick* get() const { return joystick_; }
    SDL_JoystickID instance_id() const { return instance_id_; }

    // Simulates an unplug mid-test (the destructor then has nothing to do).
    void detach()
    {
        if (joystick_ != nullptr)
        {
            SDL_CloseJoystick(joystick_);
            joystick_ = nullptr;
        }
        if (instance_id_ != 0)
        {
            SDL_DetachVirtualJoystick(instance_id_);
            instance_id_ = 0;
        }
    }

private:
    SDL_JoystickID instance_id_ = 0;
    SDL_Joystick* joystick_ = nullptr;
};

// 0-based device index of an attached instance id, -1 when not enumerable.
int device_index_of(SDL_JoystickID instance)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    int found = -1;
    if (ids != nullptr)
    {
        for (int i = 0; i < count; ++i)
        {
            if (ids[i] == instance)
            {
                found = i;
                break;
            }
        }
        SDL_free(ids);
    }
    return found;
}
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

    player_joy[0] = j;
    clear_player_joystick(0);
    ASSERT_TRUE(!playerHasJoystick(0))
        << "clear_player_joystick should leave the seat keyboard-driven";
    ASSERT_EQ(JoyData::NONE, player_joy[0].key_type[KEY_FIRE])
        << "clear_player_joystick is a full unbind, not just an index drop";
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
// an earlier test happened to leave the subsystem up, and
// SDL_UpdateJoysticks() is a silent no-op while the subsystem is down — the
// virtual button press never landed. init_input now brings the subsystem up
// at binary startup, but the paired init/quit keeps each test standalone;
// subsystem init is refcounted, so it restores the prior state exactly.
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
    // No saved GUIDs: init_input must not attach anything to any seat.
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    init_input();

    EXPECT_TRUE(og::input_native::joystick_subsystem_initialized())
        << "boot init must bring the joystick subsystem up";
    const int count = std::min(
        og::input_native::num_joysticks(), 10);
    ASSERT_GT(count, 0);
    bool opened_device = false;
    for (int i = 0; i < count; ++i)
        opened_device = opened_device || joysticks[i] != nullptr;
    EXPECT_TRUE(opened_device);
    EXPECT_TRUE(SDL_JoystickEventsEnabled());
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_FALSE(playerHasJoystick(p))
            << "the positional device-i-to-player-i auto-bind is gone; seat "
            << p << " must stay keyboard-driven";
    }

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

TEST(InputJoystick, hat_motion_dispatch_sets_key_press_event)
{
    clear_keyboard();
    SDL_Event e{};
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    e.jhat.which = 3;
    e.jhat.hat = 0;
    e.jhat.value = SDL_HAT_UP;
    handle_events(e);
    EXPECT_EQ(1, query_key_press_event())
        << "hat-only d-pads must reach press-any-key waits through handle_events";

    clear_keyboard();
    e.jhat.value = SDL_HAT_CENTERED;
    handle_events(e);
    EXPECT_EQ(0, query_key_press_event())
        << "a hat returning to center is a release, not a press";

    // Sub-deadzone axis noise must not count as a press either.
    clear_keyboard();
    SDL_Event axis{};
    axis.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    axis.jaxis.which = 3;
    axis.jaxis.axis = 0;
    axis.jaxis.value = 7999;
    handle_events(axis);
    EXPECT_EQ(0, query_key_press_event());
}

TEST(InputJoystick, joy_binding_display_name_formats)
{
    EXPECT_EQ("B0", joy_binding_display_name(JoyData::BUTTON, 0));
    EXPECT_EQ("B11", joy_binding_display_name(JoyData::BUTTON, 11));
    EXPECT_EQ("A0+", joy_binding_display_name(JoyData::POS_AXIS, 0));
    EXPECT_EQ("A1-", joy_binding_display_name(JoyData::NEG_AXIS, 1));
    EXPECT_EQ("HU", joy_binding_display_name(JoyData::HAT_UP, 0));
    EXPECT_EQ("HR", joy_binding_display_name(JoyData::HAT_RIGHT, 0));
    EXPECT_EQ("HD", joy_binding_display_name(JoyData::HAT_DOWN, 0));
    EXPECT_EQ("HL", joy_binding_display_name(JoyData::HAT_LEFT, 1));
    EXPECT_EQ("--", joy_binding_display_name(JoyData::NONE, 0));
    // Diagonal hat kinds never produce input, so they read as unbound.
    EXPECT_EQ("--", joy_binding_display_name(JoyData::HAT_UP_RIGHT, 0));
    EXPECT_EQ("--", joy_binding_display_name(JoyData::HAT_DOWN_LEFT, 0));
    EXPECT_EQ("--", joy_binding_display_name(999, 3));
}

TEST(InputJoystick, assignment_api_assign_steal_clear)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad_a(2, 6, 0, "OpenGlad assign pad A");
    VirtualJoystick pad_b(2, 6, 0, "OpenGlad assign pad B");
    ASSERT_NE(nullptr, pad_a.get()) << SDL_GetError();
    ASSERT_NE(nullptr, pad_b.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d_a = device_index_of(pad_a.instance_id());
    const int d_b = device_index_of(pad_b.instance_id());
    ASSERT_GE(d_a, 0);
    ASSERT_GE(d_b, 0);
    ASSERT_LT(d_a, 10);
    ASSERT_LT(d_b, 10);
    joysticks[d_a] = pad_a.get();
    joysticks[d_b] = pad_b.get();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    // Rejections: bad seat, bad device index, empty table slot.
    EXPECT_FALSE(assign_joystick_to_player(-1, d_a));
    EXPECT_FALSE(assign_joystick_to_player(4, d_a));
    EXPECT_FALSE(assign_joystick_to_player(0, -1));
    EXPECT_FALSE(assign_joystick_to_player(0, 10));
    int empty_slot = -1;
    for (int i = 0; i < 10; ++i)
    {
        if (i != d_a && i != d_b)
        {
            empty_slot = i;
            break;
        }
    }
    EXPECT_FALSE(assign_joystick_to_player(0, empty_slot));
    EXPECT_FALSE(playerHasJoystick(0));

    // Happy path: default layout synthesis plus a GUID claim.
    ASSERT_TRUE(assign_joystick_to_player(0, d_a));
    EXPECT_EQ(d_a, player_joystick_device(0));
    EXPECT_TRUE(playerHasJoystick(0));
    EXPECT_EQ(JoyData::BUTTON, player_joy[0].key_type[KEY_FIRE]);
    EXPECT_EQ(JoyData::POS_AXIS, player_joy[0].key_type[KEY_RIGHT]);
    const std::string guid_a =
        input_hardware_state().player_joystick_guids[0];
    EXPECT_FALSE(guid_a.empty());
    EXPECT_EQ(guid_a, og::input_native::joystick_device_guid(d_a));

    ASSERT_TRUE(assign_joystick_to_player(1, d_b));
    EXPECT_EQ(d_b, player_joystick_device(1));

    // Steal: assigning a held device clears it from the previous seat.
    ASSERT_TRUE(assign_joystick_to_player(2, d_a));
    EXPECT_EQ(d_a, player_joystick_device(2));
    EXPECT_EQ(-1, player_joystick_device(0));
    EXPECT_TRUE(input_hardware_state().player_joystick_guids[0].empty())
        << "the robbed seat's persisted claim must be voided";
    EXPECT_EQ(d_b, player_joystick_device(1)) << "unrelated seat untouched";

    clear_player_joystick(2);
    EXPECT_EQ(-1, player_joystick_device(2));
    EXPECT_TRUE(input_hardware_state().player_joystick_guids[2].empty());
    EXPECT_EQ(JoyData::NONE, player_joy[2].key_type[KEY_FIRE])
        << "clear_player_joystick fully unbinds the layout";

    EXPECT_EQ(-1, player_joystick_device(-1));
    EXPECT_EQ(-1, player_joystick_device(4));
    EXPECT_GE(joystick_device_count(), 2);
}

TEST(InputJoystick, joystick_device_guid_seam_bounds_and_identity)
{
    JoystickSubsystemGuard subsystem_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad guid pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();

    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    const std::string guid = og::input_native::joystick_device_guid(d);
    EXPECT_FALSE(guid.empty()) << "an attached device has a GUID";

    const int count = og::input_native::num_joysticks();
    EXPECT_EQ("", og::input_native::joystick_device_guid(-1));
    EXPECT_EQ("", og::input_native::joystick_device_guid(count));

    // GUIDs identify the device model: an identical descriptor yields the
    // same GUID (this is what makes replug re-attach possible), while a
    // different one yields a different GUID.
    VirtualJoystick twin(2, 6, 0, "OpenGlad guid pad");
    VirtualJoystick other(1, 2, 1, "OpenGlad other guid pad");
    ASSERT_NE(nullptr, twin.get()) << SDL_GetError();
    ASSERT_NE(nullptr, other.get()) << SDL_GetError();
    const int d_twin = device_index_of(twin.instance_id());
    const int d_other = device_index_of(other.instance_id());
    ASSERT_GE(d_twin, 0);
    ASSERT_GE(d_other, 0);
    EXPECT_EQ(guid, og::input_native::joystick_device_guid(d_twin));
    EXPECT_NE(guid, og::input_native::joystick_device_guid(d_other));
}

TEST(InputJoystick, guid_persists_and_reattaches_through_local_cfg_store)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad persist pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    ASSERT_TRUE(assign_joystick_to_player(1, d));
    const std::string guid =
        input_hardware_state().player_joystick_guids[1];
    ASSERT_FALSE(guid.empty());

    // LOCAL store only — never disk (the cfg clobber hazard).
    cfg_store config;
    save_player_control_settings_to_cfg(config);
    EXPECT_EQ(guid, config.get_setting("controls", "player2_joystick_guid"));
    EXPECT_EQ("", config.get_setting("controls", "player1_joystick_guid"));

    // Wipe the seat, then reload: the SDL-free loader stores the GUID and
    // the registered re-attach hook resolves it against connected hardware.
    clear_player_joystick(1);
    ASSERT_FALSE(playerHasJoystick(1));
    load_player_control_settings_from_cfg(config);
    EXPECT_EQ(guid, input_hardware_state().player_joystick_guids[1]);
    EXPECT_EQ(d, player_joystick_device(1))
        << "the saved GUID must re-attach to the connected device";
    EXPECT_EQ(JoyData::BUTTON, player_joy[1].key_type[KEY_FIRE])
        << "a fresh GUID attach synthesizes the default layout";

    // A cfg whose GUID differs from the live one drops the attachment.
    cfg_store other_config;
    other_config.apply_setting(
        "controls", "player2_joystick_guid", "not-a-connected-device");
    load_player_control_settings_from_cfg(other_config);
    EXPECT_EQ(-1, player_joystick_device(1));
    EXPECT_EQ("not-a-connected-device",
              input_hardware_state().player_joystick_guids[1])
        << "an unresolved GUID stays stored, waiting for a hotplug add";

    // Two seats claiming the same GUID (identical pad models, one unit
    // connected): the lower seat wins, the other keeps waiting.
    cfg_store twin_config;
    twin_config.apply_setting("controls", "player1_joystick_guid", guid);
    twin_config.apply_setting("controls", "player2_joystick_guid", guid);
    load_player_control_settings_from_cfg(twin_config);
    EXPECT_EQ(d, player_joystick_device(0));
    EXPECT_EQ(-1, player_joystick_device(1));
    EXPECT_EQ(guid, input_hardware_state().player_joystick_guids[1]);
}

TEST(InputJoystick, hotplug_remove_falls_back_and_replug_reattaches)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    JoystickHandleGuard handle_guard;
    og::input_native::joystick_set_event_state(true);
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);
    trace_clear();

    VirtualJoystick decoy(1, 2, 0, "OpenGlad hotplug decoy");
    VirtualJoystick pad(2, 6, 0, "OpenGlad hotplug pad");
    ASSERT_NE(nullptr, decoy.get()) << SDL_GetError();
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    const int d_decoy = device_index_of(decoy.instance_id());
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d_decoy, 0);
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d_decoy] = decoy.get();
    joysticks[d] = pad.get();
    ASSERT_TRUE(assign_joystick_to_player(0, d));
    const std::string guid =
        input_hardware_state().player_joystick_guids[0];
    ASSERT_FALSE(guid.empty());
    // Customize the layout to prove a replug keeps the session's bindings.
    player_joy[0].key_index[KEY_YELL] = 5;

    // Drain the attach events before the controlled unplugs.
    clear_events();

    // Removing an UNRELATED device re-anchors the seat onto its device's
    // (possibly shifted) new index instead of dropping it.
    decoy.detach();
    get_input_events(POLL);
    EXPECT_EQ(device_index_of(pad.instance_id()), player_joystick_device(0))
        << "an unrelated removal must keep the seat on its own device";
    EXPECT_EQ(5, player_joy[0].key_index[KEY_YELL]);

    pad.detach();
    get_input_events(POLL);
    EXPECT_EQ(-1, player_joystick_device(0))
        << "unplugging the assigned device falls the seat back to keyboard";
    EXPECT_EQ(guid, input_hardware_state().player_joystick_guids[0])
        << "the GUID survives the unplug so the seat can re-attach";
    EXPECT_TRUE(trace_contains("joystick", "device_removed player=0"));

    // Replug (same descriptor = same GUID): the added event re-opens the
    // device table and re-attaches the seat, keeping its customized layout.
    VirtualJoystick replug(2, 6, 0, "OpenGlad hotplug pad");
    ASSERT_NE(nullptr, replug.get()) << SDL_GetError();
    get_input_events(POLL);
    const int d_replug = device_index_of(replug.instance_id());
    ASSERT_GE(d_replug, 0);
    EXPECT_EQ(d_replug, player_joystick_device(0));
    EXPECT_TRUE(trace_contains("joystick", "device_added"));
    EXPECT_TRUE(trace_contains("joystick", "guid_reattach player=0"));
    EXPECT_EQ(5, player_joy[0].key_index[KEY_YELL])
        << "a replug re-anchors the device without resetting the layout";

    // Legacy seat (takeover-bound, no GUID) pointing past the table after a
    // removal: falls back and stays unbound.
    player_joy[1].index = 9;
    player_joy[1].key_type[KEY_FIRE] = JoyData::BUTTON;
    player_joy[1].key_index[KEY_FIRE] = 0;
    trace_clear();
    replug.detach();
    get_input_events(POLL);
    EXPECT_EQ(-1, player_joystick_device(0));
    EXPECT_EQ(guid, input_hardware_state().player_joystick_guids[0]);
    EXPECT_EQ(-1, player_joystick_device(1));
    EXPECT_TRUE(trace_contains("joystick", "device_removed player=1"));
}

TEST(InputJoystick, remap_ignores_other_devices_when_seat_is_assigned)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad remap-lock pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    ASSERT_TRUE(assign_joystick_to_player(0, d));
    ASSERT_EQ(3, player_joy[0].key_index[KEY_YELL]) << "default layout";

    // Another device's press during remap must be ignored: no re-point, no
    // mapping change. (4096 is not an attached device, so the decode seam's
    // pass-through delivers it as that raw index.)
    SDL_Event e{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 4096;
    e.jbutton.button = 2;
    player_joy[0].setKeyFromEvent(KEY_YELL, e);
    EXPECT_EQ(d, player_joy[0].index)
        << "an assigned seat must not follow another device";
    EXPECT_EQ(3, player_joy[0].key_index[KEY_YELL])
        << "the ignored event must not touch the mapping";

    // Same rule for axis and hat events from another device.
    SDL_Event axis{};
    axis.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    axis.jaxis.which = 4096;
    axis.jaxis.axis = 1;
    axis.jaxis.value = 20000;
    player_joy[0].setKeyFromEvent(KEY_FIRE, axis);
    EXPECT_EQ(JoyData::BUTTON, player_joy[0].key_type[KEY_FIRE]);
    EXPECT_EQ(d, player_joy[0].index);
    SDL_Event hat{};
    hat.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    hat.jhat.which = 4096;
    hat.jhat.hat = 0;
    hat.jhat.value = SDL_HAT_UP;
    player_joy[0].setKeyFromEvent(KEY_UP, hat);
    EXPECT_EQ(JoyData::NEG_AXIS, player_joy[0].key_type[KEY_UP])
        << "default axis layout must survive the foreign hat event";
    EXPECT_EQ(d, player_joy[0].index);

    // The seat's own device still remaps (instance id resolves to index d).
    e.jbutton.which = static_cast<SDL_JoystickID>(pad.instance_id());
    e.jbutton.button = 5;
    player_joy[0].setKeyFromEvent(KEY_YELL, e);
    EXPECT_EQ(5, player_joy[0].key_index[KEY_YELL]);
    EXPECT_EQ(d, player_joy[0].index);

    // Legacy behavior on an unassigned seat: last device pressed wins and
    // robs the assigned seat, voiding its GUID claim.
    e.jbutton.button = 1;
    player_joy[1].setKeyFromEvent(KEY_FIRE, e);
    EXPECT_EQ(d, player_joy[1].index);
    EXPECT_EQ(-1, player_joy[0].index)
        << "the takeover must release the previous holder";
    EXPECT_TRUE(input_hardware_state().player_joystick_guids[0].empty())
        << "the robbed seat's GUID claim must be cleared";
}

// ---------------------------------------------------------------------------
// The per-seat INPUT cycler (og::ui, design §2.2): keyboard mapping names
// first, then JOY{n} for each connected device not held by another active
// seat. All through a LOCAL cfg_store — never disk (the cfg clobber hazard).

namespace
{
std::vector<std::string> cycle_option_names(
    const std::vector<og::ui::InputCycleOption>& options)
{
    std::vector<std::string> names;
    names.reserve(options.size());
    for (const og::ui::InputCycleOption& option : options)
        names.push_back(option.name);
    return names;
}
} // namespace

TEST(InputJoystick, input_cycler_enumerates_devices_and_skips_held_elsewhere)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad_a(2, 6, 0, "OpenGlad cycler pad A");
    VirtualJoystick pad_b(2, 6, 0, "OpenGlad cycler pad B");
    ASSERT_NE(nullptr, pad_a.get()) << SDL_GetError();
    ASSERT_NE(nullptr, pad_b.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d_a = device_index_of(pad_a.instance_id());
    const int d_b = device_index_of(pad_b.instance_id());
    ASSERT_GE(d_a, 0);
    ASSERT_GE(d_b, 0);
    ASSERT_LT(d_a, 10);
    ASSERT_LT(d_b, 10);
    joysticks[d_a] = pad_a.get();
    joysticks[d_b] = pad_b.get();
    reset_default_player_controls();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    // LOCAL store only — never disk (the cfg clobber hazard).
    cfg_store config;

    // Both devices free: seat 0 sees its keyboard names (minus seat 1's
    // ARROWS) and then every connected device, 1-based, in index order.
    {
        const std::vector<og::ui::InputCycleOption> options =
            og::ui::input_cycle_options(config, 0, 2);
        std::vector<std::string> expected = {"WASD", "IJKL", "TFGH"};
        for (int device = 0; device < joystick_device_count(); ++device)
            expected.push_back(std::format("JOY{}", device + 1));
        EXPECT_EQ(expected, cycle_option_names(options));
        for (const og::ui::InputCycleOption& option : options)
        {
            if (option.is_joystick)
            {
                EXPECT_EQ(std::format("JOY{}", option.joystick_device + 1),
                          option.name);
            }
            else
            {
                EXPECT_EQ(-1, option.joystick_device) << option.name;
            }
        }
    }

    // A device held by ANOTHER active seat is skipped...
    ASSERT_TRUE(assign_joystick_to_player(1, d_b));
    {
        const std::vector<og::ui::InputCycleOption> options =
            og::ui::input_cycle_options(config, 0, 2);
        std::vector<std::string> expected = {"WASD", "IJKL", "TFGH"};
        for (int device = 0; device < joystick_device_count(); ++device)
        {
            if (device != d_b)
                expected.push_back(std::format("JOY{}", device + 1));
        }
        EXPECT_EQ(expected, cycle_option_names(options));
    }

    // ...but not from the seat that holds it,
    {
        const std::vector<og::ui::InputCycleOption> options =
            og::ui::input_cycle_options(config, 1, 2);
        std::vector<std::string> expected = {"ARROWS", "IJKL", "TFGH"};
        for (int device = 0; device < joystick_device_count(); ++device)
            expected.push_back(std::format("JOY{}", device + 1));
        EXPECT_EQ(expected, cycle_option_names(options));
    }

    // ...and not from a roster that does not include the holder (seat 1 is
    // outside a 1-seat roster, so its claim is invisible).
    {
        const std::vector<og::ui::InputCycleOption> options =
            og::ui::input_cycle_options(config, 0, 1);
        std::vector<std::string> expected = {"WASD", "ARROWS", "IJKL", "TFGH"};
        for (int device = 0; device < joystick_device_count(); ++device)
            expected.push_back(std::format("JOY{}", device + 1));
        EXPECT_EQ(expected, cycle_option_names(options));
    }

    // current_input_selection reflects the assigned device.
    const og::ui::InputCycleOption current =
        og::ui::current_input_selection(1);
    EXPECT_TRUE(current.is_joystick);
    EXPECT_EQ(d_b, current.joystick_device);
    EXPECT_EQ(std::format("JOY{}", d_b + 1), current.name);
}

TEST(InputJoystick, input_cycler_cycles_between_keyboard_and_joystick)
{
    JoystickSubsystemGuard subsystem_guard;
    CompleteInputStateGuard input_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad cycler landing pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    reset_default_player_controls();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    // LOCAL store only — never disk (the cfg clobber hazard).
    cfg_store config;
    const std::string joy_name = std::format("JOY{}", d + 1);

    // A joystick option pointing at an unopened device slot is refused.
    int empty_slot = -1;
    for (int i = 0; i < 10; ++i)
    {
        if (i != d)
        {
            empty_slot = i;
            break;
        }
    }
    EXPECT_FALSE(og::ui::apply_input_cycle_selection(
        config, 0,
        {.name = "JOY?", .is_joystick = true, .joystick_device = empty_slot}));
    EXPECT_FALSE(playerHasJoystick(0));

    // Park the solo seat on the LAST keyboard option (no joystick assigned,
    // so the keyboard branch skips the clear)...
    ASSERT_TRUE(og::ui::apply_input_cycle_selection(config, 0,
                                                    {.name = "TFGH"}));
    ASSERT_EQ("TFGH", og::ui::current_input_selection(0).name);

    // ...so the next cycle lands on the joystick: device assigned, JOY name.
    EXPECT_EQ(joy_name, og::ui::cycle_player_input(config, 0, 1));
    EXPECT_EQ(d, player_joystick_device(0));
    const og::ui::InputCycleOption current =
        og::ui::current_input_selection(0);
    EXPECT_TRUE(current.is_joystick);
    EXPECT_EQ(d, current.joystick_device);
    EXPECT_EQ(joy_name, current.name);
    EXPECT_EQ(std::format("INPUT: {}", joy_name),
              og::ui::input_cycle_button_label(0));
    EXPECT_EQ(JoyData::BUTTON, player_joy[0].key_type[KEY_FIRE])
        << "landing on a JOY option synthesizes the default device layout";

    // Cycling off the joystick wraps to WASD: device cleared, mapping loaded.
    EXPECT_EQ("WASD", og::ui::cycle_player_input(config, 0, 1));
    EXPECT_EQ(-1, player_joystick_device(0));
    EXPECT_FALSE(playerHasJoystick(0));
    EXPECT_FALSE(og::ui::current_input_selection(0).is_joystick);
    EXPECT_EQ("WASD", og::ui::current_input_selection(0).name)
        << "the factory WASD mapping must actually load into the seat";

    // A seat whose current JOY selection vanished from the options (device
    // detached, hotplug not yet pumped) restarts the cycle at the first
    // option instead of getting stuck.
    ASSERT_TRUE(og::ui::apply_input_cycle_selection(
        config, 0,
        {.name = joy_name, .is_joystick = true, .joystick_device = d}));
    ASSERT_TRUE(playerHasJoystick(0));
    joysticks[d] = nullptr;
    pad.detach();
    ASSERT_EQ(0, joystick_device_count())
        << "the detached virtual pad must leave enumeration immediately";
    EXPECT_EQ("WASD", og::ui::cycle_player_input(config, 0, 1));
    EXPECT_EQ(-1, player_joystick_device(0));
}

TEST(InputJoystick, input_cycler_single_option_is_a_no_op)
{
    CompleteInputStateGuard input_guard;
    JoystickHandleGuard handle_guard;
    reset_default_player_controls();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    // LOCAL store only — never disk (the cfg clobber hazard).
    cfg_store config;
    ASSERT_EQ(0, joystick_device_count())
        << "this test needs a joystick-free device table";

    // Four active seats hold all four factory names and no devices are
    // connected: seat 0's only option is its own WASD, so the cycle is a
    // no-op that reports the current name.
    EXPECT_EQ("WASD", og::ui::cycle_player_input(config, 0, 4));
    EXPECT_EQ("WASD", og::ui::current_input_selection(0).name);
}
