#ifndef _OUYACONTROLLER_H__
#define _OUYACONTROLLER_H__

#include "SDL.h"

class OuyaControllerManager;

class OuyaController
{
public:
    friend class OuyaControllerManager;
    
    enum ButtonEnum {BUTTON_O = 96, BUTTON_U = 99, BUTTON_Y = 100, BUTTON_A = 97,
                     BUTTON_L1 = 102, BUTTON_R1 = 103, BUTTON_L3 = 106, BUTTON_R3 = 107,
                     BUTTON_MENU = 82,
                     BUTTON_DPAD_UP = 19, BUTTON_DPAD_RIGHT = 22, BUTTON_DPAD_DOWN = 20, BUTTON_DPAD_LEFT = 21};
    
    enum AxisEnum {AXIS_LS_X = 0, AXIS_LS_Y = 1, AXIS_RS_X = 11, AXIS_RS_Y = 14, AXIS_L2 = 17, AXIS_R2 = 18};
    
    static const int NUM_BUTTONS = 13;
    static const int NUM_AXES = 6;
    static const float DEADZONE = 0.25f;
    
    bool button_state[NUM_BUTTONS];
    float axis_state[NUM_AXES];
    
    // player is 0-based
    int getPlayerNum() const;
    bool getButtonValue(ButtonEnum button) const;
    float getAxisValue(AxisEnum axis) const;
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

#endif
