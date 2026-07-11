// Pins for the cfg graphics/scale parsing and world-canvas dimension math
// (og::parse_world_scale_setting / og::compute_world_canvas_dims in
// openglad/platform/scale_mode.h).

#include <gtest/gtest.h>

#include <openglad/platform/scale_mode.h>

using og::WorldScaleMode;
using og::WorldScaleSetting;
using og::compute_world_canvas_dims;
using og::parse_world_scale_setting;

TEST(ScaleMode, parse_accepted_values)
{
    struct
    {
        const char* value;
        WorldScaleMode mode;
        int factor;
    } cases[] = {
        {"1", WorldScaleMode::Integer, 1}, {"2", WorldScaleMode::Integer, 2},
        {"3", WorldScaleMode::Integer, 3}, {"4", WorldScaleMode::Integer, 4},
        {"8", WorldScaleMode::Integer, 8}, {"sai", WorldScaleMode::Sai, 2},
        {"eagle", WorldScaleMode::Eagle, 2},
    };
    for (const auto& c : cases)
    {
        const WorldScaleSetting s = parse_world_scale_setting(c.value);
        EXPECT_EQ(c.mode, s.mode) << c.value;
        EXPECT_EQ(c.factor, s.factor) << c.value;
    }
}

TEST(ScaleMode, parse_everything_else_is_legacy)
{
    // Missing key (empty string), the documented explicit "off", and any
    // unrecognized value — including integers outside {1,2,3,4,8} — must be
    // Legacy: pre-existing cfgs behave exactly as today.
    const char* legacy_values[] = {"", "off", "normal", "double", "0",
                                   "5",  "16", "2x",     "SAI",    "-1"};
    for (const char* v : legacy_values)
        EXPECT_EQ(WorldScaleMode::Legacy, parse_world_scale_setting(v).mode)
            << "'" << v << "'";
}

TEST(ScaleMode, legacy_dims_are_always_classic)
{
    const WorldScaleSetting legacy{}; // default = Legacy
    const auto d = compute_world_canvas_dims(1920, 1080, legacy);
    EXPECT_EQ(320, d.w);
    EXPECT_EQ(200, d.h);
    const auto d2 = compute_world_canvas_dims(0, 0, legacy);
    EXPECT_EQ(320, d2.w);
    EXPECT_EQ(200, d2.h);
}

TEST(ScaleMode, integer_dims_divide_the_window)
{
    // The classic default window at scale 2 lands exactly on the classic
    // canvas (the shared-surface byte-identity dims).
    const auto d = compute_world_canvas_dims(640, 400, {WorldScaleMode::Integer, 2});
    EXPECT_EQ(320, d.w);
    EXPECT_EQ(200, d.h);

    const auto d1 = compute_world_canvas_dims(640, 400, {WorldScaleMode::Integer, 1});
    EXPECT_EQ(640, d1.w);
    EXPECT_EQ(400, d1.h);

    const auto d3 = compute_world_canvas_dims(1920, 1080, {WorldScaleMode::Integer, 3});
    EXPECT_EQ(640, d3.w);
    EXPECT_EQ(360, d3.h);
}

TEST(ScaleMode, dims_clamp_to_classic_minimum)
{
    // window/8 of a 640x400 window would be 80x50 — clamped up to 320x200
    // (the smallest geometry the sprite clipper / radar / HUD support).
    const auto d = compute_world_canvas_dims(640, 400, {WorldScaleMode::Integer, 8});
    EXPECT_EQ(320, d.w);
    EXPECT_EQ(200, d.h);
    // Per-axis clamp: a wide-but-short window clamps only the height.
    const auto d2 = compute_world_canvas_dims(1600, 300, {WorldScaleMode::Integer, 2});
    EXPECT_EQ(800, d2.w);
    EXPECT_EQ(200, d2.h);
}

TEST(ScaleMode, width_rounds_down_to_multiple_of_four)
{
    // 1000/3 = 333 -> 332 (the software 2x scalers and the legacy
    // partial-present path require multiple-of-4 widths).
    const auto d = compute_world_canvas_dims(1000, 750, {WorldScaleMode::Integer, 3});
    EXPECT_EQ(332, d.w);
    EXPECT_EQ(250, d.h);
    EXPECT_EQ(0, d.w % 4);
}

TEST(ScaleMode, sai_and_eagle_halve_the_window)
{
    const auto d = compute_world_canvas_dims(1280, 800, {WorldScaleMode::Sai, 2});
    EXPECT_EQ(640, d.w);
    EXPECT_EQ(400, d.h);
    const auto e = compute_world_canvas_dims(640, 400, {WorldScaleMode::Eagle, 2});
    EXPECT_EQ(320, e.w);
    EXPECT_EQ(200, e.h);
}

TEST(ScaleMode, nonpositive_factor_is_defensively_clamped)
{
    const auto d = compute_world_canvas_dims(640, 400, {WorldScaleMode::Integer, 0});
    EXPECT_EQ(640, d.w);
    EXPECT_EQ(400, d.h);
}
