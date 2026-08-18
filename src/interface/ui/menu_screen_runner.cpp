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
#include <openglad/core/util.h>
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
#include <mutex>
#include <string>
#include <string_view>

// Shared picker loop helpers (defined in picker_input.cpp /
// picker_team_build.cpp; declared locally by every consumer — repo pattern).
Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue,
                     bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons,
                   Sint32& retvalue);
void sync_button_hidden_state(const button* buttons, int button_index);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);
#ifdef TESTING
std::mutex& get_allbuttons_mutex();
#endif

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

void draw_menu_highlight(const MenuScreenSpec& spec,
                         const button* buttons,
                         int highlighted_button)
{
    // The compact Base Camp seat rail packs a numbered team chip into each
    // card's last nine pixels, and the move-up column hugs the roster
    // panel's inner face. Their focus ring must stay inside the selected
    // control so it cannot paint over the chip or the panel bevel. Bounded
    // to the rail/move-up band (33..48): the appended gameplay-zone rows
    // past it take the normal exterior focus ring.
    if (std::string_view(spec.name) == "team_build" &&
        highlighted_button >= kBaseCampInteriorRingFirstIndex &&
        highlighted_button <= kBaseCampInteriorRingLastIndex)
    {
        if (highlighted_button >= kBaseCampSeatCardBase &&
            highlighted_button <
                kBaseCampSeatCardBase + kBaseCampSeatCardsPerPage)
        {
            // The numbered chip occupies the final nine pixels of a seat
            // card. Keep the pulsing ring around the label face so the
            // highlight never erases the chip's border or glyph.
            button label_face = buttons[highlighted_button];
            label_face.sizex -= 10;
            draw_highlight_interior(label_face);
        }
        else
        {
            draw_highlight_interior(buttons[highlighted_button]);
        }
        return;
    }
    draw_highlight(buttons[highlighted_button]);
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

// The materialized-index view of the spec rows (build-gate filtered): every
// per-row pass below indexes THROUGH this so buttons[i] pairs with the spec
// row it was materialized from.
using SpecRowView = std::vector<const MenuButtonSpec*>;

// Re-apply pixie art faces. Must run AFTER init_buttons (and after every
// reset_buttons): set_graphic overwrites the vbutton's w/h from the loaded
// pixie (button.cpp), so a reset without a re-apply drops the face.
void apply_art_bindings(const SpecRowView& rows, int num_buttons)
{
    for (int i = 0; i < num_buttons && i < static_cast<int>(rows.size()); ++i) {
        const MenuButtonSpec& row = *rows[static_cast<std::size_t>(i)];
        if (row.art_family < 0)
            continue;
        vbutton* live = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
        if (live != nullptr)
            live->set_graphic(static_cast<char>(row.art_family));
    }
}

// Gate pass (§1.5): evaluate each row's state, write hidden to BOTH
// surfaces, and make Disabled rows inert on both activation paths (leftclick
// and handle_menu_nav both gate on a nonzero myfun/myfunc) while keeping
// them visible for nav, with a dimmed face.
void apply_row_states(const SpecRowView& rows, button* buttons,
                      int num_buttons, const MenuLabelContext& context,
                      RowState* states)
{
    for (int i = 0; i < num_buttons && i < static_cast<int>(rows.size()); ++i) {
        const MenuButtonSpec& row = *rows[static_cast<std::size_t>(i)];
        const RowState state = row.state_override != nullptr
            ? row.state_override(context)
            : gate_state(row.gate, context);
        states[i] = state;
        buttons[i].hidden = (state == RowState::Hidden);
        sync_button_hidden_state(buttons, i);
        vbutton* live = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
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
void apply_label_bindings(const SpecRowView& rows, button* buttons,
                          int num_buttons, const MenuLabelContext& context)
{
    for (int i = 0; i < num_buttons && i < static_cast<int>(rows.size()); ++i) {
        const MenuButtonSpec& row = *rows[static_cast<std::size_t>(i)];
        vbutton* live = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
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

// The build this binary materializes for by default.
constexpr MenuBuildVariant kCompiledBuildVariant =
#ifdef __EMSCRIPTEN__
    MenuBuildVariant::Web;
#else
    MenuBuildVariant::Native;
#endif

// One-shot: set by the post-game teardown, consumed by the next screen.
bool g_suppress_entry_fade_out = false;

bool spec_row_survives_build(const MenuButtonSpec& row, MenuBuildVariant variant)
{
    switch (row.build) {
    case MenuBuildGate::NativeOnly:
        return variant == MenuBuildVariant::Native;
    case MenuBuildGate::WebOnly:
        return variant == MenuBuildVariant::Web;
    case MenuBuildGate::Always:
        break;
    }
    return true;
}

} // namespace

void suppress_next_menu_entry_fade_out()
{
    g_suppress_entry_fade_out = true;
}

bool consume_suppressed_menu_entry_fade_out()
{
    const bool suppressed = g_suppress_entry_fade_out;
    g_suppress_entry_fade_out = false;
    return suppressed;
}

#ifdef TESTING
void picker_testing_draw_menu_highlight(const MenuScreenSpec& spec,
                                        const button* buttons,
                                        int highlighted_button)
{
    draw_menu_highlight(spec, buttons, highlighted_button);
}
#endif

std::vector<const MenuButtonSpec*> materialized_spec_rows_for(
    const MenuScreenSpec& spec, MenuBuildVariant variant)
{
    std::vector<const MenuButtonSpec*> rows;
    rows.reserve(static_cast<std::size_t>(spec.row_count));
    for (int i = 0; i < spec.row_count; ++i) {
        if (spec_row_survives_build(spec.rows[i], variant))
            rows.push_back(&spec.rows[i]);
    }
    return rows;
}

std::vector<const MenuButtonSpec*> materialized_spec_rows(
    const MenuScreenSpec& spec)
{
    return materialized_spec_rows_for(spec, kCompiledBuildVariant);
}

void materialize_menu_buttons_for(const MenuScreenSpec& spec,
                                  MenuBuildVariant variant,
                                  std::vector<button>& out)
{
    out.clear();
    out.reserve(static_cast<std::size_t>(spec.row_count));
    for (const MenuButtonSpec* row_ptr : materialized_spec_rows_for(spec, variant)) {
        const MenuButtonSpec& row = *row_ptr;
        button materialized(row.id, row.label, row.hotkey, row.x, row.y, row.w,
                            row.h, button_action_id(row.action), row.arg,
                            row.nav, row.hidden);
        materialized.no_draw = row.no_draw;
        out.push_back(materialized);
    }
    OG_MENU_ENGINE_CHECK(out.size() <= static_cast<std::size_t>(MAX_BUTTONS),
                         "materialized row count exceeds MAX_BUTTONS");
}

void materialize_menu_buttons(const MenuScreenSpec& spec,
                              std::vector<button>& out)
{
    materialize_menu_buttons_for(spec, kCompiledBuildVariant, out);
}

Sint32 run_menu_screen(const MenuScreenSpec& spec, void* screen_state)
{
    // Consumed unconditionally: the flag is a hand-off from the screen that
    // ran last, so whichever screen opens next must clear it even when it
    // does not fade around its entry.
    const bool skip_entry_fade_out = consume_suppressed_menu_entry_fade_out();
    // Sequence the D3 accessors: buttons() fills the vector count() reads.
    button* buttons = spec.buttons_accessor();
    const int num_buttons = spec.count_accessor();
    OG_MENU_ENGINE_CHECK(buttons != nullptr && num_buttons > 0,
                         "engine screen materialized no buttons");
    OG_MENU_ENGINE_CHECK(num_buttons <= MAX_BUTTONS,
                         "engine screen exceeds MAX_BUTTONS");
    // The materialized spec-row view: buttons[i] <-> *spec_rows[i]. Raw
    // spec ordinals diverge from materialized indices past a build-gated row
    // (the main-menu enabled/disabled-QUIT fork), so every per-row pass uses this.
    const SpecRowView spec_rows = materialized_spec_rows(spec);
    OG_MENU_ENGINE_CHECK(static_cast<int>(spec_rows.size()) == num_buttons,
                         "accessor row count diverges from the spec's "
                         "build-gate materialization");
    // Entry-time descriptor fix-ups (the legacy pre-init mutations), once,
    // before the live vbuttons are created from the descriptor rows.
    if (spec.prepare_buttons != nullptr)
        spec.prepare_buttons(buttons, num_buttons, screen_state);
    int highlighted_button = spec.default_highlight;
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
    clear_keyboard();
    apply_art_bindings(spec_rows, num_buttons);

    RowState states[MAX_BUTTONS] = {};
    {
        // Initial visibility + nav, before the first frame (the legacy
        // pre-loop sync_* call). Publish the generic gate pass and the
        // screen-specific rewire atomically to injector-thread observers.
        const MenuLabelContext context = build_label_context(spec);
#ifdef TESTING
        std::lock_guard<std::mutex> lock(get_allbuttons_mutex());
#endif
        apply_row_states(spec_rows, buttons, num_buttons, context, states);
        apply_nav_program(spec, buttons, num_buttons, highlighted_button);
    }

    if (spec.enter == EnterTransition::FadeAroundEntry) {
        // The legacy team-build entry, verbatim: fade the previous screen
        // out, compose the cold first frame, and fade it in (fadeblack
        // presents the buffer itself; the first highlighted frame follows in
        // the loop, after the level-reload guard). Content must be present in
        // this compose: Base Camp's roster ink is a content hook while its
        // deploy X is a button, and splitting them across the fade makes the
        // control pop in as the first full frame replaces the partial one.
        screen* scr = og::runtime::current_session->myscreen_;
        // ...unless the post-game teardown already faded to black (#200): the
        // fade-out would run over the stale menu image the UI canvas still
        // holds and play the same transition a second time.
        if (!skip_entry_fade_out)
            scr->fadeblack(0);
        if (spec.backdrop)
            draw_backdrop();
        if (spec.draw_background != nullptr)
            spec.draw_background(screen_state);
        draw_buttons(buttons, num_buttons);
        if (spec.draw_content != nullptr)
            spec.draw_content(screen_state);
        scr->fadeblack(1);
    }

    if (spec.enter == EnterTransition::FadeWithInitialDraw) {
        // The legacy mainmenu entry, verbatim: settle one timer tick, fade
        // the previous menu out, compose the first frame in the buffer, and
        // fade it in (fadeblack presents the buffer itself — no
        // buffer_to_screen here). Injector SDL_Delay(750) cadence depends on
        // this exact sequence.
        reset_timer();
        while (query_timer() < 1)
            ;
        screen* scr = og::runtime::current_session->myscreen_;
        scr->fadeblack(0);
        {
            const MenuLabelContext context = build_label_context(spec);
            apply_label_bindings(spec_rows, buttons, num_buttons, context);
        }
        if (spec.backdrop)
            draw_backdrop();
        if (spec.draw_background != nullptr)
            spec.draw_background(screen_state);
        draw_buttons(buttons, num_buttons);
        if (spec.draw_content != nullptr)
            spec.draw_content(screen_state);
        draw_menu_highlight(spec, buttons, highlighted_button);
        // Zardus: PORT: fade from black
        scr->fadeblack(1);
        grab_mouse();
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
        {
#ifdef TESTING
            // Rewire screens deliberately start some rows hidden, while the
            // generic Always gate briefly marks them visible before their
            // rewire restores the final frame state. Injector threads must
            // never observe that incomplete publication.
            std::lock_guard<std::mutex> lock(get_allbuttons_mutex());
#endif
            apply_row_states(spec_rows, buttons, num_buttons, context, states);
            apply_nav_program(spec, buttons, num_buttons, highlighted_button);
        }

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

        // Click dispatch, both legacy shapes: screens WITH right-click
        // support branch on the exact 2 (the hire/train `clickvalue`
        // pattern); every other legacy loop is `if (leftmouse(buttons))` —
        // ANY nonzero click (right included) activates leftclick.
        const Sint32 click = leftmouse(buttons);
        if (click == 2 && spec.right_click_enabled) {
            const Sint32 click_result =
                og::runtime::current_session->localbuttons_->rightclick();
            if (click_result == MENU_EXIT)
                break;
            if (click_result != 0)
                retvalue = click_result;
        } else if (click != 0) {
#ifdef TESTING
            // §1.5: a click landing on a Disabled row no-ops, with a TRACE.
            for (int i = 0; i < num_buttons; ++i) {
                vbutton* live = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)];
                if (states[i] == RowState::Disabled && !buttons[i].hidden
                    && live != nullptr && live->mouse_on()) {
                    TRACE("menu_engine", "disabled_row_click %s",
                          buttons[i].id.c_str());
                }
            }
#endif
            const Sint32 click_result =
                og::runtime::current_session->localbuttons_->leftclick();
            if (click_result == MENU_EXIT) {
                // A blocking child can discover a remote start and return its
                // structural exit through this activation. Do not fold that
                // into the parent's ordinary local-BACK exit_value.
                Sint32 nested_remote_start = 0;
                if (remote_start_requested(spec, nested_remote_start))
                    return nested_remote_start;
                break;
            }
            if (click_result != 0)
                retvalue = click_result;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        if (retvalue == MENU_EXIT) {
            // Same child-return path as mouse activation, for Enter/hotkeys.
            Sint32 nested_remote_start = 0;
            if (remote_start_requested(spec, nested_remote_start))
                return nested_remote_start;
            break;
        }

        // Subscreens whose BACK carries MENU_REDRAW (view team / MATCHUP /
        // the slot menus) END here — the same point the legacy loops
        // checked, before reset_buttons could consume the value — and
        // return MENU_REDRAW to the parent loop. MENU_EXIT-bearing exits
        // (remote start, an intercepted GO) still return spec.exit_value /
        // the remote MENU_EXIT through their own paths above.
        if (spec.exit_on_redraw && (retvalue & MENU_REDRAW))
            return MENU_REDRAW;

        // Screen-owned click consumption (the VIEW LEVEL page-step stash) —
        // at the exact legacy point: after the redraw-exit test, before
        // reset_buttons could consume the retvalue.
        if (spec.consume_click != nullptr)
            retvalue = spec.consume_click(retvalue, screen_state);

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
            apply_art_bindings(spec_rows, num_buttons);
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
        apply_label_bindings(spec_rows, buttons, num_buttons, label_context);

        if (spec.backdrop)
            draw_backdrop();
        if (spec.draw_background != nullptr)
            spec.draw_background(screen_state);
        draw_buttons(buttons, num_buttons);
        if (spec.draw_content != nullptr)
            spec.draw_content(screen_state);
        draw_menu_highlight(spec, buttons, highlighted_button);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        // The ONE asyncify yield per iteration (emscripten contract).
        og::input_native::sleep_ms(10);
    }

    return spec.exit_value;
}

} // namespace og::ui
