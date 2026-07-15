// Pins for the cfg graphics/zoom + graphics/smoothing parsing and the
// world-canvas dimension math (og::parse_zoom_steps /
// og::compute_zoom_canvas_dims / og::parse_smoothing_setting in
// openglad/core/scale_mode.h).

#include <gtest/gtest.h>

#include <openglad/core/scale_mode.h>

using og::WorldScaleMode;
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

TEST(ScaleMode, classic_zoom_dims_are_the_byte_identity_pair)
{
    const auto d = compute_zoom_canvas_dims(kZoomStepsMax);
    EXPECT_EQ(320, d.w);
    EXPECT_EQ(200, d.h);
}

TEST(ScaleMode, zoom_dims_divide_the_classic_canvas)
{
    // canvas = classic / zoom: 0.5 doubles the visible world per axis.
    const auto half = compute_zoom_canvas_dims(5);
    EXPECT_EQ(640, half.w);
    EXPECT_EQ(400, half.h);
    const auto deep = compute_zoom_canvas_dims(1);
    EXPECT_EQ(3200, deep.w);
    EXPECT_EQ(2000, deep.h);
    const auto eight = compute_zoom_canvas_dims(8);
    EXPECT_EQ(400, eight.w);
    EXPECT_EQ(250, eight.h);
}

TEST(ScaleMode, zoom_dims_width_rounds_up_to_multiple_of_four)
{
    // 320*10/3 = 1066 -> 1068 (the software 2x scalers and the legacy
    // partial-present path require multiple-of-4 widths).
    for (int steps = 1; steps <= kZoomStepsMax; ++steps)
    {
        const auto d = compute_zoom_canvas_dims(steps);
        EXPECT_EQ(0, d.w % 4) << "steps=" << steps;
        EXPECT_GE(d.w, 320) << "steps=" << steps;
        EXPECT_GE(d.h, 200) << "steps=" << steps;
    }
    EXPECT_EQ(1068, compute_zoom_canvas_dims(3).w);
    EXPECT_EQ(666, compute_zoom_canvas_dims(3).h);
}

TEST(ScaleMode, zoom_dims_clamp_hostile_steps)
{
    const auto low = compute_zoom_canvas_dims(0);
    EXPECT_EQ(3200, low.w);
    const auto neg = compute_zoom_canvas_dims(-3);
    EXPECT_EQ(3200, neg.w);
    const auto high = compute_zoom_canvas_dims(99);
    EXPECT_EQ(320, high.w);
    EXPECT_EQ(200, high.h);
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
