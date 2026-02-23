#include "unit.h"
#include <openglad/render/view.h>
#include <cmath>

// Tests exercise the production zoom math functions directly:
//   zoom_clamp, zoom_apply_in, zoom_apply_out, zoom_world_dim
// These are the same functions used by viewscreen::zoom_in/zoom_out
// and viewscreen::world_width/world_height.

// --- Zoom constants ---

OG_UNIT_TEST(test_zoom_default_is_one)
{
    OG_ASSERT(ZOOM_DEFAULT == 1.0f);
}

OG_UNIT_TEST(test_zoom_min_less_than_max)
{
    OG_ASSERT(ZOOM_MIN < ZOOM_MAX);
}

OG_UNIT_TEST(test_zoom_step_positive)
{
    OG_ASSERT(ZOOM_STEP > 0.0f);
}

OG_UNIT_TEST(test_zoom_default_in_range)
{
    OG_ASSERT(ZOOM_DEFAULT >= ZOOM_MIN);
    OG_ASSERT(ZOOM_DEFAULT <= ZOOM_MAX);
}

// --- zoom_clamp ---

OG_UNIT_TEST(test_zoom_clamp_in_range)
{
    OG_ASSERT(zoom_clamp(1.5f) == 1.5f);
}

OG_UNIT_TEST(test_zoom_clamp_below_min)
{
    OG_ASSERT(zoom_clamp(0.1f) == ZOOM_MIN);
}

OG_UNIT_TEST(test_zoom_clamp_above_max)
{
    OG_ASSERT(zoom_clamp(10.0f) == ZOOM_MAX);
}

// --- zoom_apply_in / zoom_apply_out ---

OG_UNIT_TEST(test_zoom_in_increases_level)
{
    float level = zoom_apply_in(1.0f);
    OG_ASSERT(level > 1.0f);
}

OG_UNIT_TEST(test_zoom_out_decreases_level)
{
    float level = zoom_apply_out(1.0f);
    OG_ASSERT(level < 1.0f);
}

OG_UNIT_TEST(test_zoom_in_clamped_at_max)
{
    float level = ZOOM_DEFAULT;
    for (int i = 0; i < 100; ++i)
        level = zoom_apply_in(level);
    OG_ASSERT(level <= ZOOM_MAX);
    OG_ASSERT(level >= ZOOM_MAX - 0.001f);
}

OG_UNIT_TEST(test_zoom_out_clamped_at_min)
{
    float level = ZOOM_DEFAULT;
    for (int i = 0; i < 100; ++i)
        level = zoom_apply_out(level);
    OG_ASSERT(level >= ZOOM_MIN);
    OG_ASSERT(level <= ZOOM_MIN + 0.001f);
}

OG_UNIT_TEST(test_zoom_step_size)
{
    float before = 1.0f;
    float after = zoom_apply_in(before);
    float diff = after - before;
    OG_ASSERT(diff > ZOOM_STEP - 0.001f && diff < ZOOM_STEP + 0.001f);
}

// --- zoom_world_dim ---

OG_UNIT_TEST(test_world_dim_default_zoom)
{
    OG_ASSERT(zoom_world_dim(320, 1.0f) == 320);
    OG_ASSERT(zoom_world_dim(200, 1.0f) == 200);
}

OG_UNIT_TEST(test_world_dim_zoom_in)
{
    // At 1.25x zoom, world dim = ceil(320/1.25) = 256
    OG_ASSERT(zoom_world_dim(320, 1.25f) == 256);
    OG_ASSERT(zoom_world_dim(200, 1.25f) == 160);
}

OG_UNIT_TEST(test_world_dim_zoom_out)
{
    // At 0.75x zoom, world dim = ceil(320/0.75) = 427
    int w = zoom_world_dim(320, 0.75f);
    OG_ASSERT(w > 320);
    int h = zoom_world_dim(200, 0.75f);
    OG_ASSERT(h > 200);
}

OG_UNIT_TEST(test_world_dim_zoom_2x)
{
    OG_ASSERT(zoom_world_dim(320, 2.0f) == 160);
    OG_ASSERT(zoom_world_dim(200, 2.0f) == 100);
}

OG_UNIT_TEST(test_world_dim_zoom_half)
{
    OG_ASSERT(zoom_world_dim(320, 0.5f) == 640);
    OG_ASSERT(zoom_world_dim(200, 0.5f) == 400);
}

OG_UNIT_TEST(test_world_dim_half_viewport)
{
    // Simulate 2-player split (half-width viewport)
    OG_ASSERT(zoom_world_dim(159, 2.0f) == 80);  // ceil(159/2.0) = 80
    OG_ASSERT(zoom_world_dim(200, 2.0f) == 100);
}
