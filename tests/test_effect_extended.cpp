#include <cstdint>
#include <openglad/gameplay/effect.h>
#include <openglad/resources/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// hits() is a free function
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);

// ---------------------------------------------------------------------------
// hits() AABB collision tests
// ---------------------------------------------------------------------------

TEST(EffectExtended, hits_overlap)
{
    short result = hits(10, 10, 20, 20, 15, 15, 20, 20);
    ASSERT_EQ(1, (int)result) << "overlapping boxes should hit";
}


TEST(EffectExtended, hits_no_overlap_right)
{
    short result = hits(10, 10, 20, 20, 50, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "boxes far apart should not hit";
}


TEST(EffectExtended, hits_no_overlap_below)
{
    short result = hits(10, 10, 20, 20, 10, 50, 20, 20);
    ASSERT_EQ(0, (int)result) << "box below should not hit";
}


TEST(EffectExtended, hits_no_overlap_left)
{
    short result = hits(50, 10, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "box to left should not hit";
}


TEST(EffectExtended, hits_no_overlap_above)
{
    short result = hits(10, 50, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(0, (int)result) << "box above should not hit";
}


TEST(EffectExtended, hits_exact_touching)
{
    // Edge-touching: x1 right edge = x2 left edge
    short result = hits(10, 10, 20, 20, 30, 10, 20, 20);
    ASSERT_EQ(1, (int)result) << "touching edges should hit";
}


TEST(EffectExtended, hits_contained)
{
    short result = hits(10, 10, 40, 40, 15, 15, 10, 10);
    ASSERT_EQ(1, (int)result) << "contained box should hit";
}


TEST(EffectExtended, hits_same_box)
{
    short result = hits(10, 10, 20, 20, 10, 10, 20, 20);
    ASSERT_EQ(1, (int)result) << "same box should hit";
}


TEST(EffectExtended, hits_zero_size)
{
    short result = hits(10, 10, 0, 0, 10, 10, 0, 0);
    ASSERT_EQ(1, (int)result) << "zero-size at same point should hit";
}


// ---------------------------------------------------------------------------
// orbit_offset extended tests
// ---------------------------------------------------------------------------

TEST(EffectExtended, orbit_offset_all_directions)
{
    for (int i = 0; i < 16; i++) {
        float xd = 0, yd = 0;
        orbit_offset(i, xd, yd);
        // Each position should have some offset
        ASSERT_TRUE(xd != 0 || yd != 0) << "orbit offset should be non-zero for all positions";
    }
}


TEST(EffectExtended, orbit_offset_symmetry)
{
    // North and South should have opposite y values
    float xn = 0, yn = 0, xs = 0, ys = 0;
    orbit_offset(0, xn, yn);
    orbit_offset(8, xs, ys);
    ASSERT_TRUE(yn < 0) << "north should have negative y";
    ASSERT_TRUE(ys > 0) << "south should have positive y";
    ASSERT_EQ(0, static_cast<int>(xn)) << "north x should be 0";
    ASSERT_EQ(0, static_cast<int>(xs)) << "south x should be 0";
}


// ---------------------------------------------------------------------------
// compute_explosion_range extended tests
// ---------------------------------------------------------------------------

TEST(EffectExtended, compute_explosion_range_scaling)
{
    std::int32_t r1 = compute_explosion_range(1, 0);
    std::int32_t r5 = compute_explosion_range(5, 0);
    std::int32_t r10 = compute_explosion_range(10, 0);
    ASSERT_TRUE(r5 > r1) << "range should increase with level";
    ASSERT_TRUE(r10 > r5) << "range should increase with level";
}


TEST(EffectExtended, compute_explosion_range_clamp)
{
    std::int32_t r50 = compute_explosion_range(50, 0);
    std::int32_t r100 = compute_explosion_range(100, 0);
    ASSERT_EQ(96, (int)r50) << "level 50 should cap at 96";
    ASSERT_EQ(96, (int)r100) << "level 100 should cap at 96";
}


TEST(EffectExtended, effect_ctor_defaults_and_owner_pointer_cleanup_in_act)
{
    effect headless;
    ASSERT_EQ(1, (int)headless.ignore()) << "headless effect ctor should set ignore";

    effect fx;
    fx.set_ani_type(ANI_WALK);
    fx.set_order_family(Order::FX, 120);

    walker foe;
    walker leader;
    walker owner;
    fx.set_foe(&foe);
    fx.set_leader(&leader);
    fx.set_owner(&owner);
    foe.set_dead(1);
    leader.set_dead(1);
    owner.set_dead(1);

    (void)fx.act();
    ASSERT_TRUE(fx.foe() == nullptr) << "effect act should clear dead foe pointer";
    ASSERT_TRUE(fx.leader() == nullptr) << "effect act should clear dead leader pointer";
    ASSERT_TRUE(fx.owner() == nullptr) << "effect act should clear dead owner pointer";
}
