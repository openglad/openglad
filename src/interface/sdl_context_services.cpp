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
#include <openglad/gameplay/belt_input.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/test_trace.h>

#include <array>
#include <cstddef>
#include <cstdint>

// myscreen and theprefs are now macros defined in base.h / view.h

namespace
{

// New Ability Select: the SPEC keys' raw history. The wire's held bits are
// the previous sample of every other key, but the Special bit is the FOLD's
// output (a SPEC 2 hold sets it), so the edges of SPEC 1/2/3 need the raw
// keys of the previous sample. `sent` is the PlayerInput this sampler wrote
// with them; when the InputState handed in is not that one (a fresh state, a
// test context swap), the history is stale and the edges come from the bits
// handed in, which is what the sampler did before the belt.
struct SpecKeyHistory {
    bool raw[3] = {false, false, false};
    PlayerInput sent{};
    bool valid = false;
};

std::array<SpecKeyHistory, MAX_PLAYERS>& spec_key_history()
{
    static std::array<SpecKeyHistory, MAX_PLAYERS> history{};
    return history;
}

bool same_input(const PlayerInput& a, const PlayerInput& b)
{
    for (int k = 0; k < NUM_INPUT_KEYS; ++k)
    {
        if (a.held[k] != b.held[k] || a.pressed[k] != b.pressed[k])
            return false;
    }
    return a.belt_cast == b.belt_cast;
}

// The walker local seat `seat` drives (its view's control, the mirror walker
// in local and networked play), resolved against its guy's belt, or nullptr
// when the seat drives none (a menu, a dead or empty seat).
const og::sim::belt::Layout* seat_belt(int seat,
                                       og::sim::belt::Layout& storage)
{
    og::runtime::SessionState* const session = og::runtime::current_session;
    if (session == nullptr || session->myscreen_ == nullptr)
        return nullptr;
    screen& view_screen = *session->myscreen_;
    if (seat >= view_screen.numviews || !view_screen.viewob[seat])
        return nullptr;
    const walker* const control = view_screen.viewob[seat]->control;
    if (control == nullptr || control->dead() || control->stats() == nullptr)
        return nullptr;
    const FamilyDescriptor* const descriptor = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(control->family())));
    storage = og::sim::belt::resolve(descriptor, control->stats()->level(),
                                     view_screen.world().new_specials,
                                     og::sim::belt::bytes_for(*control));
    return &storage;
}

// SPEC 1/2/3 + SHIFTER through the belt fold (og::sim::belt::fold_keys):
// writes the Special and SwitchSpecial bits and belt_cast of `now`. `before`
// is the PlayerInput the caller handed in, before this sample.
void fold_spec_keys(int seat, const PlayerInput& before, PlayerInput& now)
{
    SpecKeyHistory& history = spec_key_history()[static_cast<std::size_t>(seat)];
    bool was_raw[3];
    if (history.valid && same_input(history.sent, before))
    {
        for (int i = 0; i < 3; ++i)
            was_raw[i] = history.raw[i];
    }
    else
    {
        was_raw[0] = before.is_held(InputAction::Special);
        was_raw[1] = before.is_held(InputAction::SpecialSwitch);
        was_raw[2] = false;
    }

    static constexpr int kSpecKeys[3] = {KEY_SPECIAL, KEY_SPEC2, KEY_SPEC3};
    og::sim::belt::Keys keys;
    for (int i = 0; i < 3; ++i)
    {
        keys.held[i] = isPlayerHoldingKey(seat, kSpecKeys[i]);
        keys.pressed[i] = keys.held[i] && !was_raw[i];
        history.raw[i] = keys.held[i];
    }
    keys.shift_held = isPlayerHoldingKey(seat, KEY_SHIFTER);

    // The layout matters only while a SPEC key is down: with none held,
    // fold_keys answers the same with or without a walker (no bits, the
    // latch cleared), so the walker is not looked up on idle samples.
    const bool ability_select = og::input::ability_select_on();
    og::sim::belt::Layout storage;
    const og::sim::belt::Layout* belt = nullptr;
    if (ability_select && (keys.held[0] || keys.held[1] || keys.held[2]))
        belt = seat_belt(seat, storage);

    const og::sim::belt::Fold fold = og::sim::belt::fold_keys(
        keys, ability_select, belt,
        input_hardware_state().belt_latch[seat]);
    now.held[static_cast<int>(InputAction::Special)] = fold.special_held;
    now.pressed[static_cast<int>(InputAction::Special)] = fold.special_pressed;
    now.held[static_cast<int>(InputAction::SpecialSwitch)] = fold.switch_held;
    now.pressed[static_cast<int>(InputAction::SpecialSwitch)] =
        fold.switch_pressed;
    now.belt_cast = fold.belt_cast;
    if (fold.special_pressed && fold.belt_cast != 0)
        TRACE("belt", "seat %d sends belt_cast=0x%02x", seat,
              static_cast<unsigned>(fold.belt_cast));

    history.sent = now;
    history.valid = true;
}

} // namespace

void input_state_from_sdl(InputState& out)
{
    out.timer_wait_request = kNoTimerWaitRequest;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        const PlayerInput before = out.players[p];
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

        // Two client-side direction shapers; the InputState wire format and
        // the sim are untouched. See input_direction_grace.h for both
        // contracts.
        //
        // 1. Opposite-direction re-assert: a fresh press must beat a STALE
        //    opposite hold. A browser-swallowed keyup (iPad Safari around
        //    system gestures) otherwise latches the old direction forever
        //    and every real opposite press nets the axis to zero. On
        //    platforms with unreliable keyups the suppression is
        //    persistent (cancel mode) with event-driven re-assert.
        // 2. Diagonal release retention: a sloppy two-key diagonal release
        //    (one key clearing 1-3 samples before the other) keeps
        //    reporting the diagonal for a short grace window so the control
        //    walker's facing does not snap to the surviving cardinal.
        std::uint8_t raw_mask = 0;
        for (int d = KEY_UP; d <= KEY_UP_LEFT; d++)
            if (out.players[p].held[d])
                raw_mask = static_cast<std::uint8_t>(raw_mask | (1u << d));
        const std::uint8_t resolved = resolve_opposing_directions(
            raw_mask, input_hardware_state().direction_conflict[p],
            input_hardware_state().unreliable_keyups);
        const std::uint8_t shaped = coalesce_direction_release(
            resolved, input_hardware_state().direction_grace[p]);
        if (shaped != raw_mask)
            for (int d = KEY_UP; d <= KEY_UP_LEFT; d++)
                out.players[p].held[d] = (shaped & (1u << d)) != 0;

        // Pressed = held now but wasn't held last frame. Edges are computed
        // from the shaped output so held/pressed stay mutually consistent.
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            out.players[p].pressed[k] =
                out.players[p].held[k] && !was_held[static_cast<std::size_t>(k)];

        // New Ability Select: SPEC 1/2/3 (slots 9, 17, 18) + SHIFTER become
        // the Special and SwitchSpecial bits and the belt byte. Slot 11 (the
        // retired change-special slot) is never bound; the fold owns its bit.
        fold_spec_keys(p, before, out.players[p]);
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
	ScopedCanvasTarget world_canvas(*screenp, CanvasTarget::World);
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
    // #162 chokepoint: wiring runs at every SDL LevelRuntimeData
    // construction AND load(), so every scratch/preview level build (the
    // picker's VIEW LEVEL loads included) gets a loader that
    // matches the currently mounted campaign — the modes campaign ships
    // pack families (flag/waypoint on wire 13/14) whose sprites only exist
    // after its mount. Safe here: the walkers of THIS world are created
    // after the wiring with fresh pixie borrows, and every other world
    // reloads through its own enumerated flow site (or the gameplay-entry
    // safety net) before its next draw.
    //
    // CAUTION — reload_graphics() FREES the loader buffers that live pixies
    // hold raw pointers into (pixieN caches facings/bmp), so anything already
    // on screen must be re-created before the next draw. On a live menu that
    // rests on TWO independent facts, both verified on this branch:
    //
    //   1. Handler-driven builds (VIEW LEVEL, SET LEVEL, pick campaign, the
    //      new-game setup reload) return MENU_OK/MENU_REDRAW, so the frame
    //      ends in reset_buttons() re-creating every button pixie
    //      (menu_screen_runner.cpp, picker_input.cpp).
    //   2. spec.frame_tick runs AFTER reset_buttons and BEFORE draw_buttons,
    //      with no re-init in between — and SCENARIO and Base Camp both
    //      load a level from exactly there (they poll the lobby, whose
    //      campaign sync deliberately leaves the loader stale, #162). That is
    //      safe ONLY because neither of those specs carries an art_family
    //      row: their buttons are text+rect and borrow no loader pixels. The
    //      only art_family rows in the tree are the main menu's NEW GAME and
    //      TRAIN's +/- buttons, and neither screen loads a level.
    //
    // So: giving SCENARIO / Base Camp an art_family row, or giving
    // the main menu or TRAIN a level-loading frame_tick (a preview on a live
    // menu), re-opens the #162 use-after-free with no other code change. A
    // new flow of either shape must re-init its buttons after the wiring.
    //
    // Staged lobby (#218): MatchStage::maintain() restages inside
    // picker_lobby_poll on EVERY polls_lobby screen — TRAIN included, which
    // owns art_family rows — and stays safe because staged/mirror loads are
    // HEADLESS-hooked only (they never reach this free-er). The only
    // SDL-hooked load stays inside VIEW LEVEL's own handler/frame_tick
    // (facts 1 + 2 above). Keep it that way: a MatchStage that ever loads
    // through the SDL hooks re-opens #162 on TRAIN with no other change.
    sdl_entity_loader()->reload_graphics_if_stale();
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
