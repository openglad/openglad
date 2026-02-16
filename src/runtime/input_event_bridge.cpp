// input_event_bridge.cpp
//
// Implements input event handlers that require runtime/render access.
// These functions are declared in input.h but implemented here (in the
// runtime module) to keep the input module free of runtime/render deps.

#include <openglad/input/input.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/data/gparser.h>
#include <openglad/platform/io.h>
#include <openglad/legacy/base.h>

// Globals defined in input.cpp
extern int raw_key;
extern short key_press_event;
extern bool input_continue;

static inline screen* active_screen()
{
    if(ctx().active_screen() != nullptr)
        return ctx().active_screen();
    return myscreen;
}

static inline cfg_store* active_config()
{
    if(ctx().active_config() != nullptr)
        return ctx().active_config();
    return &cfg;
}

void handle_window_event(const SDL_Event& event)
{
    switch(event.window.event)
    {
        case SDL_WINDOWEVENT_MINIMIZED:
            // Save state here on Android
            if(screen* s = active_screen())
                s->save_data.save("save0");
            break;
        case SDL_WINDOWEVENT_CLOSE:
            // Save state here on Android
            if(screen* s = active_screen())
                s->save_data.save("save0");
            break;
        case SDL_WINDOWEVENT_RESTORED:
            // Restore state here on Android.
            // Redraw the screen so it's not blank
            if(screen* s = active_screen())
                s->refresh();
            break;
        case SDL_WINDOWEVENT_RESIZED:
            window_w = static_cast<float>(event.window.data1);
            window_h = static_cast<float>(event.window.data2);
            update_overscan_setting();
            break;
        break;
    }
}

void handle_key_event(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_KEYDOWN:
        #ifdef USE_TOUCH_INPUT
        // Back button faking Escape key
        if(event.key.keysym.scancode == SDL_SCANCODE_AC_BACK)
        {
            sendFakeKeyDownEvent(SDLK_ESCAPE);
            break;
        }
        #endif
        raw_key = event.key.keysym.sym;
        if(raw_key == SDLK_ESCAPE)
            input_continue = true;
        key_press_event = 1;

        if(event.key.keysym.sym == SDLK_F10)
        {
            if(screen* s = active_screen())
                s->save_screenshot();
        }
        else if(event.key.keysym.sym == SDLK_F12 && event.key.keysym.mod & KMOD_CTRL)
        {
            restore_default_settings();
            active_config()->load_settings();
        }
        break;
    case SDL_KEYUP:
        #ifdef USE_TOUCH_INPUT
        // Back button faking Escape key
        if(event.key.keysym.scancode == SDL_SCANCODE_AC_BACK)
        {
            sendFakeKeyUpEvent(SDLK_ESCAPE);
            break;
        }
        #endif
        break;
    }
}

#ifdef USE_TOUCH_INPUT
#include <openglad/entities/obmap.h>
#include <openglad/render/view.h>
#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>

// Touch globals defined in input.cpp
extern bool moving;
extern int moving_touch_x;
extern int moving_touch_y;
extern int moving_touch_target_x;
extern int moving_touch_target_y;

void draw_touch_controls(screen* vob)
{
    walker* control = vob->viewob[0]->control;
    if(control == nullptr || control->dead)
        return;

    if(moving)
    {
        // Touch movement feedback
        vob->fastbox(moving_touch_x - MOVE_AREA_DIM/2, moving_touch_y - MOVE_AREA_DIM/2, MOVE_AREA_DIM, MOVE_AREA_DIM, 17);
        vob->fastbox(moving_touch_x - 4, moving_touch_y - 4, 8, 8, 16);
        vob->fastbox(moving_touch_target_x - 2, moving_touch_target_y - 2, 4, 4, 15);
    }

    // Touch buttons
    vob->fastbox(FIRE_BUTTON_X, FIRE_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 25);

    if(vob->special_name[static_cast<int>(control->query_family())][static_cast<int>(control->current_special)] != "NONE")
        vob->fastbox(SPECIAL_BUTTON_X, SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 26);

    if(control->current_special != 1
                || (control->current_special + 1 <= NUM_SPECIALS && control->current_special*3+1 <= control->stats()->level
                     && vob->special_name[static_cast<int>(control->query_family())][static_cast<int>(control->current_special) + 1] != "NONE"))
        vob->fastbox(NEXT_SPECIAL_BUTTON_X, NEXT_SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 27);

    if(vob->alternate_name[static_cast<int>(control->query_family())][static_cast<int>(control->current_special)] != "NONE")
        vob->fastbox(ALTERNATE_SPECIAL_BUTTON_X, ALTERNATE_SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 28);
}

bool input_touch_has_alternate()
{
    screen* s = active_screen();
    if(s != nullptr
        && s->viewob[0] != nullptr
        && s->viewob[0]->control != nullptr
        && s->alternate_name[static_cast<int>(s->viewob[0]->control->query_family())][static_cast<int>(s->viewob[0]->control->current_special)] != "NONE")
        return true;
    return false;
}
#endif
