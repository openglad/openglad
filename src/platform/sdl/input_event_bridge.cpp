// input_event_bridge.cpp
//
// Implements input event handlers that require runtime/render access.
// These functions are declared in input.h but implemented here (in the
// runtime module) to keep the input module free of runtime/render deps.

#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/core/util.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/sai2x.h>
#include <SDL3/SDL.h>

static inline screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

namespace
{
const SDL_Event& as_sdl_event(const void* native_event)
{
    return *static_cast<const SDL_Event*>(native_event);
}

void autosave_active_screen(screen& s, const char* event_name)
{
    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->gameplay_active_)
    {
        s.sync_save_data_from_world();
    }
    const SaveDataIoError err = s.save_data.save_with_error("save0");
    if (err != SaveDataIoError::None)
    {
        LogError("window_autosave_failed event={} error={}\n",
                 event_name, static_cast<int>(err));
    }
}
} // namespace

void handle_window_event(const void* native_event)
{
    if (!native_event)
        return;
    const SDL_Event& event = as_sdl_event(native_event);

    // SDL3: window events are first-class event types; switch on event.type
    // (unhandled window subtypes fall through harmlessly).
    switch(event.type)
    {
        case SDL_EVENT_WINDOW_MINIMIZED:
            // Save state here on Android
            if(screen* s = active_screen())
                autosave_active_screen(*s, "minimized");
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // Save state here on Android
            if(screen* s = active_screen())
                autosave_active_screen(*s, "close");
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            // Restore state here on Android.
            // Redraw the screen so it's not blank
            if(screen* s = active_screen())
                s->refresh();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            og::runtime::current_session->window_w_ = static_cast<float>(event.window.data1);
            og::runtime::current_session->window_h_ = static_cast<float>(event.window.data2);
            update_overscan_setting();
            // Under a cfg graphics/scale mode the world canvas tracks the
            // window (canvas = window/factor): re-derive it and re-lay-out the
            // viewscreens. Gated on non-Legacy so default runs (classic fixed
            // 320x200 canvas) take exactly the pre-existing path.
            if (E_Screen &&
                E_Screen->world_scale().mode != og::WorldScaleMode::Legacy)
            {
                const int old_w = E_Screen->world_w();
                const int old_h = E_Screen->world_h();
                E_Screen->apply_world_scale_for_window(
                    static_cast<int>(event.window.data1),
                    static_cast<int>(event.window.data2));
                if (E_Screen->world_w() != old_w || E_Screen->world_h() != old_h)
                {
                    if (screen* s = active_screen())
                        s->relayout_views();
                }
            }
            break;
        break;
    }
}

void handle_key_event(const void* native_event)
{
    if (!native_event)
        return;
    const SDL_Event& event = as_sdl_event(native_event);

    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        #ifdef USE_TOUCH_INPUT
        // Back button faking Escape key
        if(event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            sendFakeKeyDownEvent(SDLK_ESCAPE);
            break;
        }
        #endif
        og::runtime::current_session->raw_key_ = static_cast<int>(event.key.key);
        if(og::runtime::current_session->raw_key_ == SDLK_ESCAPE)
            og::runtime::current_session->input_continue_ = true;
        og::runtime::current_session->key_press_event_ = 1;

        if(event.key.key == SDLK_F10)
        {
            if(screen* s = active_screen())
                s->save_screenshot();
        }
        else if(event.key.key == SDLK_F12 && event.key.mod & SDL_KMOD_CTRL)
        {
            restore_default_settings();
            cfg.load_settings();
            load_player_control_settings_from_cfg(cfg);
            og::runtime::current_session->overscan_percentage_ = static_cast<float>(
                parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
            update_overscan_setting();
        }
        break;
    case SDL_EVENT_KEY_UP:
        #ifdef USE_TOUCH_INPUT
        // Back button faking Escape key
        if(event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            sendFakeKeyUpEvent(SDLK_ESCAPE);
            break;
        }
        #endif
        break;
    }
}

#ifdef USE_TOUCH_INPUT
#include <openglad/gameplay/obmap.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

void draw_touch_controls(screen* vob)
{
    walker* control = vob->viewob[0]->control;
    if(control == nullptr || control->dead())
        return;

    auto& hw = *og::runtime::current_session->input_hw_;
    if(hw.moving)
    {
        // Touch movement feedback
        vob->fastbox(hw.moving_touch_x - MOVE_AREA_DIM/2, hw.moving_touch_y - MOVE_AREA_DIM/2, MOVE_AREA_DIM, MOVE_AREA_DIM, 17);
        vob->fastbox(hw.moving_touch_x - 4, hw.moving_touch_y - 4, 8, 8, 16);
        vob->fastbox(hw.moving_touch_target_x - 2, hw.moving_touch_target_y - 2, 4, 4, 15);
    }

    // Touch buttons
    vob->fastbox(FIRE_BUTTON_X, FIRE_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 25);

    if(vob->special_name[static_cast<int>(control->family)][static_cast<int>(control->current_special())] != "NONE")
        vob->fastbox(SPECIAL_BUTTON_X, SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 26);

    if(control->current_special() != 1
                || (control->current_special() + 1 <= NUM_SPECIALS && control->current_special()*3+1 <= control->stats()->level()
                     && vob->special_name[static_cast<int>(control->family)][static_cast<int>(control->current_special()) + 1] != "NONE"))
        vob->fastbox(NEXT_SPECIAL_BUTTON_X, NEXT_SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 27);

    if(vob->alternate_name[static_cast<int>(control->family)][static_cast<int>(control->current_special())] != "NONE")
        vob->fastbox(ALTERNATE_SPECIAL_BUTTON_X, ALTERNATE_SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM, 28);
}

bool input_touch_has_alternate()
{
    screen* s = active_screen();
    if(s != nullptr
        && s->viewob[0] != nullptr
        && s->viewob[0]->control != nullptr
        && s->alternate_name[static_cast<int>(s->viewob[0]->control->family)][static_cast<int>(s->viewob[0]->control->current_special())] != "NONE")
        return true;
    return false;
}
#endif
