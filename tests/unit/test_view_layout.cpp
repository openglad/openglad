// Pins for og::view_layout::compute_view_layout — the pure world-canvas
// viewscreen layout math that replaced the hardcoded 320x200 tables in
// viewscreen::resize(whatmode) and the screen viewscreen constructions.
//
// The 320x200 cases below are the LEGACY TABLE transcribed verbatim from the
// pre-refactor switch statement: at the default canvas the formulas must
// reproduce it exactly (defaults are byte-identical). The 640x400 cases pin
// the doubled-canvas geometry.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <openglad/interface/render/view_layout.h>

using og::view_layout::compute_view_layout;
using og::view_layout::project_view_layout;
using og::view_layout::ViewLayout;

namespace
{

struct LegacyEntry
{
    int numviews;
    int mynum;
    int mode;
    int x, y, w, h;
};

// The full legacy viewscreen::resize(whatmode) table at 320x200
// (modes: 0 FULL, 1 PANELS, 2 VIEW_1, 3 VIEW_2, 4 VIEW_3).
const LegacyEntry kLegacyTable[] = {
    // 1 player
    {1, 0, 0, 0, 0, 320, 200},
    {1, 0, 1, 44, 12, 232, 176},
    {1, 0, 2, 64, 28, 192, 144},
    {1, 0, 3, 86, 44, 148, 112},
    {1, 0, 4, 106, 60, 108, 80},
    // 2 players, left pane
    {2, 0, 0, 0, 0, 159, 200},
    {2, 0, 1, 4, 16, 152, 168},
    {2, 0, 2, 4, 32, 152, 136},
    {2, 0, 3, 4, 48, 152, 104},
    {2, 0, 4, 4, 64, 152, 72},
    // 2 players, right pane
    {2, 1, 0, 161, 0, 159, 200},
    {2, 1, 1, 164, 16, 152, 168},
    {2, 1, 2, 164, 32, 152, 136},
    {2, 1, 3, 164, 48, 152, 104},
    {2, 1, 4, 164, 64, 152, 72},
    // 3 players: full-height left pane, split right half; inset modes are
    // three columns at x = 4 / 216 / 112 for mynum 0 / 1 / 2.
    {3, 0, 0, 0, 0, 159, 200},
    {3, 0, 1, 4, 16, 100, 168},
    {3, 0, 2, 4, 32, 100, 136},
    {3, 0, 3, 4, 48, 100, 104},
    {3, 0, 4, 4, 64, 100, 72},
    {3, 1, 0, 161, 0, 159, 99},
    {3, 1, 1, 216, 16, 100, 168},
    {3, 1, 2, 216, 32, 100, 136},
    {3, 1, 3, 216, 48, 100, 104},
    {3, 1, 4, 216, 64, 100, 72},
    {3, 2, 0, 161, 101, 159, 99},
    {3, 2, 1, 112, 16, 100, 168},
    {3, 2, 2, 112, 32, 100, 136},
    {3, 2, 3, 112, 48, 100, 104},
    {3, 2, 4, 112, 64, 100, 72},
    // 4 players: quadrants for EVERY mode.
    {4, 0, 0, 0, 0, 159, 99},
    {4, 1, 0, 161, 0, 159, 99},
    {4, 2, 0, 0, 101, 159, 99},
    {4, 3, 0, 161, 101, 159, 99},
    {4, 0, 1, 0, 0, 159, 99},
    {4, 1, 2, 161, 0, 159, 99},
    {4, 2, 3, 0, 101, 159, 99},
    {4, 3, 4, 161, 101, 159, 99},
};

} // namespace

TEST(ViewLayout, classic_canvas_reproduces_legacy_table_verbatim)
{
    for (const LegacyEntry& e : kLegacyTable)
    {
        const ViewLayout r =
            compute_view_layout(e.numviews, e.mynum, e.mode, 320, 200);
        ASSERT_TRUE(r.applies)
            << "numviews=" << e.numviews << " mynum=" << e.mynum
            << " mode=" << e.mode;
        EXPECT_EQ(e.x, r.x) << "numviews=" << e.numviews << " mynum=" << e.mynum
                            << " mode=" << e.mode;
        EXPECT_EQ(e.y, r.y) << "numviews=" << e.numviews << " mynum=" << e.mynum
                            << " mode=" << e.mode;
        EXPECT_EQ(e.w, r.w) << "numviews=" << e.numviews << " mynum=" << e.mynum
                            << " mode=" << e.mode;
        EXPECT_EQ(e.h, r.h) << "numviews=" << e.numviews << " mynum=" << e.mynum
                            << " mode=" << e.mode;
    }
}

TEST(ViewLayout, legacy_no_entry_arms_do_not_apply)
{
    // 2p only had switch arms for mynum 0/1; 3p for mynum 0/1/2. The legacy
    // code silently kept the previous geometry — applies must be false.
    EXPECT_FALSE(compute_view_layout(2, 2, 0, 320, 200).applies);
    EXPECT_FALSE(compute_view_layout(2, -1, 1, 320, 200).applies);
    EXPECT_FALSE(compute_view_layout(3, 3, 0, 320, 200).applies);
    EXPECT_FALSE(compute_view_layout(3, -1, 4, 320, 200).applies);
}

TEST(ViewLayout, legacy_default_arms)
{
    // Out-of-range mode falls back to FULL (the inner `default:` arms).
    const ViewLayout full = compute_view_layout(1, 0, 0, 320, 200);
    const ViewLayout junk_mode = compute_view_layout(1, 0, 99, 320, 200);
    EXPECT_EQ(full.x, junk_mode.x);
    EXPECT_EQ(full.w, junk_mode.w);
    const ViewLayout neg_mode = compute_view_layout(2, 1, -3, 320, 200);
    EXPECT_EQ(161, neg_mode.x);
    EXPECT_EQ(159, neg_mode.w);

    // numviews outside 1..4 lands on the quadrant layout (the outer
    // `case 4: default:`), and mynum outside 0..3 on the fourth quadrant.
    const ViewLayout weird_count = compute_view_layout(7, 0, 0, 320, 200);
    ASSERT_TRUE(weird_count.applies);
    EXPECT_EQ(0, weird_count.x);
    EXPECT_EQ(159, weird_count.w);
    EXPECT_EQ(99, weird_count.h);
    const ViewLayout weird_num = compute_view_layout(4, 9, 0, 320, 200);
    ASSERT_TRUE(weird_num.applies);
    EXPECT_EQ(161, weird_num.x);
    EXPECT_EQ(101, weird_num.y);
}

TEST(ViewLayout, doubled_canvas_scales_panes_not_chrome)
{
    // 1p FULL covers the whole canvas.
    const ViewLayout full1 = compute_view_layout(1, 0, 0, 640, 400);
    EXPECT_EQ(0, full1.x);
    EXPECT_EQ(0, full1.y);
    EXPECT_EQ(640, full1.w);
    EXPECT_EQ(400, full1.h);

    // 1p PANELS keeps the FIXED 44/12 px chrome insets (the score-panel HUD
    // blocks do not grow with the canvas) — only the pane grows.
    const ViewLayout panels1 = compute_view_layout(1, 0, 1, 640, 400);
    EXPECT_EQ(44, panels1.x);
    EXPECT_EQ(12, panels1.y);
    EXPECT_EQ(640 - 88, panels1.w);
    EXPECT_EQ(400 - 24, panels1.h);

    // 2p side-by-side: half width minus the 2px seam, full height.
    const ViewLayout left = compute_view_layout(2, 0, 0, 640, 400);
    const ViewLayout right = compute_view_layout(2, 1, 0, 640, 400);
    EXPECT_EQ(0, left.x);
    EXPECT_EQ(319, left.w);
    EXPECT_EQ(400, left.h);
    EXPECT_EQ(321, right.x);
    EXPECT_EQ(319, right.w);
    EXPECT_EQ(400, right.h);

    // 2p PANELS: fixed 4/3 px horizontal margins inside each half pane,
    // fixed 16 px vertical inset.
    const ViewLayout p2 = compute_view_layout(2, 1, 1, 640, 400);
    EXPECT_EQ(324, p2.x);
    EXPECT_EQ(16, p2.y);
    EXPECT_EQ(319 - 7, p2.w);
    EXPECT_EQ(400 - 32, p2.h);

    // 3p FULL: full-height left pane + split right half.
    const ViewLayout t0 = compute_view_layout(3, 0, 0, 640, 400);
    const ViewLayout t1 = compute_view_layout(3, 1, 0, 640, 400);
    const ViewLayout t2 = compute_view_layout(3, 2, 0, 640, 400);
    EXPECT_EQ(0, t0.x);
    EXPECT_EQ(319, t0.w);
    EXPECT_EQ(400, t0.h);
    EXPECT_EQ(321, t1.x);
    EXPECT_EQ(0, t1.y);
    EXPECT_EQ(199, t1.h);
    EXPECT_EQ(321, t2.x);
    EXPECT_EQ(201, t2.y);
    EXPECT_EQ(199, t2.h);

    // 3p columns: width (640-20)/3 = 206 at x = 4 / 428 / 218.
    const ViewLayout c0 = compute_view_layout(3, 0, 1, 640, 400);
    const ViewLayout c1 = compute_view_layout(3, 1, 1, 640, 400);
    const ViewLayout c2 = compute_view_layout(3, 2, 1, 640, 400);
    EXPECT_EQ(4, c0.x);
    EXPECT_EQ(428, c1.x);
    EXPECT_EQ(218, c2.x);
    EXPECT_EQ(206, c0.w);
    EXPECT_EQ(206, c1.w);
    EXPECT_EQ(206, c2.w);

    // 4p quadrants.
    const ViewLayout q0 = compute_view_layout(4, 0, 0, 640, 400);
    const ViewLayout q3 = compute_view_layout(4, 3, 0, 640, 400);
    EXPECT_EQ(0, q0.x);
    EXPECT_EQ(0, q0.y);
    EXPECT_EQ(319, q0.w);
    EXPECT_EQ(199, q0.h);
    EXPECT_EQ(321, q3.x);
    EXPECT_EQ(201, q3.y);
    EXPECT_EQ(319, q3.w);
    EXPECT_EQ(199, q3.h);
}

TEST(ViewLayoutProjection, projects_non_integer_ratios_by_rectangle_edges)
{
    const ViewLayout projected =
        project_view_layout({true, 44, 12, 232, 176}, 320, 200, 853, 533);

    ASSERT_TRUE(projected.applies);
    EXPECT_EQ(117, projected.x); // floor(44 * 853 / 320)
    EXPECT_EQ(31, projected.y);  // floor(12 * 533 / 200)
    EXPECT_EQ(618, projected.w); // floor(276 * 853 / 320) - projected.x
    EXPECT_EQ(470, projected.h); // floor(188 * 533 / 200) - projected.y
}

TEST(ViewLayoutProjection, covers_every_player_count_and_view_mode)
{
    constexpr int baseline_w = 320;
    constexpr int baseline_h = 200;
    constexpr int world_w = 853;
    constexpr int world_h = 533;
    const auto projected_edge = [](int edge, int baseline_extent,
                                   int world_extent) {
        return static_cast<int>(static_cast<std::int64_t>(edge) * world_extent /
                                baseline_extent);
    };

    for (int numviews = 1; numviews <= 4; ++numviews)
    {
        for (int mode = 0; mode <= 4; ++mode)
        {
            for (int mynum = 0; mynum < numviews; ++mynum)
            {
                const ViewLayout baseline = compute_view_layout(
                    numviews, mynum, mode, baseline_w, baseline_h);
                ASSERT_TRUE(baseline.applies)
                    << "numviews=" << numviews << " mynum=" << mynum
                    << " mode=" << mode;

                const ViewLayout projected = project_view_layout(
                    baseline, baseline_w, baseline_h, world_w, world_h);
                ASSERT_TRUE(projected.applies)
                    << "numviews=" << numviews << " mynum=" << mynum
                    << " mode=" << mode;

                const int expected_left =
                    projected_edge(baseline.x, baseline_w, world_w);
                const int expected_top =
                    projected_edge(baseline.y, baseline_h, world_h);
                const int expected_right = projected_edge(
                    baseline.x + baseline.w, baseline_w, world_w);
                const int expected_bottom = projected_edge(
                    baseline.y + baseline.h, baseline_h, world_h);
                EXPECT_EQ(expected_left, projected.x);
                EXPECT_EQ(expected_top, projected.y);
                EXPECT_EQ(expected_right - expected_left, projected.w);
                EXPECT_EQ(expected_bottom - expected_top, projected.h);
                EXPECT_GE(projected.x, 0);
                EXPECT_GE(projected.y, 0);
                EXPECT_LE(projected.x + projected.w, world_w);
                EXPECT_LE(projected.y + projected.h, world_h);
            }
        }
    }
}

TEST(ViewLayoutProjection, shared_edges_remain_exactly_aligned)
{
    // Both pairs meet at source edge (101, 77). The target dimensions make
    // each scale ratio non-integral, exercising the integer-rounding seam.
    const ViewLayout top_left =
        project_view_layout({true, 0, 0, 101, 77}, 320, 200, 853, 533);
    const ViewLayout top_right =
        project_view_layout({true, 101, 0, 219, 77}, 320, 200, 853, 533);
    const ViewLayout bottom_left =
        project_view_layout({true, 0, 77, 101, 123}, 320, 200, 853, 533);

    ASSERT_TRUE(top_left.applies);
    ASSERT_TRUE(top_right.applies);
    ASSERT_TRUE(bottom_left.applies);
    EXPECT_EQ(top_left.x + top_left.w, top_right.x);
    EXPECT_EQ(top_left.y + top_left.h, bottom_left.y);
    EXPECT_EQ(853, top_right.x + top_right.w);
    EXPECT_EQ(533, bottom_left.y + bottom_left.h);
}

TEST(ViewLayoutProjection, equal_canvas_dimensions_are_an_identity)
{
    for (int numviews = 1; numviews <= 4; ++numviews)
    {
        for (int mode = 0; mode <= 4; ++mode)
        {
            for (int mynum = 0; mynum < numviews; ++mynum)
            {
                const ViewLayout baseline =
                    compute_view_layout(numviews, mynum, mode, 320, 200);
                const ViewLayout projected =
                    project_view_layout(baseline, 320, 200, 320, 200);
                ASSERT_TRUE(projected.applies);
                EXPECT_EQ(baseline.x, projected.x);
                EXPECT_EQ(baseline.y, projected.y);
                EXPECT_EQ(baseline.w, projected.w);
                EXPECT_EQ(baseline.h, projected.h);
            }
        }
    }
}

TEST(ViewLayoutProjection, rejects_non_projectable_inputs)
{
    EXPECT_FALSE(project_view_layout({}, 320, 200, 640, 400).applies);
    EXPECT_FALSE(
        project_view_layout({true, 0, 0, 320, 200}, 0, 200, 640, 400)
            .applies);
    EXPECT_FALSE(
        project_view_layout({true, 0, 0, 320, 200}, 320, 200, -1, 400)
            .applies);
    EXPECT_FALSE(
        project_view_layout({true, -1, 0, 1, 1}, 320, 200, 640, 400)
            .applies);
    EXPECT_FALSE(
        project_view_layout({true, 319, 0, 2, 1}, 320, 200, 640, 400)
            .applies);
}

TEST(ViewLayoutProjection, widened_edge_arithmetic_avoids_int_overflow)
{
    constexpr int max = std::numeric_limits<int>::max();
    const ViewLayout projected =
        project_view_layout({true, max - 1, max - 1, 1, 1}, max, max,
                            max - 1, max - 1);

    ASSERT_TRUE(projected.applies);
    EXPECT_EQ(max - 2, projected.x);
    EXPECT_EQ(max - 2, projected.y);
    EXPECT_EQ(1, projected.w);
    EXPECT_EQ(1, projected.h);
}
