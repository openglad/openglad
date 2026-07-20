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
#include <openglad/resources/company.h>
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
    const SaveDataIoError err =
        s.save_data.save_with_error(og::data::active_company_slot());
    if (err != SaveDataIoError::None)
    {
        LogError("window_autosave_failed event={} error={}\n",
                 event_name, static_cast<int>(err));
    }
}

void reapply_world_zoom_after_window_change(screen& s)
{
    const int old_w = s.world_canvas_w();
    const int old_h = s.world_canvas_h();
    s.reapply_world_scale();
    if (s.world_canvas_w() != old_w || s.world_canvas_h() != old_h)
        s.relayout_views();
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
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            // Fullscreen changes can complete asynchronously. Reconcile cfg
            // and viewport state from the window once SDL reports the final
            // transition rather than trusting the initiating request alone.
            if (screen* s = active_screen())
            {
                s->reflect_display_settings_from_window(
                    DisplayStateConfirmation::EnterFullscreen,
                    event.common.timestamp);
                reapply_world_zoom_after_window_change(*s);
            }
            break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            if (screen* s = active_screen())
            {
                s->reflect_display_settings_from_window(
                    DisplayStateConfirmation::LeaveFullscreen,
                    event.common.timestamp);
                reapply_world_zoom_after_window_change(*s);
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            // Persist a user/WM resize in Windowed mode. In fullscreen the SDL
            // backend keeps the remembered Windowed dimensions intact while it
            // reconciles the actual Borderless/Exclusive state.
            if (screen* s = active_screen())
            {
                s->reflect_display_settings_from_window(
                    DisplayStateConfirmation::Resized,
                    event.common.timestamp);
                // Reflection queries SDL's completed logical size and rejects
                // stale completion events by timestamp. Never overwrite that
                // protected result with an unconditionally trusted payload.
                reapply_world_zoom_after_window_change(*s);
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            // A fullscreen mode switch can complete with only this event on
            // HiDPI backends. Its payload is physical pixels, so confirm the
            // display mode without overwriting logical input/window metrics.
            if (screen* s = active_screen())
            {
                s->reflect_display_settings_from_window(
                    DisplayStateConfirmation::PixelSizeChanged,
                    event.common.timestamp);
                reapply_world_zoom_after_window_change(*s);
            }
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
            if (screen* s = active_screen())
            {
                const int old_w = s->world_canvas_w();
                const int old_h = s->world_canvas_h();
                // Match the in-menu RESTORE DEFAULTS path: mode, window
                // size, zoom and smoothing all take effect immediately.
                s->apply_display_settings_from_cfg();
                if (s->world_canvas_w() != old_w || s->world_canvas_h() != old_h)
                    s->relayout_views();
                s->redrawme = 1;
            }
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

    const TouchControlLayout layout = touch_control_layout(
        vob->canvas_w(), vob->canvas_h());
    const auto draw_control = [&](const TouchControlRect& rect,
                                  unsigned char color) {
        vob->fastbox(rect.x, rect.y, rect.w, rect.h, color);
    };

    auto& hw = *og::runtime::current_session->input_hw_;
    if(hw.moving)
    {
        // Touch movement feedback
        vob->fastbox(hw.moving_touch_x - layout.movement_area_offset_x,
                     hw.moving_touch_y - layout.movement_area_offset_y,
                     layout.movement_area_w, layout.movement_area_h, 17);
        vob->fastbox(hw.moving_touch_x - layout.movement_center_offset_x,
                     hw.moving_touch_y - layout.movement_center_offset_y,
                     layout.movement_center_w, layout.movement_center_h, 16);
        vob->fastbox(hw.moving_touch_target_x - layout.movement_target_offset_x,
                     hw.moving_touch_target_y - layout.movement_target_offset_y,
                     layout.movement_target_w, layout.movement_target_h, 15);
    }

    // Touch buttons
    draw_control(layout.fire, 25);

    if(vob->special_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())] != "NONE")
        draw_control(layout.special, 26);

    if(control->current_special() != 1
                || (control->current_special() + 1 <= NUM_SPECIALS && control->current_special()*3+1 <= control->stats()->level()
                     && vob->special_name[static_cast<int>(control->family())][static_cast<int>(control->current_special()) + 1] != "NONE"))
        draw_control(layout.next_special, 27);

    if(vob->alternate_name[static_cast<int>(control->family())][static_cast<int>(control->current_special())] != "NONE")
        draw_control(layout.alternate_special, 28);
}

bool input_touch_has_alternate()
{
    screen* s = active_screen();
    if(s != nullptr
        && s->viewob[0] != nullptr
        && s->viewob[0]->control != nullptr
        && s->alternate_name[static_cast<int>(s->viewob[0]->control->family())][static_cast<int>(s->viewob[0]->control->current_special())] != "NONE")
        return true;
    return false;
}
#endif
