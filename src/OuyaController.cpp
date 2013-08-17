#include "OuyaController.h"
#include <cmath>

// Init for OuyaControllerManager
static bool inited = false;

bool OuyaControllerManager::send_user_events;
Uint32 OuyaControllerManager::BUTTON_DOWN_EVENT;
Uint32 OuyaControllerManager::BUTTON_UP_EVENT;
Uint32 OuyaControllerManager::AXIS_EVENT;
OuyaController OuyaControllerManager::controller[MAX_PLAYERS];

OuyaController::OuyaController()
    : player(-1)
{
    for(int i = 0; i < NUM_BUTTONS; i++)
        button_state[i] = false;
    
    for(int i = 0; i < NUM_AXES; i++)
        axis_state[i] = 0.0f;
}

int OuyaController::getPlayerNum() const
{
    return player;
}

bool& OuyaController::getButtonValue(ButtonEnum button)
{
    switch(button)
    {
    case BUTTON_O:
        return button_state[0];
    case BUTTON_U:
        return button_state[1];
    case BUTTON_Y:
        return button_state[2];
    case BUTTON_A:
        return button_state[3];
    case BUTTON_L1:
        return button_state[4];
    case BUTTON_R1:
        return button_state[5];
    case BUTTON_L3:
        return button_state[6];
    case BUTTON_R3:
        return button_state[7];
    case BUTTON_MENU:
        return button_state[8];
    case BUTTON_DPAD_UP:
        return button_state[9];
    case BUTTON_DPAD_RIGHT:
        return button_state[10];
    case BUTTON_DPAD_DOWN:
        return button_state[11];
    case BUTTON_DPAD_LEFT:
        return button_state[12];
    }
    SDL_assert(false);
    return button_state[0];
}

bool OuyaController::getButtonValue(ButtonEnum button) const
{
    switch(button)
    {
    case BUTTON_O:
        return button_state[0];
    case BUTTON_U:
        return button_state[1];
    case BUTTON_Y:
        return button_state[2];
    case BUTTON_A:
        return button_state[3];
    case BUTTON_L1:
        return button_state[4];
    case BUTTON_R1:
        return button_state[5];
    case BUTTON_L3:
        return button_state[6];
    case BUTTON_R3:
        return button_state[7];
    case BUTTON_MENU:
        return button_state[8];
    case BUTTON_DPAD_UP:
        return button_state[9];
    case BUTTON_DPAD_RIGHT:
        return button_state[10];
    case BUTTON_DPAD_DOWN:
        return button_state[11];
    case BUTTON_DPAD_LEFT:
        return button_state[12];
    }
    return false;
}

float& OuyaController::getAxisValue(AxisEnum axis)
{
    switch(axis)
    {
    case AXIS_LS_X:
        return axis_state[0];
    case AXIS_LS_Y:
        return axis_state[1];
    case AXIS_RS_X:
        return axis_state[2];
    case AXIS_RS_Y:
        return axis_state[3];
    case AXIS_L2:
        return axis_state[4];
    case AXIS_R2:
        return axis_state[5];
    }
    SDL_assert(false);
    return axis_state[0];
}

float OuyaController::getAxisValue(AxisEnum axis) const
{
    switch(axis)
    {
    case AXIS_LS_X:
        return axis_state[0];
    case AXIS_LS_Y:
        return axis_state[1];
    case AXIS_RS_X:
        return axis_state[2];
    case AXIS_RS_Y:
        return axis_state[3];
    case AXIS_L2:
        return axis_state[4];
    case AXIS_R2:
        return axis_state[5];
    }
    return 0.0f;
}

inline float dist(float x, float y)
{
    return sqrtf(x*x + y*y);
}

bool OuyaController::isStickBeyondDeadzone(AxisEnum axis) const
{
    switch(axis)
    {
    case AXIS_LS_X:
        return dist(axis_state[0], axis_state[1]) >= DEADZONE;
    case AXIS_LS_Y:
        return dist(axis_state[0], axis_state[1]) >= DEADZONE;
    case AXIS_RS_X:
        return dist(axis_state[2], axis_state[3]) >= DEADZONE;
    case AXIS_RS_Y:
        return dist(axis_state[2], axis_state[3]) >= DEADZONE;
    case AXIS_L2:
        return fabs(axis_state[4]) >= DEADZONE;
    case AXIS_R2:
        return fabs(axis_state[5]) >= DEADZONE;
    }
    return false;
}

// Extra room for the cone to extend beyond 45 degree diagonals
// e.g. the negative X cone points left and covers a little more than 90 degrees total.
#define DIAG_OFFSET 11.25f  // 45/4

bool OuyaController::isStickInNegativeCone(AxisEnum axis) const
{
    if(!isStickBeyondDeadzone(axis))
        return false;
    
    switch(axis)
    {
    case AXIS_LS_X:
        {
            float dir = atan2(axis_state[1], axis_state[0]) * 180 / M_PI;
            return dir < -135.0f + DIAG_OFFSET || dir > 135.0f - DIAG_OFFSET;
        }
    case AXIS_LS_Y:
        {
            float dir = atan2(axis_state[1], axis_state[0]) * 180 / M_PI;
            return -135.0f - DIAG_OFFSET < dir && dir < -45.0f + DIAG_OFFSET;
        }
    case AXIS_RS_X:
        {
            float dir = atan2(axis_state[3], axis_state[2]) * 180 / M_PI;
            return dir < -135.0f + DIAG_OFFSET || dir > 135.0f - DIAG_OFFSET;
        }
    case AXIS_RS_Y:
        {
            float dir = atan2(axis_state[3], axis_state[2]) * 180 / M_PI;
            return -135.0f - DIAG_OFFSET < dir && dir < -45.0f + DIAG_OFFSET;
        }
    case AXIS_L2:
    case AXIS_R2:
        return false;
    }
    return false;
}

bool OuyaController::isStickInPositiveCone(AxisEnum axis) const
{
    if(!isStickBeyondDeadzone(axis))
        return false;
    
    switch(axis)
    {
    case AXIS_LS_X:
        {
            float dir = atan2(axis_state[1], axis_state[0]) * 180 / M_PI;
            return -45.0f - DIAG_OFFSET < dir && dir < 45.0f + DIAG_OFFSET;
        }
    case AXIS_LS_Y:
        {
            float dir = atan2(axis_state[1], axis_state[0]) * 180 / M_PI;
            return 135.0f + DIAG_OFFSET > dir && dir > 45.0f - DIAG_OFFSET;
        }
    case AXIS_RS_X:
        {
            float dir = atan2(axis_state[3], axis_state[2]) * 180 / M_PI;
            return -45.0f - DIAG_OFFSET < dir && dir < 45.0f + DIAG_OFFSET;
        }
    case AXIS_RS_Y:
        {
            float dir = atan2(axis_state[3], axis_state[2]) * 180 / M_PI;
            return 135.0f + DIAG_OFFSET > dir && dir > 45.0f - DIAG_OFFSET;
        }
    case AXIS_L2:
    case AXIS_R2:
        return false;
    }
    return false;
}








OuyaControllerManager::OuyaControllerManager()
{
    init();
}

void OuyaControllerManager::init()
{
    if(inited)
        return;
    
    send_user_events = true;
    
    BUTTON_DOWN_EVENT = SDL_RegisterEvents(1);
    BUTTON_UP_EVENT = SDL_RegisterEvents(1);
    AXIS_EVENT = SDL_RegisterEvents(1);
    
    SDL_assert(BUTTON_DOWN_EVENT != Uint32(-1));
    SDL_assert(BUTTON_UP_EVENT != Uint32(-1));
    SDL_assert(AXIS_EVENT != Uint32(-1));
    
    for(int i = 0; i < MAX_PLAYERS; i++)
    {
        controller[i].player = i;
    }
    
    inited = true;
}

OuyaController& OuyaControllerManager::getController(int player)
{
    SDL_assert(player >= 0 && player < MAX_PLAYERS);
    return controller[player];
}

void OuyaControllerManager::key_down(int player, int button)
{
    SDL_assert(player >= 0 && player < MAX_PLAYERS);
    
    controller[player].getButtonValue(OuyaController::ButtonEnum(button)) = true;
    
    if(send_user_events)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = BUTTON_DOWN_EVENT;
        event.user.code = player;
        event.user.data1 = (void*)button;
        event.user.data2 = 0;
        SDL_PushEvent(&event);
    }
}

void OuyaControllerManager::key_up(int player, int button)
{
    SDL_assert(player >= 0 && player < MAX_PLAYERS);
    
    controller[player].getButtonValue(OuyaController::ButtonEnum(button)) = false;
    
    if(send_user_events)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = BUTTON_UP_EVENT;
        event.user.code = player;
        event.user.data1 = (void*)button;
        event.user.data2 = 0;
        SDL_PushEvent(&event);
    }
}

void OuyaControllerManager::axis_motion(int player, float LS_X, float LS_Y, float RS_X, float RS_Y, float L2, float R2)
{
    SDL_assert(player >= 0 && player < MAX_PLAYERS);
    
    OuyaController& c = controller[player];
    c.getAxisValue(OuyaController::AXIS_LS_X) = LS_X;
    c.getAxisValue(OuyaController::AXIS_LS_Y) = LS_Y;
    c.getAxisValue(OuyaController::AXIS_RS_X) = RS_X;
    c.getAxisValue(OuyaController::AXIS_RS_Y) = RS_Y;
    c.getAxisValue(OuyaController::AXIS_L2) = L2;
    c.getAxisValue(OuyaController::AXIS_R2) = R2;
    
    if(send_user_events)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = AXIS_EVENT;
        event.user.code = player;
        event.user.data1 = 0;
        event.user.data2 = 0;
        SDL_PushEvent(&event);
    }
}

#ifdef __ANDROID__

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <jni.h>
#include <android/log.h>


extern "C" void Java_com_dinomage_openglad_Openglad_OuyaControllerKeyDown(
                                    JNIEnv* env, jclass cls,
                                    jint player, jint keyCode)
{
    //__android_log_print(ANDROID_LOG_VERBOSE, "OG", "nativeOnKeyDown(): player=%d, keyCode=%d", player, keyCode);
    OuyaControllerManager::key_down(player, keyCode);
}

extern "C" void Java_com_dinomage_openglad_Openglad_OuyaControllerKeyUp(
                                    JNIEnv* env, jclass cls,
                                    jint player, jint keyCode)
{
    //__android_log_print(ANDROID_LOG_VERBOSE, "OG", "nativeOnKeyUp()");
    OuyaControllerManager::key_up(player, keyCode);
}

extern "C" void Java_com_dinomage_openglad_Openglad_OuyaControllerGenericMotionEvent(
                                    JNIEnv* env, jclass cls,
                                    jint player, jfloat LS_X, jfloat LS_Y, jfloat RS_X, jfloat RS_Y, jfloat L2, jfloat R2)
{
    //__android_log_print(ANDROID_LOG_VERBOSE, "OG", "nativeOnGenericMotionEvent()");
    OuyaControllerManager::axis_motion(player, LS_X, LS_Y, RS_X, RS_Y, L2, R2);
}

#endif


