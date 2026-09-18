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
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
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
        // [SAVE-F2] Never write the company file from a mission-only roster.
        // Network games hold the combined roster; local lobby games hold an
        // isolated mission copy of the company. Their explicit win/withdraw
        // paths merge back into the private company. Skipping also leaves an
        // abandoned mission unable to promote the company timestamp or erase
        // private roster state.
        if (og::runtime::current_session->networked_session_ ||
            og::runtime::current_session->isolated_company_session_)
            return;
        s.sync_save_data_from_world();
    }
    // §3.8 WindowEvent autosave: stamp + atomic write to the active company,
    // no backup. [SAVE-F1] In a networked LOBBY (menus / between levels) the
    // in-memory save carries the HOST's campaign/settings, so the context
    // routes the write through the owner-preserving merge instead of a
    // plain save.
    const SaveDataIoError err = og::data::company_autosave(
        s.save_data,
        og::data::CompanyAutosaveKind::WindowEvent,
        og::ui::company_autosave_context(s.save_data,
                                         picker_lobby_is_networked()));
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
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_HIDDEN:
            // A key released while focus is elsewhere (or while the page is
            // hidden — on the web, visibilitychange delivers only HIDDEN
            // and SDL's own blur-time keyboard reset never runs) would
            // never reach us: the key would stay latched and, if it is a
            // direction, walk that player's character forever. Drop all
            // transient held input; genuine holds re-assert on the next
            // delivered key events.
            clear_transient_input_state();
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
    // Key pressed or released:
    case SDL_EVENT_KEY_DOWN:
        // Event-layer feed for the direction resolver: repeats included on
        // purpose — a repeat=true keydown is the only visible signal of a
        // physical re-press of a key whose keyup the browser swallowed (the
        // sampled keystate never went up, so it can't edge). See
        // input_direction_grace.h.
        note_direction_key_event(static_cast<int>(event.key.key));

        og::runtime::current_session->raw_key_ = static_cast<int>(event.key.key);
        if(og::runtime::current_session->raw_key_ == SDLK_ESCAPE)
            og::runtime::current_session->input_continue_ = true;
        og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;

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
                // Ctrl-F12 remains the full emergency reset (including
                // controls); mode, size, zoom and smoothing apply immediately.
                s->apply_display_settings_from_cfg();
                if (s->world_canvas_w() != old_w || s->world_canvas_h() != old_h)
                    s->relayout_views();
                s->redrawme = 1;
            }
        }
        break;
    case SDL_EVENT_KEY_UP:
        break;
    }
}
