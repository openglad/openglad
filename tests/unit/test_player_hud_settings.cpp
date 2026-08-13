// §7.1 pure helpers: the SDL-free per-player HUD/zoom cfg persistence
// (input_state.cpp), the per-view zoom budget math and step scale (view.h),
// and the shared binding-panel line formatter (pause_menu.cpp). Everything
// here runs against a LOCAL cfg_store — never the process-global cfg.

#include <gtest/gtest.h>

#include <openglad/core/scale_mode.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/resources/gparser.h>

#include "../../src/interface/ui/picker_sdl_defs.h"

#include <string>

TEST(PlayerHudSettings, cfg_round_trip_with_local_store)
{
    cfg_store local;
    PlayerHudSettings in;
    in.radar = 0;
    in.life_on = 0;
    in.foes = 1;
    in.score = 0;
    in.zoom_step = 3;
    save_player_hud_settings_to_cfg(local, 2, in);

    PlayerHudSettings out;
    ASSERT_TRUE(load_player_hud_settings_from_cfg(local, 2, out))
        << "the save stamps the migration marker";
    EXPECT_EQ(0, out.radar);
    EXPECT_EQ(0, out.life_on);
    EXPECT_EQ(1, out.foes);
    EXPECT_EQ(0, out.score);
    EXPECT_EQ(3, out.zoom_step);

    // The keys are per player: player 1's marker is still absent, and the
    // absent-key load reports the defaults.
    PlayerHudSettings other;
    EXPECT_FALSE(load_player_hud_settings_from_cfg(local, 0, other));
    EXPECT_EQ(1, other.radar);
    EXPECT_EQ(1, other.life_on);
    EXPECT_EQ(1, other.foes);
    EXPECT_EQ(1, other.score);
    EXPECT_EQ(0, other.zoom_step);

    // The exact key names are the §7.1 contract.
    EXPECT_EQ("0", local.get_setting("controls", "player3_hud_radar"));
    EXPECT_EQ("0", local.get_setting("controls", "player3_hud_life"));
    EXPECT_EQ("1", local.get_setting("controls", "player3_hud_foes"));
    EXPECT_EQ("0", local.get_setting("controls", "player3_hud_score"));
    EXPECT_EQ("3", local.get_setting("controls", "player3_view_zoom"));
    EXPECT_EQ("1", local.get_setting("controls", "player3_hud_migrated"));
}

TEST(PlayerHudSettings, load_sanitizes_and_clamps_values)
{
    cfg_store local;
    local.apply_setting("controls", "player1_hud_migrated", "1");
    local.apply_setting("controls", "player1_hud_radar", "7");
    local.apply_setting("controls", "player1_hud_life", "abc");
    local.apply_setting("controls", "player1_hud_foes", "0");
    local.apply_setting("controls", "player1_view_zoom", "9");

    PlayerHudSettings out;
    ASSERT_TRUE(load_player_hud_settings_from_cfg(local, 0, out));
    EXPECT_EQ(1, out.radar) << "any nonzero int reads as ON";
    EXPECT_EQ(1, out.life_on) << "garbage falls back to the default";
    EXPECT_EQ(0, out.foes);
    EXPECT_EQ(5, out.zoom_step) << "zoom clamps to the top cycle step";

    local.apply_setting("controls", "player1_view_zoom", "-3");
    ASSERT_TRUE(load_player_hud_settings_from_cfg(local, 0, out));
    EXPECT_EQ(0, out.zoom_step);

    // Out-of-range players are rejected outright.
    PlayerHudSettings ignored;
    EXPECT_FALSE(load_player_hud_settings_from_cfg(local, -1, ignored));
    EXPECT_FALSE(load_player_hud_settings_from_cfg(local, 4, ignored));
    save_player_hud_settings_to_cfg(local, 4, ignored);  // must not write
    EXPECT_EQ("", local.get_setting("controls", "player5_hud_radar"));
}

TEST(PlayerHudSettings, save_normalizes_values_to_ints)
{
    cfg_store local;
    PlayerHudSettings in;
    in.radar = 7;      // any nonzero writes "1"
    in.life_on = -2;
    in.zoom_step = 42; // clamps to 5
    save_player_hud_settings_to_cfg(local, 0, in);
    EXPECT_EQ("1", local.get_setting("controls", "player1_hud_radar"));
    EXPECT_EQ("1", local.get_setting("controls", "player1_hud_life"));
    EXPECT_EQ("5", local.get_setting("controls", "player1_view_zoom"));
}

TEST(PerViewZoomMath, step_scale_num_and_cycle_bounds)
{
    EXPECT_EQ(6, viewscreen::kViewZoomStepCount);
    EXPECT_EQ(10, viewscreen::view_scale_num_for_step(0)); // GAME
    EXPECT_EQ(9, viewscreen::view_scale_num_for_step(1));  // 0.9x
    EXPECT_EQ(7, viewscreen::view_scale_num_for_step(3));  // 0.7x
    EXPECT_EQ(5, viewscreen::view_scale_num_for_step(5));  // 0.5x
    // Out-of-range steps clamp instead of extrapolating.
    EXPECT_EQ(10, viewscreen::view_scale_num_for_step(-1));
    EXPECT_EQ(5, viewscreen::view_scale_num_for_step(9));
    // The scale-num range mirrors the core composition clamp.
    EXPECT_EQ(og::kViewScaleNumMax, viewscreen::view_scale_num_for_step(0));
    EXPECT_EQ(og::kViewScaleNumMin,
              viewscreen::view_scale_num_for_step(
                  viewscreen::kViewZoomStepCount - 1));
}

TEST(PerViewZoomMath, composed_pct_delegation_is_identical_to_steps)
{
    // The steps math is the pct math at pct = steps*10 — the mechanism that
    // makes zoom OFF for every view byte-identical by construction.
    for (int w : {320, 640, 1000, 1366, 1920, 2560})
        for (int h : {200, 400, 768, 1080, 1440})
            for (int steps = 1; steps <= og::kZoomStepsMax; ++steps)
            {
                const og::WorldCanvasDims a =
                    og::compute_zoom_canvas_dims(w, h, steps);
                const og::WorldCanvasDims b =
                    og::compute_zoom_canvas_dims_pct(w, h, steps * 10);
                EXPECT_EQ(a.w, b.w) << w << "x" << h << " steps " << steps;
                EXPECT_EQ(a.h, b.h) << w << "x" << h << " steps " << steps;
                EXPECT_EQ(0, b.w % 4) << "scaler-safe width";
            }
}

TEST(PerViewZoomMath, composed_pct_canvas_and_clamps)
{
    // compose_zoom_pct: global steps x per-view scale num, both clamped.
    EXPECT_EQ(100, og::compose_zoom_pct(10, 10));
    EXPECT_EQ(50, og::compose_zoom_pct(10, 5));  // global 1.0 x view 0.5
    EXPECT_EQ(45, og::compose_zoom_pct(5, 9));   // global 0.5 x view 0.9
    EXPECT_EQ(5, og::compose_zoom_pct(1, 5));    // the deepest composition
    EXPECT_EQ(100, og::compose_zoom_pct(99, 99)) << "both factors clamp";
    EXPECT_EQ(og::kViewScaleNumMax, og::clamp_view_scale_num(42));
    EXPECT_EQ(og::kViewScaleNumMin, og::clamp_view_scale_num(-1));

    // A composed pct grows the canvas exactly like the equivalent global
    // zoom: global 1.0 + view 0.5 == global 0.5 alone (crispness parity by
    // construction).
    const og::WorldCanvasDims composed =
        og::compute_zoom_canvas_dims_pct(640, 400, og::compose_zoom_pct(10, 5));
    const og::WorldCanvasDims global_half =
        og::compute_zoom_canvas_dims(640, 400, 5);
    EXPECT_EQ(global_half.w, composed.w);
    EXPECT_EQ(global_half.h, composed.h);
}

TEST(PerViewZoomMath, composed_pct_budget_gate)
{
    using og::zoom_canvas_fits_budget_pct;
    // pct == 100 is the always-selectable baseline.
    EXPECT_TRUE(zoom_canvas_fits_budget_pct(640, 400, 100));
    // Global 1.0 x view 0.5 on a desktop window: 1280x800 well under 8.38M.
    EXPECT_TRUE(zoom_canvas_fits_budget_pct(640, 400, 50));
    // Deep global zoom composed with a deep view zoom overflows the split
    // canvas budget: 0.1 x 0.8 on a 640x400 window = 4000x2500 = 10M > 8.38M.
    EXPECT_FALSE(zoom_canvas_fits_budget_pct(640, 400, 8));
    // ... while 0.1 x 0.9 (3552x2220 ~= 7.9M) still fits.
    EXPECT_TRUE(zoom_canvas_fits_budget_pct(640, 400, 9));
    // A renderer texture cap gates independently of the pixel budget: the
    // composed 640x400 canvas needs both axes under the cap.
    EXPECT_FALSE(zoom_canvas_fits_budget_pct(640, 400, 50, 512));
    EXPECT_TRUE(zoom_canvas_fits_budget_pct(640, 400, 50, 1024));
    // The steps wrapper is the pct gate at pct = steps*10.
    EXPECT_EQ(og::zoom_canvas_fits_budget(640, 400, 5),
              zoom_canvas_fits_budget_pct(640, 400, 50));
}

TEST(BindingPanelLine, formatter_clamps_to_the_column_budget)
{
    using og::ui::format_binding_panel_line;
    using og::ui::kBindingPanelActionsChars;
    using og::ui::kBindingPanelMovementChars;

    // Within budget: verbatim "{label}: {value}".
    EXPECT_EQ("UP: W",
              format_binding_panel_line("UP", "W",
                                        kBindingPanelMovementChars));
    EXPECT_EQ("FIRE: LC",
              format_binding_panel_line("FIRE", "LC",
                                        kBindingPanelActionsChars));

    // The widest keyboard value (get_key_display_name_short caps at 9
    // chars) truncates to EXACTLY the column budget — movement lines end at
    // x = 20 + 14*6 = 104 (the ACTIONS column), actions at
    // x = 104 + 17*6 = 206 <= 208 (the panel edge).
    const std::string wide(9, 'W');
    const std::string movement =
        format_binding_panel_line("DN-R", wide, kBindingPanelMovementChars);
    EXPECT_EQ(kBindingPanelMovementChars,
              static_cast<int>(movement.size()));
    EXPECT_EQ("DN-R: WWWWWWWW", movement);
    const std::string action =
        format_binding_panel_line("SHIFTER", wide, kBindingPanelActionsChars);
    EXPECT_EQ(kBindingPanelActionsChars, static_cast<int>(action.size()));
    EXPECT_EQ("SHIFTER: WWWWWWWW", action);
    // Every real movement label stays inside the budget even with the
    // widest value.
    for (const char* label :
         {"UP", "UP-R", "RIGHT", "DN-R", "DOWN", "DN-L", "LEFT", "UP-L"})
    {
        EXPECT_LE(static_cast<int>(
                      format_binding_panel_line(
                          label, wide, kBindingPanelMovementChars)
                          .size()),
                  kBindingPanelMovementChars)
            << label;
    }
    // The widest joystick forms ("A10+" style) never need truncation on an
    // action row.
    EXPECT_EQ("SPEC SW: A10+",
              format_binding_panel_line("SPEC SW", "A10+",
                                        kBindingPanelActionsChars));
    // A non-positive budget disables the clamp instead of erasing the line.
    EXPECT_EQ("UP: W", format_binding_panel_line("UP", "W", 0));
}
