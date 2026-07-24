/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// SDL-specific GameContext wiring and link-time dispatch implementations.
// This file is compiled only in the SDL build (not in openglad_text).

#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/gloader.h>

#include <array>
#include <cstddef>
#include <cstdint>

// myscreen and theprefs are now macros defined in base.h / view.h

void input_state_from_sdl(InputState& out)
{
    out.timer_wait_request = kNoTimerWaitRequest;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        // Save previous held state to detect press edges
        std::array<bool, NUM_INPUT_KEYS> was_held;
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            was_held[static_cast<std::size_t>(k)] = out.players[p].held[k];

        // Sample current held state from SDL
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            out.players[p].held[k] = isPlayerHoldingKey(p, k);

        if (!player_allows_diagonal_movement(p))
        {
            // Four-direction mode suppresses keyboard/joystick diagonal
            // bindings, but the DOM stick is intrinsically eight-way. Keep
            // its dedicated held bits source-aware so a later controls reset
            // cannot make diagonal touch sectors silently produce no motion.
            const InputHardwareState& input_hw = input_hardware_state();
            out.players[p].held[KEY_UP_RIGHT] =
                input_hw.touch_keystate[p][KEY_UP_RIGHT];
            out.players[p].held[KEY_DOWN_RIGHT] =
                input_hw.touch_keystate[p][KEY_DOWN_RIGHT];
            out.players[p].held[KEY_DOWN_LEFT] =
                input_hw.touch_keystate[p][KEY_DOWN_LEFT];
            out.players[p].held[KEY_UP_LEFT] =
                input_hw.touch_keystate[p][KEY_UP_LEFT];
        }

        // Diagonal release retention: a sloppy two-key diagonal release
        // (one key clearing 1-3 samples before the other) keeps reporting
        // the diagonal for a short grace window so the control walker's
        // facing does not snap to the surviving cardinal. Client-side
        // shaping only — the InputState wire format and the sim are
        // untouched. See input_direction_grace.h.
        std::uint8_t raw_mask = 0;
        for (int d = KEY_UP; d <= KEY_UP_LEFT; d++)
            if (out.players[p].held[d])
                raw_mask = static_cast<std::uint8_t>(raw_mask | (1u << d));
        const std::uint8_t shaped = coalesce_direction_release(
            raw_mask, input_hardware_state().direction_grace[p]);
        if (shaped != raw_mask)
            for (int d = KEY_UP; d <= KEY_UP_LEFT; d++)
                out.players[p].held[d] = (shaped & (1u << d)) != 0;

        // Pressed = held now but wasn't held last frame. Edges are computed
        // from the shaped output so held/pressed stay mutually consistent.
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            out.players[p].pressed[k] =
                out.players[p].held[k] && !was_held[static_cast<std::size_t>(k)];
    }

    if (og::runtime::current_session != nullptr)
    {
        out.timer_wait_request =
            og::runtime::current_session->pending_timer_wait_request_;
        og::runtime::current_session->pending_timer_wait_request_ =
            kNoTimerWaitRequest;
    }
}

// Compatibility hook retained for callers that initialize platform services
// explicitly. SDL services now live in immutable provider tables and the
// active GameSession, so installation must preserve all existing state.
namespace og::runtime {
void install_sdl_context_services()
{
}
} // namespace og::runtime

void popup_dialog(const char* title, const char* message);

loader* sdl_entity_loader()
{
    static auto game_loader = [] {
        auto entity_loader = std::make_unique<loader>([] {
            EntityFactory factory;
            factory.attach_render = [](walker& w, const PixieData& data) {
                w.attach_render(data);
            };
            factory.report_error = [](const std::string& message) {
                popup_dialog("ERROR", message.c_str());
            };
            return factory;
        }());
        return entity_loader;
    }();
    return game_loader.get();
}

namespace
{
void sdl_clear_stale_view_controls(LevelRuntimeData* level)
{
    if (og::runtime::current_session &&
        og::runtime::current_session->myscreen_ != nullptr &&
        &og::runtime::current_session->myscreen_->level_runtime_data() == level)
    {
        for (auto& view : og::runtime::current_session->myscreen_->viewob)
        {
            if (view)
                view->control = nullptr;
        }
    }
}

void sdl_level_data_draw(LevelRuntimeData* level, screen* screenp)
{
    for (short i = 0; i < screenp->numviews; i++)
        screenp->viewob[i]->redraw(level, false);
}

std::unique_ptr<LevelRender> sdl_create_level_render(PixieData pixdata[])
{
    return create_sdl_level_render(pixdata);
}

EntityFactory sdl_create_entity_factory()
{
    EntityFactory factory;
    factory.attach_render = [](walker& w, const PixieData& data) {
        w.attach_render(data);
    };
    factory.report_error = [](const std::string& message) {
        popup_dialog("ERROR", message.c_str());
    };
    return factory;
}

void wire_world_with_loader(GameWorld* world, loader* game_loader)
{
    if (world == nullptr || game_loader == nullptr)
        return;

    world->entity_factory = [game_loader](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        return game_loader->create_walker_owned(order, family);
    };

    world->entity_configurator = [game_loader](walker& entity, Order order, std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };

    world->entity_derived_stats = [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr)
            return;
        game_loader->set_derived_stats(entity, order, family);
    };
}

void sdl_wire_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    wire_world_with_loader(world, sdl_entity_loader());
}

const LevelDataHooks kSdlLevelDataHooks{
    .clear_stale_view_controls = sdl_clear_stale_view_controls,
    .draw = sdl_level_data_draw,
    .create_level_render = sdl_create_level_render,
    .create_entity_factory = sdl_create_entity_factory,
    .wire_world_entity_services = sdl_wire_world_entity_services,
};
} // namespace

const LevelDataHooks& sdl_level_data_hooks()
{
    return kSdlLevelDataHooks;
}
