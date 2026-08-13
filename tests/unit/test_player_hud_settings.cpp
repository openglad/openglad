// §7.1 pure helpers: the SDL-free per-player HUD/zoom cfg persistence
// (input_state.cpp), the per-view zoom budget math and step scale (view.h),
// and the shared binding-panel line formatter (pause_menu.cpp). Everything
// here runs against a LOCAL cfg_store — never the process-global cfg.

#include <gtest/gtest.h>

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

TEST(PerViewZoomMath, step_scale_and_cycle_bounds)
{
    EXPECT_EQ(6, viewscreen::kViewZoomStepCount);
    EXPECT_FLOAT_EQ(1.0f, viewscreen::per_view_zoom_scale_for_step(0));
    EXPECT_FLOAT_EQ(0.9f, viewscreen::per_view_zoom_scale_for_step(1));
    EXPECT_FLOAT_EQ(0.7f, viewscreen::per_view_zoom_scale_for_step(3));
    EXPECT_FLOAT_EQ(0.5f, viewscreen::per_view_zoom_scale_for_step(5));
    // Out-of-range steps clamp instead of extrapolating.
    EXPECT_FLOAT_EQ(1.0f, viewscreen::per_view_zoom_scale_for_step(-1));
    EXPECT_FLOAT_EQ(0.5f, viewscreen::per_view_zoom_scale_for_step(9));
}

TEST(PerViewZoomMath, padded_window_budget)
{
    using og::view_zoom_window_fits_budget;
    // Scale >= 1 never pads: always fits.
    EXPECT_TRUE(view_zoom_window_fits_budget(0, 0, 320, 200, 320, 200, 1.0f));
    // The classic canvas at 0.5x: 640x400 = 256k, far under 4.096M.
    EXPECT_TRUE(view_zoom_window_fits_budget(0, 0, 320, 200, 320, 200, 0.5f));
    // 0.5x doubles the window per axis (quadruples the pixels): the exact
    // boundary is xview*yview == budget/4.
    EXPECT_TRUE(
        view_zoom_window_fits_budget(0, 0, 1280, 800, 1280, 800, 0.5f));
    EXPECT_FALSE(
        view_zoom_window_fits_budget(0, 0, 1282, 800, 1282, 800, 0.5f));
    // A large low-global-zoom canvas at 0.5x blows the budget.
    EXPECT_FALSE(
        view_zoom_window_fits_budget(0, 0, 3200, 2000, 3200, 2000, 0.5f));
    // The layer allocates max(canvas, extent) per axis: a small pane on a
    // huge canvas is still bounded below by the canvas.
    EXPECT_FALSE(
        view_zoom_window_fits_budget(0, 0, 64, 40, 3200, 2000, 0.5f));
    // Split-screen: the bottom-right quarter pane's extent includes its
    // origin offset.
    EXPECT_TRUE(
        view_zoom_window_fits_budget(160, 100, 160, 100, 320, 200, 0.5f));
    // Degenerate scales are refused (fail closed).
    EXPECT_FALSE(view_zoom_window_fits_budget(0, 0, 320, 200, 320, 200, 0.0f));
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
