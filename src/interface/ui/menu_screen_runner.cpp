/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* MenuScreen Runtime — the single frame skeleton (docs/menu-engine.md,
 * design §1.4). Every engine-hosted picker screen runs this loop; the
 * per-screen differences live in its MenuScreenSpec (rows, bindings, nav
 * program, obligations, hooks).
 *
 * The loop shape is transcribed from the canonical legacy loops
 * (run_difficulty_menu / run_fx_options_screen / create_team_menu): inner
 * breaks fire only on an exact MENU_EXIT, composite values exit at the
 * loop-top bit test after one final draw, and reset_buttons consumes
 * MENU_OK/MENU_REDRAW by re-initializing the live vbuttons.
 */

#include <openglad/interface/ui/menu_screen_spec.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>

#include "picker_sdl_defs.h"

#include <cstdio>
#include <cstdlib>
#include <string>

// Shared picker loop helpers (defined in picker_input.cpp /
// picker_team_build.cpp; declared locally by every consumer — repo pattern).
Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue,
                     bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons,
                   Sint32& retvalue);
void sync_button_hidden_state(const button* buttons, int button_index);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);

// Engine invariants are enforced in TESTING builds regardless of NDEBUG
// (assert() would vanish in release-flavored test configs).
#ifdef TESTING
#define OG_MENU_ENGINE_CHECK(cond, msg)                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "menu_engine invariant failed: %s\n", (msg)); \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
#else
#define OG_MENU_ENGINE_CHECK(cond, msg) \
    do {                                \
    } while (0)
#endif

namespace og::ui {

namespace {

inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

MenuLabelContext build_label_context(const MenuScreenSpec& spec)
{
    MenuLabelContext context;
    screen* scr = og::runtime::current_session->myscreen_;
    context.save = &scr->save_data;
    context.session_difficulty =
        og::runtime::current_session->current_difficulty_;
    context.is_networked = picker_lobby_is_networked();
    context.is_host = picker_lobby_host_controls_visible();
    context.spectator = scr->save_data.numplayers == 0;
    context.campaign = scr->save_data.current_campaign;
    context.level = scr->save_data.scen_num;
    if (spec.build_context != nullptr)
        spec.build_context(context);
    return context;
}

// Re-apply pixie art faces. Must run AFTER init_buttons (and after every
// reset_buttons): set_graphic overwrites the vbutton's w/h from the loaded
// pixie (button.cpp), so a reset without a re-apply drops the face.
void apply_art_bindings(const MenuScreenSpec& spec, int num_buttons)
{
    for (int i = 0; i < num_buttons && i < spec.row_count; ++i) {
        const MenuButtonSpec& row = spec.rows[i];
        if (row.art_family < 0)
            continue;
        vbutton* live = og::runtime::current_session->allbuttons_[i];
        if (live != nullptr)
            live->set_graphic(static_cast<char>(row.art_family));
    }
}

// Gate pass (§1.5): evaluate each row's state, write hidden to BOTH
// surfaces, and make Disabled rows inert on both activation paths (leftclick
// and handle_menu_nav both gate on a nonzero myfun/myfunc) while keeping
// them visible for nav, with a dimmed face.
void apply_row_states(const MenuScreenSpec& spec, button* buttons,
                      int num_buttons, const MenuLabelContext& context,
                      RowState* states)
{
    for (int i = 0; i < num_buttons && i < spec.row_count; ++i) {
        const MenuButtonSpec& row = spec.rows[i];
        const RowState state = row.state_override != nullptr
            ? row.state_override(context)
            : gate_state(row.gate, context);
        states[i] = state;
        buttons[i].hidden = (state == RowState::Hidden);
        sync_button_hidden_state(buttons, i);
        vbutton* live = og::runtime::current_session->allbuttons_[i];
        if (state == RowState::Disabled) {
            buttons[i].myfun = 0;
            if (live != nullptr) {
                live->myfunc = 0;
                live->color = GREY;  // draw dimmed
            }
        } else {
            const Sint32 fun = button_action_id(row.action);
            buttons[i].myfun = fun;
            if (live != nullptr) {
                live->myfunc = fun;
                if (row.color == nullptr)
                    live->color = BUTTON_FACING;
            }
        }
    }
}

void apply_nav_program(const MenuScreenSpec& spec, button* buttons,
                       int num_buttons, int& highlighted_button)
{
    switch (spec.nav.kind) {
    case NavProgramKind::Rewire:
        // G1: the legacy hand-wired rewire function, verbatim.
        if (spec.nav.rewire != nullptr)
            spec.nav.rewire(buttons, num_buttons, highlighted_button);
        break;
    case NavProgramKind::RouteAround:
        // Reserved (G1 proof-gated); no screen may declare it yet.
        OG_MENU_ENGINE_CHECK(false, "NavProgramKind::RouteAround not implemented");
        break;
    case NavProgramKind::Static:
        break;
    }
    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);

#ifdef TESTING
    // Engine invariant (§1.5): after nav application no visible button's nav
    // links target a hidden one (Disabled counts as visible).
    for (int i = 0; i < num_buttons; ++i) {
        if (buttons[i].hidden)
            continue;
        const int links[4] = {buttons[i].nav.up, buttons[i].nav.down,
                              buttons[i].nav.left, buttons[i].nav.right};
        for (int link : links) {
            OG_MENU_ENGINE_CHECK(
                link < num_buttons,
                "nav link out of range on an engine screen");
            OG_MENU_ENGINE_CHECK(
                link < 0 || !buttons[link].hidden,
                "visible button nav-links to a hidden button");
        }
    }
#endif
}

// Remote-start preemption (§1.4). Returns true when the loop must end;
// `retvalue` carries the remote MENU_EXIT.
bool remote_start_requested(const MenuScreenSpec& spec, Sint32& retvalue)
{
    switch (spec.remote_start) {
    case RemoteStartScope::MainScope:
        return picker_main_scope_remote_start_requested(retvalue);
    case RemoteStartScope::TeamBuildScope:
        return team_build_remote_start_requested(retvalue);
    case RemoteStartScope::None:
        break;
    }
    return false;
}

// Label-sync pass (§1.4 LABELS): every bound row is re-derived from the
// context and written to BOTH surfaces — the mutable descriptor row (backs
// later redraws/resets) and the live vbutton (what draw_buttons shows).
// Outline and color bindings ride the same pass.
void apply_label_bindings(const MenuScreenSpec& spec, button* buttons,
                          int num_buttons, const MenuLabelContext& context)
{
    for (int i = 0; i < num_buttons && i < spec.row_count; ++i) {
        const MenuButtonSpec& row = spec.rows[i];
        vbutton* live = og::runtime::current_session->allbuttons_[i];
        if (row.label_binding.formatter != nullptr) {
            const std::string label = row.label_binding.formatter(context);
            buttons[i].label = label;
            if (live != nullptr)
                live->label = label;
        }
        if (row.outline == MenuOutlineBinding::PlayerCountEquals
            && live != nullptr && context.save != nullptr) {
            live->do_outline =
                (context.save->numplayers == row.outline_arg) ? 1 : 0;
        }
        if (row.color != nullptr && live != nullptr)
            live->color = row.color(context);
    }
}

} // namespace

void materialize_menu_buttons(const MenuScreenSpec& spec,
                              std::vector<button>& out)
{
    out.clear();
    out.reserve(static_cast<std::size_t>(spec.row_count));
    for (int i = 0; i < spec.row_count; ++i) {
        const MenuButtonSpec& row = spec.rows[i];
#ifdef __EMSCRIPTEN__
        if (row.build == MenuBuildGate::NativeOnly)
            continue;
#else
        if (row.build == MenuBuildGate::WebOnly)
            continue;
#endif
        button materialized(row.id, row.label, row.hotkey, row.x, row.y, row.w,
                            row.h, button_action_id(row.action), row.arg,
                            row.nav);
        materialized.no_draw = row.no_draw;
        out.push_back(materialized);
    }
    OG_MENU_ENGINE_CHECK(out.size() <= static_cast<std::size_t>(MAX_BUTTONS),
                         "materialized row count exceeds MAX_BUTTONS");
}

Sint32 run_menu_screen(const MenuScreenSpec& spec, void* screen_state)
{
    // Sequence the D3 accessors: buttons() fills the vector count() reads.
    button* buttons = spec.buttons_accessor();
    const int num_buttons = spec.count_accessor();
    OG_MENU_ENGINE_CHECK(buttons != nullptr && num_buttons > 0,
                         "engine screen materialized no buttons");
    OG_MENU_ENGINE_CHECK(num_buttons <= MAX_BUTTONS,
                         "engine screen exceeds MAX_BUTTONS");
    // Entry transitions land with the screens that use them (main menu);
    // every migrated screen so far enters cold, exactly like its legacy loop.
    OG_MENU_ENGINE_CHECK(spec.enter == EnterTransition::None,
                         "EnterTransition not implemented yet");

    int highlighted_button = spec.default_highlight;
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
    clear_keyboard();
    apply_art_bindings(spec, num_buttons);

    RowState states[MAX_BUTTONS] = {};
    {
        // Initial visibility + nav, before the first frame (the legacy
        // pre-loop sync_* call).
        const MenuLabelContext context = build_label_context(spec);
        apply_row_states(spec, buttons, num_buttons, context, states);
        apply_nav_program(spec, buttons, num_buttons, highlighted_button);
    }

    int frame = 0;
    Sint32 retvalue = 0;
    while (!(retvalue & MENU_EXIT)) {
        if (spec.polls_lobby)
            picker_lobby_poll();

        // Per-frame gating: host state can flip mid-screen (connection loss,
        // lobby changes). Both surfaces, then the nav program, then the
        // highlight pull.
        const MenuLabelContext context = build_label_context(spec);
        apply_row_states(spec, buttons, num_buttons, context, states);
        apply_nav_program(spec, buttons, num_buttons, highlighted_button);

        // Remote-start preemption: a host GO must launch a peer parked in
        // this screen. Subscreens RETURN the remote MENU_EXIT (with CONTINUE
        // selected) so the whole menu stack unwinds; the main menu instead
        // breaks and lets present_menu act on pks().selected_menu_item.
        Sint32 remote_start = 0;
        if (remote_start_requested(spec, remote_start)) {
            if (spec.remote_start_exit == RemoteStartExit::ReturnMenuExit)
                return remote_start;
            retvalue = remote_start;
            break;
        }

        const Sint32 click = leftmouse(buttons);
        if (click == 1) {
#ifdef TESTING
            // §1.5: a click landing on a Disabled row no-ops, with a TRACE.
            for (int i = 0; i < num_buttons; ++i) {
                vbutton* live = og::runtime::current_session->allbuttons_[i];
                if (states[i] == RowState::Disabled && !buttons[i].hidden
                    && live != nullptr && live->mouse_on()) {
                    TRACE("menu_engine", "disabled_row_click %s",
                          buttons[i].id.c_str());
                }
            }
#endif
            const Sint32 click_result =
                og::runtime::current_session->localbuttons_->leftclick();
            if (click_result == MENU_EXIT)
                break;
            if (click_result != 0)
                retvalue = click_result;
        } else if (click == 2 && spec.right_click_enabled) {
            const Sint32 click_result =
                og::runtime::current_session->localbuttons_->rightclick();
            if (click_result == MENU_EXIT)
                break;
            if (click_result != 0)
                retvalue = click_result;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        if (retvalue == MENU_EXIT)
            break;

        // G3 generic row dispatch: ButtonAction::MenuSpecRow stashed which
        // materialized row was activated; retvalue only carried the action
        // id (101). 101 & MENU_EXIT != 0, so the stash MUST be consumed and
        // retvalue rewritten before the loop-condition test (the networking
        // loop's retvalue-zero discipline).
        if (pks().menu_spec_clicked_row >= 0) {
            const int clicked_row = pks().menu_spec_clicked_row;
            pks().menu_spec_clicked_row = -1;
            retvalue = spec.on_spec_row != nullptr
                ? spec.on_spec_row(clicked_row, screen_state)
                : 0;
            // G12 single-point obligation for lobby-backed engine rows.
            if (spec.sync_settings_after_mutation)
                picker_lobby_sync_settings_from_save();
            // Structural exit from a spec row propagates MENU_EXIT itself
            // (a local BACK's break returns spec.exit_value instead).
            if (retvalue == MENU_EXIT)
                return MENU_EXIT;
        }
        OG_MENU_ENGINE_CHECK(pks().menu_spec_clicked_row < 0,
                             "MenuSpecRow stash was not consumed");

        const bool was_reset = reset_buttons(
            og::runtime::current_session->localbuttons_, buttons, num_buttons,
            retvalue);
        if (was_reset) {
            apply_art_bindings(spec, num_buttons);
            if (spec.on_reset != nullptr)
                spec.on_reset(screen_state);
        }

        ++frame;
        if (spec.frame_tick != nullptr && !spec.frame_tick(screen_state, frame))
            break;

        // Labels re-derive from a FRESH context: a click this frame (or a
        // lobby poll rewriting the save under the open screen) must show on
        // this frame's draw, exactly like the legacy per-frame re-derives.
        const MenuLabelContext label_context = build_label_context(spec);
        apply_label_bindings(spec, buttons, num_buttons, label_context);

        if (spec.backdrop)
            draw_backdrop();
        if (spec.draw_background != nullptr)
            spec.draw_background(screen_state);
        draw_buttons(buttons, num_buttons);
        if (spec.draw_content != nullptr)
            spec.draw_content(screen_state);
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        // The ONE asyncify yield per iteration (emscripten contract).
        og::input_native::sleep_ms(10);
    }

    return spec.exit_value;
}

} // namespace og::ui
