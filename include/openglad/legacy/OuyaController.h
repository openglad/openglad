#pragma once

#include "SDL.h"

class OuyaControllerManager;

class OuyaController
{
public:
    friend class OuyaControllerManager;
    
    enum class ButtonEnum {O = 96, U = 99, Y = 100, A = 97,
                     L1 = 102, R1 = 103, L3 = 106, R3 = 107,
                     Menu = 82,
                     DpadUp = 19, DpadRight = 22, DpadDown = 20, DpadLeft = 21};

    enum class AxisEnum {LsX = 0, LsY = 1, RsX = 11, RsY = 14, L2 = 17, R2 = 18};
    
    static const int NUM_BUTTONS = 13;
    static const int NUM_AXES = 6;
    static constexpr float DEADZONE = 0.25f;
    
    bool button_state[NUM_BUTTONS];
    float axis_state[NUM_AXES];
    
    // player is 0-based
    int getPlayerNum() const;
    bool getButtonValue(ButtonEnum button) const;
    float getAxisValue(AxisEnum axis) const;
    float getNormalizedAxisValue(AxisEnum axis) const;
    bool& getButtonValue(ButtonEnum button);
    float& getAxisValue(AxisEnum axis);
    
    // Combines coupled axes for a distance calculation
    bool isStickBeyondDeadzone(AxisEnum axis) const;
    bool isStickInNegativeCone(AxisEnum axis) const;
    bool isStickInPositiveCone(AxisEnum axis) const;
    
protected:
    // player is 0-based
    int player;
    
private:
    OuyaController();
};

// A monostate (static) class
class OuyaControllerManager
{
public:
    
    static const int MAX_PLAYERS = 4;
    
    static OuyaController controller[MAX_PLAYERS];
    
    static bool send_user_events;
    static Uint32 BUTTON_DOWN_EVENT;
    static Uint32 BUTTON_UP_EVENT;
    static Uint32 AXIS_EVENT;
    
    OuyaControllerManager();
    
    static void init();
    
    // player is 0-based
    static OuyaController& getController(int player);
    
    // Generate events and change state
    static void key_down(int player, int button);
    static void key_up(int player, int button);
    static void axis_motion(int player, float LS_X, float LS_Y, float RS_X, float RS_Y, float L2, float R2);
};

