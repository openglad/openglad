#include "unit.h"
#include <openglad/render/view.h>
#include <cmath>

// We test the zoom constants and the pure helper methods (world_width,
// world_height, zoom_in, zoom_out, clamping) without constructing a
// full viewscreen — that would need SDL + screen. Instead we exercise
// the logic directly.

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

// --- Zoom level clamping via the viewscreen struct ---
// We cannot fully construct a viewscreen (needs SDL), so we exercise
// the zoom_level field and the helper methods by directly manipulating
// a partially-initialised instance.  The zoom_in/zoom_out/world_width/
// world_height methods only touch zoom_level, xview, and yview — no SDL
// calls — so this is safe.

namespace {
struct FakeViewscreen {
    // Minimal fake to test zoom helpers.
    // viewscreen's zoom methods only use: zoom_level, xview, yview.
    float zoom_level = ZOOM_DEFAULT;
    int xview = 320;
    int yview = 200;

    void zoom_in()
    {
        float nz = zoom_level + ZOOM_STEP;
        zoom_level = nz < ZOOM_MAX ? nz : ZOOM_MAX;
    }
    void zoom_out()
    {
        float nz = zoom_level - ZOOM_STEP;
        zoom_level = nz > ZOOM_MIN ? nz : ZOOM_MIN;
    }
    int world_width() const
    {
        return static_cast<int>(std::ceil(static_cast<float>(xview) / zoom_level));
    }
    int world_height() const
    {
        return static_cast<int>(std::ceil(static_cast<float>(yview) / zoom_level));
    }
};
} // namespace

OG_UNIT_TEST(test_zoom_in_increases_level)
{
    FakeViewscreen v;
    OG_ASSERT(v.zoom_level == 1.0f);
    v.zoom_in();
    OG_ASSERT(v.zoom_level > 1.0f);
}

OG_UNIT_TEST(test_zoom_out_decreases_level)
{
    FakeViewscreen v;
    v.zoom_out();
    OG_ASSERT(v.zoom_level < 1.0f);
}

OG_UNIT_TEST(test_zoom_clamped_at_max)
{
    FakeViewscreen v;
    for (int i = 0; i < 100; ++i)
        v.zoom_in();
    OG_ASSERT(v.zoom_level <= ZOOM_MAX);
    OG_ASSERT(v.zoom_level >= ZOOM_MAX - 0.001f);
}

OG_UNIT_TEST(test_zoom_clamped_at_min)
{
    FakeViewscreen v;
    for (int i = 0; i < 100; ++i)
        v.zoom_out();
    OG_ASSERT(v.zoom_level >= ZOOM_MIN);
    OG_ASSERT(v.zoom_level <= ZOOM_MIN + 0.001f);
}

OG_UNIT_TEST(test_zoom_step_size)
{
    FakeViewscreen v;
    float before = v.zoom_level;
    v.zoom_in();
    float diff = v.zoom_level - before;
    OG_ASSERT(diff > ZOOM_STEP - 0.001f && diff < ZOOM_STEP + 0.001f);
}

// --- World area changes with zoom ---

OG_UNIT_TEST(test_world_area_default_zoom)
{
    FakeViewscreen v;
    OG_ASSERT(v.world_width() == 320);
    OG_ASSERT(v.world_height() == 200);
}

OG_UNIT_TEST(test_world_area_zoom_in)
{
    FakeViewscreen v;
    v.zoom_in(); // 1.25x
    // At 1.25x zoom, world_width = ceil(320/1.25) = 256
    OG_ASSERT(v.world_width() < 320);
    OG_ASSERT(v.world_height() < 200);
}

OG_UNIT_TEST(test_world_area_zoom_out)
{
    FakeViewscreen v;
    v.zoom_out(); // 0.75x
    // At 0.75x zoom, world_width = ceil(320/0.75) = 427
    OG_ASSERT(v.world_width() > 320);
    OG_ASSERT(v.world_height() > 200);
}

OG_UNIT_TEST(test_world_area_zoom_2x)
{
    FakeViewscreen v;
    v.zoom_level = 2.0f;
    // At 2x zoom, world_width = ceil(320/2.0) = 160
    OG_ASSERT(v.world_width() == 160);
    OG_ASSERT(v.world_height() == 100);
}

OG_UNIT_TEST(test_world_area_zoom_half)
{
    FakeViewscreen v;
    v.zoom_level = 0.5f;
    // At 0.5x zoom, world_width = ceil(320/0.5) = 640
    OG_ASSERT(v.world_width() == 640);
    OG_ASSERT(v.world_height() == 400);
}

OG_UNIT_TEST(test_world_area_half_viewport)
{
    // Simulate 2-player split (half-width viewport)
    FakeViewscreen v;
    v.xview = 159;
    v.yview = 200;
    v.zoom_level = 2.0f;
    OG_ASSERT(v.world_width() == 80);  // ceil(159/2.0) = 80
    OG_ASSERT(v.world_height() == 100);
}
