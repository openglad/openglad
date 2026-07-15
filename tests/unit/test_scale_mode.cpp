// Pins for the cfg graphics/zoom + graphics/smoothing parsing and the
// world-canvas dimension math (og::parse_zoom_steps /
// og::compute_zoom_canvas_dims / og::parse_smoothing_setting in
// openglad/core/scale_mode.h).

#include <gtest/gtest.h>

#include <openglad/core/scale_mode.h>

#include <array>
#include <cstdlib>

using og::WorldScaleMode;
using og::compute_gameplay_ui_canvas_dims;
using og::compute_zoom_canvas_dims;
using og::kZoomStepsMax;
using og::parse_smoothing_setting;
using og::parse_zoom_steps;

TEST(ScaleMode, zoom_parse_accepted_values)
{
    struct
    {
        const char* value;
        int steps;
    } cases[] = {
        {"1.0", 10}, {"1", 10},  {"1.", 10},  {"0.9", 9}, {"0.5", 5},
        {"0.1", 1},  {".5", 5},  {"0.50", 5}, {"0.3", 3},
    };
    for (const auto& c : cases)
        EXPECT_EQ(c.steps, parse_zoom_steps(c.value)) << c.value;
}

TEST(ScaleMode, zoom_parse_quantizes_to_the_grid)
{
    // Off-grid values round to the nearest 0.1 step, so a hand-edited cfg
    // and the menu cycler always agree on the canvas.
    EXPECT_EQ(5, parse_zoom_steps("0.49"));
    EXPECT_EQ(5, parse_zoom_steps("0.451"));
    EXPECT_EQ(4, parse_zoom_steps("0.44"));
    EXPECT_EQ(3, parse_zoom_steps("0.25"));
    EXPECT_EQ(1, parse_zoom_steps("0.14"));
}

TEST(ScaleMode, zoom_parse_clamps_out_of_range)
{
    // Below the deepest zoom clamps to 0.1; above classic clamps to 1.0.
    EXPECT_EQ(1, parse_zoom_steps("0.04"));
    EXPECT_EQ(1, parse_zoom_steps("0"));
    EXPECT_EQ(kZoomStepsMax, parse_zoom_steps("2"));
    EXPECT_EQ(kZoomStepsMax, parse_zoom_steps("11"));
    EXPECT_EQ(kZoomStepsMax, parse_zoom_steps("100.0"));
    EXPECT_EQ(kZoomStepsMax,
              parse_zoom_steps("999999999999999999999999999999999999999"));
}

TEST(ScaleMode, zoom_parse_garbage_reads_classic)
{
    // Missing key (empty string) and anything non-numeric must read as the
    // classic 1.0: pre-existing cfgs behave exactly as today.
    const char* classic_values[] = {"",    "off", "normal", "sai", "2x",
                                    "-0.5", "1e9", "0..5",   "0.5x"};
    for (const char* v : classic_values)
        EXPECT_EQ(kZoomStepsMax, parse_zoom_steps(v)) << "'" << v << "'";
}

TEST(ScaleMode, zoom_one_matches_masters_window_sized_scale_one)
{
	const auto hd = compute_zoom_canvas_dims(1920, 1080, kZoomStepsMax);
	EXPECT_EQ(1920, hd.w);
	EXPECT_EQ(1080, hd.h);
	const auto minimum = compute_zoom_canvas_dims(200, 100, kZoomStepsMax);
	EXPECT_EQ(320, minimum.w);
	EXPECT_EQ(200, minimum.h);
	const auto odd = compute_zoom_canvas_dims(1365, 767, kZoomStepsMax);
	EXPECT_EQ(1364, odd.w);
	EXPECT_EQ(767, odd.h);
}

TEST(ScaleMode, zoom_dims_divide_the_window_canvas)
{
	// canvas = window / zoom: 0.5 doubles the visible world per axis.
	const auto half = compute_zoom_canvas_dims(640, 400, 5);
	EXPECT_EQ(1280, half.w);
	EXPECT_EQ(800, half.h);
	const auto deep = compute_zoom_canvas_dims(640, 400, 1);
	EXPECT_EQ(6400, deep.w);
	EXPECT_EQ(4000, deep.h);
	const auto eight = compute_zoom_canvas_dims(640, 400, 8);
	EXPECT_EQ(800, eight.w);
	EXPECT_EQ(500, eight.h);
}

TEST(ScaleMode, gameplay_ui_keeps_classic_density_and_matches_world_aspect)
{
	const auto classic = compute_gameplay_ui_canvas_dims(640, 400);
	EXPECT_EQ(320, classic.w);
	EXPECT_EQ(200, classic.h);

	const auto widescreen = compute_gameplay_ui_canvas_dims(1920, 1080);
	EXPECT_EQ(356, widescreen.w);
	EXPECT_EQ(200, widescreen.h);

	const auto four_three = compute_gameplay_ui_canvas_dims(1024, 768);
	EXPECT_EQ(320, four_three.w);
	EXPECT_EQ(240, four_three.h);

	const auto invalid = compute_gameplay_ui_canvas_dims(0, -1);
	EXPECT_EQ(320, invalid.w);
	EXPECT_EQ(200, invalid.h);
}

TEST(ScaleMode, zoom_dims_width_rounds_down_to_multiple_of_four)
{
	// 640*10/3 rounds down to 2133, then 2132 (the software scalers and
	// partial-present path require multiple-of-4 widths).
	for (int steps = 1; steps <= kZoomStepsMax; ++steps)
	{
		const auto d = compute_zoom_canvas_dims(640, 400, steps);
        EXPECT_EQ(0, d.w % 4) << "steps=" << steps;
        EXPECT_GE(d.w, 320) << "steps=" << steps;
        EXPECT_GE(d.h, 200) << "steps=" << steps;
    }
	EXPECT_EQ(2132, compute_zoom_canvas_dims(640, 400, 3).w);
	EXPECT_EQ(1333, compute_zoom_canvas_dims(640, 400, 3).h);
}

TEST(ScaleMode, zoom_dims_clamp_hostile_steps)
{
	const auto low = compute_zoom_canvas_dims(640, 400, 0);
	EXPECT_EQ(6400, low.w);
	const auto neg = compute_zoom_canvas_dims(640, 400, -3);
	EXPECT_EQ(6400, neg.w);
	const auto high = compute_zoom_canvas_dims(640, 400, 99);
	EXPECT_EQ(640, high.w);
	EXPECT_EQ(400, high.h);
}

TEST(ScaleMode, minimum_zoom_step_respects_the_canvas_budget)
{
	EXPECT_EQ(2, og::minimum_zoom_steps_for_window(640, 400));
	EXPECT_EQ(5, og::minimum_zoom_steps_for_window(1920, 1080));
	EXPECT_EQ(10, og::minimum_zoom_steps_for_window(3840, 2160));
	EXPECT_TRUE(og::zoom_canvas_fits_budget(3840, 2160, 10));
	EXPECT_FALSE(og::zoom_canvas_fits_budget(3840, 2160, 9));
	EXPECT_TRUE(og::zoom_canvas_fits_budget(7680, 4320, 10, 4096));
	EXPECT_EQ(10, og::minimum_zoom_steps_for_window(7680, 4320, 4096));
	const auto capped = og::constrain_world_canvas_dims(
		compute_zoom_canvas_dims(7680, 4320, 10), 16384);
	EXPECT_LE(static_cast<std::int64_t>(capped.w) * capped.h,
	          og::kWorldCanvasAbsolutePixelBudget);
	EXPECT_EQ(0, capped.w % 4);
	EXPECT_LT(std::abs(capped.w * 4320LL - capped.h * 7680LL), 7680);
	EXPECT_EQ(8, og::minimum_zoom_steps_for_window(1920, 1080, 2560));
	EXPECT_FALSE(og::zoom_canvas_fits_budget(1920, 1080, 7, 2560));
}

TEST(ScaleMode, modal_backdrop_center_crop_preserves_target_aspect)
{
	const auto wide = og::crop_canvas_to_aspect(1280, 720, 320, 200);
	EXPECT_EQ(64, wide.x);
	EXPECT_EQ(0, wide.y);
	EXPECT_EQ(1152, wide.w);
	EXPECT_EQ(720, wide.h);

	const auto tall = og::crop_canvas_to_aspect(800, 600, 320, 200);
	EXPECT_EQ(0, tall.x);
	EXPECT_EQ(50, tall.y);
	EXPECT_EQ(800, tall.w);
	EXPECT_EQ(500, tall.h);
}

TEST(ScaleMode, presentation_aspect_fits_without_stretching)
{
	const auto widescreen = og::fit_canvas_in_viewport(
		320, 200, 0, 0, 1920, 1080);
	EXPECT_EQ(96, widescreen.x);
	EXPECT_EQ(0, widescreen.y);
	EXPECT_EQ(1728, widescreen.w);
	EXPECT_EQ(1080, widescreen.h);

	const auto four_three = og::fit_canvas_in_viewport(
		320, 200, 0, 0, 800, 600);
	EXPECT_EQ(0, four_three.x);
	EXPECT_EQ(50, four_three.y);
	EXPECT_EQ(800, four_three.w);
	EXPECT_EQ(500, four_three.h);

	const auto matching = og::fit_canvas_in_viewport(
		1920, 1080, 10, 20, 1920, 1080);
	EXPECT_EQ(10, matching.x);
	EXPECT_EQ(20, matching.y);
	EXPECT_EQ(1920, matching.w);
	EXPECT_EQ(1080, matching.h);

	for (const auto [canvas_w, canvas_h, viewport_w, viewport_h] :
	     {std::array{321, 201, 1365, 767},
	      std::array{1920, 1080, 1001, 777},
	      std::array{320, 200, 853, 480}})
	{
		const auto fitted = og::fit_canvas_in_viewport(
			canvas_w, canvas_h, 7, 11, viewport_w, viewport_h);
		EXPECT_LT(std::abs(fitted.w * canvas_h - fitted.h * canvas_w),
		          std::max(canvas_w, canvas_h));
		EXPECT_LE(std::abs(2 * (fitted.x - 7) + fitted.w - viewport_w), 1);
		EXPECT_LE(std::abs(2 * (fitted.y - 11) + fitted.h - viewport_h), 1);
	}
}

TEST(ScaleMode, smoothing_parse)
{
    EXPECT_EQ(WorldScaleMode::Sai, parse_smoothing_setting("sai"));
    EXPECT_EQ(WorldScaleMode::Eagle, parse_smoothing_setting("eagle"));
    // "off", the empty string an absent key reads back as, and anything
    // unrecognized all mean the plain nearest stretch.
    EXPECT_EQ(WorldScaleMode::Integer, parse_smoothing_setting("off"));
    EXPECT_EQ(WorldScaleMode::Integer, parse_smoothing_setting(""));
    EXPECT_EQ(WorldScaleMode::Integer, parse_smoothing_setting("SAI"));
    EXPECT_EQ(WorldScaleMode::Integer, parse_smoothing_setting("trilinear"));
}
